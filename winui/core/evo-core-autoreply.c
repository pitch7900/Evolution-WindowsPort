/*
 * evo-core : réponses automatiques d'absence (Out of Office).
 *   - Microsoft 365 : API Microsoft Graph (mailboxSettings/automaticRepliesSetting),
 *     jeton OAuth2 du service "Microsoft365" de la collection du compte ;
 *   - Gmail / Google Workspace : API Gmail users.settings.getVacation / updateVacation,
 *     jeton OAuth2 du service "Google" de la collection ;
 *   - IMAP générique, Exchange sur site (EWS) : non pris en charge (erreur explicite).
 * Voir evo-core.h et evo-core-private.h ; fonctions pures dans evo-core-autoreply-util.h.
 */

#include "evo-core-private.h"
#include "evo-core-autoreply-util.h"

#define AUTOREPLY_GRAPH_URI "https://graph.microsoft.com/v1.0/me/mailboxSettings"
#define AUTOREPLY_GMAIL_URI "https://gmail.googleapis.com/gmail/v1/users/me/settings/vacation"

typedef enum {
	AUTOREPLY_PROVIDER_MICROSOFT365,
	AUTOREPLY_PROVIDER_GMAIL
} AutoReplyProvider;

/* Réglages d'absence, indépendants du fournisseur. */
typedef struct {
	gboolean enabled;
	const gchar *external_audience;	/* "none", "known" ou "all" (statique) */
	gint64 start;			/* secondes Unix, 0 = sans limite */
	gint64 end;
	gchar *subject;			/* Gmail seulement */
	gchar *internal_message;	/* texte brut */
	gchar *external_message;
} AutoReplySettings;

static void
autoreply_settings_clear (AutoReplySettings *settings)
{
	g_clear_pointer (&settings->subject, g_free);
	g_clear_pointer (&settings->internal_message, g_free);
	g_clear_pointer (&settings->external_message, g_free);
}

/* Retrouve une source du registre pour le service OAuth2. */
static ESource *
autoreply_ref_source_cb (gpointer user_data,
			 const gchar *uid)
{
	return e_source_registry_ref_source (user_data, uid);
}

/* Fournisseur et jeton OAuth2 d'un compte.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   account_uid  : identifiant du compte
 *   out_provider : reçoit le fournisseur
 *   out_token    : reçoit le jeton d'accès (g_free)
 *   error        : emplacement pour une éventuelle erreur
 * Retourne : TRUE si le compte est pris en charge et qu'un jeton a été obtenu. */
static gboolean
autoreply_get_token (EvoCore *core,
		     const gchar *account_uid,
		     AutoReplyProvider *out_provider,
		     gchar **out_token,
		     GError **error)
{
	ESource *source;
	EOAuth2Service *oauth2;
	const gchar *backend = NULL;
	gint expires_in = 0;
	GError *local_error = NULL;
	gboolean ok;

	source = core_ref_credentials_source (core, account_uid, error);
	if (!source)
		return FALSE;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION))
		backend = e_source_backend_get_backend_name (E_SOURCE_BACKEND (
			e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION)));

	if (g_strcmp0 (backend, "microsoft365") == 0) {
		*out_provider = AUTOREPLY_PROVIDER_MICROSOFT365;
	} else if (g_strcmp0 (backend, "google") == 0) {
		*out_provider = AUTOREPLY_PROVIDER_GMAIL;
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s",
			g_strcmp0 (backend, "ews") == 0
			? "Les réponses automatiques ne sont pas prises en charge pour un compte Exchange sur site (EWS) ; utilisez Microsoft 365 ou Gmail."
			: "Les réponses automatiques ne sont prises en charge que pour les comptes Microsoft 365 et Gmail (pas pour les comptes IMAP génériques).");
		g_object_unref (source);
		return FALSE;
	}

	oauth2 = e_oauth2_services_find (e_source_registry_get_oauth2_services (core->registry), source);
	if (!oauth2) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Ce compte n'utilise pas l'authentification OAuth2 : les réponses automatiques sont indisponibles.");
		g_object_unref (source);
		return FALSE;
	}

	ok = e_oauth2_service_get_access_token_sync (oauth2, source, autoreply_ref_source_cb,
		core->registry, out_token, &expires_in, core->cancellable, &local_error);
	if (!ok) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"Impossible d'obtenir le jeton d'accès du compte (reconnectez-le dans les propriétés du compte) : %s",
			local_error ? local_error->message : "jeton absent");
		g_clear_error (&local_error);
	}

	g_object_unref (oauth2);
	g_object_unref (source);
	return ok;
}

/* Message d'erreur d'une réponse Graph ({"error": {"message": ...}}) ou Gmail
 * (même structure), NULL si absent. */
static gchar *
autoreply_response_error_message (GBytes *bytes)
{
	JsonNode *root;
	gchar *message = NULL;
	gsize size = 0;
	const gchar *data;

	if (!bytes)
		return NULL;

	data = g_bytes_get_data (bytes, &size);
	root = json_from_string (data ? data : "", NULL);
	if (root && JSON_NODE_HOLDS_OBJECT (root)) {
		JsonObject *object = json_node_get_object (root);

		if (json_object_has_member (object, "error") &&
		    JSON_NODE_HOLDS_OBJECT (json_object_get_member (object, "error")))
			message = g_strdup (json_object_get_string_member_with_default (
				json_object_get_object_member (object, "error"), "message", ""));
	}
	if (root)
		json_node_unref (root);
	if (message && !*message)
		g_clear_pointer (&message, g_free);

	return message;
}

/* Requête HTTPS authentifiée (Bearer) vers l'API du fournisseur.
 * Paramètres :
 *   core     : instance du moteur evo-core
 *   provider : fournisseur (pour les messages d'erreur)
 *   method   : "GET", "PATCH" ou "PUT"
 *   uri      : adresse de l'API
 *   token    : jeton d'accès OAuth2
 *   body     : corps JSON, NULL pour une lecture
 *   error    : emplacement pour une éventuelle erreur
 * Retourne : le corps de la réponse (succès 2xx), NULL sinon. */
static GBytes *
autoreply_http (EvoCore *core,
		AutoReplyProvider provider,
		const gchar *method,
		const gchar *uri,
		const gchar *token,
		const gchar *body,
		GError **error)
{
	SoupSession *soup;
	SoupMessage *msg;
	GBytes *bytes;
	GError *local_error = NULL;
	gchar *authorization;
	guint status;

	soup = soup_session_new_with_options ("timeout", 30, "user-agent", "Evolution (Windows)", NULL);
	msg = soup_message_new (method, uri);
	authorization = g_strdup_printf ("Bearer %s", token);
	soup_message_headers_replace (soup_message_get_request_headers (msg), "Authorization", authorization);
	soup_message_headers_replace (soup_message_get_request_headers (msg), "Accept", "application/json");
	g_free (authorization);

	if (body) {
		GBytes *request_body = g_bytes_new (body, strlen (body));

		soup_message_set_request_body_from_bytes (msg, "application/json", request_body);
		g_bytes_unref (request_body);
	}

	bytes = soup_session_send_and_read (soup, msg, core->cancellable, &local_error);
	status = soup_message_get_status (msg);

	if (!bytes) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND,
			"Le serveur est injoignable : %s", local_error ? local_error->message : "erreur réseau");
	} else if (status < 200 || status >= 300) {
		gchar *detail = autoreply_response_error_message (bytes);

		if (status == 401)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
				"Authentification refusée : reconnectez le compte dans ses propriétés.");
		else if (status == 403)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
				provider == AUTOREPLY_PROVIDER_GMAIL
				? "Gmail : autorisation manquante. Supprimez puis ajoutez de nouveau le compte Google pour accorder la gestion des réponses automatiques (%s)."
				: "Autorisation insuffisante : la permission « MailboxSettings.ReadWrite » est requise (consentement de l'administrateur possible) ; reconnectez le compte (%s).",
				detail ? detail : "accès refusé");
		else
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Le serveur a refusé la demande (HTTP %u) : %s", status, detail ? detail : "erreur inconnue");
		g_free (detail);
		g_clear_pointer (&bytes, g_bytes_unref);
	}

	g_clear_error (&local_error);
	g_object_unref (msg);
	g_object_unref (soup);

	return bytes;
}

/* Objet JSON racine d'une réponse HTTP, NULL (avec erreur) si invalide. */
static JsonNode *
autoreply_parse_response (GBytes *bytes,
			  GError **error)
{
	gsize size = 0;
	const gchar *data = g_bytes_get_data (bytes, &size);
	gchar *text = g_strndup (data ? data : "{}", size);
	JsonNode *root = json_from_string (text, error);

	g_free (text);
	if (root && !JSON_NODE_HOLDS_OBJECT (root)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Réponse inattendue du serveur");
		json_node_unref (root);
		return NULL;
	}

	return root;
}

/* Texte brut d'un message HTML Graph (chaîne vide si absent). */
static gchar *
autoreply_plain_from_html (const gchar *html)
{
	return html && *html ? html_to_text (html) : g_strdup ("");
}

/* HTML d'un texte brut (échappé, retours à la ligne en <br>). */
static gchar *
autoreply_html_from_plain (const gchar *text)
{
	gchar *escaped = g_markup_escape_text (text ? text : "", -1);
	gchar **lines;
	gchar *joined, *html;

	g_strdelimit (escaped, "\r", ' ');
	lines = g_strsplit (escaped, "\n", -1);
	joined = g_strjoinv ("<br>", lines);
	html = g_strdup_printf ("<html><body>%s</body></html>", joined);

	g_strfreev (lines);
	g_free (joined);
	g_free (escaped);
	return html;
}

/* Lecture Microsoft Graph. Retourne FALSE (avec erreur) en cas d'échec. */
static gboolean
autoreply_graph_get (EvoCore *core,
		     const gchar *token,
		     AutoReplySettings *settings,
		     GError **error)
{
	GBytes *bytes;
	JsonNode *root;
	JsonObject *object;
	const gchar *status;
	gchar *uri;

	/* GET sur mailboxSettings entier : l'objet automaticRepliesSetting y est imbriqué */
	uri = g_strdup (AUTOREPLY_GRAPH_URI);
	bytes = autoreply_http (core, AUTOREPLY_PROVIDER_MICROSOFT365, "GET", uri, token, NULL, error);
	g_free (uri);
	if (!bytes)
		return FALSE;

	root = autoreply_parse_response (bytes, error);
	g_bytes_unref (bytes);
	if (!root)
		return FALSE;

	object = json_node_get_object (root);
	if (json_object_has_member (object, "automaticRepliesSetting") &&
	    JSON_NODE_HOLDS_OBJECT (json_object_get_member (object, "automaticRepliesSetting")))
		object = json_object_get_object_member (object, "automaticRepliesSetting");
	status = json_object_get_string_member_with_default (object, "status", "disabled");
	settings->enabled = g_strcmp0 (status, "disabled") != 0;
	settings->external_audience = autoreply_audience_from_graph (
		json_object_get_string_member_with_default (object, "externalAudience", "all"));
	settings->internal_message = autoreply_plain_from_html (
		json_object_get_string_member_with_default (object, "internalReplyMessage", ""));
	settings->external_message = autoreply_plain_from_html (
		json_object_get_string_member_with_default (object, "externalReplyMessage", ""));
	settings->subject = g_strdup ("");

	g_message ("auto-reply Graph : status=%s, message interne=%u octets, externe=%u octets (texte), champs=%u",
		status, (guint) strlen (settings->internal_message), (guint) strlen (settings->external_message),
		json_object_get_size (object));

	/* Graph renvoie toujours des dates ; elles ne comptent qu'en mode "scheduled" */
	if (g_strcmp0 (status, "scheduled") == 0) {
		JsonObject *start = json_object_has_member (object, "scheduledStartDateTime")
			? json_object_get_object_member (object, "scheduledStartDateTime") : NULL;
		JsonObject *end = json_object_has_member (object, "scheduledEndDateTime")
			? json_object_get_object_member (object, "scheduledEndDateTime") : NULL;

		settings->start = start ? autoreply_epoch_from_iso_utc (
			json_object_get_string_member_with_default (start, "dateTime", "")) : 0;
		settings->end = end ? autoreply_epoch_from_iso_utc (
			json_object_get_string_member_with_default (end, "dateTime", "")) : 0;
	}

	json_node_unref (root);
	return TRUE;
}

/* Écriture Microsoft Graph (PATCH mailboxSettings). */
static gboolean
autoreply_graph_set (EvoCore *core,
		     const gchar *token,
		     const AutoReplySettings *settings,
		     GError **error)
{
	JsonBuilder *builder = json_builder_new ();
	gboolean scheduled = settings->enabled && settings->start > 0 && settings->end > 0;
	gchar *html, *body;
	gchar iso[40];
	GBytes *bytes;

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "automaticRepliesSetting");
	json_builder_begin_object (builder);
	builder_add_string (builder, "status", !settings->enabled ? "disabled" : (scheduled ? "scheduled" : "alwaysEnabled"));
	builder_add_string (builder, "externalAudience", autoreply_audience_to_graph (settings->external_audience));

	html = autoreply_html_from_plain (settings->internal_message);
	builder_add_string (builder, "internalReplyMessage", html);
	g_free (html);
	html = autoreply_html_from_plain (settings->external_message);
	builder_add_string (builder, "externalReplyMessage", html);
	g_free (html);

	if (scheduled) {
		json_builder_set_member_name (builder, "scheduledStartDateTime");
		json_builder_begin_object (builder);
		autoreply_iso_from_epoch_utc (settings->start, iso, sizeof (iso));
		builder_add_string (builder, "dateTime", iso);
		builder_add_string (builder, "timeZone", "UTC");
		json_builder_end_object (builder);
		json_builder_set_member_name (builder, "scheduledEndDateTime");
		json_builder_begin_object (builder);
		autoreply_iso_from_epoch_utc (settings->end, iso, sizeof (iso));
		builder_add_string (builder, "dateTime", iso);
		builder_add_string (builder, "timeZone", "UTC");
		json_builder_end_object (builder);
	}

	json_builder_end_object (builder);
	json_builder_end_object (builder);
	body = json_from_builder (builder);
	g_object_unref (builder);

	bytes = autoreply_http (core, AUTOREPLY_PROVIDER_MICROSOFT365, "PATCH", AUTOREPLY_GRAPH_URI, token, body, error);
	g_free (body);
	if (!bytes)
		return FALSE;

	g_bytes_unref (bytes);
	return TRUE;
}

/* Valeur entière (texte ou nombre JSON, en millisecondes) d'un membre Gmail. */
static gint64
autoreply_gmail_int (JsonObject *object,
		     const gchar *name)
{
	JsonNode *node = json_object_has_member (object, name) ? json_object_get_member (object, name) : NULL;

	if (!node || !JSON_NODE_HOLDS_VALUE (node))
		return 0;
	if (json_node_get_value_type (node) == G_TYPE_STRING)
		return g_ascii_strtoll (json_node_get_string (node), NULL, 10);
	return json_node_get_int (node);
}

/* Lecture Gmail (users.settings.getVacation). */
static gboolean
autoreply_gmail_get (EvoCore *core,
		     const gchar *token,
		     AutoReplySettings *settings,
		     GError **error)
{
	GBytes *bytes = autoreply_http (core, AUTOREPLY_PROVIDER_GMAIL, "GET", AUTOREPLY_GMAIL_URI, token, NULL, error);
	JsonNode *root;
	JsonObject *object;
	const gchar *plain, *html;
	gchar *message;

	if (!bytes)
		return FALSE;

	root = autoreply_parse_response (bytes, error);
	g_bytes_unref (bytes);
	if (!root)
		return FALSE;

	object = json_node_get_object (root);
	settings->enabled = json_object_get_boolean_member_with_default (object, "enableAutoReply", FALSE);
	settings->external_audience = autoreply_audience_from_gmail (
		json_object_get_boolean_member_with_default (object, "restrictToContacts", FALSE),
		json_object_get_boolean_member_with_default (object, "restrictToDomain", FALSE));
	settings->start = autoreply_gmail_int (object, "startTime") / 1000;
	settings->end = autoreply_gmail_int (object, "endTime") / 1000;
	settings->subject = g_strdup (json_object_get_string_member_with_default (object, "responseSubject", ""));

	/* un seul message chez Gmail : il sert aux deux audiences */
	plain = json_object_get_string_member_with_default (object, "responseBodyPlainText", "");
	html = json_object_get_string_member_with_default (object, "responseBodyHtml", "");
	message = *plain ? g_strdup (plain) : autoreply_plain_from_html (html);
	settings->internal_message = g_strdup (message);
	settings->external_message = message;

	json_node_unref (root);
	return TRUE;
}

/* Écriture Gmail (users.settings.updateVacation, PUT). */
static gboolean
autoreply_gmail_set (EvoCore *core,
		     const gchar *token,
		     const AutoReplySettings *settings,
		     GError **error)
{
	JsonBuilder *builder = json_builder_new ();
	const gchar *message;
	gchar *body, *millis;
	int contacts, domain;
	GBytes *bytes;

	/* un seul message : celui des expéditeurs externes, sinon l'interne */
	message = settings->external_message && *settings->external_message
		? settings->external_message : settings->internal_message;
	autoreply_audience_to_gmail (settings->external_audience, &contacts, &domain);

	json_builder_begin_object (builder);
	builder_add_boolean (builder, "enableAutoReply", settings->enabled);
	builder_add_string (builder, "responseSubject", settings->subject ? settings->subject : "");
	builder_add_string (builder, "responseBodyPlainText", message ? message : "");
	builder_add_boolean (builder, "restrictToContacts", contacts);
	builder_add_boolean (builder, "restrictToDomain", domain);
	if (settings->start > 0) {
		millis = g_strdup_printf ("%" G_GINT64_FORMAT, settings->start * 1000);
		builder_add_string (builder, "startTime", millis);
		g_free (millis);
	}
	if (settings->end > 0) {
		millis = g_strdup_printf ("%" G_GINT64_FORMAT, settings->end * 1000);
		builder_add_string (builder, "endTime", millis);
		g_free (millis);
	}
	json_builder_end_object (builder);
	body = json_from_builder (builder);
	g_object_unref (builder);

	bytes = autoreply_http (core, AUTOREPLY_PROVIDER_GMAIL, "PUT", AUTOREPLY_GMAIL_URI, token, body, error);
	g_free (body);
	if (!bytes)
		return FALSE;

	g_bytes_unref (bytes);
	return TRUE;
}

/* JSON de réglages renvoyé à l'interface. */
static gchar *
autoreply_settings_to_json (AutoReplyProvider provider,
			    const AutoReplySettings *settings)
{
	JsonBuilder *builder = json_builder_new ();
	gchar *json;

	json_builder_begin_object (builder);
	builder_add_string (builder, "provider", provider == AUTOREPLY_PROVIDER_GMAIL ? "gmail" : "microsoft365");
	builder_add_boolean (builder, "single_message", provider == AUTOREPLY_PROVIDER_GMAIL);
	builder_add_boolean (builder, "enabled", settings->enabled);
	builder_add_string (builder, "external_audience", settings->external_audience);
	builder_add_int (builder, "start", settings->start);
	builder_add_int (builder, "end", settings->end);
	builder_add_string (builder, "subject", settings->subject ? settings->subject : "");
	builder_add_string (builder, "internal_message", settings->internal_message ? settings->internal_message : "");
	builder_add_string (builder, "external_message", settings->external_message ? settings->external_message : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	return json;
}

/* Lit les réponses automatiques d'un compte.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte
 *   error : emplacement pour une éventuelle erreur
 * Retourne : les réglages JSON (voir evo_core_get_auto_reply), NULL sinon. */
static gchar *
core_get_auto_reply_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	AutoReplySettings settings = { FALSE, "all", 0, 0, NULL, NULL, NULL };
	AutoReplyProvider provider;
	gchar *token = NULL, *json = NULL;
	gboolean ok;

	if (!autoreply_get_token (core, args[0], &provider, &token, error))
		return NULL;

	ok = provider == AUTOREPLY_PROVIDER_GMAIL
		? autoreply_gmail_get (core, token, &settings, error)
		: autoreply_graph_get (core, token, &settings, error);
	if (ok)
		json = autoreply_settings_to_json (provider, &settings);

	autoreply_settings_clear (&settings);
	e_util_safe_free_string (token);
	return json;
}

/* Enregistre les réponses automatiques d'un compte.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte, [1] = réglages JSON
 *   error : emplacement pour une éventuelle erreur
 * Retourne : les réglages enregistrés (JSON), NULL sinon. */
static gchar *
core_set_auto_reply_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	AutoReplySettings settings = { FALSE, "all", 0, 0, NULL, NULL, NULL };
	AutoReplyProvider provider;
	JsonNode *root;
	JsonObject *object;
	const gchar *audience;
	gchar *token = NULL, *json = NULL;
	gboolean ok;
	int range;

	root = json_from_string (args[1], error);
	if (!root)
		return NULL;
	if (!JSON_NODE_HOLDS_OBJECT (root)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Réglages de réponses automatiques invalides");
		json_node_unref (root);
		return NULL;
	}

	object = json_node_get_object (root);
	audience = json_object_get_string_member_with_default (object, "external_audience", "all");
	if (!autoreply_audience_valid (audience)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Audience externe invalide : « %s » (attendu : none, known ou all)", audience);
		json_node_unref (root);
		return NULL;
	}

	settings.enabled = json_object_get_boolean_member_with_default (object, "enabled", FALSE);
	settings.external_audience = !strcmp (audience, "none") ? "none" : (!strcmp (audience, "known") ? "known" : "all");
	settings.start = json_object_get_int_member_with_default (object, "start", 0);
	settings.end = json_object_get_int_member_with_default (object, "end", 0);
	settings.subject = g_strdup (json_object_get_string_member_with_default (object, "subject", ""));
	settings.internal_message = g_strdup (json_object_get_string_member_with_default (object, "internal_message", ""));
	settings.external_message = g_strdup (json_object_get_string_member_with_default (object, "external_message", ""));
	json_node_unref (root);

	if (!autoreply_get_token (core, args[0], &provider, &token, error))
		goto exit;

	range = autoreply_validate_range (settings.start, settings.end, provider == AUTOREPLY_PROVIDER_MICROSOFT365);
	if (range == AUTOREPLY_RANGE_END_BEFORE_START) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"La fin de la période d'absence doit être postérieure à son début");
		goto exit;
	}
	if (range == AUTOREPLY_RANGE_INCOMPLETE) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Microsoft 365 exige une date de début et une date de fin (ou aucune des deux)");
		goto exit;
	}
	if (settings.enabled && !*settings.internal_message && !*settings.external_message) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Saisissez au moins un message de réponse automatique");
		goto exit;
	}

	ok = provider == AUTOREPLY_PROVIDER_GMAIL
		? autoreply_gmail_set (core, token, &settings, error)
		: autoreply_graph_set (core, token, &settings, error);
	if (ok)
		json = autoreply_settings_to_json (provider, &settings);

 exit:
	autoreply_settings_clear (&settings);
	e_util_safe_free_string (token);
	return json;
}

void
evo_core_get_auto_reply (EvoCore *core,
			 const char *account_uid,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, NULL };

	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_get_auto_reply_run, g_strdupv (args), func, user_data);
}

void
evo_core_set_auto_reply (EvoCore *core,
			 const char *account_uid,
			 const char *settings_json,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) settings_json, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (settings_json != NULL);

	core_submit (core, core_set_auto_reply_run, g_strdupv (args), func, user_data);
}
