/*
 * evo-core : Invitations iCalendar (iTIP / iMIP) : detection dans un message,
 * mise a jour de l'agenda de l'utilisateur et envoi de la reponse (METHOD:REPLY).
 * Voir evo-core.h, evo-core-private.h et evo-core-itip.h (logique pure testee).
 */

#include "evo-core-private.h"
#include "evo-core-itip.h"

/** Etiquette Camel memorisant la reponse donnee a une invitation. */
#define INVITATION_TAG "evo-invite-response"

/* Cherche dans un message la partie text/calendar (ou application/ics) d'une
 * invitation et renvoie son texte.
 * Paramètres :
 *   part         : message ou partie à explorer
 *   cancellable  : annulation éventuelle
 *   out_method   : reçoit le paramètre « method » du Content-Type (à libérer), ou NULL
 * Retourne : texte alloué (à libérer avec g_free), ou NULL si le message n'en contient pas. */
static gchar *
find_calendar_part (CamelMimePart *part,
		    GCancellable *cancellable,
		    gchar **out_method)
{
	CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
	CamelContentType *content_type = camel_mime_part_get_content_type (part);

	if (content_type && (camel_content_type_is (content_type, "text", "calendar") ||
	    camel_content_type_is (content_type, "application", "ics"))) {
		const gchar *method = camel_content_type_param (content_type, "method");

		if (out_method)
			*out_method = g_strdup (method);
		return part_get_text (part, cancellable);
	}

	if (content && CAMEL_IS_MULTIPART (content)) {
		guint ii, n = camel_multipart_get_number (CAMEL_MULTIPART (content));

		for (ii = 0; ii < n; ii++) {
			gchar *found = find_calendar_part (camel_multipart_get_part (CAMEL_MULTIPART (content), ii),
				cancellable, out_method);

			if (found)
				return found;
		}
	}

	return NULL;
}

/* Fuseau horaire d'un TZID : VTIMEZONE embarque dans le document, sinon fuseau
 * connu de libical. Retourne une reference (g_object_unref) ou NULL. */
static ICalTimezone *
itip_ref_zone (ICalComponent *vcalendar,
	       const gchar *tzid)
{
	ICalComponent *vtz;
	ICalTimezone *zone = NULL;

	if (!tzid || !*tzid)
		return NULL;

	for (vtz = vcalendar ? i_cal_component_get_first_component (vcalendar, I_CAL_VTIMEZONE_COMPONENT) : NULL;
	     vtz && !zone;
	     vtz = i_cal_component_get_next_component (vcalendar, I_CAL_VTIMEZONE_COMPONENT)) {
		ICalProperty *prop = i_cal_component_get_first_property (vtz, I_CAL_TZID_PROPERTY);

		if (prop && g_strcmp0 (i_cal_property_get_tzid (prop), tzid) == 0) {
			ICalComponent *copy = i_cal_component_clone (vtz);

			zone = i_cal_timezone_new ();
			if (!i_cal_timezone_set_component (zone, copy))
				g_clear_object (&zone);
			g_clear_object (&copy);
		}
		g_clear_object (&prop);
		g_object_unref (vtz);
	}

	if (!zone)
		zone = i_cal_timezone_get_builtin_timezone (tzid);

	return zone;
}

/* Heure Unix d'une date / heure iCalendar brute : en UTC si elle finit par « Z »,
 * dans son fuseau (TZID) sinon, en heure locale pour une journée entière ou une
 * heure flottante. 0 si la valeur est vide ou invalide. */
static gint64
itip_time_to_unix (ICalComponent *vcalendar,
		   const gchar *value,
		   const gchar *tzid)
{
	ICalTime *itime;
	ICalTimezone *zone = NULL;
	gint64 result;

	if (!value || !*value)
		return 0;

	itime = i_cal_time_new_from_string (value);
	if (!itime || i_cal_time_is_null_time (itime)) {
		g_clear_object (&itime);
		return 0;
	}

	if (i_cal_time_is_utc (itime) && !i_cal_time_is_date (itime)) {
		result = (gint64) i_cal_time_as_timet (itime);
	} else {
		if (!i_cal_time_is_date (itime))
			zone = itip_ref_zone (vcalendar, tzid);
		result = (gint64) i_cal_time_as_timet_with_zone (itime, zone ? zone : e_cal_util_get_system_timezone ());
	}

	g_clear_object (&zone);
	g_object_unref (itime);

	return result;
}

/* Premier et dernier instants de l'évenement décrit par un résumé. */
static void
itip_get_times (ICalComponent *vcalendar,
		const EvoItip *itip,
		gint64 *out_start,
		gint64 *out_end)
{
	gint64 start = itip_time_to_unix (vcalendar, itip->dtstart, itip->dtstart_tzid);
	gint64 end = itip_time_to_unix (vcalendar, itip->dtend, itip->dtend_tzid[0] ? itip->dtend_tzid : itip->dtstart_tzid);

	if (end <= start)
		end = start + (itip->dtstart_is_date ? 86400 : 3600);

	*out_start = start;
	*out_end = end;
}

/* Adresse de messagerie de l'identité d'un compte (à libérer), ou NULL. */
static gchar *
itip_identity_address (EvoCore *core,
		       const gchar *account_uid,
		       gchar **out_name)
{
	ESource *identity = core_ref_identity (core, account_uid);
	gchar *address = NULL;

	if (out_name)
		*out_name = NULL;
	if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY)) {
		ESourceMailIdentity *extension = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);

		address = g_strdup (e_source_mail_identity_get_address (extension));
		if (out_name)
			*out_name = g_strdup (e_source_mail_identity_get_name (extension));
	}
	g_clear_object (&identity);

	return address;
}

/* Réponse (accepted / declined / tentative / needs-action) d'une adresse dans un
 * événement de l'agenda, selon son PARTSTAT. */
static const gchar *
itip_component_response (ICalComponent *icomp,
			 const gchar *email)
{
	ICalProperty *prop = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);
	const gchar *response = "needs-action";

	while (prop) {
		const gchar *value = i_cal_property_get_attendee (prop);
		gboolean mine = value && g_ascii_strcasecmp (g_str_has_prefix (value, "mailto:") || g_str_has_prefix (value, "MAILTO:") ? value + 7 : value, email) == 0;

		if (mine) {
			ICalParameter *param = i_cal_property_get_first_parameter (prop, I_CAL_PARTSTAT_PARAMETER);

			if (param) {
				switch (i_cal_parameter_get_partstat (param)) {
				case I_CAL_PARTSTAT_ACCEPTED: response = "accepted"; break;
				case I_CAL_PARTSTAT_DECLINED: response = "declined"; break;
				case I_CAL_PARTSTAT_TENTATIVE: response = "tentative"; break;
				default: break;
				}
				g_object_unref (param);
			}
			g_object_unref (prop);
			break;
		}
		g_object_unref (prop);
		prop = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY);
	}

	return response;
}

/* Échéance d'une recherche : annule l'appel d'agenda en cours. */
static gboolean
itip_budget_expired_cb (gpointer user_data)
{
	g_cancellable_cancel (user_data);

	return G_SOURCE_REMOVE;
}

/* Cherche un événement (par UID) dans l'agenda par défaut puis dans les autres
 * agendas activés. Les erreurs de connexion sont ignorées (recherche au mieux).
 * Paramètres :
 *   core        : moteur
 *   account_uid : compte qui a reçu l'invitation : seuls ses agendas (et l'agenda par défaut)
 *                 sont consultés ; NULL pour consulter tous les agendas activés
 *   uid         : UID de l'événement
 *   out_client  : reçoit le client de l'agenda qui le contient (à libérer), ou NULL
 *   out_icomp   : reçoit l'événement (à libérer), ou NULL
 *   budget_ms   : durée maximale de la recherche (0 : illimitée). Ouvrir un agenda dont le
 *                 service n'est pas encore démarré peut prendre plus d'une minute (mesuré : 58 à
 *                 82 s) : l'affichage d'un message ne doit pas l'attendre. Un minuteur annule
 *                 l'appel d'agenda en cours à l'échéance (un simple contrôle entre deux agendas
 *                 ne suffit pas, un seul appel peut bloquer).
 * Retourne : TRUE si l'événement existe dans un agenda. */
static gboolean
itip_find_event (EvoCore *core,
		 const gchar *account_uid,
		 const gchar *uid,
		 ECalClient **out_client,
		 ICalComponent **out_icomp,
		 gint64 budget_ms)
{
	ESource *default_source = e_source_registry_ref_default_calendar (core->registry);
	GList *sources, *link;
	gboolean found = FALSE;
	gint64 started_at = g_get_monotonic_time ();
	gchar *account_parent = NULL;
	GCancellable *cancellable = core->cancellable;
	GCancellable *budget = NULL;
	GSource *budget_timer = NULL;

	if (budget_ms > 0) {
		/* la source garde sa propre référence : pas d'accès à un objet libéré si elle se déclenche en même temps */
		budget = g_cancellable_new ();
		budget_timer = g_timeout_source_new ((guint) budget_ms);
		g_source_set_callback (budget_timer, itip_budget_expired_cb, g_object_ref (budget), g_object_unref);
		g_source_attach (budget_timer, core->context);
		cancellable = budget;
	}

	if (account_uid && *account_uid) {
		ESource *account_source = e_source_registry_ref_source (core->registry, account_uid);

		if (account_source) {
			account_parent = e_source_dup_parent (account_source);
			g_object_unref (account_source);
		}
	}

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_CALENDAR);
	if (default_source)
		sources = g_list_prepend (sources, g_object_ref (default_source));

	for (link = sources; link && !found; link = g_list_next (link)) {
		ESource *source = link->data;
		EClient *client;
		ICalComponent *icomp = NULL;

		/* l'agenda par défaut est en tête de liste : ne pas le tester deux fois */
		if (link != sources && default_source && g_strcmp0 (e_source_get_uid (source), e_source_get_uid (default_source)) == 0)
			continue;

		/* les agendas d'un autre compte ne contiennent pas l'événement d'une invitation reçue ici */
		if (link != sources || !default_source) {
			gchar *parent = e_source_dup_parent (source);
			gboolean other_account = account_parent && g_strcmp0 (parent, account_parent) != 0;

			g_free (parent);
			if (other_account)
				continue;
		}

		if (budget && g_cancellable_is_cancelled (budget))
			break;

		client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, budget_ms > 0 ? 2 : 5,
			cancellable, NULL);
		if (!client)
			continue;

		if (e_cal_client_get_object_sync (E_CAL_CLIENT (client), uid, NULL, &icomp, cancellable, NULL) && icomp) {
			found = TRUE;
			if (out_icomp)
				*out_icomp = icomp;
			else
				g_object_unref (icomp);
			if (out_client)
				*out_client = g_object_ref (E_CAL_CLIENT (client));
		}
		g_object_unref (client);
	}

	if (budget_timer) {
		g_source_destroy (budget_timer);
		g_source_unref (budget_timer);
	}
	if (budget && g_cancellable_is_cancelled (budget) && !found)
		g_message ("invitation : recherche de l'événement abandonnée après %" G_GINT64_FORMAT " ms",
			(g_get_monotonic_time () - started_at) / 1000);
	g_clear_object (&budget);

	g_list_free_full (sources, g_object_unref);
	g_clear_object (&default_source);
	g_free (account_parent);

	return found;
}

/* Ajoute à un message JSON la description de l'invitation qu'il contient.
 * Paramètres :
 *   core        : moteur
 *   account_uid : compte qui a reçu le message
 *   folder      : dossier du message
 *   message_uid : identifiant du message
 *   message     : message MIME
 *   builder     : constructeur JSON du message (objet ouvert)
 * Émet un membre « invitation » si le message contient une partie iCalendar décrivant
 * un événement : {method, uid, sequence, summary, start, end, all_day, location,
 * recurring, instance, status, organizer{name,email}, attendees[], is_organizer,
 * my_status, already_in_calendar, calendar_checked, calendar_sequence}. */
void
core_invitation_describe (EvoCore *core,
			  const gchar *account_uid,
			  CamelFolder *folder,
			  const gchar *message_uid,
			  CamelMimeMessage *message,
			  JsonBuilder *builder)
{
	EvoItip *itip = g_new0 (EvoItip, 1);
	ICalComponent *vcalendar = NULL, *existing = NULL;
	CamelMessageInfo *info;
	gchar *method_param = NULL, *text, *my_address, *my_name = NULL;
	const gchar *my_status = "needs-action", *tag;
	gint64 start, end;
	gint attendee_index, existing_sequence = 0, ii;
	gboolean in_calendar = FALSE, is_organizer, calendar_checked;

	text = find_calendar_part (CAMEL_MIME_PART (message), core->cancellable, &method_param);
	if (!text || !evo_itip_parse (text, itip)) {
		g_free (text);
		g_free (method_param);
		g_free (itip);
		return;
	}

	if (!itip->method[0] && method_param) {
		evo_itip_copy (itip->method, sizeof itip->method, method_param);
		evo_itip_upper (itip->method);
	}

	vcalendar = i_cal_component_new_from_string (text);
	itip_get_times (vcalendar, itip, &start, &end);

	my_address = itip_identity_address (core, account_uid, &my_name);
	attendee_index = evo_itip_find_attendee (itip, my_address);
	is_organizer = my_address && evo_itip_casecmp (itip->organizer.email, my_address) == 0;
	if (attendee_index >= 0)
		my_status = evo_itip_response_from_partstat (itip->attendees[attendee_index].partstat);

	/* présent dans un agenda ? Ouvrir un agenda Microsoft 365 peut prendre plus d'une minute : la
	 * recherche n'est plus faite ici, pour que le message s'affiche tout de suite. L'interface la
	 * demande ensuite (evo_core_find_invitation_event) et complète le bandeau. */
	calendar_checked = !(g_strcmp0 (itip->method, "REQUEST") == 0 || g_strcmp0 (itip->method, "CANCEL") == 0);

	/* réponse déjà donnée depuis cette application */
	info = camel_folder_get_message_info (folder, message_uid);
	if (info) {
		tag = camel_message_info_get_user_tag (info, INVITATION_TAG);
		if (tag && *tag)
			my_status = evo_itip_response_from_partstat (tag);
		if (tag && g_strcmp0 (tag, "removed") == 0)
			my_status = "declined";
		g_object_unref (info);
	}

	json_builder_set_member_name (builder, "invitation");
	json_builder_begin_object (builder);
	builder_add_string (builder, "method", itip->method);
	builder_add_string (builder, "uid", itip->uid);
	builder_add_int (builder, "sequence", itip->sequence);
	builder_add_string (builder, "summary", itip->summary);
	builder_add_int (builder, "start", start);
	builder_add_int (builder, "end", end);
	builder_add_boolean (builder, "all_day", itip->dtstart_is_date);
	builder_add_string (builder, "location", itip->location);
	builder_add_boolean (builder, "recurring", itip->rrule[0] != '\0');
	builder_add_boolean (builder, "instance", itip->recurrence_id[0] != '\0');
	builder_add_string (builder, "status", itip->status);
	json_builder_set_member_name (builder, "organizer");
	json_builder_begin_object (builder);
	builder_add_string (builder, "name", itip->organizer.name);
	builder_add_string (builder, "email", itip->organizer.email);
	json_builder_end_object (builder);
	json_builder_set_member_name (builder, "attendees");
	json_builder_begin_array (builder);
	for (ii = 0; ii < itip->n_attendees; ii++) {
		json_builder_begin_object (builder);
		builder_add_string (builder, "name", itip->attendees[ii].name);
		builder_add_string (builder, "email", itip->attendees[ii].email);
		builder_add_string (builder, "partstat", evo_itip_response_from_partstat (itip->attendees[ii].partstat));
		builder_add_string (builder, "role", itip->attendees[ii].role);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	builder_add_boolean (builder, "is_organizer", is_organizer);
	builder_add_string (builder, "my_status", my_status);
	builder_add_boolean (builder, "already_in_calendar", in_calendar);
	builder_add_boolean (builder, "calendar_checked", calendar_checked);
	builder_add_int (builder, "calendar_sequence", existing_sequence);
	json_builder_end_object (builder);

	g_clear_object (&existing);
	g_clear_object (&vcalendar);
	g_free (my_address);
	g_free (my_name);
	g_free (method_param);
	g_free (text);
	g_free (itip);
}

/* --- état d'une invitation dans l'agenda (recherche asynchrone) --- */

typedef struct {
	gchar *json;
	gint64 expires_at;	/* horloge monotone, µs */
} ItipCacheEntry;

static GMutex itip_cache_lock;
static GHashTable *itip_cache = NULL;

static void
itip_cache_entry_free (gpointer data)
{
	ItipCacheEntry *entry = data;

	g_free (entry->json);
	g_free (entry);
}

/* Cherche l'événement d'une invitation dans les agendas du compte et renvoie son état.
 * Le résultat est mémorisé (10 min si trouvé, 1 min sinon) : les clics suivants sont instantanés.
 * Paramètres :
 *   core  : moteur
 *   args  : [0] = compte qui a reçu le message, [1] = UID de l'événement
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"found": bool, "my_status": "accepted|declined|tentative|needs-action", "sequence": n}. */
static gchar *
core_find_invitation_event_run (EvoCore *core,
				gchar **args,
				GError **error)
{
	gchar *key = g_strconcat (args[0] ? args[0] : "", "\x1F", args[1] ? args[1] : "", NULL);
	gchar *json = NULL;
	ItipCacheEntry *entry;

	g_mutex_lock (&itip_cache_lock);
	if (!itip_cache)
		itip_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, itip_cache_entry_free);
	entry = g_hash_table_lookup (itip_cache, key);
	if (entry && entry->expires_at > g_get_monotonic_time ())
		json = g_strdup (entry->json);
	g_mutex_unlock (&itip_cache_lock);

	if (!json) {
		ICalComponent *existing = NULL;
		gboolean found = itip_find_event (core, args[0], args[1], NULL, &existing, 60000);
		gchar *my_address = found ? itip_identity_address (core, args[0], NULL) : NULL;
		JsonBuilder *builder = json_builder_new ();

		json_builder_begin_object (builder);
		builder_add_boolean (builder, "found", found);
		builder_add_string (builder, "my_status", found && my_address ? itip_component_response (existing, my_address) : "needs-action");
		builder_add_int (builder, "sequence", found ? i_cal_component_get_sequence (existing) : 0);
		json_builder_end_object (builder);
		json = json_from_builder (builder);

		entry = g_new0 (ItipCacheEntry, 1);
		entry->json = g_strdup (json);
		entry->expires_at = g_get_monotonic_time () + (found ? 600 : 60) * G_USEC_PER_SEC;
		g_mutex_lock (&itip_cache_lock);
		g_hash_table_insert (itip_cache, g_strdup (key), entry);
		g_mutex_unlock (&itip_cache_lock);

		g_object_unref (builder);
		g_clear_object (&existing);
		g_free (my_address);
	}

	g_free (key);

	return json;
}

/* Soumet la recherche de l'état d'une invitation dans l'agenda (voir evo-core.h).
 * Paramètres :
 *   core        : moteur
 *   account_uid : compte qui a reçu le message
 *   event_uid   : UID de l'événement (membre « uid » de l'invitation)
 *   func        : rappel recevant {"found", "my_status", "sequence"}
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_find_invitation_event (EvoCore *core,
				const char *account_uid,
				const char *event_uid,
				EvoCoreResultFunc func,
				void *user_data)
{
	gchar *args[] = { (gchar *) (account_uid ? account_uid : ""), (gchar *) (event_uid ? event_uid : ""), NULL };

	g_return_if_fail (core != NULL);

	/* peut durer longtemps (agenda Microsoft 365 à ouvrir) : tâche de fond */
	core_submit_background (core, core_find_invitation_event_run, g_strdupv (args), func, user_data);
}

/* Fixe le PARTSTAT d'une adresse dans un événement ; ajoute le participant s'il
 * n'y figure pas (invitation transmise, liste de diffusion). */
static void
itip_set_partstat (ICalComponent *icomp,
		   const gchar *email,
		   const gchar *name,
		   ICalParameterPartstat partstat)
{
	ICalProperty *prop = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);

	while (prop) {
		const gchar *value = i_cal_property_get_attendee (prop);

		if (value && g_ascii_strcasecmp (g_ascii_strncasecmp (value, "mailto:", 7) == 0 ? value + 7 : value, email) == 0) {
			ICalParameter *param = i_cal_property_get_first_parameter (prop, I_CAL_PARTSTAT_PARAMETER);

			if (param) {
				i_cal_parameter_set_partstat (param, partstat);
				g_object_unref (param);
			} else {
				i_cal_property_take_parameter (prop, i_cal_parameter_new_partstat (partstat));
			}
			g_object_unref (prop);
			return;
		}
		g_object_unref (prop);
		prop = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY);
	}

	{
		gchar *mailto = g_strconcat ("mailto:", email, NULL);

		prop = i_cal_property_new_attendee (mailto);
		i_cal_property_take_parameter (prop, i_cal_parameter_new_partstat (partstat));
		if (name && *name)
			i_cal_property_take_parameter (prop, i_cal_parameter_new_cn (name));
		i_cal_component_take_property (icomp, prop);
		g_free (mailto);
	}
}

/* Copie les VTIMEZONE du document dans l'agenda pour que les TZID de l'événement
 * soient résolus par le serveur ou le magasin local. */
static void
itip_add_timezones (EvoCore *core,
		    ECalClient *client,
		    ICalComponent *vcalendar)
{
	ICalComponent *vtz;

	for (vtz = i_cal_component_get_first_component (vcalendar, I_CAL_VTIMEZONE_COMPONENT);
	     vtz;
	     vtz = i_cal_component_get_next_component (vcalendar, I_CAL_VTIMEZONE_COMPONENT)) {
		ICalComponent *copy = i_cal_component_clone (vtz);
		ICalTimezone *zone = i_cal_timezone_new ();

		if (i_cal_timezone_set_component (zone, copy))
			e_cal_client_add_timezone_sync (client, zone, core->cancellable, NULL);
		g_object_unref (zone);
		g_object_unref (copy);
		g_object_unref (vtz);
	}
}

/* Met à jour l'agenda de l'utilisateur selon l'invitation et sa réponse.
 * Paramètres :
 *   core       : moteur
 *   itip       : résumé de l'invitation
 *   vcalendar  : document iCalendar complet
 *   partstat   : PARTSTAT choisi, ou NULL pour une annulation
 *   address    : adresse de l'utilisateur
 *   name       : nom de l'utilisateur
 *   out_error  : message d'erreur (à libérer) si la mise à jour a échoué
 * Retourne : action effectuée : "added", "updated", "kept", "removed" ou "unchanged" ;
 *            "skipped" pour une occurrence isolée (non prise en charge). */
static const gchar *
itip_update_calendar (EvoCore *core,
		      const EvoItip *itip,
		      ICalComponent *vcalendar,
		      const gchar *partstat,
		      const gchar *address,
		      const gchar *name,
		      gchar **out_error)
{
	ECalClient *client = NULL;
	ICalComponent *existing = NULL, *event = NULL;
	ESource *source = NULL;
	GError *error = NULL;
	const gchar *result = "unchanged";
	gboolean has_existing;
	EvoItipAction action;
	ICalParameterPartstat ical_partstat = I_CAL_PARTSTAT_ACCEPTED;

	if (itip->recurrence_id[0])
		return "skipped";

	if (partstat && g_strcmp0 (partstat, "DECLINED") == 0)
		ical_partstat = I_CAL_PARTSTAT_DECLINED;
	else if (partstat && g_strcmp0 (partstat, "TENTATIVE") == 0)
		ical_partstat = I_CAL_PARTSTAT_TENTATIVE;

	has_existing = itip_find_event (core, NULL, itip->uid, &client, &existing, 0);
	action = evo_itip_decide (itip->method, has_existing, existing ? i_cal_component_get_sequence (existing) : 0,
		itip->sequence, g_strcmp0 (partstat, "DECLINED") == 0);

	if (action == EVO_ITIP_IGNORE)
		goto exit;

	if (action == EVO_ITIP_REMOVE) {
		if (e_cal_client_remove_object_sync (client, itip->uid, NULL, E_CAL_OBJ_MOD_ALL,
			E_CAL_OPERATION_FLAG_NONE, core->cancellable, &error))
			result = "removed";
		goto exit;
	}

	if (action == EVO_ITIP_KEEP) {
		event = g_object_ref (existing);
	} else {
		ICalComponent *vevent = i_cal_component_get_first_component (vcalendar, I_CAL_VEVENT_COMPONENT);

		if (!vevent) {
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invitation sans événement");
			goto exit;
		}
		event = i_cal_component_clone (vevent);
		g_object_unref (vevent);
	}
	itip_set_partstat (event, address, name, ical_partstat);

	if (action == EVO_ITIP_ADD) {
		source = e_source_registry_ref_default_calendar (core->registry);
		if (!source) {
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Aucun agenda disponible");
			goto exit;
		}
		g_clear_object (&client);
		client = E_CAL_CLIENT (e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, core->cancellable, &error));
		if (!client)
			goto exit;
		itip_add_timezones (core, client, vcalendar);
		if (e_cal_client_create_object_sync (client, event, E_CAL_OPERATION_FLAG_NONE, NULL, core->cancellable, &error))
			result = "added";
	} else {
		if (action == EVO_ITIP_UPDATE)
			itip_add_timezones (core, client, vcalendar);
		if (e_cal_client_modify_object_sync (client, event, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE,
			core->cancellable, &error))
			result = action == EVO_ITIP_KEEP ? "kept" : "updated";
		/* agenda distant qui refuse la modification (objet absent côté serveur) :
		 * repli sur l'agenda local « Sur cet ordinateur » */
		if (error && g_error_matches (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND)) {
			ESource *local = e_source_registry_ref_builtin_calendar (core->registry);

			g_clear_error (&error);
			g_clear_object (&client);
			if (local) {
				client = E_CAL_CLIENT (e_cal_client_connect_sync (local, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5, core->cancellable, &error));
				if (client) {
					itip_add_timezones (core, client, vcalendar);
					if (e_cal_client_create_object_sync (client, event, E_CAL_OPERATION_FLAG_NONE, NULL, core->cancellable, &error))
						result = "added";
				}
				g_object_unref (local);
			}
		}
	}

 exit:
	if (error) {
		*out_error = g_strdup (error->message);
		g_error_free (error);
	}
	g_clear_object (&event);
	g_clear_object (&existing);
	g_clear_object (&client);
	g_clear_object (&source);

	return result;
}

/* Envoie un message par le serveur SMTP d'un compte.
 * Paramètres :
 *   core        : moteur
 *   account_uid : compte expéditeur
 *   message     : message MIME complet
 *   to          : destinataire
 *   error       : erreur éventuelle
 * Retourne : TRUE si le message est parti. */
static gboolean
itip_send_via_account (EvoCore *core,
		       const gchar *account_uid,
		       CamelMimeMessage *message,
		       CamelInternetAddress *from,
		       CamelInternetAddress *to,
		       GError **error)
{
	ESource *identity = core_ref_identity (core, account_uid);
	CamelService *transport = NULL;
	gboolean ok = FALSE, saved = FALSE;

	if (evo_core_get_offline (core) || (core->session && !camel_session_get_online (core->session))) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "Hors connexion : la réponse ne peut pas être envoyée maintenant");
		goto exit;
	}
	if (!identity || !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Ce compte n'a pas d'identité d'expéditeur");
		goto exit;
	}

	transport = camel_session_ref_service (core->session, e_source_mail_submission_get_transport_uid (
		e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)));
	if (!CAMEL_IS_TRANSPORT (transport)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Ce compte n'a pas de serveur sortant (SMTP)");
		goto exit;
	}

	if (camel_service_connect_sync (transport, core->cancellable, error) &&
	    camel_transport_send_to_sync (CAMEL_TRANSPORT (transport), message, CAMEL_ADDRESS (from),
		CAMEL_ADDRESS (to), &saved, core->cancellable, error)) {
		camel_service_disconnect_sync (transport, TRUE, NULL, NULL);
		ok = TRUE;
	} else {
		camel_service_disconnect_sync (transport, FALSE, NULL, NULL);
	}

 exit:
	g_clear_object (&transport);
	g_clear_object (&identity);

	return ok;
}

/* Construit le message de réponse iTIP : multipart/alternative (texte lisible puis
 * text/calendar; method=REPLY). Retourne le message (à libérer), ou NULL. */
static CamelMimeMessage *
itip_build_reply (const EvoItip *itip,
		  const gchar *original_id,
		  const gchar *address,
		  const gchar *name,
		  const gchar *partstat,
		  gint64 start,
		  CamelInternetAddress *from,
		  CamelInternetAddress *to)
{
	CamelMimeMessage *reply = camel_mime_message_new ();
	CamelMultipart *alternative = camel_multipart_new ();
	CamelMimePart *human = camel_mime_part_new ();
	CamelMimePart *calendar = camel_mime_part_new ();
	GDateTime *now = g_date_time_new_now_utc (), *when_time = NULL;
	gchar dtstamp[32], ics[8192], subject[400], body[1600], when[64] = "";
	gchar *stamp = g_date_time_format (now, "%Y%m%dT%H%M%SZ"), *when_text = NULL;

	g_strlcpy (dtstamp, stamp, sizeof dtstamp);
	g_free (stamp);
	g_date_time_unref (now);

	if (evo_itip_format_reply (ics, sizeof ics, itip, address, name, partstat, dtstamp) < 0) {
		g_object_unref (reply);
		g_object_unref (alternative);
		g_object_unref (human);
		g_object_unref (calendar);
		return NULL;
	}

	if (start > 0) {
		when_time = g_date_time_new_from_unix_local (start);
		when_text = g_date_time_format (when_time, itip->dtstart_is_date ? "%d/%m/%Y" : "%d/%m/%Y %H:%M");
		g_strlcpy (when, when_text, sizeof when);
		g_free (when_text);
		g_date_time_unref (when_time);
	}

	evo_itip_reply_subject (subject, sizeof subject, partstat, itip->summary);
	evo_itip_reply_body (body, sizeof body, (name && *name) ? name : address, partstat, itip->summary, when);

	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (alternative), "multipart/alternative");
	camel_multipart_set_boundary (alternative, NULL);

	camel_mime_part_set_content (human, body, strlen (body), "text/plain; charset=utf-8");
	camel_mime_part_set_encoding (human, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
	camel_multipart_add_part (alternative, human);

	camel_mime_part_set_content (calendar, ics, strlen (ics), "text/calendar; charset=utf-8; method=REPLY");
	camel_mime_part_set_encoding (calendar, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
	camel_multipart_add_part (alternative, calendar);

	camel_mime_message_set_subject (reply, subject);
	camel_medium_set_content (CAMEL_MEDIUM (reply), CAMEL_DATA_WRAPPER (alternative));
	camel_mime_message_set_from (reply, from);
	camel_mime_message_set_recipients (reply, CAMEL_RECIPIENT_TYPE_TO, to);
	camel_mime_message_set_date (reply, CAMEL_MESSAGE_DATE_CURRENT, 0);
	camel_mime_message_set_message_id (reply, NULL);
	if (original_id && *original_id) {
		gchar clean[512];

		evo_mdn_sanitize_value (clean, sizeof clean, original_id);
		camel_medium_set_header (CAMEL_MEDIUM (reply), "In-Reply-To", clean);
		camel_medium_set_header (CAMEL_MEDIUM (reply), "References", clean);
	}
	camel_medium_set_header (CAMEL_MEDIUM (reply), "X-Mailer", "Evolution for Windows");

	g_object_unref (human);
	g_object_unref (calendar);
	g_object_unref (alternative);

	return reply;
}

/* Répond à une invitation : met à jour l'agenda, envoie la réponse iTIP et mémorise
 * la réponse sur le message.
 * Paramètres :
 *   args : [0] account_uid, [1] folder_full_name, [2] message_uid,
 *          [3] réponse : "accepted", "declined", "tentative" ou "remove" (annulation),
 *          [4] "1" pour envoyer la réponse à l'organisateur, "0" pour ne pas l'envoyer
 * Retourne : {"response", "partstat", "calendar", "calendar_error"?, "sent", "to"} ou NULL. */
static gchar *
core_respond_invitation_run (EvoCore *core,
			     gchar **args,
			     GError **error)
{
	CamelFolder *folder;
	CamelMimeMessage *message = NULL, *reply = NULL;
	CamelMessageInfo *info;
	CamelInternetAddress *from = NULL, *to = NULL;
	EvoItip *itip = g_new0 (EvoItip, 1);
	ICalComponent *vcalendar = NULL;
	JsonBuilder *builder;
	gchar *text = NULL, *method_param = NULL, *address = NULL, *name = NULL;
	gchar *calendar_error = NULL, *json = NULL;
	const gchar *partstat = evo_itip_partstat_from_response (args[3]), *calendar_action = "unchanged";
	gboolean want_send = g_strcmp0 (args[4], "1") == 0, sent = FALSE, removing = g_strcmp0 (args[3], "remove") == 0;
	gint64 start = 0, end = 0;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder) {
		g_free (itip);
		return NULL;
	}

	message = camel_folder_get_message_sync (folder, args[2], core->cancellable, error);
	if (!message)
		goto exit;

	text = find_calendar_part (CAMEL_MIME_PART (message), core->cancellable, &method_param);
	if (!text || !evo_itip_parse (text, itip)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Ce message ne contient pas d'invitation");
		goto exit;
	}
	if (!itip->method[0] && method_param) {
		evo_itip_copy (itip->method, sizeof itip->method, method_param);
		evo_itip_upper (itip->method);
	}

	if (g_strcmp0 (itip->method, "CANCEL") == 0 ? !removing : !partstat) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Réponse « %s » non valide pour une invitation %s",
			args[3] ? args[3] : "", itip->method);
		goto exit;
	}
	if (g_strcmp0 (itip->method, "CANCEL") != 0 && g_strcmp0 (itip->method, "REQUEST") != 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Méthode iCalendar non prise en charge : %s", itip->method);
		goto exit;
	}

	address = itip_identity_address (core, args[0], &name);
	if (!address || !*address) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Ce compte n'a pas d'adresse d'expéditeur");
		goto exit;
	}

	vcalendar = i_cal_component_new_from_string (text);
	if (!vcalendar) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Document iCalendar illisible");
		goto exit;
	}
	itip_get_times (vcalendar, itip, &start, &end);

	/* 1. agenda de l'utilisateur */
	calendar_action = itip_update_calendar (core, itip, vcalendar, partstat, address, name, &calendar_error);

	/* 2. réponse iTIP à l'organisateur */
	if (want_send && partstat && itip->organizer.email[0] && evo_itip_casecmp (itip->organizer.email, address) != 0) {
		from = camel_internet_address_new ();
		camel_internet_address_add (from, name, address);
		to = camel_internet_address_new ();
		camel_internet_address_add (to, itip->organizer.name[0] ? itip->organizer.name : NULL, itip->organizer.email);

		reply = itip_build_reply (itip, camel_medium_get_header (CAMEL_MEDIUM (message), "Message-ID"),
			address, name, partstat, start, from, to);
		if (!reply) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Réponse iCalendar impossible à construire");
			goto exit;
		}
		if (!itip_send_via_account (core, args[0], reply, from, to, error))
			goto exit;
		sent = TRUE;
	}

	/* 3. mémorise la réponse sur le message */
	info = camel_folder_get_message_info (folder, args[2]);
	if (info) {
		camel_message_info_set_user_tag (info, INVITATION_TAG, removing ? "removed" : evo_itip_response_from_partstat (partstat));
		g_object_unref (info);
	}
	camel_folder_synchronize_message_sync (folder, args[2], core->cancellable, NULL);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "response", args[3]);
	builder_add_string (builder, "partstat", partstat ? partstat : "");
	builder_add_string (builder, "calendar", calendar_action);
	if (calendar_error)
		builder_add_string (builder, "calendar_error", calendar_error);
	builder_add_boolean (builder, "sent", sent);
	builder_add_string (builder, "to", itip->organizer.email);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_clear_object (&reply);
	g_clear_object (&from);
	g_clear_object (&to);
	g_clear_object (&vcalendar);
	g_clear_object (&message);
	g_object_unref (folder);
	g_free (calendar_error);
	g_free (address);
	g_free (name);
	g_free (method_param);
	g_free (text);
	g_free (itip);

	return json;
}

/* Répond à une invitation iCalendar reçue.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : compte qui a reçu le message
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message
 *   response         : "accepted", "declined", "tentative", ou "remove" pour retirer
 *                      de l'agenda une réunion annulée (METHOD:CANCEL)
 *   send_reply       : non nul pour envoyer la réponse à l'organisateur
 *   func             : rappel recevant {"response", "partstat", "calendar", "calendar_error"?, "sent", "to"}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_respond_invitation (EvoCore *core,
			     const char *account_uid,
			     const char *folder_full_name,
			     const char *message_uid,
			     const char *response,
			     int send_reply,
			     EvoCoreResultFunc func,
			     void *user_data)
{
	gchar *args[] = {
		(gchar *) account_uid,
		(gchar *) folder_full_name,
		(gchar *) message_uid,
		(gchar *) response,
		(gchar *) (send_reply ? "1" : "0"),
		NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);
	g_return_if_fail (response != NULL);

	core_submit (core, core_respond_invitation_run, g_strdupv (args), func, user_data);
}

/* Envoie les invitations (iTIP METHOD:REQUEST) d'un événement de l'agenda à ses
 * participants, depuis l'adresse d'un compte de messagerie. L'organisateur est
 * posé s'il manque (adresse du compte), chaque participant reçoit RSVP=TRUE.
 * Paramètres :
 *   args : [0] compte expéditeur, [1] identifiant de l'agenda, [2] uid de l'événement
 * Retourne : {"sent": nombre de destinataires, "to": "a@b.c, d@e.f"} ou NULL. */
static gchar *
core_send_event_invitations_run (EvoCore *core,
				 gchar **args,
				 GError **error)
{
	ESource *source = NULL;
	ECalClient *client = NULL;
	ICalComponent *icomp = NULL, *vcalendar = NULL, *copy = NULL;
	ICalProperty *prop;
	CamelMimeMessage *message = NULL;
	CamelMultipart *alternative = NULL;
	CamelMimePart *human = NULL, *calendar_part = NULL;
	CamelInternetAddress *from = NULL, *to = NULL;
	GString *recipients = g_string_new (NULL);
	gchar *address = NULL, *name = NULL, *json = NULL, *ics = NULL, *organizer_uri = NULL, *subject = NULL, *body = NULL;
	gchar *when = NULL;
	guint count = 0;
	gboolean organizer_missing;

	address = itip_identity_address (core, args[0], &name);
	if (!address || !*address) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"Ce compte n'a pas d'adresse d'expéditeur : choisissez un autre compte pour envoyer les invitations.");
		goto exit;
	}

	source = e_source_registry_ref_source (core->registry, args[1]);
	if (!source) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Agenda introuvable");
		goto exit;
	}
	client = E_CAL_CLIENT (e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 10, core->cancellable, error));
	if (!client)
		goto exit;
	if (!e_cal_client_get_object_sync (client, args[2], NULL, &icomp, core->cancellable, error))
		goto exit;

	/* organisateur : adresse du compte choisi, s'il n'y en a pas déjà un */
	organizer_uri = g_strconcat ("mailto:", address, NULL);
	organizer_missing = !(prop = i_cal_component_get_first_property (icomp, I_CAL_ORGANIZER_PROPERTY));
	if (prop)
		g_object_unref (prop);
	if (organizer_missing) {
		ICalParameter *cn;

		prop = i_cal_property_new_organizer (organizer_uri);
		if (name && *name) {
			cn = i_cal_parameter_new_cn (name);
			i_cal_property_take_parameter (prop, cn);
		}
		i_cal_component_take_property (icomp, prop);
	}

	/* destinataires : tous les participants sauf l'organisateur ; réponse demandée */
	to = camel_internet_address_new ();
	for (prop = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY); prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY)) {
		const gchar *value = i_cal_property_get_attendee (prop);
		const gchar *mail = value && g_ascii_strncasecmp (value, "mailto:", 7) == 0 ? value + 7 : value;
		ICalParameter *rsvp;

		if (!mail || !*mail || g_ascii_strcasecmp (mail, address) == 0)
			continue;
		rsvp = i_cal_property_get_first_parameter (prop, I_CAL_RSVP_PARAMETER);
		if (!rsvp) {
			rsvp = i_cal_parameter_new_rsvp (I_CAL_RSVP_TRUE);
			i_cal_property_take_parameter (prop, rsvp);
		} else {
			g_object_unref (rsvp);
		}
		camel_internet_address_add (to, NULL, mail);
		if (count++)
			g_string_append (recipients, ", ");
		g_string_append (recipients, mail);
	}

	if (count == 0) {
		json = g_strdup ("{\"sent\":0,\"to\":\"\"}");
		goto exit;
	}

	/* mémorise l'organisateur sur l'événement (les réponses reviennent à cette adresse) */
	e_cal_client_modify_object_sync (client, icomp, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE,
			core->cancellable, NULL);

	/* VCALENDAR METHOD:REQUEST contenant l'événement */
	vcalendar = i_cal_component_new_vcalendar ();
	i_cal_component_take_property (vcalendar, i_cal_property_new_prodid ("-//EvolutionWin//Evolution for Windows//FR"));
	i_cal_component_take_property (vcalendar, i_cal_property_new_version ("2.0"));
	i_cal_component_take_property (vcalendar, i_cal_property_new_method (I_CAL_METHOD_REQUEST));
	copy = i_cal_component_clone (icomp);
	i_cal_component_add_component (vcalendar, copy);
	ics = i_cal_component_as_ical_string (vcalendar);

	{
		ICalTime *dtstart = i_cal_component_get_dtstart (icomp);
		const gchar *summary = i_cal_component_get_summary (icomp);

		if (dtstart && !i_cal_time_is_null_time (dtstart)) {
			time_t start = i_cal_time_as_timet_with_zone (dtstart, i_cal_time_get_timezone (dtstart));
			GDateTime *date = g_date_time_new_from_unix_local ((gint64) start);

			when = g_date_time_format (date, i_cal_time_is_date (dtstart) ? "%d/%m/%Y" : "%d/%m/%Y %H:%M");
			g_date_time_unref (date);
		}
		subject = g_strdup_printf ("Invitation : %s", summary && *summary ? summary : "(Sans titre)");
		body = g_strdup_printf ("%s vous invite à : %s%s%s\r\n\r\n"
			"Ouvrez ce message dans un client de messagerie compatible pour accepter, refuser ou répondre « provisoire ».\r\n",
			name && *name ? name : address, summary && *summary ? summary : "(Sans titre)",
			when ? "\r\nQuand : " : "", when ? when : "");
		g_clear_object (&dtstart);
	}

	from = camel_internet_address_new ();
	camel_internet_address_add (from, name, address);

	message = camel_mime_message_new ();
	alternative = camel_multipart_new ();
	human = camel_mime_part_new ();
	calendar_part = camel_mime_part_new ();

	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (alternative), "multipart/alternative");
	camel_multipart_set_boundary (alternative, NULL);
	camel_mime_part_set_content (human, body, strlen (body), "text/plain; charset=utf-8");
	camel_mime_part_set_encoding (human, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
	camel_multipart_add_part (alternative, human);
	camel_mime_part_set_content (calendar_part, ics, strlen (ics), "text/calendar; charset=utf-8; method=REQUEST");
	camel_mime_part_set_encoding (calendar_part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
	camel_multipart_add_part (alternative, calendar_part);

	camel_mime_message_set_subject (message, subject);
	camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (alternative));
	camel_mime_message_set_from (message, from);
	camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to);
	camel_mime_message_set_date (message, CAMEL_MESSAGE_DATE_CURRENT, 0);
	camel_mime_message_set_message_id (message, NULL);
	camel_medium_set_header (CAMEL_MEDIUM (message), "X-Mailer", "Evolution for Windows");

	if (itip_send_via_account (core, args[0], message, from, to, error)) {
		JsonBuilder *builder = json_builder_new ();

		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "sent");
		json_builder_add_int_value (builder, count);
		builder_add_string (builder, "to", recipients->str);
		json_builder_end_object (builder);
		json = json_from_builder (builder);
		g_object_unref (builder);
	}

 exit:
	g_string_free (recipients, TRUE);
	g_free (address);
	g_free (name);
	g_free (organizer_uri);
	g_free (subject);
	g_free (body);
	g_free (when);
	g_free (ics);
	g_clear_object (&message);
	g_clear_object (&alternative);
	g_clear_object (&human);
	g_clear_object (&calendar_part);
	g_clear_object (&from);
	g_clear_object (&to);
	g_clear_object (&vcalendar);
	g_clear_object (&icomp);
	g_clear_object (&client);
	g_clear_object (&source);

	return json;
}

/* Soumet l'envoi des invitations d'un événement à ses participants (voir
 * evo_core_send_event_invitations dans evo-core.h).
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   account_uid  : compte de messagerie dont l'adresse envoie les invitations
 *   calendar_uid : agenda contenant l'événement
 *   event_uid    : identifiant de l'événement
 *   func         : rappel recevant {"sent": nombre, "to": "adresses"}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_send_event_invitations (EvoCore *core,
				 const char *account_uid,
				 const char *calendar_uid,
				 const char *event_uid,
				 EvoCoreResultFunc func,
				 void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) calendar_uid, (gchar *) event_uid, NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (calendar_uid != NULL);
	g_return_if_fail (event_uid != NULL);

	core_submit (core, core_send_event_invitations_run, g_strdupv (args), func, user_data);
}
