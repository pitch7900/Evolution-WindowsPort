/*
 * evo-core : filtrage anti-spam local par classificateur bayésien.
 *
 * Ce fichier relie la logique pure (evo-core-spam-util.h : jetons, base,
 * score) à Camel : extraction des jetons d'un message, apprentissage à la
 * demande de l'utilisateur (« Indésirable » / « Non indésirable »),
 * apprentissage en lot d'un dossier, classement automatique des nouveaux
 * messages de la boîte de réception vers le dossier Indésirables, remise à
 * zéro de l'apprentissage. La base est un fichier texte (spam.db) du dossier
 * %LOCALAPPDATA%\EvolutionWin, chargé à la première utilisation.
 *
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"
#include "evo-core-spam-util.h"

/* Taille maximale du source HTML analysé pour en extraire texte et liens (octets) */
#define SPAM_MAX_HTML_BYTES 200000

/* État global du filtre, protégé par spam_lock */
static GMutex spam_lock;
static SpamDb spam_db;
static gboolean spam_loaded = FALSE;
static gchar *spam_path = NULL;
static gboolean spam_enabled = FALSE;
static gint spam_threshold = 90;	/* pourcentage : score minimal pour classer en indésirable */

/**
 * @brief Chemin par défaut de la base : %LOCALAPPDATA%\EvolutionWin\spam.db.
 * @return chemin nouvellement alloué
 */
static gchar *
spam_default_path (void)
{
	const gchar *local = g_getenv ("LOCALAPPDATA");

	if (local && *local)
		return g_build_filename (local, "EvolutionWin", "spam.db", NULL);

	return g_build_filename (g_get_user_data_dir (), "EvolutionWin", "spam.db", NULL);
}

/**
 * @brief Charge la base depuis le disque si elle ne l'est pas encore (spam_lock tenu).
 *        Un fichier absent ou illisible donne une base vide.
 */
static void
spam_load_locked (void)
{
	FILE *file;

	if (spam_loaded)
		return;

	spam_loaded = TRUE;
	spam_db_clear (&spam_db);

	if (!spam_path)
		spam_path = spam_default_path ();

	file = g_fopen (spam_path, "rb");
	if (!file)
		return;

	if (spam_db_read (&spam_db, file) != 0)
		g_warning ("spam : base d'apprentissage illisible (%s), elle est ignorée", spam_path);

	fclose (file);
}

/**
 * @brief Enregistre la base sur disque (spam_lock tenu) : fichier temporaire puis renommage,
 *        pour ne jamais laisser une base à moitié écrite.
 * @return TRUE si l'enregistrement a réussi
 */
static gboolean
spam_save_locked (void)
{
	gchar *tmp_path, *dir;
	FILE *file;
	gboolean ok;

	if (!spam_path)
		spam_path = spam_default_path ();

	spam_db_prune (&spam_db);

	dir = g_path_get_dirname (spam_path);
	g_mkdir_with_parents (dir, 0700);
	g_free (dir);

	tmp_path = g_strconcat (spam_path, ".tmp", NULL);
	file = g_fopen (tmp_path, "wb");
	if (!file) {
		g_free (tmp_path);
		return FALSE;
	}

	ok = spam_db_write (&spam_db, file) == 0;
	if (fclose (file) != 0)
		ok = FALSE;

	if (ok && g_rename (tmp_path, spam_path) != 0) {
		g_remove (spam_path);
		ok = g_rename (tmp_path, spam_path) == 0;
	}

	if (!ok)
		g_remove (tmp_path);
	g_free (tmp_path);

	return ok;
}

/**
 * @brief Ajoute à l'ensemble les adresses d'un en-tête d'adresses (From).
 * @param set ensemble de jetons
 * @param address adresses du message (peut être NULL)
 */
static void
spam_collect_from (SpamDb *set,
		   CamelInternetAddress *address)
{
	gint ii, count;

	if (!address)
		return;

	count = camel_address_length (CAMEL_ADDRESS (address));
	for (ii = 0; ii < count && ii < 4; ii++) {
		const gchar *name = NULL, *email = NULL;

		if (camel_internet_address_get (address, ii, &name, &email)) {
			gchar *value = g_strdup_printf ("%s <%s>", name ? name : "", email ? email : "");

			spam_tokenize_address (set, value, "f:");
			g_free (value);
		}
	}
}

/**
 * @brief Ajoute à l'ensemble les noms d'hôtes des liens (href="…") d'un source HTML.
 * @param set ensemble de jetons
 * @param html source HTML
 */
static void
spam_collect_links (SpamDb *set,
		    const gchar *html)
{
	const gchar *cursor = html;

	while ((cursor = strstr (cursor, "href=")) != NULL) {
		const gchar *start = cursor + 5, *end;
		gchar *link;

		cursor = start;
		if (*start == '"' || *start == '\'')
			start++;

		end = start;
		while (*end && *end != '"' && *end != '\'' && *end != ' ' && *end != '>')
			end++;

		if (end > start) {
			link = g_strndup (start, MIN ((gsize) (end - start), 300));
			spam_tokenize (set, link, "u:");
			g_free (link);
		}
	}
}

/**
 * @brief Parcourt récursivement les parties d'un message et ajoute à l'ensemble les jetons
 *        des textes (brut ou HTML), des liens et des noms de pièces jointes.
 * @param core instance du moteur (annulation)
 * @param set ensemble de jetons
 * @param part partie MIME à parcourir
 * @param depth profondeur de récursion (bornée)
 */
static void
spam_collect_part (EvoCore *core,
		   SpamDb *set,
		   CamelMimePart *part,
		   gint depth)
{
	CamelContentType *type = camel_mime_part_get_content_type (part);
	CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
	const gchar *filename = camel_mime_part_get_filename (part);

	if (depth > 8)
		return;

	if (filename && *filename) {
		const gchar *dot = strrchr (filename, '.');

		if (dot)
			spam_tokenize (set, dot, "att:");
	}

	if (content && CAMEL_IS_MULTIPART (content)) {
		guint ii, count = camel_multipart_get_number (CAMEL_MULTIPART (content));

		for (ii = 0; ii < count; ii++) {
			CamelMimePart *child = camel_multipart_get_part (CAMEL_MULTIPART (content), ii);

			if (child)
				spam_collect_part (core, set, child, depth + 1);
		}
		return;
	}

	if (type && camel_content_type_is (type, "text", "plain")) {
		gchar *text = part_get_text (part, core->cancellable);

		spam_tokenize (set, text, NULL);
		g_free (text);
	} else if (type && camel_content_type_is (type, "text", "html")) {
		gchar *source = part_get_text (part, core->cancellable);
		gchar *limited = g_strndup (source, SPAM_MAX_HTML_BYTES);
		gchar *text;

		spam_collect_links (set, limited);
		text = html_to_text (limited);
		spam_tokenize (set, text, NULL);

		g_free (text);
		g_free (limited);
		g_free (source);
	}
}

/**
 * @brief Extrait les jetons d'un message : expéditeur, objet, en-têtes de logiciel d'envoi,
 *        corps, liens et pièces jointes.
 * @param core instance du moteur
 * @param message message Camel
 * @param set ensemble de jetons à remplir (initialisé par l'appelant)
 */
static void
spam_collect_message (EvoCore *core,
		      CamelMimeMessage *message,
		      SpamDb *set)
{
	const gchar *value;

	spam_collect_from (set, camel_mime_message_get_from (message));
	spam_tokenize (set, camel_mime_message_get_subject (message), "s:");

	value = camel_medium_get_header (CAMEL_MEDIUM (message), "X-Mailer");
	if (!value)
		value = camel_medium_get_header (CAMEL_MEDIUM (message), "User-Agent");
	spam_tokenize (set, value, "h:");

	spam_collect_part (core, set, CAMEL_MIME_PART (message), 0);
}

/**
 * @brief Clé de suivi d'un message appris : empreinte de son Message-ID (ou, à défaut, de son sujet).
 * @param message message Camel
 * @param out reçoit la clé
 * @param size taille de out
 */
static void
spam_message_key_of (CamelMimeMessage *message,
		     gchar *out,
		     gsize size)
{
	const gchar *id = camel_mime_message_get_message_id (message);

	if (!id || !*id)
		id = camel_mime_message_get_subject (message);

	spam_message_key (id, out, size);
}

/**
 * @brief Éclate une liste d'identifiants séparés par des sauts de ligne.
 * @param list liste (peut être NULL)
 * @return tableau NULL-terminé à libérer avec g_strfreev ()
 */
static gchar **
spam_split_uids (const gchar *list)
{
	return g_strsplit (list ? list : "", "\n", -1);
}

/**
 * @brief Résultat JSON des compteurs de la base (spam_lock tenu).
 * @return objet JSON {"spam", "ham", "tokens", "ready", "min_trained"}, à libérer avec g_free ()
 */
static gchar *
spam_stats_json_locked (void)
{
	JsonBuilder *builder = json_builder_new ();
	gchar *json;

	json_builder_begin_object (builder);
	builder_add_int (builder, "spam", spam_db.n_spam);
	builder_add_int (builder, "ham", spam_db.n_ham);
	builder_add_int (builder, "tokens", (gint64) spam_db_token_count (&spam_db));
	builder_add_boolean (builder, "ready", spam_db_ready (&spam_db));
	builder_add_int (builder, "min_trained", SPAM_MIN_TRAINED);
	json_builder_end_object (builder);

	json = json_from_builder (builder);
	g_object_unref (builder);

	return json;
}

/**
 * @brief Apprend les messages d'un dossier dans une catégorie.
 * @param core instance du moteur
 * @param folder dossier source
 * @param uids identifiants des messages
 * @param is_spam TRUE pour « indésirable », FALSE pour « légitime »
 * @param out_trained reçoit le nombre de messages réellement appris
 * @param out_failed reçoit le nombre de messages illisibles
 */
static void
spam_train_uids (EvoCore *core,
		 CamelFolder *folder,
		 gchar **uids,
		 gboolean is_spam,
		 gint *out_trained,
		 gint *out_failed)
{
	gint ii;

	for (ii = 0; uids[ii]; ii++) {
		CamelMimeMessage *message;
		SpamDb set;
		gchar key[32];

		if (!*uids[ii] || g_cancellable_is_cancelled (core->cancellable))
			continue;

		message = camel_folder_get_message_sync (folder, uids[ii], core->cancellable, NULL);
		if (!message) {
			(*out_failed)++;
			continue;
		}

		spam_db_init (&set);
		spam_collect_message (core, message, &set);
		spam_message_key_of (message, key, sizeof (key));

		g_mutex_lock (&spam_lock);
		spam_load_locked ();
		if (spam_db_train (&spam_db, &set, key, is_spam ? 1 : 0))
			(*out_trained)++;
		g_mutex_unlock (&spam_lock);

		spam_db_clear (&set);
		g_object_unref (message);
	}
}

/**
 * @brief Exécutant de evo_core_spam_train () : apprend des messages dans une catégorie.
 * @param core instance du moteur
 * @param args [0] compte, [1] dossier, [2] identifiants séparés par « \n », [3] « 1 » (indésirable) ou « 0 »
 * @param error erreur
 * @return JSON {"trained", "failed", "spam", "ham", "tokens", "ready"}
 */
static gchar *
core_spam_train_run (EvoCore *core,
		     gchar **args,
		     GError **error)
{
	CamelFolder *folder = core_ref_folder (core, args[0], args[1], error);
	gchar **uids;
	gint trained = 0, failed = 0;
	JsonBuilder *builder;
	gchar *json;

	if (!folder)
		return NULL;

	uids = spam_split_uids (args[2]);
	spam_train_uids (core, folder, uids, g_strcmp0 (args[3], "1") == 0, &trained, &failed);
	g_strfreev (uids);
	g_object_unref (folder);

	g_mutex_lock (&spam_lock);
	spam_load_locked ();
	if (trained > 0)
		spam_save_locked ();

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_int (builder, "trained", trained);
	builder_add_int (builder, "failed", failed);
	builder_add_int (builder, "spam", spam_db.n_spam);
	builder_add_int (builder, "ham", spam_db.n_ham);
	builder_add_int (builder, "tokens", (gint64) spam_db_token_count (&spam_db));
	builder_add_boolean (builder, "ready", spam_db_ready (&spam_db));
	json_builder_end_object (builder);
	g_mutex_unlock (&spam_lock);

	json = json_from_builder (builder);
	g_object_unref (builder);

	return json;
}

/* Identifiant d'un message et sa date, pour trier du plus récent au plus ancien */
typedef struct {
	gchar *uid;
	gint64 date;
} SpamUidDate;

/**
 * @brief Compare deux messages par date décroissante (plus récent d'abord).
 * @param a premier SpamUidDate
 * @param b second SpamUidDate
 * @return résultat de comparaison pour g_array_sort ()
 */
static gint
spam_compare_uid_date (gconstpointer a,
		       gconstpointer b)
{
	const SpamUidDate *ua = a, *ub = b;

	return ua->date < ub->date ? 1 : (ua->date > ub->date ? -1 : 0);
}

/**
 * @brief Exécutant de evo_core_spam_train_folder () : apprend les messages les plus récents d'un dossier.
 * @param core instance du moteur
 * @param args [0] compte, [1] dossier, [2] « 1 » (indésirable) ou « 0 », [3] nombre maximal de messages
 * @param error erreur
 * @return JSON comme core_spam_train_run ()
 */
static gchar *
core_spam_train_folder_run (EvoCore *core,
			    gchar **args,
			    GError **error)
{
	CamelFolder *folder = core_ref_folder (core, args[0], args[1], error);
	GPtrArray *all;
	GArray *dated;
	gchar **uids;
	guint ii, limit = (guint) MAX (1, atoi (args[3] ? args[3] : "200"));
	gchar *joined, *json;
	gchar *train_args[5];

	if (!folder)
		return NULL;

	all = camel_folder_dup_uids (folder);
	dated = g_array_new (FALSE, FALSE, sizeof (SpamUidDate));

	for (ii = 0; all && ii < all->len; ii++) {
		CamelMessageInfo *info = camel_folder_get_message_info (folder, all->pdata[ii]);
		SpamUidDate item;

		if (!info)
			continue;

		item.date = camel_message_info_get_date_received (info);
		if (item.date <= 0)
			item.date = camel_message_info_get_date_sent (info);
		item.uid = g_strdup (all->pdata[ii]);
		g_array_append_val (dated, item);
		g_object_unref (info);
	}

	g_array_sort (dated, spam_compare_uid_date);

	uids = g_new0 (gchar *, MIN (limit, dated->len) + 1);
	for (ii = 0; ii < dated->len; ii++) {
		if (ii < limit)
			uids[ii] = g_array_index (dated, SpamUidDate, ii).uid;
		else
			g_free (g_array_index (dated, SpamUidDate, ii).uid);
	}
	g_array_unref (dated);
	if (all)
		g_ptr_array_unref (all);
	g_object_unref (folder);

	joined = g_strjoinv ("\n", uids);
	g_strfreev (uids);

	train_args[0] = args[0];
	train_args[1] = args[1];
	train_args[2] = joined;
	train_args[3] = args[2];
	train_args[4] = NULL;
	json = core_spam_train_run (core, train_args, error);
	g_free (joined);

	return json;
}

/**
 * @brief Exécutant de evo_core_spam_score () : score d'un message.
 * @param core instance du moteur
 * @param args [0] compte, [1] dossier, [2] identifiant du message
 * @param error erreur
 * @return JSON {"score", "junk", "ready", "significant"}
 */
static gchar *
core_spam_score_run (EvoCore *core,
		     gchar **args,
		     GError **error)
{
	CamelFolder *folder = core_ref_folder (core, args[0], args[1], error);
	CamelMimeMessage *message;
	SpamDb set;
	JsonBuilder *builder;
	gchar *json;
	gint significant = 0;
	gdouble score;
	gboolean ready;

	if (!folder)
		return NULL;

	message = camel_folder_get_message_sync (folder, args[2], core->cancellable, error);
	g_object_unref (folder);
	if (!message)
		return NULL;

	spam_db_init (&set);
	spam_collect_message (core, message, &set);
	g_object_unref (message);

	g_mutex_lock (&spam_lock);
	spam_load_locked ();
	score = spam_score (&spam_db, &set, &significant);
	ready = spam_db_ready (&spam_db);
	g_mutex_unlock (&spam_lock);
	spam_db_clear (&set);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "score");
	json_builder_add_double_value (builder, score);
	builder_add_boolean (builder, "junk", ready && score * 100.0 >= spam_threshold);
	builder_add_boolean (builder, "ready", ready);
	builder_add_int (builder, "significant", significant);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	return json;
}

/**
 * @brief Exécutant de evo_core_spam_stats () : compteurs de la base.
 * @return JSON {"spam", "ham", "tokens", "ready", "min_trained"}
 */
static gchar *
core_spam_stats_run (EvoCore *core,
		     gchar **args,
		     GError **error)
{
	gchar *json;

	g_mutex_lock (&spam_lock);
	spam_load_locked ();
	json = spam_stats_json_locked ();
	g_mutex_unlock (&spam_lock);

	return json;
}

/**
 * @brief Exécutant de evo_core_spam_reset () : efface tout l'apprentissage (mémoire et fichier).
 * @return JSON des compteurs après remise à zéro
 */
static gchar *
core_spam_reset_run (EvoCore *core,
		     gchar **args,
		     GError **error)
{
	gchar *json;

	g_mutex_lock (&spam_lock);
	spam_load_locked ();
	spam_db_clear (&spam_db);
	if (!spam_path)
		spam_path = spam_default_path ();
	g_remove (spam_path);
	json = spam_stats_json_locked ();
	g_mutex_unlock (&spam_lock);

	return json;
}

/**
 * @brief Exécutant du classement automatique : examine les nouveaux messages d'un dossier et
 *        déplace vers Indésirables ceux dont le score atteint le seuil.
 * @param core instance du moteur
 * @param args [0] compte, [1] dossier, [2] identifiants séparés par « \n »
 * @param error erreur
 * @return JSON {"moved": n}
 */
static gchar *
core_spam_classify_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	CamelFolder *folder, *junk_folder = NULL;
	CamelStore *store;
	CamelFolderInfo *fi;
	gchar *junk_name = NULL;
	gchar **uids;
	GPtrArray *to_move;
	gint ii, moved = 0;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	store = camel_folder_get_parent_store (folder);
	fi = store ? camel_store_get_folder_info_sync (store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST, core->cancellable, NULL) : NULL;
	if (fi) {
		junk_name = core_find_role_folder (core, store, fi, "junk");
		camel_folder_info_free (fi);
	}

	if (junk_name && g_strcmp0 (junk_name, args[1]) != 0)
		junk_folder = core_ref_folder (core, args[0], junk_name, NULL);

	if (!junk_folder) {
		g_free (junk_name);
		g_object_unref (folder);
		return g_strdup ("{\"moved\":0}");
	}

	uids = spam_split_uids (args[2]);
	to_move = g_ptr_array_new_with_free_func (g_free);

	for (ii = 0; uids[ii]; ii++) {
		CamelMessageInfo *info;
		CamelMimeMessage *message;
		SpamDb set;
		SpamEntry *mark;
		gchar key[32];
		gboolean junk;
		guint32 flags;

		if (!*uids[ii] || g_cancellable_is_cancelled (core->cancellable))
			continue;

		info = camel_folder_get_message_info (folder, uids[ii]);
		if (!info)
			continue;
		flags = camel_message_info_get_flags (info);
		g_object_unref (info);
		if (flags & (CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_FLAGGED))
			continue;

		message = camel_folder_get_message_sync (folder, uids[ii], core->cancellable, NULL);
		if (!message)
			continue;

		spam_db_init (&set);
		spam_collect_message (core, message, &set);
		spam_message_key_of (message, key, sizeof (key));
		g_object_unref (message);

		g_mutex_lock (&spam_lock);
		spam_load_locked ();
		/* un message que l'utilisateur a déclaré légitime n'est jamais reclassé */
		mark = spam_db_lookup (&spam_db, key);
		junk = spam_enabled && spam_db_ready (&spam_db) && !(mark && mark->ham) &&
			spam_score (&spam_db, &set, NULL) * 100.0 >= spam_threshold;
		g_mutex_unlock (&spam_lock);
		spam_db_clear (&set);

		if (junk)
			g_ptr_array_add (to_move, g_strdup (uids[ii]));
	}
	g_strfreev (uids);

	if (to_move->len > 0 &&
	    camel_folder_transfer_messages_to_sync (folder, to_move, junk_folder, TRUE, NULL, core->cancellable, NULL)) {
		JsonBuilder *builder = core_event_builder ();

		moved = (gint) to_move->len;
		camel_folder_synchronize_sync (folder, TRUE, core->cancellable, NULL);
		camel_folder_synchronize_sync (junk_folder, FALSE, core->cancellable, NULL);

		builder_add_string (builder, "account", args[0]);
		builder_add_string (builder, "folder", args[1]);
		builder_add_string (builder, "junk_folder", junk_name);
		builder_add_int (builder, "count", moved);
		core_emit_builder (core, "spam-filtered", builder);
	}

	g_ptr_array_unref (to_move);
	g_free (junk_name);
	g_object_unref (junk_folder);
	g_object_unref (folder);

	return g_strdup_printf ("{\"moved\":%d}", moved);
}

/**
 * @brief Soumet le classement automatique des messages arrivés dans un dossier (appelée par
 *        core_folder_changed_cb ()). Sans effet si le filtre est désactivé ou pas assez entraîné.
 * @param core instance du moteur
 * @param folder dossier où des messages sont arrivés
 * @param added identifiants des messages ajoutés
 */
void
core_spam_classify_added (EvoCore *core,
			  CamelFolder *folder,
			  GPtrArray *added)
{
	CamelStore *store = camel_folder_get_parent_store (folder);
	gboolean active;
	gchar *joined;
	gchar *args[4];
	GString *list;
	guint ii;

	if (!store || !added || added->len == 0)
		return;

	g_mutex_lock (&spam_lock);
	spam_load_locked ();
	active = spam_enabled && spam_db_ready (&spam_db);
	g_mutex_unlock (&spam_lock);

	if (!active)
		return;

	list = g_string_new (NULL);
	for (ii = 0; ii < added->len; ii++) {
		if (ii > 0)
			g_string_append_c (list, '\n');
		g_string_append (list, added->pdata[ii]);
	}
	joined = g_string_free (list, FALSE);

	args[0] = (gchar *) camel_service_get_uid (CAMEL_SERVICE (store));
	args[1] = (gchar *) camel_folder_get_full_name (folder);
	args[2] = joined;
	args[3] = NULL;
	core_submit_background (core, core_spam_classify_run, g_strdupv (args), core_ignore_result, NULL);

	g_free (joined);
}

/**
 * @brief Configure le filtre anti-spam (appel synchrone, à refaire à chaque changement de réglage).
 *        Le changement de fichier de base force un rechargement à la prochaine utilisation.
 * @param core instance du moteur (non utilisée, conservée pour la cohérence de l'API)
 * @param db_path chemin UTF-8 du fichier de la base, ou NULL / vide pour le défaut
 * @param enabled non nul pour activer le classement automatique
 * @param threshold_percent score minimal (en %, 50 à 99) pour classer un message en indésirable
 */
void
evo_core_spam_configure (EvoCore *core,
			 const char *db_path,
			 int enabled,
			 int threshold_percent)
{
	g_mutex_lock (&spam_lock);

	if (db_path && *db_path && g_strcmp0 (db_path, spam_path) != 0) {
		g_free (spam_path);
		spam_path = g_strdup (db_path);
		spam_loaded = FALSE;
	}

	spam_enabled = enabled != 0;
	spam_threshold = CLAMP (threshold_percent, 50, 99);

	g_mutex_unlock (&spam_lock);
}

/**
 * @brief Apprend des messages comme indésirables ou légitimes.
 * @param core instance du moteur
 * @param account_uid compte
 * @param folder_full_name dossier contenant les messages
 * @param message_uids identifiants des messages, séparés par des sauts de ligne
 * @param is_spam non nul : indésirables ; 0 : légitimes
 * @param func rappel recevant {"trained", "failed", "spam", "ham", "tokens", "ready"}
 * @param user_data données du rappel
 */
void
evo_core_spam_train (EvoCore *core,
		     const char *account_uid,
		     const char *folder_full_name,
		     const char *message_uids,
		     int is_spam,
		     EvoCoreResultFunc func,
		     void *user_data)
{
	gchar *args[] = {
		(gchar *) account_uid, (gchar *) folder_full_name, (gchar *) message_uids,
		is_spam ? "1" : "0", NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uids != NULL);

	core_submit (core, core_spam_train_run, g_strdupv (args), func, user_data);
}

/**
 * @brief Apprend les messages les plus récents d'un dossier (tâche de fond).
 * @param core instance du moteur
 * @param account_uid compte
 * @param folder_full_name dossier
 * @param is_spam non nul : indésirables ; 0 : légitimes
 * @param max_messages nombre maximal de messages (les plus récents)
 * @param func rappel recevant le même JSON que evo_core_spam_train ()
 * @param user_data données du rappel
 */
void
evo_core_spam_train_folder (EvoCore *core,
			    const char *account_uid,
			    const char *folder_full_name,
			    int is_spam,
			    int max_messages,
			    EvoCoreResultFunc func,
			    void *user_data)
{
	gchar *max_text = g_strdup_printf ("%d", max_messages);
	gchar *args[] = {
		(gchar *) account_uid, (gchar *) folder_full_name, is_spam ? "1" : "0", max_text, NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);

	core_submit_background (core, core_spam_train_folder_run, g_strdupv (args), func, user_data);
	g_free (max_text);
}

/**
 * @brief Calcule le score d'indésirabilité d'un message.
 * @param core instance du moteur
 * @param account_uid compte
 * @param folder_full_name dossier
 * @param message_uid identifiant du message
 * @param func rappel recevant {"score": 0..1, "junk": bool, "ready": bool, "significant": n}
 * @param user_data données du rappel
 */
void
evo_core_spam_score (EvoCore *core,
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

	core_submit (core, core_spam_score_run, g_strdupv (args), func, user_data);
}

/**
 * @brief Compteurs de l'apprentissage.
 * @param core instance du moteur
 * @param func rappel recevant {"spam", "ham", "tokens", "ready", "min_trained"}
 * @param user_data données du rappel
 */
void
evo_core_spam_stats (EvoCore *core,
		     EvoCoreResultFunc func,
		     void *user_data)
{
	gchar *args[] = { NULL };

	core_submit (core, core_spam_stats_run, g_strdupv (args), func, user_data);
}

/**
 * @brief Efface tout l'apprentissage (base en mémoire et fichier).
 * @param core instance du moteur
 * @param func rappel recevant les compteurs remis à zéro
 * @param user_data données du rappel
 */
void
evo_core_spam_reset (EvoCore *core,
		     EvoCoreResultFunc func,
		     void *user_data)
{
	gchar *args[] = { NULL };

	core_submit (core, core_spam_reset_run, g_strdupv (args), func, user_data);
}
