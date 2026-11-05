/*
 * evo-core : Envoi des messages, construction MIME et Boite d'envoi.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"
#include "evo-core-mdn.h"

/* identity (sender) and submission settings of an account */
ESource *
core_ref_identity (EvoCore *core,
		   const gchar *account_uid)
{
	ESource *account, *identity = NULL;

	account = e_source_registry_ref_source (core->registry, account_uid);

	if (account && e_source_has_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		gchar *identity_uid = e_source_mail_account_dup_identity_uid (
			e_source_get_extension (account, E_SOURCE_EXTENSION_MAIL_ACCOUNT));

		if (identity_uid)
			identity = e_source_registry_ref_source (core->registry, identity_uid);
		g_free (identity_uid);
	}

	g_clear_object (&account);

	return identity;
}

/* Analyse et valide une liste d'adresses de destinataires (À, Cc, Cci).
 * Remplace les séparateurs point-virgule ';' par des virgules ',', retire les
 * délimiteurs superflus ou traînants et filtre les entrées invalides (comme ';')
 * pour éviter l'erreur SMTP "RCPT TO <;>".
 * Paramètres :
 *   object : objet JSON contenant le champ
 *   member : nom du champ ("to", "cc", "bcc")
 * Retourne : un objet CamelInternetAddress * nettoyé et validé. */
static CamelInternetAddress *
parse_addresses (JsonObject *object,
		 const gchar *member)
{
	const gchar *raw_value = json_object_get_string_member_with_default (object, member, "");
	CamelInternetAddress *addresses = camel_internet_address_new ();
	gchar *sanitized, *p;
	gboolean in_quote = FALSE;
	CamelInternetAddress *temp;
	gint len, i;

	if (!raw_value || !*raw_value)
		return addresses;

	sanitized = g_strdup (raw_value);

	/* Remplace les ';' par des ',' hors des guillemets (standard Windows / Outlook) */
	for (p = sanitized; *p; p++) {
		if (*p == '"')
			in_quote = !in_quote;
		else if (*p == ';' && !in_quote)
			*p = ',';
	}

	/* Supprime les espaces et séparateurs finaux */
	p = sanitized + strlen (sanitized) - 1;
	while (p >= sanitized && (g_ascii_isspace (*p) || *p == ',' || *p == ';')) {
		*p = '\0';
		p--;
	}

	temp = camel_internet_address_new ();
	if (*sanitized)
		camel_address_unformat (CAMEL_ADDRESS (temp), sanitized);

	len = camel_address_length (CAMEL_ADDRESS (temp));
	for (i = 0; i < len; i++) {
		const gchar *name = NULL, *addr = NULL;
		if (camel_internet_address_get (temp, i, &name, &addr) && addr && *addr) {
			/* Ne conserver que les adresses contenant un '@' et non réductibles à de la ponctuation */
			if (strchr (addr, '@') != NULL && strcmp (addr, ";") != 0 && strcmp (addr, ",") != 0) {
				camel_internet_address_add (addresses, name, addr);
			}
		}
	}

	g_object_unref (temp);
	g_free (sanitized);

	return addresses;
}

/* Construit la partie MIME contenant le corps du message :
 * - si html est fourni et non vide, crée un conteneur multipart/alternative
 *   combinant la version text/plain et la version text/html (UTF-8,
 *   quoted-printable) ;
 * - sinon, crée une simple partie text/plain (UTF-8, quoted-printable).
 * Paramètres :
 *   text : corps du message en texte brut (peut être vide si html est fourni)
 *   html : corps du message en HTML (peut être NULL ou vide)
 * Retourne : un nouvel objet CamelMimePart * ou CamelMultipart * encapsulé
 *            dans un CamelMimePart * prêt à être attaché au message. */
/* Remplace les images intégrées en adresses data:image/...;base64,... (images
 * d'une signature HTML, par exemple) par des références cid: vers des parties
 * MIME, mieux acceptées par les clients de messagerie.
 * Paramètres :
 *   html  : document HTML
 *   parts : tableau recevant les CamelMimePart * des images (avec Content-ID)
 * Retourne : nouveau HTML (à libérer avec g_free). */
static gchar *
extract_data_images (const gchar *html,
		     GPtrArray *parts)
{
	GString *out = g_string_sized_new (strlen (html));
	const gchar *pos = html, *found;

	while ((found = strstr (pos, "data:image/")) != NULL) {
		gchar quote = found > html ? found[-1] : 0;
		const gchar *marker, *data_start, *data_end;
		gchar *mime_type, *encoded;
		guchar *decoded;
		gsize length = 0;
		CamelMimePart *part;

		marker = strstr (found, ";base64,");
		data_start = marker ? marker + strlen (";base64,") : NULL;
		data_end = data_start && (quote == '"' || quote == '\'') ? strchr (data_start, quote) : NULL;

		/* pas une valeur d'attribut entre guillemets : laissée telle quelle */
		if (!data_end || memchr (found, quote, marker - found)) {
			g_string_append_len (out, pos, found + 1 - pos);
			pos = found + 1;
			continue;
		}

		mime_type = g_strndup (found + strlen ("data:"), marker - found - strlen ("data:"));
		encoded = g_strndup (data_start, data_end - data_start);
		decoded = g_base64_decode (encoded, &length);

		if (!decoded || !length) {
			g_free (decoded);
			g_free (encoded);
			g_free (mime_type);
			g_string_append_len (out, pos, data_end - pos);
			pos = data_end;
			continue;
		}

		part = camel_mime_part_new ();
		camel_mime_part_set_content (part, (const gchar *) decoded, length, mime_type);
		camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_BASE64);
		camel_mime_part_set_disposition (part, "inline");
		camel_mime_part_set_content_id (part, NULL);
		g_ptr_array_add (parts, part);

		g_string_append_len (out, pos, found - pos);
		g_string_append (out, "cid:");
		g_string_append (out, camel_mime_part_get_content_id (part));
		pos = data_end;

		g_free (decoded);
		g_free (encoded);
		g_free (mime_type);
	}

	g_string_append (out, pos);

	return g_string_free (out, FALSE);
}

static void
set_message_or_part_body (CamelMedium *target_medium,
			  const gchar *text,
			  const gchar *html)
{
	gchar *fallback_text = NULL;
	const gchar *plain_text = text ? text : "";

	if (html && *html) {
		CamelMultipart *alt = camel_multipart_new ();
		CamelMimePart *plain_part = camel_mime_part_new ();
		CamelMimePart *html_part = camel_mime_part_new ();

		if (!*plain_text) {
			fallback_text = html_to_text (html);
			plain_text = fallback_text ? fallback_text : "";
		}

		camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (alt), "multipart/alternative");
		camel_multipart_set_boundary (alt, NULL);

		camel_mime_part_set_content (plain_part, plain_text, strlen (plain_text), "text/plain; charset=utf-8");
		camel_mime_part_set_encoding (plain_part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
		camel_multipart_add_part (alt, plain_part);

		if (strstr (html, "data:image/")) {
			/* images intégrées : multipart/related (HTML + images cid:) */
			GPtrArray *images = g_ptr_array_new_with_free_func (g_object_unref);
			gchar *related_html = extract_data_images (html, images);

			camel_mime_part_set_content (html_part, related_html, strlen (related_html), "text/html; charset=utf-8");
			camel_mime_part_set_encoding (html_part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);

			if (images->len > 0) {
				CamelMultipart *related = camel_multipart_new ();
				CamelMimePart *related_part = camel_mime_part_new ();
				guint ii;

				camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (related), "multipart/related; type=\"text/html\"");
				camel_multipart_set_boundary (related, NULL);
				camel_multipart_add_part (related, html_part);
				for (ii = 0; ii < images->len; ii++)
					camel_multipart_add_part (related, images->pdata[ii]);

				camel_medium_set_content (CAMEL_MEDIUM (related_part), CAMEL_DATA_WRAPPER (related));
				camel_multipart_add_part (alt, related_part);

				g_object_unref (related_part);
				g_object_unref (related);
			} else {
				camel_multipart_add_part (alt, html_part);
			}

			g_ptr_array_unref (images);
			g_free (related_html);
		} else {
			camel_mime_part_set_content (html_part, html, strlen (html), "text/html; charset=utf-8");
			camel_mime_part_set_encoding (html_part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
			camel_multipart_add_part (alt, html_part);
		}

		camel_medium_set_content (target_medium, CAMEL_DATA_WRAPPER (alt));

		g_object_unref (html_part);
		g_object_unref (plain_part);
		g_object_unref (alt);
		g_free (fallback_text);
	} else {
		camel_mime_part_set_content (CAMEL_MIME_PART (target_medium),
			plain_text, strlen (plain_text), "text/plain; charset=utf-8");
		camel_mime_part_set_encoding (CAMEL_MIME_PART (target_medium),
			CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
	}
}

static gchar *
core_send_message_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	JsonNode *root;
	JsonObject *object;
	ESource *identity = NULL;
	ESourceMailIdentity *identity_extension;
	ESourceMailSubmission *submission;
	CamelInternetAddress *from = NULL, *to = NULL, *cc = NULL, *bcc = NULL, *recipients = NULL;
	CamelMimeMessage *message = NULL;
	CamelService *transport = NULL;
	const gchar *account_uid, *subject, *text, *html, *header;
	gboolean saved = FALSE, sent_message_saved = FALSE;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account_uid = object ? json_object_get_string_member_with_default (object, "account", NULL) : NULL;

	identity = account_uid ? core_ref_identity (core, account_uid) : NULL;
	if (!identity || !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY) ||
	    !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "This account has no sender identity");
		goto exit;
	}

	identity_extension = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
	submission = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION);

	transport = camel_session_ref_service (core->session, e_source_mail_submission_get_transport_uid (submission));
	if (!CAMEL_IS_TRANSPORT (transport)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "This account has no outgoing (SMTP) server");
		goto exit;
	}

	from = camel_internet_address_new ();
	camel_internet_address_add (from,
		e_source_mail_identity_get_name (identity_extension),
		e_source_mail_identity_get_address (identity_extension));

	to = parse_addresses (object, "to");
	cc = parse_addresses (object, "cc");
	bcc = parse_addresses (object, "bcc");

	recipients = camel_internet_address_new ();
	camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (to));
	camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (cc));
	camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (bcc));

	if (camel_address_length (CAMEL_ADDRESS (recipients)) == 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No recipient");
		goto exit;
	}

	subject = json_object_get_string_member_with_default (object, "subject", "");
	text = json_object_get_string_member_with_default (object, "text", "");
	html = json_object_get_string_member_with_default (object, "html", "");

	message = camel_mime_message_new ();
	camel_mime_message_set_subject (message, subject);
	camel_mime_message_set_from (message, from);
	camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to);
	if (camel_address_length (CAMEL_ADDRESS (cc)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_CC, cc);
	/* Bcc is not written in the message: recipients of the transport only */
	camel_mime_message_set_date (message, CAMEL_MESSAGE_DATE_CURRENT, 0);
	camel_mime_message_set_message_id (message, NULL);

	if (e_source_mail_identity_get_reply_to (identity_extension) &&
	    *e_source_mail_identity_get_reply_to (identity_extension)) {
		CamelInternetAddress *reply_to = camel_internet_address_new ();

		camel_address_unformat (CAMEL_ADDRESS (reply_to), e_source_mail_identity_get_reply_to (identity_extension));
		camel_mime_message_set_reply_to (message, reply_to);
		g_object_unref (reply_to);
	}

	header = json_object_get_string_member_with_default (object, "in_reply_to", "");
	if (header && *header)
		camel_medium_set_header (CAMEL_MEDIUM (message), "In-Reply-To", header);

	header = json_object_get_string_member_with_default (object, "references", "");
	if (header && *header)
		camel_medium_set_header (CAMEL_MEDIUM (message), "References", header);

	camel_medium_set_header (CAMEL_MEDIUM (message), "X-Mailer", "Evolution for Windows");

	/* Accusé de réception et confirmation de lecture (RFC 3798 / RFC 2298) */
	if (json_object_get_boolean_member_with_default (object, "request_read_receipt", FALSE)) {
		const gchar *sender_addr = e_source_mail_identity_get_address (identity_extension);
		if (sender_addr && *sender_addr) {
			camel_medium_set_header (CAMEL_MEDIUM (message), "Disposition-Notification-To", sender_addr);
			camel_medium_set_header (CAMEL_MEDIUM (message), "Return-Receipt-To", sender_addr);
		}
	}

	if (json_object_has_member (object, "attachments")) {
		JsonArray *attachments = json_object_get_array_member (object, "attachments");
		guint n_att = attachments ? json_array_get_length (attachments) : 0;

		if (n_att > 0) {
			CamelMultipart *multipart = camel_multipart_new ();
			CamelMimePart *body_part = camel_mime_part_new ();
			guint ii;

			camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart), "multipart/mixed");
			camel_multipart_set_boundary (multipart, NULL);

			set_message_or_part_body (CAMEL_MEDIUM (body_part), text, html);
			camel_multipart_add_part (multipart, body_part);
			g_object_unref (body_part);

			for (ii = 0; ii < n_att; ii++) {
				const gchar *path = json_array_get_string_element (attachments, ii);
				gchar *contents = NULL, *basename = NULL, *guessed = NULL, *mime_type = NULL;
				gsize length = 0;
				CamelMimePart *att_part;

				if (!path || !*path)
					continue;

				if (!g_file_get_contents (path, &contents, &length, error)) {
					g_object_unref (multipart);
					goto exit;
				}

				basename = g_path_get_basename (path);
				if (g_ascii_strcasecmp (basename + MAX ((gssize) strlen (basename) - 4, 0), ".eml") == 0) {
					mime_type = g_strdup ("message/rfc822");
				} else {
					guessed = g_content_type_guess (basename, (const guchar *) contents, length, NULL);
					mime_type = guessed ? g_content_type_get_mime_type (guessed) : NULL;
				}

				att_part = camel_mime_part_new ();
				camel_mime_part_set_content (att_part, contents, length,
					(mime_type && *mime_type) ? mime_type : "application/octet-stream");
				camel_mime_part_set_disposition (att_part, "attachment");
				camel_mime_part_set_filename (att_part, basename);
				camel_mime_part_set_encoding (att_part, CAMEL_TRANSFER_ENCODING_BASE64);
				camel_multipart_add_part (multipart, att_part);

				g_object_unref (att_part);
				g_free (mime_type);
				g_free (guessed);
				g_free (basename);
				g_free (contents);
			}

			camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (multipart));
			g_object_unref (multipart);
		} else {
			set_message_or_part_body (CAMEL_MEDIUM (message), text, html);
		}
	} else {
		set_message_or_part_body (CAMEL_MEDIUM (message), text, html);
	}

	/* Signature et/ou chiffrement cryptographique OpenPGP (Gpg4win) ou S/MIME (NSS) */
	{
		gboolean pgp_sign = json_object_get_boolean_member_with_default (object, "pgp_sign", FALSE);
		gboolean pgp_encrypt = json_object_get_boolean_member_with_default (object, "pgp_encrypt", FALSE);
		gboolean smime_sign = json_object_get_boolean_member_with_default (object, "smime_sign", FALSE);
		gboolean smime_encrypt = json_object_get_boolean_member_with_default (object, "smime_encrypt", FALSE);
		const gchar *sender_addr = e_source_mail_identity_get_address (identity_extension);
		const gchar *pgp_key_id = json_object_get_string_member_with_default (object, "pgp_key_id", NULL);
		const gchar *smime_key_id = json_object_get_string_member_with_default (object, "smime_key_id", NULL);

		if (pgp_sign || pgp_encrypt || smime_sign || smime_encrypt) {
			CamelMimePart *work_part = camel_mime_part_new ();
			CamelDataWrapper *orig_dw = camel_medium_get_content (CAMEL_MEDIUM (message));
			CamelContentType *orig_ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));
			const gchar *userid_pgp = (pgp_key_id && *pgp_key_id) ? pgp_key_id : sender_addr;
			const gchar *userid_smime = (smime_key_id && *smime_key_id) ? smime_key_id : sender_addr;

			if (orig_ct) {
				gchar *ct_str = camel_content_type_format (orig_ct);
				camel_mime_part_set_content_type (work_part, ct_str);
				g_free (ct_str);
			}
			camel_mime_part_set_encoding (work_part,
				camel_mime_part_get_encoding (CAMEL_MIME_PART (message)));
			if (orig_dw)
				camel_medium_set_content (CAMEL_MEDIUM (work_part), orig_dw);

			/* 1. Signature OpenPGP */
			if (pgp_sign) {
				CamelCipherContext *ctx = camel_gpg_context_new (core->session);
				CamelMimePart *signed_part = camel_mime_part_new ();

				camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);
				if (!camel_cipher_context_sign_sync (
					ctx, userid_pgp, CAMEL_CIPHER_HASH_SHA256,
					work_part, signed_part, core->cancellable, error)) {
					g_object_unref (signed_part);
					g_object_unref (ctx);
					g_object_unref (work_part);
					goto exit;
				}
				g_object_unref (work_part);
				work_part = signed_part;
				g_object_unref (ctx);
			}

			/* 2. Chiffrement OpenPGP */
			if (pgp_encrypt) {
				CamelCipherContext *ctx = camel_gpg_context_new (core->session);
				CamelMimePart *encrypted_part = camel_mime_part_new ();
				GPtrArray *rcpt_keys = g_ptr_array_new_with_free_func (g_free);
				gint n_rcpt = camel_address_length (CAMEL_ADDRESS (recipients));
				gint r_idx;

				camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);
				for (r_idx = 0; r_idx < n_rcpt; r_idx++) {
					const gchar *r_addr = NULL;
					if (camel_internet_address_get (recipients, r_idx, NULL, &r_addr) && r_addr && *r_addr)
						g_ptr_array_add (rcpt_keys, g_strdup (r_addr));
				}
				if (userid_pgp && *userid_pgp)
					g_ptr_array_add (rcpt_keys, g_strdup (userid_pgp));

				if (!camel_cipher_context_encrypt_sync (
					ctx, userid_pgp, rcpt_keys,
					work_part, encrypted_part, core->cancellable, error)) {
					g_ptr_array_unref (rcpt_keys);
					g_object_unref (encrypted_part);
					g_object_unref (ctx);
					g_object_unref (work_part);
					goto exit;
				}
				g_ptr_array_unref (rcpt_keys);
				g_object_unref (work_part);
				work_part = encrypted_part;
				g_object_unref (ctx);
			}

			/* 3. Signature S/MIME */
			if (smime_sign) {
				CamelCipherContext *ctx = camel_smime_context_new (core->session);
				CamelMimePart *signed_part = camel_mime_part_new ();

				camel_smime_context_set_sign_mode (CAMEL_SMIME_CONTEXT (ctx), CAMEL_SMIME_SIGN_CLEARSIGN);
				if (!camel_cipher_context_sign_sync (
					ctx, userid_smime, CAMEL_CIPHER_HASH_SHA256,
					work_part, signed_part, core->cancellable, error)) {
					g_object_unref (signed_part);
					g_object_unref (ctx);
					g_object_unref (work_part);
					goto exit;
				}
				g_object_unref (work_part);
				work_part = signed_part;
				g_object_unref (ctx);
			}

			/* 4. Chiffrement S/MIME */
			if (smime_encrypt) {
				CamelCipherContext *ctx = camel_smime_context_new (core->session);
				CamelMimePart *encrypted_part = camel_mime_part_new ();
				GPtrArray *rcpt_keys = g_ptr_array_new_with_free_func (g_free);
				gint n_rcpt = camel_address_length (CAMEL_ADDRESS (recipients));
				gint r_idx;

				for (r_idx = 0; r_idx < n_rcpt; r_idx++) {
					const gchar *r_addr = NULL;
					if (camel_internet_address_get (recipients, r_idx, NULL, &r_addr) && r_addr && *r_addr)
						g_ptr_array_add (rcpt_keys, g_strdup (r_addr));
				}
				if (userid_smime && *userid_smime)
					g_ptr_array_add (rcpt_keys, g_strdup (userid_smime));

				if (!camel_cipher_context_encrypt_sync (
					ctx, userid_smime, rcpt_keys,
					work_part, encrypted_part, core->cancellable, error)) {
					g_ptr_array_unref (rcpt_keys);
					g_object_unref (encrypted_part);
					g_object_unref (ctx);
					g_object_unref (work_part);
					goto exit;
				}
				g_ptr_array_unref (rcpt_keys);
				g_object_unref (work_part);
				work_part = encrypted_part;
				g_object_unref (ctx);
			}

			/* Transfère le contenu signé/chiffré sur le CamelMimeMessage */
			{
				CamelDataWrapper *final_dw = camel_medium_get_content (CAMEL_MEDIUM (work_part));
				CamelContentType *final_ct = camel_mime_part_get_content_type (work_part);

				if (final_dw)
					camel_medium_set_content (CAMEL_MEDIUM (message), final_dw);
				if (final_ct) {
					gchar *ct_str = camel_content_type_format (final_ct);
					camel_mime_part_set_content_type (CAMEL_MIME_PART (message), ct_str);
					g_free (ct_str);
				}
				camel_mime_part_set_encoding (CAMEL_MIME_PART (message),
					camel_mime_part_get_encoding (work_part));
			}

			g_object_unref (work_part);
		}
	}

	/* Si l'application est en mode hors connexion, enregistre le message
	 * dans la Boîte d'envoi locale ("local" / "Outbox") pour envoi automatique dès le retour en ligne */
	if (evo_core_get_offline (core) || (core->session && !camel_session_get_online (core->session))) {
		CamelFolder *outbox = core_ref_folder (core, "local", "Outbox", error);
		if (outbox) {
			CamelMessageInfo *info = camel_message_info_new (NULL);

			camel_medium_set_header (CAMEL_MEDIUM (message), "X-EvolutionWin-Account", account_uid);
			if (camel_address_length (CAMEL_ADDRESS (bcc)) > 0) {
				gchar *bcc_str = camel_address_format (CAMEL_ADDRESS (bcc));
				camel_medium_set_header (CAMEL_MEDIUM (message), "X-EvolutionWin-Bcc", bcc_str);
				g_free (bcc_str);
			}

			camel_message_info_set_flags (info, 0, CAMEL_MESSAGE_SEEN);
			if (camel_folder_append_message_sync (outbox, message, info, NULL, core->cancellable, error)) {
				camel_folder_synchronize_sync (outbox, FALSE, core->cancellable, NULL);
				core_emit_folders_updated (core, "local");
				json = g_strdup ("{\"status\":\"queued\",\"message\":\"Message placé dans la Boîte d'envoi\"}");
			}
			g_object_unref (info);
			g_object_unref (outbox);
		}
		goto exit;
	}

	{
		JsonBuilder *builder = core_event_builder ();

		builder_add_string (builder, "subject", subject);
		core_emit_builder (core, "sending", builder);
	}

	if (!camel_service_connect_sync (transport, core->cancellable, error) ||
	    !camel_transport_send_to_sync (CAMEL_TRANSPORT (transport), message, CAMEL_ADDRESS (from),
		CAMEL_ADDRESS (recipients), &sent_message_saved, core->cancellable, error)) {
		camel_service_disconnect_sync (transport, FALSE, NULL, NULL);

		/* Si l'envoi direct a échoué en raison d'une panne réseau imprévue, basculer
		 * automatiquement le message dans la Boîte d'envoi locale pour ne pas le perdre */
		if (error && *error && (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED) ||
		    g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND) ||
		    g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NETWORK_UNREACHABLE) ||
		    g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT))) {
			CamelFolder *outbox = core_ref_folder (core, "local", "Outbox", NULL);
			if (outbox) {
				CamelMessageInfo *info = camel_message_info_new (NULL);

				camel_medium_set_header (CAMEL_MEDIUM (message), "X-EvolutionWin-Account", account_uid);
				if (camel_address_length (CAMEL_ADDRESS (bcc)) > 0) {
					gchar *bcc_str = camel_address_format (CAMEL_ADDRESS (bcc));
					camel_medium_set_header (CAMEL_MEDIUM (message), "X-EvolutionWin-Bcc", bcc_str);
					g_free (bcc_str);
				}

				camel_message_info_set_flags (info, 0, CAMEL_MESSAGE_SEEN);
				if (camel_folder_append_message_sync (outbox, message, info, NULL, core->cancellable, NULL)) {
					camel_folder_synchronize_sync (outbox, FALSE, core->cancellable, NULL);
					core_emit_folders_updated (core, "local");
					g_clear_error (error);
					json = g_strdup ("{\"status\":\"queued\",\"message\":\"Serveur injoignable : message placé dans la Boîte d'envoi\"}");
				}
				g_object_unref (info);
				g_object_unref (outbox);
			}
		}
		goto exit;
	}

	camel_service_disconnect_sync (transport, TRUE, NULL, NULL);

	{
		JsonBuilder *builder = core_event_builder ();

		builder_add_string (builder, "subject", subject);
		core_emit_builder (core, "sent", builder);
	}

	/* copy in the Sent folder, unless the server did it (e.g. Gmail) */
	saved = sent_message_saved;

	if (!saved && e_source_mail_submission_get_use_sent_folder (submission)) {
		gchar *sent_uri = e_source_mail_submission_dup_sent_folder (submission);
		gchar *store_uid = NULL, *folder_name = NULL;
		GError *local_error = NULL;

		if (parse_folder_uri (sent_uri, &store_uid, &folder_name)) {
			CamelFolder *sent = core_ref_folder (core, store_uid, folder_name, &local_error);

			if (sent) {
				CamelMessageInfo *info = camel_message_info_new (NULL);

				camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
				saved = camel_folder_append_message_sync (sent, message, info, NULL, core->cancellable, &local_error);

				g_object_unref (info);
				g_object_unref (sent);
			}
		}

		if (local_error) {
			/* the message is sent: only warn */
			g_warning ("Cannot save the sent message in “%s”: %s", sent_uri, local_error->message);
			g_clear_error (&local_error);
		}

		g_free (store_uid);
		g_free (folder_name);
		g_free (sent_uri);
	}

	json = g_strdup (saved ? "{\"saved\":true}" : "{\"saved\":false}");

 exit:
	g_clear_object (&message);
	g_clear_object (&from);
	g_clear_object (&to);
	g_clear_object (&cc);
	g_clear_object (&bcc);
	g_clear_object (&recipients);
	g_clear_object (&transport);
	g_clear_object (&identity);
	json_node_unref (root);

	return json;
}

/* Parcourt la Boîte d'envoi locale ("local" / "Outbox") et transmet chaque
 * message en attente via le serveur SMTP de son compte émetteur.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : arguments (non utilisé)
 *   error : récepteur d'erreur éventuel
 * Retourne : chaîne JSON indiquant le résultat de l'opération. */
static gchar *
core_save_draft_run (EvoCore *core,
		     gchar **args,
		     GError **error)
{
	JsonNode *root;
	JsonObject *object;
	CamelFolder *drafts = NULL;
	CamelMimeMessage *message = NULL;
	const gchar *account_uid, *subject, *text, *html, *old_draft_uid;
	gchar *appended_uid = NULL;
	gchar *store_uid = NULL, *drafts_folder_name = NULL;
	gchar *json = NULL;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account_uid = object ? json_object_get_string_member_with_default (object, "account", NULL) : NULL;
	old_draft_uid = object ? json_object_get_string_member_with_default (object, "draft_uid", NULL) : NULL;

	/* 1. Recherche du dossier Brouillons associé au compte */
	if (account_uid && *account_uid) {
		ESource *identity = core_ref_identity (core, account_uid);
		if (identity) {
			if (e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION)) {
				ESourceMailComposition *mc = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_COMPOSITION);
				gchar *uri = e_source_mail_composition_dup_drafts_folder (mc);
				if (uri) {
					parse_folder_uri (uri, &store_uid, &drafts_folder_name);
					g_free (uri);
				}
			}
			g_object_unref (identity);
		}

		if (store_uid && drafts_folder_name) {
			drafts = core_ref_folder (core, store_uid, drafts_folder_name, NULL);
		}

		if (!drafts) {
			static const gchar *try_names[] = { "Drafts", "Brouillons", "INBOX/Drafts", "INBOX.Drafts", NULL };
			guint i;
			for (i = 0; try_names[i] && !drafts; i++) {
				drafts = core_ref_folder (core, account_uid, try_names[i], NULL);
				if (drafts) {
					g_free (store_uid);
					g_free (drafts_folder_name);
					store_uid = g_strdup (account_uid);
					drafts_folder_name = g_strdup (try_names[i]);
					break;
				}
			}
		}
	}

	if (!drafts) {
		drafts = core_ref_folder (core, "local", "Drafts", error);
		if (drafts) {
			g_free (store_uid);
			g_free (drafts_folder_name);
			store_uid = g_strdup ("local");
			drafts_folder_name = g_strdup ("Drafts");
		}
	}

	if (!drafts) {
		json_node_unref (root);
		return NULL;
	}

	/* 2. Construction du CamelMimeMessage */
	subject = object ? json_object_get_string_member_with_default (object, "subject", "") : "";
	text = object ? json_object_get_string_member_with_default (object, "body", "") : "";
	html = object ? json_object_get_string_member_with_default (object, "html_body", NULL) : NULL;

	message = camel_mime_message_new ();
	camel_mime_message_set_subject (message, subject);
	camel_mime_message_set_date (message, CAMEL_MESSAGE_DATE_CURRENT, 0);
	camel_medium_set_header (CAMEL_MEDIUM (message), "X-Mailer", "Evolution for Windows");

	{
		const gchar *to_str = object ? json_object_get_string_member_with_default (object, "to", "") : "";
		const gchar *cc_str = object ? json_object_get_string_member_with_default (object, "cc", "") : "";
		const gchar *bcc_str = object ? json_object_get_string_member_with_default (object, "bcc", "") : "";

		if (to_str && *to_str) {
			CamelInternetAddress *to_addr = camel_internet_address_new ();
			camel_address_unformat (CAMEL_ADDRESS (to_addr), to_str);
			camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to_addr);
			g_object_unref (to_addr);
		}
		if (cc_str && *cc_str) {
			CamelInternetAddress *cc_addr = camel_internet_address_new ();
			camel_address_unformat (CAMEL_ADDRESS (cc_addr), cc_str);
			camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_CC, cc_addr);
			g_object_unref (cc_addr);
		}
		if (bcc_str && *bcc_str) {
			CamelInternetAddress *bcc_addr = camel_internet_address_new ();
			camel_address_unformat (CAMEL_ADDRESS (bcc_addr), bcc_str);
			camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_BCC, bcc_addr);
			g_object_unref (bcc_addr);
		}
	}

	if (json_object_has_member (object, "attachments")) {
		JsonArray *attachments = json_object_get_array_member (object, "attachments");
		guint n_att = attachments ? json_array_get_length (attachments) : 0;

		if (n_att > 0) {
			CamelMultipart *multipart = camel_multipart_new ();
			CamelMimePart *body_part = camel_mime_part_new ();
			guint ii;

			camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart), "multipart/mixed");
			camel_multipart_set_boundary (multipart, NULL);

			set_message_or_part_body (CAMEL_MEDIUM (body_part), text, html);
			camel_multipart_add_part (multipart, body_part);
			g_object_unref (body_part);

			for (ii = 0; ii < n_att; ii++) {
				const gchar *path = json_array_get_string_element (attachments, ii);
				gchar *contents = NULL, *basename = NULL, *guessed = NULL, *mime_type = NULL;
				gsize length = 0;
				CamelMimePart *att_part;

				if (!g_file_get_contents (path, &contents, &length, NULL))
					continue;

				basename = g_path_get_basename (path);
				guessed = g_content_type_guess (path, (const guchar *) contents, length, NULL);
				if (guessed)
					mime_type = g_content_type_get_mime_type (guessed);
				if (!mime_type)
					mime_type = g_strdup ("application/octet-stream");

				att_part = camel_mime_part_new ();
				camel_mime_part_set_content (att_part, contents, (gint) length, mime_type);
				camel_mime_part_set_disposition (att_part, "attachment");
				camel_mime_part_set_filename (att_part, basename);
				camel_mime_part_set_encoding (att_part, CAMEL_TRANSFER_ENCODING_BASE64);
				camel_multipart_add_part (multipart, att_part);

				g_object_unref (att_part);
				g_free (mime_type);
				g_free (guessed);
				g_free (basename);
				g_free (contents);
			}

			camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (multipart));
			g_object_unref (multipart);
		} else {
			set_message_or_part_body (CAMEL_MEDIUM (message), text, html);
		}
	} else {
		set_message_or_part_body (CAMEL_MEDIUM (message), text, html);
	}

	/* 3. Remplacement de l'ancien brouillon auto-enregistré si spécifié */
	if (old_draft_uid && *old_draft_uid) {
		camel_folder_set_message_flags (drafts, old_draft_uid,
						CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
						CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
	}

	/* 4. Enregistrement du nouveau brouillon */
	{
		CamelMessageInfo *info = camel_message_info_new (NULL);
		camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DRAFT, CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DRAFT);
		if (camel_folder_append_message_sync (drafts, message, info, &appended_uid, core->cancellable, error)) {
			camel_folder_synchronize_sync (drafts, FALSE, core->cancellable, NULL);
			if (store_uid)
				core_emit_folders_updated (core, store_uid);
			json = g_strdup_printf ("{\"saved\":true,\"uid\":\"%s\",\"account\":\"%s\",\"folder\":\"%s\"}",
						appended_uid ? appended_uid : "",
						store_uid ? store_uid : "",
						drafts_folder_name ? drafts_folder_name : "");
		}
		g_object_unref (info);
	}

	g_free (appended_uid);
	g_free (store_uid);
	g_free (drafts_folder_name);
	g_object_unref (drafts);
	g_object_unref (message);
	json_node_unref (root);

	return json;
}

/* Actions sur tout un dossier.
 * "mark-all-read" : tous les messages marqués lus.
 * "empty" : dossier Corbeille ou Indésirables (ou compte sans corbeille) :
 * messages supprimés définitivement ; autre dossier : messages déplacés
 * vers la corbeille du compte.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] action
 *   error : emplacement d'erreur GError
 * Retourne : {"count": n, "permanent": true|false}, ou NULL en cas d'erreur. */
static gchar *
core_process_outbox_run (EvoCore *core,
			 gchar **args,
			 GError **error)
{
	CamelFolder *outbox;
	GPtrArray *uids;
	guint ii;
	gboolean changed = FALSE;

	if (evo_core_get_offline (core) || (core->session && !camel_session_get_online (core->session)))
		return g_strdup ("{\"status\":\"offline\"}");

	outbox = core_ref_folder (core, "local", "Outbox", NULL);
	if (!outbox)
		return g_strdup ("{\"status\":\"no_outbox\"}");

	uids = camel_folder_dup_uids (outbox);
	if (!uids || uids->len == 0) {
		if (uids)
			g_ptr_array_unref (uids);
		g_object_unref (outbox);
		return g_strdup ("{\"status\":\"empty\"}");
	}

	core_emit_progress (core, "local", "Outbox", "Envoi des messages de la Boîte d'envoi...", 0, TRUE);

	for (ii = 0; ii < uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (uids, ii);
		CamelMimeMessage *msg;
		const gchar *account_uid;
		const gchar *bcc_header;
		ESource *identity;
		ESourceMailIdentity *identity_ext;
		ESourceMailSubmission *submission;
		CamelService *transport;
		const CamelInternetAddress *from;
		CamelInternetAddress *recipients;
		const CamelInternetAddress *msg_to, *msg_cc;
		gboolean sent_saved = FALSE;
		GError *send_err = NULL;

		msg = camel_folder_get_message_sync (outbox, uid, core->cancellable, NULL);
		if (!msg)
			continue;

		account_uid = camel_medium_get_header (CAMEL_MEDIUM (msg), "X-EvolutionWin-Account");
		if (!account_uid || !*account_uid) {
			ESource *def = e_source_registry_ref_default_mail_account (core->registry);
			if (def) {
				account_uid = e_source_get_uid (def);
				g_object_unref (def);
			}
		}

		identity = account_uid ? core_ref_identity (core, account_uid) : NULL;
		if (!identity || !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY) ||
		    !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
			g_clear_object (&identity);
			g_object_unref (msg);
			continue;
		}

		identity_ext = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
		submission = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
		transport = camel_session_ref_service (core->session, e_source_mail_submission_get_transport_uid (submission));

		if (!CAMEL_IS_TRANSPORT (transport)) {
			g_clear_object (&transport);
			g_object_unref (identity);
			g_object_unref (msg);
			continue;
		}

		from = camel_mime_message_get_from (msg);
		recipients = camel_internet_address_new ();
		msg_to = camel_mime_message_get_recipients (msg, CAMEL_RECIPIENT_TYPE_TO);
		msg_cc = camel_mime_message_get_recipients (msg, CAMEL_RECIPIENT_TYPE_CC);
		if (msg_to)
			camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (msg_to));
		if (msg_cc)
			camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (msg_cc));

		bcc_header = camel_medium_get_header (CAMEL_MEDIUM (msg), "X-EvolutionWin-Bcc");
		if (bcc_header && *bcc_header) {
			CamelInternetAddress *bcc = camel_internet_address_new ();
			camel_address_unformat (CAMEL_ADDRESS (bcc), bcc_header);
			camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (bcc));
			g_object_unref (bcc);
		}

		/* Supprime les en-têtes techniques avant transmission réseau */
		camel_medium_remove_header (CAMEL_MEDIUM (msg), "X-EvolutionWin-Account");
		camel_medium_remove_header (CAMEL_MEDIUM (msg), "X-EvolutionWin-Bcc");

		{
			JsonBuilder *builder = core_event_builder ();
			builder_add_string (builder, "subject", camel_mime_message_get_subject (msg)
				? camel_mime_message_get_subject (msg) : "");
			core_emit_builder (core, "sending", builder);
		}

		if (camel_service_connect_sync (transport, core->cancellable, &send_err) &&
		    camel_transport_send_to_sync (CAMEL_TRANSPORT (transport), msg, CAMEL_ADDRESS (from),
			CAMEL_ADDRESS (recipients), &sent_saved, core->cancellable, &send_err)) {

			camel_service_disconnect_sync (transport, TRUE, NULL, NULL);

			{
				JsonBuilder *builder = core_event_builder ();
				builder_add_string (builder, "subject", camel_mime_message_get_subject (msg)
					? camel_mime_message_get_subject (msg) : "");
				core_emit_builder (core, "sent", builder);
			}

			if (!sent_saved && e_source_mail_submission_get_use_sent_folder (submission)) {
				gchar *sent_uri = e_source_mail_submission_dup_sent_folder (submission);
				gchar *store_uid = NULL, *folder_name = NULL;
				if (parse_folder_uri (sent_uri, &store_uid, &folder_name)) {
					CamelFolder *sent = core_ref_folder (core, store_uid, folder_name, NULL);
					if (sent) {
						CamelMessageInfo *info = camel_message_info_new (NULL);
						camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
						camel_folder_append_message_sync (sent, msg, info, NULL, core->cancellable, NULL);
						g_object_unref (info);
						g_object_unref (sent);
					}
				}
				g_free (store_uid);
				g_free (folder_name);
				g_free (sent_uri);
			}

			camel_folder_set_message_flags (outbox, uid,
				CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
				CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);
			changed = TRUE;
		} else {
			camel_service_disconnect_sync (transport, FALSE, NULL, NULL);
			if (send_err) {
				g_warning ("Échec de l'envoi du message en attente %s : %s", uid, send_err->message);
				g_clear_error (&send_err);
			}
		}

		g_object_unref (recipients);
		g_object_unref (transport);
		g_object_unref (identity);
		g_object_unref (msg);
	}

	if (changed) {
		camel_folder_expunge_sync (outbox, core->cancellable, NULL);
		camel_folder_synchronize_sync (outbox, FALSE, core->cancellable, NULL);
		core_emit_folders_updated (core, "local");
	}

	core_emit_progress (core, "local", "Outbox", "Boîte d'envoi traitée", 100, FALSE);

	g_ptr_array_unref (uids);
	g_object_unref (outbox);

	return g_strdup ("{\"status\":\"done\"}");
}

/* Lance le traitement asynchrone de la Boîte d'envoi. */
void
core_process_outbox (EvoCore *core)
{
	gchar *args[] = { NULL };
	core_submit (core, core_process_outbox_run, g_strdupv (args), core_ignore_result, NULL);
}

/* Envoie tous les messages placés dans la Boîte d'envoi locale. */
void
evo_core_send_unsent_messages (EvoCore *core)
{
	g_return_if_fail (core != NULL);
	core_process_outbox (core);
}

void
evo_core_send_message (EvoCore *core,
		       const char *message_json,
		       EvoCoreResultFunc func,
		       void *user_data)
{
	gchar *args[] = { (gchar *) message_json, NULL };

	g_return_if_fail (message_json != NULL);

	core_submit (core, core_send_message_run, g_strdupv (args), func, user_data);
}

void
evo_core_save_draft (EvoCore *core,
		     const char *draft_json,
		     EvoCoreResultFunc func,
		     void *user_data)
{
	gchar *args[] = { (gchar *) draft_json, NULL };

	g_return_if_fail (draft_json != NULL);

	core_submit (core, core_save_draft_run, g_strdupv (args), func, user_data);
}

/* Construit et envoie l'accusé de lecture (MDN, RFC 8098) d'un message, ou le
 * marque simplement comme traité.
 * Paramètres :
 *   args  : [0] account_uid, [1] folder_full_name, [2] message_uid,
 *           [3] "1" pour envoyer l'accusé, "0" pour seulement marquer le
 *           message comme traité (« Ne pas envoyer »)
 * Le destinataire est lu dans Disposition-Notification-To (à défaut
 * Return-Receipt-To) ; l'accusé part par le serveur SMTP du compte du message.
 * Une fois traité, l'étiquette Camel "$MDNSent" est posée (comme Evolution) pour
 * ne plus reproposer l'accusé.
 * Retourne : {"sent": bool, "processed": true, "to": "adresse"} ou NULL. */
static gchar *
core_send_read_receipt_run (EvoCore *core,
			    gchar **args,
			    GError **error)
{
	CamelFolder *folder;
	CamelMimeMessage *message = NULL;
	CamelMessageInfo *mi;
	CamelInternetAddress *to = NULL, *from = NULL;
	CamelMimeMessage *mdn = NULL;
	CamelService *transport = NULL;
	ESource *identity = NULL;
	ESourceMailIdentity *identity_extension;
	ESourceMailSubmission *submission;
	JsonBuilder *builder;
	GString *text;
	const gchar *header, *to_addr = NULL, *original_id, *subject;
	gchar *json = NULL;
	gchar fields[2048];
	gboolean send = g_strcmp0 (args[3], "1") == 0, sent = FALSE;
	const gchar *stage = "folder";
	gboolean saved = FALSE;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;

	stage = "message";
	message = camel_folder_get_message_sync (folder, args[2], core->cancellable, error);
	if (!message) {
		g_object_unref (folder);
		return NULL;
	}

	if (send) {
		header = camel_medium_get_header (CAMEL_MEDIUM (message), "Disposition-Notification-To");
		if (!header || !*header)
			header = camel_medium_get_header (CAMEL_MEDIUM (message), "Return-Receipt-To");
		if (!header || !*header) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "This message does not request a read receipt");
			goto exit;
		}

		to = camel_internet_address_new ();
		if (camel_address_unformat (CAMEL_ADDRESS (to), header) < 1 ||
		    !camel_internet_address_get (to, 0, NULL, &to_addr) || !to_addr || !*to_addr) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid read receipt address");
			goto exit;
		}

		if (evo_core_get_offline (core) || (core->session && !camel_session_get_online (core->session))) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "Offline: the read receipt cannot be sent now");
			goto exit;
		}

		stage = "identity";
		identity = core_ref_identity (core, args[0]);
		if (!identity || !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY) ||
		    !e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "This account has no sender identity");
			goto exit;
		}

		identity_extension = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
		submission = e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
		transport = camel_session_ref_service (core->session, e_source_mail_submission_get_transport_uid (submission));
		if (!CAMEL_IS_TRANSPORT (transport)) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "This account has no outgoing (SMTP) server");
			goto exit;
		}

		from = camel_internet_address_new ();
		camel_internet_address_add (from,
			e_source_mail_identity_get_name (identity_extension),
			e_source_mail_identity_get_address (identity_extension));

		original_id = camel_medium_get_header (CAMEL_MEDIUM (message), "Message-ID");
		subject = camel_mime_message_get_subject (message);

		if (evo_mdn_format_fields (fields, sizeof fields, "EvolutionWin",
			e_source_mail_identity_get_address (identity_extension),
			original_id ? original_id : "", "displayed") < 0) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Read receipt fields too long");
			goto exit;
		}

		/* partie lisible, puis partie machine (multipart/report, RFC 8098 §3) */
		text = g_string_new (NULL);
		g_string_append_printf (text,
			"Ceci est un accusé de lecture pour le message envoyé à %s avec l'objet « %s ».\r\n\r\n"
			"Il indique seulement que le message a été affiché sur l'ordinateur du destinataire ; "
			"il ne garantit pas qu'il a été lu ni compris.\r\n\r\n"
			"This is a return receipt for the message with the subject \"%s\". "
			"It only acknowledges that the message was displayed on the recipient's computer; "
			"it does not guarantee that it was read or understood.\r\n",
			e_source_mail_identity_get_address (identity_extension),
			subject ? subject : "", subject ? subject : "");

		stage = "build";
		mdn = camel_mime_message_new ();
		{
			CamelMultipart *report = camel_multipart_new ();
			CamelMimePart *human = camel_mime_part_new ();
			CamelMimePart *machine = camel_mime_part_new ();
			gchar *mdn_subject = g_strconcat ("Accusé de lecture : ", subject ? subject : "", NULL);

			camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (report),
				"multipart/report; report-type=disposition-notification");
			camel_multipart_set_boundary (report, NULL);

			camel_mime_part_set_content (human, text->str, text->len, "text/plain; charset=utf-8");
			camel_mime_part_set_encoding (human, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
			camel_multipart_add_part (report, human);

			camel_mime_part_set_content (machine, fields, strlen (fields), "message/disposition-notification");
			camel_mime_part_set_encoding (machine, CAMEL_TRANSFER_ENCODING_7BIT);
			camel_multipart_add_part (report, machine);

			camel_mime_message_set_subject (mdn, mdn_subject);
			camel_medium_set_content (CAMEL_MEDIUM (mdn), CAMEL_DATA_WRAPPER (report));

			g_free (mdn_subject);
			g_object_unref (human);
			g_object_unref (machine);
			g_object_unref (report);
		}
		g_string_free (text, TRUE);

		camel_mime_message_set_from (mdn, from);
		camel_mime_message_set_recipients (mdn, CAMEL_RECIPIENT_TYPE_TO, to);
		camel_mime_message_set_date (mdn, CAMEL_MESSAGE_DATE_CURRENT, 0);
		camel_mime_message_set_message_id (mdn, NULL);
		if (original_id && *original_id) {
			gchar clean[512];

			evo_mdn_sanitize_value (clean, sizeof clean, original_id);
			camel_medium_set_header (CAMEL_MEDIUM (mdn), "In-Reply-To", clean);
			camel_medium_set_header (CAMEL_MEDIUM (mdn), "References", clean);
		}
		camel_medium_set_header (CAMEL_MEDIUM (mdn), "Auto-Submitted", "auto-replied");
		camel_medium_set_header (CAMEL_MEDIUM (mdn), "X-Mailer", "Evolution for Windows");

		stage = "smtp";
		if (!camel_service_connect_sync (transport, core->cancellable, error) ||
		    !camel_transport_send_to_sync (CAMEL_TRANSPORT (transport), mdn, CAMEL_ADDRESS (from),
			CAMEL_ADDRESS (to), &saved, core->cancellable, error)) {
			camel_service_disconnect_sync (transport, FALSE, NULL, NULL);
			goto exit;
		}
		camel_service_disconnect_sync (transport, TRUE, NULL, NULL);
		sent = TRUE;
	}

	/* mémorise le traitement : étiquette Camel $MDNSent */
	mi = camel_folder_get_message_info (folder, args[2]);
	if (mi) {
		camel_message_info_set_user_flag (mi, "$MDNSent", TRUE);
		g_object_unref (mi);
	}
	camel_folder_synchronize_message_sync (folder, args[2], core->cancellable, NULL);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_boolean (builder, "sent", sent);
	builder_add_boolean (builder, "processed", TRUE);
	builder_add_string (builder, "to", to_addr ? to_addr : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

 exit:
	if (!json && error && !*error)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Read receipt failed at step \"%s\" (no error reported)", stage);
	g_clear_object (&mdn);
	g_clear_object (&from);
	g_clear_object (&to);
	g_clear_object (&transport);
	g_clear_object (&identity);
	g_clear_object (&message);
	g_object_unref (folder);

	return json;
}

/* Soumet le traitement de l'accusé de lecture d'un message.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte qui a reçu le message
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message
 *   send             : non nul pour envoyer l'accusé, 0 pour seulement marquer
 *                      le message comme traité
 *   func             : rappel recevant {"sent", "processed", "to"}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_send_read_receipt (EvoCore *core,
			    const char *account_uid,
			    const char *folder_full_name,
			    const char *message_uid,
			    int send,
			    EvoCoreResultFunc func,
			    void *user_data)
{
	gchar *args[] = {
		(gchar *) account_uid,
		(gchar *) folder_full_name,
		(gchar *) message_uid,
		(gchar *) (send ? "1" : "0"),
		NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);

	core_submit (core, core_send_read_receipt_run, g_strdupv (args), func, user_data);
}
