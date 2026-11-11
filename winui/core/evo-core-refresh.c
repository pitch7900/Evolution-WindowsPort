/*
 * evo-core : Releve des nouveaux messages, synchronisation periodique et reconnexion.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Full name of the inbox of a store, from its folder tree, to open it like
 * any other folder (core_ref_folder). Not camel_store_get_inbox_folder_sync ():
 * its default implementation (IMAP) opens a second CamelFolder, outside the
 * store's folder cache; on a first run the messages fetched by the server go
 * to the cached instance while the list showed the other one, empty. */
static gchar *
core_dup_inbox_name (EvoCore *core,
		     CamelStore *store)
{
	CamelFolderInfo *fi;
	gchar *name = NULL;

	fi = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, NULL);
	if (fi) {
		name = g_strdup (folder_info_find_inbox (fi));
		camel_folder_info_free (fi);
	}

	return name ? name : g_strdup ("INBOX");
}

/* F9: every folder of the store besides the opened ones (already
 * refreshed), each opened for its refresh only, so that later periodic
 * refreshes stay limited to the inbox and the folders shown */
static void
core_refresh_folder_infos (EvoCore *core,
			   CamelStore *store,
			   CamelFolderInfo *fi,
			   GPtrArray *opened)
{
	for (; fi && !g_cancellable_is_cancelled (core->cancellable); fi = fi->next) {
		gboolean done = (fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)) != 0;
		guint ii;

		for (ii = 0; !done && ii < opened->len; ii++)
			done = g_strcmp0 (camel_folder_get_full_name (opened->pdata[ii]), fi->full_name) == 0;

		if (!done) {
			CamelFolder *folder;
			GError *local_error = NULL;
			gchar *status;

			status = g_strdup_printf ("Relève de « %s » : %s",
				camel_service_get_display_name (CAMEL_SERVICE (store)), fi->display_name);
			core_emit (core, "status", status);
			g_free (status);

			folder = camel_store_get_folder_sync (store, fi->full_name, 0, core->cancellable, &local_error);
			if (folder && camel_folder_refresh_info_sync (folder, core->cancellable, &local_error))
				g_message ("  “%s” refreshed: %d messages", fi->full_name, camel_folder_get_message_count (folder));
			if (local_error) {
				g_warning ("Cannot refresh “%s”: %s", fi->full_name, local_error->message);
				g_clear_error (&local_error);
			}
			g_clear_object (&folder);
		}

		core_refresh_folder_infos (core, store, fi->child, opened);
	}
}

/* repères chronométrés (evo_core_trace) pendant la première actualisation seulement */
#define startup_tracing (core_startup_tracing ())

static gchar *
core_refresh_run (EvoCore *core,
		  gchar **args,
		  GError **error)
{
	GList *services, *link;

	guint total_services = 0, current_service = 0;
	const gchar *pass = (args[0] && g_str_equal (args[0], "all")) ? "passe complète" : "passe rapide";

	services = camel_session_list_services (core->session);

	for (link = services; link; link = g_list_next (link)) {
		if (CAMEL_IS_STORE (link->data) && CAMEL_IS_NETWORK_SERVICE (link->data))
			total_services++;
	}

	core_emit_progress (core, NULL, NULL, "Vérification des nouveaux messages...", 0, TRUE);

	for (link = services; link && !g_cancellable_is_cancelled (core->cancellable); link = g_list_next (link)) {
		CamelService *service = link->data;
		const gchar *uid = camel_service_get_uid (service);
		CamelFolder *inbox;
		GHashTableIter iter;
		GPtrArray *opened;
		gpointer key, value;
		gchar *prefix, *inbox_name;
		GError *local_error = NULL;
		guint ii;

		if (!CAMEL_IS_STORE (service) || !CAMEL_IS_NETWORK_SERVICE (service))
			continue;

		current_service++;
		g_message ("refresh of “%s”", camel_service_get_display_name (service));
		if (startup_tracing) {
			gchar *step = g_strdup_printf ("actualisation (%s) « %s » : début", pass, camel_service_get_display_name (service));

			evo_core_trace (step);
			g_free (step);
		}

		core_emit_progress (core, uid, NULL,
			camel_service_get_display_name (service),
			total_services > 0 ? (gint) ((current_service * 100) / total_services) : -1,
			TRUE);

		if (!core_ensure_connected (core, CAMEL_STORE (service), &local_error)) {
			g_warning ("Cannot connect “%s”: %s", camel_service_get_display_name (service),
				local_error ? local_error->message : "?");
			core_log_m365_identity (core, camel_service_get_display_name (service));
			core_emit_connection_status (core, uid, "imap", "error",
				local_error ? local_error->message : "Échec de connexion au serveur");
			g_clear_error (&local_error);
			continue;
		}

		core_emit_connection_status (core, uid, "imap", "connected", "Connecté");
		if (startup_tracing) {
			gchar *step = g_strdup_printf ("actualisation (%s) « %s » : connexion établie", pass, camel_service_get_display_name (service));

			evo_core_trace (step);
			g_free (step);
		}

		/* the inbox is followed even when not shown, for new mail */
		inbox_name = core_dup_inbox_name (core, CAMEL_STORE (service));
		inbox = core_ref_folder (core, uid, inbox_name, NULL);
		g_clear_object (&inbox);
		g_free (inbox_name);

		/* refreshing emits "changed" on the folders with new messages */
		prefix = g_strconcat (uid, "\x1F", NULL);
		opened = g_ptr_array_new_with_free_func (g_object_unref);

		g_mutex_lock (&core->folders_lock);
		g_hash_table_iter_init (&iter, core->folders);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			if (g_str_has_prefix (key, prefix))
				g_ptr_array_add (opened, g_object_ref (value));
		}
		g_mutex_unlock (&core->folders_lock);

		for (ii = 0; ii < opened->len; ii++) {
			CamelFolder *folder = opened->pdata[ii];

			if (!camel_folder_refresh_info_sync (folder, core->cancellable, &local_error)) {
				g_warning ("Cannot refresh “%s”: %s", camel_folder_get_full_name (folder),
					local_error ? local_error->message : "?");
				g_clear_error (&local_error);
			} else {
				g_message ("  “%s” refreshed: %d messages", camel_folder_get_full_name (folder),
					camel_folder_get_message_count (folder));
			}
			g_object_set_data (G_OBJECT (folder), "evo-initial-sync-done", GINT_TO_POINTER (1));
		}

		/* args[0] = "all" (evo_core_refresh_all): every other folder too */
		if (args[0] && g_str_equal (args[0], "all")) {
			CamelFolderInfo *fi;

			fi = camel_store_get_folder_info_sync (CAMEL_STORE (service), NULL,
				CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, NULL);
			core_refresh_folder_infos (core, CAMEL_STORE (service), fi, opened);
			camel_folder_info_free (fi);
			core_emit (core, "status", "");
		}

		g_ptr_array_unref (opened);
		g_free (prefix);

		if (startup_tracing) {
			gchar *step = g_strdup_printf ("actualisation (%s) « %s » : dossiers ouverts actualisés", pass, camel_service_get_display_name (service));

			evo_core_trace (step);
			g_free (step);
		}

		{
			JsonBuilder *builder = core_event_builder ();

			builder_add_string (builder, "account", uid);
			core_emit_builder (core, "folders-updated", builder);
		}
	}

	core_emit_progress (core, NULL, NULL, "Tous les dossiers sont à jour", 100, FALSE);
	core_emit_connection_status (core, NULL, "global", "connected", "Connecté");

	if (startup_tracing) {
		gchar *step = g_strdup_printf ("actualisation (%s) : terminée pour tous les comptes", pass);

		evo_core_trace (step);
		g_free (step);
	}

	g_list_free_full (services, g_object_unref);

	return g_strdup ("{}");
}

gboolean
core_refresh_timeout_cb (gpointer user_data)
{
	evo_core_refresh (user_data);

	return G_SOURCE_CONTINUE;
}

/* --- sending --- */

/* "folder://<store uid>/<escaped folder path>" -> store uid, folder name */
gboolean
parse_folder_uri (const gchar *uri,
		  gchar **out_store_uid,
		  gchar **out_folder_name)
{
	const gchar *rest, *slash;

	if (!uri || !g_str_has_prefix (uri, "folder://"))
		return FALSE;

	rest = uri + strlen ("folder://");
	slash = strchr (rest, '/');
	if (!slash || slash == rest || !slash[1])
		return FALSE;

	*out_store_uid = g_strndup (rest, slash - rest);
	*out_folder_name = g_uri_unescape_string (slash + 1, NULL);

	return *out_folder_name != NULL;
}

void
evo_core_refresh (EvoCore *core)
{
	core_process_outbox (core);
	core_submit (core, core_refresh_run, g_new0 (gchar *, 1), core_ignore_result, NULL);
}

void
evo_core_refresh_all (EvoCore *core)
{
	gchar *args[] = { (gchar *) "all", NULL };

	core_process_outbox (core);
	core_submit (core, core_refresh_run, g_strdupv (args), core_ignore_result, NULL);
}

/* Relance la connexion / l'authentification pour un compte spécifique.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant unique du compte à reconnecter */
void
evo_core_reconnect_account (EvoCore *core,
			    const char *account_uid)
{
	CamelService *service;

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL);

	service = camel_session_ref_service (core->session, account_uid);
	if (service) {
		if (CAMEL_IS_OFFLINE_STORE (service))
			camel_offline_store_set_online_sync (CAMEL_OFFLINE_STORE (service), TRUE, core->cancellable, NULL);
		g_object_unref (service);
	}

	evo_core_refresh (core);
}
