/*
 * evo-core : Cycle de vie du moteur : journal, chargement des modules EDS, creation et
 * arret du moteur, fil principal GLib, mode hors connexion.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Messages and warnings of the engine and of Camel/EDS go to
 * %LOCALAPPDATA%\EvolutionWin\evo-core.log (a GUI application has no
 * console); debug messages only with EVO_CORE_DEBUG=1. */

static FILE *log_file;

G_LOCK_DEFINE_STATIC (log_file);

static GLogWriterOutput
core_log_writer (GLogLevelFlags log_level,
		 const GLogField *fields,
		 gsize n_fields,
		 gpointer user_data)
{
	const gchar *domain = NULL, *message = NULL;
	GDateTime *now;
	gchar *stamp;
	gsize ii;

	if ((log_level & G_LOG_LEVEL_DEBUG) && g_strcmp0 (g_getenv ("EVO_CORE_DEBUG"), "1") != 0)
		return G_LOG_WRITER_HANDLED;

	for (ii = 0; ii < n_fields; ii++) {
		if (g_strcmp0 (fields[ii].key, "GLIB_DOMAIN") == 0)
			domain = fields[ii].value;
		else if (g_strcmp0 (fields[ii].key, "MESSAGE") == 0)
			message = fields[ii].value;
	}

	now = g_date_time_new_now_local ();
	stamp = g_date_time_format (now, "%Y-%m-%d %H:%M:%S");

	G_LOCK (log_file);
	if (log_file) {
		fprintf (log_file, "%s [%s] %s%s\n", stamp, domain ? domain : "-",
			(log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL)) ? "CRITICAL: " :
			(log_level & G_LOG_LEVEL_WARNING) ? "WARNING: " :
			(log_level & G_LOG_LEVEL_DEBUG) ? "debug: " : "",
			message ? message : "");
		fflush (log_file);
	}
	G_UNLOCK (log_file);

	g_free (stamp);
	g_date_time_unref (now);

	return G_LOG_WRITER_HANDLED;
}

static void
core_log_open (void)
{
	const gchar *local_app_data = g_getenv ("LOCALAPPDATA");
	gchar *dir, *path;

	if (log_file || !local_app_data)
		return;

	dir = g_build_filename (local_app_data, "EvolutionWin", NULL);
	g_mkdir_with_parents (dir, 0700);
	path = g_build_filename (dir, "evo-core.log", NULL);

	/* the previous session is kept as .old */
	{
		gchar *old = g_strconcat (path, ".old", NULL);
		g_remove (old);
		g_rename (path, old);
		g_free (old);
	}

	log_file = g_fopen (path, "w");
	if (log_file)
		g_log_set_writer_func (core_log_writer, NULL, NULL);

	/* CAMEL_DEBUG (e.g. "imapx") makes Camel print protocol traces on
	 * stdout, which a GUI application does not have: into a file */
	if (g_getenv ("CAMEL_DEBUG") && *g_getenv ("CAMEL_DEBUG") && _fileno (stdout) < 0) {
		gchar *trace = g_build_filename (dir, "imap-trace.log", NULL);
		gunichar2 *wtrace = g_utf8_to_utf16 (trace, -1, NULL, NULL, NULL);
		FILE *redirected = NULL;

		if (wtrace && _wfreopen_s (&redirected, (const wchar_t *) wtrace, L"w", stdout) == 0)
			setvbuf (stdout, NULL, _IONBF, 0);

		g_free (wtrace);
		g_free (trace);
	}

	g_free (path);
	g_free (dir);
}

/* The OAuth2 services (EOAuth2Service implementations) are types registered
 * by registry modules; ESourceRegistry collects them when it is created, so
 * load them first. Microsoft 365 (Graph) and EWS come from evolution-ews,
 * Google/Outlook.com from EDS; each one is optional. The modules stay
 * loaded for the life of the process. */
static void
core_load_oauth2_modules (void)
{
	const gchar *names[] = {
		"module-microsoft365-backend",
		"module-ews-backend",
		"module-oauth2-services"
	};
	gchar *dir;
	guint ii;

	dir = g_build_filename (e_util_get_prefix (), "lib", "evolution-data-server", "registry-modules", NULL);

	for (ii = 0; ii < G_N_ELEMENTS (names); ii++) {
		gchar *filename = g_strconcat (names[ii], "." G_MODULE_SUFFIX, NULL);
		gchar *path = g_build_filename (dir, filename, NULL);

		if (g_file_test (path, G_FILE_TEST_IS_REGULAR) && !e_module_load_file (path))
			g_warning ("Cannot load OAuth2 module “%s”", path);

		g_free (path);
		g_free (filename);
	}

	g_free (dir);
}

/* The EDS services (libexec\*.exe), started by evo-core, inherit its
 * environment: put the folder of evo-core.dll, where the installed
 * application keeps every DLL (bin\), first in PATH so that they find
 * their DLLs without any system-wide PATH change. */
static void
core_add_module_dir_to_path (void)
{
	HMODULE module = NULL;
	wchar_t wpath[MAX_PATH], *slash;
	const wchar_t *old_path;
	gsize dir_len;

	if (!GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				 (LPCWSTR) core_add_module_dir_to_path, &module) ||
	    !GetModuleFileNameW (module, wpath, G_N_ELEMENTS (wpath)))
		return;

	slash = wcsrchr (wpath, L'\\');
	if (!slash)
		return;
	*slash = L'\0';
	dir_len = wcslen (wpath);

	old_path = _wgetenv (L"PATH");

	/* already first: nothing to do */
	if (old_path && _wcsnicmp (old_path, wpath, dir_len) == 0 &&
	    (old_path[dir_len] == L';' || old_path[dir_len] == L'\0'))
		return;

	{
		gsize len = dir_len + 2 + (old_path ? wcslen (old_path) : 0);
		wchar_t *new_path = g_new (wchar_t, len);

		_snwprintf_s (new_path, len, _TRUNCATE, L"%s;%s", wpath, old_path ? old_path : L"");
		/* both the CRT copy (getenv, GLib) and the process block (CreateProcessW) */
		_wputenv_s (L"PATH", new_path);
		SetEnvironmentVariableW (L"PATH", new_path);
		g_free (new_path);
	}
}

gchar *
json_from_builder (JsonBuilder *builder)
{
	JsonNode *root = json_builder_get_root (builder);
	gchar *json = json_to_string (root, FALSE);

	json_node_unref (root);

	return json;
}

#define REFRESH_INTERVAL_SECONDS (5 * 60)

static gpointer
core_thread_func (gpointer user_data)
{
	EvoCore *core = user_data;
	GError *error = NULL;

	g_main_context_push_thread_default (core->context);

	camel_init (e_get_user_data_dir (), TRUE);
	e_source_camel_register_types ();
	evo_core_trace ("moteur : Camel initialisé");

	core_emit (core, "status", "Connecting to evolution-data-server");

	core_load_oauth2_modules ();
	evo_core_trace ("moteur : modules OAuth2 chargés");

	/* starts evolution-source-registry when needed */
	core->registry = e_source_registry_new_sync (core->cancellable, &error);
	evo_core_trace ("moteur : registre des sources prêt (evolution-source-registry)");

	if (core->registry) {
		core->session = evo_core_session_new (core, core->registry);
		core_add_stores (core);
		evo_core_trace ("moteur : magasins de courrier ajoutés");
		g_signal_connect (core->registry, "credentials-required",
			G_CALLBACK (core_credentials_required_cb), core);
		core_check_pending_credentials (core);
	}

	g_mutex_lock (&core->lock);
	core->started = TRUE;
	if (error)
		core->start_error = g_strdup (error->message);
	g_cond_broadcast (&core->cond);
	g_mutex_unlock (&core->lock);

	if (error) {
		core_emit (core, "error", error->message);
		g_clear_error (&error);
	} else {
		evo_core_trace ("moteur : événement ready émis");
		core_emit (core, "ready", NULL);

		/* new mail: now, then periodically */
		evo_core_refresh (core);

		core->refresh_source = g_timeout_source_new_seconds (REFRESH_INTERVAL_SECONDS);
		g_source_set_callback (core->refresh_source, core_refresh_timeout_cb, core, NULL);
		g_source_attach (core->refresh_source, core->context);
	}

	g_main_loop_run (core->loop);

	if (core->refresh_source) {
		g_source_destroy (core->refresh_source);
		g_clear_pointer (&core->refresh_source, g_source_unref);
	}

	g_main_context_pop_thread_default (core->context);

	return NULL;
}

/* waits for the startup; FALSE and error_message when it failed */
gboolean
core_wait_started (EvoCore *core,
		   gchar **error_message)
{
	gboolean success;

	g_mutex_lock (&core->lock);
	while (!core->started)
		g_cond_wait (&core->cond, &core->lock);
	success = core->start_error == NULL;
	if (!success)
		*error_message = g_strdup (core->start_error);
	g_mutex_unlock (&core->lock);

	return success;
}

/* Millisecondes écoulées depuis la création du processus (même horloge pour le
 * moteur et l'interface, donc des repères comparables). */
static gint64
core_process_age_ms (void)
{
	FILETIME created, exited, kernel, user, now;
	ULARGE_INTEGER start, current;

	if (!GetProcessTimes (GetCurrentProcess (), &created, &exited, &kernel, &user))
		return -1;
	GetSystemTimeAsFileTime (&now);
	start.LowPart = created.dwLowDateTime;
	start.HighPart = created.dwHighDateTime;
	current.LowPart = now.dwLowDateTime;
	current.HighPart = now.dwHighDateTime;

	return (gint64) ((current.QuadPart - start.QuadPart) / 10000);
}

/* TRUE pendant les 90 premières secondes du processus : les requêtes lentes et les
 * étapes de démarrage sont alors journalisées (evo_core_trace). */
gboolean
core_startup_tracing (void)
{
	gint64 age = core_process_age_ms ();

	return age >= 0 && age < 90000;
}

/* Repère chronométré du démarrage : « démarrage +1234 ms : étape » dans le journal
 * (voir evo-core.h ; analyse par tools\analyse-demarrage.ps1).
 * Paramètres :
 *   step : libellé de l'étape franchie */
void
evo_core_trace (const char *step)
{
	core_log_open ();
	g_message ("démarrage +%" G_GINT64_FORMAT " ms : %s", core_process_age_ms (), step ? step : "");
}

EvoCore *
evo_core_new (EvoCoreEventFunc event_func,
	      void *user_data)
{
	EvoCore *core = g_new0 (EvoCore, 1);

	core_log_open ();
	g_message ("evo-core starting");
	evo_core_trace ("moteur : création");
	core_add_module_dir_to_path ();

	core->event_func = event_func;
	core->event_data = user_data;
	core->context = g_main_context_new ();
	core->loop = g_main_loop_new (core->context, FALSE);
	core->cancellable = g_cancellable_new ();
	g_mutex_init (&core->lock);
	g_cond_init (&core->cond);
	g_mutex_init (&core->folders_lock);
	g_mutex_init (&core->connect_lock);
	g_mutex_init (&core->refreshing_lock);
	core->refreshing = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	core->connect_locks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, core_connect_lock_free);
	core->folders =g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	core->oauth2_pending = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, core_oauth2_pending_free);
	core->book_clients = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	/* network requests run in parallel, a few at a time */
	core->pool = g_thread_pool_new (core_request_run, core, 4, FALSE, NULL);
	/* tâches de fond lentes : leur propre pool, pour ne jamais retarder l'affichage */
	core->bg_pool = g_thread_pool_new (core_request_run, core, 4, FALSE, NULL);
	core->thread = g_thread_new ("evo-core", core_thread_func, core);

	return core;
}

static gboolean
core_quit_cb (gpointer user_data)
{
	g_main_loop_quit (user_data);
	return G_SOURCE_REMOVE;
}

/* durée maximale d'attente des requêtes en cours à l'arrêt (ms) */
#define CORE_SHUTDOWN_WAIT_MS 1500

void
evo_core_free (EvoCore *core)
{
	gint64 started_at = g_get_monotonic_time ();

	if (!core)
		return;

	g_atomic_int_set (&core->shutting_down, TRUE);
	g_message ("arrêt : début (%d requête(s) en cours)", g_atomic_int_get (&core->running_requests));

	g_cancellable_cancel (core->cancellable);

	/* les requêtes en attente sont abandonnées ; celles en cours reçoivent l'annulation, mais
	 * une synchronisation de gros dossier ou une connexion réseau peut tarder à réagir : on ne
	 * les attend que quelques secondes (sinon la fermeture de l'application gèle) */
	g_thread_pool_free (core->pool, TRUE, FALSE);
	core->pool = NULL;
	g_thread_pool_free (core->bg_pool, TRUE, FALSE);
	core->bg_pool = NULL;

	while (g_atomic_int_get (&core->running_requests) > 0 &&
	       (g_get_monotonic_time () - started_at) / 1000 < CORE_SHUTDOWN_WAIT_MS)
		g_usleep (20 * 1000);

	if (g_atomic_int_get (&core->running_requests) > 0) {
		/* des threads utilisent encore le moteur : on ne le libère pas (le processus se termine) */
		g_message ("arrêt : %d requête(s) encore en cours après %" G_GINT64_FORMAT " ms, libération abandonnée",
			g_atomic_int_get (&core->running_requests), (g_get_monotonic_time () - started_at) / 1000);
		return;
	}
	g_message ("arrêt : requêtes terminées en %" G_GINT64_FORMAT " ms", (g_get_monotonic_time () - started_at) / 1000);

	g_main_context_invoke (core->context, core_quit_cb, core->loop);
	g_thread_join (core->thread);

	/* no "folder-changed" after this point */
	{
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init (&iter, core->folders);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			g_signal_handlers_disconnect_by_func (value, core_folder_changed_cb, core);
	}
	g_hash_table_destroy (core->folders);
	g_mutex_clear (&core->folders_lock);
	g_hash_table_destroy (core->connect_locks);
	g_hash_table_destroy (core->refreshing);
	g_mutex_clear (&core->refreshing_lock);
	g_mutex_clear (&core->connect_lock);

	g_clear_object (&core->session);
	g_clear_object (&core->registry);
	g_main_loop_unref (core->loop);
	g_main_context_unref (core->context);
	g_object_unref (core->cancellable);
	g_hash_table_destroy (core->oauth2_pending);
	g_hash_table_destroy (core->book_clients);
	g_mutex_clear (&core->lock);
	g_cond_clear (&core->cond);
	g_free (core->start_error);
	g_message ("arrêt : moteur libéré en %" G_GINT64_FORMAT " ms", (g_get_monotonic_time () - started_at) / 1000);
	g_free (core);
}

void
evo_core_cancel_operations (EvoCore *core)
{
	JsonBuilder *builder;

	g_return_if_fail (core != NULL);

	if (core->cancellable) {
		g_cancellable_cancel (core->cancellable);
		g_object_unref (core->cancellable);
		core->cancellable = g_cancellable_new ();
	}

	builder = core_event_builder ();
	builder_add_string (builder, "message", "Opération annulée");
	core_emit_builder (core, "cancelled", builder);
}

/* Active ou désactive le mode hors connexion global du moteur.
 * Paramètres :
 *   core    : instance du moteur evo-core
 *   offline : 1 pour travailler hors connexion, 0 pour revenir en ligne */
void
evo_core_set_offline (EvoCore *core,
		      int offline)
{
	g_return_if_fail (core != NULL);

	if (core->session) {
		camel_session_set_online (core->session, !offline);
		core_emit_connection_status (core, NULL, "global",
			offline ? "offline" : "online",
			offline ? "Mode hors connexion activé" : "Connexion en cours...");

		if (!offline) {
			core_process_outbox (core);
			evo_core_refresh (core);
		}
	}
}

/* Renvoie 1 si le moteur est actuellement en mode hors connexion, 0 sinon.
 * Paramètres :
 *   core : instance du moteur evo-core
 * Retourne : 1 si hors connexion, 0 si en ligne. */
int
evo_core_get_offline (EvoCore *core)
{
	if (!core || !core->session)
		return 0;

	return !camel_session_get_online (core->session);
}

const char *
evo_core_version_info (void)
{
	static gchar *json = NULL;

	if (g_once_init_enter (&json)) {
		JsonBuilder *builder = json_builder_new ();
		gchar *eds, *glib, *soup;

		eds = g_strdup_printf ("%d.%d.%d", EDS_MAJOR_VERSION, EDS_MINOR_VERSION, EDS_MICRO_VERSION);
		glib = g_strdup_printf ("%u.%u.%u", glib_major_version, glib_minor_version, glib_micro_version);
		soup = g_strdup_printf ("%u.%u.%u", soup_get_major_version (), soup_get_minor_version (), soup_get_micro_version ());

		json_builder_begin_object (builder);
		builder_add_string (builder, "evolution_data_server", eds);
		/* only when installed: its Camel provider is found */
		builder_add_string (builder, "evolution_ews",
			camel_provider_get ("microsoft365", NULL) ? EVO_CORE_EWS_VERSION : "");
		builder_add_string (builder, "glib", glib);
		builder_add_string (builder, "libsoup", soup);
		json_builder_end_object (builder);

		g_once_init_leave (&json, json_from_builder (builder));

		g_object_unref (builder);
		g_free (eds);
		g_free (glib);
		g_free (soup);
	}

	return json;
}
