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
gal_http_get_prefer (EvoCore *core,
		     GalProvider provider,
		     const gchar *uri,
		     const gchar *token,
		     const gchar *prefer,
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
	if (prefer)
		soup_message_headers_replace (soup_message_get_request_headers (msg), "Prefer", prefer);
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

static GBytes *
gal_http_get (EvoCore *core,
	      GalProvider provider,
	      const gchar *uri,
	      const gchar *token,
	      guint *out_status,
	      GError **error)
{
	return gal_http_get_prefer (core, provider, uri, token, NULL, out_status, error);
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

/* Identifiant (objet Entra) d'un utilisateur à partir de son adresse électronique.
 * L'adresse de messagerie peut différer du nom d'utilisateur principal (UPN) :
 * /users/{adresse} passe alors par la boîte Exchange, souvent sans photo, alors
 * que /users/{id} lit la photo du profil Entra.
 * Paramètres :
 *   core     : instance du moteur evo-core
 *   provider : fournisseur (Microsoft 365)
 *   token    : jeton d'accès
 *   email    : adresse de la personne
 * Retourne : l'identifiant (g_free), NULL si introuvable. */
static gchar *
gal_resolve_user_id (EvoCore *core,
		     GalProvider provider,
		     const gchar *token,
		     const gchar *email)
{
	gchar *escaped, *uri, *id = NULL;
	GBytes *bytes;
	guint status = 0;

	if (!email || !*email || strchr (email, '\''))
		return NULL;

	escaped = g_uri_escape_string (email, NULL, TRUE);
	uri = g_strconcat ("https://graph.microsoft.com/v1.0/users?$filter=mail%20eq%20%27", escaped,
		"%27&$select=id,userPrincipalName&$top=1", NULL);
	bytes = gal_http_get (core, provider, uri, token, &status, NULL);

	if (bytes && status >= 200 && status < 300) {
		JsonNode *root = NULL;
		JsonObject *object = gal_parse_response (bytes, &root, NULL);
		JsonNode *value = object && json_object_has_member (object, "value") ?
			json_object_get_member (object, "value") : NULL;

		if (value && JSON_NODE_HOLDS_ARRAY (value) && json_array_get_length (json_node_get_array (value)) > 0) {
			JsonNode *first = json_array_get_element (json_node_get_array (value), 0);

			if (JSON_NODE_HOLDS_OBJECT (first)) {
				const gchar *text = gal_string_member (json_node_get_object (first), "id");

				if (text && *text)
					id = g_strdup (text);
			}
		}
		if (root)
			json_node_unref (root);
	}
	g_message ("photo annuaire Graph : identifiant de %s -> HTTP %u, %s", email, status, id ? id : "introuvable");

	g_clear_pointer (&bytes, g_bytes_unref);
	g_free (uri);
	g_free (escaped);

	return id;
}

/* Image en URI de données (type reconnu à la signature des premiers octets).
 * Paramètres :
 *   data : contenu de l'image
 *   size : taille en octets
 * Retourne : "data:image/...;base64,..." (g_free). */
gchar *
core_photo_data_uri (gconstpointer data,
		     gsize size)
{
	const guchar *bytes = data;
	const gchar *mime = "image/jpeg";
	gchar *base64, *uri;

	if (size > 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
		mime = "image/png";
	else if (size > 6 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F')
		mime = "image/gif";
	else if (size > 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
		 bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P')
		mime = "image/webp";

	base64 = g_base64_encode (data, size);
	uri = g_strconcat ("data:", mime, ";base64,", base64, NULL);
	g_free (base64);

	return uri;
}

/* TRUE si un code HTTP ne permet pas de conclure « pas de photo » (réseau, droits,
 * limitation de débit, erreur serveur) : le résultat ne doit pas être mis en cache. */
static gboolean
gal_status_is_uncertain (guint status)
{
	return status == 0 || status == 401 || status == 403 || status == 429 || status >= 500;
}

/* Photo d'une personne de l'annuaire Microsoft 365 : lecture par l'identifiant du
 * profil (l'adresse peut différer de l'UPN), puis par l'adresse.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   token     : jeton d'accès Graph
 *   email     : adresse de la personne
 *   uncertain : mis à TRUE si l'absence de photo n'est pas certaine (peut être NULL)
 * Retourne : URI de données de la photo (g_free), NULL si aucune. */
static gchar *
gal_m365_photo (EvoCore *core,
		const gchar *token,
		const gchar *email,
		gboolean *uncertain)
{
	gchar *photo = NULL, *identifiers[2] = { NULL, NULL };
	guint ii;

	identifiers[0] = gal_resolve_user_id (core, GAL_PROVIDER_MICROSOFT365, token, email);
	identifiers[1] = g_uri_escape_string (email ? email : "", NULL, TRUE);

	for (ii = 0; ii < G_N_ELEMENTS (identifiers) && !photo; ii++) {
		gchar *uri;
		guint status = 0;
		GBytes *bytes;

		if (!identifiers[ii])
			continue;

		uri = g_strconcat ("https://graph.microsoft.com/v1.0/users/", identifiers[ii], "/photo/$value", NULL);
		bytes = gal_http_get (core, GAL_PROVIDER_MICROSOFT365, uri, token, &status, NULL);
		g_message ("photo annuaire Graph : HTTP %u pour %s via %s (%.200s)", status, email,
			ii == 0 ? "identifiant" : "adresse",
			bytes && status >= 300 && g_bytes_get_data (bytes, NULL) ? (const gchar *) g_bytes_get_data (bytes, NULL) : "");
		if (bytes && status >= 200 && status < 300) {
			gsize size = 0;
			gconstpointer data = g_bytes_get_data (bytes, &size);

			if (data && size > 0)
				photo = core_photo_data_uri (data, size);
		} else if (uncertain && gal_status_is_uncertain (status)) {
			*uncertain = TRUE;
		}
		g_clear_pointer (&bytes, g_bytes_unref);
		g_free (uri);
	}
	g_free (identifiers[0]);
	g_free (identifiers[1]);

	return photo;
}

/* Photo de profil d'une personne de l'annuaire Microsoft 365 (Graph
 * GET /users/{identifiant}/photo/$value).
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
	gchar *json, *photo = NULL;

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_COLLECTION);

	for (slink = sources; slink && !photo; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		GalProvider provider;
		gchar *token = NULL;

		if (!gal_collection_provider (source, &provider) || provider != GAL_PROVIDER_MICROSOFT365)
			continue;
		if (args[0] && *args[0] && g_strcmp0 (args[0], e_source_get_uid (source)) != 0 &&
		    g_strcmp0 (args[0], e_source_get_display_name (source)) != 0)
			continue;
		if (!gal_get_token (core, source, &token, NULL))
			continue;

		photo = gal_m365_photo (core, token, args[1], NULL);
		e_util_safe_free_string (token);
	}
	g_list_free_full (sources, g_object_unref);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "photo", photo ? photo : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	g_free (photo);

	return json;
}

/* Domaine (en minuscules) de l'identité d'une collection ; NULL si inconnu. */
static gchar *
gal_collection_domain (ESource *source)
{
	ESourceCollection *collection;
	gchar *identity, *domain = NULL;
	const gchar *at;

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION))
		return NULL;

	collection = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
	identity = e_source_collection_dup_identity (collection);
	at = identity ? strrchr (identity, '@') : NULL;
	if (at && at[1])
		domain = g_ascii_strdown (at + 1, -1);
	g_free (identity);

	return domain;
}

/* Photo d'une personne de l'annuaire d'un compte Microsoft 365 ou Google Workspace
 * dont le domaine est celui de l'adresse. Les domaines grand public (gmail.com)
 * n'ont pas d'annuaire.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   email     : adresse de la personne
 *   uncertain : mis à TRUE si l'absence de photo n'est pas certaine (peut être NULL)
 * Retourne : URI de données de la photo (g_free), NULL si aucune. */
gchar *
core_gal_photo_by_domain (EvoCore *core,
			  const gchar *email,
			  gboolean *uncertain)
{
	GList *sources, *slink;
	const gchar *at = email ? strrchr (email, '@') : NULL;
	gchar *photo = NULL, *domain;

	if (!at || !at[1])
		return NULL;
	domain = g_ascii_strdown (at + 1, -1);

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_COLLECTION);
	for (slink = sources; slink && !photo; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		GalProvider provider;
		gchar *source_domain, *token = NULL;

		if (!gal_collection_provider (source, &provider))
			continue;
		source_domain = gal_collection_domain (source);
		if (g_strcmp0 (source_domain, domain) != 0 ||
		    (provider == GAL_PROVIDER_GOOGLE &&
		     (g_strcmp0 (domain, "gmail.com") == 0 || g_strcmp0 (domain, "googlemail.com") == 0))) {
			g_free (source_domain);
			continue;
		}
		g_free (source_domain);

		if (!gal_get_token (core, source, &token, NULL)) {
			if (uncertain)
				*uncertain = TRUE;
			continue;
		}

		if (provider == GAL_PROVIDER_MICROSOFT365) {
			photo = gal_m365_photo (core, token, email, uncertain);
		} else {
			GPtrArray *people = g_ptr_array_new_with_free_func (gal_person_free);
			GError *local_error = NULL;
			guint ii;

			if (gal_search_google (core, e_source_get_display_name (source), token, email, 5, people, &local_error)) {
				for (ii = 0; ii < people->len && !photo; ii++) {
					const GalPerson *person = people->pdata[ii];
					SoupSession *soup;
					SoupMessage *msg;
					GBytes *bytes;

					if (g_ascii_strcasecmp (person->email ? person->email : "", email) != 0 ||
					    !person->photo_url || !g_str_has_prefix (person->photo_url, "https://"))
						continue;

					/* URL publique : aucun jeton n'est envoyé */
					soup = soup_session_new_with_options ("timeout", 15, "user-agent", "Evolution (Windows)", NULL);
					msg = soup_message_new ("GET", person->photo_url);
					bytes = soup_session_send_and_read (soup, msg, core->cancellable, NULL);
					if (bytes && soup_message_get_status (msg) >= 200 && soup_message_get_status (msg) < 300) {
						gsize size = 0;
						gconstpointer data = g_bytes_get_data (bytes, &size);

						if (data && size > 0)
							photo = core_photo_data_uri (data, size);
					} else if (uncertain && (!bytes || gal_status_is_uncertain (soup_message_get_status (msg)))) {
						*uncertain = TRUE;
					}
					g_clear_pointer (&bytes, g_bytes_unref);
					g_object_unref (msg);
					g_object_unref (soup);
				}
			} else if (uncertain) {
				*uncertain = TRUE;
			}
			g_clear_error (&local_error);
			g_ptr_array_unref (people);
		}
		e_util_safe_free_string (token);
	}
	g_list_free_full (sources, g_object_unref);
	g_free (domain);

	return photo;
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

/* Diagnostic : journalise l'utilisateur réellement associé au jeton d'un compte
 * Microsoft 365 (Graph GET /me) et l'état de sa boîte de réception, pour
 * comprendre un échec de connexion (jeton émis pour un autre utilisateur,
 * boîte aux lettres absente...). Sans effet pour les autres comptes.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   display_name : nom affiché du compte (service de messagerie) */
void
core_log_m365_identity (EvoCore *core,
			const gchar *display_name)
{
	GList *sources, *slink;

	sources = e_source_registry_list_enabled (core->registry, E_SOURCE_EXTENSION_COLLECTION);
	for (slink = sources; slink; slink = g_list_next (slink)) {
		ESource *source = slink->data;
		GalProvider provider;
		gchar *token = NULL;
		const gchar *uris[] = {
			"https://graph.microsoft.com/v1.0/me?$select=id,userPrincipalName,mail",
			"https://graph.microsoft.com/v1.0/me/mailFolders/inbox?$select=id,displayName,totalItemCount",
			NULL
		};
		guint ii;

		if (!gal_collection_provider (source, &provider) || provider != GAL_PROVIDER_MICROSOFT365 ||
		    g_strcmp0 (display_name, e_source_get_display_name (source)) != 0)
			continue;
		if (!gal_get_token (core, source, &token, NULL)) {
			g_message ("diagnostic Graph « %s » : jeton d'accès introuvable", display_name);
			continue;
		}

		for (ii = 0; uris[ii]; ii++) {
			guint status = 0;
			GBytes *bytes = gal_http_get (core, provider, uris[ii], token, &status, NULL);
			const gchar *body = bytes ? g_bytes_get_data (bytes, NULL) : NULL;

			g_message ("diagnostic Graph « %s » : %s -> HTTP %u %.300s", display_name,
				strstr (uris[ii], "/me/mailFolders") ? "/me/mailFolders/inbox" : "/me",
				status, body ? body : "");
			g_clear_pointer (&bytes, g_bytes_unref);
		}
		e_util_safe_free_string (token);
	}
	g_list_free_full (sources, g_object_unref);
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

/* Groupes de calendriers Outlook du compte (Graph /me/calendarGroups) et calendriers
 * qu'ils contiennent. Les groupes d'équipes ou de salles créés dans Outlook y figurent
 * par leur nom ; leurs membres (personnes, salles) ne sont pas exposés par Graph.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = compte (collection)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : [{"name", "class_id", "calendars": [{"name", "address"}]}, ...]. */
static gchar *
core_list_calendar_groups_run (EvoCore *core,
			       gchar **args,
			       GError **error)
{
	ESource *source = gal_ref_m365_collection (core, args[0]);
	gchar *token = NULL, *json = NULL;
	GBytes *bytes;
	guint status = 0;
	JsonNode *root = NULL;
	JsonObject *object;
	JsonBuilder *builder;
	JsonArray *groups;
	guint ii;

	if (!source) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Les groupes de calendriers ne sont disponibles que pour les comptes Microsoft 365.");
		return NULL;
	}
	if (!gal_get_token (core, source, &token, error)) {
		g_object_unref (source);
		return NULL;
	}
	g_object_unref (source);

	bytes = gal_http_get (core, GAL_PROVIDER_MICROSOFT365,
		"https://graph.microsoft.com/v1.0/me/calendarGroups?$select=id,name,classId", token, &status, error);
	if (!bytes) {
		e_util_safe_free_string (token);
		return NULL;
	}
	if (status < 200 || status >= 300) {
		gal_set_http_error (GAL_PROVIDER_MICROSOFT365, args[0], status, bytes, error);
		g_bytes_unref (bytes);
		e_util_safe_free_string (token);
		return NULL;
	}
	object = gal_parse_response (bytes, &root, error);
	g_bytes_unref (bytes);
	if (!object) {
		e_util_safe_free_string (token);
		return NULL;
	}

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	groups = json_object_has_member (object, "value") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (object, "value"))
		? json_object_get_array_member (object, "value") : NULL;
	for (ii = 0; groups && ii < json_array_get_length (groups); ii++) {
		JsonObject *group;
		gchar *escaped, *uri;
		GBytes *calendars_bytes;
		guint calendars_status = 0;
		JsonNode *calendars_root = NULL;
		JsonObject *calendars_object = NULL;

		if (!JSON_NODE_HOLDS_OBJECT (json_array_get_element (groups, ii)))
			continue;
		group = json_array_get_object_element (groups, ii);

		json_builder_begin_object (builder);
		builder_add_string (builder, "name", gal_string_member (group, "name"));
		builder_add_string (builder, "class_id", gal_string_member (group, "classId"));
		json_builder_set_member_name (builder, "calendars");
		json_builder_begin_array (builder);

		escaped = g_uri_escape_string (gal_string_member (group, "id"), NULL, TRUE);
		uri = g_strconcat ("https://graph.microsoft.com/v1.0/me/calendarGroups/", escaped,
			"/calendars?$select=name,owner", NULL);
		calendars_bytes = gal_http_get (core, GAL_PROVIDER_MICROSOFT365, uri, token, &calendars_status, NULL);
		if (calendars_bytes && calendars_status >= 200 && calendars_status < 300)
			calendars_object = gal_parse_response (calendars_bytes, &calendars_root, NULL);
		if (calendars_object && json_object_has_member (calendars_object, "value") &&
		    JSON_NODE_HOLDS_ARRAY (json_object_get_member (calendars_object, "value"))) {
			JsonArray *list = json_object_get_array_member (calendars_object, "value");
			guint jj;

			for (jj = 0; jj < json_array_get_length (list); jj++) {
				JsonObject *calendar, *owner;

				if (!JSON_NODE_HOLDS_OBJECT (json_array_get_element (list, jj)))
					continue;
				calendar = json_array_get_object_element (list, jj);
				owner = json_object_has_member (calendar, "owner") &&
					JSON_NODE_HOLDS_OBJECT (json_object_get_member (calendar, "owner"))
					? json_object_get_object_member (calendar, "owner") : NULL;
				json_builder_begin_object (builder);
				builder_add_string (builder, "name", gal_string_member (calendar, "name"));
				builder_add_string (builder, "address", owner ? gal_string_member (owner, "address") : "");
				json_builder_end_object (builder);
			}
		}
		if (calendars_root)
			json_node_unref (calendars_root);
		g_clear_pointer (&calendars_bytes, g_bytes_unref);
		g_free (uri);
		g_free (escaped);

		json_builder_end_array (builder);
		json_builder_end_object (builder);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	json_node_unref (root);
	e_util_safe_free_string (token);

	return json;
}

/* Soumet la lecture des groupes de calendriers Outlook du compte (voir
 * evo_core_list_calendar_groups dans evo-core.h).
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : compte Microsoft 365 ("" : premier compte)
 *   func        : rappel recevant [{"name", "class_id", "calendars": [{"name", "address"}]}]
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_list_calendar_groups (EvoCore *core,
			       const char *account_uid,
			       EvoCoreResultFunc func,
			       void *user_data)
{
	gchar *args[] = { (gchar *) (account_uid ? account_uid : ""), NULL };

	g_return_if_fail (core != NULL);

	core_submit_background (core, core_list_calendar_groups_run, g_strdupv (args), func, user_data);
}

/* --- groupes de calendriers d'Outlook lus par EWS (navigation de la boîte aux lettres) --- */

/* Requête POST authentifiée (Bearer) ; retourne le corps de la réponse (même en
 * erreur HTTP), NULL si injoignable.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   uri          : adresse du service
 *   token        : jeton d'accès, ou NULL pour aucune authentification
 *   content_type : type du corps
 *   body         : corps de la requête
 *   out_status   : reçoit le code HTTP (0 si injoignable)
 *   error        : emplacement pour une erreur réseau */
static GBytes *
gal_http_post (EvoCore *core,
	       const gchar *uri,
	       const gchar *token,
	       const gchar *content_type,
	       const gchar *body,
	       guint *out_status,
	       GError **error)
{
	SoupSession *soup;
	SoupMessage *msg;
	GBytes *request_body, *bytes;
	GError *local_error = NULL;

	soup = soup_session_new_with_options ("timeout", 30, "user-agent", "Evolution (Windows)", NULL);
	msg = soup_message_new ("POST", uri);
	if (token) {
		gchar *authorization = g_strdup_printf ("Bearer %s", token);

		soup_message_headers_replace (soup_message_get_request_headers (msg), "Authorization", authorization);
		g_free (authorization);
	}
	request_body = g_bytes_new (body, strlen (body));
	soup_message_set_request_body_from_bytes (msg, content_type, request_body);
	g_bytes_unref (request_body);

	bytes = soup_session_send_and_read (soup, msg, core->cancellable, &local_error);
	*out_status = bytes ? soup_message_get_status (msg) : 0;
	if (!bytes)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND,
			"Le serveur est injoignable : %s", local_error ? local_error->message : "erreur réseau");

	g_clear_error (&local_error);
	g_object_unref (msg);
	g_object_unref (soup);

	return bytes;
}

/* Jeton d'accès pour EWS (Exchange Online, EWS.AccessAsUser.All) obtenu avec le jeton de
 * renouvellement du compte Microsoft 365. Le jeton n'est pas mémorisé : les jetons Graph
 * du compte restent intacts. Échoue si l'application n'est pas autorisée à accéder à EWS
 * (consentement de l'administrateur).
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   source    : source de la collection Microsoft 365
 *   out_token : reçoit le jeton (e_util_safe_free_string)
 *   error     : emplacement pour une éventuelle erreur */
static gboolean
gal_get_ews_token (EvoCore *core,
		   ESource *source,
		   gchar **out_token,
		   GError **error)
{
	ESourceAuthentication *authentication;
	gchar *user, *store_id, *uid, *secret = NULL, *client_id = NULL, *form, *scope, *refresh_enc, *scope_enc, *client_enc;
	const gchar *refresh_token = NULL;
	JsonNode *secret_root = NULL, *token_root = NULL;
	JsonObject *secret_object, *token_object;
	GBytes *bytes;
	guint status = 0;
	gboolean ok = FALSE;

	*out_token = NULL;
	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Compte sans authentification OAuth2");
		return FALSE;
	}
	authentication = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	user = e_source_authentication_dup_user (authentication);
	store_id = e_source_authentication_dup_credential_store_id (authentication);
	if (store_id && *store_id)
		uid = g_strdup_printf ("OAuth2::Microsoft365[%s][%s]", user ? user : "", store_id);
	else
		uid = g_strdup_printf ("OAuth2::Microsoft365[%s]", user ? user : "");
	g_free (user);
	g_free (store_id);

	if (!e_secret_store_lookup_sync (uid, &secret, core->cancellable, error))
		goto exit;
	if (!secret) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Jeton de renouvellement du compte introuvable");
		goto exit;
	}

	secret_root = json_from_string (secret, NULL);
	secret_object = secret_root && JSON_NODE_HOLDS_OBJECT (secret_root) ? json_node_get_object (secret_root) : NULL;
	refresh_token = secret_object ? gal_string_member (secret_object, "refresh_token") : NULL;
	if (!refresh_token || !*refresh_token) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Jeton de renouvellement du compte vide");
		goto exit;
	}

	if (e_source_has_extension (source, "Microsoft365 Backend")) {
		GObject *extension = G_OBJECT (e_source_get_extension (source, "Microsoft365 Backend"));

		if (g_object_class_find_property (G_OBJECT_GET_CLASS (extension), "oauth2-client-id"))
			g_object_get (extension, "oauth2-client-id", &client_id, NULL);
	}
	if (!client_id || !*client_id) {
		g_free (client_id);
		client_id = g_strdup ("20460e5d-ce91-49af-a3a5-70b6be7486d1");
	}

	scope = g_strdup ("https://outlook.office365.com/EWS.AccessAsUser.All offline_access");
	scope_enc = g_uri_escape_string (scope, NULL, FALSE);
	refresh_enc = g_uri_escape_string (refresh_token, NULL, FALSE);
	client_enc = g_uri_escape_string (client_id, NULL, FALSE);
	form = g_strdup_printf ("client_id=%s&grant_type=refresh_token&refresh_token=%s&scope=%s"
		"&redirect_uri=https%%3A%%2F%%2Flogin.microsoftonline.com%%2Fcommon%%2Foauth2%%2Fnativeclient",
		client_enc, refresh_enc, scope_enc);
	g_free (scope);
	g_free (scope_enc);
	g_free (refresh_enc);
	g_free (client_enc);

	bytes = gal_http_post (core, "https://login.microsoftonline.com/common/oauth2/v2.0/token", NULL,
		"application/x-www-form-urlencoded", form, &status, error);
	e_util_safe_free_string (form);
	if (!bytes)
		goto exit;

	token_object = gal_parse_response (bytes, &token_root, NULL);
	if (token_object && *gal_string_member (token_object, "access_token")) {
		*out_token = g_strdup (gal_string_member (token_object, "access_token"));
		ok = TRUE;
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"Accès EWS refusé pour l'application (HTTP %u) : %s", status,
			token_object && *gal_string_member (token_object, "error_description")
			? gal_string_member (token_object, "error_description") : "réponse inattendue");
	}
	g_bytes_unref (bytes);

 exit:
	if (token_root)
		json_node_unref (token_root);
	if (secret_root)
		json_node_unref (secret_root);
	e_util_safe_free_string (secret);
	g_free (client_id);
	g_free (uid);

	return ok;
}

/* Lien de navigation (raccourci vers un calendrier) lu dans « Common Views ». */
typedef struct {
	gchar *name;		/* nom affiché du calendrier (objet de l'élément) */
	gchar *group;		/* nom du groupe de navigation */
	gint link_type;		/* PidTagWlinkType (2 : calendrier) ; -1 si absent */
} NavLink;

static void
nav_link_free (gpointer data)
{
	NavLink *link = data;

	g_free (link->name);
	g_free (link->group);
	g_free (link);
}

typedef struct {
	GPtrArray *links;
	NavLink *current;
	gchar *pending_tag;	/* PropertyTag de la propriété étendue en cours */
	GString *text;		/* texte de l'élément en cours */
	gboolean in_subject;
	gboolean in_value;
} NavParse;

/* Éléments EWS d'un élément de la liste : ouverture. */
static void
nav_xml_start (GMarkupParseContext *context,
	       const gchar *element,
	       const gchar **names,
	       const gchar **values,
	       gpointer user_data,
	       GError **error)
{
	NavParse *parse = user_data;
	guint ii;

	if (g_str_has_suffix (element, ":Message") || g_str_has_suffix (element, ":Item") ||
	    g_str_has_suffix (element, ":PostItem")) {
		parse->current = g_new0 (NavLink, 1);
		parse->current->link_type = -1;
	} else if (parse->current && g_str_has_suffix (element, ":Subject")) {
		parse->in_subject = TRUE;
		g_string_truncate (parse->text, 0);
	} else if (parse->current && g_str_has_suffix (element, ":ExtendedFieldURI")) {
		g_clear_pointer (&parse->pending_tag, g_free);
		for (ii = 0; names[ii]; ii++)
			if (g_strcmp0 (names[ii], "PropertyTag") == 0)
				parse->pending_tag = g_strdup (values[ii]);
	} else if (parse->current && g_str_has_suffix (element, ":Value")) {
		parse->in_value = TRUE;
		g_string_truncate (parse->text, 0);
	}
}

/* Texte d'un élément. */
static void
nav_xml_text (GMarkupParseContext *context,
	      const gchar *text,
	      gsize len,
	      gpointer user_data,
	      GError **error)
{
	NavParse *parse = user_data;

	if (parse->in_subject || parse->in_value)
		g_string_append_len (parse->text, text, len);
}

/* Fermeture d'un élément : valeur de propriété, objet ou fin d'élément. */
static void
nav_xml_end (GMarkupParseContext *context,
	     const gchar *element,
	     gpointer user_data,
	     GError **error)
{
	NavParse *parse = user_data;

	if (parse->in_subject && g_str_has_suffix (element, ":Subject")) {
		parse->in_subject = FALSE;
		if (parse->current)
			parse->current->name = g_strdup (parse->text->str);
	} else if (parse->in_value && g_str_has_suffix (element, ":Value")) {
		parse->in_value = FALSE;
		if (parse->current && parse->pending_tag) {
			if (g_ascii_strcasecmp (parse->pending_tag, "0x6851") == 0) {
				g_free (parse->current->group);
				parse->current->group = g_strdup (parse->text->str);
			} else if (g_ascii_strcasecmp (parse->pending_tag, "0x6849") == 0) {
				parse->current->link_type = (gint) g_ascii_strtoll (parse->text->str, NULL, 10);
			}
		}
	} else if (parse->current && (g_str_has_suffix (element, ":Message") || g_str_has_suffix (element, ":Item") ||
		   g_str_has_suffix (element, ":PostItem"))) {
		g_ptr_array_add (parse->links, parse->current);
		parse->current = NULL;
	}
}

/* Texte d'erreur d'une réponse EWS (faultstring, MessageText ou code de réponse), sinon un
 * extrait de la réponse. Retourne une chaîne à libérer (g_free). */
static gchar *
nav_error_detail (GBytes *bytes)
{
	gsize size = 0;
	const gchar *data = bytes ? g_bytes_get_data (bytes, &size) : NULL;
	gchar *text, *result = NULL;
	static const gchar * const tags[][2] = {
		{ "<faultstring>", "</faultstring>" },
		{ "<m:MessageText>", "</m:MessageText>" },
		{ "<MessageText>", "</MessageText>" },
		{ "<m:ResponseCode>", "</m:ResponseCode>" },
		{ "<ResponseCode>", "</ResponseCode>" }
	};
	guint ii;

	if (!data || !size)
		return g_strdup ("réponse vide");
	text = g_strndup (data, size);
	for (ii = 0; ii < G_N_ELEMENTS (tags) && !result; ii++) {
		const gchar *start = strstr (text, tags[ii][0]);

		if (start) {
			const gchar *end = strstr (start + strlen (tags[ii][0]), tags[ii][1]);

			if (end)
				result = g_strndup (start + strlen (tags[ii][0]), end - start - strlen (tags[ii][0]));
		}
	}
	if (!result)
		result = g_strndup (text, MIN (size, 240));
	g_free (text);

	return result;
}

/* Recherche du dossier « Common Views » parmi les enfants de la racine de la boîte (FindFolder). */
typedef struct {
	gchar *current_id;
	GString *text;
	gboolean in_name;
	gchar *found_id;
	GString *names;		/* noms vus, pour le message d'erreur */
} FolderParse;

static void
folder_xml_start (GMarkupParseContext *context,
		  const gchar *element,
		  const gchar **names,
		  const gchar **values,
		  gpointer user_data,
		  GError **error)
{
	FolderParse *parse = user_data;
	guint ii;

	if (g_str_has_suffix (element, ":FolderId")) {
		for (ii = 0; names[ii]; ii++)
			if (g_strcmp0 (names[ii], "Id") == 0) {
				g_free (parse->current_id);
				parse->current_id = g_strdup (values[ii]);
			}
	} else if (g_str_has_suffix (element, ":DisplayName")) {
		parse->in_name = TRUE;
		g_string_truncate (parse->text, 0);
	}
}

static void
folder_xml_text (GMarkupParseContext *context,
		 const gchar *text,
		 gsize len,
		 gpointer user_data,
		 GError **error)
{
	FolderParse *parse = user_data;

	if (parse->in_name)
		g_string_append_len (parse->text, text, len);
}

static void
folder_xml_end (GMarkupParseContext *context,
		const gchar *element,
		gpointer user_data,
		GError **error)
{
	FolderParse *parse = user_data;

	if (parse->in_name && g_str_has_suffix (element, ":DisplayName")) {
		static const gchar * const wanted[] = { "Common Views", "Affichages communs", "Vues communes", "Gemeinsame Ansichten" };
		guint ii;

		parse->in_name = FALSE;
		g_string_append_printf (parse->names, "%s%s", parse->names->len ? ", " : "", parse->text->str);
		for (ii = 0; ii < G_N_ELEMENTS (wanted); ii++)
			if (!parse->found_id && parse->current_id && g_ascii_strcasecmp (parse->text->str, wanted[ii]) == 0)
				parse->found_id = g_strdup (parse->current_id);
	}
}

/* Identifiant EWS du dossier « Common Views » (navigation des calendriers). Retourne NULL
 * avec une erreur sinon.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   token : jeton EWS
 *   error : emplacement pour une éventuelle erreur */
static gchar *
nav_find_common_views (EvoCore *core,
		       const gchar *token,
		       GError **error)
{
	static const gchar request[] =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"xmlns:t=\"http://schemas.microsoft.com/exchange/services/2006/types\" "
		"xmlns:m=\"http://schemas.microsoft.com/exchange/services/2006/messages\">"
		"<soap:Header><t:RequestServerVersion Version=\"Exchange2013\"/></soap:Header>"
		"<soap:Body><m:FindFolder Traversal=\"Shallow\">"
		"<m:FolderShape><t:BaseShape>IdOnly</t:BaseShape><t:AdditionalProperties>"
		"<t:FieldURI FieldURI=\"folder:DisplayName\"/></t:AdditionalProperties></m:FolderShape>"
		"<m:ParentFolderIds><t:DistinguishedFolderId Id=\"root\"/></m:ParentFolderIds>"
		"</m:FindFolder></soap:Body></soap:Envelope>";
	GBytes *bytes;
	guint status = 0;
	FolderParse parse = { 0 };
	GMarkupParser parser = { folder_xml_start, folder_xml_end, folder_xml_text, NULL, NULL };
	GMarkupParseContext *context;
	gconstpointer data;
	gsize size = 0;
	gchar *found = NULL;

	bytes = gal_http_post (core, "https://outlook.office365.com/EWS/Exchange.asmx", token,
		"text/xml; charset=utf-8", request, &status, error);
	if (!bytes)
		return NULL;
	if (status < 200 || status >= 300) {
		gchar *detail = nav_error_detail (bytes);

		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"EWS : recherche du dossier de navigation refusée (HTTP %u) : %s", status, detail);
		g_free (detail);
		g_bytes_unref (bytes);
		return NULL;
	}

	parse.text = g_string_new (NULL);
	parse.names = g_string_new (NULL);
	data = g_bytes_get_data (bytes, &size);
	context = g_markup_parse_context_new (&parser, 0, &parse, NULL);
	g_markup_parse_context_parse (context, data ? data : "", size, NULL);
	g_markup_parse_context_end_parse (context, NULL);
	g_markup_parse_context_free (context);
	g_bytes_unref (bytes);

	found = parse.found_id;
	if (!found)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"EWS : dossier « Common Views » introuvable (dossiers vus : %s).", parse.names->str);
	g_free (parse.current_id);
	g_string_free (parse.text, TRUE);
	g_string_free (parse.names, TRUE);

	return found;
}

/* Groupes de calendriers d'Outlook (navigation de la boîte aux lettres) lus par EWS :
 * FindItem « Associated » dans le dossier « Common Views », éléments
 * IPM.Microsoft.WunderBar.Link (un par calendrier de personne, de salle ou de groupe).
 * Le jeton EWS est demandé avec le jeton de renouvellement du compte ; si l'application
 * n'y est pas autorisée, l'erreur du serveur est renvoyée telle quelle.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = compte (collection)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : [{"name": groupe, "calendars": [{"name", "email"}]}, ...]. */
static gchar *
core_list_nav_calendars_run (EvoCore *core,
			     gchar **args,
			     GError **error)
{
	static const gchar request[] =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"xmlns:t=\"http://schemas.microsoft.com/exchange/services/2006/types\" "
		"xmlns:m=\"http://schemas.microsoft.com/exchange/services/2006/messages\">"
		"<soap:Header><t:RequestServerVersion Version=\"Exchange2013\"/></soap:Header>"
		"<soap:Body><m:FindItem Traversal=\"Associated\">"
		"<m:ItemShape><t:BaseShape>IdOnly</t:BaseShape><t:AdditionalProperties>"
		"<t:FieldURI FieldURI=\"item:Subject\"/>"
		"<t:ExtendedFieldURI PropertyTag=\"0x6849\" PropertyType=\"Integer\"/>"
		"<t:ExtendedFieldURI PropertyTag=\"0x6851\" PropertyType=\"String\"/>"
		"</t:AdditionalProperties></m:ItemShape>"
		"<m:IndexedPageItemView MaxEntriesReturned=\"500\" Offset=\"0\" BasePoint=\"Beginning\"/>"
		"<m:Restriction><t:IsEqualTo><t:FieldURI FieldURI=\"item:ItemClass\"/>"
		"<t:FieldURIOrConstant><t:Constant Value=\"IPM.Microsoft.WunderBar.Link\"/></t:FieldURIOrConstant>"
		"</t:IsEqualTo></m:Restriction>"
		"<m:ParentFolderIds><t:FolderId Id=\"%s\"/></m:ParentFolderIds>"
		"</m:FindItem></soap:Body></soap:Envelope>";
	ESource *source = gal_ref_m365_collection (core, args[0]);
	gchar *ews_token = NULL, *graph_token = NULL, *json = NULL, *common_views, *find_request;
	GBytes *bytes;
	guint status = 0;
	NavParse parse = { 0 };
	GMarkupParser parser = { nav_xml_start, nav_xml_end, nav_xml_text, NULL, NULL };
	GMarkupParseContext *context;
	GHashTable *groups;	/* nom du groupe -> tableau de NavLink * (non possédés) */
	GList *group_names, *glink;
	JsonBuilder *builder;
	gconstpointer data;
	gsize size = 0;
	guint ii;

	if (!source) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Les groupes de calendriers ne sont disponibles que pour les comptes Microsoft 365.");
		return NULL;
	}
	if (!gal_get_ews_token (core, source, &ews_token, error) ||
	    !gal_get_token (core, source, &graph_token, error)) {
		g_object_unref (source);
		e_util_safe_free_string (ews_token);
		return NULL;
	}
	g_object_unref (source);

	common_views = nav_find_common_views (core, ews_token, error);
	if (!common_views) {
		e_util_safe_free_string (ews_token);
		e_util_safe_free_string (graph_token);
		return NULL;
	}
	find_request = g_strdup_printf (request, common_views);
	g_free (common_views);

	bytes = gal_http_post (core, "https://outlook.office365.com/EWS/Exchange.asmx", ews_token,
		"text/xml; charset=utf-8", find_request, &status, error);
	g_free (find_request);
	e_util_safe_free_string (ews_token);
	if (!bytes) {
		e_util_safe_free_string (graph_token);
		return NULL;
	}
	if (status < 200 || status >= 300) {
		gchar *detail = nav_error_detail (bytes);

		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"EWS a refusé la lecture de la navigation des calendriers (HTTP %u) : %s", status, detail);
		g_free (detail);
		g_bytes_unref (bytes);
		e_util_safe_free_string (graph_token);
		return NULL;
	}

	parse.links = g_ptr_array_new_with_free_func (nav_link_free);
	parse.text = g_string_new (NULL);
	data = g_bytes_get_data (bytes, &size);
	context = g_markup_parse_context_new (&parser, 0, &parse, NULL);
	if (!g_markup_parse_context_parse (context, data ? data : "", size, NULL) ||
	    !g_markup_parse_context_end_parse (context, NULL)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Réponse EWS illisible");
		g_markup_parse_context_free (context);
		g_bytes_unref (bytes);
		g_ptr_array_unref (parse.links);
		g_string_free (parse.text, TRUE);
		g_free (parse.pending_tag);
		e_util_safe_free_string (graph_token);
		return NULL;
	}
	g_markup_parse_context_free (context);
	g_bytes_unref (bytes);

	/* regroupe les raccourcis de calendrier par groupe de navigation */
	groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
	for (ii = 0; ii < parse.links->len; ii++) {
		NavLink *link = parse.links->pdata[ii];
		GPtrArray *members;
		const gchar *group = link->group && *link->group ? link->group : "";

		if (!link->name || !*link->name || (link->link_type != -1 && link->link_type != 2))
			continue;
		members = g_hash_table_lookup (groups, group);
		if (!members) {
			members = g_ptr_array_new ();
			g_hash_table_insert (groups, g_strdup (group), members);
		}
		g_ptr_array_add (members, link);
	}

	builder = json_builder_new ();
	json_builder_begin_array (builder);
	group_names = g_hash_table_get_keys (groups);
	group_names = g_list_sort (group_names, (GCompareFunc) g_strcmp0);
	for (glink = group_names; glink; glink = g_list_next (glink)) {
		GPtrArray *members = g_hash_table_lookup (groups, glink->data);

		json_builder_begin_object (builder);
		builder_add_string (builder, "name", glink->data);
		json_builder_set_member_name (builder, "calendars");
		json_builder_begin_array (builder);
		for (ii = 0; ii < members->len; ii++) {
			NavLink *link = members->pdata[ii];
			gchar term[160];
			GPtrArray *people = g_ptr_array_new_with_free_func (gal_person_free);
			const gchar *email = "";
			guint kk;

			/* adresse de la personne ou de la salle : recherche de son nom dans l'annuaire */
			gal_sanitize_term (link->name, term, sizeof (term));
			if (gal_query_valid (term) &&
			    gal_search_graph (core, "", graph_token, term, 5, people, NULL)) {
				for (kk = 0; kk < people->len; kk++) {
					GalPerson *person = people->pdata[kk];

					if (person->name && g_ascii_strcasecmp (person->name, link->name) == 0) {
						email = person->email;
						break;
					}
				}
			}
			json_builder_begin_object (builder);
			builder_add_string (builder, "name", link->name);
			builder_add_string (builder, "email", email);
			json_builder_end_object (builder);
			g_ptr_array_unref (people);
		}
		json_builder_end_array (builder);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	g_list_free (group_names);
	g_hash_table_destroy (groups);
	g_ptr_array_unref (parse.links);
	g_string_free (parse.text, TRUE);
	g_free (parse.pending_tag);
	e_util_safe_free_string (graph_token);

	return json;
}

/* Soumet la lecture des groupes de calendriers d'Outlook par EWS (voir
 * evo_core_list_nav_calendars dans evo-core.h).
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : compte Microsoft 365 ("" : premier compte)
 *   func        : rappel recevant [{"name": groupe, "calendars": [{"name", "email"}]}]
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_list_nav_calendars (EvoCore *core,
			     const char *account_uid,
			     EvoCoreResultFunc func,
			     void *user_data)
{
	gchar *args[] = { (gchar *) (account_uid ? account_uid : ""), NULL };

	g_return_if_fail (core != NULL);

	core_submit_background (core, core_list_nav_calendars_run, g_strdupv (args), func, user_data);
}

/* Adresse de la page d'autorisation Microsoft pour l'accès EWS (lecture de la
 * navigation des calendriers d'Outlook). L'utilisateur y accorde l'accès à Exchange
 * Online pour l'application ; la page se termine sur une page blanche de Microsoft,
 * sans autre action (l'autorisation est mémorisée côté Microsoft).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = compte (collection)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"url": "https://login.microsoftonline.com/..."}. */
static gchar *
core_ews_consent_url_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	ESource *source = gal_ref_m365_collection (core, args[0]);
	ESourceAuthentication *authentication;
	gchar *user, *client_id = NULL, *client_enc, *hint_enc, *scope_enc, *redirect_enc, *url, *json;
	JsonBuilder *builder;

	if (!source) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"L'autorisation EWS n'est disponible que pour les comptes Microsoft 365.");
		return NULL;
	}

	if (e_source_has_extension (source, "Microsoft365 Backend")) {
		GObject *extension = G_OBJECT (e_source_get_extension (source, "Microsoft365 Backend"));

		if (g_object_class_find_property (G_OBJECT_GET_CLASS (extension), "oauth2-client-id"))
			g_object_get (extension, "oauth2-client-id", &client_id, NULL);
	}
	if (!client_id || !*client_id) {
		g_free (client_id);
		client_id = g_strdup ("20460e5d-ce91-49af-a3a5-70b6be7486d1");
	}
	authentication = e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)
		? e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION) : NULL;
	user = authentication ? e_source_authentication_dup_user (authentication) : NULL;
	g_object_unref (source);

	client_enc = g_uri_escape_string (client_id, NULL, FALSE);
	hint_enc = g_uri_escape_string (user ? user : "", NULL, FALSE);
	scope_enc = g_uri_escape_string ("https://outlook.office365.com/EWS.AccessAsUser.All offline_access", NULL, FALSE);
	redirect_enc = g_uri_escape_string ("https://login.microsoftonline.com/common/oauth2/nativeclient", NULL, FALSE);
	url = g_strdup_printf ("https://login.microsoftonline.com/common/oauth2/v2.0/authorize?client_id=%s&response_type=code"
		"&redirect_uri=%s&response_mode=query&scope=%s&login_hint=%s&prompt=consent",
		client_enc, redirect_enc, scope_enc, hint_enc);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "url", url);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	g_free (url);
	g_free (client_enc);
	g_free (hint_enc);
	g_free (scope_enc);
	g_free (redirect_enc);
	g_free (client_id);
	g_free (user);

	return json;
}

/* Soumet la lecture de l'adresse de la page d'autorisation EWS (voir
 * evo_core_get_ews_consent_url dans evo-core.h).
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : compte Microsoft 365 ("" : premier compte)
 *   func        : rappel recevant {"url": "..."}
 *   user_data   : données utilisateur transmises au rappel */
void
evo_core_get_ews_consent_url (EvoCore *core,
			      const char *account_uid,
			      EvoCoreResultFunc func,
			      void *user_data)
{
	gchar *args[] = { (gchar *) (account_uid ? account_uid : ""), NULL };

	g_return_if_fail (core != NULL);

	core_submit (core, core_ews_consent_url_run, g_strdupv (args), func, user_data);
}

/* Requête JSON authentifiée (Bearer) avec une méthode HTTP quelconque (PATCH...).
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   method     : méthode HTTP
 *   uri        : adresse du service
 *   token      : jeton d'accès
 *   body       : corps JSON
 *   out_status : reçoit le code HTTP (0 si injoignable)
 *   error      : emplacement pour une erreur réseau
 * Retourne : le corps de la réponse (même en erreur HTTP), NULL si injoignable. */
static GBytes *
gal_http_send_json (EvoCore *core,
		    const gchar *method,
		    const gchar *uri,
		    const gchar *token,
		    const gchar *body,
		    guint *out_status,
		    GError **error)
{
	SoupSession *soup;
	SoupMessage *msg;
	GBytes *request_body, *bytes;
	GError *local_error = NULL;
	gchar *authorization;

	soup = soup_session_new_with_options ("timeout", 30, "user-agent", "Evolution (Windows)", NULL);
	msg = soup_message_new (method, uri);
	authorization = g_strdup_printf ("Bearer %s", token);
	soup_message_headers_replace (soup_message_get_request_headers (msg), "Authorization", authorization);
	g_free (authorization);
	request_body = g_bytes_new (body, strlen (body));
	soup_message_set_request_body_from_bytes (msg, "application/json", request_body);
	g_bytes_unref (request_body);

	bytes = soup_session_send_and_read (soup, msg, core->cancellable, &local_error);
	*out_status = bytes ? soup_message_get_status (msg) : 0;
	if (!bytes)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND,
			"Google Agenda est injoignable : %s", local_error ? local_error->message : "erreur réseau");

	g_clear_error (&local_error);
	g_object_unref (msg);
	g_object_unref (soup);

	return bytes;
}

/* Identifiant Google Agenda d'un calendrier EDS : segment suivant « /v2/ » dans l'adresse CalDAV
 * (https://apidata.googleusercontent.com/caldav/v2/<id>/events), « primary » à défaut.
 * Paramètres :
 *   source : source du calendrier
 * Retourne : identifiant (g_free). */
static gchar *
google_calendar_id (ESource *source)
{
	gchar *id = NULL;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
		ESourceWebdav *webdav = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		GUri *uri = e_source_webdav_dup_uri (webdav);

		if (uri) {
			const gchar *path = g_uri_get_path (uri);
			const gchar *v2 = path ? strstr (path, "/v2/") : NULL;

			if (v2) {
				gchar **parts = g_strsplit (v2 + 4, "/", 2);

				if (parts[0] && *parts[0])
					id = g_uri_unescape_string (parts[0], NULL);
				g_strfreev (parts);
			}
			g_uri_unref (uri);
		}
	}

	return id ? id : g_strdup ("primary");
}

/* Texte JSON d'une réponse HTTP -> noeud racine (json_node_unref), NULL si illisible. */
static JsonNode *
gal_bytes_to_json (GBytes *bytes)
{
	gsize size = 0;
	const gchar *data = g_bytes_get_data (bytes, &size);
	gchar *text = g_strndup (data ? data : "", size);
	JsonNode *root = json_from_string (text, NULL);

	g_free (text);
	return root;
}

/* Identifiant Google d'un événement d'après son UID iCalendar ; NULL s'il n'est pas (encore) publié.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   token       : jeton d'accès
 *   calendar_id : identifiant de calendrier déjà encodé pour une adresse
 *   ical_uid    : UID de l'événement
 *   error       : emplacement pour une erreur (autre que « introuvable ») */
static gchar *
google_find_event_id (EvoCore *core,
		      const gchar *token,
		      const gchar *calendar_id,
		      const gchar *ical_uid,
		      GError **error)
{
	gchar *uid_enc = g_uri_escape_string (ical_uid, NULL, FALSE);
	gchar *uri = g_strdup_printf ("https://www.googleapis.com/calendar/v3/calendars/%s/events?iCalUID=%s&maxResults=1",
		calendar_id, uid_enc);
	gchar *event_id = NULL;
	guint status = 0;
	GBytes *bytes = gal_http_get (core, GAL_PROVIDER_GOOGLE, uri, token, &status, error);

	if (bytes) {
		if (status == 200) {
			JsonNode *root = gal_bytes_to_json (bytes);

			if (root && JSON_NODE_HOLDS_OBJECT (root)) {
				JsonArray *items = json_object_get_array_member (json_node_get_object (root), "items");

				if (items && json_array_get_length (items) > 0)
					event_id = g_strdup (json_object_get_string_member_with_default (
						json_array_get_object_element (items, 0), "id", ""));
			}
			if (root)
				json_node_unref (root);
			if (event_id && !*event_id)
				g_clear_pointer (&event_id, g_free);
		} else if (error && !*error) {
			gchar *message = gal_response_error_message (bytes);

			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Google Agenda (HTTP %u) : %s", status,
				message ? message : "erreur");
			g_free (message);
		}
		g_bytes_unref (bytes);
	}

	g_free (uid_enc);
	g_free (uri);
	return event_id;
}

/* Ajoute une réunion Google Meet à un événement d'un calendrier Gmail (API Google Agenda,
 * conferenceData.createRequest), puis actualise le calendrier.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = calendar_uid, [1] = event_uid
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"link": "..."} (vide si Google n'a pas encore fourni le lien), NULL en cas d'erreur. */
static gchar *
core_add_google_meet_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	ESource *source, *parent = NULL;
	GalProvider provider;
	gchar *token = NULL, *calendar_id, *calendar_enc, *event_id = NULL, *link = NULL, *json = NULL;
	guint attempt;

	source = e_source_registry_ref_source (core->registry, args[0]);
	if (source && e_source_get_parent (source))
		parent = e_source_registry_ref_source (core->registry, e_source_get_parent (source));
	if (!parent || !gal_collection_provider (parent, &provider) || provider != GAL_PROVIDER_GOOGLE) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Google Meet n'est disponible que pour les calendriers d'un compte Gmail.");
		g_clear_object (&source);
		g_clear_object (&parent);
		return NULL;
	}

	if (!gal_get_token (core, parent, &token, error)) {
		g_object_unref (source);
		g_object_unref (parent);
		return NULL;
	}

	calendar_id = google_calendar_id (source);
	calendar_enc = g_uri_escape_string (calendar_id, NULL, FALSE);

	/* l'événement vient d'être créé : quelques essais le temps que Google le publie */
	for (attempt = 0; attempt < 5 && !event_id && !(error && *error); attempt++) {
		if (attempt)
			g_usleep (2 * G_USEC_PER_SEC);
		event_id = google_find_event_id (core, token, calendar_enc, args[1], error);
	}

	if (event_id) {
		gchar *request_id = g_uuid_string_random ();
		gchar *body = g_strdup_printf ("{\"conferenceData\":{\"createRequest\":{\"requestId\":\"%s\","
			"\"conferenceSolutionKey\":{\"type\":\"hangoutsMeet\"}}}}", request_id);
		gchar *uri = g_strdup_printf ("https://www.googleapis.com/calendar/v3/calendars/%s/events/%s"
			"?conferenceDataVersion=1&sendUpdates=all", calendar_enc, event_id);
		guint status = 0;
		GBytes *bytes = gal_http_send_json (core, "PATCH", uri, token, body, &status, error);

		if (bytes) {
			if (status == 200) {
				JsonNode *root = gal_bytes_to_json (bytes);

				link = g_strdup ("");
				if (root && JSON_NODE_HOLDS_OBJECT (root)) {
					g_free (link);
					link = g_strdup (json_object_get_string_member_with_default (
						json_node_get_object (root), "hangoutLink", ""));
				}
				if (root)
					json_node_unref (root);
			} else {
				gchar *message = gal_response_error_message (bytes);

				g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Google Meet (HTTP %u) : %s", status,
					message ? message : "erreur");
				g_free (message);
			}
			g_bytes_unref (bytes);
		}
		g_free (uri);
		g_free (body);
		g_free (request_id);
	} else if (error && !*error) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"L'événement n'est pas encore visible chez Google : réessayez dans un instant.");
	}

	if (link) {
		/* le calendrier récupère l'événement mis à jour (lien dans la fiche) */
		EClient *client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 5,
			core->cancellable, NULL);
		JsonBuilder *builder = json_builder_new ();

		if (client) {
			e_client_refresh_sync (client, core->cancellable, NULL);
			g_object_unref (client);
		}

		json_builder_begin_object (builder);
		builder_add_string (builder, "link", link);
		json_builder_end_object (builder);
		json = json_from_builder (builder);
		g_object_unref (builder);
	}

	e_util_safe_free_string (token);
	g_free (calendar_id);
	g_free (calendar_enc);
	g_free (event_id);
	g_free (link);
	g_object_unref (source);
	g_object_unref (parent);

	return json;
}

/* Soumet l'ajout d'une réunion Google Meet (voir evo_core_add_google_meet dans evo-core.h).
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   calendar_uid : calendrier Gmail de l'événement
 *   event_uid    : identifiant de l'événement
 *   func         : rappel recevant {"link": "..."}
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_add_google_meet (EvoCore *core,
			  const char *calendar_uid,
			  const char *event_uid,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar *args[] = { (gchar *) calendar_uid, (gchar *) event_uid, NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (calendar_uid != NULL && event_uid != NULL);

	core_submit (core, core_add_google_meet_run, g_strdupv (args), func, user_data);
}

/* Racine Graph de la boîte d'un compte Microsoft 365 : « me » ou « users/<boîte partagée> ».
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : compte courrier
 * Retourne : adresse (g_free). */
static gchar *
recoverable_mailbox_base (EvoCore *core,
			  const gchar *account_uid)
{
	ESource *account = e_source_registry_ref_source (core->registry, account_uid);
	gchar *shared = NULL, *base;

	if (account && e_source_has_extension (account, e_source_camel_get_extension_name ("microsoft365"))) {
		CamelSettings *settings = e_source_camel_get_settings (
			e_source_get_extension (account, e_source_camel_get_extension_name ("microsoft365")));
		gboolean use = FALSE;

		g_object_get (settings, "use-impersonation", &use, "impersonate-user", &shared, NULL);
		if (!use)
			g_clear_pointer (&shared, g_free);
	}
	g_clear_object (&account);

	if (shared && *shared) {
		gchar *encoded = g_uri_escape_string (shared, NULL, FALSE);

		base = g_strdup_printf ("https://graph.microsoft.com/v1.0/users/%s", encoded);
		g_free (encoded);
	} else {
		base = g_strdup ("https://graph.microsoft.com/v1.0/me");
	}
	g_free (shared);

	return base;
}

/* Jeton Graph du compte Microsoft 365 (collection du compte courrier). */
static gboolean
recoverable_get_token (EvoCore *core,
		       const gchar *account_uid,
		       gchar **token,
		       GError **error)
{
	ESource *credentials = core_ref_credentials_source (core, account_uid, error);
	GalProvider provider;
	gboolean ok;

	if (!credentials)
		return FALSE;
	if (!gal_collection_provider (credentials, &provider) || provider != GAL_PROVIDER_MICROSOFT365) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"La récupération des éléments supprimés n'est disponible que pour les comptes Microsoft 365.");
		g_object_unref (credentials);
		return FALSE;
	}
	ok = gal_get_token (core, credentials, token, error);
	g_object_unref (credentials);
	return ok;
}

/* Éléments du dossier « Éléments récupérables » (suppressions) d'un compte Microsoft 365 : Microsoft Graph,
 * /mailFolders/recoverableitemsdeletions/messages.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte courrier
 *   error : emplacement pour une éventuelle erreur
 * Retourne : [{"id", "subject", "from", "to", "received", "deleted"}] (100 plus récents), NULL sinon. */
static gchar *
core_list_recoverable_run (EvoCore *core,
			   gchar **args,
			   GError **error)
{
	gchar *token = NULL, *base, *uri, *json = NULL;
	GBytes *bytes;
	guint status = 0;
	JsonBuilder *builder;

	if (!recoverable_get_token (core, args[0], &token, error))
		return NULL;

	base = recoverable_mailbox_base (core, args[0]);
	uri = g_strdup_printf ("%s/mailFolders/recoverableitemsdeletions/messages?$top=100"
		"&$select=id,subject,from,toRecipients,receivedDateTime,lastModifiedDateTime&$orderby=lastModifiedDateTime%%20desc", base);
	bytes = gal_http_get (core, GAL_PROVIDER_MICROSOFT365, uri, token, &status, error);
	g_free (uri);
	g_free (base);

	if (bytes) {
		if (status == 200) {
			JsonNode *root = gal_bytes_to_json (bytes);
			JsonArray *items = root && JSON_NODE_HOLDS_OBJECT (root)
				? json_object_get_array_member (json_node_get_object (root), "value") : NULL;
			guint ii;

			builder = json_builder_new ();
			json_builder_begin_array (builder);
			for (ii = 0; items && ii < json_array_get_length (items); ii++) {
				JsonObject *item = json_array_get_object_element (items, ii);
				JsonObject *from = json_object_has_member (item, "from") &&
					JSON_NODE_HOLDS_OBJECT (json_object_get_member (item, "from"))
					? json_object_get_object_member (item, "from") : NULL;
				JsonObject *address = from && json_object_has_member (from, "emailAddress") &&
					JSON_NODE_HOLDS_OBJECT (json_object_get_member (from, "emailAddress"))
					? json_object_get_object_member (from, "emailAddress") : NULL;
				const gchar *name = address ? json_object_get_string_member_with_default (address, "name", "") : "";
				const gchar *mail = address ? json_object_get_string_member_with_default (address, "address", "") : "";
				const gchar *received = json_object_get_string_member_with_default (item, "receivedDateTime", "");
				const gchar *modified = json_object_get_string_member_with_default (item, "lastModifiedDateTime", "");
				GDateTime *date = *received ? g_date_time_new_from_iso8601 (received, NULL) : NULL;
				GDateTime *deleted = *modified ? g_date_time_new_from_iso8601 (modified, NULL) : NULL;
				JsonArray *recipients = json_object_has_member (item, "toRecipients") &&
					JSON_NODE_HOLDS_ARRAY (json_object_get_member (item, "toRecipients"))
					? json_object_get_array_member (item, "toRecipients") : NULL;
				GString *to = g_string_new (NULL);
				guint rr;

				for (rr = 0; recipients && rr < json_array_get_length (recipients); rr++) {
					JsonObject *recipient = json_array_get_object_element (recipients, rr);
					JsonObject *to_address = recipient && json_object_has_member (recipient, "emailAddress") &&
						JSON_NODE_HOLDS_OBJECT (json_object_get_member (recipient, "emailAddress"))
						? json_object_get_object_member (recipient, "emailAddress") : NULL;
					const gchar *to_name = to_address ? json_object_get_string_member_with_default (to_address, "name", "") : "";
					const gchar *to_mail = to_address ? json_object_get_string_member_with_default (to_address, "address", "") : "";

					if (!*to_name && !*to_mail)
						continue;
					if (to->len)
						g_string_append (to, "; ");
					g_string_append (to, *to_name ? to_name : to_mail);
				}

				json_builder_begin_object (builder);
				builder_add_string (builder, "id", json_object_get_string_member_with_default (item, "id", ""));
				builder_add_string (builder, "subject", json_object_get_string_member_with_default (item, "subject", ""));
				builder_add_string (builder, "from", *name ? name : mail);
				json_builder_set_member_name (builder, "received");
				json_builder_add_int_value (builder, date ? g_date_time_to_unix (date) : 0);
				json_builder_set_member_name (builder, "deleted");
				json_builder_add_int_value (builder, deleted ? g_date_time_to_unix (deleted) : 0);
				builder_add_string (builder, "to", to->str);
				json_builder_end_object (builder);
				g_string_free (to, TRUE);
				if (date)
					g_date_time_unref (date);
				if (deleted)
					g_date_time_unref (deleted);
			}
			json_builder_end_array (builder);
			json = json_from_builder (builder);
			g_object_unref (builder);
			if (root)
				json_node_unref (root);
		} else {
			gchar *message = gal_response_error_message (bytes);

			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Microsoft Graph (HTTP %u) : %s", status,
				message ? message : "erreur");
			g_free (message);
		}
		g_bytes_unref (bytes);
	}

	e_util_safe_free_string (token);
	return json;
}

/* Restaure des éléments récupérables dans la Boîte de réception (Graph : déplacement du message).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte, [1] = identifiants Graph des messages séparés par des espaces,
 *           [2] = "restore" (Boîte de réception) ou "purge" (suppression définitive)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"restored": n, "failed": n, "error": "premier échec"}, NULL en cas d'erreur globale. */
static gchar *
core_restore_recoverable_run (EvoCore *core,
			      gchar **args,
			      GError **error)
{
	gchar *token = NULL, *base, *json;
	gchar **ids = g_strsplit (args[1] ? args[1] : "", " ", -1);
	gboolean purge = g_strcmp0 (args[2], "purge") == 0;
	gchar *first_error = NULL;
	guint restored = 0, failed = 0, ii;
	JsonBuilder *builder;

	if (!recoverable_get_token (core, args[0], &token, error)) {
		g_strfreev (ids);
		return NULL;
	}

	base = recoverable_mailbox_base (core, args[0]);
	for (ii = 0; ids[ii]; ii++) {
		gchar *encoded, *uri;
		GBytes *bytes;
		guint status = 0;

		if (!*ids[ii])
			continue;
		encoded = g_uri_escape_string (ids[ii], NULL, FALSE);
		uri = g_strdup_printf (purge ? "%s/messages/%s/permanentDelete" : "%s/messages/%s/move", base, encoded);
		bytes = gal_http_send_json (core, "POST", uri, token, purge ? "{}" : "{\"destinationId\":\"inbox\"}", &status, NULL);
		if (bytes && status >= 200 && status < 300) {
			restored++;
		} else {
			failed++;
			if (!first_error) {
				gchar *message = bytes ? gal_response_error_message (bytes) : NULL;

				first_error = g_strdup_printf ("HTTP %u : %s", status, message ? message : "erreur");
				g_free (message);
			}
		}
		if (bytes)
			g_bytes_unref (bytes);
		g_free (uri);
		g_free (encoded);
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "restored");
	json_builder_add_int_value (builder, restored);
	json_builder_set_member_name (builder, "failed");
	json_builder_add_int_value (builder, failed);
	builder_add_string (builder, "error", first_error ? first_error : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	g_free (first_error);
	g_free (base);
	g_strfreev (ids);
	e_util_safe_free_string (token);
	return json;
}

/* Soumet la lecture des éléments récupérables (voir evo_core_list_recoverable_items dans evo-core.h). */
void
evo_core_list_recoverable_items (EvoCore *core,
				 const char *account_uid,
				 EvoCoreResultFunc func,
				 void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_list_recoverable_run, g_strdupv (args), func, user_data);
}

/* Soumet la restauration d'éléments récupérables (voir evo_core_restore_recoverable_items dans evo-core.h). */
void
evo_core_restore_recoverable_items (EvoCore *core,
				    const char *account_uid,
				    const char *message_ids,
				    EvoCoreResultFunc func,
				    void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) message_ids, (gchar *) "restore", NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL && message_ids != NULL);

	core_submit (core, core_restore_recoverable_run, g_strdupv (args), func, user_data);
}

/* Soumet la suppression définitive d'éléments récupérables (voir evo_core_purge_recoverable_items dans evo-core.h). */
void
evo_core_purge_recoverable_items (EvoCore *core,
				  const char *account_uid,
				  const char *message_ids,
				  EvoCoreResultFunc func,
				  void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) message_ids, (gchar *) "purge", NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL && message_ids != NULL);

	core_submit (core, core_restore_recoverable_run, g_strdupv (args), func, user_data);
}

/* Boîte de réception prioritaire (Focused Inbox) d'un compte Microsoft 365 : identifiants des messages
 * de la Boîte de réception classés « Autres » (propriété Graph inferenceClassification = other).
 * Les identifiants Graph sont aussi les identifiants (UID) des messages du moteur.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte courrier
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"other": ["id", ...], "complete": true|false}, NULL en cas d'erreur. */
static gchar *
core_inbox_classification_run (EvoCore *core,
			       gchar **args,
			       GError **error)
{
	gchar *token = NULL, *base, *uri, *json = NULL;
	JsonBuilder *builder;
	guint page;
	gboolean complete = FALSE;

	if (!recoverable_get_token (core, args[0], &token, error))
		return NULL;

	base = recoverable_mailbox_base (core, args[0]);
	uri = g_strdup_printf ("%s/mailFolders/inbox/messages?$filter=inferenceClassification%%20eq%%20'other'&$select=id&$top=250", base);
	g_free (base);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "other");
	json_builder_begin_array (builder);

	for (page = 0; page < 4 && uri; page++) {
		GBytes *bytes;
		guint status = 0;
		gchar *next = NULL;

		bytes = gal_http_get (core, GAL_PROVIDER_MICROSOFT365, uri, token, &status, error);
		g_clear_pointer (&uri, g_free);
		if (!bytes) {
			g_object_unref (builder);
			e_util_safe_free_string (token);
			return NULL;
		}
		if (status == 200) {
			JsonNode *root = gal_bytes_to_json (bytes);

			if (root && JSON_NODE_HOLDS_OBJECT (root)) {
				JsonObject *object = json_node_get_object (root);
				JsonArray *items = json_object_get_array_member (object, "value");
				guint ii;

				for (ii = 0; items && ii < json_array_get_length (items); ii++)
					json_builder_add_string_value (builder, json_object_get_string_member_with_default (
						json_array_get_object_element (items, ii), "id", ""));
				if (json_object_has_member (object, "@odata.nextLink"))
					next = g_strdup (json_object_get_string_member_with_default (object, "@odata.nextLink", ""));
			}
			if (root)
				json_node_unref (root);
		} else {
			gchar *message = gal_response_error_message (bytes);

			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Microsoft Graph (HTTP %u) : %s", status,
				message ? message : "erreur");
			g_free (message);
			g_bytes_unref (bytes);
			g_object_unref (builder);
			e_util_safe_free_string (token);
			return NULL;
		}
		g_bytes_unref (bytes);
		if (next && *next)
			uri = next;
		else {
			g_free (next);
			complete = TRUE;
		}
	}
	g_free (uri);

	json_builder_end_array (builder);
	json_builder_set_member_name (builder, "complete");
	json_builder_add_boolean_value (builder, complete);
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);
	e_util_safe_free_string (token);

	return json;
}

/* Classe un message dans « Prioritaire » ou « Autres » et, si demandé, mémorise la règle pour
 * l'expéditeur (Graph : inferenceClassification des messages et /inferenceClassification/overrides).
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = uid du compte, [1] = identifiant du message, [2] = « focused » ou « other »,
 *           [3] = adresse de l'expéditeur pour mémoriser la règle ("" : aucune règle)
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"override_error": "..."} (vide si tout a réussi), NULL en cas d'erreur. */
static gchar *
core_set_classification_run (EvoCore *core,
			     gchar **args,
			     GError **error)
{
	gchar *token = NULL, *base, *uri, *body, *encoded, *json, *override_error = NULL;
	const gchar *classification = g_strcmp0 (args[2], "other") == 0 ? "other" : "focused";
	GBytes *bytes;
	guint status = 0;
	JsonBuilder *builder;

	if (!recoverable_get_token (core, args[0], &token, error))
		return NULL;

	base = recoverable_mailbox_base (core, args[0]);
	encoded = g_uri_escape_string (args[1], NULL, FALSE);
	uri = g_strdup_printf ("%s/messages/%s", base, encoded);
	body = g_strdup_printf ("{\"inferenceClassification\":\"%s\"}", classification);
	bytes = gal_http_send_json (core, "PATCH", uri, token, body, &status, error);
	g_free (body);
	g_free (uri);
	g_free (encoded);

	if (!bytes) {
		g_free (base);
		e_util_safe_free_string (token);
		return NULL;
	}
	if (status < 200 || status >= 300) {
		gchar *message = gal_response_error_message (bytes);

		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Microsoft Graph (HTTP %u) : %s", status,
			message ? message : "erreur");
		g_free (message);
		g_bytes_unref (bytes);
		g_free (base);
		e_util_safe_free_string (token);
		return NULL;
	}
	g_bytes_unref (bytes);

	if (args[3] && *args[3]) {
		JsonBuilder *override = json_builder_new ();
		JsonGenerator *generator;
		JsonNode *node;

		json_builder_begin_object (override);
		json_builder_set_member_name (override, "classifyAs");
		json_builder_add_string_value (override, classification);
		json_builder_set_member_name (override, "senderEmailAddress");
		json_builder_begin_object (override);
		json_builder_set_member_name (override, "address");
		json_builder_add_string_value (override, args[3]);
		json_builder_end_object (override);
		json_builder_end_object (override);
		node = json_builder_get_root (override);
		generator = json_generator_new ();
		json_generator_set_root (generator, node);
		body = json_generator_to_data (generator, NULL);
		json_node_unref (node);
		g_object_unref (generator);
		g_object_unref (override);

		uri = g_strdup_printf ("%s/inferenceClassification/overrides", base);
		bytes = gal_http_send_json (core, "POST", uri, token, body, &status, NULL);
		if (!bytes || status < 200 || status >= 300) {
			gchar *message = bytes ? gal_response_error_message (bytes) : NULL;

			override_error = g_strdup_printf ("HTTP %u : %s", status, message ? message : "erreur");
			g_free (message);
		}
		if (bytes)
			g_bytes_unref (bytes);
		g_free (uri);
		g_free (body);
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "override_error", override_error ? override_error : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	g_free (override_error);
	g_free (base);
	e_util_safe_free_string (token);
	return json;
}

/* Soumet la lecture des messages « Autres » (voir evo_core_list_inbox_classification dans evo-core.h). */
void
evo_core_list_inbox_classification (EvoCore *core,
				    const char *account_uid,
				    EvoCoreResultFunc func,
				    void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL);

	core_submit (core, core_inbox_classification_run, g_strdupv (args), func, user_data);
}

/* Soumet le classement d'un message (voir evo_core_set_message_classification dans evo-core.h). */
void
evo_core_set_message_classification (EvoCore *core,
				     const char *account_uid,
				     const char *message_id,
				     const char *classification,
				     const char *sender_address,
				     EvoCoreResultFunc func,
				     void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) message_id, (gchar *) (classification ? classification : "focused"),
		(gchar *) (sender_address ? sender_address : ""), NULL };

	g_return_if_fail (core != NULL);
	g_return_if_fail (account_uid != NULL && message_id != NULL);

	core_submit (core, core_set_classification_run, g_strdupv (args), func, user_data);
}

/* --- état d'une invitation et événements du jour (Microsoft Graph) --- */

/* Date ISO 8601 d'une réponse (« 2026-11-11T08:16:09.0000000Z ») en secondes Unix ; 0 si absente ou
 * invalide (Exchange renvoie l'an 1 pour « pas de réponse »). */
static gint64
gal_iso_to_unix (const gchar *text)
{
	gchar buffer[32];
	GTimeZone *utc;
	GDateTime *date;
	gint64 result = 0;

	if (!text || strlen (text) < 19)
		return 0;

	g_strlcpy (buffer, text, 20);
	g_strlcat (buffer, "Z", sizeof (buffer));

	utc = g_time_zone_new_utc ();
	date = g_date_time_new_from_iso8601 (buffer, utc);
	if (date) {
		result = g_date_time_to_unix (date);
		g_date_time_unref (date);
	}
	g_time_zone_unref (utc);

	return result > 631152000 ? result : 0;	/* avant 1990 : « pas de date » */
}

/* Texte d'un membre de type objet (NULL si absent). */
static JsonObject *
gal_object_member (JsonObject *object,
		   const gchar *name)
{
	return object && json_object_has_member (object, name) &&
		JSON_NODE_HOLDS_OBJECT (json_object_get_member (object, name)) ?
		json_object_get_object_member (object, name) : NULL;
}

/* État d'une invitation de réunion dans l'agenda Microsoft 365 du compte, par Microsoft Graph : réponse
 * donnée (et sa date), et événements de l'agenda autour de la réunion (mini-grille de la journée).
 * Bien plus rapide que d'ouvrir les agendas EDS (plus d'une minute pour un agenda Microsoft 365).
 * Paramètres :
 *   core        : moteur
 *   account_uid : compte qui a reçu le message
 *   event_uid   : UID iCalendar de l'événement (iCalUId)
 *   out_found   : reçoit TRUE si l'événement est dans l'agenda
 * Retourne : {"found", "my_status", "response_time", "sequence", "start", "end", "events": [{"subject",
 *   "start", "end", "all_day", "show_as", "self"}]} (g_free), NULL si le compte n'est pas un compte
 *   Microsoft 365 ou si Graph est injoignable (l'appelant se rabat alors sur les agendas EDS). */
gchar *
core_graph_invitation_event (EvoCore *core,
			     const gchar *account_uid,
			     const gchar *event_uid,
			     gboolean *out_found)
{
	ESource *source;
	GalProvider provider;
	gchar *token = NULL, *escaped, *uri, *json = NULL;
	GBytes *bytes;
	guint status = 0;
	JsonNode *root = NULL;
	JsonObject *object, *self_event = NULL;
	JsonBuilder *builder;
	const gchar *prefer = "outlook.timezone=\"UTC\"";
	const gchar *my_status = "needs-action";
	gint64 response_time = 0, start = 0, end = 0;
	gboolean found = FALSE;

	*out_found = FALSE;

	if (!event_uid || !*event_uid)
		return NULL;

	source = core_ref_credentials_source (core, account_uid, NULL);
	if (!source)
		return NULL;
	if (!gal_collection_provider (source, &provider) || provider != GAL_PROVIDER_MICROSOFT365 ||
	    !gal_get_token (core, source, &token, NULL)) {
		g_object_unref (source);
		return NULL;
	}
	g_object_unref (source);

	/* 1. l'événement lui-même : réponse donnée et sa date */
	{
		gchar *quoted = g_strdup (event_uid), *cursor;
		GString *doubled = g_string_new (NULL);

		for (cursor = quoted; *cursor; cursor++) {
			if (*cursor == '\'')
				g_string_append_c (doubled, '\'');
			g_string_append_c (doubled, *cursor);
		}
		escaped = g_uri_escape_string (doubled->str, NULL, TRUE);
		g_string_free (doubled, TRUE);
		g_free (quoted);
	}
	uri = g_strconcat ("https://graph.microsoft.com/v1.0/me/events?$filter=iCalUId%20eq%20%27", escaped,
		"%27&$select=id,subject,start,end,isAllDay,showAs,responseStatus,isCancelled,iCalUId&$top=5", NULL);
	g_free (escaped);

	bytes = gal_http_get_prefer (core, GAL_PROVIDER_MICROSOFT365, uri, token, prefer, &status, NULL);
	g_free (uri);
	if (!bytes || status < 200 || status >= 300) {
		g_message ("invitation Graph : HTTP %u pour l'événement", status);
		g_clear_pointer (&bytes, g_bytes_unref);
		e_util_safe_free_string (token);
		return NULL;
	}

	object = gal_parse_response (bytes, &root, NULL);
	g_bytes_unref (bytes);
	if (object && json_object_has_member (object, "value") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (object, "value"))) {
		JsonArray *array = json_object_get_array_member (object, "value");

		if (json_array_get_length (array) > 0 && JSON_NODE_HOLDS_OBJECT (json_array_get_element (array, 0))) {
			JsonObject *event = json_array_get_object_element (array, 0);
			JsonObject *rs = gal_object_member (event, "responseStatus");
			const gchar *response = rs ? gal_string_member (rs, "response") : "";

			found = !json_object_get_boolean_member_with_default (event, "isCancelled", FALSE);
			if (g_strcmp0 (response, "accepted") == 0)
				my_status = "accepted";
			else if (g_strcmp0 (response, "tentativelyAccepted") == 0)
				my_status = "tentative";
			else if (g_strcmp0 (response, "declined") == 0)
				my_status = "declined";
			response_time = rs ? gal_iso_to_unix (gal_string_member (rs, "time")) : 0;
			start = gal_graph_time (event, "start");
			end = gal_graph_time (event, "end");
			self_event = event;
			(void) self_event;
		}
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_boolean (builder, "found", found);
	builder_add_string (builder, "my_status", my_status);
	builder_add_int (builder, "response_time", response_time);
	builder_add_int (builder, "sequence", 0);
	builder_add_int (builder, "start", start);
	builder_add_int (builder, "end", end);

	/* 2. les événements de l'agenda autour de la réunion (mini-grille) */
	json_builder_set_member_name (builder, "events");
	json_builder_begin_array (builder);
	if (found && start > 0 && end > start) {
		GDateTime *from = g_date_time_new_from_unix_utc (start - 3 * 3600);
		GDateTime *to = g_date_time_new_from_unix_utc (end + 3 * 3600);
		gchar *from_iso = from ? g_date_time_format (from, "%Y-%m-%dT%H:%M:%SZ") : NULL;
		gchar *to_iso = to ? g_date_time_format (to, "%Y-%m-%dT%H:%M:%SZ") : NULL;

		if (from_iso && to_iso) {
			JsonNode *root2 = NULL;
			JsonObject *window;

			uri = g_strconcat ("https://graph.microsoft.com/v1.0/me/calendarView?startDateTime=", from_iso,
				"&endDateTime=", to_iso, "&$select=subject,start,end,isAllDay,showAs,isCancelled,iCalUId&$orderby=start/dateTime&$top=50", NULL);
			bytes = gal_http_get_prefer (core, GAL_PROVIDER_MICROSOFT365, uri, token, prefer, &status, NULL);
			g_free (uri);

			window = bytes && status >= 200 && status < 300 ? gal_parse_response (bytes, &root2, NULL) : NULL;
			if (window && json_object_has_member (window, "value") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (window, "value"))) {
				JsonArray *array = json_object_get_array_member (window, "value");
				guint ii;

				for (ii = 0; ii < json_array_get_length (array); ii++) {
					JsonObject *item;

					if (!JSON_NODE_HOLDS_OBJECT (json_array_get_element (array, ii)))
						continue;
					item = json_array_get_object_element (array, ii);
					if (json_object_get_boolean_member_with_default (item, "isCancelled", FALSE))
						continue;

					json_builder_begin_object (builder);
					builder_add_string (builder, "subject", gal_string_member (item, "subject"));
					builder_add_int (builder, "start", gal_graph_time (item, "start"));
					builder_add_int (builder, "end", gal_graph_time (item, "end"));
					builder_add_boolean (builder, "all_day", json_object_get_boolean_member_with_default (item, "isAllDay", FALSE));
					builder_add_string (builder, "show_as", gal_string_member (item, "showAs"));
					builder_add_boolean (builder, "self", g_strcmp0 (gal_string_member (item, "iCalUId"), event_uid) == 0);
					json_builder_end_object (builder);
				}
			}
			if (root2)
				json_node_unref (root2);
			g_clear_pointer (&bytes, g_bytes_unref);
		}
		g_free (from_iso);
		g_free (to_iso);
		if (from)
			g_date_time_unref (from);
		if (to)
			g_date_time_unref (to);
	}
	json_builder_end_array (builder);
	json_builder_end_object (builder);

	json = json_from_builder (builder);
	g_object_unref (builder);
	if (root)
		json_node_unref (root);
	e_util_safe_free_string (token);

	*out_found = found;

	return json;
}
