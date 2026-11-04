/*
 * evo-core : Constructeurs JSON et emission des evenements vers l'interface.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* emits event with the object built so far, and frees the builder */
void
core_emit_builder (EvoCore *core,
		   const gchar *event,
		   JsonBuilder *builder)
{
	gchar *json;

	json_builder_end_object (builder);
	json = json_from_builder (builder);

	if (core->event_func)
		core->event_func (event, json, core->event_data);

	g_free (json);
	g_object_unref (builder);
}

JsonBuilder *
core_event_builder (void)
{
	JsonBuilder *builder = json_builder_new ();

	json_builder_begin_object (builder);

	return builder;
}

void
builder_add_string (JsonBuilder *builder,
		    const gchar *name,
		    const gchar *value)
{
	json_builder_set_member_name (builder, name);
	json_builder_add_string_value (builder, value ? value : "");
}

void
builder_add_boolean (JsonBuilder *builder,
		     const gchar *name,
		     gboolean value)
{
	json_builder_set_member_name (builder, name);
	json_builder_add_boolean_value (builder, value);
}

void
builder_add_int (JsonBuilder *builder,
		 const gchar *name,
		 gint64 value)
{
	json_builder_set_member_name (builder, name);
	json_builder_add_int_value (builder, value);
}

void
core_emit (EvoCore *core,
	   const gchar *event,
	   const gchar *message)
{
	JsonBuilder *builder = core_event_builder ();

	if (message)
		builder_add_string (builder, "message", message);

	core_emit_builder (core, event, builder);
}

/* Émet un événement de progression pour la barre d'état de l'interface.
 * Paramètres :
 *   core    : instance du moteur evo-core
 *   account : identifiant du compte concerné (ou NULL si non spécifique)
 *   folder  : nom complet du dossier concerné (ou NULL)
 *   message : libellé de l'étape en cours
 *   percent : pourcentage d'avancement (0..100, ou -1 pour indéterminé)
 *   active  : TRUE si une opération est en cours, FALSE si elle est terminée */
void
core_emit_progress (EvoCore *core,
		    const gchar *account,
		    const gchar *folder,
		    const gchar *message,
		    gint percent,
		    gboolean active)
{
	JsonBuilder *builder = core_event_builder ();

	if (account)
		builder_add_string (builder, "account", account);
	if (folder)
		builder_add_string (builder, "folder", folder);
	if (message)
		builder_add_string (builder, "message", message);
	json_builder_set_member_name (builder, "percent");
	json_builder_add_int_value (builder, percent);
	json_builder_set_member_name (builder, "active");
	json_builder_add_boolean_value (builder, active);

	core_emit_builder (core, "progress", builder);
}

/* Émet un événement d'état de connexion d'un compte ou du moteur global.
 * Paramètres :
 *   core    : instance du moteur evo-core
 *   account : identifiant du compte concerné (ou NULL si global)
 *   service : type de service ("imap", "smtp", "global")
 *   status  : état ("connected", "connecting", "offline", "auth-required", "error")
 *   message : libellé explicatif de l'état */
void
core_emit_connection_status (EvoCore *core,
			     const gchar *account,
			     const gchar *service,
			     const gchar *status,
			     const gchar *message)
{
	JsonBuilder *builder = core_event_builder ();

	if (account)
		builder_add_string (builder, "account", account);
	if (service)
		builder_add_string (builder, "service", service);
	if (status)
		builder_add_string (builder, "status", status);
	if (message)
		builder_add_string (builder, "message", message);

	core_emit_builder (core, "connection-status", builder);
}

void
core_add_stores (EvoCore *core)
{
	GList *sources, *link;

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_MAIL_ACCOUNT);

	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;
		ESourceBackend *extension;
		const gchar *backend_name;
		CamelService *service;
		GError *error = NULL;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
		backend_name = e_source_backend_get_backend_name (extension);

		/* search folders need Evolution's vfolder rules, news feeds its
		 * RSS plug-in */
		if (!backend_name || g_strcmp0 (backend_name, "none") == 0 ||
		    g_strcmp0 (backend_name, "vfolder") == 0 ||
		    g_strcmp0 (backend_name, "rss") == 0)
			continue;

		service = camel_session_add_service (core->session, e_source_get_uid (source),
			backend_name, CAMEL_PROVIDER_STORE, &error);

		if (error) {
			g_warning ("Cannot add account “%s” (%s): %s",
				e_source_get_display_name (source), backend_name, error->message);
			g_clear_error (&error);
		}

		g_clear_object (&service);
	}

	g_list_free_full (sources, g_object_unref);

	/* outgoing servers (SMTP), referenced by the identities */
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_MAIL_TRANSPORT);

	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;
		const gchar *backend_name;
		CamelService *service;
		GError *error = NULL;

		backend_name = e_source_backend_get_backend_name (
			e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_TRANSPORT));

		/* no sendmail on Windows */
		if (!backend_name || g_strcmp0 (backend_name, "none") == 0 ||
		    g_strcmp0 (backend_name, "sendmail") == 0)
			continue;

		service = camel_session_add_service (core->session, e_source_get_uid (source),
			backend_name, CAMEL_PROVIDER_TRANSPORT, &error);

		if (error) {
			g_warning ("Cannot add outgoing server “%s” (%s): %s",
				e_source_get_display_name (source), backend_name, error->message);
			g_clear_error (&error);
		}

		g_clear_object (&service);
	}

	g_list_free_full (sources, g_object_unref);
}
