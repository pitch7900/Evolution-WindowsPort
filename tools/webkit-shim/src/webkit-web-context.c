/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitWebContext and WebKitURISchemeRequest.
 *
 * A context stores its configuration and URI scheme handlers.
 * TODO WebView2: one ICoreWebView2Environment per context (user data folder
 * and browser process shared by its views); custom schemes are served by
 * WebResourceRequested (AddWebResourceRequestedFilter("<scheme>:*")) through
 * _webkit_web_context_handle_uri_scheme_request(). Web extensions do not
 * exist with WebView2: "initialize-web-extensions" is still emitted (once,
 * before the first view of the context is created) for compatibility, and
 * the extension directory is ignored.
 */

#include <string.h>

#include "webkit-shim-private.h"

/* ------------------------------------------------------------------------ */
/* WebKitURISchemeRequest                                                   */
/* ------------------------------------------------------------------------ */

struct _WebKitURISchemeRequest {
	GObject parent;

	WebKitWebView *web_view; /* weak */
	gchar *uri;
	gchar *scheme;
	gchar *path;
	gchar *http_method;
	gboolean finished;

	WebKitShimURISchemeResponseFunc response_func;
	gpointer response_data;
};

struct _WebKitURISchemeRequestClass {
	GObjectClass parent_class;
};

G_DEFINE_TYPE (WebKitURISchemeRequest, webkit_uri_scheme_request, G_TYPE_OBJECT)

static void
webkit_uri_scheme_request_finalize (GObject *object)
{
	WebKitURISchemeRequest *request = WEBKIT_URI_SCHEME_REQUEST (object);

	if (!request->finished) {
		g_warning ("%s: request for '%s' finalized without being finished", G_STRFUNC, request->uri);

		/* answer anyway, the page would wait for the resource forever */
		if (request->response_func) {
			GError *error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
				"The request was not answered");

			request->response_func (request, NULL, -1, NULL, error, request->response_data);
			g_error_free (error);
		}
	}

	if (request->web_view)
		g_object_remove_weak_pointer (G_OBJECT (request->web_view), (gpointer *) &request->web_view);

	g_free (request->uri);
	g_free (request->scheme);
	g_free (request->path);
	g_free (request->http_method);

	G_OBJECT_CLASS (webkit_uri_scheme_request_parent_class)->finalize (object);
}

static void
webkit_uri_scheme_request_class_init (WebKitURISchemeRequestClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = webkit_uri_scheme_request_finalize;
}

static void
webkit_uri_scheme_request_init (WebKitURISchemeRequest *request)
{
}

WebKitURISchemeRequest *
_webkit_uri_scheme_request_new (WebKitWebView *web_view,
				const gchar *uri,
				const gchar *http_method,
				WebKitShimURISchemeResponseFunc response_func,
				gpointer response_data)
{
	WebKitURISchemeRequest *request;
	const gchar *colon;

	g_return_val_if_fail (uri != NULL, NULL);

	request = g_object_new (WEBKIT_TYPE_URI_SCHEME_REQUEST, NULL);
	request->uri = g_strdup (uri);
	request->http_method = g_strdup (http_method ? http_method : "GET");
	request->response_func = response_func;
	request->response_data = response_data;

	colon = strchr (uri, ':');
	if (colon) {
		const gchar *path = colon + 1;

		request->scheme = g_ascii_strdown (uri, colon - uri);

		/* "scheme://authority/path" -> "/path"; "scheme:path" -> "path" */
		if (g_str_has_prefix (path, "//")) {
			path = strchr (path + 2, '/');
			if (!path)
				path = "";
		}
		request->path = g_strdup (path);
	} else {
		request->scheme = g_strdup ("");
		request->path = g_strdup (uri);
	}

	if (web_view) {
		request->web_view = web_view;
		g_object_add_weak_pointer (G_OBJECT (web_view), (gpointer *) &request->web_view);
	}

	return request;
}

const gchar *
webkit_uri_scheme_request_get_scheme (WebKitURISchemeRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_URI_SCHEME_REQUEST (request), NULL);

	return request->scheme;
}

const gchar *
webkit_uri_scheme_request_get_uri (WebKitURISchemeRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_URI_SCHEME_REQUEST (request), NULL);

	return request->uri;
}

const gchar *
webkit_uri_scheme_request_get_path (WebKitURISchemeRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_URI_SCHEME_REQUEST (request), NULL);

	return request->path;
}

const gchar *
webkit_uri_scheme_request_get_http_method (WebKitURISchemeRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_URI_SCHEME_REQUEST (request), NULL);

	return request->http_method;
}

WebKitWebView *
webkit_uri_scheme_request_get_web_view (WebKitURISchemeRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_URI_SCHEME_REQUEST (request), NULL);

	return request->web_view;
}

void
webkit_uri_scheme_request_finish (WebKitURISchemeRequest *request,
				  GInputStream *stream,
				  gint64 stream_length,
				  const gchar *content_type)
{
	g_return_if_fail (WEBKIT_IS_URI_SCHEME_REQUEST (request));
	g_return_if_fail (G_IS_INPUT_STREAM (stream));
	g_return_if_fail (!request->finished);

	request->finished = TRUE;

	/* TODO WebView2: the backend wraps the stream into an IStream for
	   the WebResourceRequested response */
	if (request->response_func)
		request->response_func (request, stream, stream_length, content_type, NULL, request->response_data);
}

void
webkit_uri_scheme_request_finish_error (WebKitURISchemeRequest *request,
					GError *error)
{
	g_return_if_fail (WEBKIT_IS_URI_SCHEME_REQUEST (request));
	g_return_if_fail (error != NULL);
	g_return_if_fail (!request->finished);

	request->finished = TRUE;

	if (request->response_func)
		request->response_func (request, NULL, -1, NULL, error, request->response_data);
}

/* ------------------------------------------------------------------------ */
/* WebKitWebContext                                                         */
/* ------------------------------------------------------------------------ */

typedef struct {
	WebKitURISchemeRequestCallback callback;
	gpointer user_data;
	GDestroyNotify user_data_destroy_func;
} URISchemeHandler;

struct _WebKitWebContext {
	GObject parent;

	WebKitCacheModel cache_model;
	gchar *web_extensions_directory;
	GVariant *web_extensions_initialization_user_data;
	gboolean sandbox_enabled;
	GPtrArray *sandbox_paths;
	GHashTable *uri_schemes; /* gchar *scheme ~> URISchemeHandler * */
	gboolean spell_checking_enabled;
	gchar **spell_checking_languages;
	gboolean web_extensions_initialized;
};

struct _WebKitWebContextClass {
	GObjectClass parent_class;

	void (* initialize_web_extensions) (WebKitWebContext *context);
};

enum {
	CONTEXT_SIGNAL_INITIALIZE_WEB_EXTENSIONS,
	CONTEXT_N_SIGNALS
};

static guint context_signals[CONTEXT_N_SIGNALS];

G_DEFINE_TYPE (WebKitWebContext, webkit_web_context, G_TYPE_OBJECT)

static void
uri_scheme_handler_free (gpointer ptr)
{
	URISchemeHandler *handler = ptr;

	if (handler->user_data_destroy_func)
		handler->user_data_destroy_func (handler->user_data);

	g_free (handler);
}

static void
webkit_web_context_finalize (GObject *object)
{
	WebKitWebContext *context = WEBKIT_WEB_CONTEXT (object);

	g_free (context->web_extensions_directory);
	g_clear_pointer (&context->web_extensions_initialization_user_data, g_variant_unref);
	g_ptr_array_unref (context->sandbox_paths);
	g_hash_table_destroy (context->uri_schemes);
	g_strfreev (context->spell_checking_languages);

	G_OBJECT_CLASS (webkit_web_context_parent_class)->finalize (object);
}

static void
webkit_web_context_class_init (WebKitWebContextClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = webkit_web_context_finalize;

	/* void (* initialize_web_extensions) (WebKitWebContext *context,
	                                       gpointer user_data); */
	context_signals[CONTEXT_SIGNAL_INITIALIZE_WEB_EXTENSIONS] = g_signal_new ("initialize-web-extensions",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebContextClass, initialize_web_extensions),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);
}

static void
webkit_web_context_init (WebKitWebContext *context)
{
	context->cache_model = WEBKIT_CACHE_MODEL_WEB_BROWSER;
	context->sandbox_paths = g_ptr_array_new_with_free_func (g_free);
	context->uri_schemes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, uri_scheme_handler_free);
}

WebKitWebContext *
webkit_web_context_get_default (void)
{
	static WebKitWebContext *default_context = NULL;

	if (g_once_init_enter (&default_context)) {
		WebKitWebContext *context = webkit_web_context_new ();

		g_once_init_leave (&default_context, context);
	}

	return default_context;
}

WebKitWebContext *
webkit_web_context_new (void)
{
	return g_object_new (WEBKIT_TYPE_WEB_CONTEXT, NULL);
}

void
_webkit_web_context_ensure_web_extensions_initialized (WebKitWebContext *context)
{
	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));

	/* WebKitGTK emits it when the first web process of the context is
	   spawned; Evolution configures the (unused) extensions there */
	if (!context->web_extensions_initialized) {
		context->web_extensions_initialized = TRUE;
		g_signal_emit (context, context_signals[CONTEXT_SIGNAL_INITIALIZE_WEB_EXTENSIONS], 0);
	}
}

void
webkit_web_context_set_cache_model (WebKitWebContext *context,
				    WebKitCacheModel cache_model)
{
	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));

	/* TODO WebView2: no equivalent; kept for the getter */
	context->cache_model = cache_model;
}

WebKitCacheModel
webkit_web_context_get_cache_model (WebKitWebContext *context)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_CONTEXT (context), WEBKIT_CACHE_MODEL_WEB_BROWSER);

	return context->cache_model;
}

void
webkit_web_context_set_web_extensions_directory (WebKitWebContext *context,
						 const gchar *directory)
{
	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));

	/* WebView2 has no in-process web extensions; stored only */
	g_free (context->web_extensions_directory);
	context->web_extensions_directory = g_strdup (directory);
}

void
webkit_web_context_set_web_extensions_initialization_user_data (WebKitWebContext *context,
								GVariant *user_data)
{
	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));

	g_clear_pointer (&context->web_extensions_initialization_user_data, g_variant_unref);
	if (user_data)
		context->web_extensions_initialization_user_data = g_variant_ref_sink (user_data);
}

void
webkit_web_context_set_sandbox_enabled (WebKitWebContext *context,
					gboolean enabled)
{
	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));

	/* WebView2 renderers are always sandboxed */
	context->sandbox_enabled = enabled;
}

gboolean
webkit_web_context_get_sandbox_enabled (WebKitWebContext *context)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_CONTEXT (context), FALSE);

	return context->sandbox_enabled;
}

void
webkit_web_context_add_path_to_sandbox (WebKitWebContext *context,
					const gchar *path,
					gboolean read_only)
{
	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));
	g_return_if_fail (path != NULL);

	/* TODO WebView2: SetVirtualHostNameToFolderMapping() if file access
	   to these directories turns out to be needed */
	g_ptr_array_add (context->sandbox_paths, g_strdup (path));
}

void
webkit_web_context_register_uri_scheme (WebKitWebContext *context,
					const gchar *scheme,
					WebKitURISchemeRequestCallback callback,
					gpointer user_data,
					GDestroyNotify user_data_destroy_func)
{
	URISchemeHandler *handler;

	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));
	g_return_if_fail (scheme != NULL);
	g_return_if_fail (callback != NULL);

	handler = g_new0 (URISchemeHandler, 1);
	handler->callback = callback;
	handler->user_data = user_data;
	handler->user_data_destroy_func = user_data_destroy_func;

	/* TODO WebView2: custom schemes must also be declared with
	   ICoreWebView2CustomSchemeRegistration when the environment is
	   created, then filtered with AddWebResourceRequestedFilter() */
	g_hash_table_insert (context->uri_schemes, g_ascii_strdown (scheme, -1), handler);
}

gboolean
_webkit_web_context_is_uri_scheme_registered (WebKitWebContext *context,
					      const gchar *scheme)
{
	gchar *lower;
	gboolean registered;

	g_return_val_if_fail (WEBKIT_IS_WEB_CONTEXT (context), FALSE);
	g_return_val_if_fail (scheme != NULL, FALSE);

	lower = g_ascii_strdown (scheme, -1);
	registered = g_hash_table_contains (context->uri_schemes, lower);
	g_free (lower);

	return registered;
}

/* NULL-terminated array of the registered schemes, free with g_strfreev() */
gchar **
_webkit_web_context_dup_uri_schemes (WebKitWebContext *context)
{
	GHashTableIter iter;
	gpointer key;
	GPtrArray *schemes;

	g_return_val_if_fail (WEBKIT_IS_WEB_CONTEXT (context), NULL);

	schemes = g_ptr_array_new ();

	g_hash_table_iter_init (&iter, context->uri_schemes);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (schemes, g_strdup (key));

	g_ptr_array_add (schemes, NULL);

	return (gchar **) g_ptr_array_free (schemes, FALSE);
}

gboolean
_webkit_web_context_handle_uri_scheme_request (WebKitWebContext *context,
					       WebKitURISchemeRequest *request)
{
	URISchemeHandler *handler;

	g_return_val_if_fail (WEBKIT_IS_WEB_CONTEXT (context), FALSE);
	g_return_val_if_fail (WEBKIT_IS_URI_SCHEME_REQUEST (request), FALSE);

	handler = g_hash_table_lookup (context->uri_schemes, request->scheme);
	if (!handler)
		return FALSE;

	handler->callback (request, handler->user_data);

	return TRUE;
}

gboolean
webkit_web_context_get_spell_checking_enabled (WebKitWebContext *context)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_CONTEXT (context), FALSE);

	return context->spell_checking_enabled;
}

void
webkit_web_context_set_spell_checking_enabled (WebKitWebContext *context,
					       gboolean enabled)
{
	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));

	/* TODO WebView2: spell checking is a browser-wide setting of Edge;
	   map to the spellcheck attribute of the editor body */
	context->spell_checking_enabled = enabled;
}

const gchar * const *
webkit_web_context_get_spell_checking_languages (WebKitWebContext *context)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_CONTEXT (context), NULL);

	return (const gchar * const *) context->spell_checking_languages;
}

void
webkit_web_context_set_spell_checking_languages (WebKitWebContext *context,
						 const gchar * const *languages)
{
	g_return_if_fail (WEBKIT_IS_WEB_CONTEXT (context));

	g_strfreev (context->spell_checking_languages);
	context->spell_checking_languages = g_strdupv ((gchar **) languages);
}
