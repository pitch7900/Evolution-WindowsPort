/*
 * evo-core : règles de messagerie.
 *   - lecture du Cc et des en-têtes d'un message, sans le marquer comme lu (règles côté client) ;
 *   - règles côté serveur Exchange Online : synchronisation vers Microsoft Graph (messageRules) ;
 *   - règles côté serveur IMAP : envoi d'un script Sieve par ManageSieve (RFC 5804).
 * Voir evo-core.h et evo-core-private.h ; fonctions pures ManageSieve dans evo-core-rules-util.h.
 */

#include "evo-core-private.h"
#include "evo-core-rules-util.h"

#define RULES_GRAPH_BASE "https://graph.microsoft.com/v1.0/me"
#define RULES_GRAPH_PREFIX "[Evolution] "
#define RULES_SIEVE_DEFAULT_PORT 4190
#define RULES_SIEVE_MAX_LINE 65536

/* ---- Lecture des en-têtes d'un message ---- */

/* Texte des destinataires d'un type (Cc...) d'un message, chaîne vide si aucun (g_free). */
static gchar *
rules_format_recipients (CamelMimeMessage *message,
			 const gchar *type)
{
	CamelInternetAddress *addresses = camel_mime_message_get_recipients (message, type);
	gchar *formatted = addresses ? camel_address_format (CAMEL_ADDRESS (addresses)) : NULL;

	return formatted ? formatted : g_strdup ("");
}

/* Lit le Cc et les en-têtes d'un message sans modifier ses indicateurs.
 *   args : [0] = compte, [1] = dossier, [2] = uid du message
 * Retourne : {"cc": "...", "headers": {"Nom": "valeur", ...}} (un en-tête répété est
 * joint par des virgules, les valeurs sont tronquées à 2000 caractères), NULL sinon. */
static gchar *
core_get_message_headers_run (EvoCore *core,
			      gchar **args,
			      GError **error)
{
	CamelFolder *folder;
	CamelMimeMessage *message;
	const CamelNameValueArray *headers;
	JsonBuilder *builder;
	GHashTable *joined;
	GHashTableIter iter;
	gpointer key, value;
	gchar *cc, *json;
	guint ii, count;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	message = camel_folder_get_message_sync (folder, args[2], core->cancellable, error);
	g_object_unref (folder);
	if (!message)
		return NULL;

	joined = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	headers = camel_medium_get_headers (CAMEL_MEDIUM (message));
	count = camel_name_value_array_get_length (headers);
	for (ii = 0; ii < count; ii++) {
		const gchar *name = NULL, *header_value = NULL;
		gchar *old, *text;

		if (!camel_name_value_array_get (headers, ii, &name, &header_value) || !name)
			continue;
		old = g_hash_table_lookup (joined, name);
		text = old ? g_strdup_printf ("%s, %s", old, header_value ? header_value : "") : g_strdup (header_value ? header_value : "");
		if (strlen (text) > 2000)
			text[2000] = '\0';
		g_hash_table_replace (joined, g_strdup (name), text);
	}

	cc = rules_format_recipients (message, CAMEL_RECIPIENT_TYPE_CC);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "cc");
	json_builder_add_string_value (builder, cc);
	json_builder_set_member_name (builder, "headers");
	json_builder_begin_object (builder);
	g_hash_table_iter_init (&iter, joined);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		json_builder_set_member_name (builder, key);
		json_builder_add_string_value (builder, value);
	}
	json_builder_end_object (builder);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	g_free (cc);
	g_hash_table_destroy (joined);
	g_object_unref (message);
	return json;
}

void
evo_core_get_message_headers (EvoCore *core,
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

	core_submit (core, core_get_message_headers_run, g_strdupv (args), func, user_data);
}

/* ---- Règles côté serveur : Microsoft Graph ---- */

/* Identifiants des dossiers d'un parcours de chemin, pour une même synchronisation. */
typedef struct {
	EvoCore *core;
	const gchar *account;
	GHashTable *cache;	/* chemin -> identifiant Graph */
} RulesGraphContext;

/* Requête Graph sur la boîte du compte ; retourne l'objet JSON racine (json_node_unref), NULL sinon. */
static JsonNode *
rules_graph_call (RulesGraphContext *ctx,
		  const gchar *method,
		  const gchar *path,
		  const gchar *body,
		  GError **error)
{
	gchar *uri = g_strdup_printf ("%s%s", RULES_GRAPH_BASE, path);
	gchar *text = core_graph_request (ctx->core, ctx->account, method, uri, body, error);
	JsonNode *root = NULL;

	g_free (uri);
	if (!text)
		return NULL;
	root = json_from_string (text, error);
	g_free (text);
	return root;
}

/* Identifiant Graph d'un dossier de premier niveau connu par son nom anglais (« Inbox », « Sent Items »...), NULL sinon. */
static const gchar *
rules_graph_well_known (const gchar *name)
{
	static const struct { const gchar *name; const gchar *id; } known[] = {
		{ "inbox", "inbox" }, { "drafts", "drafts" }, { "sent items", "sentitems" }, { "sent", "sentitems" },
		{ "deleted items", "deleteditems" }, { "trash", "deleteditems" }, { "junk email", "junkemail" },
		{ "junk", "junkemail" }, { "archive", "archive" }, { "outbox", "outbox" }
	};
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (known); ii++)
		if (g_ascii_strcasecmp (name, known[ii].name) == 0)
			return known[ii].id;
	return NULL;
}

/* Cherche dans la liste de dossiers JSON (« value ») celui dont le nom affiché est name ; identifiant (g_free) ou NULL. */
static gchar *
rules_graph_find_child (JsonNode *root,
			const gchar *name)
{
	JsonObject *object;
	JsonArray *array;
	guint ii;

	if (!root || !JSON_NODE_HOLDS_OBJECT (root))
		return NULL;
	object = json_node_get_object (root);
	if (!json_object_has_member (object, "value") || !JSON_NODE_HOLDS_ARRAY (json_object_get_member (object, "value")))
		return NULL;
	array = json_object_get_array_member (object, "value");
	for (ii = 0; ii < json_array_get_length (array); ii++) {
		JsonObject *item = json_array_get_object_element (array, ii);
		const gchar *display = item ? json_object_get_string_member_with_default (item, "displayName", "") : "";

		if (g_utf8_collate (display, name) == 0 || g_ascii_strcasecmp (display, name) == 0)
			return g_strdup (json_object_get_string_member_with_default (item, "id", ""));
	}
	return NULL;
}

/* Identifiant Graph du dossier désigné par un chemin Camel (« INBOX/Factures »).
 * Retourne : l'identifiant (g_free), NULL avec erreur si le dossier est introuvable. */
static gchar *
rules_graph_resolve_folder (RulesGraphContext *ctx,
			    const gchar *path,
			    GError **error)
{
	gchar **segments;
	gchar *current = NULL, *cached;
	guint ii;

	cached = g_hash_table_lookup (ctx->cache, path);
	if (cached)
		return g_strdup (cached);

	segments = g_strsplit (path, "/", -1);
	for (ii = 0; segments[ii]; ii++) {
		gchar *request, *found = NULL;
		JsonNode *root;
		const gchar *known = ii == 0 ? rules_graph_well_known (segments[ii]) : NULL;

		if (!*segments[ii])
			continue;
		if (known) {
			found = g_strdup (known);
		} else {
			request = current
				? g_strdup_printf ("/mailFolders/%s/childFolders?$top=250&$select=id,displayName", current)
				: g_strdup ("/mailFolders?$top=250&$select=id,displayName");
			root = rules_graph_call (ctx, "GET", request, NULL, error);
			g_free (request);
			if (!root) {
				g_free (current);
				g_strfreev (segments);
				return NULL;
			}
			found = rules_graph_find_child (root, segments[ii]);
			json_node_unref (root);
		}
		g_free (current);
		current = found;
		if (!current) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				"Dossier introuvable sur le serveur Exchange Online : « %s »", path);
			g_strfreev (segments);
			return NULL;
		}
	}
	g_strfreev (segments);
	if (!current) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Chemin de dossier vide");
		return NULL;
	}
	g_hash_table_insert (ctx->cache, g_strdup (path), g_strdup (current));
	return current;
}

/* Remplace, dans l'objet « actions » d'une règle, les chemins de dossiers par les identifiants Graph. */
static gboolean
rules_graph_resolve_actions (RulesGraphContext *ctx,
			     JsonObject *rule,
			     GError **error)
{
	static const gchar *const members[] = { "moveToFolder", "copyToFolder" };
	JsonObject *actions;
	guint ii;

	if (!json_object_has_member (rule, "actions") || !JSON_NODE_HOLDS_OBJECT (json_object_get_member (rule, "actions")))
		return TRUE;
	actions = json_object_get_object_member (rule, "actions");
	for (ii = 0; ii < G_N_ELEMENTS (members); ii++) {
		gchar *id;

		if (!json_object_has_member (actions, members[ii]))
			continue;
		id = rules_graph_resolve_folder (ctx, json_object_get_string_member_with_default (actions, members[ii], ""), error);
		if (!id)
			return FALSE;
		json_object_set_string_member (actions, members[ii], id);
		g_free (id);
	}
	return TRUE;
}

/* Remplace les règles « [Evolution] » du compte par celles fournies.
 *   args : [0] = compte, [1] = tableau JSON de règles Graph (dossiers sous forme de chemins Camel)
 * Retourne : {"removed": n, "created": n}, NULL sinon. */
static gchar *
core_sync_server_rules_run (EvoCore *core,
			    gchar **args,
			    GError **error)
{
	RulesGraphContext ctx = { core, args[0], NULL };
	JsonNode *wanted = NULL, *existing = NULL;
	JsonArray *wanted_array, *existing_array;
	GPtrArray *bodies;
	gchar *result = NULL;
	guint ii, removed = 0, created = 0;

	wanted = json_from_string (args[1], error);
	if (!wanted)
		return NULL;
	if (!JSON_NODE_HOLDS_ARRAY (wanted)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Les règles doivent être un tableau JSON");
		json_node_unref (wanted);
		return NULL;
	}
	wanted_array = json_node_get_array (wanted);
	ctx.cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	bodies = g_ptr_array_new_with_free_func (g_free);

	/* 1. préparation complète (résolution des dossiers) avant toute suppression sur le serveur */
	for (ii = 0; ii < json_array_get_length (wanted_array); ii++) {
		JsonObject *rule = json_array_get_object_element (wanted_array, ii);
		JsonGenerator *generator;
		JsonNode *node;

		if (!rule || !rules_graph_resolve_actions (&ctx, rule, error))
			goto exit;
		node = json_node_new (JSON_NODE_OBJECT);
		json_node_set_object (node, rule);
		generator = json_generator_new ();
		json_generator_set_root (generator, node);
		g_ptr_array_add (bodies, json_generator_to_data (generator, NULL));
		g_object_unref (generator);
		json_node_unref (node);
	}

	/* 2. suppression des règles précédemment créées par Evolution */
	existing = rules_graph_call (&ctx, "GET", "/mailFolders/inbox/messageRules", NULL, error);
	if (!existing)
		goto exit;
	if (JSON_NODE_HOLDS_OBJECT (existing) && json_object_has_member (json_node_get_object (existing), "value") &&
	    JSON_NODE_HOLDS_ARRAY (json_object_get_member (json_node_get_object (existing), "value"))) {
		existing_array = json_object_get_array_member (json_node_get_object (existing), "value");
		for (ii = 0; ii < json_array_get_length (existing_array); ii++) {
			JsonObject *item = json_array_get_object_element (existing_array, ii);
			const gchar *name = item ? json_object_get_string_member_with_default (item, "displayName", "") : "";
			const gchar *id = item ? json_object_get_string_member_with_default (item, "id", "") : "";
			gchar *path;
			JsonNode *reply;

			if (!g_str_has_prefix (name, RULES_GRAPH_PREFIX) || !*id)
				continue;
			path = g_strdup_printf ("/mailFolders/inbox/messageRules/%s", id);
			reply = rules_graph_call (&ctx, "DELETE", path, NULL, error);
			g_free (path);
			if (!reply)
				goto exit;
			json_node_unref (reply);
			removed++;
		}
	}

	/* 3. création des nouvelles règles */
	for (ii = 0; ii < bodies->len; ii++) {
		JsonNode *reply = rules_graph_call (&ctx, "POST", "/mailFolders/inbox/messageRules", g_ptr_array_index (bodies, ii), error);

		if (!reply)
			goto exit;
		json_node_unref (reply);
		created++;
	}

	result = g_strdup_printf ("{\"removed\":%u,\"created\":%u}", removed, created);

 exit:
	if (existing)
		json_node_unref (existing);
	json_node_unref (wanted);
	g_ptr_array_unref (bodies);
	g_hash_table_destroy (ctx.cache);
	return result;
}

void
evo_core_sync_server_rules (EvoCore *core,
			    const char *account_uid,
			    const char *rules_json,
			    EvoCoreResultFunc func,
			    void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) rules_json, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (rules_json != NULL);

	core_submit (core, core_sync_server_rules_run, g_strdupv (args), func, user_data);
}

/* ---- Règles côté serveur : ManageSieve ---- */

/* Connexion ManageSieve : flux d'entrée en lecture par ligne et flux de sortie. */
typedef struct {
	GIOStream *stream;
	GDataInputStream *in;
	GOutputStream *out;
	GCancellable *cancellable;
} SieveConnection;

/* (Re)crée les flux de lecture et d'écriture sur le flux de la connexion. */
static void
sieve_attach (SieveConnection *conn)
{
	g_clear_object (&conn->in);
	conn->in = g_data_input_stream_new (g_io_stream_get_input_stream (conn->stream));
	g_data_input_stream_set_newline_type (conn->in, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
	conn->out = g_io_stream_get_output_stream (conn->stream);
}

/* Envoie une commande (déjà terminée par CRLF). */
static gboolean
sieve_send (SieveConnection *conn,
	    const gchar *command,
	    gsize length,
	    GError **error)
{
	return g_output_stream_write_all (conn->out, command, length, NULL, conn->cancellable, error) &&
		g_output_stream_flush (conn->out, conn->cancellable, error);
}

/* Lit les lignes jusqu'à une réponse OK, NO ou BYE (chaînes littérales « {n} » comprises).
 * Paramètres :
 *   detail : reçoit le texte de la réponse (g_free), peut être NULL
 * Retourne : TRUE pour OK ; FALSE avec erreur pour NO / BYE / erreur de lecture. */
static gboolean
sieve_read_response (SieveConnection *conn,
		     gchar **detail,
		     GError **error)
{
	if (detail)
		*detail = NULL;

	for (;;) {
		gsize length = 0;
		gchar *line = g_data_input_stream_read_line (conn->in, &length, conn->cancellable, error);
		gint kind;
		long literal;

		if (!line) {
			if (error && !*error)
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED, "Le serveur ManageSieve a fermé la connexion");
			return FALSE;
		}
		kind = rules_sieve_response_class (line);
		literal = rules_sieve_literal_size (line);
		if (literal > 0) {
			gchar *skipped = g_malloc ((gsize) literal + 1);
			gsize got = 0;
			gboolean ok = g_input_stream_read_all (G_INPUT_STREAM (conn->in), skipped, (gsize) literal, &got, conn->cancellable, error);

			if (ok && kind != RULES_SIEVE_RESPONSE_OTHER && detail) {
				skipped[got] = '\0';
				*detail = skipped;
				skipped = NULL;
			}
			g_free (skipped);
			if (!ok) {
				g_free (line);
				return FALSE;
			}
		}
		if (kind == RULES_SIEVE_RESPONSE_OK) {
			g_free (line);
			return TRUE;
		}
		if (kind == RULES_SIEVE_RESPONSE_NO || kind == RULES_SIEVE_RESPONSE_BYE) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Le serveur ManageSieve a refusé la demande : %s",
				detail && *detail ? *detail : line);
			g_free (line);
			return FALSE;
		}
		g_free (line);
	}
}

/* Envoie une commande simple et attend sa réponse. */
static gboolean
sieve_command (SieveConnection *conn,
	       const gchar *command,
	       GError **error)
{
	gchar *detail = NULL;
	gboolean ok = sieve_send (conn, command, strlen (command), error) && sieve_read_response (conn, &detail, error);

	g_free (detail);
	return ok;
}

/* Nom d'hôte et utilisateur du compte IMAP (g_free), mot de passe enregistré (e_util_safe_free_string). */
static gboolean
rules_sieve_account_identity (EvoCore *core,
			      const gchar *account_uid,
			      gchar **out_host,
			      gchar **out_user,
			      gchar **out_password,
			      GError **error)
{
	CamelService *service = camel_session_ref_service (core->session, account_uid);
	CamelSettings *settings;
	ESource *source;
	ENamedParameters *credentials = NULL;
	EvoCoreSession *session = EVO_CORE_SESSION (core->session);

	*out_host = *out_user = *out_password = NULL;
	if (!service) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Compte introuvable : « %s »", account_uid);
		return FALSE;
	}
	settings = camel_service_ref_settings (service);
	if (CAMEL_IS_NETWORK_SETTINGS (settings)) {
		*out_host = camel_network_settings_dup_host (CAMEL_NETWORK_SETTINGS (settings));
		*out_user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (settings));
	}
	g_clear_object (&settings);
	if (!*out_host || !**out_host || !*out_user || !**out_user) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"ManageSieve n'est disponible que pour les comptes IMAP avec un serveur et un identifiant.");
		g_object_unref (service);
		return FALSE;
	}

	source = e_source_registry_ref_source (core->registry, account_uid);
	if (source) {
		if (e_source_credentials_provider_lookup_sync (session->credentials_provider, source, core->cancellable, &credentials, NULL) && credentials) {
			const gchar *password = e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD);

			if (password && *password)
				*out_password = g_strdup (password);
			e_named_parameters_free (credentials);
		}
		g_object_unref (source);
	}
	if (!*out_password) {
		const gchar *password = camel_service_get_password (service);

		if (password && *password)
			*out_password = g_strdup (password);
	}
	g_object_unref (service);

	if (!*out_password) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"Mot de passe du compte introuvable : connectez le compte une première fois.");
		return FALSE;
	}
	return TRUE;
}

/* Envoie un script Sieve au serveur ManageSieve du compte et l'active.
 *   args : [0] = compte, [1] = nom du script, [2] = script, [3] = "1" pour l'activer,
 *          [4] = port (vide : 4190)
 * Retourne : {"uploaded": true, "active": bool}, NULL sinon. */
static gchar *
core_managesieve_put_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	SieveConnection conn = { NULL, NULL, NULL, core->cancellable };
	GSocketClient *client;
	GSocketConnection *socket_conn;
	GSocketConnectable *identity;
	GIOStream *tls;
	gchar *host = NULL, *user = NULL, *password = NULL, *command = NULL, *detail = NULL;
	gchar quoted[300], authenticate[1024];
	gsize command_size;
	guint port = RULES_SIEVE_DEFAULT_PORT;
	gboolean ok = FALSE, activate = g_strcmp0 (args[3], "1") == 0;
	gchar *result = NULL;

	if (args[4] && *args[4]) {
		gint64 value = g_ascii_strtoll (args[4], NULL, 10);

		if (value > 0 && value < 65536)
			port = (guint) value;
	}
	if (!rules_sieve_account_identity (core, args[0], &host, &user, &password, error))
		return NULL;

	client = g_socket_client_new ();
	g_socket_client_set_timeout (client, 30);
	socket_conn = g_socket_client_connect_to_host (client, host, (guint16) port, core->cancellable, error);
	if (!socket_conn)
		goto exit;
	conn.stream = G_IO_STREAM (socket_conn);
	sieve_attach (&conn);

	/* bannière et capacités, puis STARTTLS (obligatoire : le mot de passe ne circule jamais en clair) */
	if (!sieve_read_response (&conn, &detail, error))
		goto exit;
	g_clear_pointer (&detail, g_free);
	if (!sieve_command (&conn, "STARTTLS\r\n", error))
		goto exit;

	identity = g_network_address_new (host, (guint16) port);
	tls = g_tls_client_connection_new (conn.stream, identity, error);
	g_object_unref (identity);
	if (!tls)
		goto exit;
	g_object_unref (conn.stream);
	conn.stream = tls;
	if (!g_tls_connection_handshake (G_TLS_CONNECTION (tls), core->cancellable, error))
		goto exit;
	sieve_attach (&conn);
	/* capacités renvoyées après la négociation TLS */
	if (!sieve_read_response (&conn, &detail, error))
		goto exit;
	g_clear_pointer (&detail, g_free);

	if (!rules_sieve_authenticate_plain (user, password, authenticate, sizeof (authenticate))) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Identifiants ManageSieve trop longs ou vides");
		goto exit;
	}
	if (!sieve_send (&conn, authenticate, strlen (authenticate), error) || !sieve_read_response (&conn, &detail, error)) {
		if (error && *error && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_FAILED))
			g_prefix_error (error, "Authentification ManageSieve refusée. ");
		goto exit;
	}
	g_clear_pointer (&detail, g_free);

	command_size = strlen (args[2]) + 512;
	command = g_malloc (command_size);
	command_size = rules_sieve_putscript_command (args[1], args[2], command, command_size);
	if (!command_size) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Script Sieve ou nom de script invalide");
		goto exit;
	}
	if (!sieve_send (&conn, command, command_size, error) || !sieve_read_response (&conn, &detail, error))
		goto exit;
	g_clear_pointer (&detail, g_free);

	if (activate) {
		gchar *set_active;

		if (!rules_sieve_quote (args[1], quoted, sizeof (quoted))) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Nom de script invalide");
			goto exit;
		}
		set_active = g_strdup_printf ("SETACTIVE %s\r\n", quoted);
		ok = sieve_command (&conn, set_active, error);
		g_free (set_active);
		if (!ok)
			goto exit;
	}
	sieve_send (&conn, "LOGOUT\r\n", 8, NULL);
	result = g_strdup_printf ("{\"uploaded\":true,\"active\":%s}", activate ? "true" : "false");

 exit:
	g_clear_object (&conn.in);
	g_clear_object (&conn.stream);
	g_clear_object (&client);
	g_free (command);
	g_free (detail);
	g_free (host);
	g_free (user);
	e_util_safe_free_string (password);
	return result;
}

void
evo_core_managesieve_put (EvoCore *core,
			  const char *account_uid,
			  const char *script_name,
			  const char *script,
			  int activate,
			  const char *port,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) script_name, (gchar *) script,
		(gchar *) (activate ? "1" : "0"), (gchar *) (port ? port : ""), NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (script_name != NULL);
	g_return_if_fail (script != NULL);

	core_submit (core, core_managesieve_put_run, g_strdupv (args), func, user_data);
}
