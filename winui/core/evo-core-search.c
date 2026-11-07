/*
 * evo-core : Recherche de messages (dossier, sous-dossiers, compte, tous les comptes)
 * et vues unifiées multi-comptes (boîte de réception, envoyés, brouillons, corbeille).
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
				  GPtrArray *targets,
				  gboolean skip_trash_junk)
{
	for (; fi; fi = fi->next) {
		guint32 type = fi->flags & CAMEL_FOLDER_TYPE_MASK;

		/* recherche par critères : ni corbeille ni indésirables, comme les dossiers de recherche d'Outlook */
		if (skip_trash_junk && (type == CAMEL_FOLDER_TYPE_TRASH || type == CAMEL_FOLDER_TYPE_JUNK))
			continue;
		if (!(fi->flags & CAMEL_FOLDER_NOSELECT) && fi->full_name && *fi->full_name) {
			SearchTargetFolder *t = g_slice_new0 (SearchTargetFolder);
			t->account_uid = g_strdup (account_uid);
			t->account_name = g_strdup (account_name);
			t->folder_full_name = g_strdup (fi->full_name);
			t->folder_display_name = g_strdup (fi->display_name ? fi->display_name : fi->full_name);
			g_ptr_array_add (targets, t);
		}
		if (fi->child)
			collect_folder_targets_recursive (fi->child, account_uid, account_name, targets, skip_trash_junk);
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

/* Sérialise des messages trouvés dans plusieurs dossiers en tableau JSON, avec
 * leur localisation (compte, dossier).
 * Paramètres :
 *   matches       : messages triés (ScopedSearchMatch *)
 *   max_count     : nombre maximal de messages sérialisés (0 pour tous)
 *   composite_uid : TRUE pour que "uid" soit unique entre comptes et dossiers
 *                   ("<uid>\x1f<compte>\x1f<dossier>") ; les actions du client
 *                   en retirent la localisation avant d'appeler le moteur
 * Retourne : chaîne JSON nouvellement allouée. */
static gchar *
scoped_matches_to_json (GPtrArray *matches,
			guint max_count,
			gboolean composite_uid)
{
	JsonBuilder *builder;
	gchar *json;
	guint ii;

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
		if (composite_uid) {
			gchar *composite = g_strdup_printf ("%s\x1f%s\x1f%s", camel_message_info_get_uid (info),
				m->account_uid ? m->account_uid : "", m->folder_full_name ? m->folder_full_name : "");

			json_builder_add_string_value (builder, composite);
			g_free (composite);
		} else {
			json_builder_add_string_value (builder, camel_message_info_get_uid (info));
		}
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
		/* regroupement par discussion (empreintes 64 bits Camel) */
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

	return json;
}

/* Critères d'une recherche enregistrée : opérateurs de la requête, combinés (ET) avec les mots libres. */
typedef struct _SearchFilters {
	gboolean any;
	gboolean unread;
	gboolean read;
	gboolean flagged;
	gboolean attachments;
	gint64 larger;		/* taille minimale en octets (0 : aucune) */
	gchar *from;		/* texte à trouver dans l'expéditeur (casefold), NULL : aucun */
} SearchFilters;

/* Sépare les opérateurs (is:unread, is:read, is:flagged, has:attachment, larger:<octets>, from:<texte>)
 * des mots libres d'une requête.
 * Paramètres :
 *   query      : requête saisie ou enregistrée
 *   free_query : reçoit les mots libres, séparés par des espaces
 *   filters    : reçoit les critères (à libérer avec search_filters_clear) */
static void
search_parse_operators (const gchar *query,
			GString *free_query,
			SearchFilters *filters)
{
	gchar **tokens = g_strsplit_set (query ? query : "", " \t\r\n", -1);
	guint ii;

	for (ii = 0; tokens && tokens[ii]; ii++) {
		const gchar *token = tokens[ii];

		if (!*token)
			continue;
		if (g_ascii_strcasecmp (token, "is:unread") == 0) {
			filters->unread = TRUE;
		} else if (g_ascii_strcasecmp (token, "is:read") == 0) {
			filters->read = TRUE;
		} else if (g_ascii_strcasecmp (token, "is:flagged") == 0) {
			filters->flagged = TRUE;
		} else if (g_ascii_strcasecmp (token, "has:attachment") == 0) {
			filters->attachments = TRUE;
		} else if (g_ascii_strncasecmp (token, "larger:", 7) == 0 && token[7]) {
			filters->larger = g_ascii_strtoll (token + 7, NULL, 10);
		} else if (g_ascii_strncasecmp (token, "from:", 5) == 0 && token[5]) {
			g_free (filters->from);
			filters->from = g_utf8_casefold (token + 5, -1);
		} else {
			if (free_query->len)
				g_string_append_c (free_query, ' ');
			g_string_append (free_query, token);
			continue;
		}
		filters->any = TRUE;
	}
	g_strfreev (tokens);
}

/* Expression Camel équivalente aux critères, évaluée par l'index du dossier (rapide sur de gros dossiers) ;
 * search_filters_match () reste appelé ensuite pour les critères exacts (taille en octets, expéditeur).
 * Retourne : expression (g_free), NULL sans critère. */
static gchar *
search_filters_sexp (const SearchFilters *filters)
{
	GString *sexp;

	if (!filters->any)
		return NULL;

	sexp = g_string_new ("(match-all (and (not (system-flag \"deleted\"))");
	if (filters->unread)
		g_string_append (sexp, " (not (system-flag \"seen\"))");
	if (filters->read)
		g_string_append (sexp, " (system-flag \"seen\")");
	if (filters->flagged)
		g_string_append (sexp, " (system-flag \"flagged\")");
	if (filters->attachments)
		g_string_append (sexp, " (system-flag \"attachments\")");
	if (filters->larger > 1024)
		g_string_append_printf (sexp, " (> (get-size) %" G_GINT64_FORMAT ")", filters->larger / 1024 - 1);
	if (filters->from) {
		g_string_append (sexp, " (header-contains \"from\" ");
		camel_sexp_encode_string (sexp, filters->from);
		g_string_append_c (sexp, ')');
	}
	g_string_append (sexp, "))");

	return g_string_free (sexp, FALSE);
}

static void
search_filters_clear (SearchFilters *filters)
{
	g_free (filters->from);
	filters->from = NULL;
}

/* Un message respecte-t-il les critères ? */
static gboolean
search_filters_match (const SearchFilters *filters,
		      CamelMessageInfo *info)
{
	guint32 flags = camel_message_info_get_flags (info);

	if (filters->unread && (flags & CAMEL_MESSAGE_SEEN))
		return FALSE;
	if (filters->read && !(flags & CAMEL_MESSAGE_SEEN))
		return FALSE;
	if (filters->flagged && !(flags & CAMEL_MESSAGE_FLAGGED))
		return FALSE;
	if (filters->attachments && !(flags & CAMEL_MESSAGE_ATTACHMENTS))
		return FALSE;
	if (filters->larger > 0 && (gint64) camel_message_info_get_size (info) < filters->larger)
		return FALSE;
	if (filters->from) {
		const gchar *from = camel_message_info_get_from (info);
		gchar *folded = g_utf8_casefold (from ? from : "", -1);
		gboolean found = strstr (folded, filters->from) != NULL;

		g_free (folded);
		if (!found)
			return FALSE;
	}

	return TRUE;
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
	GString *sexp = NULL, *free_query = g_string_new (NULL);
	SearchFilters filters = { 0 };
	gboolean skip_trash_junk;
	gchar *filter_sexp;
	guint ii, n_words = 0;
	gchar *json;

	/* opérateurs (is:unread, from:…) : retirés de la requête, appliqués comme critères */
	search_parse_operators (query, free_query, &filters);
	skip_trash_junk = filters.any;
	if (filters.any)
		query = free_query->str;
	filter_sexp = search_filters_sexp (&filters);

	if (!filters.any && (!query || !*query)) {
		if (g_strcmp0 (scope, "folder") == 0) {
			gchar *list_args[] = { args[0], args[1], args[3], NULL };

			g_string_free (free_query, TRUE);
			g_free (filter_sexp);
			return core_list_messages_run (core, list_args, error);
		}
		g_string_free (free_query, TRUE);
		g_free (filter_sexp);
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
						collect_folder_targets_recursive (fi, s_uid, s_name, targets, skip_trash_junk);
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
					collect_folder_targets_recursive (fi, account_uid, s_name, targets, skip_trash_junk);
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
							collect_folder_targets_recursive (found->child, account_uid, s_name, targets, skip_trash_junk);
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

		if (filter_sexp) {
			/* seuls les messages respectant les critères sont lus */
			if (!camel_folder_search_sync (folder, filter_sexp, &uids, core->cancellable, NULL))
				uids = NULL;
		} else {
			uids = camel_folder_dup_uids (folder);
		}
		for (ii = 0; uids && ii < uids->len; ii++) {
			const gchar *uid = uids->pdata[ii];
			CamelMessageInfo *info = camel_folder_get_message_info (folder, uid);

			if (info && !(camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED) &&
			    search_filters_match (&filters, info) &&
			    (n_words == 0 ||
			     (body_set && g_hash_table_contains (body_set, uid)) ||
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

	json = scoped_matches_to_json (matches, max_count, g_strcmp0 (scope, "folder") != 0);

	g_ptr_array_unref (matches);
	g_ptr_array_unref (targets);
	if (sexp)
		g_string_free (sexp, TRUE);
	g_strfreev (words);
	g_strfreev (raw_words);
	search_filters_clear (&filters);
	g_string_free (free_query, TRUE);
	g_free (filter_sexp);

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

/* --- vues unifiées multi-comptes --- */

/* Indique si un compte fait partie de la sélection d'une vue unifiée.
 * Paramètres :
 *   included : identifiants de comptes retenus (NULL ou vide : tous)
 *   uid      : identifiant du compte testé
 * Retourne : TRUE si le compte est retenu. */
static gboolean
unified_account_included (gchar **included,
			  const gchar *uid)
{
	if (!included || !*included)
		return TRUE;

	return g_strv_contains ((const gchar * const *) included, uid);
}

/* Construit la vue unifiée d'un rôle de dossier : les messages du dossier de ce
 * rôle (boîte de réception, envoyés, brouillons, corbeille...) de tous les
 * comptes, fusionnés et triés du plus récent au plus ancien. Chaque message
 * porte son compte et son dossier d'origine, et un "uid" composite unique.
 * Choix d'architecture : agrégation côté moteur plutôt que CamelVeeFolder (qui
 * exige un CamelVeeStore, des requêtes S-expression et une synchronisation de
 * résumés non fournis ici) ; les messages restent ceux de leur dossier réel,
 * donc lecture, indicateurs, déplacement et suppression sont inchangés.
 * Un compte injoignable est ignoré (pas d'échec global).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] rôle, [1] comptes retenus séparés par des virgules ("" : tous),
 *           [2] nombre maximal de messages (0 : tous)
 *   error : emplacement d'erreur GError
 * Retourne : tableau JSON des messages, ou NULL sur erreur. */
static gchar *
core_list_unified_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	const gchar *role = args[0];
	guint max_count = (guint) g_ascii_strtoull (args[2], NULL, 10);
	gchar **included = (args[1] && *args[1]) ? g_strsplit (args[1], ",", -1) : NULL;
	GPtrArray *matches;
	GList *services, *link;
	gchar *json;

	if (!role || !*role) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Rôle de dossier manquant");
		g_strfreev (included);
		return NULL;
	}

	matches = g_ptr_array_new_with_free_func ((GDestroyNotify) scoped_search_match_free);
	services = camel_session_list_services (core->session);

	for (link = services; link; link = g_list_next (link)) {
		CamelService *srv;
		CamelStore *store;
		CamelProvider *provider;
		CamelFolderInfo *fi;
		CamelFolder *folder;
		GPtrArray *uids;
		const gchar *s_uid, *s_name;
		gchar *folder_name;
		guint ii;

		if (!CAMEL_IS_STORE (link->data))
			continue;

		srv = CAMEL_SERVICE (link->data);
		store = CAMEL_STORE (link->data);
		provider = camel_service_get_provider (srv);
		s_uid = camel_service_get_uid (srv);

		/* les dossiers de recherche (vfolder) ne sont pas des comptes */
		if ((provider && g_strcmp0 (provider->protocol, "vfolder") == 0) ||
		    !unified_account_included (included, s_uid))
			continue;

		s_name = camel_service_get_display_name (srv);
		if (!s_name)
			s_name = s_uid;

		/* arborescence depuis le cache local, sinon depuis le serveur */
		fi = camel_store_get_folder_info_sync (store, NULL,
			CAMEL_STORE_FOLDER_INFO_FAST | CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, NULL);
		if (!fi && core_ensure_connected (core, store, NULL))
			fi = camel_store_get_folder_info_sync (store, NULL,
				CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, NULL);
		if (!fi)
			continue;

		folder_name = core_find_role_folder (core, store, fi, role);
		camel_folder_info_free (fi);
		if (!folder_name)
			continue;

		folder = core_ref_folder (core, s_uid, folder_name, NULL);
		if (!folder) {
			g_free (folder_name);
			continue;
		}

		uids = camel_folder_dup_uids (folder);
		if (!uids || uids->len == 0) {
			/* premier chargement sans cache : interrogation serveur */
			if (uids)
				g_ptr_array_unref (uids);
			camel_folder_refresh_info_sync (folder, core->cancellable, NULL);
			uids = camel_folder_dup_uids (folder);
		} else {
			/* cache présent : relève serveur en arrière-plan */
			gchar **bg_args = g_new0 (gchar *, 3);

			bg_args[0] = g_strdup (s_uid);
			bg_args[1] = g_strdup (folder_name);
			core_submit (core, core_folder_background_refresh_run, bg_args, core_ignore_result, NULL);
		}

		g_object_set_data (G_OBJECT (folder), "evo-initial-sync-done", GINT_TO_POINTER (1));

		for (ii = 0; uids && ii < uids->len; ii++) {
			CamelMessageInfo *info = camel_folder_get_message_info (folder, uids->pdata[ii]);

			if (info && !(camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED)) {
				ScopedSearchMatch *m = g_slice_new0 (ScopedSearchMatch);

				m->info = info;
				m->account_uid = g_strdup (s_uid);
				m->account_name = g_strdup (s_name);
				m->folder_full_name = g_strdup (folder_name);
				m->folder_display_name = g_strdup (camel_folder_get_display_name (folder));
				g_ptr_array_add (matches, m);
			} else {
				g_clear_object (&info);
			}
		}

		if (uids)
			g_ptr_array_unref (uids);
		g_object_unref (folder);
		g_free (folder_name);
	}

	g_list_free (services);
	g_strfreev (included);

	g_ptr_array_sort (matches, compare_scoped_matches_by_date);
	json = scoped_matches_to_json (matches, max_count, TRUE);
	g_ptr_array_unref (matches);

	return json;
}

/* Soumet le chargement d'une vue unifiée (messages d'un rôle de dossier de
 * tous les comptes), triés du plus récent au plus ancien.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   role         : rôle du dossier ("inbox", "sent", "drafts", "trash", "junk", "archive")
 *   account_uids : comptes inclus, identifiants séparés par des virgules (NULL ou "" : tous)
 *   max_count    : nombre maximal de messages renvoyés (0 pour tous)
 *   func         : rappel recevant le tableau JSON des messages ; chaque message porte
 *                  "account", "account_name", "folder", "folder_name" et un "uid"
 *                  composite "<uid>\x1f<compte>\x1f<dossier>" à réduire à la partie
 *                  avant le premier \x1f pour les autres appels du moteur
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_list_unified (EvoCore *core,
		       const char *role,
		       const char *account_uids,
		       unsigned int max_count,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar count[16];
	gchar *args[] = {
		(gchar *) (role ? role : ""),
		(gchar *) (account_uids ? account_uids : ""),
		count,
		NULL
	};

	g_return_if_fail (role != NULL);

	g_snprintf (count, sizeof (count), "%u", max_count);
	core_submit (core, core_list_unified_run, g_strdupv (args), func, user_data);
}
