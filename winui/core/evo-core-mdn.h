/*
 * evo-core : fonctions pures (C standard, sans GLib ni Camel) de construction
 * des notifications de disposition de message (MDN, RFC 8098). Partagées par le
 * moteur (evo-core-send.c) et par les tests unitaires (tests/unit).
 */

#ifndef EVO_CORE_MDN_H
#define EVO_CORE_MDN_H

#include <stdio.h>
#include <string.h>

/**
 * @brief Copie une valeur d'en-tête en une seule ligne (anti-injection d'en-têtes).
 *
 * Les retours chariot et sauts de ligne sont remplacés par des espaces ; la
 * sortie est toujours terminée par un zéro, même tronquée.
 * @param dst Tampon de destination.
 * @param size Taille du tampon de destination (octets).
 * @param src Valeur source (NULL équivaut à une chaîne vide).
 */
static void
evo_mdn_sanitize_value (char *dst,
			size_t size,
			const char *src)
{
	size_t ii = 0;

	if (!dst || size == 0)
		return;

	for (; src && *src && ii + 1 < size; src++, ii++)
		dst[ii] = (*src == '\r' || *src == '\n') ? ' ' : *src;
	dst[ii] = '\0';
}

/**
 * @brief Écrit les champs du bloc message/disposition-notification (RFC 8098 §3.2).
 *
 * Champs produits : Reporting-UA, Final-Recipient, Original-Message-ID (si
 * connu) et Disposition. L'envoi étant décidé par l'utilisateur, le mode est
 * « manual-action/MDN-sent-manually ».
 * @param buf Tampon de sortie.
 * @param size Taille du tampon (octets).
 * @param reporting_ua Nom de l'agent utilisateur (ex. « EvolutionWin »).
 * @param final_recipient Adresse du destinataire final (celui qui a lu).
 * @param original_message_id Identifiant du message lu, avec chevrons ("" si inconnu).
 * @param disposition_type Type : "displayed", "deleted", "dispatched", "denied"...
 * @return Nombre d'octets écrits (hors zéro final), ou -1 si le tampon est trop petit.
 */
static int
evo_mdn_format_fields (char *buf,
		       size_t size,
		       const char *reporting_ua,
		       const char *final_recipient,
		       const char *original_message_id,
		       const char *disposition_type)
{
	char ua[256], recipient[512], msgid[512];
	int written;

	evo_mdn_sanitize_value (ua, sizeof ua, reporting_ua && *reporting_ua ? reporting_ua : "EvolutionWin");
	evo_mdn_sanitize_value (recipient, sizeof recipient, final_recipient);
	evo_mdn_sanitize_value (msgid, sizeof msgid, original_message_id);

	written = snprintf (buf, size,
		"Reporting-UA: %s\r\n"
		"Final-Recipient: rfc822; %s\r\n"
		"%s%s%s"
		"Disposition: manual-action/MDN-sent-manually; %s\r\n",
		ua, recipient,
		*msgid ? "Original-Message-ID: " : "", msgid, *msgid ? "\r\n" : "",
		disposition_type && *disposition_type ? disposition_type : "displayed");

	return (written < 0 || (size_t) written >= size) ? -1 : written;
}

#endif /* EVO_CORE_MDN_H */
