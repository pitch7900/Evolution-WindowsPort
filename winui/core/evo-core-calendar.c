/*
 * evo-core : Agendas : evenements (liste, detail, creation, modification, suppression).
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* --- catégories communes (CATEGORIES iCalendar / vCard) --- */

/* Catégories d'un composant iCalendar (propriétés CATEGORIES, chacune
 * pouvant contenir plusieurs valeurs séparées par des virgules).
 * Paramètres :
 *   icomp : composant VEVENT / VTODO
 * Retourne : tableau de chaînes terminé par NULL (g_strfreev), jamais NULL. */
gchar **
ical_dup_categories (ICalComponent *icomp)
{
	GPtrArray *names = g_ptr_array_new ();
	ICalProperty *prop;

	for (prop = i_cal_component_get_first_property (icomp, I_CAL_CATEGORIES_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_CATEGORIES_PROPERTY)) {
		const gchar *value = i_cal_property_get_categories (prop);
		gchar **parts;
		guint ii;

		if (!value)
			continue;

		parts = g_strsplit (value, ",", -1);
		for (ii = 0; parts[ii]; ii++) {
			g_strstrip (parts[ii]);
			if (*parts[ii])
				g_ptr_array_add (names, g_strdup (parts[ii]));
		}
		g_strfreev (parts);
	}

	g_ptr_array_add (names, NULL);

	return (gchar **) g_ptr_array_free (names, FALSE);
}

/* Ajoute au JSON le membre "categories" (tableau de chaînes).
 * Paramètres :
 *   builder : constructeur JSON, dans un objet
 *   names   : noms de catégories (terminé par NULL), NULL pour un tableau vide */
void
builder_add_categories (JsonBuilder *builder,
			const gchar * const *names)
{
	guint ii;

	json_builder_set_member_name (builder, "categories");
	json_builder_begin_array (builder);
	for (ii = 0; names && names[ii]; ii++)
		json_builder_add_string_value (builder, names[ii]);
	json_builder_end_array (builder);
}

/* Ajoute au JSON les catégories d'un composant iCalendar.
 * Paramètres :
 *   builder : constructeur JSON, dans un objet
 *   icomp   : composant VEVENT / VTODO */
void
ical_add_categories (JsonBuilder *builder,
		     ICalComponent *icomp)
{
	gchar **names = ical_dup_categories (icomp);

	builder_add_categories (builder, (const gchar * const *) names);
	g_strfreev (names);
}

/* Remplace les catégories d'un composant iCalendar si l'objet JSON contient
 * le membre "categories" (tableau de chaînes) ; sinon ne touche à rien.
 * Une propriété CATEGORIES est écrite par catégorie ; les noms vides ou
 * contenant une virgule sont ignorés.
 * Paramètres :
 *   icomp  : composant VEVENT / VTODO à modifier
 *   object : objet JSON décrivant la modification */
void
ical_apply_categories_json (ICalComponent *icomp,
			    JsonObject *object)
{
	JsonNode *node;

	if (!json_object_has_member (object, "categories"))
		return;

	node = json_object_get_member (object, "categories");
	e_cal_util_component_remove_property_by_kind (icomp, I_CAL_CATEGORIES_PROPERTY, TRUE);

	if (node && JSON_NODE_HOLDS_ARRAY (node)) {
		JsonArray *array = json_node_get_array (node);
		guint ii;

		for (ii = 0; ii < json_array_get_length (array); ii++) {
			const gchar *category = json_array_get_string_element (array, ii);
			gchar *clean;

			if (!category)
				continue;

			clean = g_strstrip (g_strdup (category));
			if (*clean && !strchr (clean, ','))
				i_cal_component_take_property (icomp, i_cal_property_new_categories (clean));
			g_free (clean);
		}
	}
}

/* Ouvre l'agenda calendar_uid (connexion EDS). */
static EClient *
core_connect_calendar (EvoCore *core,
		       const gchar *calendar_uid,
		       GError **error)
{
	ESource *source = e_source_registry_ref_source (core->registry, calendar_uid);
	EClient *client;

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Agenda introuvable : « %s »", calendar_uid);
		return NULL;
	}

	client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, core->cancellable, error);
	g_object_unref (source);

	return client;
}

/* Nom et adresse d'une propriété ORGANIZER / ATTENDEE (CN, mailto:). */
static void
builder_add_ical_person (JsonBuilder *builder,
			 ICalProperty *prop,
			 const gchar *address)
{
	ICalParameter *cn = i_cal_property_get_first_parameter (prop, I_CAL_CN_PARAMETER);
	const gchar *email = address ? address : "";

	if (g_ascii_strncasecmp (email, "mailto:", 7) == 0)
		email += 7;

	builder_add_string (builder, "name", cn && i_cal_parameter_get_cn (cn) ? i_cal_parameter_get_cn (cn) : "");
	builder_add_string (builder, "email", email);
	g_clear_object (&cn);
}

/* Détail d'un événement : champs modifiables, organisateur, participants
 * et leur réponse, périodicité, agenda en lecture seule.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid de l'agenda, [1] = uid de l'événement
 *   error : emplacement pour une éventuelle erreur
 * Retourne : JSON de l'événement, NULL sinon. */
static gchar *
core_get_event_run (EvoCore *core,
		    gchar **args,
		    GError **error)
{
	EClient *client;
	ICalComponent *icomp = NULL;
	ICalProperty *prop;
	ICalTime *dtstart, *dtend;
	JsonBuilder *builder;
	gchar *json = NULL;
	gint64 st = 0, et = 0;
	gboolean all_day = FALSE;

	client = core_connect_calendar (core, args[0], error);
	if (!client)
		return NULL;

	if (!e_cal_client_get_object_sync (E_CAL_CLIENT (client), args[1], NULL, &icomp, core->cancellable, error))
		goto exit;

	dtstart = i_cal_component_get_dtstart (icomp);
	dtend = i_cal_component_get_dtend (icomp);
	if (dtstart && !i_cal_time_is_null_time (dtstart)) {
		st = (gint64) i_cal_time_as_timet (dtstart);
		all_day = i_cal_time_is_date (dtstart);
	}
	et = dtend && !i_cal_time_is_null_time (dtend) ? (gint64) i_cal_time_as_timet (dtend) : st + 3600;
	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", args[1]);
	builder_add_string (builder, "calendar_uid", args[0]);
	builder_add_string (builder, "calendar_name", e_source_get_display_name (e_client_get_source (client)));
	builder_add_string (builder, "summary", i_cal_component_get_summary (icomp));
	builder_add_string (builder, "location", i_cal_component_get_location (icomp));

	prop = i_cal_component_get_first_property (icomp, I_CAL_DESCRIPTION_PROPERTY);
	builder_add_string (builder, "description", prop ? i_cal_property_get_description (prop) : "");
	g_clear_object (&prop);

	json_builder_set_member_name (builder, "start");
	json_builder_add_int_value (builder, st);
	json_builder_set_member_name (builder, "end");
	json_builder_add_int_value (builder, et);
	json_builder_set_member_name (builder, "all_day");
	json_builder_add_boolean_value (builder, all_day);

	/* fuseau horaire (TZID) du début et heures murales dans ce fuseau */
	{
		ICalProperty *dtstart_prop = i_cal_component_get_first_property (icomp, I_CAL_DTSTART_PROPERTY);
		ICalParameter *tzparam = dtstart_prop ? i_cal_property_get_first_parameter (dtstart_prop, I_CAL_TZID_PARAMETER) : NULL;
		const gchar *tzid = tzparam ? i_cal_parameter_get_tzid (tzparam) : NULL;

		builder_add_string (builder, "timezone", tzid ? tzid : "");
		if (tzid && *tzid && !all_day) {
			ICalTime *ws = i_cal_component_get_dtstart (icomp);
			ICalTime *we = i_cal_component_get_dtend (icomp);

			builder_add_string (builder, "start_wall", ws ? i_cal_time_as_ical_string (ws) : "");
			builder_add_string (builder, "end_wall", we ? i_cal_time_as_ical_string (we) : "");
			g_clear_object (&ws);
			g_clear_object (&we);
		}
		g_clear_object (&tzparam);
		g_clear_object (&dtstart_prop);
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_RRULE_PROPERTY);
	json_builder_set_member_name (builder, "recurring");
	json_builder_add_boolean_value (builder, prop != NULL);
	g_clear_object (&prop);

	json_builder_set_member_name (builder, "reminder_minutes");
	json_builder_add_int_value (builder, event_get_reminder (icomp));

	json_builder_set_member_name (builder, "readonly");
	json_builder_add_boolean_value (builder, e_client_is_readonly (client));

	ical_add_categories (builder, icomp);

	prop = i_cal_component_get_first_property (icomp, I_CAL_ORGANIZER_PROPERTY);
	json_builder_set_member_name (builder, "organizer");
	json_builder_begin_object (builder);
	if (prop)
		builder_add_ical_person (builder, prop, i_cal_property_get_organizer (prop));
	json_builder_end_object (builder);
	g_clear_object (&prop);

	json_builder_set_member_name (builder, "attendees");
	json_builder_begin_array (builder);
	for (prop = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY); prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY)) {
		ICalParameter *partstat = i_cal_property_get_first_parameter (prop, I_CAL_PARTSTAT_PARAMETER);
		const gchar *status = "needs-action";

		if (partstat) {
			switch (i_cal_parameter_get_partstat (partstat)) {
			case I_CAL_PARTSTAT_ACCEPTED: status = "accepted"; break;
			case I_CAL_PARTSTAT_DECLINED: status = "declined"; break;
			case I_CAL_PARTSTAT_TENTATIVE: status = "tentative"; break;
			case I_CAL_PARTSTAT_DELEGATED: status = "delegated"; break;
			default: break;
			}
			g_object_unref (partstat);
		}

		json_builder_begin_object (builder);
		builder_add_ical_person (builder, prop, i_cal_property_get_attendee (prop));
		builder_add_string (builder, "status", status);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);

	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_clear_object (&icomp);
	g_object_unref (client);

	return json;
}

/* Supprime les rappels (VALARM) d'un événement puis, si minutes >= 0, ajoute
 * un rappel d'affichage déclenché « minutes » minutes avant le début.
 * Paramètres :
 *   icomp   : composant VEVENT à modifier
 *   minutes : délai en minutes, négatif pour aucun rappel */
static void
event_set_reminder (ICalComponent *icomp,
		    gint minutes)
{
	ICalComponent *alarm;
	ICalTrigger *trigger;

	while ((alarm = i_cal_component_get_first_component (icomp, I_CAL_VALARM_COMPONENT)) != NULL) {
		i_cal_component_remove_component (icomp, alarm);
		g_object_unref (alarm);
	}

	if (minutes < 0)
		return;

	alarm = i_cal_component_new_valarm ();
	i_cal_component_take_property (alarm, i_cal_property_new_action (I_CAL_ACTION_DISPLAY));
	trigger = i_cal_trigger_new_from_int (-minutes * 60);
	i_cal_component_take_property (alarm, i_cal_property_new_trigger (trigger));
	g_object_unref (trigger);
	i_cal_component_take_property (alarm, i_cal_property_new_description ("Rappel"));
	i_cal_component_take_component (icomp, alarm);
}

/* Applique début, fin, fuseau horaire et participants d'un événement JSON
 * sur un composant VEVENT (création comme modification).
 * Champs lus : "start" / "end" (secondes Unix), "all_day", "timezone" (nom
 * IANA, ex. « Europe/Zurich »), "start_wall" / "end_wall" (heure locale du
 * fuseau, « AAAAMMJJTHHMMSS ») et "attendees" (tableau d'adresses).
 * Paramètres :
 *   client : agenda (reçoit le VTIMEZONE du fuseau choisi)
 *   icomp  : composant VEVENT à modifier
 *   object : événement JSON */
static void
event_apply_times_and_attendees (ECalClient *client,
				 ICalComponent *icomp,
				 JsonObject *object)
{
	gint64 start_unix = json_object_get_int_member_with_default (object, "start", 0);
	gint64 end_unix = json_object_get_int_member_with_default (object, "end", start_unix + 3600);
	gboolean all_day = json_object_get_boolean_member_with_default (object, "all_day", FALSE);
	const gchar *tzid = json_object_get_string_member_with_default (object, "timezone", "");
	const gchar *start_wall = json_object_get_string_member_with_default (object, "start_wall", "");
	const gchar *end_wall = json_object_get_string_member_with_default (object, "end_wall", "");
	ICalTimezone *zone = NULL;
	ICalTime *time_value;

	if (!all_day && *tzid)
		zone = i_cal_timezone_get_builtin_timezone (tzid);

	if (zone && *start_wall && *end_wall) {
		/* heure murale dans le fuseau choisi (comme Outlook) */
		time_value = i_cal_time_new_from_string (start_wall);
		i_cal_time_set_timezone (time_value, zone);
		i_cal_component_set_dtstart (icomp, time_value);
		g_object_unref (time_value);

		time_value = i_cal_time_new_from_string (end_wall);
		i_cal_time_set_timezone (time_value, zone);
		i_cal_component_set_dtend (icomp, time_value);
		g_object_unref (time_value);

		e_cal_client_add_timezone_sync (client, zone, NULL, NULL);
	} else if (start_unix > 0) {
		/* journée entière : dates sans heure ; sinon heures en UTC */
		ICalTimezone *utc = all_day ? NULL : i_cal_timezone_get_utc_timezone ();

		time_value = i_cal_time_new_from_timet_with_zone ((time_t) start_unix, all_day, utc);
		i_cal_component_set_dtstart (icomp, time_value);
		g_object_unref (time_value);

		time_value = i_cal_time_new_from_timet_with_zone ((time_t) MAX (end_unix, start_unix), all_day, utc);
		i_cal_component_set_dtend (icomp, time_value);
		g_object_unref (time_value);
	}

	if (json_object_has_member (object, "attendees")) {
		JsonArray *list = json_object_get_array_member (object, "attendees");
		ICalProperty *prop;
		guint i;

		/* conserve les participants existants dont l'adresse reste listée
		 * (leur réponse est ainsi préservée) ; ajoute les nouveaux */
		prop = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);
		while (prop) {
			ICalProperty *next = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY);
			const gchar *value = i_cal_property_get_attendee (prop);
			gboolean keep = FALSE;

			if (value && list) {
				const gchar *addr = g_ascii_strncasecmp (value, "mailto:", 7) == 0 ? value + 7 : value;

				for (i = 0; i < json_array_get_length (list); i++)
					if (g_ascii_strcasecmp (addr, json_array_get_string_element (list, i)) == 0)
						keep = TRUE;
			}
			if (!keep)
				i_cal_component_remove_property (icomp, prop);
			g_object_unref (prop);
			prop = next;
		}

		for (i = 0; list && i < json_array_get_length (list); i++) {
			const gchar *addr = json_array_get_string_element (list, i);
			gboolean present = FALSE;
			gchar *uri;

			if (!addr || !*addr)
				continue;
			for (prop = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY); prop;
			     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY)) {
				const gchar *value = i_cal_property_get_attendee (prop);

				if (value && g_ascii_strcasecmp (g_ascii_strncasecmp (value, "mailto:", 7) == 0 ? value + 7 : value, addr) == 0)
					present = TRUE;
			}
			if (present)
				continue;

			uri = g_strconcat ("mailto:", addr, NULL);
			prop = i_cal_property_new_attendee (uri);
			i_cal_property_take_parameter (prop, i_cal_parameter_new_partstat (I_CAL_PARTSTAT_NEEDSACTION));
			i_cal_property_take_parameter (prop, i_cal_parameter_new_role (I_CAL_ROLE_REQPARTICIPANT));
			i_cal_property_take_parameter (prop, i_cal_parameter_new_rsvp (I_CAL_RSVP_TRUE));
			i_cal_component_take_property (icomp, prop);
			g_free (uri);
		}
	}
}

/* Délai du premier rappel (VALARM) d'un événement.
 * Paramètres :
 *   icomp : composant VEVENT
 * Retourne : minutes avant le début, -1 s'il n'y a aucun rappel relatif. */
static gint
event_get_reminder (ICalComponent *icomp)
{
	ICalComponent *alarm = i_cal_component_get_first_component (icomp, I_CAL_VALARM_COMPONENT);
	ICalProperty *prop;
	gint minutes = -1;

	if (!alarm)
		return -1;

	prop = i_cal_component_get_first_property (alarm, I_CAL_TRIGGER_PROPERTY);
	if (prop) {
		ICalTrigger *trigger = i_cal_property_get_trigger (prop);
		ICalDuration *duration = trigger ? i_cal_trigger_get_duration (trigger) : NULL;

		if (duration && !i_cal_duration_is_null_duration (duration)) {
			gint secs = i_cal_duration_as_int (duration);

			if (secs <= 0)
				minutes = (-secs) / 60;
		}
		g_clear_object (&duration);
		g_clear_object (&trigger);
		g_object_unref (prop);
	}
	g_object_unref (alarm);

	return minutes;
}

/* Modifie un événement existant (toute la série s'il est périodique).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON {"calendar_uid", "uid", "summary", "location",
 *           "description", "start", "end", "all_day"}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_modify_event_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	JsonNode *root;
	JsonObject *object;
	EClient *client = NULL;
	ICalComponent *icomp = NULL;
	ICalProperty *prop;
	const gchar *summary, *location, *description;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Événement non valide");
		goto exit;
	}

	client = core_connect_calendar (core, json_object_get_string_member_with_default (object, "calendar_uid", ""), error);
	if (!client)
		goto exit;

	if (!e_cal_client_get_object_sync (E_CAL_CLIENT (client), json_object_get_string_member_with_default (object, "uid", ""),
		NULL, &icomp, core->cancellable, error))
		goto exit;

	summary = json_object_get_string_member_with_default (object, "summary", "");
	location = json_object_get_string_member_with_default (object, "location", "");
	description = json_object_get_string_member_with_default (object, "description", "");

	/* modification partielle : seuls les champs présents dans le JSON sont
	 * touchés (ex. {"calendar_uid", "uid", "categories"} ne change que les
	 * catégories) */
	if (json_object_has_member (object, "summary"))
		i_cal_component_set_summary (icomp, *summary ? summary : "(Sans titre)");
	if (json_object_has_member (object, "location"))
		i_cal_component_set_location (icomp, location);

	if (json_object_has_member (object, "description")) {
		while ((prop = i_cal_component_get_first_property (icomp, I_CAL_DESCRIPTION_PROPERTY)) != NULL) {
			i_cal_component_remove_property (icomp, prop);
			g_object_unref (prop);
		}
		if (*description)
			i_cal_component_take_property (icomp, i_cal_property_new_description (description));
	}

	ical_apply_categories_json (icomp, object);
	event_apply_times_and_attendees (E_CAL_CLIENT (client), icomp, object);

	if (json_object_has_member (object, "reminder_minutes"))
		event_set_reminder (icomp, (gint) json_object_get_int_member (object, "reminder_minutes"));

	if (e_cal_client_modify_object_sync (E_CAL_CLIENT (client), icomp, E_CAL_OBJ_MOD_ALL,
		E_CAL_OPERATION_FLAG_NONE, core->cancellable, error))
		json = g_strdup ("{}");

 exit:
	g_clear_object (&icomp);
	g_clear_object (&client);
	json_node_unref (root);

	return json;
}

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
	g_free (item->calendar_color);
	g_strfreev (item->categories);
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

/* Heure Unix d'une date / heure iCalendar : dans son fuseau (TZID), en heure
 * locale pour une date sans heure (journée entière), UTC sinon. */
static gint64
cal_time_to_unix (ICalTime *value)
{
	ICalTimezone *zone;

	if (!value || i_cal_time_is_null_time (value))
		return 0;

	if (i_cal_time_is_date (value))
		return (gint64) i_cal_time_as_timet_with_zone (value, e_cal_util_get_system_timezone ());

	zone = i_cal_time_get_timezone (value);
	return (gint64) (zone ? i_cal_time_as_timet_with_zone (value, zone) : i_cal_time_as_timet (value));
}

/* Élément de la liste des événements pour une occurrence de icomp. */
static CalEventItem *
cal_event_item_new (ICalComponent *icomp,
		    ESource *source,
		    ICalTime *start,
		    ICalTime *end)
{
	CalEventItem *item = g_new0 (CalEventItem, 1);
	ICalProperty *prop;

	item->uid = g_strdup (i_cal_component_get_uid (icomp));
	item->summary = g_strdup (i_cal_component_get_summary (icomp) ? i_cal_component_get_summary (icomp) : "(Sans titre)");
	item->location = g_strdup (i_cal_component_get_location (icomp) ? i_cal_component_get_location (icomp) : "");

	prop = i_cal_component_get_first_property (icomp, I_CAL_DESCRIPTION_PROPERTY);
	item->description = g_strdup (prop && i_cal_property_get_description (prop) ? i_cal_property_get_description (prop) : "");
	g_clear_object (&prop);

	prop = i_cal_component_get_first_property (icomp, I_CAL_RRULE_PROPERTY);
	item->recurring = prop != NULL || e_cal_util_component_has_recurrences (icomp) ||
		e_cal_util_component_is_instance (icomp);
	g_clear_object (&prop);

	item->categories = ical_dup_categories (icomp);

	item->all_day = start && i_cal_time_is_date (start);
	item->start = cal_time_to_unix (start);
	item->end = end && !i_cal_time_is_null_time (end) ? cal_time_to_unix (end) : item->start + 3600;

	item->calendar_uid = g_strdup (e_source_get_uid (source));
	item->calendar_name = g_strdup (e_source_get_display_name (source));
	if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR))
		item->calendar_color = e_source_selectable_dup_color (
			e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR));

	return item;
}

typedef struct {
	GPtrArray *items;
	ESource *source;
} CalInstancesData;

static gboolean
cal_event_instance_cb (ICalComponent *icomp,
		       ICalTime *instance_start,
		       ICalTime *instance_end,
		       gpointer user_data,
		       GCancellable *cancellable,
		       GError **error)
{
	CalInstancesData *data = user_data;

	if (icomp && i_cal_component_get_uid (icomp))
		g_ptr_array_add (data->items, cal_event_item_new (icomp, data->source, instance_start, instance_end));

	return TRUE;
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
	gchar **only;
	guint ii;
	gboolean ranged = start_unix > 0 && end_unix > 0 && end_unix >= start_unix;

	if (ranged) {
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

	/* args[2] : uids des agendas à lire, séparés par des virgules (tous sinon) */
	only = args[2] && *args[2] ? g_strsplit (args[2], ",", -1) : NULL;

	for (slink = sources; slink; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		EClient *client;
		GSList *objects = NULL, *olink;

		if (only && !g_strv_contains ((const gchar * const *) only, e_source_get_uid (source)))
			continue;

		GError *local_error = NULL;

		client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, core->cancellable, &local_error);
		if (!client) {
			g_warning ("Cannot open calendar “%s” (%s): %s", e_source_get_display_name (source),
				e_source_get_uid (source), local_error ? local_error->message : "unknown error");
			g_clear_error (&local_error);
			continue;
		}

		if (ranged) {
			/* une entrée par occurrence (événements périodiques développés) */
			CalInstancesData data = { items, source };

			e_cal_client_generate_instances_sync (E_CAL_CLIENT (client), (time_t) start_unix, (time_t) end_unix,
				core->cancellable, cal_event_instance_cb, &data);
		} else if (!e_cal_client_get_object_list_sync (E_CAL_CLIENT (client), sexp, &objects, core->cancellable, &local_error)) {
			g_warning ("Cannot read calendar “%s” (%s): %s", e_source_get_display_name (source),
				e_source_get_uid (source), local_error ? local_error->message : "unknown error");
			g_clear_error (&local_error);
		} else {
			for (olink = objects; olink; olink = g_slist_next (olink)) {
				ICalComponent *icomp = olink->data;
				ICalTime *dtstart, *dtend;

				if (!icomp || !i_cal_component_get_uid (icomp))
					continue;

				dtstart = i_cal_component_get_dtstart (icomp);
				dtend = i_cal_component_get_dtend (icomp);
				g_ptr_array_add (items, cal_event_item_new (icomp, source, dtstart, dtend));
				g_clear_object (&dtstart);
				g_clear_object (&dtend);
			}
			g_slist_free_full (objects, g_object_unref);
		}

		g_object_unref (client);
	}

	g_list_free_full (sources, g_object_unref);
	g_free (sexp);
	g_strfreev (only);

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
		builder_add_string (builder, "calendar_color", item->calendar_color);
		json_builder_set_member_name (builder, "all_day");
		json_builder_add_boolean_value (builder, item->all_day);
		json_builder_set_member_name (builder, "recurring");
		json_builder_add_boolean_value (builder, item->recurring);
		builder_add_categories (builder, (const gchar * const *) item->categories);
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

	g_message ("New event in calendar “%s” (%s)", e_source_get_display_name (source), e_source_get_uid (source));

	client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, core->cancellable, error);
	if (!client)
		goto exit;

	json_object_set_int_member (object, "start", start_unix);
	json_object_set_int_member (object, "end", end_unix);
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

	ical_apply_categories_json (icomp, object);
	event_apply_times_and_attendees (E_CAL_CLIENT (client), icomp, object);

	if (json_object_has_member (object, "reminder_minutes"))
		event_set_reminder (icomp, (gint) json_object_get_int_member (object, "reminder_minutes"));

	/* réunion en ligne Microsoft Teams : le backend Microsoft 365 la crée côté serveur
	 * (isOnlineMeeting, teamsForBusiness) quand cette propriété est présente */
	if (json_object_get_boolean_member_with_default (object, "online_meeting", FALSE)) {
		ICalProperty *teams = i_cal_property_new (I_CAL_X_PROPERTY);

		i_cal_property_set_x_name (teams, "X-M365-ONLINE-MEETING");
		i_cal_property_set_x (teams, "TRUE");
		i_cal_component_take_property (icomp, teams);
	}

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

/* Actualise auprès des serveurs distants (Google Agenda, Outlook, CalDAV)
 * tous les agendas activés.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : inutilisé
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}". */
static gchar *
core_refresh_calendars_run (EvoCore *core,
			    gchar **args,
			    GError **error)
{
	GList *sources, *link;

	/* demande d'abord aux collections (Exchange Online, Google...) de
	 * redécouvrir leurs agendas, groupes d'agendas compris */
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_COLLECTION);
	for (link = sources; link; link = g_list_next (link)) {
		ESource *collection = link->data;

		if (e_source_collection_get_calendar_enabled (e_source_get_extension (collection, E_SOURCE_EXTENSION_COLLECTION)))
			e_source_registry_refresh_backend_sync (core->registry, e_source_get_uid (collection), core->cancellable, NULL);
	}
	g_list_free_full (sources, g_object_unref);

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_CALENDAR);
	for (link = sources; link && !g_cancellable_is_cancelled (core->cancellable); link = g_list_next (link)) {
		EClient *client = core_connect_calendar (core, e_source_get_uid (link->data), NULL);

		if (!client)
			continue;
		if (e_client_check_refresh_supported (client))
			e_client_refresh_sync (client, core->cancellable, NULL);
		g_object_unref (client);
	}
	g_list_free_full (sources, g_object_unref);

	return g_strdup ("{}");
}

void
evo_core_refresh_calendars (EvoCore *core,
			    EvoCoreResultFunc func,
			    void *user_data)
{
	g_return_if_fail (core != NULL);

	core_submit (core, core_refresh_calendars_run, g_new0 (gchar *, 1), func, user_data);
}

/* Soumet une requête de lecture des événements des agendas activés.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   start_unix : début de la plage temporelle en secondes Unix
 *   end_unix   : fin de la plage temporelle en secondes Unix
 *   func       : rappel recevant le tableau JSON des événements
 *   user_data  : données utilisateur transmises au rappel */
void
evo_core_get_event (EvoCore *core,
		    const char *calendar_uid,
		    const char *event_uid,
		    EvoCoreResultFunc func,
		    void *user_data)
{
	gchar *args[] = { (gchar *) calendar_uid, (gchar *) event_uid, NULL };

	g_return_if_fail (calendar_uid != NULL && event_uid != NULL);

	core_submit (core, core_get_event_run, g_strdupv (args), func, user_data);
}

void
evo_core_modify_event (EvoCore *core,
		       const char *event_json,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) event_json, NULL };

	g_return_if_fail (event_json != NULL);

	core_submit (core, core_modify_event_run, g_strdupv (args), func, user_data);
}

void
evo_core_list_events (EvoCore *core,
		      long long start_unix,
		      long long end_unix,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	evo_core_list_calendar_events (core, start_unix, end_unix, NULL, func, user_data);
}

void
evo_core_list_calendar_events (EvoCore *core,
			       long long start_unix,
			       long long end_unix,
			       const char *calendar_uids,
			       EvoCoreResultFunc func,
			       void *user_data)
{
	gchar start_buf[32], end_buf[32];
	gchar *args[] = { start_buf, end_buf, (gchar *) (calendar_uids ? calendar_uids : ""), NULL };

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
