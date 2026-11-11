/*
 * evo-core : File des requetes asynchrones, magasins Camel et connexion des comptes.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

typedef struct {
	RequestFunc func;
	gchar **args;
	EvoCoreResultFunc result_func;
	gpointer user_data;
	const gchar *name;	/* nom de la fonction exécutante (journal) */
	gint64 queued_at;	/* instant de mise en file (µs, horloge monotone) */
	gboolean background;	/* tâche de fond (pool bg_pool) : priorité de thread réduite */
} Request;

/* attente maximale de la synchronisation en fond devant une action de l'utilisateur (ms) */
#define CORE_BG_YIELD_MAX_MS 3000

/* Cède la place aux actions de l'utilisateur : tant qu'une requête de l'interface (lecture
 * d'un message, liste d'un dossier...) s'exécute, la synchronisation en fond attend, pour ne
 * pas se disputer avec elle le serveur et la base de Camel. À appeler entre deux étapes d'une
 * tâche de fond (dossier, compte). L'attente est bornée : la synchronisation avance toujours.
 * Paramètres :
 *   core : instance du moteur evo-core */
void
core_bg_yield (EvoCore *core)
{
	gint64 started_at = g_get_monotonic_time ();

	while (g_atomic_int_get (&core->user_requests) > 0 &&
	       !g_cancellable_is_cancelled (core->cancellable) &&
	       (g_get_monotonic_time () - started_at) / 1000 < CORE_BG_YIELD_MAX_MS)
		g_usleep (50 * 1000);

	if (core_startup_tracing () && (g_get_monotonic_time () - started_at) >= 300 * 1000) {
		gchar *step = g_strdup_printf ("synchronisation en pause %" G_GINT64_FORMAT " ms (action de l'utilisateur)",
			(g_get_monotonic_time () - started_at) / 1000);

		evo_core_trace (step);
		g_free (step);
	}
}

void
core_request_run (gpointer data,
		  gpointer user_data)
{
	Request *request = data;
	EvoCore *core = user_data;
	gchar *json = NULL;
	gchar *start_error = NULL;
	GError *error = NULL;

	int previous_priority = THREAD_PRIORITY_NORMAL;

	g_atomic_int_inc (&core->running_requests);
	if (request->background) {
		/* synchronisation en fond : le processeur va d'abord à l'interface */
		previous_priority = GetThreadPriority (GetCurrentThread ());
		SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);
	} else {
		g_atomic_int_inc (&core->user_requests);
	}

	if (!core_wait_started (core, &start_error)) {
		request->result_func (NULL, start_error, request->user_data);
		g_free (start_error);
	} else if (g_cancellable_is_cancelled (core->cancellable)) {
		/* shutting down: no callback */
	} else {
		gint64 started_at = g_get_monotonic_time ();

		json = request->func (core, request->args, &error);

		/* démarrage : requêtes lentes (attente dans la file ou exécution), pour repérer
		 * ce qui retarde l'affichage initial (voir tools\analyse-demarrage.ps1) */
		{
			gint64 waited_ms = (started_at - request->queued_at) / 1000;
			gint64 ran_ms = (g_get_monotonic_time () - started_at) / 1000;

			if (core_startup_tracing () && (waited_ms >= 300 || ran_ms >= 1000)) {
				gchar *step = g_strdup_printf ("requête %s : attente %" G_GINT64_FORMAT " ms, exécution %" G_GINT64_FORMAT " ms",
					request->name ? request->name : "?", waited_ms, ran_ms);

				evo_core_trace (step);
				g_free (step);
			} else if (!request->background && (waited_ms >= 2000 || ran_ms >= 5000)) {
				/* après le démarrage : seules les requêtes de l'interface vraiment lentes */
				g_message ("requête lente %s : attente %" G_GINT64_FORMAT " ms, exécution %" G_GINT64_FORMAT " ms",
					request->name ? request->name : "?", waited_ms, ran_ms);
			}
		}

		if (json)
			request->result_func (json, NULL, request->user_data);
		else
			request->result_func (NULL, error ? error->message : "Unknown error", request->user_data);
	}

	g_clear_error (&error);
	g_free (json);
	g_strfreev (request->args);
	if (request->background)
		SetThreadPriority (GetCurrentThread (), previous_priority);
	else
		g_atomic_int_dec_and_test (&core->user_requests);

	g_free (request);

	g_atomic_int_dec_and_test (&core->running_requests);
}

/* Fonction de rappel neutre qui ignore le résultat d'une requête asynchrone.
 * Paramètres :
 *   json          : chaîne JSON éventuelle (ignorée)
 *   error_message : message d'erreur éventuel (ignoré)
 *   user_data     : données utilisateur éventuelles (ignorées) */
void
core_ignore_result (const char *json,
		    const char *error_message,
		    void *user_data)
{
}

/* Met une requête dans un pool de threads.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   background  : TRUE pour le pool des tâches de fond, FALSE pour le pool principal
 *   func, name, args, result_func, user_data : voir core_submit_named () */
static void
core_push_request (EvoCore *core,
		   gboolean background,
		   RequestFunc func,
		   const gchar *name,
		   gchar **args,
		   EvoCoreResultFunc result_func,
		   gpointer user_data)
{
	Request *request;

	g_return_if_fail (core != NULL);
	g_return_if_fail (result_func != NULL);

	/* arrêt en cours : aucune nouvelle requête (pas de rappel, comme à l'arrêt) */
	if (g_atomic_int_get (&core->shutting_down)) {
		g_strfreev (args);
		return;
	}

	request = g_new0 (Request, 1);
	request->func = func;
	request->args = args;
	request->result_func = result_func;
	request->user_data = user_data;
	request->name = name;
	request->queued_at = g_get_monotonic_time ();
	request->background = background;

	g_thread_pool_push (background ? core->bg_pool : core->pool, request, NULL);
}

void
core_submit_named (EvoCore *core,
		   RequestFunc func,
		   const gchar *name,
		   gchar **args,
		   EvoCoreResultFunc result_func,
		   gpointer user_data)
{
	core_push_request (core, FALSE, func, name, args, result_func, user_data);
}

void
core_submit_background_named (EvoCore *core,
			     RequestFunc func,
			     const gchar *name,
			     gchar **args,
			     EvoCoreResultFunc result_func,
			     gpointer user_data)
{
	core_push_request (core, TRUE, func, name, args, result_func, user_data);
}

CamelStore *
core_ref_store (EvoCore *core,
		const gchar *account_uid,
		GError **error)
{
	CamelService *service;

	service = camel_session_ref_service (core->session, account_uid);

	if (!CAMEL_IS_STORE (service)) {
		g_clear_object (&service);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Unknown account “%s”", account_uid);
		return NULL;
	}

	return CAMEL_STORE (service);
}

void
core_connect_lock_free (gpointer data)
{
	g_mutex_clear (data);
	g_free (data);
}

/* Verrou de connexion d'un magasin (créé à la demande).
 * Paramètres :
 *   core    : instance du moteur evo-core
 *   service : magasin de courrier
 * Retourne : verrou propre à ce magasin (appartient au moteur). */
static GMutex *
core_store_connect_lock (EvoCore *core,
			 CamelService *service)
{
	const gchar *uid = camel_service_get_uid (service);
	GMutex *lock;

	g_mutex_lock (&core->connect_lock);
	lock = g_hash_table_lookup (core->connect_locks, uid);
	if (!lock) {
		lock = g_new0 (GMutex, 1);
		g_mutex_init (lock);
		g_hash_table_insert (core->connect_locks, g_strdup (uid), lock);
	}
	g_mutex_unlock (&core->connect_lock);

	return lock;
}

gboolean
core_ensure_connected (EvoCore *core,
		       CamelStore *store,
		       GError **error)
{
	gboolean success = TRUE;
	GMutex *store_lock;

	if (!CAMEL_IS_NETWORK_SERVICE (store))
		return TRUE;

	/* One connection at a time for a given store, and connected on return: while
	 * another thread is connecting it, camel_offline_store_set_online_sync () returns
	 * success at once, and an IMAP refresh started then is skipped without
	 * error, e.g. an empty inbox on a first run (startup refresh and the
	 * message list connecting together). Les comptes différents se connectent en
	 * parallèle : la connexion lente d'un compte (Microsoft 365, Gmail) ne retarde pas
	 * l'affichage d'un autre. */
	store_lock = core_store_connect_lock (core, CAMEL_SERVICE (store));
	g_mutex_lock (store_lock);

	/* offline-capable stores (IMAP) refuse to work until set online;
	 * this also connects them */
	if (CAMEL_IS_OFFLINE_STORE (store) && !camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		success = camel_offline_store_set_online_sync (CAMEL_OFFLINE_STORE (store), TRUE, core->cancellable, error);

	if (success && camel_service_get_connection_status (CAMEL_SERVICE (store)) != CAMEL_SERVICE_CONNECTED)
		success = camel_service_connect_sync (CAMEL_SERVICE (store), core->cancellable, error);

	g_mutex_unlock (store_lock);

	return success;
}

/* --- accounts --- */

gchar *
core_account_address (EvoCore *core,
		      const gchar *uid)
{
	ESource *source, *identity = NULL;
	gchar *address = NULL;

	source = e_source_registry_ref_source (core->registry, uid);

	if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		ESourceMailAccount *extension;
		gchar *identity_uid;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
		identity_uid = e_source_mail_account_dup_identity_uid (extension);
		if (identity_uid)
			identity = e_source_registry_ref_source (core->registry, identity_uid);
		g_free (identity_uid);
	}

	if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY))
		address = e_source_mail_identity_dup_address (
			e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY));

	g_clear_object (&identity);
	g_clear_object (&source);

	return address;
}
