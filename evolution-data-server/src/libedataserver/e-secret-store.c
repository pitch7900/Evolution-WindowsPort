/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: e-secret-store
 * @include: libedataserver/libedataserver.h
 * @short_description: Interface to store secrets
 *
 * The e-secret-store API provides an interface to store,
 * lookup and delete secrets from the keyring.
 **/

#include "evolution-data-server-config.h"

#include <glib.h>

#ifdef G_OS_WIN32
#include <string.h>
#include <windows.h>
#include <wincred.h>
#include <gio/gio.h>
#include <errno.h>
#include <stdlib.h>
#else
#include <libsecret/secret.h>
#endif

#include "e-data-server-util.h"
#include "e-secret-store.h"

#ifdef G_OS_WIN32

/* Permanent secrets are kept in the Windows Credential Manager (DPAPI
 * protected, per user), session secrets only in memory. The old base64
 * "secrets" key file, used by previous Win32 builds, is read as a fallback
 * and its entries are migrated to the Credential Manager on lookup.
 *
 * A credential blob holds at most CRED_MAX_CREDENTIAL_BLOB_SIZE (2560)
 * bytes, less than the OAuth2 tokens of Microsoft 365 work accounts:
 * longer secrets are split in "<target>#1".."#n" credentials and the main
 * one holds CHUNKS_MARKER followed by n. */
#define CHUNKS_MARKER "\001EDS-CHUNKS:"

G_LOCK_DEFINE_STATIC (secrets_file);
static GHashTable *session_secrets = NULL;
#define SECRETS_SECTION "Secrets"
#define CREDENTIAL_TARGET_PREFIX "Evolution-Data-Server:"

/* index 0: main credential, 1..n: chunks of a long secret */
static gunichar2 *
win32_credential_target (const gchar *uid,
			 guint index)
{
	gchar *target;
	gunichar2 *wtarget;

	if (index)
		target = g_strdup_printf (CREDENTIAL_TARGET_PREFIX "%s#%u", uid, index);
	else
		target = g_strconcat (CREDENTIAL_TARGET_PREFIX, uid, NULL);
	wtarget = g_utf8_to_utf16 (target, -1, NULL, NULL, NULL);
	g_free (target);

	return wtarget;
}

static void
win32_set_error_from_last_error (GError **error,
				 const gchar *what)
{
	DWORD err = GetLastError ();
	gchar *msg;

	msg = g_win32_error_message (err);
	g_set_error (error, G_IO_ERROR, g_io_error_from_win32_error (err), "%s: %s", what, msg);
	g_free (msg);
}

static gchar *
get_secrets_filename (void)
{
	return g_build_filename (e_get_user_config_dir (), "secrets", NULL);
}

static gchar *
decode_secret (const gchar *secret)
{
	guchar *decoded;
	gchar *tmp;
	gsize len = 0;

	decoded = g_base64_decode (secret, &len);
	if (!decoded || !len) {
		g_free (decoded);
		return NULL;
	}

	tmp = g_strndup ((const gchar *) decoded, len);
	e_util_safe_free_string ((gchar *) decoded);

	return tmp;
}

/* Reads (and optionally removes) the secret from the legacy key file */
static gchar *
legacy_secrets_file_take (const gchar *uid,
			  gboolean remove)
{
	GKeyFile *secrets;
	gchar *filename, *tmp, *secret = NULL;

	filename = get_secrets_filename ();

	if (!g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
		g_free (filename);
		return NULL;
	}

	secrets = g_key_file_new ();

	if (g_key_file_load_from_file (secrets, filename, G_KEY_FILE_NONE, NULL)) {
		tmp = g_key_file_get_string (secrets, SECRETS_SECTION, uid, NULL);
		if (tmp) {
			secret = *tmp ? decode_secret (tmp) : g_strdup ("");
			e_util_safe_free_string (tmp);

			if (remove && g_key_file_remove_key (secrets, SECRETS_SECTION, uid, NULL))
				g_key_file_save_to_file (secrets, filename, NULL);
		}
	}

	g_key_file_free (secrets);
	g_free (filename);

	return secret;
}

static gboolean
win32_credential_write_blob (const gchar *uid,
			     guint index,
			     const gchar *data,
			     gsize length,
			     GError **error)
{
	CREDENTIALW cred = { 0 };
	gunichar2 *target, *user;
	gboolean success;

	target = win32_credential_target (uid, index);
	user = g_utf8_to_utf16 (uid, -1, NULL, NULL, NULL);

	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = (LPWSTR) target;
	cred.UserName = (LPWSTR) user;
	cred.CredentialBlobSize = (DWORD) length;
	cred.CredentialBlob = (LPBYTE) data;
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

	success = CredWriteW (&cred, 0) != FALSE;
	if (!success)
		win32_set_error_from_last_error (error, "CredWrite");

	g_free (target);
	g_free (user);

	return success;
}

/* Returns the blob of a credential, or NULL (error left unset when it
 * does not exist). */
static gchar *
win32_credential_read_blob (const gchar *uid,
			    guint index,
			    GError **error)
{
	PCREDENTIALW cred = NULL;
	gunichar2 *target;
	gchar *blob = NULL;

	target = win32_credential_target (uid, index);

	if (CredReadW ((LPCWSTR) target, CRED_TYPE_GENERIC, 0, &cred)) {
		blob = g_strndup ((const gchar *) cred->CredentialBlob, cred->CredentialBlobSize);
		SecureZeroMemory (cred->CredentialBlob, cred->CredentialBlobSize);
		CredFree (cred);
	} else if (GetLastError () != ERROR_NOT_FOUND) {
		win32_set_error_from_last_error (error, "CredRead");
	}

	g_free (target);

	return blob;
}

/* Deletes the chunks from "#first" on, up to the first missing one. */
static void
win32_credential_delete_chunks (const gchar *uid,
				guint first)
{
	gboolean deleted = TRUE;
	guint index;

	for (index = first; deleted; index++) {
		gunichar2 *target = win32_credential_target (uid, index);

		deleted = CredDeleteW ((LPCWSTR) target, CRED_TYPE_GENERIC, 0) != FALSE;
		g_free (target);
	}
}

static gboolean
win32_credential_write (const gchar *uid,
			const gchar *secret,
			GError **error)
{
	gsize length = strlen (secret), n_chunks, index;
	gchar *header;
	gboolean success;

	if (length <= CRED_MAX_CREDENTIAL_BLOB_SIZE && !g_str_has_prefix (secret, CHUNKS_MARKER)) {
		success = win32_credential_write_blob (uid, 0, secret, length, error);
		if (success)
			win32_credential_delete_chunks (uid, 1);
		return success;
	}

	n_chunks = (length + CRED_MAX_CREDENTIAL_BLOB_SIZE - 1) / CRED_MAX_CREDENTIAL_BLOB_SIZE;

	for (index = 1; index <= n_chunks; index++) {
		gsize offset = (index - 1) * CRED_MAX_CREDENTIAL_BLOB_SIZE;

		if (!win32_credential_write_blob (uid, (guint) index, secret + offset,
			MIN (CRED_MAX_CREDENTIAL_BLOB_SIZE, length - offset), error))
			return FALSE;
	}

	win32_credential_delete_chunks (uid, (guint) n_chunks + 1);

	header = g_strdup_printf (CHUNKS_MARKER "%u", (guint) n_chunks);
	success = win32_credential_write_blob (uid, 0, header, strlen (header), error);
	g_free (header);

	return success;
}

/* Returns the secret of uid, reassembling its chunks, or NULL (error left
 * unset when there is none). */
static gchar *
win32_credential_read (const gchar *uid,
		       GError **error)
{
	GString *secret;
	gchar *blob;
	guint n_chunks, index;

	blob = win32_credential_read_blob (uid, 0, error);
	if (!blob || !g_str_has_prefix (blob, CHUNKS_MARKER))
		return blob;

	n_chunks = (guint) strtoul (blob + strlen (CHUNKS_MARKER), NULL, 10);
	g_free (blob);

	secret = g_string_new (NULL);

	for (index = 1; index <= n_chunks; index++) {
		blob = win32_credential_read_blob (uid, index, error);
		if (!blob) {
			if (error && !*error)
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
					"CredRead: part %u of %u of the secret is missing", index, n_chunks);
			e_util_safe_free_string (g_string_free (secret, FALSE));
			return NULL;
		}
		g_string_append (secret, blob);
		e_util_safe_free_string (blob);
	}

	return g_string_free (secret, FALSE);
}

static gboolean
e_win32_secret_store_secret_sync (const gchar *uid,
				  const gchar *secret,
				  gboolean permanently,
				  GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (uid != NULL, FALSE);

	G_LOCK (secrets_file);

	if (permanently) {
		success = win32_credential_write (uid, secret ? secret : "", error);
		e_util_safe_free_string (legacy_secrets_file_take (uid, TRUE));
	} else {
		if (!session_secrets)
			session_secrets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) e_util_safe_free_string);

		if (!secret)
			g_hash_table_remove (session_secrets, uid);
		else
			g_hash_table_insert (session_secrets, g_strdup (uid), g_strdup (secret));
	}

	G_UNLOCK (secrets_file);

	return success;
}

static gchar *
e_win32_secret_lookup_secret_sync (const gchar *uid,
				   GError **error)
{
	gchar *secret = NULL;

	g_return_val_if_fail (uid != NULL, NULL);

	G_LOCK (secrets_file);

	if (session_secrets)
		secret = g_strdup (g_hash_table_lookup (session_secrets, uid));

	if (!secret)
		secret = win32_credential_read (uid, error);

	if (!secret && !(error && *error)) {
		/* Migrate from the legacy plain key file */
		secret = legacy_secrets_file_take (uid, FALSE);
		if (secret && win32_credential_write (uid, secret, NULL))
			e_util_safe_free_string (legacy_secrets_file_take (uid, TRUE));
	}

	G_UNLOCK (secrets_file);

	return secret;
}

static gboolean
e_win32_secret_delete_secret_sync (const gchar *uid,
				   GError **error)
{
	gunichar2 *target;
	gboolean success = TRUE;

	g_return_val_if_fail (uid != NULL, FALSE);

	G_LOCK (secrets_file);

	if (session_secrets)
		g_hash_table_remove (session_secrets, uid);

	target = win32_credential_target (uid, 0);

	if (!CredDeleteW ((LPCWSTR) target, CRED_TYPE_GENERIC, 0) && GetLastError () != ERROR_NOT_FOUND) {
		win32_set_error_from_last_error (error, "CredDelete");
		success = FALSE;
	}

	g_free (target);

	win32_credential_delete_chunks (uid, 1);

	e_util_safe_free_string (legacy_secrets_file_take (uid, TRUE));

	G_UNLOCK (secrets_file);

	return success;
}
#else /* G_OS_WIN32 */

#define KEYRING_ITEM_ATTRIBUTE_UID	"e-source-uid"
#define KEYRING_ITEM_ATTRIBUTE_ORIGIN	"eds-origin"

#ifdef DBUS_SERVICES_PREFIX
#define ORIGIN_KEY DBUS_SERVICES_PREFIX "." PACKAGE
#else
#define ORIGIN_KEY PACKAGE
#endif

static SecretSchema password_schema = {
	"org.gnome.Evolution.Data.Source",
	SECRET_SCHEMA_DONT_MATCH_NAME,
	{
		{ KEYRING_ITEM_ATTRIBUTE_UID, SECRET_SCHEMA_ATTRIBUTE_STRING },
		{ KEYRING_ITEM_ATTRIBUTE_ORIGIN, SECRET_SCHEMA_ATTRIBUTE_STRING },
		{ NULL, 0 }
	}
};

#endif /* G_OS_WIN32 */

/**
 * e_secret_store_store_sync:
 * @uid: a unique identifier of the secret
 * @secret: the secret to store
 * @label: human readable description of the secret
 * @permanently: store permanently or just for the session
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Stores the @secret for the @uid.
 *
 * If @permanently is %TRUE, the secret is stored in the default keyring.
 * Otherwise the secret is stored in the memory-only session keyring. If
 * an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.18
 **/
gboolean
e_secret_store_store_sync (const gchar *uid,
			   const gchar *secret,
			   const gchar *label,
			   gboolean permanently,
			   GCancellable *cancellable,
			   GError **error)
{
	gboolean success;
#ifndef G_OS_WIN32
	const gchar *collection;
#endif

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (secret != NULL, FALSE);

#ifndef G_OS_WIN32
	if (permanently)
		collection = SECRET_COLLECTION_DEFAULT;
	else
		collection = SECRET_COLLECTION_SESSION;
#endif

#ifdef G_OS_WIN32
	success = e_win32_secret_store_secret_sync (uid, secret, permanently, error);
#else
	success = secret_password_store_sync (
		&password_schema,
		collection, label, secret,
		cancellable, error,
		KEYRING_ITEM_ATTRIBUTE_UID, uid,
		KEYRING_ITEM_ATTRIBUTE_ORIGIN, ORIGIN_KEY,
		NULL);
#endif

	return success;
}

/**
 * e_secret_store_lookup_sync:
 * @uid: a unique identifier of the secret
 * @out_secret: (out): return location for the secret, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Looks up a secret for the @uid. Both the default and session keyrings
 * are queried.
 *
 * Note the boolean return value indicates whether the lookup operation
 * itself completed successfully, not whether the secret was found. If
 * no secret was found, the function will set @out_secret to %NULL,
 * but still return %TRUE. If an error occurs, the function sets @error
 * and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.18
 **/
gboolean
e_secret_store_lookup_sync (const gchar *uid,
			    gchar **out_secret,
			    GCancellable *cancellable,
			    GError **error)
{
	gchar *temp = NULL;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (uid != NULL, FALSE);

#ifdef G_OS_WIN32
	temp = e_win32_secret_lookup_secret_sync (uid, &local_error);
#else
	temp = secret_password_lookup_sync (
		&password_schema,
		cancellable, &local_error,
		KEYRING_ITEM_ATTRIBUTE_UID, uid,
		NULL);
#endif

	if (local_error != NULL) {
		g_warn_if_fail (temp == NULL);
		g_propagate_error (error, local_error);
		success = FALSE;
	} else if (out_secret != NULL) {
		*out_secret = temp;  /* takes ownership */
	} else {
		e_util_safe_free_string (temp);
	}

	return success;
}

/**
 * e_secret_store_delete_sync:
 * @uid: a unique identifier of the secret
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes the secret for @uid from either the default keyring or
 * session keyring.
 *
 * Note the boolean return value indicates whether the delete operation
 * itself completed successfully, not whether the secret was found and
 * deleted. If no such secret was found, the function will still return
 * %TRUE. If an error occurs, the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.18
 **/
gboolean
e_secret_store_delete_sync (const gchar *uid,
			    GCancellable *cancellable,
			    GError **error)
{
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (uid != NULL, FALSE);

#ifdef G_OS_WIN32
	e_win32_secret_delete_secret_sync (uid, &local_error);
#else
	/* The return value indicates whether any passwords were removed,
	 * not whether the operation completed successfully.  So we have
	 * to check the GError directly. */
	secret_password_clear_sync (
		&password_schema,
		cancellable, &local_error,
		KEYRING_ITEM_ATTRIBUTE_UID, uid,
		NULL);
#endif

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		success = FALSE;
	}

	return success;
}
