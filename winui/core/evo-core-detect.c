/*
 * evo-core : Detection automatique des parametres d'un compte : ISPDB, DNS (MX, SRV),
 * Autodiscover Exchange, autoconfig du domaine.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* --- détection automatique du type de compte --- */

/* Configuration de messagerie d'un fournisseur, au format clientConfig de
 * Thunderbird (base ISPDB embarquée, autoconfig du domaine, ISPDB en ligne).
 * Les valeurs peuvent contenir %EMAILADDRESS%, %EMAILLOCALPART% et
 * %EMAILDOMAIN% (core_mail_config_expand). */
typedef struct {
	gchar *id;
	gchar *display_name;
	GPtrArray *domains;
	gchar *imap_host, *imap_security, *imap_user;
	gint imap_port;
	gboolean imap_oauth2;
	gchar *smtp_host, *smtp_security, *smtp_user;
	gint smtp_port;
	gchar *ews_url;
} CoreMailConfig;

static void
core_mail_config_free (CoreMailConfig *config)
{
	if (!config)
		return;

	g_free (config->id);
	g_free (config->display_name);
	g_ptr_array_unref (config->domains);
	g_free (config->imap_host);
	g_free (config->imap_security);
	g_free (config->imap_user);
	g_free (config->smtp_host);
	g_free (config->smtp_security);
	g_free (config->smtp_user);
	g_free (config->ews_url);
	g_free (config);
}

static CoreMailConfig *
core_mail_config_new (void)
{
	CoreMailConfig *config = g_new0 (CoreMailConfig, 1);

	config->domains = g_ptr_array_new_with_free_func (g_free);
	return config;
}

/* état de l'analyse d'un fichier clientConfig : seul le premier
 * <emailProvider> compte, avec son premier serveur IMAP, son premier
 * serveur SMTP et son éventuel serveur Exchange (ewsURL) */
typedef struct {
	CoreMailConfig *config;
	gint providers;
	gint server;		/* 0 = aucun, 1 = IMAP, 2 = SMTP, 3 = Exchange, -1 = ignoré */
	GString *text;
} CoreMailConfigParser;

static void
core_mail_config_start (GMarkupParseContext *context,
			const gchar *element,
			const gchar **names,
			const gchar **values,
			gpointer user_data,
			GError **error)
{
	CoreMailConfigParser *parser = user_data;
	CoreMailConfig *config = parser->config;
	const gchar *type = NULL, *id = NULL;
	guint ii;

	g_string_truncate (parser->text, 0);

	for (ii = 0; names[ii]; ii++) {
		if (g_str_equal (names[ii], "type"))
			type = values[ii];
		else if (g_str_equal (names[ii], "id"))
			id = values[ii];
	}

	if (g_str_equal (element, "emailProvider")) {
		if (++parser->providers == 1)
			config->id = g_strdup (id);
	} else if (parser->providers != 1) {
		return;
	} else if (g_str_equal (element, "incomingServer")) {
		if (g_strcmp0 (type, "imap") == 0 && !config->imap_host)
			parser->server = 1;
		else if (g_strcmp0 (type, "exchange") == 0 && !config->ews_url)
			parser->server = 3;
		else
			parser->server = -1;
	} else if (g_str_equal (element, "outgoingServer")) {
		parser->server = g_strcmp0 (type, "smtp") == 0 && !config->smtp_host ? 2 : -1;
	}
}

static void
core_mail_config_text (GMarkupParseContext *context,
		       const gchar *text,
		       gsize text_len,
		       gpointer user_data,
		       GError **error)
{
	CoreMailConfigParser *parser = user_data;

	g_string_append_len (parser->text, text, text_len);
}

/* "SSL" / "STARTTLS" / "plain" vers les valeurs de evo_core_add_account */
static const gchar *
core_mail_config_security (const gchar *socket_type)
{
	if (g_ascii_strcasecmp (socket_type, "STARTTLS") == 0)
		return "starttls";
	if (g_ascii_strcasecmp (socket_type, "plain") == 0)
		return "none";
	return "tls";
}

static void
core_mail_config_end (GMarkupParseContext *context,
		      const gchar *element,
		      gpointer user_data,
		      GError **error)
{
	CoreMailConfigParser *parser = user_data;
	CoreMailConfig *config = parser->config;
	gchar *text = g_strstrip (parser->text->str);

	if (parser->providers != 1)
		goto exit;

	if (g_str_equal (element, "incomingServer") || g_str_equal (element, "outgoingServer")) {
		parser->server = 0;
	} else if (parser->server == 0) {
		if (g_str_equal (element, "domain") && *text)
			g_ptr_array_add (config->domains, g_ascii_strdown (text, -1));
		else if (g_str_equal (element, "displayName") && !config->display_name)
			config->display_name = g_strdup (text);
	} else if (parser->server == 1 || parser->server == 2) {
		gboolean imap = parser->server == 1;

		if (g_str_equal (element, "hostname")) {
			g_free (imap ? config->imap_host : config->smtp_host);
			if (imap)
				config->imap_host = g_strdup (text);
			else
				config->smtp_host = g_strdup (text);
		} else if (g_str_equal (element, "port")) {
			if (imap)
				config->imap_port = atoi (text);
			else
				config->smtp_port = atoi (text);
		} else if (g_str_equal (element, "socketType")) {
			g_free (imap ? config->imap_security : config->smtp_security);
			if (imap)
				config->imap_security = g_strdup (core_mail_config_security (text));
			else
				config->smtp_security = g_strdup (core_mail_config_security (text));
		} else if (g_str_equal (element, "username")) {
			g_free (imap ? config->imap_user : config->smtp_user);
			if (imap)
				config->imap_user = g_strdup (text);
			else
				config->smtp_user = g_strdup (text);
		} else if (g_str_equal (element, "authentication") && imap &&
			   g_ascii_strcasecmp (text, "OAuth2") == 0) {
			config->imap_oauth2 = TRUE;
		}
	} else if (parser->server == 3 && g_str_equal (element, "ewsURL") && *text) {
		g_free (config->ews_url);
		config->ews_url = g_strdup (text);
	}

 exit:
	g_string_truncate (parser->text, 0);
}

/* Analyse un fichier clientConfig.
 * Paramètres :
 *   xml    : contenu du fichier
 *   length : longueur en octets (-1 si terminé par un zéro)
 * Retourne : la configuration (core_mail_config_free), NULL si le fichier
 *            n'est pas valide ou ne décrit ni IMAP / SMTP ni Exchange. */
static CoreMailConfig *
core_mail_config_parse (const gchar *xml,
			gssize length)
{
	static const GMarkupParser markup = {
		core_mail_config_start, core_mail_config_end, core_mail_config_text, NULL, NULL
	};
	CoreMailConfigParser parser = { 0 };
	GMarkupParseContext *context;
	gboolean ok;

	parser.config = core_mail_config_new ();
	parser.text = g_string_new (NULL);

	context = g_markup_parse_context_new (&markup, G_MARKUP_IGNORE_QUALIFIED, &parser, NULL);
	ok = g_markup_parse_context_parse (context, xml, length, NULL) &&
	     g_markup_parse_context_end_parse (context, NULL);
	g_markup_parse_context_free (context);
	g_string_free (parser.text, TRUE);

	if (!ok || !((parser.config->imap_host && parser.config->smtp_host) || parser.config->ews_url)) {
		core_mail_config_free (parser.config);
		return NULL;
	}

	if (!parser.config->imap_security)
		parser.config->imap_security = g_strdup ("tls");
	if (!parser.config->smtp_security)
		parser.config->smtp_security = g_strdup ("tls");

	return parser.config;
}

/* Base ISPDB de Thunderbird embarquée (ressources /org/evolution-win/ispdb,
 * dépôt thunderbird/autoconfig, MPL 2.0) : domaine -> configuration.
 * Chargée à la première détection, conservée jusqu'à la fin du programme. */
static GHashTable *
core_ispdb_index (void)
{
	static gsize initialized = 0;
	static GHashTable *index = NULL;

	if (g_once_init_enter (&initialized)) {
		gchar **names;
		guint ii, jj;

		index = g_hash_table_new (g_str_hash, g_str_equal);
		names = g_resources_enumerate_children ("/org/evolution-win/ispdb", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);

		for (ii = 0; names && names[ii]; ii++) {
			gchar *path = g_strconcat ("/org/evolution-win/ispdb/", names[ii], NULL);
			GBytes *bytes = g_resources_lookup_data (path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
			CoreMailConfig *config = NULL;

			if (bytes) {
				gsize size = 0;
				const gchar *data = g_bytes_get_data (bytes, &size);

				config = core_mail_config_parse (data, size);
				g_bytes_unref (bytes);
			}

			/* le nom du fichier est aussi un domaine du fournisseur */
			if (config && g_str_has_suffix (names[ii], ".xml"))
				g_ptr_array_add (config->domains, g_strndup (names[ii], strlen (names[ii]) - 4));

			for (jj = 0; config && jj < config->domains->len; jj++) {
				if (!g_hash_table_contains (index, config->domains->pdata[jj]))
					g_hash_table_insert (index, config->domains->pdata[jj], config);
			}

			g_free (path);
		}

		g_strfreev (names);
		g_once_init_leave (&initialized, 1);
	}

	return index;
}

static const CoreMailConfig *
core_ispdb_lookup (const gchar *domain)
{
	return g_hash_table_lookup (core_ispdb_index (), domain);
}

/* Configuration ISPDB d'un serveur MX : son domaine sur deux puis trois
 * niveaux (mx1.free.fr -> free.fr, mx.example.co.uk -> example.co.uk). */
static const CoreMailConfig *
core_ispdb_lookup_mx (const gchar *mx_host)
{
	gchar **labels = g_strsplit (mx_host, ".", -1);
	guint n_labels = g_strv_length (labels), levels;
	const CoreMailConfig *config = NULL;

	for (levels = 2; !config && levels <= 3 && levels < n_labels; levels++) {
		gchar *domain = g_strjoinv (".", labels + n_labels - levels);

		config = core_ispdb_lookup (domain);
		g_free (domain);
	}

	g_strfreev (labels);
	return config;
}

/* Remplace les variables d'un fichier clientConfig.
 * Paramètres :
 *   value : valeur du fichier (hostname, username...), peut être NULL
 *   email : adresse du compte
 * Retourne : la valeur développée (g_free), "" si value est NULL. */
static gchar *
core_mail_config_expand (const gchar *value,
			 const gchar *email)
{
	const gchar *at = strrchr (email, '@');
	gchar *local_part = at ? g_strndup (email, at - email) : g_strdup (email);
	GString *text = g_string_new (value ? value : "");

	g_string_replace (text, "%EMAILADDRESS%", email, 0);
	g_string_replace (text, "%EMAILLOCALPART%", local_part, 0);
	g_string_replace (text, "%EMAILDOMAIN%", at ? at + 1 : "", 0);

	g_free (local_part);
	return g_string_free (text, FALSE);
}

/* Type de compte d'un fournisseur de la base ISPDB : Microsoft
 * (Outlook.com, Microsoft 365), Google, sinon IMAP / SMTP. */
static const gchar *
core_mail_config_type (const CoreMailConfig *config)
{
	if (config && (g_strcmp0 (config->id, "office365.com") == 0 ||
		       g_strcmp0 (config->id, "hotmail.com") == 0 ||
		       g_strcmp0 (config->id, "outlook.com") == 0))
		return "m365";
	if (config && g_strcmp0 (config->id, "googlemail.com") == 0)
		return "google";
	return "imap";
}

/* Serveur MX préféré (plus petite préférence) d'un domaine, NULL sinon. */
static gchar *
core_dns_mx (const gchar *domain)
{
	gunichar2 *name = g_utf8_to_utf16 (domain, -1, NULL, NULL, NULL);
	PDNS_RECORDW records = NULL, record;
	gchar *best = NULL;
	WORD best_preference = 0xffff;

	if (name && DnsQuery_W (name, DNS_TYPE_MX, DNS_QUERY_STANDARD, NULL, (PDNS_RECORD *) &records, NULL) == 0) {
		for (record = records; record; record = record->pNext) {
			if (record->wType != DNS_TYPE_MX || record->Flags.S.Section != DnsSectionAnswer ||
			    record->Data.MX.wPreference >= best_preference)
				continue;
			g_free (best);
			best = g_utf16_to_utf8 (record->Data.MX.pNameExchange, -1, NULL, NULL, NULL);
			best_preference = record->Data.MX.wPreference;
		}
		DnsRecordListFree ((PDNS_RECORD) records, DnsFreeRecordList);
	}

	g_free (name);

	if (best) {
		gchar *lower = g_ascii_strdown (best, -1);

		g_free (best);
		best = lower;
	}

	return best;
}

/* Enregistrement SRV prioritaire (RFC 6186) : _imaps._tcp.<domaine>...
 * Paramètres :
 *   service  : "_imaps._tcp", "_submission._tcp"...
 *   domain   : domaine de l'adresse
 *   out_port : reçoit le port
 * Retourne : le serveur (g_free), NULL si absent ou désactivé ("."). */
static gchar *
core_dns_srv (const gchar *service,
	      const gchar *domain,
	      gint *out_port)
{
	gchar *query = g_strconcat (service, ".", domain, NULL);
	gunichar2 *name = g_utf8_to_utf16 (query, -1, NULL, NULL, NULL);
	PDNS_RECORDW records = NULL, record;
	gchar *best = NULL;
	WORD best_priority = 0xffff;

	if (name && DnsQuery_W (name, DNS_TYPE_SRV, DNS_QUERY_STANDARD, NULL, (PDNS_RECORD *) &records, NULL) == 0) {
		for (record = records; record; record = record->pNext) {
			if (record->wType != DNS_TYPE_SRV || record->Flags.S.Section != DnsSectionAnswer ||
			    record->Data.SRV.wPriority >= best_priority)
				continue;
			g_free (best);
			best = g_utf16_to_utf8 (record->Data.SRV.pNameTarget, -1, NULL, NULL, NULL);
			best_priority = record->Data.SRV.wPriority;
			*out_port = record->Data.SRV.wPort;
		}
		DnsRecordListFree ((PDNS_RECORD) records, DnsFreeRecordList);
	}

	g_free (name);
	g_free (query);

	if (best && (!*best || g_str_equal (best, "."))) {
		g_free (best);
		best = NULL;
	}

	return best;
}

/* Configuration IMAP / SMTP des enregistrements SRV du domaine (TLS
 * implicite de préférence, puis STARTTLS), NULL s'il en manque un. */
static CoreMailConfig *
core_srv_config (const gchar *domain)
{
	CoreMailConfig *config = core_mail_config_new ();
	gint port = 0;

	if ((config->imap_host = core_dns_srv ("_imaps._tcp", domain, &port)) != NULL) {
		config->imap_security = g_strdup ("tls");
	} else if ((config->imap_host = core_dns_srv ("_imap._tcp", domain, &port)) != NULL) {
		config->imap_security = g_strdup ("starttls");
	}
	config->imap_port = port;

	if ((config->smtp_host = core_dns_srv ("_submissions._tcp", domain, &port)) != NULL) {
		config->smtp_security = g_strdup ("tls");
	} else if ((config->smtp_host = core_dns_srv ("_submission._tcp", domain, &port)) != NULL) {
		config->smtp_security = g_strdup ("starttls");
	}
	config->smtp_port = port;

	if (!config->imap_host || !config->smtp_host) {
		core_mail_config_free (config);
		return NULL;
	}

	return config;
}

/* Requête HTTP GET de la détection.
 * Paramètres :
 *   soup         : session HTTP (délai court)
 *   uri          : adresse demandée
 *   follow       : FALSE pour lire une redirection au lieu de la suivre
 *   out_status   : reçoit le code HTTP (0 si le serveur est injoignable)
 *   out_location : reçoit l'en-tête Location (g_free), ou NULL
 *   cancellable  : objet d'annulation
 * Retourne : le corps de la réponse si le code est 200, NULL sinon. */
static GBytes *
core_http_get (SoupSession *soup,
	       const gchar *uri,
	       gboolean follow,
	       guint *out_status,
	       gchar **out_location,
	       GCancellable *cancellable)
{
	SoupMessage *msg = soup_message_new ("GET", uri);
	GBytes *bytes;

	*out_status = 0;
	if (out_location)
		*out_location = NULL;
	if (!msg)
		return NULL;

	if (!follow)
		soup_message_add_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	bytes = soup_session_send_and_read (soup, msg, cancellable, NULL);
	*out_status = soup_message_get_status (msg);
	if (out_location)
		*out_location = g_strdup (soup_message_headers_get_one (
			soup_message_get_response_headers (msg), "Location"));

	if (bytes && *out_status != SOUP_STATUS_OK)
		g_clear_pointer (&bytes, g_bytes_unref);

	g_object_unref (msg);
	return bytes;
}

/* Champ "Url" d'une réponse Autodiscover JSON ({"Protocol":"EWS","Url":...}). */
static gchar *
core_autodiscover_url (GBytes *bytes)
{
	JsonParser *parser = json_parser_new ();
	gchar *url = NULL;
	gsize size = 0;
	const gchar *data = g_bytes_get_data (bytes, &size);

	if (json_parser_load_from_data (parser, data, size, NULL) &&
	    JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser))) {
		JsonObject *object = json_node_get_object (json_parser_get_root (parser));
		const gchar *value = json_object_get_string_member_with_default (object, "Url", "");

		if (*value)
			url = g_strdup (value);
	}

	g_object_unref (parser);
	return url;
}

/* Autodiscover Exchange : le service de Microsoft répond directement pour
 * une boîte Microsoft 365 / Exchange Online, sinon redirige vers
 * l'Autodiscover du domaine, qui répond (URL EWS) ou demande une
 * authentification (401) s'il s'agit d'un serveur Exchange de l'entreprise.
 * Paramètres :
 *   soup        : session HTTP
 *   email       : adresse du compte
 *   out_ews_url : reçoit l'URL EWS (g_free)
 *   cancellable : objet d'annulation
 * Retourne : "m365", "exchange" ou NULL. */
static const gchar *
core_probe_autodiscover (SoupSession *soup,
			 const gchar *email,
			 gchar **out_ews_url,
			 GCancellable *cancellable)
{
	gchar *escaped = g_uri_escape_string (email, "@", FALSE);
	gchar *uri = g_strdup_printf ("https://outlook.office365.com/autodiscover/autodiscover.json/v1.0/%s?Protocol=EWS", escaped);
	gchar *location = NULL;
	const gchar *type = NULL;
	guint status = 0;
	GBytes *bytes;

	bytes = core_http_get (soup, uri, FALSE, &status, &location, cancellable);
	if (bytes) {
		gchar *url = core_autodiscover_url (bytes);

		if (url && strstr (url, "://outlook.office365.com/")) {
			type = "m365";
			g_free (url);
		} else if (url) {
			type = "exchange";
			*out_ews_url = url;
		}
		g_bytes_unref (bytes);
	} else if (location && SOUP_STATUS_IS_REDIRECTION (status) && g_str_has_prefix (location, "https://")) {
		bytes = core_http_get (soup, location, FALSE, &status, NULL, cancellable);
		if (bytes) {
			*out_ews_url = core_autodiscover_url (bytes);
			if (*out_ews_url)
				type = "exchange";
			g_bytes_unref (bytes);
		} else if (status == SOUP_STATUS_UNAUTHORIZED) {
			/* serveur Exchange protégé : EWS sur le même serveur, à confirmer */
			GUri *guri = g_uri_parse (location, G_URI_FLAGS_NONE, NULL);

			if (guri) {
				*out_ews_url = g_strdup_printf ("https://%s/EWS/Exchange.asmx", g_uri_get_host (guri));
				type = "exchange";
				g_uri_unref (guri);
			}
		}
	}

	g_free (location);
	g_free (uri);
	g_free (escaped);

	return type;
}

/* Autoconfig : fichier du domaine (autoconfig.<domaine>, puis
 * .well-known), puis base ISPDB en ligne de Thunderbird.
 * Paramètres :
 *   soup        : session HTTP
 *   email       : adresse du compte
 *   domain      : domaine de l'adresse
 *   out_source  : reçoit "autoconfig" ou "ispdb"
 *   cancellable : objet d'annulation
 * Retourne : la configuration (core_mail_config_free), NULL sinon. */
static CoreMailConfig *
core_fetch_autoconfig (SoupSession *soup,
		       const gchar *email,
		       const gchar *domain,
		       const gchar **out_source,
		       GCancellable *cancellable)
{
	gchar *escaped = g_uri_escape_string (email, "@", FALSE);
	gchar *uris[4];
	CoreMailConfig *config = NULL;
	guint ii;

	uris[0] = g_strdup_printf ("https://autoconfig.%s/mail/config-v1.1.xml?emailaddress=%s", domain, escaped);
	uris[1] = g_strdup_printf ("https://%s/.well-known/autoconfig/mail/config-v1.1.xml?emailaddress=%s", domain, escaped);
	uris[2] = g_strdup_printf ("https://autoconfig.thunderbird.net/v1.1/%s", domain);
	uris[3] = NULL;

	for (ii = 0; !config && uris[ii] && !g_cancellable_is_cancelled (cancellable); ii++) {
		guint status = 0;
		GBytes *bytes = core_http_get (soup, uris[ii], TRUE, &status, NULL, cancellable);

		if (bytes) {
			gsize size = 0;
			const gchar *data = g_bytes_get_data (bytes, &size);

			config = core_mail_config_parse (data, size);
			if (config && !config->imap_host)
				g_clear_pointer (&config, core_mail_config_free);
			*out_source = ii < 2 ? "autoconfig" : "ispdb";
			g_bytes_unref (bytes);
		}
	}

	for (ii = 0; uris[ii]; ii++)
		g_free (uris[ii]);
	g_free (escaped);

	return config;
}

/* Détecte le type de compte et les serveurs d'une adresse, dans l'ordre :
 * fournisseurs connus (base ISPDB embarquée), enregistrements MX du
 * domaine, Autodiscover Exchange / Microsoft 365, autoconfig du domaine et
 * ISPDB en ligne, enregistrements SRV ; à défaut, serveurs supposés
 * (imap.<domaine>, smtp.<domaine>).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = adresse électronique
 *   error : emplacement pour une éventuelle erreur
 * Retourne : JSON {"type": "m365"|"exchange"|"google"|"imap",
 *            "source": "known"|"mx"|"autodiscover"|"autoconfig"|"ispdb"|"srv"|"guess",
 *            "provider", "user", "imap_host", "imap_port", "imap_security",
 *            "smtp_host", "smtp_port", "smtp_security", "oauth2", "ews_url"}. */
static gchar *
core_detect_account_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	const gchar *email = args[0];
	const gchar *at = strrchr (email, '@');
	const CoreMailConfig *config = NULL;
	CoreMailConfig *owned = NULL;
	const gchar *type = NULL, *source = NULL;
	gchar *domain, *mx, *ews_url = NULL, *value;
	SoupSession *soup;
	JsonBuilder *builder;
	gchar *json;

	if (!at || at == email || !at[1] || strchr (at + 1, '.') == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Adresse électronique non valide : « %s »", email);
		return NULL;
	}

	domain = g_ascii_strdown (at + 1, -1);

	/* 1. fournisseurs connus */
	if ((config = core_ispdb_lookup (domain)) != NULL) {
		type = core_mail_config_type (config);
		source = "known";
	}

	/* 2. serveurs MX du domaine */
	if (!type && (mx = core_dns_mx (domain)) != NULL) {
		if (g_str_has_suffix (mx, ".mail.protection.outlook.com") || g_str_has_suffix (mx, ".outlook.com")) {
			type = "m365";
			config = core_ispdb_lookup ("office365.com");
		} else if (g_str_has_suffix (mx, ".google.com") || g_str_has_suffix (mx, ".googlemail.com")) {
			type = "google";
			config = core_ispdb_lookup ("gmail.com");
		} else if ((config = core_ispdb_lookup_mx (mx)) != NULL) {
			type = core_mail_config_type (config);
		}
		if (type)
			source = "mx";
		g_free (mx);
	}

	soup = soup_session_new_with_options ("timeout", 6, "user-agent", "Evolution (Windows)", NULL);

	/* 3. Autodiscover Exchange / Microsoft 365 */
	if (!type && (type = core_probe_autodiscover (soup, email, &ews_url, core->cancellable)) != NULL) {
		source = "autodiscover";
		if (g_str_equal (type, "m365"))
			config = core_ispdb_lookup ("office365.com");
	}

	/* 4. autoconfig du domaine, ISPDB en ligne */
	if (!type && (owned = core_fetch_autoconfig (soup, email, domain, &source, core->cancellable)) != NULL) {
		config = owned;
		type = core_mail_config_type (config);
	}

	/* 5. enregistrements SRV */
	if (!type && (owned = core_srv_config (domain)) != NULL) {
		config = owned;
		type = "imap";
		source = "srv";
	}

	/* 6. serveurs supposés */
	if (!type) {
		owned = core_mail_config_new ();
		owned->imap_host = g_strconcat ("imap.", domain, NULL);
		owned->imap_port = 993;
		owned->imap_security = g_strdup ("tls");
		owned->smtp_host = g_strconcat ("smtp.", domain, NULL);
		owned->smtp_port = 465;
		owned->smtp_security = g_strdup ("tls");
		config = owned;
		type = "imap";
		source = "guess";
	}

	g_object_unref (soup);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "type", type);
	builder_add_string (builder, "source", source);
	builder_add_string (builder, "provider", config && config->display_name ? config->display_name : "");

	value = core_mail_config_expand (config && config->imap_user ? config->imap_user : "%EMAILADDRESS%", email);
	builder_add_string (builder, "user", value);
	g_free (value);

	value = core_mail_config_expand (config ? config->imap_host : NULL, email);
	builder_add_string (builder, "imap_host", value);
	g_free (value);
	json_builder_set_member_name (builder, "imap_port");
	json_builder_add_int_value (builder, config && config->imap_port > 0 ? config->imap_port : 993);
	builder_add_string (builder, "imap_security", config && config->imap_security ? config->imap_security : "tls");

	value = core_mail_config_expand (config ? config->smtp_host : NULL, email);
	builder_add_string (builder, "smtp_host", value);
	g_free (value);
	json_builder_set_member_name (builder, "smtp_port");
	json_builder_add_int_value (builder, config && config->smtp_port > 0 ? config->smtp_port : 465);
	builder_add_string (builder, "smtp_security", config && config->smtp_security ? config->smtp_security : "tls");

	json_builder_set_member_name (builder, "oauth2");
	json_builder_add_boolean_value (builder, config && config->imap_oauth2);

	value = core_mail_config_expand (ews_url ? ews_url : config ? config->ews_url : NULL, email);
	builder_add_string (builder, "ews_url", value);
	g_free (value);
	json_builder_end_object (builder);

	json = json_from_builder (builder);
	g_object_unref (builder);

	core_mail_config_free (owned);
	g_free (ews_url);
	g_free (domain);

	return json;
}

/* Réglages Camel (IMAP / SMTP) d'une source de courrier Gmail : serveur,
 * port, TLS, utilisateur et mécanisme « Google » (XOAUTH2 avec le jeton du
 * service OAuth2 Google d'EDS). */
void
core_set_google_mail_settings (ESource *source,
			       const gchar *backend,
			       const gchar *host,
			       guint16 port,
			       const gchar *email)
{
	CamelSettings *settings = e_source_camel_get_settings (
		e_source_get_extension (source, e_source_camel_get_extension_name (backend)));
	ESourceAuthentication *authentication = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	camel_network_settings_set_host (CAMEL_NETWORK_SETTINGS (settings), host);
	camel_network_settings_set_port (CAMEL_NETWORK_SETTINGS (settings), port);
	camel_network_settings_set_security_method (CAMEL_NETWORK_SETTINGS (settings),
		CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT);
	camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (settings), email);
	camel_network_settings_set_auth_mechanism (CAMEL_NETWORK_SETTINGS (settings), "Google");

	e_source_authentication_set_host (authentication, host);
	e_source_authentication_set_port (authentication, port);
	e_source_authentication_set_user (authentication, email);
	e_source_authentication_set_method (authentication, "Google");
}

void
evo_core_detect_account (EvoCore *core,
			 const char *email,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) email, NULL };

	g_return_if_fail (email != NULL);

	core_submit (core, core_detect_account_run, g_strdupv (args), func, user_data);
}
