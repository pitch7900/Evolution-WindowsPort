/*
 * evo-core : plages de synchronisation hors connexion.
 *   - par compte : CamelOfflineSettings (stay-synchronized, limit-by-age,
 *     limit-unit, limit-value), enregistrés dans la source EDS du compte ;
 *   - par dossier : CamelOfflineFolder (état à trois valeurs « hériter du
 *     compte » / « toujours » / « jamais »), conservé dans l'état du dossier.
 * Le serveur IMAP (et les magasins Camel qui l'acceptent) télécharge alors les
 * messages du dossier lors de la relève, en respectant la limite d'ancienneté.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Nom JSON d'une unité de durée de Camel. */
static const gchar *
offline_unit_name (CamelTimeUnit unit)
{
	switch (unit) {
	case CAMEL_TIME_UNIT_WEEKS:
		return "weeks";
	case CAMEL_TIME_UNIT_MONTHS:
		return "months";
	case CAMEL_TIME_UNIT_YEARS:
		return "years";
	default:
		return "days";
	}
}

/* Unité de durée de Camel d'après son nom JSON (jours par défaut). */
static CamelTimeUnit
offline_unit_from_name (const gchar *name)
{
	if (g_strcmp0 (name, "weeks") == 0)
		return CAMEL_TIME_UNIT_WEEKS;
	if (g_strcmp0 (name, "months") == 0)
		return CAMEL_TIME_UNIT_MONTHS;
	if (g_strcmp0 (name, "years") == 0)
		return CAMEL_TIME_UNIT_YEARS;
	return CAMEL_TIME_UNIT_DAYS;
}

/* Réglages hors connexion de la source d'un compte ; NULL si le protocole du
 * compte n'en a pas. Le résultat appartient à l'extension de la source. */
static CamelOfflineSettings *
offline_ref_settings (ESource *account)
{
	ESourceMailAccount *mail_account;
	const gchar *backend;
	ESourceCamel *camel_extension;
	CamelSettings *settings;

	if (!e_source_has_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT))
		return NULL;

	mail_account = e_source_get_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	backend = e_source_backend_get_backend_name (E_SOURCE_BACKEND (mail_account));
	if (!backend || !*backend)
		return NULL;

	if (!e_source_has_extension (account, e_source_camel_get_extension_name (backend)))
		return NULL;

	camel_extension = e_source_get_extension (account, e_source_camel_get_extension_name (backend));
	settings = e_source_camel_get_settings (camel_extension);

	return CAMEL_IS_OFFLINE_SETTINGS (settings) ? CAMEL_OFFLINE_SETTINGS (settings) : NULL;
}

/* Lecture des réglages hors connexion d'un compte et, si folder n'est pas
 * vide, de l'état de synchronisation de ce dossier.
 *   args : [0] = uid du compte, [1] = nom complet du dossier ("" : compte seul) */
static gchar *
core_get_offline_sync_run (EvoCore *core,
			   gchar **args,
			   GError **error)
{
	ESource *account;
	CamelOfflineSettings *settings;
	JsonBuilder *builder;
	gchar *json;

	account = e_source_registry_ref_source (core->registry, args[0]);
	if (!account) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Compte introuvable : « %s »", args[0]);
		return NULL;
	}

	settings = offline_ref_settings (account);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "account");
	json_builder_add_string_value (builder, args[0]);
	json_builder_set_member_name (builder, "supported");
	json_builder_add_boolean_value (builder, settings != NULL);
	json_builder_set_member_name (builder, "stay_synchronized");
	json_builder_add_boolean_value (builder, settings && camel_offline_settings_get_stay_synchronized (settings));
	json_builder_set_member_name (builder, "limit_by_age");
	json_builder_add_boolean_value (builder, settings && camel_offline_settings_get_limit_by_age (settings));
	json_builder_set_member_name (builder, "limit_unit");
	json_builder_add_string_value (builder, settings ?
		offline_unit_name (camel_offline_settings_get_limit_unit (settings)) : "months");
	json_builder_set_member_name (builder, "limit_value");
	json_builder_add_int_value (builder, settings ? camel_offline_settings_get_limit_value (settings) : 3);

	if (args[1] && *args[1]) {
		CamelFolder *folder = core_ref_folder (core, args[0], args[1], error);
		const gchar *state = "inherit";

		if (!folder) {
			g_object_unref (builder);
			g_object_unref (account);
			return NULL;
		}

		if (CAMEL_IS_OFFLINE_FOLDER (folder)) {
			switch (camel_offline_folder_get_offline_sync (CAMEL_OFFLINE_FOLDER (folder))) {
			case CAMEL_THREE_STATE_ON:
				state = "on";
				break;
			case CAMEL_THREE_STATE_OFF:
				state = "off";
				break;
			default:
				break;
			}
		}
		json_builder_set_member_name (builder, "folder");
		json_builder_add_string_value (builder, args[1]);
		json_builder_set_member_name (builder, "folder_supported");
		json_builder_add_boolean_value (builder, CAMEL_IS_OFFLINE_FOLDER (folder));
		json_builder_set_member_name (builder, "folder_offline_sync");
		json_builder_add_string_value (builder, state);
		g_object_unref (folder);
	}

	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	g_object_unref (account);

	return json;
}

/* Enregistre les réglages hors connexion d'un compte et/ou l'état d'un dossier.
 *   args : [0] = JSON {"account", "stay_synchronized"?, "limit_by_age"?,
 *          "limit_unit"?, "limit_value"?, "folder"?, "folder_offline_sync"?} */
static gchar *
core_set_offline_sync_run (EvoCore *core,
			   gchar **args,
			   GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *account_uid, *folder_name, *state;
	ESource *account = NULL;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account_uid = object ? json_object_get_string_member_with_default (object, "account", "") : "";
	if (!account_uid || !*account_uid) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Identifiant du compte manquant");
		goto exit;
	}

	account = e_source_registry_ref_source (core->registry, account_uid);
	if (!account) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Compte introuvable : « %s »", account_uid);
		goto exit;
	}

	if (json_object_has_member (object, "stay_synchronized") || json_object_has_member (object, "limit_by_age") ||
	    json_object_has_member (object, "limit_unit") || json_object_has_member (object, "limit_value")) {
		CamelOfflineSettings *settings = offline_ref_settings (account);

		if (!settings) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				"Ce type de compte ne propose pas de synchronisation hors connexion");
			goto exit;
		}

		if (json_object_has_member (object, "stay_synchronized"))
			camel_offline_settings_set_stay_synchronized (settings,
				json_object_get_boolean_member (object, "stay_synchronized"));
		if (json_object_has_member (object, "limit_by_age"))
			camel_offline_settings_set_limit_by_age (settings,
				json_object_get_boolean_member (object, "limit_by_age"));
		if (json_object_has_member (object, "limit_unit"))
			camel_offline_settings_set_limit_unit (settings,
				offline_unit_from_name (json_object_get_string_member (object, "limit_unit")));
		if (json_object_has_member (object, "limit_value")) {
			gint64 value = json_object_get_int_member (object, "limit_value");

			camel_offline_settings_set_limit_value (settings, (guint) CLAMP (value, 1, 9999));
		}

		if (!e_source_registry_commit_source_sync (core->registry, account, core->cancellable, error))
			goto exit;
	}

	folder_name = json_object_get_string_member_with_default (object, "folder", "");
	state = json_object_get_string_member_with_default (object, "folder_offline_sync", "");
	if (folder_name && *folder_name && state && *state) {
		CamelFolder *folder = core_ref_folder (core, account_uid, folder_name, error);

		if (!folder)
			goto exit;

		if (!CAMEL_IS_OFFLINE_FOLDER (folder)) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				"Ce dossier ne propose pas de synchronisation hors connexion");
			g_object_unref (folder);
			goto exit;
		}

		camel_offline_folder_set_offline_sync (CAMEL_OFFLINE_FOLDER (folder),
			g_strcmp0 (state, "on") == 0 ? CAMEL_THREE_STATE_ON :
			g_strcmp0 (state, "off") == 0 ? CAMEL_THREE_STATE_OFF : CAMEL_THREE_STATE_INCONSISTENT);
		/* propriété persistante : Camel l'écrit avec l'état du dossier */
		g_object_unref (folder);
	}

	json = g_strdup ("{}");

exit:
	g_clear_object (&account);
	json_node_unref (root);

	return json;
}

/* Lit les réglages de synchronisation hors connexion d'un compte (et d'un dossier).
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant du compte
 *   folder_name : nom complet du dossier, "" pour le compte seul
 *   func        : rappel recevant {"account", "supported", "stay_synchronized", "limit_by_age",
 *                 "limit_unit": "days"|"weeks"|"months"|"years", "limit_value",
 *                 et si folder_name est donné : "folder", "folder_supported",
 *                 "folder_offline_sync": "inherit"|"on"|"off"}
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_get_offline_sync (EvoCore *core,
			   const char *account_uid,
			   const char *folder_name,
			   EvoCoreResultFunc func,
			   void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) (folder_name ? folder_name : ""), NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_get_offline_sync_run, g_strdupv (args), func, user_data);
}

/* Enregistre les réglages de synchronisation hors connexion.
 * Paramètres :
 *   core          : instance du moteur evo-core
 *   settings_json : {"account": "<uid>", "stay_synchronized": bool, "limit_by_age": bool,
 *                    "limit_unit": "days"|"weeks"|"months"|"years", "limit_value": n,
 *                    "folder": "<nom complet>", "folder_offline_sync": "inherit"|"on"|"off"}
 *                   (seuls les champs présents sont modifiés)
 *   func          : rappel recevant {}
 *   user_data     : données utilisateur transmises au rappel */
void
evo_core_set_offline_sync (EvoCore *core,
			   const char *settings_json,
			   EvoCoreResultFunc func,
			   void *user_data)
{
	gchar *args[] = { (gchar *) settings_json, NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (settings_json != NULL);

	core_submit (core, core_set_offline_sync_run, g_strdupv (args), func, user_data);
}
