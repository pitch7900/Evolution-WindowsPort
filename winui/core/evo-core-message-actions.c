/*
 * evo-core : Actions sur un message : indicateurs, suivi, deplacement, copie, suppression.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Modifie un indicateur ("seen" ou "flagged") sur un message et synchronise le
 * message avec le serveur.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] message_uid,
 *           [3] flag_name ("seen" ou "flagged"), [4] "1" ou "0"
 *   error : emplacement d'erreur GError
 * Retourne : objet JSON {"seen": ..., "flagged": ...}, ou NULL en cas d'erreur. */
static gchar *
core_set_message_flags_run (EvoCore *core,
			    gchar **args,
			    GError **error)
{
	CamelFolder *folder;
	guint32 mask = 0, flags;
	gboolean set_value;
	JsonBuilder *builder;
	gchar *json;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	set_value = g_strcmp0 (args[4], "0") != 0;

	if (g_strcmp0 (args[3], "seen") == 0) {
		mask = CAMEL_MESSAGE_SEEN;
		camel_folder_set_message_flags (folder, args[2], mask, set_value ? mask : 0);
	} else if (g_strcmp0 (args[3], "flagged") == 0) {
		mask = CAMEL_MESSAGE_FLAGGED;
		camel_folder_set_message_flags (folder, args[2], mask, set_value ? mask : 0);
	} else {
		/* Flag utilisateur / étiquette de catégorie (ex. "Rouge", "Travail", "Important") */
		CamelMessageInfo *mi = camel_folder_get_message_info (folder, args[2]);
		if (mi) {
			camel_message_info_set_user_flag (mi, args[3], set_value);
			g_clear_object (&mi);
		}
	}

	/* Synchronise le changement d'indicateur avec le serveur sans bloquer
	 * en cas d'erreur réseau temporaire (le cache local le conserve) */
	camel_folder_synchronize_message_sync (folder, args[2], core->cancellable, NULL);

	flags = camel_folder_get_message_flags (folder, args[2]);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "seen");
	json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_SEEN) != 0);
	json_builder_set_member_name (builder, "flagged");
	json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_FLAGGED) != 0);
	json_builder_set_member_name (builder, "labels");
	json_builder_begin_array (builder);
	{
		CamelMessageInfo *mi = camel_folder_get_message_info (folder, args[2]);
		if (mi) {
			const CamelNamedFlags *user_flags = camel_message_info_get_user_flags (mi);
			guint jj, n_flags = user_flags ? camel_named_flags_get_length (user_flags) : 0;
			for (jj = 0; jj < n_flags; jj++) {
				const gchar *name = camel_named_flags_get (user_flags, jj);
				if (name && *name && *name != '$' && *name != '\\')
					json_builder_add_string_value (builder, name);
			}
			g_clear_object (&mi);
		}
	}
	json_builder_end_array (builder);
	json_builder_end_object (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_object_unref (folder);

	return json;
}

/* Recherche récursivement dans l'arbre CamelFolderInfo le nom complet du
 * dossier de corbeille réel du compte (CAMEL_FOLDER_TYPE_TRASH).
 * Paramètres :
 *   fi : premier nœud de la liste CamelFolderInfo à parcourir
 * Retourne : le chemin complet du dossier corbeille (nouvellement alloué),
 *            ou NULL si aucun dossier corbeille n'est trouvé. */
gchar *
find_trash_folder_name (CamelFolderInfo *fi)
{
	for (; fi; fi = fi->next) {
		if ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_TRASH &&
		    fi->full_name && g_strcmp0 (fi->full_name, CAMEL_VTRASH_NAME) != 0)
			return g_strdup (fi->full_name);

		if (fi->child) {
			gchar *found = find_trash_folder_name (fi->child);
			if (found)
				return found;
		}
	}

	return NULL;
}

/* Recherche un dossier dans l'arbre CamelFolderInfo par son nom complet.
 * Paramètres :
 *   fi        : premier nœud de la liste à parcourir
 *   full_name : nom complet recherché (séparateur « / » de Camel)
 * Retourne : le nœud trouvé (appartenant à l'arbre), ou NULL. */
CamelFolderInfo *
find_folder_info (CamelFolderInfo *fi,
		  const gchar *full_name)
{
	for (; fi; fi = fi->next) {
		CamelFolderInfo *found;

		if (g_strcmp0 (fi->full_name, full_name) == 0)
			return fi;

		found = find_folder_info (fi->child, full_name);
		if (found)
			return found;
	}

	return NULL;
}

/* Type (CAMEL_FOLDER_TYPE_*) of a folder, from the folder tree. */
guint32
find_folder_type (CamelFolderInfo *fi,
		  const gchar *full_name)
{
	for (; fi; fi = fi->next) {
		guint32 type;

		if (g_strcmp0 (fi->full_name, full_name) == 0)
			return fi->flags & CAMEL_FOLDER_TYPE_MASK;

		type = find_folder_type (fi->child, full_name);
		if (type != CAMEL_FOLDER_TYPE_NORMAL)
			return type;
	}

	return CAMEL_FOLDER_TYPE_NORMAL;
}

/* Soumet une requête de modification d'indicateur ("seen" ou "flagged") sur
 * un message.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message
 *   flag_name        : nom de l'indicateur ("seen" ou "flagged")
 *   value            : valeur non nulle pour activer, 0 pour désactiver
 *   func             : rappel recevant l'état JSON des indicateurs
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_set_message_flags (EvoCore *core,
			    const char *account_uid,
			    const char *folder_full_name,
			    const char *message_uid,
			    const char *flag_name,
			    int value,
			    EvoCoreResultFunc func,
			    void *user_data)
{
	gchar *args[] = {
		(gchar *) account_uid,
		(gchar *) folder_full_name,
		(gchar *) message_uid,
		(gchar *) flag_name,
		value ? "1" : "0",
		NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);
	g_return_if_fail (flag_name != NULL);

	core_submit (core, core_set_message_flags_run, g_strdupv (args), func, user_data);
}
