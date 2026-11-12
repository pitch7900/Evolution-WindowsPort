/*
 * evo-core : Operations sur les dossiers : creation, renommage, deplacement, copie,
 * suppression, quota et actions sur un dossier entier.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Supprime un message : s'il ne se trouve pas déjà dans la corbeille du compte
 * et que celle-ci existe, l'y transfère ; sinon le marque comme supprimé et
 * purge le dossier.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] message_uid
 *   error : emplacement d'erreur GError
 * Retourne : {"trashed": true|false}, ou NULL en cas d'erreur. */
static gchar *
core_get_folder_quota_run (EvoCore *core,
			   gchar **args,
			   GError **error)
{
	CamelFolder *folder;
	CamelFolderQuotaInfo *quota;
	gchar *json;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	quota = camel_folder_get_quota_info_sync (folder, core->cancellable, NULL);
	if (quota) {
		guint64 used = quota->used;
		guint64 total = quota->total;
		guint percent = 0;
		if (total > 0) {
			percent = (guint) ((used * 100) / total);
			if (percent > 100)
				percent = 100;
		}
		json = g_strdup_printf ("{\"has_quota\":true,\"name\":\"%s\",\"used\":%" G_GUINT64_FORMAT ",\"total\":%" G_GUINT64_FORMAT ",\"percent\":%u}",
					quota->name ? quota->name : "Stockage",
					used, total, percent);
		camel_folder_quota_info_free (quota);
	} else {
		json = g_strdup ("{\"has_quota\":false,\"name\":\"\",\"used\":0,\"total\":0,\"percent\":0}");
	}

	g_object_unref (folder);
	return json;
}

static gchar *
core_folder_action_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	CamelFolder *folder, *trash = NULL;
	CamelStore *store;
	GPtrArray *uids;
	gboolean permanent = FALSE, success = TRUE;
	guint ii, count = 0;
	gchar *json;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	uids = camel_folder_dup_uids (folder);
	if (!uids)
		uids = g_ptr_array_new_with_free_func (g_free);

	if (g_str_equal (args[2], "mark-all-read")) {
		camel_folder_freeze (folder);
		for (ii = 0; ii < uids->len; ii++) {
			if (!(camel_folder_get_message_flags (folder, uids->pdata[ii]) & CAMEL_MESSAGE_SEEN)) {
				camel_folder_set_message_flags (folder, uids->pdata[ii], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
				count++;
			}
		}
		camel_folder_thaw (folder);
		success = camel_folder_synchronize_sync (folder, FALSE, core->cancellable, error);
	} else if (g_str_equal (args[2], "empty")) {
		CamelFolderInfo *fi;
		gchar *trash_name = NULL;
		guint32 type = CAMEL_FOLDER_TYPE_NORMAL;

		store = camel_folder_get_parent_store (folder);
		fi = camel_store_get_folder_info_sync (store, NULL,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST, core->cancellable, NULL);
		if (fi) {
			trash_name = find_trash_folder_name (fi);
			type = find_folder_type (fi, args[1]);
			camel_folder_info_free (fi);
		}

		permanent = type == CAMEL_FOLDER_TYPE_TRASH || type == CAMEL_FOLDER_TYPE_JUNK ||
			!trash_name || g_strcmp0 (trash_name, args[1]) == 0;
		if (!permanent)
			trash = core_ref_folder (core, args[0], trash_name, NULL);
		permanent = permanent || !trash;
		count = uids->len;

		if (count > 0 && permanent) {
			camel_folder_freeze (folder);
			for (ii = 0; ii < uids->len; ii++)
				camel_folder_set_message_flags (folder, uids->pdata[ii],
					CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
					CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
			camel_folder_thaw (folder);
			success = camel_folder_synchronize_sync (folder, TRUE, core->cancellable, error);
		} else if (count > 0) {
			success = camel_folder_transfer_messages_to_sync (folder, uids, trash, TRUE, NULL, core->cancellable, error) &&
				camel_folder_synchronize_sync (folder, TRUE, core->cancellable, error);
			camel_folder_synchronize_sync (trash, FALSE, core->cancellable, NULL);
		}

		g_free (trash_name);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Action inconnue : « %s »", args[2]);
		success = FALSE;
	}

	g_ptr_array_unref (uids);
	g_clear_object (&trash);
	g_object_unref (folder);

	if (!success)
		return NULL;

	{
		JsonBuilder *builder = core_event_builder ();

		builder_add_string (builder, "account", args[0]);
		core_emit_builder (core, "folders-updated", builder);
	}

	json = g_strdup_printf ("{\"count\":%u,\"permanent\":%s}", count, permanent ? "true" : "false");

	return json;
}

/* Déplace un message d'un dossier source vers un dossier de destination, puis
 * synchronise les deux dossiers et notifie l'interface.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] src_account_uid, [1] src_folder_full_name, [2] message_uid,
 *           [3] dest_account_uid, [4] dest_folder_full_name,
 *           [5] "copy" pour copier (l'original reste en place), facultatif
 *   error : emplacement d'erreur GError
 * Retourne : "{}" en cas de succès, ou NULL en cas d'erreur. */
gchar *
core_move_message_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	CamelFolder *src, *dest;
	GPtrArray *uids;
	gboolean ok, copy = args[5] && g_str_equal (args[5], "copy");

	src = core_ref_folder (core, args[0], args[1], error);
	if (!src)
		return NULL;

	dest = core_ref_folder (core, args[3], args[4], error);
	if (!dest) {
		g_object_unref (src);
		return NULL;
	}

	if (src == dest) {
		g_object_unref (dest);
		g_object_unref (src);
		return g_strdup ("{}");
	}

	uids = g_ptr_array_new ();
	g_ptr_array_add (uids, args[2]);

	ok = camel_folder_transfer_messages_to_sync (src, uids, dest, !copy, NULL, core->cancellable, error);
	g_ptr_array_unref (uids);

	if (ok) {
		if (!copy)
			camel_folder_set_message_flags (src, args[2], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
		camel_folder_synchronize_sync (src, !copy, core->cancellable, NULL);
		camel_folder_synchronize_sync (dest, FALSE, core->cancellable, NULL);

		{
			JsonBuilder *builder = core_event_builder ();

			builder_add_string (builder, "account", args[0]);
			core_emit_builder (core, "folders-updated", builder);
		}

		if (g_strcmp0 (args[0], args[3]) != 0) {
			JsonBuilder *builder = core_event_builder ();

			builder_add_string (builder, "account", args[3]);
			core_emit_builder (core, "folders-updated", builder);
		}
	}

	g_object_unref (dest);
	g_object_unref (src);

	return ok ? g_strdup ("{}") : NULL;
}

/* Nom complet du dossier parent ("" pour un dossier de premier niveau).
 * Les noms complets de Camel utilisent toujours « / » : le séparateur IMAP
 * du serveur (« . » par exemple) est converti par le fournisseur IMAPX.
 * Paramètres :
 *   full_name : nom complet du dossier
 * Retourne : chaîne nouvellement allouée. */
static gchar *
folder_dup_parent_name (const gchar *full_name)
{
	const gchar *slash = strrchr (full_name, '/');

	return slash ? g_strndup (full_name, slash - full_name) : g_strdup ("");
}

/* Dernier élément du nom complet d'un dossier (« a/b/c » -> « c »).
 * Paramètres :
 *   full_name : nom complet du dossier
 * Retourne : pointeur dans full_name. */
static const gchar *
folder_base_name (const gchar *full_name)
{
	const gchar *slash = strrchr (full_name, '/');

	return slash ? slash + 1 : full_name;
}

/* Nom complet d'un dossier « name » sous « parent » ("" : premier niveau).
 * Retourne : chaîne nouvellement allouée. */
static gchar *
folder_build_full_name (const gchar *parent,
			const gchar *name)
{
	return parent && *parent ? g_strconcat (parent, "/", name, NULL) : g_strdup (name);
}

/* Vérifie un nom de dossier saisi : non vide, sans « / » (séparateur des
 * noms complets de Camel). Le séparateur propre au serveur IMAP est vérifié
 * par Camel lors de la création.
 * Paramètres :
 *   name  : nom saisi (déjà débarrassé des espaces de début et de fin)
 *   error : emplacement d'erreur GError
 * Retourne : TRUE si le nom est utilisable. */
static gboolean
folder_check_name (const gchar *name,
		   GError **error)
{
	if (!name || !*name) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Le nom du dossier est vide.");
		return FALSE;
	}

	if (strchr (name, '/')) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Le nom de dossier « %s » ne peut pas contenir le caractère « / ».", name);
		return FALSE;
	}

	return TRUE;
}

/* Ajoute à « names » le nom du dossier désigné par une adresse
 * « folder://<compte>/<dossier> » s'il appartient au compte account_uid. */
static void
add_account_folder_uri (GPtrArray *names,
			const gchar *account_uid,
			gchar *uri)
{
	gchar *store_uid = NULL, *folder_name = NULL;

	if (parse_folder_uri (uri, &store_uid, &folder_name) && g_strcmp0 (store_uid, account_uid) == 0) {
		g_ptr_array_add (names, folder_name);
		folder_name = NULL;
	}

	g_free (store_uid);
	g_free (folder_name);
	g_free (uri);
}

/* Dossiers Envoyés et Brouillons configurés dans l'identité du compte :
 * IMAPX ne leur attribue pas de type (CAMEL_FOLDER_TYPE_SENT/DRAFTS).
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant du compte
 * Retourne : tableau de noms complets (g_free), à libérer. */
static GPtrArray *
core_dup_configured_special_folders (EvoCore *core,
				     const gchar *account_uid)
{
	GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
	ESource *identity = core_ref_identity (core, account_uid);

	if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION))
		add_account_folder_uri (names, account_uid, e_source_mail_submission_dup_sent_folder (
			e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)));

	if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION))
		add_account_folder_uri (names, account_uid, e_source_mail_composition_dup_drafts_folder (
			e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION)));

	g_clear_object (&identity);

	return names;
}

/* Dossier spécial (Boîte de réception, Boîte d'envoi, Corbeille,
 * Indésirables, Envoyés, Brouillons, dossiers virtuels) : ni renommé, ni
 * déplacé, ni supprimé. Avec « recursive », un sous-dossier spécial (un
 * serveur qui range tout sous INBOX, par exemple) protège aussi le parent.
 * Paramètres :
 *   fi         : nœud du dossier dans l'arbre CamelFolderInfo
 *   configured : dossiers Envoyés / Brouillons configurés du compte
 *   recursive  : examiner aussi les sous-dossiers
 * Retourne : TRUE pour un dossier spécial. */
static gboolean
folder_info_is_special (CamelFolderInfo *fi,
			GPtrArray *configured,
			gboolean recursive)
{
	CamelFolderInfo *child;
	guint ii;

	switch (fi->flags & CAMEL_FOLDER_TYPE_MASK) {
	case CAMEL_FOLDER_TYPE_INBOX:
	case CAMEL_FOLDER_TYPE_OUTBOX:
	case CAMEL_FOLDER_TYPE_TRASH:
	case CAMEL_FOLDER_TYPE_JUNK:
	case CAMEL_FOLDER_TYPE_SENT:
	case CAMEL_FOLDER_TYPE_DRAFTS:
		return TRUE;
	default:
		break;
	}

	if ((fi->flags & (CAMEL_FOLDER_VIRTUAL | CAMEL_FOLDER_SYSTEM)) != 0 ||
	    g_ascii_strcasecmp (fi->full_name, "INBOX") == 0 ||
	    g_strcmp0 (fi->full_name, CAMEL_VTRASH_NAME) == 0 ||
	    g_strcmp0 (fi->full_name, CAMEL_VJUNK_NAME) == 0)
		return TRUE;

	for (ii = 0; configured && ii < configured->len; ii++) {
		if (g_strcmp0 (fi->full_name, configured->pdata[ii]) == 0)
			return TRUE;
	}

	for (child = recursive ? fi->child : NULL; child; child = child->next) {
		if (folder_info_is_special (child, configured, TRUE))
			return TRUE;
	}

	return FALSE;
}

/* Ferme les dossiers suivis (core->folders) d'un dossier et de tous ses
 * sous-dossiers, avant de le renommer, déplacer ou supprimer : ils seront
 * rouverts sous leur nouveau nom à la demande.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant du compte
 *   full_name   : nom complet du dossier */
static void
core_untrack_folder_tree (EvoCore *core,
			  const gchar *account_uid,
			  const gchar *full_name)
{
	GHashTableIter iter;
	gpointer key, value;
	gchar *exact = folder_key (account_uid, full_name);
	gchar *prefix = g_strconcat (exact, "/", NULL);

	g_mutex_lock (&core->folders_lock);
	g_hash_table_iter_init (&iter, core->folders);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		if (g_str_equal (key, exact) || g_str_has_prefix (key, prefix)) {
			g_signal_handlers_disconnect_by_func (value, core_folder_changed_cb, core);
			g_hash_table_iter_remove (&iter);
		}
	}
	g_mutex_unlock (&core->folders_lock);

	g_free (prefix);
	g_free (exact);
}

/* Émet "folders-updated" pour un compte (l'interface recharge l'arbre). */
void
core_emit_folders_updated (EvoCore *core,
			   const gchar *account_uid)
{
	JsonBuilder *builder = core_event_builder ();

	builder_add_string (builder, "account", account_uid);
	core_emit_builder (core, "folders-updated", builder);
}

/* Contexte commun des opérations sur les dossiers : compte connecté et
 * arbre complet de ses dossiers. */
typedef struct {
	CamelStore *store;
	CamelFolderInfo *tree;
} FolderOpContext;

static gboolean
folder_op_begin (EvoCore *core,
		 const gchar *account_uid,
		 FolderOpContext *ctx,
		 GError **error)
{
	ctx->store = core_ref_store (core, account_uid, error);
	ctx->tree = NULL;

	if (!ctx->store)
		return FALSE;

	if (!core_ensure_connected (core, ctx->store, error))
		return FALSE;

	ctx->tree = camel_store_get_folder_info_sync (ctx->store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST, core->cancellable, error);

	return ctx->tree != NULL || !(error && *error);
}

static void
folder_op_end (FolderOpContext *ctx)
{
	camel_folder_info_free (ctx->tree);
	g_clear_object (&ctx->store);
}

/* Nœud d'un dossier existant, avec une erreur en français s'il est absent. */
static CamelFolderInfo *
folder_op_lookup (FolderOpContext *ctx,
		  const gchar *full_name,
		  GError **error)
{
	CamelFolderInfo *fi = find_folder_info (ctx->tree, full_name);

	if (!fi)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Dossier introuvable : « %s ».", full_name);

	return fi;
}

/* Refuse un nom complet déjà utilisé dans le compte. */
static gboolean
folder_op_check_free (FolderOpContext *ctx,
		      const gchar *full_name,
		      GError **error)
{
	if (find_folder_info (ctx->tree, full_name)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
			"Un dossier « %s » existe déjà à cet emplacement.", folder_base_name (full_name));
		return FALSE;
	}

	return TRUE;
}

/* Abonne le compte à un nouveau dossier (IMAP), comme Evolution : sans
 * abonnement, un compte limité aux dossiers abonnés ne l'afficherait pas.
 * Un échec n'est qu'un avertissement. */
void
core_subscribe_new_folder (EvoCore *core,
			   CamelStore *store,
			   const gchar *full_name)
{
	GError *local_error = NULL;

	if (!CAMEL_IS_SUBSCRIBABLE (store))
		return;

	if (!camel_subscribable_subscribe_folder_sync (CAMEL_SUBSCRIBABLE (store), full_name, core->cancellable, &local_error)) {
		g_warning ("Cannot subscribe to “%s”: %s", full_name, local_error ? local_error->message : "?");
		g_clear_error (&local_error);
	}
}

/* Crée un dossier (et s'y abonne) sous parent ("" : premier niveau).
 * Retourne : le nom complet du nouveau dossier, ou NULL en cas d'erreur. */
static gchar *
folder_op_create (EvoCore *core,
		  FolderOpContext *ctx,
		  const gchar *parent,
		  const gchar *name,
		  GError **error)
{
	CamelFolderInfo *created;
	GError *local_error = NULL;
	gchar *full_name = folder_build_full_name (parent, name);

	if (!folder_check_name (name, error) || !folder_op_check_free (ctx, full_name, error)) {
		g_free (full_name);
		return NULL;
	}

	if (parent && *parent) {
		CamelFolderInfo *parent_fi = folder_op_lookup (ctx, parent, error);

		/* IMAPX crée au premier niveau l'enfant d'un dossier non
		 * sélectionnable (\Noselect) : refusé plutôt que mal placé */
		if (parent_fi && (parent_fi->flags & CAMEL_FOLDER_NOSELECT) != 0)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				"Impossible de créer un dossier dans « %s ».",
				parent_fi->display_name ? parent_fi->display_name : parent);

		if (!parent_fi || (parent_fi->flags & CAMEL_FOLDER_NOSELECT) != 0) {
			g_free (full_name);
			return NULL;
		}
	}

	created = camel_store_create_folder_sync (ctx->store, parent && *parent ? parent : NULL, name,
		core->cancellable, &local_error);

	/* Le dossier est absent de l'arborescence (vérifié plus haut) mais Camel le croit
	 * encore présent : après la suppression d'un dossier local, sa ligne de folders.db
	 * peut être réécrite par l'enregistrement différé du résumé. Cette ligne périmée
	 * est effacée, puis la création est retentée une fois. */
	if (!created && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
	    camel_store_db_delete_folder (camel_store_get_db (ctx->store), full_name, NULL)) {
		g_clear_error (&local_error);
		created = camel_store_create_folder_sync (ctx->store, parent && *parent ? parent : NULL, name,
			core->cancellable, &local_error);
	}

	if (!created) {
		g_propagate_error (error, local_error);
		g_free (full_name);
		return NULL;
	}

	camel_folder_info_free (created);
	core_subscribe_new_folder (core, ctx->store, full_name);

	return full_name;
}

/* Résultat {"full_name": "...", "count": n} des opérations sur les dossiers
 * ("count" omis si count < 0). */
static gchar *
folder_op_result_full_name (const gchar *full_name,
			    gint count)
{
	JsonBuilder *builder = json_builder_new ();
	gchar *json;

	json_builder_begin_object (builder);
	builder_add_string (builder, "full_name", full_name);
	if (count >= 0) {
		json_builder_set_member_name (builder, "count");
		json_builder_add_int_value (builder, count);
	}
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	return json;
}

/* Crée un dossier.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] parent_full_name ("" : premier niveau du
 *           compte), [2] nom du nouveau dossier
 *   error : emplacement d'erreur GError
 * Retourne : {"full_name": "..."}, ou NULL en cas d'erreur. */
static gchar *
core_create_folder_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	FolderOpContext ctx = { NULL, NULL };
	gchar *name, *full_name = NULL, *json;

	name = g_strstrip (g_strdup (args[2]));

	if (folder_op_begin (core, args[0], &ctx, error))
		full_name = folder_op_create (core, &ctx, args[1], name, error);

	folder_op_end (&ctx);
	g_free (name);

	if (!full_name)
		return NULL;

	core_emit_folders_updated (core, args[0]);
	json = folder_op_result_full_name (full_name, -1);
	g_free (full_name);

	return json;
}

/* Renomme un dossier vers un nouveau nom complet (renommage ou déplacement
 * dans le même compte), sous-dossiers compris. Refuse les dossiers spéciaux
 * et les déplacements d'un dossier dans lui-même ou l'un de ses
 * sous-dossiers.
 * Retourne : TRUE en cas de succès. */
static gboolean
folder_op_rename (EvoCore *core,
		  FolderOpContext *ctx,
		  const gchar *account_uid,
		  const gchar *old_name,
		  const gchar *new_name,
		  GError **error)
{
	CamelFolderInfo *fi;
	CamelFolder *folder;
	GPtrArray *configured;
	gboolean special, ok;
	gsize old_len = strlen (old_name);

	fi = folder_op_lookup (ctx, old_name, error);
	if (!fi)
		return FALSE;

	configured = core_dup_configured_special_folders (core, account_uid);
	special = folder_info_is_special (fi, configured, TRUE);
	g_ptr_array_unref (configured);

	if (special) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"« %s » est un dossier spécial (ou en contient un) : il ne peut être ni renommé ni déplacé.",
			fi->display_name ? fi->display_name : old_name);
		return FALSE;
	}

	if (g_str_equal (old_name, new_name))
		return TRUE;

	if (strncmp (new_name, old_name, old_len) == 0 && new_name[old_len] == '/') {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Un dossier ne peut pas être déplacé dans lui-même ni dans l'un de ses sous-dossiers.");
		return FALSE;
	}

	if (!folder_op_check_free (ctx, new_name, error))
		return FALSE;

	/* imapx_store_rename_folder_sync () se sert du CamelFolder après avoir
	 * rendu sa référence : le garder ouvert pendant le renommage */
	folder = camel_store_get_folder_sync (ctx->store, old_name, 0, core->cancellable, NULL);

	ok = camel_store_rename_folder_sync (ctx->store, old_name, new_name, core->cancellable, error);

	core_untrack_folder_tree (core, account_uid, old_name);
	g_clear_object (&folder);

	return ok;
}

/* Renomme un dossier : le nouveau nom garde le chemin du parent.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] nouveau nom (sans chemin)
 *   error : emplacement d'erreur GError
 * Retourne : {"full_name": "<nouveau nom complet>"}, ou NULL en cas d'erreur. */
static gchar *
core_rename_folder_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	FolderOpContext ctx = { NULL, NULL };
	gchar *name, *parent, *new_name, *json = NULL;
	gboolean ok = FALSE;

	name = g_strstrip (g_strdup (args[2]));
	parent = folder_dup_parent_name (args[1]);
	new_name = folder_build_full_name (parent, name);

	if (folder_check_name (name, error) && folder_op_begin (core, args[0], &ctx, error))
		ok = folder_op_rename (core, &ctx, args[0], args[1], new_name, error);

	folder_op_end (&ctx);

	if (ok) {
		core_emit_folders_updated (core, args[0]);
		json = folder_op_result_full_name (new_name, -1);
	}

	g_free (new_name);
	g_free (parent);
	g_free (name);

	return json;
}

/* Déplace un dossier (et ses sous-dossiers) sous un autre parent du même
 * compte, par renommage.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] nouveau parent ("" :
 *           premier niveau du compte)
 *   error : emplacement d'erreur GError
 * Retourne : {"full_name": "<nouveau nom complet>"}, ou NULL en cas d'erreur. */
static gchar *
core_move_folder_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	FolderOpContext ctx = { NULL, NULL };
	gchar *new_name, *json = NULL;
	gboolean ok = FALSE;

	new_name = folder_build_full_name (args[2], folder_base_name (args[1]));

	if (folder_op_begin (core, args[0], &ctx, error) &&
	    (!*args[2] || folder_op_lookup (&ctx, args[2], error)))
		ok = folder_op_rename (core, &ctx, args[0], args[1], new_name, error);

	folder_op_end (&ctx);

	if (ok) {
		core_emit_folders_updated (core, args[0]);
		json = folder_op_result_full_name (new_name, -1);
	}

	g_free (new_name);

	return json;
}

/* Copie un dossier sous un autre parent du même compte : crée un dossier de
 * même nom puis y copie tous les messages (les originaux restent en place).
 * Les sous-dossiers ne sont pas copiés.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] parent de la copie
 *           ("" : premier niveau du compte)
 *   error : emplacement d'erreur GError
 * Retourne : {"full_name": "<copie>", "count": n}, ou NULL en cas d'erreur. */
static gchar *
core_copy_folder_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	FolderOpContext ctx = { NULL, NULL };
	CamelFolderInfo *fi;
	CamelFolder *src = NULL, *dest = NULL;
	GPtrArray *uids = NULL;
	gchar *new_name = NULL, *json = NULL;
	gboolean ok = FALSE;

	if (!folder_op_begin (core, args[0], &ctx, error))
		goto exit;

	fi = folder_op_lookup (&ctx, args[1], error);
	if (!fi)
		goto exit;

	if ((fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)) != 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"« %s » ne contient pas de messages et ne peut pas être copié.",
			fi->display_name ? fi->display_name : args[1]);
		goto exit;
	}

	src = core_ref_folder (core, args[0], args[1], error);
	if (!src || !camel_folder_refresh_info_sync (src, core->cancellable, error))
		goto exit;

	new_name = folder_op_create (core, &ctx, args[2], folder_base_name (args[1]), error);
	if (!new_name)
		goto exit;

	dest = core_ref_folder (core, args[0], new_name, error);
	if (!dest)
		goto exit;

	uids = camel_folder_dup_uids (src);
	if (!uids)
		uids = g_ptr_array_new_with_free_func (g_free);

	ok = uids->len == 0 ||
		camel_folder_transfer_messages_to_sync (src, uids, dest, FALSE, NULL, core->cancellable, error);
	if (ok)
		camel_folder_synchronize_sync (dest, FALSE, core->cancellable, NULL);

 exit:
	folder_op_end (&ctx);

	/* le dossier créé reste (même vide) en cas d'échec de la copie */
	if (new_name)
		core_emit_folders_updated (core, args[0]);

	if (ok)
		json = folder_op_result_full_name (new_name, (gint) uids->len);

	if (uids)
		g_ptr_array_unref (uids);
	g_clear_object (&dest);
	g_clear_object (&src);
	g_free (new_name);

	return json;
}

/* Supprime un dossier du magasin après l'avoir désabonné et oublié.
 * Le dossier est gardé ouvert pendant la suppression : Camel n'efface les
 * données locales du dossier (ligne de folders.db, messages en cache, dossiers
 * virtuels Corbeille / Indésirables) que si son objet est encore dans le cache
 * du magasin. Sans cela, le dossier disparaît du disque mais reste connu de la
 * base (voir aussi folder_op_create () qui purge une ligne réécrite plus tard).
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   store       : magasin du compte
 *   account_uid : uid du compte
 *   full_name   : nom complet du dossier
 *   error       : emplacement d'erreur GError
 * Retourne : TRUE en cas de succès. */
static gboolean
folder_delete_forgetting (EvoCore *core,
			  CamelStore *store,
			  const gchar *account_uid,
			  const gchar *full_name,
			  GError **error)
{
	CamelFolder *keep;
	gboolean success;

	/* sans erreur remontée : un dossier non sélectionnable ne s'ouvre pas */
	keep = camel_store_get_folder_sync (store, full_name, 0, core->cancellable, NULL);

	core_untrack_folder_tree (core, account_uid, full_name);

	if (CAMEL_IS_SUBSCRIBABLE (store) &&
	    camel_subscribable_folder_is_subscribed (CAMEL_SUBSCRIBABLE (store), full_name))
		camel_subscribable_unsubscribe_folder_sync (CAMEL_SUBSCRIBABLE (store), full_name, core->cancellable, NULL);

	success = camel_store_delete_folder_sync (store, full_name, core->cancellable, error);
	g_clear_object (&keep);

	return success;
}

/* Supprime un dossier. Choix : un dossier qui a des sous-dossiers est
 * refusé (les supprimer d'abord) ; ses messages sont supprimés
 * définitivement avec lui (l'interface demande confirmation en indiquant leur
 * nombre). Les dossiers spéciaux sont refusés.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name
 *   error : emplacement d'erreur GError
 * Retourne : {"count": <messages supprimés>}, ou NULL en cas d'erreur. */
static gchar *
core_delete_folder_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	FolderOpContext ctx = { NULL, NULL };
	CamelFolderInfo *fi;
	GPtrArray *configured;
	gchar *json = NULL;
	guint count = 0;
	gboolean special;

	if (!folder_op_begin (core, args[0], &ctx, error))
		goto exit;

	fi = folder_op_lookup (&ctx, args[1], error);
	if (!fi)
		goto exit;

	configured = core_dup_configured_special_folders (core, args[0]);
	special = folder_info_is_special (fi, configured, FALSE);
	g_ptr_array_unref (configured);

	if (special) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"« %s » est un dossier spécial : il ne peut pas être supprimé.",
			fi->display_name ? fi->display_name : args[1]);
		goto exit;
	}

	if (fi->child) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY,
			"« %s » contient des sous-dossiers : supprimez-les d'abord.",
			fi->display_name ? fi->display_name : args[1]);
		goto exit;
	}

	/* messages supprimés d'abord : les dossiers locaux (Maildir) doivent
	 * être vides pour être supprimés */
	if ((fi->flags & CAMEL_FOLDER_NOSELECT) == 0) {
		CamelFolder *folder = core_ref_folder (core, args[0], args[1], error);
		GPtrArray *uids;
		gboolean ok;
		guint ii;

		if (!folder || !camel_folder_refresh_info_sync (folder, core->cancellable, error)) {
			g_clear_object (&folder);
			goto exit;
		}

		uids = camel_folder_dup_uids (folder);
		count = uids ? uids->len : 0;

		camel_folder_freeze (folder);
		for (ii = 0; ii < count; ii++)
			camel_folder_set_message_flags (folder, uids->pdata[ii],
				CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
				CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
		camel_folder_thaw (folder);

		ok = count == 0 || camel_folder_synchronize_sync (folder, TRUE, core->cancellable, error);

		if (uids)
			g_ptr_array_unref (uids);
		g_object_unref (folder);

		if (!ok)
			goto exit;
	}

	if (folder_delete_forgetting (core, ctx.store, args[0], args[1], error))
		json = g_strdup_printf ("{\"count\":%u}", count);

	core_emit_folders_updated (core, args[0]);

 exit:
	folder_op_end (&ctx);

	return json;
}

/* Supprime un dossier et ses sous-dossiers (du plus profond au plus haut) :
 * messages supprimés d'abord (les dossiers locaux doivent être vides), puis
 * désabonnement et suppression du dossier.
 * Retourne : TRUE en cas de succès. */
static gboolean
folder_delete_tree (EvoCore *core,
		    FolderOpContext *ctx,
		    const gchar *account_uid,
		    CamelFolderInfo *fi,
		    GError **error)
{
	CamelFolderInfo *child;

	for (child = fi->child; child; child = child->next) {
		if (!folder_delete_tree (core, ctx, account_uid, child, error))
			return FALSE;
	}

	if ((fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)) == 0) {
		CamelFolder *folder = core_ref_folder (core, account_uid, fi->full_name, error);
		GPtrArray *uids;
		gboolean ok;
		guint ii, count;

		if (!folder || !camel_folder_refresh_info_sync (folder, core->cancellable, error)) {
			g_clear_object (&folder);
			return FALSE;
		}

		uids = camel_folder_dup_uids (folder);
		count = uids ? uids->len : 0;

		camel_folder_freeze (folder);
		for (ii = 0; ii < count; ii++)
			camel_folder_set_message_flags (folder, uids->pdata[ii],
				CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
				CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
		camel_folder_thaw (folder);

		ok = count == 0 || camel_folder_synchronize_sync (folder, TRUE, core->cancellable, error);

		if (uids)
			g_ptr_array_unref (uids);
		g_object_unref (folder);

		if (!ok)
			return FALSE;
	}

	return folder_delete_forgetting (core, ctx->store, account_uid, fi->full_name, error);
}

/* État d'un transfert de dossier (copie ou déplacement entre comptes) :
 * compteurs, annulation et premier message d'erreur, partagés par tous les
 * dossiers de l'arborescence. */
typedef struct {
	GCancellable *cancel;	/* annulation de ce transfert (référence prise au début) */
	gchar *src_account;
	gchar *dst_account;
	gchar *label;		/* nom du dossier de tête, pour la progression */
	gboolean merge;		/* réutiliser les dossiers déjà présents à la destination */
	guint total;		/* messages à traiter (0 : inconnu) */
	guint done;		/* messages traités (copiés, ignorés ou en échec) */
	guint copied;
	guint skipped;		/* déjà présents à la destination (même Message-ID) */
	guint failed;
	gboolean cancelled;
	gchar *first_error;
} TransferState;

/* Émet la progression d'un transfert de dossier (événement « progress » de
 * type « transfer » : l'interface compose le texte dans la langue de
 * l'utilisateur).
 * Paramètres :
 *   core   : instance du moteur evo-core
 *   st     : état du transfert
 *   folder : dossier en cours de traitement
 *   active : FALSE pour le message de fin */
static void
transfer_emit_progress (EvoCore *core,
			TransferState *st,
			const gchar *folder,
			gboolean active)
{
	JsonBuilder *builder = core_event_builder ();

	builder_add_string (builder, "account", st->src_account);
	builder_add_string (builder, "kind", "transfer");
	builder_add_string (builder, "folder", folder ? folder : "");
	builder_add_string (builder, "label", st->label ? st->label : "");
	builder_add_int (builder, "done", st->done);
	builder_add_int (builder, "total", st->total);
	builder_add_int (builder, "percent", !active ? 100 : st->total > 0 ? (gint64) st->done * 100 / st->total : -1);
	builder_add_boolean (builder, "active", active);
	core_emit_builder (core, "progress", builder);
}

/* Total des messages d'une arborescence de dossiers (d'après les compteurs
 * déjà connus des dossiers ; ceux qui sont inconnus comptent pour 0). */
static guint
transfer_count_messages (CamelFolderInfo *fi)
{
	guint total = 0;

	for (; fi; fi = fi->next) {
		if (fi->total > 0 && (fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)) == 0)
			total += (guint) fi->total;
		total += transfer_count_messages (fi->child);
	}

	return total;
}

/* Copie un à un les messages d'un dossier vers un autre dossier, par lots
 * enregistrés sur le serveur toutes les 50 copies. Le message est transmis avec
 * son CamelMessageInfo : indicateurs (lu, suivi), étiquettes / mots-clés et date
 * de réception d'origine (INTERNALDATE IMAP) suivent quand le serveur de
 * destination les accepte. Un message déjà présent à la destination (même
 * Message-ID) est ignoré, ce qui permet de reprendre un transfert interrompu ;
 * un message en échec est compté et le transfert continue. Les messages marqués
 * supprimés ne sont pas copiés.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   st          : état du transfert (compteurs mis à jour)
 *   src         : dossier source
 *   dest        : dossier de destination
 *   folder_name : nom complet du dossier source (progression et messages d'erreur)
 * Retourne : FALSE si le transfert a été annulé. */
static gboolean
transfer_messages (EvoCore *core,
		   TransferState *st,
		   CamelFolder *src,
		   CamelFolder *dest,
		   const gchar *folder_name)
{
	GHashTable *known;
	GPtrArray *uids;
	guint ii, since_sync = 0;

	known = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);

	/* Message-ID des messages déjà présents à la destination (reprise) */
	if (camel_folder_refresh_info_sync (dest, st->cancel, NULL)) {
		uids = camel_folder_dup_uids (dest);
		for (ii = 0; uids && ii < uids->len; ii++) {
			CamelMessageInfo *mi = camel_folder_get_message_info (dest, uids->pdata[ii]);

			if (mi) {
				guint64 id = camel_message_info_get_message_id (mi);

				if (id != 0) {
					gint64 *key = g_new (gint64, 1);

					*key = (gint64) id;
					g_hash_table_add (known, key);
				}
				g_object_unref (mi);
			}
		}
		if (uids)
			g_ptr_array_unref (uids);
	}

	uids = camel_folder_dup_uids (src);
	for (ii = 0; uids && ii < uids->len; ii++) {
		CamelMessageInfo *mi;
		CamelMimeMessage *message;
		GError *local_error = NULL;
		gint64 id;

		if (g_cancellable_is_cancelled (st->cancel)) {
			st->cancelled = TRUE;
			break;
		}

		mi = camel_folder_get_message_info (src, uids->pdata[ii]);
		if (!mi) {
			st->done++;
			continue;
		}

		id = (gint64) camel_message_info_get_message_id (mi);
		if ((camel_message_info_get_flags (mi) & CAMEL_MESSAGE_DELETED) != 0) {
			/* message déjà supprimé : rien à copier */
		} else if (id != 0 && g_hash_table_contains (known, &id)) {
			st->skipped++;
		} else {
			message = camel_folder_get_message_sync (src, uids->pdata[ii], st->cancel, &local_error);
			if (message) {
				if (camel_folder_append_message_sync (dest, message, mi, NULL, st->cancel, &local_error)) {
					st->copied++;
					since_sync++;
				}
				g_object_unref (message);
			}

			if (local_error) {
				if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
				    g_cancellable_is_cancelled (st->cancel)) {
					st->cancelled = TRUE;
				} else {
					st->failed++;
					if (!st->first_error)
						st->first_error = g_strdup_printf ("%s : %s", folder_name, local_error->message);
				}
				g_error_free (local_error);
			}
		}

		g_object_unref (mi);
		if (st->cancelled)
			break;

		st->done++;
		transfer_emit_progress (core, st, folder_name, TRUE);

		if (since_sync >= 50) {
			camel_folder_synchronize_sync (dest, FALSE, st->cancel, NULL);
			since_sync = 0;
		}
	}

	if (uids)
		g_ptr_array_unref (uids);
	if (since_sync > 0)
		camel_folder_synchronize_sync (dest, FALSE, NULL, NULL);
	g_hash_table_unref (known);

	return !st->cancelled;
}

/* Copie un dossier, ses messages et ses sous-dossiers vers un autre compte
 * (ou un autre emplacement) : crée un dossier de même nom sous dst_parent de
 * la destination (ou réutilise celui qui existe si st->merge), y copie les
 * messages (transfer_messages), puis recommence pour chaque sous-dossier. Les
 * originaux restent en place.
 * top_name reçoit le nom complet du dossier créé au sommet (si non NULL et
 * encore vide).
 * Retourne : FALSE en cas d'erreur de dossier ou d'annulation (voir st). */
static gboolean
folder_transfer_tree (EvoCore *core,
		      FolderOpContext *dst,
		      TransferState *st,
		      CamelFolderInfo *fi,
		      const gchar *dst_parent,
		      const gchar *name_override,
		      gchar **top_name,
		      GError **error)
{
	CamelFolderInfo *child;
	const gchar *base = name_override && *name_override ? name_override : folder_base_name (fi->full_name);
	gchar *new_name = NULL;
	gboolean ok = TRUE;

	if (st->merge) {
		gchar *wanted = folder_build_full_name (dst_parent, base);
		CamelFolder *existing = camel_store_get_folder_sync (dst->store, wanted, 0, st->cancel, NULL);

		if (existing) {
			g_object_unref (existing);
			new_name = wanted;
		} else {
			g_free (wanted);
		}
	}

	if (!new_name)
		new_name = folder_op_create (core, dst, dst_parent, base, error);
	if (!new_name)
		return FALSE;

	if (top_name && !*top_name)
		*top_name = g_strdup (new_name);

	if ((fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)) == 0) {
		CamelFolder *src, *dest;

		src = core_ref_folder (core, st->src_account, fi->full_name, error);
		dest = src ? core_ref_folder (core, st->dst_account, new_name, error) : NULL;
		ok = src && dest && camel_folder_refresh_info_sync (src, st->cancel, error);

		if (ok)
			ok = transfer_messages (core, st, src, dest, fi->full_name);

		g_clear_object (&src);
		g_clear_object (&dest);
	}

	for (child = fi->child; ok && child; child = child->next)
		ok = folder_transfer_tree (core, dst, st, child, new_name, NULL, NULL, error);

	g_free (new_name);

	return ok;
}

/* Copie ou déplace un dossier (sous-dossiers compris) vers n'importe quel
 * compte. Un déplacement copie d'abord toute l'arborescence, puis supprime
 * l'original seulement si la copie a réussi sans aucun message en échec et sans
 * annulation. Émet des événements « progress » de type « transfer ».
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] compte source, [1] dossier source, [2] compte de destination,
 *           [3] parent de destination ("" : racine du compte), [4] "1" pour
 *           copier, "0" pour déplacer, [5] nouveau nom du dossier copié ("" :
 *           garder le nom d'origine), [6] "1" pour fusionner avec le dossier
 *           de même nom déjà présent (reprise d'un transfert interrompu)
 *   error : emplacement d'erreur GError
 * Retourne : {"full_name": "<dossier créé>", "count": <copiés>, "skipped": <déjà
 * présents>, "failed": <en échec>, "cancelled": bool, "error": "<première
 * erreur>"}, ou NULL en cas d'erreur de dossier. */
static gchar *
core_transfer_folder_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	FolderOpContext src = { NULL, NULL }, dst = { NULL, NULL };
	CamelFolderInfo *fi = NULL;
	TransferState st = { NULL, };
	gboolean copy = args[4][0] == '1', ok = FALSE, complete;
	gchar *top = NULL, *json = NULL;
	gsize src_len = strlen (args[1]);

	st.cancel = core->cancellable ? g_object_ref (core->cancellable) : g_cancellable_new ();
	st.src_account = args[0];
	st.dst_account = args[2];
	st.merge = args[6][0] == '1';

	if (folder_op_begin (core, args[0], &src, error) &&
	    folder_op_begin (core, args[2], &dst, error))
		fi = folder_op_lookup (&src, args[1], error);

	if (fi && !copy) {
		GPtrArray *configured = core_dup_configured_special_folders (core, args[0]);
		gboolean special = folder_info_is_special (fi, configured, TRUE);

		g_ptr_array_unref (configured);
		if (special) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
				"« %s » est un dossier spécial (ou en contient un) : il ne peut pas être déplacé.",
				fi->display_name ? fi->display_name : args[1]);
			fi = NULL;
		}
	}

	/* un dossier ne va pas dans lui-même ni dans l'un de ses sous-dossiers (même compte, déplacement) */
	if (fi && !copy && strcmp (args[0], args[2]) == 0 &&
	    strncmp (args[3], args[1], src_len) == 0 &&
	    (args[3][src_len] == '\0' || args[3][src_len] == '/')) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Un dossier ne peut pas être déplacé dans lui-même ni dans l'un de ses sous-dossiers.");
		fi = NULL;
	}

	if (fi) {
		st.label = g_strdup (fi->display_name ? fi->display_name : folder_base_name (fi->full_name));
		st.total = transfer_count_messages (fi);
		transfer_emit_progress (core, &st, fi->full_name, TRUE);

		ok = folder_transfer_tree (core, &dst, &st, fi, args[3], args[5], &top, error);
		complete = ok && !st.cancelled && st.failed == 0;
		if (complete && !copy)
			ok = folder_delete_tree (core, &src, args[0], fi, error);

		transfer_emit_progress (core, &st, fi->full_name, FALSE);
	}

	folder_op_end (&src);
	folder_op_end (&dst);

	/* des dossiers ont pu être créés (ou supprimés) même en cas d'échec partiel */
	if (top) {
		core_emit_folders_updated (core, args[2]);
		if (!copy && strcmp (args[0], args[2]) != 0)
			core_emit_folders_updated (core, args[0]);
	}

	if (ok) {
		JsonBuilder *builder = json_builder_new ();

		json_builder_begin_object (builder);
		builder_add_string (builder, "full_name", top ? top : "");
		builder_add_int (builder, "count", st.copied);
		builder_add_int (builder, "skipped", st.skipped);
		builder_add_int (builder, "failed", st.failed);
		builder_add_boolean (builder, "cancelled", st.cancelled);
		if (st.first_error)
			builder_add_string (builder, "error", st.first_error);
		json_builder_end_object (builder);
		json = json_from_builder (builder);
		g_object_unref (builder);
	}

	g_free (top);
	g_free (st.label);
	g_free (st.first_error);
	g_object_unref (st.cancel);

	return json;
}

/* Copie ou déplace un dossier avec ses sous-dossiers et ses messages vers un
 * dossier (ou la racine) de n'importe quel compte, en arrière-plan, avec
 * progression (événements « progress » de type « transfer », "done" / "total")
 * et annulation (evo_core_cancel_operations). Les indicateurs, les étiquettes et la
 * date de réception des messages sont conservés quand le serveur de destination
 * le permet ; les messages déjà présents à la destination (même Message-ID) ne
 * sont pas recopiés.
 * Paramètres :
 *   core               : instance du moteur evo-core
 *   src_account_uid    : compte du dossier
 *   folder_full_name   : chemin complet du dossier
 *   dest_account_uid   : compte de destination
 *   dest_parent_name   : chemin complet du parent de destination ("" : racine)
 *   new_name           : nouveau nom du dossier à la destination (NULL ou "" : nom d'origine)
 *   copy               : TRUE pour copier (l'original reste), FALSE pour déplacer
 *   merge              : TRUE pour fusionner avec le dossier de même nom déjà présent
 *                        à la destination (reprise d'un transfert interrompu)
 *   func               : rappel recevant {"full_name", "count", "skipped", "failed",
 *                        "cancelled", "error"}
 *   user_data          : données utilisateur transmises au rappel */
void
evo_core_transfer_folder (EvoCore *core,
			  const char *src_account_uid,
			  const char *folder_full_name,
			  const char *dest_account_uid,
			  const char *dest_parent_name,
			  const char *new_name,
			  int copy,
			  int merge,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) src_account_uid, (gchar *) folder_full_name,
		(gchar *) dest_account_uid, (gchar *) dest_parent_name, (gchar *) (copy ? "1" : "0"),
		(gchar *) (new_name ? new_name : ""), (gchar *) (merge ? "1" : "0"), NULL };

	g_return_if_fail (src_account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (dest_account_uid != NULL);
	g_return_if_fail (dest_parent_name != NULL);

	core_submit (core, core_transfer_folder_run, g_strdupv (args), func, user_data);
}

void
evo_core_folder_action (EvoCore *core,
			const char *account_uid,
			const char *folder_full_name,
			const char *action,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, (gchar *) action, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (action != NULL);

	core_submit (core, core_folder_action_run, g_strdupv (args), func, user_data);
}

void
evo_core_get_folder_quota (EvoCore *core,
			   const char *account_uid,
			   const char *folder_full_name,
			   EvoCoreResultFunc func,
			   void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);

	core_submit (core, core_get_folder_quota_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de déplacement d'un message vers un autre dossier.
 * Paramètres :
 *   core                  : instance du moteur evo-core
 *   src_account_uid       : identifiant du compte source
 *   src_folder_full_name  : chemin complet du dossier source
 *   message_uid           : identifiant du message à déplacer
 *   dest_account_uid      : identifiant du compte de destination
 *   dest_folder_full_name : chemin complet du dossier de destination
 *   func                  : rappel recevant {}
 *   user_data             : données utilisateur transmises au rappel */
void
evo_core_copy_message (EvoCore *core,
		       const char *src_account_uid,
		       const char *src_folder_full_name,
		       const char *message_uid,
		       const char *dest_account_uid,
		       const char *dest_folder_full_name,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = {
		(gchar *) src_account_uid,
		(gchar *) src_folder_full_name,
		(gchar *) message_uid,
		(gchar *) dest_account_uid,
		(gchar *) dest_folder_full_name,
		(gchar *) "copy",
		NULL
	};

	g_return_if_fail (src_account_uid != NULL && src_folder_full_name != NULL && message_uid != NULL);
	g_return_if_fail (dest_account_uid != NULL && dest_folder_full_name != NULL);

	core_submit (core, core_oq_move_run, g_strdupv (args), func, user_data);
}

void
evo_core_move_message (EvoCore *core,
		       const char *src_account_uid,
		       const char *src_folder_full_name,
		       const char *message_uid,
		       const char *dest_account_uid,
		       const char *dest_folder_full_name,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = {
		(gchar *) src_account_uid,
		(gchar *) src_folder_full_name,
		(gchar *) message_uid,
		(gchar *) dest_account_uid,
		(gchar *) dest_folder_full_name,
		NULL
	};

	g_return_if_fail (src_account_uid != NULL);
	g_return_if_fail (src_folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);
	g_return_if_fail (dest_account_uid != NULL);
	g_return_if_fail (dest_folder_full_name != NULL);

	core_submit (core, core_oq_move_run, g_strdupv (args), func, user_data);
}

/* Soumet la création d'un dossier.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   parent_full_name : chemin complet du parent ("" : premier niveau)
 *   name             : nom du nouveau dossier
 *   func             : rappel recevant {"full_name": "..."}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_create_folder (EvoCore *core,
			const char *account_uid,
			const char *parent_full_name,
			const char *name,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) parent_full_name, (gchar *) name, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (parent_full_name != NULL);
	g_return_if_fail (name != NULL);

	core_submit (core, core_create_folder_run, g_strdupv (args), func, user_data);
}

/* Soumet le renommage d'un dossier (le chemin du parent est conservé).
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   new_name         : nouveau nom (sans chemin)
 *   func             : rappel recevant {"full_name": "..."}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_rename_folder (EvoCore *core,
			const char *account_uid,
			const char *folder_full_name,
			const char *new_name,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, (gchar *) new_name, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (new_name != NULL);

	core_submit (core, core_rename_folder_run, g_strdupv (args), func, user_data);
}

/* Soumet le déplacement d'un dossier sous un autre parent du même compte.
 * Paramètres :
 *   core                 : instance du moteur evo-core
 *   account_uid          : identifiant du compte
 *   folder_full_name     : chemin complet du dossier
 *   new_parent_full_name : chemin complet du nouveau parent ("" : premier niveau)
 *   func                 : rappel recevant {"full_name": "..."}
 *   user_data            : données utilisateur transmises au rappel */
void
evo_core_move_folder (EvoCore *core,
		      const char *account_uid,
		      const char *folder_full_name,
		      const char *new_parent_full_name,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, (gchar *) new_parent_full_name, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (new_parent_full_name != NULL);

	core_submit (core, core_move_folder_run, g_strdupv (args), func, user_data);
}

/* Soumet la copie d'un dossier (ses messages, sans ses sous-dossiers) sous un
 * autre parent du même compte.
 * Paramètres :
 *   core                 : instance du moteur evo-core
 *   account_uid          : identifiant du compte
 *   folder_full_name     : chemin complet du dossier copié
 *   new_parent_full_name : chemin complet du parent de la copie ("" : premier niveau)
 *   func                 : rappel recevant {"full_name": "...", "count": n}
 *   user_data            : données utilisateur transmises au rappel */
void
evo_core_copy_folder (EvoCore *core,
		      const char *account_uid,
		      const char *folder_full_name,
		      const char *new_parent_full_name,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, (gchar *) new_parent_full_name, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (new_parent_full_name != NULL);

	core_submit (core, core_copy_folder_run, g_strdupv (args), func, user_data);
}

/* Soumet la suppression d'un dossier sans sous-dossiers (ses messages sont
 * supprimés définitivement).
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   func             : rappel recevant {"count": n}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_delete_folder (EvoCore *core,
			const char *account_uid,
			const char *folder_full_name,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);

	core_submit (core, core_delete_folder_run, g_strdupv (args), func, user_data);
}
