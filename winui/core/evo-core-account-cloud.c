/*
 * evo-core : Comptes Microsoft 365 / Exchange (EWS) et Google (Gmail, agenda, contacts).
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Crée un compte Microsoft 365 / Exchange Online (Microsoft Graph,
 * evolution-ews) comme l'assistant d'Evolution : une collection (backend
 * "microsoft365", authentification OAuth2 "Microsoft365") portant le compte
 * de courrier, l'identité et le serveur d'envoi ; le module du registre
 * ajoute lui-même les carnets d'adresses et agendas de la collection.
 * L'interface enchaîne avec evo_core_oauth2_begin () sur "collection_uid".
 * Avec "backend": "ews", compte Exchange de l'entreprise (EWS sur
 * "hosturl", authentification NTLM, mot de passe enregistré).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON {"email", "display_name", "full_name",
 *           "client_id"?, "tenant"?, "backend"?, "hosturl"?, "user"?,
 *           "password"?}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{\"uid\":\"<compte>\",\"collection_uid\":\"...\"}", NULL sinon. */
static gchar *
core_add_m365_account_run (EvoCore *core,
			   gchar **args,
			   GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *email, *name, *real_name, *client_id, *tenant;
	const gchar *backend, *hosturl, *user, *password, *shared;
	ESource *collection_source, *account_source, *identity_source, *transport_source;
	ESourceCollection *collection;
	ESourceAuthentication *authentication;
	ESourceMailAccount *mail_account;
	ESourceMailIdentity *mail_identity;
	ESourceMailSubmission *mail_submission;
	ESourceMailTransport *mail_transport;
	GList *sources = NULL;
	CamelService *service;
	JsonBuilder *builder;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	email = object ? json_object_get_string_member_with_default (object, "email", "") : "";
	if (!email || !*email) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "L'adresse e-mail est obligatoire");
		json_node_unref (root);
		return NULL;
	}

	backend = g_strcmp0 (json_object_get_string_member_with_default (object, "backend", ""), "ews") == 0
		? "ews" : "microsoft365";
	hosturl = json_object_get_string_member_with_default (object, "hosturl", "");
	user = json_object_get_string_member_with_default (object, "user", "");
	if (!*user)
		user = email;
	password = json_object_get_string_member_with_default (object, "password", "");

	if (!camel_provider_get (backend, NULL)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Le module Microsoft 365 / Exchange (evolution-ews) n'est pas installé");
		json_node_unref (root);
		return NULL;
	}

	if (g_str_equal (backend, "ews") && !*hosturl) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "L'adresse du serveur EWS est obligatoire");
		json_node_unref (root);
		return NULL;
	}

	name = json_object_get_string_member_with_default (object, "display_name", "");
	if (!*name)
		name = email;
	real_name = json_object_get_string_member_with_default (object, "full_name", "");
	if (!*real_name)
		real_name = name;
	client_id = json_object_get_string_member_with_default (object, "client_id", "");
	tenant = json_object_get_string_member_with_default (object, "tenant", "");
	/* boîte partagée : adresse de la boîte ouverte avec les identifiants de "email" */
	shared = g_str_equal (backend, "microsoft365")
		? json_object_get_string_member_with_default (object, "impersonate_user", "") : "";

	collection_source = e_source_new (NULL, NULL, NULL);
	account_source = e_source_new (NULL, NULL, NULL);
	identity_source = e_source_new (NULL, NULL, NULL);
	transport_source = e_source_new (NULL, NULL, NULL);

	e_source_set_display_name (collection_source, name);
	e_source_set_display_name (account_source, name);
	e_source_set_display_name (identity_source, name);
	e_source_set_display_name (transport_source, name);
	e_source_set_parent (account_source, e_source_get_uid (collection_source));
	e_source_set_parent (identity_source, e_source_get_uid (collection_source));
	e_source_set_parent (transport_source, e_source_get_uid (collection_source));

	/* collection: OAuth2 authentication, mail + calendars + contacts */
	collection = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (collection), backend);
	e_source_collection_set_identity (collection, email);
	e_source_collection_set_mail_enabled (collection, TRUE);
	e_source_collection_set_calendar_enabled (collection, !*shared);
	e_source_collection_set_contacts_enabled (collection, !*shared);

	authentication = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_AUTHENTICATION);
	if (g_str_equal (backend, "ews")) {
		GUri *guri = g_uri_parse (hosturl, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);

		e_source_authentication_set_method (authentication, "NTLM");
		if (guri) {
			e_source_authentication_set_host (authentication, g_uri_get_host (guri));
			g_uri_unref (guri);
		}
	} else {
		e_source_authentication_set_method (authentication, "Microsoft365");
	}
	e_source_authentication_set_user (authentication, user);
	core_set_m365_settings (collection_source, backend, email, user, hosturl, client_id, tenant);
	if (*shared)
		core_set_m365_impersonation (collection_source, shared);

	mail_account = e_source_get_extension (account_source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (mail_account), backend);
	e_source_mail_account_set_identity_uid (mail_account, e_source_get_uid (identity_source));
	core_set_m365_settings (account_source, backend, email, user, hosturl, client_id, tenant);
	if (*shared)
		core_set_m365_impersonation (account_source, shared);

	mail_identity = e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	e_source_mail_identity_set_name (mail_identity, real_name);
	e_source_mail_identity_set_address (mail_identity, *shared ? shared : email);
	mail_submission = e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
	e_source_mail_submission_set_transport_uid (mail_submission, e_source_get_uid (transport_source));
	/* the server saves sent messages itself */
	e_source_mail_submission_set_use_sent_folder (mail_submission, FALSE);
	e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_COMPOSITION);

	mail_transport = e_source_get_extension (transport_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (mail_transport), backend);
	core_set_m365_settings (transport_source, backend, email, user, hosturl, client_id, tenant);
	if (*shared)
		core_set_m365_impersonation (transport_source, shared);

	/* parents before children */
	sources = g_list_append (sources, collection_source);
	sources = g_list_append (sources, account_source);
	sources = g_list_append (sources, identity_source);
	sources = g_list_append (sources, transport_source);

	if (!e_source_registry_create_sources_sync (core->registry, sources, core->cancellable, error) ||
	    !core_wait_for_sources (core, sources, 30000, error))
		goto exit;

	/* EWS: the password is kept for the collection (credentials source) */
	if (g_str_equal (backend, "ews") && *password &&
	    !core_store_source_password (core, e_source_get_uid (collection_source), password, error))
		goto exit;

	service = camel_session_add_service (core->session, e_source_get_uid (account_source),
		backend, CAMEL_PROVIDER_STORE, NULL);
	g_clear_object (&service);
	service = camel_session_add_service (core->session, e_source_get_uid (transport_source),
		backend, CAMEL_PROVIDER_TRANSPORT, NULL);
	g_clear_object (&service);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", e_source_get_uid (account_source));
	builder_add_string (builder, "collection_uid", e_source_get_uid (collection_source));
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_list_free (sources);
	g_object_unref (collection_source);
	g_object_unref (account_source);
	g_object_unref (identity_source);
	g_object_unref (transport_source);
	json_node_unref (root);

	return json;
}

/* Active l'agenda et les contacts Google d'un compte Gmail déjà configuré
 * sans ces services. Si une collection « google » existe pour l'adresse, ses
 * services agenda et contacts sont activés ; sinon une collection dédiée
 * (sans courrier) est créée avec le même identifiant et l'authentification
 * OAuth2 « Google ». L'interface enchaîne avec evo_core_oauth2_begin () sur
 * "collection_uid" pour accorder les portées CalDAV / CardDAV.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = adresse e-mail du compte Gmail
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"collection_uid", "created", "already_enabled"}, NULL sinon. */
static gchar *
core_enable_google_services_run (EvoCore *core,
				 gchar **args,
				 GError **error)
{
	const gchar *email = args[0];
	ESource *collection_source = NULL;
	ESourceCollection *collection;
	ESourceAuthentication *authentication;
	GList *sources, *link;
	JsonBuilder *builder;
	gboolean created = FALSE, already = FALSE;
	gchar *json;

	if (!email || !*email) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "L'adresse e-mail est obligatoire");
		return NULL;
	}

	sources = e_source_registry_list_sources (core->registry, E_SOURCE_EXTENSION_COLLECTION);
	for (link = sources; link && !collection_source; link = g_list_next (link)) {
		ESource *candidate = link->data;
		ESourceBackend *backend;
		gchar *backend_name, *identity;

		backend = E_SOURCE_BACKEND (e_source_get_extension (candidate, E_SOURCE_EXTENSION_COLLECTION));
		backend_name = e_source_backend_dup_backend_name (backend);
		identity = e_source_collection_dup_identity (E_SOURCE_COLLECTION (backend));
		if (g_strcmp0 (backend_name, "google") == 0 && identity && g_ascii_strcasecmp (identity, email) == 0)
			collection_source = g_object_ref (candidate);
		g_free (backend_name);
		g_free (identity);
	}
	g_list_free_full (sources, g_object_unref);

	if (collection_source) {
		collection = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);
		already = e_source_collection_get_calendar_enabled (collection) &&
			e_source_collection_get_contacts_enabled (collection);
		if (!already) {
			e_source_collection_set_calendar_enabled (collection, TRUE);
			e_source_collection_set_contacts_enabled (collection, TRUE);
			if (!e_source_write_sync (collection_source, core->cancellable, error)) {
				g_object_unref (collection_source);
				return NULL;
			}
		}
	} else {
		GList *new_sources;
		gchar *display_name = g_strdup_printf ("%s (agenda et contacts)", email);

		collection_source = e_source_new (NULL, NULL, NULL);
		e_source_set_display_name (collection_source, display_name);
		g_free (display_name);

		collection = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);
		e_source_backend_set_backend_name (E_SOURCE_BACKEND (collection), "google");
		e_source_collection_set_identity (collection, email);
		e_source_collection_set_mail_enabled (collection, FALSE);
		e_source_collection_set_calendar_enabled (collection, TRUE);
		e_source_collection_set_contacts_enabled (collection, TRUE);

		authentication = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_AUTHENTICATION);
		e_source_authentication_set_method (authentication, "Google");
		e_source_authentication_set_user (authentication, email);
		e_source_authentication_set_host (authentication, "www.google.com");

		new_sources = g_list_append (NULL, collection_source);
		if (!e_source_registry_create_sources_sync (core->registry, new_sources, core->cancellable, error)) {
			g_list_free (new_sources);
			g_object_unref (collection_source);
			return NULL;
		}
		g_list_free (new_sources);
		created = TRUE;
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "collection_uid", e_source_get_uid (collection_source));
	json_builder_set_member_name (builder, "created");
	json_builder_add_boolean_value (builder, created);
	json_builder_set_member_name (builder, "already_enabled");
	json_builder_add_boolean_value (builder, already);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	g_object_unref (collection_source);

	return json;
}

/* Crée un compte Gmail / Google Workspace comme l'assistant d'Evolution :
 * une collection (backend "google", authentification OAuth2 "Google") qui
 * porte le courrier IMAP / SMTP de Gmail, l'identité, et dont le module du
 * registre découvre les agendas (CalDAV) et carnets d'adresses (CardDAV).
 * L'interface enchaîne avec evo_core_oauth2_begin () sur "collection_uid".
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON {"email", "display_name", "full_name"}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{\"uid\":\"<compte>\",\"collection_uid\":\"...\"}", NULL sinon. */
static gchar *
core_add_google_account_run (EvoCore *core,
			     gchar **args,
			     GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *email, *name, *real_name;
	ESource *collection_source, *account_source, *identity_source, *transport_source;
	ESourceCollection *collection;
	ESourceAuthentication *authentication;
	ESourceMailAccount *mail_account;
	ESourceMailIdentity *mail_identity;
	ESourceMailSubmission *mail_submission;
	ESourceMailTransport *mail_transport;
	GList *sources = NULL;
	CamelService *service;
	JsonBuilder *builder;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	email = object ? json_object_get_string_member_with_default (object, "email", "") : "";
	if (!email || !*email) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "L'adresse e-mail est obligatoire");
		json_node_unref (root);
		return NULL;
	}

	name = json_object_get_string_member_with_default (object, "display_name", "");
	if (!*name)
		name = email;
	real_name = json_object_get_string_member_with_default (object, "full_name", "");
	if (!*real_name)
		real_name = name;

	collection_source = e_source_new (NULL, NULL, NULL);
	account_source = e_source_new (NULL, NULL, NULL);
	identity_source = e_source_new (NULL, NULL, NULL);
	transport_source = e_source_new (NULL, NULL, NULL);

	e_source_set_display_name (collection_source, name);
	e_source_set_display_name (account_source, name);
	e_source_set_display_name (identity_source, name);
	e_source_set_display_name (transport_source, name);
	e_source_set_parent (account_source, e_source_get_uid (collection_source));
	e_source_set_parent (identity_source, e_source_get_uid (collection_source));
	e_source_set_parent (transport_source, e_source_get_uid (collection_source));

	/* collection : OAuth2 Google, courrier + agendas + contacts */
	collection = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (collection), "google");
	e_source_collection_set_identity (collection, email);
	e_source_collection_set_mail_enabled (collection, TRUE);
	e_source_collection_set_calendar_enabled (collection, TRUE);
	e_source_collection_set_contacts_enabled (collection, TRUE);

	authentication = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_AUTHENTICATION);
	e_source_authentication_set_method (authentication, "Google");
	e_source_authentication_set_user (authentication, email);
	e_source_authentication_set_host (authentication, "www.google.com");

	mail_account = e_source_get_extension (account_source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (mail_account), "imapx");
	e_source_mail_account_set_identity_uid (mail_account, e_source_get_uid (identity_source));
	core_set_google_mail_settings (account_source, "imapx", "imap.gmail.com", 993, email);

	mail_identity = e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	e_source_mail_identity_set_name (mail_identity, real_name);
	e_source_mail_identity_set_address (mail_identity, email);
	mail_submission = e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
	e_source_mail_submission_set_transport_uid (mail_submission, e_source_get_uid (transport_source));
	/* Gmail range lui-même les messages envoyés */
	e_source_mail_submission_set_use_sent_folder (mail_submission, FALSE);
	e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_COMPOSITION);

	mail_transport = e_source_get_extension (transport_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (mail_transport), "smtp");
	core_set_google_mail_settings (transport_source, "smtp", "smtp.gmail.com", 465, email);

	/* parents before children */
	sources = g_list_append (sources, collection_source);
	sources = g_list_append (sources, account_source);
	sources = g_list_append (sources, identity_source);
	sources = g_list_append (sources, transport_source);

	if (!e_source_registry_create_sources_sync (core->registry, sources, core->cancellable, error) ||
	    !core_wait_for_sources (core, sources, 30000, error))
		goto exit;

	service = camel_session_add_service (core->session, e_source_get_uid (account_source),
		"imapx", CAMEL_PROVIDER_STORE, NULL);
	g_clear_object (&service);
	service = camel_session_add_service (core->session, e_source_get_uid (transport_source),
		"smtp", CAMEL_PROVIDER_TRANSPORT, NULL);
	g_clear_object (&service);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", e_source_get_uid (account_source));
	builder_add_string (builder, "collection_uid", e_source_get_uid (collection_source));
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_list_free (sources);
	g_object_unref (collection_source);
	g_object_unref (account_source);
	g_object_unref (identity_source);
	g_object_unref (transport_source);
	json_node_unref (root);

	return json;
}

/* Source carrying the authentication of uid: its collection when it is
 * part of one (Microsoft 365 / EWS accounts), else itself. */
ESource *
core_ref_credentials_source (EvoCore *core,
			     const gchar *uid,
			     GError **error)
{
	ESource *source, *collection;
	guint attempt;

	/* a source just created may not be published yet */
	source = e_source_registry_ref_source (core->registry, uid);
	for (attempt = 0; !source && attempt < 100 && !g_cancellable_is_cancelled (core->cancellable); attempt++) {
		g_usleep (100 * 1000);
		source = e_source_registry_ref_source (core->registry, uid);
	}
	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Compte introuvable : « %s »", uid);
		return NULL;
	}

	collection = e_source_registry_find_extension (core->registry, source, E_SOURCE_EXTENSION_COLLECTION);
	if (collection) {
		g_object_unref (source);
		return collection;
	}

	return source;
}

void
evo_core_enable_google_services (EvoCore *core,
				 const char *email,
				 EvoCoreResultFunc func,
				 void *user_data)
{
	gchar *args[] = { (gchar *) email, NULL };

	g_return_if_fail (email != NULL);

	core_submit (core, core_enable_google_services_run, g_strdupv (args), func, user_data);
}

void
evo_core_add_google_account (EvoCore *core,
			     const char *account_json,
			     EvoCoreResultFunc func,
			     void *user_data)
{
	gchar *args[] = { (gchar *) account_json, NULL };

	g_return_if_fail (account_json != NULL);

	core_submit (core, core_add_google_account_run, g_strdupv (args), func, user_data);
}

void
evo_core_add_microsoft365_account (EvoCore *core,
				   const char *account_json,
				   EvoCoreResultFunc func,
				   void *user_data)
{
	gchar *args[] = { (gchar *) account_json, NULL };

	g_return_if_fail (account_json != NULL);

	core_submit (core, core_add_m365_account_run, g_strdupv (args), func, user_data);
}
