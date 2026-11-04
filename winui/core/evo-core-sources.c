/*
 * evo-core : Sources EDS (agendas, taches, memos, carnets) et creation d'agendas.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Crée un agenda : agenda local vide, ou agenda web abonné à une URL
 * iCalendar (backend EDS « webcal »).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON {"name": "...", "url": "..." ("" pour un agenda local),
 *           "color": "#rrggbb" (optionnel)}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"uid": "..."} en cas de succès, NULL sinon. */
static gchar *
core_create_calendar_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	JsonNode *root;
	JsonObject *object;
	ESource *source = NULL;
	ESourceCalendar *calendar;
	const gchar *name, *url, *color;
	gchar *url_fixed = NULL;
	gboolean is_web, success = FALSE;
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

	name = json_object_get_string_member_with_default (object, "name", "");
	url = json_object_get_string_member_with_default (object, "url", "");
	color = json_object_get_string_member_with_default (object, "color", "");
	is_web = url && *url;

	if (!name || !*name) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "L'agenda n'a pas de nom");
		goto exit;
	}

	if (is_web && !g_ascii_strncasecmp (url, "webcal://", 9)) {
		/* le backend accepte webcal:// et le convertit selon ESourceSecurity ;
		 * https est la valeur sûre par défaut */
		url_fixed = g_strconcat ("https://", url + 9, NULL);
		url = url_fixed;
	}

	if (is_web && g_ascii_strncasecmp (url, "http://", 7) && g_ascii_strncasecmp (url, "https://", 8)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"L'adresse doit commencer par http://, https:// ou webcal://");
		goto exit;
	}

	source = e_source_new (NULL, NULL, error);
	if (!source)
		goto exit;

	e_source_set_display_name (source, name);
	e_source_set_parent (source, is_web ? "webcal-stub" : "local-stub");

	calendar = e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (calendar), is_web ? "webcal" : "local");
	if (color && *color)
		e_source_selectable_set_color (E_SOURCE_SELECTABLE (calendar), color);

	if (is_web) {
		ESourceWebdav *webdav = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		ESourceSecurity *security = e_source_get_extension (source, E_SOURCE_EXTENSION_SECURITY);
		ESourceRefresh *refresh = e_source_get_extension (source, E_SOURCE_EXTENSION_REFRESH);
		GUri *uri = g_uri_parse (url, G_URI_FLAGS_PARSE_RELAXED, error);

		if (!uri)
			goto exit;

		e_source_webdav_set_uri (webdav, uri);
		g_uri_unref (uri);
		e_source_security_set_secure (security, g_ascii_strncasecmp (url, "https://", 8) == 0);
		/* actualisation périodique de l'abonnement (30 minutes) */
		e_source_refresh_set_enabled (refresh, TRUE);
		e_source_refresh_set_interval_minutes (refresh, 30);
	}

	success = e_source_registry_commit_source_sync (core->registry, source, core->cancellable, error);
	if (success) {
		JsonBuilder *builder = json_builder_new ();

		json_builder_begin_object (builder);
		builder_add_string (builder, "uid", e_source_get_uid (source));
		json_builder_end_object (builder);
		json = json_from_builder (builder);
		g_object_unref (builder);
	}

 exit:
	g_free (url_fixed);
	g_clear_object (&source);
	json_node_unref (root);

	return json;
}

/* Choisit un chemin libre « dossier\nom.ext », sinon « nom (2).ext »...
 * Paramètres :
 *   dir  : dossier de destination
 *   name : nom de fichier déjà nettoyé, sans extension
 *   ext  : extension avec le point (".vcf")
 * Retourne : chemin nouvellement alloué (à libérer avec g_free). */
gchar *
unique_file_path (const gchar *dir,
		  const gchar *name,
		  const gchar *ext)
{
	gchar *filename = g_strconcat (name, ext, NULL);
	gchar *path = g_build_filename (dir, filename, NULL);
	guint suffix = 1;

	while (g_file_test (path, G_FILE_TEST_EXISTS)) {
		g_free (filename);
		g_free (path);
		filename = g_strdup_printf ("%s (%u)%s", name, ++suffix, ext);
		path = g_build_filename (dir, filename, NULL);
	}

	g_free (filename);

	return path;
}

/* Soumet une requête de création d'un agenda (local vide ou abonnement à une
 * URL iCalendar / webcal).
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : JSON {"name": "...", "url": "..." ("" pour local), "color": "#rrggbb"}
 *   func         : rappel recevant {"uid": "..."}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_create_calendar (EvoCore *core,
			  const char *request_json,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	core_submit (core, core_create_calendar_run, g_strdupv (args), func, user_data);
}
