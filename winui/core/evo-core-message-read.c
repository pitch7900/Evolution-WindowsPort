/*
 * evo-core : Lecture d'un message (corps HTML / texte, chiffrement), enregistrement
 * des pieces jointes et export .eml.
 * Voir evo-core.h et evo-core-private.h.
 */

#include "evo-core-private.h"

/* --- message content --- */

/* transfer-decoded content of a part, converted to UTF-8 for text */
gchar *
part_get_text (CamelMimePart *part,
	       GCancellable *cancellable)
{
	CamelDataWrapper *content;
	CamelContentType *content_type;
	GOutputStream *output;
	const gchar *charset;
	gchar *data, *text;
	gsize length;

	content = camel_medium_get_content (CAMEL_MEDIUM (part));
	if (!content)
		return g_strdup ("");

	output = g_memory_output_stream_new_resizable ();
	camel_data_wrapper_decode_to_output_stream_sync (content, output, cancellable, NULL);
	g_output_stream_close (output, NULL, NULL);

	length = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output));
	data = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output));
	g_object_unref (output);

	content_type = camel_mime_part_get_content_type (part);
	charset = content_type ? camel_content_type_param (content_type, "charset") : NULL;

	if (charset && g_ascii_strcasecmp (charset, "utf-8") != 0 && g_ascii_strcasecmp (charset, "us-ascii") != 0) {
		text = g_convert_with_fallback (data, length, "UTF-8", charset, "?", NULL, NULL, NULL);
		if (!text)
			text = g_convert_with_fallback (data, length, "UTF-8", "ISO-8859-1", "?", NULL, NULL, NULL);
	} else {
		text = g_utf8_make_valid (data ? data : "", length);
	}

	g_free (data);

	return text ? text : g_strdup ("");
}

typedef struct {
	CamelMimePart *html;
	CamelMimePart *plain;
	GPtrArray *related;     /* CamelMimePart * avec Content-ID */
	GPtrArray *attachments; /* CamelMimePart * pièces jointes */
} BodyParts;

static gboolean
part_is_attachment (CamelMimePart *part)
{
	const gchar *disposition = camel_mime_part_get_disposition (part);

	return disposition && g_ascii_strcasecmp (disposition, "attachment") == 0;
}

static void
find_body_parts (CamelMimePart *part,
		 BodyParts *parts)
{
	CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
	CamelContentType *content_type = camel_mime_part_get_content_type (part);
	gboolean is_inline_cid_image = FALSE;

	if (CAMEL_IS_MULTIPART (content)) {
		guint ii, n = camel_multipart_get_number (CAMEL_MULTIPART (content));

		for (ii = 0; ii < n; ii++)
			find_body_parts (camel_multipart_get_part (CAMEL_MULTIPART (content), ii), parts);
		return;
	}

	if (CAMEL_IS_MIME_MESSAGE (content)) {
		if (parts->attachments)
			g_ptr_array_add (parts->attachments, part);
		return;
	}

	if (camel_mime_part_get_content_id (part) &&
	    content_type && camel_content_type_is (content_type, "image", "*")) {
		g_ptr_array_add (parts->related, part);
		is_inline_cid_image = TRUE;
	}

	if (part_is_attachment (part)) {
		if (parts->attachments)
			g_ptr_array_add (parts->attachments, part);
		return;
	}

	if (!content_type)
		return;

	if (!parts->html && camel_content_type_is (content_type, "text", "html") && !camel_mime_part_get_filename (part))
		parts->html = part;
	else if (!parts->plain && camel_content_type_is (content_type, "text", "plain") && !camel_mime_part_get_filename (part))
		parts->plain = part;
	else if (!is_inline_cid_image && parts->attachments)
		g_ptr_array_add (parts->attachments, part);
}

/* inline images: cid: references replaced by data: URIs */
static gchar *
inline_related_parts (gchar *html,
		      GPtrArray *related,
		      GCancellable *cancellable)
{
	guint ii;

	for (ii = 0; ii < related->len; ii++) {
		CamelMimePart *part = related->pdata[ii];
		CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
		GOutputStream *output;
		gchar *cid, *mime_type, *base64, *data_uri, **split;

		cid = g_strconcat ("cid:", camel_mime_part_get_content_id (part), NULL);
		if (!strstr (html, cid)) {
			g_free (cid);
			continue;
		}

		output = g_memory_output_stream_new_resizable ();
		camel_data_wrapper_decode_to_output_stream_sync (content, output, cancellable, NULL);
		g_output_stream_close (output, NULL, NULL);

		mime_type = camel_content_type_simple (camel_mime_part_get_content_type (part));
		base64 = g_base64_encode (
			g_memory_output_stream_get_data (G_MEMORY_OUTPUT_STREAM (output)),
			g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output)));
		data_uri = g_strdup_printf ("data:%s;base64,%s", mime_type, base64);

		split = g_strsplit (html, cid, -1);
		g_free (html);
		html = g_strjoinv (data_uri, split);

		g_strfreev (split);
		g_free (data_uri);
		g_free (base64);
		g_free (mime_type);
		g_object_unref (output);
		g_free (cid);
	}

	return html;
}

/* rough plain text of an HTML body, to quote it in a reply */
gchar *
html_to_text (const gchar *html)
{
	static const struct { const gchar *entity, *text; } entities[] = {
		{ "&nbsp;", " " }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" },
		{ "&#39;", "'" }, { "&amp;", "&" }
	};
	GRegex *regex;
	gchar *text, *tmp;
	guint ii;

	/* drop style/script/head contents, turn block ends into line breaks */
	regex = g_regex_new ("<(style|script|head)[^>]*>.*?</\\1>", G_REGEX_CASELESS | G_REGEX_DOTALL, 0, NULL);
	text = g_regex_replace_literal (regex, html, -1, 0, "", 0, NULL);
	g_regex_unref (regex);

	regex = g_regex_new ("<(br|/p|/div|/tr|/h[1-6]|/li)[^>]*>", G_REGEX_CASELESS, 0, NULL);
	tmp = g_regex_replace_literal (regex, text, -1, 0, "\n", 0, NULL);
	g_regex_unref (regex);
	g_free (text);

	regex = g_regex_new ("<[^>]*>", 0, 0, NULL);
	text = g_regex_replace_literal (regex, tmp, -1, 0, "", 0, NULL);
	g_regex_unref (regex);
	g_free (tmp);

	for (ii = 0; ii < G_N_ELEMENTS (entities); ii++) {
		gchar **split = g_strsplit (text, entities[ii].entity, -1);
		g_free (text);
		text = g_strjoinv (entities[ii].text, split);
		g_strfreev (split);
	}

	/* at most one blank line in a row */
	regex = g_regex_new ("\\n[ \\t\\r]*\\n([ \\t\\r]*\\n)+", 0, 0, NULL);
	tmp = g_regex_replace_literal (regex, text, -1, 0, "\n\n", 0, NULL);
	g_regex_unref (regex);
	g_free (text);

	return g_strstrip (tmp);
}

/* Décode le contenu binaire d'une partie MIME en mémoire.
 * Paramètres :
 *   part        : partie MIME à décoder
 *   cancellable : objet d'annulation
 *   out_length  : taille en octets des données décodées
 * Retourne : tampon d'octets nouvellement alloué (à libérer avec g_free). */
static gchar *
part_decode_bytes (CamelMimePart *part,
		   GCancellable *cancellable,
		   gsize *out_length)
{
	CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
	GOutputStream *output;
	gchar *data;

	if (!content) {
		*out_length = 0;
		return g_strdup ("");
	}

	output = g_memory_output_stream_new_resizable ();
	if (CAMEL_IS_MIME_MESSAGE (content))
		camel_data_wrapper_write_to_output_stream_sync (content, output, cancellable, NULL);
	else
		camel_data_wrapper_decode_to_output_stream_sync (content, output, cancellable, NULL);
	g_output_stream_close (output, NULL, NULL);

	*out_length = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output));
	data = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output));
	g_object_unref (output);

	return data ? data : g_strdup ("");
}

/* Texte déjà échappé en HTML dont les adresses web (http://, https://) deviennent des liens.
 * Paramètres :
 *   escaped : texte échappé (g_markup_escape_text)
 * Retourne : HTML (g_free). */
static gchar *
linkify_escaped_text (const gchar *escaped)
{
	GString *out = g_string_new (NULL);
	const gchar *cursor = escaped;

	while (*cursor) {
		const gchar *scheme = strstr (cursor, "http");
		const gchar *end;
		gsize prefix;

		if (!scheme) {
			g_string_append (out, cursor);
			break;
		}
		prefix = g_str_has_prefix (scheme, "https://") ? 8 : (g_str_has_prefix (scheme, "http://") ? 7 : 0);
		if (!prefix || (scheme > escaped && g_ascii_isalnum (scheme[-1]))) {
			g_string_append_len (out, cursor, scheme + 4 - cursor);
			cursor = scheme + 4;
			continue;
		}

		g_string_append_len (out, cursor, scheme - cursor);
		end = scheme + prefix;
		while (*end && !g_ascii_isspace (*end) && *end != '<' && *end != '>' && *end != '"' &&
		       !g_str_has_prefix (end, "&lt;") && !g_str_has_prefix (end, "&gt;") && !g_str_has_prefix (end, "&quot;"))
			end++;
		while (end > scheme + prefix && strchr (".,;:!?)]'", end[-1]))
			end--;
		if (end > scheme + prefix) {
			g_string_append (out, "<a href=\"");
			g_string_append_len (out, scheme, end - scheme);
			g_string_append (out, "\">");
			g_string_append_len (out, scheme, end - scheme);
			g_string_append (out, "</a>");
		} else {
			g_string_append_len (out, scheme, prefix);
			end = scheme + prefix;
		}
		cursor = end;
	}

	return g_string_free (out, FALSE);
}

/* Extrait le corps HTML d'un message, son équivalent texte brut et la liste
 * de ses pièces jointes.
 * Paramètres :
 *   message         : message MIME complet
 *   cancellable     : objet d'annulation
 *   out_text        : reçoit le texte brut (pour les citations)
 *   out_attachments : GPtrArray * optionnel recevant les CamelMimePart * des pièces jointes
 * Retourne : document HTML prêt à afficher. */
static gchar *
message_body_html (CamelMimeMessage *message,
		   GCancellable *cancellable,
		   gchar **out_text,
		   GPtrArray *out_attachments)
{
	BodyParts parts = { NULL, NULL, g_ptr_array_new (), out_attachments };
	gchar *html;

	find_body_parts (CAMEL_MIME_PART (message), &parts);

	if (parts.html) {
		gchar *source = part_get_text (parts.html, cancellable);
		gchar *inlined;

		*out_text = parts.plain ? part_get_text (parts.plain, cancellable) : html_to_text (source);
		inlined = inline_related_parts (source, parts.related, cancellable);

		/* Force un fond blanc et un texte sombre par défaut (avant et après le
		 * contenu du message, ainsi que sur les attributs de <body>) afin d'éviter
		 * tout affichage noir sur fond noir lorsque Windows 11 est en mode sombre
		 * ou que le message HTML redéfinit <body> / <style>. */
		html = g_strdup_printf (
			"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
			"<meta name=\"color-scheme\" content=\"light only\">"
			"<style id=\"evo-base-style\">"
			":root, html, body { color-scheme: light only !important; "
			"background-color: #ffffff !important; color: #1a1a1a !important; }"
			"body { font-family: 'Segoe UI', -apple-system, sans-serif; margin: 12px; }"
			"a { color: #0066cc; }"
			"</style></head>"
			"<body bgcolor=\"#ffffff\" text=\"#1a1a1a\" "
			"style=\"background-color: #ffffff !important; color: #1a1a1a !important; "
			"font-family: 'Segoe UI', -apple-system, sans-serif; margin: 12px;\">"
			"%s"
			"<style id=\"evo-override-style\">"
			":root, html, body { color-scheme: light only !important; "
			"background-color: #ffffff !important; color: #1a1a1a !important; }"
			"</style></body></html>",
			inlined);
		g_free (inlined);
	} else {
		gchar *text = parts.plain ? part_get_text (parts.plain, cancellable) : g_strdup ("");
		gchar *plain_escaped = g_markup_escape_text (text, -1);
		gchar *escaped = linkify_escaped_text (plain_escaped);

		g_free (plain_escaped);
		*out_text = g_strdup (text);

		html = g_strdup_printf (
			"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
			"<meta name=\"color-scheme\" content=\"light only\">"
			"<style>:root, html, body { color-scheme: light only !important; "
			"background-color: #ffffff !important; color: #1a1a1a !important; }</style></head>"
			"<body bgcolor=\"#ffffff\" text=\"#1a1a1a\" "
			"style=\"background-color: #ffffff !important; color: #1a1a1a !important; margin: 12px;\">"
			"<pre style=\"white-space: pre-wrap; word-wrap: break-word; "
			"background-color: #ffffff !important; color: #1a1a1a !important; "
			"font-family: 'Cascadia Mono', Consolas, monospace; font-size: 10pt;\">%s</pre></body></html>",
			escaped);

		g_free (escaped);
		g_free (text);
	}

	g_ptr_array_unref (parts.related);

	return html;
}

static gchar *
format_recipients (CamelMimeMessage *message,
		   const gchar *type)
{
	CamelInternetAddress *addresses = camel_mime_message_get_recipients (message, type);
	gchar *formatted = addresses ? camel_address_format (CAMEL_ADDRESS (addresses)) : NULL;

	return formatted ? formatted : g_strdup ("");
}

/* Adresse électronique de l'expéditeur d'un message (From), à libérer avec g_free, ou NULL. */
static gchar *
message_from_email (CamelMimeMessage *message)
{
	CamelInternetAddress *from = camel_mime_message_get_from (message);
	const gchar *address = NULL;

	if (from && camel_internet_address_get (from, 0, NULL, &address) && address && *address)
		return g_strdup (address);

	return NULL;
}

/* Vérifie la signature cryptographique ou déchiffre un message MIME (OpenPGP
 * via GnuPG/Gpg4win ou S/MIME via NSS) et écrit l'objet "security" dans le
 * constructeur JSON du message. Si le message était chiffré et que le
 * déchiffrement réussit, remplace son contenu en mémoire par la partie
 * déchiffrée afin que l'extraction du corps et des pièces jointes opère sur
 * le texte en clair.
 * Paramètres :
 *   core    : instance du moteur evo-core
 *   message : message MIME à inspecter / déchiffrer
 *   builder : constructeur JSON dans lequel écrire le membre "security" */
static void
verify_or_decrypt_message (EvoCore *core,
			   CamelMimeMessage *message,
			   JsonBuilder *builder)
{
	CamelDataWrapper *content;
	CamelContentType *ct;
	gboolean is_encrypted = FALSE;
	gboolean is_signed = FALSE;
	gboolean is_valid = FALSE;
	gboolean is_trusted = FALSE;
	const gchar *protocol = "";
	GString *signer_str = g_string_new ("");
	GString *desc_str = g_string_new ("");

	content = camel_medium_get_content (CAMEL_MEDIUM (message));
	ct = content ? camel_data_wrapper_get_mime_type_field (content) : NULL;
	if (!ct)
		ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));

	/* 1. Déchiffrement éventuel (OpenPGP multipart/encrypted ou S/MIME pkcs7-mime) */
	if (ct && camel_content_type_is (ct, "multipart", "encrypted")) {
		const gchar *proto = camel_content_type_param (ct, "protocol");

		if (proto && g_ascii_strcasecmp (proto, "application/pgp-encrypted") == 0) {
			CamelCipherContext *ctx = camel_gpg_context_new (core->session);
			CamelMimePart *decrypted_part = camel_mime_part_new ();
			GError *local_error = NULL;
			CamelCipherValidity *validity;

			is_encrypted = TRUE;
			protocol = "OpenPGP";
			camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);

			validity = camel_cipher_context_decrypt_sync (
				ctx, CAMEL_MIME_PART (message), decrypted_part,
				core->cancellable, &local_error);
			if (validity) {
				CamelDataWrapper *dec_content = camel_medium_get_content (CAMEL_MEDIUM (decrypted_part));

				if (dec_content)
					camel_medium_set_content (CAMEL_MEDIUM (message), dec_content);

				if (validity->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE) {
					GList *link;

					is_signed = TRUE;
					is_valid = (validity->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_GOOD);
					for (link = g_queue_peek_head_link (&validity->sign.signers); link; link = link->next) {
						CamelCipherCertInfo *info = link->data;
						if (!info)
							continue;
						if (signer_str->len > 0)
							g_string_append (signer_str, ", ");
						if (info->name && *info->name && info->email && *info->email)
							g_string_append_printf (signer_str, "%s <%s>", info->name, info->email);
						else if (info->email && *info->email)
							g_string_append (signer_str, info->email);
						else if (info->name && *info->name)
							g_string_append (signer_str, info->name);
					}
				} else {
					is_valid = (validity->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE);
				}

				if (validity->sign.description && *validity->sign.description)
					g_string_append (desc_str, validity->sign.description);
				else if (validity->encrypt.description && *validity->encrypt.description)
					g_string_append (desc_str, validity->encrypt.description);
				else
					g_string_append (desc_str, "Message déchiffré avec OpenPGP (GnuPG).");

				camel_cipher_validity_free (validity);
			} else {
				is_valid = FALSE;
				g_string_append_printf (desc_str,
					"Échec du déchiffrement OpenPGP : %s",
					local_error ? local_error->message : "clé privée indisponible ou Gpg4win absent");
				g_clear_error (&local_error);
			}

			g_object_unref (decrypted_part);
			g_object_unref (ctx);
		}
	} else if (ct && (camel_content_type_is (ct, "application", "pkcs7-mime") ||
			  camel_content_type_is (ct, "application", "x-pkcs7-mime"))) {
		/* S/MIME natif Windows : déchiffrement ou signature opaque */
		CoreCryptoInfo cinfo = { 0 };
		gchar *from_email = message_from_email (message);
		CamelMimePart *inner = core_crypto_open_pkcs7 (CAMEL_MIME_PART (message), from_email, &cinfo);

		protocol = "S/MIME";
		if (inner) {
			CamelDataWrapper *inner_content = camel_medium_get_content (CAMEL_MEDIUM (inner));
			CamelContentType *inner_type = camel_mime_part_get_content_type (inner);

			if (inner_content && inner_type) {
				gchar *formatted = camel_content_type_format (inner_type);

				camel_medium_set_content (CAMEL_MEDIUM (message), inner_content);
				camel_mime_part_set_content_type (CAMEL_MIME_PART (message), formatted);
				if (camel_mime_part_get_encoding (inner) != CAMEL_TRANSFER_ENCODING_DEFAULT)
					camel_mime_part_set_encoding (CAMEL_MIME_PART (message), camel_mime_part_get_encoding (inner));
				else
					camel_medium_remove_header (CAMEL_MEDIUM (message), "Content-Transfer-Encoding");
				g_free (formatted);
			}
			g_object_unref (inner);
		}
		is_encrypted = cinfo.encrypted;
		is_signed = cinfo.is_signed;
		is_valid = cinfo.valid;
		is_trusted = cinfo.trusted;
		if (cinfo.signer)
			g_string_assign (signer_str, cinfo.signer);
		if (cinfo.description)
			g_string_assign (desc_str, cinfo.description);
		core_crypto_info_clear (&cinfo);
		g_free (from_email);
	}

	/* 2. Vérification de signature multipart/signed (y compris après déchiffrement) */
	content = camel_medium_get_content (CAMEL_MEDIUM (message));
	ct = content ? camel_data_wrapper_get_mime_type_field (content) : NULL;
	if (!ct)
		ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));

	if (ct && camel_content_type_is (ct, "multipart", "signed")) {
		const gchar *proto = camel_content_type_param (ct, "protocol");
		CamelCipherContext *ctx = NULL;

		if (proto && g_ascii_strcasecmp (proto, "application/pgp-signature") == 0) {
			ctx = camel_gpg_context_new (core->session);
			protocol = "OpenPGP";
		} else if (proto && (g_ascii_strcasecmp (proto, "application/pkcs7-signature") == 0 ||
				     g_ascii_strcasecmp (proto, "application/x-pkcs7-signature") == 0)) {
			/* S/MIME natif Windows : signature détachée */
			CoreCryptoInfo cinfo = { 0 };
			gchar *from_email = message_from_email (message);

			protocol = "S/MIME";
			if (CAMEL_IS_MULTIPART (content))
				core_crypto_verify_multipart_signed (CAMEL_MULTIPART (content), from_email, &cinfo);
			is_signed = TRUE;
			is_valid = cinfo.valid;
			is_trusted = cinfo.trusted;
			g_string_truncate (signer_str, 0);
			if (cinfo.signer)
				g_string_assign (signer_str, cinfo.signer);
			if (cinfo.description) {
				if (desc_str->len > 0)
					g_string_append_c (desc_str, ' ');
				g_string_append (desc_str, cinfo.description);
			}
			core_crypto_info_clear (&cinfo);
			g_free (from_email);
		}

		if (ctx) {
			GError *local_error = NULL;
			CamelCipherValidity *validity;
			CamelMimePart *sig_part = CAMEL_MIME_PART (message);

			/* Si le contenu déchiffré a remplacé le corps du message mais que
			 * l'en-tête Content-Type de message est encore multipart/encrypted,
			 * construit une partie temporaire portant le multipart/signed. */
			if (is_encrypted && content) {
				sig_part = camel_mime_part_new ();
				camel_medium_set_content (CAMEL_MEDIUM (sig_part), content);
			}

			is_signed = TRUE;
			validity = camel_cipher_context_verify_sync (
				ctx, sig_part, core->cancellable, &local_error);
			if (validity) {
				GList *link;

				is_valid = (validity->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_GOOD);
				g_string_truncate (signer_str, 0);
				for (link = g_queue_peek_head_link (&validity->sign.signers); link; link = link->next) {
					CamelCipherCertInfo *info = link->data;
					if (!info)
						continue;
					if (signer_str->len > 0)
						g_string_append (signer_str, ", ");
					if (info->name && *info->name && info->email && *info->email)
						g_string_append_printf (signer_str, "%s <%s>", info->name, info->email);
					else if (info->email && *info->email)
						g_string_append (signer_str, info->email);
					else if (info->name && *info->name)
						g_string_append (signer_str, info->name);
				}

				if (validity->sign.description && *validity->sign.description) {
					if (desc_str->len > 0)
						g_string_append (desc_str, "\n");
					g_string_append (desc_str, validity->sign.description);
				}

				camel_cipher_validity_free (validity);
			} else {
				is_valid = FALSE;
				if (desc_str->len > 0)
					g_string_append (desc_str, "\n");
				g_string_append_printf (desc_str,
					"Vérification de signature %s impossible : %s",
					protocol,
					local_error ? local_error->message : "erreur inconnue");
				g_clear_error (&local_error);
			}

			if (sig_part != CAMEL_MIME_PART (message))
				g_object_unref (sig_part);
			g_object_unref (ctx);
		}
	}

	json_builder_set_member_name (builder, "security");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "encrypted");
	json_builder_add_boolean_value (builder, is_encrypted);
	json_builder_set_member_name (builder, "signed");
	json_builder_add_boolean_value (builder, is_signed);
	json_builder_set_member_name (builder, "valid");
	json_builder_add_boolean_value (builder, is_valid);
	json_builder_set_member_name (builder, "trusted");
	json_builder_add_boolean_value (builder, is_trusted);
	builder_add_string (builder, "protocol", protocol);
	builder_add_string (builder, "signer", signer_str->str);
	builder_add_string (builder, "description", desc_str->str);
	json_builder_end_object (builder);

	g_string_free (signer_str, TRUE);
	g_string_free (desc_str, TRUE);
}

/* Cherche, dans un message, la partie message/disposition-notification d'un
 * accusé de lecture reçu (multipart/report; report-type=disposition-notification)
 * et renvoie son texte brut (champs RFC 8098), ou NULL si le message n'en est pas un.
 * Paramètres :
 *   part        : message ou partie à explorer
 *   cancellable : annulation éventuelle
 * Retourne : texte alloué (à libérer avec g_free), ou NULL. */
static gchar *
find_mdn_report (CamelMimePart *part,
		 GCancellable *cancellable)
{
	CamelDataWrapper *content = camel_medium_get_content (CAMEL_MEDIUM (part));
	CamelContentType *content_type = camel_mime_part_get_content_type (part);
	guint ii, n;

	if (content_type && camel_content_type_is (content_type, "message", "disposition-notification"))
		return part_get_text (part, cancellable);

	if (!content || !CAMEL_IS_MULTIPART (content))
		return NULL;

	n = camel_multipart_get_number (CAMEL_MULTIPART (content));
	for (ii = 0; ii < n; ii++) {
		gchar *found = find_mdn_report (camel_multipart_get_part (CAMEL_MULTIPART (content), ii), cancellable);

		if (found)
			return found;
	}

	return NULL;
}

static gchar *
core_get_message_run (EvoCore *core,
		      gchar **args,
		      GError **error)
{
	CamelFolder *folder;
	CamelMimeMessage *message;
	CamelInternetAddress *from;
	GPtrArray *attachments;
	JsonBuilder *builder;
	gchar *json, *value;
	gint offset = 0;
	guint ii;

	gint64 started_at = g_get_monotonic_time (), opened_at;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder)
		return NULL;
	opened_at = g_get_monotonic_time ();

	message = camel_folder_get_message_sync (folder, args[2], core->cancellable, error);

	/* lecture lente : où passe le temps (connexion, téléchargement) */
	if ((g_get_monotonic_time () - started_at) >= 1000 * 1000)
		g_message ("lecture lente d'un message : ouverture et connexion %" G_GINT64_FORMAT " ms, téléchargement %" G_GINT64_FORMAT " ms",
			(opened_at - started_at) / 1000, (g_get_monotonic_time () - opened_at) / 1000);

	if (!message) {
		g_object_unref (folder);
		return NULL;
	}

	/* EVO_CORE_READ_ONLY=1 (tests): leave the mailbox untouched */
	if (g_strcmp0 (g_getenv ("EVO_CORE_READ_ONLY"), "1") != 0)
		camel_folder_set_message_flags (folder, args[2], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "subject");
	json_builder_add_string_value (builder, camel_mime_message_get_subject (message) ? camel_mime_message_get_subject (message) : "");

	from = camel_mime_message_get_from (message);
	value = from ? camel_address_format (CAMEL_ADDRESS (from)) : NULL;
	json_builder_set_member_name (builder, "from");
	json_builder_add_string_value (builder, value ? value : "");
	g_free (value);

	value = format_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	json_builder_set_member_name (builder, "to");
	json_builder_add_string_value (builder, value);
	g_free (value);

	value = format_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	json_builder_set_member_name (builder, "cc");
	json_builder_add_string_value (builder, value);
	g_free (value);

	json_builder_set_member_name (builder, "date");
	json_builder_add_int_value (builder, camel_mime_message_get_date (message, &offset));

	{
		CamelInternetAddress *reply_to = camel_mime_message_get_reply_to (message);
		gchar *formatted = reply_to ? camel_address_format (CAMEL_ADDRESS (reply_to)) : NULL;

		builder_add_string (builder, "reply_to", formatted);
		g_free (formatted);
	}

	builder_add_string (builder, "message_id", camel_medium_get_header (CAMEL_MEDIUM (message), "Message-ID"));
	builder_add_string (builder, "references", camel_medium_get_header (CAMEL_MEDIUM (message), "References"));

	/* Accusé de lecture : demande reçue (Disposition-Notification-To ou
	 * Return-Receipt-To) tant qu'elle n'est pas traitée ($MDNSent), et accusé
	 * reçu (MDN) à afficher. Rien n'est proposé pour un brouillon ni pour un
	 * message de soi-même (copie dans Envoyés). */
	{
		CamelMessageInfo *info = camel_folder_get_message_info (folder, args[2]);
		const gchar *receipt = camel_medium_get_header (CAMEL_MEDIUM (message), "Disposition-Notification-To");
		CamelContentType *ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));
		gboolean skip = FALSE;
		gchar *report = NULL;

		if (!receipt || !*receipt)
			receipt = camel_medium_get_header (CAMEL_MEDIUM (message), "Return-Receipt-To");

		if (info) {
			skip = (camel_message_info_get_flags (info) & CAMEL_MESSAGE_DRAFT) != 0;
			json_builder_set_member_name (builder, "mdn_sent");
			json_builder_add_boolean_value (builder, camel_message_info_get_user_flag (info, "$MDNSent"));
		}
		if (!skip) {
			ESource *identity = core_ref_identity (core, args[0]);

			if (identity && e_source_has_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY)) {
				const gchar *own = e_source_mail_identity_get_address (
					e_source_get_extension (identity, E_SOURCE_EXTENSION_MAIL_IDENTITY));

				if (own && *own && receipt && strstr (receipt, own))
					skip = TRUE;
			}
			g_clear_object (&identity);
		}
		builder_add_string (builder, "receipt_to", skip ? "" : receipt);
		g_clear_object (&info);

		if (ct && camel_content_type_is (ct, "multipart", "report")) {
			const gchar *type = camel_content_type_param (ct, "report-type");

			if (type && g_ascii_strcasecmp (type, "disposition-notification") == 0)
				report = find_mdn_report (CAMEL_MIME_PART (message), core->cancellable);
		}
		if (report) {
			json_builder_set_member_name (builder, "mdn");
			json_builder_begin_object (builder);
			builder_add_string (builder, "report", report);
			json_builder_end_object (builder);
			g_free (report);
		}
	}

	/* Invitation iCalendar (réunion) : résumé affiché dans le bandeau d'action */
	{
		gint64 invitation_started_at = g_get_monotonic_time ();

		core_invitation_describe (core, args[0], folder, args[2], message, builder);

		if ((g_get_monotonic_time () - invitation_started_at) >= 500 * 1000)
			g_message ("lecture d'un message : analyse de l'invitation %" G_GINT64_FORMAT " ms",
				(g_get_monotonic_time () - invitation_started_at) / 1000);
	}

	verify_or_decrypt_message (core, message, builder);

	attachments = g_ptr_array_new ();
	{
		gchar *text = NULL;

		value = message_body_html (message, core->cancellable, &text, attachments);
		builder_add_string (builder, "html", value);
		builder_add_string (builder, "text", text);
		g_free (value);
		g_free (text);
	}

	json_builder_set_member_name (builder, "attachments");
	json_builder_begin_array (builder);
	for (ii = 0; ii < attachments->len; ii++) {
		CamelMimePart *part = attachments->pdata[ii];
		CamelContentType *ct = camel_mime_part_get_content_type (part);
		const gchar *filename = camel_mime_part_get_filename (part);
		gchar *mime_type = ct ? camel_content_type_simple (ct) : g_strdup ("application/octet-stream");
		gsize size = 0;
		gchar *bytes = part_decode_bytes (part, core->cancellable, &size);

		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "index");
		json_builder_add_int_value (builder, ii);
		builder_add_string (builder, "filename", (filename && *filename) ? filename : "piece-jointe");
		builder_add_string (builder, "mime_type", mime_type);
		json_builder_set_member_name (builder, "size");
		json_builder_add_int_value (builder, (gint64) size);
		json_builder_end_object (builder);

		g_free (bytes);
		g_free (mime_type);
	}
	json_builder_end_array (builder);
	g_ptr_array_unref (attachments);

	json_builder_end_object (builder);
	json = json_from_builder (builder);

	g_object_unref (builder);
	g_object_unref (message);
	g_object_unref (folder);

	return json;
}

/* Enregistre une pièce jointe d'un message dans un fichier local.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] account_uid, [1] folder_full_name, [2] message_uid,
 *           [3] attachment_index, [4] dest_path
 *   error : emplacement d'erreur GError
 * Retourne : {"size": <octets>}, ou NULL en cas d'erreur. */
static gchar *
core_save_attachment_run (EvoCore *core,
			  gchar **args,
			  GError **error)
{
	CamelFolder *folder;
	CamelMimeMessage *message;
	BodyParts parts = { NULL, NULL, g_ptr_array_new (), g_ptr_array_new () };
	guint idx;
	gchar *data = NULL, *json = NULL;
	gsize size = 0;

	folder = core_ref_folder (core, args[0], args[1], error);
	if (!folder) {
		g_ptr_array_unref (parts.related);
		g_ptr_array_unref (parts.attachments);
		return NULL;
	}

	message = camel_folder_get_message_sync (folder, args[2], core->cancellable, error);
	if (!message) {
		g_ptr_array_unref (parts.related);
		g_ptr_array_unref (parts.attachments);
		g_object_unref (folder);
		return NULL;
	}

	find_body_parts (CAMEL_MIME_PART (message), &parts);
	idx = (guint) g_ascii_strtoull (args[3], NULL, 10);

	if (idx >= parts.attachments->len) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"Attachment #%u not found", idx);
	} else {
		CamelMimePart *part = parts.attachments->pdata[idx];

		data = part_decode_bytes (part, core->cancellable, &size);
		if (g_file_set_contents (args[4], data, (gssize) size, error))
			json = g_strdup_printf ("{\"size\":%" G_GSIZE_FORMAT "}", size);
		g_free (data);
	}

	g_ptr_array_unref (parts.related);
	g_ptr_array_unref (parts.attachments);
	g_object_unref (message);
	g_object_unref (folder);

	return json;
}

/* Nettoie une chaîne (comme l'objet d'un courriel) pour en faire un nom de
 * fichier Windows valide sans caractères interdits.
 * Paramètres :
 *   raw      : chaîne source en UTF-8
 *   fallback : nom utilisé si la chaîne nettoyée est vide
 * Retourne : chaîne nettoyée nouvellement allouée (à libérer avec g_free). */
gchar *
sanitize_filename (const gchar *raw,
		   const gchar *fallback)
{
	GString *out = g_string_new ("");
	const gchar *p;

	if (raw && *raw) {
		for (p = raw; *p; p = g_utf8_next_char (p)) {
			gunichar c = g_utf8_get_char (p);

			if (c < 0x20 || c == '\\' || c == '/' || c == ':' || c == '*' ||
			    c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
				g_string_append_c (out, '_');
			else
				g_string_append_unichar (out, c);

			if (out->len >= 96)
				break;
		}
	}

	g_strstrip (out->str);
	out->len = strlen (out->str);
	while (out->len > 0 && (out->str[out->len - 1] == '.' || out->str[out->len - 1] == ' '))
		g_string_truncate (out, out->len - 1);

	if (out->len == 0)
		g_string_assign (out, fallback);

	return g_string_free (out, FALSE);
}

/* Exporte une liste de messages d'un dossier sous forme de fichiers .eml dans
 * un sous-répertoire temporaire afin de les joindre à un autre courriel.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] JSON {"account": "...", "folder": "...", "uids": [...]}
 *   error : emplacement d'erreur GError
 * Retourne : tableau JSON ["C:\\...\\Objet.eml", ...], ou NULL en cas d'erreur. */
static gchar *
core_export_messages_eml_run (EvoCore *core,
			      gchar **args,
			      GError **error)
{
	JsonNode *root;
	JsonObject *object;
	JsonArray *uids;
	const gchar *account_uid, *folder_name;
	CamelFolder *folder = NULL;
	JsonBuilder *builder = NULL;
	gchar *batch_dir = NULL, *json = NULL;
	guint ii, len;

	root = json_from_string (args[0], error);
	if (!root)
		return NULL;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	account_uid = object ? json_object_get_string_member_with_default (object, "account", NULL) : NULL;
	folder_name = object ? json_object_get_string_member_with_default (object, "folder", NULL) : NULL;
	uids = (object && json_object_has_member (object, "uids")) ? json_object_get_array_member (object, "uids") : NULL;

	if (!account_uid || !folder_name || !uids) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Invalid export_messages_eml request");
		json_node_unref (root);
		return NULL;
	}

	folder = core_ref_folder (core, account_uid, folder_name, error);
	if (!folder) {
		json_node_unref (root);
		return NULL;
	}

	batch_dir = g_strdup_printf ("%s\\EvolutionWin-eml-%" G_GINT64_FORMAT,
		g_get_tmp_dir (), g_get_monotonic_time ());
	g_mkdir_with_parents (batch_dir, 0700);

	builder = json_builder_new ();
	json_builder_begin_array (builder);

	len = json_array_get_length (uids);
	for (ii = 0; ii < len; ii++) {
		const gchar *uid = json_array_get_string_element (uids, ii);
		CamelMimeMessage *message;
		GOutputStream *output;
		const gchar *subject;
		gchar *clean_name, *filename, *full_path, *data;
		gsize size;
		guint suffix = 1;

		if (!uid || !*uid)
			continue;

		message = camel_folder_get_message_sync (folder, uid, core->cancellable, error);
		if (!message)
			goto cleanup;

		subject = camel_mime_message_get_subject (message);
		clean_name = sanitize_filename (subject, "Message");
		filename = g_strdup_printf ("%s.eml", clean_name);
		full_path = g_build_filename (batch_dir, filename, NULL);

		while (g_file_test (full_path, G_FILE_TEST_EXISTS)) {
			g_free (filename);
			g_free (full_path);
			filename = g_strdup_printf ("%s (%u).eml", clean_name, ++suffix);
			full_path = g_build_filename (batch_dir, filename, NULL);
		}

		output = g_memory_output_stream_new_resizable ();
		camel_data_wrapper_write_to_output_stream_sync (
			CAMEL_DATA_WRAPPER (message), output, core->cancellable, NULL);
		g_output_stream_close (output, NULL, NULL);

		size = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output));
		data = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output));
		g_object_unref (output);
		g_object_unref (message);

		if (!g_file_set_contents (full_path, data ? data : "", (gssize) size, error)) {
			g_free (data);
			g_free (clean_name);
			g_free (filename);
			g_free (full_path);
			goto cleanup;
		}

		json_builder_add_string_value (builder, full_path);

		g_free (data);
		g_free (clean_name);
		g_free (filename);
		g_free (full_path);
	}

	json_builder_end_array (builder);
	json = json_from_builder (builder);

cleanup:
	if (builder)
		g_object_unref (builder);
	g_free (batch_dir);
	g_object_unref (folder);
	json_node_unref (root);

	return json;
}

/* --- refresh --- */

const gchar *
folder_info_find_inbox (CamelFolderInfo *fi)
{
	for (; fi; fi = fi->next) {
		const gchar *found;

		if ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX)
			return fi->full_name;

		found = folder_info_find_inbox (fi->child);
		if (found)
			return found;
	}

	return NULL;
}

void
evo_core_get_message (EvoCore *core,
		      const char *account_uid,
		      const char *folder_full_name,
		      const char *message_uid,
		      EvoCoreResultFunc func,
		      void *user_data)
{
	gchar *args[] = { (gchar *) account_uid, (gchar *) folder_full_name, (gchar *) message_uid, NULL };

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);

	core_submit (core, core_get_message_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête d'enregistrement d'une pièce jointe d'un message dans un
 * fichier local.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message
 *   attachment_index : indice de la pièce jointe
 *   dest_path        : chemin complet du fichier de destination
 *   func             : rappel recevant {"size": <octets>}
 *   user_data        : données utilisateur transmises au rappel */
void
evo_core_save_attachment (EvoCore *core,
			  const char *account_uid,
			  const char *folder_full_name,
			  const char *message_uid,
			  unsigned int attachment_index,
			  const char *dest_path,
			  EvoCoreResultFunc func,
			  void *user_data)
{
	gchar idx[16];
	gchar *args[] = {
		(gchar *) account_uid,
		(gchar *) folder_full_name,
		(gchar *) message_uid,
		idx,
		(gchar *) dest_path,
		NULL
	};

	g_return_if_fail (account_uid != NULL);
	g_return_if_fail (folder_full_name != NULL);
	g_return_if_fail (message_uid != NULL);
	g_return_if_fail (dest_path != NULL);

	g_snprintf (idx, sizeof (idx), "%u", attachment_index);
	core_submit (core, core_save_attachment_run, g_strdupv (args), func, user_data);
}

/* Soumet une requête d'exportation d'un ou plusieurs messages sous forme de
 * fichiers .eml temporaires.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : JSON contenant {"account": "...", "folder": "...", "uids": [...]}
 *   func         : rappel recevant le tableau JSON des chemins .eml créés
 *   user_data    : données utilisateur transmises au rappel */
void
evo_core_export_messages_eml (EvoCore *core,
			      const char *request_json,
			      EvoCoreResultFunc func,
			      void *user_data)
{
	gchar *args[] = { (gchar *) request_json, NULL };

	g_return_if_fail (request_json != NULL);

	/* préparation du glisser-déposer : tâche de fond, elle ne doit pas occuper les threads de l'affichage */
	core_submit_background (core, core_export_messages_eml_run, g_strdupv (args), func, user_data);
}
