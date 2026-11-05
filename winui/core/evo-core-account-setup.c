/*
 * evo-core : Creation, test, lecture et modification des comptes IMAP / POP3 / SMTP
 * et enregistrement des mots de passe.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* --- gestion des comptes et des mots de passe --- */

/* Attend que les sources créées soient connues du registre de evo-core.
 * e_source_registry_create_sources_sync () n'attend que 2 s leur
 * publication par evolution-source-registry, qui peut être plus lent (premier
 * chargement des modules evolution-ews, antivirus, machine lente) : un
 * service Camel ajouté avant n'aurait ni nom ni réglages.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   sources    : sources créées (ESource)
 *   timeout_ms : délai maximal en millisecondes
 *   error      : emplacement pour une éventuelle erreur
 * Retourne : TRUE quand toutes les sources sont connues. */
gboolean
core_wait_for_sources (EvoCore *core,
		       GList *sources,
		       guint timeout_ms,
		       GError **error)
{
	gint64 deadline = g_get_monotonic_time () + (gint64) timeout_ms * 1000;
	gint64 start = g_get_monotonic_time ();

	for (;;) {
		const gchar *missing = NULL;
		GList *link;

		for (link = sources; link && !missing; link = g_list_next (link)) {
			ESource *known = e_source_registry_ref_source (core->registry, e_source_get_uid (link->data));

			if (known)
				g_object_unref (known);
			else
				missing = e_source_get_uid (link->data);
		}

		if (!missing) {
			if (g_get_monotonic_time () - start > 2 * G_USEC_PER_SEC)
				g_message ("New sources published after %" G_GINT64_FORMAT " ms",
					(g_get_monotonic_time () - start) / 1000);
			return TRUE;
		}

		if (g_get_monotonic_time () >= deadline || g_cancellable_is_cancelled (core->cancellable)) {
			g_warning ("Source “%s” still not published by the registry after %u ms", missing, timeout_ms);
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
				"Le service des comptes (evolution-source-registry) n'a pas enregistré le "
				"nouveau compte dans les %u secondes ; réessayer dans un instant", timeout_ms / 1000);
			return FALSE;
		}

		g_usleep (100 * 1000);
	}
}

/* Enregistre le mot de passe d'une source dans le gestionnaire d'identifiants
 * Windows via ESourceCredentialsProvider.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   source_uid : identifiant de la source
 *   password   : mot de passe en clair
 *   error      : emplacement pour une éventuelle erreur
 * Retourne : TRUE en cas de succès. */
gboolean
core_store_source_password (EvoCore *core,
			    const gchar *source_uid,
			    const gchar *password,
			    GError **error)
{
	EvoCoreSession *session = EVO_CORE_SESSION (core->session);
	ESource *source;
	ENamedParameters *credentials;
	CamelService *service;
	gboolean ok;

	source = e_source_registry_ref_source (core->registry, source_uid);
	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"Source introuvable : « %s »", source_uid);
		return FALSE;
	}

	credentials = e_named_parameters_new ();
	e_named_parameters_set (credentials, E_SOURCE_CREDENTIAL_PASSWORD, password ? password : "");

	ok = e_source_credentials_provider_store_sync (
		session->credentials_provider, source, credentials, TRUE,
		core->cancellable, error);

	e_named_parameters_free (credentials);
	g_object_unref (source);

	service = camel_session_ref_service (core->session, source_uid);
	if (service) {
		camel_service_set_password (service, password);
		g_object_unref (service);
	}

	g_mutex_lock (&session->prompted_lock);
	g_hash_table_remove (session->prompted_sources, source_uid);
	g_mutex_unlock (&session->prompted_lock);

	return ok;
}

/* Exécute la sauvegarde du mot de passe d'un compte ou serveur d'envoi.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = source_uid, [1] = password
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_save_password_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	if (!core_store_source_password (core, args[0], args[1], error))
		return NULL;

	return g_strdup ("{}");
}

/* Convertit une chaîne de méthode de chiffrement ("tls", "starttls", "none")
 * en valeur de l'énumération CamelNetworkSecurityMethod.
 * Paramètres :
 *   security : chaîne décrivant le mode de sécurité
 * Retourne : la constante CamelNetworkSecurityMethod correspondante. */
static CamelNetworkSecurityMethod
parse_security_method (const gchar *security)
{
	if (!security || g_ascii_strcasecmp (security, "tls") == 0 ||
	    g_ascii_strcasecmp (security, "ssl") == 0 ||
	    g_ascii_strcasecmp (security, "ssl-on-alternate-port") == 0)
		return CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT;

	if (g_ascii_strcasecmp (security, "starttls") == 0 ||
	    g_ascii_strcasecmp (security, "starttls-on-standard-port") == 0)
		return CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT;

	return CAMEL_NETWORK_SECURITY_METHOD_NONE;
}

/* Crée un compte de messagerie IMAP / SMTP complet (3 sources EDS : compte de
 * réception, identité d'expéditeur et serveur d'envoi), l'enregistre dans le
 * registre EDS, sauvegarde le mot de passe et ajoute les services à la session.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON décrivant le compte
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{\"uid\":\"...\"}" en cas de succès, NULL sinon. */
static gchar *
core_add_account_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *name, *real_name, *address;
	const gchar *imap_host, *imap_user, *imap_security;
	const gchar *smtp_host, *smtp_user, *smtp_security;
	const gchar *password;
	gint64 imap_port, smtp_port;
	ESource *account_source = NULL, *identity_source = NULL, *transport_source = NULL;
	ESourceMailAccount *mail_account;
	ESourceMailIdentity *mail_identity;
	ESourceMailSubmission *mail_submission;
	ESourceMailTransport *mail_transport;
	ESourceCamel *imap_camel, *smtp_camel;
	CamelSettings *imap_settings, *smtp_settings;
	GList *sources = NULL;
	CamelService *store_service = NULL, *transport_service = NULL;
	JsonBuilder *builder;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		json_node_unref (root);
		return NULL;
	}

	address = json_object_get_string_member_with_default (object, "address", "");
	if (!address || !*address)
		address = json_object_get_string_member_with_default (object, "email", "");
	name = json_object_get_string_member_with_default (object, "name", "");
	if (!name || !*name)
		name = json_object_get_string_member_with_default (object, "display_name", "");
	if (!name || !*name)
		name = address;
	real_name = json_object_get_string_member_with_default (object, "real_name", "");
	if (!real_name || !*real_name)
		real_name = json_object_get_string_member_with_default (object, "full_name", "");
	if (!real_name || !*real_name)
		real_name = name;

	imap_host = json_object_get_string_member_with_default (object, "imap_host", "");
	imap_port = json_object_get_int_member_with_default (object, "imap_port", 993);
	imap_user = json_object_get_string_member_with_default (object, "imap_user", "");
	if (!imap_user || !*imap_user)
		imap_user = json_object_get_string_member_with_default (object, "user", "");
	if (!imap_user || !*imap_user)
		imap_user = address;
	imap_security = json_object_get_string_member_with_default (object, "imap_security", "tls");

	smtp_host = json_object_get_string_member_with_default (object, "smtp_host", "");
	smtp_port = json_object_get_int_member_with_default (object, "smtp_port", 465);
	smtp_user = json_object_get_string_member_with_default (object, "smtp_user", "");
	if (!smtp_user || !*smtp_user)
		smtp_user = imap_user;
	smtp_security = json_object_get_string_member_with_default (object, "smtp_security", "tls");

	password = json_object_get_string_member_with_default (object, "password", "");

	if (!address || !*address || !imap_host || !*imap_host || !smtp_host || !*smtp_host) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"L'adresse e-mail et les serveurs IMAP et SMTP sont obligatoires");
		json_node_unref (root);
		return NULL;
	}

	account_source = e_source_new (NULL, NULL, NULL);
	identity_source = e_source_new (NULL, NULL, NULL);
	transport_source = e_source_new (NULL, NULL, NULL);

	e_source_set_display_name (account_source, name);
	e_source_set_display_name (identity_source, name);
	e_source_set_display_name (transport_source, name);
	e_source_set_enabled (account_source, TRUE);
	e_source_set_enabled (identity_source, TRUE);
	e_source_set_enabled (transport_source, TRUE);

	/* Configuration de la source de réception IMAPX */
	mail_account = e_source_get_extension (account_source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (mail_account), "imapx");
	e_source_mail_account_set_identity_uid (mail_account, e_source_get_uid (identity_source));

	imap_camel = e_source_get_extension (account_source, e_source_camel_get_extension_name ("imapx"));
	imap_settings = e_source_camel_get_settings (imap_camel);
	camel_network_settings_set_host (CAMEL_NETWORK_SETTINGS (imap_settings), imap_host);
	camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (imap_settings), (guint16) imap_port);
	camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (imap_settings), imap_user);
	camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (imap_settings),
		parse_security_method (imap_security));
	{
		const gchar *auth_mech = json_object_get_string_member_with_default (object, "auth_mechanism", "");
		if (auth_mech && *auth_mech)
			camel_network_settings_set_auth_mechanism (CAMEL_NETWORK_SETTINGS (imap_settings), auth_mech);
	}

	/* Configuration de l'identité d'expéditeur et de la soumission */
	mail_identity = e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	e_source_mail_identity_set_name (mail_identity, real_name);
	e_source_mail_identity_set_address (mail_identity, address);

	mail_submission = e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
	e_source_mail_submission_set_transport_uid (mail_submission, e_source_get_uid (transport_source));
	e_source_mail_submission_set_use_sent_folder (mail_submission, TRUE);
	e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_COMPOSITION);

	/* Configuration du serveur d'envoi SMTP */
	mail_transport = e_source_get_extension (transport_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (mail_transport), "smtp");

	smtp_camel = e_source_get_extension (transport_source, e_source_camel_get_extension_name ("smtp"));
	smtp_settings = e_source_camel_get_settings (smtp_camel);
	camel_network_settings_set_host (CAMEL_NETWORK_SETTINGS (smtp_settings), smtp_host);
	camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (smtp_settings), (guint16) smtp_port);
	camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (smtp_settings), smtp_user);
	camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (smtp_settings),
		parse_security_method (smtp_security));
	{
		const gchar *auth_mech = json_object_get_string_member_with_default (object, "auth_mechanism", "");
		if (auth_mech && *auth_mech)
			camel_network_settings_set_auth_mechanism (CAMEL_NETWORK_SETTINGS (smtp_settings), auth_mech);
		else if (smtp_user && *smtp_user)
			camel_network_settings_set_auth_mechanism (CAMEL_NETWORK_SETTINGS (smtp_settings), "LOGIN");
	}

	sources = g_list_append (sources, account_source);
	sources = g_list_append (sources, identity_source);
	sources = g_list_append (sources, transport_source);

	if (!e_source_registry_create_sources_sync (core->registry, sources, core->cancellable, error) ||
	    !core_wait_for_sources (core, sources, 30000, error))
		goto exit;

	if (password && *password) {
		core_store_source_password (core, e_source_get_uid (account_source), password, NULL);
		core_store_source_password (core, e_source_get_uid (transport_source), password, NULL);
	}

	store_service = camel_session_add_service (core->session, e_source_get_uid (account_source),
		"imapx", CAMEL_PROVIDER_STORE, NULL);
	transport_service = camel_session_add_service (core->session, e_source_get_uid (transport_source),
		"smtp", CAMEL_PROVIDER_TRANSPORT, NULL);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", e_source_get_uid (account_source));
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_clear_object (&store_service);
	g_clear_object (&transport_service);
	g_list_free (sources);
	g_clear_object (&account_source);
	g_clear_object (&identity_source);
	g_clear_object (&transport_source);
	json_node_unref (root);

	return json;
}

/* Teste la connectivité réseau TCP et la négociation TLS / bannière vers un serveur.
 * Paramètres :
 *   host            : nom d'hôte ou adresse IP du serveur
 *   port            : numéro de port réseau
 *   security        : mode de chiffrement ("tls", "starttls", "none")
 *   expected_prefix : préfixe attendu pour la bannière ("* OK" pour IMAP, "220" pour SMTP)
 *   out_banner      : reçoit la bannière lue en cas de succès (à libérer)
 *   out_error       : reçoit le message d'erreur en cas d'échec (à libérer)
 * Retourne : TRUE si la connexion a réussi. */
static gboolean
core_test_single_server (const gchar *host,
			 guint16 port,
			 const gchar *security,
			 const gchar *expected_prefix,
			 gchar **out_banner,
			 gchar **out_error)
{
	GSocketClient *client;
	GSocketConnection *conn;
	GError *error = NULL;
	gboolean is_tls = (security && (g_ascii_strcasecmp (security, "tls") == 0 ||
					g_ascii_strcasecmp (security, "ssl") == 0)) ||
			  port == 993 || port == 465;

	if (!host || !*host) {
		*out_error = g_strdup ("Adresse de serveur non renseignée");
		return FALSE;
	}

	client = g_socket_client_new ();
	g_socket_client_set_timeout (client, 8);
	if (is_tls)
		g_socket_client_set_tls (client, TRUE);

	conn = g_socket_client_connect_to_host (client, host, port, NULL, &error);
	if (!conn) {
		*out_error = g_strdup (error ? error->message : "Échec de connexion au serveur");
		g_clear_error (&error);
		g_object_unref (client);
		return FALSE;
	}

	GInputStream *in_stream = g_io_stream_get_input_stream (G_IO_STREAM (conn));
	GDataInputStream *data_in = g_data_input_stream_new (in_stream);
	gchar *line = g_data_input_stream_read_line (data_in, NULL, NULL, &error);

	if (line) {
		g_strstrip (line);
		*out_banner = g_strdup (line);
		g_free (line);
	} else if (error) {
		*out_banner = g_strdup ("Connecté (bannière non lisible)");
		g_clear_error (&error);
	} else {
		*out_banner = g_strdup ("Connecté avec succès");
	}

	g_object_unref (data_in);
	g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
	g_object_unref (conn);
	g_object_unref (client);

	return TRUE;
}

/* Teste la connectivité aux serveurs entrant et sortant spécifiés dans le JSON.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON décrivant les serveurs à tester
 *   error : emplacement pour une éventuelle erreur
 * Retourne : JSON contenant imap_ok, imap_banner, imap_error, smtp_ok, smtp_banner, smtp_error. */
static gchar *
core_test_connection_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *imap_host, *imap_security;
	const gchar *smtp_host, *smtp_security;
	gint64 imap_port, smtp_port;
	gboolean imap_ok = FALSE, smtp_ok = FALSE;
	gchar *imap_banner = NULL, *imap_err = NULL;
	gchar *smtp_banner = NULL, *smtp_err = NULL;
	JsonBuilder *builder;
	gchar *json;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		json_node_unref (root);
		return NULL;
	}

	imap_host = json_object_get_string_member_with_default (object, "imap_host", "");
	imap_port = json_object_get_int_member_with_default (object, "imap_port", 993);
	imap_security = json_object_get_string_member_with_default (object, "imap_security", "tls");

	smtp_host = json_object_get_string_member_with_default (object, "smtp_host", "");
	smtp_port = json_object_get_int_member_with_default (object, "smtp_port", 465);
	smtp_security = json_object_get_string_member_with_default (object, "smtp_security", "tls");

	if (imap_host && *imap_host)
		imap_ok = core_test_single_server (imap_host, (guint16) imap_port, imap_security, "* OK",
						   &imap_banner, &imap_err);
	else
		imap_err = g_strdup ("Aucun serveur entrant spécifié");

	if (smtp_host && *smtp_host)
		smtp_ok = core_test_single_server (smtp_host, (guint16) smtp_port, smtp_security, "220",
						   &smtp_banner, &smtp_err);
	else
		smtp_err = g_strdup ("Aucun serveur sortant spécifié");

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_boolean (builder, "imap_ok", imap_ok);
	builder_add_string (builder, "imap_banner", imap_banner ? imap_banner : "");
	builder_add_string (builder, "imap_error", imap_err ? imap_err : "");
	builder_add_boolean (builder, "smtp_ok", smtp_ok);
	builder_add_string (builder, "smtp_banner", smtp_banner ? smtp_banner : "");
	builder_add_string (builder, "smtp_error", smtp_err ? smtp_err : "");
	json_builder_end_object (builder);

	json = json_from_builder (builder);

	g_free (imap_banner);
	g_free (imap_err);
	g_free (smtp_banner);
	g_free (smtp_err);
	g_object_unref (builder);
	json_node_unref (root);

	return json;
}

/* Convertit une valeur CamelNetworkSecurityMethod en nom lisible ("tls", "starttls", "none").
 * Paramètres :
 *   method : méthode de sécurité Camel
 * Retourne : chaîne statique correspondante. */
static const gchar *
security_method_name (CamelNetworkSecurityMethod method)
{
	switch (method) {
	case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
		return "tls";
	case CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT:
		return "starttls";
	default:
		return "none";
	}
}

/* Récupère la configuration détaillée d'un compte de messagerie existant.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = account_uid
 *   error : emplacement pour une éventuelle erreur
 * Retourne : JSON avec toutes les propriétés du compte, ses serveurs et ses dossiers spéciaux. */
static gchar *
core_get_account_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	const gchar *account_uid = args[0];
	ESource *account, *identity = NULL, *transport = NULL, *default_account = NULL;
	ESourceMailAccount *mail_account = NULL;
	ESourceMailIdentity *mail_identity = NULL;
	ESourceMailSubmission *mail_submission = NULL;
	ESourceMailComposition *mail_composition = NULL;
	ESourceCamel *imap_camel = NULL, *smtp_camel = NULL;
	CamelSettings *imap_settings = NULL, *smtp_settings = NULL;
	gboolean is_default = FALSE;
	const gchar *backend = "imapx";
	gchar *address = NULL, *real_name = NULL;
	const gchar *imap_host = "", *imap_user = "", *imap_security = "tls";
	guint16 imap_port = 993;
	const gchar *smtp_host = "", *smtp_user = "", *smtp_security = "tls";
	guint16 smtp_port = 465;
	gchar *sent_folder = NULL, *drafts_folder = NULL, *archive_folder = NULL;
	gchar *trash_folder = NULL, *junk_folder = NULL;
	JsonBuilder *builder;
	gchar *json;

	account = e_source_registry_ref_source (core->registry, account_uid);
	if (!account) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Compte introuvable : « %s »", account_uid);
		return NULL;
	}

	default_account = e_source_registry_ref_default_mail_account (core->registry);
	is_default = (default_account != NULL && g_strcmp0 (e_source_get_uid (default_account), account_uid) == 0);
	g_clear_object (&default_account);

	if (e_source_has_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		mail_account = e_source_get_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
		backend = e_source_backend_get_backend_name (E_SOURCE_BACKEND (mail_account));
		gchar *identity_uid = e_source_mail_account_dup_identity_uid (mail_account);
		if (identity_uid) {
			identity = e_source_registry_ref_source (core->registry, identity_uid);
			g_free (identity_uid);
		}
		gchar *arch_uri = e_source_mail_account_dup_archive_folder (mail_account);
		if (arch_uri) {
			gchar *dummy = NULL;
			parse_folder_uri (arch_uri, &dummy, &archive_folder);
			g_free (dummy);
			g_free (arch_uri);
		}
	}

	if (identity) {
		if (e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY)) {
			mail_identity = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
			address = e_source_mail_identity_dup_address (mail_identity);
			real_name = e_source_mail_identity_dup_name (mail_identity);
		}
		if (e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
			mail_submission = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
			gchar *trans_uid = e_source_mail_submission_dup_transport_uid (mail_submission);
			if (trans_uid) {
				transport = e_source_registry_ref_source (core->registry, trans_uid);
				g_free (trans_uid);
			}
			gchar *sent_uri = e_source_mail_submission_dup_sent_folder (mail_submission);
			if (sent_uri) {
				gchar *dummy = NULL;
				parse_folder_uri (sent_uri, &dummy, &sent_folder);
				g_free (dummy);
				g_free (sent_uri);
			}
		}
		if (e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION)) {
			mail_composition = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION);
			gchar *drafts_uri = e_source_mail_composition_dup_drafts_folder (mail_composition);
			if (drafts_uri) {
				gchar *dummy = NULL;
				parse_folder_uri (drafts_uri, &dummy, &drafts_folder);
				g_free (dummy);
				g_free (drafts_uri);
			}
		}
	}

	if (e_source_has_extension (account, e_source_camel_get_extension_name (backend))) {
		imap_camel = e_source_get_extension (account, e_source_camel_get_extension_name (backend));
		imap_settings = e_source_camel_get_settings (imap_camel);
		if (CAMEL_IS_NETWORK_SETTINGS (imap_settings)) {
			imap_host = camel_network_settings_get_host (CAMEL_NETWORK_SETTINGS (imap_settings));
			imap_port = camel_network_settings_get_port (CAMEL_NETWORK_SETTINGS (imap_settings));
			imap_user = camel_network_settings_get_user (CAMEL_NETWORK_SETTINGS (imap_settings));
			imap_security = security_method_name (camel_network_settings_get_security_method (CAMEL_NETWORK_SETTINGS (imap_settings)));
		}
		gboolean use_trash = FALSE, use_junk = FALSE;
		gchar *rtrash = NULL, *rjunk = NULL;
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (imap_settings), "use-real-trash-path"))
			g_object_get (imap_settings, "use-real-trash-path", &use_trash, "real-trash-path", &rtrash, NULL);
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (imap_settings), "use-real-junk-path"))
			g_object_get (imap_settings, "use-real-junk-path", &use_junk, "real-junk-path", &rjunk, NULL);
		if (use_trash && rtrash && *rtrash)
			trash_folder = rtrash;
		else
			g_free (rtrash);
		if (use_junk && rjunk && *rjunk)
			junk_folder = rjunk;
		else
			g_free (rjunk);
	}

	if (transport && e_source_has_extension (transport, e_source_camel_get_extension_name ("smtp"))) {
		smtp_camel = e_source_get_extension (transport, e_source_camel_get_extension_name ("smtp"));
		smtp_settings = e_source_camel_get_settings (smtp_camel);
		if (CAMEL_IS_NETWORK_SETTINGS (smtp_settings)) {
			smtp_host = camel_network_settings_get_host (CAMEL_NETWORK_SETTINGS (smtp_settings));
			smtp_port = camel_network_settings_get_port (CAMEL_NETWORK_SETTINGS (smtp_settings));
			smtp_user = camel_network_settings_get_user (CAMEL_NETWORK_SETTINGS (smtp_settings));
			smtp_security = security_method_name (camel_network_settings_get_security_method (CAMEL_NETWORK_SETTINGS (smtp_settings)));
		}
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", account_uid);
	builder_add_string (builder, "name", e_source_get_display_name (account));
	builder_add_string (builder, "display_name", e_source_get_display_name (account));
	builder_add_string (builder, "email", address ? address : "");
	builder_add_string (builder, "address", address ? address : "");
	builder_add_string (builder, "real_name", real_name ? real_name : "");
	builder_add_string (builder, "backend", backend ? backend : "");
	builder_add_boolean (builder, "default", is_default);
	builder_add_boolean (builder, "local", g_strcmp0 (account_uid, "local") == 0);
	builder_add_boolean (builder, "enabled", e_source_get_enabled (account));

	builder_add_string (builder, "imap_host", imap_host ? imap_host : "");
	builder_add_int (builder, "imap_port", (gint64) imap_port);
	builder_add_string (builder, "imap_user", imap_user ? imap_user : "");
	builder_add_string (builder, "imap_security", imap_security ? imap_security : "tls");

	builder_add_string (builder, "smtp_host", smtp_host ? smtp_host : "");
	builder_add_int (builder, "smtp_port", (gint64) smtp_port);
	builder_add_string (builder, "smtp_user", smtp_user ? smtp_user : "");
	builder_add_string (builder, "smtp_security", smtp_security ? smtp_security : "tls");

	json_builder_set_member_name (builder, "special_folders");
	json_builder_begin_object (builder);
	builder_add_string (builder, "sent", sent_folder ? sent_folder : "");
	builder_add_string (builder, "drafts", drafts_folder ? drafts_folder : "");
	builder_add_string (builder, "archive", archive_folder ? archive_folder : "");
	builder_add_string (builder, "trash", trash_folder ? trash_folder : "");
	builder_add_string (builder, "junk", junk_folder ? junk_folder : "");
	json_builder_end_object (builder);

	json_builder_end_object (builder);
	json = json_from_builder (builder);

	g_free (address);
	g_free (real_name);
	g_free (sent_folder);
	g_free (drafts_folder);
	g_free (archive_folder);
	g_free (trash_folder);
	g_free (junk_folder);
	g_clear_object (&identity);
	g_clear_object (&transport);
	g_clear_object (&account);
	g_object_unref (builder);

	return json;
}

/* Modifie la configuration d'un compte de messagerie existant (nom, serveurs, mot de passe, dossiers spéciaux).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON des modifications
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"uid": "<account_uid>"} en cas de succès, NULL sinon. */
static gchar *
core_modify_account_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *account_uid;
	const gchar *display_name, *real_name, *email;
	const gchar *imap_host, *imap_user, *imap_security;
	const gchar *smtp_host, *smtp_user, *smtp_security;
	const gchar *password;
	gint64 imap_port, smtp_port;
	ESource *account, *identity = NULL, *transport = NULL;
	ESourceMailAccount *mail_account = NULL;
	ESourceMailIdentity *mail_identity = NULL;
	ESourceMailSubmission *mail_submission = NULL;
	ESourceMailComposition *mail_composition = NULL;
	ESourceCamel *imap_camel = NULL, *smtp_camel = NULL;
	CamelSettings *imap_settings = NULL, *smtp_settings = NULL;
	const gchar *backend = "imapx";
	JsonBuilder *builder;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		json_node_unref (root);
		return NULL;
	}

	account_uid = json_object_get_string_member_with_default (object, "uid", "");
	if (!account_uid || !*account_uid) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Identifiant du compte (uid) manquant");
		json_node_unref (root);
		return NULL;
	}

	account = e_source_registry_ref_source (core->registry, account_uid);
	if (!account) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Compte introuvable : « %s »", account_uid);
		json_node_unref (root);
		return NULL;
	}

	display_name = json_object_get_string_member_with_default (object, "display_name", "");
	if (!display_name || !*display_name)
		display_name = json_object_get_string_member_with_default (object, "name", "");
	real_name = json_object_get_string_member_with_default (object, "real_name", "");
	email = json_object_get_string_member_with_default (object, "email", "");
	if (!email || !*email)
		email = json_object_get_string_member_with_default (object, "address", "");

	imap_host = json_object_get_string_member_with_default (object, "imap_host", "");
	imap_port = json_object_get_int_member_with_default (object, "imap_port", 0);
	imap_user = json_object_get_string_member_with_default (object, "imap_user", "");
	if (!imap_user || !*imap_user)
		imap_user = json_object_get_string_member_with_default (object, "user", "");
	imap_security = json_object_get_string_member_with_default (object, "imap_security", "tls");

	smtp_host = json_object_get_string_member_with_default (object, "smtp_host", "");
	smtp_port = json_object_get_int_member_with_default (object, "smtp_port", 0);
	smtp_user = json_object_get_string_member_with_default (object, "smtp_user", "");
	smtp_security = json_object_get_string_member_with_default (object, "smtp_security", "tls");

	password = json_object_get_string_member_with_default (object, "password", "");

	if (display_name && *display_name)
		e_source_set_display_name (account, display_name);

	if (json_object_has_member (object, "enabled")) {
		gboolean enabled = json_object_get_boolean_member (object, "enabled");
		e_source_set_enabled (account, enabled);
		if (identity)
			e_source_set_enabled (identity, enabled);
		if (transport)
			e_source_set_enabled (transport, enabled);
	}

	if (e_source_has_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		mail_account = e_source_get_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
		backend = e_source_backend_get_backend_name (E_SOURCE_BACKEND (mail_account));
		gchar *identity_uid = e_source_mail_account_dup_identity_uid (mail_account);
		if (identity_uid) {
			identity = e_source_registry_ref_source (core->registry, identity_uid);
			g_free (identity_uid);
		}
	}

	if (identity) {
		if (display_name && *display_name)
			e_source_set_display_name (identity, display_name);
		if (e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY)) {
			mail_identity = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
			if (real_name && *real_name)
				e_source_mail_identity_set_name (mail_identity, real_name);
			if (email && *email)
				e_source_mail_identity_set_address (mail_identity, email);
		}
		if (e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
			mail_submission = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
			gchar *trans_uid = e_source_mail_submission_dup_transport_uid (mail_submission);
			if (trans_uid) {
				transport = e_source_registry_ref_source (core->registry, trans_uid);
				g_free (trans_uid);
			}
		}
		if (e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION))
			mail_composition = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION);
	}

	if (transport && display_name && *display_name)
		e_source_set_display_name (transport, display_name);

	/* Mise à jour des réglages réseau IMAP */
	if (e_source_has_extension (account, e_source_camel_get_extension_name (backend))) {
		imap_camel = e_source_get_extension (account, e_source_camel_get_extension_name (backend));
		imap_settings = e_source_camel_get_settings (imap_camel);
		if (CAMEL_IS_NETWORK_SETTINGS (imap_settings)) {
			if (imap_host && *imap_host)
				camel_network_settings_set_host (CAMEL_NETWORK_SETTINGS (imap_settings), imap_host);
			if (imap_port > 0)
				camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (imap_settings), (guint16) imap_port);
			if (imap_user && *imap_user)
				camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (imap_settings), imap_user);
			if (imap_security && *imap_security)
				camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (imap_settings),
					parse_security_method (imap_security));
		}
	}

	/* Mise à jour des réglages réseau SMTP */
	if (transport && e_source_has_extension (transport, e_source_camel_get_extension_name ("smtp"))) {
		smtp_camel = e_source_get_extension (transport, e_source_camel_get_extension_name ("smtp"));
		smtp_settings = e_source_camel_get_settings (smtp_camel);
		if (CAMEL_IS_NETWORK_SETTINGS (smtp_settings)) {
			if (smtp_host && *smtp_host)
				camel_network_settings_set_host (CAMEL_NETWORK_SETTINGS (smtp_settings), smtp_host);
			if (smtp_port > 0)
				camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (smtp_settings), (guint16) smtp_port);
			if (smtp_user && *smtp_user)
				camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (smtp_settings), smtp_user);
			if (smtp_security && *smtp_security)
				camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (smtp_settings),
					parse_security_method (smtp_security));
		}
	}

	/* Mise à jour des rôles de dossiers spéciaux */
	if (json_object_has_member (object, "special_folders")) {
		JsonObject *sf = json_object_get_object_member (object, "special_folders");
		if (sf) {
			const gchar *sent = json_object_get_string_member_with_default (sf, "sent", NULL);
			const gchar *drafts = json_object_get_string_member_with_default (sf, "drafts", NULL);
			const gchar *archive = json_object_get_string_member_with_default (sf, "archive", NULL);
			const gchar *trash = json_object_get_string_member_with_default (sf, "trash", NULL);
			const gchar *junk = json_object_get_string_member_with_default (sf, "junk", NULL);

			if (sent && mail_submission) {
				gchar *escaped = g_uri_escape_string (sent, NULL, FALSE);
				gchar *uri = g_strdup_printf ("folder://%s/%s", account_uid, escaped);
				e_source_mail_submission_set_sent_folder (mail_submission, uri);
				g_free (uri);
				g_free (escaped);
			}
			if (drafts && mail_composition) {
				gchar *escaped = g_uri_escape_string (drafts, NULL, FALSE);
				gchar *uri = g_strdup_printf ("folder://%s/%s", account_uid, escaped);
				e_source_mail_composition_set_drafts_folder (mail_composition, uri);
				g_free (uri);
				g_free (escaped);
			}
			if (archive && mail_account) {
				gchar *escaped = g_uri_escape_string (archive, NULL, FALSE);
				gchar *uri = g_strdup_printf ("folder://%s/%s", account_uid, escaped);
				e_source_mail_account_set_archive_folder (mail_account, uri);
				g_free (uri);
				g_free (escaped);
			}
			if (imap_settings) {
				if (trash && g_object_class_find_property (G_OBJECT_GET_CLASS (imap_settings), "use-real-trash-path"))
					g_object_set (imap_settings, "use-real-trash-path", *trash != '\0',
						      "real-trash-path", trash, NULL);
				if (junk && g_object_class_find_property (G_OBJECT_GET_CLASS (imap_settings), "use-real-junk-path"))
					g_object_set (imap_settings, "use-real-junk-path", *junk != '\0',
						      "real-junk-path", junk, NULL);
			}
		}
	}

	/* Enregistrement des modifications dans EDS */
	if (!e_source_registry_commit_source_sync (core->registry, account, core->cancellable, error))
		goto exit;
	if (identity && !e_source_registry_commit_source_sync (core->registry, identity, core->cancellable, error))
		goto exit;
	if (transport && !e_source_registry_commit_source_sync (core->registry, transport, core->cancellable, error))
		goto exit;

	/* Sauvegarde du mot de passe s'il est spécifié */
	if (password && *password) {
		core_store_source_password (core, account_uid, password, NULL);
		if (transport)
			core_store_source_password (core, e_source_get_uid (transport), password, NULL);
	}

	/* Notification de mise à jour des dossiers */
	{
		JsonBuilder *upd_builder = core_event_builder ();
		builder_add_string (upd_builder, "account", account_uid);
		core_emit_builder (core, "folders-updated", upd_builder);
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", account_uid);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

exit:
	g_clear_object (&identity);
	g_clear_object (&transport);
	g_clear_object (&account);
	json_node_unref (root);

	return json;
}

/* Microsoft 365 or EWS settings (CamelM365Settings / CamelEwsSettings,
 * from evolution-ews) of a source, set through their properties: evo-core
 * does not link to evolution-ews, the types come with the loaded
 * "microsoft365" and "ews" providers. EWS uses NTLM on hosturl. */
void
core_set_m365_settings (ESource *source,
			const gchar *backend,
			const gchar *email,
			const gchar *user,
			const gchar *hosturl,
			const gchar *client_id,
			const gchar *tenant)
{
	ESourceCamel *extension;
	CamelSettings *settings;
	gboolean override_oauth2 = (client_id && *client_id) || (tenant && *tenant);

	extension = e_source_get_extension (source, e_source_camel_get_extension_name (backend));
	settings = e_source_camel_get_settings (extension);

	camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (settings), user);
	g_object_set (settings, "email", email, NULL);

	if (g_str_equal (backend, "ews")) {
		g_object_set (settings, "hosturl", hosturl, NULL);
		camel_network_settings_set_auth_mechanism (CAMEL_NETWORK_SETTINGS (settings), "NTLM");
	} else if (override_oauth2) {
		g_object_set (settings, "override-oauth2", TRUE, NULL);
		if (client_id && *client_id)
			g_object_set (settings, "oauth2-client-id", client_id, NULL);
		if (tenant && *tenant)
			g_object_set (settings, "oauth2-tenant", tenant, NULL);
	}
}

/* Soumet une requête d'enregistrement du mot de passe d'un compte ou serveur.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   source_uid : identifiant de la source
 *   password   : mot de passe à sauvegarder
 *   func       : rappel recevant {}
 *   user_data  : données utilisateur transmises au rappel */
void
evo_core_save_password (EvoCore *core,
			const char *source_uid,
			const char *password,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = { (gchar *) source_uid, (gchar *) (password ? password : ""), NULL };

	g_return_if_fail (source_uid != NULL);

	core_submit (core, core_save_password_run, g_strdupv (args), func, user_data);
}

/* Soumet un test de connectivité réseau et protocole aux serveurs spécifiés.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   account_json : JSON contenant les serveurs à tester
 *   func         : rappel recevant les résultats du test
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_test_connection (EvoCore *core,
			  const char *account_json,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) account_json, NULL };

	g_return_if_fail (account_json != NULL);

	core_submit (core, core_test_connection_run, g_strdupv (args), func, user_data);
}

/* Récupère la configuration détaillée d'un compte existant.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant unique du compte
 *   func        : rappel recevant le JSON des propriétés du compte
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_get_account (EvoCore *core,
		      const char *account_uid,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, NULL };

	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_get_account_run, g_strdupv (args), func, user_data);
}

/* Modifie la configuration d'un compte existant.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   account_json : JSON contenant les modifications à appliquer
 *   func         : rappel recevant {"uid": "..."}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_modify_account (EvoCore *core,
			 const char *account_json,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) account_json, NULL };

	g_return_if_fail (account_json != NULL);

	core_submit (core, core_modify_account_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de création d'un nouveau compte IMAP / SMTP.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   account_json : configuration JSON du compte
 *   func         : rappel recevant {"uid": "..."}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_add_account (EvoCore *core,
		      const char *account_json,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) account_json, NULL };

	g_return_if_fail (account_json != NULL);

	core_submit (core, core_add_account_run, g_strdupv (args), func, user_data);
}
