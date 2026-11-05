/*
 * evo-core : fonctions pures (sans GLib) des réponses automatiques d'absence :
 * conversion des dates ISO 8601 UTC, correspondance des audiences externes
 * et validation de la plage de dates. Fichier d'en-tête autonome, inclus par
 * evo-core-autoreply.c et par les tests unitaires (tests/unit).
 */

#ifndef EVO_CORE_AUTOREPLY_UTIL_H
#define EVO_CORE_AUTOREPLY_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Codes de retour de autoreply_validate_range () */
#define AUTOREPLY_RANGE_OK		0
#define AUTOREPLY_RANGE_END_BEFORE_START	1
#define AUTOREPLY_RANGE_INCOMPLETE	2

/**
 * @brief Nombre de jours entre le 1er janvier 1970 et une date civile (calendrier grégorien).
 * @param y année
 * @param m mois (1 à 12)
 * @param d jour (1 à 31)
 * @return nombre de jours (négatif avant 1970)
 */
static inline int64_t
autoreply_days_from_civil (int y,
			   unsigned m,
			   unsigned d)
{
	int64_t era;
	unsigned yoe, doy, doe;

	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (unsigned) (y - era * 400);
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return era * 146097 + (int64_t) doe - 719468;
}

/**
 * @brief Date civile correspondant à un nombre de jours depuis le 1er janvier 1970.
 * @param z nombre de jours
 * @param y reçoit l'année
 * @param m reçoit le mois (1 à 12)
 * @param d reçoit le jour (1 à 31)
 */
static inline void
autoreply_civil_from_days (int64_t z,
			   int *y,
			   unsigned *m,
			   unsigned *d)
{
	int64_t era;
	unsigned doe, yoe, doy, mp;

	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = (unsigned) (z - era * 146097);
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp < 10 ? mp + 3 : mp - 9;
	*y = (int) (yoe + era * 400 + (*m <= 2));
}

/**
 * @brief Convertit une date ISO 8601 UTC ("2026-11-04T07:30:00", fraction de seconde ignorée) en secondes Unix.
 * @param iso chaîne ISO 8601 (peut se terminer par « Z » ou une fraction)
 * @return secondes Unix, 0 si la chaîne est vide, invalide ou antérieure à 1971
 */
static inline int64_t
autoreply_epoch_from_iso_utc (const char *iso)
{
	int y, mo, d, h = 0, mi = 0, s = 0, n;
	int64_t t;

	if (!iso || !*iso)
		return 0;

	n = sscanf (iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s);
	if (n < 3 || y < 1971 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
	    h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60)
		return 0;

	t = autoreply_days_from_civil (y, (unsigned) mo, (unsigned) d) * 86400 + h * 3600 + mi * 60 + s;
	return t > 0 ? t : 0;
}

/**
 * @brief Formate des secondes Unix en date ISO 8601 UTC à 7 décimales (format de Microsoft Graph).
 * @param t secondes Unix
 * @param buf tampon de sortie (au moins 32 octets)
 * @param size taille du tampon
 */
static inline void
autoreply_iso_from_epoch_utc (int64_t t,
			      char *buf,
			      size_t size)
{
	int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
	int64_t rest = t - days * 86400;
	int y;
	unsigned m, d;

	autoreply_civil_from_days (days, &y, &m, &d);
	snprintf (buf, size, "%04d-%02u-%02uT%02d:%02d:%02d.0000000",
		y, m, d, (int) (rest / 3600), (int) (rest % 3600 / 60), (int) (rest % 60));
}

/**
 * @brief Indique si une audience externe est une valeur connue.
 * @param audience "none", "known" ou "all"
 * @return 1 si valide, 0 sinon
 */
static inline int
autoreply_audience_valid (const char *audience)
{
	return audience && (!strcmp (audience, "none") || !strcmp (audience, "known") || !strcmp (audience, "all"));
}

/**
 * @brief Audience externe de l'interface vers la valeur Microsoft Graph (externalAudience).
 * @param audience "none", "known" ou "all"
 * @return "none", "contactsOnly" ou "all" ("all" par défaut)
 */
static inline const char *
autoreply_audience_to_graph (const char *audience)
{
	if (audience && !strcmp (audience, "none"))
		return "none";
	if (audience && !strcmp (audience, "known"))
		return "contactsOnly";
	return "all";
}

/**
 * @brief Valeur Microsoft Graph (externalAudience) vers l'audience externe de l'interface.
 * @param graph "none", "contactsOnly" ou "all"
 * @return "none", "known" ou "all" ("all" par défaut)
 */
static inline const char *
autoreply_audience_from_graph (const char *graph)
{
	if (graph && !strcmp (graph, "none"))
		return "none";
	if (graph && !strcmp (graph, "contactsOnly"))
		return "known";
	return "all";
}

/**
 * @brief Audience externe de l'interface vers les options Gmail restrictToContacts / restrictToDomain.
 * @param audience "none" (domaine seulement), "known" (contacts) ou "all"
 * @param contacts reçoit 1 si restrictToContacts
 * @param domain reçoit 1 si restrictToDomain
 */
static inline void
autoreply_audience_to_gmail (const char *audience,
			     int *contacts,
			     int *domain)
{
	*contacts = audience && !strcmp (audience, "known");
	*domain = audience && !strcmp (audience, "none");
}

/**
 * @brief Options Gmail restrictToContacts / restrictToDomain vers l'audience externe de l'interface.
 * @param contacts valeur de restrictToContacts
 * @param domain valeur de restrictToDomain
 * @return "known" si contacts, "none" si domaine seulement, "all" sinon
 */
static inline const char *
autoreply_audience_from_gmail (int contacts,
			       int domain)
{
	if (contacts)
		return "known";
	return domain ? "none" : "all";
}

/**
 * @brief Valide la plage de dates d'une réponse automatique.
 * @param start début en secondes Unix (0 = sans limite)
 * @param end fin en secondes Unix (0 = sans limite)
 * @param require_both 1 si le fournisseur exige les deux bornes ou aucune (Microsoft 365)
 * @return AUTOREPLY_RANGE_OK, AUTOREPLY_RANGE_END_BEFORE_START ou AUTOREPLY_RANGE_INCOMPLETE
 */
static inline int
autoreply_validate_range (int64_t start,
			  int64_t end,
			  int require_both)
{
	if (start > 0 && end > 0 && end <= start)
		return AUTOREPLY_RANGE_END_BEFORE_START;
	if (require_both && ((start > 0) != (end > 0)))
		return AUTOREPLY_RANGE_INCOMPLETE;
	return AUTOREPLY_RANGE_OK;
}

#endif /* EVO_CORE_AUTOREPLY_UTIL_H */
