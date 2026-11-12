/*
 * evo-core : classificateur bayésien local du courrier indésirable.
 *
 * Fichier d'en-tête autonome (C pur, sans GLib ni Camel) inclus par
 * evo-core-spam.c et par les tests unitaires (tests/unit/test_spam.c).
 *
 * Principe (méthode de Robinson-Fisher, celle de SpamBayes / Bogofilter) :
 *  - un message est réduit à un ensemble de « jetons » (mots du corps et de
 *    l'objet, adresse et domaine de l'expéditeur, quelques en-têtes) ;
 *  - la base mémorise, pour chaque jeton, dans combien de messages
 *    indésirables et dans combien de messages légitimes il est apparu ;
 *  - la probabilité d'indésirabilité de chaque jeton est lissée, puis les
 *    jetons les plus significatifs sont combinés par le test du khi-deux de
 *    Fisher : le résultat est un score entre 0 (légitime) et 1 (indésirable).
 *
 * Les messages appris sont repérés par une empreinte de leur Message-ID
 * (entrée « @<empreinte> » de la base) : réapprendre un message ne le compte
 * pas deux fois, et le classer dans l'autre catégorie corrige l'apprentissage.
 */

#ifndef EVO_CORE_SPAM_UTIL_H
#define EVO_CORE_SPAM_UTIL_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bornes du découpage en jetons */
#define SPAM_MIN_TOKEN_LEN		3	/* octets */
#define SPAM_MAX_TOKEN_LEN		32	/* octets : au-delà, jeton ignoré (base64, identifiants) */
#define SPAM_MAX_TEXT_BYTES		65536	/* octets d'un texte réellement analysés */
#define SPAM_TOKEN_BUFFER		64

/* Bornes du calcul du score */
#define SPAM_MAX_SIGNIFICANT		150	/* jetons les plus significatifs retenus */
#define SPAM_MIN_STRENGTH		0.1	/* écart minimal à 0,5 pour qu'un jeton compte */
#define SPAM_PRIOR_STRENGTH		1.0	/* poids de l'a priori dans le lissage de Robinson */
#define SPAM_PRIOR_PROBABILITY		0.5

/* Nombre minimal de messages appris dans chaque catégorie pour que le
 * classement automatique soit actif */
#define SPAM_MIN_TRAINED		20

/* Taille maximale de la base avant élagage (jetons vus une seule fois) */
#define SPAM_MAX_ENTRIES		250000

#define SPAM_DB_MAGIC			"EVOSPAM 1"

/* Entrée de la base : un jeton et ses compteurs */
typedef struct {
	char *token;		/* NULL : emplacement libre */
	uint32_t spam;		/* messages indésirables contenant le jeton */
	uint32_t ham;		/* messages légitimes contenant le jeton */
} SpamEntry;

/* Base d'apprentissage, ou simple ensemble de jetons d'un message (compteurs inutilisés) */
typedef struct {
	SpamEntry *entries;	/* table de hachage à adressage ouvert, capacité en puissance de 2 */
	size_t capacity;
	size_t used;
	uint32_t n_spam;	/* messages indésirables appris */
	uint32_t n_ham;		/* messages légitimes appris */
} SpamDb;

/**
 * @brief Empreinte FNV-1a 64 bits d'une chaîne.
 * @param text chaîne terminée par un zéro
 * @return empreinte
 */
static inline uint64_t
spam_hash (const char *text)
{
	uint64_t hash = 1469598103934665603ULL;

	for (; text && *text; text++) {
		hash ^= (unsigned char) *text;
		hash *= 1099511628211ULL;
	}

	return hash;
}

/**
 * @brief Copie d'une chaîne (malloc).
 * @param text chaîne à copier
 * @return copie à libérer avec free (), ou NULL si la mémoire manque
 */
static inline char *
spam_strdup (const char *text)
{
	size_t length = strlen (text) + 1;
	char *copy = (char *) malloc (length);

	if (copy)
		memcpy (copy, text, length);

	return copy;
}

/**
 * @brief Initialise une base vide.
 * @param db base à initialiser
 */
static inline void
spam_db_init (SpamDb *db)
{
	memset (db, 0, sizeof (*db));
}

/**
 * @brief Libère les entrées d'une base et la remet à l'état vide (compteurs de messages inclus).
 * @param db base à vider
 */
static inline void
spam_db_clear (SpamDb *db)
{
	size_t ii;

	for (ii = 0; ii < db->capacity; ii++)
		free (db->entries[ii].token);

	free (db->entries);
	spam_db_init (db);
}

/**
 * @brief Cherche l'emplacement d'un jeton (existant ou libre) dans une table.
 * @param entries table
 * @param capacity capacité de la table (puissance de 2, non nulle)
 * @param token jeton recherché
 * @return emplacement du jeton s'il existe, sinon premier emplacement libre
 */
static inline SpamEntry *
spam_find_slot (SpamEntry *entries,
		size_t capacity,
		const char *token)
{
	size_t index = (size_t) (spam_hash (token) & (capacity - 1));

	while (entries[index].token && strcmp (entries[index].token, token) != 0)
		index = (index + 1) & (capacity - 1);

	return &entries[index];
}

/**
 * @brief Agrandit la table d'une base (ou l'alloue à la première insertion).
 * @param db base
 * @return 1 si réussi, 0 si la mémoire manque
 */
static inline int
spam_db_grow (SpamDb *db)
{
	size_t new_capacity = db->capacity ? db->capacity * 2 : 1024;
	SpamEntry *entries = (SpamEntry *) calloc (new_capacity, sizeof (SpamEntry));
	size_t ii;

	if (!entries)
		return 0;

	for (ii = 0; ii < db->capacity; ii++) {
		if (db->entries[ii].token)
			*spam_find_slot (entries, new_capacity, db->entries[ii].token) = db->entries[ii];
	}

	free (db->entries);
	db->entries = entries;
	db->capacity = new_capacity;

	return 1;
}

/**
 * @brief Cherche un jeton dans une base.
 * @param db base
 * @param token jeton
 * @return entrée, ou NULL si le jeton est inconnu
 */
static inline SpamEntry *
spam_db_lookup (const SpamDb *db,
		const char *token)
{
	SpamEntry *entry;

	if (!db->capacity)
		return NULL;

	entry = spam_find_slot (db->entries, db->capacity, token);

	return entry->token ? entry : NULL;
}

/**
 * @brief Retourne l'entrée d'un jeton, en la créant (compteurs à zéro) si besoin.
 * @param db base
 * @param token jeton
 * @return entrée, ou NULL si la mémoire manque
 */
static inline SpamEntry *
spam_db_add (SpamDb *db,
	     const char *token)
{
	SpamEntry *entry;

	if ((db->used + 1) * 10 > db->capacity * 7 && !spam_db_grow (db))
		return NULL;

	entry = spam_find_slot (db->entries, db->capacity, token);
	if (!entry->token) {
		entry->token = spam_strdup (token);
		if (!entry->token)
			return NULL;
		entry->spam = 0;
		entry->ham = 0;
		db->used++;
	}

	return entry;
}

/**
 * @brief Replie un octet UTF-8 d'une lettre latine accentuée en minuscule (U+00C0..U+00DE sauf U+00D7).
 * @param lead premier octet de la séquence UTF-8 (0xC3 pour le bloc Latin-1)
 * @param second second octet
 * @return second octet replié en minuscule
 */
static inline unsigned char
spam_fold_latin1 (unsigned char lead,
		  unsigned char second)
{
	if (lead == 0xC3 && second >= 0x80 && second <= 0x9E && second != 0x97)
		return (unsigned char) (second + 0x20);

	return second;
}

/**
 * @brief Indique si un octet fait partie d'un jeton (lettre, chiffre, octet UTF-8, apostrophe, tiret, $).
 * @param c octet
 * @return 1 si l'octet est un caractère de jeton
 */
static inline int
spam_is_token_char (unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		c >= 0x80 || c == '\'' || c == '-' || c == '$' || c == '_';
}

/**
 * @brief Ajoute un jeton à un ensemble après normalisation (ponctuation de bord retirée, bornes de longueur).
 * @param set ensemble de jetons
 * @param prefix préfixe du jeton (« s: » pour l'objet…), ou NULL
 * @param token jeton brut en minuscules (pas terminé par un zéro)
 * @param length longueur du jeton brut
 */
static inline void
spam_add_token (SpamDb *set,
		const char *prefix,
		const char *token,
		size_t length)
{
	char buffer[SPAM_TOKEN_BUFFER];
	size_t prefix_length = prefix ? strlen (prefix) : 0;
	size_t ii;
	int digits_only = 1;

	/* ponctuation de bord */
	while (length && (*token == '-' || *token == '\'' || *token == '_')) {
		token++;
		length--;
	}
	while (length && (token[length - 1] == '-' || token[length - 1] == '\'' || token[length - 1] == '_'))
		length--;

	if (length < SPAM_MIN_TOKEN_LEN || length > SPAM_MAX_TOKEN_LEN || prefix_length + length >= sizeof (buffer))
		return;

	for (ii = 0; ii < length; ii++) {
		if (token[ii] < '0' || token[ii] > '9')
			digits_only = 0;
	}
	/* les nombres seuls (numéros, identifiants) n'apprennent rien */
	if (digits_only)
		return;

	if (prefix_length)
		memcpy (buffer, prefix, prefix_length);
	memcpy (buffer + prefix_length, token, length);
	buffer[prefix_length + length] = '\0';

	spam_db_add (set, buffer);
}

/**
 * @brief Découpe un texte en jetons et les ajoute à un ensemble (doublons fusionnés).
 *        Les lettres ASCII et latines accentuées sont repliées en minuscules ; seuls les
 *        premiers SPAM_MAX_TEXT_BYTES octets sont analysés. Trois points d'exclamation ou
 *        plus dans le texte ajoutent le jeton synthétique « *excl ».
 * @param set ensemble de jetons
 * @param text texte UTF-8 (peut être NULL)
 * @param prefix préfixe ajouté à chaque jeton (« s: » pour l'objet), ou NULL pour le corps
 */
static inline void
spam_tokenize (SpamDb *set,
	       const char *text,
	       const char *prefix)
{
	char token[SPAM_MAX_TOKEN_LEN + 2];
	size_t length = 0, position;
	int overflow = 0;
	int exclamations = 0;

	if (!text)
		return;

	for (position = 0; position < SPAM_MAX_TEXT_BYTES && text[position]; position++) {
		unsigned char c = (unsigned char) text[position];

		if (c == '!')
			exclamations++;

		if (spam_is_token_char (c)) {
			if (c >= 'A' && c <= 'Z')
				c = (unsigned char) (c + ('a' - 'A'));
			else if (position > 0 && (unsigned char) text[position - 1] == 0xC3)
				c = spam_fold_latin1 (0xC3, c);

			if (length < SPAM_MAX_TOKEN_LEN + 1)
				token[length++] = (char) c;
			else
				overflow = 1;
		} else {
			if (length && !overflow)
				spam_add_token (set, prefix, token, length);
			length = 0;
			overflow = 0;
		}
	}

	if (length && !overflow)
		spam_add_token (set, prefix, token, length);

	if (exclamations >= 3)
		spam_db_add (set, "*excl");
}

/**
 * @brief Ajoute les jetons d'une adresse d'expéditeur ou de destinataire : ses mots (préfixe « f: »
 *        ou celui fourni) et son domaine entier (préfixe « d: » + préfixe), ex. « d:f:exemple.com ».
 * @param set ensemble de jetons
 * @param value valeur de l'en-tête (« Nom <adresse> »)
 * @param prefix préfixe des mots (« f: » pour From), ou NULL
 */
static inline void
spam_tokenize_address (SpamDb *set,
		       const char *value,
		       const char *prefix)
{
	const char *at, *end;
	char domain[SPAM_TOKEN_BUFFER];
	size_t length, ii;

	if (!value)
		return;

	spam_tokenize (set, value, prefix);

	at = strrchr (value, '@');
	if (!at)
		return;

	at++;
	end = at;
	while (*end && *end != '>' && *end != ' ' && *end != ',' && *end != ';' && *end != '"')
		end++;

	length = (size_t) (end - at);
	if (length < 3 || length + 4 + (prefix ? strlen (prefix) : 0) >= sizeof (domain))
		return;

	memcpy (domain, "d:", 2);
	ii = 2;
	if (prefix) {
		memcpy (domain + ii, prefix, strlen (prefix));
		ii += strlen (prefix);
	}
	for (length = 0; at + length < end; length++) {
		char c = at[length];
		domain[ii + length] = (c >= 'A' && c <= 'Z') ? (char) (c + ('a' - 'A')) : c;
	}
	domain[ii + length] = '\0';
	spam_db_add (set, domain);
}

/**
 * @brief Nombre de jetons réels d'une base ou d'un ensemble (entrées « @… » de suivi des messages exclues).
 * @param db base
 * @return nombre de jetons
 */
static inline size_t
spam_db_token_count (const SpamDb *db)
{
	size_t ii, count = 0;

	for (ii = 0; ii < db->capacity; ii++) {
		if (db->entries[ii].token && db->entries[ii].token[0] != '@')
			count++;
	}

	return count;
}

/**
 * @brief Applique (ou annule) l'apprentissage d'un message : incrémente ou décrémente les compteurs
 *        de chacun de ses jetons et le nombre de messages de la catégorie. Les compteurs ne
 *        descendent jamais sous zéro.
 * @param db base d'apprentissage
 * @param set jetons du message
 * @param is_spam 1 pour la catégorie « indésirable », 0 pour « légitime »
 * @param undo 1 pour annuler un apprentissage précédent
 */
static inline void
spam_db_apply (SpamDb *db,
	       const SpamDb *set,
	       int is_spam,
	       int undo)
{
	size_t ii;
	uint32_t *total = is_spam ? &db->n_spam : &db->n_ham;

	if (undo) {
		if (*total > 0)
			(*total)--;
	} else {
		(*total)++;
	}

	for (ii = 0; ii < set->capacity; ii++) {
		SpamEntry *entry;
		uint32_t *count;

		if (!set->entries[ii].token)
			continue;

		if (undo) {
			entry = spam_db_lookup (db, set->entries[ii].token);
			if (!entry)
				continue;
		} else {
			entry = spam_db_add (db, set->entries[ii].token);
			if (!entry)
				continue;
		}

		count = is_spam ? &entry->spam : &entry->ham;
		if (undo) {
			if (*count > 0)
				(*count)--;
		} else {
			(*count)++;
		}
	}
}

/**
 * @brief Entrée de suivi d'un message appris : « @<empreinte du Message-ID> ».
 * @param message_id Message-ID (ou toute chaîne identifiant le message)
 * @param out reçoit l'identifiant (au moins 24 octets)
 * @param size taille de out
 */
static inline void
spam_message_key (const char *message_id,
		  char *out,
		  size_t size)
{
	snprintf (out, size, "@%016llx", (unsigned long long) spam_hash (message_id ? message_id : ""));
}

/**
 * @brief Apprend un message dans une catégorie, en tenant compte d'un apprentissage précédent du
 *        même message (même clé) : déjà dans la bonne catégorie, rien ne change ; dans l'autre, il
 *        est d'abord « désappris ». Les jetons du message doivent être ceux du message appris.
 * @param db base d'apprentissage
 * @param set jetons du message
 * @param message_key clé du message (spam_message_key ()), ou NULL pour ne pas le suivre
 * @param is_spam 1 pour indésirable, 0 pour légitime
 * @return 1 si la base a changé, 0 si le message était déjà appris dans cette catégorie
 */
static inline int
spam_db_train (SpamDb *db,
	       const SpamDb *set,
	       const char *message_key,
	       int is_spam)
{
	SpamEntry *mark = NULL;

	if (message_key) {
		mark = spam_db_add (db, message_key);
		if (!mark)
			return 0;

		if ((is_spam && mark->spam) || (!is_spam && mark->ham))
			return 0;

		if (mark->spam || mark->ham) {
			spam_db_apply (db, set, mark->spam ? 1 : 0, 1);
			mark->spam = 0;
			mark->ham = 0;
		}
	}

	spam_db_apply (db, set, is_spam, 0);

	if (mark) {
		/* l'ajout de jetons a pu redimensionner la table : retrouver l'entrée */
		mark = spam_db_lookup (db, message_key);
		if (mark) {
			if (is_spam)
				mark->spam = 1;
			else
				mark->ham = 1;
		}
	}

	return 1;
}

/**
 * @brief Indique si la base a assez d'exemples dans chaque catégorie pour classer automatiquement.
 * @param db base
 * @return 1 si le classement est fiable (au moins SPAM_MIN_TRAINED messages de chaque sorte)
 */
static inline int
spam_db_ready (const SpamDb *db)
{
	return db->n_spam >= SPAM_MIN_TRAINED && db->n_ham >= SPAM_MIN_TRAINED;
}

/**
 * @brief Probabilité lissée (Robinson) qu'un jeton annonce un message indésirable.
 * @param db base
 * @param entry compteurs du jeton
 * @return probabilité entre 0 et 1, ou -1 si le jeton n'apporte aucune information
 */
static inline double
spam_token_probability (const SpamDb *db,
			const SpamEntry *entry)
{
	double ps = db->n_spam ? (double) entry->spam / db->n_spam : 0.0;
	double ph = db->n_ham ? (double) entry->ham / db->n_ham : 0.0;
	double count = (double) entry->spam + (double) entry->ham;
	double p, f;

	if (ps + ph <= 0.0)
		return -1.0;

	p = ps / (ps + ph);
	f = (SPAM_PRIOR_STRENGTH * SPAM_PRIOR_PROBABILITY + count * p) / (SPAM_PRIOR_STRENGTH + count);

	if (f < 1e-6)
		f = 1e-6;
	if (f > 1.0 - 1e-6)
		f = 1.0 - 1e-6;

	return f;
}

/**
 * @brief Probabilité du khi-deux inverse pour un nombre pair de degrés de liberté.
 * @param x2 valeur du khi-deux
 * @param degrees degrés de liberté (pair)
 * @return probabilité Q entre 0 et 1
 */
static inline double
spam_chi2q (double x2,
	    int degrees)
{
	double m = x2 / 2.0;
	double term = exp (-m);
	double sum = term;
	int ii;

	for (ii = 1; ii < degrees / 2; ii++) {
		term *= m / ii;
		sum += term;
	}

	return sum > 1.0 ? 1.0 : sum;
}

/**
 * @brief Comparaison décroissante (qsort) de l'importance des probabilités de jetons.
 */
static inline int
spam_compare_strength (const void *a,
		       const void *b)
{
	double sa = fabs (*(const double *) a - 0.5);
	double sb = fabs (*(const double *) b - 0.5);

	return sa < sb ? 1 : (sa > sb ? -1 : 0);
}

/**
 * @brief Score d'indésirabilité d'un message (combinaison de Fisher des jetons les plus significatifs).
 * @param db base d'apprentissage
 * @param set jetons du message
 * @param out_significant reçoit le nombre de jetons retenus (peut être NULL)
 * @return score entre 0 (légitime) et 1 (indésirable) ; 0,5 si le message n'apporte aucun indice
 */
static inline double
spam_score (const SpamDb *db,
	    const SpamDb *set,
	    int *out_significant)
{
	double *probs;
	size_t ii, count = 0;
	int n, kk;
	double sum_ln_f = 0.0, sum_ln_1mf = 0.0, s, h;

	if (out_significant)
		*out_significant = 0;

	if (!set->used)
		return 0.5;

	probs = (double *) malloc (set->used * sizeof (double));
	if (!probs)
		return 0.5;

	for (ii = 0; ii < set->capacity; ii++) {
		const SpamEntry *entry;
		double f;

		if (!set->entries[ii].token || set->entries[ii].token[0] == '@')
			continue;

		entry = spam_db_lookup (db, set->entries[ii].token);
		if (!entry)
			continue;

		f = spam_token_probability (db, entry);
		if (f >= 0.0 && fabs (f - 0.5) >= SPAM_MIN_STRENGTH)
			probs[count++] = f;
	}

	if (!count) {
		free (probs);
		return 0.5;
	}

	qsort (probs, count, sizeof (double), spam_compare_strength);
	n = (int) (count > SPAM_MAX_SIGNIFICANT ? SPAM_MAX_SIGNIFICANT : count);

	for (kk = 0; kk < n; kk++) {
		sum_ln_f += log (probs[kk]);
		sum_ln_1mf += log (1.0 - probs[kk]);
	}
	free (probs);

	s = 1.0 - spam_chi2q (-2.0 * sum_ln_1mf, 2 * n);
	h = 1.0 - spam_chi2q (-2.0 * sum_ln_f, 2 * n);

	if (out_significant)
		*out_significant = n;

	return (s - h + 1.0) / 2.0;
}

/**
 * @brief Élague la base : supprime les jetons vus une seule fois (hors suivi des messages) quand la
 *        base dépasse SPAM_MAX_ENTRIES entrées. Sans effet sous ce seuil.
 * @param db base
 * @return nombre d'entrées supprimées
 */
static inline size_t
spam_db_prune (SpamDb *db)
{
	SpamDb pruned;
	size_t ii, removed = 0;

	if (db->used <= SPAM_MAX_ENTRIES)
		return 0;

	spam_db_init (&pruned);
	pruned.n_spam = db->n_spam;
	pruned.n_ham = db->n_ham;

	for (ii = 0; ii < db->capacity; ii++) {
		SpamEntry *entry = &db->entries[ii];
		SpamEntry *copy;

		if (!entry->token)
			continue;

		if (entry->token[0] != '@' && entry->spam + entry->ham <= 1) {
			removed++;
			continue;
		}

		copy = spam_db_add (&pruned, entry->token);
		if (copy) {
			copy->spam = entry->spam;
			copy->ham = entry->ham;
		}
	}

	spam_db_clear (db);
	*db = pruned;

	return removed;
}

/**
 * @brief Écrit la base au format texte : en-tête, « N <indésirables> <légitimes> », puis une ligne
 *        « <indésirable> <légitime> <jeton> » par entrée.
 * @param db base
 * @param file fichier ouvert en écriture
 * @return 0 si réussi, -1 en cas d'erreur d'écriture
 */
static inline int
spam_db_write (const SpamDb *db,
	       FILE *file)
{
	size_t ii;

	if (fprintf (file, "%s\nN %u %u\n", SPAM_DB_MAGIC, (unsigned) db->n_spam, (unsigned) db->n_ham) < 0)
		return -1;

	for (ii = 0; ii < db->capacity; ii++) {
		const SpamEntry *entry = &db->entries[ii];

		if (!entry->token || (entry->spam == 0 && entry->ham == 0))
			continue;

		if (fprintf (file, "%u %u %s\n", (unsigned) entry->spam, (unsigned) entry->ham, entry->token) < 0)
			return -1;
	}

	return 0;
}

/**
 * @brief Lit une base écrite par spam_db_write (). La base fournie est vidée d'abord ; en cas
 *        d'erreur elle reste vide. Les lignes mal formées sont ignorées.
 * @param db base à remplir
 * @param file fichier ouvert en lecture
 * @return 0 si réussi, -1 si l'en-tête est absent ou invalide
 */
static inline int
spam_db_read (SpamDb *db,
	      FILE *file)
{
	char line[256];
	char *end;

	spam_db_clear (db);

	if (!fgets (line, sizeof (line), file) || strncmp (line, SPAM_DB_MAGIC, strlen (SPAM_DB_MAGIC)) != 0)
		return -1;

	if (!fgets (line, sizeof (line), file) || line[0] != 'N' || line[1] != ' ')
		return -1;

	db->n_spam = (uint32_t) strtoul (line + 2, &end, 10);
	db->n_ham = (uint32_t) strtoul (end, NULL, 10);

	while (fgets (line, sizeof (line), file)) {
		unsigned long spam, ham;
		char *token;
		size_t length = strlen (line);
		SpamEntry *entry;

		while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
			line[--length] = '\0';

		spam = strtoul (line, &end, 10);
		if (end == line || *end != ' ')
			continue;
		ham = strtoul (end + 1, &token, 10);
		if (token == end + 1 || *token != ' ' || !token[1])
			continue;

		entry = spam_db_add (db, token + 1);
		if (entry) {
			entry->spam = (uint32_t) spam;
			entry->ham = (uint32_t) ham;
		}
	}

	return 0;
}

#endif /* EVO_CORE_SPAM_UTIL_H */
