/*
 * evo-core : fonctions pures (sans GLib) des règles côté serveur par ManageSieve (RFC 5804) :
 * mise entre guillemets, commande PUTSCRIPT avec chaîne littérale, authentification PLAIN
 * (SASL, RFC 4616) en base64, analyse des lignes de réponse. Fichier d'en-tête autonome, inclus
 * par evo-core-rules.c et par les tests unitaires (tests/unit/test_rules.cpp).
 */

#ifndef EVO_CORE_RULES_UTIL_H
#define EVO_CORE_RULES_UTIL_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Classes de réponse ManageSieve renvoyées par rules_sieve_response_class () */
#define RULES_SIEVE_RESPONSE_OTHER	0	/* ligne de donnée (capacité, chaîne littérale...) */
#define RULES_SIEVE_RESPONSE_OK		1
#define RULES_SIEVE_RESPONSE_NO		2
#define RULES_SIEVE_RESPONSE_BYE	3

/**
 * @brief Écrit une chaîne ManageSieve entre guillemets (échappement de « " » et « \ »).
 * @param text chaîne à citer
 * @param out tampon de sortie
 * @param size taille du tampon
 * @return nombre de caractères écrits (hors NUL), 0 si le tampon est trop petit
 */
static inline size_t
rules_sieve_quote (const char *text,
		   char *out,
		   size_t size)
{
	size_t used = 0;

	if (!out || size < 3)
		return 0;
	out[used++] = '"';
	for (; text && *text; text++) {
		size_t needed = (*text == '"' || *text == '\\') ? 2 : 1;

		if (used + needed + 2 > size) {
			out[0] = '\0';
			return 0;
		}
		if (needed == 2)
			out[used++] = '\\';
		out[used++] = *text;
	}
	out[used++] = '"';
	out[used] = '\0';
	return used;
}

/**
 * @brief Écrit la commande « PUTSCRIPT "nom" {taille+} » suivie du script et de la fin de ligne.
 *
 * Le script est transmis en chaîne littérale non synchronisante (« {n+} », RFC 5804 §2.4 avec
 * l'extension LITERAL+ implicite de ManageSieve) : aucun échappement du contenu n'est nécessaire.
 *
 * @param name nom du script
 * @param script contenu du script (UTF-8)
 * @param out tampon de sortie
 * @param size taille du tampon
 * @return nombre d'octets écrits, 0 si le tampon est trop petit
 */
static inline size_t
rules_sieve_putscript_command (const char *name,
			       const char *script,
			       char *out,
			       size_t size)
{
	char quoted[256];
	size_t quoted_len, script_len, head;
	int written;

	if (!name || !script || !out)
		return 0;
	quoted_len = rules_sieve_quote (name, quoted, sizeof (quoted));
	if (!quoted_len)
		return 0;
	script_len = strlen (script);
	written = snprintf (out, size, "PUTSCRIPT %s {%lu+}\r\n", quoted, (unsigned long) script_len);
	if (written < 0 || (size_t) written >= size)
		return 0;
	head = (size_t) written;
	if (head + script_len + 3 > size)
		return 0;
	memcpy (out + head, script, script_len);
	memcpy (out + head + script_len, "\r\n", 3);	/* avec le NUL final */
	return head + script_len + 2;
}

/**
 * @brief Encode des octets en base64 (alphabet standard, avec remplissage « = »).
 * @param data octets à encoder
 * @param length nombre d'octets
 * @param out tampon de sortie
 * @param size taille du tampon
 * @return nombre de caractères écrits (hors NUL), 0 si le tampon est trop petit
 */
static inline size_t
rules_base64_encode (const unsigned char *data,
		     size_t length,
		     char *out,
		     size_t size)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t i, used = 0;

	if (!out || (length && !data) || size < ((length + 2) / 3) * 4 + 1)
		return 0;
	for (i = 0; i < length; i += 3) {
		unsigned long chunk = (unsigned long) data[i] << 16;
		size_t remaining = length - i;

		if (remaining > 1)
			chunk |= (unsigned long) data[i + 1] << 8;
		if (remaining > 2)
			chunk |= data[i + 2];
		out[used++] = alphabet[(chunk >> 18) & 63];
		out[used++] = alphabet[(chunk >> 12) & 63];
		out[used++] = remaining > 1 ? alphabet[(chunk >> 6) & 63] : '=';
		out[used++] = remaining > 2 ? alphabet[chunk & 63] : '=';
	}
	out[used] = '\0';
	return used;
}

/**
 * @brief Écrit la commande « AUTHENTICATE "PLAIN" "<base64>" » (identifiant vide, utilisateur, mot de passe).
 * @param user nom d'utilisateur
 * @param password mot de passe
 * @param out tampon de sortie
 * @param size taille du tampon
 * @return nombre d'octets écrits, 0 si le tampon est trop petit ou si l'identité est vide
 */
static inline size_t
rules_sieve_authenticate_plain (const char *user,
				const char *password,
				char *out,
				size_t size)
{
	unsigned char raw[512];
	char encoded[720];
	size_t user_len, pass_len, raw_len;
	int written;

	if (!user || !*user || !password)
		return 0;
	user_len = strlen (user);
	pass_len = strlen (password);
	if (user_len + pass_len + 2 > sizeof (raw))
		return 0;
	raw_len = 0;
	raw[raw_len++] = 0;
	memcpy (raw + raw_len, user, user_len);
	raw_len += user_len;
	raw[raw_len++] = 0;
	memcpy (raw + raw_len, password, pass_len);
	raw_len += pass_len;
	if (!rules_base64_encode (raw, raw_len, encoded, sizeof (encoded)))
		return 0;
	written = snprintf (out, size, "AUTHENTICATE \"PLAIN\" \"%s\"\r\n", encoded);
	return (written < 0 || (size_t) written >= size) ? 0 : (size_t) written;
}

/**
 * @brief Classe une ligne de réponse ManageSieve (« OK », « NO », « BYE » en début de ligne, sans tenir compte de la casse).
 * @param line ligne reçue (sans fin de ligne)
 * @return RULES_SIEVE_RESPONSE_OK / _NO / _BYE, sinon RULES_SIEVE_RESPONSE_OTHER
 */
static inline int
rules_sieve_response_class (const char *line)
{
	static const char *const words[] = { "OK", "NO", "BYE" };
	int ii;

	if (!line)
		return RULES_SIEVE_RESPONSE_OTHER;
	for (ii = 0; ii < 3; ii++) {
		size_t len = strlen (words[ii]);
		size_t jj;
		int same = 1;

		for (jj = 0; jj < len; jj++) {
			char ch = line[jj];

			if (ch >= 'a' && ch <= 'z')
				ch = (char) (ch - 'a' + 'A');
			if (ch != words[ii][jj]) {
				same = 0;
				break;
			}
		}
		if (same && (line[len] == '\0' || line[len] == ' '))
			return ii + 1;
	}
	return RULES_SIEVE_RESPONSE_OTHER;
}

/**
 * @brief Taille d'une chaîne littérale annoncée en fin de ligne (« {123} » ou « {123+} »).
 * @param line ligne reçue (sans fin de ligne)
 * @return taille annoncée, -1 si la ligne ne se termine pas par une annonce de littéral
 */
static inline long
rules_sieve_literal_size (const char *line)
{
	size_t len;
	long value = 0;
	size_t pos;
	int digits = 0;

	if (!line)
		return -1;
	len = strlen (line);
	if (len < 3 || line[len - 1] != '}')
		return -1;
	pos = len - 1;
	if (pos > 0 && line[pos - 1] == '+')
		pos--;
	while (pos > 0 && line[pos - 1] >= '0' && line[pos - 1] <= '9')
		pos--, digits++;
	if (!digits || pos == 0 || line[pos - 1] != '{')
		return -1;
	for (; line[pos] >= '0' && line[pos] <= '9'; pos++) {
		value = value * 10 + (line[pos] - '0');
		if (value > 100000000L)
			return -1;
	}
	return value;
}

#endif /* EVO_CORE_RULES_UTIL_H */
