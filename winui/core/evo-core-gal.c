/*
 * evo-core : recherche dans l'annuaire de l'organisation (carnet d'adresses
 * global, GAL).
 *   - Microsoft 365 / Exchange Online : API Microsoft Graph GET /users?$search=
 *     (portée User.ReadBasic.All, déjà demandée par le service OAuth2 d'EDS ;
 *     les détails professionnels exigent User.Read.All, sinon jeu de base) ;
 *   - Google Workspace : API People people:searchDirectoryPeople (portée
 *     directory.readonly, ajoutée à la demande de connexion par
 *     evo_core_oauth2_begin) ;
 *   - Exchange sur site (EWS ResolveNames) et IMAP générique : non pris en
 *     charge ici (les carnets EDS « GAL » d'EWS restent visibles dans les
 *     carnets d'adresses habituels).
 * Le jeton OAuth2 est celui de la collection du compte, renouvelé par EDS.
 * Voir evo-core.h et evo-core-private.h ; fonctions pures dans evo-core-gal-util.h.
 */

#include "evo-core-private.h"
#include "evo-core-gal-util.h"

typedef enum {
	GAL_PROVIDER_MICROSOFT365,
	GAL_PROVIDER_GOOGLE
} GalProvider;

/* Retrouve une source du registre pour le service OAuth2. */
static ESource *
gal_ref_source_cb (gpointer user_data,
		   const gchar *uid)
{
	return e_source_registry_ref_source (user_data, uid);
}

/* Fournisseur d'annuaire d'une collection ; FALSE si elle n'en a pas. */
static gboolean
gal_collection_provider (ESource *source,
			 GalProvider *out_provider)
{
	const gchar *backend;

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION))
		return FALSE;

	backend = e_source_backend_get_backend_name (E_SOURCE_BACKEND (
		e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION)));
	if (g_strcmp0 (backend, "microsoft365") == 0) {
		*out_provider = GAL_PROVIDER_MICROSOFT365;
		return TRUE;
	}
	if (g_strcmp0 (backend, "google") == 0) {
		*out_provider = GAL_PROVIDER_GOOGLE;
		return TRUE;
	}

	return FALSE;
}

/* Jeton d'accès OAuth2 de la collection.
 * Paramètres :
 *   core   : instance du moteur evo-core
 *   source : source de la collection
 *   token  : reçoit le jeton (e_util_safe_free_string)
 *   error  : emplacement pour une éventuelle erreur
 * Retourne : TRUE si un jeton a été obtenu. */
static gboolean
gal_get_token (EvoCore *core,
	       ESource *source,
	       gchar **token,
	       GError **error)
{
	EOAuth2Service *oauth2;
	gint expires_in = 0;
	GError *local_error = NULL;
	gboolean ok;

	oauth2 = e_oauth2_services_find (e_source_registry_get_oauth2_services (core->registry), source);
	if (!oauth2) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Le compte « %s » n'utilise pas l'authentification OAuth2 : l'annuaire est indisponible.",
			e_source_get_display_name (source));
		return FALSE;
	}

	ok = e_oauth2_service_get_access_token_sync (oauth2, source, gal_ref_source_cb,
		core->registry, token, &expires_in, core->cancellable, &local_error);
	if (!ok) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"Impossible d'obtenir le jeton d'accès du compte « %s » (reconnectez-le dans ses propriétés) : %s",
			e_source_get_display_name (source), local_error ? local_error->message : "jeton absent");
		g_clear_error (&local_error);
	}

	g_object_unref (oauth2);
	return ok;
}

/* Requête GET authentifiée (Bearer) sur une API d'annuaire.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   provider   : fournisseur (en-têtes et messages d'erreur)
 *   uri        : adresse de l'API
 *   token      : jeton d'accès
 *   out_status : reçoit le code HTTP (0 si le serveur est injoignable)
 *   error      : emplacement pour une erreur réseau
 * Retourne : le corps de la réponse (même en cas d'erreur HTTP), NULL si injoignable. */
static GBytes *
gal_http_get (EvoCore *core,
	      GalProvider provider,
	      const gchar *uri,
	      const gchar *token,
	      guint *out_status,
	      GError **error)
{
	SoupSession *soup;
	SoupMessage *msg;
	GBytes *bytes;
	GError *local_error = NULL;
	gchar *authorization;

	soup = soup_session_new_with_options ("timeout", 15, "user-agent", "Evolution (Windows)", NULL);
	msg = soup_message_new ("GET", uri);
	authorization = g_strdup_printf ("Bearer %s", token);
	soup_message_headers_replace (soup_message_get_request_headers (msg), "Authorization", authorization);
	soup_message_headers_replace (soup_message_get_request_headers (msg), "Accept", "application/json");
	if (provider == GAL_PROVIDER_MICROSOFT365)
		soup_message_headers_replace (soup_message_get_request_headers (msg), "ConsistencyLevel", "eventual");
	g_free (authorization);

	bytes = soup_session_send_and_read (soup, msg, core->cancellable, &local_error);
	*out_status = bytes ? soup_message_get_status (msg) : 0;
	if (!bytes)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND,
			"Le serveur d'annuaire est injoignable : %s", local_error ? local_error->message : "erreur réseau");

	g_clear_error (&local_error);
	g_object_unref (msg);
	g_object_unref (soup);

	return bytes;
}

/* Message d'erreur d'une réponse Graph ou Google ({"error": {"message": ...}}). */
static gchar *
gal_response_error_message (GBytes *bytes)
{
	JsonNode *root;
	gchar *message = NULL;
	gsize size = 0;
	const gchar *data = bytes ? g_bytes_get_data (bytes, &size) : NULL;
	gchar *text;

	if (!data)
		return NULL;

	text = g_strndup (data, size);
	root = json_from_string (text, NULL);
	g_free (text);
	if (root && JSON_NODE_HOLDS_OBJECT (root)) {
		JsonObject *object = json_node_get_object (root);

		if (json_object_has_member (object, "error") &&
		    JSON_NODE_HOLDS_OBJECT (json_object_get_member (object, "error")))
			message = g_strdup (json_object_get_string_member_with_default (
				json_object_get_object_member (object, "error"), "message", ""));
	}
	if (root)
		json_node_unref (root);
	if (message && !*message)
		g_clear_pointer (&message, g_free);

	return message;
}

/* Erreur lisible pour un code HTTP d'annuaire. */
static void
gal_set_http_error (GalProvider provider,
		    const gchar *account,
		    guint status,
		    GBytes *bytes,
		    GError **error)
{
	gchar *detail = gal_response_error_message (bytes);

	if (status == 401)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"Authentification refusée pour « %s » : reconnectez le compte dans ses propriétés.", account);
	else if (status == 403)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			provider == GAL_PROVIDER_GOOGLE
			? "Annuaire indisponible pour « %s » : la portée OAuth2 « directory.readonly » est requise ; reconnectez le compte Google pour l'accorder (%s)."
			: "Annuaire indisponible pour « %s » : la permission « User.ReadBasic.All » est requise (consentement de l'administrateur possible) ; reconnectez le compte (%s).",
			account, detail ? detail : "accès refusé");
	else
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"L'annuaire de « %s » a refusé la demande (HTTP %u) : %s",
			account, status, detail ? detail : "erreur inconnue");
	g_free (detail);
}

/* Racine JSON objet d'une réponse HTTP, NULL (avec erreur) si invalide. */
static JsonObject *
gal_parse_response (GBytes *bytes,
		    JsonNode **out_root,
		    GError **error)
{
	gsize size = 0;
	const gchar *data = g_bytes_get_data (bytes, &size);
	gchar *text = g_strndup (data ? data : "{}", size);
	JsonNode *root = json_from_string (text, error);

	g_free (text);
	if (root && !JSON_NODE_HOLDS_OBJECT (root)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Réponse inattendue du serveur d'annuaire");
		json_node_unref (root);
		return NULL;
	}
	*out_root = root;

	return root ? json_node_get_object (root) : NULL;
}

/* Texte d'un membre de type chaîne (vide si absent ou nul). */
static const gchar *
gal_string_member (JsonObject *object,
		   const gchar *name)
{
	JsonNode *node;

	if (!object || !json_object_has_member (object, name))
		return "";
	node = json_object_get_member (object, name);
	if (!node || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
		return "";

	return json_node_get_string (node);
}

/* Premier objet d'un membre tableau (NULL si absent ou vide). */
static JsonObject *
gal_first_object (JsonObject *object,
		  const gchar *array_name)
{
	JsonNode *node;
	JsonArray *array;

	if (!object || !json_object_has_member (object, array_name))
		return NULL;
	node = json_object_get_member (object, array_name);
	if (!node || !JSON_NODE_HOLDS_ARRAY (node))
		return NULL;
	array = json_node_get_array (node);
	if (json_array_get_length (array) == 0 || !JSON_NODE_HOLDS_OBJECT (json_array_get_element (array, 0)))
		return NULL;

	return json_array_get_object_element (array, 0);
}

/* Fiche d'annuaire normalisée, indépendante du fournisseur. */
typedef struct {
	gchar *name;
	gchar *email;
	gchar *given_name;
	gchar *family_name;
	gchar *title;
	gchar *department;
	gchar *company;
	gchar *office;
	gchar *phone;
	gchar *mobile;
	gchar *phone2;	/* autre numéro (2e téléphone professionnel, fax) */
	gchar *address;	/* adresse postale sur une ligne */
	gchar *photo_url;	/* URL publique de la photo (Google) */
} GalPerson;

static void
gal_person_free (gpointer data)
{
	GalPerson *person = data;

	g_free (person->name);
	g_free (person->email);
	g_free (person->given_name);
	g_free (person->family_name);
	g_free (person->title);
	g_free (person->department);
	g_free (person->company);
	g_free (person->office);
	g_free (person->phone);
	g_free (person->mobile);
	g_free (person->phone2);
	g_free (person->address);
	g_free (person->photo_url);
	g_free (person);
}

/* Assemble une adresse postale sur une ligne : « rue, code ville, région, pays »
 * (les éléments vides sont omis). Retourne une chaîne vide s'il n'y a rien. */
static gchar *
gal_join_address (const gchar *street,
		  const gchar *postal_code,
		  const gchar *city,
		  const gchar *state,
		  const gchar *country)
{
	GPtrArray *parts = g_ptr_array_new_with_free_func (g_free);
	gchar *locality = g_strstrip (g_strdup_printf ("%s %s", postal_code ? postal_code : "", city ? city : ""));
	gchar *result;

	if (street && *street)
		g_ptr_array_add (parts, g_strdup (street));
	if (*locality)
		g_ptr_array_add (parts, g_strdup (locality));
	if (state && *state)
		g_ptr_array_add (parts, g_strdup (state));
	if (country && *country)
		g_ptr_array_add (parts, g_strdup (country));
	g_ptr_array_add (parts, NULL);

	result = g_strjoinv (", ", (gchar **) parts->pdata);
	g_ptr_array_unref (parts);
	g_free (locality);

	return result;
}

/* Adresse principale d'un utilisateur Graph : "mail", à défaut le nom
 * d'utilisateur principal s'il ressemble à une adresse. NULL si aucune. */
static gchar *
gal_graph_email (JsonObject *user)
{
	gchar buffer[320];

	if (gal_normalize_email (gal_string_member (user, "mail"), buffer, sizeof (buffer)))
		return g_strdup (buffer);
	if (gal_normalize_email (gal_string_member (user, "userPrincipalName"), buffer, sizeof (buffer)))
		return g_strdup (buffer);

	return NULL;
}

/* Fiche d'un utilisateur Graph, NULL s'il n'a pas d'adresse. */
static GalPerson *
gal_person_from_graph (JsonObject *user)
{
	GalPerson *person;
	gchar *email = gal_graph_email (user);
	JsonNode *phones;

	if (!email)
		return NULL;

	person = g_new0 (GalPerson, 1);
	person->email = email;
	person->name = g_strdup (gal_string_member (user, "displayName"));
	person->given_name = g_strdup (gal_string_member (user, "givenName"));
	person->family_name = g_strdup (gal_string_member (user, "surname"));
	person->title = g_strdup (gal_string_member (user, "jobTitle"));
	person->department = g_strdup (gal_string_member (user, "department"));
	person->company = g_strdup (gal_string_member (user, "companyName"));
	person->office = g_strdup (gal_string_member (user, "officeLocation"));
	person->mobile = g_strdup (gal_string_member (user, "mobilePhone"));
	person->phone = g_strdup ("");
	person->phone2 = g_strdup (gal_string_member (user, "faxNumber"));
	person->photo_url = g_strdup ("");
	person->address = gal_join_address (gal_string_member (user, "streetAddress"),
		gal_string_member (user, "postalCode"), gal_string_member (user, "city"),
		gal_string_member (user, "state"), gal_string_member (user, "country"));

	phones = json_object_has_member (user, "businessPhones") ? json_object_get_member (user, "businessPhones") : NULL;
	if (phones && JSON_NODE_HOLDS_ARRAY (phones)) {
		JsonArray *array = json_node_get_array (phones);
		guint ii;

		for (ii = 0; ii < json_array_get_length (array); ii++) {
			JsonNode *node = json_array_get_element (array, ii);
			const gchar *number;

			if (!node || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
				continue;
			number = json_node_get_string (node);
			if (!number || !*number)
				continue;
			/* 1er numéro professionnel : téléphone ; suivants : « autre » (avant le fax) */
			if (!*person->phone) {
				g_free (person->phone);
				person->phone = g_strdup (number);
			} else if (!*person->phone2) {
				g_free (person->phone2);
				person->phone2 = g_strdup (number);
			}
		}
	}

	return person;
}

/* Fiche d'une personne de l'API People de Google, NULL sans adresse. */
static GalPerson *
gal_person_from_google (JsonObject *entry)
{
	GalPerson *person;
	JsonObject *name = gal_first_object (entry, "names");
	JsonObject *email = gal_first_object (entry, "emailAddresses");
	JsonObject *organization = gal_first_object (entry, "organizations");
	JsonObject *location = gal_first_object (entry, "locations");
	JsonNode *phones = json_object_has_member (entry, "phoneNumbers") ? json_object_get_member (entry, "phoneNumbers") : NULL;
	gchar buffer[320];
	const gchar *office;
	guint ii;

	if (!email || !gal_normalize_email (gal_string_member (email, "value"), buffer, sizeof (buffer)))
		return NULL;

	person = g_new0 (GalPerson, 1);
	person->email = g_strdup (buffer);
	person->name = g_strdup (gal_string_member (name, "displayName"));
	person->given_name = g_strdup (gal_string_member (name, "givenName"));
	person->family_name = g_strdup (gal_string_member (name, "familyName"));
	person->title = g_strdup (gal_string_member (organization, "title"));
	person->department = g_strdup (gal_string_member (organization, "department"));
	person->company = g_strdup (gal_string_member (organization, "name"));
	office = gal_string_member (location, "value");
	person->office = g_strdup (*office ? office : gal_string_member (location, "buildingId"));
	person->phone = g_strdup ("");
	person->mobile = g_strdup ("");
	person->phone2 = g_strdup ("");
	person->address = g_strdup (gal_string_member (gal_first_object (entry, "addresses"), "formattedValue"));
	person->photo_url = g_strdup (gal_string_member (gal_first_object (entry, "photos"), "url"));

	if (phones && JSON_NODE_HOLDS_ARRAY (phones)) {
		JsonArray *array = json_node_get_array (phones);

		for (ii = 0; ii < json_array_get_length (array); ii++) {
			JsonObject *phone = JSON_NODE_HOLDS_OBJECT (json_array_get_element (array, ii))
				? json_array_get_object_element (array, ii) : NULL;
			const gchar *type = gal_string_member (phone, "type"), *value = gal_string_member (phone, "value");
			gchar **target = !g_ascii_strcasecmp (type, "mobile") ? &person->mobile : &person->phone;

			if (!*value || **target)
				continue;
			g_free (*target);
			*target = g_strdup (value);
		}
	}

	return person;
}

/* Interroge l'annuaire Microsoft Graph : jeu complet de champs d'abord, jeu
 * de base si la portée ne l'autorise pas (HTTP 400 ou 403). */
static gboolean
gal_search_graph (EvoCore *core,
		  const gchar *account,
		  const gchar *token,
		  const gchar *term,
		  int limit,
		  GPtrArray *people,
		  GError **error)
{
	const gchar *selects[] = { GAL_GRAPH_SELECT_FULL, GAL_GRAPH_SELECT_BASIC };
	guint attempt;

	for (attempt = 0; attempt < G_N_ELEMENTS (selects); attempt++) {
		gchar uri[1536];
		GBytes *bytes;
		guint status = 0;
		JsonNode *root = NULL;
		JsonObject *object;
		JsonNode *value;
		guint ii;

		if (!gal_graph_search_url (term, limit, selects[attempt], uri, sizeof (uri))) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Recherche d'annuaire invalide");
			return FALSE;
		}

		bytes = gal_http_get (core, GAL_PROVIDER_MICROSOFT365, uri, token, &status, error);
		if (!bytes)
			return FALSE;

		if (status < 200 || status >= 300) {
			/* champs professionnels refusés sans User.Read.All : jeu de base */
			if ((status == 400 || status == 403) && attempt + 1 < G_N_ELEMENTS (selects)) {
				g_bytes_unref (bytes);
				continue;
			}
			gal_set_http_error (GAL_PROVIDER_MICROSOFT365, account, status, bytes, error);
			g_bytes_unref (bytes);
			return FALSE;
		}

		object = gal_parse_response (bytes, &root, error);
		g_bytes_unref (bytes);
		if (!object)
			return FALSE;

		value = json_object_has_member (object, "value") ? json_object_get_member (object, "value") : NULL;
		if (value && JSON_NODE_HOLDS_ARRAY (value)) {
			JsonArray *array = json_node_get_array (value);

			for (ii = 0; ii < json_array_get_length (array); ii++) {
				GalPerson *person;

				if (!JSON_NODE_HOLDS_OBJECT (json_array_get_element (array, ii)))
					continue;
				person = gal_person_from_graph (json_array_get_object_element (array, ii));
				if (person)
					g_ptr_array_add (people, person);
			}
		}
		json_node_unref (root);
		return TRUE;
	}

	return FALSE;
}

/* Interroge l'annuaire Google Workspace (API People). */
static gboolean
gal_search_google (EvoCore *core,
		   const gchar *account,
		   const gchar *token,
		   const gchar *term,
		   int limit,
		   GPtrArray *people,
		   GError **error)
{
	gchar uri[1024];
	GBytes *bytes;
	guint status = 0;
	JsonNode *root = NULL, *value;
	JsonObject *object;
	guint ii;

	if (!gal_google_search_url (term, limit, uri, sizeof (uri))) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Recherche d'annuaire invalide");
		return FALSE;
	}

	bytes = gal_http_get (core, GAL_PROVIDER_GOOGLE, uri, token, &status, error);
	if (!bytes)
		return FALSE;
	if (status < 200 || status >= 300) {
		gal_set_http_error (GAL_PROVIDER_GOOGLE, account, status, bytes, error);
		g_bytes_unref (bytes);
		return FALSE;
	}

	object = gal_parse_response (bytes, &root, error);
	g_bytes_unref (bytes);
	if (!object)
		return FALSE;

	/* l'API omet "people" lorsqu'il n'y a aucun résultat */
	value = json_object_has_member (object, "people") ? json_object_get_member (object, "people") : NULL;
	if (value && JSON_NODE_HOLDS_ARRAY (value)) {
		JsonArray *array = json_node_get_array (value);

		for (ii = 0; ii < json_array_get_length (array); ii++) {
			GalPerson *person;

			if (!JSON_NODE_HOLDS_OBJECT (json_array_get_element (array, ii)))
				continue;
			person = gal_person_from_google (json_array_get_object_element (array, ii));
			if (person)
				g_ptr_array_add (people, person);
		}
	}
	json_node_unref (root);

	return TRUE;
}

/* Ajoute une fiche au tableau JSON de résultat. */
static void
gal_add_person_json (JsonBuilder *builder,
		     const GalPerson *person,
		     const gchar *account,
		     const gchar *account_uid,
		     GalProvider provider)
{
	json_builder_begin_object (builder);
	builder_add_string (builder, "name", person->name ? person->name : "");
	builder_add_string (builder, "email", person->email);
	builder_add_string (builder, "given_name", person->given_name ? person->given_name : "");
	builder_add_string (builder, "family_name", person->family_name ? person->family_name : "");
	builder_add_string (builder, "title", person->title ? person->title : "");
	builder_add_string (builder, "department", person->department ? person->department : "");
	builder_add_string (builder, "company", person->company ? person->company : "");
	builder_add_string (builder, "office", person->office ? person->office : "");
	builder_add_string (builder, "phone", person->phone ? person->phone : "");
	builder_add_string (builder, "mobile", person->mobile ? person->mobile : "");
	builder_add_string (builder, "phone2", person->phone2 ? person->phone2 : "");
	builder_add_string (builder, "address", person->address ? person->address : "");
	builder_add_string (builder, "photo_url", person->photo_url ? person->photo_url : "");
	builder_add_string (builder, "account", account);
	builder_add_string (builder, "account_uid", account_uid);
	builder_add_string (builder, "provider", provider == GAL_PROVIDER_GOOGLE ? "google" : "microsoft365");
	json_builder_end_object (builder);
}

/* Recherche dans l'annuaire de tous les comptes Microsoft 365 et Google.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = saisie, [1] = nombre maximal de résultats
 *   error : emplacement pour une éventuelle erreur
 * Retourne : [{"name", "email", ...}, ...] (voir evo_core_search_gal). Les
 *   résultats des comptes joignables sont renvoyés même si un autre compte
 *   échoue ; l'erreur n'est signalée que si tous les comptes échouent. */
static gchar *
core_search_gal_run (EvoCore *core,
		     gchar **args,
		     GError **error)
{
	gchar term[160];
	int limit = gal_clamp_limit ((int) g_ascii_strtoll (args[1] ? args[1] : "15", NULL, 10));
	GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GList *sources, *slink;
	JsonBuilder *builder;
	GError *first_error = NULL;
	gchar *json;
	guint accounts = 0, failures = 0, found = 0;

	gal_sanitize_term (args[0], term, sizeof (term));
	if (!gal_query_valid (term)) {
		g_hash_table_destroy (seen);
		return g_strdup ("[]");
	}

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_COLLECTION);
	for (slink = sources; slink && found < (guint) limit; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		GalProvider provider;
		const gchar *account = e_source_get_display_name (source);
		GPtrArray *people;
		gchar *token = NULL;
		GError *local_error = NULL;
		gboolean ok;
		guint ii;

		if (!gal_collection_provider (source, &provider))
			continue;
		/* filtre optionnel : un seul compte (identifiant ou nom affiché) */
		if (args[2] && *args[2] && g_strcmp0 (args[2], e_source_get_uid (source)) != 0 &&
		    g_strcmp0 (args[2], account) != 0)
			continue;
		accounts++;

		if (!gal_get_token (core, source, &token, &local_error)) {
			failures++;
			if (!first_error)
				g_propagate_error (&first_error, local_error);
			else
				g_clear_error (&local_error);
			continue;
		}

		people = g_ptr_array_new_with_free_func (gal_person_free);
		ok = provider == GAL_PROVIDER_GOOGLE
			? gal_search_google (core, account, token, term, limit, people, &local_error)
			: gal_search_graph (core, account, token, term, limit, people, &local_error);
		if (!ok) {
			failures++;
			if (!first_error)
				g_propagate_error (&first_error, local_error);
			else
				g_clear_error (&local_error);
		}

		for (ii = 0; ii < people->len && found < (guint) limit; ii++) {
			GalPerson *person = people->pdata[ii];

			/* une entrée par adresse, même annuaire interrogé via deux comptes */
			if (g_hash_table_contains (seen, person->email))
				continue;
			g_hash_table_add (seen, g_strdup (person->email));
			gal_add_person_json (builder, person, account, e_source_get_uid (source), provider);
			found++;
		}

		g_ptr_array_unref (people);
		e_util_safe_free_string (token);
	}
	g_list_free_full (sources, g_object_unref);

	json_builder_end_array (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	g_hash_table_destroy (seen);

	if (accounts == 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"L'annuaire de l'organisation n'est disponible que pour les comptes Microsoft 365 et Google Workspace ; aucun n'est configuré.");
		g_free (json);
		json = NULL;
	} else if (failures == accounts && first_error) {
		g_propagate_error (error, first_error);
		first_error = NULL;
		g_free (json);
		json = NULL;
	}

	g_clear_error (&first_error);
	return json;
}

void
evo_core_search_gal (EvoCore *core,
		     const char *text,
		     int max_count,
		     EvoCoreResultFunc func,
		     void *user_data)
{
	gchar count_buf[16];
	gchar *args[] = { (gchar *) (text ? text : ""), count_buf, (gchar *) "", NULL };

	g_return_if_fail (core != NULL);

	g_snprintf (count_buf, sizeof (count_buf), "%d", max_count > 0 ? max_count : 15);

	core_submit (core, core_search_gal_run, g_strdupv (args), func, user_data);
}


/* Recherche dans l'annuaire d'un seul compte.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant (ou nom affiché) du compte Microsoft 365 / Google
 *   text        : saisie de l'utilisateur
 *   max_count   : nombre maximal de résultats
 *   func        : rappel recevant le tableau JSON (voir evo_core_search_gal)
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_search_gal_account (EvoCore *core,
			     const char *account_uid,
			     const char *text,
			     int max_count,
			     EvoCoreResultFunc func,
			     void *user_data)
{
	gchar count_buf[16];
	gchar *args[] = { (gchar *) (text ? text : ""), count_buf, (gchar *) (account_uid ? account_uid : ""), NULL };

	g_return_if_fail (core != NULL);

	g_snprintf (count_buf, sizeof (count_buf), "%d", max_count > 0 ? max_count : 15);

	core_submit (core, core_search_gal_run, g_strdupv (args), func, user_data);
}

/* Photo de profil d'une personne de l'annuaire Microsoft 365 (Graph
 * GET /users/{adresse}/photo/$value).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = compte (identifiant ou nom, vide = tous), [1] = adresse
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"photo": "data:image/jpeg;base64,..."} ; "photo" vide si la
 *   personne n'a pas de photo ou si la portée est insuffisante. */
static gchar *
core_gal_photo_run (EvoCore *core,
		    gchar **args,
		    GError **error)
{
	GList *sources, *slink;
	JsonBuilder *builder;
	gchar *json, *photo = NULL, *escaped;

	escaped = g_uri_escape_string (args[1] ? args[1] : "", NULL, TRUE);
	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_COLLECTION);

	for (slink = sources; slink && !photo; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		GalProvider provider;
		gchar *token = NULL, *uri;
		GBytes *bytes;
		guint status = 0;

		if (!gal_collection_provider (source, &provider) || provider != GAL_PROVIDER_MICROSOFT365)
			continue;
		if (args[0] && *args[0] && g_strcmp0 (args[0], e_source_get_uid (source)) != 0 &&
		    g_strcmp0 (args[0], e_source_get_display_name (source)) != 0)
			continue;
		if (!gal_get_token (core, source, &token, NULL))
			continue;

		uri = g_strconcat ("https://graph.microsoft.com/v1.0/users/", escaped, "/photo/$value", NULL);
		bytes = gal_http_get (core, provider, uri, token, &status, NULL);
		if (bytes && status >= 200 && status < 300) {
			gsize size = 0;
			gconstpointer data = g_bytes_get_data (bytes, &size);

			if (data && size > 0) {
				gchar *base64 = g_base64_encode (data, size);

				photo = g_strconcat ("data:image/jpeg;base64,", base64, NULL);
				g_free (base64);
			}
		}
		g_clear_pointer (&bytes, g_bytes_unref);
		g_free (uri);
		e_util_safe_free_string (token);
	}
	g_list_free_full (sources, g_object_unref);
	g_free (escaped);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "photo", photo ? photo : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	g_free (photo);

	return json;
}

/* Soumet la lecture de la photo de profil d'une personne de l'annuaire.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : compte à interroger (vide pour tous les comptes Microsoft 365)
 *   email       : adresse de la personne
 *   func        : rappel recevant {"photo": "data:image/jpeg;base64,..."} (vide si aucune)
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_get_gal_photo (EvoCore *core,
			const char *account_uid,
			const char *email,
			EvoCoreResultFunc func,
			void *user_data)
{
	gchar *args[] = { (gchar *) (account_uid ? account_uid : ""), (gchar *) (email ? email : ""), NULL };

	g_return_if_fail (core != NULL);

	core_submit (core, core_gal_photo_run, g_strdupv (args), func, user_data);
}

/* --- calendriers de personnes (Microsoft Graph, lecture seule) --- */

/* Source de collection Microsoft 365 : celle de l'identifiant demandé, sinon la
 * première activée. NULL si aucune. */
static ESource *
gal_ref_m365_collection (EvoCore *core,
			 const gchar *account_uid)
{
	GList *sources, *slink;
	ESource *found = NULL;

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_COLLECTION);
	for (slink = sources; slink && !found; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		GalProvider provider;

		if (!gal_collection_provider (source, &provider) || provider != GAL_PROVIDER_MICROSOFT365)
			continue;
		if (account_uid && *account_uid && g_strcmp0 (account_uid, e_source_get_uid (source)) != 0)
			continue;
		found = g_object_ref (source);
	}
	g_list_free_full (sources, g_object_unref);

	return found;
}

/* Date Graph {"dateTime": "2026-11-05T09:00:00.0000000", "timeZone": "UTC"} en
 * secondes Unix (0 si absente ou invalide). */
static gint64
gal_graph_time (JsonObject *object,
		const gchar *member)
{
	JsonObject *when = json_object_has_member (object, member) &&
		JSON_NODE_HOLDS_OBJECT (json_object_get_member (object, member))
		? json_object_get_object_member (object, member) : NULL;
	const gchar *text = when ? gal_string_member (when, "dateTime") : "";
	gchar buffer[40];
	GTimeZone *utc;
	GDateTime *date;
	gint64 result = 0;

	if (!text || strlen (text) < 19)
		return 0;

	/* "AAAA-MM-JJTHH:MM:SS" + fraction éventuelle limitée à 6 chiffres */
	g_strlcpy (buffer, text, sizeof (buffer));
	if (strlen (buffer) > 26)
		buffer[26] = '\0';

	utc = g_time_zone_new_utc ();
	date = g_date_time_new_from_iso8601 (buffer, utc);
	if (date) {
		result = g_date_time_to_unix (date);
		g_date_time_unref (date);
	}
	g_time_zone_unref (utc);

	return result;
}

/* Événements du calendrier d'une personne sur une période.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = compte (collection), [1] = adresse de la personne, [2] = nom
 *           affiché, [3] = couleur "#rrggbb" ou "", [4] = début (secondes Unix),
 *           [5] = fin (secondes Unix)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : tableau JSON au format de evo_core_list_events (calendar_uid =
 *   "person:adresse", lecture seule). */
static gchar *
core_list_person_events_run (EvoCore *core,
			     gchar **args,
			     GError **error)
{
	ESource *source = gal_ref_m365_collection (core, args[0]);
	gint64 start = g_ascii_strtoll (args[4], NULL, 10), end = g_ascii_strtoll (args[5], NULL, 10);
	gchar *token = NULL, *uri, *escaped, *start_text, *end_text, *calendar_uid, *json = NULL;
	GDateTime *start_date, *end_date;
	GBytes *bytes;
	guint status = 0;
	JsonNode *root = NULL;
	JsonObject *object;
	JsonBuilder *builder;

	if (!source) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Les calendriers de personnes ne sont disponibles que pour les comptes Microsoft 365.");
		return NULL;
	}
	if (!gal_get_token (core, source, &token, error)) {
		g_object_unref (source);
		return NULL;
	}
	g_object_unref (source);

	if (end <= start)
		end = start + 7 * 24 * 3600;
	start_date = g_date_time_new_from_unix_utc (start);
	end_date = g_date_time_new_from_unix_utc (end);
	start_text = g_date_time_format (start_date, "%Y-%m-%dT%H:%M:%SZ");
	end_text = g_date_time_format (end_date, "%Y-%m-%dT%H:%M:%SZ");
	escaped = g_uri_escape_string (args[1], NULL, TRUE);
	uri = g_strdup_printf ("https://graph.microsoft.com/v1.0/users/%s/calendarView?startDateTime=%s&endDateTime=%s"
		"&$top=250&$select=id,subject,start,end,isAllDay,location,type,isCancelled,sensitivity,showAs",
		escaped, start_text, end_text);
	g_free (start_text);
	g_free (end_text);
	g_free (escaped);
	g_date_time_unref (start_date);
	g_date_time_unref (end_date);

	bytes = gal_http_get (core, GAL_PROVIDER_MICROSOFT365, uri, token, &status, error);
	g_free (uri);
	e_util_safe_free_string (token);
	if (!bytes)
		return NULL;

	if (status == 403 || status == 404) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"Le calendrier de %s n'est pas partagé avec vous (accès refusé par le serveur).",
			args[2] && *args[2] ? args[2] : args[1]);
		g_bytes_unref (bytes);
		return NULL;
	}
	if (status < 200 || status >= 300) {
		gal_set_http_error (GAL_PROVIDER_MICROSOFT365, args[1], status, bytes, error);
		g_bytes_unref (bytes);
		return NULL;
	}

	object = gal_parse_response (bytes, &root, error);
	g_bytes_unref (bytes);
	if (!object)
		return NULL;

	calendar_uid = g_strconcat ("person:", args[1], NULL);
	builder = json_builder_new ();
	json_builder_begin_array (builder);

	if (json_object_has_member (object, "value") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (object, "value"))) {
		JsonArray *array = json_object_get_array_member (object, "value");
		guint ii;

		for (ii = 0; ii < json_array_get_length (array); ii++) {
			JsonObject *event;
			JsonObject *location;
			gchar *event_uid;
			const gchar *type;

			if (!JSON_NODE_HOLDS_OBJECT (json_array_get_element (array, ii)))
				continue;
			event = json_array_get_object_element (array, ii);
			if (json_object_get_boolean_member_with_default (event, "isCancelled", FALSE))
				continue;

			location = json_object_has_member (event, "location") &&
				JSON_NODE_HOLDS_OBJECT (json_object_get_member (event, "location"))
				? json_object_get_object_member (event, "location") : NULL;
			type = gal_string_member (event, "type");
			event_uid = g_strconcat (calendar_uid, ":", gal_string_member (event, "id"), NULL);

			json_builder_begin_object (builder);
			builder_add_string (builder, "uid", event_uid);
			builder_add_string (builder, "summary", *gal_string_member (event, "subject")
				? gal_string_member (event, "subject") : "(Sans titre)");
			builder_add_string (builder, "location", location ? gal_string_member (location, "displayName") : "");
			builder_add_string (builder, "description", "");
			json_builder_set_member_name (builder, "start");
			json_builder_add_int_value (builder, gal_graph_time (event, "start"));
			json_builder_set_member_name (builder, "end");
			json_builder_add_int_value (builder, gal_graph_time (event, "end"));
			builder_add_string (builder, "calendar_uid", calendar_uid);
			builder_add_string (builder, "calendar_name", args[2] ? args[2] : "");
			builder_add_string (builder, "calendar_color", args[3] ? args[3] : "");
			json_builder_set_member_name (builder, "all_day");
			json_builder_add_boolean_value (builder, json_object_get_boolean_member_with_default (event, "isAllDay", FALSE));
			json_builder_set_member_name (builder, "recurring");
			json_builder_add_boolean_value (builder, g_strcmp0 (type, "occurrence") == 0 || g_strcmp0 (type, "exception") == 0);
			builder_add_categories (builder, NULL);
			json_builder_set_member_name (builder, "readonly");
			json_builder_add_boolean_value (builder, TRUE);
			json_builder_end_object (builder);
			g_free (event_uid);
		}
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	g_free (calendar_uid);
	json_node_unref (root);

	return json;
}

/* Soumet la lecture des événements du calendrier d'une personne (Microsoft 365,
 * lecture seule ; la personne doit avoir partagé son calendrier, ou l'organisation
 * autoriser la consultation des disponibilités).
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : compte Microsoft 365 utilisé ("" : premier compte)
 *   email       : adresse de la personne
 *   name        : nom affiché (repris dans calendar_name)
 *   color       : couleur "#rrggbb" attribuée au calendrier ("" : aucune)
 *   start_unix, end_unix : période en secondes Unix
 *   func        : rappel recevant le tableau JSON des événements
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_list_person_events (EvoCore *core,
			     const char *account_uid,
			     const char *email,
			     const char *name,
			     const char *color,
			     long long start_unix,
			     long long end_unix,
			     EvoCoreResultFunc func,
			     void *user_data)
{
	gchar start_buf[32], end_buf[32];
	gchar *args[] = { (gchar *) (account_uid ? account_uid : ""), (gchar *) (email ? email : ""),
		(gchar *) (name ? name : ""), (gchar *) (color ? color : ""), start_buf, end_buf, NULL };

	g_return_if_fail (core != NULL);

	g_snprintf (start_buf, sizeof (start_buf), "%" G_GINT64_FORMAT, (gint64) start_unix);
	g_snprintf (end_buf, sizeof (end_buf), "%" G_GINT64_FORMAT, (gint64) end_unix);

	core_submit (core, core_list_person_events_run, g_strdupv (args), func, user_data);
}

/* Ajoute à un tableau JSON les personnes d'une réponse Graph (liste "value"). */
static void
gal_add_people_json (JsonBuilder *builder,
		     JsonObject *response,
		     const gchar *relation)
{
	JsonNode *value = json_object_has_member (response, "value") ? json_object_get_member (response, "value") : NULL;
	guint ii;

	if (!value || !JSON_NODE_HOLDS_ARRAY (value))
		return;

	for (ii = 0; ii < json_array_get_length (json_node_get_array (value)); ii++) {
		JsonNode *node = json_array_get_element (json_node_get_array (value), ii);
		GalPerson *person;

		if (!node || !JSON_NODE_HOLDS_OBJECT (node))
			continue;
		person = gal_person_from_graph (json_node_get_object (node));
		if (!person)
			continue;
		json_builder_begin_object (builder);
		builder_add_string (builder, "name", person->name ? person->name : person->email);
		builder_add_string (builder, "email", person->email);
		builder_add_string (builder, "relation", relation);
		json_builder_end_object (builder);
		gal_person_free (person);
	}
}

/* Équipe de l'utilisateur : son responsable, ses collaborateurs directs et les
 * collaborateurs directs de son responsable (équipes Outlook).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = compte (collection)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : [{"name", "email", "relation": "manager"|"report"|"peer"}, ...]. */
static gchar *
core_list_team_run (EvoCore *core,
		    gchar **args,
		    GError **error)
{
	ESource *source = gal_ref_m365_collection (core, args[0]);
	gchar *token = NULL, *manager_email = NULL, *json;
	JsonBuilder *builder;
	guint step;

	if (!source) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Les équipes ne sont disponibles que pour les comptes Microsoft 365.");
		return NULL;
	}
	if (!gal_get_token (core, source, &token, error)) {
		g_object_unref (source);
		return NULL;
	}
	g_object_unref (source);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	for (step = 0; step < 3; step++) {
		static const gchar * const select = "?$select=displayName,givenName,surname,mail,userPrincipalName";
		gchar *uri;
		GBytes *bytes;
		guint status = 0;
		JsonNode *root = NULL;
		JsonObject *object;

		if (step == 0)
			uri = g_strconcat ("https://graph.microsoft.com/v1.0/me/manager", select, NULL);
		else if (step == 1)
			uri = g_strconcat ("https://graph.microsoft.com/v1.0/me/directReports", select, NULL);
		else if (manager_email) {
			gchar *escaped = g_uri_escape_string (manager_email, NULL, TRUE);

			uri = g_strconcat ("https://graph.microsoft.com/v1.0/users/", escaped, "/directReports", select, NULL);
			g_free (escaped);
		} else
			break;

		bytes = gal_http_get (core, GAL_PROVIDER_MICROSOFT365, uri, token, &status, NULL);
		g_free (uri);
		if (!bytes)
			continue;
		if (status >= 200 && status < 300) {
			object = gal_parse_response (bytes, &root, NULL);
			if (object) {
				if (step == 0) {
					GalPerson *person = gal_person_from_graph (object);

					if (person) {
						manager_email = g_strdup (person->email);
						json_builder_begin_object (builder);
						builder_add_string (builder, "name", person->name ? person->name : person->email);
						builder_add_string (builder, "email", person->email);
						builder_add_string (builder, "relation", "manager");
						json_builder_end_object (builder);
						gal_person_free (person);
					}
				} else {
					gal_add_people_json (builder, object, step == 1 ? "report" : "peer");
				}
			}
			if (root)
				json_node_unref (root);
		}
		g_bytes_unref (bytes);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	g_free (manager_email);
	e_util_safe_free_string (token);

	return json;
}

/* Soumet la lecture de l'équipe de l'utilisateur (responsable, collaborateurs
 * directs, collègues du même responsable). Les relations non autorisées par la
 * portée OAuth2 (User.ReadBasic.All) sont simplement omises.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : compte Microsoft 365 utilisé ("" : premier compte)
 *   func        : rappel recevant [{"name", "email", "relation"}, ...]
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_list_team (EvoCore *core,
		    const char *account_uid,
		    EvoCoreResultFunc func,
		    void *user_data)
{
	gchar *args[] = { (gchar *) (account_uid ? account_uid : ""), NULL };

	g_return_if_fail (core != NULL);

	core_submit (core, core_list_team_run, g_strdupv (args), func, user_data);
}
