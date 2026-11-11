/*
 * evo-core : Connexion OAuth2 interactive et demandes d'identifiants du service des
 * comptes (credentials-required).
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* An OAuth2 sign-in between evo_core_oauth2_begin () and
 * evo_core_oauth2_finish (): the same service object must exchange the
 * code (it may keep PKCE data per source) and the state must come back. */
typedef struct {
	EOAuth2Service *service;
	gchar *state;
	gchar *redirect_uri;
} CoreOAuth2Pending;

void
core_oauth2_pending_free (gpointer data)
{
	CoreOAuth2Pending *pending = data;

	g_clear_object (&pending->service);
	g_free (pending->state);
	g_free (pending->redirect_uri);
	g_free (pending);
}

/* Première étape de la connexion OAuth2 d'un compte : adresse de la page de
 * connexion du fournisseur, à ouvrir dans un navigateur intégré, et adresse
 * de redirection, à surveiller (evo_core_oauth2_finish). Même construction
 * que la fenêtre OAuth2 d'EDS (paramètres du service, "state" vérifié au
 * retour).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte ou de sa collection
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"source_uid", "uri", "redirect_uri", "service"}, NULL sinon. */
static gchar *
core_oauth2_begin_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	ESource *source;
	EOAuth2Service *service;
	CoreOAuth2Pending *pending;
	GHashTable *query_form;
	GUri *auth_uri;
	JsonBuilder *builder;
	gchar *query, *uri, *json;

	source = core_ref_credentials_source (core, args[0], error);
	if (!source)
		return NULL;

	service = e_oauth2_services_find (e_source_registry_get_oauth2_services (core->registry), source);
	if (!service) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Aucun service OAuth2 pour « %s »", e_source_get_display_name (source));
		g_object_unref (source);
		return NULL;
	}

	const gchar *custom_redirect = (args[1] && *args[1]) ? args[1] : NULL;
	if (custom_redirect) {
		g_object_set_data_full (G_OBJECT (service), "oauth2-google-redirect-uri",
			g_strdup (custom_redirect), g_free);
		g_object_set_data_full (G_OBJECT (service), "oauth2-redirect-uri",
			g_strdup (custom_redirect), g_free);
	}

	auth_uri = g_uri_parse (e_oauth2_service_get_authentication_uri (service, source),
		SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, error);
	if (!auth_uri) {
		g_object_unref (service);
		g_object_unref (source);
		return NULL;
	}

	query_form = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	e_oauth2_service_prepare_authentication_uri_query (service, source, query_form);

	/* Réponses automatiques Gmail (API users.settings.updateVacation) : portée
	 * absente de la liste d'EDS, ajoutée à la demande de connexion Google */
	if (g_strcmp0 (e_oauth2_service_get_name (service), "Google") == 0) {
		const gchar *scope = g_hash_table_lookup (query_form, "scope");

		if (scope && !strstr (scope, "gmail.settings.basic"))
			g_hash_table_insert (query_form, g_strdup ("scope"),
				g_strconcat (scope, " https://www.googleapis.com/auth/gmail.settings.basic", NULL));

		/* Délégués de la boîte (API Gmail settings.delegates, Google Workspace) */
		scope = g_hash_table_lookup (query_form, "scope");
		if (scope && !strstr (scope, "gmail.settings.sharing"))
			g_hash_table_insert (query_form, g_strdup ("scope"),
				g_strconcat (scope, " https://www.googleapis.com/auth/gmail.settings.sharing", NULL));

		/* Annuaire de l'organisation (API People searchDirectoryPeople) */
		scope = g_hash_table_lookup (query_form, "scope");
		if (scope && !strstr (scope, "directory.readonly"))
			g_hash_table_insert (query_form, g_strdup ("scope"),
				g_strconcat (scope, " https://www.googleapis.com/auth/directory.readonly", NULL));
	}

	/* Impose l'identité du compte dans la page de connexion (login_hint) : sans
	 * cela, le navigateur réutilise la session déjà ouverte (par exemple un compte
	 * administrateur) et le jeton est émis pour un autre utilisateur, ce qui donne
	 * « Default folder Inbox not found » à la première synchronisation. */
	if (!g_hash_table_contains (query_form, "login_hint") &&
	    e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *authentication = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		gchar *hint = e_source_authentication_dup_user (authentication);

		if (hint && strchr (hint, '@'))
			g_hash_table_insert (query_form, g_strdup ("login_hint"), g_strdup (hint));
		g_free (hint);
	}

	if (custom_redirect)
		g_hash_table_insert (query_form, g_strdup ("redirect_uri"), g_strdup (custom_redirect));
	query = soup_form_encode_hash (query_form);
	e_util_change_uri_component (&auth_uri, SOUP_URI_QUERY, query);
	uri = g_uri_to_string_partial (auth_uri, G_URI_HIDE_PASSWORD);

	pending = g_new0 (CoreOAuth2Pending, 1);
	pending->service = g_object_ref (service);
	pending->state = g_strdup (g_hash_table_lookup (query_form, "state"));
	pending->redirect_uri = g_strdup (custom_redirect);

	g_mutex_lock (&core->lock);
	g_hash_table_insert (core->oauth2_pending, g_strdup (e_source_get_uid (source)), pending);
	g_mutex_unlock (&core->lock);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "source_uid", e_source_get_uid (source));
	builder_add_string (builder, "uri", uri);
	builder_add_string (builder, "redirect_uri",
		custom_redirect ? custom_redirect : e_oauth2_service_get_redirect_uri (service, source));
	builder_add_string (builder, "service", e_oauth2_service_get_display_name (service));
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	g_free (uri);
	g_free (query);
	g_hash_table_destroy (query_form);
	g_uri_unref (auth_uri);
	g_object_unref (service);
	g_object_unref (source);

	return json;
}

static ESource *
core_oauth2_ref_source_cb (gpointer user_data,
			   const gchar *uid)
{
	return e_source_registry_ref_source (user_data, uid);
}

/* Value of a query parameter of an URI, NULL when absent. */
static gchar *
uri_dup_query_param (const gchar *uri,
		     const gchar *name)
{
	GUri *guri;
	GHashTable *params;
	gchar *value = NULL;

	guri = g_uri_parse (uri, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);
	if (!guri)
		return NULL;

	if (g_uri_get_query (guri)) {
		/* WWW_FORM: "+" is a space in the query */
		params = g_uri_parse_params (g_uri_get_query (guri), -1, "&", G_URI_PARAMS_WWW_FORM, NULL);
		if (params) {
			value = g_strdup (g_hash_table_lookup (params, name));
			g_hash_table_destroy (params);
		}
	}

	g_uri_unref (guri);

	return value;
}

/* Seconde étape : l'interface transmet l'adresse de redirection atteinte
 * (elle contient le code d'autorisation) ; le code est échangé contre les
 * jetons, que le service enregistre (magasin de secrets d'EDS, donc le
 * Gestionnaire d'identifiants Windows) et que le registre EDS renouvellera.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = source_uid renvoyé par evo_core_oauth2_begin, [1] = adresse
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_oauth2_finish_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	CoreOAuth2Pending *pending;
	ESource *source;
	gchar *code = NULL, *state, *error_message;
	gboolean success;

	g_mutex_lock (&core->lock);
	pending = g_hash_table_lookup (core->oauth2_pending, args[0]);
	if (pending)
		g_hash_table_steal (core->oauth2_pending, args[0]);
	g_mutex_unlock (&core->lock);

	if (!pending) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Aucune connexion OAuth2 en cours");
		return NULL;
	}

	source = core_ref_credentials_source (core, args[0], error);
	if (!source) {
		core_oauth2_pending_free (pending);
		return NULL;
	}

	if (e_oauth2_service_extract_error_message (pending->service, source, NULL, args[1], NULL, &error_message)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "%s",
			error_message ? error_message : "Connexion refusée");
		g_free (error_message);
		success = FALSE;
		goto exit;
	}

	if (!e_oauth2_service_extract_authorization_code (pending->service, source, NULL, args[1], NULL, &code) || !code) {
		/* refusal or failure reported in the redirect (RFC 6749 4.1.2.1) */
		error_message = uri_dup_query_param (args[1], "error_description");
		if (!error_message)
			error_message = uri_dup_query_param (args[1], "error");

		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "%s",
			error_message ? error_message : "L'adresse ne contient pas de code d'autorisation");
		g_free (error_message);
		success = FALSE;
		goto exit;
	}

	/* the answer must belong to this sign-in */
	state = uri_dup_query_param (args[1], "state");
	if (pending->state && g_strcmp0 (state, pending->state) != 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			"La réponse ne correspond pas à cette connexion (state)");
		g_free (state);
		success = FALSE;
		goto exit;
	}
	g_free (state);

	if (pending->redirect_uri) {
		g_object_set_data_full (G_OBJECT (pending->service), "oauth2-google-redirect-uri",
			g_strdup (pending->redirect_uri), g_free);
		g_object_set_data_full (G_OBJECT (pending->service), "oauth2-redirect-uri",
			g_strdup (pending->redirect_uri), g_free);
	}

	success = e_oauth2_service_receive_and_store_token_sync (pending->service, source, code,
		core_oauth2_ref_source_cb, core->registry, core->cancellable, error);

	if (success) {
		EvoCoreSession *session = EVO_CORE_SESSION (core->session);

		/* a later failure may prompt again */
		g_mutex_lock (&session->prompted_lock);
		g_hash_table_remove (session->prompted_sources, e_source_get_uid (source));
		g_mutex_unlock (&session->prompted_lock);

		/* le service des comptes relance l'authentification de la collection
		   (découverte des agendas et carnets d'une collection Google...) */
		{
			ENamedParameters *credentials = e_named_parameters_new ();
			GError *local_error = NULL;

			if (!e_source_invoke_authenticate_sync (source, credentials, core->cancellable, &local_error))
				g_warning ("Cannot authenticate “%s”: %s", e_source_get_display_name (source),
					local_error ? local_error->message : "unknown error");
			g_clear_error (&local_error);
			e_named_parameters_free (credentials);
		}

		evo_core_refresh (core);
	}

 exit:
	e_util_safe_free_string (code);
	g_object_unref (source);
	core_oauth2_pending_free (pending);

	return success ? g_strdup ("{}") : NULL;
}

/* Désigne le compte de messagerie par défaut (nouveaux messages), avec son
 * identité d'expédition comme identité par défaut d'EDS.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_set_default_account_run (EvoCore *core,
			      gchar **args,
			      GError **error)
{
	ESource *account, *identity = NULL;

	account = e_source_registry_ref_source (core->registry, args[0]);
	if (!account || !e_source_has_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Compte introuvable : « %s »", args[0]);
		g_clear_object (&account);
		return NULL;
	}

	e_source_registry_set_default_mail_account (core->registry, account);

	{
		gchar *identity_uid = e_source_mail_account_dup_identity_uid (
			e_source_get_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT));

		if (identity_uid)
			identity = e_source_registry_ref_source (core->registry, identity_uid);
		g_free (identity_uid);
	}
	if (identity)
		e_source_registry_set_default_mail_identity (core->registry, identity);

	g_clear_object (&identity);
	g_object_unref (account);

	return g_strdup ("{}");
}

/* --- demandes d'identification du service des comptes --- */

/* Le service des comptes (evolution-source-registry) et les fabriques
 * demandent des identifiants par le signal "credentials-required" ; dans
 * Evolution, c'est le « credentials prompter » de l'interface GTK qui y
 * répond. Ici, pour une source OAuth2 d'EDS (collection Google...) : si le
 * jeton est disponible, l'authentification est relancée
 * (e_source_invoke_authenticate) ; sinon, ou s'il a été refusé,
 * l'interface est invitée à se reconnecter ("oauth2-required"). */

typedef struct {
	EvoCore *core;
	ESource *source;
	ESourceCredentialsReason reason;
} CoreCredentialsRequest;

static ESource *
core_credentials_ref_source_cb (gpointer user_data,
				const gchar *uid)
{
	return e_source_registry_ref_source (user_data, uid);
}

/* Émet "oauth2-required" pour une source (collection) : connexion à refaire. */
static void
core_emit_oauth2_required_for_source (EvoCore *core,
				      ESource *source)
{
	JsonBuilder *builder = core_event_builder ();
	gchar *user = NULL;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION))
		user = e_source_authentication_dup_user (e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION));

	builder_add_string (builder, "source_uid", e_source_get_uid (source));
	builder_add_string (builder, "name", e_source_get_display_name (source));
	builder_add_string (builder, "user", user ? user : "");
	core_emit_builder (core, "oauth2-required", builder);

	g_free (user);
}

static void
core_credentials_request_thread (GTask *task,
				 gpointer source_object,
				 gpointer task_data,
				 GCancellable *cancellable)
{
	CoreCredentialsRequest *request = task_data;
	EvoCore *core = request->core;
	EOAuth2Service *oauth2;
	gchar *token = NULL;
	gint expires_in = 0;
	GError *local_error = NULL;

	oauth2 = e_oauth2_services_find (e_source_registry_get_oauth2_services (core->registry), request->source);
	if (!oauth2)
		return;

	if (request->reason == E_SOURCE_CREDENTIALS_REASON_REQUIRED &&
	    e_oauth2_service_get_access_token_sync (oauth2, request->source, core_credentials_ref_source_cb,
		core->registry, &token, &expires_in, cancellable, &local_error)) {
		ENamedParameters *credentials = e_named_parameters_new ();

		/* le jeton est relu par la source elle-même : identifiants vides */
		if (!e_source_invoke_authenticate_sync (request->source, credentials, cancellable, &local_error))
			g_warning ("Cannot authenticate “%s”: %s", e_source_get_display_name (request->source),
				local_error ? local_error->message : "unknown error");
		e_named_parameters_free (credentials);
	} else if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		core_emit_oauth2_required_for_source (core, request->source);
	}

	e_util_safe_free_string (token);
	g_clear_error (&local_error);
	g_object_unref (oauth2);
}

static void
core_credentials_request_free (gpointer data)
{
	CoreCredentialsRequest *request = data;

	g_object_unref (request->source);
	g_free (request);
}

/* Au démarrage : demandes d'identification émises avant que le moteur
 * n'écoute (collection créée puis connectée, service déjà lancé). */
static void
core_check_pending_credentials_thread (GTask *task,
				       gpointer source_object,
				       gpointer task_data,
				       GCancellable *cancellable)
{
	EvoCore *core = task_data;
	GList *sources, *link;

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_COLLECTION);

	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;
		ESourceCredentialsReason reason = E_SOURCE_CREDENTIALS_REASON_UNKNOWN;
		gchar *certificate_pem = NULL;
		GTlsCertificateFlags certificate_errors = 0;
		GError *op_error = NULL;

		if (e_source_get_last_credentials_required_arguments_sync (source, &reason, &certificate_pem,
			&certificate_errors, &op_error, cancellable, NULL))
			core_credentials_required_cb (core->registry, source, reason, certificate_pem,
				certificate_errors, op_error, core);

		g_free (certificate_pem);
		g_clear_error (&op_error);
	}

	g_list_free_full (sources, g_object_unref);
}

void
core_check_pending_credentials (EvoCore *core)
{
	GTask *task = g_task_new (NULL, core->cancellable, NULL, NULL);

	g_task_set_task_data (task, core, NULL);
	g_task_run_in_thread (task, core_check_pending_credentials_thread);
	g_object_unref (task);
}

void
core_credentials_required_cb (ESourceRegistry *registry,
			      ESource *source,
			      ESourceCredentialsReason reason,
			      const gchar *certificate_pem,
			      GTlsCertificateFlags certificate_errors,
			      const GError *op_error,
			      gpointer user_data)
{
	EvoCore *core = user_data;
	CoreCredentialsRequest *request;
	GTask *task;
	gchar *method = NULL;

	if (reason != E_SOURCE_CREDENTIALS_REASON_REQUIRED && reason != E_SOURCE_CREDENTIALS_REASON_REJECTED)
		return;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION))
		method = e_source_authentication_dup_method (e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION));

	if (!method || !e_oauth2_services_is_oauth2_alias (e_source_registry_get_oauth2_services (registry), method)) {
		g_free (method);
		return;
	}
	g_free (method);

	g_message ("Credentials required for “%s” (%s)", e_source_get_display_name (source),
		reason == E_SOURCE_CREDENTIALS_REASON_REJECTED ? "rejected" : "required");

	request = g_new0 (CoreCredentialsRequest, 1);
	request->core = core;
	request->source = g_object_ref (source);
	request->reason = reason;

	task = g_task_new (NULL, core->cancellable, NULL, NULL);
	g_task_set_task_data (task, request, core_credentials_request_free);
	g_task_run_in_thread (task, core_credentials_request_thread);
	g_object_unref (task);
}

/* Liste les sources activées d'un ou plusieurs types (agendas, listes de
 * tâches, listes de mémos, carnets d'adresses), triées par nom.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = types séparés par des virgules ("calendar,tasks"), ou "" pour tous
 *   error : emplacement pour une éventuelle erreur
 * Retourne : tableau JSON [{"uid", "name", "kind"}, ...]. */
static gchar *
core_list_sources_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	gchar **kinds = g_strsplit (args[0] ? args[0] : "", ",", -1);
	JsonBuilder *builder;
	gchar *json;
	guint ii;

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (ii = 0; ii < G_N_ELEMENTS (source_kinds); ii++) {
		GList *sources, *slink;

		if (kinds[0] && *kinds[0] && !g_strv_contains ((const gchar * const *) kinds, source_kinds[ii].kind))
			continue;

		ESource *default_source = NULL;

		sources = e_source_registry_list_enabled (core->registry, source_kinds[ii].extension);
		sources = g_list_sort (sources, (GCompareFunc) e_source_compare_by_display_name);

		if (g_str_equal (source_kinds[ii].kind, "calendar"))
			default_source = e_source_registry_ref_default_calendar (core->registry);
		else if (g_str_equal (source_kinds[ii].kind, "tasks"))
			default_source = e_source_registry_ref_default_task_list (core->registry);
		else if (g_str_equal (source_kinds[ii].kind, "memos"))
			default_source = e_source_registry_ref_default_memo_list (core->registry);
		else
			default_source = e_source_registry_ref_default_address_book (core->registry);

		for (slink = sources; slink; slink = g_list_next (slink)) {
			ESource *source = slink->data;
			gpointer extension = e_source_get_extension (source, source_kinds[ii].extension);
			const gchar *parent_uid = e_source_get_parent (source);
			ESource *parent = parent_uid ? e_source_registry_ref_source (core->registry, parent_uid) : NULL;
			gchar *color = E_IS_SOURCE_SELECTABLE (extension)
				? e_source_selectable_dup_color (E_SOURCE_SELECTABLE (extension)) : NULL;

			json_builder_begin_object (builder);
			builder_add_string (builder, "uid", e_source_get_uid (source));
			builder_add_string (builder, "name", e_source_get_display_name (source));
			builder_add_string (builder, "kind", source_kinds[ii].kind);
			gchar *source_acc_email = parent_uid ? core_account_address (core, parent_uid) : NULL;
			if (!source_acc_email)
				source_acc_email = core_account_address (core, e_source_get_uid (source));
			/* compte : collection ou source parente (« Sur cet ordinateur »...) */
			builder_add_string (builder, "account", parent ? e_source_get_display_name (parent) : "");
			builder_add_string (builder, "account_uid", parent_uid ? parent_uid : "");
			builder_add_string (builder, "account_email", source_acc_email ? source_acc_email : "");
			builder_add_string (builder, "color", color);
			g_free (source_acc_email);
			{
				/* protocole de la collection (microsoft365, ews, google...) et nom du
				 * groupe de calendriers Exchange Online (« Équipe de ... ») */
				gchar *collection_backend = NULL, *group_name = NULL;

				if (parent && e_source_has_extension (parent, E_SOURCE_EXTENSION_COLLECTION))
					collection_backend = e_source_backend_dup_backend_name (E_SOURCE_BACKEND (
						e_source_get_extension (parent, E_SOURCE_EXTENSION_COLLECTION)));
				if (e_source_has_extension (source, "Microsoft365 Folder")) {
					GObject *folder_ext = G_OBJECT (e_source_get_extension (source, "Microsoft365 Folder"));

					if (g_object_class_find_property (G_OBJECT_GET_CLASS (folder_ext), "group-name"))
						g_object_get (folder_ext, "group-name", &group_name, NULL);
				}
				builder_add_string (builder, "backend", collection_backend ? collection_backend : "");
				builder_add_string (builder, "group_name", group_name ? group_name : "");
				g_free (collection_backend);
				g_free (group_name);
			}
			json_builder_set_member_name (builder, "default");
			json_builder_add_boolean_value (builder, default_source && e_source_equal (source, default_source));
			json_builder_end_object (builder);

			g_free (color);
			g_clear_object (&parent);
		}

		g_clear_object (&default_source);
		g_list_free_full (sources, g_object_unref);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_strfreev (kinds);

	return json;
}

void
evo_core_set_default_account (EvoCore *core,
			      const char *account_uid,
			      EvoCoreResultFunc func,
			      void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, NULL };

	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_set_default_account_run, g_strdupv (args), func, user_data);
}

void
evo_core_oauth2_begin (EvoCore *core,
		       const char *source_uid,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) source_uid, NULL };

	g_return_if_fail (source_uid != NULL);

	core_submit (core, core_oauth2_begin_run, g_strdupv (args), func, user_data);
}

void
evo_core_oauth2_begin_with_redirect (EvoCore *core,
				     const char *source_uid,
				     const char *redirect_uri,
				     EvoCoreResultFunc func,
				     void *user_data)
{
	gchar *args[] = { (gchar *) source_uid, (gchar *) redirect_uri, NULL };

	g_return_if_fail (source_uid != NULL);

	core_submit (core, core_oauth2_begin_run, g_strdupv (args), func, user_data);
}

void
evo_core_oauth2_finish (EvoCore *core,
			const char *source_uid,
			const char *redirect_uri,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = { (gchar *) source_uid, (gchar *) redirect_uri, NULL };

	g_return_if_fail (source_uid != NULL);
	g_return_if_fail (redirect_uri != NULL);

	core_submit (core, core_oauth2_finish_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de liste des sources activées (agendas, listes de
 * tâches, listes de mémos, carnets d'adresses).
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   kinds     : types séparés par des virgules ("calendar,tasks"), "" ou NULL pour tous
 *   func      : rappel recevant le tableau JSON des sources
 *   user_data : données utilisateur transmises au rappel */
void
evo_core_list_sources (EvoCore *core,
		       const char *kinds,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) (kinds ? kinds : ""), NULL };

	core_submit (core, core_list_sources_run, g_strdupv (args), func, user_data);
}
