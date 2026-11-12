/*
 * evo-core : Carnets d'adresses : contacts et autocompletion des destinataires.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"
#include "evo-core-conflict-util.h"

/* --- contacts (EBookClient / libebook-1.2) --- */

typedef struct {
	gchar *uid;
	gchar *full_name;
	gchar *email;
	gchar *phone;
	gchar *org;
	gchar *notes;
	gchar *book_uid;
	gchar *book_name;
	gchar **categories;	/* noms de catégories (NULL-terminé) */
	gchar *revision;	/* jeton de révision (détection des conflits de modification) */
} ContactItem;

/* Catégories d'un contact (champ vCard CATEGORIES : valeurs séparées par des
 * virgules).
 * Paramètres :
 *   contact : contact evolution-data-server
 * Retourne : tableau de chaînes terminé par NULL (g_strfreev), jamais NULL. */
static gchar **
contact_dup_categories (EContact *contact)
{
	const gchar *value = e_contact_get_const (contact, E_CONTACT_CATEGORIES);
	GPtrArray *names = g_ptr_array_new ();

	if (value && *value) {
		gchar **parts = g_strsplit (value, ",", -1);
		guint ii;

		for (ii = 0; parts[ii]; ii++) {
			g_strstrip (parts[ii]);
			if (*parts[ii])
				g_ptr_array_add (names, g_strdup (parts[ii]));
		}
		g_strfreev (parts);
	}

	g_ptr_array_add (names, NULL);

	return (gchar **) g_ptr_array_free (names, FALSE);
}

/* Remplace les catégories d'un contact par le contenu d'un tableau JSON de
 * chaînes (noms vides ou contenant une virgule ignorés ; tableau vide : plus
 * aucune catégorie).
 * Paramètres :
 *   contact : contact à modifier
 *   array   : tableau JSON de noms de catégories */
static void
contact_apply_categories (EContact *contact,
			  JsonArray *array)
{
	GString *joined = g_string_new ("");
	guint ii;

	for (ii = 0; array && ii < json_array_get_length (array); ii++) {
		const gchar *category = json_array_get_string_element (array, ii);
		gchar *clean;

		if (!category)
			continue;

		clean = g_strstrip (g_strdup (category));
		if (*clean && !strchr (clean, ',')) {
			if (joined->len > 0)
				g_string_append_c (joined, ',');
			g_string_append (joined, clean);
		}
		g_free (clean);
	}

	e_contact_set (contact, E_CONTACT_CATEGORIES, joined->len > 0 ? joined->str : NULL);
	g_string_free (joined, TRUE);
}

/* Libère un élément de contact temporaire.
 * Paramètres :
 *   data : pointeur vers ContactItem */
static void
contact_item_free (gpointer data)
{
	ContactItem *item = data;

	if (!item)
		return;

	g_free (item->uid);
	g_free (item->full_name);
	g_free (item->email);
	g_free (item->phone);
	g_free (item->org);
	g_free (item->notes);
	g_free (item->book_uid);
	g_free (item->book_name);
	g_strfreev (item->categories);
	g_free (item->revision);
	g_free (item);
}

/* Compare deux contacts par nom complet alphabétique.
 * Paramètres :
 *   a, b : pointeurs vers ContactItem *
 * Retourne : résultat de g_utf8_collate. */
static gint
compare_contacts (gconstpointer a,
		  gconstpointer b)
{
	const ContactItem *ca = *(const ContactItem **) a;
	const ContactItem *cb = *(const ContactItem **) b;

	return g_utf8_collate (ca->full_name ? ca->full_name : "", cb->full_name ? cb->full_name : "");
}

/* Liste ou recherche les contacts dans tous les carnets d'adresses activés.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = query
 *   error : emplacement pour une éventuelle erreur
 * Retourne : tableau JSON des contacts. */
static gchar *
core_list_contacts_run (EvoCore *core,
			gchar **args,
			GError **error)
{
	gchar **only;
	const gchar *query_text = args[0] ? args[0] : "";
	EBookQuery *book_query;
	gchar *sexp;
	GList *sources, *slink;
	GPtrArray *items;
	JsonBuilder *builder;
	gchar *json;
	guint ii;

	book_query = e_book_query_any_field_contains (query_text);
	sexp = book_query ? e_book_query_to_string (book_query) : g_strdup ("");
	if (book_query)
		e_book_query_unref (book_query);

	items = g_ptr_array_new_with_free_func (contact_item_free);
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_ADDRESS_BOOK);

	/* args[1] : uids des carnets à lire, séparés par des virgules (tous sinon) */
	only = args[1] && *args[1] ? g_strsplit (args[1], ",", -1) : NULL;

	for (slink = sources; slink; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		EClient *client;
		GSList *contacts = NULL, *clink;
		GError *local_error = NULL;

		if (only && !g_strv_contains ((const gchar * const *) only, e_source_get_uid (source)))
			continue;

		client = e_book_client_connect_sync (source, 5, core->cancellable, &local_error);
		if (!client) {
			g_warning ("Cannot open address book “%s” (%s): %s", e_source_get_display_name (source),
				e_source_get_uid (source), local_error ? local_error->message : "unknown error");
			g_clear_error (&local_error);
			continue;
		}

		if (!e_book_client_get_contacts_sync (E_BOOK_CLIENT (client), sexp, &contacts, core->cancellable, &local_error)) {
			g_warning ("Cannot read address book “%s” (%s): %s", e_source_get_display_name (source),
				e_source_get_uid (source), local_error ? local_error->message : "unknown error");
			g_clear_error (&local_error);
		} else {
			for (clink = contacts; clink; clink = g_slist_next (clink)) {
				EContact *contact = clink->data;
				const gchar *uid, *full_name, *email, *phone, *org, *notes;
				ContactItem *item;

				if (!E_IS_CONTACT (contact))
					continue;

				uid = e_contact_get_const (contact, E_CONTACT_UID);
				if (!uid || !*uid)
					continue;

				full_name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
				if (!full_name || !*full_name)
					full_name = e_contact_get_const (contact, E_CONTACT_FILE_AS);
				email = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
				phone = e_contact_get_const (contact, E_CONTACT_PHONE_MOBILE);
				if (!phone || !*phone)
					phone = e_contact_get_const (contact, E_CONTACT_PHONE_BUSINESS);
				if (!phone || !*phone)
					phone = e_contact_get_const (contact, E_CONTACT_PHONE_HOME);
				org = e_contact_get_const (contact, E_CONTACT_ORG);
				notes = e_contact_get_const (contact, E_CONTACT_NOTE);

				item = g_new0 (ContactItem, 1);
				item->uid = g_strdup (uid);
				item->full_name = g_strdup ((full_name && *full_name) ? full_name : (email ? email : "(Sans nom)"));
				item->email = g_strdup (email ? email : "");
				item->phone = g_strdup (phone ? phone : "");
				item->org = g_strdup (org ? org : "");
				item->notes = g_strdup (notes ? notes : "");
				item->book_uid = g_strdup (e_source_get_uid (source));
				item->book_name = g_strdup (e_source_get_display_name (source));
				item->categories = contact_dup_categories (contact);
				item->revision = core_conflict_contact_revision (contact);

				g_ptr_array_add (items, item);
			}
			g_slist_free_full (contacts, g_object_unref);
		}

		g_object_unref (client);
	}

	g_list_free_full (sources, g_object_unref);
	g_strfreev (only);
	g_free (sexp);

	g_ptr_array_sort (items, compare_contacts);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (ii = 0; ii < items->len; ii++) {
		ContactItem *item = items->pdata[ii];

		json_builder_begin_object (builder);
		builder_add_string (builder, "uid", item->uid);
		builder_add_string (builder, "full_name", item->full_name);
		builder_add_string (builder, "email", item->email);
		builder_add_string (builder, "phone", item->phone);
		builder_add_string (builder, "org", item->org);
		builder_add_string (builder, "notes", item->notes);
		builder_add_string (builder, "book_uid", item->book_uid);
		builder_add_string (builder, "book_name", item->book_name);
		builder_add_string (builder, "revision", item->revision);
		builder_add_categories (builder, (const gchar * const *) item->categories);
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_ptr_array_unref (items);

	return json;
}

/* Crée un nouveau contact dans un carnet d'adresses evolution-data-server.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON décrivant le contact
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{\"uid\":\"...\"}" en cas de succès, NULL sinon. */
static gchar *
core_create_contact_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *book_uid, *full_name, *email, *phone, *org, *notes;
	ESource *source = NULL;
	EClient *client = NULL;
	EContact *contact = NULL;
	gchar *created_uid = NULL, *json = NULL;
	JsonBuilder *builder;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		json_node_unref (root);
		return NULL;
	}

	book_uid = json_object_get_string_member_with_default (object, "book_uid", "");
	full_name = json_object_get_string_member_with_default (object, "full_name", "");
	email = json_object_get_string_member_with_default (object, "email", "");
	phone = json_object_get_string_member_with_default (object, "phone", "");
	org = json_object_get_string_member_with_default (object, "org", "");
	notes = json_object_get_string_member_with_default (object, "notes", "");

	if ((!full_name || !*full_name) && (!email || !*email)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Le nom ou l'adresse e-mail du contact est obligatoire");
		json_node_unref (root);
		return NULL;
	}

	if (book_uid && *book_uid)
		source = e_source_registry_ref_source (core->registry, book_uid);
	if (!source)
		source = e_source_registry_ref_default_address_book (core->registry);

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Aucun carnet d'adresses disponible");
		goto exit;
	}

	client = e_book_client_connect_sync (source, 5, core->cancellable, error);
	if (!client)
		goto exit;

	contact = e_contact_new ();
	e_contact_set (contact, E_CONTACT_FULL_NAME, (full_name && *full_name) ? full_name : email);
	if (email && *email)
		e_contact_set (contact, E_CONTACT_EMAIL_1, email);
	if (phone && *phone)
		e_contact_set (contact, E_CONTACT_PHONE_MOBILE, phone);
	if (org && *org)
		e_contact_set (contact, E_CONTACT_ORG, org);
	if (notes && *notes)
		e_contact_set (contact, E_CONTACT_NOTE, notes);
	if (json_object_has_member (object, "categories") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (object, "categories")))
		contact_apply_categories (contact, json_object_get_array_member (object, "categories"));

	if (!e_book_client_add_contact_sync (E_BOOK_CLIENT (client), contact, E_BOOK_OPERATION_FLAG_NONE,
		&created_uid, core->cancellable, error))
		goto exit;

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "uid", created_uid ? created_uid : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	g_free (created_uid);
	g_clear_object (&contact);
	g_clear_object (&client);
	g_clear_object (&source);
	json_node_unref (root);

	return json;
}

/* Supprime un contact d'un carnet d'adresses evolution-data-server.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = book_uid, [1] = contact_uid
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_delete_contact_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	const gchar *book_uid = args[0];
	const gchar *contact_uid = args[1];
	ESource *source = NULL;
	EClient *client;
	gboolean ok;

	if (book_uid && *book_uid)
		source = e_source_registry_ref_source (core->registry, book_uid);
	if (!source)
		source = e_source_registry_ref_default_address_book (core->registry);

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Carnet d'adresses introuvable");
		return NULL;
	}

	client = e_book_client_connect_sync (source, 5, core->cancellable, error);
	g_object_unref (source);
	if (!client)
		return NULL;

	ok = e_book_client_remove_contact_by_uid_sync (E_BOOK_CLIENT (client), contact_uid,
		E_BOOK_OPERATION_FLAG_NONE, core->cancellable, error);

	g_object_unref (client);

	return ok ? g_strdup ("{}") : NULL;
}

/* Modifie un contact existant dans son carnet d'adresses.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = JSON du contact {"book_uid", "uid", "full_name", "email", "phone", "org", "notes"}
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_modify_contact_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	JsonNode *root;
	JsonObject *object;
	const gchar *book_uid, *uid, *full_name, *email, *phone, *org, *notes;
	ESource *source = NULL;
	EClient *client = NULL;
	EContact *contact = NULL;
	gboolean ok = FALSE;
	gchar *result = NULL;
	const gchar *base_revision;
	gint resolution;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	if (!object) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Objet JSON invalide");
		json_node_unref (root);
		return NULL;
	}

	book_uid = json_object_get_string_member_with_default (object, "book_uid", "");
	uid = json_object_get_string_member_with_default (object, "uid", "");
	full_name = json_object_get_string_member_with_default (object, "full_name", "");
	email = json_object_get_string_member_with_default (object, "email", "");
	phone = json_object_get_string_member_with_default (object, "phone", "");
	org = json_object_get_string_member_with_default (object, "org", "");
	notes = json_object_get_string_member_with_default (object, "notes", "");
	base_revision = json_object_get_string_member_with_default (object, "base_revision", "");
	resolution = cf_resolution_from_name (json_object_get_string_member_with_default (object, "conflict_resolution", ""));

	if (!uid || !*uid) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Identifiant de contact obligatoire");
		goto exit;
	}

	if (book_uid && *book_uid)
		source = e_source_registry_ref_source (core->registry, book_uid);
	if (!source)
		source = e_source_registry_ref_default_address_book (core->registry);

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Carnet d'adresses introuvable");
		goto exit;
	}

	client = e_book_client_connect_sync (source, 5, core->cancellable, error);
	if (!client)
		goto exit;

	/* modification concurrente : voir core_modify_event_run () */
	if (*base_revision && resolution == CF_RESOLVE_NONE && !core_oq_is_offline (core))
		e_client_refresh_sync (client, core->cancellable, NULL);

	if (!e_book_client_get_contact_sync (E_BOOK_CLIENT (client), uid, &contact, core->cancellable, error))
		goto exit;

	if (*base_revision) {
		gchar *current = core_conflict_contact_revision (contact);
		gint differing = 0;

		if (cf_must_block (base_revision, current, resolution)) {
			gchar *conflict = core_conflict_contact_json (object, contact, current, &differing);

			if (differing > 0) {
				g_free (current);
				result = conflict;
				ok = TRUE;
				goto exit;
			}
			g_free (conflict);
		}
		g_free (current);
	}

	if (resolution == CF_RESOLVE_REMOTE) {
		/* garder la version distante : rien n'est écrit */
		result = g_strdup ("{\"resolved\":\"remote\"}");
		ok = TRUE;
		goto exit;
	}

	if (resolution == CF_RESOLVE_BOTH) {
		/* garder les deux : la version locale devient un nouveau contact, l'original distant reste */
		EContact *copy = e_contact_duplicate (contact);
		gchar *added_uid = NULL;

		e_contact_set (copy, E_CONTACT_UID, NULL);
		e_contact_set (copy, E_CONTACT_FULL_NAME, (full_name && *full_name) ? full_name : email);
		e_contact_set (copy, E_CONTACT_EMAIL_1, (email && *email) ? email : NULL);
		e_contact_set (copy, E_CONTACT_PHONE_MOBILE, (phone && *phone) ? phone : NULL);
		e_contact_set (copy, E_CONTACT_ORG, (org && *org) ? org : NULL);
		e_contact_set (copy, E_CONTACT_NOTE, (notes && *notes) ? notes : NULL);
		e_vcard_remove_attributes (E_VCARD (copy), NULL, "X-EVOLUTION-WEBDAV-ETAG");
		e_vcard_remove_attributes (E_VCARD (copy), NULL, "X-EVOLUTION-WEBDAV-HREF");

		ok = e_book_client_add_contact_sync (E_BOOK_CLIENT (client), copy, E_BOOK_OPERATION_FLAG_NONE,
			&added_uid, core->cancellable, error);
		if (ok)
			result = g_strdup_printf ("{\"resolved\":\"both\",\"uid\":\"%s\"}", added_uid ? added_uid : "");
		g_free (added_uid);
		g_object_unref (copy);
		goto exit;
	}

	e_contact_set (contact, E_CONTACT_FULL_NAME, (full_name && *full_name) ? full_name : email);
	e_contact_set (contact, E_CONTACT_EMAIL_1, (email && *email) ? email : NULL);
	e_contact_set (contact, E_CONTACT_PHONE_MOBILE, (phone && *phone) ? phone : NULL);
	e_contact_set (contact, E_CONTACT_ORG, (org && *org) ? org : NULL);
	e_contact_set (contact, E_CONTACT_NOTE, (notes && *notes) ? notes : NULL);

	ok = e_book_client_modify_contact_sync (E_BOOK_CLIENT (client), contact, E_BOOK_OPERATION_FLAG_NONE, core->cancellable, error);
	if (ok)
		result = g_strdup ("{}");

exit:
	g_clear_object (&contact);
	g_clear_object (&client);
	g_clear_object (&source);
	json_node_unref (root);

	if (!ok) {
		g_free (result);
		return NULL;
	}

	return result;
}

/* Remplace les catégories d'un contact existant.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = book_uid, [1] = contact_uid, [2] = tableau JSON de noms
 *   error : emplacement pour une éventuelle erreur
 * Retourne : "{}" en cas de succès, NULL sinon. */
static gchar *
core_set_contact_categories_run (EvoCore *core,
				 gchar **args,
				 GError **error)
{
	JsonNode *root;
	ESource *source = NULL;
	EClient *client;
	EContact *contact = NULL;
	gchar *json = NULL;

	root = json_from_string (args[2], error);
	if (!root)
		return NULL;

	if (!JSON_NODE_HOLDS_ARRAY (root)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Liste de catégories invalide");
		json_node_unref (root);
		return NULL;
	}

	if (args[0] && *args[0])
		source = e_source_registry_ref_source (core->registry, args[0]);
	if (!source)
		source = e_source_registry_ref_default_address_book (core->registry);

	if (!source) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Carnet d'adresses introuvable");
		json_node_unref (root);
		return NULL;
	}

	client = e_book_client_connect_sync (source, 5, core->cancellable, error);
	g_object_unref (source);
	if (!client) {
		json_node_unref (root);
		return NULL;
	}

	if (e_book_client_get_contact_sync (E_BOOK_CLIENT (client), args[1], &contact, core->cancellable, error)) {
		contact_apply_categories (contact, json_node_get_array (root));
		if (e_book_client_modify_contact_sync (E_BOOK_CLIENT (client), contact, E_BOOK_OPERATION_FLAG_NONE,
			core->cancellable, error))
			json = g_strdup ("{}");
	}

	g_clear_object (&contact);
	g_object_unref (client);
	json_node_unref (root);

	return json;
}

/* --- autocomplétion des destinataires --- */

typedef struct {
	gchar *name;
	gchar *email;
	gchar *book;
	gint rank;	/* 0 : début du nom complet ou de l'adresse, 1 : autre champ */
} RecipientItem;

static void
recipient_item_free (gpointer data)
{
	RecipientItem *item = data;

	g_free (item->name);
	g_free (item->email);
	g_free (item->book);
	g_free (item);
}

static gint
compare_recipients (gconstpointer a,
		    gconstpointer b)
{
	const RecipientItem *ra = *(const RecipientItem **) a, *rb = *(const RecipientItem **) b;

	if (ra->rank != rb->rank)
		return ra->rank - rb->rank;

	return g_utf8_collate (ra->name && *ra->name ? ra->name : ra->email,
			       rb->name && *rb->name ? rb->name : rb->email);
}

/* Carnet ouvert une fois puis gardé : les recherches suivent la frappe. */
static EBookClient *
core_ref_book_client (EvoCore *core,
		      ESource *source)
{
	EClient *client;

	g_mutex_lock (&core->lock);
	client = g_hash_table_lookup (core->book_clients, e_source_get_uid (source));
	if (client)
		g_object_ref (client);
	g_mutex_unlock (&core->lock);

	if (client)
		return E_BOOK_CLIENT (client);

	client = e_book_client_connect_sync (source, 5, core->cancellable, NULL);
	if (!client)
		return NULL;

	g_mutex_lock (&core->lock);
	g_hash_table_replace (core->book_clients, g_strdup (e_source_get_uid (source)), g_object_ref (client));
	g_mutex_unlock (&core->lock);

	return E_BOOK_CLIENT (client);
}

/* Photo d'un contact des carnets d'adresses personnels, retrouvé par son adresse.
 * Seules les photos incluses dans la fiche (vCard) sont lues.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   email : adresse électronique du contact
 * Retourne : URI de données de la photo (g_free), NULL si aucun contact n'en a. */
gchar *
core_contact_photo_by_email (EvoCore *core,
			     const gchar *email)
{
	GList *sources, *slink;
	EBookQuery *query;
	gchar *sexp, *photo = NULL;

	if (!email || !*email)
		return NULL;

	query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_IS, email);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	for (slink = sources; slink && !photo; slink = g_list_next (slink)) {
		EBookClient *client = core_ref_book_client (core, slink->data);
		GSList *contacts = NULL, *clink;

		if (!client)
			continue;

		if (e_book_client_get_contacts_sync (client, sexp, &contacts, core->cancellable, NULL)) {
			for (clink = contacts; clink && !photo; clink = g_slist_next (clink)) {
				EContactPhoto *contact_photo = e_contact_get (clink->data, E_CONTACT_PHOTO);

				if (contact_photo && contact_photo->type == E_CONTACT_PHOTO_TYPE_INLINED &&
				    contact_photo->data.inlined.data && contact_photo->data.inlined.length > 0)
					photo = core_photo_data_uri (contact_photo->data.inlined.data,
						contact_photo->data.inlined.length);
				if (contact_photo)
					e_contact_photo_free (contact_photo);
			}
			g_slist_free_full (contacts, g_object_unref);
		}

		g_object_unref (client);
	}

	g_list_free_full (sources, g_object_unref);
	g_free (sexp);

	return photo;
}

/* Destinataires dont le nom, le prénom, le nom de famille, le surnom ou
 * l'adresse commence par la saisie, dans tous les carnets activés (GAL
 * comprise) ; une entrée par adresse, sans doublon.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = saisie, [1] = nombre maximal de résultats
 *   error : emplacement pour une éventuelle erreur
 * Retourne : [{"name", "email", "book"}, ...]. */
static gchar *
core_search_recipients_run (EvoCore *core,
			    gchar **args,
			    GError **error)
{
	const gchar *text = args[0] ? args[0] : "";
	guint limit = (guint) MAX (1, g_ascii_strtoll (args[1] ? args[1] : "15", NULL, 10));
	GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray *items = g_ptr_array_new_with_free_func (recipient_item_free);
	GList *sources, *slink;
	JsonBuilder *builder;
	gchar *sexp, *lower, *json;
	guint ii;

	if (g_utf8_strlen (text, -1) < 2) {
		g_hash_table_destroy (seen);
		g_ptr_array_unref (items);
		return g_strdup ("[]");
	}

	{
		EBookQuery *fields[] = {
			e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, text),
			e_book_query_field_test (E_CONTACT_GIVEN_NAME, E_BOOK_QUERY_BEGINS_WITH, text),
			e_book_query_field_test (E_CONTACT_FAMILY_NAME, E_BOOK_QUERY_BEGINS_WITH, text),
			e_book_query_field_test (E_CONTACT_NICKNAME, E_BOOK_QUERY_BEGINS_WITH, text),
			e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_BEGINS_WITH, text)
		};
		EBookQuery *query = e_book_query_or (G_N_ELEMENTS (fields), fields, TRUE);

		sexp = e_book_query_to_string (query);
		e_book_query_unref (query);
	}

	lower = g_utf8_casefold (text, -1);
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_ADDRESS_BOOK);

	for (slink = sources; slink; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		EBookClient *client = core_ref_book_client (core, source);
		GSList *contacts = NULL, *clink;

		if (!client)
			continue;

		if (e_book_client_get_contacts_sync (client, sexp, &contacts, core->cancellable, NULL)) {
			for (clink = contacts; clink; clink = g_slist_next (clink)) {
				EContact *contact = clink->data;
				const gchar *full_name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
				GList *emails = e_contact_get (contact, E_CONTACT_EMAIL), *elink;
				gchar *name_lower = g_utf8_casefold (full_name ? full_name : "", -1);

				for (elink = emails; elink; elink = g_list_next (elink)) {
					const gchar *email = elink->data;
					gchar *key, *email_lower;
					RecipientItem *item;

					if (!email || !*email)
						continue;

					key = g_ascii_strdown (email, -1);
					if (g_hash_table_contains (seen, key)) {
						g_free (key);
						continue;
					}
					g_hash_table_add (seen, key);

					email_lower = g_utf8_casefold (email, -1);
					item = g_new0 (RecipientItem, 1);
					item->name = g_strdup (full_name ? full_name : "");
					item->email = g_strdup (email);
					item->book = g_strdup (e_source_get_display_name (source));
					item->rank = g_str_has_prefix (name_lower, lower) || g_str_has_prefix (email_lower, lower) ? 0 : 1;
					g_ptr_array_add (items, item);
					g_free (email_lower);
				}

				g_free (name_lower);
				g_list_free_full (emails, g_free);
			}
			g_slist_free_full (contacts, g_object_unref);
		}

		g_object_unref (client);
	}

	g_list_free_full (sources, g_object_unref);
	g_free (lower);
	g_free (sexp);

	g_ptr_array_sort (items, compare_recipients);

	builder = json_builder_new ();
	json_builder_begin_array (builder);
	for (ii = 0; ii < items->len && ii < limit; ii++) {
		RecipientItem *item = items->pdata[ii];

		json_builder_begin_object (builder);
		builder_add_string (builder, "name", item->name);
		builder_add_string (builder, "email", item->email);
		builder_add_string (builder, "book", item->book);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_ptr_array_unref (items);
	g_hash_table_destroy (seen);

	return json;
}

/* Soumet une requête de liste ou recherche de contacts.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   query     : texte recherché (ou "" / NULL pour tous les contacts)
 *   func      : rappel recevant le tableau JSON des contacts
 *   user_data : données utilisateur transmises au rappel */
void
evo_core_search_recipients (EvoCore *core,
			    const char *text,
			    int max_count,
			    EvoCoreResultFunc func,
			    void *user_data)
{
	gchar count_buf[16];
	gchar *args[] = { (gchar *) (text ? text : ""), count_buf, NULL };

	g_snprintf (count_buf, sizeof (count_buf), "%d", max_count > 0 ? max_count : 15);

	core_submit (core, core_search_recipients_run, g_strdupv (args), func, user_data);
}

void
evo_core_list_contacts (EvoCore *core,
			const char *query,
			EvoCoreResultFunc func,
			void *user_data)
{
	evo_core_list_book_contacts (core, query, NULL, func, user_data);
}

void
evo_core_list_book_contacts (EvoCore *core,
			     const char *query,
			     const char *book_uids,
			     EvoCoreResultFunc func,
			     void *user_data)
{
	gchar *args[] = { (gchar *) (query ? query : ""), (gchar *) (book_uids ? book_uids : ""), NULL };

	core_submit (core, core_list_contacts_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de création d'un contact.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   contact_json : description JSON du contact
 *   func         : rappel recevant {"uid": "..."}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_create_contact (EvoCore *core,
			 const char *contact_json,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) contact_json, NULL };

	g_return_if_fail (contact_json != NULL);

	core_submit (core, core_create_contact_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de remplacement des catégories d'un contact.
 * Paramètres :
 *   core            : instance du moteur evo-core
 *   book_uid        : identifiant du carnet d'adresses
 *   contact_uid     : identifiant du contact
 *   categories_json : tableau JSON de noms de catégories (["Travail", ...])
 *   func            : rappel recevant {}
 *   user_data       : données utilisateur transmises au rappel */
void
evo_core_set_contact_categories (EvoCore *core,
				 const char *book_uid,
				 const char *contact_uid,
				 const char *categories_json,
				 EvoCoreResultFunc func,
				 void *user_data)
{
	gchar *args[] = { (gchar *) (book_uid ? book_uid : ""), (gchar *) contact_uid, (gchar *) categories_json, NULL };

	g_return_if_fail (contact_uid != NULL);
	g_return_if_fail (categories_json != NULL);

	core_submit (core, core_set_contact_categories_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de suppression d'un contact.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   book_uid    : identifiant du carnet d'adresses
 *   contact_uid : identifiant du contact
 *   func        : rappel recevant {}
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_delete_contact (EvoCore *core,
			 const char *book_uid,
			 const char *contact_uid,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) (book_uid ? book_uid : ""), (gchar *) contact_uid, NULL };

	g_return_if_fail (contact_uid != NULL);

	core_submit (core, core_delete_contact_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête de modification d'un contact existant.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   contact_json : JSON modifié {"book_uid", "uid", "full_name", ...}
 *   func         : rappel recevant {}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_modify_contact (EvoCore *core,
			 const char *contact_json,
			 EvoCoreResultFunc func,
			 void *user_data)
{
	gchar *args[] = { (gchar *) contact_json, NULL };

	g_return_if_fail (contact_json != NULL);

	core_submit (core, core_modify_contact_run, g_strdupv (args), func, user_data);
}
