/*
 * evo-core : primitives S/MIME natives Windows (CryptoAPI / CNG), sans GLib ni
 * Camel. Signature et chiffrement CMS (RFC 5652), recherche des certificats
 * par adresse électronique dans un magasin de certificats Windows, création
 * d'un certificat personnel auto-signé, vérification et déchiffrement.
 *
 * Fichier d'en-tête autonome, inclus par evo-core-crypto.c (colle MIME / Camel)
 * et par les tests unitaires (tests/unit). Les magasins de certificats sont
 * passés en paramètre : le moteur utilise les magasins Windows de
 * l'utilisateur (« MY » et « AddressBook »), les tests un magasin en mémoire.
 *
 * Toute la mémoire renvoyée (tampons) est allouée avec malloc : à libérer avec free.
 */

#ifndef EVO_CORE_CRYPTO_UTIL_H
#define EVO_CORE_CRYPTO_UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

#define CRYPTO_ENC		(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)

/* Résultat de la vérification d'une signature */
typedef struct {
	int		signature_ok;	/* la signature mathématique est valide */
	int		trusted;	/* la chaîne du certificat est approuvée par Windows */
	DWORD		trust_error;	/* code CERT_E_* / TRUST_E_* si non approuvée */
	PCCERT_CONTEXT	signer;		/* certificat du signataire (à libérer), ou NULL */
} CryptoVerifyResult;

/* Canonicalise un texte : supprime tous les CR puis remplace chaque LF par
 * CRLF (crlf != 0) ou le laisse LF. Si keep_trailing vaut 0, retire le saut de
 * ligne final (celui qui précède le délimiteur dans un multipart/signed).
 * Paramètres : in, len : texte ; crlf, keep_trailing : options ; out_len : taille du résultat
 * Retourne : tampon alloué terminé par NUL (free), ou NULL. */
static inline char *
crypto_canonicalize (const char *in,
		     size_t len,
		     int crlf,
		     int keep_trailing,
		     size_t *out_len)
{
	char *out = malloc (len * 2 + 3);
	size_t ii, oo = 0;

	if (!out)
		return NULL;

	for (ii = 0; ii < len; ii++) {
		if (in[ii] == '\r')
			continue;
		if (in[ii] == '\n') {
			if (crlf)
				out[oo++] = '\r';
			out[oo++] = '\n';
		} else {
			out[oo++] = in[ii];
		}
	}

	if (!keep_trailing && oo > 0 && out[oo - 1] == '\n') {
		oo--;
		if (crlf && oo > 0 && out[oo - 1] == '\r')
			oo--;
	}

	out[oo] = '\0';
	if (out_len)
		*out_len = oo;

	return out;
}

/* Compare un texte large (certificat) à une adresse UTF-8, sans tenir compte de la casse. */
static inline int
crypto_email_equal_w (const WCHAR *wide,
		      const char *email)
{
	char buf[512];

	if (!wide || !email || !WideCharToMultiByte (CP_UTF8, 0, wide, -1, buf, sizeof (buf), NULL, NULL))
		return 0;

	return _stricmp (buf, email) == 0;
}

/* Indique si un certificat porte l'adresse donnée (nom alternatif rfc822 ou champ E= du sujet). */
static inline int
crypto_cert_has_email (PCCERT_CONTEXT cert,
		       const char *email)
{
	static const char *const oids[] = { szOID_SUBJECT_ALT_NAME2, szOID_SUBJECT_ALT_NAME };
	WCHAR name[320];
	int ii;

	for (ii = 0; ii < 2; ii++) {
		PCERT_EXTENSION ext = CertFindExtension (oids[ii], cert->pCertInfo->cExtension, cert->pCertInfo->rgExtension);
		CERT_ALT_NAME_INFO *info = NULL;
		DWORD cb = 0;

		if (ext && CryptDecodeObjectEx (CRYPTO_ENC, X509_ALTERNATE_NAME, ext->Value.pbData, ext->Value.cbData,
		    CRYPT_DECODE_ALLOC_FLAG, NULL, &info, &cb)) {
			DWORD jj;
			int found = 0;

			for (jj = 0; jj < info->cAltEntry; jj++) {
				if (info->rgAltEntry[jj].dwAltNameChoice == CERT_ALT_NAME_RFC822_NAME &&
				    crypto_email_equal_w (info->rgAltEntry[jj].pwszRfc822Name, email)) {
					found = 1;
					break;
				}
			}
			LocalFree (info);
			if (found)
				return 1;
		}
	}

	return CertGetNameStringW (cert, CERT_NAME_EMAIL_TYPE, 0, NULL, name, 320) > 1 &&
		crypto_email_equal_w (name, email);
}

/* Indique si un certificat est utilisable pour le courrier : dates de validité,
 * usage de clé (signature ou chiffrement) et usage étendu « protection du courrier »
 * (ou absence de restriction), et clé privée associée si demandée. */
static inline int
crypto_cert_usable (PCCERT_CONTEXT cert,
		    int for_signing,
		    int need_private_key)
{
	BYTE usage = 0;
	DWORD cb = 0;

	if (CertVerifyTimeValidity (NULL, cert->pCertInfo) != 0)
		return 0;

	if (CertGetIntendedKeyUsage (CRYPTO_ENC, cert->pCertInfo, &usage, 1)) {
		BYTE wanted = for_signing ?
			(CERT_DIGITAL_SIGNATURE_KEY_USAGE | CERT_NON_REPUDIATION_KEY_USAGE) :
			(CERT_KEY_ENCIPHERMENT_KEY_USAGE | CERT_KEY_AGREEMENT_KEY_USAGE);

		if (!(usage & wanted))
			return 0;
	}

	if (CertGetEnhancedKeyUsage (cert, 0, NULL, &cb) && cb > 0) {
		CERT_ENHKEY_USAGE *eku = malloc (cb);

		if (eku && CertGetEnhancedKeyUsage (cert, 0, eku, &cb) && eku->cUsageIdentifier > 0) {
			DWORD ii;
			int ok = 0;

			for (ii = 0; ii < eku->cUsageIdentifier; ii++) {
				if (strcmp (eku->rgpszUsageIdentifier[ii], szOID_PKIX_KP_EMAIL_PROTECTION) == 0 ||
				    strcmp (eku->rgpszUsageIdentifier[ii], szOID_ANY_ENHANCED_KEY_USAGE) == 0) {
					ok = 1;
					break;
				}
			}
			if (!ok) {
				free (eku);
				return 0;
			}
		}
		free (eku);
	}

	if (need_private_key) {
		DWORD size = 0;

		if (!CertGetCertificateContextProperty (cert, CERT_KEY_PROV_INFO_PROP_ID, NULL, &size))
			return 0;
	}

	return 1;
}

/* Cherche dans un magasin le meilleur certificat (celui qui expire le plus tard)
 * portant l'adresse donnée. Retourne un contexte à libérer avec CertFreeCertificateContext, ou NULL. */
static inline PCCERT_CONTEXT
crypto_find_cert (HCERTSTORE store,
		  const char *email,
		  int for_signing,
		  int need_private_key)
{
	PCCERT_CONTEXT cert = NULL, best = NULL;

	if (!store || !email || !*email)
		return NULL;

	while ((cert = CertEnumCertificatesInStore (store, cert)) != NULL) {
		if (!crypto_cert_has_email (cert, email) || !crypto_cert_usable (cert, for_signing, need_private_key))
			continue;
		if (!best || CompareFileTime (&cert->pCertInfo->NotAfter, &best->pCertInfo->NotAfter) > 0) {
			if (best)
				CertFreeCertificateContext (best);
			best = CertDuplicateCertificateContext (cert);
		}
	}

	return best;
}

/* Crée un certificat personnel auto-signé (RSA 2048, SHA-256, 5 ans) pour une
 * adresse, avec les usages « signature », « chiffrement » et « courrier »,
 * et l'ajoute au magasin. La clé privée reste dans le fournisseur de clés
 * Windows (non exportable en clair). Retourne le certificat du magasin
 * (à libérer), ou NULL (GetLastError donne la cause). */
static inline PCCERT_CONTEXT
crypto_create_self_signed (HCERTSTORE store,
			   const char *email,
			   const char *display_name)
{
	static volatile LONG counter = 0;
	WCHAR wemail[256], wname[256], subject[640], keyname[80];
	CERT_NAME_BLOB subject_blob = { 0, NULL };
	NCRYPT_PROV_HANDLE provider = 0;
	NCRYPT_KEY_HANDLE key = 0;
	BYTE usage_bits = CERT_DIGITAL_SIGNATURE_KEY_USAGE | CERT_NON_REPUDIATION_KEY_USAGE | CERT_KEY_ENCIPHERMENT_KEY_USAGE;
	CRYPT_BIT_BLOB usage_blob = { 1, &usage_bits, 5 };
	LPSTR eku_oid = (LPSTR) szOID_PKIX_KP_EMAIL_PROTECTION;
	CERT_ENHKEY_USAGE eku = { 1, &eku_oid };
	CERT_ALT_NAME_ENTRY alt_entry;
	CERT_ALT_NAME_INFO alt_info = { 1, &alt_entry };
	CERT_EXTENSION exts[3];
	CERT_EXTENSIONS ext_list = { 3, exts };
	BYTE *enc_usage = NULL, *enc_eku = NULL, *enc_alt = NULL;
	DWORD cb_usage = 0, cb_eku = 0, cb_alt = 0, key_length = 2048, error = 0;
	CRYPT_ALGORITHM_IDENTIFIER algorithm;
	CRYPT_KEY_PROV_INFO prov_info;
	SYSTEMTIME end_time;
	PCCERT_CONTEXT created = NULL, stored = NULL;
	WCHAR *clean;
	DWORD cb_name = 0;

	memset (&alt_entry, 0, sizeof (alt_entry));
	memset (&algorithm, 0, sizeof (algorithm));
	memset (&prov_info, 0, sizeof (prov_info));

	if (!MultiByteToWideChar (CP_UTF8, 0, email, -1, wemail, 256))
		return NULL;
	wname[0] = L'\0';
	if (display_name && *display_name)
		MultiByteToWideChar (CP_UTF8, 0, display_name, -1, wname, 256);
	if (!wname[0])
		wcscpy_s (wname, 256, wemail);
	for (clean = wname; *clean; clean++) {
		if (*clean == L'"')
			*clean = L'\'';
	}
	swprintf (subject, 640, L"E=%s, CN=\"%s\"", wemail, wname);

	if (!CertStrToNameW (CRYPTO_ENC, subject, CERT_X500_NAME_STR, NULL, NULL, &cb_name, NULL))
		return NULL;
	subject_blob.pbData = malloc (cb_name);
	subject_blob.cbData = cb_name;
	if (!subject_blob.pbData || !CertStrToNameW (CRYPTO_ENC, subject, CERT_X500_NAME_STR, NULL,
	    subject_blob.pbData, &subject_blob.cbData, NULL)) {
		error = GetLastError ();
		goto out;
	}

	if (NCryptOpenStorageProvider (&provider, MS_KEY_STORAGE_PROVIDER, 0) != ERROR_SUCCESS)
		goto out;

	swprintf (keyname, 80, L"EvolutionWin-smime-%08lx%08lx%04lx", (unsigned long) GetTickCount (),
		(unsigned long) GetCurrentProcessId (), (unsigned long) InterlockedIncrement (&counter));

	if (NCryptCreatePersistedKey (provider, &key, BCRYPT_RSA_ALGORITHM, keyname, 0, 0) != ERROR_SUCCESS ||
	    NCryptSetProperty (key, NCRYPT_LENGTH_PROPERTY, (PBYTE) &key_length, sizeof (key_length), 0) != ERROR_SUCCESS ||
	    NCryptFinalizeKey (key, 0) != ERROR_SUCCESS)
		goto out;

	if (!CryptEncodeObjectEx (CRYPTO_ENC, X509_KEY_USAGE, &usage_blob, CRYPT_ENCODE_ALLOC_FLAG, NULL, &enc_usage, &cb_usage) ||
	    !CryptEncodeObjectEx (CRYPTO_ENC, X509_ENHANCED_KEY_USAGE, &eku, CRYPT_ENCODE_ALLOC_FLAG, NULL, &enc_eku, &cb_eku))
		goto out;

	alt_entry.dwAltNameChoice = CERT_ALT_NAME_RFC822_NAME;
	alt_entry.pwszRfc822Name = wemail;
	if (!CryptEncodeObjectEx (CRYPTO_ENC, X509_ALTERNATE_NAME, &alt_info, CRYPT_ENCODE_ALLOC_FLAG, NULL, &enc_alt, &cb_alt))
		goto out;

	memset (exts, 0, sizeof (exts));
	exts[0].pszObjId = (LPSTR) szOID_KEY_USAGE;
	exts[0].fCritical = TRUE;
	exts[0].Value.cbData = cb_usage;
	exts[0].Value.pbData = enc_usage;
	exts[1].pszObjId = (LPSTR) szOID_ENHANCED_KEY_USAGE;
	exts[1].Value.cbData = cb_eku;
	exts[1].Value.pbData = enc_eku;
	exts[2].pszObjId = (LPSTR) szOID_SUBJECT_ALT_NAME2;
	exts[2].Value.cbData = cb_alt;
	exts[2].Value.pbData = enc_alt;

	GetSystemTime (&end_time);
	end_time.wYear += 5;
	if (end_time.wMonth == 2 && end_time.wDay > 28)
		end_time.wDay = 28;

	algorithm.pszObjId = (LPSTR) szOID_RSA_SHA256RSA;
	prov_info.pwszContainerName = keyname;
	prov_info.pwszProvName = (LPWSTR) MS_KEY_STORAGE_PROVIDER;
	prov_info.dwProvType = 0;
	prov_info.dwKeySpec = CERT_NCRYPT_KEY_SPEC;

	created = CertCreateSelfSignCertificate (key, &subject_blob, 0, &prov_info, &algorithm, NULL, &end_time, &ext_list);
	if (!created) {
		error = GetLastError ();
		goto out;
	}

	if (!CertAddCertificateContextToStore (store, created, CERT_STORE_ADD_ALWAYS, &stored))
		error = GetLastError ();

out:
	if (created)
		CertFreeCertificateContext (created);
	if (!stored && key)
		NCryptDeleteKey (key, 0);
	else if (key)
		NCryptFreeObject (key);
	if (provider)
		NCryptFreeObject (provider);
	if (enc_usage)
		LocalFree (enc_usage);
	if (enc_eku)
		LocalFree (enc_eku);
	if (enc_alt)
		LocalFree (enc_alt);
	free (subject_blob.pbData);
	if (!stored && error)
		SetLastError (error);

	return stored;
}

/* Supprime la clé privée associée à un certificat (nettoyage des tests). */
static inline void
crypto_delete_key_of_cert (PCCERT_CONTEXT cert)
{
	DWORD size = 0;
	CRYPT_KEY_PROV_INFO *info;
	NCRYPT_PROV_HANDLE provider = 0;
	NCRYPT_KEY_HANDLE key = 0;

	if (!CertGetCertificateContextProperty (cert, CERT_KEY_PROV_INFO_PROP_ID, NULL, &size))
		return;
	info = malloc (size);
	if (info && CertGetCertificateContextProperty (cert, CERT_KEY_PROV_INFO_PROP_ID, info, &size) &&
	    NCryptOpenStorageProvider (&provider, info->pwszProvName, 0) == ERROR_SUCCESS) {
		if (NCryptOpenKey (provider, &key, info->pwszContainerName, info->dwKeySpec == CERT_NCRYPT_KEY_SPEC ? 0 : info->dwKeySpec, 0) == ERROR_SUCCESS)
			NCryptDeleteKey (key, 0);
		NCryptFreeObject (provider);
	}
	free (info);
}

/* Signe des données (signature CMS détachée, SHA-256) avec un certificat dont la
 * clé privée est accessible ; joint la chaîne du certificat (sans la racine).
 * Retourne 1 et la signature DER (free) ou 0 (GetLastError). */
static inline int
crypto_sign_detached (PCCERT_CONTEXT cert,
		      const BYTE *data,
		      DWORD len,
		      BYTE **out,
		      DWORD *out_len)
{
	CRYPT_SIGN_MESSAGE_PARA para;
	PCCERT_CONTEXT chain_certs[16];
	PCCERT_CHAIN_CONTEXT chain = NULL;
	CERT_CHAIN_PARA chain_para;
	const BYTE *blobs[1];
	DWORD sizes[1], cb = 0, count = 1, ii;
	int ok = 0;

	chain_certs[0] = cert;
	memset (&chain_para, 0, sizeof (chain_para));
	chain_para.cbSize = sizeof (chain_para);
	if (CertGetCertificateChain (NULL, cert, NULL, NULL, &chain_para, 0, NULL, &chain) &&
	    chain->cChain > 0 && chain->rgpChain[0]->cElement > 1) {
		PCERT_SIMPLE_CHAIN simple = chain->rgpChain[0];
		DWORD n = simple->cElement;
		PCCERT_CONTEXT last = simple->rgpElement[n - 1]->pCertContext;

		if (CertCompareCertificateName (CRYPTO_ENC, &last->pCertInfo->Subject, &last->pCertInfo->Issuer))
			n--;
		if (n > 16)
			n = 16;
		for (ii = 0; ii < n; ii++)
			chain_certs[ii] = simple->rgpElement[ii]->pCertContext;
		count = n;
	}

	memset (&para, 0, sizeof (para));
	para.cbSize = sizeof (para);
	para.dwMsgEncodingType = CRYPTO_ENC;
	para.pSigningCert = cert;
	para.HashAlgorithm.pszObjId = (LPSTR) szOID_NIST_sha256;
	para.cMsgCert = count;
	para.rgpMsgCert = chain_certs;

	blobs[0] = data;
	sizes[0] = len;

	if (CryptSignMessage (&para, TRUE, 1, blobs, sizes, NULL, &cb)) {
		*out = malloc (cb);
		if (*out && CryptSignMessage (&para, TRUE, 1, blobs, sizes, *out, &cb)) {
			*out_len = cb;
			ok = 1;
		} else {
			free (*out);
			*out = NULL;
		}
	}

	if (chain)
		CertFreeCertificateChain (chain);

	return ok;
}

/* Chiffre des données pour des destinataires (enveloppe CMS, AES-256, repli sur
 * 3DES). Retourne 1 et les données DER (free) ou 0 (GetLastError). */
static inline int
crypto_encrypt (PCCERT_CONTEXT *recipients,
		DWORD n_recipients,
		const BYTE *data,
		DWORD len,
		BYTE **out,
		DWORD *out_len)
{
	static const char *const algorithms[] = { szOID_NIST_AES256_CBC, szOID_RSA_DES_EDE3_CBC };
	int ii;

	for (ii = 0; ii < 2; ii++) {
		CRYPT_ENCRYPT_MESSAGE_PARA para;
		DWORD cb = 0;

		memset (&para, 0, sizeof (para));
		para.cbSize = sizeof (para);
		para.dwMsgEncodingType = CRYPTO_ENC;
		para.ContentEncryptionAlgorithm.pszObjId = (LPSTR) algorithms[ii];

		if (CryptEncryptMessage (&para, n_recipients, recipients, data, len, NULL, &cb)) {
			*out = malloc (cb);
			if (*out && CryptEncryptMessage (&para, n_recipients, recipients, data, len, *out, &cb)) {
				*out_len = cb;
				return 1;
			}
			free (*out);
			*out = NULL;
		}
	}

	return 0;
}

/* Déchiffre une enveloppe CMS avec les clés privées des certificats du magasin.
 * Retourne 1 et le texte en clair (free) ou 0 (GetLastError, p. ex. CRYPT_E_NO_DECRYPT_CERT). */
static inline int
crypto_decrypt (HCERTSTORE store,
		const BYTE *blob,
		DWORD len,
		BYTE **out,
		DWORD *out_len)
{
	CRYPT_DECRYPT_MESSAGE_PARA para;
	DWORD cb = 0;

	memset (&para, 0, sizeof (para));
	para.cbSize = sizeof (para);
	para.dwMsgAndCertEncodingType = CRYPTO_ENC;
	para.cCertStore = 1;
	para.rghCertStore = &store;

	if (!CryptDecryptMessage (&para, blob, len, NULL, &cb, NULL))
		return 0;
	*out = malloc (cb ? cb : 1);
	if (!*out)
		return 0;
	if (!CryptDecryptMessage (&para, blob, len, *out, &cb, NULL)) {
		DWORD error = GetLastError ();

		free (*out);
		*out = NULL;
		SetLastError (error);
		return 0;
	}
	*out_len = cb;

	return 1;
}

/* Évalue l'approbation de la chaîne d'un certificat de signataire. */
static inline void
crypto_check_trust (CryptoVerifyResult *result)
{
	CERT_CHAIN_PARA chain_para;
	PCCERT_CHAIN_CONTEXT chain = NULL;

	result->trusted = 0;
	result->trust_error = 0;
	if (!result->signer)
		return;

	memset (&chain_para, 0, sizeof (chain_para));
	chain_para.cbSize = sizeof (chain_para);
	if (CertGetCertificateChain (NULL, result->signer, NULL, result->signer->hCertStore, &chain_para, 0, NULL, &chain)) {
		CERT_CHAIN_POLICY_PARA policy_para;
		CERT_CHAIN_POLICY_STATUS status;

		memset (&policy_para, 0, sizeof (policy_para));
		policy_para.cbSize = sizeof (policy_para);
		memset (&status, 0, sizeof (status));
		status.cbSize = sizeof (status);
		if (CertVerifyCertificateChainPolicy (CERT_CHAIN_POLICY_BASE, chain, &policy_para, &status)) {
			result->trusted = status.dwError == 0;
			result->trust_error = status.dwError;
		}
		CertFreeCertificateChain (chain);
	}
}

/* Vérifie une signature détachée sur des données. */
static inline void
crypto_verify_detached (const BYTE *sig,
			DWORD sig_len,
			const BYTE *data,
			DWORD len,
			CryptoVerifyResult *result)
{
	CRYPT_VERIFY_MESSAGE_PARA para;
	const BYTE *blobs[1];
	DWORD sizes[1];

	memset (result, 0, sizeof (*result));
	memset (&para, 0, sizeof (para));
	para.cbSize = sizeof (para);
	para.dwMsgAndCertEncodingType = CRYPTO_ENC;
	blobs[0] = data;
	sizes[0] = len;

	result->signature_ok = CryptVerifyDetachedMessageSignature (&para, 0, sig, sig_len, 1, blobs, sizes, &result->signer) ? 1 : 0;
	if (result->signature_ok)
		crypto_check_trust (result);
}

/* Vérifie une signature opaque (signed-data) et en extrait le contenu (free). */
static inline void
crypto_verify_opaque (const BYTE *blob,
		      DWORD len,
		      BYTE **content,
		      DWORD *content_len,
		      CryptoVerifyResult *result)
{
	CRYPT_VERIFY_MESSAGE_PARA para;
	DWORD cb = 0;

	memset (result, 0, sizeof (*result));
	*content = NULL;
	*content_len = 0;
	memset (&para, 0, sizeof (para));
	para.cbSize = sizeof (para);
	para.dwMsgAndCertEncodingType = CRYPTO_ENC;

	if (!CryptVerifyMessageSignature (&para, 0, blob, len, NULL, &cb, NULL))
		return;
	*content = malloc (cb ? cb : 1);
	if (!*content)
		return;
	if (CryptVerifyMessageSignature (&para, 0, blob, len, *content, &cb, &result->signer)) {
		result->signature_ok = 1;
		*content_len = cb;
		crypto_check_trust (result);
	} else {
		free (*content);
		*content = NULL;
	}
}

/* Libellé « Nom <adresse> » d'un certificat (UTF-8) dans buf. */
static inline void
crypto_cert_label (PCCERT_CONTEXT cert,
		   char *buf,
		   size_t size)
{
	WCHAR name[256], mail[320];
	char name8[512] = "", mail8[512] = "";

	if (!cert || !size)
		return;
	buf[0] = '\0';

	if (CertGetNameStringW (cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name, 256) > 1)
		WideCharToMultiByte (CP_UTF8, 0, name, -1, name8, sizeof (name8), NULL, NULL);
	if (CertGetNameStringW (cert, CERT_NAME_EMAIL_TYPE, 0, NULL, mail, 320) > 1)
		WideCharToMultiByte (CP_UTF8, 0, mail, -1, mail8, sizeof (mail8), NULL, NULL);

	if (name8[0] && mail8[0] && _stricmp (name8, mail8) != 0)
		_snprintf_s (buf, size, _TRUNCATE, "%s <%s>", name8, mail8);
	else
		_snprintf_s (buf, size, _TRUNCATE, "%s", mail8[0] ? mail8 : name8);
}

#endif /* EVO_CORE_CRYPTO_UTIL_H */
