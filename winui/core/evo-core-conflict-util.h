/*
 * evo-core : logique pure (sans GLib) de la détection de conflits de modification
 * des événements d'agenda et des contacts : jeton de révision (ETag, LAST-MODIFIED,
 * SEQUENCE, empreinte du contenu), comparaison de révisions, politique de résolution
 * et liste des champs en désaccord. Fichier d'en-tête autonome, inclus par
 * evo-core-conflicts.c et par les tests unitaires (tests/unit/test_offline_queue.c).
 */

#ifndef EVO_CORE_CONFLICT_UTIL_H
#define EVO_CORE_CONFLICT_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Résultat de cf_detect () */
#define CF_NO_CONFLICT		0	/* la version distante n'a pas changé (ou aucune base connue) */
#define CF_REMOTE_CHANGED	1	/* la version distante a été modifiée depuis le chargement */

/* Résolutions possibles */
#define CF_RESOLVE_NONE		0	/* aucune : détecter le conflit seulement */
#define CF_RESOLVE_LOCAL	1	/* garder la version locale (écrase la distante) */
#define CF_RESOLVE_REMOTE	2	/* garder la version distante (abandonne les modifications locales) */
#define CF_RESOLVE_BOTH		3	/* garder les deux (la version locale devient un nouvel élément) */

#define CF_REVISION_SIZE	192
#define CF_FIELD_SIZE		512
#define CF_MAX_FIELDS		32

/**
 * @brief Champ d'un élément (nom et valeur texte) pour la comparaison local / distant.
 */
typedef struct {
	char name[48];
	char value[CF_FIELD_SIZE];
} CfField;

/**
 * @brief Empreinte FNV-1a 64 bits d'un texte (contenu iCalendar / vCard complet).
 * @param text texte à hacher (NULL : chaîne vide)
 * @return empreinte
 */
static inline uint64_t
cf_hash_string (const char *text)
{
	uint64_t hash = 1469598103934665603ULL;

	for (; text && *text; text++) {
		hash ^= (unsigned char) *text;
		hash *= 1099511628211ULL;
	}

	return hash;
}

/**
 * @brief Compose le jeton de révision d'un élément : « e=<etag>;m=<modifié>;s=<séquence>;h=<empreinte> ».
 * @param out tampon de sortie
 * @param size taille du tampon
 * @param etag ETag du serveur (CalDAV / CardDAV / EWS), peut être NULL
 * @param modified date de dernière modification (LAST-MODIFIED, REV), peut être NULL
 * @param sequence numéro de séquence (SEQUENCE), -1 si inconnu
 * @param content texte complet de l'élément pour l'empreinte, peut être NULL
 */
static inline void
cf_revision_make (char *out,
		  size_t size,
		  const char *etag,
		  const char *modified,
		  int sequence,
		  const char *content)
{
	char seq[16] = "";

	if (sequence >= 0)
		snprintf (seq, sizeof (seq), "%d", sequence);

	snprintf (out, size, "e=%s;m=%s;s=%s;h=%016llx",
		etag ? etag : "", modified ? modified : "", seq,
		(unsigned long long) cf_hash_string (content));
}

/**
 * @brief Extrait un composant (« e », « m », « s » ou « h ») d'un jeton de révision.
 * @param revision jeton
 * @param key lettre du composant
 * @param out tampon de sortie
 * @param size taille du tampon
 * @return 1 si le composant est présent et non vide
 */
static inline int
cf_revision_part (const char *revision,
		  char key,
		  char *out,
		  size_t size)
{
	const char *p = revision;

	out[0] = '\0';
	while (p && *p) {
		const char *end = strchr (p, ';');
		size_t len = end ? (size_t) (end - p) : strlen (p);

		if (len >= 2 && p[0] == key && p[1] == '=') {
			len -= 2;
			if (len >= size)
				len = size - 1;
			memcpy (out, p + 2, len);
			out[len] = '\0';
			return len > 0;
		}
		if (!end)
			break;
		p = end + 1;
	}

	return 0;
}

/**
 * @brief Détecte une modification concurrente en comparant la révision chargée à la révision actuelle.
 *
 * Sans révision de base (élément ouvert avant l'existence du mécanisme, nouveau contact...),
 * aucun conflit n'est signalé. Si les deux révisions portent un ETag, seul l'ETag décide
 * (le serveur fait foi) ; sinon la date de modification, la séquence et l'empreinte du
 * contenu sont comparées (celles présentes des deux côtés).
 * @param base révision au moment du chargement dans l'éditeur (NULL ou vide : inconnue)
 * @param current révision actuelle de l'élément dans le cache / sur le serveur
 * @return CF_NO_CONFLICT ou CF_REMOTE_CHANGED
 */
static inline int
cf_detect (const char *base,
	   const char *current)
{
	static const char keys[] = { 'm', 's', 'h' };
	char a[CF_REVISION_SIZE], b[CF_REVISION_SIZE];
	int ii;

	if (!base || !*base || !current || !*current)
		return CF_NO_CONFLICT;

	if (cf_revision_part (base, 'e', a, sizeof (a)) && cf_revision_part (current, 'e', b, sizeof (b)))
		return strcmp (a, b) != 0 ? CF_REMOTE_CHANGED : CF_NO_CONFLICT;

	for (ii = 0; ii < 3; ii++) {
		if (cf_revision_part (base, keys[ii], a, sizeof (a)) && cf_revision_part (current, keys[ii], b, sizeof (b)) &&
		    strcmp (a, b) != 0)
			return CF_REMOTE_CHANGED;
	}

	return CF_NO_CONFLICT;
}

/**
 * @brief Résolution d'après son nom JSON.
 * @param name "local", "remote" ou "both"
 * @return CF_RESOLVE_* (CF_RESOLVE_NONE si inconnue ou vide)
 */
static inline int
cf_resolution_from_name (const char *name)
{
	if (!name)
		return CF_RESOLVE_NONE;
	if (strcmp (name, "local") == 0) return CF_RESOLVE_LOCAL;
	if (strcmp (name, "remote") == 0) return CF_RESOLVE_REMOTE;
	if (strcmp (name, "both") == 0) return CF_RESOLVE_BOTH;
	return CF_RESOLVE_NONE;
}

/**
 * @brief Décide si une écriture doit être bloquée par un conflit.
 *
 * Une résolution explicite (locale, distante ou les deux) lève le blocage : l'utilisateur
 * a déjà arbitré. Sans résolution, un changement distant bloque l'écriture.
 * @param base révision de base
 * @param current révision actuelle
 * @param resolution CF_RESOLVE_*
 * @return 1 si l'écriture doit être bloquée et le conflit remonté à l'utilisateur
 */
static inline int
cf_must_block (const char *base,
	       const char *current,
	       int resolution)
{
	if (resolution != CF_RESOLVE_NONE)
		return 0;
	return cf_detect (base, current) == CF_REMOTE_CHANGED;
}

/**
 * @brief Cherche la valeur d'un champ par son nom.
 * @param fields tableau de champs
 * @param count nombre de champs
 * @param name nom recherché
 * @return valeur, NULL si absente
 */
static inline const char *
cf_field_value (const CfField *fields,
		int count,
		const char *name)
{
	int ii;

	for (ii = 0; ii < count; ii++) {
		if (strcmp (fields[ii].name, name) == 0)
			return fields[ii].value;
	}

	return NULL;
}

/**
 * @brief Liste les champs dont la valeur diffère entre la version locale et la version distante.
 *
 * Seuls les champs présents dans la version locale (ceux que l'utilisateur modifie)
 * sont comparés ; un champ absent de la version distante vaut la chaîne vide.
 * @param local champs de la version locale
 * @param local_count nombre de champs locaux
 * @param remote champs de la version distante
 * @param remote_count nombre de champs distants
 * @param out_names tableau recevant les indices des champs locaux différents
 * @param out_max capacité de out_names
 * @return nombre de champs différents
 */
static inline int
cf_diff_fields (const CfField *local,
		int local_count,
		const CfField *remote,
		int remote_count,
		int *out_names,
		int out_max)
{
	int ii, found = 0;

	for (ii = 0; ii < local_count; ii++) {
		const char *other = cf_field_value (remote, remote_count, local[ii].name);

		if (strcmp (local[ii].value, other ? other : "") != 0) {
			if (found < out_max)
				out_names[found] = ii;
			found++;
		}
	}

	return found < out_max ? found : out_max;
}

#endif /* EVO_CORE_CONFLICT_UTIL_H */
