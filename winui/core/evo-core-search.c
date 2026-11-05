/*
 * evo-core : Recherche de messages (dossier, sous-dossiers, compte, tous les comptes).
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

typedef struct _SearchTargetFolder {
	gchar *account_uid;
	gchar *account_name;
	gchar *folder_full_name;
	gchar *folder_display_name;
} SearchTargetFolder;

static void
search_target_folder_free (SearchTargetFolder *target)
{
	if (!target)
		return;
	g_free (target->account_uid);
	g_free (target->account_name);
	g_free (target->folder_full_name);
	g_free (target->folder_display_name);
	g_slice_free (SearchTargetFolder, target);
}

typedef struct _ScopedSearchMatch {
	CamelMessageInfo *info;
	gchar *account_uid;
	gchar *account_name;
	gchar *folder_full_name;
	gchar *folder_display_name;
} ScopedSearchMatch;

static void
scoped_search_match_free (ScopedSearchMatch *match)
{
	if (!match)
		return;
	g_clear_object (&match->info);
	g_free (match->account_uid);
	g_free (match->account_name);
	g_free (match->folder_full_name);
	g_free (match->folder_display_name);
	g_slice_free (ScopedSearchMatch, match);
}

static gint
compare_scoped_matches_by_date (gconstpointer a,
				gconstpointer b)
{
	ScopedSearchMatch *ma = *(ScopedSearchMatch **) a;
	ScopedSearchMatch *mb = *(ScopedSearchMatch **) b;
	gint64 da = camel_message_info_get_date_received (ma->info);
	gint64 db = camel_message_info_get_date_received (mb->info);

	if (da <= 0)
		da = camel_message_info_get_date_sent (ma->info);
	if (db <= 0)
		db = camel_message_info_get_date_sent (mb->info);

	if (da < db)
		return 1;
	if (da > db)
		return -1;
	return 0;
}

static void
collect_folder_targets_recursive (CamelFolderInfo *fi,
				  const gchar *account_uid,
				  const gchar *account_name,
				  GPtrArray *targets)
{
	for (; fi; fi = fi->next) {
		if (!(fi->flags & CAMEL_FOLDER_NOSELECT) && fi->full_name && *fi->full_name) {
			SearchTargetFolder *t = g_slice_new0 (SearchTargetFolder);
			t->account_uid = g_strdup (account_uid);
			t->account_name = g_strdup (account_name);
			t->folder_full_name = g_strdup (fi->full_name);
			t->folder_display_name = g_strdup (fi->display_name ? fi->display_name : fi->full_name);
			g_ptr_array_add (targets, t);
		}
		if (fi->child)
			collect_folder_targets_recursive (fi->child, account_uid, account_name, targets);
	}
}

static CamelFolderInfo *
find_folder_info_recursive (CamelFolderInfo *fi,
			    const gchar *full_name)
{
	for (; fi; fi = fi->next) {
		if (fi->full_name && g_strcmp0 (fi->full_name, full_name) == 0)
			return fi;
		if (fi->child) {
			CamelFolderInfo *found = find_folder_info_recursive (fi->child, full_name);
			if (found)
				return found;
		}
	}
	return NULL;
}

static gchar *
core_account_display_name (EvoCore *core,
			   const gchar *account_uid)
{
	CamelService *service;
	gchar *name = NULL;

	if (!account_uid || !*account_uid)
		return g_strdup ("");

	service = camel_session_ref_service (core->session, account_uid);
	if (service) {
		const gchar *disp = camel_service_get_display_name (service);
		name = g_strdup (disp ? disp : account_uid);
		g_object_unref (service);
	} else {
		name = g_strdup (account_uid);
	}
	return name;
}

/* Exécute une recherche de messages selon une portée définie :
 *   - "folder"     : dossier courant uniquement
 *   - "subfolders" : dossier courant et tous ses sous-dossiers récursivement
 *   - "account"    : ensemble des dossiers du compte courant
 *   - "all"        : ensemble des dossiers de tous les comptes configurés
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] query, [3] max_count, [4] scope
 *   error : emplacement d'erreur GError
 * Retourne : tableau JSON des messages correspondants enrichis de leur localisation, ou NULL. */
static gchar *
core_search_messages_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	const gchar *account_uid = args[0];
	const gchar *folder_full_name = args[1];
	const gchar *query = args[2];
	const gchar *scope = (args[3] && args[4] && *args[4]) ? args[4] : "folder";
	guint max_count = (guint) g_ascii_strtoull (args[3], NULL, 10);
	GPtrArray *targets, *matches;
	gchar **raw_words = NULL, **words = NULL;
	GString *sexp = NULL;
	guint ii, n_words = 0;
	JsonBuilder *builder;
	gchar *json;

	if (!query || !*query) {
		if (g_strcmp0 (scope, "folder") == 0) {
			gchar *list_args[] = { args[0], args[1], args[3], NULL };
			return core_list_messages_run (core, list_args, error);
		}
		return g_strdup ("[]");
	}

	targets = g_ptr_array_new_with_free_func ((GDestroyNotify) search_target_folder_free);

	if (g_strcmp0 (scope, "all") == 0) {
		GList *services = camel_session_list_services (core->session);
		for (GList *link = services; link; link = g_list_next (link)) {
			if (CAMEL_IS_STORE (link->data)) {
				CamelService *srv = CAMEL_SERVICE (link->data);
				CamelStore *st = CAMEL_STORE (link->data);
				const gchar *s_uid = camel_service_get_uid (srv);
				const gchar *s_name = camel_service_get_display_name (srv);
				if (!s_name)
					s_name = s_uid;
				if (core_ensure_connected (core, st, NULL)) {
					CamelFolderInfo *fi = camel_store_get_folder_info_sync (st, NULL,
						CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, NULL);
					if (fi) {
						collect_folder_targets_recursive (fi, s_uid, s_name, targets);
						camel_folder_info_free (fi);
					}
				}
			}
		}
		g_list_free (services);
	} else if (g_strcmp0 (scope, "account") == 0) {
		CamelStore *store = core_ref_store (core, account_uid, NULL);
		if (store) {
			CamelService *srv = CAMEL_SERVICE (store);
			const gchar *s_name = camel_service_get_display_name (srv);
			if (!s_name)
				s_name = account_uid;
			if (core_ensure_connected (core, store, NULL)) {
				CamelFolderInfo *fi = camel_store_get_folder_info_sync (store, NULL,
					CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, NULL);
				if (fi) {
					collect_folder_targets_recursive (fi, account_uid, s_name, targets);
					camel_folder_info_free (fi);
				}
			}
			g_object_unref (store);
		}
	} else if (g_strcmp0 (scope, "subfolders") == 0) {
		CamelStore *store = core_ref_store (core, account_uid, NULL);
		if (store) {
			CamelService *srv = CAMEL_SERVICE (store);
			const gchar *s_name = camel_service_get_display_name (srv);
			if (!s_name)
				s_name = account_uid;
			if (core_ensure_connected (core, store, NULL)) {
				CamelFolderInfo *root_fi = camel_store_get_folder_info_sync (store, NULL,
					CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, NULL);
				if (root_fi) {
					CamelFolderInfo *found = find_folder_info_recursive (root_fi, folder_full_name);
					if (found) {
						if (!(found->flags & CAMEL_FOLDER_NOSELECT) && found->full_name && *found->full_name) {
							SearchTargetFolder *t = g_slice_new0 (SearchTargetFolder);
							t->account_uid = g_strdup (account_uid);
							t->account_name = g_strdup (s_name);
							t->folder_full_name = g_strdup (found->full_name);
							t->folder_display_name = g_strdup (found->display_name ? found->display_name : found->full_name);
							g_ptr_array_add (targets, t);
						}
						if (found->child)
							collect_folder_targets_recursive (found->child, account_uid, s_name, targets);
					}
					camel_folder_info_free (root_fi);
				}
			}
			g_object_unref (store);
		}
		if (targets->len == 0 && folder_full_name && *folder_full_name) {
			SearchTargetFolder *t = g_slice_new0 (SearchTargetFolder);
			t->account_uid = g_strdup (account_uid);
			t->account_name = g_strdup (account_uid);
			t->folder_full_name = g_strdup (folder_full_name);
			t->folder_display_name = g_strdup (folder_full_name);
			g_ptr_array_add (targets, t);
		}
	} else {
		/* Dossier courant par défaut */
		SearchTargetFolder *t = g_slice_new0 (SearchTargetFolder);
		t->account_uid = g_strdup (account_uid);
		t->account_name = core_account_display_name (core, account_uid);
		t->folder_full_name = g_strdup (folder_full_name);
		t->folder_display_name = g_strdup (folder_full_name);
		g_ptr_array_add (targets, t);
	}

	raw_words = g_strsplit_set (query, " \t\r\n", -1);
	if (raw_words) {
		words = g_new0 (gchar *, g_strv_length (raw_words) + 1);
		sexp = g_string_new ("(match-all (and (not (system-flag \"deleted\"))");

		for (ii = 0; raw_words[ii]; ii++) {
			if (!*raw_words[ii])
				continue;

			words[n_words++] = g_utf8_casefold (raw_words[ii], -1);

			g_string_append (sexp, " (body-contains ");
			camel_sexp_encode_string (sexp, raw_words[ii]);
			g_string_append_c (sexp, ')');
		}

		g_string_append (sexp, "))");
	}

	matches = g_ptr_array_new_with_free_func ((GDestroyNotify) scoped_search_match_free);

	for (guint t_idx = 0; t_idx < targets->len; t_idx++) {
		SearchTargetFolder *t = targets->pdata[t_idx];
		CamelFolder *folder = core_ref_folder (core, t->account_uid, t->folder_full_name, NULL);
		GPtrArray *body_uids = NULL, *uids;
		GHashTable *body_set = NULL;

		if (!folder)
			continue;

		if (n_words > 0 && camel_folder_search_sync (folder, sexp->str, &body_uids, core->cancellable, NULL) && body_uids) {
			body_set = g_hash_table_new (g_str_hash, g_str_equal);
			for (ii = 0; ii < body_uids->len; ii++)
				g_hash_table_add (body_set, body_uids->pdata[ii]);
		}

		uids = camel_folder_dup_uids (folder);
		for (ii = 0; uids && ii < uids->len; ii++) {
			const gchar *uid = uids->pdata[ii];
			CamelMessageInfo *info = camel_folder_get_message_info (folder, uid);

			if (info && !(camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED) &&
			    ((body_set && g_hash_table_contains (body_set, uid)) ||
			     message_info_matches_query (info, query, words))) {
				ScopedSearchMatch *m = g_slice_new0 (ScopedSearchMatch);
				m->info = info;
				m->account_uid = g_strdup (t->account_uid);
				m->account_name = g_strdup (t->account_name);
				m->folder_full_name = g_strdup (t->folder_full_name);
				m->folder_display_name = g_strdup (t->folder_display_name);
				g_ptr_array_add (matches, m);
			} else {
				g_clear_object (&info);
			}
		}

		if (body_set)
			g_hash_table_destroy (body_set);
		if (body_uids)
			g_ptr_array_unref (body_uids);
		if (uids)
			g_ptr_array_unref (uids);
		g_object_unref (folder);
	}

	g_ptr_array_sort (matches, compare_scoped_matches_by_date);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (ii = 0; ii < matches->len && (max_count == 0 || ii < max_count); ii++) {
		ScopedSearchMatch *m = matches->pdata[ii];
		CamelMessageInfo *info = m->info;
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

		/* Données de localisation du message dans les portées multi-dossiers et multi-comptes */
		json_builder_set_member_name (builder, "account");
		json_builder_add_string_value (builder, m->account_uid ? m->account_uid : "");
		json_builder_set_member_name (builder, "account_name");
		json_builder_add_string_value (builder, m->account_name ? m->account_name : "");
		json_builder_set_member_name (builder, "folder");
		json_builder_add_string_value (builder, m->folder_full_name ? m->folder_full_name : "");
		json_builder_set_member_name (builder, "folder_name");
		json_builder_add_string_value (builder, m->folder_display_name ? m->folder_display_name : "");
		json_builder_set_member_name (builder, "preview");
		json_builder_add_string_value (builder, camel_message_info_get_preview (info) ?
			camel_message_info_get_preview (info) : "");

		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	g_ptr_array_unref (matches);
	g_ptr_array_unref (targets);
	if (sexp)
		g_string_free (sexp, TRUE);
	g_strfreev (words);
	g_strfreev (raw_words);

	return json;
}

/* --- actions sur les messages (drapeaux, suppression, déplacement) --- */

/* Définit (ou efface) le suivi d'un message : libellé, échéance, date de fin.
 * Paramètres :
 *   args  : [0] account_uid, [1] folder_full_name, [2] message_uid,
 *           [3] libellé ("" pour effacer le suivi), [4] échéance (secondes
 *           Unix en texte, "" si aucune), [5] date de fin (idem)
 * Stockage : étiquettes utilisateur Camel "follow-up", "due-by" et
 * "completed-on" (comme Evolution) ; le drapeau FLAGGED suit le libellé.
 * Retourne : {"flagged", "follow_up", "due_by", "completed_on"} ou NULL. */
static gchar *
core_set_follow_up_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	CamelFolder *folder;
	CamelMessageInfo *mi;
	JsonBuilder *builder;
	gchar *json;
	gboolean active = args[3] && *args[3];

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	mi = camel_folder_get_message_info (folder, args[2]);
	if (mi) {
		camel_message_info_freeze_notifications (mi);
		camel_message_info_set_user_tag (mi, "follow-up", active ? args[3] : NULL);
		camel_message_info_set_user_tag (mi, "due-by", active && args[4] && *args[4] ? args[4] : NULL);
		camel_message_info_set_user_tag (mi, "completed-on", active && args[5] && *args[5] ? args[5] : NULL);
		camel_message_info_thaw_notifications (mi);
	}
	camel_folder_set_message_flags (folder, args[2], CAMEL_MESSAGE_FLAGGED, active ? CAMEL_MESSAGE_FLAGGED : 0);
	camel_folder_synchronize_message_sync (folder, args[2], core->cancellable, NULL);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "flagged");
	json_builder_add_boolean_value (builder, active);
	builder_add_string (builder, "follow_up", active ? args[3] : "");
	builder_add_string (builder, "due_by", active && args[4] ? args[4] : "");
	builder_add_string (builder, "completed_on", active && args[5] ? args[5] : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_clear_object (&mi);
	g_object_unref (folder);

	return json;
}

/* Soumet la définition du suivi d'un message (libellé, échéance, date de fin).
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message
 *   label            : libellé du suivi, "" pour effacer le suivi
 *   due_by           : échéance en secondes Unix (texte), "" ou NULL si aucune
 *   completed_on     : date de fin en secondes Unix (texte), "" ou NULL
 *   func             : rappel recevant {"flagged", "follow_up", "due_by", "completed_on"}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_set_follow_up (EvoCore *core,
			const char *account_uid,
			const char *folder_full_name,
			const char *message_uid,
			const char *label,
			const char *due_by,
			const char *completed_on,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = {
		(gchar *) account_uid,
		(gchar *) folder_full_name,
		(gchar *) message_uid,
		(gchar *) (label ? label : ""),
		(gchar *) (due_by ? due_by : ""),
		(gchar *) (completed_on ? completed_on : ""),
		NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);

	core_submit (core, core_set_follow_up_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de recherche de messages dans un dossier.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   query            : texte recherché
 *   max_count        : nombre maximal de messages renvoyés (0 pour tous)
 *   func             : rappel recevant le tableau JSON des messages
 *   user_data        : données utilisateur transmises au rappel */
/* Recherche des messages avec sélection de la portée (dossier courant,
 * sous-dossiers, compte courant, ou tous les comptes).
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte (pour portée folder/subfolders/account)
 *   folder_full_name : chemin complet du dossier de base (pour portée folder/subfolders)
 *   scope            : portée ("folder", "subfolders", "account", "all")
 *   query            : texte recherché (mots séparés par des espaces)
 *   max_count        : nombre maximal de messages renvoyés (0 pour tous)
 *   func             : rappel recevant le tableau JSON des messages
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_search_messages_scoped (EvoCore *core,
				 const char *account_uid,
				 const char *folder_full_name,
				 const char *scope,
				 const char *query,
				 unsigned int max_count,
				 EvoCoreResultFunc func,
				 void *user_data)
{
	gchar count[16];
	gchar *args[] = {
		(gchar *) (account_uid ? account_uid : ""),
		(gchar *) (folder_full_name ? folder_full_name : ""),
		(gchar *) (query ? query : ""),
		count,
		(gchar *) (scope && *scope ? scope : "folder"),
		NULL
	};

	g_snprintf (count, sizeof (count), "%u", max_count);
	core_submit (core, core_search_messages_run, g_strdupv (args), func, user_data);
}

void
evo_core_search_messages (EvoCore *core,
			  const char *account_uid,
			  const char *folder_full_name,
			  const char *query,
			  unsigned int max_count,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	evo_core_search_messages_scoped (core, account_uid, folder_full_name, "folder", query, max_count, func, user_data);
}
