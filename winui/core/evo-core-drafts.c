/*
 * evo-core : Enregistrement des brouillons.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

gchar *
core_delete_message_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	CamelFolder *folder, *trash = NULL;
	CamelStore *store;
	gchar *trash_name = NULL;
	gboolean trashed = FALSE;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	store = camel_folder_get_parent_store (folder);
	if (store) {
		CamelFolderInfo *fi = camel_store_get_folder_info_sync (store, NULL,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST,
			core->cancellable, NULL);

		if (fi) {
			trash_name = find_trash_folder_name (fi);
			camel_folder_info_free (fi);
		}
	}

	if (trash_name && g_strcmp0 (args[1], trash_name) != 0)
		trash = core_ref_folder (core, args[0], trash_name, NULL);

	if (trash && trash != folder) {
		GPtrArray *uids = g_ptr_array_new ();

		g_ptr_array_add (uids, args[2]);
		camel_folder_set_message_flags (folder, args[2], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

		if (!camel_folder_transfer_messages_to_sync (folder, uids, trash, TRUE, NULL, core->cancellable, error)) {
			g_ptr_array_unref (uids);
			g_object_unref (trash);
			g_free (trash_name);
			g_object_unref (folder);
			return NULL;
		}

		g_ptr_array_unref (uids);
		camel_folder_synchronize_sync (folder, TRUE, core->cancellable, NULL);
		camel_folder_synchronize_sync (trash, FALSE, core->cancellable, NULL);
		trashed = TRUE;
	} else {
		camel_folder_set_message_flags (folder, args[2],
			CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
			CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);

		if (!camel_folder_synchronize_sync (folder, TRUE, core->cancellable, error)) {
			g_clear_object (&trash);
			g_free (trash_name);
			g_object_unref (folder);
			return NULL;
		}
	}

	{
		JsonBuilder *builder = core_event_builder ();

		builder_add_string (builder, "account", args[0]);
		core_emit_builder (core, "folders-updated", builder);
	}

	g_clear_object (&trash);
	g_free (trash_name);
	g_object_unref (folder);

	return g_strdup (trashed ? "{\"trashed\":true}" : "{\"trashed\":false}");
}

/* Soumet une requête de suppression d'un message (vers la corbeille ou
 * définitive).
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message à supprimer
 *   func             : rappel recevant {"trashed": true|false}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_delete_message (EvoCore *core,
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

	core_submit (core, core_oq_delete_run, g_strdupv (args), func, user_data);
}
