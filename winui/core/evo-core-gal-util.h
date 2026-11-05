/*
 * evo-core : fonctions pures (sans GLib) de la recherche dans l'annuaire de
 * l'organisation (GAL) : nettoyage de la saisie, encodage d'URL, construction
 * des adresses des API Microsoft Graph et Google People, normalisation des
 * adresses électroniques. Fichier d'en-tête autonome, inclus par
 * evo-core-gal.c et par les tests unitaires (tests/unit).
 */

#ifndef EVO_CORE_GAL_UTIL_H
#define EVO_CORE_GAL_UTIL_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Nombre minimal de caractères d'une recherche dans l'annuaire */
#define GAL_MIN_QUERY_LENGTH	2

/* Nombre maximal de résultats demandés au serveur */
#define GAL_MAX_RESULTS		50

/* Adresses des API (sans barre finale) */
#define GAL_GRAPH_USERS_URI	"https://graph.microsoft.com/v1.0/users"
#define GAL_GOOGLE_PEOPLE_URI	"https://people.googleapis.com/v1/people:searchDirectoryPeople"

/* Champs Graph demandés : l'ensemble complet (portée User.Read.All) puis le
 * jeu de base seul (portée User.ReadBasic.All, sans consentement administrateur). */
#define GAL_GRAPH_SELECT_FULL	"displayName,givenName,surname,mail,userPrincipalName,jobTitle,department,companyName,officeLocation,mobilePhone,businessPhones"
#define GAL_GRAPH_SELECT_BASIC	"displayName,givenName,surname,mail,userPrincipalName"

/**
 * @brief Nettoie une saisie avant son insertion dans une requête de recherche :
 *        les guillemets, antislashs et caractères de contrôle sont remplacés par
 *        des espaces, les espaces multiples réduits et les extrémités rognées.
 * @param in saisie brute (UTF-8, peut être NULL)
 * @param out tampon recevant la saisie nettoyée (toujours terminé par un zéro)
 * @param cap taille du tampon en octets (au moins 1)
 * @return longueur en octets du texte produit
 */
static inline size_t
gal_sanitize_term (const char *in,
		   char *out,
		   size_t cap)
{
	size_t len = 0;
	int pending_space = 0;

	if (cap == 0)
		return 0;
	out[0] = '\0';
	if (!in)
		return 0;

	for (; *in && len + 1 < cap; in++) {
		unsigned char c = (unsigned char) *in;

		if (c == '"' || c == '\\' || c < 0x20 || c == 0x7f || c == ' ') {
			pending_space = len > 0;
			continue;
		}
		if (pending_space) {
			if (len + 2 >= cap)
				break;
			out[len++] = ' ';
			pending_space = 0;
		}
		out[len++] = (char) c;
	}
	out[len] = '\0';

	return len;
}

/**
 * @brief Nombre de caractères (points de code) d'une chaîne UTF-8.
 * @param text chaîne UTF-8 (peut être NULL)
 * @return nombre de caractères
 */
static inline size_t
gal_utf8_length (const char *text)
{
	size_t count = 0;

	for (; text && *text; text++)
		if ((((unsigned char) *text) & 0xC0) != 0x80)
			count++;

	return count;
}

/**
 * @brief Indique si une saisie nettoyée est assez longue pour interroger l'annuaire.
 * @param term saisie déjà nettoyée par gal_sanitize_term ()
 * @return 1 si la recherche est permise, 0 sinon
 */
static inline int
gal_query_valid (const char *term)
{
	return gal_utf8_length (term) >= GAL_MIN_QUERY_LENGTH;
}

/**
 * @brief Encode un texte en pourcentage (RFC 3986) pour l'insérer dans une URL.
 * @param in texte UTF-8 (peut être NULL)
 * @param out tampon recevant le texte encodé (toujours terminé par un zéro)
 * @param cap taille du tampon en octets (au moins 1)
 * @return longueur du texte produit ; s'il est tronqué faute de place, le
 *         résultat est vide (jamais d'encodage partiel)
 */
static inline size_t
gal_url_encode (const char *in,
		char *out,
		size_t cap)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t len = 0;

	if (cap == 0)
		return 0;
	out[0] = '\0';
	if (!in)
		return 0;

	for (; *in; in++) {
		unsigned char c = (unsigned char) *in;

		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '.' || c == '_' || c == '~') {
			if (len + 1 >= cap)
				goto overflow;
			out[len++] = (char) c;
		} else {
			if (len + 3 >= cap)
				goto overflow;
			out[len++] = '%';
			out[len++] = hex[c >> 4];
			out[len++] = hex[c & 15];
		}
	}
	out[len] = '\0';
	return len;

 overflow:
	out[0] = '\0';
	return 0;
}

/**
 * @brief Borne le nombre de résultats demandés (1 à GAL_MAX_RESULTS, 15 par défaut).
 * @param limit valeur demandée
 * @return valeur utilisable
 */
static inline int
gal_clamp_limit (int limit)
{
	if (limit <= 0)
		return 15;
	return limit > GAL_MAX_RESULTS ? GAL_MAX_RESULTS : limit;
}

/**
 * @brief Construit l'adresse de la recherche d'utilisateurs Microsoft Graph :
 *        GET /users?$search="displayName:x" OR "mail:x"&$top=n&$select=...
 *        (requiert l'en-tête ConsistencyLevel: eventual).
 * @param term saisie nettoyée par gal_sanitize_term ()
 * @param limit nombre maximal de résultats
 * @param select liste des champs demandés (GAL_GRAPH_SELECT_FULL ou _BASIC)
 * @param out tampon recevant l'adresse
 * @param cap taille du tampon
 * @return 1 en cas de succès, 0 si la saisie est invalide ou le tampon trop petit
 */
static inline int
gal_graph_search_url (const char *term,
		      int limit,
		      const char *select,
		      char *out,
		      size_t cap)
{
	char expression[320], encoded[1024];
	int written;

	if (!term || !gal_query_valid (term) || !select)
		return 0;

	written = snprintf (expression, sizeof (expression),
		"\"displayName:%s\" OR \"mail:%s\"", term, term);
	if (written < 0 || (size_t) written >= sizeof (expression))
		return 0;
	if (gal_url_encode (expression, encoded, sizeof (encoded)) == 0)
		return 0;

	written = snprintf (out, cap, "%s?$search=%s&$top=%d&$select=%s",
		GAL_GRAPH_USERS_URI, encoded, gal_clamp_limit (limit), select);
	return written > 0 && (size_t) written < cap;
}

/**
 * @brief Construit l'adresse de la recherche dans l'annuaire Google (API People
 *        people:searchDirectoryPeople, domaine Workspace de l'utilisateur).
 * @param term saisie nettoyée par gal_sanitize_term ()
 * @param limit nombre maximal de résultats
 * @param out tampon recevant l'adresse
 * @param cap taille du tampon
 * @return 1 en cas de succès, 0 si la saisie est invalide ou le tampon trop petit
 */
static inline int
gal_google_search_url (const char *term,
		       int limit,
		       char *out,
		       size_t cap)
{
	char encoded[512];
	int written;

	if (!term || !gal_query_valid (term))
		return 0;
	if (gal_url_encode (term, encoded, sizeof (encoded)) == 0)
		return 0;

	written = snprintf (out, cap,
		"%s?query=%s&pageSize=%d&readMask=names,emailAddresses,organizations,phoneNumbers,locations"
		"&sources=DIRECTORY_SOURCE_TYPE_DOMAIN_PROFILE&sources=DIRECTORY_SOURCE_TYPE_DOMAIN_CONTACT",
		GAL_GOOGLE_PEOPLE_URI, encoded, gal_clamp_limit (limit));
	return written > 0 && (size_t) written < cap;
}

/**
 * @brief Normalise une adresse électronique d'annuaire : espaces rognés, préfixe
 *        « SMTP: » (adresses proxy Exchange) retiré, ASCII mis en minuscules.
 * @param in adresse brute (peut être NULL)
 * @param out tampon recevant l'adresse normalisée
 * @param cap taille du tampon (au moins 1)
 * @return 1 si l'adresse obtenue est plausible (un seul « @ », parties non vides,
 *         sans espace), 0 sinon (le tampon contient alors le texte normalisé)
 */
static inline int
gal_normalize_email (const char *in,
		     char *out,
		     size_t cap)
{
	size_t len = 0, at_count = 0, at_pos = 0, ii;

	if (cap == 0)
		return 0;
	out[0] = '\0';
	if (!in)
		return 0;

	while (*in == ' ' || *in == '\t')
		in++;
	if ((in[0] == 'S' || in[0] == 's') && (in[1] == 'M' || in[1] == 'm') &&
	    (in[2] == 'T' || in[2] == 't') && (in[3] == 'P' || in[3] == 'p') && in[4] == ':')
		in += 5;

	for (; *in && len + 1 < cap; in++) {
		char c = *in;

		if (c >= 'A' && c <= 'Z')
			c = (char) (c - 'A' + 'a');
		out[len++] = c;
	}
	while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t'))
		len--;
	out[len] = '\0';

	for (ii = 0; ii < len; ii++) {
		if (out[ii] == ' ' || out[ii] == '\t')
			return 0;
		if (out[ii] == '@') {
			at_count++;
			at_pos = ii;
		}
	}

	return at_count == 1 && at_pos > 0 && at_pos + 1 < len;
}

#endif /* EVO_CORE_GAL_UTIL_H */
