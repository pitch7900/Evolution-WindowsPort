/*
 * evo-core : CamelSession du moteur : authentification (mots de passe, OAuth2),
 * approbation des certificats TLS.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

G_DEFINE_TYPE (EvoCoreSession, evo_core_session, CAMEL_TYPE_SESSION)

static CamelService *
evo_core_session_add_service (CamelSession *session,
			      const gchar *uid,
			      const gchar *protocol,
			      CamelProviderType type,
			      GError **error)
{
	EvoCoreSession *self = EVO_CORE_SESSION (session);
	CamelService *service;

	service = CAMEL_SESSION_CLASS (evo_core_session_parent_class)->add_service (session, uid, protocol, type, error);

	if (CAMEL_IS_SERVICE (service)) {
		ESource *source, *camel_source;

		source = e_source_registry_ref_source (self->registry, uid);

		if (source) {
			/* the Camel settings may live in a related source */
			camel_source = e_source_registry_find_extension (self->registry, source,
				e_source_camel_get_extension_name (protocol));

			e_source_camel_configure_service (camel_source ? camel_source : source, service);
			camel_service_set_display_name (service, e_source_get_display_name (source));

			g_clear_object (&camel_source);
			g_object_unref (source);
		}
	}

	return service;
}

/* saved password of a source, NULL when there is none */
static gchar *
evo_core_session_lookup_source_password (EvoCoreSession *self,
					 ESource *source,
					 GCancellable *cancellable,
					 GError **error)
{
	ENamedParameters *credentials = NULL;
	gchar *password = NULL;

	if (e_source_credentials_provider_lookup_sync (self->credentials_provider, source,
		cancellable, &credentials, error) && credentials) {
		password = g_strdup (e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD));
		e_named_parameters_free (credentials);
	}

	if (password && !*password)
		g_clear_pointer (&password, g_free);

	return password;
}

static gchar *
service_dup_user (CamelService *service)
{
	CamelSettings *settings = camel_service_ref_settings (service);
	gchar *user = NULL;

	if (CAMEL_IS_NETWORK_SETTINGS (settings))
		user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (settings));

	g_clear_object (&settings);

	return user;
}

/* The mail account (incoming server) whose identity sends through the
 * given outgoing server, if it logs in with the same user name: outgoing
 * servers commonly share the incoming server's credentials, and Evolution
 * saves the SMTP password only after prompting for it on the first send. */
static ESource *
evo_core_session_ref_account_of_transport (EvoCoreSession *self,
					   CamelService *transport)
{
	GList *accounts, *link;
	ESource *found = NULL;
	gchar *transport_user = service_dup_user (transport);

	accounts = e_source_registry_list_enabled (self->registry, E_SOURCE_EXTENSION_MAIL_ACCOUNT);

	for (link = accounts; link && !found; link = g_list_next (link)) {
		ESource *account = link->data;
		ESource *identity = NULL;
		gchar *identity_uid;

		identity_uid = e_source_mail_account_dup_identity_uid (
			e_source_get_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT));
		if (identity_uid)
			identity = e_source_registry_ref_source (self->registry, identity_uid);
		g_free (identity_uid);

		if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION) &&
		    g_strcmp0 (e_source_mail_submission_get_transport_uid (
				e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)),
			       camel_service_get_uid (transport)) == 0) {
			CamelService *store = camel_session_ref_service (CAMEL_SESSION (self), e_source_get_uid (account));
			gchar *store_user = store ? service_dup_user (store) : NULL;

			if (!transport_user || !*transport_user || !store_user || !*store_user ||
			    g_ascii_strcasecmp (transport_user, store_user) == 0)
				found = g_object_ref (account);

			g_free (store_user);
			g_clear_object (&store);
		}

		g_clear_object (&identity);
	}

	g_list_free_full (accounts, g_object_unref);
	g_free (transport_user);

	return found;
}

static gchar *
evo_core_session_lookup_password (EvoCoreSession *self,
				  CamelService *service,
				  GCancellable *cancellable,
				  GError **error)
{
	ESource *source;
	gchar *password;
	GError *local_error = NULL;

	source = e_source_registry_ref_source (self->registry, camel_service_get_uid (service));
	if (!source) {
		g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			"No account found for “%s”", camel_service_get_uid (service));
		return NULL;
	}

	password = evo_core_session_lookup_source_password (self, source, cancellable, &local_error);
	g_object_unref (source);

	/* outgoing server without saved password: the incoming server's */
	if (!password && CAMEL_IS_TRANSPORT (service) &&
	    !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		ESource *account = evo_core_session_ref_account_of_transport (self, service);

		if (account) {
			g_clear_error (&local_error);
			password = evo_core_session_lookup_source_password (self, account, cancellable, &local_error);
			g_object_unref (account);
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

	return password;
}

/* Émet l'événement "auth-required" vers l'interface utilisateur lorsqu'un
 * compte ou serveur d'envoi n'a pas de mot de passe enregistré ou que son mot
 * de passe a été refusé par le serveur.
 * Paramètres :
 *   self     : session Camel courante
 *   service  : service Camel concerné (réception ou envoi)
 *   rejected : TRUE si le mot de passe enregistré a été refusé */
static void
evo_core_session_emit_auth_required (EvoCoreSession *self,
				     CamelService *service,
				     gboolean rejected)
{
	const gchar *uid;
	gchar *user;
	JsonBuilder *builder;

	if (!self->core || !CAMEL_IS_SERVICE (service))
		return;

	uid = camel_service_get_uid (service);
	if (!uid || !*uid)
		return;

	g_mutex_lock (&self->prompted_lock);
	if (!rejected && g_hash_table_contains (self->prompted_sources, uid)) {
		g_mutex_unlock (&self->prompted_lock);
		return;
	}
	g_hash_table_add (self->prompted_sources, g_strdup (uid));
	g_mutex_unlock (&self->prompted_lock);

	user = service_dup_user (service);

	builder = core_event_builder ();
	builder_add_string (builder, "source_uid", uid);
	builder_add_string (builder, "name", camel_service_get_display_name (service));
	builder_add_string (builder, "user", user ? user : "");
	json_builder_set_member_name (builder, "rejected");
	json_builder_add_boolean_value (builder, rejected);
	core_emit_builder (self->core, "auth-required", builder);

	g_free (user);
}

/* Whether the authentication mechanism is an OAuth2 service of EDS (e.g.
 * "Microsoft365"): its token comes from the source registry, which
 * refreshes it, not from a password. */
static gboolean
evo_core_session_is_oauth2_method (EvoCoreSession *self,
				   const gchar *mechanism)
{
	return mechanism && self->registry &&
		e_oauth2_services_is_oauth2_alias (e_source_registry_get_oauth2_services (self->registry), mechanism);
}

/* Emits "oauth2-required" when an OAuth2 account has no valid token (never
 * signed in, or refresh token revoked/expired): the interface opens the
 * sign-in page (evo_core_oauth2_begin). source_uid is the source carrying
 * the authentication, the collection for Microsoft 365 / EWS accounts.
 * Once per source until evo_core_oauth2_finish () succeeds. */
static void
evo_core_session_emit_oauth2_required (EvoCoreSession *self,
				       CamelService *service)
{
	ESource *source, *collection = NULL;
	JsonBuilder *builder;
	const gchar *uid;
	gchar *user;

	if (!self->core || !self->registry)
		return;

	source = e_source_registry_ref_source (self->registry, camel_service_get_uid (service));
	if (source)
		collection = e_source_registry_find_extension (self->registry, source, E_SOURCE_EXTENSION_COLLECTION);
	uid = e_source_get_uid (collection ? collection : source);

	g_mutex_lock (&self->prompted_lock);
	if (!uid || g_hash_table_contains (self->prompted_sources, uid)) {
		g_mutex_unlock (&self->prompted_lock);
		g_clear_object (&collection);
		g_clear_object (&source);
		return;
	}
	g_hash_table_add (self->prompted_sources, g_strdup (uid));
	g_mutex_unlock (&self->prompted_lock);

	user = service_dup_user (service);

	builder = core_event_builder ();
	builder_add_string (builder, "source_uid", uid);
	builder_add_string (builder, "name", camel_service_get_display_name (service));
	builder_add_string (builder, "user", user ? user : "");
	core_emit_builder (self->core, "oauth2-required", builder);

	g_free (user);
	g_clear_object (&collection);
	g_clear_object (&source);
}

static gchar *
evo_core_session_get_password (CamelSession *session,
			       CamelService *service,
			       const gchar *prompt,
			       const gchar *item,
			       guint32 flags,
			       GError **error)
{
	gchar *password;

	password = evo_core_session_lookup_password (EVO_CORE_SESSION (session), service, NULL, error);

	if (!password) {
		evo_core_session_emit_auth_required (EVO_CORE_SESSION (session), service, FALSE);
		if (error && !*error)
			g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				"No saved password for “%s”", camel_service_get_display_name (service));
	}

	return password;
}

static gboolean
evo_core_session_authenticate_sync (CamelSession *session,
				    CamelService *service,
				    const gchar *mechanism,
				    GCancellable *cancellable,
				    GError **error)
{
	CamelServiceAuthType *authtype = NULL;
	CamelAuthenticationResult result;
	gchar *password;

	if (g_strcmp0 (mechanism, "none") == 0)
		mechanism = NULL;

	if (mechanism)
		authtype = camel_sasl_authtype (mechanism);

	/* mechanisms without password get one try; an EDS OAuth2 token
	 * missing or refused (not a network error) needs a new sign-in */
	if ((authtype && !authtype->need_password) ||
	    evo_core_session_is_oauth2_method (EVO_CORE_SESSION (session), mechanism)) {
		result = camel_service_authenticate_sync (service, mechanism, cancellable, error);

		if (result == CAMEL_AUTHENTICATION_REJECTED &&
		    evo_core_session_is_oauth2_method (EVO_CORE_SESSION (session), mechanism))
			evo_core_session_emit_oauth2_required (EVO_CORE_SESSION (session), service);

		return result == CAMEL_AUTHENTICATION_ACCEPTED;
	}

	password = evo_core_session_lookup_password (EVO_CORE_SESSION (session), service, cancellable, error);
	if (!password) {
		evo_core_session_emit_auth_required (EVO_CORE_SESSION (session), service, FALSE);
		if (error && !*error)
			g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				"No saved password for “%s”", camel_service_get_display_name (service));
		return FALSE;
	}

	camel_service_set_password (service, password);
	e_util_safe_free_string (password);

	result = camel_service_authenticate_sync (service, mechanism, cancellable, error);

	if (result == CAMEL_AUTHENTICATION_REJECTED) {
		evo_core_session_emit_auth_required (EVO_CORE_SESSION (session), service, TRUE);
		if (error && !*error)
			g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				"The password of “%s” was rejected", camel_service_get_display_name (service));
	}

	return result == CAMEL_AUTHENTICATION_ACCEPTED;
}

/* Fournit le jeton d'accès OAuth2 (XOAUTH2) d'un service Camel à partir du
 * Gestionnaire d'identifiants Windows. Le secret stocké peut être soit le
 * jeton d'accès brut, soit un objet JSON {"access_token": "..."}.
 * Paramètres :
 *   session          : session Camel courante
 *   service          : service IMAP ou SMTP demandant le jeton OAuth2
 *   out_access_token : reçoit le jeton d'accès alloué
 *   out_expires_in   : reçoit la durée de validité en secondes
 *   cancellable      : objet d'annulation
 *   error            : emplacement pour une éventuelle erreur
 * Retourne : TRUE si un jeton d'accès valide a été obtenu. */
static ESource *
evo_core_session_ref_source_cb (gpointer user_data,
				const gchar *uid)
{
	return e_source_registry_ref_source (user_data, uid);
}

static gboolean
evo_core_session_get_oauth2_access_token_sync (CamelSession *session,
					       CamelService *service,
					       gchar **out_access_token,
					       gint *out_expires_in,
					       GCancellable *cancellable,
					       GError **error)
{
	EvoCoreSession *self = EVO_CORE_SESSION (session);
	gchar *stored;

	/* compte d'une collection OAuth2 d'EDS (Google...) : jeton du service
	   OAuth2 de la collection, renouvelé par EDS */
	if (self->registry) {
		ESource *source = e_source_registry_ref_source (self->registry, camel_service_get_uid (service));
		ESource *collection = source ? e_source_registry_find_extension (self->registry, source, E_SOURCE_EXTENSION_COLLECTION) : NULL;
		EOAuth2Service *oauth2 = collection
			? e_oauth2_services_find (e_source_registry_get_oauth2_services (self->registry), collection) : NULL;

		if (oauth2) {
			GError *local_error = NULL;
			gboolean ok = e_oauth2_service_get_access_token_sync (oauth2, collection,
				evo_core_session_ref_source_cb, self->registry, out_access_token, out_expires_in,
				cancellable, &local_error);

			if (!ok && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
			    !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND))
				evo_core_session_emit_oauth2_required (self, service);
			if (local_error)
				g_propagate_error (error, local_error);

			g_object_unref (oauth2);
			g_object_unref (collection);
			g_object_unref (source);
			return ok;
		}

		g_clear_object (&collection);
		g_clear_object (&source);
	}

	stored = evo_core_session_lookup_password (self, service, cancellable, error);
	if (!stored || !*stored) {
		g_free (stored);
		evo_core_session_emit_auth_required (self, service, FALSE);
		if (error && !*error)
			g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				"Aucun jeton OAuth2 enregistré pour « %s »", camel_service_get_display_name (service));
		return FALSE;
	}

	if (stored[0] == '{') {
		JsonNode *root = json_from_string (stored, NULL);
		if (root && JSON_NODE_HOLDS_OBJECT (root)) {
			JsonObject *obj = json_node_get_object (root);
			const gchar *token = json_object_get_string_member_with_default (obj, "access_token", "");
			const gchar *refresh_token = json_object_get_string_member_with_default (obj, "refresh_token", "");
			const gchar *token_endpoint = json_object_get_string_member_with_default (obj, "token_endpoint", "");
			const gchar *client_id = json_object_get_string_member_with_default (obj, "client_id", "");
			gint64 expires_in = json_object_get_int_member_with_default (obj, "expires_in", 3600);
			gint64 expires_at = json_object_get_int_member_with_default (obj, "expires_at", 0);
			gint64 now_sec = g_get_real_time () / G_USEC_PER_SEC;

			/* Renouvellement automatique du jeton OAuth2 via refresh_token lorsqu'il a expiré */
			if (refresh_token && *refresh_token && token_endpoint && *token_endpoint &&
			    client_id && *client_id && (!token || !*token || (expires_at > 0 && now_sec + 60 >= expires_at))) {
				SoupSession *soup = soup_session_new ();
				SoupMessage *msg = soup_message_new ("POST", token_endpoint);

				if (msg) {
					gchar *esc_cid = g_uri_escape_string (client_id, NULL, FALSE);
					gchar *esc_rt = g_uri_escape_string (refresh_token, NULL, FALSE);
					gchar *form = g_strdup_printf (
						"client_id=%s&grant_type=refresh_token&refresh_token=%s",
						esc_cid, esc_rt);
					GBytes *req_bytes = g_bytes_new_take (form, strlen (form));
					GBytes *resp_bytes;

					soup_message_set_request_body_from_bytes (
						msg, "application/x-www-form-urlencoded", req_bytes);
					g_bytes_unref (req_bytes);
					g_free (esc_cid);
					g_free (esc_rt);

					resp_bytes = soup_session_send_and_read (soup, msg, cancellable, NULL);
					if (resp_bytes) {
						gsize resp_len = 0;
						const gchar *resp_str = g_bytes_get_data (resp_bytes, &resp_len);
						JsonNode *resp_node = json_from_string (resp_str, NULL);

						if (resp_node && JSON_NODE_HOLDS_OBJECT (resp_node)) {
							JsonObject *resp_obj = json_node_get_object (resp_node);
							const gchar *new_token = json_object_get_string_member_with_default (resp_obj, "access_token", "");
							const gchar *new_refresh = json_object_get_string_member_with_default (resp_obj, "refresh_token", "");
							gint64 new_exp = json_object_get_int_member_with_default (resp_obj, "expires_in", 3600);

							if (new_token && *new_token) {
								ESource *src;

								json_object_set_string_member (obj, "access_token", new_token);
								if (new_refresh && *new_refresh)
									json_object_set_string_member (obj, "refresh_token", new_refresh);
								json_object_set_int_member (obj, "expires_in", new_exp > 0 ? new_exp : 3600);
								json_object_set_int_member (obj, "expires_at", now_sec + (new_exp > 0 ? new_exp : 3600));

								src = e_source_registry_ref_source (self->registry, camel_service_get_uid (service));
								if (src) {
									gchar *updated_json = json_to_string (root, FALSE);
									ENamedParameters *creds = e_named_parameters_new ();
									e_named_parameters_set (creds, E_SOURCE_CREDENTIAL_PASSWORD, updated_json);
									e_source_credentials_provider_store_sync (
										self->credentials_provider, src, creds, TRUE, cancellable, NULL);
									e_named_parameters_free (creds);
									g_free (updated_json);
									g_object_unref (src);
								}

								token = json_object_get_string_member_with_default (obj, "access_token", "");
								expires_in = new_exp > 0 ? new_exp : 3600;
							}
						}
						if (resp_node)
							json_node_unref (resp_node);
						g_bytes_unref (resp_bytes);
					}
					g_object_unref (msg);
				}
				g_object_unref (soup);
			}

			if (token && *token) {
				*out_access_token = g_strdup (token);
				*out_expires_in = (gint) (expires_in > 0 ? expires_in : 3600);
				json_node_unref (root);
				e_util_safe_free_string (stored);
				return TRUE;
			}
		}
		if (root)
			json_node_unref (root);
	}

	*out_access_token = stored;
	*out_expires_in = 3600;
	return TRUE;
}

/* Charge la liste des certificats TLS approuvés de façon permanente depuis
 * le fichier trusted-certs.ini du dossier de données utilisateur.
 * Paramètres :
 *   self : instance EvoCoreSession */
static void
evo_core_session_load_trusted_certs (EvoCoreSession *self)
{
	GKeyFile *keyfile;
	gchar **keys = NULL;
	gsize n_keys = 0, ii;

	if (!self->trust_file || !g_file_test (self->trust_file, G_FILE_TEST_IS_REGULAR))
		return;

	keyfile = g_key_file_new ();
	if (g_key_file_load_from_file (keyfile, self->trust_file, G_KEY_FILE_NONE, NULL)) {
		keys = g_key_file_get_keys (keyfile, "trusted", &n_keys, NULL);
		for (ii = 0; ii < n_keys && keys && keys[ii]; ii++) {
			gint val = g_key_file_get_integer (keyfile, "trusted", keys[ii], NULL);
			if (val == (gint) CAMEL_CERT_TRUST_FULLY) {
				g_hash_table_insert (self->trusted_certs,
					g_strdup (keys[ii]),
					GINT_TO_POINTER ((gint) CAMEL_CERT_TRUST_FULLY));
			}
		}
		g_strfreev (keys);
	}
	g_key_file_free (keyfile);
}

/* Enregistre sur le disque les certificats TLS approuvés de façon permanente.
 * Doit être appelée avec self->trust_lock verrouillé.
 * Paramètres :
 *   self : instance EvoCoreSession */
static void
evo_core_session_save_trusted_certs_locked (EvoCoreSession *self)
{
	GKeyFile *keyfile;
	GHashTableIter iter;
	gpointer key, value;
	gchar *data = NULL, *dir;
	gsize len = 0;

	if (!self->trust_file)
		return;

	keyfile = g_key_file_new ();
	g_hash_table_iter_init (&iter, self->trusted_certs);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		if (GPOINTER_TO_INT (value) == (gint) CAMEL_CERT_TRUST_FULLY)
			g_key_file_set_integer (keyfile, "trusted", (const gchar *) key, (gint) CAMEL_CERT_TRUST_FULLY);
	}

	data = g_key_file_to_data (keyfile, &len, NULL);
	if (data) {
		dir = g_path_get_dirname (self->trust_file);
		g_mkdir_with_parents (dir, 0700);
		g_file_set_contents (self->trust_file, data, (gssize) len, NULL);
		g_free (dir);
		g_free (data);
	}
	g_key_file_free (keyfile);
}

/* Calcule l'empreinte SHA-256 formatée (AA:BB:CC:...) d'un certificat TLS.
 * Paramètres :
 *   certificate : certificat GTlsCertificate
 * Retourne : chaîne nouvellement allouée contenant l'empreinte SHA-256. */
static gchar *
certificate_dup_sha256_fingerprint (GTlsCertificate *certificate)
{
	GByteArray *der = NULL;
	gchar *raw_hex, *formatted;
	GString *str;
	gsize ii, len;

	g_object_get (certificate, "certificate", &der, NULL);
	if (!der || !der->data || der->len == 0) {
		if (der)
			g_byte_array_unref (der);
		return g_strdup ("(inconnue)");
	}

	raw_hex = g_compute_checksum_for_data (G_CHECKSUM_SHA256, der->data, der->len);
	g_byte_array_unref (der);

	if (!raw_hex)
		return g_strdup ("(inconnue)");

	len = strlen (raw_hex);
	str = g_string_sized_new (len + len / 2 + 2);
	for (ii = 0; ii < len; ii += 2) {
		if (ii > 0)
			g_string_append_c (str, ':');
		g_string_append_c (str, g_ascii_toupper (raw_hex[ii]));
		if (ii + 1 < len)
			g_string_append_c (str, g_ascii_toupper (raw_hex[ii + 1]));
	}
	g_free (raw_hex);
	formatted = g_string_free (str, FALSE);

	return formatted;
}

/* Construit une description lisible en français des anomalies détectées sur
 * un certificat TLS (GTlsCertificateFlags).
 * Paramètres :
 *   errors : masque d'erreurs GTlsCertificateFlags
 * Retourne : chaîne nouvellement allouée décrivant les erreurs. */
static gchar *
format_tls_errors_fr (GTlsCertificateFlags errors)
{
	GString *desc = g_string_new ("");

	if (errors & G_TLS_CERTIFICATE_UNKNOWN_CA)
		g_string_append (desc, "• Autorité de certification inconnue ou certificat auto-signé\n");
	if (errors & G_TLS_CERTIFICATE_BAD_IDENTITY)
		g_string_append (desc, "• Le nom du serveur ne correspond pas au certificat\n");
	if (errors & G_TLS_CERTIFICATE_EXPIRED)
		g_string_append (desc, "• Le certificat a expiré\n");
	if (errors & G_TLS_CERTIFICATE_NOT_ACTIVATED)
		g_string_append (desc, "• Le certificat n'est pas encore activé\n");
	if (errors & G_TLS_CERTIFICATE_REVOKED)
		g_string_append (desc, "• Le certificat a été révoqué\n");
	if (errors & G_TLS_CERTIFICATE_INSECURE)
		g_string_append (desc, "• Algorithme cryptographique faible\n");
	if (desc->len == 0)
		g_string_append (desc, "• Certificat TLS non vérifié\n");

	if (desc->len > 0 && desc->str[desc->len - 1] == '\n')
		g_string_truncate (desc, desc->len - 1);

	return g_string_free (desc, FALSE);
}

/* Demande à l'interface utilisateur si un certificat TLS présentant des erreurs
 * de validation doit être refusé, accepté pour la session ou approuvé de façon
 * permanente.
 * Paramètres :
 *   session     : session Camel courante
 *   service     : service réseau concerné
 *   certificate : certificat TLS présenté par le serveur
 *   errors      : erreurs de validation TLS
 * Retourne : la décision CamelCertTrust choisie par l'utilisateur. */
static CamelCertTrust
evo_core_session_trust_prompt (CamelSession *session,
			       CamelService *service,
			       GTlsCertificate *certificate,
			       GTlsCertificateFlags errors)
{
	EvoCoreSession *self = EVO_CORE_SESSION (session);
	CamelSettings *settings;
	gchar *host = NULL, *fingerprint, *trust_id, *errors_desc;
	gchar *subject = NULL, *issuer = NULL;
	gpointer cached;
	gint decision = 0;

	if (!self->core || !certificate)
		return CAMEL_CERT_TRUST_NEVER;

	settings = service ? camel_service_ref_settings (service) : NULL;
	if (CAMEL_IS_NETWORK_SETTINGS (settings))
		host = camel_network_settings_dup_host (CAMEL_NETWORK_SETTINGS (settings));
	g_clear_object (&settings);

	if (!host || !*host) {
		g_free (host);
		host = g_strdup (service ? camel_service_get_display_name (service) : "serveur");
	}

	fingerprint = certificate_dup_sha256_fingerprint (certificate);
	trust_id = g_strdup_printf ("%s|%s", host, fingerprint);

	g_mutex_lock (&self->trust_lock);
	cached = g_hash_table_lookup (self->trusted_certs, trust_id);
	if (cached) {
		CamelCertTrust trust = (CamelCertTrust) GPOINTER_TO_INT (cached);
		g_mutex_unlock (&self->trust_lock);
		g_free (trust_id);
		g_free (fingerprint);
		g_free (host);
		return trust;
	}

	/* Si aucune demande n'est déjà affichée pour ce certificat, émet l'événement */
	if (!g_hash_table_contains (self->pending_trust, trust_id)) {
		JsonBuilder *builder;

		g_hash_table_insert (self->pending_trust, g_strdup (trust_id), GINT_TO_POINTER (0));
		g_mutex_unlock (&self->trust_lock);

		g_object_get (certificate,
			"subject-name", &subject,
			"issuer-name", &issuer,
			NULL);
		errors_desc = format_tls_errors_fr (errors);

		builder = core_event_builder ();
		builder_add_string (builder, "id", trust_id);
		builder_add_string (builder, "source_uid", service ? camel_service_get_uid (service) : "");
		builder_add_string (builder, "name", service ? camel_service_get_display_name (service) : host);
		builder_add_string (builder, "host", host);
		builder_add_string (builder, "fingerprint", fingerprint);
		builder_add_string (builder, "subject", subject ? subject : host);
		builder_add_string (builder, "issuer", issuer ? issuer : "(inconnu)");
		builder_add_string (builder, "errors", errors_desc);
		core_emit_builder (self->core, "tls-trust-required", builder);

		g_free (errors_desc);
		g_free (subject);
		g_free (issuer);

		g_mutex_lock (&self->trust_lock);
	}

	/* Attend la réponse de l'utilisateur (evo_core_respond_tls_trust) */
	while (!g_cancellable_is_cancelled (self->core->cancellable)) {
		decision = GPOINTER_TO_INT (g_hash_table_lookup (self->pending_trust, trust_id));
		if (decision != 0)
			break;
		g_cond_wait_until (&self->trust_cond, &self->trust_lock,
			g_get_monotonic_time () + 250 * G_TIME_SPAN_MILLISECOND);
	}

	g_hash_table_remove (self->pending_trust, trust_id);
	g_mutex_unlock (&self->trust_lock);

	g_free (trust_id);
	g_free (fingerprint);
	g_free (host);

	if (decision == 1)
		return CAMEL_CERT_TRUST_TEMPORARY;
	if (decision >= 2)
		return CAMEL_CERT_TRUST_FULLY;

	return CAMEL_CERT_TRUST_NEVER;
}

/* no filter rules yet: an empty driver */
static CamelFilterDriver *
evo_core_session_get_filter_driver (CamelSession *session,
				    const gchar *type,
				    CamelFolder *for_folder,
				    GError **error)
{
	return camel_filter_driver_new (session);
}

static void
evo_core_session_dispose (GObject *object)
{
	EvoCoreSession *self = EVO_CORE_SESSION (object);

	self->core = NULL;
	g_clear_object (&self->credentials_provider);
	g_clear_object (&self->registry);
	if (self->prompted_sources) {
		g_hash_table_destroy (self->prompted_sources);
		self->prompted_sources = NULL;
		g_mutex_clear (&self->prompted_lock);
	}
	if (self->trusted_certs) {
		g_hash_table_destroy (self->trusted_certs);
		self->trusted_certs = NULL;
	}
	if (self->pending_trust) {
		g_hash_table_destroy (self->pending_trust);
		self->pending_trust = NULL;
		g_mutex_clear (&self->trust_lock);
		g_cond_clear (&self->trust_cond);
	}
	g_clear_pointer (&self->trust_file, g_free);

	G_OBJECT_CLASS (evo_core_session_parent_class)->dispose (object);
}

static void
evo_core_session_class_init (EvoCoreSessionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	CamelSessionClass *session_class = CAMEL_SESSION_CLASS (klass);

	object_class->dispose = evo_core_session_dispose;

	session_class->add_service = evo_core_session_add_service;
	session_class->get_password = evo_core_session_get_password;
	session_class->authenticate_sync = evo_core_session_authenticate_sync;
	session_class->get_oauth2_access_token_sync = evo_core_session_get_oauth2_access_token_sync;
	session_class->trust_prompt = evo_core_session_trust_prompt;
	session_class->get_filter_driver = evo_core_session_get_filter_driver;
}

static void
evo_core_session_init (EvoCoreSession *self)
{
	g_mutex_init (&self->prompted_lock);
	self->prompted_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_mutex_init (&self->trust_lock);
	g_cond_init (&self->trust_cond);
	self->trusted_certs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->pending_trust = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

CamelSession *
evo_core_session_new (EvoCore *core,
		      ESourceRegistry *registry)
{
	EvoCoreSession *self;
	gchar *data_dir, *cache_dir;

	/* own folders: the GTK Evolution may run at the same time and its
	 * caches must not be shared */
	data_dir = g_build_filename (e_get_user_data_dir (), "evolution-windows", "mail", NULL);
	cache_dir = g_build_filename (e_get_user_cache_dir (), "evolution-windows", "mail", NULL);

	self = g_object_new (EVO_TYPE_CORE_SESSION,
		"user-data-dir", data_dir,
		"user-cache-dir", cache_dir,
		"online", TRUE,
		NULL);

	self->core = core;
	self->registry = g_object_ref (registry);
	self->credentials_provider = e_source_credentials_provider_new (registry);
	self->trust_file = g_build_filename (data_dir, "trusted-certs.ini", NULL);
	evo_core_session_load_trusted_certs (self);

	g_free (data_dir);
	g_free (cache_dir);

	return CAMEL_SESSION (self);
}

/* Transmet la décision de l'utilisateur pour une demande de confiance TLS en
 * attente (événement "tls-trust-required").
 * Paramètres :
 *   core           : instance du moteur evo-core
 *   trust_id       : identifiant de la demande ("host:port|sha256")
 *   trust_decision : 0 = refuser, 1 = accepter temporairement, 2 = toujours faire confiance */
void
evo_core_respond_tls_trust (EvoCore *core,
			    const char *trust_id,
			    int trust_decision)
{
	EvoCoreSession *session;

	g_return_if_fail (core != NULL);
	g_return_if_fail (trust_id != NULL && *trust_id != '\0');

	session = EVO_CORE_SESSION (core->session);

	g_mutex_lock (&session->trust_lock);
	if (trust_decision == 1) {
		g_hash_table_insert (session->trusted_certs, g_strdup (trust_id), GINT_TO_POINTER ((gint) CAMEL_CERT_TRUST_TEMPORARY));
		g_hash_table_insert (session->pending_trust, g_strdup (trust_id), GINT_TO_POINTER (1));
	} else if (trust_decision >= 2) {
		g_hash_table_insert (session->trusted_certs, g_strdup (trust_id), GINT_TO_POINTER ((gint) CAMEL_CERT_TRUST_FULLY));
		g_hash_table_insert (session->pending_trust, g_strdup (trust_id), GINT_TO_POINTER (2));
		evo_core_session_save_trusted_certs_locked (session);
	} else {
		g_hash_table_insert (session->pending_trust, g_strdup (trust_id), GINT_TO_POINTER (-1));
	}
	g_cond_broadcast (&session->trust_cond);
	g_mutex_unlock (&session->trust_lock);
}
