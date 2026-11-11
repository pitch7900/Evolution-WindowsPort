/*
 * evo-core : détection des modifications concurrentes (conflits) des événements d'agenda
 * et des contacts.
 *
 * Chaque événement et chaque contact porte un jeton de révision (ETag du serveur CalDAV /
 * CardDAV quand il existe, LAST-MODIFIED / REV, SEQUENCE et empreinte du contenu) renvoyé
 * par evo_core_get_event et evo_core_list_contacts. L'éditeur le renvoie à l'enregistrement
 * ("base_revision") : si la version actuelle de l'élément a changé entre-temps (modification
 * distante reçue par la synchronisation), l'enregistrement n'est pas écrit et le moteur
 * répond {"conflict": true, ...} avec les champs en désaccord ; l'utilisateur choisit alors
 * « Garder la version locale / distante / les deux » ("conflict_resolution").
 * Logique pure : evo-core-conflict-util.h (testée par tests/unit/test_offline_queue.c).
 * Voir evo-core-calendar.c, evo-core-contacts.c et evo-core-private.h.
 */

#include "evo-core-private.h"
#include "evo-core-conflict-util.h"

/* Propriétés ajoutées par les backends EDS pour mémoriser l'ETag du serveur. */
#define CONFLICT_CALDAV_ETAG "X-EVOLUTION-CALDAV-ETAG"
#define CONFLICT_WEBDAV_ETAG "X-EVOLUTION-WEBDAV-ETAG"

/* Valeur d'une propriété X- d'un composant iCalendar (NULL si absente, à libérer). */
static gchar *
conflict_dup_x_property (ICalComponent *icomp,
			 const gchar *name)
{
	ICalProperty *prop;
	gchar *value = NULL;

	for (prop = i_cal_component_get_first_property (icomp, I_CAL_X_PROPERTY); prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_X_PROPERTY)) {
		const gchar *prop_name = i_cal_property_get_x_name (prop);

		if (prop_name && g_ascii_strcasecmp (prop_name, name) == 0) {
			value = g_strdup (i_cal_property_get_x (prop));
			g_object_unref (prop);
			break;
		}
	}

	return value;
}

/* Jeton de révision d'un événement (voir cf_revision_make).
 * Retourne : chaîne nouvellement allouée. */
gchar *
core_conflict_event_revision (ICalComponent *icomp)
{
	gchar rev[CF_REVISION_SIZE];
	gchar *etag = conflict_dup_x_property (icomp, CONFLICT_CALDAV_ETAG);
	gchar *content = i_cal_component_as_ical_string (icomp);
	gchar *modified = NULL;
	ICalProperty *prop = i_cal_component_get_first_property (icomp, I_CAL_LASTMODIFIED_PROPERTY);
	gint sequence = i_cal_component_get_sequence (icomp);

	if (prop) {
		ICalTime *value = i_cal_property_get_lastmodified (prop);

		if (value)
			modified = g_strdup (i_cal_time_as_ical_string (value));
		g_clear_object (&value);
		g_object_unref (prop);
	}

	cf_revision_make (rev, sizeof (rev), etag, modified, sequence, content);
	g_free (etag);
	g_free (modified);
	g_free (content);

	return g_strdup (rev);
}

/* Jeton de révision d'un contact (ETag WebDAV, REV et empreinte de la vCard).
 * Retourne : chaîne nouvellement allouée. */
gchar *
core_conflict_contact_revision (EContact *contact)
{
	gchar rev[CF_REVISION_SIZE];
	EVCardAttribute *etag_attr = e_vcard_get_attribute (E_VCARD (contact), CONFLICT_WEBDAV_ETAG);
	gchar *etag = etag_attr ? e_vcard_attribute_get_value (etag_attr) : NULL;
	const gchar *modified = e_contact_get_const (contact, E_CONTACT_REV);
	gchar *content = e_vcard_to_string (E_VCARD (contact));

	cf_revision_make (rev, sizeof (rev), etag, modified, -1, content);
	g_free (etag);
	g_free (content);

	return g_strdup (rev);
}

/* Retire d'une copie d'événement les propriétés propres au serveur d'origine (ETag, adresse
 * WebDAV...) pour qu'elle soit créée comme un nouvel élément. */
void
core_conflict_strip_server_props (ICalComponent *icomp)
{
	ICalProperty *prop = i_cal_component_get_first_property (icomp, I_CAL_X_PROPERTY);

	while (prop) {
		const gchar *name = i_cal_property_get_x_name (prop);
		ICalProperty *next = i_cal_component_get_next_property (icomp, I_CAL_X_PROPERTY);

		if (name && (g_ascii_strncasecmp (name, "X-EVOLUTION-CALDAV", 18) == 0 ||
		    g_ascii_strncasecmp (name, "X-EVOLUTION-WEBDAV", 18) == 0))
			i_cal_component_remove_property (icomp, prop);
		g_object_unref (prop);
		prop = next;
	}
}

/* Ajoute à la liste des champs en désaccord (name, local, remote) si les valeurs diffèrent. */
static void
conflict_add_field (JsonBuilder *builder,
		    gint *count,
		    const gchar *name,
		    const gchar *local_value,
		    const gchar *remote_value)
{
	CfField local_field, remote_field;
	gint differing[1];

	memset (&local_field, 0, sizeof (local_field));
	memset (&remote_field, 0, sizeof (remote_field));
	g_strlcpy (local_field.name, name, sizeof (local_field.name));
	g_strlcpy (local_field.value, local_value ? local_value : "", sizeof (local_field.value));
	g_strlcpy (remote_field.name, name, sizeof (remote_field.name));
	g_strlcpy (remote_field.value, remote_value ? remote_value : "", sizeof (remote_field.value));

	if (cf_diff_fields (&local_field, 1, &remote_field, 1, differing, 1) == 0)
		return;

	json_builder_begin_object (builder);
	builder_add_string (builder, "name", name);
	builder_add_string (builder, "local", local_value ? local_value : "");
	builder_add_string (builder, "remote", remote_value ? remote_value : "");
	json_builder_end_object (builder);
	(*count)++;
}

/* Champ texte d'un objet JSON local, NULL si absent (champ non modifié par l'éditeur). */
static const gchar *
conflict_local_text (JsonObject *local,
		     const gchar *name)
{
	return json_object_has_member (local, name) ? json_object_get_string_member_with_default (local, name, "") : NULL;
}

/* Réponse JSON d'un conflit d'événement : {"conflict": true, "kind": "event", "revision",
 * "fields": [{"name", "local", "remote"}], "count"}. Seuls les champs que l'éditeur envoie
 * sont comparés. count vaut 0 quand aucun champ n'est en désaccord (pas de vrai conflit).
 * Paramètres :
 *   local    : JSON de la modification demandée
 *   remote   : version actuelle de l'événement
 *   revision : révision actuelle (core_conflict_event_revision)
 *   out_count: reçoit le nombre de champs en désaccord (peut être NULL)
 * Retourne : chaîne nouvellement allouée. */
gchar *
core_conflict_event_json (JsonObject *local,
			  ICalComponent *remote,
			  const gchar *revision,
			  gint *out_count)
{
	JsonBuilder *builder = json_builder_new ();
	ICalProperty *prop;
	ICalTime *time_value;
	gint count = 0;
	gchar *json, *text, *local_text;
	const gchar *value;

	json_builder_begin_object (builder);
	builder_add_boolean (builder, "conflict", TRUE);
	builder_add_string (builder, "kind", "event");
	builder_add_string (builder, "revision", revision);
	json_builder_set_member_name (builder, "fields");
	json_builder_begin_array (builder);

	if ((value = conflict_local_text (local, "summary")) != NULL)
		conflict_add_field (builder, &count, "summary", value, i_cal_component_get_summary (remote));
	if ((value = conflict_local_text (local, "location")) != NULL)
		conflict_add_field (builder, &count, "location", value, i_cal_component_get_location (remote));

	if ((value = conflict_local_text (local, "description")) != NULL) {
		prop = i_cal_component_get_first_property (remote, I_CAL_DESCRIPTION_PROPERTY);
		conflict_add_field (builder, &count, "description", value, prop ? i_cal_property_get_description (prop) : "");
		g_clear_object (&prop);
	}

	if (json_object_has_member (local, "start")) {
		time_value = i_cal_component_get_dtstart (remote);
		text = g_strdup_printf ("%" G_GINT64_FORMAT, time_value && !i_cal_time_is_null_time (time_value) ?
			(gint64) i_cal_time_as_timet (time_value) : (gint64) 0);
		local_text = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) json_object_get_int_member_with_default (local, "start", 0));
		conflict_add_field (builder, &count, "start", local_text, text);
		g_free (local_text);
		g_free (text);
		g_clear_object (&time_value);
	}

	if (json_object_has_member (local, "end")) {
		time_value = i_cal_component_get_dtend (remote);
		text = g_strdup_printf ("%" G_GINT64_FORMAT, time_value && !i_cal_time_is_null_time (time_value) ?
			(gint64) i_cal_time_as_timet (time_value) : (gint64) 0);
		local_text = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) json_object_get_int_member_with_default (local, "end", 0));
		conflict_add_field (builder, &count, "end", local_text, text);
		g_free (local_text);
		g_free (text);
		g_clear_object (&time_value);
	}

	json_builder_end_array (builder);
	builder_add_int (builder, "count", count);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	if (out_count)
		*out_count = count;

	return json;
}

/* Réponse JSON d'un conflit de contact : même forme que core_conflict_event_json (kind "contact"),
 * champs comparés : full_name, email, phone, org, notes. */
gchar *
core_conflict_contact_json (JsonObject *local,
			    EContact *remote,
			    const gchar *revision,
			    gint *out_count)
{
	static const struct {
		const gchar *name;
		EContactField field;
	} fields[] = {
		{ "full_name", E_CONTACT_FULL_NAME },
		{ "email", E_CONTACT_EMAIL_1 },
		{ "phone", E_CONTACT_PHONE_MOBILE },
		{ "org", E_CONTACT_ORG },
		{ "notes", E_CONTACT_NOTE }
	};
	JsonBuilder *builder = json_builder_new ();
	gint count = 0;
	guint ii;
	gchar *json;

	json_builder_begin_object (builder);
	builder_add_boolean (builder, "conflict", TRUE);
	builder_add_string (builder, "kind", "contact");
	builder_add_string (builder, "revision", revision);
	json_builder_set_member_name (builder, "fields");
	json_builder_begin_array (builder);

	for (ii = 0; ii < G_N_ELEMENTS (fields); ii++) {
		const gchar *value = conflict_local_text (local, fields[ii].name);

		if (value)
			conflict_add_field (builder, &count, fields[ii].name, value, e_contact_get_const (remote, fields[ii].field));
	}

	json_builder_end_array (builder);
	builder_add_int (builder, "count", count);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	if (out_count)
		*out_count = count;

	return json;
}
