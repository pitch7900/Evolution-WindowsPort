/*
 * evo-core : Arborescence des dossiers : roles, comptage, suivi des dossiers ouverts
 * et signal "folder-changed".
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Rôle (dossier spécial) de chaque dossier d'un compte, pour ceux que le
 * serveur ne signale pas (IMAP sans SPECIAL-USE, par exemple) : dossiers
 * configurés dans le compte, sinon noms usuels. full_name -> type. */
typedef struct {
	GHashTable *roles;	/* full_name -> type (statique) */
	GHashTable *used;	/* types déjà attribués dans le compte */
} FolderRoles;

static void
folder_roles_add_uri (FolderRoles *fr,
		      const gchar *account_uid,
		      gchar *uri,
		      const gchar *type)
{
	gchar *store_uid = NULL, *folder_name = NULL;

	if (parse_folder_uri (uri, &store_uid, &folder_name) && g_strcmp0 (store_uid, account_uid) == 0 &&
	    !g_hash_table_contains (fr->roles, folder_name)) {
		g_hash_table_insert (fr->roles, folder_name, (gpointer) type);
		folder_name = NULL;
	}

	g_free (store_uid);
	g_free (folder_name);
	g_free (uri);
}

/* Dossier réel « real-trash-path » / « real-junk-path » des réglages Camel
 * (IMAP), s'il est activé. */
static void
folder_roles_add_real_path (FolderRoles *fr,
			    CamelSettings *settings,
			    const gchar *use_property,
			    const gchar *path_property,
			    const gchar *type)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (settings);
	gboolean use = FALSE;
	gchar *path = NULL;

	if (!g_object_class_find_property (klass, use_property) ||
	    !g_object_class_find_property (klass, path_property))
		return;

	g_object_get (settings, use_property, &use, path_property, &path, NULL);

	if (use && path && *path && !g_hash_table_contains (fr->roles, path))
		g_hash_table_insert (fr->roles, g_steal_pointer (&path), (gpointer) type);

	g_free (path);
}

static void
folder_roles_mark_used (FolderRoles *fr,
			CamelFolderInfo *fi)
{
	for (; fi; fi = fi->next) {
		const gchar *type = folder_type_name (fi->flags);

		if (!*type)
			type = g_hash_table_lookup (fr->roles, fi->full_name);
		if (type && *type)
			g_hash_table_add (fr->used, (gpointer) type);

		folder_roles_mark_used (fr, fi->child);
	}
}

static FolderRoles *
folder_roles_new (EvoCore *core,
		  CamelStore *store,
		  CamelFolderInfo *fi)
{
	FolderRoles *fr = g_new0 (FolderRoles, 1);
	const gchar *account_uid = camel_service_get_uid (CAMEL_SERVICE (store));
	ESource *identity = core_ref_identity (core, account_uid);
	ESource *account = e_source_registry_ref_source (core->registry, account_uid);
	CamelSettings *settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	fr->roles = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	fr->used = g_hash_table_new (g_str_hash, g_str_equal);

	if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION))
		folder_roles_add_uri (fr, account_uid, e_source_mail_submission_dup_sent_folder (
			e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)), "sent");

	if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION))
		folder_roles_add_uri (fr, account_uid, e_source_mail_composition_dup_drafts_folder (
			e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION)), "drafts");

	if (account && e_source_has_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT))
		folder_roles_add_uri (fr, account_uid, e_source_mail_account_dup_archive_folder (
			e_source_get_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT)), "archive");

	if (settings) {
		folder_roles_add_real_path (fr, settings, "use-real-trash-path", "real-trash-path", "trash");
		folder_roles_add_real_path (fr, settings, "use-real-junk-path", "real-junk-path", "junk");
	}

	folder_roles_mark_used (fr, fi);

	g_clear_object (&settings);
	g_clear_object (&account);
	g_clear_object (&identity);

	return fr;
}

static void
folder_roles_free (FolderRoles *fr)
{
	if (!fr)
		return;

	g_hash_table_destroy (fr->roles);
	g_hash_table_destroy (fr->used);
	g_free (fr);
}

/* Type d'un dossier : rôle signalé par le serveur, dossier configuré dans le
 * compte, sinon nom usuel (anglais / français) pour un rôle encore libre
 * dans le compte (le premier dossier trouvé l'obtient). */
static const gchar *
folder_roles_type (FolderRoles *fr,
		   CamelFolderInfo *fi)
{
	static const struct {
		const gchar *type;
		const gchar *names;	/* séparés par « | », en minuscules */
	} usual[] = {
		{ "inbox", "inbox|boîte de réception" },
		{ "drafts", "drafts|draft|brouillons" },
		{ "sent", "sent|sent items|sent messages|sent mail|éléments envoyés|envoyés|messages envoyés" },
		{ "trash", "trash|deleted items|deleted messages|corbeille|éléments supprimés" },
		{ "junk", "junk|junk e-mail|junk email|spam|courrier indésirable|indésirables|pourriels" },
		{ "archive", "archive|archives" },
		{ "outbox", "outbox|boîte d'envoi" }
	};
	const gchar *type = folder_type_name (fi->flags);
	gchar *name;
	guint ii;

	if (*type || !fr)
		return type;

	type = g_hash_table_lookup (fr->roles, fi->full_name);
	if (type)
		return type;

	name = g_utf8_strdown (fi->display_name ? fi->display_name : "", -1);

	for (ii = 0; ii < G_N_ELEMENTS (usual) && !type; ii++) {
		gchar **names;
		guint jj;

		if (g_hash_table_contains (fr->used, usual[ii].type))
			continue;

		names = g_strsplit (usual[ii].names, "|", -1);
		for (jj = 0; names[jj] && !type; jj++) {
			if (g_strcmp0 (name, names[jj]) == 0)
				type = usual[ii].type;
		}
		g_strfreev (names);
	}

	g_free (name);

	if (!type)
		return "";

	g_hash_table_add (fr->used, (gpointer) type);
	return type;
}

/* Parcourt l'arborescence et retient les dossiers de base (voir core_dup_base_folder_names). */
static void
folder_roles_collect_base (FolderRoles *fr,
			   CamelFolderInfo *fi,
			   GHashTable *names)
{
	for (; fi; fi = fi->next) {
		const gchar *type = folder_roles_type (fr, fi);

		if (g_strcmp0 (type, "inbox") == 0 || g_strcmp0 (type, "sent") == 0 || g_strcmp0 (type, "drafts") == 0)
			g_hash_table_add (names, g_strdup (fi->full_name));

		folder_roles_collect_base (fr, fi->child, names);
	}
}

/* Noms complets des dossiers de base d'un compte : Boîte de réception, Éléments envoyés et
 * Brouillons, reconnus comme dans la liste des dossiers (rôle du serveur, dossier configuré dans le
 * compte, nom usuel) : IMAP n'attribue pas toujours de type aux dossiers envoyés et brouillons.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   store : magasin du compte
 *   fi    : arborescence des dossiers du compte
 * Retourne : ensemble de noms complets (g_hash_table_destroy). */
GHashTable *
core_dup_base_folder_names (EvoCore *core,
			    CamelStore *store,
			    CamelFolderInfo *fi)
{
	FolderRoles *roles = folder_roles_new (core, store, fi);
	GHashTable *names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	folder_roles_collect_base (roles, fi, names);
	folder_roles_free (roles);

	return names;
}

static void
add_folder_infos (JsonBuilder *builder,
		  CamelFolderInfo *fi,
		  FolderRoles *roles)
{
	json_builder_begin_array (builder);

	for (; fi; fi = fi->next) {
		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "full_name");
		json_builder_add_string_value (builder, fi->full_name);
		json_builder_set_member_name (builder, "name");
		json_builder_add_string_value (builder, fi->display_name);
		json_builder_set_member_name (builder, "unread");
		json_builder_add_int_value (builder, MAX (fi->unread, 0));
		json_builder_set_member_name (builder, "total");
		json_builder_add_int_value (builder, MAX (fi->total, 0));
		json_builder_set_member_name (builder, "type");
		json_builder_add_string_value (builder, folder_roles_type (roles, fi));
		json_builder_set_member_name (builder, "selectable");
		json_builder_add_boolean_value (builder, (fi->flags & CAMEL_FOLDER_NOSELECT) == 0);
		json_builder_set_member_name (builder, "children");
		add_folder_infos (builder, fi->child, roles);
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
}

/* Parcourt l'arborescence en préordre (même ordre que add_folder_infos, donc
 * mêmes attributions de rôles) et renvoie le premier dossier sélectionnable
 * du rôle demandé.
 * Paramètres :
 *   fi    : dossiers à parcourir (frères et enfants)
 *   roles : rôles du compte
 *   role  : rôle recherché ("inbox", "sent", ...)
 * Retourne : nom complet du dossier (à libérer avec g_free) ou NULL. */
static gchar *
find_role_folder_recursive (CamelFolderInfo *fi,
			    FolderRoles *roles,
			    const gchar *role)
{
	for (; fi; fi = fi->next) {
		/* le type est toujours évalué : il réserve le rôle dans le compte */
		const gchar *type = folder_roles_type (roles, fi);
		gchar *found;

		if (fi->full_name && !(fi->flags & CAMEL_FOLDER_NOSELECT) &&
		    g_strcmp0 (type, role) == 0)
			return g_strdup (fi->full_name);

		found = find_role_folder_recursive (fi->child, roles, role);
		if (found)
			return found;
	}

	return NULL;
}

/* Trouve le dossier d'un rôle donné dans un compte (boîte de réception,
 * envoyés, brouillons, corbeille...), avec les mêmes règles que l'arborescence
 * affichée dans l'interface (rôle serveur, dossier configuré, nom usuel).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   store : magasin du compte
 *   fi    : arborescence des dossiers du compte (récursive)
 *   role  : rôle recherché ("inbox", "sent", "drafts", "trash", "junk", "archive")
 * Retourne : nom complet du dossier (à libérer avec g_free) ou NULL. */
gchar *
core_find_role_folder (EvoCore *core,
		       CamelStore *store,
		       CamelFolderInfo *fi,
		       const gchar *role)
{
	FolderRoles *roles;
	gchar *name;

	if (!fi || !role || !*role)
		return NULL;

	roles = folder_roles_new (core, store, fi);
	name = find_role_folder_recursive (fi, roles, role);
	folder_roles_free (roles);

	return name;
}

/* Liste l'arborescence des dossiers d'un compte : tente d'abord une lecture
 * rapide depuis le cache local (CAMEL_STORE_FOLDER_INFO_FAST) pour un affichage
 * instantané au démarrage, sans attendre la connexion réseau, puis se rabat sur
 * le serveur si le compte n'a pas encore de cache local.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : args[0] = uid du compte
 *   error : récepteur d'erreur en cas d'échec
 * Retourne : chaîne JSON de l'arborescence des dossiers ou NULL sur erreur. */
static gchar *
core_list_folders_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	CamelStore *store;
	CamelFolderInfo *fi = NULL;
	JsonBuilder *builder;
	gchar *json = NULL;

	store = core_ref_store (core, args[0], error);
	if (!store)
		return NULL;

	/* Essai 1 : lecture immédiate depuis le cache local sans attendre le réseau */
	fi = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_FAST | CAMEL_STORE_FOLDER_INFO_RECURSIVE,
		core->cancellable, NULL);

	/* Essai 2 : si aucun cache local n'existe encore (premier démarrage), se connecter au serveur */
	if (!fi) {
		if (core_ensure_connected (core, store, error)) {
			fi = camel_store_get_folder_info_sync (store, NULL,
				CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, error);
		}
	}

	if (fi || !(error && *error)) {
		FolderRoles *roles = folder_roles_new (core, store, fi);

		builder = json_builder_new ();
		add_folder_infos (builder, fi, roles);
		folder_roles_free (roles);
		json = json_from_builder (builder);
		g_object_unref (builder);
	}

	if (fi)
		camel_folder_info_free (fi);

	g_object_unref (store);

	return json;
}

/* --- messages --- */

gint
compare_infos_by_date (gconstpointer a,
		       gconstpointer b)
{
	CamelMessageInfo *ia = *(CamelMessageInfo **) a, *ib = *(CamelMessageInfo **) b;
	gint64 da = camel_message_info_get_date_received (ia);
	gint64 db = camel_message_info_get_date_received (ib);

	if (da <= 0)
		da = camel_message_info_get_date_sent (ia);
	if (db <= 0)
		db = camel_message_info_get_date_sent (ib);

	/* most recent first */
	return da < db ? 1 : (da > db ? -1 : 0);
}

/* CamelFolder::changed, emitted in the core thread: "folder-changed" */
void
core_folder_changed_cb (CamelFolder *folder,
			CamelFolderChangeInfo *changes,
			gpointer user_data)
{
	EvoCore *core = user_data;
	CamelStore *store = camel_folder_get_parent_store (folder);
	CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);
	JsonBuilder *builder;

	if (!store || g_cancellable_is_cancelled (core->cancellable))
		return;

	g_message ("folder “%s” changed: +%u -%u ~%u, %u unread, %d messages",
		camel_folder_get_full_name (folder),
		changes && changes->uid_added ? changes->uid_added->len : 0,
		changes && changes->uid_removed ? changes->uid_removed->len : 0,
		changes && changes->uid_changed ? changes->uid_changed->len : 0,
		summary ? camel_folder_summary_get_unread_count (summary) : 0,
		camel_folder_get_message_count (folder));

	builder = core_event_builder ();
	builder_add_string (builder, "account", camel_service_get_uid (CAMEL_SERVICE (store)));
	builder_add_string (builder, "folder", camel_folder_get_full_name (folder));
	json_builder_set_member_name (builder, "unread");
	json_builder_add_int_value (builder, summary ? camel_folder_summary_get_unread_count (summary) : 0);
	json_builder_set_member_name (builder, "total");
	json_builder_add_int_value (builder, camel_folder_get_message_count (folder));
	json_builder_set_member_name (builder, "added");
	json_builder_add_int_value (builder, changes && changes->uid_added ? changes->uid_added->len : 0);
	json_builder_set_member_name (builder, "removed");
	json_builder_add_int_value (builder, changes && changes->uid_removed ? changes->uid_removed->len : 0);
	json_builder_set_member_name (builder, "changed");
	json_builder_add_int_value (builder, changes && changes->uid_changed ? changes->uid_changed->len : 0);

	core_emit_builder (core, "folder-changed", builder);

	/* Émet "new-mail" lorsque de nouveaux messages non lus arrivent dans la
	 * boîte de réception après la synchronisation initiale */
	if (g_object_get_data (G_OBJECT (folder), "evo-initial-sync-done") != NULL &&
	    changes && changes->uid_added && changes->uid_added->len > 0 &&
	    ((camel_folder_get_flags (folder) & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX ||
	     g_ascii_strcasecmp (camel_folder_get_full_name (folder), "INBOX") == 0)) {
		guint ii, unread_added = 0;
		CamelMessageInfo *newest = NULL;
		gint64 newest_date = -1;

		for (ii = 0; ii < changes->uid_added->len; ii++) {
			CamelMessageInfo *info = camel_folder_get_message_info (folder, changes->uid_added->pdata[ii]);
			if (!info)
				continue;

			guint32 flags = camel_message_info_get_flags (info);
			if (!(flags & CAMEL_MESSAGE_SEEN) && !(flags & CAMEL_MESSAGE_DELETED)) {
				gint64 d = camel_message_info_get_date_received (info);
				if (d <= 0)
					d = camel_message_info_get_date_sent (info);
				unread_added++;
				if (!newest || d >= newest_date) {
					g_clear_object (&newest);
					newest = g_object_ref (info);
					newest_date = d;
				}
			}
			g_object_unref (info);
		}

		if (unread_added > 0 && newest) {
			JsonBuilder *mail_builder = core_event_builder ();

		/* filtre anti-spam : classement des nouveaux messages vers Indésirables (en tâche de fond) */
		core_spam_classify_added (core, folder, changes->uid_added);

			builder_add_string (mail_builder, "account", camel_service_get_uid (CAMEL_SERVICE (store)));
			builder_add_string (mail_builder, "folder", camel_folder_get_full_name (folder));
			json_builder_set_member_name (mail_builder, "count");
			json_builder_add_int_value (mail_builder, unread_added);
			builder_add_string (mail_builder, "from", camel_message_info_get_from (newest));
			builder_add_string (mail_builder, "subject", camel_message_info_get_subject (newest));
			core_emit_builder (core, "new-mail", mail_builder);
		}

		g_clear_object (&newest);
	}
}

gchar *
folder_key (const gchar *account_uid,
	    const gchar *folder_name)
{
	return g_strconcat (account_uid, "\x1F", folder_name, NULL);
}

/* keeps the folder open (cache) and follows its changes */
static CamelFolder *
core_track_folder (EvoCore *core,
		   const gchar *account_uid,
		   CamelFolder *folder)
{
	gchar *key = folder_key (account_uid, camel_folder_get_full_name (folder));
	CamelFolder *tracked;

	g_mutex_lock (&core->folders_lock);

	tracked = g_hash_table_lookup (core->folders, key);
	if (!tracked) {
		tracked = g_object_ref (folder);
		g_hash_table_insert (core->folders, key, tracked);
		g_signal_connect (tracked, "changed", G_CALLBACK (core_folder_changed_cb), core);
		key = NULL;
	}
	g_object_ref (tracked);

	g_mutex_unlock (&core->folders_lock);

	g_free (key);

	return tracked;
}

CamelFolder *
core_ref_folder (EvoCore *core,
		 const gchar *account_uid,
		 const gchar *folder_name,
		 GError **error)
{
	CamelStore *store;
	CamelFolder *folder = NULL, *tracked = NULL;
	gchar *key = folder_key (account_uid, folder_name);

	g_mutex_lock (&core->folders_lock);
	folder = g_hash_table_lookup (core->folders, key);
	if (folder)
		g_object_ref (folder);
	g_mutex_unlock (&core->folders_lock);
	g_free (key);

	if (folder) {
		/* dossier déjà suivi : il a pu être ouvert hors connexion depuis le cache
		 * (core_ref_folder_cached) ; une action sur le serveur exige la connexion */
		store = core_ref_store (core, account_uid, NULL);
		if (store) {
			core_ensure_connected (core, store, NULL);
			g_object_unref (store);
		}
		return folder;
	}

	store = core_ref_store (core, account_uid, error);
	if (!store)
		return NULL;

	if (core_ensure_connected (core, store, error))
		folder = camel_store_get_folder_sync (store, folder_name, 0, core->cancellable, error);

	if (folder) {
		tracked = core_track_folder (core, account_uid, folder);
		g_object_unref (folder);
	}

	g_object_unref (store);

	return tracked;
}

/* Ouvre un dossier depuis le cache local de Camel, sans se connecter : le résumé des
 * messages déjà téléchargés s'affiche alors tout de suite, pendant que la mise à jour
 * tourne en tâche de fond (IMAP et Microsoft 365 savent ouvrir un dossier hors
 * connexion). Si le dossier est inconnu du cache (premier lancement du compte), se
 * rabat sur core_ref_folder (), qui se connecte.
 * À réserver à l'affichage d'une liste : toute action sur le serveur (lire un message,
 * déplacer, supprimer...) doit passer par core_ref_folder ().
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   account_uid  : identifiant du compte
 *   folder_name  : nom complet du dossier
 *   from_cache   : reçoit TRUE si le dossier a été ouvert sans connexion (peut être NULL)
 *   error        : emplacement pour une éventuelle erreur
 * Retourne : le dossier suivi (g_object_unref), NULL en cas d'échec. */
CamelFolder *
core_ref_folder_cached (EvoCore *core,
			const gchar *account_uid,
			const gchar *folder_name,
			gboolean *from_cache,
			GError **error)
{
	CamelStore *store;
	CamelFolder *folder = NULL, *tracked = NULL;
	gchar *key = folder_key (account_uid, folder_name);

	if (from_cache)
		*from_cache = FALSE;

	g_mutex_lock (&core->folders_lock);
	folder = g_hash_table_lookup (core->folders, key);
	if (folder)
		g_object_ref (folder);
	g_mutex_unlock (&core->folders_lock);
	g_free (key);

	if (folder)
		return folder;

	store = core_ref_store (core, account_uid, NULL);
	if (store) {
		folder = camel_store_get_folder_sync (store, folder_name, 0, core->cancellable, NULL);
		g_object_unref (store);
	}

	if (!folder)
		return core_ref_folder (core, account_uid, folder_name, error);

	tracked = core_track_folder (core, account_uid, folder);
	g_object_unref (folder);
	if (from_cache && tracked)
		*from_cache = TRUE;

	return tracked;
}

/* Fenêtre de synchronisation d'un dossier : certains fournisseurs (Microsoft 365) ne synchronisent
 * que les N messages les plus récents (propriétés "sync-limit" et "sync-truncated" du dossier).
 * Paramètres :
 *   folder        : dossier
 *   out_truncated : TRUE si le serveur a des messages plus anciens que ceux synchronisés
 *   out_limit     : nombre de messages les plus récents synchronisés (0 : tous)
 * Retourne : FALSE si le fournisseur n'a pas de fenêtre (tous les messages sont synchronisés). */
gboolean
core_folder_sync_window_of (CamelFolder *folder,
			    gboolean *out_truncated,
			    gint *out_limit)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (folder);
	gboolean truncated = FALSE;
	gint limit = 0;

	if (!g_object_class_find_property (klass, "sync-limit") || !g_object_class_find_property (klass, "sync-truncated"))
		return FALSE;

	g_object_get (folder, "sync-truncated", &truncated, "sync-limit", &limit, NULL);

	if (out_truncated)
		*out_truncated = truncated;
	if (out_limit)
		*out_limit = limit;

	return TRUE;
}

/* Nombre de messages affichés par défaut dans la liste d'un dossier : la fenêtre de synchronisation
 * s'il y en a une (Microsoft 365), sinon 500. */
guint
core_folder_default_list_max (CamelFolder *folder)
{
	gint limit = 0;

	if (core_folder_sync_window_of (folder, NULL, &limit))
		return limit > 0 ? (guint) limit : 5000;

	return 500;
}

/* Comme core_folder_sync_window_of (), pour un dossier suivi (déjà ouvert) désigné par son nom. */
gboolean
core_folder_sync_window (EvoCore *core,
			 const gchar *account_uid,
			 const gchar *folder_name,
			 gboolean *out_truncated,
			 gint *out_limit)
{
	gchar *key = folder_key (account_uid, folder_name);
	CamelFolder *folder;
	gboolean supported = FALSE;

	g_mutex_lock (&core->folders_lock);
	folder = g_hash_table_lookup (core->folders, key);
	if (folder)
		g_object_ref (folder);
	g_mutex_unlock (&core->folders_lock);
	g_free (key);

	if (folder) {
		supported = core_folder_sync_window_of (folder, out_truncated, out_limit);
		g_object_unref (folder);
	}

	return supported;
}

/* État de la fenêtre de synchronisation d'un dossier.
 * Paramètres : args = [compte, dossier]
 * Retourne : {"supported": bool, "truncated": bool, "limit": n}. */
static gchar *
core_folder_sync_info_run (EvoCore *core,
			   gchar **args,
			   GError **error)
{
	gchar *key = folder_key (args[0], args[1]);
	CamelFolder *folder;
	gboolean truncated = FALSE, supported = FALSE;
	gint limit = 0;
	guint count = 0, default_max = 500;

	g_mutex_lock (&core->folders_lock);
	folder = g_hash_table_lookup (core->folders, key);
	if (folder)
		g_object_ref (folder);
	g_mutex_unlock (&core->folders_lock);
	g_free (key);

	if (folder) {
		CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);

		supported = core_folder_sync_window_of (folder, &truncated, &limit);
		default_max = core_folder_default_list_max (folder);
		count = summary ? camel_folder_summary_count (summary) : 0;
		g_object_unref (folder);
	}

	return g_strdup_printf ("{\"supported\": %s, \"truncated\": %s, \"limit\": %d, \"count\": %u, \"default_max\": %u}",
		supported ? "true" : "false", supported && truncated ? "true" : "false", supported ? limit : 0, count, default_max);
}

/* Étend la fenêtre de synchronisation d'un dossier de « extra » messages plus anciens et lance la
 * relève en fond (la fin est annoncée par "folder-refreshed").
 * Paramètres : args = [compte, dossier, nombre de messages à ajouter]
 * Retourne : {"limit": nouvelle limite}. */
static gchar *
core_extend_folder_sync_run (EvoCore *core,
			     gchar **args,
			     GError **error)
{
	CamelFolder *folder = core_ref_folder_cached (core, args[0], args[1], NULL, error);
	gint64 extra = g_ascii_strtoll (args[2] ? args[2] : "0", NULL, 10);
	gint limit = 0;
	gchar **bg_args;

	if (!folder)
		return NULL;

	if (!core_folder_sync_window_of (folder, NULL, &limit)) {
		g_object_unref (folder);
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Ce compte synchronise déjà tous les messages du dossier");
		return NULL;
	}

	if (limit > 0 && extra > 0) {
		/* extra_count très grand (G_MAXUINT) : tout charger, plus de limite */
		limit = extra >= G_MAXINT / 2 ? 0 : limit + (gint) extra;
		g_object_set (folder, "sync-limit", limit, NULL);
	}
	g_object_unref (folder);

	bg_args = g_new0 (gchar *, 3);
	bg_args[0] = g_strdup (args[0]);
	bg_args[1] = g_strdup (args[1]);
	core_submit_background (core, core_folder_background_refresh_run, bg_args, core_ignore_result, NULL);

	return g_strdup_printf ("{\"limit\": %d}", limit);
}

void
evo_core_get_folder_sync_info (EvoCore *core,
			       const char *account_uid,
			       const char *folder_full_name,
			       EvoCoreResultFunc func,
			       void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL && folder_full_name != NULL);

	core_submit (core, core_folder_sync_info_run, g_strdupv (args), func, user_data);
}

void
evo_core_extend_folder_sync (EvoCore *core,
			     const char *account_uid,
			     const char *folder_full_name,
			     unsigned int extra_count,
			     EvoCoreResultFunc func,
			     void *user_data)
{
	gchar count[16];
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, count, NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL && folder_full_name != NULL);

	g_snprintf (count, sizeof (count), "%u", extra_count);
	core_submit (core, core_extend_folder_sync_run, g_strdupv (args), func, user_data);
}

void
evo_core_list_folders (EvoCore *core,
		       const char *account_uid,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, NULL };

	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_list_folders_run, g_strdupv (args), func, user_data);
}
