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

/* Nombre de secondes d'une limite d'ancienneté (mois = 30 jours, année = 365 jours). */
static gint64
offline_limit_seconds (CamelOfflineSettings *settings)
{
	gint64 value = camel_offline_settings_get_limit_value (settings);

	switch (camel_offline_settings_get_limit_unit (settings)) {
	case CAMEL_TIME_UNIT_WEEKS:
		return value * 7 * 86400;
	case CAMEL_TIME_UNIT_MONTHS:
		return value * 30 * 86400;
	case CAMEL_TIME_UNIT_YEARS:
		return value * 365 * 86400;
	default:
		return value * 86400;
	}
}

/* Noms complets des dossiers à parcourir (sélectionnables, non virtuels). */
static void
offline_collect_folders (CamelFolderInfo *fi,
			 GPtrArray *names)
{
	for (; fi; fi = fi->next) {
		if (!(fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)))
			g_ptr_array_add (names, g_strdup (fi->full_name));
		offline_collect_folders (fi->child, names);
	}
}

/* Émet la progression du téléchargement hors connexion (événement « progress » de
 * type « offline » : l'interface compose le texte dans la langue de l'utilisateur). */
static void
offline_emit_progress (EvoCore *core,
		       const gchar *account_uid,
		       const gchar *folder_name,
		       guint folder_index,
		       guint folder_count,
		       guint done,
		       guint total,
		       gboolean active)
{
	JsonBuilder *builder = core_event_builder ();
	gdouble fraction = folder_count > 0 ?
		((gdouble) folder_index + (total > 0 ? (gdouble) done / total : 1.0)) / folder_count : 1.0;

	builder_add_string (builder, "account", account_uid);
	builder_add_string (builder, "kind", "offline");
	builder_add_string (builder, "folder", folder_name ? folder_name : "");
	builder_add_int (builder, "folder_index", (gint64) folder_index + 1);
	builder_add_int (builder, "folder_count", folder_count);
	builder_add_int (builder, "done", done);
	builder_add_int (builder, "total", total);
	builder_add_int (builder, "percent", active ? (gint64) (fraction * 100.0) : 100);
	builder_add_boolean (builder, "active", active);
	core_emit_builder (core, "progress", builder);
}

/* Télécharge pour le mode hors connexion les messages d'un compte (ou d'un dossier),
 * selon les réglages : état du dossier (toujours / jamais / comme le compte), limite
 * d'ancienneté. Émet des événements « progress » de type « offline ».
 *   args : [0] = uid du compte, [1] = nom complet du dossier ("" : tous les dossiers) */
static gchar *
core_offline_sync_now_run (EvoCore *core,
			   gchar **args,
			   GError **error)
{
	CamelStore *store;
	ESource *account;
	CamelOfflineSettings *settings;
	GPtrArray *names;
	gboolean explicit_folder = args[1] && *args[1];
	gboolean stay;
	gchar *expression = NULL, *json = NULL;
	guint ii, synced_folders = 0, total_missing = 0, downloaded = 0, failed = 0;
	JsonBuilder *builder;

	store = core_ref_store (core, args[0], error);
	if (!store)
		return NULL;
	if (!core_ensure_connected (core, store, error)) {
		g_object_unref (store);
		return NULL;
	}

	account = e_source_registry_ref_source (core->registry, args[0]);
	settings = account ? offline_ref_settings (account) : NULL;
	if (!settings) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Ce type de compte ne propose pas de synchronisation hors connexion");
		g_clear_object (&account);
		g_object_unref (store);
		return NULL;
	}

	stay = camel_offline_settings_get_stay_synchronized (settings);
	if (camel_offline_settings_get_limit_by_age (settings))
		expression = g_strdup_printf ("(match-all (> (get-received-date) (- (get-current-date) %" G_GINT64_FORMAT ")))",
			offline_limit_seconds (settings));

	names = g_ptr_array_new_with_free_func (g_free);
	if (explicit_folder) {
		g_ptr_array_add (names, g_strdup (args[1]));
	} else {
		CamelFolderInfo *fi = camel_store_get_folder_info_sync (store, NULL,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE, core->cancellable, NULL);

		if (fi) {
			offline_collect_folders (fi, names);
			camel_folder_info_free (fi);
		}
	}

	for (ii = 0; ii < names->len && !g_cancellable_is_cancelled (core->cancellable); ii++) {
		const gchar *folder_name = names->pdata[ii];
		CamelFolder *folder = camel_store_get_folder_sync (store, folder_name, 0, core->cancellable, NULL);
		CamelThreeState state;
		GPtrArray *uids = NULL, *missing;
		guint jj, consecutive_errors = 0;

		if (!folder)
			continue;
		if (!CAMEL_IS_OFFLINE_FOLDER (folder)) {
			g_object_unref (folder);
			continue;
		}

		state = camel_offline_folder_get_offline_sync (CAMEL_OFFLINE_FOLDER (folder));
		if (state == CAMEL_THREE_STATE_OFF || (state != CAMEL_THREE_STATE_ON && !stay && !explicit_folder)) {
			g_object_unref (folder);
			continue;
		}

		offline_emit_progress (core, args[0], folder_name, ii, names->len, 0, 0, TRUE);
		camel_folder_refresh_info_sync (folder, core->cancellable, NULL);

		if (expression) {
			if (!camel_folder_search_sync (folder, expression, &uids, core->cancellable, NULL))
				uids = NULL;
		} else {
			uids = camel_folder_dup_uids (folder);
		}

		missing = g_ptr_array_new ();
		for (jj = 0; uids && jj < uids->len; jj++) {
			CamelMimeMessage *cached = camel_folder_get_message_cached (folder, uids->pdata[jj], core->cancellable);

			if (cached)
				g_object_unref (cached);
			else
				g_ptr_array_add (missing, uids->pdata[jj]);
		}
		total_missing += missing->len;
		synced_folders++;

		for (jj = 0; jj < missing->len && !g_cancellable_is_cancelled (core->cancellable); jj++) {
			GError *local_error = NULL;

			if (camel_folder_synchronize_message_sync (folder, missing->pdata[jj], core->cancellable, &local_error)) {
				downloaded++;
				consecutive_errors = 0;
			} else {
				failed++;
				g_clear_error (&local_error);
				/* connexion perdue : inutile d'insister sur ce dossier */
				if (++consecutive_errors >= 5)
					break;
			}
			if (jj % 10 == 0 || jj + 1 == missing->len)
				offline_emit_progress (core, args[0], folder_name, ii, names->len, jj + 1, missing->len, TRUE);
		}

		g_ptr_array_free (missing, TRUE);
		if (uids)
			g_ptr_array_unref (uids);
		g_object_unref (folder);
	}

	offline_emit_progress (core, args[0], "", names->len, names->len, 0, 0, FALSE);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_int (builder, "folders", synced_folders);
	builder_add_int (builder, "messages", total_missing);
	builder_add_int (builder, "downloaded", downloaded);
	builder_add_int (builder, "failed", failed);
	builder_add_boolean (builder, "cancelled", g_cancellable_is_cancelled (core->cancellable));
	json_builder_end_object (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_ptr_array_unref (names);
	g_free (expression);
	g_object_unref (account);
	g_object_unref (store);

	return json;
}

/* Lance le téléchargement hors connexion d'un compte ou d'un dossier, sans attendre la relève
 * périodique. Les dossiers « jamais » sont ignorés ; ceux « comme le compte » suivent le réglage
 * du compte (sauf si un dossier précis est demandé). Progression : événements « progress » avec
 * "kind": "offline", "folder", "folder_index", "folder_count", "done", "total", "percent", "active".
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant du compte
 *   folder_name : nom complet du dossier, "" pour tous les dossiers du compte
 *   func        : rappel recevant {"folders", "messages" (à télécharger), "downloaded", "failed",
 *                 "cancelled"}
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_offline_sync_now (EvoCore *core,
			   const char *account_uid,
			   const char *folder_name,
			   EvoCoreResultFunc func,
			   void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) (folder_name ? folder_name : ""), NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_offline_sync_now_run, g_strdupv (args), func, user_data);
}
