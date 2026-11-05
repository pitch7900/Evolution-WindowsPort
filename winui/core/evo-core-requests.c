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
} Request;

void
core_request_run (gpointer data,
		  gpointer user_data)
{
	Request *request = data;
	EvoCore *core = user_data;
	gchar *json = NULL;
	gchar *start_error = NULL;
	GError *error = NULL;

	if (!core_wait_started (core, &start_error)) {
		request->result_func (NULL, start_error, request->user_data);
		g_free (start_error);
	} else if (g_cancellable_is_cancelled (core->cancellable)) {
		/* shutting down: no callback */
	} else {
		json = request->func (core, request->args, &error);

		if (json)
			request->result_func (json, NULL, request->user_data);
		else
			request->result_func (NULL, error ? error->message : "Unknown error", request->user_data);
	}

	g_clear_error (&error);
	g_free (json);
	g_strfreev (request->args);
	g_free (request);
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

void
core_submit (EvoCore *core,
	     RequestFunc func,
	     gchar **args,
	     EvoCoreResultFunc result_func,
	     gpointer user_data)
{
	Request *request;

	g_return_if_fail (core != NULL);
	g_return_if_fail (result_func != NULL);

	request = g_new0 (Request, 1);
	request->func = func;
	request->args = args;
	request->result_func = result_func;
	request->user_data = user_data;

	g_thread_pool_push (core->pool, request, NULL);
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

gboolean
core_ensure_connected (EvoCore *core,
		       CamelStore *store,
		       GError **error)
{
	gboolean success = TRUE;

	if (!CAMEL_IS_NETWORK_SERVICE (store))
		return TRUE;

	/* One connection at a time, and connected on return: while another
	 * thread is connecting, camel_offline_store_set_online_sync () returns
	 * success at once, and an IMAP refresh started then is skipped without
	 * error, e.g. an empty inbox on a first run (startup refresh and the
	 * message list connecting together). */
	g_mutex_lock (&core->connect_lock);

	/* offline-capable stores (IMAP) refuse to work until set online;
	 * this also connects them */
	if (CAMEL_IS_OFFLINE_STORE (store) && !camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		success = camel_offline_store_set_online_sync (CAMEL_OFFLINE_STORE (store), TRUE, core->cancellable, error);

	if (success && camel_service_get_connection_status (CAMEL_SERVICE (store)) != CAMEL_SERVICE_CONNECTED)
		success = camel_service_connect_sync (CAMEL_SERVICE (store), core->cancellable, error);

	g_mutex_unlock (&core->connect_lock);

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
