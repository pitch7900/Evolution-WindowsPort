/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitWebContext and WebKitURISchemeRequest.
 * WebView2 counterparts: the ICoreWebView2Environment (shared by the views of
 * one context) and AddWebResourceRequestedFilter + WebResourceRequested for
 * custom URI schemes (evo-http, cid, mail, ...).
 */

#ifndef WEBKIT_SHIM_WEB_CONTEXT_H
#define WEBKIT_SHIM_WEB_CONTEXT_H

#include <gio/gio.h>
#include <webkit2/webkit-forward.h>
#include <webkit2/webkit-enums.h>

G_BEGIN_DECLS

/* WebKitURISchemeRequest */

#define WEBKIT_TYPE_URI_SCHEME_REQUEST			(webkit_uri_scheme_request_get_type ())
#define WEBKIT_URI_SCHEME_REQUEST(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_URI_SCHEME_REQUEST, WebKitURISchemeRequest))
#define WEBKIT_URI_SCHEME_REQUEST_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_URI_SCHEME_REQUEST, WebKitURISchemeRequestClass))
#define WEBKIT_IS_URI_SCHEME_REQUEST(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_URI_SCHEME_REQUEST))
#define WEBKIT_IS_URI_SCHEME_REQUEST_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_URI_SCHEME_REQUEST))
#define WEBKIT_URI_SCHEME_REQUEST_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_URI_SCHEME_REQUEST, WebKitURISchemeRequestClass))

typedef void (* WebKitURISchemeRequestCallback)	(WebKitURISchemeRequest *request,
						 gpointer user_data);

GType		webkit_uri_scheme_request_get_type
						(void);
const gchar *	webkit_uri_scheme_request_get_scheme
						(WebKitURISchemeRequest *request);
const gchar *	webkit_uri_scheme_request_get_uri
						(WebKitURISchemeRequest *request);
const gchar *	webkit_uri_scheme_request_get_path
						(WebKitURISchemeRequest *request);
const gchar *	webkit_uri_scheme_request_get_http_method
						(WebKitURISchemeRequest *request);
WebKitWebView *	webkit_uri_scheme_request_get_web_view
						(WebKitURISchemeRequest *request);
void		webkit_uri_scheme_request_finish
						(WebKitURISchemeRequest *request,
						 GInputStream *stream,
						 gint64 stream_length,
						 const gchar *content_type);
void		webkit_uri_scheme_request_finish_error
						(WebKitURISchemeRequest *request,
						 GError *error);

/* WebKitWebContext */

#define WEBKIT_TYPE_WEB_CONTEXT			(webkit_web_context_get_type ())
#define WEBKIT_WEB_CONTEXT(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_WEB_CONTEXT, WebKitWebContext))
#define WEBKIT_WEB_CONTEXT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_WEB_CONTEXT, WebKitWebContextClass))
#define WEBKIT_IS_WEB_CONTEXT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_WEB_CONTEXT))
#define WEBKIT_IS_WEB_CONTEXT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_WEB_CONTEXT))
#define WEBKIT_WEB_CONTEXT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_WEB_CONTEXT, WebKitWebContextClass))

GType		webkit_web_context_get_type	(void);
WebKitWebContext *
		webkit_web_context_get_default	(void);
WebKitWebContext *
		webkit_web_context_new		(void);
void		webkit_web_context_set_cache_model
						(WebKitWebContext *context,
						 WebKitCacheModel cache_model);
WebKitCacheModel
		webkit_web_context_get_cache_model
						(WebKitWebContext *context);
void		webkit_web_context_set_web_extensions_directory
						(WebKitWebContext *context,
						 const gchar *directory);
void		webkit_web_context_set_web_extensions_initialization_user_data
						(WebKitWebContext *context,
						 GVariant *user_data);
void		webkit_web_context_set_sandbox_enabled
						(WebKitWebContext *context,
						 gboolean enabled);
gboolean	webkit_web_context_get_sandbox_enabled
						(WebKitWebContext *context);
void		webkit_web_context_add_path_to_sandbox
						(WebKitWebContext *context,
						 const gchar *path,
						 gboolean read_only);
void		webkit_web_context_register_uri_scheme
						(WebKitWebContext *context,
						 const gchar *scheme,
						 WebKitURISchemeRequestCallback callback,
						 gpointer user_data,
						 GDestroyNotify user_data_destroy_func);
gboolean	webkit_web_context_get_spell_checking_enabled
						(WebKitWebContext *context);
void		webkit_web_context_set_spell_checking_enabled
						(WebKitWebContext *context,
						 gboolean enabled);
const gchar * const *
		webkit_web_context_get_spell_checking_languages
						(WebKitWebContext *context);
void		webkit_web_context_set_spell_checking_languages
						(WebKitWebContext *context,
						 const gchar * const *languages);

G_END_DECLS

#endif /* WEBKIT_SHIM_WEB_CONTEXT_H */
