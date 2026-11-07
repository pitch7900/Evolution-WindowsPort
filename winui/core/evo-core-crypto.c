/*
 * evo-core : signature, chiffrement, vérification et déchiffrement S/MIME
 * natifs Windows (CryptoAPI). Colle entre les messages Camel et les
 * primitives de evo-core-crypto-util.h. Voir aussi evo-core-private.h.
 *
 * Principe « transparent » pour l'utilisateur :
 *   - le certificat personnel est pris dans le magasin Windows (« Personnel »)
 *     d'après l'adresse du compte ; s'il n'existe pas, un certificat auto-signé
 *     est créé automatiquement (aucune configuration) ;
 *   - les certificats des destinataires sont pris dans « Autres personnes » ;
 *     ils y sont ajoutés automatiquement à la lecture de chaque message signé
 *     valide reçu de ces personnes ;
 *   - la lecture déchiffre et vérifie automatiquement.
 *
 * Variable d'environnement EVO_CORE_CRYPTO_MEMORY_STORE=1 : utilise des
 * magasins en mémoire à la place des magasins Windows (tests automatisés).
 */

#include "evo-core-private.h"
#include "evo-core-crypto-util.h"

/* ---------------------------------------------------------------- */
/* Magasins de certificats                                          */
/* ---------------------------------------------------------------- */

static HCERTSTORE memory_stores[2] = { NULL, NULL };

/* Ouvre un magasin Windows de l'utilisateur (ou un magasin mémoire en mode test).
 * Paramètres : name : « MY » (personnel) ou « AddressBook » (autres personnes)
 * Retourne : le magasin, à rendre avec close_store (). */
static HCERTSTORE
open_store (const gchar *name)
{
	if (g_getenv ("EVO_CORE_CRYPTO_MEMORY_STORE")) {
		gint index = g_strcmp0 (name, "MY") == 0 ? 0 : 1;

		if (!memory_stores[index])
			memory_stores[index] = CertOpenStore (CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
		return memory_stores[index];
	} else {
		gunichar2 *wide = g_utf8_to_utf16 (name, -1, NULL, NULL, NULL);
		HCERTSTORE store = CertOpenSystemStoreW (0, wide);

		g_free (wide);
		return store;
	}
}

static void
close_store (HCERTSTORE store)
{
	if (store && store != memory_stores[0] && store != memory_stores[1])
		CertCloseStore (store, 0);
}

/* Cherche le certificat de chiffrement d'un destinataire : « Autres personnes »,
 * puis « Personnel » (ses propres certificats). Retourne un contexte à libérer, ou NULL. */
static PCCERT_CONTEXT
find_recipient_cert (const gchar *email)
{
	static const gchar *const names[] = { "AddressBook", "MY", "TrustedPeople" };
	PCCERT_CONTEXT cert = NULL;
	guint ii;

	for (ii = 0; !cert && ii < G_N_ELEMENTS (names); ii++) {
		HCERTSTORE store = open_store (names[ii]);

		if (store) {
			cert = crypto_find_cert (store, email, 0, g_strcmp0 (names[ii], "MY") == 0);
			close_store (store);
		}
	}

	return cert;
}

/* Formate le dernier code d'erreur Windows en texte (UTF-8, à libérer). */
static gchar *
last_error_text (DWORD code)
{
	WCHAR *buffer = NULL;
	gchar *text;

	if (!FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	    NULL, code, 0, (LPWSTR) &buffer, 0, NULL) || !buffer)
		return g_strdup_printf ("code 0x%08lx", (unsigned long) code);

	text = g_utf16_to_utf8 ((const gunichar2 *) buffer, -1, NULL, NULL, NULL);
	LocalFree (buffer);
	if (text)
		g_strstrip (text);

	return text ? text : g_strdup_printf ("code 0x%08lx", (unsigned long) code);
}

/* ---------------------------------------------------------------- */
/* Conversions Camel <-> octets                                     */
/* ---------------------------------------------------------------- */

/* Sérialise un objet Camel (message ou partie) en texte MIME (fins de ligne LF). */
static gchar *
wrapper_to_text (CamelDataWrapper *wrapper,
		 gsize *length,
		 GError **error)
{
	CamelStream *stream = camel_stream_mem_new ();
	GByteArray *bytes;
	gchar *text = NULL;

	if (camel_data_wrapper_write_to_stream_sync (wrapper, stream, NULL, error)) {
		bytes = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (stream));
		text = g_strndup ((const gchar *) bytes->data, bytes->len);
		*length = bytes->len;
	}
	g_object_unref (stream);

	return text;
}

/* Construit une partie MIME à partir de son texte (en-têtes Content-*, ligne vide, corps). */
static CamelMimePart *
part_from_text (const gchar *text,
		gsize length,
		GError **error)
{
	CamelStream *stream = camel_stream_mem_new ();
	CamelMimePart *part = camel_mime_part_new ();

	camel_stream_mem_set_buffer (CAMEL_STREAM_MEM (stream), (const gchar *) text, length);
	if (!camel_data_wrapper_construct_from_stream_sync (CAMEL_DATA_WRAPPER (part), stream, NULL, error))
		g_clear_object (&part);
	g_object_unref (stream);

	return part;
}

/* Extrait d'un message complet le texte de l'entité à protéger : uniquement ses
 * en-têtes « Content-* » (avec lignes de continuation), une ligne vide et le corps.
 * Paramètres : full, length : message sérialisé
 * Retourne : texte alloué (g_free). */
static gchar *
extract_entity (const gchar *full,
		gsize length)
{
	GString *entity = g_string_new ("");
	const gchar *p = full, *end = full + length, *body = end;
	gboolean keep = FALSE;

	while (p < end) {
		const gchar *eol = memchr (p, '\n', end - p);
		gsize line_len = eol ? (gsize) (eol - p) + 1 : (gsize) (end - p);
		gsize content_len = line_len;

		while (content_len > 0 && (p[content_len - 1] == '\n' || p[content_len - 1] == '\r'))
			content_len--;

		if (content_len == 0) {
			body = p + line_len;
			break;
		}

		if (*p == ' ' || *p == '\t') {
			/* continuation : suit l'en-tête précédent */
		} else {
			keep = g_ascii_strncasecmp (p, "Content-", 8) == 0;
		}

		if (keep) {
			g_string_append_len (entity, p, content_len);
			g_string_append_c (entity, '\n');
		}
		p += line_len;
	}

	g_string_append_c (entity, '\n');
	if (body < end)
		g_string_append_len (entity, body, end - body);

	return g_string_free (entity, FALSE);
}

/* Génère une chaîne de délimitation MIME aléatoire. */
static gchar *
new_boundary (void)
{
	return g_strdup_printf ("----=_EvolutionWin_%08x%08x", g_random_int (), g_random_int ());
}

/* Encode en base64 par lignes de 76 caractères (fins de ligne LF). */
static gchar *
base64_lines (const guint8 *data,
	      gsize length)
{
	gchar *encoded = g_base64_encode (data, length);
	GString *out = g_string_new ("");
	gsize ii, total = strlen (encoded);

	for (ii = 0; ii < total; ii += 76) {
		g_string_append_len (out, encoded + ii, MIN ((gsize) 76, total - ii));
		g_string_append_c (out, '\n');
	}
	g_free (encoded);

	return g_string_free (out, FALSE);
}

/* ---------------------------------------------------------------- */
/* Envoi : signature et chiffrement                                 */
/* ---------------------------------------------------------------- */

/* Retourne le certificat personnel de l'expéditeur, en le créant au besoin.
 * Paramètres : email, name : identité du compte ; for_signing : usage ; created : indique la création
 * Retourne : contexte à libérer, ou NULL avec error. */
static PCCERT_CONTEXT
ensure_own_cert (const gchar *email,
		 const gchar *name,
		 gboolean for_signing,
		 gboolean *created,
		 GError **error)
{
	HCERTSTORE store = open_store ("MY");
	PCCERT_CONTEXT cert;

	if (!store) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"Impossible d'ouvrir le magasin de certificats de Windows.");
		return NULL;
	}

	cert = crypto_find_cert (store, email, for_signing, TRUE);
	if (!cert) {
		cert = crypto_create_self_signed (store, email, name);
		if (cert) {
			if (created)
				*created = TRUE;
		} else {
			gchar *reason = last_error_text (GetLastError ());

			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Impossible de créer votre certificat personnel pour %s : %s", email, reason);
			g_free (reason);
		}
	}
	close_store (store);

	return cert;
}

/* Remplace le contenu d'un message par une entité MIME protégée (texte complet
 * « en-têtes Content-* + ligne vide + corps »). */
static gboolean
replace_message_content (CamelMimeMessage *message,
			 const gchar *text,
			 gsize length,
			 GError **error)
{
	CamelMimePart *part = part_from_text (text, length, error);
	CamelDataWrapper *content;
	CamelContentType *type;

	if (!part)
		return FALSE;

	content = camel_medium_get_content (CAMEL_MEDIUM (part));
	type = camel_mime_part_get_content_type (part);
	if (!content || !type) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Le message protégé est illisible.");
		g_object_unref (part);
		return FALSE;
	}

	camel_medium_set_content (CAMEL_MEDIUM (message), content);
	{
		gchar *formatted = camel_content_type_format (type);

		camel_mime_part_set_content_type (CAMEL_MIME_PART (message), formatted);
		g_free (formatted);
	}
	if (camel_mime_part_get_encoding (part) != CAMEL_TRANSFER_ENCODING_DEFAULT)
		camel_mime_part_set_encoding (CAMEL_MIME_PART (message), camel_mime_part_get_encoding (part));
	else
		camel_medium_remove_header (CAMEL_MEDIUM (message), "Content-Transfer-Encoding");
	g_object_unref (part);

	return TRUE;
}

/* Signe et/ou chiffre le message (S/MIME) avec le magasin de certificats Windows.
 * Signature : multipart/signed avec signature détachée (application/pkcs7-signature),
 * lisible par tous les clients. Chiffrement : application/pkcs7-mime enveloped-data,
 * pour tous les destinataires et pour l'expéditeur (copie dans Envoyés lisible).
 * Si les deux sont demandés, signe d'abord puis chiffre.
 * Paramètres :
 *   message      : message complet, dont l'en-tête et le corps sont déjà construits
 *   sender_email : adresse du compte ; sender_name : son nom d'affichage
 *   sign, encrypt: opérations demandées
 *   recipients   : tous les destinataires (À, Cc, Cci) pour le chiffrement
 *   error        : message d'erreur en français destiné à l'utilisateur
 * Retourne : TRUE si le message a été protégé. */
gboolean
core_crypto_protect_message (CamelMimeMessage *message,
			     const gchar *sender_email,
			     const gchar *sender_name,
			     gboolean sign,
			     gboolean encrypt,
			     CamelInternetAddress *recipients,
			     GError **error)
{
	gchar *full, *entity = NULL, *result = NULL;
	gsize full_len = 0, entity_len;
	gboolean ok = FALSE;
	PCCERT_CONTEXT own = NULL;

	if (!sender_email || !*sender_email) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Impossible de signer ou chiffrer : le compte n'a pas d'adresse électronique.");
		return FALSE;
	}

	full = wrapper_to_text (CAMEL_DATA_WRAPPER (message), &full_len, error);
	if (!full)
		return FALSE;
	entity = extract_entity (full, full_len);
	entity_len = strlen (entity);
	g_free (full);

	/* certificat personnel : nécessaire pour signer, et pour relire la copie envoyée */
	own = ensure_own_cert (sender_email, sender_name, sign, NULL, error);
	if (!own)
		goto out;

	if (sign) {
		gchar *canonical;
		gsize canonical_len;
		BYTE *signature = NULL;
		DWORD signature_len = 0;

		canonical = crypto_canonicalize (entity, entity_len, 1, 0, &canonical_len);
		if (!canonical || !crypto_sign_detached (own, (const BYTE *) canonical, (DWORD) canonical_len, &signature, &signature_len)) {
			gchar *reason = last_error_text (GetLastError ());

			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"La signature a échoué (l'accès à votre clé privée a peut-être été refusé) : %s", reason);
			g_free (reason);
			g_free (canonical);
			goto out;
		}
		g_free (canonical);

		{
			gchar *boundary = new_boundary ();
			gchar *sig64 = base64_lines (signature, signature_len);
			gsize body_len;
			gchar *body = crypto_canonicalize (entity, entity_len, 0, 0, &body_len);

			result = g_strdup_printf (
				"Content-Type: multipart/signed; protocol=\"application/pkcs7-signature\"; micalg=sha-256;\n"
				"\tboundary=\"%s\"\n"
				"\n"
				"This is a cryptographically signed message in MIME format.\n"
				"\n"
				"--%s\n"
				"%s\n"
				"--%s\n"
				"Content-Type: application/pkcs7-signature; name=\"smime.p7s\"\n"
				"Content-Transfer-Encoding: base64\n"
				"Content-Disposition: attachment; filename=\"smime.p7s\"\n"
				"Content-Description: S/MIME Cryptographic Signature\n"
				"\n"
				"%s"
				"--%s--\n",
				boundary, boundary, body, boundary, sig64, boundary);

			g_free (body);
			g_free (sig64);
			g_free (boundary);
		}
		free (signature);
		g_free (entity);
		entity = result;
		entity_len = strlen (entity);
		result = NULL;
	}

	if (encrypt) {
		GPtrArray *certs = g_ptr_array_new ();
		GPtrArray *missing = g_ptr_array_new_with_free_func (g_free);
		GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		gint ii, n = recipients ? camel_address_length (CAMEL_ADDRESS (recipients)) : 0;
		gchar *canonical;
		gsize canonical_len;
		BYTE *enveloped = NULL;
		DWORD enveloped_len = 0;

		for (ii = 0; ii < n; ii++) {
			const gchar *address = NULL;
			gchar *key;
			PCCERT_CONTEXT cert;

			if (!camel_internet_address_get (recipients, ii, NULL, &address) || !address || !*address)
				continue;
			key = g_ascii_strdown (address, -1);
			if (g_hash_table_contains (seen, key)) {
				g_free (key);
				continue;
			}
			g_hash_table_add (seen, key);

			cert = find_recipient_cert (address);
			if (cert)
				g_ptr_array_add (certs, (gpointer) cert);
			else
				g_ptr_array_add (missing, g_strdup (address));
		}

		if (missing->len > 0) {
			GString *list = g_string_new ("");

			for (ii = 0; ii < (gint) missing->len; ii++) {
				if (ii > 0)
					g_string_append (list, ", ");
				g_string_append (list, g_ptr_array_index (missing, ii));
			}
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Impossible de chiffrer : aucun certificat connu pour %s. "
				"Demandez à ces personnes de vous envoyer un message signé : leur certificat sera mémorisé automatiquement.",
				list->str);
			g_string_free (list, TRUE);
		} else {
			/* l'expéditeur lit aussi sa copie : son propre certificat est ajouté aux destinataires */
			g_ptr_array_add (certs, (gpointer) CertDuplicateCertificateContext (own));

			canonical = crypto_canonicalize (entity, entity_len, 1, 1, &canonical_len);
			if (canonical && crypto_encrypt ((PCCERT_CONTEXT *) certs->pdata, certs->len,
			    (const BYTE *) canonical, (DWORD) canonical_len, &enveloped, &enveloped_len)) {
				gchar *b64 = base64_lines (enveloped, enveloped_len);

				result = g_strdup_printf (
					"Content-Type: application/pkcs7-mime; smime-type=enveloped-data; name=\"smime.p7m\"\n"
					"Content-Transfer-Encoding: base64\n"
					"Content-Disposition: attachment; filename=\"smime.p7m\"\n"
					"Content-Description: S/MIME Encrypted Message\n"
					"\n"
					"%s", b64);
				g_free (b64);
			} else {
				gchar *reason = last_error_text (GetLastError ());

				g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Le chiffrement a échoué : %s", reason);
				g_free (reason);
			}
			g_free (canonical);
			free (enveloped);
		}

		for (ii = 0; ii < (gint) certs->len; ii++)
			CertFreeCertificateContext (g_ptr_array_index (certs, ii));
		g_ptr_array_free (certs, TRUE);
		g_ptr_array_free (missing, TRUE);
		g_hash_table_destroy (seen);

		if (!result)
			goto out;
		g_free (entity);
		entity = result;
		entity_len = strlen (entity);
		result = NULL;
	}

	ok = replace_message_content (message, entity, entity_len, error);

out:
	if (own)
		CertFreeCertificateContext (own);
	g_free (entity);
	g_free (result);

	return ok;
}

/* ---------------------------------------------------------------- */
/* Lecture : déchiffrement et vérification                          */
/* ---------------------------------------------------------------- */

/* Libère les champs d'un CoreCryptoInfo. */
void
core_crypto_info_clear (CoreCryptoInfo *info)
{
	g_clear_pointer (&info->signer, g_free);
	g_clear_pointer (&info->description, g_free);
}

/* Ajoute une phrase à la description. */
static void
info_add (CoreCryptoInfo *info,
	  const gchar *sentence)
{
	gchar *old = info->description;

	info->description = old && *old ? g_strconcat (old, " ", sentence, NULL) : g_strdup (sentence);
	g_free (old);
}

/* Renseigne info à partir du résultat d'une vérification : signataire, validité,
 * approbation, concordance avec l'expéditeur ; mémorise le certificat du signataire
 * (« Autres personnes ») pour pouvoir lui écrire chiffré. */
static void
info_apply_verification (CoreCryptoInfo *info,
			 CryptoVerifyResult *result,
			 const gchar *from_email)
{
	info->is_signed = TRUE;

	if (!result->signature_ok) {
		info->valid = FALSE;
		info_add (info, "La signature est invalide : le message a peut-être été modifié depuis son envoi.");
		return;
	}

	{
		char label[600];

		crypto_cert_label (result->signer, label, sizeof (label));
		g_free (info->signer);
		info->signer = g_strdup (label);
	}

	if (from_email && *from_email && !crypto_cert_has_email (result->signer, from_email)) {
		info->valid = FALSE;
		info_add (info, "L'adresse du certificat ne correspond pas à celle de l'expéditeur : soyez prudent.");
		return;
	}

	info->valid = TRUE;
	info->trusted = result->trusted;
	if (result->trusted) {
		info_add (info, "Le contenu n'a pas été modifié et le certificat de l'expéditeur est approuvé.");
	} else {
		info_add (info, "Le contenu n'a pas été modifié, mais le certificat de l'expéditeur n'est pas reconnu par Windows "
			"(certificat personnel auto-signé ou autorité inconnue).");
	}

	/* mémorise le certificat pour chiffrer les futures réponses à cette personne */
	if (from_email && *from_email && crypto_cert_usable (result->signer, 0, 0)) {
		HCERTSTORE book = open_store ("AddressBook");

		if (book) {
			CertAddCertificateContextToStore (book, result->signer, CERT_STORE_ADD_NEWER, NULL);
			close_store (book);
		}
	}
}

/* Retourne le contenu binaire décodé (base64 → DER) d'une partie pkcs7. */
static GByteArray *
part_decoded_bytes (CamelMimePart *part)
{
	CamelStream *stream = camel_stream_mem_new ();
	CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
	GByteArray *copy = NULL;

	if (content && camel_data_wrapper_decode_to_stream_sync (content, stream, NULL, NULL)) {
		GByteArray *bytes = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (stream));

		copy = g_byte_array_sized_new (bytes->len);
		g_byte_array_append (copy, bytes->data, bytes->len);
	}
	g_object_unref (stream);

	return copy;
}

/* Ouvre une partie application/pkcs7-mime : déchiffre (enveloped-data) ou extrait le
 * contenu d'une signature opaque (signed-data). Plusieurs niveaux sont enchaînés
 * (déchiffrer puis lire une signature opaque).
 * Paramètres : part : partie pkcs7 ; from_email : expéditeur ; info : résultat cumulé
 * Retourne : la partie MIME interne (g_object_unref), ou NULL si impossible. */
CamelMimePart *
core_crypto_open_pkcs7 (CamelMimePart *part,
			const gchar *from_email,
			CoreCryptoInfo *info)
{
	CamelMimePart *current = g_object_ref (part);
	gint depth;

	for (depth = 0; depth < 3; depth++) {
		CamelContentType *type = camel_mime_part_get_content_type (current);
		const gchar *smime_type = type ? camel_content_type_param (type, "smime-type") : NULL;
		GByteArray *der;
		BYTE *plain = NULL;
		DWORD plain_len = 0;
		CamelMimePart *inner;

		if (!type || !(camel_content_type_is (type, "application", "pkcs7-mime") ||
		    camel_content_type_is (type, "application", "x-pkcs7-mime")))
			return current;

		der = part_decoded_bytes (current);
		if (!der || der->len == 0) {
			if (der)
				g_byte_array_free (der, TRUE);
			info_add (info, "Le message S/MIME est vide ou illisible.");
			g_object_unref (current);
			return NULL;
		}

		if (g_strcmp0 (smime_type, "signed-data") == 0) {
			CryptoVerifyResult result;

			crypto_verify_opaque (der->data, der->len, &plain, &plain_len, &result);
			info_apply_verification (info, &result, from_email);
			if (result.signer)
				CertFreeCertificateContext (result.signer);
			if (!plain) {
				g_byte_array_free (der, TRUE);
				g_object_unref (current);
				return NULL;
			}
		} else {
			HCERTSTORE store = open_store ("MY");

			info->encrypted = TRUE;
			if (!store || !crypto_decrypt (store, der->data, der->len, &plain, &plain_len)) {
				DWORD code = GetLastError ();

				info->valid = FALSE;
				if (code == (DWORD) CRYPT_E_NO_DECRYPT_CERT)
					info_add (info, "Ce message a été chiffré pour un certificat que vous ne possédez pas sur cet ordinateur.");
				else {
					gchar *reason = last_error_text (code);
					gchar *sentence = g_strdup_printf ("Le déchiffrement a échoué : %s", reason);

					info_add (info, sentence);
					g_free (sentence);
					g_free (reason);
				}
				close_store (store);
				g_byte_array_free (der, TRUE);
				g_object_unref (current);
				return NULL;
			}
			close_store (store);
			info->valid = TRUE;
			info->trusted = TRUE;
			info_add (info, "Ce message était chiffré : seul vous pouvez le lire.");
		}
		g_byte_array_free (der, TRUE);

		inner = part_from_text ((const gchar *) plain, plain_len, NULL);
		free (plain);
		g_object_unref (current);
		if (!inner)
			return NULL;
		current = inner;
	}

	return current;
}

/* Vérifie un multipart/signed S/MIME (signature détachée).
 * Paramètres : signed_part : multipart/signed ; from_email : expéditeur ; info : résultat */
void
core_crypto_verify_multipart_signed (CamelMultipart *signed_part,
				     const gchar *from_email,
				     CoreCryptoInfo *info)
{
	GError *error = NULL;
	CamelStream *content_stream;
	CamelMimePart *signature_part;
	GByteArray *signature;
	GByteArray *content;

	info->is_signed = TRUE;

	if (!CAMEL_IS_MULTIPART_SIGNED (signed_part) || camel_multipart_get_number (signed_part) < 2) {
		info->valid = FALSE;
		info_add (info, "La structure de la signature est incorrecte.");
		return;
	}

	content_stream = camel_multipart_signed_get_content_stream (CAMEL_MULTIPART_SIGNED (signed_part), &error);
	signature_part = camel_multipart_get_part (signed_part, CAMEL_MULTIPART_SIGNED_SIGNATURE);
	if (!content_stream || !signature_part) {
		info->valid = FALSE;
		info_add (info, "Le contenu signé est introuvable.");
		g_clear_error (&error);
		g_clear_object (&content_stream);
		return;
	}

	signature = part_decoded_bytes (signature_part);
	{
		CamelStream *copy = camel_stream_mem_new ();
		GByteArray *bytes;

		camel_stream_write_to_stream (content_stream, copy, NULL, NULL);
		bytes = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (copy));
		content = g_byte_array_sized_new (bytes->len);
		g_byte_array_append (content, bytes->data, bytes->len);
		g_object_unref (copy);
	}
	g_object_unref (content_stream);

	if (signature && signature->len > 0 && content->len > 0) {
		size_t canonical_len = 0;
		char *canonical = crypto_canonicalize ((const char *) content->data, content->len, 1, 0, &canonical_len);
		CryptoVerifyResult result;

		crypto_verify_detached (signature->data, signature->len, (const BYTE *) canonical, (DWORD) canonical_len, &result);
		info_apply_verification (info, &result, from_email);
		if (result.signer)
			CertFreeCertificateContext (result.signer);
		free (canonical);
	} else {
		info->valid = FALSE;
		info_add (info, "La signature est vide.");
	}

	if (signature)
		g_byte_array_free (signature, TRUE);
	g_byte_array_free (content, TRUE);
}

/* ---------------------------------------------------------------- */
/* Pré-vérification pour l'écran de rédaction                       */
/* ---------------------------------------------------------------- */

/* Prépare le chiffrement et la signature d'un message en cours de rédaction :
 * indique si l'expéditeur a déjà un certificat (sinon il sera créé automatiquement)
 * et quels destinataires n'ont pas de certificat connu (chiffrement impossible).
 * Paramètres de args : [0] = JSON {"account": uid, "to": "...", "cc": "...", "bcc": "..."}
 * Retourne : {"sender":"adresse","has_certificate":bool,"will_create":bool,
 *             "recipients_ready":[...],"recipients_missing":[...]} */
static gchar *
core_crypto_check_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	JsonNode *root = json_from_string (args[0], error);
	JsonObject *object;
	ESource *identity;
	const gchar *account, *email = NULL;
	JsonBuilder *builder;
	HCERTSTORE store;
	PCCERT_CONTEXT own = NULL;
	CamelInternetAddress *all;
	gint ii, n;
	gchar *json;
	const gchar *fields[] = { "to", "cc", "bcc" };
	GHashTable *seen;
	GPtrArray *ready, *missing;

	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account = object ? json_object_get_string_member_with_default (object, "account", NULL) : NULL;
	identity = account ? core_ref_identity (core, account) : NULL;
	if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY))
		email = e_source_mail_identity_get_address (e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY));

	store = open_store ("MY");
	if (store && email)
		own = crypto_find_cert (store, email, 1, TRUE);
	close_store (store);

	all = camel_internet_address_new ();
	for (ii = 0; object && ii < 3; ii++) {
		const gchar *raw = json_object_get_string_member_with_default (object, fields[ii], "");
		gchar *clean = g_strdelimit (g_strdup (raw), ";", ',');
		CamelInternetAddress *part = camel_internet_address_new ();

		camel_address_unformat (CAMEL_ADDRESS (part), clean);
		camel_address_cat (CAMEL_ADDRESS (all), CAMEL_ADDRESS (part));
		g_object_unref (part);
		g_free (clean);
	}

	seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	ready = g_ptr_array_new_with_free_func (g_free);
	missing = g_ptr_array_new_with_free_func (g_free);
	n = camel_address_length (CAMEL_ADDRESS (all));
	for (ii = 0; ii < n; ii++) {
		const gchar *address = NULL;
		gchar *key;
		PCCERT_CONTEXT cert;

		if (!camel_internet_address_get (all, ii, NULL, &address) || !address || !*address)
			continue;
		key = g_ascii_strdown (address, -1);
		if (g_hash_table_contains (seen, key)) {
			g_free (key);
			continue;
		}
		g_hash_table_add (seen, key);

		cert = find_recipient_cert (address);
		if (cert) {
			g_ptr_array_add (ready, g_strdup (address));
			CertFreeCertificateContext (cert);
		} else {
			g_ptr_array_add (missing, g_strdup (address));
		}
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "sender");
	json_builder_add_string_value (builder, email ? email : "");
	json_builder_set_member_name (builder, "has_certificate");
	json_builder_add_boolean_value (builder, own != NULL);
	json_builder_set_member_name (builder, "will_create");
	json_builder_add_boolean_value (builder, own == NULL && email != NULL);
	json_builder_set_member_name (builder, "recipients_ready");
	json_builder_begin_array (builder);
	for (ii = 0; ii < (gint) ready->len; ii++)
		json_builder_add_string_value (builder, g_ptr_array_index (ready, ii));
	json_builder_end_array (builder);
	json_builder_set_member_name (builder, "recipients_missing");
	json_builder_begin_array (builder);
	for (ii = 0; ii < (gint) missing->len; ii++)
		json_builder_add_string_value (builder, g_ptr_array_index (missing, ii));
	json_builder_end_array (builder);
	json_builder_end_object (builder);
	{
		JsonGenerator *generator = json_generator_new ();
		JsonNode *node = json_builder_get_root (builder);

		json_generator_set_root (generator, node);
		json = json_generator_to_data (generator, NULL);
		json_node_unref (node);
		g_object_unref (generator);
	}
	g_object_unref (builder);

	if (own)
		CertFreeCertificateContext (own);
	g_ptr_array_free (ready, TRUE);
	g_ptr_array_free (missing, TRUE);
	g_hash_table_destroy (seen);
	g_object_unref (all);
	g_clear_object (&identity);
	json_node_unref (root);

	return json;
}

void
evo_core_crypto_check (EvoCore *core,
		       const char *check_json,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) check_json, NULL };

	g_return_if_fail (check_json != NULL);

	core_submit (core, core_crypto_check_run, g_strdupv (args), func, user_data);
}
