/*
 * evo-core : fonctions pures de l'exportation et de l'importation du courrier
 * (mbox, EML, Maildir, profil Thunderbird) et des contacts (vCard).
 *
 * Fichier d'en-tête autonome (C pur, sans GLib ni Camel) inclus par
 * evo-core-mail-transfer.c et par les tests unitaires
 * (tests/unit/test_transfer.c).
 *
 * Format mbox : un message est précédé d'une ligne « From expéditeur date ».
 * Pour qu'une ligne du message commençant par « From » ne soit pas prise pour
 * un séparateur, l'écriture la protège par un « > » supplémentaire (variante
 * « mboxrd », réversible : toute ligne « >*From  » reçoit un « > » de plus) ;
 * la lecture retire un « > » de ces lignes (ce qui relit aussi correctement
 * les « mboxo » de Thunderbird).
 */

#ifndef EVO_CORE_TRANSFER_UTIL_H
#define EVO_CORE_TRANSFER_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Indicateurs d'un message importé (masque de bits) */
#define MT_FLAG_SEEN		0x01	/* lu */
#define MT_FLAG_REPLIED		0x02	/* répondu */
#define MT_FLAG_FLAGGED		0x04	/* marqué d'un drapeau */
#define MT_FLAG_DRAFT		0x08	/* brouillon */
#define MT_FLAG_DELETED		0x10	/* à ne pas importer (supprimé) */

/* Octets au plus d'un nom de fichier ou de dossier exporté */
#define MT_MAX_NAME_BYTES	120

/* ------------------------------------------------------------------ */
/* Chaînes                                                              */
/* ------------------------------------------------------------------ */

/** @brief Compare deux caractères ASCII sans tenir compte de la casse. */
static inline int
mt_lower (int c)
{
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/**
 * @brief Teste si une chaîne commence par un préfixe (casse ignorée, ASCII).
 * @param text   texte testé
 * @param len    longueur de text
 * @param prefix préfixe attendu
 * @return 1 si text commence par prefix
 */
static inline int
mt_starts_with_ci (const char *text, size_t len, const char *prefix)
{
	size_t ii, plen = strlen (prefix);

	if (len < plen)
		return 0;
	for (ii = 0; ii < plen; ii++) {
		if (mt_lower ((unsigned char) text[ii]) != mt_lower ((unsigned char) prefix[ii]))
			return 0;
	}
	return 1;
}

/**
 * @brief Teste si un nom de fichier se termine par une extension (casse ignorée).
 * @param name nom de fichier
 * @param ext  extension avec son point (« .eml »)
 * @return 1 si name se termine par ext (et est plus long qu'elle)
 */
static inline int
mt_has_ext (const char *name, const char *ext)
{
	size_t nlen = strlen (name), elen = strlen (ext), ii;

	if (nlen <= elen)
		return 0;
	for (ii = 0; ii < elen; ii++) {
		if (mt_lower ((unsigned char) name[nlen - elen + ii]) != mt_lower ((unsigned char) ext[ii]))
			return 0;
	}
	return 1;
}

/**
 * @brief Copie le nom sans son extension (dernier point), pour un nom de dossier.
 * @param name nom de fichier
 * @param out  tampon recevant le nom
 * @param size taille de out
 */
static inline void
mt_strip_ext (const char *name, char *out, size_t size)
{
	const char *dot = strrchr (name, '.');
	size_t len = (dot && dot != name) ? (size_t) (dot - name) : strlen (name);

	if (size == 0)
		return;
	if (len >= size)
		len = size - 1;
	memcpy (out, name, len);
	out[len] = '\0';
}

/**
 * @brief Rend un texte utilisable comme nom de fichier ou de dossier Windows.
 *
 * Remplace les caractères interdits (< > : " / \ | ? * et les contrôles) par
 * « _ », retire les espaces et points de fin, préfixe par « _ » les noms
 * réservés (CON, NUL, COM1…), borne la longueur à MT_MAX_NAME_BYTES sans
 * couper une séquence UTF-8, et utilise fallback si le résultat est vide.
 * @param raw      texte d'origine (peut être NULL)
 * @param fallback nom de repli
 * @param out      tampon recevant le nom
 * @param size     taille de out
 */
static inline void
mt_safe_name (const char *raw, const char *fallback, char *out, size_t size)
{
	size_t len = 0, ii;
	static const char *const reserved[] = { "CON", "PRN", "AUX", "NUL", NULL };

	if (size == 0)
		return;

	for (ii = 0; raw && raw[ii] && len < MT_MAX_NAME_BYTES && len + 1 < size; ii++) {
		unsigned char c = (unsigned char) raw[ii];

		if (c < 0x20 || strchr ("<>:\"/\\|?*", c))
			c = '_';
		out[len++] = (char) c;
	}
	/* ne pas couper une séquence UTF-8 en deux */
	if (raw && raw[ii] && len > 0) {
		while (len > 0 && ((unsigned char) out[len - 1] & 0xC0) == 0x80)
			len--;
		if (len > 0 && ((unsigned char) out[len - 1] & 0xC0) == 0xC0)
			len--;
	}
	/* espaces au début, espaces et points à la fin */
	while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '.'))
		len--;
	for (ii = 0; ii < len && out[ii] == ' '; ii++)
		;
	if (ii > 0) {
		memmove (out, out + ii, len - ii);
		len -= ii;
	}
	out[len] = '\0';

	if (len == 0) {
		snprintf (out, size, "%s", fallback && *fallback ? fallback : "_");
		return;
	}

	/* noms réservés (avec ou sans extension) */
	{
		char base[16];
		size_t blen = 0, jj;
		int is_reserved = 0;

		while (blen < sizeof (base) - 1 && out[blen] && out[blen] != '.') {
			base[blen] = (char) (out[blen] >= 'a' && out[blen] <= 'z' ? out[blen] - 32 : out[blen]);
			blen++;
		}
		base[blen] = '\0';
		if (out[blen] == '\0' || out[blen] == '.') {
			for (jj = 0; reserved[jj]; jj++)
				if (strcmp (base, reserved[jj]) == 0)
					is_reserved = 1;
			if (blen == 4 && (strncmp (base, "COM", 3) == 0 || strncmp (base, "LPT", 3) == 0) &&
			    base[3] >= '1' && base[3] <= '9')
				is_reserved = 1;
		}
		if (is_reserved && len + 1 < size) {
			memmove (out + 1, out, len + 1);
			out[0] = '_';
		}
	}
}

/* ------------------------------------------------------------------ */
/* Dates                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Teste si une date (secondes depuis 1970) est dans une plage.
 * @param t    date du message
 * @param from début inclus (0 : pas de borne)
 * @param to   fin incluse (0 : pas de borne)
 * @return 1 si le message est retenu
 */
static inline int
mt_in_range (int64_t t, int64_t from, int64_t to)
{
	if (from > 0 && t < from)
		return 0;
	if (to > 0 && t > to)
		return 0;
	return 1;
}

/**
 * @brief Décompose une date UTC (secondes depuis 1970) sans dépendre de gmtime.
 * @param t      secondes depuis le 1er janvier 1970 UTC
 * @param year   année
 * @param month  mois (1-12)
 * @param day    jour (1-31)
 * @param hour   heure
 * @param minute minute
 * @param second seconde
 * @param wday   jour de la semaine (0 : dimanche)
 */
static inline void
mt_civil (int64_t t, int *year, int *month, int *day, int *hour, int *minute, int *second, int *wday)
{
	int64_t days = t / 86400, rem = t % 86400, z, era, doe, yoe, doy, mp, y;

	if (rem < 0) {
		rem += 86400;
		days--;
	}
	*hour = (int) (rem / 3600);
	*minute = (int) (rem % 3600 / 60);
	*second = (int) (rem % 60);
	*wday = (int) (((days % 7) + 11) % 7);	/* le 1er janvier 1970 était un jeudi */

	z = days + 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*day = (int) (doy - (153 * mp + 2) / 5 + 1);
	*month = (int) (mp < 10 ? mp + 3 : mp - 9);
	*year = (int) (y + (*month <= 2));
}

/**
 * @brief Écrit la ligne séparatrice d'un message mbox : « From expéditeur Mon Jan  1 00:00:00 2024\n ».
 * @param sender adresse de l'expéditeur (espaces remplacés ; vide : MAILER-DAEMON)
 * @param t      date du message (secondes depuis 1970 UTC)
 * @param out    tampon recevant la ligne
 * @param size   taille de out
 */
static inline void
mt_from_line (const char *sender, int64_t t, char *out, size_t size)
{
	static const char *const days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	char addr[200];
	size_t len = 0, ii;
	int y, mo, d, h, mi, s, wd;

	for (ii = 0; sender && sender[ii] && len < sizeof (addr) - 1; ii++) {
		unsigned char c = (unsigned char) sender[ii];

		addr[len++] = (c <= ' ' || c == 0x7f) ? '_' : (char) c;
	}
	addr[len] = '\0';
	if (len == 0)
		snprintf (addr, sizeof (addr), "MAILER-DAEMON");

	mt_civil (t, &y, &mo, &d, &h, &mi, &s, &wd);
	snprintf (out, size, "From %s %s %s %2d %02d:%02d:%02d %d\n", addr, days[wd], months[mo - 1], d, h, mi, s, y);
}

/* ------------------------------------------------------------------ */
/* Format mbox                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Indique si une ligne ressemble à un séparateur mbox (« From adresse date… »).
 * @param line début de la ligne
 * @param len  longueur de la ligne, fin de ligne exclue
 * @return 1 si la ligne est un séparateur
 */
static inline int
mt_is_from_line (const char *line, size_t len)
{
	size_t ii = 5;
	int colon = 0;

	if (len < 12 || strncmp (line, "From ", 5) != 0)
		return 0;
	/* l'adresse : au moins un caractère non blanc */
	if (line[ii] == ' ')
		return 0;
	while (ii < len && line[ii] != ' ')
		ii++;
	if (ii >= len)
		return 0;
	/* la date : une heure « hh:mm » doit y figurer */
	for (; ii < len; ii++) {
		if (line[ii] == ':')
			colon = 1;
	}
	return colon;
}

/**
 * @brief Protège les lignes « From » d'un message pour l'écriture dans un mbox
 * (mboxrd) et termine le message par une ligne vide.
 * @param msg     message (RFC 822)
 * @param len     longueur du message
 * @param out_len longueur du résultat
 * @return tampon alloué avec malloc (à libérer avec free), ou NULL
 */
static inline char *
mt_mbox_quote (const char *msg, size_t len, size_t *out_len)
{
	size_t cap = len + len / 16 + 8, n = 0, pos = 0;
	char *out = (char *) malloc (cap);

	if (!out)
		return NULL;

	while (pos < len) {
		size_t eol = pos, gt = 0;
		int quote;

		while (eol < len && msg[eol] != '\n')
			eol++;
		while (pos + gt < eol && msg[pos + gt] == '>')
			gt++;
		quote = (eol - pos - gt >= 5 && strncmp (msg + pos + gt, "From ", 5) == 0);

		if (n + (eol - pos) + 4 > cap) {
			char *grown;

			cap = cap * 2 + (eol - pos) + 4;
			grown = (char *) realloc (out, cap);
			if (!grown) {
				free (out);
				return NULL;
			}
			out = grown;
		}
		if (quote)
			out[n++] = '>';
		memcpy (out + n, msg + pos, eol - pos);
		n += eol - pos;
		if (eol < len)
			out[n++] = '\n';
		pos = eol < len ? eol + 1 : eol;
	}
	/* fin de ligne finale, puis ligne vide séparatrice */
	if (n == 0 || out[n - 1] != '\n')
		out[n++] = '\n';
	out[n++] = '\n';
	out[n] = '\0';
	*out_len = n;
	return out;
}

/**
 * @brief Annule la protection des lignes « >*From » d'un message lu dans un mbox.
 * @param msg     message tel qu'écrit dans le mbox
 * @param len     longueur du message
 * @param out_len longueur du résultat
 * @return tampon alloué avec malloc (à libérer avec free), ou NULL
 */
static inline char *
mt_mbox_unquote (const char *msg, size_t len, size_t *out_len)
{
	size_t n = 0, pos = 0;
	char *out = (char *) malloc (len + 1);

	if (!out)
		return NULL;

	while (pos < len) {
		size_t eol = pos, gt = 0, copy_from = pos;

		while (eol < len && msg[eol] != '\n')
			eol++;
		while (pos + gt < eol && msg[pos + gt] == '>')
			gt++;
		if (gt > 0 && eol - pos - gt >= 5 && strncmp (msg + pos + gt, "From ", 5) == 0)
			copy_from = pos + 1;
		memcpy (out + n, msg + copy_from, eol - copy_from);
		n += eol - copy_from;
		if (eol < len)
			out[n++] = '\n';
		pos = eol < len ? eol + 1 : eol;
	}
	out[n] = '\0';
	*out_len = n;
	return out;
}

/** Lecteur séquentiel d'un fichier mbox en mémoire. */
typedef struct {
	const char *data;	/* contenu du fichier */
	size_t len;		/* taille du contenu */
	size_t pos;		/* début de la prochaine ligne à examiner */
} MtMboxReader;

/**
 * @brief Initialise un lecteur mbox.
 * @param reader lecteur
 * @param data   contenu du fichier
 * @param len    taille du contenu
 */
static inline void
mt_mbox_reader_init (MtMboxReader *reader, const char *data, size_t len)
{
	reader->data = data;
	reader->len = len;
	reader->pos = 0;
}

/**
 * @brief Extrait le message suivant d'un mbox (sans sa ligne « From » ni la ligne
 * vide qui le termine). Le texte rendu est encore protégé : le passer à
 * mt_mbox_unquote().
 * @param reader  lecteur
 * @param start   début du message dans les données
 * @param msg_len longueur du message
 * @return 1 si un message a été trouvé, 0 en fin de fichier
 */
static inline int
mt_mbox_next (MtMboxReader *reader, const char **start, size_t *msg_len)
{
	size_t pos = reader->pos, body = 0, end;
	int found = 0;

	/* cherche la première ligne séparatrice */
	while (pos < reader->len) {
		size_t eol = pos;

		while (eol < reader->len && reader->data[eol] != '\n')
			eol++;
		if (mt_is_from_line (reader->data + pos, eol > pos && reader->data[eol - 1] == '\r' ? eol - pos - 1 : eol - pos)) {
			body = eol < reader->len ? eol + 1 : eol;
			found = 1;
			break;
		}
		pos = eol < reader->len ? eol + 1 : eol;
	}
	if (!found) {
		reader->pos = reader->len;
		return 0;
	}

	/* le message se termine à la prochaine ligne séparatrice */
	pos = body;
	end = reader->len;
	while (pos < reader->len) {
		size_t eol = pos;

		while (eol < reader->len && reader->data[eol] != '\n')
			eol++;
		if (mt_is_from_line (reader->data + pos, eol > pos && reader->data[eol - 1] == '\r' ? eol - pos - 1 : eol - pos)) {
			end = pos;
			break;
		}
		pos = eol < reader->len ? eol + 1 : eol;
	}
	reader->pos = end;

	/* retire la ligne vide ajoutée entre deux messages */
	if (end - body >= 2 && reader->data[end - 1] == '\n' && reader->data[end - 2] == '\n')
		end -= 1;
	else if (end - body >= 3 && reader->data[end - 1] == '\n' && reader->data[end - 2] == '\r' &&
		 reader->data[end - 3] == '\n')
		end -= 2;
	*start = reader->data + body;
	*msg_len = end - body;
	return 1;
}

/**
 * @brief Teste si le début d'un fichier est celui d'un mbox.
 * @param data début du fichier
 * @param len  nombre d'octets disponibles
 * @return 1 si la première ligne est un séparateur « From … »
 */
static inline int
mt_looks_like_mbox (const char *data, size_t len)
{
	size_t eol = 0;

	while (eol < len && data[eol] != '\n')
		eol++;
	if (eol > 0 && data[eol - 1] == '\r')
		eol--;
	return mt_is_from_line (data, eol);
}

/* ------------------------------------------------------------------ */
/* En-têtes et indicateurs                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Lit la valeur d'un en-tête d'un message (sans dépliage ; en-têtes seulement).
 * @param msg   message
 * @param len   longueur du message
 * @param name  nom de l'en-tête, sans les deux-points
 * @param out   tampon recevant la valeur
 * @param size  taille de out
 * @return 1 si l'en-tête existe
 */
static inline int
mt_header_value (const char *msg, size_t len, const char *name, char *out, size_t size)
{
	size_t pos = 0, nlen = strlen (name);

	while (pos < len) {
		size_t eol = pos, vstart, vlen;

		while (eol < len && msg[eol] != '\n')
			eol++;
		vlen = eol - pos;
		if (vlen > 0 && msg[eol - 1] == '\r')
			vlen--;
		if (vlen == 0)
			return 0;	/* fin des en-têtes */
		if (mt_starts_with_ci (msg + pos, vlen, name) && vlen > nlen && msg[pos + nlen] == ':') {
			vstart = pos + nlen + 1;
			while (vstart < pos + vlen && (msg[vstart] == ' ' || msg[vstart] == '\t'))
				vstart++;
			vlen = pos + vlen - vstart;
			if (vlen >= size)
				vlen = size - 1;
			memcpy (out, msg + vstart, vlen);
			out[vlen] = '\0';
			return 1;
		}
		pos = eol < len ? eol + 1 : eol;
	}
	return 0;
}

/**
 * @brief Indicateurs d'un message d'après l'en-tête « X-Mozilla-Status » de Thunderbird.
 * Bits : 0x0001 lu, 0x0002 répondu, 0x0004 drapeau, 0x0008 supprimé, 0x0040 transféré.
 * @param msg message
 * @param len longueur du message
 * @return masque de MT_FLAG_* ; MT_FLAG_SEEN si l'en-tête est absent (courrier d'archive)
 */
static inline int
mt_mozilla_flags (const char *msg, size_t len)
{
	char value[32];
	unsigned long status;
	int flags = 0;

	if (!mt_header_value (msg, len, "X-Mozilla-Status", value, sizeof (value)))
		return MT_FLAG_SEEN;

	status = strtoul (value, NULL, 16);
	if (status & 0x0001)
		flags |= MT_FLAG_SEEN;
	if (status & 0x0002)
		flags |= MT_FLAG_REPLIED;
	if (status & 0x0004)
		flags |= MT_FLAG_FLAGGED;
	if (status & 0x0008)
		flags |= MT_FLAG_DELETED;
	return flags;
}

/**
 * @brief Indicateurs d'un message Maildir d'après le suffixe de son nom (« :2,SRF »).
 * S lu, R répondu, F drapeau, D brouillon, T supprimé. Un fichier du dossier « new »
 * (non lu) se passe de suffixe.
 * @param filename nom du fichier
 * @param in_new   1 si le fichier est dans le sous-dossier « new »
 * @return masque de MT_FLAG_*
 */
static inline int
mt_maildir_flags (const char *filename, int in_new)
{
	const char *info = strstr (filename, ":2,");

	/* Windows interdit « : » dans un nom de fichier : « ! » ou « ; » le remplacent */
	if (!info)
		info = strstr (filename, "!2,");
	if (!info)
		info = strstr (filename, ";2,");
	int flags = 0;

	if (!info)
		return in_new ? 0 : MT_FLAG_SEEN;

	for (info += 3; *info; info++) {
		switch (*info) {
		case 'S': flags |= MT_FLAG_SEEN; break;
		case 'R': flags |= MT_FLAG_REPLIED; break;
		case 'F': flags |= MT_FLAG_FLAGGED; break;
		case 'D': flags |= MT_FLAG_DRAFT; break;
		case 'T': flags |= MT_FLAG_DELETED; break;
		default: break;
		}
	}
	return flags;
}

/* ------------------------------------------------------------------ */
/* Profil Thunderbird et arborescences                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Teste si un fichier d'un dossier de courrier Thunderbird n'est pas un dossier
 * de messages (index .msf, .dat, .json, journaux, verrous, fichiers cachés).
 * @param name nom du fichier
 * @return 1 s'il faut l'ignorer
 */
static inline int
mt_is_aux_file (const char *name)
{
	static const char *const ext[] = { ".msf", ".dat", ".json", ".sqlite", ".html", ".rdf", ".tmp",
		".log", ".lock", ".wmsf", ".sbd", NULL };
	size_t ii;

	if (!name || !*name || name[0] == '.')
		return 1;
	for (ii = 0; ext[ii]; ii++)
		if (mt_has_ext (name, ext[ii]))
			return 1;
	return 0;
}

/**
 * @brief Nom d'un dossier d'après celui d'un répertoire « X.sbd » (sous-dossiers
 * de X dans un profil Thunderbird), ou d'un répertoire Maildir++ « .A.B » (nom
 * hiérarchique « A/B »).
 * @param name nom du répertoire
 * @param out  tampon recevant le nom du dossier (« / » comme séparateur)
 * @param size taille de out
 */
static inline void
mt_tb_folder_name (const char *name, char *out, size_t size)
{
	size_t len;

	if (size == 0)
		return;
	if (mt_has_ext (name, ".sbd"))
		len = strlen (name) - 4;
	else
		len = strlen (name);
	if (len >= size)
		len = size - 1;
	memcpy (out, name, len);
	out[len] = '\0';
}

/**
 * @brief Convertit un nom Maildir++ (« .Travail.Projets ») en chemin de dossier
 * (« Travail/Projets »).
 * @param name nom du répertoire, commençant par un point
 * @param out  tampon recevant le chemin
 * @param size taille de out
 */
static inline void
mt_maildirpp_path (const char *name, char *out, size_t size)
{
	size_t ii, n = 0;

	if (name[0] == '.')
		name++;
	for (ii = 0; name[ii] && n + 1 < size; ii++)
		out[n++] = name[ii] == '.' ? '/' : name[ii];
	out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* vCard                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Extrait la carte suivante d'un fichier .vcf (de BEGIN:VCARD à END:VCARD inclus).
 * @param data   contenu du fichier
 * @param len    taille du contenu
 * @param pos    position de lecture (mise à jour)
 * @param start  début de la carte
 * @param vc_len longueur de la carte
 * @return 1 si une carte a été trouvée, 0 en fin de fichier
 */
static inline int
mt_vcard_next (const char *data, size_t len, size_t *pos, const char **start, size_t *vc_len)
{
	size_t p = *pos, begin = 0;
	int in_card = 0;

	while (p < len) {
		size_t eol = p, next;

		while (eol < len && data[eol] != '\n')
			eol++;
		next = eol < len ? eol + 1 : eol;
		if (!in_card && mt_starts_with_ci (data + p, eol - p, "BEGIN:VCARD")) {
			begin = p;
			in_card = 1;
		} else if (in_card && mt_starts_with_ci (data + p, eol - p, "END:VCARD")) {
			*start = data + begin;
			*vc_len = next - begin;
			*pos = next;
			return 1;
		}
		p = next;
	}
	*pos = len;
	return 0;
}

#endif /* EVO_CORE_TRANSFER_UTIL_H */
