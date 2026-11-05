/*
 * evo-core : fonctions pures (C standard, sans GLib, Camel ni libical) de lecture
 * des invitations iCalendar (iTIP / iMIP, RFC 5545, 5546 et 6047) et de
 * construction des reponses (METHOD:REPLY). Partagees par le moteur
 * (evo-core-invitations.c) et par les tests unitaires (tests/unit).
 *
 * Le resume lu ici (methode, UID, SEQUENCE, titre, lieu, organisateur, participants,
 * dates brutes) ne depend d'aucune bibliotheque : la conversion des dates en heure
 * Unix (fuseaux horaires) et l'ecriture dans l'agenda restent a la charge du moteur.
 */

#ifndef EVO_CORE_ITIP_H
#define EVO_CORE_ITIP_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evo-core-mdn.h"

/** Nombre maximal de participants conserves dans un resume. */
#define EVO_ITIP_MAX_ATTENDEES 64

/** Personne (organisateur ou participant) d'une invitation. */
typedef struct {
	char email[256];	/**< adresse, sans le prefixe « mailto: » */
	char name[128];		/**< nom affiche (parametre CN) */
	char partstat[24];	/**< PARTSTAT en majuscules (NEEDS-ACTION, ACCEPTED...), vide si absent */
	char role[24];		/**< ROLE en majuscules, vide si absent */
} EvoItipPerson;

/** Resume du premier VEVENT d'un document iCalendar. */
typedef struct {
	char method[16];	/**< METHOD en majuscules (REQUEST, CANCEL, REPLY...), vide si absent */
	char uid[256];
	int sequence;		/**< SEQUENCE, 0 si absente */
	char summary[256];
	char location[256];
	char dtstart[40];	/**< valeur brute de DTSTART (ex. 20261105T090000Z) */
	char dtstart_tzid[96];	/**< parametre TZID de DTSTART, vide si absent */
	int dtstart_is_date;	/**< non nul si DTSTART est une date sans heure (journee entiere) */
	char dtend[40];
	char dtend_tzid[96];
	char rrule[200];	/**< regle de recurrence brute, vide si l'evenement n'est pas periodique */
	char recurrence_id[40];	/**< RECURRENCE-ID brut (occurrence isolee d'une serie), vide sinon */
	char status[24];	/**< STATUS en majuscules (CONFIRMED, CANCELLED...), vide si absent */
	EvoItipPerson organizer;
	int n_attendees;
	EvoItipPerson attendees[EVO_ITIP_MAX_ATTENDEES];
} EvoItip;

/** Decision a prendre sur l'agenda de l'utilisateur pour une invitation. */
typedef enum {
	EVO_ITIP_IGNORE = 0,	/**< rien a faire */
	EVO_ITIP_ADD,		/**< ajouter l'evenement */
	EVO_ITIP_UPDATE,	/**< remplacer l'evenement existant par la version recue */
	EVO_ITIP_KEEP,		/**< l'agenda a une version plus recente : ne changer que la reponse */
	EVO_ITIP_REMOVE		/**< retirer l'evenement de l'agenda */
} EvoItipAction;

/**
 * @brief Copie une chaine en limitant sa taille (terminaison garantie).
 * @param dst Tampon de destination.
 * @param size Taille du tampon.
 * @param src Source (NULL equivaut a "").
 */
static inline void
evo_itip_copy (char *dst,
	       size_t size,
	       const char *src)
{
	size_t len;

	if (!dst || size == 0)
		return;
	len = src ? strlen (src) : 0;
	if (len >= size)
		len = size - 1;
	if (len)
		memcpy (dst, src, len);
	dst[len] = '\0';
}

/**
 * @brief Compare deux chaines sans tenir compte de la casse ASCII.
 * @param a Premiere chaine.
 * @param b Seconde chaine.
 * @return 0 si egales, une valeur non nulle sinon.
 */
static inline int
evo_itip_casecmp (const char *a,
		  const char *b)
{
	for (; *a && *b; a++, b++) {
		if (tolower ((unsigned char) *a) != tolower ((unsigned char) *b))
			break;
	}
	return tolower ((unsigned char) *a) - tolower ((unsigned char) *b);
}

/**
 * @brief Compare les n premiers caracteres de deux chaines sans tenir compte de la casse ASCII.
 * @param a Premiere chaine.
 * @param b Seconde chaine.
 * @param n Nombre maximal de caracteres compares.
 * @return 0 si egaux, une valeur non nulle sinon.
 */
static inline int
evo_itip_casecmp_n (const char *a,
		    const char *b,
		    size_t n)
{
	for (; n > 0; n--, a++, b++) {
		int d = tolower ((unsigned char) *a) - tolower ((unsigned char) *b);

		if (d != 0 || !*a)
			return d;
	}
	return 0;
}

/**
 * @brief Lit la ligne logique suivante d'un document iCalendar (lignes repliees reunies).
 * @param cursor Position de lecture (avancee apres la ligne).
 * @param line Tampon recevant la ligne, sans fin de ligne ; tronquee si trop longue.
 * @param size Taille du tampon.
 * @return 1 si une ligne a ete lue, 0 en fin de document.
 */
static inline int
evo_itip_next_line (const char **cursor,
		    char *line,
		    size_t size)
{
	const char *p = *cursor;
	size_t len = 0;

	if (!p || !*p)
		return 0;

	for (;;) {
		while (*p && *p != '\r' && *p != '\n') {
			if (len + 1 < size)
				line[len++] = *p;
			p++;
		}
		if (*p == '\r')
			p++;
		if (*p == '\n')
			p++;
		/* ligne de continuation : commence par une espace ou une tabulation */
		if (*p == ' ' || *p == '\t') {
			p++;
			continue;
		}
		break;
	}

	line[len] = '\0';
	*cursor = p;
	return 1;
}

/**
 * @brief Cherche le separateur « : » entre nom/parametres et valeur (hors guillemets).
 * @param line Ligne de contenu.
 * @return Position du « : », ou NULL.
 */
static inline const char *
evo_itip_find_colon (const char *line)
{
	int quoted = 0;

	for (; *line; line++) {
		if (*line == '"')
			quoted = !quoted;
		else if (*line == ':' && !quoted)
			return line;
	}
	return NULL;
}

/**
 * @brief Extrait la valeur d'un parametre de propriete (ex. PARTSTAT, CN, TZID).
 * @param params Texte des parametres, commencant par « ; » (ex. ";CN=\"Marie, D\";ROLE=CHAIR").
 * @param params_len Longueur du texte des parametres.
 * @param name Nom du parametre recherche (insensible a la casse).
 * @param out Tampon recevant la valeur, sans guillemets.
 * @param size Taille du tampon.
 * @return 1 si le parametre est present, 0 sinon (out vaut alors "").
 */
static inline int
evo_itip_param (const char *params,
		size_t params_len,
		const char *name,
		char *out,
		size_t size)
{
	size_t name_len = strlen (name), pos = 0;

	if (size)
		out[0] = '\0';

	while (pos < params_len) {
		size_t start, end;
		int quoted = 0;

		if (params[pos] == ';')
			pos++;
		start = pos;
		/* fin de ce parametre : prochain « ; » hors guillemets */
		for (end = pos; end < params_len; end++) {
			if (params[end] == '"')
				quoted = !quoted;
			else if (params[end] == ';' && !quoted)
				break;
		}
		if (end - start > name_len && params[start + name_len] == '=' &&
		    evo_itip_casecmp_n (params + start, name, name_len) == 0) {
			size_t vstart = start + name_len + 1, vlen = end - vstart, ii, o = 0;

			for (ii = 0; ii < vlen && o + 1 < size; ii++) {
				if (params[vstart + ii] != '"')
					out[o++] = params[vstart + ii];
			}
			if (size)
				out[o] = '\0';
			return 1;
		}
		pos = end;
	}
	return 0;
}

/**
 * @brief Retire les espaces de debut et de fin d'une chaine (en place).
 * @param text Chaine a rogner.
 */
static inline void
evo_itip_trim (char *text)
{
	size_t len = strlen (text), start = 0;

	while (len > 0 && isspace ((unsigned char) text[len - 1]))
		text[--len] = '\0';
	while (text[start] && isspace ((unsigned char) text[start]))
		start++;
	if (start)
		memmove (text, text + start, strlen (text + start) + 1);
}

/**
 * @brief Decode une valeur TEXT iCalendar (\n, \, \; \\) en une seule ligne.
 * @param dst Tampon de sortie.
 * @param size Taille du tampon.
 * @param src Valeur brute.
 */
static inline void
evo_itip_unescape_text (char *dst,
			size_t size,
			const char *src)
{
	size_t o = 0;

	if (!size)
		return;
	for (; src && *src && o + 1 < size; src++) {
		if (*src == '\\' && src[1]) {
			src++;
			dst[o++] = (*src == 'n' || *src == 'N') ? ' ' : *src;
		} else if (*src == '\r' || *src == '\n') {
			dst[o++] = ' ';
		} else {
			dst[o++] = *src;
		}
	}
	dst[o] = '\0';
}

/**
 * @brief Copie une adresse en retirant le prefixe « mailto: » et les espaces.
 * @param dst Tampon de sortie.
 * @param size Taille du tampon.
 * @param value Valeur de la propriete ORGANIZER / ATTENDEE.
 */
static inline void
evo_itip_address (char *dst,
		  size_t size,
		  const char *value)
{
	while (*value && isspace ((unsigned char) *value))
		value++;
	if (evo_itip_casecmp_n (value, "mailto:", 7) == 0)
		value += 7;
	evo_itip_copy (dst, size, value);
	evo_itip_trim (dst);
}

/**
 * @brief Met une chaine en majuscules ASCII (en place).
 * @param text Chaine a convertir.
 */
static inline void
evo_itip_upper (char *text)
{
	for (; *text; text++)
		*text = (char) toupper ((unsigned char) *text);
}

/**
 * @brief Remplit une personne a partir d'une propriete ORGANIZER ou ATTENDEE.
 * @param person Personne a remplir.
 * @param params Texte des parametres (avec le « ; » initial).
 * @param params_len Longueur du texte des parametres.
 * @param value Valeur (adresse mailto:).
 */
static inline void
evo_itip_fill_person (EvoItipPerson *person,
		      const char *params,
		      size_t params_len,
		      const char *value)
{
	char raw[160];

	memset (person, 0, sizeof *person);
	evo_itip_address (person->email, sizeof person->email, value);
	if (evo_itip_param (params, params_len, "CN", raw, sizeof raw))
		evo_itip_unescape_text (person->name, sizeof person->name, raw);
	evo_itip_param (params, params_len, "PARTSTAT", person->partstat, sizeof person->partstat);
	evo_itip_param (params, params_len, "ROLE", person->role, sizeof person->role);
	evo_itip_upper (person->partstat);
	evo_itip_upper (person->role);
}

/**
 * @brief Lit le resume du premier VEVENT d'un document iCalendar.
 *
 * Les proprietes des composants imbriques (VALARM) et des VTIMEZONE sont ignorees.
 * METHOD est lue au niveau VCALENDAR.
 * @param ics Texte du document iCalendar.
 * @param out Resume a remplir (remis a zero).
 * @return 1 si un VEVENT portant un UID a ete trouve, 0 sinon.
 */
static inline int
evo_itip_parse (const char *ics,
		EvoItip *out)
{
	char line[2048], name[48], text[64];
	const char *cursor = ics;
	int depth = 0, event_depth = 0, done = 0, found = 0;

	memset (out, 0, sizeof *out);
	if (!ics)
		return 0;

	while (!done && evo_itip_next_line (&cursor, line, sizeof line)) {
		const char *colon = evo_itip_find_colon (line), *params, *value;
		size_t name_len, params_len, ii;

		if (!colon)
			continue;

		/* nom de la propriete, puis texte des parametres a partir du premier « ; » */
		params = memchr (line, ';', (size_t) (colon - line));
		name_len = (size_t) ((params ? params : colon) - line);
		if (name_len == 0 || name_len >= sizeof name)
			continue;
		for (ii = 0; ii < name_len; ii++)
			name[ii] = (char) toupper ((unsigned char) line[ii]);
		name[name_len] = '\0';
		params_len = params ? (size_t) (colon - params) : 0;
		if (!params)
			params = "";
		value = colon + 1;

		if (strcmp (name, "BEGIN") == 0) {
			depth++;
			if (!event_depth && !found && evo_itip_casecmp (value, "VEVENT") == 0)
				event_depth = depth;
			continue;
		}
		if (strcmp (name, "END") == 0) {
			if (event_depth && depth == event_depth) {
				done = 1;
				found = out->uid[0] != '\0';
			}
			depth--;
			continue;
		}

		if (!event_depth) {
			if (strcmp (name, "METHOD") == 0 && depth <= 1) {
				evo_itip_copy (out->method, sizeof out->method, value);
				evo_itip_trim (out->method);
				evo_itip_upper (out->method);
			}
			continue;
		}
		if (depth != event_depth)
			continue;

		if (strcmp (name, "UID") == 0) {
			evo_itip_copy (out->uid, sizeof out->uid, value);
			evo_itip_trim (out->uid);
		} else if (strcmp (name, "SEQUENCE") == 0) {
			out->sequence = atoi (value);
		} else if (strcmp (name, "SUMMARY") == 0) {
			evo_itip_unescape_text (out->summary, sizeof out->summary, value);
		} else if (strcmp (name, "LOCATION") == 0) {
			evo_itip_unescape_text (out->location, sizeof out->location, value);
		} else if (strcmp (name, "DTSTART") == 0) {
			evo_itip_copy (out->dtstart, sizeof out->dtstart, value);
			evo_itip_trim (out->dtstart);
			evo_itip_param (params, params_len, "TZID", out->dtstart_tzid, sizeof out->dtstart_tzid);
			out->dtstart_is_date = strlen (out->dtstart) == 8 ||
				(evo_itip_param (params, params_len, "VALUE", text, sizeof text) && evo_itip_casecmp (text, "DATE") == 0);
		} else if (strcmp (name, "DTEND") == 0) {
			evo_itip_copy (out->dtend, sizeof out->dtend, value);
			evo_itip_trim (out->dtend);
			evo_itip_param (params, params_len, "TZID", out->dtend_tzid, sizeof out->dtend_tzid);
		} else if (strcmp (name, "RRULE") == 0) {
			evo_itip_copy (out->rrule, sizeof out->rrule, value);
		} else if (strcmp (name, "RECURRENCE-ID") == 0) {
			evo_itip_copy (out->recurrence_id, sizeof out->recurrence_id, value);
			evo_itip_trim (out->recurrence_id);
		} else if (strcmp (name, "STATUS") == 0) {
			evo_itip_copy (out->status, sizeof out->status, value);
			evo_itip_trim (out->status);
			evo_itip_upper (out->status);
		} else if (strcmp (name, "ORGANIZER") == 0) {
			evo_itip_fill_person (&out->organizer, params, params_len, value);
		} else if (strcmp (name, "ATTENDEE") == 0 && out->n_attendees < EVO_ITIP_MAX_ATTENDEES) {
			evo_itip_fill_person (&out->attendees[out->n_attendees++], params, params_len, value);
		}
	}

	return found;
}

/**
 * @brief Cherche une personne parmi les participants par son adresse.
 * @param itip Resume de l'invitation.
 * @param email Adresse recherchee (insensible a la casse).
 * @return Indice du participant, ou -1 s'il n'est pas dans la liste.
 */
static inline int
evo_itip_find_attendee (const EvoItip *itip,
			const char *email)
{
	int ii;

	if (!email || !*email)
		return -1;
	for (ii = 0; ii < itip->n_attendees; ii++) {
		if (evo_itip_casecmp (itip->attendees[ii].email, email) == 0)
			return ii;
	}
	return -1;
}

/**
 * @brief Convertit une reponse de l'interface en valeur PARTSTAT.
 * @param response "accepted", "declined" ou "tentative".
 * @return "ACCEPTED", "DECLINED", "TENTATIVE", ou NULL si la reponse est inconnue.
 */
static inline const char *
evo_itip_partstat_from_response (const char *response)
{
	if (!response)
		return NULL;
	if (evo_itip_casecmp (response, "accepted") == 0)
		return "ACCEPTED";
	if (evo_itip_casecmp (response, "declined") == 0)
		return "DECLINED";
	if (evo_itip_casecmp (response, "tentative") == 0)
		return "TENTATIVE";
	return NULL;
}

/**
 * @brief Convertit une valeur PARTSTAT en reponse de l'interface.
 * @param partstat Valeur PARTSTAT (insensible a la casse).
 * @return "accepted", "declined", "tentative" ou "needs-action" (valeur par defaut).
 */
static inline const char *
evo_itip_response_from_partstat (const char *partstat)
{
	if (partstat && evo_itip_casecmp (partstat, "ACCEPTED") == 0)
		return "accepted";
	if (partstat && evo_itip_casecmp (partstat, "DECLINED") == 0)
		return "declined";
	if (partstat && evo_itip_casecmp (partstat, "TENTATIVE") == 0)
		return "tentative";
	return "needs-action";
}

/**
 * @brief Decide quoi faire dans l'agenda de l'utilisateur (evite les doublons).
 *
 * - CANCEL : retire l'evenement s'il existe et si l'annulation n'est pas perimee
 *   (SEQUENCE inferieure a celle de l'agenda).
 * - REQUEST refuse : retire l'evenement s'il existe, sinon rien.
 * - REQUEST accepte / provisoire : ajoute si absent ; remplace si la version recue
 *   est au moins aussi recente ; sinon conserve la version de l'agenda.
 * @param method METHOD de l'invitation ("REQUEST", "CANCEL", ...).
 * @param has_existing Non nul si l'evenement (meme UID) est deja dans l'agenda.
 * @param existing_sequence SEQUENCE de l'evenement de l'agenda.
 * @param incoming_sequence SEQUENCE de l'invitation recue.
 * @param declined Non nul si l'utilisateur refuse l'invitation.
 * @return Action a effectuer.
 */
static inline EvoItipAction
evo_itip_decide (const char *method,
		 int has_existing,
		 int existing_sequence,
		 int incoming_sequence,
		 int declined)
{
	if (method && evo_itip_casecmp (method, "CANCEL") == 0)
		return (has_existing && incoming_sequence >= existing_sequence) ? EVO_ITIP_REMOVE : EVO_ITIP_IGNORE;

	if (!method || evo_itip_casecmp (method, "REQUEST") != 0)
		return EVO_ITIP_IGNORE;

	if (declined)
		return has_existing ? EVO_ITIP_REMOVE : EVO_ITIP_IGNORE;
	if (!has_existing)
		return EVO_ITIP_ADD;
	return incoming_sequence >= existing_sequence ? EVO_ITIP_UPDATE : EVO_ITIP_KEEP;
}

/**
 * @brief Echappe une valeur TEXT iCalendar (barre oblique inverse, point-virgule, virgule, saut de ligne).
 * @param dst Tampon de sortie.
 * @param size Taille du tampon.
 * @param src Valeur a echapper.
 */
static inline void
evo_itip_escape_text (char *dst,
		      size_t size,
		      const char *src)
{
	size_t o = 0;

	if (!size)
		return;
	for (; src && *src; src++) {
		if (*src == '\r')
			continue;
		if (*src == '\\' || *src == ';' || *src == ',' || *src == '\n') {
			if (o + 2 >= size)
				break;
			dst[o++] = '\\';
			dst[o++] = *src == '\n' ? 'n' : *src;
		} else {
			if (o + 1 >= size)
				break;
			dst[o++] = *src;
		}
	}
	dst[o] = '\0';
}

/**
 * @brief Ajoute une ligne de contenu au document en la repliant a 75 octets (RFC 5545 §3.1).
 *
 * Le repli ne coupe jamais un caractere UTF-8 multi-octets.
 * @param buf Tampon du document.
 * @param size Taille du tampon.
 * @param len Longueur courante du document (mise a jour).
 * @param text Ligne sans fin de ligne.
 * @return 0 si succes, -1 si le tampon est trop petit.
 */
static inline int
evo_itip_append_line (char *buf,
		      size_t size,
		      size_t *len,
		      const char *text)
{
	size_t pos = 0, total = strlen (text), limit = 75;

	if (total == 0)
		return 0;
	while (pos < total) {
		size_t chunk = total - pos;

		if (chunk > limit) {
			chunk = limit;
			/* ne pas couper au milieu d'un caractere UTF-8 */
			while (chunk > 1 && (((unsigned char) text[pos + chunk]) & 0xC0) == 0x80)
				chunk--;
		}
		if (*len + chunk + 3 + (pos ? 1 : 0) >= size)
			return -1;
		if (pos)
			buf[(*len)++] = ' ';
		memcpy (buf + *len, text + pos, chunk);
		*len += chunk;
		buf[(*len)++] = '\r';
		buf[(*len)++] = '\n';
		buf[*len] = '\0';
		pos += chunk;
		limit = 74;	/* l'espace de continuation compte dans les 75 octets */
	}
	return 0;
}

/**
 * @brief Prepare la valeur d'un parametre CN (guillemets si necessaire, sans caractere de controle).
 * @param dst Tampon de sortie.
 * @param size Taille du tampon.
 * @param name Nom brut.
 */
static inline void
evo_itip_quote_param (char *dst,
		      size_t size,
		      const char *name)
{
	char clean[160];
	size_t o = 0;

	for (; name && *name && o + 1 < sizeof clean; name++) {
		if ((unsigned char) *name >= 0x20 && *name != '"')
			clean[o++] = *name;
	}
	clean[o] = '\0';
	if (strpbrk (clean, ",;:"))
		snprintf (dst, size, "\"%s\"", clean);
	else
		evo_itip_copy (dst, size, clean);
}

/**
 * @brief Construit le document iCalendar d'une reponse (iTIP METHOD:REPLY, RFC 5546 §3.2.3).
 *
 * Le document contient un VEVENT avec UID, SEQUENCE et RECURRENCE-ID de l'invitation, DTSTAMP,
 * ORGANIZER et un seul ATTENDEE portant le PARTSTAT choisi. DTSTART n'est repris que s'il ne
 * depend pas d'un fuseau horaire (le document n'embarque pas de VTIMEZONE).
 * @param buf Tampon de sortie (fins de ligne CRLF).
 * @param size Taille du tampon.
 * @param itip Invitation recue.
 * @param attendee_email Adresse de l'utilisateur qui repond.
 * @param attendee_name Nom de l'utilisateur ("" si inconnu).
 * @param partstat "ACCEPTED", "DECLINED" ou "TENTATIVE".
 * @param dtstamp Horodatage UTC au format 20261105T090000Z.
 * @return Longueur ecrite, ou -1 si les arguments sont invalides ou le tampon trop petit.
 */
static inline int
evo_itip_format_reply (char *buf,
		       size_t size,
		       const EvoItip *itip,
		       const char *attendee_email,
		       const char *attendee_name,
		       const char *partstat,
		       const char *dtstamp)
{
	char line[1024], text[600], cn[200], safe[300];
	size_t len = 0;

	if (!buf || size == 0 || !itip || !itip->uid[0] || !itip->organizer.email[0] ||
	    !attendee_email || !*attendee_email || !partstat || !*partstat || !dtstamp)
		return -1;
	buf[0] = '\0';

#define EVO_ITIP_ADD_LINE(...) do { \
	snprintf (line, sizeof line, __VA_ARGS__); \
	if (evo_itip_append_line (buf, size, &len, line) < 0) return -1; \
} while (0)

	EVO_ITIP_ADD_LINE ("BEGIN:VCALENDAR");
	EVO_ITIP_ADD_LINE ("PRODID:-//EvolutionWin//Invitations//FR");
	EVO_ITIP_ADD_LINE ("VERSION:2.0");
	EVO_ITIP_ADD_LINE ("METHOD:REPLY");
	EVO_ITIP_ADD_LINE ("BEGIN:VEVENT");
	evo_mdn_sanitize_value (safe, sizeof safe, itip->uid);
	EVO_ITIP_ADD_LINE ("UID:%s", safe);
	if (itip->recurrence_id[0]) {
		evo_mdn_sanitize_value (safe, sizeof safe, itip->recurrence_id);
		EVO_ITIP_ADD_LINE ("RECURRENCE-ID:%s", safe);
	}
	EVO_ITIP_ADD_LINE ("SEQUENCE:%d", itip->sequence);
	evo_mdn_sanitize_value (safe, sizeof safe, dtstamp);
	EVO_ITIP_ADD_LINE ("DTSTAMP:%s", safe);
	if (itip->dtstart[0] && !itip->dtstart_tzid[0]) {
		evo_mdn_sanitize_value (safe, sizeof safe, itip->dtstart);
		if (itip->dtstart_is_date)
			EVO_ITIP_ADD_LINE ("DTSTART;VALUE=DATE:%s", safe);
		else
			EVO_ITIP_ADD_LINE ("DTSTART:%s", safe);
	}

	evo_itip_quote_param (cn, sizeof cn, itip->organizer.name);
	evo_mdn_sanitize_value (safe, sizeof safe, itip->organizer.email);
	if (cn[0])
		EVO_ITIP_ADD_LINE ("ORGANIZER;CN=%s:mailto:%s", cn, safe);
	else
		EVO_ITIP_ADD_LINE ("ORGANIZER:mailto:%s", safe);

	evo_itip_quote_param (cn, sizeof cn, attendee_name);
	evo_mdn_sanitize_value (safe, sizeof safe, attendee_email);
	evo_mdn_sanitize_value (text, sizeof text, partstat);
	if (cn[0])
		EVO_ITIP_ADD_LINE ("ATTENDEE;PARTSTAT=%s;CN=%s:mailto:%s", text, cn, safe);
	else
		EVO_ITIP_ADD_LINE ("ATTENDEE;PARTSTAT=%s:mailto:%s", text, safe);

	evo_itip_escape_text (text, sizeof text, itip->summary);
	EVO_ITIP_ADD_LINE ("SUMMARY:%s", text);
	EVO_ITIP_ADD_LINE ("END:VEVENT");
	EVO_ITIP_ADD_LINE ("END:VCALENDAR");

#undef EVO_ITIP_ADD_LINE

	return (int) len;
}

/**
 * @brief Objet du message de reponse (« Accepté : titre », « Refusé : titre », « Provisoire : titre »).
 * @param buf Tampon de sortie.
 * @param size Taille du tampon.
 * @param partstat "ACCEPTED", "DECLINED" ou "TENTATIVE".
 * @param summary Titre de la reunion.
 */
static inline void
evo_itip_reply_subject (char *buf,
			size_t size,
			const char *partstat,
			const char *summary)
{
	const char *prefix = "Provisoire";
	char clean[300];

	if (partstat && evo_itip_casecmp (partstat, "ACCEPTED") == 0)
		prefix = "Accept\xC3\xA9";
	else if (partstat && evo_itip_casecmp (partstat, "DECLINED") == 0)
		prefix = "Refus\xC3\xA9";
	evo_mdn_sanitize_value (clean, sizeof clean, summary);
	snprintf (buf, size, "%s : %s", prefix, clean);
}

/**
 * @brief Corps lisible (francais puis anglais) du message de reponse.
 * @param buf Tampon de sortie.
 * @param size Taille du tampon.
 * @param who Nom ou adresse de la personne qui repond.
 * @param partstat "ACCEPTED", "DECLINED" ou "TENTATIVE".
 * @param summary Titre de la reunion.
 * @param when Date et heure lisibles de la reunion ("" si inconnues).
 */
static inline void
evo_itip_reply_body (char *buf,
		     size_t size,
		     const char *who,
		     const char *partstat,
		     const char *summary,
		     const char *when)
{
	const char *fr = "a r\xC3\xA9pondu \xC3\xA0";
	const char *en = "has responded to";

	if (partstat && evo_itip_casecmp (partstat, "ACCEPTED") == 0) {
		fr = "a accept\xC3\xA9";
		en = "has accepted";
	} else if (partstat && evo_itip_casecmp (partstat, "DECLINED") == 0) {
		fr = "a refus\xC3\xA9";
		en = "has declined";
	} else if (partstat && evo_itip_casecmp (partstat, "TENTATIVE") == 0) {
		fr = "a r\xC3\xA9pondu \xC2\xAB provisoirement \xC2\xBB \xC3\xA0";
		en = "has tentatively accepted";
	}
	snprintf (buf, size,
		"%s %s l'invitation \xC2\xAB %s \xC2\xBB%s%s.\r\n\r\n"
		"%s %s the invitation \"%s\"%s%s.\r\n",
		who, fr, summary, *when ? " du " : "", when,
		who, en, summary, *when ? " on " : "", when);
}

#endif /* EVO_CORE_ITIP_H */
