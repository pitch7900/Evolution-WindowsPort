/*
 * evo-core : Signatures de courrier (ESourceMailSignature).
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* Crée ou modifie une signature : nom, type MIME et contenu (fichier
 * signatures/<uid> du dossier de configuration d'EDS).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON {"uid" ("" pour créer), "name", "mime_type", "content"}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"uid": "..."} en cas de succès, NULL sinon. */
static gchar *
core_save_signature_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	JsonNode *root;
	JsonObject *object;
	ESource *source = NULL;
	ESourceMailSignature *extension;
	const gchar *uid, *name, *mime_type, *content;
	gboolean is_new, success = FALSE;
	JsonBuilder *builder;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		json_node_unref (root);
		return NULL;
	}

	uid = json_object_get_string_member_with_default (object, "uid", "");
	name = json_object_get_string_member_with_default (object, "name", "");
	mime_type = json_object_get_string_member_with_default (object, "mime_type", "text/plain");
	content = json_object_get_string_member_with_default (object, "content", "");
	is_new = !uid || !*uid;

	if (!name || !*name) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "La signature n'a pas de nom");
		goto exit;
	}

	if (is_new) {
		source = e_source_new (NULL, NULL, error);
		if (!source)
			goto exit;
	} else {
		source = e_source_registry_ref_source (core->registry, uid);
		if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_SIGNATURE)) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Signature introuvable : « %s »", uid);
			goto exit;
		}
	}

	e_source_set_display_name (source, name);
	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_SIGNATURE);
	e_source_mail_signature_set_mime_type (extension,
		mime_type && g_ascii_strcasecmp (mime_type, "text/html") == 0 ? "text/html" : "text/plain");

	/* le fichier (nommé d'après l'uid) existe avant que la source soit publiée */
	if (!e_source_mail_signature_replace_sync (source, content ? content : "", content ? strlen (content) : 0,
						   core->cancellable, error))
		goto exit;

	if (is_new)
		success = e_source_registry_commit_source_sync (core->registry, source, core->cancellable, error);
	else
		success = e_source_write_sync (source, core->cancellable, error);

	if (success) {
		builder = json_builder_new ();
		json_builder_begin_object (builder);
		builder_add_string (builder, "uid", e_source_get_uid (source));
		json_builder_end_object (builder);
		json = json_from_builder (builder);
		g_object_unref (builder);
	} else if (is_new) {
		/* source non créée : pas de fichier orphelin */
		g_file_delete (e_source_mail_signature_get_file (extension), NULL, NULL);
	}

 exit:
	g_clear_object (&source);
	json_node_unref (root);

	return json;
}

/* Supprime une signature et son fichier ; les identités qui l'utilisaient
 * passent à « aucune signature ».
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid de la signature
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_delete_signature_run (EvoCore *core,
			   gchar **args,
			   GError **error)
{
	ESource *source;
	GFile *file;
	GList *identities, *link;

	source = e_source_registry_ref_source (core->registry, args[0]);
	if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_SIGNATURE)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Signature introuvable : « %s »", args[0]);
		g_clear_object (&source);
		return NULL;
	}

	file = g_object_ref (e_source_mail_signature_get_file (
		e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_SIGNATURE)));

	if (!e_source_remove_sync (source, core->cancellable, error)) {
		g_object_unref (file);
		g_object_unref (source);
		return NULL;
	}

	g_file_delete (file, NULL, NULL);
	g_object_unref (file);

	identities = e_source_registry_list_sources (core->registry, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	for (link = identities; link; link = g_list_next (link)) {
		ESource *identity = link->data;
		ESourceMailIdentity *extension = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
		gchar *signature_uid = e_source_mail_identity_dup_signature_uid (extension);

		if (g_strcmp0 (signature_uid, args[0]) == 0 && e_source_get_writable (identity)) {
			e_source_mail_identity_set_signature_uid (extension, "none");
			e_source_write_sync (identity, core->cancellable, NULL);
		}

		g_free (signature_uid);
	}
	g_list_free_full (identities, g_object_unref);

	g_object_unref (source);

	return g_strdup ("{}");
}

/* Choisit la signature de l'identité d'un compte (nouveaux messages).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte, [1] = "none", "autogenerated" ou uid d'une signature
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_set_account_signature_run (EvoCore *core,
				gchar **args,
				GError **error)
{
	const gchar *signature_uid = args[1] && *args[1] ? args[1] : "none";
	ESource *identity;
	ESourceMailIdentity *extension;
	gboolean success;

	if (g_strcmp0 (signature_uid, "none") != 0 && g_strcmp0 (signature_uid, "autogenerated") != 0) {
		ESource *signature = e_source_registry_ref_source (core->registry, signature_uid);
		gboolean found = signature && e_source_has_extension (signature, E_SOURCE_EXTENSION_MAIL_SIGNATURE);

		g_clear_object (&signature);
		if (!found) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Signature introuvable : « %s »", signature_uid);
			return NULL;
		}
	}

	identity = core_ref_identity (core, args[0]);
	if (!identity || !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Ce compte n'a pas d'identité d'expédition");
		g_clear_object (&identity);
		return NULL;
	}

	extension = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	e_source_mail_identity_set_signature_uid (extension, signature_uid);
	success = e_source_write_sync (identity, core->cancellable, error);

	g_object_unref (identity);

	return success ? g_strdup ("{}") : NULL;
}

/* Soumet une requête de création ou de modification d'une signature.
 * Paramètres :
 *   core           : instance du moteur evo-core
 *   signature_json : JSON {"uid" ("" pour créer), "name", "mime_type", "content"}
 *   func           : rappel recevant {"uid": "..."}
 *   user_data      : données utilisateur transmises au rappel */
void
evo_core_save_signature (EvoCore *core,
			 const char *signature_json,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) signature_json, NULL };

	g_return_if_fail (signature_json != NULL);

	core_submit (core, core_save_signature_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de suppression d'une signature.
 * Paramètres :
 *   core          : instance du moteur evo-core
 *   signature_uid : uid de la signature
 *   func          : rappel recevant {}
 *   user_data     : données utilisateur transmises au rappel */
void
evo_core_delete_signature (EvoCore *core,
			   const char *signature_uid,
			   EvoCoreResultFunc func,
			   void *user_data)
{
	gchar *args[] = { (gchar *) signature_uid, NULL };

	g_return_if_fail (signature_uid != NULL);

	core_submit (core, core_delete_signature_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de choix de la signature d'un compte.
 * Paramètres :
 *   core          : instance du moteur evo-core
 *   account_uid   : uid du compte
 *   signature_uid : "none", "autogenerated" ou uid d'une signature
 *   func          : rappel recevant {}
 *   user_data     : données utilisateur transmises au rappel */
void
evo_core_set_account_signature (EvoCore *core,
				const char *account_uid,
				const char *signature_uid,
				EvoCoreResultFunc func,
				void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) (signature_uid ? signature_uid : "none"), NULL };

	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_set_account_signature_run, g_strdupv (args), func, user_data);
}
