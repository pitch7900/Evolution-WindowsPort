/*
 * evo-core : exportation du courrier (mbox, EML) et importation de courrier
 * (mbox, EML, Maildir, profil Thunderbird), de contacts (vCard) et d'agendas
 * (iCalendar). Voir evo-core.h et evo-core-private.h.
 *
 * Les opérations tournent en arrière-plan, émettent des événements « progress »
 * (types « export » et « import ») et s'interrompent avec
 * evo_core_cancel_operations (). La logique pure (format mbox, noms de fichiers,
 * indicateurs) est dans evo-core-transfer-util.h.
 */

#include "evo-core-private.h"
#include "evo-core-transfer-util.h"

/* État d'une opération d'exportation ou d'importation. */
typedef struct {
	EvoCore *core;
	GCancellable *cancel;	/* annulation de cette opération (référence prise au début) */
	const gchar *kind;	/* « export » ou « import » : type de l'événement de progression */
	gchar *account;		/* compte concerné (pour l'événement) */
	gchar *label;		/* élément en cours de traitement */
	guint64 total;		/* total prévu (messages à exporter, octets à importer) ; 0 : inconnu */
	guint64 done;		/* avancement dans la même unité que total */
	guint messages;		/* messages exportés / importés */
	guint folders;		/* dossiers exportés / créés */
	guint skipped;		/* éléments ignorés (déjà présents, supprimés, hors période) */
	guint failed;		/* éléments en échec */
	gboolean cancelled;
	gchar *first_error;
	gint64 last_emit;	/* dernier événement émis (temps monotone, µs) */
} TransferState;

/* Initialise l'état d'une opération.
 * Paramètres :
 *   st      : état à initialiser
 *   core    : instance du moteur
 *   kind    : « export » ou « import »
 *   account : compte concerné ("" si aucun) */
static void
transfer_state_init (TransferState *st,
		     EvoCore *core,
		     const gchar *kind,
		     const gchar *account)
{
	memset (st, 0, sizeof (*st));
	st->core = core;
	st->cancel = core->cancellable ? g_object_ref (core->cancellable) : g_cancellable_new ();
	st->kind = kind;
	st->account = g_strdup (account ? account : "");
}

/* Libère l'état d'une opération. */
static void
transfer_state_clear (TransferState *st)
{
	g_free (st->account);
	g_free (st->label);
	g_free (st->first_error);
	g_clear_object (&st->cancel);
}

/* Émet l'événement de progression.
 * Paramètres :
 *   st     : état de l'opération
 *   active : FALSE pour le message de fin
 *   force  : TRUE pour ignorer la limite de fréquence (un événement tous les 150 ms) */
static void
transfer_emit_progress (TransferState *st,
			gboolean active,
			gboolean force)
{
	gint64 now = g_get_monotonic_time ();
	JsonBuilder *builder;

	if (active && !force && now - st->last_emit < 150000)
		return;
	st->last_emit = now;

	builder = core_event_builder ();
	builder_add_string (builder, "account", st->account);
	builder_add_string (builder, "kind", st->kind);
	builder_add_string (builder, "folder", "");
	builder_add_string (builder, "label", st->label ? st->label : "");
	builder_add_int (builder, "done", (gint64) st->done);
	builder_add_int (builder, "total", (gint64) st->total);
	builder_add_int (builder, "messages", st->messages);
	builder_add_int (builder, "percent", !active ? 100 : st->total > 0 ? (gint64) (st->done * 100 / st->total) : -1);
	builder_add_boolean (builder, "active", active);
	core_emit_builder (st->core, "progress", builder);
}

/* Compte un élément en échec et mémorise la première erreur.
 * Paramètres :
 *   st      : état de l'opération
 *   context : élément concerné (fichier, dossier)
 *   error   : erreur (libérée), ou NULL */
static void
transfer_fail (TransferState *st,
	       const gchar *context,
	       GError *error)
{
	if (error && (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
	    g_cancellable_is_cancelled (st->cancel))) {
		st->cancelled = TRUE;
		g_error_free (error);
		return;
	}

	st->failed++;
	if (!st->first_error)
		st->first_error = g_strdup_printf ("%s : %s", context, error ? error->message : "erreur inconnue");
	if (error)
		g_error_free (error);
}

/* Vrai si l'opération est annulée (la marque dans l'état). */
static gboolean
transfer_is_cancelled (TransferState *st)
{
	if (g_cancellable_is_cancelled (st->cancel))
		st->cancelled = TRUE;
	return st->cancelled;
}

/* Résultat JSON commun : {"messages", "folders", "skipped", "failed", "cancelled", "error"}.
 * Retourne : chaîne JSON à libérer avec g_free (). */
static gchar *
transfer_result_json (TransferState *st,
		      const gchar *dest)
{
	JsonBuilder *builder = json_builder_new ();
	gchar *json;

	json_builder_begin_object (builder);
	builder_add_int (builder, "messages", st->messages);
	builder_add_int (builder, "folders", st->folders);
	builder_add_int (builder, "skipped", st->skipped);
	builder_add_int (builder, "failed", st->failed);
	builder_add_boolean (builder, "cancelled", st->cancelled);
	if (dest)
		builder_add_string (builder, "dest", dest);
	if (st->first_error)
		builder_add_string (builder, "error", st->first_error);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	return json;
}

/* Sérialise un message Camel (RFC 822) dans un tampon.
 * Paramètres :
 *   message : message
 *   cancel  : annulation
 *   data    : reçoit le contenu (à libérer avec g_free)
 *   size    : reçoit la taille
 * Retourne : TRUE en cas de succès */
static gboolean
transfer_message_bytes (CamelMimeMessage *message,
			GCancellable *cancel,
			gchar **data,
			gsize *size)
{
	GOutputStream *output = g_memory_output_stream_new_resizable ();
	gboolean ok;

	ok = camel_data_wrapper_write_to_output_stream_sync (CAMEL_DATA_WRAPPER (message), output, cancel, NULL) >= 0;
	g_output_stream_close (output, NULL, NULL);
	*size = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output));
	*data = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output));
	g_object_unref (output);

	return ok && *data != NULL;
}

/* ------------------------------------------------------------------ */
/* Exportation du courrier                                              */
/* ------------------------------------------------------------------ */

/* Paramètres d'une exportation. */
typedef struct {
	gchar *dest;		/* dossier de destination */
	gboolean mbox;		/* TRUE : un fichier mbox par dossier ; FALSE : un .eml par message */
	gint64 date_from;	/* début de la période (secondes depuis 1970, 0 : sans borne) */
	gint64 date_to;		/* fin de la période (0 : sans borne) */
	GHashTable *used;	/* chemins déjà écrits pendant cette exportation (noms uniques) */
} ExportContext;

/* Rend unique un chemin de fichier dans l'exportation en cours
 * (« Nom (2).ext » en cas de doublon).
 * Paramètres :
 *   ctx  : contexte d'exportation
 *   dir  : dossier du fichier
 *   name : nom sans extension (déjà sûr)
 *   ext  : extension avec son point
 * Retourne : chemin à libérer avec g_free () */
static gchar *
export_unique_path (ExportContext *ctx,
		    const gchar *dir,
		    const gchar *name,
		    const gchar *ext)
{
	guint suffix = 1;
	gchar *filename = g_strconcat (name, ext, NULL);
	gchar *path = g_build_filename (dir, filename, NULL);
	gchar *key = g_utf8_strdown (path, -1);

	while (g_hash_table_contains (ctx->used, key)) {
		g_free (filename);
		g_free (path);
		g_free (key);
		filename = g_strdup_printf ("%s (%u)%s", name, ++suffix, ext);
		path = g_build_filename (dir, filename, NULL);
		key = g_utf8_strdown (path, -1);
	}
	g_hash_table_add (ctx->used, key);
	g_free (filename);

	return path;
}

/* Dossier de destination d'un dossier de courrier : chaque niveau du chemin
 * complet devient un sous-dossier (noms rendus sûrs pour Windows).
 * Paramètres :
 *   dest      : dossier de destination de l'exportation
 *   full_name : chemin complet du dossier de courrier (« / » comme séparateur)
 *   leaf      : reçoit le nom sûr du dernier niveau (à libérer avec g_free)
 *   with_leaf : TRUE pour inclure le dernier niveau dans le chemin retourné
 * Retourne : chemin à libérer avec g_free () */
static gchar *
export_folder_dir (const gchar *dest,
		   const gchar *full_name,
		   gchar **leaf,
		   gboolean with_leaf)
{
	gchar **parts = g_strsplit (full_name, "/", -1);
	gchar *dir = g_strdup (dest);
	guint ii, count = g_strv_length (parts);

	*leaf = NULL;
	for (ii = 0; ii < count; ii++) {
		gchar safe[MT_MAX_NAME_BYTES + 8], *next;

		mt_safe_name (parts[ii], "Dossier", safe, sizeof (safe));
		if (ii + 1 == count) {
			*leaf = g_strdup (safe);
			if (!with_leaf)
				break;
		}
		next = g_build_filename (dir, safe, NULL);
		g_free (dir);
		dir = next;
	}
	g_strfreev (parts);
	if (!*leaf)
		*leaf = g_strdup ("Dossier");

	return dir;
}

/* Adresse de l'expéditeur d'un message, pour la ligne « From » d'un mbox.
 * Retourne : adresse à libérer avec g_free (), ou chaîne vide */
static gchar *
export_sender_address (CamelMimeMessage *message)
{
	CamelInternetAddress *from = camel_mime_message_get_from (message);
	const gchar *name = NULL, *email = NULL;

	if (from && camel_internet_address_get (from, 0, &name, &email) && email)
		return g_strdup (email);

	return g_strdup ("");
}

/* Exporte les messages d'un dossier de courrier.
 * Paramètres :
 *   core : instance du moteur
 *   st   : état (progression, compteurs)
 *   ctx  : paramètres de l'exportation
 *   fi   : dossier à exporter */
static void
export_one_folder (EvoCore *core,
		   TransferState *st,
		   ExportContext *ctx,
		   CamelFolderInfo *fi)
{
	CamelFolder *folder;
	GPtrArray *uids;
	GError *local_error = NULL;
	gchar *leaf, *dir, *mbox_path = NULL;
	FILE *mbox_file = NULL;
	guint ii;

	if ((fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)) != 0)
		return;

	g_free (st->label);
	st->label = g_strdup (fi->display_name ? fi->display_name : fi->full_name);
	transfer_emit_progress (st, TRUE, TRUE);

	folder = core_ref_folder (core, st->account, fi->full_name, &local_error);
	if (!folder) {
		transfer_fail (st, fi->full_name, local_error);
		return;
	}
	camel_folder_refresh_info_sync (folder, st->cancel, NULL);

	/* un fichier .mbox par dossier ou un sous-dossier de fichiers .eml */
	dir = export_folder_dir (ctx->dest, fi->full_name, &leaf, !ctx->mbox);
	if (g_mkdir_with_parents (dir, 0700) != 0) {
		local_error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED, "Impossible de créer le dossier « %s »", dir);
		transfer_fail (st, fi->full_name, local_error);
		goto exit;
	}
	if (ctx->mbox) {
		mbox_path = export_unique_path (ctx, dir, leaf, ".mbox");
		mbox_file = g_fopen (mbox_path, "wb");
		if (!mbox_file) {
			local_error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED, "Impossible d'écrire « %s »", mbox_path);
			transfer_fail (st, fi->full_name, local_error);
			goto exit;
		}
	}
	st->folders++;

	uids = camel_folder_dup_uids (folder);
	for (ii = 0; uids && ii < uids->len; ii++) {
		CamelMessageInfo *mi;
		CamelMimeMessage *message;
		gint64 date;
		gchar *data = NULL;
		gsize size = 0;

		if (transfer_is_cancelled (st))
			break;

		st->done++;
		mi = camel_folder_get_message_info (folder, uids->pdata[ii]);
		if (!mi)
			continue;

		date = camel_message_info_get_date_received (mi);
		if (date <= 0)
			date = camel_message_info_get_date_sent (mi);
		if ((camel_message_info_get_flags (mi) & CAMEL_MESSAGE_DELETED) != 0 ||
		    !mt_in_range (date, ctx->date_from, ctx->date_to)) {
			st->skipped++;
			g_object_unref (mi);
			continue;
		}

		message = camel_folder_get_message_sync (folder, uids->pdata[ii], st->cancel, &local_error);
		if (message && transfer_message_bytes (message, st->cancel, &data, &size)) {
			if (mbox_file) {
				gchar *sender = export_sender_address (message), from_line[400];
				size_t qlen = 0;
				gchar *quoted = mt_mbox_quote (data, size, &qlen);

				mt_from_line (sender, date, from_line, sizeof (from_line));
				if (!quoted || fputs (from_line, mbox_file) < 0 || fwrite (quoted, 1, qlen, mbox_file) != qlen)
					local_error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED, "Écriture impossible dans « %s »", mbox_path);
				else
					st->messages++;
				free (quoted);
				g_free (sender);
			} else {
				gchar safe[MT_MAX_NAME_BYTES + 8], *path;

				mt_safe_name (camel_mime_message_get_subject (message), "Message", safe, sizeof (safe));
				path = export_unique_path (ctx, dir, safe, ".eml");
				if (g_file_set_contents (path, data, (gssize) size, &local_error))
					st->messages++;
				g_free (path);
			}
		} else if (message && !local_error) {
			local_error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, "Message illisible");
		}

		if (local_error) {
			gchar *context = g_strdup_printf ("%s (%s)", fi->full_name, (const gchar *) uids->pdata[ii]);

			transfer_fail (st, context, local_error);
			local_error = NULL;
			g_free (context);
		}

		g_free (data);
		g_clear_object (&message);
		g_object_unref (mi);
		transfer_emit_progress (st, TRUE, FALSE);
	}
	if (uids)
		g_ptr_array_unref (uids);

 exit:
	if (mbox_file)
		fclose (mbox_file);
	g_free (mbox_path);
	g_free (leaf);
	g_free (dir);
	g_object_unref (folder);
}

/* Ajoute un dossier (et ses sous-dossiers si recursive) à la liste des dossiers
 * à exporter, sans doublon.
 * Paramètres :
 *   list      : liste de CamelFolderInfo *
 *   seen      : noms complets déjà ajoutés
 *   fi        : dossier à ajouter
 *   recursive : TRUE pour inclure les sous-dossiers
 *   total     : somme des messages annoncés, mise à jour */
static void
export_collect_folders (GPtrArray *list,
			GHashTable *seen,
			CamelFolderInfo *fi,
			gboolean recursive,
			guint64 *total)
{
	CamelFolderInfo *child;

	if (!g_hash_table_contains (seen, fi->full_name)) {
		g_hash_table_add (seen, g_strdup (fi->full_name));
		g_ptr_array_add (list, fi);
		if (fi->total > 0 && (fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)) == 0)
			*total += (guint64) fi->total;
	}
	for (child = recursive ? fi->child : NULL; child; child = child->next)
		export_collect_folders (list, seen, child, TRUE, total);
}

/* Exporte des dossiers de courrier vers des fichiers mbox ou EML.
 * Paramètres :
 *   core  : instance du moteur
 *   args  : [0] = JSON {"account", "folders": ["INBOX", ...], "recursive": bool,
 *           "dest": "C:\\...", "format": "mbox"|"eml", "date_from": s, "date_to": s}
 *   error : emplacement d'erreur
 * Retourne : résultat JSON (voir transfer_result_json), ou NULL en cas d'erreur */
static gchar *
core_export_mail_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	JsonNode *root;
	JsonObject *object;
	JsonArray *folders;
	TransferState st;
	ExportContext ctx = { NULL, FALSE, 0, 0, NULL };
	CamelStore *store = NULL;
	CamelFolderInfo *tree = NULL;
	GPtrArray *list = NULL;
	GHashTable *seen = NULL;
	const gchar *account, *format;
	gboolean recursive;
	gchar *json = NULL;
	guint ii;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account = object ? json_object_get_string_member_with_default (object, "account", "") : "";
	format = object ? json_object_get_string_member_with_default (object, "format", "mbox") : "mbox";
	recursive = object ? json_object_get_boolean_member_with_default (object, "recursive", FALSE) : FALSE;
	folders = (object && json_object_has_member (object, "folders")) ? json_object_get_array_member (object, "folders") : NULL;
	ctx.dest = g_strdup (object ? json_object_get_string_member_with_default (object, "dest", "") : "");
	ctx.mbox = g_strcmp0 (format, "eml") != 0;
	ctx.date_from = object ? json_object_get_int_member_with_default (object, "date_from", 0) : 0;
	ctx.date_to = object ? json_object_get_int_member_with_default (object, "date_to", 0) : 0;
	ctx.used = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	transfer_state_init (&st, core, "export", account);

	if (!*account || !folders || json_array_get_length (folders) == 0 || !*ctx.dest) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Compte, dossiers ou destination d'exportation manquants");
		goto exit;
	}
	if (g_mkdir_with_parents (ctx.dest, 0700) != 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Impossible de créer le dossier « %s »", ctx.dest);
		goto exit;
	}

	store = core_ref_store (core, account, error);
	if (!store || !core_ensure_connected (core, store, error))
		goto exit;
	tree = camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST, st.cancel, error);
	if (!tree)
		goto exit;

	list = g_ptr_array_new ();
	seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (ii = 0; ii < json_array_get_length (folders); ii++) {
		const gchar *name = json_array_get_string_element (folders, ii);
		CamelFolderInfo *fi = name ? find_folder_info (tree, name) : NULL;

		if (!fi) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Dossier introuvable : « %s ».", name ? name : "");
			goto exit;
		}
		export_collect_folders (list, seen, fi, recursive, &st.total);
	}

	transfer_emit_progress (&st, TRUE, TRUE);
	for (ii = 0; ii < list->len && !transfer_is_cancelled (&st); ii++)
		export_one_folder (core, &st, &ctx, list->pdata[ii]);
	transfer_emit_progress (&st, FALSE, TRUE);

	json = transfer_result_json (&st, ctx.dest);

 exit:
	if (seen)
		g_hash_table_unref (seen);
	if (list)
		g_ptr_array_unref (list);
	camel_folder_info_free (tree);
	g_clear_object (&store);
	g_hash_table_unref (ctx.used);
	g_free (ctx.dest);
	transfer_state_clear (&st);
	json_node_unref (root);

	return json;
}

/* Exporte des dossiers de courrier (avec ou sans leurs sous-dossiers) vers un
 * dossier de l'ordinateur, en arrière-plan, avec progression (événements
 * « progress » de type « export ») et annulation (evo_core_cancel_operations).
 * Format de request_json :
 * {"account": "...", "folders": ["INBOX", "INBOX/Projets"], "recursive": true,
 *  "dest": "C:\\...", "format": "mbox" ou "eml", "date_from": 0, "date_to": 0}
 * « mbox » écrit un fichier <dossier>.mbox par dossier de courrier, dans une
 * arborescence de dossiers reprenant celle du compte ; « eml » écrit un fichier
 * .eml par message dans un dossier par dossier de courrier. « date_from » et
 * « date_to » (secondes depuis 1970, 0 : sans borne, bornes incluses) limitent
 * l'exportation aux messages reçus pendant la période.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : requête au format JSON UTF-8
 *   func         : rappel recevant {"messages", "folders", "skipped", "failed",
 *                  "cancelled", "dest", "error"}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_export_mail (EvoCore *core,
		      const char *request_json,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	core_submit_background (core, core_export_mail_run, g_strdupv (args), func, user_data);
}

/* ------------------------------------------------------------------ */
/* Importation du courrier                                              */
/* ------------------------------------------------------------------ */

/* Contexte d'une importation de courrier. */
typedef struct {
	TransferState *st;
	CamelStore *store;
	GHashTable *known;	/* dossiers déjà vérifiés ou créés (noms complets) */
	GHashTable *appended;	/* dossiers où des messages ont été ajoutés (à synchroniser) */
	guint since_sync;
} ImportContext;

/* Joint un nom de dossier à son parent ("" : premier niveau). */
static gchar *
import_join (const gchar *parent,
	     const gchar *name)
{
	return parent && *parent ? g_strconcat (parent, "/", name, NULL) : g_strdup (name);
}

/* Vérifie qu'un dossier existe dans le compte et le crée sinon, avec ses
 * parents manquants (et s'y abonne).
 * Paramètres :
 *   ctx       : contexte d'importation
 *   full_name : chemin complet (« / » comme séparateur)
 *   error     : emplacement d'erreur
 * Retourne : TRUE si le dossier existe à la fin */
static gboolean
import_ensure_folder (ImportContext *ctx,
		      const gchar *full_name,
		      GError **error)
{
	gchar **parts;
	gchar *prefix = NULL;
	guint ii, count;
	gboolean ok = TRUE;

	if (g_hash_table_contains (ctx->known, full_name))
		return TRUE;

	parts = g_strsplit (full_name, "/", -1);
	count = g_strv_length (parts);
	for (ii = 0; ok && ii < count; ii++) {
		gchar *next = import_join (prefix, parts[ii]);
		CamelFolder *existing;

		g_free (prefix);
		prefix = next;
		if (g_hash_table_contains (ctx->known, prefix))
			continue;

		existing = camel_store_get_folder_sync (ctx->store, prefix, 0, ctx->st->cancel, NULL);
		if (existing) {
			g_object_unref (existing);
		} else {
			CamelFolderInfo *created;
			gchar *parent_path = NULL;

			/* parent : le chemin sans son dernier niveau (NULL au premier niveau) */
			if (ii > 0) {
				gchar *slash;

				parent_path = g_strdup (prefix);
				slash = strrchr (parent_path, '/');
				if (slash)
					*slash = '\0';
			}

			created = camel_store_create_folder_sync (ctx->store, parent_path, parts[ii], ctx->st->cancel, error);
			g_free (parent_path);
			if (!created) {
				ok = FALSE;
				break;
			}
			camel_folder_info_free (created);
			core_subscribe_new_folder (ctx->st->core, ctx->store, prefix);
			ctx->st->folders++;
		}
		g_hash_table_add (ctx->known, g_strdup (prefix));
	}
	g_free (prefix);
	g_strfreev (parts);

	return ok;
}

/* Ajoute un message à un dossier.
 * Paramètres :
 *   ctx    : contexte d'importation
 *   folder : dossier de destination
 *   data   : message (RFC 822)
 *   len    : taille du message
 *   flags  : indicateurs MT_FLAG_*
 *   where  : origine du message (fichier, dossier) pour les erreurs
 * Retourne : TRUE si le message a été ajouté */
static gboolean
import_append_message (ImportContext *ctx,
		       CamelFolder *folder,
		       const gchar *data,
		       gsize len,
		       gint flags,
		       const gchar *where)
{
	CamelStream *stream = camel_stream_mem_new ();
	CamelMimeMessage *message = camel_mime_message_new ();
	CamelMessageInfo *info;
	GError *local_error = NULL;
	guint32 set = 0;
	gboolean ok = FALSE;

	camel_stream_mem_set_buffer (CAMEL_STREAM_MEM (stream), data, len);
	if (!camel_data_wrapper_construct_from_stream_sync (CAMEL_DATA_WRAPPER (message), stream, ctx->st->cancel, &local_error)) {
		transfer_fail (ctx->st, where, local_error);
		goto exit;
	}

	if (flags & MT_FLAG_SEEN)
		set |= CAMEL_MESSAGE_SEEN;
	if (flags & MT_FLAG_REPLIED)
		set |= CAMEL_MESSAGE_ANSWERED;
	if (flags & MT_FLAG_FLAGGED)
		set |= CAMEL_MESSAGE_FLAGGED;
	if (flags & MT_FLAG_DRAFT)
		set |= CAMEL_MESSAGE_DRAFT;

	info = camel_message_info_new (NULL);
	camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_DRAFT, set);
	if (camel_folder_append_message_sync (folder, message, info, NULL, ctx->st->cancel, &local_error)) {
		ok = TRUE;
		ctx->st->messages++;
		if (++ctx->since_sync >= 100) {
			camel_folder_synchronize_sync (folder, FALSE, ctx->st->cancel, NULL);
			ctx->since_sync = 0;
		}
	} else {
		transfer_fail (ctx->st, where, local_error);
	}
	g_object_unref (info);

 exit:
	g_object_unref (message);
	g_object_unref (stream);

	return ok;
}

/* Ouvre un dossier de destination (créé au besoin) et le mémorise pour la
 * synchronisation finale.
 * Retourne : le dossier (à libérer), ou NULL (erreur comptée dans l'état) */
static CamelFolder *
import_open_folder (ImportContext *ctx,
		    const gchar *full_name)
{
	GError *local_error = NULL;
	CamelFolder *folder;

	if (!import_ensure_folder (ctx, full_name, &local_error)) {
		transfer_fail (ctx->st, full_name, local_error);
		return NULL;
	}
	folder = core_ref_folder (ctx->st->core, ctx->st->account, full_name, &local_error);
	if (!folder) {
		transfer_fail (ctx->st, full_name, local_error);
		return NULL;
	}
	if (!g_hash_table_contains (ctx->appended, full_name))
		g_hash_table_add (ctx->appended, g_strdup (full_name));
	g_free (ctx->st->label);
	ctx->st->label = g_strdup (full_name);

	return folder;
}

/* Importe un fichier mbox dans un dossier.
 * Paramètres :
 *   ctx    : contexte d'importation
 *   path   : fichier mbox
 *   folder : chemin complet du dossier de destination */
static void
import_mbox_file (ImportContext *ctx,
		  const gchar *path,
		  const gchar *folder_name)
{
	TransferState *st = ctx->st;
	GMappedFile *map;
	GError *local_error = NULL;
	CamelFolder *folder;
	MtMboxReader reader;
	const gchar *start;
	gsize len;

	map = g_mapped_file_new (path, FALSE, &local_error);
	if (!map) {
		transfer_fail (st, path, local_error);
		return;
	}
	folder = import_open_folder (ctx, folder_name);
	if (!folder) {
		g_mapped_file_unref (map);
		return;
	}

	mt_mbox_reader_init (&reader, g_mapped_file_get_contents (map), g_mapped_file_get_length (map));
	while (!transfer_is_cancelled (st) && mt_mbox_next (&reader, &start, &len)) {
		size_t ulen = 0;
		gchar *msg = mt_mbox_unquote (start, len, &ulen);
		gint flags;

		st->done += len;
		if (!msg) {
			transfer_fail (st, path, NULL);
			continue;
		}
		flags = mt_mozilla_flags (msg, ulen);
		if (flags & MT_FLAG_DELETED)
			st->skipped++;
		else
			import_append_message (ctx, folder, msg, ulen, flags, path);
		free (msg);
		transfer_emit_progress (st, TRUE, FALSE);
	}

	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);
	ctx->since_sync = 0;
	g_object_unref (folder);
	g_mapped_file_unref (map);
}

/* Importe un fichier de message (.eml ou fichier Maildir) dans un dossier.
 * Paramètres :
 *   ctx         : contexte d'importation
 *   path        : fichier
 *   folder      : dossier de destination déjà ouvert
 *   maildir_new : TRUE si le fichier vient du sous-dossier « new » d'un Maildir
 *   maildir     : TRUE pour lire les indicateurs dans le nom du fichier */
static void
import_message_file (ImportContext *ctx,
		     const gchar *path,
		     CamelFolder *folder,
		     gboolean maildir,
		     gboolean maildir_new)
{
	TransferState *st = ctx->st;
	GMappedFile *map;
	GError *local_error = NULL;
	gint flags = MT_FLAG_SEEN;
	gsize length;

	map = g_mapped_file_new (path, FALSE, &local_error);
	if (!map) {
		/* un fichier vide ne peut pas être projeté en mémoire */
		if (g_file_test (path, G_FILE_TEST_IS_REGULAR) && g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_INVAL)) {
			g_clear_error (&local_error);
			st->skipped++;
			return;
		}
		transfer_fail (st, path, local_error);
		return;
	}

	length = g_mapped_file_get_length (map);
	if (maildir) {
		gchar *base = g_path_get_basename (path);

		flags = mt_maildir_flags (base, maildir_new);
		g_free (base);
	}
	st->done += length;
	if (flags & MT_FLAG_DELETED)
		st->skipped++;
	else
		import_append_message (ctx, folder, g_mapped_file_get_contents (map), length, flags, path);
	g_mapped_file_unref (map);
	transfer_emit_progress (st, TRUE, FALSE);
}

/* Liste triée des noms d'un répertoire (sans « . » ni « .. »).
 * Retourne : tableau de chaînes (à libérer avec g_ptr_array_unref) */
static GPtrArray *
import_list_dir (const gchar *dir)
{
	GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
	GDir *handle = g_dir_open (dir, 0, NULL);
	const gchar *name;

	while (handle && (name = g_dir_read_name (handle)) != NULL)
		g_ptr_array_add (names, g_strdup (name));
	if (handle)
		g_dir_close (handle);
	g_ptr_array_sort (names, (GCompareFunc) strcmp);

	return names;
}

/* Lit les premiers octets d'un fichier pour savoir s'il s'agit d'un mbox. */
static gboolean
import_file_is_mbox (const gchar *path)
{
	gchar head[512];
	FILE *file = g_fopen (path, "rb");
	size_t got;

	if (!file)
		return FALSE;
	got = fread (head, 1, sizeof (head), file);
	fclose (file);

	return got > 0 && mt_looks_like_mbox (head, got);
}

/* Octets à importer sous un chemin (pour la progression). */
static guint64
import_count_bytes (const gchar *path)
{
	guint64 total = 0;
	GStatBuf st;

	if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
		GPtrArray *names = import_list_dir (path);
		guint ii;

		for (ii = 0; ii < names->len; ii++) {
			gchar *child = g_build_filename (path, names->pdata[ii], NULL);

			if (!mt_is_aux_file (names->pdata[ii]) || g_file_test (child, G_FILE_TEST_IS_DIR) ||
			    mt_has_ext (names->pdata[ii], ".sbd"))
				total += import_count_bytes (child);
			g_free (child);
		}
		g_ptr_array_unref (names);
	} else if (g_stat (path, &st) == 0) {
		total = (guint64) st.st_size;
	}

	return total;
}

static void
import_dir (ImportContext *ctx,
	    const gchar *dir,
	    const gchar *folder_name);

/* Importe un répertoire Maildir (sous-dossiers cur et new) dans un dossier. */
static void
import_maildir (ImportContext *ctx,
		const gchar *dir,
		const gchar *folder_name)
{
	static const gchar *const subdirs[] = { "cur", "new", NULL };
	CamelFolder *folder = import_open_folder (ctx, folder_name);
	guint ii;

	if (!folder)
		return;

	for (ii = 0; subdirs[ii] && !transfer_is_cancelled (ctx->st); ii++) {
		gchar *sub = g_build_filename (dir, subdirs[ii], NULL);
		GPtrArray *names = import_list_dir (sub);
		guint jj;

		for (jj = 0; jj < names->len && !transfer_is_cancelled (ctx->st); jj++) {
			gchar *path = g_build_filename (sub, names->pdata[jj], NULL);

			if (((const gchar *) names->pdata[jj])[0] != '.' && g_file_test (path, G_FILE_TEST_IS_REGULAR))
				import_message_file (ctx, path, folder, TRUE, ii == 1);
			g_free (path);
		}
		g_ptr_array_unref (names);
		g_free (sub);
	}

	camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);
	ctx->since_sync = 0;
	g_object_unref (folder);
}

/* Importe l'arborescence d'un répertoire dans un dossier du compte :
 *  - un fichier mbox (extension .mbox / .mbx, ou fichier sans extension commençant
 *    par « From », comme ceux de Thunderbird) devient un dossier du nom du fichier ;
 *  - le répertoire « X.sbd » (Thunderbird) ou « X » placé à côté fournit les
 *    sous-dossiers de ce dossier ;
 *  - les fichiers .eml du répertoire vont dans le dossier courant ;
 *  - un répertoire Maildir (cur / new) devient un dossier, ses sous-dossiers
 *    « .A.B » (Maildir++) des sous-dossiers ;
 *  - tout autre répertoire devient un sous-dossier.
 * Les index et fichiers auxiliaires de Thunderbird (.msf, .dat…) sont ignorés.
 * Paramètres :
 *   ctx         : contexte d'importation
 *   dir         : répertoire à importer
 *   folder_name : dossier de destination (chemin complet) */
static void
import_dir (ImportContext *ctx,
	    const gchar *dir,
	    const gchar *folder_name)
{
	GPtrArray *names = import_list_dir (dir);
	GHashTable *consumed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	CamelFolder *own = NULL;
	gchar *cur = g_build_filename (dir, "cur", NULL), *neu = g_build_filename (dir, "new", NULL);
	gboolean maildir = g_file_test (cur, G_FILE_TEST_IS_DIR) || g_file_test (neu, G_FILE_TEST_IS_DIR);
	guint ii;

	g_free (cur);
	g_free (neu);

	if (maildir) {
		import_maildir (ctx, dir, folder_name);
		g_hash_table_add (consumed, g_strdup ("cur"));
		g_hash_table_add (consumed, g_strdup ("new"));
		g_hash_table_add (consumed, g_strdup ("tmp"));
	}

	/* premier passage : fichiers (messages, mbox) */
	for (ii = 0; ii < names->len && !transfer_is_cancelled (ctx->st); ii++) {
		const gchar *name = names->pdata[ii];
		gchar *path = g_build_filename (dir, name, NULL);

		if (!g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
			g_free (path);
			continue;
		}

		if (mt_has_ext (name, ".eml")) {
			if (!own)
				own = import_open_folder (ctx, folder_name);
			if (own)
				import_message_file (ctx, path, own, FALSE, FALSE);
		} else if (mt_has_ext (name, ".mbox") || mt_has_ext (name, ".mbx") ||
			   (!mt_is_aux_file (name) && import_file_is_mbox (path))) {
			gchar base[MT_MAX_NAME_BYTES + 8], safe[MT_MAX_NAME_BYTES + 8], *sub, *sbd, *plain;

			mt_strip_ext (name, base, sizeof (base));
			if (!mt_has_ext (name, ".mbox") && !mt_has_ext (name, ".mbx"))
				g_strlcpy (base, name, sizeof (base));
			mt_safe_name (base, "Dossier", safe, sizeof (safe));
			sub = import_join (folder_name, safe);
			import_mbox_file (ctx, path, sub);

			/* sous-dossiers : « X.sbd » (Thunderbird) ou « X » à côté du fichier */
			sbd = g_strconcat (base, ".sbd", NULL);
			plain = g_strdup (base);
			if (g_ptr_array_find_with_equal_func (names, sbd, g_str_equal, NULL) &&
			    !g_hash_table_contains (consumed, sbd)) {
				gchar *subdir = g_build_filename (dir, sbd, NULL);

				g_hash_table_add (consumed, g_strdup (sbd));
				if (g_file_test (subdir, G_FILE_TEST_IS_DIR))
					import_dir (ctx, subdir, sub);
				g_free (subdir);
			}
			if (g_ptr_array_find_with_equal_func (names, plain, g_str_equal, NULL) &&
			    !g_hash_table_contains (consumed, plain)) {
				gchar *subdir = g_build_filename (dir, plain, NULL);

				if (g_file_test (subdir, G_FILE_TEST_IS_DIR)) {
					g_hash_table_add (consumed, g_strdup (plain));
					import_dir (ctx, subdir, sub);
				}
				g_free (subdir);
			}
			g_free (sbd);
			g_free (plain);
			g_free (sub);
		}
		g_free (path);
	}

	/* second passage : répertoires non encore traités */
	for (ii = 0; ii < names->len && !transfer_is_cancelled (ctx->st); ii++) {
		const gchar *name = names->pdata[ii];
		gchar *path = g_build_filename (dir, name, NULL);

		if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_hash_table_contains (consumed, name)) {
			gchar fname[MT_MAX_NAME_BYTES * 2 + 8], *sub;

			if (name[0] == '.' && maildir) {
				/* Maildir++ : « .A.B » est le dossier A/B */
				gchar hier[MT_MAX_NAME_BYTES * 2 + 8];

				mt_maildirpp_path (name, hier, sizeof (hier));
				sub = import_join (folder_name, hier);
				import_dir (ctx, path, sub);
				g_free (sub);
			} else if (name[0] != '.') {
				mt_tb_folder_name (name, fname, sizeof (fname));
				{
					gchar safe[MT_MAX_NAME_BYTES + 8];

					mt_safe_name (fname, "Dossier", safe, sizeof (safe));
					sub = import_join (folder_name, safe);
				}
				import_dir (ctx, path, sub);
				g_free (sub);
			}
		}
		g_free (path);
	}

	if (own) {
		camel_folder_synchronize_sync (own, FALSE, NULL, NULL);
		ctx->since_sync = 0;
		g_object_unref (own);
	}
	g_hash_table_unref (consumed);
	g_ptr_array_unref (names);
}

/* Importe du courrier d'un fichier mbox ou .eml, d'un répertoire de fichiers
 * .eml, d'un Maildir ou d'un dossier de profil Thunderbird.
 * Paramètres :
 *   core  : instance du moteur
 *   args  : [0] = JSON {"account", "dest_folder", "path"}
 *   error : emplacement d'erreur
 * Retourne : résultat JSON, ou NULL en cas d'erreur de préparation */
static gchar *
core_import_mail_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	JsonNode *root;
	JsonObject *object;
	TransferState st;
	ImportContext ctx = { &st, NULL, NULL, NULL, 0 };
	const gchar *account, *dest_folder, *path;
	gchar *json = NULL;
	GHashTableIter iter;
	gpointer key;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account = object ? json_object_get_string_member_with_default (object, "account", "") : "";
	dest_folder = object ? json_object_get_string_member_with_default (object, "dest_folder", "") : "";
	path = object ? json_object_get_string_member_with_default (object, "path", "") : "";

	transfer_state_init (&st, core, "import", account);

	if (!*account || !*dest_folder || !*path) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Compte, dossier de destination ou source d'importation manquants");
		goto exit;
	}
	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Source introuvable : « %s ».", path);
		goto exit;
	}

	ctx.store = core_ref_store (core, account, error);
	if (!ctx.store || !core_ensure_connected (core, ctx.store, error))
		goto exit;
	ctx.known = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	ctx.appended = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	st.total = import_count_bytes (path);
	transfer_emit_progress (&st, TRUE, TRUE);

	if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
		import_dir (&ctx, path, dest_folder);
	} else if (mt_has_ext (path, ".eml")) {
		CamelFolder *folder = import_open_folder (&ctx, dest_folder);

		if (folder) {
			import_message_file (&ctx, path, folder, FALSE, FALSE);
			camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);
			g_object_unref (folder);
		}
	} else if (import_file_is_mbox (path) || mt_has_ext (path, ".mbox") || mt_has_ext (path, ".mbx")) {
		import_mbox_file (&ctx, path, dest_folder);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Format non reconnu : « %s » n'est ni un fichier mbox ni un message .eml.", path);
		goto exit;
	}

	/* enregistre les messages sur le serveur */
	g_hash_table_iter_init (&iter, ctx.appended);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		CamelFolder *folder = core_ref_folder (core, account, key, NULL);

		if (folder) {
			camel_folder_synchronize_sync (folder, FALSE, NULL, NULL);
			g_object_unref (folder);
		}
	}
	core_emit_folders_updated (core, account);
	transfer_emit_progress (&st, FALSE, TRUE);
	json = transfer_result_json (&st, NULL);

 exit:
	if (ctx.known)
		g_hash_table_unref (ctx.known);
	if (ctx.appended)
		g_hash_table_unref (ctx.appended);
	g_clear_object (&ctx.store);
	transfer_state_clear (&st);
	json_node_unref (root);

	return json;
}

/* Importe du courrier depuis un fichier mbox, un message .eml, un répertoire
 * (arborescence de fichiers mbox / .eml, Maildir, dossier de courrier d'un
 * profil Thunderbird) vers un dossier d'un compte, en arrière-plan, avec
 * progression (événements « progress » de type « import ») et annulation
 * (evo_core_cancel_operations). Les dossiers manquants sont créés. Les
 * messages importés sont marqués lus, sauf ceux dont l'indicateur d'origine
 * (X-Mozilla-Status, suffixe Maildir) dit le contraire ; ceux marqués
 * supprimés sont ignorés.
 * Format de request_json :
 * {"account": "...", "dest_folder": "Importé", "path": "C:\\..."}
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : requête au format JSON UTF-8
 *   func         : rappel recevant {"messages", "folders", "skipped", "failed",
 *                  "cancelled", "error"}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_import_mail (EvoCore *core,
		      const char *request_json,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	core_submit_background (core, core_import_mail_run, g_strdupv (args), func, user_data);
}

/* ------------------------------------------------------------------ */
/* Importation de contacts (vCard) et d'agendas (iCalendar)             */
/* ------------------------------------------------------------------ */

/* Lit un fichier texte en UTF-8 (BOM retiré ; Windows-1252 converti si le
 * contenu n'est pas de l'UTF-8 valide).
 * Retourne : texte à libérer avec g_free (), ou NULL en cas d'erreur */
static gchar *
import_read_text (const gchar *path,
		  gsize *length,
		  GError **error)
{
	gchar *text = NULL;
	gsize len = 0;

	if (!g_file_get_contents (path, &text, &len, error))
		return NULL;

	if (len >= 3 && (guchar) text[0] == 0xEF && (guchar) text[1] == 0xBB && (guchar) text[2] == 0xBF) {
		memmove (text, text + 3, len - 2);
		len -= 3;
	}
	if (!g_utf8_validate (text, (gssize) len, NULL)) {
		gsize written = 0;
		gchar *converted = g_convert (text, (gssize) len, "UTF-8", "CP1252", NULL, &written, NULL);

		if (converted) {
			g_free (text);
			text = converted;
			len = written;
		}
	}
	*length = len;

	return text;
}

/* Importe des contacts d'un fichier .vcf dans un carnet d'adresses.
 * Paramètres :
 *   core  : instance du moteur
 *   args  : [0] = JSON {"book_uid", "path"}
 *   error : emplacement d'erreur
 * Retourne : {"count", "skipped", "failed", "cancelled", "error"} ou NULL */
static gchar *
core_import_contacts_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	JsonNode *root;
	JsonObject *object;
	TransferState st;
	const gchar *book_uid, *path;
	ESource *source = NULL;
	EClient *client = NULL;
	gchar *text = NULL, *json = NULL;
	gsize length = 0, pos = 0, vlen;
	const gchar *start;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	book_uid = object ? json_object_get_string_member_with_default (object, "book_uid", "") : "";
	path = object ? json_object_get_string_member_with_default (object, "path", "") : "";
	transfer_state_init (&st, core, "import", "");

	if (!*book_uid || !*path) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Carnet d'adresses ou fichier manquant");
		goto exit;
	}
	source = e_source_registry_ref_source (core->registry, book_uid);
	if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Carnet d'adresses introuvable");
		goto exit;
	}
	text = import_read_text (path, &length, error);
	if (!text)
		goto exit;
	client = e_book_client_connect_sync (source, 5, st.cancel, error);
	if (!client)
		goto exit;

	st.total = length;
	g_free (st.label);
	st.label = g_strdup (e_source_get_display_name (source));
	transfer_emit_progress (&st, TRUE, TRUE);

	while (!transfer_is_cancelled (&st) && mt_vcard_next (text, length, &pos, &start, &vlen)) {
		gchar *card = g_strndup (start, vlen);
		EContact *contact = e_contact_new_from_vcard (card);
		GError *local_error = NULL;

		st.done = pos;
		if (!contact) {
			transfer_fail (&st, path, NULL);
		} else if (e_book_client_add_contact_sync (E_BOOK_CLIENT (client), contact, E_BOOK_OPERATION_FLAG_NONE,
			   NULL, st.cancel, &local_error)) {
			st.messages++;
		} else if (g_error_matches (local_error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS)) {
			st.skipped++;
			g_clear_error (&local_error);
		} else {
			transfer_fail (&st, e_contact_get_const (contact, E_CONTACT_FULL_NAME) ?
				e_contact_get_const (contact, E_CONTACT_FULL_NAME) : path, local_error);
		}
		g_clear_object (&contact);
		g_free (card);
		transfer_emit_progress (&st, TRUE, FALSE);
	}
	transfer_emit_progress (&st, FALSE, TRUE);

	{
		JsonBuilder *builder = json_builder_new ();

		json_builder_begin_object (builder);
		builder_add_int (builder, "count", st.messages);
		builder_add_int (builder, "skipped", st.skipped);
		builder_add_int (builder, "failed", st.failed);
		builder_add_boolean (builder, "cancelled", st.cancelled);
		if (st.first_error)
			builder_add_string (builder, "error", st.first_error);
		json_builder_end_object (builder);
		json = json_from_builder (builder);
		g_object_unref (builder);
	}

 exit:
	g_free (text);
	g_clear_object (&client);
	g_clear_object (&source);
	transfer_state_clear (&st);
	json_node_unref (root);

	return json;
}

/* Importe des composants d'un fichier iCalendar dans un agenda, une liste de
 * tâches ou une liste de mémos.
 * Paramètres :
 *   core  : instance du moteur
 *   args  : [0] = JSON {"source_uid", "path"}
 *   error : emplacement d'erreur
 * Retourne : {"count", "skipped", "failed", "cancelled", "error"} ou NULL */
static gchar *
core_import_calendar_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	JsonNode *root;
	JsonObject *object;
	TransferState st;
	const gchar *source_uid, *path;
	ESource *source = NULL;
	EClient *client = NULL;
	ECalClientSourceType source_type;
	ICalComponentKind wanted;
	ICalComponent *vcalendar = NULL, *comp;
	gchar *text = NULL, *json = NULL;
	gsize length = 0;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	source_uid = object ? json_object_get_string_member_with_default (object, "source_uid", "") : "";
	path = object ? json_object_get_string_member_with_default (object, "path", "") : "";
	transfer_state_init (&st, core, "import", "");

	if (!*source_uid || !*path) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Agenda ou fichier manquant");
		goto exit;
	}
	source = e_source_registry_ref_source (core->registry, source_uid);
	if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR)) {
		source_type = E_CAL_CLIENT_SOURCE_TYPE_EVENTS;
		wanted = I_CAL_VEVENT_COMPONENT;
	} else if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST)) {
		source_type = E_CAL_CLIENT_SOURCE_TYPE_TASKS;
		wanted = I_CAL_VTODO_COMPONENT;
	} else if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_MEMO_LIST)) {
		source_type = E_CAL_CLIENT_SOURCE_TYPE_MEMOS;
		wanted = I_CAL_VJOURNAL_COMPONENT;
	} else {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Agenda ou liste de tâches introuvable");
		goto exit;
	}

	text = import_read_text (path, &length, error);
	if (!text)
		goto exit;
	vcalendar = i_cal_component_new_from_string (text);
	if (!vcalendar || i_cal_component_isa (vcalendar) != I_CAL_VCALENDAR_COMPONENT) {
		g_clear_object (&vcalendar);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "« %s » n'est pas un fichier iCalendar valide.", path);
		goto exit;
	}

	client = e_cal_client_connect_sync (source, source_type, 5, st.cancel, error);
	if (!client)
		goto exit;

	g_free (st.label);
	st.label = g_strdup (e_source_get_display_name (source));
	for (comp = i_cal_component_get_first_component (vcalendar, I_CAL_ANY_COMPONENT); comp; ) {
		ICalComponent *next;

		if (i_cal_component_isa (comp) == wanted)
			st.total++;
		next = i_cal_component_get_next_component (vcalendar, I_CAL_ANY_COMPONENT);
		g_object_unref (comp);
		comp = next;
	}
	transfer_emit_progress (&st, TRUE, TRUE);

	/* fuseaux horaires d'abord, pour que les dates avec TZID soient comprises */
	for (comp = i_cal_component_get_first_component (vcalendar, I_CAL_VTIMEZONE_COMPONENT); comp; ) {
		ICalTimezone *zone = i_cal_timezone_new ();
		ICalComponent *next;

		if (i_cal_timezone_set_component (zone, i_cal_component_clone (comp)))
			e_cal_client_add_timezone_sync (E_CAL_CLIENT (client), zone, st.cancel, NULL);
		g_object_unref (zone);
		next = i_cal_component_get_next_component (vcalendar, I_CAL_VTIMEZONE_COMPONENT);
		g_object_unref (comp);
		comp = next;
	}

	for (comp = i_cal_component_get_first_component (vcalendar, wanted); comp && !transfer_is_cancelled (&st); ) {
		ICalComponent *next;
		GError *local_error = NULL;
		const gchar *uid = i_cal_component_get_uid (comp);

		if (!uid || !*uid) {
			gchar *generated = e_util_generate_uid ();

			i_cal_component_set_uid (comp, generated);
			g_free (generated);
		}
		if (e_cal_client_create_object_sync (E_CAL_CLIENT (client), comp, E_CAL_OPERATION_FLAG_NONE, NULL,
		    st.cancel, &local_error)) {
			st.messages++;
		} else if (g_error_matches (local_error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_ID_ALREADY_EXISTS)) {
			st.skipped++;
			g_clear_error (&local_error);
		} else {
			transfer_fail (&st, uid && *uid ? uid : path, local_error);
		}
		st.done++;
		transfer_emit_progress (&st, TRUE, FALSE);
		next = i_cal_component_get_next_component (vcalendar, wanted);
		g_object_unref (comp);
		comp = next;
	}
	transfer_emit_progress (&st, FALSE, TRUE);

	{
		JsonBuilder *builder = json_builder_new ();

		json_builder_begin_object (builder);
		builder_add_int (builder, "count", st.messages);
		builder_add_int (builder, "skipped", st.skipped);
		builder_add_int (builder, "failed", st.failed);
		builder_add_boolean (builder, "cancelled", st.cancelled);
		if (st.first_error)
			builder_add_string (builder, "error", st.first_error);
		json_builder_end_object (builder);
		json = json_from_builder (builder);
		g_object_unref (builder);
	}

 exit:
	g_free (text);
	g_clear_object (&vcalendar);
	g_clear_object (&client);
	g_clear_object (&source);
	transfer_state_clear (&st);
	json_node_unref (root);

	return json;
}

/* Importe des contacts d'un fichier vCard (.vcf, une ou plusieurs cartes) dans
 * un carnet d'adresses, en arrière-plan, avec progression (événements « progress »
 * de type « import ») et annulation. Les contacts dont l'identifiant existe déjà
 * sont ignorés. Format de request_json : {"book_uid": "...", "path": "C:\\...\\contacts.vcf"}
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : requête au format JSON UTF-8
 *   func         : rappel recevant {"count", "skipped", "failed", "cancelled", "error"}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_import_contacts (EvoCore *core,
			  const char *request_json,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	core_submit_background (core, core_import_contacts_run, g_strdupv (args), func, user_data);
}

/* Importe les événements (VEVENT), tâches (VTODO) ou mémos (VJOURNAL) d'un
 * fichier iCalendar dans un agenda, une liste de tâches ou une liste de mémos
 * (selon le type de la source), avec leurs fuseaux horaires. Les composants d'un
 * autre type et ceux dont l'UID existe déjà sont ignorés. Format de request_json :
 * {"source_uid": "...", "path": "C:\\...\\agenda.ics"}
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : requête au format JSON UTF-8
 *   func         : rappel recevant {"count", "skipped", "failed", "cancelled", "error"}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_import_calendar (EvoCore *core,
			  const char *request_json,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	core_submit_background (core, core_import_calendar_run, g_strdupv (args), func, user_data);
}
