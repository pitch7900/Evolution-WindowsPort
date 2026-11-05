/*
 * evo-core : Export des contacts (vCard) et des agendas (iCalendar).
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Exporte les contacts d'un carnet d'adresses (ou de tous les carnets
 * activés) au format vCard 3.0, dans un seul fichier .vcf ou dans un fichier
 * par contact.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON {"book_uid": "..." ou "", "path": "...", "per_contact": false}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"count": n, "files": ["...", ...]}, ou NULL en cas d'erreur. */
static gchar *
core_export_contacts_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *book_uid, *path;
	gboolean per_contact;
	GList *sources = NULL, *slink;
	GPtrArray *files;
	GString *single = NULL;
	EBookQuery *book_query;
	gchar *sexp, *json = NULL;
	JsonBuilder *builder;
	guint count = 0, ii;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	book_uid = object ? json_object_get_string_member_with_default (object, "book_uid", "") : "";
	path = object ? json_object_get_string_member_with_default (object, "path", "") : "";
	per_contact = object ? json_object_get_boolean_member_with_default (object, "per_contact", FALSE) : FALSE;

	if (!path || !*path) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Chemin d'exportation manquant");
		json_node_unref (root);
		return NULL;
	}

	if (book_uid && *book_uid) {
		ESource *source = e_source_registry_ref_source (core->registry, book_uid);

		if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Carnet d'adresses introuvable");
			g_clear_object (&source);
			json_node_unref (root);
			return NULL;
		}
		sources = g_list_append (NULL, source);
	} else {
		sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	}

	if (per_contact && g_mkdir_with_parents (path, 0700) != 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Impossible de créer le dossier « %s »", path);
		g_list_free_full (sources, g_object_unref);
		json_node_unref (root);
		return NULL;
	}

	book_query = e_book_query_any_field_contains ("");
	sexp = book_query ? e_book_query_to_string (book_query) : g_strdup ("");
	if (book_query)
		e_book_query_unref (book_query);

	files = g_ptr_array_new_with_free_func (g_free);
	if (!per_contact)
		single = g_string_new ("");

	for (slink = sources; slink; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		EClient *client;
		GSList *contacts = NULL, *clink;
		GError *local_error = NULL;

		client = e_book_client_connect_sync (source, 5, core->cancellable, &local_error);
		if (client && !e_book_client_get_contacts_sync (E_BOOK_CLIENT (client), sexp, &contacts, core->cancellable, &local_error))
			g_clear_object (&client);

		if (!client) {
			/* un carnet demandé explicitement doit être exporté, les autres sont ignorés */
			if (book_uid && *book_uid) {
				g_propagate_error (error, local_error);
				goto exit;
			}
			g_warning ("Cannot export address book “%s”: %s", e_source_get_display_name (source),
				local_error ? local_error->message : "unknown error");
			g_clear_error (&local_error);
			continue;
		}

		for (clink = contacts; clink; clink = g_slist_next (clink)) {
			EContact *contact = clink->data;
			gchar *vcard;

			if (!E_IS_CONTACT (contact))
				continue;

			vcard = e_vcard_convert_to_string (E_VCARD (contact), E_VCARD_VERSION_30);
			if (!vcard)
				continue;

			if (per_contact) {
				const gchar *name;
				gchar *clean_name, *file_path;

				name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
				if (!name || !*name)
					name = e_contact_get_const (contact, E_CONTACT_FILE_AS);
				if (!name || !*name)
					name = e_contact_get_const (contact, E_CONTACT_EMAIL_1);

				clean_name = sanitize_filename (name, "Contact");
				file_path = unique_file_path (path, clean_name, ".vcf");
				g_free (clean_name);

				if (!g_file_set_contents (file_path, vcard, -1, error)) {
					g_free (file_path);
					g_free (vcard);
					g_slist_free_full (contacts, g_object_unref);
					g_object_unref (client);
					goto exit;
				}

				g_ptr_array_add (files, file_path);
			} else {
				g_string_append (single, vcard);
				if (!g_str_has_suffix (vcard, "\n"))
					g_string_append (single, "\r\n");
			}

			g_free (vcard);
			count++;
		}

		g_slist_free_full (contacts, g_object_unref);
		g_object_unref (client);
	}

	if (count == 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Aucun contact à exporter");
		goto exit;
	}

	if (single) {
		if (!g_file_set_contents (path, single->str, (gssize) single->len, error))
			goto exit;
		g_ptr_array_add (files, g_strdup (path));
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "count");
	json_builder_add_int_value (builder, count);
	json_builder_set_member_name (builder, "files");
	json_builder_begin_array (builder);
	for (ii = 0; ii < files->len; ii++)
		json_builder_add_string_value (builder, files->pdata[ii]);
	json_builder_end_array (builder);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	if (single)
		g_string_free (single, TRUE);
	g_ptr_array_unref (files);
	g_free (sexp);
	g_list_free_full (sources, g_object_unref);
	json_node_unref (root);

	return json;
}

typedef struct {
	ECalClient *client;
	ICalComponent *vcalendar;
	GHashTable *tzids;
} CalExportTzData;

/* Ajoute au VCALENDAR exporté la définition VTIMEZONE d'un TZID référencé
 * par un composant, une seule fois par TZID (rappel de
 * i_cal_component_foreach_tzid).
 * Paramètres :
 *   param     : paramètre TZID d'une propriété du composant
 *   user_data : pointeur vers CalExportTzData */
static void
cal_export_add_timezone (ICalParameter *param,
			 gpointer user_data)
{
	CalExportTzData *data = user_data;
	ICalTimezone *zone = NULL;
	ICalComponent *vtimezone;
	const gchar *tzid;

	tzid = i_cal_parameter_get_tzid (param);
	if (!tzid || !*tzid || g_hash_table_contains (data->tzids, tzid))
		return;

	g_hash_table_add (data->tzids, g_strdup (tzid));

	/* fuseaux de l'agenda, sinon fuseaux intégrés à libical */
	if (!e_cal_client_get_timezone_sync (data->client, tzid, &zone, NULL, NULL) || !zone) {
		zone = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			zone = i_cal_timezone_get_builtin_timezone (tzid);
	}

	vtimezone = zone ? i_cal_timezone_get_component (zone) : NULL;
	if (!vtimezone) {
		g_warning ("No VTIMEZONE for TZID “%s”", tzid);
		return;
	}

	i_cal_component_take_component (data->vcalendar, i_cal_component_clone (vtimezone));
	g_object_unref (vtimezone);
}

/* Exporte tous les composants d'un agenda (VEVENT), d'une liste de tâches
 * (VTODO) ou d'une liste de mémos (VJOURNAL) dans un fichier iCalendar,
 * avec les définitions VTIMEZONE des fuseaux horaires référencés.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON {"source_uid": "...", "path": "...ics"}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"count": n}, ou NULL en cas d'erreur. */
static gchar *
core_export_calendar_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *source_uid, *path;
	ESource *source = NULL;
	EClient *client = NULL;
	ECalClientSourceType source_type;
	ICalComponent *vcalendar = NULL;
	GSList *objects = NULL, *olink;
	CalExportTzData tz_data = { NULL, NULL, NULL };
	gchar *ical = NULL, *json = NULL;
	guint count = 0;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	source_uid = object ? json_object_get_string_member_with_default (object, "source_uid", "") : "";
	path = object ? json_object_get_string_member_with_default (object, "path", "") : "";

	if (!source_uid || !*source_uid || !path || !*path) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Agenda ou chemin d'exportation manquant");
		goto exit;
	}

	source = e_source_registry_ref_source (core->registry, source_uid);
	if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR)) {
		source_type = E_CAL_CLIENT_SOURCE_TYPE_EVENTS;
	} else if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST)) {
		source_type = E_CAL_CLIENT_SOURCE_TYPE_TASKS;
	} else if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_MEMO_LIST)) {
		source_type = E_CAL_CLIENT_SOURCE_TYPE_MEMOS;
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Agenda ou liste de tâches introuvable");
		goto exit;
	}

	client = e_cal_client_connect_sync (source, source_type, 5, core->cancellable, error);
	if (!client)
		goto exit;

	if (!e_cal_client_get_object_list_sync (E_CAL_CLIENT (client), "#t", &objects, core->cancellable, error))
		goto exit;

	/* VERSION:2.0, PRODID et CALSCALE */
	vcalendar = e_cal_util_new_top_level ();

	/* les VTIMEZONE d'abord, puis les composants */
	tz_data.client = E_CAL_CLIENT (client);
	tz_data.vcalendar = vcalendar;
	tz_data.tzids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (olink = objects; olink; olink = g_slist_next (olink))
		i_cal_component_foreach_tzid (olink->data, cal_export_add_timezone, &tz_data);
	g_hash_table_destroy (tz_data.tzids);

	for (olink = objects; olink; olink = g_slist_next (olink)) {
		i_cal_component_add_component (vcalendar, olink->data);
		count++;
	}

	ical = i_cal_component_as_ical_string (vcalendar);
	if (!g_file_set_contents (path, ical ? ical : "", -1, error))
		goto exit;

	json = g_strdup_printf ("{\"count\":%u}", count);

 exit:
	g_free (ical);
	g_slist_free_full (objects, g_object_unref);
	g_clear_object (&vcalendar);
	g_clear_object (&client);
	g_clear_object (&source);
	json_node_unref (root);

	return json;
}

/* Soumet une requête d'exportation de contacts au format vCard.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : JSON {"book_uid": "...", "path": "...", "per_contact": false}
 *   func         : rappel recevant {"count": n, "files": [...]}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_export_contacts (EvoCore *core,
			  const char *request_json,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	core_submit (core, core_export_contacts_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête d'exportation d'un agenda ou d'une liste de tâches au
 * format iCalendar.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : JSON {"source_uid": "...", "path": "...ics"}
 *   func         : rappel recevant {"count": n}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_export_calendar (EvoCore *core,
			  const char *request_json,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	core_submit (core, core_export_calendar_run, g_strdupv (args), func, user_data);
}
