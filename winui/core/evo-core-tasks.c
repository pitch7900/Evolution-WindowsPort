/*
 * evo-core : Listes de taches (VTODO).
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* --- tâches (ECalClient, VTODO) --- */

/* États « en attente » et « différée » : absents d'iCalendar (RFC 5545), ils
 * sont conservés dans cette propriété, à côté d'un STATUS standard que les
 * autres clients comprennent (IN-PROCESS pour « en attente », NEEDS-ACTION
 * pour « différée »). */
#define TASK_X_STATUS "X-EVOLUTIONWIN-STATUS"

/* Ouvre la liste de tâches demandée, ou la liste de tâches par défaut.
 * Paramètres :
 *   core     : instance du moteur evo-core
 *   list_uid : identifiant de la liste de tâches ("" ou NULL pour celle par défaut)
 *   error    : emplacement pour une éventuelle erreur
 * Retourne : client connecté (à libérer avec g_object_unref), NULL sinon. */
static ECalClient *
task_list_connect (EvoCore *core,
		   const gchar *list_uid,
		   GError **error)
{
	ESource *source = NULL;
	EClient *client;

	if (list_uid && *list_uid) {
		source = e_source_registry_ref_source (core->registry, list_uid);
		if (source && !e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
			g_clear_object (&source);
	} else {
		source = e_source_registry_ref_default_task_list (core->registry);
	}

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Liste de tâches introuvable");
		return NULL;
	}

	client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_TASKS, 5, core->cancellable, error);
	g_object_unref (source);

	return client ? E_CAL_CLIENT (client) : NULL;
}

/* Convertit une date de tâche (DTSTART, DUE, COMPLETED) en secondes Unix :
 * date seule = minuit local, heure UTC, heure avec TZID (fuseau de la liste
 * de tâches ou fuseau intégré à libical), heure flottante = heure locale.
 * Paramètres :
 *   client    : liste de tâches du composant (résolution des TZID)
 *   icomp     : composant VTODO
 *   kind      : propriété lue (I_CAL_DTSTART_PROPERTY, I_CAL_DUE_PROPERTY...)
 *   date_only : reçoit TRUE pour une date sans heure (peut être NULL)
 * Retourne : secondes Unix, 0 si la propriété est absente. */
static gint64
task_property_to_unix (ECalClient *client,
		       ICalComponent *icomp,
		       ICalPropertyKind kind,
		       gboolean *date_only)
{
	ICalProperty *prop;
	ICalParameter *param;
	ICalTime *tt = NULL;
	gint64 result = 0;

	if (date_only)
		*date_only = FALSE;

	prop = i_cal_component_get_first_property (icomp, kind);
	if (!prop)
		return 0;

	if (kind == I_CAL_DTSTART_PROPERTY)
		tt = i_cal_property_get_dtstart (prop);
	else if (kind == I_CAL_DUE_PROPERTY)
		tt = i_cal_property_get_due (prop);
	else if (kind == I_CAL_COMPLETED_PROPERTY)
		tt = i_cal_property_get_completed (prop);

	if (tt && !i_cal_time_is_null_time (tt)) {
		param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);

		if (i_cal_time_is_date (tt) || (!i_cal_time_is_utc (tt) && !param)) {
			/* date seule ou heure flottante : heure locale */
			gint hour = 0, minute = 0, second = 0;
			GDateTime *local;

			if (!i_cal_time_is_date (tt))
				i_cal_time_get_time (tt, &hour, &minute, &second);
			else if (date_only)
				*date_only = TRUE;

			local = g_date_time_new_local (i_cal_time_get_year (tt), i_cal_time_get_month (tt),
				i_cal_time_get_day (tt), hour, minute, second);
			if (local) {
				result = g_date_time_to_unix (local);
				g_date_time_unref (local);
			}
		} else if (param) {
			const gchar *tzid = i_cal_parameter_get_tzid (param);
			ICalTimezone *zone = NULL;

			if (!tzid || !e_cal_client_get_timezone_sync (client, tzid, &zone, NULL, NULL) || !zone) {
				zone = tzid ? i_cal_timezone_get_builtin_timezone_from_tzid (tzid) : NULL;
				if (!zone && tzid)
					zone = i_cal_timezone_get_builtin_timezone (tzid);
			}

			result = (gint64) i_cal_time_as_timet_with_zone (tt, zone ? zone : i_cal_timezone_get_utc_timezone ());
		} else {
			result = (gint64) i_cal_time_as_timet (tt);
		}

		g_clear_object (&param);
	}

	g_clear_object (&tt);
	g_object_unref (prop);

	return result;
}

/* Nom de l'état d'une tâche pour le JSON : "needs-action", "in-process",
 * "completed", "cancelled", "waiting" ou "deferred".
 * Paramètres :
 *   icomp : composant VTODO
 * Retourne : chaîne statique. */
static const gchar *
task_status_name (ICalComponent *icomp)
{
	ICalPropertyStatus status = i_cal_component_get_status (icomp);
	const gchar *name = "needs-action";
	gchar *x_status;

	if (status == I_CAL_STATUS_COMPLETED)
		return "completed";
	if (status == I_CAL_STATUS_CANCELLED)
		return "cancelled";
	if (status == I_CAL_STATUS_INPROCESS)
		name = "in-process";

	x_status = e_cal_util_component_dup_x_property (icomp, TASK_X_STATUS);
	if (x_status && g_ascii_strcasecmp (x_status, "WAITING") == 0)
		name = "waiting";
	else if (x_status && g_ascii_strcasecmp (x_status, "DEFERRED") == 0)
		name = "deferred";
	g_free (x_status);

	return name;
}

/* Ajoute au JSON les catégories d'une tâche (propriétés CATEGORIES, chacune
 * pouvant contenir plusieurs valeurs séparées par des virgules).
 * Paramètres :
 *   builder : constructeur JSON, dans un objet
 *   icomp   : composant VTODO */
static void
task_add_categories (JsonBuilder *builder,
		     ICalComponent *icomp)
{
	ICalProperty *prop;

	json_builder_set_member_name (builder, "categories");
	json_builder_begin_array (builder);

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
				json_builder_add_string_value (builder, parts[ii]);
		}
		g_strfreev (parts);
	}

	json_builder_end_array (builder);
}

typedef struct {
	gint64 due;
	gint priority;
	gchar *summary;
	JsonNode *node;
} TaskItem;

/* Libère un élément de tâche temporaire.
 * Paramètres :
 *   data : pointeur vers TaskItem */
static void
task_item_free (gpointer data)
{
	TaskItem *item = data;

	if (!item)
		return;

	g_free (item->summary);
	if (item->node)
		json_node_unref (item->node);
	g_free (item);
}

/* Compare deux tâches : échéance croissante (sans échéance à la fin), puis
 * priorité (haute d'abord, non définie à la fin), puis objet.
 * Paramètres :
 *   a, b : pointeurs vers TaskItem *
 * Retourne : -1, 0 ou 1. */
static gint
compare_tasks (gconstpointer a,
	       gconstpointer b)
{
	const TaskItem *ta = *(const TaskItem **) a;
	const TaskItem *tb = *(const TaskItem **) b;
	gint pa = ta->priority > 0 ? ta->priority : 10;
	gint pb = tb->priority > 0 ? tb->priority : 10;

	if (ta->due != tb->due) {
		if (!ta->due || !tb->due)
			return ta->due ? -1 : 1;
		return ta->due < tb->due ? -1 : 1;
	}

	if (pa != pb)
		return pa < pb ? -1 : 1;

	return g_utf8_collate (ta->summary, tb->summary);
}

/* Décrit une tâche en JSON (voir evo_core_list_tasks).
 * Paramètres :
 *   client : liste de tâches du composant
 *   source : source EDS de la liste de tâches
 *   icomp  : composant VTODO
 * Retourne : élément de tâche nouvellement alloué. */
static TaskItem *
task_item_new (EvoCore *core,
	       ECalClient *client,
	       ESource *source,
	       ICalComponent *icomp)
{
	JsonBuilder *builder = json_builder_new ();
	ICalProperty *prop;
	TaskItem *item = g_new0 (TaskItem, 1);
	const gchar *summary = i_cal_component_get_summary (icomp);
	const gchar *parent_uid;
	ESource *parent = NULL;
	const gchar *account_name = "";
	gchar *account_email = NULL;
	gboolean start_date_only, due_date_only;
	gint64 start, completed;
	gint percent = 0;

	parent_uid = e_source_get_parent (source);
	if (parent_uid && *parent_uid)
		parent = e_source_registry_ref_source (core->registry, parent_uid);
	account_name = parent ? e_source_get_display_name (parent) : e_source_get_display_name (source);
	account_email = parent_uid ? core_account_address (core, parent_uid) : NULL;
	if (!account_email)
		account_email = core_account_address (core, e_source_get_uid (source));

	item->summary = g_strdup ((summary && *summary) ? summary : "(Sans titre)");
	item->due = task_property_to_unix (client, icomp, I_CAL_DUE_PROPERTY, &due_date_only);
	start = task_property_to_unix (client, icomp, I_CAL_DTSTART_PROPERTY, &start_date_only);
	completed = task_property_to_unix (client, icomp, I_CAL_COMPLETED_PROPERTY, NULL);

	prop = i_cal_component_get_first_property (icomp, I_CAL_PRIORITY_PROPERTY);
	if (prop) {
		item->priority = CLAMP (i_cal_property_get_priority (prop), 0, 9);
		g_object_unref (prop);
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		percent = CLAMP (i_cal_property_get_percentcomplete (prop), 0, 100);
		g_object_unref (prop);
	}

	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", i_cal_component_get_uid (icomp));
	builder_add_string (builder, "summary", item->summary);

	prop = i_cal_component_get_first_property (icomp, I_CAL_DESCRIPTION_PROPERTY);
	builder_add_string (builder, "description", prop && i_cal_property_get_description (prop) ? i_cal_property_get_description (prop) : "");
	g_clear_object (&prop);

	json_builder_set_member_name (builder, "start");
	json_builder_add_int_value (builder, start);
	json_builder_set_member_name (builder, "start_date_only");
	json_builder_add_boolean_value (builder, start_date_only);
	json_builder_set_member_name (builder, "due");
	json_builder_add_int_value (builder, item->due);
	json_builder_set_member_name (builder, "due_date_only");
	json_builder_add_boolean_value (builder, due_date_only);
	json_builder_set_member_name (builder, "completed");
	json_builder_add_int_value (builder, completed);
	json_builder_set_member_name (builder, "priority");
	json_builder_add_int_value (builder, item->priority);
	builder_add_string (builder, "status", task_status_name (icomp));
	json_builder_set_member_name (builder, "percent");
	json_builder_add_int_value (builder, percent);
	task_add_categories (builder, icomp);
	builder_add_string (builder, "list_uid", e_source_get_uid (source));
	builder_add_string (builder, "list_name", e_source_get_display_name (source));
	builder_add_string (builder, "account_name", account_name ? account_name : "");
	builder_add_string (builder, "account_email", account_email ? account_email : "");
	builder_add_string (builder, "account_uid", parent_uid ? parent_uid : "");
	json_builder_set_member_name (builder, "readonly");
	json_builder_add_boolean_value (builder, e_client_is_readonly (E_CLIENT (client)));
	json_builder_end_object (builder);

	g_free (account_email);
	g_clear_object (&parent);

	item->node = json_builder_get_root (builder);
	g_object_unref (builder);

	return item;
}

/* Liste les tâches de toutes les listes de tâches activées, ou d'une seule.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = identifiant de la liste de tâches ("" pour toutes)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : tableau JSON des tâches, triées par échéance. */
static gchar *
core_list_tasks_run (EvoCore *core,
		     gchar **args,
		     GError **error)
{
	const gchar *list_uid = args[0];
	GList *sources, *slink;
	GPtrArray *items;
	JsonBuilder *builder;
	gchar *json;
	guint ii;

	items = g_ptr_array_new_with_free_func (task_item_free);
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_TASK_LIST);

	for (slink = sources; slink; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		EClient *client;
		GSList *objects = NULL, *olink;

		if (list_uid && *list_uid && g_strcmp0 (list_uid, e_source_get_uid (source)) != 0)
			continue;

		client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_TASKS, 5, core->cancellable, NULL);
		if (!client)
			continue;

		if (e_cal_client_get_object_list_sync (E_CAL_CLIENT (client), "#t", &objects, core->cancellable, NULL)) {
			for (olink = objects; olink; olink = g_slist_next (olink)) {
				ICalComponent *icomp = olink->data;
				ICalProperty *rid;

				if (!icomp || i_cal_component_isa (icomp) != I_CAL_VTODO_COMPONENT ||
				    !i_cal_component_get_uid (icomp))
					continue;

				/* occurrences détachées des tâches périodiques (non gérées) */
				rid = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY);
				if (rid) {
					g_object_unref (rid);
					continue;
				}

				g_ptr_array_add (items, task_item_new (core, E_CAL_CLIENT (client), source, icomp));
			}
			g_slist_free_full (objects, g_object_unref);
		}

		g_object_unref (client);
	}

	g_list_free_full (sources, g_object_unref);

	g_ptr_array_sort (items, compare_tasks);

	builder = json_builder_new ();
	json_builder_begin_array (builder);
	for (ii = 0; ii < items->len; ii++) {
		TaskItem *item = items->pdata[ii];

		json_builder_add_value (builder, g_steal_pointer (&item->node));
	}
	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_ptr_array_unref (items);

	return json;
}

/* Remplace une date de tâche (DTSTART ou DUE) : date seule (minuit local du
 * jour de unix_time) ou heure UTC ; supprimée si unix_time <= 0.
 * Paramètres :
 *   icomp     : composant VTODO
 *   kind      : I_CAL_DTSTART_PROPERTY ou I_CAL_DUE_PROPERTY
 *   unix_time : secondes Unix, ou 0 pour aucune date
 *   date_only : TRUE pour une date sans heure */
static void
task_set_time (ICalComponent *icomp,
	       ICalPropertyKind kind,
	       gint64 unix_time,
	       gboolean date_only)
{
	ICalTime *tt;

	e_cal_util_component_remove_property_by_kind (icomp, kind, TRUE);
	if (kind == I_CAL_DUE_PROPERTY)
		e_cal_util_component_remove_property_by_kind (icomp, I_CAL_DURATION_PROPERTY, TRUE);

	if (unix_time <= 0)
		return;

	if (date_only) {
		GDateTime *local = g_date_time_new_from_unix_local (unix_time);

		tt = i_cal_time_new_null_time ();
		i_cal_time_set_date (tt, g_date_time_get_year (local), g_date_time_get_month (local),
			g_date_time_get_day_of_month (local));
		i_cal_time_set_is_date (tt, TRUE);
		g_date_time_unref (local);
	} else {
		tt = i_cal_time_new_from_timet_with_zone ((time_t) unix_time, FALSE, i_cal_timezone_get_utc_timezone ());
	}

	i_cal_component_take_property (icomp, kind == I_CAL_DUE_PROPERTY ?
		i_cal_property_new_due (tt) : i_cal_property_new_dtstart (tt));
	g_object_unref (tt);
}

/* Applique à un VTODO les champs présents dans le JSON d'une tâche (voir
 * evo_core_create_task) ; les champs absents restent inchangés.
 * Paramètres :
 *   icomp  : composant VTODO à modifier
 *   object : objet JSON de la tâche */
static void
task_apply_json (ICalComponent *icomp,
		 JsonObject *object)
{
	const gchar *status = NULL;
	gint percent = -1;

	if (json_object_has_member (object, "summary")) {
		const gchar *summary = json_object_get_string_member_with_default (object, "summary", "");

		i_cal_component_set_summary (icomp, (summary && *summary) ? summary : "(Sans titre)");
	}

	if (json_object_has_member (object, "description")) {
		const gchar *description = json_object_get_string_member_with_default (object, "description", "");

		e_cal_util_component_remove_property_by_kind (icomp, I_CAL_DESCRIPTION_PROPERTY, TRUE);
		if (description && *description)
			i_cal_component_take_property (icomp, i_cal_property_new_description (description));
	}

	if (json_object_has_member (object, "start"))
		task_set_time (icomp, I_CAL_DTSTART_PROPERTY,
			json_object_get_int_member_with_default (object, "start", 0),
			json_object_get_boolean_member_with_default (object, "start_date_only", TRUE));

	if (json_object_has_member (object, "due"))
		task_set_time (icomp, I_CAL_DUE_PROPERTY,
			json_object_get_int_member_with_default (object, "due", 0),
			json_object_get_boolean_member_with_default (object, "due_date_only", TRUE));

	if (json_object_has_member (object, "priority")) {
		gint priority = CLAMP (json_object_get_int_member_with_default (object, "priority", 0), 0, 9);

		e_cal_util_component_remove_property_by_kind (icomp, I_CAL_PRIORITY_PROPERTY, TRUE);
		if (priority > 0)
			i_cal_component_take_property (icomp, i_cal_property_new_priority (priority));
	}

	if (json_object_has_member (object, "categories")) {
		JsonNode *node = json_object_get_member (object, "categories");

		e_cal_util_component_remove_property_by_kind (icomp, I_CAL_CATEGORIES_PROPERTY, TRUE);
		if (node && JSON_NODE_HOLDS_ARRAY (node)) {
			JsonArray *array = json_node_get_array (node);
			guint ii;

			/* une propriété CATEGORIES par catégorie */
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

	if (json_object_has_member (object, "percent"))
		percent = CLAMP (json_object_get_int_member_with_default (object, "percent", 0), 0, 100);
	if (json_object_has_member (object, "status"))
		status = json_object_get_string_member_with_default (object, "status", "needs-action");

	/* 100 % = terminée, et une tâche terminée l'est à 100 % */
	if (percent == 100 && !status)
		status = "completed";
	if (g_strcmp0 (status, "completed") == 0) {
		percent = 100;
	} else if (status && percent < 0) {
		/* tâche rouverte : elle n'est plus achevée à 100 % */
		ICalProperty *prop = i_cal_component_get_first_property (icomp, I_CAL_PERCENTCOMPLETE_PROPERTY);

		if (prop && i_cal_property_get_percentcomplete (prop) >= 100)
			percent = 0;
		g_clear_object (&prop);
	}

	if (status) {
		ICalPropertyStatus value = I_CAL_STATUS_NEEDSACTION;
		const gchar *x_status = NULL;

		if (g_strcmp0 (status, "completed") == 0) {
			value = I_CAL_STATUS_COMPLETED;
		} else if (g_strcmp0 (status, "cancelled") == 0) {
			value = I_CAL_STATUS_CANCELLED;
		} else if (g_strcmp0 (status, "in-process") == 0) {
			value = I_CAL_STATUS_INPROCESS;
		} else if (g_strcmp0 (status, "waiting") == 0) {
			value = I_CAL_STATUS_INPROCESS;
			x_status = "WAITING";
		} else if (g_strcmp0 (status, "deferred") == 0) {
			x_status = "DEFERRED";
		}

		i_cal_component_set_status (icomp, value);
		e_cal_util_component_set_x_property (icomp, TASK_X_STATUS, x_status);

		if (value == I_CAL_STATUS_COMPLETED) {
			ICalProperty *prop = i_cal_component_get_first_property (icomp, I_CAL_COMPLETED_PROPERTY);

			/* date d'achèvement : conservée si déjà présente */
			if (prop) {
				g_object_unref (prop);
			} else {
				ICalTime *now = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());

				i_cal_component_take_property (icomp, i_cal_property_new_completed (now));
				g_object_unref (now);
			}
		} else {
			e_cal_util_component_remove_property_by_kind (icomp, I_CAL_COMPLETED_PROPERTY, TRUE);
		}
	}

	if (percent >= 0) {
		e_cal_util_component_remove_property_by_kind (icomp, I_CAL_PERCENTCOMPLETE_PROPERTY, TRUE);
		if (percent > 0)
			i_cal_component_take_property (icomp, i_cal_property_new_percentcomplete (percent));
	}
}

/* Lit l'objet JSON d'une requête de tâche.
 * Paramètres :
 *   json  : texte JSON UTF-8
 *   root  : reçoit le nœud racine (à libérer avec json_node_unref)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : objet JSON (appartenant à root), NULL en cas d'erreur. */
static JsonObject *
task_parse_request (const gchar *json,
		    JsonNode **root,
		    GError **error)
{
	*root = json_from_string (json, error);
	if (!*root)
		return NULL;

	if (!JSON_NODE_HOLDS_OBJECT (*root)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		g_clear_pointer (root, json_node_unref);
		return NULL;
	}

	return json_node_get_object (*root);
}

/* Crée une nouvelle tâche VTODO.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON décrivant la tâche
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"uid": "...", "list_uid": "..."} en cas de succès, NULL sinon. */
static gchar *
core_create_task_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	JsonNode *root = NULL;
	JsonObject *object;
	ECalClient *client = NULL;
	ICalComponent *icomp = NULL;
	ICalTime *now;
	gchar *new_uid = NULL, *created_uid = NULL, *json = NULL;
	JsonBuilder *builder;

	object = task_parse_request (args[0], &root, error);
	if (!object)
		return NULL;

	client = task_list_connect (core, json_object_get_string_member_with_default (object, "list_uid", ""), error);
	if (!client)
		goto exit;

	icomp = i_cal_component_new_vtodo ();
	new_uid = e_util_generate_uid ();
	i_cal_component_set_uid (icomp, new_uid);
	now = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	i_cal_component_set_dtstamp (icomp, now);
	g_object_unref (now);

	i_cal_component_set_summary (icomp, "(Sans titre)");
	task_apply_json (icomp, object);

	if (!e_cal_client_create_object_sync (client, icomp, E_CAL_OPERATION_FLAG_NONE,
		&created_uid, core->cancellable, error))
		goto exit;

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", (created_uid && *created_uid) ? created_uid : new_uid);
	builder_add_string (builder, "list_uid", e_source_get_uid (e_client_get_source (E_CLIENT (client))));
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_free (created_uid);
	g_free (new_uid);
	g_clear_object (&icomp);
	g_clear_object (&client);
	json_node_unref (root);

	return json;
}

/* Modifie une tâche existante : seuls les champs présents dans le JSON sont
 * changés, les autres propriétés du VTODO (alarmes, pièces jointes...) sont
 * conservées.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON {"list_uid", "uid", champs modifiés...}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_modify_task_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	JsonNode *root = NULL;
	JsonObject *object;
	ECalClient *client = NULL;
	ICalComponent *icomp = NULL;
	const gchar *list_uid, *uid;
	gchar *json = NULL;

	object = task_parse_request (args[0], &root, error);
	if (!object)
		return NULL;

	list_uid = json_object_get_string_member_with_default (object, "list_uid", "");
	uid = json_object_get_string_member_with_default (object, "uid", "");
	if (!uid || !*uid) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Tâche non indiquée");
		goto exit;
	}

	client = task_list_connect (core, list_uid, error);
	if (!client)
		goto exit;

	if (!e_cal_client_get_object_sync (client, uid, NULL, &icomp, core->cancellable, error))
		goto exit;

	task_apply_json (icomp, object);

	if (!e_cal_client_modify_object_sync (client, icomp, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE,
		core->cancellable, error))
		goto exit;

	json = g_strdup ("{}");

 exit:
	g_clear_object (&icomp);
	g_clear_object (&client);
	json_node_unref (root);

	return json;
}

/* Supprime une tâche.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = list_uid, [1] = task_uid
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_delete_task_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	ECalClient *client;
	gboolean ok;

	client = task_list_connect (core, args[0], error);
	if (!client)
		return NULL;

	ok = e_cal_client_remove_object_sync (client, args[1], NULL,
		E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, core->cancellable, error);

	g_object_unref (client);

	return ok ? g_strdup ("{}") : NULL;
}

/* Soumet une requête de liste des tâches.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   list_uid  : identifiant de la liste de tâches ("" ou NULL pour toutes)
 *   func      : rappel recevant le tableau JSON des tâches
 *   user_data : données utilisateur transmises au rappel */
void
evo_core_list_tasks (EvoCore *core,
		     const char *list_uid,
		     EvoCoreResultFunc func,
		     void *user_data)
{
	gchar *args[] = { (gchar *) (list_uid ? list_uid : ""), NULL };

	core_submit (core, core_list_tasks_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de création d'une tâche.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   task_json : description JSON de la tâche
 *   func      : rappel recevant {"uid": "...", "list_uid": "..."}
 *   user_data : données utilisateur transmises au rappel */
void
evo_core_create_task (EvoCore *core,
		      const char *task_json,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) task_json, NULL };

	g_return_if_fail (task_json != NULL);

	core_submit (core, core_create_task_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de modification d'une tâche.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   task_json : JSON {"list_uid", "uid", champs modifiés...}
 *   func      : rappel recevant {}
 *   user_data : données utilisateur transmises au rappel */
void
evo_core_modify_task (EvoCore *core,
		      const char *task_json,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) task_json, NULL };

	g_return_if_fail (task_json != NULL);

	core_submit (core, core_modify_task_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de suppression d'une tâche.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   list_uid  : identifiant de la liste de tâches
 *   task_uid  : identifiant de la tâche
 *   func      : rappel recevant {}
 *   user_data : données utilisateur transmises au rappel */
void
evo_core_delete_task (EvoCore *core,
		      const char *list_uid,
		      const char *task_uid,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) (list_uid ? list_uid : ""), (gchar *) task_uid, NULL };

	g_return_if_fail (task_uid != NULL);

	core_submit (core, core_delete_task_run, g_strdupv (args), func, user_data);
}
