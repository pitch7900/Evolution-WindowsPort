/*
 * evo-core : index plein texte local, logique pure (sans GLib ni Camel).
 *
 * Index inversé en mémoire pour la recherche instantanée dans le corps des
 * messages et le contenu des pièces jointes texte :
 *   - tokenisation UTF-8 insensible à la casse et aux accents (français et
 *     langues latines, grec et cyrillique en minuscules, idéogrammes un par un) ;
 *   - documents identifiés par une clé texte (l'UID du message) ;
 *   - requête : tous les mots sont requis (ET), chacun comme préfixe de terme ;
 *   - sérialisation binaire pour la persistance sur disque.
 *
 * Fichier d'en-tête autonome, inclus par evo-core-index.c et par les tests
 * unitaires (tests/unit/test_fulltext_index.c). Aucune synchronisation : le
 * code appelant protège l'accès concurrent à un même index.
 */

#ifndef EVO_CORE_INDEX_UTIL_H
#define EVO_CORE_INDEX_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Longueur minimale (en caractères) d'un mot indexé ou recherché (hors idéogrammes) */
#define EVOIDX_MIN_TOKEN_CHARS	2

/* Longueur maximale (en caractères) d'un mot indexé : au-delà, c'est du bruit
 * (empreintes, données encodées en base 64) qui gonflerait l'index. */
#define EVOIDX_MAX_TOKEN_CHARS	40

/* Version du format de sérialisation */
#define EVOIDX_FORMAT_VERSION	1

/* Rappel appelé pour chaque mot produit par la tokenisation ; retourne 0 pour continuer. */
typedef int (* EvoIdxTokenFunc) (const char *token, size_t length, void *user_data);

/* ------------------------------------------------------------------------ */
/* Tokenisation                                                              */
/* ------------------------------------------------------------------------ */

/**
 * @brief Décode un caractère UTF-8.
 * @param s début du texte
 * @param end fin du texte (exclue)
 * @param cp reçoit le point de code (0xFFFD si la séquence est invalide)
 * @return nombre d'octets consommés (au moins 1)
 */
static inline size_t
evoidx_utf8_decode (const unsigned char *s,
		    const unsigned char *end,
		    uint32_t *cp)
{
	unsigned char c = *s;
	size_t need, ii;
	uint32_t value;

	if (c < 0x80) {
		*cp = c;
		return 1;
	}
	if (c >= 0xC2 && c <= 0xDF) {
		need = 1;
		value = c & 0x1F;
	} else if (c >= 0xE0 && c <= 0xEF) {
		need = 2;
		value = c & 0x0F;
	} else if (c >= 0xF0 && c <= 0xF4) {
		need = 3;
		value = c & 0x07;
	} else {
		*cp = 0xFFFD;
		return 1;
	}
	if ((size_t) (end - s) <= need) {
		*cp = 0xFFFD;
		return 1;
	}
	for (ii = 1; ii <= need; ii++) {
		if ((s[ii] & 0xC0) != 0x80) {
			*cp = 0xFFFD;
			return 1;
		}
		value = (value << 6) | (s[ii] & 0x3F);
	}
	*cp = value;
	return need + 1;
}

/**
 * @brief Encode un point de code en UTF-8.
 * @param cp point de code
 * @param out tampon d'au moins 4 octets
 * @return nombre d'octets écrits
 */
static inline size_t
evoidx_utf8_encode (uint32_t cp,
		    unsigned char *out)
{
	if (cp < 0x80) {
		out[0] = (unsigned char) cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (unsigned char) (0xC0 | (cp >> 6));
		out[1] = (unsigned char) (0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (unsigned char) (0xE0 | (cp >> 12));
		out[1] = (unsigned char) (0x80 | ((cp >> 6) & 0x3F));
		out[2] = (unsigned char) (0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (unsigned char) (0xF0 | (cp >> 18));
	out[1] = (unsigned char) (0x80 | ((cp >> 12) & 0x3F));
	out[2] = (unsigned char) (0x80 | ((cp >> 6) & 0x3F));
	out[3] = (unsigned char) (0x80 | (cp & 0x3F));
	return 4;
}

/**
 * @brief Un point de code est-il un idéogramme (chaque caractère forme alors un mot) ?
 * @param cp point de code
 * @return 1 pour les kana, hangul et idéogrammes CJK
 */
static inline int
evoidx_is_ideograph (uint32_t cp)
{
	return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
		(cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
		(cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FFFF);
}

/**
 * @brief Plie un point de code : minuscule sans accent.
 * @param cp point de code
 * @param out reçoit un ou deux points de code (ligatures : ß, æ, œ, þ)
 * @return nombre de points de code produits : 0 pour un séparateur, 1 ou 2 pour une lettre ou un chiffre
 */
static inline int
evoidx_fold (uint32_t cp,
	     uint32_t out[2])
{
	/* Latin-1 supplément (U+00C0..U+00FF) : \0 séparateur, \1 « ae », \2 « ss », \3 « th » */
	static const char latin1[64] = {
		'a','a','a','a','a','a','\1','c', 'e','e','e','e','i','i','i','i',
		'd','n','o','o','o','o','o','\0', 'o','u','u','u','u','y','\3','\2',
		'a','a','a','a','a','a','\1','c', 'e','e','e','e','i','i','i','i',
		'd','n','o','o','o','o','o','\0', 'o','u','u','u','u','y','\3','y'
	};
	/* Latin étendu A (U+0100..U+017F) */
	static const char latin_a[129] =
		"aaaaaaccccccccdd"
		"ddeeeeeeeeeegggg"
		"gggghhhhiiiiiiii"
		"iiiijjkkklllllll"
		"lllnnnnnnnnnoooo"
		"oooorrrrrrssssss"
		"sstttttt" "uuuuuuuu"
		"uuuuwwyyyzzzzzzs";

	out[1] = 0;
	if (cp < 0x80) {
		if (cp >= 'a' && cp <= 'z') {
			out[0] = cp;
			return 1;
		}
		if (cp >= 'A' && cp <= 'Z') {
			out[0] = cp + 32;
			return 1;
		}
		if (cp >= '0' && cp <= '9') {
			out[0] = cp;
			return 1;
		}
		return 0;
	}
	if (cp >= 0xC0 && cp <= 0xFF) {
		char c = latin1[cp - 0xC0];

		if (c == 0)
			return 0;
		if (c == 1) {
			out[0] = 'a';
			out[1] = 'e';
			return 2;
		}
		if (c == 2) {
			out[0] = 's';
			out[1] = 's';
			return 2;
		}
		if (c == 3) {
			out[0] = 't';
			out[1] = 'h';
			return 2;
		}
		out[0] = (uint32_t) c;
		return 1;
	}
	if (cp == 0x152 || cp == 0x153) {	/* œ, Œ */
		out[0] = 'o';
		out[1] = 'e';
		return 2;
	}
	if (cp == 0x132 || cp == 0x133) {	/* ĳ, Ĳ */
		out[0] = 'i';
		out[1] = 'j';
		return 2;
	}
	if (cp >= 0x100 && cp <= 0x17F) {
		out[0] = (uint32_t) latin_a[cp - 0x100];
		return 1;
	}
	if (cp < 0xC0)	/* ponctuation et symboles Latin-1 (sauf lettres ci-dessus) */
		return 0;
	if (cp >= 0x2000 && cp <= 0x206F)	/* ponctuation générale */
		return 0;
	if (cp >= 0x20A0 && cp <= 0x2BFF)	/* monnaies, flèches, symboles, formes */
		return 0;
	if (cp >= 0x3000 && cp <= 0x303F)	/* ponctuation CJK */
		return 0;
	if (cp >= 0xFF00 && cp <= 0xFF0F)
		return 0;
	if (cp == 0xFFFD || cp == 0xFEFF || (cp >= 0xE000 && cp <= 0xF8FF))
		return 0;
	if (cp >= 0x1F000 && cp <= 0x1FAFF)	/* émojis */
		return 0;
	/* grec */
	if (cp >= 0x391 && cp <= 0x3A9) {
		out[0] = cp + 0x20;
		return 1;
	}
	if (cp == 0x3AC || cp == 0x386) {
		out[0] = 0x3B1;
		return 1;
	}
	if (cp == 0x3AD || cp == 0x388) {
		out[0] = 0x3B5;
		return 1;
	}
	if (cp == 0x3AE || cp == 0x389) {
		out[0] = 0x3B7;
		return 1;
	}
	if (cp == 0x3AF || cp == 0x38A) {
		out[0] = 0x3B9;
		return 1;
	}
	if (cp == 0x3CC || cp == 0x38C) {
		out[0] = 0x3BF;
		return 1;
	}
	if (cp == 0x3CD || cp == 0x38E) {
		out[0] = 0x3C5;
		return 1;
	}
	if (cp == 0x3CE || cp == 0x38F) {
		out[0] = 0x3C9;
		return 1;
	}
	if (cp == 0x3C2) {
		out[0] = 0x3C3;
		return 1;
	}
	/* cyrillique */
	if (cp >= 0x410 && cp <= 0x42F) {
		out[0] = cp + 0x20;
		return 1;
	}
	if (cp >= 0x400 && cp <= 0x40F) {
		out[0] = cp + 0x50;
		return 1;
	}
	out[0] = cp;
	return 1;
}

/**
 * @brief Découpe un texte UTF-8 en mots (suites de lettres et de chiffres), pliés
 *        en minuscules sans accents. Les mots trop courts ou trop longs sont ignorés.
 * @param text texte UTF-8 (peut être NULL)
 * @param length longueur en octets du texte
 * @param min_chars longueur minimale d'un mot en caractères (idéogrammes exceptés)
 * @param func rappel appelé pour chaque mot ; s'il retourne une valeur non nulle, la tokenisation s'arrête
 * @param user_data donnée transmise au rappel
 * @return nombre de mots produits
 */
static inline size_t
evoidx_tokenize (const char *text,
		 size_t length,
		 size_t min_chars,
		 EvoIdxTokenFunc func,
		 void *user_data)
{
	const unsigned char *p = (const unsigned char *) text;
	const unsigned char *end = p + (text ? length : 0);
	unsigned char token[EVOIDX_MAX_TOKEN_CHARS * 4 + 8];
	size_t used = 0, chars = 0, count = 0;
	int overflow = 0;

	for (;;) {
		uint32_t cp = 0, folded[2] = { 0, 0 };
		int produced = 0, ideograph = 0, at_end = (p >= end), ii;

		if (!at_end) {
			p += evoidx_utf8_decode (p, end, &cp);
			ideograph = evoidx_is_ideograph (cp);
			if (!ideograph)
				produced = evoidx_fold (cp, folded);
		}

		if (produced > 0) {
			/* lettre ou chiffre : prolonge le mot courant */
			for (ii = 0; ii < produced; ii++) {
				if (chars >= EVOIDX_MAX_TOKEN_CHARS) {
					overflow = 1;
				} else {
					used += evoidx_utf8_encode (folded[ii], token + used);
					chars++;
				}
			}
			continue;
		}

		/* séparateur, idéogramme ou fin du texte : le mot courant est terminé */
		if (used > 0 && !overflow && chars >= min_chars) {
			count++;
			if (func ((const char *) token, used, user_data))
				return count;
		}
		used = 0;
		chars = 0;
		overflow = 0;
		if (ideograph) {
			unsigned char one[4];
			size_t n = evoidx_utf8_encode (cp, one);

			count++;
			if (func ((const char *) one, n, user_data))
				return count;
		}
		if (at_end)
			break;
	}

	return count;
}

/**
 * @brief Un fichier joint est-il indexable (son contenu est du texte lisible) ?
 *        Les PDF et documents Office ne sont pas extraits : ils ne sont pas indexables.
 * @param mime_type type MIME de la pièce jointe (ex. « text/plain »), peut être NULL
 * @param filename nom du fichier, peut être NULL
 * @return 1 si le contenu est du texte à indexer
 */
static inline int
evoidx_is_indexable_attachment (const char *mime_type,
				const char *filename)
{
	static const char * const types[] = {
		"application/json", "application/xml", "application/xhtml+xml",
		"application/javascript", "application/x-javascript",
		"application/x-sh", "application/x-yaml", "application/sql", NULL
	};
	static const char * const extensions[] = {
		".txt", ".csv", ".tsv", ".log", ".md", ".json", ".xml", ".html", ".htm", ".ics",
		".vcf", ".yaml", ".yml", ".ini", ".cfg", ".conf", ".sql", ".srt", ".tex", NULL
	};
	size_t ii;

	if (mime_type && *mime_type) {
		/* comparaison insensible à la casse, sans les paramètres éventuels */
		char lower[64];
		size_t n = 0;

		for (; mime_type[n] && mime_type[n] != ';' && n + 1 < sizeof (lower); n++)
			lower[n] = (char) ((mime_type[n] >= 'A' && mime_type[n] <= 'Z') ? mime_type[n] + 32 : mime_type[n]);
		lower[n] = '\0';
		if (strncmp (lower, "text/", 5) == 0)
			return 1;
		for (ii = 0; types[ii]; ii++) {
			if (strcmp (lower, types[ii]) == 0)
				return 1;
		}
	}
	if (filename && *filename) {
		size_t len = strlen (filename);

		for (ii = 0; extensions[ii]; ii++) {
			size_t ext_len = strlen (extensions[ii]), jj;

			if (len < ext_len)
				continue;
			for (jj = 0; jj < ext_len; jj++) {
				char c = filename[len - ext_len + jj];

				if (c >= 'A' && c <= 'Z')
					c = (char) (c + 32);
				if (c != extensions[ii][jj])
					break;
			}
			if (jj == ext_len)
				return 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Table de hachage chaînée (clé texte ~> entier)                            */
/* ------------------------------------------------------------------------ */

typedef struct _EvoIdxNode {
	char *key;
	uint32_t value;
	struct _EvoIdxNode *next;
} EvoIdxNode;

typedef struct {
	EvoIdxNode **buckets;
	size_t n_buckets;
	size_t count;
} EvoIdxMap;

static inline uint32_t
evoidx_hash (const char *s,
	     size_t length)
{
	uint32_t h = 2166136261u;
	size_t ii;

	for (ii = 0; ii < length; ii++) {
		h ^= (unsigned char) s[ii];
		h *= 16777619u;
	}
	return h;
}

static inline void
evoidx_map_init (EvoIdxMap *map)
{
	map->n_buckets = 64;
	map->buckets = (EvoIdxNode **) calloc (map->n_buckets, sizeof (EvoIdxNode *));
	map->count = 0;
}

static inline void
evoidx_map_clear (EvoIdxMap *map)
{
	size_t ii;

	for (ii = 0; map->buckets && ii < map->n_buckets; ii++) {
		EvoIdxNode *node = map->buckets[ii];

		while (node) {
			EvoIdxNode *next = node->next;

			free (node->key);
			free (node);
			node = next;
		}
	}
	free (map->buckets);
	map->buckets = NULL;
	map->n_buckets = 0;
	map->count = 0;
}

static inline EvoIdxNode *
evoidx_map_find (const EvoIdxMap *map,
		 const char *key,
		 size_t length)
{
	EvoIdxNode *node;

	if (!map->buckets)
		return NULL;
	for (node = map->buckets[evoidx_hash (key, length) % map->n_buckets]; node; node = node->next) {
		if (strlen (node->key) == length && memcmp (node->key, key, length) == 0)
			return node;
	}
	return NULL;
}

/* Ajoute ou remplace une entrée ; retourne 0 en cas de manque de mémoire. */
static inline int
evoidx_map_set (EvoIdxMap *map,
		const char *key,
		size_t length,
		uint32_t value)
{
	EvoIdxNode *node = evoidx_map_find (map, key, length);
	size_t bucket;

	if (node) {
		node->value = value;
		return 1;
	}
	if (map->count >= map->n_buckets * 2) {
		size_t new_size = map->n_buckets * 4, ii;
		EvoIdxNode **fresh = (EvoIdxNode **) calloc (new_size, sizeof (EvoIdxNode *));

		if (fresh) {
			for (ii = 0; ii < map->n_buckets; ii++) {
				EvoIdxNode *cur = map->buckets[ii];

				while (cur) {
					EvoIdxNode *next = cur->next;
					size_t target = evoidx_hash (cur->key, strlen (cur->key)) % new_size;

					cur->next = fresh[target];
					fresh[target] = cur;
					cur = next;
				}
			}
			free (map->buckets);
			map->buckets = fresh;
			map->n_buckets = new_size;
		}
	}
	node = (EvoIdxNode *) malloc (sizeof (EvoIdxNode));
	if (!node)
		return 0;
	node->key = (char *) malloc (length + 1);
	if (!node->key) {
		free (node);
		return 0;
	}
	memcpy (node->key, key, length);
	node->key[length] = '\0';
	node->value = value;
	bucket = evoidx_hash (key, length) % map->n_buckets;
	node->next = map->buckets[bucket];
	map->buckets[bucket] = node;
	map->count++;
	return 1;
}

static inline void
evoidx_map_remove (EvoIdxMap *map,
		   const char *key,
		   size_t length)
{
	EvoIdxNode **link;

	if (!map->buckets)
		return;
	for (link = &map->buckets[evoidx_hash (key, length) % map->n_buckets]; *link; link = &(*link)->next) {
		EvoIdxNode *node = *link;

		if (strlen (node->key) == length && memcmp (node->key, key, length) == 0) {
			*link = node->next;
			free (node->key);
			free (node);
			map->count--;
			return;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Index inversé                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
	char *text;		/* mot plié (UTF-8) */
	uint32_t *postings;	/* numéros de documents, croissants */
	size_t n_postings;
	size_t cap_postings;
} EvoIdxTerm;

typedef struct {
	char *key;
	int alive;
} EvoIdxDoc;

typedef struct {
	EvoIdxDoc *docs;	/* indexés par le numéro de document */
	size_t n_docs;
	size_t cap_docs;
	size_t n_alive;
	EvoIdxMap doc_map;	/* clé ~> numéro de document (documents vivants) */

	EvoIdxTerm *terms;
	size_t n_terms;
	size_t cap_terms;
	EvoIdxMap term_map;	/* mot ~> numéro de terme */
	uint32_t *order;	/* numéros de termes triés par texte (pour les préfixes) */
	size_t order_len;
	int order_valid;
} EvoIdx;

/**
 * @brief Crée un index vide.
 * @return nouvel index, à libérer avec evoidx_free ()
 */
static inline EvoIdx *
evoidx_new (void)
{
	EvoIdx *idx = (EvoIdx *) calloc (1, sizeof (EvoIdx));

	if (!idx)
		return NULL;
	evoidx_map_init (&idx->doc_map);
	evoidx_map_init (&idx->term_map);
	return idx;
}

static inline void
evoidx_clear_contents (EvoIdx *idx)
{
	size_t ii;

	for (ii = 0; ii < idx->n_docs; ii++)
		free (idx->docs[ii].key);
	for (ii = 0; ii < idx->n_terms; ii++) {
		free (idx->terms[ii].text);
		free (idx->terms[ii].postings);
	}
	free (idx->docs);
	free (idx->terms);
	free (idx->order);
	idx->docs = NULL;
	idx->terms = NULL;
	idx->order = NULL;
	idx->n_docs = idx->cap_docs = idx->n_alive = 0;
	idx->n_terms = idx->cap_terms = idx->order_len = 0;
	idx->order_valid = 0;
}

/**
 * @brief Libère un index.
 * @param idx index (peut être NULL)
 */
static inline void
evoidx_free (EvoIdx *idx)
{
	if (!idx)
		return;
	evoidx_clear_contents (idx);
	evoidx_map_clear (&idx->doc_map);
	evoidx_map_clear (&idx->term_map);
	free (idx);
}

/**
 * @brief Nombre de documents vivants.
 * @param idx index
 * @return nombre de documents
 */
static inline size_t
evoidx_document_count (const EvoIdx *idx)
{
	return idx ? idx->n_alive : 0;
}

/**
 * @brief Nombre de mots distincts (y compris ceux de documents supprimés pas encore compactés).
 * @param idx index
 * @return nombre de termes
 */
static inline size_t
evoidx_term_count (const EvoIdx *idx)
{
	return idx ? idx->n_terms : 0;
}

/**
 * @brief Le document de cette clé est-il dans l'index ?
 * @param idx index
 * @param key clé du document (UID du message)
 * @return 1 si présent
 */
static inline int
evoidx_has_document (const EvoIdx *idx,
		     const char *key)
{
	return idx && key && evoidx_map_find (&idx->doc_map, key, strlen (key)) != NULL;
}

/**
 * @brief Parcourt les clés des documents vivants.
 * @param idx index
 * @param func rappel (clé, donnée) ; une valeur non nulle interrompt le parcours
 * @param user_data donnée transmise au rappel
 */
static inline void
evoidx_foreach_document (const EvoIdx *idx,
			 int (* func) (const char *key, void *user_data),
			 void *user_data)
{
	size_t ii;

	for (ii = 0; idx && ii < idx->n_docs; ii++) {
		if (idx->docs[ii].alive && func (idx->docs[ii].key, user_data))
			return;
	}
}

/* Ajoute le numéro de document aux postings d'un mot (créé au besoin). */
static inline int
evoidx_post (EvoIdx *idx,
	     const char *token,
	     size_t length,
	     uint32_t doc)
{
	EvoIdxNode *node = evoidx_map_find (&idx->term_map, token, length);
	EvoIdxTerm *term;

	if (!node) {
		if (idx->n_terms == idx->cap_terms) {
			size_t cap = idx->cap_terms ? idx->cap_terms * 2 : 256;
			EvoIdxTerm *grown = (EvoIdxTerm *) realloc (idx->terms, cap * sizeof (EvoIdxTerm));

			if (!grown)
				return 0;
			idx->terms = grown;
			idx->cap_terms = cap;
		}
		term = &idx->terms[idx->n_terms];
		memset (term, 0, sizeof (*term));
		term->text = (char *) malloc (length + 1);
		if (!term->text)
			return 0;
		memcpy (term->text, token, length);
		term->text[length] = '\0';
		if (!evoidx_map_set (&idx->term_map, token, length, (uint32_t) idx->n_terms)) {
			free (term->text);
			return 0;
		}
		idx->n_terms++;
		idx->order_valid = 0;
	} else {
		term = &idx->terms[node->value];
	}

	if (term->n_postings > 0 && term->postings[term->n_postings - 1] == doc)
		return 1;	/* mot déjà vu dans ce document */
	if (term->n_postings == term->cap_postings) {
		size_t cap = term->cap_postings ? term->cap_postings * 2 : 4;
		uint32_t *grown = (uint32_t *) realloc (term->postings, cap * sizeof (uint32_t));

		if (!grown)
			return 0;
		term->postings = grown;
		term->cap_postings = cap;
	}
	term->postings[term->n_postings++] = doc;
	return 1;
}

static inline int
evoidx_add_token_cb (const char *token,
		     size_t length,
		     void *user_data)
{
	void **ctx = (void **) user_data;

	if (!evoidx_post ((EvoIdx *) ctx[0], token, length, *(uint32_t *) ctx[1]))
		*(int *) ctx[2] = 1;
	return *(int *) ctx[2];
}

/**
 * @brief Compacte l'index : élimine les documents supprimés (renumérotation) et
 *        les mots devenus sans document.
 * @param idx index
 */
static inline void
evoidx_compact (EvoIdx *idx)
{
	uint32_t *remap;
	size_t ii, kept = 0, new_terms = 0;

	if (idx->n_alive == idx->n_docs)
		return;
	remap = (uint32_t *) malloc ((idx->n_docs + 1) * sizeof (uint32_t));
	if (!remap)
		return;

	for (ii = 0; ii < idx->n_docs; ii++) {
		if (idx->docs[ii].alive) {
			remap[ii] = (uint32_t) kept;
			idx->docs[kept++] = idx->docs[ii];
		} else {
			remap[ii] = UINT32_MAX;
			free (idx->docs[ii].key);
		}
	}
	idx->n_docs = kept;

	evoidx_map_clear (&idx->doc_map);
	evoidx_map_init (&idx->doc_map);
	for (ii = 0; ii < idx->n_docs; ii++)
		evoidx_map_set (&idx->doc_map, idx->docs[ii].key, strlen (idx->docs[ii].key), (uint32_t) ii);

	evoidx_map_clear (&idx->term_map);
	evoidx_map_init (&idx->term_map);
	for (ii = 0; ii < idx->n_terms; ii++) {
		EvoIdxTerm *term = &idx->terms[ii];
		size_t jj, out = 0;

		for (jj = 0; jj < term->n_postings; jj++) {
			uint32_t mapped = remap[term->postings[jj]];

			if (mapped != UINT32_MAX)
				term->postings[out++] = mapped;
		}
		term->n_postings = out;
		if (out == 0) {
			free (term->text);
			free (term->postings);
			continue;
		}
		idx->terms[new_terms] = *term;
		evoidx_map_set (&idx->term_map, idx->terms[new_terms].text, strlen (idx->terms[new_terms].text),
			(uint32_t) new_terms);
		new_terms++;
	}
	idx->n_terms = new_terms;
	idx->order_valid = 0;
	free (remap);
}

/**
 * @brief Retire un document de l'index.
 * @param idx index
 * @param key clé du document
 * @return 1 si le document existait
 */
static inline int
evoidx_remove_document (EvoIdx *idx,
			const char *key)
{
	EvoIdxNode *node;

	if (!idx || !key)
		return 0;
	node = evoidx_map_find (&idx->doc_map, key, strlen (key));
	if (!node)
		return 0;
	idx->docs[node->value].alive = 0;
	idx->n_alive--;
	evoidx_map_remove (&idx->doc_map, key, strlen (key));

	/* trop de documents morts : compaction */
	if (idx->n_docs - idx->n_alive > 64 && idx->n_docs - idx->n_alive > idx->n_alive)
		evoidx_compact (idx);
	return 1;
}

/**
 * @brief Ajoute un document (remplace celui de même clé).
 * @param idx index
 * @param key clé du document (UID du message)
 * @param text texte à indexer (UTF-8)
 * @param length longueur en octets du texte
 * @return 1 en cas de succès, 0 en cas d'erreur (mémoire)
 */
static inline int
evoidx_add_document (EvoIdx *idx,
		     const char *key,
		     const char *text,
		     size_t length)
{
	uint32_t doc;
	int failed = 0;
	void *ctx[3];

	if (!idx || !key)
		return 0;
	evoidx_remove_document (idx, key);

	if (idx->n_docs == idx->cap_docs) {
		size_t cap = idx->cap_docs ? idx->cap_docs * 2 : 64;
		EvoIdxDoc *grown = (EvoIdxDoc *) realloc (idx->docs, cap * sizeof (EvoIdxDoc));

		if (!grown)
			return 0;
		idx->docs = grown;
		idx->cap_docs = cap;
	}
	doc = (uint32_t) idx->n_docs;
	idx->docs[doc].key = (char *) malloc (strlen (key) + 1);
	if (!idx->docs[doc].key)
		return 0;
	memcpy (idx->docs[doc].key, key, strlen (key) + 1);
	idx->docs[doc].alive = 1;
	if (!evoidx_map_set (&idx->doc_map, key, strlen (key), doc)) {
		free (idx->docs[doc].key);
		return 0;
	}
	idx->n_docs++;
	idx->n_alive++;

	ctx[0] = idx;
	ctx[1] = &doc;
	ctx[2] = &failed;
	evoidx_tokenize (text, text ? length : 0, EVOIDX_MIN_TOKEN_CHARS, evoidx_add_token_cb, ctx);
	return !failed;
}

/* Trie par texte les numéros de termes de a[0..n[ (tri fusion, sans état global : plusieurs index
 * peuvent être triés en même temps par des fils différents). tmp : tampon de n éléments. */
static inline void
evoidx_sort_order (const EvoIdxTerm *terms,
		   uint32_t *a,
		   uint32_t *tmp,
		   size_t n)
{
	size_t width, lo;

	for (width = 1; width < n; width *= 2) {
		for (lo = 0; lo < n; lo += 2 * width) {
			size_t mid = lo + width < n ? lo + width : n;
			size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
			size_t i = lo, j = mid, k = lo;

			while (i < mid && j < hi) {
				if (strcmp (terms[a[j]].text, terms[a[i]].text) < 0)
					tmp[k++] = a[j++];
				else
					tmp[k++] = a[i++];
			}
			while (i < mid)
				tmp[k++] = a[i++];
			while (j < hi)
				tmp[k++] = a[j++];
		}
		memcpy (a, tmp, n * sizeof (uint32_t));
	}
}

/* Reconstruit la liste triée des mots si elle est périmée. */
static inline void
evoidx_ensure_order (EvoIdx *idx)
{
	size_t ii;
	uint32_t *tmp;

	if (idx->order_valid)
		return;
	free (idx->order);
	idx->order = (uint32_t *) malloc ((idx->n_terms + 1) * sizeof (uint32_t));
	tmp = (uint32_t *) malloc ((idx->n_terms + 1) * sizeof (uint32_t));
	idx->order_len = 0;
	if (!idx->order || !tmp) {
		free (tmp);
		free (idx->order);
		idx->order = NULL;
		return;
	}
	for (ii = 0; ii < idx->n_terms; ii++)
		idx->order[ii] = (uint32_t) ii;
	idx->order_len = idx->n_terms;
	evoidx_sort_order (idx->terms, idx->order, tmp, idx->order_len);
	free (tmp);
	idx->order_valid = 1;
}

typedef struct {
	char **words;
	size_t n_words;
	size_t cap_words;
} EvoIdxQuery;

static inline int
evoidx_query_token_cb (const char *token,
		       size_t length,
		       void *user_data)
{
	EvoIdxQuery *query = (EvoIdxQuery *) user_data;

	if (query->n_words == query->cap_words) {
		size_t cap = query->cap_words ? query->cap_words * 2 : 8;
		char **grown = (char **) realloc (query->words, cap * sizeof (char *));

		if (!grown)
			return 1;
		query->words = grown;
		query->cap_words = cap;
	}
	query->words[query->n_words] = (char *) malloc (length + 1);
	if (!query->words[query->n_words])
		return 1;
	memcpy (query->words[query->n_words], token, length);
	query->words[query->n_words][length] = '\0';
	query->n_words++;
	return 0;
}

/**
 * @brief Cherche les documents contenant tous les mots de la requête, chacun
 *        pouvant n'être que le début d'un mot de l'index (recherche au fil de la saisie).
 * @param idx index
 * @param query requête UTF-8 (mots séparés par des espaces ou de la ponctuation)
 * @param keys reçoit un tableau de clés (pointeurs valables jusqu'à la prochaine
 *             modification de l'index), à libérer avec free () ; NULL si aucun résultat
 * @param count reçoit le nombre de clés
 * @return 1 si la requête a pu être évaluée, 0 si elle ne contient aucun mot utilisable
 *         (trop courts) ou en cas d'erreur de mémoire : l'appelant se rabat alors sur une autre méthode
 */
static inline int
evoidx_query (EvoIdx *idx,
	      const char *query,
	      const char ***keys,
	      size_t *count)
{
	EvoIdxQuery words = { NULL, 0, 0 };
	uint8_t *hits = NULL, *current = NULL;
	size_t ii, jj, found = 0;
	int ok = 0;

	*keys = NULL;
	*count = 0;
	if (!idx || !query)
		return 0;
	evoidx_tokenize (query, strlen (query), EVOIDX_MIN_TOKEN_CHARS, evoidx_query_token_cb, &words);
	if (words.n_words == 0)
		goto out;

	evoidx_ensure_order (idx);
	if (!idx->order && idx->n_terms > 0)
		goto out;
	hits = (uint8_t *) calloc (idx->n_docs + 1, 1);
	current = (uint8_t *) calloc (idx->n_docs + 1, 1);
	if (!hits || !current)
		goto out;
	ok = 1;

	for (ii = 0; ii < words.n_words; ii++) {
		const char *prefix = words.words[ii];
		size_t plen = strlen (prefix);
		size_t lo = 0, hi = idx->order_len;

		/* première position dont le mot est >= au préfixe */
		while (lo < hi) {
			size_t mid = (lo + hi) / 2;

			if (strcmp (idx->terms[idx->order[mid]].text, prefix) < 0)
				lo = mid + 1;
			else
				hi = mid;
		}
		memset (current, 0, idx->n_docs + 1);
		for (; lo < idx->order_len; lo++) {
			const EvoIdxTerm *term = &idx->terms[idx->order[lo]];

			if (strncmp (term->text, prefix, plen) != 0)
				break;
			for (jj = 0; jj < term->n_postings; jj++)
				current[term->postings[jj]] = 1;
		}
		if (ii == 0) {
			memcpy (hits, current, idx->n_docs + 1);
		} else {
			for (jj = 0; jj < idx->n_docs; jj++)
				hits[jj] &= current[jj];
		}
	}

	for (jj = 0; jj < idx->n_docs; jj++) {
		if (hits[jj] && idx->docs[jj].alive)
			found++;
	}
	if (found > 0) {
		const char **out = (const char **) malloc (found * sizeof (char *));
		size_t n = 0;

		if (!out) {
			ok = 0;
			goto out;
		}
		for (jj = 0; jj < idx->n_docs; jj++) {
			if (hits[jj] && idx->docs[jj].alive)
				out[n++] = idx->docs[jj].key;
		}
		*keys = out;
		*count = n;
	}

out:
	for (ii = 0; ii < words.n_words; ii++)
		free (words.words[ii]);
	free (words.words);
	free (hits);
	free (current);
	return ok;
}

/* ------------------------------------------------------------------------ */
/* Sérialisation                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
	unsigned char *data;
	size_t len;
	size_t cap;
	int failed;
} EvoIdxBuffer;

static inline void
evoidx_buf_put (EvoIdxBuffer *buf,
		const void *data,
		size_t length)
{
	if (buf->failed)
		return;
	if (buf->len + length > buf->cap) {
		size_t cap = buf->cap ? buf->cap : 4096;
		unsigned char *grown;

		while (cap < buf->len + length)
			cap *= 2;
		grown = (unsigned char *) realloc (buf->data, cap);
		if (!grown) {
			buf->failed = 1;
			return;
		}
		buf->data = grown;
		buf->cap = cap;
	}
	memcpy (buf->data + buf->len, data, length);
	buf->len += length;
}

static inline void
evoidx_buf_u32 (EvoIdxBuffer *buf,
		uint32_t value)
{
	unsigned char b[4];

	b[0] = (unsigned char) (value & 0xFF);
	b[1] = (unsigned char) ((value >> 8) & 0xFF);
	b[2] = (unsigned char) ((value >> 16) & 0xFF);
	b[3] = (unsigned char) ((value >> 24) & 0xFF);
	evoidx_buf_put (buf, b, 4);
}

/**
 * @brief Sérialise l'index (compacté au préalable) : format « EVIX », entiers 32 bits petit-boutistes.
 * @param idx index
 * @param length reçoit la taille en octets
 * @return tampon alloué avec malloc (à libérer avec free), NULL en cas d'erreur
 */
static inline unsigned char *
evoidx_serialize (EvoIdx *idx,
		  size_t *length)
{
	EvoIdxBuffer buf = { NULL, 0, 0, 0 };
	size_t ii, jj;

	*length = 0;
	if (!idx)
		return NULL;
	evoidx_compact (idx);

	evoidx_buf_put (&buf, "EVIX", 4);
	evoidx_buf_u32 (&buf, EVOIDX_FORMAT_VERSION);
	evoidx_buf_u32 (&buf, (uint32_t) idx->n_docs);
	evoidx_buf_u32 (&buf, (uint32_t) idx->n_terms);
	for (ii = 0; ii < idx->n_docs; ii++) {
		size_t klen = strlen (idx->docs[ii].key);

		evoidx_buf_u32 (&buf, (uint32_t) klen);
		evoidx_buf_put (&buf, idx->docs[ii].key, klen);
	}
	for (ii = 0; ii < idx->n_terms; ii++) {
		const EvoIdxTerm *term = &idx->terms[ii];
		size_t tlen = strlen (term->text);

		evoidx_buf_u32 (&buf, (uint32_t) tlen);
		evoidx_buf_put (&buf, term->text, tlen);
		evoidx_buf_u32 (&buf, (uint32_t) term->n_postings);
		for (jj = 0; jj < term->n_postings; jj++)
			evoidx_buf_u32 (&buf, term->postings[jj]);
	}
	if (buf.failed) {
		free (buf.data);
		return NULL;
	}
	*length = buf.len;
	return buf.data;
}

/* Lecture bornée d'un entier 32 bits ; retourne 0 si les données sont trop courtes. */
static inline int
evoidx_read_u32 (const unsigned char *data,
		 size_t length,
		 size_t *pos,
		 uint32_t *value)
{
	if (*pos + 4 > length)
		return 0;
	*value = (uint32_t) data[*pos] | ((uint32_t) data[*pos + 1] << 8) |
		((uint32_t) data[*pos + 2] << 16) | ((uint32_t) data[*pos + 3] << 24);
	*pos += 4;
	return 1;
}

/**
 * @brief Recharge un index sérialisé. Les données sont entièrement validées
 *        (bornes, numéros de documents) : un fichier tronqué ou corrompu donne NULL.
 * @param data données sérialisées
 * @param length taille des données
 * @return index (evoidx_free), NULL si les données sont invalides
 */
static inline EvoIdx *
evoidx_deserialize (const unsigned char *data,
		    size_t length)
{
	EvoIdx *idx;
	size_t pos = 8, ii, jj;
	uint32_t version, n_docs, n_terms, value;

	if (!data || length < 16 || memcmp (data, "EVIX", 4) != 0)
		return NULL;
	{
		size_t p = 4;

		if (!evoidx_read_u32 (data, length, &p, &version) || version != EVOIDX_FORMAT_VERSION)
			return NULL;
		if (!evoidx_read_u32 (data, length, &p, &n_docs) || !evoidx_read_u32 (data, length, &p, &n_terms))
			return NULL;
		pos = p;
	}
	/* chaque document et chaque terme occupent au moins 4 octets : borne contre les tailles absurdes */
	if ((size_t) n_docs > length / 4 || (size_t) n_terms > length / 8)
		return NULL;

	idx = evoidx_new ();
	if (!idx)
		return NULL;
	if (n_docs > 0) {
		idx->docs = (EvoIdxDoc *) calloc (n_docs, sizeof (EvoIdxDoc));
		if (!idx->docs)
			goto fail;
		idx->cap_docs = n_docs;
	}
	if (n_terms > 0) {
		idx->terms = (EvoIdxTerm *) calloc (n_terms, sizeof (EvoIdxTerm));
		if (!idx->terms)
			goto fail;
		idx->cap_terms = n_terms;
	}

	for (ii = 0; ii < n_docs; ii++) {
		if (!evoidx_read_u32 (data, length, &pos, &value) || (size_t) value > length - pos)
			goto fail;
		idx->docs[ii].key = (char *) malloc ((size_t) value + 1);
		if (!idx->docs[ii].key)
			goto fail;
		memcpy (idx->docs[ii].key, data + pos, value);
		idx->docs[ii].key[value] = '\0';
		idx->docs[ii].alive = 1;
		pos += value;
		idx->n_docs = ii + 1;
		if (!evoidx_map_set (&idx->doc_map, idx->docs[ii].key, value, (uint32_t) ii))
			goto fail;
	}
	idx->n_alive = idx->n_docs;

	for (ii = 0; ii < n_terms; ii++) {
		EvoIdxTerm *term = &idx->terms[ii];
		uint32_t tlen, n_post;

		if (!evoidx_read_u32 (data, length, &pos, &tlen) || (size_t) tlen > length - pos || tlen == 0)
			goto fail;
		term->text = (char *) malloc ((size_t) tlen + 1);
		if (!term->text)
			goto fail;
		memcpy (term->text, data + pos, tlen);
		term->text[tlen] = '\0';
		pos += tlen;
		idx->n_terms = ii + 1;
		if (!evoidx_read_u32 (data, length, &pos, &n_post) || (size_t) n_post > (length - pos) / 4)
			goto fail;
		if (n_post > 0) {
			term->postings = (uint32_t *) malloc ((size_t) n_post * sizeof (uint32_t));
			if (!term->postings)
				goto fail;
			term->cap_postings = n_post;
		}
		for (jj = 0; jj < n_post; jj++) {
			if (!evoidx_read_u32 (data, length, &pos, &value) || value >= n_docs)
				goto fail;
			term->postings[jj] = value;
		}
		term->n_postings = n_post;
		if (!evoidx_map_set (&idx->term_map, term->text, tlen, (uint32_t) ii))
			goto fail;
	}
	if (pos != length)
		goto fail;
	return idx;

fail:
	evoidx_free (idx);
	return NULL;
}

#endif /* EVO_CORE_INDEX_UTIL_H */
