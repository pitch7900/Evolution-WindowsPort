/*
 * evo-core : Liste des messages d'un dossier.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Construit le tableau JSON des résumés de messages triés du plus récent au
 * plus ancien.
 * Paramètres :
 *   infos     : tableau de CamelMessageInfo * (modifié sur place par le tri)
 *   max_count : nombre maximal d'éléments à sérialiser (0 pour tous)
 * Retourne : chaîne JSON nouvellement allouée. */
static gchar *
json_from_message_infos (GPtrArray *infos,
			 guint max_count)
{
	JsonBuilder *builder;
	gchar *json;
	guint ii;

	g_ptr_array_sort (infos, compare_infos_by_date);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (ii = 0; ii < infos->len && (max_count == 0 || ii < max_count); ii++) {
		CamelMessageInfo *info = infos->pdata[ii];
		guint32 flags = camel_message_info_get_flags (info);
		gint64 date = camel_message_info_get_date_received (info);

		if (date <= 0)
			date = camel_message_info_get_date_sent (info);

		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "uid");
		json_builder_add_string_value (builder, camel_message_info_get_uid (info));
		json_builder_set_member_name (builder, "subject");
		json_builder_add_string_value (builder, camel_message_info_get_subject (info) ? camel_message_info_get_subject (info) : "");
		json_builder_set_member_name (builder, "from");
		json_builder_add_string_value (builder, camel_message_info_get_from (info) ? camel_message_info_get_from (info) : "");
		json_builder_set_member_name (builder, "date");
		json_builder_add_int_value (builder, date);
		json_builder_set_member_name (builder, "seen");
		json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_SEEN) != 0);
		json_builder_set_member_name (builder, "flagged");
		json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_FLAGGED) != 0);
		json_builder_set_member_name (builder, "attachments");
		json_builder_add_boolean_value (builder, (flags & CAMEL_MESSAGE_ATTACHMENTS) != 0);
		/* regroupement par discussion : empreinte du Message-ID et de
		 * ceux de In-Reply-To / References (identifiants 64 bits Camel) */
		{
			const GArray *references = camel_message_info_get_references (info);
			guint jj;
			gchar *text = g_strdup_printf ("%" G_GUINT64_FORMAT, (guint64) camel_message_info_get_message_id (info));

			builder_add_string (builder, "msgid", text);
			g_free (text);
			json_builder_set_member_name (builder, "refs");
			json_builder_begin_array (builder);
			for (jj = 0; references && jj < references->len; jj++) {
				text = g_strdup_printf ("%" G_GUINT64_FORMAT, g_array_index (references, guint64, jj));
				json_builder_add_string_value (builder, text);
				g_free (text);
			}
			json_builder_end_array (builder);
		}
		/* for sorting the list: recipients, size, labels (categories),
		 * follow-up due date */
		json_builder_set_member_name (builder, "to");
		json_builder_add_string_value (builder, camel_message_info_get_to (info) ? camel_message_info_get_to (info) : "");
		json_builder_set_member_name (builder, "size");
		json_builder_add_int_value (builder, camel_message_info_get_size (info));
		json_builder_set_member_name (builder, "labels");
		json_builder_begin_array (builder);
		{
			const CamelNamedFlags *user_flags = camel_message_info_get_user_flags (info);
			guint jj, n_flags = user_flags ? camel_named_flags_get_length (user_flags) : 0;

			for (jj = 0; jj < n_flags; jj++) {
				const gchar *name = camel_named_flags_get (user_flags, jj);

				/* IMAP system keywords are not categories */
				if (name && *name && *name != '$' && *name != '\\')
					json_builder_add_string_value (builder, name);
			}
		}
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "due_by");
		json_builder_add_string_value (builder, camel_message_info_get_user_tag (info, "due-by") ?
			camel_message_info_get_user_tag (info, "due-by") : "");
		builder_add_string (builder, "follow_up", camel_message_info_get_user_tag (info, "follow-up") ?
			camel_message_info_get_user_tag (info, "follow-up") : "");
		builder_add_string (builder, "completed_on", camel_message_info_get_user_tag (info, "completed-on") ?
			camel_message_info_get_user_tag (info, "completed-on") : "");
		json_builder_set_member_name (builder, "preview");
		json_builder_add_string_value (builder, camel_message_info_get_preview (info) ?
			camel_message_info_get_preview (info) : "");
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);

	return json;
}

/* Rafraîchissement asynchrone d'un dossier en arrière-plan (sans bloquer l'interface).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : args[0] = uid du compte, args[1] = nom complet du dossier
 *   error : récepteur d'erreur éventuel
 * Retourne : chaîne JSON vide. */
gchar *
core_folder_background_refresh_run (EvoCore *core,
				    gchar **args,
				    GError **error)
{
	CamelFolder *folder;
	CamelStore *store;

	/* le dossier a pu être ouvert depuis le cache sans connexion (core_ref_folder_cached) :
	 * la connexion se fait ici, en fond, avant la relève du serveur */
	store = core_ref_store (core, args[0], NULL);
	if (store) {
		core_ensure_connected (core, store, NULL);
		g_object_unref (store);
	}

	/* la relève compte dès la demande (connexion comprise) : l'indicateur de l'interface la suit */
	core_folder_refresh_begin (core, args[0], args[1]);

	folder = core_ref_folder (core, args[0], args[1], error);
	if (folder) {
		core_bg_yield (core);
		camel_folder_refresh_info_sync (folder, core->cancellable, NULL);
		g_object_set_data (G_OBJECT (folder), "evo-initial-sync-done", GINT_TO_POINTER (1));
		g_object_unref (folder);
	}

	/* fin de relève, réussie ou non (annoncée quand la dernière relève du dossier se termine) */
	core_folder_refresh_end (core, args[0], args[1]);

	return g_strdup ("{}");
}

/* Liste les messages d'un dossier : s'il existe déjà des résumés en cache local,
 * ils sont immédiatement retournés pour un affichage instantané, et la relève
 * serveur est exécutée en arrière-plan (émettant "folder-changed" si de nouveaux
 * messages sont arrivés).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : args[0] = uid du compte, args[1] = nom complet du dossier, args[2] = max_count
 *   error : récepteur d'erreur en cas d'échec
 * Retourne : tableau JSON des messages du dossier. */
gchar *
core_list_messages_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	CamelFolder *folder;
	GPtrArray *uids, *infos;
	GError *refresh_error = NULL;
	guint max_count, ii;
	gchar *json;
	gboolean has_cache = FALSE;
	/* args[3] = "cache" : lecture du cache local seulement, sans relève du serveur (rechargement
	 * déclenché par la fin d'une relève : pas de nouvelle relève, sinon boucle) */
	gboolean cache_only = args[3] && g_str_equal (args[3], "cache");

	/* cache local d'abord : les derniers messages s'affichent sans attendre la connexion
	 * au serveur, la mise à jour part tout de suite en tâche de fond */
	folder = core_ref_folder_cached (core, args[0], args[1], NULL, error);
	if (!folder)
		return NULL;

	max_count = (guint) g_ascii_strtoull (args[2], NULL, 10);

	/* 0 : taille adaptée au compte, la fenêtre de synchronisation s'il y en a une (Microsoft 365), sinon 500 */
	if (max_count == 0)
		max_count = core_folder_default_list_max (folder);

	/* Vérifie si le résumé local Camel contient déjà des messages */
	uids = camel_folder_dup_uids (folder);
	has_cache = (uids != NULL && uids->len > 0);

	if (!has_cache && cache_only) {
		/* rien en cache et pas de relève demandée : liste vide */
	} else if (!has_cache) {
		/* Premier chargement de ce dossier sans cache : interrogation serveur, donc connexion
		 * d'abord (le dossier a pu être ouvert hors connexion) */
		CamelStore *store = core_ref_store (core, args[0], NULL);

		core_folder_refresh_begin (core, args[0], args[1]);

		if (store) {
			core_ensure_connected (core, store, NULL);
			g_object_unref (store);
		}

		if (!camel_folder_refresh_info_sync (folder, core->cancellable, &refresh_error)) {
			g_warning ("Cannot refresh “%s”: %s", args[1], refresh_error ? refresh_error->message : "?");
			g_clear_error (&refresh_error);
		}
		if (uids)
			g_ptr_array_unref (uids);
		uids = camel_folder_dup_uids (folder);

		/* première relève sans cache : terminée, l'indicateur de synchronisation peut disparaître */
		core_folder_refresh_end (core, args[0], args[1]);
	} else if (!cache_only) {
		/* Cache local présent : on renvoie immédiatement les messages locaux,
		 * et on lance la synchronisation distante en tâche de fond */
		gchar **bg_args = g_new0 (gchar *, 3);
		bg_args[0] = g_strdup (args[0]);
		bg_args[1] = g_strdup (args[1]);
		core_submit_background (core, core_folder_background_refresh_run, bg_args, core_ignore_result, NULL);
	}

	g_object_set_data (G_OBJECT (folder), "evo-initial-sync-done", GINT_TO_POINTER (1));

	infos = g_ptr_array_new_with_free_func (g_object_unref);

	for (ii = 0; uids && ii < uids->len; ii++) {
		CamelMessageInfo *info = camel_folder_get_message_info (folder, uids->pdata[ii]);

		/* un message dont la suppression ou le déplacement attend le retour du réseau est masqué (file hors ligne) */
		if (info && !(camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED) &&
		    !core_oq_is_pending_removal (args[0], args[1], uids->pdata[ii]))
			g_ptr_array_add (infos, info);
		else
			g_clear_object (&info);
	}

	json = json_from_message_infos (infos, max_count);

	g_ptr_array_unref (infos);
	if (uids)
		g_ptr_array_unref (uids);
	g_object_unref (folder);

	return json;
}

/* Vérifie si un résumé de message correspond à tous les termes de recherche
 * (insensible à la casse et aux accents sur l'objet, l'expéditeur et les
 * destinataires).
 * Paramètres :
 *   info  : résumé CamelMessageInfo à inspecter
 *   query : requête utilisateur brute
 *   words : tableau de mots de la requête mis en minuscules (g_utf8_casefold)
 * Retourne : TRUE si le message correspond à la requête, FALSE sinon. */
gboolean
message_info_matches_query (CamelMessageInfo *info,
			    const gchar *query,
			    gchar **words)
{
	const gchar *fields[4];
	gchar *combined, *folded;
	gboolean match = TRUE;
	guint ii;

	fields[0] = camel_message_info_get_subject (info);
	fields[1] = camel_message_info_get_from (info);
	fields[2] = camel_message_info_get_to (info);
	fields[3] = camel_message_info_get_cc (info);

	combined = g_strjoin (" ",
		fields[0] ? fields[0] : "",
		fields[1] ? fields[1] : "",
		fields[2] ? fields[2] : "",
		fields[3] ? fields[3] : "",
		NULL);

	const CamelNamedFlags *user_flags = camel_message_info_get_user_flags (info);
	if (user_flags && camel_named_flags_get_length (user_flags) > 0) {
		GString *cat_buf = g_string_new (combined);
		guint jj, n_flags = camel_named_flags_get_length (user_flags);
		for (jj = 0; jj < n_flags; jj++) {
			const gchar *fname = camel_named_flags_get (user_flags, jj);
			if (fname && *fname && *fname != '$' && *fname != '\\') {
				g_string_append_printf (cat_buf, " cat:%s category:%s %s", fname, fname, fname);
			}
		}
		g_free (combined);
		combined = g_string_free (cat_buf, FALSE);
	}

	/* g_str_match_string gère les équivalences sans accents (ex. "ecole" -> "École") */
	if (g_str_match_string (query, combined, TRUE)) {
		g_free (combined);
		return TRUE;
	}

	/* Recherche de sous-chaînes quelconques insensible à la casse */
	folded = g_utf8_casefold (combined, -1);
	for (ii = 0; words && words[ii]; ii++) {
		if (*words[ii] && !strstr (folded, words[ii])) {
			match = FALSE;
			break;
		}
	}

	g_free (folded);
	g_free (combined);

	return match;
}

void
evo_core_list_messages (EvoCore *core,
			const char *account_uid,
			const char *folder_full_name,
			unsigned int max_count,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar count[16];
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, count, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);

	g_snprintf (count, sizeof (count), "%u", max_count);
	core_submit (core, core_list_messages_run, g_strdupv (args), func, user_data);
}

/* Relit la liste des messages d'un dossier depuis le cache local de Camel, sans
 * relève du serveur (voir evo-core.h).
 * Paramètres :
 *   core, account_uid, folder_full_name, max_count, func, user_data : voir
 *   evo_core_list_messages () */
void
evo_core_reload_messages (EvoCore *core,
			  const char *account_uid,
			  const char *folder_full_name,
			  unsigned int max_count,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar count[16];
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, count, (gchar *) "cache", NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);

	g_snprintf (count, sizeof (count), "%u", max_count);
	core_submit (core, core_list_messages_run, g_strdupv (args), func, user_data);
}
