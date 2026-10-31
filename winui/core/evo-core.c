/*
 * evo-core: mail engine of the native Windows client, see evo-core.h.
 */

#include <stdio.h>
#include <string.h>

#include <glib/gstdio.h>
#include <camel/camel.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <libedataserver/libedataserver.h>
#include <libecal/libecal.h>
#include <libebook/libebook.h>

#include "evo-core.h"

struct _EvoCore {
	EvoCoreEventFunc event_func;
	gpointer event_data;

	GThread *thread;
	GMainContext *context;
	GMainLoop *loop;
	GThreadPool *pool;
	GCancellable *cancellable;

	ESourceRegistry *registry;
	CamelSession *session;

	/* opened folders, "<account>\x1F<full name>" ~> CamelFolder *;
	 * their "changed" signal becomes the "folder-changed" event */
	GHashTable *folders;
	GMutex folders_lock;
	GSource *refresh_source;

	/* startup state, protected by lock */
	GMutex lock;
	GCond cond;
	gboolean started;
	gchar *start_error;
};

static JsonBuilder *	core_event_builder	(void);
static void		builder_add_string	(JsonBuilder *builder,
						 const gchar *name,
						 const gchar *value);
static void		core_emit_builder	(EvoCore *core,
						 const gchar *event,
						 JsonBuilder *builder);

/* ------------------------------------------------------------------------ */
/* Session                                                                  */
/* ------------------------------------------------------------------------ */

/* CamelSession configuring its services from their ESource, and
 * authenticating them with the password saved by evolution-data-server
 * (Windows Credential Manager). */

#define EVO_TYPE_CORE_SESSION (evo_core_session_get_type ())
G_DECLARE_FINAL_TYPE (EvoCoreSession, evo_core_session, EVO, CORE_SESSION, CamelSession)

struct _EvoCoreSession {
	CamelSession parent;

	EvoCore *core;
	ESourceRegistry *registry;
	ESourceCredentialsProvider *credentials_provider;
	GHashTable *prompted_sources;
	GMutex prompted_lock;

	/* Gestion des certificats TLS approuvés ou en attente de décision */
	GHashTable *trusted_certs;
	GHashTable *pending_trust;
	GMutex trust_lock;
	GCond trust_cond;
	gchar *trust_file;
};

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

	/* mechanisms without password get one try */
	if (authtype && !authtype->need_password)
		return camel_service_authenticate_sync (service, mechanism, cancellable, error) == CAMEL_AUTHENTICATION_ACCEPTED;

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

static CamelSession *
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

/* ------------------------------------------------------------------------ */
/* Log                                                                      */
/* ------------------------------------------------------------------------ */

/* Messages and warnings of the engine and of Camel/EDS go to
 * %LOCALAPPDATA%\EvolutionWin\evo-core.log (a GUI application has no
 * console); debug messages only with EVO_CORE_DEBUG=1. */

static FILE *log_file;
G_LOCK_DEFINE_STATIC (log_file);

static GLogWriterOutput
core_log_writer (GLogLevelFlags log_level,
		 const GLogField *fields,
		 gsize n_fields,
		 gpointer user_data)
{
	const gchar *domain = NULL, *message = NULL;
	GDateTime *now;
	gchar *stamp;
	gsize ii;

	if ((log_level & G_LOG_LEVEL_DEBUG) && g_strcmp0 (g_getenv ("EVO_CORE_DEBUG"), "1") != 0)
		return G_LOG_WRITER_HANDLED;

	for (ii = 0; ii < n_fields; ii++) {
		if (g_strcmp0 (fields[ii].key, "GLIB_DOMAIN") == 0)
			domain = fields[ii].value;
		else if (g_strcmp0 (fields[ii].key, "MESSAGE") == 0)
			message = fields[ii].value;
	}

	now = g_date_time_new_now_local ();
	stamp = g_date_time_format (now, "%Y-%m-%d %H:%M:%S");

	G_LOCK (log_file);
	if (log_file) {
		fprintf (log_file, "%s [%s] %s%s\n", stamp, domain ? domain : "-",
			(log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL)) ? "CRITICAL: " :
			(log_level & G_LOG_LEVEL_WARNING) ? "WARNING: " :
			(log_level & G_LOG_LEVEL_DEBUG) ? "debug: " : "",
			message ? message : "");
		fflush (log_file);
	}
	G_UNLOCK (log_file);

	g_free (stamp);
	g_date_time_unref (now);

	return G_LOG_WRITER_HANDLED;
}

static void
core_log_open (void)
{
	const gchar *local_app_data = g_getenv ("LOCALAPPDATA");
	gchar *dir, *path;

	if (log_file || !local_app_data)
		return;

	dir = g_build_filename (local_app_data, "EvolutionWin", NULL);
	g_mkdir_with_parents (dir, 0700);
	path = g_build_filename (dir, "evo-core.log", NULL);

	/* the previous session is kept as .old */
	{
		gchar *old = g_strconcat (path, ".old", NULL);
		g_remove (old);
		g_rename (path, old);
		g_free (old);
	}

	log_file = g_fopen (path, "w");
	if (log_file)
		g_log_set_writer_func (core_log_writer, NULL, NULL);

	/* CAMEL_DEBUG (e.g. "imapx") makes Camel print protocol traces on
	 * stdout, which a GUI application does not have: into a file */
	if (g_getenv ("CAMEL_DEBUG") && *g_getenv ("CAMEL_DEBUG") && _fileno (stdout) < 0) {
		gchar *trace = g_build_filename (dir, "imap-trace.log", NULL);
		gunichar2 *wtrace = g_utf8_to_utf16 (trace, -1, NULL, NULL, NULL);
		FILE *redirected = NULL;

		if (wtrace && _wfreopen_s (&redirected, (const wchar_t *) wtrace, L"w", stdout) == 0)
			setvbuf (stdout, NULL, _IONBF, 0);

		g_free (wtrace);
		g_free (trace);
	}

	g_free (path);
	g_free (dir);
}

/* ------------------------------------------------------------------------ */
/* Core                                                                     */
/* ------------------------------------------------------------------------ */

static gchar *
json_from_builder (JsonBuilder *builder)
{
	JsonNode *root = json_builder_get_root (builder);
	gchar *json = json_to_string (root, FALSE);

	json_node_unref (root);

	return json;
}

/* emits event with the object built so far, and frees the builder */
static void
core_emit_builder (EvoCore *core,
		   const gchar *event,
		   JsonBuilder *builder)
{
	gchar *json;

	json_builder_end_object (builder);
	json = json_from_builder (builder);

	if (core->event_func)
		core->event_func (event, json, core->event_data);

	g_free (json);
	g_object_unref (builder);
}

static JsonBuilder *
core_event_builder (void)
{
	JsonBuilder *builder = json_builder_new ();

	json_builder_begin_object (builder);

	return builder;
}

static void
builder_add_string (JsonBuilder *builder,
		    const gchar *name,
		    const gchar *value)
{
	json_builder_set_member_name (builder, name);
	json_builder_add_string_value (builder, value ? value : "");
}

static void
core_emit (EvoCore *core,
	   const gchar *event,
	   const gchar *message)
{
	JsonBuilder *builder = core_event_builder ();

	if (message)
		builder_add_string (builder, "message", message);

	core_emit_builder (core, event, builder);
}

static void
core_add_stores (EvoCore *core)
{
	GList *sources, *link;

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_MAIL_ACCOUNT);

	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;
		ESourceBackend *extension;
		const gchar *backend_name;
		CamelService *service;
		GError *error = NULL;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
		backend_name = e_source_backend_get_backend_name (extension);

		/* search folders need Evolution's vfolder rules, news feeds its
		 * RSS plug-in */
		if (!backend_name || g_strcmp0 (backend_name, "none") == 0 ||
		    g_strcmp0 (backend_name, "vfolder") == 0 ||
		    g_strcmp0 (backend_name, "rss") == 0)
			continue;

		service = camel_session_add_service (core->session, e_source_get_uid (source),
			backend_name, CAMEL_PROVIDER_STORE, &error);

		if (error) {
			g_warning ("Cannot add account “%s” (%s): %s",
				e_source_get_display_name (source), backend_name, error->message);
			g_clear_error (&error);
		}

		g_clear_object (&service);
	}

	g_list_free_full (sources, g_object_unref);

	/* outgoing servers (SMTP), referenced by the identities */
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_MAIL_TRANSPORT);

	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;
		const gchar *backend_name;
		CamelService *service;
		GError *error = NULL;

		backend_name = e_source_backend_get_backend_name (
			e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_TRANSPORT));

		/* no sendmail on Windows */
		if (!backend_name || g_strcmp0 (backend_name, "none") == 0 ||
		    g_strcmp0 (backend_name, "sendmail") == 0)
			continue;

		service = camel_session_add_service (core->session, e_source_get_uid (source),
			backend_name, CAMEL_PROVIDER_TRANSPORT, &error);

		if (error) {
			g_warning ("Cannot add outgoing server “%s” (%s): %s",
				e_source_get_display_name (source), backend_name, error->message);
			g_clear_error (&error);
		}

		g_clear_object (&service);
	}

	g_list_free_full (sources, g_object_unref);
}

#define REFRESH_INTERVAL_SECONDS (5 * 60)

static gboolean core_refresh_timeout_cb (gpointer user_data);

static gpointer
core_thread_func (gpointer user_data)
{
	EvoCore *core = user_data;
	GError *error = NULL;

	g_main_context_push_thread_default (core->context);

	camel_init (e_get_user_data_dir (), TRUE);
	e_source_camel_register_types ();

	core_emit (core, "status", "Connecting to evolution-data-server");

	/* starts evolution-source-registry when needed */
	core->registry = e_source_registry_new_sync (core->cancellable, &error);

	if (core->registry) {
		core->session = evo_core_session_new (core, core->registry);
		core_add_stores (core);
	}

	g_mutex_lock (&core->lock);
	core->started = TRUE;
	if (error)
		core->start_error = g_strdup (error->message);
	g_cond_broadcast (&core->cond);
	g_mutex_unlock (&core->lock);

	if (error) {
		core_emit (core, "error", error->message);
		g_clear_error (&error);
	} else {
		core_emit (core, "ready", NULL);

		/* new mail: now, then periodically */
		evo_core_refresh (core);

		core->refresh_source = g_timeout_source_new_seconds (REFRESH_INTERVAL_SECONDS);
		g_source_set_callback (core->refresh_source, core_refresh_timeout_cb, core, NULL);
		g_source_attach (core->refresh_source, core->context);
	}

	g_main_loop_run (core->loop);

	if (core->refresh_source) {
		g_source_destroy (core->refresh_source);
		g_clear_pointer (&core->refresh_source, g_source_unref);
	}

	g_main_context_pop_thread_default (core->context);

	return NULL;
}

/* waits for the startup; FALSE and error_message when it failed */
static gboolean
core_wait_started (EvoCore *core,
		   gchar **error_message)
{
	gboolean success;

	g_mutex_lock (&core->lock);
	while (!core->started)
		g_cond_wait (&core->cond, &core->lock);
	success = core->start_error == NULL;
	if (!success)
		*error_message = g_strdup (core->start_error);
	g_mutex_unlock (&core->lock);

	return success;
}

/* ------------------------------------------------------------------------ */
/* Requests                                                                 */
/* ------------------------------------------------------------------------ */

typedef gchar * (* RequestFunc) (EvoCore *core,
				 gchar **args,
				 GError **error);

typedef struct {
	RequestFunc func;
	gchar **args;
	EvoCoreResultFunc result_func;
	gpointer user_data;
} Request;

static void
core_request_run (gpointer data,
		  gpointer user_data)
{
	Request *request = data;
	EvoCore *core = user_data;
	gchar *json = NULL;
	gchar *start_error = NULL;
	GError *error = NULL;

	if (!core_wait_started (core, &start_error)) {
		request->result_func (NULL, start_error, request->user_data);
		g_free (start_error);
	} else if (g_cancellable_is_cancelled (core->cancellable)) {
		/* shutting down: no callback */
	} else {
		json = request->func (core, request->args, &error);

		if (json)
			request->result_func (json, NULL, request->user_data);
		else
			request->result_func (NULL, error ? error->message : "Unknown error", request->user_data);
	}

	g_clear_error (&error);
	g_free (json);
	g_strfreev (request->args);
	g_free (request);
}

static void
core_submit (EvoCore *core,
	     RequestFunc func,
	     gchar **args,
	     EvoCoreResultFunc result_func,
	     gpointer user_data)
{
	Request *request;

	g_return_if_fail (core != NULL);
	g_return_if_fail (result_func != NULL);

	request = g_new0 (Request, 1);
	request->func = func;
	request->args = args;
	request->result_func = result_func;
	request->user_data = user_data;

	g_thread_pool_push (core->pool, request, NULL);
}

static CamelStore *
core_ref_store (EvoCore *core,
		const gchar *account_uid,
		GError **error)
{
	CamelService *service;

	service = camel_session_ref_service (core->session, account_uid);

	if (!CAMEL_IS_STORE (service)) {
		g_clear_object (&service);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Unknown account “%s”", account_uid);
		return NULL;
	}

	return CAMEL_STORE (service);
}

static gboolean
core_ensure_connected (EvoCore *core,
		       CamelStore *store,
		       GError **error)
{
	if (!CAMEL_IS_NETWORK_SERVICE (store))
		return TRUE;

	/* offline-capable stores (IMAP) refuse to work until set online;
	 * this also connects them */
	if (CAMEL_IS_OFFLINE_STORE (store) && !camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		return camel_offline_store_set_online_sync (CAMEL_OFFLINE_STORE (store), TRUE, core->cancellable, error);

	if (camel_service_get_connection_status (CAMEL_SERVICE (store)) == CAMEL_SERVICE_CONNECTED)
		return TRUE;

	return camel_service_connect_sync (CAMEL_SERVICE (store), core->cancellable, error);
}

/* --- accounts --- */

static gchar *
core_account_address (EvoCore *core,
		      const gchar *uid)
{
	ESource *source, *identity = NULL;
	gchar *address = NULL;

	source = e_source_registry_ref_source (core->registry, uid);

	if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		ESourceMailAccount *extension;
		gchar *identity_uid;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
		identity_uid = e_source_mail_account_dup_identity_uid (extension);
		if (identity_uid)
			identity = e_source_registry_ref_source (core->registry, identity_uid);
		g_free (identity_uid);
	}

	if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY))
		address = e_source_mail_identity_dup_address (
			e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY));

	g_clear_object (&identity);
	g_clear_object (&source);

	return address;
}

static gint
compare_stores (gconstpointer a,
		gconstpointer b)
{
	CamelService *sa = *(CamelService **) a, *sb = *(CamelService **) b;
	gboolean la = !CAMEL_IS_NETWORK_SERVICE (sa), lb = !CAMEL_IS_NETWORK_SERVICE (sb);

	/* network accounts first, then local folders */
	if (la != lb)
		return la ? 1 : -1;

	return g_utf8_collate (camel_service_get_display_name (sa), camel_service_get_display_name (sb));
}

static gchar *
core_list_accounts_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	GList *services, *link;
	GPtrArray *stores;
	JsonBuilder *builder;
	gchar *json;
	guint ii;

	services = camel_session_list_services (core->session);
	stores = g_ptr_array_new ();

	for (link = services; link; link = g_list_next (link)) {
		if (CAMEL_IS_STORE (link->data))
			g_ptr_array_add (stores, link->data);
	}

	g_ptr_array_sort (stores, compare_stores);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (ii = 0; ii < stores->len; ii++) {
		CamelService *service = stores->pdata[ii];
		CamelProvider *provider = camel_service_get_provider (service);
		gchar *address = core_account_address (core, camel_service_get_uid (service));

		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "uid");
		json_builder_add_string_value (builder, camel_service_get_uid (service));
		json_builder_set_member_name (builder, "name");
		json_builder_add_string_value (builder, camel_service_get_display_name (service));
		json_builder_set_member_name (builder, "address");
		json_builder_add_string_value (builder, address ? address : "");
		json_builder_set_member_name (builder, "protocol");
		json_builder_add_string_value (builder, provider ? provider->protocol : "");
		json_builder_set_member_name (builder, "local");
		json_builder_add_boolean_value (builder, !CAMEL_IS_NETWORK_SERVICE (service));
		json_builder_end_object (builder);

		g_free (address);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_ptr_array_unref (stores);
	g_list_free_full (services, g_object_unref);

	return json;
}

/* --- folders --- */

static const gchar *
folder_type_name (CamelFolderInfoFlags flags)
{
	switch (flags & CAMEL_FOLDER_TYPE_MASK) {
	case CAMEL_FOLDER_TYPE_INBOX:
		return "inbox";
	case CAMEL_FOLDER_TYPE_OUTBOX:
		return "outbox";
	case CAMEL_FOLDER_TYPE_TRASH:
		return "trash";
	case CAMEL_FOLDER_TYPE_JUNK:
		return "junk";
	case CAMEL_FOLDER_TYPE_SENT:
		return "sent";
	case CAMEL_FOLDER_TYPE_DRAFTS:
		return "drafts";
	case CAMEL_FOLDER_TYPE_ARCHIVE:
		return "archive";
	default:
		return "";
	}
}

static void
add_folder_infos (JsonBuilder *builder,
		  CamelFolderInfo *fi)
{
	json_builder_begin_array (builder);

	for (; fi; fi = fi->next) {
		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "full_name");
		json_builder_add_string_value (builder, fi->full_name);
		json_builder_set_member_name (builder, "name");
		json_builder_add_string_value (builder, fi->display_name);
		json_builder_set_member_name (builder, "unread");
		json_builder_add_int_value (builder, MAX (fi->unread, 0));
		json_builder_set_member_name (builder, "total");
		json_builder_add_int_value (builder, MAX (fi->total, 0));
		json_builder_set_member_name (builder, "type");
		json_builder_add_string_value (builder, folder_type_name (fi->flags));
		json_builder_set_member_name (builder, "selectable");
		json_builder_add_boolean_value (builder, (fi->flags & CAMEL_FOLDER_NOSELECT) == 0);
		json_builder_set_member_name (builder, "children");
		add_folder_infos (builder, fi->child);
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
}

static gchar *
core_list_folders_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	CamelStore *store;
	CamelFolderInfo *fi;
	JsonBuilder *builder;
	gchar *json = NULL;

	store = core_ref_store (core, args[0], error);
	if (!store)
		return NULL;

	if (core_ensure_connected (core, store, error)) {
		fi = camel_store_get_folder_info_sync (store, NULL,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, error);

		if (fi || !(error && *error)) {
			builder = json_builder_new ();
			add_folder_infos (builder, fi);
			json = json_from_builder (builder);
			g_object_unref (builder);
		}

		camel_folder_info_free (fi);
	}

	g_object_unref (store);

	return json;
}

/* --- messages --- */

static gint
compare_infos_by_date (gconstpointer a,
		       gconstpointer b)
{
	CamelMessageInfo *ia = *(CamelMessageInfo **) a, *ib = *(CamelMessageInfo **) b;
	gint64 da = camel_message_info_get_date_received (ia);
	gint64 db = camel_message_info_get_date_received (ib);

	if (da <= 0)
		da = camel_message_info_get_date_sent (ia);
	if (db <= 0)
		db = camel_message_info_get_date_sent (ib);

	/* most recent first */
	return da < db ? 1 : (da > db ? -1 : 0);
}

/* CamelFolder::changed, emitted in the core thread: "folder-changed" */
static void
core_folder_changed_cb (CamelFolder *folder,
			CamelFolderChangeInfo *changes,
			gpointer user_data)
{
	EvoCore *core = user_data;
	CamelStore *store = camel_folder_get_parent_store (folder);
	CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);
	JsonBuilder *builder;

	if (!store || g_cancellable_is_cancelled (core->cancellable))
		return;

	g_message ("folder “%s” changed: +%u -%u ~%u, %u unread, %d messages",
		camel_folder_get_full_name (folder),
		changes && changes->uid_added ? changes->uid_added->len : 0,
		changes && changes->uid_removed ? changes->uid_removed->len : 0,
		changes && changes->uid_changed ? changes->uid_changed->len : 0,
		summary ? camel_folder_summary_get_unread_count (summary) : 0,
		camel_folder_get_message_count (folder));

	builder = core_event_builder ();
	builder_add_string (builder, "account", camel_service_get_uid (CAMEL_SERVICE (store)));
	builder_add_string (builder, "folder", camel_folder_get_full_name (folder));
	json_builder_set_member_name (builder, "unread");
	json_builder_add_int_value (builder, summary ? camel_folder_summary_get_unread_count (summary) : 0);
	json_builder_set_member_name (builder, "total");
	json_builder_add_int_value (builder, camel_folder_get_message_count (folder));
	json_builder_set_member_name (builder, "added");
	json_builder_add_int_value (builder, changes && changes->uid_added ? changes->uid_added->len : 0);
	json_builder_set_member_name (builder, "removed");
	json_builder_add_int_value (builder, changes && changes->uid_removed ? changes->uid_removed->len : 0);
	json_builder_set_member_name (builder, "changed");
	json_builder_add_int_value (builder, changes && changes->uid_changed ? changes->uid_changed->len : 0);

	core_emit_builder (core, "folder-changed", builder);

	/* Émet "new-mail" lorsque de nouveaux messages non lus arrivent dans la
	 * boîte de réception après la synchronisation initiale */
	if (g_object_get_data (G_OBJECT (folder), "evo-initial-sync-done") != NULL &&
	    changes && changes->uid_added && changes->uid_added->len > 0 &&
	    ((camel_folder_get_flags (folder) & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX ||
	     g_ascii_strcasecmp (camel_folder_get_full_name (folder), "INBOX") == 0)) {
		guint ii, unread_added = 0;
		CamelMessageInfo *newest = NULL;
		gint64 newest_date = -1;

		for (ii = 0; ii < changes->uid_added->len; ii++) {
			CamelMessageInfo *info = camel_folder_get_message_info (folder, changes->uid_added->pdata[ii]);
			if (!info)
				continue;

			guint32 flags = camel_message_info_get_flags (info);
			if (!(flags & CAMEL_MESSAGE_SEEN) && !(flags & CAMEL_MESSAGE_DELETED)) {
				gint64 d = camel_message_info_get_date_received (info);
				if (d <= 0)
					d = camel_message_info_get_date_sent (info);
				unread_added++;
				if (!newest || d >= newest_date) {
					g_clear_object (&newest);
					newest = g_object_ref (info);
					newest_date = d;
				}
			}
			g_object_unref (info);
		}

		if (unread_added > 0 && newest) {
			JsonBuilder *mail_builder = core_event_builder ();

			builder_add_string (mail_builder, "account", camel_service_get_uid (CAMEL_SERVICE (store)));
			builder_add_string (mail_builder, "folder", camel_folder_get_full_name (folder));
			json_builder_set_member_name (mail_builder, "count");
			json_builder_add_int_value (mail_builder, unread_added);
			builder_add_string (mail_builder, "from", camel_message_info_get_from (newest));
			builder_add_string (mail_builder, "subject", camel_message_info_get_subject (newest));
			core_emit_builder (core, "new-mail", mail_builder);
		}

		g_clear_object (&newest);
	}
}

static gchar *
folder_key (const gchar *account_uid,
	    const gchar *folder_name)
{
	return g_strconcat (account_uid, "\x1F", folder_name, NULL);
}

/* keeps the folder open (cache) and follows its changes */
static CamelFolder *
core_track_folder (EvoCore *core,
		   const gchar *account_uid,
		   CamelFolder *folder)
{
	gchar *key = folder_key (account_uid, camel_folder_get_full_name (folder));
	CamelFolder *tracked;

	g_mutex_lock (&core->folders_lock);

	tracked = g_hash_table_lookup (core->folders, key);
	if (!tracked) {
		tracked = g_object_ref (folder);
		g_hash_table_insert (core->folders, key, tracked);
		g_signal_connect (tracked, "changed", G_CALLBACK (core_folder_changed_cb), core);
		key = NULL;
	}
	g_object_ref (tracked);

	g_mutex_unlock (&core->folders_lock);

	g_free (key);

	return tracked;
}

static CamelFolder *
core_ref_folder (EvoCore *core,
		 const gchar *account_uid,
		 const gchar *folder_name,
		 GError **error)
{
	CamelStore *store;
	CamelFolder *folder = NULL, *tracked = NULL;
	gchar *key = folder_key (account_uid, folder_name);

	g_mutex_lock (&core->folders_lock);
	folder = g_hash_table_lookup (core->folders, key);
	if (folder)
		g_object_ref (folder);
	g_mutex_unlock (&core->folders_lock);
	g_free (key);

	if (folder)
		return folder;

	store = core_ref_store (core, account_uid, error);
	if (!store)
		return NULL;

	if (core_ensure_connected (core, store, error))
		folder = camel_store_get_folder_sync (store, folder_name, 0, core->cancellable, error);

	if (folder) {
		tracked = core_track_folder (core, account_uid, folder);
		g_object_unref (folder);
	}

	g_object_unref (store);

	return tracked;
}

/* Construit le tableau JSON des résumés de messages triés du plus récent au
 * plus ancien.
 * Paramètres :
 *   infos     : tableau de CamelMessageInfo * (modifié sur place par le tri)
 *   max_count : nombre maximal d'éléments à sérialiser (0 pour tous)
 * Retourne : chaîne JSON nouvellement allouée. */
static gchar *
json_from_message_infos (GPtrArray *infos,
			 guint max_count)
{
	JsonBuilder *builder;
	gchar *json;
	guint ii;

	g_ptr_array_sort (infos, compare_infos_by_date);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (ii = 0; ii < infos->len && (max_count == 0 || ii < max_count); ii++) {
		CamelMessageInfo *info = infos->pdata[ii];
		guint32 flags = camel_message_info_get_flags (info);
		gint64 date = camel_message_info_get_date_received (info);

		if (date <= 0)
			date = camel_message_info_get_date_sent (info);

		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "uid");
		json_builder_add_string_value (builder, camel_message_info_get_uid (info));
		json_builder_set_member_name (builder, "subject");
		json_builder_add_string_value (builder, camel_message_info_get_subject (info) ? camel_message_info_get_subject (info) : "");
		json_builder_set_member_name (builder, "from");
		json_builder_add_string_value (builder, camel_message_info_get_from (info) ? camel_message_info_get_from (info) : "");
		json_builder_set_member_name (builder, "date");
		json_builder_add_int_value (builder, date);
		json_builder_set_member_name (builder, "seen");
		json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_SEEN) != 0);
		json_builder_set_member_name (builder, "flagged");
		json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_FLAGGED) != 0);
		json_builder_set_member_name (builder, "attachments");
		json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_ATTACHMENTS) != 0);
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);

	return json;
}

static gchar *
core_list_messages_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	CamelFolder *folder;
	GPtrArray *uids, *infos;
	GError *refresh_error = NULL;
	guint max_count, ii;
	gchar *json;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	/* new messages from the server; the cached list is used on failure */
	if (!camel_folder_refresh_info_sync (folder, core->cancellable, &refresh_error)) {
		g_warning ("Cannot refresh “%s”: %s", args[1], refresh_error ? refresh_error->message : "?");
		g_clear_error (&refresh_error);
	}
	g_object_set_data (G_OBJECT (folder), "evo-initial-sync-done", GINT_TO_POINTER (1));

	max_count = (guint) g_ascii_strtoull (args[2], NULL, 10);

	uids = camel_folder_dup_uids (folder);
	infos = g_ptr_array_new_with_free_func (g_object_unref);

	for (ii = 0; uids && ii < uids->len; ii++) {
		CamelMessageInfo *info = camel_folder_get_message_info (folder, uids->pdata[ii]);

		if (info && !(camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED))
			g_ptr_array_add (infos, info);
		else
			g_clear_object (&info);
	}

	json = json_from_message_infos (infos, max_count);

	g_ptr_array_unref (infos);
	if (uids)
		g_ptr_array_unref (uids);
	g_object_unref (folder);

	return json;
}

/* Vérifie si un résumé de message correspond à tous les termes de recherche
 * (insensible à la casse et aux accents sur l'objet, l'expéditeur et les
 * destinataires).
 * Paramètres :
 *   info  : résumé CamelMessageInfo à inspecter
 *   query : requête utilisateur brute
 *   words : tableau de mots de la requête mis en minuscules (g_utf8_casefold)
 * Retourne : TRUE si le message correspond à la requête, FALSE sinon. */
static gboolean
message_info_matches_query (CamelMessageInfo *info,
			    const gchar *query,
			    gchar **words)
{
	const gchar *fields[4];
	gchar *combined, *folded;
	gboolean match = TRUE;
	guint ii;

	fields[0] = camel_message_info_get_subject (info);
	fields[1] = camel_message_info_get_from (info);
	fields[2] = camel_message_info_get_to (info);
	fields[3] = camel_message_info_get_cc (info);

	combined = g_strjoin (" ",
		fields[0] ? fields[0] : "",
		fields[1] ? fields[1] : "",
		fields[2] ? fields[2] : "",
		fields[3] ? fields[3] : "",
		NULL);

	/* g_str_match_string gère les équivalences sans accents (ex. "ecole" -> "École") */
	if (g_str_match_string (query, combined, TRUE)) {
		g_free (combined);
		return TRUE;
	}

	/* Recherche de sous-chaînes quelconques insensible à la casse */
	folded = g_utf8_casefold (combined, -1);
	for (ii = 0; words && words[ii]; ii++) {
		if (*words[ii] && !strstr (folded, words[ii])) {
			match = FALSE;
			break;
		}
	}

	g_free (folded);
	g_free (combined);

	return match;
}

/* Exécute une recherche de messages dans un dossier (en-têtes locaux et
 * recherche plein texte Camel sur le corps des messages).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] query, [3] max_count
 *   error : emplacement d'erreur GError
 * Retourne : tableau JSON des messages correspondants, ou NULL en cas d'erreur. */
static gchar *
core_search_messages_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	CamelFolder *folder;
	const gchar *query = args[2];
	GPtrArray *uids, *infos, *body_uids = NULL;
	GHashTable *body_set = NULL;
	gchar **raw_words = NULL, **words = NULL;
	GString *sexp = NULL;
	guint max_count, ii, n_words = 0;
	gchar *json;

	if (!query || !*query) {
		gchar *list_args[] = { args[0], args[1], args[3], NULL };
		return core_list_messages_run (core, list_args, error);
	}

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	max_count = (guint) g_ascii_strtoull (args[3], NULL, 10);

	raw_words = g_strsplit_set (query, " \t\r\n", -1);
	if (raw_words) {
		words = g_new0 (gchar *, g_strv_length (raw_words) + 1);
		sexp = g_string_new ("(match-all (and (not (system-flag \"deleted\"))");

		for (ii = 0; raw_words[ii]; ii++) {
			if (!*raw_words[ii])
				continue;

			words[n_words++] = g_utf8_casefold (raw_words[ii], -1);

			g_string_append (sexp, " (body-contains ");
			camel_sexp_encode_string (sexp, raw_words[ii]);
			g_string_append_c (sexp, ')');
		}

		g_string_append (sexp, "))");
	}

	if (n_words > 0 && camel_folder_search_sync (folder, sexp->str, &body_uids, core->cancellable, NULL) && body_uids) {
		body_set = g_hash_table_new (g_str_hash, g_str_equal);
		for (ii = 0; ii < body_uids->len; ii++)
			g_hash_table_add (body_set, body_uids->pdata[ii]);
	}

	uids = camel_folder_dup_uids (folder);
	infos = g_ptr_array_new_with_free_func (g_object_unref);

	for (ii = 0; uids && ii < uids->len; ii++) {
		const gchar *uid = uids->pdata[ii];
		CamelMessageInfo *info = camel_folder_get_message_info (folder, uid);

		if (info && !(camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED) &&
		    ((body_set && g_hash_table_contains (body_set, uid)) ||
		     message_info_matches_query (info, query, words))) {
			g_ptr_array_add (infos, info);
		} else {
			g_clear_object (&info);
		}
	}

	json = json_from_message_infos (infos, max_count);

	if (body_set)
		g_hash_table_destroy (body_set);
	if (body_uids)
		g_ptr_array_unref (body_uids);
	if (sexp)
		g_string_free (sexp, TRUE);
	g_strfreev (words);
	g_strfreev (raw_words);
	g_ptr_array_unref (infos);
	if (uids)
		g_ptr_array_unref (uids);
	g_object_unref (folder);

	return json;
}

/* --- actions sur les messages (drapeaux, suppression, déplacement) --- */

/* Modifie un indicateur ("seen" ou "flagged") sur un message et synchronise le
 * message avec le serveur.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] message_uid,
 *           [3] flag_name ("seen" ou "flagged"), [4] "1" ou "0"
 *   error : emplacement d'erreur GError
 * Retourne : objet JSON {"seen": ..., "flagged": ...}, ou NULL en cas d'erreur. */
static gchar *
core_set_message_flags_run (EvoCore *core,
			    gchar **args,
			    GError **error)
{
	CamelFolder *folder;
	guint32 mask = 0, flags;
	gboolean set_value;
	JsonBuilder *builder;
	gchar *json;

	if (g_strcmp0 (args[3], "seen") == 0) {
		mask = CAMEL_MESSAGE_SEEN;
	} else if (g_strcmp0 (args[3], "flagged") == 0) {
		mask = CAMEL_MESSAGE_FLAGGED;
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Unknown message flag “%s”", args[3]);
		return NULL;
	}

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	set_value = g_strcmp0 (args[4], "0") != 0;
	camel_folder_set_message_flags (folder, args[2], mask, set_value ? mask : 0);

	/* Synchronise le changement d'indicateur avec le serveur sans bloquer
	 * en cas d'erreur réseau temporaire (le cache local le conserve) */
	camel_folder_synchronize_message_sync (folder, args[2], core->cancellable, NULL);

	flags = camel_folder_get_message_flags (folder, args[2]);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "seen");
	json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_SEEN) != 0);
	json_builder_set_member_name (builder, "flagged");
	json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_FLAGGED) != 0);
	json_builder_end_object (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_object_unref (folder);

	return json;
}

/* Recherche récursivement dans l'arbre CamelFolderInfo le nom complet du
 * dossier de corbeille réel du compte (CAMEL_FOLDER_TYPE_TRASH).
 * Paramètres :
 *   fi : premier nœud de la liste CamelFolderInfo à parcourir
 * Retourne : le chemin complet du dossier corbeille (nouvellement alloué),
 *            ou NULL si aucun dossier corbeille n'est trouvé. */
static gchar *
find_trash_folder_name (CamelFolderInfo *fi)
{
	for (; fi; fi = fi->next) {
		if ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_TRASH &&
		    fi->full_name && g_strcmp0 (fi->full_name, CAMEL_VTRASH_NAME) != 0)
			return g_strdup (fi->full_name);

		if (fi->child) {
			gchar *found = find_trash_folder_name (fi->child);
			if (found)
				return found;
		}
	}

	return NULL;
}

/* Supprime un message : s'il ne se trouve pas déjà dans la corbeille du compte
 * et que celle-ci existe, l'y transfère ; sinon le marque comme supprimé et
 * purge le dossier.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] message_uid
 *   error : emplacement d'erreur GError
 * Retourne : {"trashed": true|false}, ou NULL en cas d'erreur. */
static gchar *
core_delete_message_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	CamelFolder *folder, *trash = NULL;
	CamelStore *store;
	gchar *trash_name = NULL;
	gboolean trashed = FALSE;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	store = camel_folder_get_parent_store (folder);
	if (store) {
		CamelFolderInfo *fi = camel_store_get_folder_info_sync (store, NULL,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST,
			core->cancellable, NULL);

		if (fi) {
			trash_name = find_trash_folder_name (fi);
			camel_folder_info_free (fi);
		}
	}

	if (trash_name && g_strcmp0 (args[1], trash_name) != 0)
		trash = core_ref_folder (core, args[0], trash_name, NULL);

	if (trash && trash != folder) {
		GPtrArray *uids = g_ptr_array_new ();

		g_ptr_array_add (uids, args[2]);
		camel_folder_set_message_flags (folder, args[2], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

		if (!camel_folder_transfer_messages_to_sync (folder, uids, trash, TRUE, NULL, core->cancellable, error)) {
			g_ptr_array_unref (uids);
			g_object_unref (trash);
			g_free (trash_name);
			g_object_unref (folder);
			return NULL;
		}

		g_ptr_array_unref (uids);
		camel_folder_synchronize_sync (folder, TRUE, core->cancellable, NULL);
		camel_folder_synchronize_sync (trash, FALSE, core->cancellable, NULL);
		trashed = TRUE;
	} else {
		camel_folder_set_message_flags (folder, args[2],
			CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
			CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);

		if (!camel_folder_synchronize_sync (folder, TRUE, core->cancellable, error)) {
			g_clear_object (&trash);
			g_free (trash_name);
			g_object_unref (folder);
			return NULL;
		}
	}

	{
		JsonBuilder *builder = core_event_builder ();

		builder_add_string (builder, "account", args[0]);
		core_emit_builder (core, "folders-updated", builder);
	}

	g_clear_object (&trash);
	g_free (trash_name);
	g_object_unref (folder);

	return g_strdup (trashed ? "{\"trashed\":true}" : "{\"trashed\":false}");
}

/* Déplace un message d'un dossier source vers un dossier de destination, puis
 * synchronise les deux dossiers et notifie l'interface.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] src_account_uid, [1] src_folder_full_name, [2] message_uid,
 *           [3] dest_account_uid, [4] dest_folder_full_name
 *   error : emplacement d'erreur GError
 * Retourne : "{}" en cas de succès, ou NULL en cas d'erreur. */
static gchar *
core_move_message_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	CamelFolder *src, *dest;
	GPtrArray *uids;
	gboolean ok;

	src = core_ref_folder (core, args[0], args[1], error);
	if (!src)
		return NULL;

	dest = core_ref_folder (core, args[3], args[4], error);
	if (!dest) {
		g_object_unref (src);
		return NULL;
	}

	if (src == dest) {
		g_object_unref (dest);
		g_object_unref (src);
		return g_strdup ("{}");
	}

	uids = g_ptr_array_new ();
	g_ptr_array_add (uids, args[2]);

	ok = camel_folder_transfer_messages_to_sync (src, uids, dest, TRUE, NULL, core->cancellable, error);
	g_ptr_array_unref (uids);

	if (ok) {
		camel_folder_set_message_flags (src, args[2], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
		camel_folder_synchronize_sync (src, TRUE, core->cancellable, NULL);
		camel_folder_synchronize_sync (dest, FALSE, core->cancellable, NULL);

		{
			JsonBuilder *builder = core_event_builder ();

			builder_add_string (builder, "account", args[0]);
			core_emit_builder (core, "folders-updated", builder);
		}

		if (g_strcmp0 (args[0], args[3]) != 0) {
			JsonBuilder *builder = core_event_builder ();

			builder_add_string (builder, "account", args[3]);
			core_emit_builder (core, "folders-updated", builder);
		}
	}

	g_object_unref (dest);
	g_object_unref (src);

	return ok ? g_strdup ("{}") : NULL;
}

/* --- message content --- */

/* transfer-decoded content of a part, converted to UTF-8 for text */
static gchar *
part_get_text (CamelMimePart *part,
	       GCancellable *cancellable)
{
	CamelDataWrapper *content;
	CamelContentType *content_type;
	GOutputStream *output;
	const gchar *charset;
	gchar *data, *text;
	gsize length;

	content = camel_medium_get_content (CAMEL_MEDIUM (part));
	if (!content)
		return g_strdup ("");

	output = g_memory_output_stream_new_resizable ();
	camel_data_wrapper_decode_to_output_stream_sync (content, output, cancellable, NULL);
	g_output_stream_close (output, NULL, NULL);

	length = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output));
	data = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output));
	g_object_unref (output);

	content_type = camel_mime_part_get_content_type (part);
	charset = content_type ? camel_content_type_param (content_type, "charset") : NULL;

	if (charset && g_ascii_strcasecmp (charset, "utf-8") != 0 && g_ascii_strcasecmp (charset, "us-ascii") != 0) {
		text = g_convert_with_fallback (data, length, "UTF-8", charset, "?", NULL, NULL, NULL);
		if (!text)
			text = g_convert_with_fallback (data, length, "UTF-8", "ISO-8859-1", "?", NULL, NULL, NULL);
	} else {
		text = g_utf8_make_valid (data ? data : "", length);
	}

	g_free (data);

	return text ? text : g_strdup ("");
}

typedef struct {
	CamelMimePart *html;
	CamelMimePart *plain;
	GPtrArray *related;     /* CamelMimePart * avec Content-ID */
	GPtrArray *attachments; /* CamelMimePart * pièces jointes */
} BodyParts;

static gboolean
part_is_attachment (CamelMimePart *part)
{
	const gchar *disposition = camel_mime_part_get_disposition (part);

	return disposition && g_ascii_strcasecmp (disposition, "attachment") == 0;
}

static void
find_body_parts (CamelMimePart *part,
		 BodyParts *parts)
{
	CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
	CamelContentType *content_type = camel_mime_part_get_content_type (part);
	gboolean is_inline_cid_image = FALSE;

	if (CAMEL_IS_MULTIPART (content)) {
		guint ii, n = camel_multipart_get_number (CAMEL_MULTIPART (content));

		for (ii = 0; ii < n; ii++)
			find_body_parts (camel_multipart_get_part (CAMEL_MULTIPART (content), ii), parts);
		return;
	}

	if (CAMEL_IS_MIME_MESSAGE (content)) {
		if (parts->attachments)
			g_ptr_array_add (parts->attachments, part);
		return;
	}

	if (camel_mime_part_get_content_id (part) &&
	    content_type && camel_content_type_is (content_type, "image", "*")) {
		g_ptr_array_add (parts->related, part);
		is_inline_cid_image = TRUE;
	}

	if (part_is_attachment (part)) {
		if (parts->attachments)
			g_ptr_array_add (parts->attachments, part);
		return;
	}

	if (!content_type)
		return;

	if (!parts->html && camel_content_type_is (content_type, "text", "html") && !camel_mime_part_get_filename (part))
		parts->html = part;
	else if (!parts->plain && camel_content_type_is (content_type, "text", "plain") && !camel_mime_part_get_filename (part))
		parts->plain = part;
	else if (!is_inline_cid_image && parts->attachments)
		g_ptr_array_add (parts->attachments, part);
}

/* inline images: cid: references replaced by data: URIs */
static gchar *
inline_related_parts (gchar *html,
		      GPtrArray *related,
		      GCancellable *cancellable)
{
	guint ii;

	for (ii = 0; ii < related->len; ii++) {
		CamelMimePart *part = related->pdata[ii];
		CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
		GOutputStream *output;
		gchar *cid, *mime_type, *base64, *data_uri, **split;

		cid = g_strconcat ("cid:", camel_mime_part_get_content_id (part), NULL);
		if (!strstr (html, cid)) {
			g_free (cid);
			continue;
		}

		output = g_memory_output_stream_new_resizable ();
		camel_data_wrapper_decode_to_output_stream_sync (content, output, cancellable, NULL);
		g_output_stream_close (output, NULL, NULL);

		mime_type = camel_content_type_simple (camel_mime_part_get_content_type (part));
		base64 = g_base64_encode (
			g_memory_output_stream_get_data (G_MEMORY_OUTPUT_STREAM (output)),
			g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output)));
		data_uri = g_strdup_printf ("data:%s;base64,%s", mime_type, base64);

		split = g_strsplit (html, cid, -1);
		g_free (html);
		html = g_strjoinv (data_uri, split);

		g_strfreev (split);
		g_free (data_uri);
		g_free (base64);
		g_free (mime_type);
		g_object_unref (output);
		g_free (cid);
	}

	return html;
}

/* rough plain text of an HTML body, to quote it in a reply */
static gchar *
html_to_text (const gchar *html)
{
	static const struct { const gchar *entity, *text; } entities[] = {
		{ "&nbsp;", " " }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" },
		{ "&#39;", "'" }, { "&amp;", "&" }
	};
	GRegex *regex;
	gchar *text, *tmp;
	guint ii;

	/* drop style/script/head contents, turn block ends into line breaks */
	regex = g_regex_new ("<(style|script|head)[^>]*>.*?</\\1>", G_REGEX_CASELESS | G_REGEX_DOTALL, 0, NULL);
	text = g_regex_replace_literal (regex, html, -1, 0, "", 0, NULL);
	g_regex_unref (regex);

	regex = g_regex_new ("<(br|/p|/div|/tr|/h[1-6]|/li)[^>]*>", G_REGEX_CASELESS, 0, NULL);
	tmp = g_regex_replace_literal (regex, text, -1, 0, "\n", 0, NULL);
	g_regex_unref (regex);
	g_free (text);

	regex = g_regex_new ("<[^>]*>", 0, 0, NULL);
	text = g_regex_replace_literal (regex, tmp, -1, 0, "", 0, NULL);
	g_regex_unref (regex);
	g_free (tmp);

	for (ii = 0; ii < G_N_ELEMENTS (entities); ii++) {
		gchar **split = g_strsplit (text, entities[ii].entity, -1);
		g_free (text);
		text = g_strjoinv (entities[ii].text, split);
		g_strfreev (split);
	}

	/* at most one blank line in a row */
	regex = g_regex_new ("\\n[ \\t\\r]*\\n([ \\t\\r]*\\n)+", 0, 0, NULL);
	tmp = g_regex_replace_literal (regex, text, -1, 0, "\n\n", 0, NULL);
	g_regex_unref (regex);
	g_free (text);

	return g_strstrip (tmp);
}

/* Décode le contenu binaire d'une partie MIME en mémoire.
 * Paramètres :
 *   part        : partie MIME à décoder
 *   cancellable : objet d'annulation
 *   out_length  : taille en octets des données décodées
 * Retourne : tampon d'octets nouvellement alloué (à libérer avec g_free). */
static gchar *
part_decode_bytes (CamelMimePart *part,
		   GCancellable *cancellable,
		   gsize *out_length)
{
	CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
	GOutputStream *output;
	gchar *data;

	if (!content) {
		*out_length = 0;
		return g_strdup ("");
	}

	output = g_memory_output_stream_new_resizable ();
	if (CAMEL_IS_MIME_MESSAGE (content))
		camel_data_wrapper_write_to_output_stream_sync (content, output, cancellable, NULL);
	else
		camel_data_wrapper_decode_to_output_stream_sync (content, output, cancellable, NULL);
	g_output_stream_close (output, NULL, NULL);

	*out_length = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output));
	data = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output));
	g_object_unref (output);

	return data ? data : g_strdup ("");
}

/* Extrait le corps HTML d'un message, son équivalent texte brut et la liste
 * de ses pièces jointes.
 * Paramètres :
 *   message         : message MIME complet
 *   cancellable     : objet d'annulation
 *   out_text        : reçoit le texte brut (pour les citations)
 *   out_attachments : GPtrArray * optionnel recevant les CamelMimePart * des pièces jointes
 * Retourne : document HTML prêt à afficher. */
static gchar *
message_body_html (CamelMimeMessage *message,
		   GCancellable *cancellable,
		   gchar **out_text,
		   GPtrArray *out_attachments)
{
	BodyParts parts = { NULL, NULL, g_ptr_array_new (), out_attachments };
	gchar *html;

	find_body_parts (CAMEL_MIME_PART (message), &parts);

	if (parts.html) {
		gchar *source = part_get_text (parts.html, cancellable);
		gchar *inlined;

		*out_text = parts.plain ? part_get_text (parts.plain, cancellable) : html_to_text (source);
		inlined = inline_related_parts (source, parts.related, cancellable);

		/* Force un fond blanc et un texte sombre par défaut (avant et après le
		 * contenu du message, ainsi que sur les attributs de <body>) afin d'éviter
		 * tout affichage noir sur fond noir lorsque Windows 11 est en mode sombre
		 * ou que le message HTML redéfinit <body> / <style>. */
		html = g_strdup_printf (
			"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
			"<meta name=\"color-scheme\" content=\"light only\">"
			"<style id=\"evo-base-style\">"
			":root, html, body { color-scheme: light only !important; "
			"background-color: #ffffff !important; color: #1a1a1a !important; }"
			"body { font-family: 'Segoe UI', -apple-system, sans-serif; margin: 12px; }"
			"a { color: #0066cc; }"
			"</style></head>"
			"<body bgcolor=\"#ffffff\" text=\"#1a1a1a\" "
			"style=\"background-color: #ffffff !important; color: #1a1a1a !important; "
			"font-family: 'Segoe UI', -apple-system, sans-serif; margin: 12px;\">"
			"%s"
			"<style id=\"evo-override-style\">"
			":root, html, body { color-scheme: light only !important; "
			"background-color: #ffffff !important; color: #1a1a1a !important; }"
			"</style></body></html>",
			inlined);
		g_free (inlined);
	} else {
		gchar *text = parts.plain ? part_get_text (parts.plain, cancellable) : g_strdup ("");
		gchar *escaped = g_markup_escape_text (text, -1);

		*out_text = g_strdup (text);

		html = g_strdup_printf (
			"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
			"<meta name=\"color-scheme\" content=\"light only\">"
			"<style>:root, html, body { color-scheme: light only !important; "
			"background-color: #ffffff !important; color: #1a1a1a !important; }</style></head>"
			"<body bgcolor=\"#ffffff\" text=\"#1a1a1a\" "
			"style=\"background-color: #ffffff !important; color: #1a1a1a !important; margin: 12px;\">"
			"<pre style=\"white-space: pre-wrap; word-wrap: break-word; "
			"background-color: #ffffff !important; color: #1a1a1a !important; "
			"font-family: 'Cascadia Mono', Consolas, monospace; font-size: 10pt;\">%s</pre></body></html>",
			escaped);

		g_free (escaped);
		g_free (text);
	}

	g_ptr_array_unref (parts.related);

	return html;
}

static gchar *
format_recipients (CamelMimeMessage *message,
		   const gchar *type)
{
	CamelInternetAddress *addresses = camel_mime_message_get_recipients (message, type);
	gchar *formatted = addresses ? camel_address_format (CAMEL_ADDRESS (addresses)) : NULL;

	return formatted ? formatted : g_strdup ("");
}

/* Vérifie la signature cryptographique ou déchiffre un message MIME (OpenPGP
 * via GnuPG/Gpg4win ou S/MIME via NSS) et écrit l'objet "security" dans le
 * constructeur JSON du message. Si le message était chiffré et que le
 * déchiffrement réussit, remplace son contenu en mémoire par la partie
 * déchiffrée afin que l'extraction du corps et des pièces jointes opère sur
 * le texte en clair.
 * Paramètres :
 *   core    : instance du moteur evo-core
 *   message : message MIME à inspecter / déchiffrer
 *   builder : constructeur JSON dans lequel écrire le membre "security" */
static void
verify_or_decrypt_message (EvoCore *core,
			   CamelMimeMessage *message,
			   JsonBuilder *builder)
{
	CamelDataWrapper *content;
	CamelContentType *ct;
	gboolean is_encrypted = FALSE;
	gboolean is_signed = FALSE;
	gboolean is_valid = FALSE;
	const gchar *protocol = "";
	GString *signer_str = g_string_new ("");
	GString *desc_str = g_string_new ("");

	content = camel_medium_get_content (CAMEL_MEDIUM (message));
	ct = content ? camel_data_wrapper_get_mime_type_field (content) : NULL;
	if (!ct)
		ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));

	/* 1. Déchiffrement éventuel (OpenPGP multipart/encrypted ou S/MIME pkcs7-mime) */
	if (ct && camel_content_type_is (ct, "multipart", "encrypted")) {
		const gchar *proto = camel_content_type_param (ct, "protocol");

		if (proto && g_ascii_strcasecmp (proto, "application/pgp-encrypted") == 0) {
			CamelCipherContext *ctx = camel_gpg_context_new (core->session);
			CamelMimePart *decrypted_part = camel_mime_part_new ();
			GError *local_error = NULL;
			CamelCipherValidity *validity;

			is_encrypted = TRUE;
			protocol = "OpenPGP";
			camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);

			validity = camel_cipher_context_decrypt_sync (
				ctx, CAMEL_MIME_PART (message), decrypted_part,
				core->cancellable, &local_error);
			if (validity) {
				CamelDataWrapper *dec_content = camel_medium_get_content (CAMEL_MEDIUM (decrypted_part));

				if (dec_content)
					camel_medium_set_content (CAMEL_MEDIUM (message), dec_content);

				if (validity->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE) {
					GList *link;

					is_signed = TRUE;
					is_valid = (validity->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_GOOD);
					for (link = g_queue_peek_head_link (&validity->sign.signers); link; link = link->next) {
						CamelCipherCertInfo *info = link->data;
						if (!info)
							continue;
						if (signer_str->len > 0)
							g_string_append (signer_str, ", ");
						if (info->name && *info->name && info->email && *info->email)
							g_string_append_printf (signer_str, "%s <%s>", info->name, info->email);
						else if (info->email && *info->email)
							g_string_append (signer_str, info->email);
						else if (info->name && *info->name)
							g_string_append (signer_str, info->name);
					}
				} else {
					is_valid = (validity->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE);
				}

				if (validity->sign.description && *validity->sign.description)
					g_string_append (desc_str, validity->sign.description);
				else if (validity->encrypt.description && *validity->encrypt.description)
					g_string_append (desc_str, validity->encrypt.description);
				else
					g_string_append (desc_str, "Message déchiffré avec OpenPGP (GnuPG).");

				camel_cipher_validity_free (validity);
			} else {
				is_valid = FALSE;
				g_string_append_printf (desc_str,
					"Échec du déchiffrement OpenPGP : %s",
					local_error ? local_error->message : "clé privée indisponible ou Gpg4win absent");
				g_clear_error (&local_error);
			}

			g_object_unref (decrypted_part);
			g_object_unref (ctx);
		}
	} else if (ct && (camel_content_type_is (ct, "application", "pkcs7-mime") ||
			  camel_content_type_is (ct, "application", "x-pkcs7-mime"))) {
		const gchar *smime_type = camel_content_type_param (ct, "smime-type");
		CamelCipherContext *ctx = camel_smime_context_new (core->session);
		GError *local_error = NULL;
		CamelCipherValidity *validity = NULL;

		protocol = "S/MIME";
		if (smime_type && g_ascii_strcasecmp (smime_type, "signed-data") == 0) {
			is_signed = TRUE;
			validity = camel_cipher_context_verify_sync (
				ctx, CAMEL_MIME_PART (message), core->cancellable, &local_error);
		} else {
			CamelMimePart *decrypted_part = camel_mime_part_new ();

			is_encrypted = TRUE;
			validity = camel_cipher_context_decrypt_sync (
				ctx, CAMEL_MIME_PART (message), decrypted_part,
				core->cancellable, &local_error);
			if (validity) {
				CamelDataWrapper *dec_content = camel_medium_get_content (CAMEL_MEDIUM (decrypted_part));
				if (dec_content)
					camel_medium_set_content (CAMEL_MEDIUM (message), dec_content);
			}
			g_object_unref (decrypted_part);
		}

		if (validity) {
			GList *link;

			if (validity->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE) {
				is_signed = TRUE;
				is_valid = (validity->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_GOOD);
			} else {
				is_valid = (validity->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE);
			}

			for (link = g_queue_peek_head_link (&validity->sign.signers); link; link = link->next) {
				CamelCipherCertInfo *info = link->data;
				if (!info)
					continue;
				if (signer_str->len > 0)
					g_string_append (signer_str, ", ");
				if (info->name && *info->name && info->email && *info->email)
					g_string_append_printf (signer_str, "%s <%s>", info->name, info->email);
				else if (info->email && *info->email)
					g_string_append (signer_str, info->email);
				else if (info->name && *info->name)
					g_string_append (signer_str, info->name);
			}

			if (validity->sign.description && *validity->sign.description)
				g_string_append (desc_str, validity->sign.description);
			else if (validity->encrypt.description && *validity->encrypt.description)
				g_string_append (desc_str, validity->encrypt.description);
			else
				g_string_append (desc_str, "Opération cryptographique S/MIME réussie.");

			camel_cipher_validity_free (validity);
		} else {
			is_valid = FALSE;
			g_string_append_printf (desc_str,
				"Erreur S/MIME : %s",
				local_error ? local_error->message : "certificat ou clé introuvable");
			g_clear_error (&local_error);
		}

		g_object_unref (ctx);
	}

	/* 2. Vérification de signature multipart/signed (y compris après déchiffrement) */
	content = camel_medium_get_content (CAMEL_MEDIUM (message));
	ct = content ? camel_data_wrapper_get_mime_type_field (content) : NULL;
	if (!ct)
		ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));

	if (ct && camel_content_type_is (ct, "multipart", "signed")) {
		const gchar *proto = camel_content_type_param (ct, "protocol");
		CamelCipherContext *ctx = NULL;

		if (proto && g_ascii_strcasecmp (proto, "application/pgp-signature") == 0) {
			ctx = camel_gpg_context_new (core->session);
			protocol = "OpenPGP";
		} else if (proto && (g_ascii_strcasecmp (proto, "application/pkcs7-signature") == 0 ||
				     g_ascii_strcasecmp (proto, "application/x-pkcs7-signature") == 0)) {
			ctx = camel_smime_context_new (core->session);
			protocol = "S/MIME";
		}

		if (ctx) {
			GError *local_error = NULL;
			CamelCipherValidity *validity;
			CamelMimePart *sig_part = CAMEL_MIME_PART (message);

			/* Si le contenu déchiffré a remplacé le corps du message mais que
			 * l'en-tête Content-Type de message est encore multipart/encrypted,
			 * construit une partie temporaire portant le multipart/signed. */
			if (is_encrypted && content) {
				sig_part = camel_mime_part_new ();
				camel_medium_set_content (CAMEL_MEDIUM (sig_part), content);
			}

			is_signed = TRUE;
			validity = camel_cipher_context_verify_sync (
				ctx, sig_part, core->cancellable, &local_error);
			if (validity) {
				GList *link;

				is_valid = (validity->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_GOOD);
				g_string_truncate (signer_str, 0);
				for (link = g_queue_peek_head_link (&validity->sign.signers); link; link = link->next) {
					CamelCipherCertInfo *info = link->data;
					if (!info)
						continue;
					if (signer_str->len > 0)
						g_string_append (signer_str, ", ");
					if (info->name && *info->name && info->email && *info->email)
						g_string_append_printf (signer_str, "%s <%s>", info->name, info->email);
					else if (info->email && *info->email)
						g_string_append (signer_str, info->email);
					else if (info->name && *info->name)
						g_string_append (signer_str, info->name);
				}

				if (validity->sign.description && *validity->sign.description) {
					if (desc_str->len > 0)
						g_string_append (desc_str, "\n");
					g_string_append (desc_str, validity->sign.description);
				}

				camel_cipher_validity_free (validity);
			} else {
				is_valid = FALSE;
				if (desc_str->len > 0)
					g_string_append (desc_str, "\n");
				g_string_append_printf (desc_str,
					"Vérification de signature %s impossible : %s",
					protocol,
					local_error ? local_error->message : "erreur inconnue");
				g_clear_error (&local_error);
			}

			if (sig_part != CAMEL_MIME_PART (message))
				g_object_unref (sig_part);
			g_object_unref (ctx);
		}
	}

	json_builder_set_member_name (builder, "security");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "encrypted");
	json_builder_add_boolean_value (builder, is_encrypted);
	json_builder_set_member_name (builder, "signed");
	json_builder_add_boolean_value (builder, is_signed);
	json_builder_set_member_name (builder, "valid");
	json_builder_add_boolean_value (builder, is_valid);
	builder_add_string (builder, "protocol", protocol);
	builder_add_string (builder, "signer", signer_str->str);
	builder_add_string (builder, "description", desc_str->str);
	json_builder_end_object (builder);

	g_string_free (signer_str, TRUE);
	g_string_free (desc_str, TRUE);
}

static gchar *
core_get_message_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	CamelFolder *folder;
	CamelMimeMessage *message;
	CamelInternetAddress *from;
	GPtrArray *attachments;
	JsonBuilder *builder;
	gchar *json, *value;
	gint offset = 0;
	guint ii;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	message = camel_folder_get_message_sync (folder, args[2], core->cancellable, error);
	if (!message) {
		g_object_unref (folder);
		return NULL;
	}

	/* EVO_CORE_READ_ONLY=1 (tests): leave the mailbox untouched */
	if (g_strcmp0 (g_getenv ("EVO_CORE_READ_ONLY"), "1") != 0)
		camel_folder_set_message_flags (folder, args[2], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "subject");
	json_builder_add_string_value (builder, camel_mime_message_get_subject (message) ? camel_mime_message_get_subject (message) : "");

	from = camel_mime_message_get_from (message);
	value = from ? camel_address_format (CAMEL_ADDRESS (from)) : NULL;
	json_builder_set_member_name (builder, "from");
	json_builder_add_string_value (builder, value ? value : "");
	g_free (value);

	value = format_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	json_builder_set_member_name (builder, "to");
	json_builder_add_string_value (builder, value);
	g_free (value);

	value = format_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	json_builder_set_member_name (builder, "cc");
	json_builder_add_string_value (builder, value);
	g_free (value);

	json_builder_set_member_name (builder, "date");
	json_builder_add_int_value (builder, camel_mime_message_get_date (message, &offset));

	{
		CamelInternetAddress *reply_to = camel_mime_message_get_reply_to (message);
		gchar *formatted = reply_to ? camel_address_format (CAMEL_ADDRESS (reply_to)) : NULL;

		builder_add_string (builder, "reply_to", formatted);
		g_free (formatted);
	}

	builder_add_string (builder, "message_id", camel_medium_get_header (CAMEL_MEDIUM (message), "Message-ID"));
	builder_add_string (builder, "references", camel_medium_get_header (CAMEL_MEDIUM (message), "References"));

	verify_or_decrypt_message (core, message, builder);

	attachments = g_ptr_array_new ();
	{
		gchar *text = NULL;

		value = message_body_html (message, core->cancellable, &text, attachments);
		builder_add_string (builder, "html", value);
		builder_add_string (builder, "text", text);
		g_free (value);
		g_free (text);
	}

	json_builder_set_member_name (builder, "attachments");
	json_builder_begin_array (builder);
	for (ii = 0; ii < attachments->len; ii++) {
		CamelMimePart *part = attachments->pdata[ii];
		CamelContentType *ct = camel_mime_part_get_content_type (part);
		const gchar *filename = camel_mime_part_get_filename (part);
		gchar *mime_type = ct ? camel_content_type_simple (ct) : g_strdup ("application/octet-stream");
		gsize size = 0;
		gchar *bytes = part_decode_bytes (part, core->cancellable, &size);

		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "index");
		json_builder_add_int_value (builder, ii);
		builder_add_string (builder, "filename", (filename && *filename) ? filename : "piece-jointe");
		builder_add_string (builder, "mime_type", mime_type);
		json_builder_set_member_name (builder, "size");
		json_builder_add_int_value (builder, (gint64) size);
		json_builder_end_object (builder);

		g_free (bytes);
		g_free (mime_type);
	}
	json_builder_end_array (builder);
	g_ptr_array_unref (attachments);

	json_builder_end_object (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_object_unref (message);
	g_object_unref (folder);

	return json;
}

/* Enregistre une pièce jointe d'un message dans un fichier local.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] message_uid,
 *           [3] attachment_index, [4] dest_path
 *   error : emplacement d'erreur GError
 * Retourne : {"size": <octets>}, ou NULL en cas d'erreur. */
static gchar *
core_save_attachment_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	CamelFolder *folder;
	CamelMimeMessage *message;
	BodyParts parts = { NULL, NULL, g_ptr_array_new (), g_ptr_array_new () };
	guint idx;
	gchar *data = NULL, *json = NULL;
	gsize size = 0;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder) {
		g_ptr_array_unref (parts.related);
		g_ptr_array_unref (parts.attachments);
		return NULL;
	}

	message = camel_folder_get_message_sync (folder, args[2], core->cancellable, error);
	if (!message) {
		g_ptr_array_unref (parts.related);
		g_ptr_array_unref (parts.attachments);
		g_object_unref (folder);
		return NULL;
	}

	find_body_parts (CAMEL_MIME_PART (message), &parts);
	idx = (guint) g_ascii_strtoull (args[3], NULL, 10);

	if (idx >= parts.attachments->len) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"Attachment #%u not found", idx);
	} else {
		CamelMimePart *part = parts.attachments->pdata[idx];

		data = part_decode_bytes (part, core->cancellable, &size);
		if (g_file_set_contents (args[4], data, (gssize) size, error))
			json = g_strdup_printf ("{\"size\":%" G_GSIZE_FORMAT "}", size);
		g_free (data);
	}

	g_ptr_array_unref (parts.related);
	g_ptr_array_unref (parts.attachments);
	g_object_unref (message);
	g_object_unref (folder);

	return json;
}

/* Nettoie une chaîne (comme l'objet d'un courriel) pour en faire un nom de
 * fichier Windows valide sans caractères interdits.
 * Paramètres :
 *   raw : chaîne source en UTF-8
 * Retourne : chaîne nettoyée nouvellement allouée (à libérer avec g_free). */
static gchar *
sanitize_eml_filename (const gchar *raw)
{
	GString *out = g_string_new ("");
	const gchar *p;

	if (raw && *raw) {
		for (p = raw; *p; p = g_utf8_next_char (p)) {
			gunichar c = g_utf8_get_char (p);

			if (c < 0x20 || c == '\\' || c == '/' || c == ':' || c == '*' ||
			    c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
				g_string_append_c (out, '_');
			else
				g_string_append_unichar (out, c);

			if (out->len >= 96)
				break;
		}
	}

	g_strstrip (out->str);
	out->len = strlen (out->str);
	while (out->len > 0 && (out->str[out->len - 1] == '.' || out->str[out->len - 1] == ' '))
		g_string_truncate (out, out->len - 1);

	if (out->len == 0)
		g_string_assign (out, "Message");

	return g_string_free (out, FALSE);
}

/* Exporte une liste de messages d'un dossier sous forme de fichiers .eml dans
 * un sous-répertoire temporaire afin de les joindre à un autre courriel.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] JSON {"account": "...", "folder": "...", "uids": [...]}
 *   error : emplacement d'erreur GError
 * Retourne : tableau JSON ["C:\\...\\Objet.eml", ...], ou NULL en cas d'erreur. */
static gchar *
core_export_messages_eml_run (EvoCore *core,
			      gchar **args,
			      GError **error)
{
	JsonNode *root;
	JsonObject *object;
	JsonArray *uids;
	const gchar *account_uid, *folder_name;
	CamelFolder *folder = NULL;
	JsonBuilder *builder = NULL;
	gchar *batch_dir = NULL, *json = NULL;
	guint ii, len;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account_uid = object ? json_object_get_string_member_with_default (object, "account", NULL) : NULL;
	folder_name = object ? json_object_get_string_member_with_default (object, "folder", NULL) : NULL;
	uids = (object && json_object_has_member (object, "uids")) ? json_object_get_array_member (object, "uids") : NULL;

	if (!account_uid || !folder_name || !uids) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Invalid export_messages_eml request");
		json_node_unref (root);
		return NULL;
	}

	folder = core_ref_folder (core, account_uid, folder_name, error);
	if (!folder) {
		json_node_unref (root);
		return NULL;
	}

	batch_dir = g_strdup_printf ("%s\\EvolutionWin-eml-%" G_GINT64_FORMAT,
		g_get_tmp_dir (), g_get_monotonic_time ());
	g_mkdir_with_parents (batch_dir, 0700);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	len = json_array_get_length (uids);
	for (ii = 0; ii < len; ii++) {
		const gchar *uid = json_array_get_string_element (uids, ii);
		CamelMimeMessage *message;
		GOutputStream *output;
		const gchar *subject;
		gchar *clean_name, *filename, *full_path, *data;
		gsize size;
		guint suffix = 1;

		if (!uid || !*uid)
			continue;

		message = camel_folder_get_message_sync (folder, uid, core->cancellable, error);
		if (!message)
			goto cleanup;

		subject = camel_mime_message_get_subject (message);
		clean_name = sanitize_eml_filename (subject);
		filename = g_strdup_printf ("%s.eml", clean_name);
		full_path = g_build_filename (batch_dir, filename, NULL);

		while (g_file_test (full_path, G_FILE_TEST_EXISTS)) {
			g_free (filename);
			g_free (full_path);
			filename = g_strdup_printf ("%s (%u).eml", clean_name, ++suffix);
			full_path = g_build_filename (batch_dir, filename, NULL);
		}

		output = g_memory_output_stream_new_resizable ();
		camel_data_wrapper_write_to_output_stream_sync (
			CAMEL_DATA_WRAPPER (message), output, core->cancellable, NULL);
		g_output_stream_close (output, NULL, NULL);

		size = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output));
		data = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output));
		g_object_unref (output);
		g_object_unref (message);

		if (!g_file_set_contents (full_path, data ? data : "", (gssize) size, error)) {
			g_free (data);
			g_free (clean_name);
			g_free (filename);
			g_free (full_path);
			goto cleanup;
		}

		json_builder_add_string_value (builder, full_path);

		g_free (data);
		g_free (clean_name);
		g_free (filename);
		g_free (full_path);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

cleanup:
	if (builder)
		g_object_unref (builder);
	g_free (batch_dir);
	g_object_unref (folder);
	json_node_unref (root);

	return json;
}

/* --- refresh --- */

static gchar *
core_refresh_run (EvoCore *core,
		  gchar **args,
		  GError **error)
{
	GList *services, *link;

	services = camel_session_list_services (core->session);

	for (link = services; link && !g_cancellable_is_cancelled (core->cancellable); link = g_list_next (link)) {
		CamelService *service = link->data;
		const gchar *uid = camel_service_get_uid (service);
		CamelFolder *inbox;
		GHashTableIter iter;
		GPtrArray *opened;
		gpointer key, value;
		gchar *prefix;
		GError *local_error = NULL;
		guint ii;

		if (!CAMEL_IS_STORE (service) || !CAMEL_IS_NETWORK_SERVICE (service))
			continue;

		g_message ("refresh of “%s”", camel_service_get_display_name (service));

		if (!core_ensure_connected (core, CAMEL_STORE (service), &local_error)) {
			g_warning ("Cannot connect “%s”: %s", camel_service_get_display_name (service),
				local_error ? local_error->message : "?");
			g_clear_error (&local_error);
			continue;
		}

		/* the inbox is followed even when not shown, for new mail */
		inbox = camel_store_get_inbox_folder_sync (CAMEL_STORE (service), core->cancellable, NULL);
		if (inbox) {
			g_object_unref (core_track_folder (core, uid, inbox));
			g_object_unref (inbox);
		}

		/* refreshing emits "changed" on the folders with new messages */
		prefix = g_strconcat (uid, "\x1F", NULL);
		opened = g_ptr_array_new_with_free_func (g_object_unref);

		g_mutex_lock (&core->folders_lock);
		g_hash_table_iter_init (&iter, core->folders);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			if (g_str_has_prefix (key, prefix))
				g_ptr_array_add (opened, g_object_ref (value));
		}
		g_mutex_unlock (&core->folders_lock);

		for (ii = 0; ii < opened->len; ii++) {
			CamelFolder *folder = opened->pdata[ii];

			if (!camel_folder_refresh_info_sync (folder, core->cancellable, &local_error)) {
				g_warning ("Cannot refresh “%s”: %s", camel_folder_get_full_name (folder),
					local_error ? local_error->message : "?");
				g_clear_error (&local_error);
			} else {
				g_message ("  “%s” refreshed: %d messages", camel_folder_get_full_name (folder),
					camel_folder_get_message_count (folder));
			}
			g_object_set_data (G_OBJECT (folder), "evo-initial-sync-done", GINT_TO_POINTER (1));
		}

		g_ptr_array_unref (opened);
		g_free (prefix);

		{
			JsonBuilder *builder = core_event_builder ();

			builder_add_string (builder, "account", uid);
			core_emit_builder (core, "folders-updated", builder);
		}
	}

	g_list_free_full (services, g_object_unref);

	return g_strdup ("{}");
}

static void
core_ignore_result (const char *json,
		    const char *error_message,
		    void *user_data)
{
}

static gboolean
core_refresh_timeout_cb (gpointer user_data)
{
	evo_core_refresh (user_data);

	return G_SOURCE_CONTINUE;
}

/* --- sending --- */

/* "folder://<store uid>/<escaped folder path>" -> store uid, folder name */
static gboolean
parse_folder_uri (const gchar *uri,
		  gchar **out_store_uid,
		  gchar **out_folder_name)
{
	const gchar *rest, *slash;

	if (!uri || !g_str_has_prefix (uri, "folder://"))
		return FALSE;

	rest = uri + strlen ("folder://");
	slash = strchr (rest, '/');
	if (!slash || slash == rest || !slash[1])
		return FALSE;

	*out_store_uid = g_strndup (rest, slash - rest);
	*out_folder_name = g_uri_unescape_string (slash + 1, NULL);

	return *out_folder_name != NULL;
}

/* identity (sender) and submission settings of an account */
static ESource *
core_ref_identity (EvoCore *core,
		   const gchar *account_uid)
{
	ESource *account, *identity = NULL;

	account = e_source_registry_ref_source (core->registry, account_uid);

	if (account && e_source_has_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		gchar *identity_uid = e_source_mail_account_dup_identity_uid (
			e_source_get_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT));

		if (identity_uid)
			identity = e_source_registry_ref_source (core->registry, identity_uid);
		g_free (identity_uid);
	}

	g_clear_object (&account);

	return identity;
}

static CamelInternetAddress *
parse_addresses (JsonObject *object,
		 const gchar *member)
{
	const gchar *value = json_object_get_string_member_with_default (object, member, "");
	CamelInternetAddress *addresses = camel_internet_address_new ();

	if (value && *value)
		camel_address_unformat (CAMEL_ADDRESS (addresses), value);

	return addresses;
}

/* Construit la partie MIME contenant le corps du message :
 * - si html est fourni et non vide, crée un conteneur multipart/alternative
 *   combinant la version text/plain et la version text/html (UTF-8,
 *   quoted-printable) ;
 * - sinon, crée une simple partie text/plain (UTF-8, quoted-printable).
 * Paramètres :
 *   text : corps du message en texte brut (peut être vide si html est fourni)
 *   html : corps du message en HTML (peut être NULL ou vide)
 * Retourne : un nouvel objet CamelMimePart * ou CamelMultipart * encapsulé
 *            dans un CamelMimePart * prêt à être attaché au message. */
static void
set_message_or_part_body (CamelMedium *target_medium,
			  const gchar *text,
			  const gchar *html)
{
	gchar *fallback_text = NULL;
	const gchar *plain_text = text ? text : "";

	if (html && *html) {
		CamelMultipart *alt = camel_multipart_new ();
		CamelMimePart *plain_part = camel_mime_part_new ();
		CamelMimePart *html_part = camel_mime_part_new ();

		if (!*plain_text) {
			fallback_text = html_to_text (html);
			plain_text = fallback_text ? fallback_text : "";
		}

		camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (alt), "multipart/alternative");
		camel_multipart_set_boundary (alt, NULL);

		camel_mime_part_set_content (plain_part, plain_text, strlen (plain_text), "text/plain; charset=utf-8");
		camel_mime_part_set_encoding (plain_part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
		camel_multipart_add_part (alt, plain_part);

		camel_mime_part_set_content (html_part, html, strlen (html), "text/html; charset=utf-8");
		camel_mime_part_set_encoding (html_part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
		camel_multipart_add_part (alt, html_part);

		camel_medium_set_content (target_medium, CAMEL_DATA_WRAPPER (alt));

		g_object_unref (html_part);
		g_object_unref (plain_part);
		g_object_unref (alt);
		g_free (fallback_text);
	} else {
		camel_mime_part_set_content (CAMEL_MIME_PART (target_medium),
			plain_text, strlen (plain_text), "text/plain; charset=utf-8");
		camel_mime_part_set_encoding (CAMEL_MIME_PART (target_medium),
			CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
	}
}

static gchar *
core_send_message_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	JsonNode *root;
	JsonObject *object;
	ESource *identity = NULL;
	ESourceMailIdentity *identity_extension;
	ESourceMailSubmission *submission;
	CamelInternetAddress *from = NULL, *to = NULL, *cc = NULL, *bcc = NULL, *recipients = NULL;
	CamelMimeMessage *message = NULL;
	CamelService *transport = NULL;
	const gchar *account_uid, *subject, *text, *html, *header;
	gboolean saved = FALSE, sent_message_saved = FALSE;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account_uid = object ? json_object_get_string_member_with_default (object, "account", NULL) : NULL;

	identity = account_uid ? core_ref_identity (core, account_uid) : NULL;
	if (!identity || !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY) ||
	    !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "This account has no sender identity");
		goto exit;
	}

	identity_extension = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	submission = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION);

	transport = camel_session_ref_service (core->session, e_source_mail_submission_get_transport_uid (submission));
	if (!CAMEL_IS_TRANSPORT (transport)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "This account has no outgoing (SMTP) server");
		goto exit;
	}

	from = camel_internet_address_new ();
	camel_internet_address_add (from,
		e_source_mail_identity_get_name (identity_extension),
		e_source_mail_identity_get_address (identity_extension));

	to = parse_addresses (object, "to");
	cc = parse_addresses (object, "cc");
	bcc = parse_addresses (object, "bcc");

	recipients = camel_internet_address_new ();
	camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (to));
	camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (cc));
	camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (bcc));

	if (camel_address_length (CAMEL_ADDRESS (recipients)) == 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No recipient");
		goto exit;
	}

	subject = json_object_get_string_member_with_default (object, "subject", "");
	text = json_object_get_string_member_with_default (object, "text", "");
	html = json_object_get_string_member_with_default (object, "html", "");

	message = camel_mime_message_new ();
	camel_mime_message_set_subject (message, subject);
	camel_mime_message_set_from (message, from);
	camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to);
	if (camel_address_length (CAMEL_ADDRESS (cc)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_CC, cc);
	/* Bcc is not written in the message: recipients of the transport only */
	camel_mime_message_set_date (message, CAMEL_MESSAGE_DATE_CURRENT, 0);
	camel_mime_message_set_message_id (message, NULL);

	if (e_source_mail_identity_get_reply_to (identity_extension) &&
	    *e_source_mail_identity_get_reply_to (identity_extension)) {
		CamelInternetAddress *reply_to = camel_internet_address_new ();

		camel_address_unformat (CAMEL_ADDRESS (reply_to), e_source_mail_identity_get_reply_to (identity_extension));
		camel_mime_message_set_reply_to (message, reply_to);
		g_object_unref (reply_to);
	}

	header = json_object_get_string_member_with_default (object, "in_reply_to", "");
	if (header && *header)
		camel_medium_set_header (CAMEL_MEDIUM (message), "In-Reply-To", header);

	header = json_object_get_string_member_with_default (object, "references", "");
	if (header && *header)
		camel_medium_set_header (CAMEL_MEDIUM (message), "References", header);

	camel_medium_set_header (CAMEL_MEDIUM (message), "X-Mailer", "Evolution for Windows");

	if (json_object_has_member (object, "attachments")) {
		JsonArray *attachments = json_object_get_array_member (object, "attachments");
		guint n_att = attachments ? json_array_get_length (attachments) : 0;

		if (n_att > 0) {
			CamelMultipart *multipart = camel_multipart_new ();
			CamelMimePart *body_part = camel_mime_part_new ();
			guint ii;

			camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart), "multipart/mixed");
			camel_multipart_set_boundary (multipart, NULL);

			set_message_or_part_body (CAMEL_MEDIUM (body_part), text, html);
			camel_multipart_add_part (multipart, body_part);
			g_object_unref (body_part);

			for (ii = 0; ii < n_att; ii++) {
				const gchar *path = json_array_get_string_element (attachments, ii);
				gchar *contents = NULL, *basename = NULL, *guessed = NULL, *mime_type = NULL;
				gsize length = 0;
				CamelMimePart *att_part;

				if (!path || !*path)
					continue;

				if (!g_file_get_contents (path, &contents, &length, error)) {
					g_object_unref (multipart);
					goto exit;
				}

				basename = g_path_get_basename (path);
				if (g_ascii_strcasecmp (basename + MAX ((gssize) strlen (basename) - 4, 0), ".eml") == 0) {
					mime_type = g_strdup ("message/rfc822");
				} else {
					guessed = g_content_type_guess (basename, (const guchar *) contents, length, NULL);
					mime_type = guessed ? g_content_type_get_mime_type (guessed) : NULL;
				}

				att_part = camel_mime_part_new ();
				camel_mime_part_set_content (att_part, contents, length,
					(mime_type && *mime_type) ? mime_type : "application/octet-stream");
				camel_mime_part_set_disposition (att_part, "attachment");
				camel_mime_part_set_filename (att_part, basename);
				camel_mime_part_set_encoding (att_part, CAMEL_TRANSFER_ENCODING_BASE64);
				camel_multipart_add_part (multipart, att_part);

				g_object_unref (att_part);
				g_free (mime_type);
				g_free (guessed);
				g_free (basename);
				g_free (contents);
			}

			camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (multipart));
			g_object_unref (multipart);
		} else {
			set_message_or_part_body (CAMEL_MEDIUM (message), text, html);
		}
	} else {
		set_message_or_part_body (CAMEL_MEDIUM (message), text, html);
	}

	/* Signature et/ou chiffrement cryptographique OpenPGP (Gpg4win) ou S/MIME (NSS) */
	{
		gboolean pgp_sign = json_object_get_boolean_member_with_default (object, "pgp_sign", FALSE);
		gboolean pgp_encrypt = json_object_get_boolean_member_with_default (object, "pgp_encrypt", FALSE);
		gboolean smime_sign = json_object_get_boolean_member_with_default (object, "smime_sign", FALSE);
		gboolean smime_encrypt = json_object_get_boolean_member_with_default (object, "smime_encrypt", FALSE);
		const gchar *sender_addr = e_source_mail_identity_get_address (identity_extension);
		const gchar *pgp_key_id = json_object_get_string_member_with_default (object, "pgp_key_id", NULL);
		const gchar *smime_key_id = json_object_get_string_member_with_default (object, "smime_key_id", NULL);

		if (pgp_sign || pgp_encrypt || smime_sign || smime_encrypt) {
			CamelMimePart *work_part = camel_mime_part_new ();
			CamelDataWrapper *orig_dw = camel_medium_get_content (CAMEL_MEDIUM (message));
			CamelContentType *orig_ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));
			const gchar *userid_pgp = (pgp_key_id && *pgp_key_id) ? pgp_key_id : sender_addr;
			const gchar *userid_smime = (smime_key_id && *smime_key_id) ? smime_key_id : sender_addr;

			if (orig_ct) {
				gchar *ct_str = camel_content_type_format (orig_ct);
				camel_mime_part_set_content_type (work_part, ct_str);
				g_free (ct_str);
			}
			camel_mime_part_set_encoding (work_part,
				camel_mime_part_get_encoding (CAMEL_MIME_PART (message)));
			if (orig_dw)
				camel_medium_set_content (CAMEL_MEDIUM (work_part), orig_dw);

			/* 1. Signature OpenPGP */
			if (pgp_sign) {
				CamelCipherContext *ctx = camel_gpg_context_new (core->session);
				CamelMimePart *signed_part = camel_mime_part_new ();

				camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);
				if (!camel_cipher_context_sign_sync (
					ctx, userid_pgp, CAMEL_CIPHER_HASH_SHA256,
					work_part, signed_part, core->cancellable, error)) {
					g_object_unref (signed_part);
					g_object_unref (ctx);
					g_object_unref (work_part);
					goto exit;
				}
				g_object_unref (work_part);
				work_part = signed_part;
				g_object_unref (ctx);
			}

			/* 2. Chiffrement OpenPGP */
			if (pgp_encrypt) {
				CamelCipherContext *ctx = camel_gpg_context_new (core->session);
				CamelMimePart *encrypted_part = camel_mime_part_new ();
				GPtrArray *rcpt_keys = g_ptr_array_new_with_free_func (g_free);
				gint n_rcpt = camel_address_length (CAMEL_ADDRESS (recipients));
				gint r_idx;

				camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);
				for (r_idx = 0; r_idx < n_rcpt; r_idx++) {
					const gchar *r_addr = NULL;
					if (camel_internet_address_get (recipients, r_idx, NULL, &r_addr) && r_addr && *r_addr)
						g_ptr_array_add (rcpt_keys, g_strdup (r_addr));
				}
				if (userid_pgp && *userid_pgp)
					g_ptr_array_add (rcpt_keys, g_strdup (userid_pgp));

				if (!camel_cipher_context_encrypt_sync (
					ctx, userid_pgp, rcpt_keys,
					work_part, encrypted_part, core->cancellable, error)) {
					g_ptr_array_unref (rcpt_keys);
					g_object_unref (encrypted_part);
					g_object_unref (ctx);
					g_object_unref (work_part);
					goto exit;
				}
				g_ptr_array_unref (rcpt_keys);
				g_object_unref (work_part);
				work_part = encrypted_part;
				g_object_unref (ctx);
			}

			/* 3. Signature S/MIME */
			if (smime_sign) {
				CamelCipherContext *ctx = camel_smime_context_new (core->session);
				CamelMimePart *signed_part = camel_mime_part_new ();

				camel_smime_context_set_sign_mode (CAMEL_SMIME_CONTEXT (ctx), CAMEL_SMIME_SIGN_CLEARSIGN);
				if (!camel_cipher_context_sign_sync (
					ctx, userid_smime, CAMEL_CIPHER_HASH_SHA256,
					work_part, signed_part, core->cancellable, error)) {
					g_object_unref (signed_part);
					g_object_unref (ctx);
					g_object_unref (work_part);
					goto exit;
				}
				g_object_unref (work_part);
				work_part = signed_part;
				g_object_unref (ctx);
			}

			/* 4. Chiffrement S/MIME */
			if (smime_encrypt) {
				CamelCipherContext *ctx = camel_smime_context_new (core->session);
				CamelMimePart *encrypted_part = camel_mime_part_new ();
				GPtrArray *rcpt_keys = g_ptr_array_new_with_free_func (g_free);
				gint n_rcpt = camel_address_length (CAMEL_ADDRESS (recipients));
				gint r_idx;

				for (r_idx = 0; r_idx < n_rcpt; r_idx++) {
					const gchar *r_addr = NULL;
					if (camel_internet_address_get (recipients, r_idx, NULL, &r_addr) && r_addr && *r_addr)
						g_ptr_array_add (rcpt_keys, g_strdup (r_addr));
				}
				if (userid_smime && *userid_smime)
					g_ptr_array_add (rcpt_keys, g_strdup (userid_smime));

				if (!camel_cipher_context_encrypt_sync (
					ctx, userid_smime, rcpt_keys,
					work_part, encrypted_part, core->cancellable, error)) {
					g_ptr_array_unref (rcpt_keys);
					g_object_unref (encrypted_part);
					g_object_unref (ctx);
					g_object_unref (work_part);
					goto exit;
				}
				g_ptr_array_unref (rcpt_keys);
				g_object_unref (work_part);
				work_part = encrypted_part;
				g_object_unref (ctx);
			}

			/* Transfère le contenu signé/chiffré sur le CamelMimeMessage */
			{
				CamelDataWrapper *final_dw = camel_medium_get_content (CAMEL_MEDIUM (work_part));
				CamelContentType *final_ct = camel_mime_part_get_content_type (work_part);

				if (final_dw)
					camel_medium_set_content (CAMEL_MEDIUM (message), final_dw);
				if (final_ct) {
					gchar *ct_str = camel_content_type_format (final_ct);
					camel_mime_part_set_content_type (CAMEL_MIME_PART (message), ct_str);
					g_free (ct_str);
				}
				camel_mime_part_set_encoding (CAMEL_MIME_PART (message),
					camel_mime_part_get_encoding (work_part));
			}

			g_object_unref (work_part);
		}
	}

	{
		JsonBuilder *builder = core_event_builder ();

		builder_add_string (builder, "subject", subject);
		core_emit_builder (core, "sending", builder);
	}

	if (!camel_service_connect_sync (transport, core->cancellable, error) ||
	    !camel_transport_send_to_sync (CAMEL_TRANSPORT (transport), message, CAMEL_ADDRESS (from),
		CAMEL_ADDRESS (recipients), &sent_message_saved, core->cancellable, error)) {
		camel_service_disconnect_sync (transport, FALSE, NULL, NULL);
		goto exit;
	}

	camel_service_disconnect_sync (transport, TRUE, NULL, NULL);

	{
		JsonBuilder *builder = core_event_builder ();

		builder_add_string (builder, "subject", subject);
		core_emit_builder (core, "sent", builder);
	}

	/* copy in the Sent folder, unless the server did it (e.g. Gmail) */
	saved = sent_message_saved;

	if (!saved && e_source_mail_submission_get_use_sent_folder (submission)) {
		gchar *sent_uri = e_source_mail_submission_dup_sent_folder (submission);
		gchar *store_uid = NULL, *folder_name = NULL;
		GError *local_error = NULL;

		if (parse_folder_uri (sent_uri, &store_uid, &folder_name)) {
			CamelFolder *sent = core_ref_folder (core, store_uid, folder_name, &local_error);

			if (sent) {
				CamelMessageInfo *info = camel_message_info_new (NULL);

				camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
				saved = camel_folder_append_message_sync (sent, message, info, NULL, core->cancellable, &local_error);

				g_object_unref (info);
				g_object_unref (sent);
			}
		}

		if (local_error) {
			/* the message is sent: only warn */
			g_warning ("Cannot save the sent message in “%s”: %s", sent_uri, local_error->message);
			g_clear_error (&local_error);
		}

		g_free (store_uid);
		g_free (folder_name);
		g_free (sent_uri);
	}

	json = g_strdup (saved ? "{\"saved\":true}" : "{\"saved\":false}");

 exit:
	g_clear_object (&message);
	g_clear_object (&from);
	g_clear_object (&to);
	g_clear_object (&cc);
	g_clear_object (&bcc);
	g_clear_object (&recipients);
	g_clear_object (&transport);
	g_clear_object (&identity);
	json_node_unref (root);

	return json;
}

/* --- gestion des comptes et des mots de passe --- */

/* Enregistre le mot de passe d'une source dans le gestionnaire d'identifiants
 * Windows via ESourceCredentialsProvider.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   source_uid : identifiant de la source
 *   password   : mot de passe en clair
 *   error      : emplacement pour une éventuelle erreur
 * Retourne : TRUE en cas de succès. */
static gboolean
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

	if (!e_source_registry_create_sources_sync (core->registry, sources, core->cancellable, error))
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

/* Supprime un compte de messagerie, son identité et son serveur d'envoi SMTP
 * d'evolution-data-server ainsi que de la session Camel active.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = account_uid
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_remove_account_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	const gchar *account_uid = args[0];
	ESource *account_source, *identity_source = NULL, *transport_source = NULL;
	CamelService *service;
	gchar *prefix;

	account_source = e_source_registry_ref_source (core->registry, account_uid);
	if (!account_source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"Compte introuvable : « %s »", account_uid);
		return NULL;
	}

	if (e_source_has_extension (account_source, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		gchar *identity_uid = e_source_mail_account_dup_identity_uid (
			e_source_get_extension (account_source, E_SOURCE_EXTENSION_MAIL_ACCOUNT));

		if (identity_uid)
			identity_source = e_source_registry_ref_source (core->registry, identity_uid);
		g_free (identity_uid);
	}

	if (identity_source && e_source_has_extension (identity_source, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
		gchar *transport_uid = e_source_mail_submission_dup_transport_uid (
			e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_SUBMISSION));

		if (transport_uid)
			transport_source = e_source_registry_ref_source (core->registry, transport_uid);
		g_free (transport_uid);
	}

	/* Ferme et retire les dossiers mis en cache pour ce compte */
	prefix = g_strdup_printf ("%s\x1F", account_uid);
	g_mutex_lock (&core->folders_lock);
	{
		GHashTableIter iter;
		gpointer key, value;

		g_hash_table_iter_init (&iter, core->folders);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			if (g_str_has_prefix (key, prefix)) {
				g_signal_handlers_disconnect_by_func (value, core_folder_changed_cb, core);
				g_hash_table_iter_remove (&iter);
			}
		}
	}
	g_mutex_unlock (&core->folders_lock);
	g_free (prefix);

	service = camel_session_ref_service (core->session, account_uid);
	if (service) {
		camel_service_disconnect_sync (service, FALSE, NULL, NULL);
		camel_session_remove_service (core->session, service);
		g_object_unref (service);
	}

	if (transport_source) {
		service = camel_session_ref_service (core->session, e_source_get_uid (transport_source));
		if (service) {
			camel_service_disconnect_sync (service, FALSE, NULL, NULL);
			camel_session_remove_service (core->session, service);
			g_object_unref (service);
		}
		e_source_remove_sync (transport_source, core->cancellable, NULL);
		g_object_unref (transport_source);
	}

	if (identity_source) {
		e_source_remove_sync (identity_source, core->cancellable, NULL);
		g_object_unref (identity_source);
	}

	if (!e_source_remove_sync (account_source, core->cancellable, error)) {
		g_object_unref (account_source);
		return NULL;
	}

	g_object_unref (account_source);

	return g_strdup ("{}");
}

/* --- agenda (ECalClient / libecal-2.0) --- */

typedef struct {
	gchar *uid;
	gchar *summary;
	gchar *location;
	gchar *description;
	gint64 start;
	gint64 end;
	gchar *calendar_uid;
	gchar *calendar_name;
} CalEventItem;

/* Libère un élément d'événement d'agenda temporaire.
 * Paramètres :
 *   data : pointeur vers CalEventItem */
static void
cal_event_item_free (gpointer data)
{
	CalEventItem *item = data;

	if (!item)
		return;

	g_free (item->uid);
	g_free (item->summary);
	g_free (item->location);
	g_free (item->description);
	g_free (item->calendar_uid);
	g_free (item->calendar_name);
	g_free (item);
}

/* Compare deux événements par date de début croissante.
 * Paramètres :
 *   a, b : pointeurs vers CalEventItem *
 * Retourne : -1, 0 ou 1. */
static gint
compare_cal_events (gconstpointer a,
		    gconstpointer b)
{
	const CalEventItem *ea = *(const CalEventItem **) a;
	const CalEventItem *eb = *(const CalEventItem **) b;

	if (ea->start != eb->start)
		return (ea->start < eb->start) ? -1 : 1;

	return g_utf8_collate (ea->summary ? ea->summary : "", eb->summary ? eb->summary : "");
}

/* Liste les événements de tous les agendas activés dans l'intervalle demandé.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = start_unix, [1] = end_unix
 *   error : emplacement pour une éventuelle erreur
 * Retourne : tableau JSON des événements. */
static gchar *
core_list_events_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	gint64 start_unix = g_ascii_strtoll (args[0], NULL, 10);
	gint64 end_unix = g_ascii_strtoll (args[1], NULL, 10);
	GList *sources, *slink;
	GPtrArray *items;
	JsonBuilder *builder;
	gchar *sexp, *json;
	guint ii;

	if (start_unix > 0 && end_unix > 0 && end_unix >= start_unix) {
		gchar *iso_start = isodate_from_time_t ((time_t) start_unix);
		gchar *iso_end = isodate_from_time_t ((time_t) end_unix);

		sexp = g_strdup_printf ("(occur-in-time-range? (make-time \"%s\") (make-time \"%s\"))",
			iso_start, iso_end);
		g_free (iso_start);
		g_free (iso_end);
	} else {
		sexp = g_strdup ("#t");
	}

	items = g_ptr_array_new_with_free_func (cal_event_item_free);
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_CALENDAR);

	for (slink = sources; slink; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		EClient *client;
		GSList *objects = NULL, *olink;

		client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, core->cancellable, NULL);
		if (!client)
			continue;

		if (e_cal_client_get_object_list_sync (E_CAL_CLIENT (client), sexp, &objects, core->cancellable, NULL)) {
			for (olink = objects; olink; olink = g_slist_next (olink)) {
				ICalComponent *icomp = olink->data;
				ICalTime *dtstart, *dtend;
				ICalProperty *desc_prop;
				CalEventItem *item;
				gint64 st = 0, et = 0;

				if (!icomp || ! i_cal_component_get_uid (icomp))
					continue;

				dtstart = i_cal_component_get_dtstart (icomp);
				dtend = i_cal_component_get_dtend (icomp);
				if (dtstart && !i_cal_time_is_null_time (dtstart))
					st = (gint64) i_cal_time_as_timet (dtstart);
				if (dtend && !i_cal_time_is_null_time (dtend))
					et = (gint64) i_cal_time_as_timet (dtend);
				else
					et = st + 3600;

				g_clear_object (&dtstart);
				g_clear_object (&dtend);

				desc_prop = i_cal_component_get_first_property (icomp, I_CAL_DESCRIPTION_PROPERTY);

				item = g_new0 (CalEventItem, 1);
				item->uid = g_strdup (i_cal_component_get_uid (icomp));
				item->summary = g_strdup (i_cal_component_get_summary (icomp) ? i_cal_component_get_summary (icomp) : "(Sans titre)");
				item->location = g_strdup (i_cal_component_get_location (icomp) ? i_cal_component_get_location (icomp) : "");
				item->description = g_strdup (desc_prop && i_cal_property_get_description (desc_prop) ? i_cal_property_get_description (desc_prop) : "");
				item->start = st;
				item->end = et;
				item->calendar_uid = g_strdup (e_source_get_uid (source));
				item->calendar_name = g_strdup (e_source_get_display_name (source));

				g_clear_object (&desc_prop);
				g_ptr_array_add (items, item);
			}
			g_slist_free_full (objects, g_object_unref);
		}

		g_object_unref (client);
	}

	g_list_free_full (sources, g_object_unref);
	g_free (sexp);

	g_ptr_array_sort (items, compare_cal_events);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (ii = 0; ii < items->len; ii++) {
		CalEventItem *item = items->pdata[ii];

		json_builder_begin_object (builder);
		builder_add_string (builder, "uid", item->uid);
		builder_add_string (builder, "summary", item->summary);
		builder_add_string (builder, "location", item->location);
		builder_add_string (builder, "description", item->description);
		json_builder_set_member_name (builder, "start");
		json_builder_add_int_value (builder, item->start);
		json_builder_set_member_name (builder, "end");
		json_builder_add_int_value (builder, item->end);
		builder_add_string (builder, "calendar_uid", item->calendar_uid);
		builder_add_string (builder, "calendar_name", item->calendar_name);
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_ptr_array_unref (items);

	return json;
}

/* Crée un nouvel événement VEVENT dans un agenda evolution-data-server.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON décrivant l'événement
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{\"uid\":\"...\"}" en cas de succès, NULL sinon. */
static gchar *
core_create_event_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *cal_uid, *summary, *location, *description;
	gint64 start_unix, end_unix;
	ESource *source = NULL;
	EClient *client = NULL;
	ICalComponent *icomp = NULL;
	ICalTime *dtstart, *dtend;
	ICalTimezone *utc_zone;
	gchar *new_uid = NULL, *created_uid = NULL, *json = NULL;
	JsonBuilder *builder;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		json_node_unref (root);
		return NULL;
	}

	cal_uid = json_object_get_string_member_with_default (object, "calendar_uid", "");
	summary = json_object_get_string_member_with_default (object, "summary", "");
	location = json_object_get_string_member_with_default (object, "location", "");
	description = json_object_get_string_member_with_default (object, "description", "");
	start_unix = json_object_get_int_member_with_default (object, "start", (gint64) time (NULL));
	end_unix = json_object_get_int_member_with_default (object, "end", start_unix + 3600);
	if (end_unix <= start_unix)
		end_unix = start_unix + 3600;

	if (cal_uid && *cal_uid)
		source = e_source_registry_ref_source (core->registry, cal_uid);
	if (!source)
		source = e_source_registry_ref_default_calendar (core->registry);

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Aucun agenda disponible");
		goto exit;
	}

	client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, core->cancellable, error);
	if (!client)
		goto exit;

	utc_zone = i_cal_timezone_get_utc_timezone ();
	icomp = i_cal_component_new_vevent ();

	new_uid = e_util_generate_uid ();
	i_cal_component_set_uid (icomp, new_uid);
	i_cal_component_set_summary (icomp, (summary && *summary) ? summary : "(Sans titre)");

	if (location && *location)
		i_cal_component_set_location (icomp, location);

	if (description && *description) {
		ICalProperty *prop = i_cal_property_new_description (description);
		i_cal_component_take_property (icomp, prop);
	}

	dtstart = i_cal_time_new_from_timet_with_zone ((time_t) start_unix, FALSE, utc_zone);
	dtend = i_cal_time_new_from_timet_with_zone ((time_t) end_unix, FALSE, utc_zone);
	i_cal_component_set_dtstart (icomp, dtstart);
	i_cal_component_set_dtend (icomp, dtend);
	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	if (!e_cal_client_create_object_sync (E_CAL_CLIENT (client), icomp, E_CAL_OPERATION_FLAG_NONE,
		&created_uid, core->cancellable, error))
		goto exit;

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", (created_uid && *created_uid) ? created_uid : new_uid);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_free (created_uid);
	g_free (new_uid);
	g_clear_object (&icomp);
	g_clear_object (&client);
	g_clear_object (&source);
	json_node_unref (root);

	return json;
}

/* Supprime un événement d'un agenda evolution-data-server.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = calendar_uid, [1] = event_uid
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_delete_event_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	const gchar *cal_uid = args[0];
	const gchar *event_uid = args[1];
	ESource *source = NULL;
	EClient *client;
	gboolean ok;

	if (cal_uid && *cal_uid)
		source = e_source_registry_ref_source (core->registry, cal_uid);
	if (!source)
		source = e_source_registry_ref_default_calendar (core->registry);

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Agenda introuvable");
		return NULL;
	}

	client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, core->cancellable, error);
	g_object_unref (source);
	if (!client)
		return NULL;

	ok = e_cal_client_remove_object_sync (E_CAL_CLIENT (client), event_uid, NULL,
		E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, core->cancellable, error);

	g_object_unref (client);

	return ok ? g_strdup ("{}") : NULL;
}

/* --- contacts (EBookClient / libebook-1.2) --- */

typedef struct {
	gchar *uid;
	gchar *full_name;
	gchar *email;
	gchar *phone;
	gchar *org;
	gchar *notes;
	gchar *book_uid;
	gchar *book_name;
} ContactItem;

/* Libère un élément de contact temporaire.
 * Paramètres :
 *   data : pointeur vers ContactItem */
static void
contact_item_free (gpointer data)
{
	ContactItem *item = data;

	if (!item)
		return;

	g_free (item->uid);
	g_free (item->full_name);
	g_free (item->email);
	g_free (item->phone);
	g_free (item->org);
	g_free (item->notes);
	g_free (item->book_uid);
	g_free (item->book_name);
	g_free (item);
}

/* Compare deux contacts par nom complet alphabétique.
 * Paramètres :
 *   a, b : pointeurs vers ContactItem *
 * Retourne : résultat de g_utf8_collate. */
static gint
compare_contacts (gconstpointer a,
		  gconstpointer b)
{
	const ContactItem *ca = *(const ContactItem **) a;
	const ContactItem *cb = *(const ContactItem **) b;

	return g_utf8_collate (ca->full_name ? ca->full_name : "", cb->full_name ? cb->full_name : "");
}

/* Liste ou recherche les contacts dans tous les carnets d'adresses activés.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = query
 *   error : emplacement pour une éventuelle erreur
 * Retourne : tableau JSON des contacts. */
static gchar *
core_list_contacts_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	const gchar *query_text = args[0] ? args[0] : "";
	EBookQuery *book_query;
	gchar *sexp;
	GList *sources, *slink;
	GPtrArray *items;
	JsonBuilder *builder;
	gchar *json;
	guint ii;

	book_query = e_book_query_any_field_contains (query_text);
	sexp = book_query ? e_book_query_to_string (book_query) : g_strdup ("");
	if (book_query)
		e_book_query_unref (book_query);

	items = g_ptr_array_new_with_free_func (contact_item_free);
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_ADDRESS_BOOK);

	for (slink = sources; slink; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		EClient *client;
		GSList *contacts = NULL, *clink;

		client = e_book_client_connect_sync (source, 5, core->cancellable, NULL);
		if (!client)
			continue;

		if (e_book_client_get_contacts_sync (E_BOOK_CLIENT (client), sexp, &contacts, core->cancellable, NULL)) {
			for (clink = contacts; clink; clink = g_slist_next (clink)) {
				EContact *contact = clink->data;
				const gchar *uid, *full_name, *email, *phone, *org, *notes;
				ContactItem *item;

				if (!E_IS_CONTACT (contact))
					continue;

				uid = e_contact_get_const (contact, E_CONTACT_UID);
				if (!uid || !*uid)
					continue;

				full_name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
				if (!full_name || !*full_name)
					full_name = e_contact_get_const (contact, E_CONTACT_FILE_AS);
				email = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
				phone = e_contact_get_const (contact, E_CONTACT_PHONE_MOBILE);
				if (!phone || !*phone)
					phone = e_contact_get_const (contact, E_CONTACT_PHONE_BUSINESS);
				if (!phone || !*phone)
					phone = e_contact_get_const (contact, E_CONTACT_PHONE_HOME);
				org = e_contact_get_const (contact, E_CONTACT_ORG);
				notes = e_contact_get_const (contact, E_CONTACT_NOTE);

				item = g_new0 (ContactItem, 1);
				item->uid = g_strdup (uid);
				item->full_name = g_strdup ((full_name && *full_name) ? full_name : (email ? email : "(Sans nom)"));
				item->email = g_strdup (email ? email : "");
				item->phone = g_strdup (phone ? phone : "");
				item->org = g_strdup (org ? org : "");
				item->notes = g_strdup (notes ? notes : "");
				item->book_uid = g_strdup (e_source_get_uid (source));
				item->book_name = g_strdup (e_source_get_display_name (source));

				g_ptr_array_add (items, item);
			}
			g_slist_free_full (contacts, g_object_unref);
		}

		g_object_unref (client);
	}

	g_list_free_full (sources, g_object_unref);
	g_free (sexp);

	g_ptr_array_sort (items, compare_contacts);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (ii = 0; ii < items->len; ii++) {
		ContactItem *item = items->pdata[ii];

		json_builder_begin_object (builder);
		builder_add_string (builder, "uid", item->uid);
		builder_add_string (builder, "full_name", item->full_name);
		builder_add_string (builder, "email", item->email);
		builder_add_string (builder, "phone", item->phone);
		builder_add_string (builder, "org", item->org);
		builder_add_string (builder, "notes", item->notes);
		builder_add_string (builder, "book_uid", item->book_uid);
		builder_add_string (builder, "book_name", item->book_name);
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_ptr_array_unref (items);

	return json;
}

/* Crée un nouveau contact dans un carnet d'adresses evolution-data-server.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON décrivant le contact
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{\"uid\":\"...\"}" en cas de succès, NULL sinon. */
static gchar *
core_create_contact_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *book_uid, *full_name, *email, *phone, *org, *notes;
	ESource *source = NULL;
	EClient *client = NULL;
	EContact *contact = NULL;
	gchar *created_uid = NULL, *json = NULL;
	JsonBuilder *builder;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		json_node_unref (root);
		return NULL;
	}

	book_uid = json_object_get_string_member_with_default (object, "book_uid", "");
	full_name = json_object_get_string_member_with_default (object, "full_name", "");
	email = json_object_get_string_member_with_default (object, "email", "");
	phone = json_object_get_string_member_with_default (object, "phone", "");
	org = json_object_get_string_member_with_default (object, "org", "");
	notes = json_object_get_string_member_with_default (object, "notes", "");

	if ((!full_name || !*full_name) && (!email || !*email)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Le nom ou l'adresse e-mail du contact est obligatoire");
		json_node_unref (root);
		return NULL;
	}

	if (book_uid && *book_uid)
		source = e_source_registry_ref_source (core->registry, book_uid);
	if (!source)
		source = e_source_registry_ref_default_address_book (core->registry);

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Aucun carnet d'adresses disponible");
		goto exit;
	}

	client = e_book_client_connect_sync (source, 5, core->cancellable, error);
	if (!client)
		goto exit;

	contact = e_contact_new ();
	e_contact_set (contact, E_CONTACT_FULL_NAME, (full_name && *full_name) ? full_name : email);
	if (email && *email)
		e_contact_set (contact, E_CONTACT_EMAIL_1, email);
	if (phone && *phone)
		e_contact_set (contact, E_CONTACT_PHONE_MOBILE, phone);
	if (org && *org)
		e_contact_set (contact, E_CONTACT_ORG, org);
	if (notes && *notes)
		e_contact_set (contact, E_CONTACT_NOTE, notes);

	if (!e_book_client_add_contact_sync (E_BOOK_CLIENT (client), contact, E_BOOK_OPERATION_FLAG_NONE,
		&created_uid, core->cancellable, error))
		goto exit;

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", created_uid ? created_uid : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_free (created_uid);
	g_clear_object (&contact);
	g_clear_object (&client);
	g_clear_object (&source);
	json_node_unref (root);

	return json;
}

/* Supprime un contact d'un carnet d'adresses evolution-data-server.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = book_uid, [1] = contact_uid
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_delete_contact_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	const gchar *book_uid = args[0];
	const gchar *contact_uid = args[1];
	ESource *source = NULL;
	EClient *client;
	gboolean ok;

	if (book_uid && *book_uid)
		source = e_source_registry_ref_source (core->registry, book_uid);
	if (!source)
		source = e_source_registry_ref_default_address_book (core->registry);

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Carnet d'adresses introuvable");
		return NULL;
	}

	client = e_book_client_connect_sync (source, 5, core->cancellable, error);
	g_object_unref (source);
	if (!client)
		return NULL;

	ok = e_book_client_remove_contact_by_uid_sync (E_BOOK_CLIENT (client), contact_uid,
		E_BOOK_OPERATION_FLAG_NONE, core->cancellable, error);

	g_object_unref (client);

	return ok ? g_strdup ("{}") : NULL;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

EvoCore *
evo_core_new (EvoCoreEventFunc event_func,
	      void *user_data)
{
	EvoCore *core = g_new0 (EvoCore, 1);

	core_log_open ();
	g_message ("evo-core starting");

	core->event_func = event_func;
	core->event_data = user_data;
	core->context = g_main_context_new ();
	core->loop = g_main_loop_new (core->context, FALSE);
	core->cancellable = g_cancellable_new ();
	g_mutex_init (&core->lock);
	g_cond_init (&core->cond);
	g_mutex_init (&core->folders_lock);
	core->folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	/* network requests run in parallel, a few at a time */
	core->pool = g_thread_pool_new (core_request_run, core, 4, FALSE, NULL);
	core->thread = g_thread_new ("evo-core", core_thread_func, core);

	return core;
}

static gboolean
core_quit_cb (gpointer user_data)
{
	g_main_loop_quit (user_data);
	return G_SOURCE_REMOVE;
}

void
evo_core_free (EvoCore *core)
{
	if (!core)
		return;

	g_cancellable_cancel (core->cancellable);

	/* running requests finish (cancelled), queued ones are dropped */
	g_thread_pool_free (core->pool, TRUE, TRUE);

	g_main_context_invoke (core->context, core_quit_cb, core->loop);
	g_thread_join (core->thread);

	/* no "folder-changed" after this point */
	{
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init (&iter, core->folders);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			g_signal_handlers_disconnect_by_func (value, core_folder_changed_cb, core);
	}
	g_hash_table_destroy (core->folders);
	g_mutex_clear (&core->folders_lock);

	g_clear_object (&core->session);
	g_clear_object (&core->registry);
	g_main_loop_unref (core->loop);
	g_main_context_unref (core->context);
	g_object_unref (core->cancellable);
	g_mutex_clear (&core->lock);
	g_cond_clear (&core->cond);
	g_free (core->start_error);
	g_free (core);
}

void
evo_core_list_accounts (EvoCore *core,
			EvoCoreResultFunc func,
			void *user_data)
{
	core_submit (core, core_list_accounts_run, g_new0 (gchar *, 1), func, user_data);
}

void
evo_core_list_folders (EvoCore *core,
		       const char *account_uid,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, NULL };

	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_list_folders_run, g_strdupv (args), func, user_data);
}

void
evo_core_list_messages (EvoCore *core,
			const char *account_uid,
			const char *folder_full_name,
			unsigned int max_count,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar count[16];
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, count, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);

	g_snprintf (count, sizeof (count), "%u", max_count);
	core_submit (core, core_list_messages_run, g_strdupv (args), func, user_data);
}

void
evo_core_refresh (EvoCore *core)
{
	core_submit (core, core_refresh_run, g_new0 (gchar *, 1), core_ignore_result, NULL);
}

void
evo_core_send_message (EvoCore *core,
		       const char *message_json,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) message_json, NULL };

	g_return_if_fail (message_json != NULL);

	core_submit (core, core_send_message_run, g_strdupv (args), func, user_data);
}

void
evo_core_get_message (EvoCore *core,
		      const char *account_uid,
		      const char *folder_full_name,
		      const char *message_uid,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, (gchar *) message_uid, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);

	core_submit (core, core_get_message_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de modification d'indicateur ("seen" ou "flagged") sur
 * un message.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message
 *   flag_name        : nom de l'indicateur ("seen" ou "flagged")
 *   value            : valeur non nulle pour activer, 0 pour désactiver
 *   func             : rappel recevant l'état JSON des indicateurs
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_set_message_flags (EvoCore *core,
			    const char *account_uid,
			    const char *folder_full_name,
			    const char *message_uid,
			    const char *flag_name,
			    int value,
			    EvoCoreResultFunc func,
			    void *user_data)
{
	gchar *args[] = {
		(gchar *) account_uid,
		(gchar *) folder_full_name,
		(gchar *) message_uid,
		(gchar *) flag_name,
		value ? "1" : "0",
		NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);
	g_return_if_fail (flag_name != NULL);

	core_submit (core, core_set_message_flags_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de suppression d'un message (vers la corbeille ou
 * définitive).
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message à supprimer
 *   func             : rappel recevant {"trashed": true|false}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_delete_message (EvoCore *core,
			 const char *account_uid,
			 const char *folder_full_name,
			 const char *message_uid,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, (gchar *) message_uid, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);

	core_submit (core, core_delete_message_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de déplacement d'un message vers un autre dossier.
 * Paramètres :
 *   core                  : instance du moteur evo-core
 *   src_account_uid       : identifiant du compte source
 *   src_folder_full_name  : chemin complet du dossier source
 *   message_uid           : identifiant du message à déplacer
 *   dest_account_uid      : identifiant du compte de destination
 *   dest_folder_full_name : chemin complet du dossier de destination
 *   func                  : rappel recevant {}
 *   user_data             : données utilisateur transmises au rappel */
void
evo_core_move_message (EvoCore *core,
		       const char *src_account_uid,
		       const char *src_folder_full_name,
		       const char *message_uid,
		       const char *dest_account_uid,
		       const char *dest_folder_full_name,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = {
		(gchar *) src_account_uid,
		(gchar *) src_folder_full_name,
		(gchar *) message_uid,
		(gchar *) dest_account_uid,
		(gchar *) dest_folder_full_name,
		NULL
	};

	g_return_if_fail (src_account_uid != NULL);
	g_return_if_fail (src_folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);
	g_return_if_fail (dest_account_uid != NULL);
	g_return_if_fail (dest_folder_full_name != NULL);

	core_submit (core, core_move_message_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de recherche de messages dans un dossier.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   query            : texte recherché
 *   max_count        : nombre maximal de messages renvoyés (0 pour tous)
 *   func             : rappel recevant le tableau JSON des messages
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_search_messages (EvoCore *core,
			  const char *account_uid,
			  const char *folder_full_name,
			  const char *query,
			  unsigned int max_count,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar count[16];
	gchar *args[] = {
		(gchar *) account_uid,
		(gchar *) folder_full_name,
		(gchar *) (query ? query : ""),
		count,
		NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);

	g_snprintf (count, sizeof (count), "%u", max_count);
	core_submit (core, core_search_messages_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête d'enregistrement d'une pièce jointe d'un message dans un
 * fichier local.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message
 *   attachment_index : indice de la pièce jointe
 *   dest_path        : chemin complet du fichier de destination
 *   func             : rappel recevant {"size": <octets>}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_save_attachment (EvoCore *core,
			  const char *account_uid,
			  const char *folder_full_name,
			  const char *message_uid,
			  unsigned int attachment_index,
			  const char *dest_path,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar idx[16];
	gchar *args[] = {
		(gchar *) account_uid,
		(gchar *) folder_full_name,
		(gchar *) message_uid,
		idx,
		(gchar *) dest_path,
		NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);
	g_return_if_fail (dest_path != NULL);

	g_snprintf (idx, sizeof (idx), "%u", attachment_index);
	core_submit (core, core_save_attachment_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête d'exportation d'un ou plusieurs messages sous forme de
 * fichiers .eml temporaires.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : JSON contenant {"account": "...", "folder": "...", "uids": [...]}
 *   func         : rappel recevant le tableau JSON des chemins .eml créés
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_export_messages_eml (EvoCore *core,
			      const char *request_json,
			      EvoCoreResultFunc func,
			      void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	core_submit (core, core_export_messages_eml_run, g_strdupv (args), func, user_data);
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

/* Soumet une requête de suppression d'un compte de messagerie.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant du compte à supprimer
 *   func        : rappel recevant {}
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_remove_account (EvoCore *core,
			 const char *account_uid,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, NULL };

	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_remove_account_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de lecture des événements des agendas activés.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   start_unix : début de la plage temporelle en secondes Unix
 *   end_unix   : fin de la plage temporelle en secondes Unix
 *   func       : rappel recevant le tableau JSON des événements
 *   user_data  : données utilisateur transmises au rappel */
void
evo_core_list_events (EvoCore *core,
		      long long start_unix,
		      long long end_unix,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar start_buf[32], end_buf[32];
	gchar *args[] = { start_buf, end_buf, NULL };

	g_snprintf (start_buf, sizeof (start_buf), "%" G_GINT64_FORMAT, (gint64) start_unix);
	g_snprintf (end_buf, sizeof (end_buf), "%" G_GINT64_FORMAT, (gint64) end_unix);

	core_submit (core, core_list_events_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de création d'un événement dans un agenda.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   event_json : description JSON de l'événement
 *   func       : rappel recevant {"uid": "..."}
 *   user_data  : données utilisateur transmises au rappel */
void
evo_core_create_event (EvoCore *core,
		       const char *event_json,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) event_json, NULL };

	g_return_if_fail (event_json != NULL);

	core_submit (core, core_create_event_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de suppression d'un événement d'agenda.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   calendar_uid : identifiant de l'agenda
 *   event_uid    : identifiant de l'événement
 *   func         : rappel recevant {}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_delete_event (EvoCore *core,
		       const char *calendar_uid,
		       const char *event_uid,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) (calendar_uid ? calendar_uid : ""), (gchar *) event_uid, NULL };

	g_return_if_fail (event_uid != NULL);

	core_submit (core, core_delete_event_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de liste ou recherche de contacts.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   query     : texte recherché (ou "" / NULL pour tous les contacts)
 *   func      : rappel recevant le tableau JSON des contacts
 *   user_data : données utilisateur transmises au rappel */
void
evo_core_list_contacts (EvoCore *core,
			const char *query,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = { (gchar *) (query ? query : ""), NULL };

	core_submit (core, core_list_contacts_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de création d'un contact.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   contact_json : description JSON du contact
 *   func         : rappel recevant {"uid": "..."}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_create_contact (EvoCore *core,
			 const char *contact_json,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) contact_json, NULL };

	g_return_if_fail (contact_json != NULL);

	core_submit (core, core_create_contact_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de suppression d'un contact.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   book_uid    : identifiant du carnet d'adresses
 *   contact_uid : identifiant du contact
 *   func        : rappel recevant {}
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_delete_contact (EvoCore *core,
			 const char *book_uid,
			 const char *contact_uid,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) (book_uid ? book_uid : ""), (gchar *) contact_uid, NULL };

	g_return_if_fail (contact_uid != NULL);

	core_submit (core, core_delete_contact_run, g_strdupv (args), func, user_data);
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
