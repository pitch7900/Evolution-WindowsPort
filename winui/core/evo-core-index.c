/*
 * evo-core : indexation plein texte locale des messages (corps et pièces jointes texte).
 *
 * Un index inversé en mémoire (evo-core-index-util.h) par dossier, alimenté en tâche de
 * fond à partir des seuls messages DÉJÀ présents sur le disque (cache Camel des dossiers
 * IMAP, dossiers locaux) : l'indexation ne télécharge jamais rien et cède la place aux
 * requêtes de l'interface (core_bg_yield ()). Chaque index est enregistré dans
 * %LOCALAPPDATA%\EvolutionWin\index\<empreinte>.idx et rechargé au démarrage.
 *
 * L'index est tenu à jour : au démarrage et toutes les dix minutes (parcours des
 * comptes connectés), et à chaque arrivée ou suppression de messages (signal "changed"
 * du dossier, voir core_index_folder_changed ()). La recherche (evo-core-search.c) s'en
 * sert via core_index_search () pour le corps des messages ; elle se rabat sur la
 * recherche Camel tant qu'un dossier n'est pas entièrement indexé.
 *
 * Règle de verrouillage : le verrou index_state.lock protège les index et la file ;
 * AUCUN appel à Camel n'est fait en le détenant (le signal "changed" de Camel prend ce
 * verrou, un appel inverse provoquerait un interblocage).
 */

#include "evo-core-private.h"
#include "evo-core-index-util.h"

/* Plafonds de texte indexé par message (octets) : le corps, chaque pièce jointe, le total */
#define INDEX_MAX_BODY_BYTES		(1024 * 1024)
#define INDEX_MAX_ATTACHMENT_BYTES	(512 * 1024)
#define INDEX_MAX_MESSAGE_BYTES		(4 * 1024 * 1024)
/* Profondeur maximale des messages imbriqués (message/rfc822 dans un message) */
#define INDEX_MAX_DEPTH			8
/* Délai avant le premier parcours après l'activation, puis période des parcours complets (secondes) */
#define INDEX_FIRST_SCAN_DELAY		20
#define INDEX_RESCAN_PERIOD		600
/* Délai de regroupement des arrivées de messages (secondes) */
#define INDEX_CHANGE_DELAY		3
/* Enregistrement intermédiaire : tous les N messages indexés d'un dossier */
#define INDEX_CHECKPOINT_MESSAGES	2000
/* Intervalle minimal entre deux événements de progression (millisecondes) */
#define INDEX_PROGRESS_INTERVAL_MS	700

#define INDEX_FILE_MAGIC		"EVIF"
#define INDEX_FILE_VERSION		1

/* Index d'un dossier */
typedef struct _IndexFolder {
	gchar *key;		/* "<compte>\x1F<dossier>" */
	gchar *account_uid;
	gchar *folder_name;
	EvoIdx *idx;		/* NULL tant que le dossier n'a pas été traité */
	gboolean complete;	/* tous les messages du dossier sont indexés */
	guint total;		/* messages du dossier au dernier passage */
	guint missing;		/* messages non indexables (corps pas encore téléchargé, fournisseur distant) */
	gboolean dirty;		/* modifié depuis le dernier enregistrement */
	gboolean queued;	/* dans la file de traitement */
	gint64 updated;		/* date (secondes Unix) du dernier passage */
	guint64 file_size;	/* taille du fichier d'index */
} IndexFolder;

/* État global (un seul moteur par processus) */
static struct {
	GMutex lock;
	gboolean enabled;		/* protégé par lock ; lu sans verrou via enabled_flag */
	gint enabled_flag;		/* copie atomique de enabled, pour les chemins rapides */
	gboolean worker_running;
	gboolean rescan_requested;	/* énumérer les dossiers de tous les comptes connectés */
	gboolean rebuild_requested;	/* effacer tous les index puis tout reconstruire */
	gboolean loaded;		/* index du disque chargés */
	gboolean kick_pending;		/* un démarrage du travailleur est programmé */
	gboolean timer_started;
	guint64 generation;		/* incrémenté à chaque désactivation ou reconstruction */
	GHashTable *folders;		/* clé -> IndexFolder * */
	GQueue queue;			/* clés (gchar *) à traiter */
	guint pass_total;		/* dossiers à traiter dans le passage en cours */
	guint pass_done;
	gchar *current;			/* dossier en cours de traitement */
	gint64 last_emit_ms;
	gint64 last_done;		/* fin du dernier passage (secondes Unix) */
} index_state;

static gsize index_state_inited = 0;

/* Initialise l'état global (une seule fois). */
static void
index_state_init (void)
{
	if (g_once_init_enter (&index_state_inited)) {
		g_mutex_init (&index_state.lock);
		g_queue_init (&index_state.queue);
		index_state.folders = g_hash_table_new (g_str_hash, g_str_equal);
		g_once_init_leave (&index_state_inited, 1);
	}
}

static void
index_folder_free (IndexFolder *f)
{
	if (!f)
		return;
	evoidx_free (f->idx);
	g_free (f->key);
	g_free (f->account_uid);
	g_free (f->folder_name);
	g_free (f);
}

/* Dossier des fichiers d'index (créé si besoin).
 * Retourne : chemin (g_free), NULL si indisponible. */
static gchar *
index_dir (void)
{
	const gchar *local_app_data = g_getenv ("LOCALAPPDATA");
	gchar *dir;

	if (!local_app_data || !*local_app_data)
		return NULL;
	dir = g_build_filename (local_app_data, "EvolutionWin", "index", NULL);
	if (g_mkdir_with_parents (dir, 0700) != 0) {
		g_free (dir);
		return NULL;
	}
	return dir;
}

/* Chemin du fichier d'index d'un dossier (empreinte SHA-1 de la clé).
 * Retourne : chemin (g_free), NULL si indisponible. */
static gchar *
index_file_path (const gchar *key)
{
	gchar *dir = index_dir (), *sum, *name, *path;

	if (!dir)
		return NULL;
	sum = g_compute_checksum_for_string (G_CHECKSUM_SHA1, key, -1);
	name = g_strconcat (sum, ".idx", NULL);
	path = g_build_filename (dir, name, NULL);
	g_free (dir);
	g_free (sum);
	g_free (name);
	return path;
}

static void
put_u32 (GByteArray *buf,
	 guint32 value)
{
	guint8 b[4] = { value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, (value >> 24) & 0xFF };

	g_byte_array_append (buf, b, 4);
}

/* Cherche ou crée l'entrée d'un dossier. Appelant : verrou détenu. */
static IndexFolder *
index_folder_ensure (const gchar *account_uid,
		     const gchar *folder_name)
{
	gchar *key = folder_key (account_uid, folder_name);
	IndexFolder *f = g_hash_table_lookup (index_state.folders, key);

	if (f) {
		g_free (key);
		return f;
	}
	f = g_new0 (IndexFolder, 1);
	f->key = key;
	f->account_uid = g_strdup (account_uid);
	f->folder_name = g_strdup (folder_name);
	g_hash_table_insert (index_state.folders, f->key, f);
	return f;
}

/* Enregistre l'index d'un dossier sur disque (écriture atomique). Le verrou ne doit PAS être détenu
 * à l'appel : la sérialisation est faite sous verrou, l'écriture hors verrou. */
static void
index_folder_save (const gchar *key)
{
	GByteArray *buf = g_byte_array_new ();
	unsigned char *blob;
	size_t blob_len = 0;
	IndexFolder *f;
	gchar *path;
	gsize key_len = strlen (key);
	GError *error = NULL;

	g_mutex_lock (&index_state.lock);
	f = g_hash_table_lookup (index_state.folders, key);
	if (!f || !f->idx || !f->dirty) {
		g_mutex_unlock (&index_state.lock);
		g_byte_array_free (buf, TRUE);
		return;
	}
	blob = evoidx_serialize (f->idx, &blob_len);
	if (!blob) {
		g_mutex_unlock (&index_state.lock);
		g_byte_array_free (buf, TRUE);
		return;
	}
	g_byte_array_append (buf, (const guint8 *) INDEX_FILE_MAGIC, 4);
	put_u32 (buf, INDEX_FILE_VERSION);
	put_u32 (buf, (guint32) key_len);
	g_byte_array_append (buf, (const guint8 *) key, (guint) key_len);
	put_u32 (buf, f->complete ? 1 : 0);
	put_u32 (buf, f->total);
	put_u32 (buf, f->missing);
	put_u32 (buf, (guint32) f->updated);
	f->dirty = FALSE;
	g_mutex_unlock (&index_state.lock);

	g_byte_array_append (buf, blob, (guint) blob_len);
	free (blob);

	path = index_file_path (key);
	if (path && g_file_set_contents (path, (const gchar *) buf->data, (gssize) buf->len, &error)) {
		g_mutex_lock (&index_state.lock);
		f = g_hash_table_lookup (index_state.folders, key);
		if (f)
			f->file_size = buf->len;
		g_mutex_unlock (&index_state.lock);
	} else {
		g_message ("index : écriture impossible (%s)", error ? error->message : "dossier indisponible");
		g_mutex_lock (&index_state.lock);
		f = g_hash_table_lookup (index_state.folders, key);
		if (f)
			f->dirty = TRUE;
		g_mutex_unlock (&index_state.lock);
	}
	g_clear_error (&error);
	g_free (path);
	g_byte_array_free (buf, TRUE);
}

/* Recharge les index enregistrés (au premier passage). Les fichiers illisibles ou corrompus sont supprimés :
 * le dossier sera réindexé. Le verrou ne doit PAS être détenu. */
static void
index_load_all (void)
{
	gchar *dir = index_dir ();
	GDir *gdir;
	const gchar *name;

	if (!dir)
		return;
	gdir = g_dir_open (dir, 0, NULL);
	while (gdir && (name = g_dir_read_name (gdir)) != NULL) {
		gchar *path, *contents = NULL;
		gsize length = 0;
		size_t pos = 4;
		guint32 version, key_len, complete, total, missing, updated;
		gboolean ok = FALSE;

		if (!g_str_has_suffix (name, ".idx"))
			continue;
		path = g_build_filename (dir, name, NULL);
		if (g_file_get_contents (path, &contents, &length, NULL) && length > 32 &&
		    memcmp (contents, INDEX_FILE_MAGIC, 4) == 0 &&
		    evoidx_read_u32 ((const unsigned char *) contents, length, &pos, &version) && version == INDEX_FILE_VERSION &&
		    evoidx_read_u32 ((const unsigned char *) contents, length, &pos, &key_len) && key_len < 4096 && pos + key_len <= length) {
			gchar *key = g_strndup (contents + pos, key_len);
			gchar *sep = strchr (key, '\x1F');

			pos += key_len;
			if (sep &&
			    evoidx_read_u32 ((const unsigned char *) contents, length, &pos, &complete) &&
			    evoidx_read_u32 ((const unsigned char *) contents, length, &pos, &total) &&
			    evoidx_read_u32 ((const unsigned char *) contents, length, &pos, &missing) &&
			    evoidx_read_u32 ((const unsigned char *) contents, length, &pos, &updated)) {
				EvoIdx *idx = evoidx_deserialize ((const unsigned char *) contents + pos, length - pos);

				if (idx) {
					IndexFolder *f = g_new0 (IndexFolder, 1);

					*sep = '\0';
					f->account_uid = g_strdup (key);
					f->folder_name = g_strdup (sep + 1);
					f->key = folder_key (f->account_uid, f->folder_name);
					f->idx = idx;
					f->complete = complete != 0;
					f->total = total;
					f->missing = missing;
					f->updated = updated;
					f->file_size = length;

					g_mutex_lock (&index_state.lock);
					if (g_hash_table_contains (index_state.folders, f->key)) {
						/* déjà créé depuis (arrivée de messages) : l'état en mémoire prime */
						g_mutex_unlock (&index_state.lock);
						index_folder_free (f);
					} else {
						g_hash_table_insert (index_state.folders, f->key, f);
						g_mutex_unlock (&index_state.lock);
					}
					ok = TRUE;
				}
			}
			g_free (key);
		}
		if (!ok)
			g_remove (path);
		g_free (contents);
		g_free (path);
	}
	if (gdir)
		g_dir_close (gdir);
	g_free (dir);
}

/* Supprime tous les index (mémoire et disque). Le verrou ne doit PAS être détenu. */
static void
index_clear_all (void)
{
	gchar *dir = index_dir ();
	GDir *gdir;
	const gchar *name;
	GHashTableIter iter;
	gpointer value;
	GPtrArray *old = g_ptr_array_new_with_free_func ((GDestroyNotify) index_folder_free);

	g_mutex_lock (&index_state.lock);
	g_hash_table_iter_init (&iter, index_state.folders);
	while (g_hash_table_iter_next (&iter, NULL, &value))
		g_ptr_array_add (old, value);
	g_hash_table_remove_all (index_state.folders);
	g_queue_clear_full (&index_state.queue, g_free);
	index_state.pass_total = index_state.pass_done = 0;
	g_mutex_unlock (&index_state.lock);
	g_ptr_array_unref (old);

	gdir = dir ? g_dir_open (dir, 0, NULL) : NULL;
	while (gdir && (name = g_dir_read_name (gdir)) != NULL) {
		if (g_str_has_suffix (name, ".idx")) {
			gchar *path = g_build_filename (dir, name, NULL);

			g_remove (path);
			g_free (path);
		}
	}
	if (gdir)
		g_dir_close (gdir);
	g_free (dir);
}

/* ------------------------------------------------------------------------ */
/* Extraction du texte d'un message                                          */
/* ------------------------------------------------------------------------ */

/* Ajoute à out le texte d'une partie MIME feuille (texte brut, HTML nettoyé), limité à max_bytes.
 * Retourne : TRUE si du texte a été ajouté. */
static gboolean
index_append_part_text (CamelMimePart *part,
			gboolean is_html,
			gsize max_bytes,
			GString *out,
			GCancellable *cancellable)
{
	gchar *text, *clean = NULL;
	gsize len;

	text = part_get_text (part, cancellable);
	if (!text || !*text) {
		g_free (text);
		return FALSE;
	}
	if (is_html) {
		clean = html_to_text (text);
		g_free (text);
		text = clean;
	}
	len = strlen (text);
	if (len > max_bytes)
		len = max_bytes;
	if (out->len + len > INDEX_MAX_MESSAGE_BYTES)
		len = out->len < INDEX_MAX_MESSAGE_BYTES ? INDEX_MAX_MESSAGE_BYTES - out->len : 0;
	if (len > 0) {
		g_string_append_len (out, text, (gssize) len);
		g_string_append_c (out, '\n');
	}
	g_free (text);
	return len > 0;
}

static void
index_walk_part (CamelMimePart *part,
		 GString *out,
		 gint depth,
		 GCancellable *cancellable)
{
	CamelDataWrapper *content;
	CamelContentType *ct;

	if (!part || depth > INDEX_MAX_DEPTH || out->len >= INDEX_MAX_MESSAGE_BYTES || g_cancellable_is_cancelled (cancellable))
		return;
	content = camel_medium_get_content (CAMEL_MEDIUM (part));
	ct = camel_mime_part_get_content_type (part);
	if (!content || !ct)
		return;

	if (CAMEL_IS_MULTIPART (content)) {
		guint ii, n = camel_multipart_get_number (CAMEL_MULTIPART (content));
		CamelMimePart *chosen = NULL;

		if (camel_content_type_is (ct, "multipart", "alternative")) {
			/* même contenu en plusieurs formats : texte brut de préférence, sinon la dernière version (HTML) */
			for (ii = 0; ii < n && !chosen; ii++) {
				CamelMimePart *sub = camel_multipart_get_part (CAMEL_MULTIPART (content), ii);
				CamelContentType *sub_ct = sub ? camel_mime_part_get_content_type (sub) : NULL;

				if (sub_ct && camel_content_type_is (sub_ct, "text", "plain"))
					chosen = sub;
			}
			if (!chosen && n > 0)
				chosen = camel_multipart_get_part (CAMEL_MULTIPART (content), n - 1);
			if (chosen)
				index_walk_part (chosen, out, depth + 1, cancellable);
			return;
		}
		for (ii = 0; ii < n; ii++)
			index_walk_part (camel_multipart_get_part (CAMEL_MULTIPART (content), ii), out, depth + 1, cancellable);
		return;
	}

	if (CAMEL_IS_MIME_MESSAGE (content)) {
		/* message transféré en pièce jointe : son texte est indexé aussi */
		index_walk_part (CAMEL_MIME_PART (content), out, depth + 1, cancellable);
		return;
	}

	{
		const gchar *disposition = camel_mime_part_get_disposition (part);
		const gchar *filename = camel_mime_part_get_filename (part);
		gboolean attachment = (disposition && g_ascii_strcasecmp (disposition, "attachment") == 0) || (filename && *filename);
		gchar *mime = camel_content_type_simple (ct);
		gboolean is_html = camel_content_type_is (ct, "text", "html");
		gboolean is_text = camel_content_type_is (ct, "text", "plain") || is_html;

		if (is_text && !attachment)
			index_append_part_text (part, is_html, INDEX_MAX_BODY_BYTES, out, cancellable);
		else if (evoidx_is_indexable_attachment (mime, filename))
			index_append_part_text (part, is_html, INDEX_MAX_ATTACHMENT_BYTES, out, cancellable);
		g_free (mime);
	}
}

/* Texte indexable d'un message : corps (texte ou HTML nettoyé) et pièces jointes texte.
 * Retourne : texte UTF-8 (g_string_free (…, TRUE)), éventuellement vide. */
static GString *
index_extract_text (CamelMimeMessage *message,
		    GCancellable *cancellable)
{
	GString *out = g_string_new (NULL);

	index_walk_part (CAMEL_MIME_PART (message), out, 0, cancellable);
	return out;
}

/* ------------------------------------------------------------------------ */
/* État, progression                                                         */
/* ------------------------------------------------------------------------ */

/* Écrit l'état de l'index dans un objet JSON ouvert du constructeur. Le verrou doit être détenu. */
static void
index_fill_status_locked (JsonBuilder *builder)
{
	GHashTableIter iter;
	gpointer value;
	guint64 indexed = 0, total = 0, size = 0, terms = 0;
	guint folders = 0, complete = 0;
	gint64 updated = 0;
	const gchar *state;

	g_hash_table_iter_init (&iter, index_state.folders);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		IndexFolder *f = value;

		if (!f->idx)
			continue;
		folders++;
		if (f->complete)
			complete++;
		indexed += evoidx_document_count (f->idx);
		total += f->total;
		terms += evoidx_term_count (f->idx);
		size += f->file_size;
		if (f->updated > updated)
			updated = f->updated;
	}

	if (!index_state.enabled)
		state = "disabled";
	else if (index_state.worker_running || !g_queue_is_empty (&index_state.queue) || index_state.rescan_requested)
		state = "indexing";
	else
		state = "idle";

	builder_add_boolean (builder, "enabled", index_state.enabled);
	builder_add_string (builder, "state", state);
	builder_add_int (builder, "folders", folders);
	builder_add_int (builder, "folders_complete", complete);
	builder_add_int (builder, "messages_indexed", (gint64) indexed);
	builder_add_int (builder, "messages_total", (gint64) total);
	builder_add_int (builder, "words", (gint64) terms);
	builder_add_int (builder, "size_bytes", (gint64) size);
	builder_add_int (builder, "updated", updated);
	builder_add_int (builder, "pass_done", index_state.pass_done);
	builder_add_int (builder, "pass_total", index_state.pass_total);
	builder_add_int (builder, "pending", g_queue_get_length (&index_state.queue));
	builder_add_string (builder, "current", index_state.current ? index_state.current : "");
}

/* Émet l'événement "index-progress" (limité à un toutes les 700 ms sauf force).
 * Paramètres :
 *   core  : moteur
 *   force : TRUE pour émettre sans tenir compte de la limite (début, fin) */
static void
index_emit_progress (EvoCore *core,
		     gboolean force)
{
	JsonBuilder *builder;
	gint64 now = g_get_monotonic_time () / 1000;

	g_mutex_lock (&index_state.lock);
	if (!force && now - index_state.last_emit_ms < INDEX_PROGRESS_INTERVAL_MS) {
		g_mutex_unlock (&index_state.lock);
		return;
	}
	index_state.last_emit_ms = now;
	builder = core_event_builder ();
	index_fill_status_locked (builder);
	g_mutex_unlock (&index_state.lock);

	core_emit_builder (core, "index-progress", builder);
}

/* Le travail en cours doit-il s'arrêter (désactivation, reconstruction, arrêt du moteur) ? */
static gboolean
index_should_stop (EvoCore *core,
		   guint64 generation)
{
	gboolean stop;

	if (g_atomic_int_get (&core->shutting_down))
		return TRUE;
	g_mutex_lock (&index_state.lock);
	stop = !index_state.enabled || index_state.generation != generation;
	g_mutex_unlock (&index_state.lock);
	return stop;
}

/* ------------------------------------------------------------------------ */
/* Traitement d'un dossier                                                   */
/* ------------------------------------------------------------------------ */

static int
index_collect_stale_cb (const char *key,
			void *user_data)
{
	GHashTable *current = ((gpointer *) user_data)[0];
	GPtrArray *stale = ((gpointer *) user_data)[1];

	if (!g_hash_table_contains (current, key))
		g_ptr_array_add (stale, g_strdup (key));
	return 0;
}

/* Lit un message déjà présent sur le disque (sans réseau) : cache du dossier IMAP, ou dossier local.
 * Retourne : message (g_object_unref), NULL si le message n'est pas disponible localement. */
static CamelMimeMessage *
index_get_local_message (CamelFolder *folder,
			 const gchar *uid,
			 gboolean is_local_store,
			 GCancellable *cancellable)
{
	CamelMimeMessage *message = camel_folder_get_message_cached (folder, uid, cancellable);

	if (!message && is_local_store)
		message = camel_folder_get_message_sync (folder, uid, cancellable, NULL);
	return message;
}

/* Met à jour l'index d'un dossier : retire les messages disparus, indexe les nouveaux
 * messages disponibles localement. Peut durer longtemps : le verrou n'est détenu que pour
 * chaque modification de l'index.
 * Paramètres :
 *   core       : moteur
 *   key        : clé du dossier ("<compte>\x1F<dossier>")
 *   generation : génération au démarrage du travailleur (arrêt si elle change) */
static void
index_process_folder (EvoCore *core,
		      const gchar *key,
		      guint64 generation)
{
	IndexFolder *f;
	gchar *account_uid, *folder_name;
	CamelFolder *folder;
	CamelStore *store;
	GPtrArray *uids, *stale;
	GHashTable *current;
	GCancellable *cancellable = g_cancellable_new ();
	gboolean is_local = FALSE, stopped = FALSE;
	guint ii, indexed_now = 0, missing = 0;
	gpointer collect[2];

	g_mutex_lock (&index_state.lock);
	f = g_hash_table_lookup (index_state.folders, key);
	if (!f) {
		g_mutex_unlock (&index_state.lock);
		g_object_unref (cancellable);
		return;
	}
	f->queued = FALSE;
	account_uid = g_strdup (f->account_uid);
	folder_name = g_strdup (f->folder_name);
	g_free (index_state.current);
	index_state.current = g_strdup (folder_name);
	g_mutex_unlock (&index_state.lock);

	folder = core_ref_folder_cached (core, account_uid, folder_name, NULL, NULL);
	if (!folder) {
		g_free (account_uid);
		g_free (folder_name);
		g_object_unref (cancellable);
		return;
	}
	store = camel_folder_get_parent_store (folder);
	if (store) {
		CamelProvider *provider = camel_service_get_provider (CAMEL_SERVICE (store));

		is_local = provider && (provider->flags & CAMEL_PROVIDER_IS_LOCAL) != 0;
	}

	uids = camel_folder_dup_uids (folder);
	current = g_hash_table_new (g_str_hash, g_str_equal);
	for (ii = 0; uids && ii < uids->len; ii++)
		g_hash_table_add (current, uids->pdata[ii]);

	/* messages disparus du dossier */
	stale = g_ptr_array_new_with_free_func (g_free);
	collect[0] = current;
	collect[1] = stale;
	g_mutex_lock (&index_state.lock);
	f = g_hash_table_lookup (index_state.folders, key);
	if (f) {
		if (!f->idx)
			f->idx = evoidx_new ();
		if (f->idx) {
			evoidx_foreach_document (f->idx, index_collect_stale_cb, collect);
			for (ii = 0; ii < stale->len; ii++)
				evoidx_remove_document (f->idx, stale->pdata[ii]);
			if (stale->len > 0)
				f->dirty = TRUE;
		}
	}
	g_mutex_unlock (&index_state.lock);

	/* nouveaux messages */
	for (ii = 0; uids && ii < uids->len; ii++) {
		const gchar *uid = uids->pdata[ii];
		CamelMessageInfo *info;
		CamelMimeMessage *message;
		GString *text;
		gboolean known;

		if (index_should_stop (core, generation)) {
			stopped = TRUE;
			break;
		}

		g_mutex_lock (&index_state.lock);
		f = g_hash_table_lookup (index_state.folders, key);
		known = !f || !f->idx || evoidx_has_document (f->idx, uid);
		g_mutex_unlock (&index_state.lock);
		if (known)
			continue;

		/* les messages supprimés (corbeille Camel en attente) ne sont pas indexés ni comptés */
		info = camel_folder_get_message_info (folder, uid);
		if (info && (camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED)) {
			g_object_unref (info);
			continue;
		}
		g_clear_object (&info);

		core_bg_yield (core);
		message = index_get_local_message (folder, uid, is_local, cancellable);
		if (!message) {
			missing++;
			continue;
		}
		text = index_extract_text (message, cancellable);
		g_object_unref (message);

		g_mutex_lock (&index_state.lock);
		f = g_hash_table_lookup (index_state.folders, key);
		if (f && f->idx && index_state.generation == generation) {
			evoidx_add_document (f->idx, uid, text->str, text->len);
			f->dirty = TRUE;
		}
		g_mutex_unlock (&index_state.lock);
		g_string_free (text, TRUE);

		indexed_now++;
		if (indexed_now % INDEX_CHECKPOINT_MESSAGES == 0)
			index_folder_save (key);
		index_emit_progress (core, FALSE);
	}

	if (!stopped) {
		/* messages supprimés non indexés : ils sont ignorés par la recherche comme par l'index */
		g_mutex_lock (&index_state.lock);
		f = g_hash_table_lookup (index_state.folders, key);
		if (f && index_state.generation == generation) {
			f->total = uids ? uids->len : 0;
			f->missing = missing;
			f->complete = (missing == 0);
			f->updated = (gint64) (g_get_real_time () / G_USEC_PER_SEC);
			f->dirty = TRUE;
		}
		g_mutex_unlock (&index_state.lock);
		index_folder_save (key);
	}

	g_hash_table_destroy (current);
	g_ptr_array_unref (stale);
	if (uids)
		g_ptr_array_unref (uids);
	g_object_unref (folder);
	g_object_unref (cancellable);
	g_free (account_uid);
	g_free (folder_name);
}

/* Ajoute un dossier à la file de traitement (sans doublon). Retourne : TRUE si ajouté.
 * Appelant : verrou détenu. */
static gboolean
index_queue_locked (const gchar *account_uid,
		    const gchar *folder_name)
{
	IndexFolder *f = index_folder_ensure (account_uid, folder_name);

	if (f->queued)
		return FALSE;
	f->queued = TRUE;
	g_queue_push_tail (&index_state.queue, g_strdup (f->key));
	return TRUE;
}

/* Énumère les dossiers des comptes connectés et les ajoute à la file (Corbeille et Indésirables exclus). */
static void
index_enqueue_all_folders (EvoCore *core,
			   guint64 generation)
{
	GList *services = camel_session_list_services (core->session), *link;

	for (link = services; link; link = g_list_next (link)) {
		CamelService *service = link->data;
		CamelFolderInfo *root, *fi, *next;
		GPtrArray *found;
		guint ii;

		if (!CAMEL_IS_STORE (service) || camel_service_get_connection_status (service) != CAMEL_SERVICE_CONNECTED)
			continue;
		if (index_should_stop (core, generation))
			break;

		root = camel_store_get_folder_info_sync (CAMEL_STORE (service), NULL,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST, NULL, NULL);
		found = g_ptr_array_new_with_free_func (g_free);

		/* parcours en profondeur sans récursion */
		for (fi = root; fi; fi = next) {
			guint32 type = fi->flags & CAMEL_FOLDER_TYPE_MASK;

			if (!(fi->flags & CAMEL_FOLDER_NOSELECT) && fi->full_name && *fi->full_name &&
			    type != CAMEL_FOLDER_TYPE_TRASH && type != CAMEL_FOLDER_TYPE_JUNK)
				g_ptr_array_add (found, g_strdup (fi->full_name));
			if (fi->child) {
				next = fi->child;
			} else if (fi->next) {
				next = fi->next;
			} else {
				next = fi->parent;
				while (next && !next->next)
					next = next->parent;
				next = next ? next->next : NULL;
			}
		}

		g_mutex_lock (&index_state.lock);
		for (ii = 0; ii < found->len; ii++) {
			if (index_queue_locked (camel_service_get_uid (service), found->pdata[ii]))
				index_state.pass_total++;
		}
		g_mutex_unlock (&index_state.lock);

		g_ptr_array_unref (found);
		if (root)
			camel_folder_info_free (root);
	}
	g_list_free_full (services, g_object_unref);
}

/* Travailleur d'indexation (tâche de fond, un seul à la fois) : traite la file jusqu'à épuisement. */
static gchar *
core_index_worker_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	guint64 generation;

	g_mutex_lock (&index_state.lock);
	generation = index_state.generation;
	g_mutex_unlock (&index_state.lock);

	index_emit_progress (core, TRUE);

	for (;;) {
		gchar *key = NULL;
		gboolean need_load = FALSE, need_scan = FALSE, need_clear = FALSE;

		g_mutex_lock (&index_state.lock);
		if (g_atomic_int_get (&core->shutting_down) || !index_state.enabled) {
			index_state.worker_running = FALSE;
			g_clear_pointer (&index_state.current, g_free);
			g_mutex_unlock (&index_state.lock);
			break;
		}
		if (index_state.rebuild_requested) {
			index_state.rebuild_requested = FALSE;
			index_state.rescan_requested = TRUE;
			need_clear = TRUE;
		}
		if (index_state.generation != generation)
			generation = index_state.generation;
		if (!index_state.loaded)
			need_load = TRUE;
		if (index_state.rescan_requested && !need_clear) {
			index_state.rescan_requested = FALSE;
			need_scan = TRUE;
			index_state.pass_total = g_queue_get_length (&index_state.queue);
			index_state.pass_done = 0;
		}
		if (!need_clear && !need_load && !need_scan) {
			key = g_queue_pop_head (&index_state.queue);
			if (key && index_state.pass_total == 0)
				index_state.pass_total = g_queue_get_length (&index_state.queue) + 1;
			if (!key) {
				index_state.worker_running = FALSE;
				index_state.pass_total = index_state.pass_done = 0;
				index_state.last_done = (gint64) (g_get_real_time () / G_USEC_PER_SEC);
				g_clear_pointer (&index_state.current, g_free);
				g_mutex_unlock (&index_state.lock);
				break;
			}
		}
		g_mutex_unlock (&index_state.lock);

		if (need_clear) {
			index_clear_all ();
			g_mutex_lock (&index_state.lock);
			index_state.loaded = TRUE;
			g_mutex_unlock (&index_state.lock);
			continue;
		}
		if (need_load) {
			index_load_all ();
			g_mutex_lock (&index_state.lock);
			index_state.loaded = TRUE;
			g_mutex_unlock (&index_state.lock);
			continue;
		}
		if (need_scan) {
			index_enqueue_all_folders (core, generation);
			continue;
		}

		index_process_folder (core, key, generation);
		g_free (key);

		g_mutex_lock (&index_state.lock);
		if (index_state.pass_done < index_state.pass_total)
			index_state.pass_done++;
		g_mutex_unlock (&index_state.lock);
		index_emit_progress (core, FALSE);
	}

	/* désactivé pendant le travail : la mémoire est libérée (les fichiers sont conservés) */
	g_mutex_lock (&index_state.lock);
	if (!index_state.enabled) {
		GHashTableIter iter;
		gpointer value;
		GPtrArray *old = g_ptr_array_new_with_free_func ((GDestroyNotify) index_folder_free);

		g_hash_table_iter_init (&iter, index_state.folders);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			g_ptr_array_add (old, value);
		g_hash_table_remove_all (index_state.folders);
		g_queue_clear_full (&index_state.queue, g_free);
		index_state.loaded = FALSE;
		g_mutex_unlock (&index_state.lock);
		g_ptr_array_unref (old);
	} else {
		g_mutex_unlock (&index_state.lock);
	}
	index_emit_progress (core, TRUE);

	return g_strdup ("{}");
}

/* Démarre le travailleur s'il ne tourne pas. Appelant : verrou NON détenu. */
static void
index_start_worker (EvoCore *core)
{
	gboolean start = FALSE;

	g_mutex_lock (&index_state.lock);
	if (index_state.enabled && !index_state.worker_running &&
	    (index_state.rescan_requested || index_state.rebuild_requested || !index_state.loaded ||
	     !g_queue_is_empty (&index_state.queue))) {
		index_state.worker_running = TRUE;
		start = TRUE;
	}
	g_mutex_unlock (&index_state.lock);

	if (start)
		core_submit_background (core, core_index_worker_run, g_new0 (gchar *, 1), core_ignore_result, NULL);
}

static gboolean
index_kick_cb (gpointer user_data)
{
	EvoCore *core = user_data;

	g_mutex_lock (&index_state.lock);
	index_state.kick_pending = FALSE;
	g_mutex_unlock (&index_state.lock);
	if (!g_atomic_int_get (&core->shutting_down))
		index_start_worker (core);
	return G_SOURCE_REMOVE;
}

/* Programme le démarrage du travailleur après un délai (regroupe les rafales d'événements).
 * Appelant : verrou NON détenu. */
static void
index_schedule_kick (EvoCore *core,
		     guint delay_seconds)
{
	GSource *source;

	g_mutex_lock (&index_state.lock);
	if (index_state.kick_pending || !index_state.enabled) {
		g_mutex_unlock (&index_state.lock);
		return;
	}
	index_state.kick_pending = TRUE;
	g_mutex_unlock (&index_state.lock);

	source = g_timeout_source_new_seconds (delay_seconds);
	g_source_set_callback (source, index_kick_cb, core, NULL);
	g_source_attach (source, core->context);
	g_source_unref (source);
}

/* Parcours périodique complet (les dossiers modifiés sans signal sont rattrapés). */
static gboolean
index_rescan_timer_cb (gpointer user_data)
{
	EvoCore *core = user_data;

	if (g_atomic_int_get (&core->shutting_down))
		return G_SOURCE_REMOVE;
	g_mutex_lock (&index_state.lock);
	if (index_state.enabled)
		index_state.rescan_requested = TRUE;
	g_mutex_unlock (&index_state.lock);
	index_start_worker (core);
	return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------------ */
/* Interface avec le reste du moteur                                         */
/* ------------------------------------------------------------------------ */

/* Signal "changed" d'un dossier : les messages arrivés ou supprimés sont pris en compte par l'index.
 * Appelé depuis core_folder_changed_cb () ; ne fait aucun appel à Camel.
 * Paramètres :
 *   core    : moteur
 *   folder  : dossier modifié
 *   changes : modifications (peut être NULL) */
void
core_index_folder_changed (EvoCore *core,
			   CamelFolder *folder,
			   CamelFolderChangeInfo *changes)
{
	CamelStore *store;
	guint32 type;

	index_state_init ();
	if (!g_atomic_int_get (&index_state.enabled_flag))
		return;
	if (!changes || ((!changes->uid_added || changes->uid_added->len == 0) &&
	    (!changes->uid_removed || changes->uid_removed->len == 0)))
		return;
	store = camel_folder_get_parent_store (folder);
	type = camel_folder_get_flags (folder) & CAMEL_FOLDER_TYPE_MASK;
	if (!store || type == CAMEL_FOLDER_TYPE_TRASH || type == CAMEL_FOLDER_TYPE_JUNK)
		return;

	g_mutex_lock (&index_state.lock);
	if (index_state.enabled && index_state.loaded)
		index_queue_locked (camel_service_get_uid (CAMEL_SERVICE (store)), camel_folder_get_full_name (folder));
	g_mutex_unlock (&index_state.lock);
	index_schedule_kick (core, INDEX_CHANGE_DELAY);
}

/* Cherche dans l'index les messages d'un dossier dont le corps (ou une pièce jointe texte) contient
 * tous les mots de la requête (début de mots). L'index n'est utilisé que s'il couvre TOUS les messages
 * du dossier (aucun message non supprimé absent) ; sinon l'appelant se rabat sur la recherche Camel
 * et le dossier est mis en file pour être complété.
 * Paramètres :
 *   core     : moteur
 *   folder   : dossier cherché
 *   query    : mots recherchés (sans opérateurs)
 *   out_uids : reçoit l'ensemble des UID trouvés (g_hash_table_destroy)
 * Retourne : TRUE si l'index a répondu (out_uids est alors renseigné, éventuellement vide). */
gboolean
core_index_search (EvoCore *core,
		   CamelFolder *folder,
		   const gchar *query,
		   GHashTable **out_uids)
{
	CamelStore *store;
	GPtrArray *uids, *uncovered;
	const gchar **keys = NULL;
	gchar *key;
	IndexFolder *f;
	GHashTable *result;
	size_t n_keys = 0, ii;
	gboolean answered = FALSE;
	guint jj;

	*out_uids = NULL;
	index_state_init ();
	if (!g_atomic_int_get (&index_state.enabled_flag) || !query || !*query)
		return FALSE;
	store = camel_folder_get_parent_store (folder);
	if (!store)
		return FALSE;

	uids = camel_folder_dup_uids (folder);
	key = folder_key (camel_service_get_uid (CAMEL_SERVICE (store)), camel_folder_get_full_name (folder));
	uncovered = g_ptr_array_new_with_free_func (g_free);
	result = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	g_mutex_lock (&index_state.lock);
	f = g_hash_table_lookup (index_state.folders, key);
	if (index_state.enabled && f && f->idx && evoidx_query (f->idx, query, &keys, &n_keys)) {
		for (ii = 0; ii < n_keys; ii++)
			g_hash_table_add (result, g_strdup (keys[ii]));
		for (jj = 0; uids && jj < uids->len && uncovered->len < 50; jj++) {
			if (!evoidx_has_document (f->idx, uids->pdata[jj]))
				g_ptr_array_add (uncovered, g_strdup (uids->pdata[jj]));
		}
		answered = TRUE;
	}
	g_mutex_unlock (&index_state.lock);
	free ((void *) keys);

	/* messages absents de l'index : seuls ceux qui ne sont pas supprimés comptent (appel à Camel hors verrou) */
	for (jj = 0; answered && jj < uncovered->len; jj++) {
		CamelMessageInfo *info = camel_folder_get_message_info (folder, uncovered->pdata[jj]);
		gboolean deleted = info && (camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED);

		g_clear_object (&info);
		if (!deleted)
			answered = FALSE;
	}

	if (!answered) {
		/* l'index est incomplet : complété en fond, la recherche Camel répond cette fois */
		g_hash_table_destroy (result);
		result = NULL;
		g_mutex_lock (&index_state.lock);
		if (index_state.enabled && index_state.loaded)
			index_queue_locked (camel_service_get_uid (CAMEL_SERVICE (store)), camel_folder_get_full_name (folder));
		g_mutex_unlock (&index_state.lock);
		index_schedule_kick (core, INDEX_CHANGE_DELAY);
	}

	g_ptr_array_unref (uncovered);
	if (uids)
		g_ptr_array_unref (uids);
	g_free (key);
	*out_uids = result;
	return answered;
}

/* Exécutant de evo_core_index_status () : état courant de l'index. */
static gchar *
core_index_status_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	JsonBuilder *builder;
	gchar *json;

	index_state_init ();
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	g_mutex_lock (&index_state.lock);
	index_fill_status_locked (builder);
	g_mutex_unlock (&index_state.lock);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	return json;
}

/* Exécutant de evo_core_index_rebuild () : demande l'effacement puis la reconstruction. */
static gchar *
core_index_rebuild_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	index_state_init ();
	g_mutex_lock (&index_state.lock);
	if (!index_state.enabled) {
		g_mutex_unlock (&index_state.lock);
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "L'index de recherche est désactivé");
		return NULL;
	}
	index_state.generation++;
	index_state.rebuild_requested = TRUE;
	g_mutex_unlock (&index_state.lock);

	index_start_worker (core);
	return g_strdup ("{}");
}

void
evo_core_index_set_enabled (EvoCore *core,
			    int enabled)
{
	gboolean changed;

	g_return_if_fail (core != NULL);

	index_state_init ();
	g_mutex_lock (&index_state.lock);
	changed = (index_state.enabled != (enabled != 0));
	index_state.enabled = enabled != 0;
	g_atomic_int_set (&index_state.enabled_flag, enabled != 0);
	if (changed) {
		index_state.generation++;
		if (enabled) {
			index_state.rescan_requested = TRUE;
		} else if (!index_state.worker_running) {
			/* aucun travailleur : la mémoire est libérée ici (les fichiers sont conservés) */
			GHashTableIter iter;
			gpointer value;
			GPtrArray *old = g_ptr_array_new_with_free_func ((GDestroyNotify) index_folder_free);

			g_hash_table_iter_init (&iter, index_state.folders);
			while (g_hash_table_iter_next (&iter, NULL, &value))
				g_ptr_array_add (old, value);
			g_hash_table_remove_all (index_state.folders);
			g_queue_clear_full (&index_state.queue, g_free);
			index_state.loaded = FALSE;
			g_mutex_unlock (&index_state.lock);
			g_ptr_array_unref (old);
			index_emit_progress (core, TRUE);
			return;
		}
	}
	g_mutex_unlock (&index_state.lock);

	if (enabled) {
		/* premier parcours peu après l'activation (l'ouverture de l'interface a la priorité), puis périodique */
		index_schedule_kick (core, changed ? INDEX_FIRST_SCAN_DELAY : INDEX_CHANGE_DELAY);
		g_mutex_lock (&index_state.lock);
		if (!index_state.timer_started) {
			GSource *source = g_timeout_source_new_seconds (INDEX_RESCAN_PERIOD);

			index_state.timer_started = TRUE;
			g_source_set_callback (source, index_rescan_timer_cb, core, NULL);
			g_source_attach (source, core->context);
			g_source_unref (source);
		}
		g_mutex_unlock (&index_state.lock);
	} else {
		index_emit_progress (core, TRUE);
	}
}

void
evo_core_index_status (EvoCore *core,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	g_return_if_fail (core != NULL);

	core_submit (core, core_index_status_run, g_new0 (gchar *, 1), func, user_data);
}

void
evo_core_index_rebuild (EvoCore *core,
			EvoCoreResultFunc func,
			void *user_data)
{
	g_return_if_fail (core != NULL);

	core_submit (core, core_index_rebuild_run, g_new0 (gchar *, 1), func, user_data);
}
