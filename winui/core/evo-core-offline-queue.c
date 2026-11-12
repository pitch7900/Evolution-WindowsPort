/*
 * evo-core : file d'attente des actions hors ligne.
 *
 * Les suppressions, déplacements, copies et changements d'indicateurs de messages
 * demandés hors connexion (ou qui échouent faute de réseau) sont journalisés sur disque
 * (%LOCALAPPDATA%\EvolutionWin\offline-queue.json) puis rejoués dans l'ordre au retour
 * du réseau, avec réconciliation : un message disparu du serveur entre-temps rend
 * l'action sans objet (elle est abandonnée sans erreur), une erreur de réseau
 * interrompt le rejeu sans user les tentatives, un refus définitif est retenté avec
 * un délai croissant puis l'action est conservée à l'état « échec » pour que
 * l'utilisateur la relance ou l'annule.
 *
 * La logique pure (fusion, classement des erreurs, délais) se trouve dans
 * evo-core-offline-queue-util.h (testée par tests/unit/test_offline_queue.c).
 * Les envois hors ligne passent par la Boîte d'envoi locale (evo-core-send.c) ;
 * ils sont listés ici avec les autres actions et peuvent être annulés de la même façon.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"
#include "evo-core-offline-queue-util.h"

/* État du journal : protégé par oq_lock, chargé à la première utilisation. */
static GMutex oq_lock;
static OfflineAction *oq_items = NULL;
static int oq_count = 0;
static gint64 oq_last_id = 0;
static gboolean oq_loaded = FALSE;

/* Rejeu en cours (un seul à la fois) et indicateur propre au fil de rejeu : les
 * exécutants qu'il appelle ne doivent pas remettre l'action en file d'attente. */
static gint oq_replaying = 0;
static GPrivate oq_replay_thread = G_PRIVATE_INIT (NULL);

/* Chemin du journal sur disque (à libérer avec g_free), NULL si LOCALAPPDATA est absent. */
static gchar *
oq_journal_path (void)
{
	const gchar *local_app_data = g_getenv ("LOCALAPPDATA");
	gchar *dir, *path;

	if (!local_app_data)
		return NULL;

	dir = g_build_filename (local_app_data, "EvolutionWin", NULL);
	g_mkdir_with_parents (dir, 0700);
	path = g_build_filename (dir, "offline-queue.json", NULL);
	g_free (dir);

	return path;
}

/* Lit un membre texte d'un objet JSON dans un tampon borné. */
static void
oq_read_string (JsonObject *object,
		const gchar *name,
		gchar *out,
		gsize size)
{
	oq_copy (out, size, json_object_get_string_member_with_default (object, name, ""));
}

/* Enregistre le journal sur disque (verrou oq_lock pris par l'appelant). */
static void
oq_save_locked (void)
{
	JsonBuilder *builder = json_builder_new ();
	gchar *json, *path;
	int ii;

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "last_id");
	json_builder_add_int_value (builder, oq_last_id);
	json_builder_set_member_name (builder, "actions");
	json_builder_begin_array (builder);

	for (ii = 0; ii < oq_count; ii++) {
		const OfflineAction *a = &oq_items[ii];

		json_builder_begin_object (builder);
		builder_add_int (builder, "id", a->id);
		builder_add_string (builder, "kind", oq_kind_name (a->kind));
		builder_add_string (builder, "account", a->account);
		builder_add_string (builder, "folder", a->folder);
		builder_add_string (builder, "uid", a->uid);
		builder_add_string (builder, "dest_account", a->dest_account);
		builder_add_string (builder, "dest_folder", a->dest_folder);
		builder_add_string (builder, "flag", a->flag);
		builder_add_int (builder, "value", a->value);
		builder_add_string (builder, "subject", a->subject);
		builder_add_int (builder, "created", a->created);
		builder_add_int (builder, "attempts", a->attempts);
		builder_add_int (builder, "status", a->status);
		builder_add_int (builder, "next_try", a->next_try);
		builder_add_string (builder, "error", a->error);
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	path = oq_journal_path ();
	if (path) {
		GError *error = NULL;

		if (!g_file_set_contents (path, json, -1, &error)) {
			g_warning ("Journal des actions hors ligne non enregistré : %s", error ? error->message : "?");
			g_clear_error (&error);
		}
		g_free (path);
	}
	g_free (json);
}

/* Charge le journal depuis le disque à la première utilisation (verrou pris par
 * l'appelant). Un fichier absent ou illisible donne une file vide. */
static void
oq_load_locked (void)
{
	gchar *path, *contents = NULL;
	JsonNode *root;
	JsonObject *object;
	JsonArray *actions;
	guint ii;

	if (oq_loaded)
		return;
	oq_loaded = TRUE;
	oq_items = g_new0 (OfflineAction, OQ_MAX_ACTIONS);
	oq_count = 0;

	path = oq_journal_path ();
	if (!path || !g_file_get_contents (path, &contents, NULL, NULL)) {
		g_free (path);
		return;
	}
	g_free (path);

	root = json_from_string (contents, NULL);
	g_free (contents);
	if (!root)
		return;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (object) {
		oq_last_id = json_object_get_int_member_with_default (object, "last_id", 0);
		actions = json_object_has_member (object, "actions") ? json_object_get_array_member (object, "actions") : NULL;

		for (ii = 0; actions && ii < json_array_get_length (actions) && oq_count < OQ_MAX_ACTIONS; ii++) {
			JsonObject *item = json_array_get_object_element (actions, ii);
			OfflineAction *a = &oq_items[oq_count];

			if (!item)
				continue;

			a->kind = oq_kind_from_name (json_object_get_string_member_with_default (item, "kind", ""));
			if (a->kind == OQ_KIND_NONE)
				continue;

			a->id = json_object_get_int_member_with_default (item, "id", 0);
			oq_read_string (item, "account", a->account, sizeof (a->account));
			oq_read_string (item, "folder", a->folder, sizeof (a->folder));
			oq_read_string (item, "uid", a->uid, sizeof (a->uid));
			oq_read_string (item, "dest_account", a->dest_account, sizeof (a->dest_account));
			oq_read_string (item, "dest_folder", a->dest_folder, sizeof (a->dest_folder));
			oq_read_string (item, "flag", a->flag, sizeof (a->flag));
			a->value = (int) json_object_get_int_member_with_default (item, "value", 0);
			oq_read_string (item, "subject", a->subject, sizeof (a->subject));
			a->created = json_object_get_int_member_with_default (item, "created", 0);
			a->attempts = (int) json_object_get_int_member_with_default (item, "attempts", 0);
			a->status = (int) json_object_get_int_member_with_default (item, "status", OQ_STATUS_PENDING);
			a->next_try = json_object_get_int_member_with_default (item, "next_try", 0);
			oq_read_string (item, "error", a->error, sizeof (a->error));
			if (a->id > oq_last_id)
				oq_last_id = a->id;
			oq_count++;
		}
	}

	json_node_unref (root);
}

/* Annonce à l'interface le nombre d'actions en attente (événement « offline-queue »). */
static void
oq_emit_changed (EvoCore *core)
{
	JsonBuilder *builder;
	gint pending, total;

	g_mutex_lock (&oq_lock);
	oq_load_locked ();
	pending = oq_count_pending (oq_items, oq_count);
	total = oq_count;
	g_mutex_unlock (&oq_lock);

	builder = core_event_builder ();
	builder_add_int (builder, "pending", pending);
	builder_add_int (builder, "failed", total - pending);
	core_emit_builder (core, "offline-queue", builder);
}

/* Vrai si le moteur travaille hors connexion et que l'appelant n'est pas le fil de rejeu. */
gboolean
core_oq_is_offline (EvoCore *core)
{
	if (g_private_get (&oq_replay_thread))
		return FALSE;

	return evo_core_get_offline (core) || (core->session && !camel_session_get_online (core->session));
}

/* Indication OQ_HINT_* tirée du code d'une GError (réseau, introuvable, refus). */
static int
oq_error_hint (const GError *error)
{
	if (!error)
		return OQ_HINT_NONE;

	if (error->domain == G_IO_ERROR) {
		switch (error->code) {
		case G_IO_ERROR_NOT_CONNECTED:
		case G_IO_ERROR_TIMED_OUT:
		case G_IO_ERROR_HOST_UNREACHABLE:
		case G_IO_ERROR_NETWORK_UNREACHABLE:
		case G_IO_ERROR_CONNECTION_REFUSED:
		case G_IO_ERROR_HOST_NOT_FOUND:
		case G_IO_ERROR_BUSY:
		case G_IO_ERROR_PENDING:
		case G_IO_ERROR_CANCELLED:
			return OQ_HINT_TRANSIENT;
		case G_IO_ERROR_NOT_FOUND:
			return OQ_HINT_GONE;
		default:
			return OQ_HINT_NONE;
		}
	}

	if (g_error_matches (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE) ||
	    g_error_matches (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED))
		return OQ_HINT_TRANSIENT;

	if (g_error_matches (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_UID))
		return OQ_HINT_GONE;

	return OQ_HINT_NONE;
}

/* Classe l'erreur d'un rejeu (OQ_ERR_*). */
static int
oq_error_class (const GError *error)
{
	return oq_classify_error (oq_error_hint (error), error ? error->message : NULL);
}

/* Objet d'un message d'après le résumé local de son dossier (vide si inconnu). */
static void
oq_fill_subject (EvoCore *core,
		 OfflineAction *action)
{
	CamelFolder *folder = core_ref_folder_cached (core, action->account, action->folder, NULL, NULL);

	if (folder) {
		CamelMessageInfo *info = camel_folder_get_message_info (folder, action->uid);

		if (info) {
			oq_copy (action->subject, sizeof (action->subject), camel_message_info_get_subject (info));
			g_object_unref (info);
		}
		g_object_unref (folder);
	}
}

/* Met une action en file et enregistre le journal. Retourne le résultat OQ_ADD_*. */
static int
oq_enqueue (EvoCore *core,
	    int kind,
	    const gchar *account,
	    const gchar *folder,
	    const gchar *uid,
	    const gchar *dest_account,
	    const gchar *dest_folder,
	    const gchar *flag,
	    int value)
{
	OfflineAction action;
	int result;

	memset (&action, 0, sizeof (action));
	action.kind = kind;
	oq_copy (action.account, sizeof (action.account), account);
	oq_copy (action.folder, sizeof (action.folder), folder);
	oq_copy (action.uid, sizeof (action.uid), uid);
	oq_copy (action.dest_account, sizeof (action.dest_account), dest_account);
	oq_copy (action.dest_folder, sizeof (action.dest_folder), dest_folder);
	oq_copy (action.flag, sizeof (action.flag), flag);
	action.value = value;
	action.created = g_get_real_time () / G_USEC_PER_SEC;
	oq_fill_subject (core, &action);

	g_mutex_lock (&oq_lock);
	oq_load_locked ();
	action.id = oq_next_id (oq_items, oq_count, oq_last_id);
	result = oq_queue_add (oq_items, &oq_count, &action);
	if (result == OQ_ADD_ADDED)
		oq_last_id = action.id;
	if (result != OQ_ADD_DROPPED && result != OQ_ADD_FULL)
		oq_save_locked ();
	g_mutex_unlock (&oq_lock);

	oq_emit_changed (core);

	return result;
}

/* Réponse JSON d'une action mise en file : {"queued":true,"result":"added|merged|dropped|full"}. */
static gchar *
oq_queued_json (int result)
{
	static const gchar *const names[] = { "added", "merged", "dropped", "full" };

	return g_strdup_printf ("{\"queued\":true,\"result\":\"%s\"}", names[result >= 0 && result <= 3 ? result : 2]);
}

/* Vrai si le message est promis à la suppression ou au déplacement : la liste des
 * messages du dossier le masque en attendant le rejeu. */
gboolean
core_oq_is_pending_removal (const gchar *account,
			    const gchar *folder,
			    const gchar *uid)
{
	gboolean pending;

	g_mutex_lock (&oq_lock);
	oq_load_locked ();
	pending = oq_count > 0 && oq_is_message_pending_removal (oq_items, oq_count, account, folder, uid);
	g_mutex_unlock (&oq_lock);

	return pending;
}

/* Nombre d'actions du journal, rapide (sans E/S une fois chargé). */
static int
oq_total (void)
{
	int total;

	g_mutex_lock (&oq_lock);
	oq_load_locked ();
	total = oq_count;
	g_mutex_unlock (&oq_lock);

	return total;
}

/* --- exécutants différables ------------------------------------------------ */

/* Suppression d'un message : mise en file hors connexion ou si le réseau échoue.
 * args : comme core_delete_message_run (). */
gchar *
core_oq_delete_run (EvoCore *core,
		    gchar **args,
		    GError **error)
{
	GError *local_error = NULL;
	gchar *json;

	if (core_oq_is_offline (core))
		return oq_queued_json (oq_enqueue (core, OQ_KIND_DELETE, args[0], args[1], args[2], NULL, NULL, NULL, 0));

	json = core_delete_message_run (core, args, &local_error);
	if (!json && !g_private_get (&oq_replay_thread) && oq_error_class (local_error) == OQ_ERR_TRANSIENT) {
		g_clear_error (&local_error);
		return oq_queued_json (oq_enqueue (core, OQ_KIND_DELETE, args[0], args[1], args[2], NULL, NULL, NULL, 0));
	}

	if (local_error)
		g_propagate_error (error, local_error);

	return json;
}

/* Déplacement ou copie d'un message : mise en file hors connexion ou si le réseau échoue.
 * args : comme core_move_message_run (). */
gchar *
core_oq_move_run (EvoCore *core,
		  gchar **args,
		  GError **error)
{
	GError *local_error = NULL;
	gboolean copy = args[5] && g_str_equal (args[5], "copy");
	int kind = copy ? OQ_KIND_COPY : OQ_KIND_MOVE;
	gchar *json;

	if (core_oq_is_offline (core))
		return oq_queued_json (oq_enqueue (core, kind, args[0], args[1], args[2], args[3], args[4], NULL, 0));

	json = core_move_message_run (core, args, &local_error);
	if (!json && !g_private_get (&oq_replay_thread) && oq_error_class (local_error) == OQ_ERR_TRANSIENT) {
		g_clear_error (&local_error);
		return oq_queued_json (oq_enqueue (core, kind, args[0], args[1], args[2], args[3], args[4], NULL, 0));
	}

	if (local_error)
		g_propagate_error (error, local_error);

	return json;
}

/* Applique localement un indicateur ou une étiquette (résumé du dossier) sans réseau.
 * Retourne l'objet JSON des indicateurs du message, comme core_set_message_flags_run (). */
static gchar *
oq_apply_flag_local (EvoCore *core,
		     gchar **args,
		     GError **error)
{
	CamelFolder *folder = core_ref_folder_cached (core, args[0], args[1], NULL, error);
	gboolean set_value = g_strcmp0 (args[4], "0") != 0;
	JsonBuilder *builder;
	CamelMessageInfo *info;
	gchar *json;
	guint32 mask = 0, flags;

	if (!folder)
		return NULL;

	if (g_strcmp0 (args[3], "seen") == 0)
		mask = CAMEL_MESSAGE_SEEN;
	else if (g_strcmp0 (args[3], "flagged") == 0)
		mask = CAMEL_MESSAGE_FLAGGED;

	if (mask) {
		camel_folder_set_message_flags (folder, args[2], mask, set_value ? mask : 0);
	} else {
		info = camel_folder_get_message_info (folder, args[2]);
		if (info) {
			camel_message_info_set_user_flag (info, args[3], set_value);
			g_object_unref (info);
		}
	}

	flags = camel_folder_get_message_flags (folder, args[2]);
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_boolean (builder, "seen", (flags & CAMEL_MESSAGE_SEEN) != 0);
	builder_add_boolean (builder, "flagged", (flags & CAMEL_MESSAGE_FLAGGED) != 0);
	builder_add_boolean (builder, "queued", TRUE);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	g_object_unref (folder);

	return json;
}

/* Changement d'indicateur : appliqué tout de suite au résumé local ; hors connexion, il est
 * en plus journalisé pour être envoyé au serveur au retour du réseau (le serveur, lui, ne
 * peut pas être joint). args : comme core_set_message_flags_run (). */
gchar *
core_oq_flags_run (EvoCore *core,
		   gchar **args,
		   GError **error)
{
	gchar *json;

	if (!core_oq_is_offline (core))
		return core_set_message_flags_run (core, args, error);

	json = oq_apply_flag_local (core, args, error);
	if (json)
		oq_enqueue (core, OQ_KIND_FLAG, args[0], args[1], args[2], NULL, NULL, args[3], g_strcmp0 (args[4], "0") != 0);

	return json;
}

/* --- rejeu ------------------------------------------------------------------ */

/* Vérifie que le message de l'action existe encore sur le serveur (réconciliation).
 * Le dossier est relevé d'abord pour que les suppressions faites ailleurs entre-temps
 * soient connues. Retourne FALSE si le message a disparu. */
static gboolean
oq_message_still_exists (EvoCore *core,
			 const OfflineAction *action,
			 GError **error)
{
	CamelFolder *folder = core_ref_folder (core, action->account, action->folder, error);
	CamelMessageInfo *info;
	gboolean exists;

	if (!folder)
		return TRUE;	/* dossier injoignable : l'erreur est traitée par l'exécution */

	camel_folder_refresh_info_sync (folder, core->cancellable, NULL);
	info = camel_folder_get_message_info (folder, action->uid);
	exists = info != NULL;
	g_clear_object (&info);
	g_object_unref (folder);

	return exists;
}

/* Exécute une action du journal avec les exécutants habituels. */
static gchar *
oq_execute (EvoCore *core,
	    const OfflineAction *action,
	    GError **error)
{
	gchar *args[7] = { NULL };
	gchar *json;

	args[0] = (gchar *) action->account;
	args[1] = (gchar *) action->folder;
	args[2] = (gchar *) action->uid;

	switch (action->kind) {
	case OQ_KIND_DELETE:
		return core_delete_message_run (core, args, error);
	case OQ_KIND_MOVE:
	case OQ_KIND_COPY:
		args[3] = (gchar *) action->dest_account;
		args[4] = (gchar *) action->dest_folder;
		args[5] = action->kind == OQ_KIND_COPY ? (gchar *) "copy" : NULL;
		return core_move_message_run (core, args, error);
	case OQ_KIND_FLAG:
		args[3] = (gchar *) action->flag;
		args[4] = action->value ? (gchar *) "1" : (gchar *) "0";
		json = core_set_message_flags_run (core, args, error);
		return json;
	default:
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Action hors ligne inconnue");
		return NULL;
	}
}

/* Rejoue la file d'attente.
 *   args : [0] = "force" pour relancer aussi les actions abandonnées et ignorer les délais
 * Retourne : {"status":"done|offline|busy|interrupted","done":n,"gone":n,"failed":n,"remaining":n} */
static gchar *
core_oq_replay_run (EvoCore *core,
		    gchar **args,
		    GError **error)
{
	gboolean force = args[0] && g_str_equal (args[0], "force");
	gint done = 0, gone = 0, failed = 0, waiting = 0;
	const gchar *status = "done";
	OfflineAction *snapshot;
	int snapshot_count, ii, remaining;
	JsonBuilder *builder;
	gchar *json;

	if (evo_core_get_offline (core) || (core->session && !camel_session_get_online (core->session)))
		return g_strdup ("{\"status\":\"offline\"}");

	if (!g_atomic_int_compare_and_exchange (&oq_replaying, 0, 1))
		return g_strdup ("{\"status\":\"busy\"}");

	g_private_set (&oq_replay_thread, GINT_TO_POINTER (1));

	g_mutex_lock (&oq_lock);
	oq_load_locked ();
	snapshot_count = oq_count;
	snapshot = g_new0 (OfflineAction, MAX (1, snapshot_count));
	if (snapshot_count > 0)
		memcpy (snapshot, oq_items, snapshot_count * sizeof (OfflineAction));
	g_mutex_unlock (&oq_lock);

	for (ii = 0; ii < snapshot_count && !g_cancellable_is_cancelled (core->cancellable); ii++) {
		OfflineAction action;
		GError *local_error = NULL;
		gchar *result = NULL;
		gboolean exists = TRUE, removed = FALSE;
		int index, decision, error_class = OQ_ERR_PERMANENT;
		gint64 now = g_get_real_time () / G_USEC_PER_SEC;

		/* action encore présente (elle a pu être annulée) et prête à rejouer ? */
		g_mutex_lock (&oq_lock);
		index = oq_queue_find (oq_items, oq_count, snapshot[ii].id);
		if (index < 0) {
			g_mutex_unlock (&oq_lock);
			continue;
		}
		action = oq_items[index];
		decision = oq_replay_decision (&action, now, force);
		g_mutex_unlock (&oq_lock);

		if (decision != OQ_REPLAY_READY) {
			waiting++;
			continue;
		}

		core_bg_yield (core);

		exists = oq_message_still_exists (core, &action, &local_error);
		g_clear_error (&local_error);

		if (!exists) {
			/* réconciliation : le message n'existe plus sur le serveur, l'action est sans objet */
			gone++;
			removed = TRUE;
		} else {
			result = oq_execute (core, &action, &local_error);
			if (result) {
				done++;
				removed = TRUE;
			} else {
				error_class = oq_error_class (local_error);
				if (error_class == OQ_ERR_GONE) {
					gone++;
					removed = TRUE;
				}
			}
		}

		g_mutex_lock (&oq_lock);
		index = oq_queue_find (oq_items, oq_count, action.id);
		if (index >= 0) {
			if (removed) {
				oq_queue_remove_at (oq_items, &oq_count, index);
			} else {
				oq_record_failure (&oq_items[index], error_class, local_error ? local_error->message : "Erreur inconnue", now);
				if (error_class != OQ_ERR_TRANSIENT)
					failed++;
			}
			oq_save_locked ();
		}
		g_mutex_unlock (&oq_lock);

		g_free (result);

		if (!removed && error_class == OQ_ERR_TRANSIENT) {
			/* le réseau est de nouveau indisponible : inutile d'insister, la suite attend le prochain retour */
			status = "interrupted";
			g_clear_error (&local_error);
			break;
		}
		g_clear_error (&local_error);
	}

	g_free (snapshot);
	g_private_set (&oq_replay_thread, NULL);
	g_atomic_int_set (&oq_replaying, 0);

	g_mutex_lock (&oq_lock);
	remaining = oq_count;
	g_mutex_unlock (&oq_lock);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "status", status);
	builder_add_int (builder, "done", done);
	builder_add_int (builder, "gone", gone);
	builder_add_int (builder, "failed", failed);
	builder_add_int (builder, "waiting", waiting);
	builder_add_int (builder, "remaining", remaining);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	oq_emit_changed (core);

	if (done > 0 || gone > 0 || failed > 0) {
		JsonBuilder *event = core_event_builder ();

		builder_add_int (event, "done", done);
		builder_add_int (event, "gone", gone);
		builder_add_int (event, "failed", failed);
		builder_add_int (event, "remaining", remaining);
		core_emit_builder (core, "offline-replay", event);
	}

	return json;
}

/* Lance le rejeu de la file en tâche de fond s'il y a quelque chose à rejouer.
 * Appelée au retour en ligne et à chaque relève. */
void
core_oq_replay (EvoCore *core)
{
	gchar *args[] = { NULL, NULL };

	if (oq_total () == 0 || g_atomic_int_get (&oq_replaying))
		return;

	core_submit_background (core, core_oq_replay_run, g_strdupv (args), core_ignore_result, NULL);
}

/* --- API publique ---------------------------------------------------------- */

/* Liste des actions en attente : actions du journal puis messages de la Boîte d'envoi.
 * Retourne : {"pending":n,"failed":n,"actions":[{"id","kind","account","folder","uid","subject",
 *   "dest_account","dest_folder","flag","value","created","attempts","status","error"}]}
 * (kind "send" pour un message de la Boîte d'envoi, id "send:<uid>"). */
static gchar *
core_oq_list_run (EvoCore *core,
		  gchar **args,
		  GError **error)
{
	JsonBuilder *builder = json_builder_new ();
	CamelFolder *outbox;
	gint pending, ii;
	gchar *json;

	json_builder_begin_object (builder);

	g_mutex_lock (&oq_lock);
	oq_load_locked ();
	pending = oq_count_pending (oq_items, oq_count);
	builder_add_int (builder, "pending", pending);
	builder_add_int (builder, "failed", oq_count - pending);
	json_builder_set_member_name (builder, "actions");
	json_builder_begin_array (builder);

	for (ii = 0; ii < oq_count; ii++) {
		const OfflineAction *a = &oq_items[ii];
		gchar *id = g_strdup_printf ("%" G_GINT64_FORMAT, a->id);

		json_builder_begin_object (builder);
		builder_add_string (builder, "id", id);
		builder_add_string (builder, "kind", oq_kind_name (a->kind));
		builder_add_string (builder, "account", a->account);
		builder_add_string (builder, "folder", a->folder);
		builder_add_string (builder, "uid", a->uid);
		builder_add_string (builder, "subject", a->subject);
		builder_add_string (builder, "dest_account", a->dest_account);
		builder_add_string (builder, "dest_folder", a->dest_folder);
		builder_add_string (builder, "flag", a->flag);
		builder_add_int (builder, "value", a->value);
		builder_add_int (builder, "created", a->created);
		builder_add_int (builder, "attempts", a->attempts);
		builder_add_int (builder, "status", a->status);
		builder_add_string (builder, "error", a->error);
		json_builder_end_object (builder);
		g_free (id);
	}
	g_mutex_unlock (&oq_lock);

	/* envois en attente dans la Boîte d'envoi locale */
	outbox = core_ref_folder_cached (core, "local", "Outbox", NULL, NULL);
	if (outbox) {
		GPtrArray *uids = camel_folder_dup_uids (outbox);

		for (ii = 0; uids && ii < (gint) uids->len; ii++) {
			const gchar *uid = uids->pdata[ii];
			CamelMessageInfo *info = camel_folder_get_message_info (outbox, uid);
			gchar *id = g_strdup_printf ("send:%s", uid);

			if (info) {
				json_builder_begin_object (builder);
				builder_add_string (builder, "id", id);
				builder_add_string (builder, "kind", "send");
				builder_add_string (builder, "account", "local");
				builder_add_string (builder, "folder", "Outbox");
				builder_add_string (builder, "uid", uid);
				builder_add_string (builder, "subject", camel_message_info_get_subject (info));
				builder_add_string (builder, "dest_account", camel_message_info_get_to (info));
				builder_add_int (builder, "created", camel_message_info_get_date_sent (info));
				json_builder_end_object (builder);
				g_object_unref (info);
			}
			g_free (id);
		}
		if (uids)
			g_ptr_array_unref (uids);
		g_object_unref (outbox);
	}

	json_builder_end_array (builder);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	return json;
}

/* Annule une action en attente.
 *   args : [0] = identifiant (« 12 » pour une action du journal, « send:<uid> » pour un envoi)
 * Un changement d'indicateur est rétabli localement (valeur inverse) ; une suppression ou un
 * déplacement annulé fait réapparaître le message ; un envoi annulé est supprimé de la
 * Boîte d'envoi. Retourne : {"cancelled":true|false}. */
static gchar *
core_oq_cancel_run (EvoCore *core,
		    gchar **args,
		    GError **error)
{
	const gchar *id = args[0];
	OfflineAction action;
	gint64 numeric;
	int index;

	if (g_str_has_prefix (id, "send:")) {
		CamelFolder *outbox = core_ref_folder_cached (core, "local", "Outbox", NULL, error);

		if (!outbox)
			return NULL;

		camel_folder_set_message_flags (outbox, id + 5, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
		camel_folder_synchronize_sync (outbox, TRUE, core->cancellable, NULL);
		g_object_unref (outbox);
		core_emit_folders_updated (core, "local");
		oq_emit_changed (core);
		return g_strdup ("{\"cancelled\":true}");
	}

	numeric = g_ascii_strtoll (id, NULL, 10);

	g_mutex_lock (&oq_lock);
	oq_load_locked ();
	index = oq_queue_find (oq_items, oq_count, numeric);
	if (index < 0) {
		g_mutex_unlock (&oq_lock);
		return g_strdup ("{\"cancelled\":false}");
	}
	action = oq_items[index];
	oq_queue_remove_at (oq_items, &oq_count, index);
	oq_save_locked ();
	g_mutex_unlock (&oq_lock);

	if (action.kind == OQ_KIND_FLAG) {
		gchar *revert[] = { action.account, action.folder, action.uid, action.flag,
			action.value ? (gchar *) "0" : (gchar *) "1", NULL };
		gchar *json = oq_apply_flag_local (core, revert, NULL);

		g_free (json);
	}

	core_emit_folders_updated (core, action.account);
	oq_emit_changed (core);

	return g_strdup ("{\"cancelled\":true}");
}

/* Abandonne toutes les actions du journal (hors Boîte d'envoi). Retourne {"cancelled":n}. */
static gchar *
core_oq_cancel_all_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	gint count;

	g_mutex_lock (&oq_lock);
	oq_load_locked ();
	count = oq_count;
	oq_count = 0;
	oq_save_locked ();
	g_mutex_unlock (&oq_lock);

	oq_emit_changed (core);
	evo_core_refresh (core);

	return g_strdup_printf ("{\"cancelled\":%d}", count);
}

/* Liste les actions hors ligne en attente (voir core_oq_list_run). */
void
evo_core_offline_queue_list (EvoCore *core,
			     EvoCoreResultFunc func,
			     void *user_data)
{
	g_return_if_fail (core != NULL);

	core_submit (core, core_oq_list_run, g_new0 (gchar *, 1), func, user_data);
}

/* Annule une action hors ligne en attente (voir core_oq_cancel_run). */
void
evo_core_offline_queue_cancel (EvoCore *core,
			       const char *action_id,
			       EvoCoreResultFunc func,
			       void *user_data)
{
	gchar *args[] = { (gchar *) action_id, NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (action_id != NULL);

	core_submit (core, core_oq_cancel_run, g_strdupv (args), func, user_data);
}

/* Annule toutes les actions du journal (voir core_oq_cancel_all_run). */
void
evo_core_offline_queue_cancel_all (EvoCore *core,
				   EvoCoreResultFunc func,
				   void *user_data)
{
	g_return_if_fail (core != NULL);

	core_submit (core, core_oq_cancel_all_run, g_new0 (gchar *, 1), func, user_data);
}

/* Rejoue la file maintenant ; force non nul : relance aussi les actions abandonnées. */
void
evo_core_offline_queue_replay (EvoCore *core,
			       int force,
			       EvoCoreResultFunc func,
			       void *user_data)
{
	gchar *args[] = { (gchar *) (force ? "force" : ""), NULL };

	g_return_if_fail (core != NULL);

	core_submit_background (core, core_oq_replay_run, g_strdupv (args), func, user_data);
}
