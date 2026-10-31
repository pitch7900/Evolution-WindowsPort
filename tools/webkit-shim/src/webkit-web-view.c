/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitWebView: GObject type with WebKitGTK's properties and signals, and
 * stub methods. State that does not need a rendering engine (settings,
 * zoom level, editable flag, URI, ...) is kept here so that getters return
 * what setters stored and "notify::" handlers fire.
 *
 * The page is rendered by a WebView2 controller (webview2-host.h) living in
 * a native child GdkWindow of the widget, resized with the allocation:
 *   load_html/bytes     -> served from the internal scheme below
 *   load_uri            -> Navigate; NavigationStarting / ContentLoading /
 *                          NavigationCompleted -> "load-changed"
 *   custom URI schemes  -> WebResourceRequested -> the handlers registered
 *                          with webkit_web_context_register_uri_scheme()
 *   evaluate_javascript -> ExecuteScript (JSON result -> JSCValue)
 *   script messages     -> page script (user content manager) +
 *                          WebMessageReceived
 *   decide-policy       -> NavigationStarting / NewWindowRequested
 *   context-menu        -> ContextMenuRequested
 *   mouse-target-changed -> StatusBarTextChanged (hovered link)
 *   web-process-terminated -> ProcessFailed
 */

#include "webkit-shim-private.h"
#include "webview2-host.h"

#include <gdk/gdkwin32.h>

/* Content given to load_html()/load_bytes() is served from this scheme:
 * NavigateToString() is limited to 2 MB and has no base URI */
#define SHIM_PAGE_SCHEME "webkit-shim-page"

/* Schemes Evolution registers; the WebView2 environment needs them before
 * its creation, possibly before the first registration happened */
static const gchar *known_custom_schemes[] = {
	"cid", "evo-file", "evo-http", "evo-https", "gtk-stock", "mail", NULL
};

typedef struct {
	GBytes *bytes;
	gchar *content_type;
} ShimPage;

/* WEBKIT_SHIM_DEBUG=1: trace navigations, requests and script errors */
static gboolean
shim_debug_enabled (void)
{
	static gint enabled = -1;

	if (enabled == -1)
		enabled = g_strcmp0 (g_getenv ("WEBKIT_SHIM_DEBUG"), "1") == 0 ? 1 : 0;

	return enabled == 1;
}

#define SHIM_DEBUG(...) G_STMT_START { \
	if (shim_debug_enabled ()) \
		g_printerr ("webkit-shim: " __VA_ARGS__); \
} G_STMT_END

/* Content-Type for WebView2:
 * - GIO content types are file extensions on Windows (".css"), which
 *   WebView2 rejects (strict MIME checks for style sheets and scripts):
 *   converted to MIME types;
 * - text without a charset is read as Windows-1252 while Evolution
 *   generates UTF-8. */
static gchar *
shim_content_type_with_charset (const gchar *content_type)
{
	gchar *mime_type = NULL;
	gchar *result;

	if (content_type && *content_type == '.')
		mime_type = g_content_type_get_mime_type (content_type);

	if (!mime_type || g_strcmp0 (mime_type, "application/octet-stream") == 0) {
		g_free (mime_type);
		mime_type = g_strdup (content_type && *content_type != '.' ? content_type : NULL);
	}

	/* the registry may not know the common web types */
	if (!mime_type && content_type) {
		if (g_ascii_strcasecmp (content_type, ".css") == 0)
			mime_type = g_strdup ("text/css");
		else if (g_ascii_strcasecmp (content_type, ".js") == 0)
			mime_type = g_strdup ("text/javascript");
		else if (g_ascii_strcasecmp (content_type, ".html") == 0 || g_ascii_strcasecmp (content_type, ".htm") == 0)
			mime_type = g_strdup ("text/html");
		else if (g_ascii_strcasecmp (content_type, ".svg") == 0)
			mime_type = g_strdup ("image/svg+xml");
	}

	if (!mime_type)
		return g_strdup ("application/octet-stream");

	if (g_ascii_strncasecmp (mime_type, "text/", 5) == 0 && !strstr (mime_type, "charset"))
		result = g_strconcat (mime_type, "; charset=utf-8", NULL);
	else
		result = g_strdup (mime_type);

	g_free (mime_type);

	return result;
}

struct _WebKitWebViewPrivate {
	WebView2Host *host;
	JSCContext *jsc_context;
	gulong content_changed_handler_id;
	/* gchar *uri ~> ShimPage * */
	GHashTable *pages;
	guint page_serial;
	/* URI given to load_*(): its navigation needs no policy decision */
	gchar *own_navigation_uri;
	/* last URI navigated to through the API, reloaded by a new host */
	gchar *current_navigation_uri;
	gboolean focus_from_webview;

	WebKitSettings *settings;
	WebKitUserContentManager *user_content_manager;
	WebKitWebContext *web_context;
	WebKitWebView *related_view;

	WebKitFindController *find_controller;
	WebKitWebInspector *inspector;
	WebKitEditorState *editor_state;

	gchar *uri;
	gchar *title;
	gboolean is_loading;
	gdouble estimated_load_progress;
	gboolean editable;
	gdouble zoom_level;
	gboolean is_ephemeral;
	guint64 page_id;
	GdkRGBA background_color;
	gchar **cors_allowlist;
};

enum {
	PROP_0,
	PROP_WEB_CONTEXT,
	PROP_RELATED_VIEW,
	PROP_SETTINGS,
	PROP_USER_CONTENT_MANAGER,
	PROP_TITLE,
	PROP_ESTIMATED_LOAD_PROGRESS,
	PROP_URI,
	PROP_ZOOM_LEVEL,
	PROP_IS_LOADING,
	PROP_EDITABLE,
	PROP_IS_EPHEMERAL,
	PROP_PAGE_ID,
	N_PROPS
};

enum {
	SIGNAL_LOAD_CHANGED,
	SIGNAL_LOAD_FAILED,
	SIGNAL_LOAD_FAILED_WITH_TLS_ERRORS,
	SIGNAL_CREATE,
	SIGNAL_READY_TO_SHOW,
	SIGNAL_RUN_AS_MODAL,
	SIGNAL_CLOSE,
	SIGNAL_DECIDE_POLICY,
	SIGNAL_MOUSE_TARGET_CHANGED,
	SIGNAL_PRINT,
	SIGNAL_CONTEXT_MENU,
	SIGNAL_CONTEXT_MENU_DISMISSED,
	SIGNAL_AUTHENTICATE,
	SIGNAL_WEB_PROCESS_TERMINATED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE_WITH_PRIVATE (WebKitWebView, webkit_web_view, GTK_TYPE_CONTAINER)

/* Boolean-returning signals stop at the first handler returning TRUE */
static gboolean
webkit_web_view_accumulator_object_handled (GSignalInvocationHint *ihint,
					    GValue *return_accu,
					    const GValue *handler_return,
					    gpointer data)
{
	GObject *object = g_value_get_object (handler_return);

	g_value_set_object (return_accu, object);

	return object == NULL;
}

static void
webkit_web_view_set_uri_internal (WebKitWebView *web_view,
				  const gchar *uri)
{
	WebKitWebViewPrivate *priv = web_view->priv;

	if (g_strcmp0 (priv->uri, uri) == 0)
		return;

	g_free (priv->uri);
	priv->uri = g_strdup (uri);

	g_object_notify_by_pspec (G_OBJECT (web_view), properties[PROP_URI]);
}

static void
webkit_web_view_set_loading (WebKitWebView *web_view,
			     gboolean is_loading,
			     gdouble progress)
{
	WebKitWebViewPrivate *priv = web_view->priv;

	g_object_freeze_notify (G_OBJECT (web_view));

	if (priv->is_loading != is_loading) {
		priv->is_loading = is_loading;
		g_object_notify_by_pspec (G_OBJECT (web_view), properties[PROP_IS_LOADING]);
	}

	if (priv->estimated_load_progress != progress) {
		priv->estimated_load_progress = progress;
		g_object_notify_by_pspec (G_OBJECT (web_view), properties[PROP_ESTIMATED_LOAD_PROGRESS]);
	}

	g_object_thaw_notify (G_OBJECT (web_view));
}

/* ------------------------------------------------------------------------ */
/* WebView2 backend                                                         */
/* ------------------------------------------------------------------------ */

static void
shim_page_free (gpointer ptr)
{
	ShimPage *page = ptr;

	if (page) {
		g_bytes_unref (page->bytes);
		g_free (page->content_type);
		g_free (page);
	}
}

/* JSON string literal, safe to embed in a script */
static gchar *
shim_js_string_literal (const gchar *value)
{
	JsonNode *node = json_node_new (JSON_NODE_VALUE);
	gchar *literal;

	json_node_set_string (node, value ? value : "");
	literal = json_to_string (node, FALSE);
	json_node_unref (node);

	return literal;
}

static void
shim_ensure_environment (WebKitWebView *web_view)
{
	static gboolean configured = FALSE;
	GPtrArray *schemes;
	gchar **registered;
	gchar *folder;
	guint ii;

	if (configured)
		return;

	configured = TRUE;

	schemes = g_ptr_array_new_with_free_func (g_free);

	for (ii = 0; known_custom_schemes[ii]; ii++)
		g_ptr_array_add (schemes, g_strdup (known_custom_schemes[ii]));

	registered = _webkit_web_context_dup_uri_schemes (web_view->priv->web_context);
	for (ii = 0; registered && registered[ii]; ii++) {
		if (!g_strv_contains (known_custom_schemes, registered[ii]))
			g_ptr_array_add (schemes, g_strdup (registered[ii]));
	}
	g_strfreev (registered);

	g_ptr_array_add (schemes, g_strdup (SHIM_PAGE_SCHEME));
	g_ptr_array_add (schemes, NULL);

	folder = g_build_filename (g_get_user_cache_dir (), "evolution", "webview2", NULL);
	webview2_environment_configure (folder, (const gchar * const *) schemes->pdata);

	g_free (folder);
	g_ptr_array_unref (schemes);
}

static WebKitShimPolicyResult
shim_decide_policy (WebKitWebView *web_view,
		    const gchar *uri,
		    WebKitPolicyDecisionType decision_type,
		    WebKitNavigationType navigation_type,
		    gboolean is_user_gesture)
{
	WebKitURIRequest *request;
	WebKitNavigationAction *action;
	WebKitPolicyDecision *decision;
	WebKitShimPolicyResult result;
	gboolean handled = FALSE;

	request = webkit_uri_request_new (uri);
	action = _webkit_navigation_action_new (request, navigation_type, 1, 0, is_user_gesture);
	decision = _webkit_navigation_policy_decision_new (action, NULL);

	g_signal_emit (web_view, signals[SIGNAL_DECIDE_POLICY], 0, decision, decision_type, &handled);

	result = _webkit_policy_decision_get_result (decision);

	g_object_unref (decision);
	webkit_navigation_action_free (action);
	g_object_unref (request);

	return result;
}

static void
host_navigation_starting_cb (gpointer user_data,
			     const gchar *uri,
			     gboolean is_user_initiated,
			     gboolean *cancel)
{
	WebKitWebView *web_view = user_data;
	WebKitWebViewPrivate *priv = web_view->priv;
	gboolean is_shim_page = g_str_has_prefix (uri, SHIM_PAGE_SCHEME ":");

	/* loads requested through the API need no decision */
	if (!is_shim_page && g_strcmp0 (uri, priv->own_navigation_uri) != 0) {
		WebKitShimPolicyResult result;

		result = shim_decide_policy (web_view, uri,
			WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION,
			is_user_initiated ? WEBKIT_NAVIGATION_TYPE_LINK_CLICKED : WEBKIT_NAVIGATION_TYPE_OTHER,
			is_user_initiated);

		if (result == WEBKIT_SHIM_POLICY_IGNORE || result == WEBKIT_SHIM_POLICY_DOWNLOAD) {
			*cancel = TRUE;
			return;
		}
	}

	g_clear_pointer (&priv->own_navigation_uri, g_free);

	if (!is_shim_page)
		webkit_web_view_set_uri_internal (web_view, uri);

	webkit_web_view_set_loading (web_view, TRUE, 0.1);
	g_signal_emit (web_view, signals[SIGNAL_LOAD_CHANGED], 0, WEBKIT_LOAD_STARTED);
}

static void
host_content_loading_cb (gpointer user_data)
{
	WebKitWebView *web_view = user_data;

	webkit_web_view_set_loading (web_view, TRUE, 0.5);
	g_signal_emit (web_view, signals[SIGNAL_LOAD_CHANGED], 0, WEBKIT_LOAD_COMMITTED);
}

static void
host_navigation_completed_cb (gpointer user_data,
			      gboolean success,
			      gint web_error_status)
{
	WebKitWebView *web_view = user_data;

	/* cancelled navigations never started */
	if (!web_view->priv->is_loading)
		return;

	if (!success) {
		GError *error;
		gboolean handled = FALSE;

		error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
			"The page could not be loaded (WebView2 error %d)", web_error_status);
		g_signal_emit (web_view, signals[SIGNAL_LOAD_FAILED], 0,
			WEBKIT_LOAD_COMMITTED, web_view->priv->uri, error, &handled);
		g_error_free (error);
	}

	webkit_web_view_set_loading (web_view, FALSE, 1.0);
	g_signal_emit (web_view, signals[SIGNAL_LOAD_CHANGED], 0, WEBKIT_LOAD_FINISHED);
}

static void
host_title_changed_cb (gpointer user_data,
		       const gchar *title)
{
	WebKitWebView *web_view = user_data;
	WebKitWebViewPrivate *priv = web_view->priv;

	if (g_strcmp0 (priv->title, title) == 0)
		return;

	g_free (priv->title);
	priv->title = g_strdup (title);

	g_object_notify_by_pspec (G_OBJECT (web_view), properties[PROP_TITLE]);
}

/* {"name": "<handler>", "value": <JSON value>}, see the page script */
static void
host_web_message_cb (gpointer user_data,
		     const gchar *message)
{
	WebKitWebView *web_view = user_data;
	WebKitWebViewPrivate *priv = web_view->priv;
	JsonNode *root;
	JsonObject *object;
	const gchar *name;

	SHIM_DEBUG ("message %.200s\n", message);

	root = json_from_string (message, NULL);
	if (!root)
		return;

	object = JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
	name = object ? json_object_get_string_member_with_default (object, "name", NULL) : NULL;

	if (g_strcmp0 (name, "__webkit_shim_error") == 0) {
		SHIM_DEBUG ("page error: %s\n", json_object_get_string_member_with_default (object, "value", "?"));
	} else if (name) {
		JsonNode *value = json_object_get_member (object, "value");
		JSCValue *jsc_value;

		jsc_value = _jsc_value_new_take_node (priv->jsc_context,
			value ? json_node_copy (value) : json_node_new (JSON_NODE_NULL));
		_webkit_user_content_manager_dispatch_script_message (priv->user_content_manager, name, jsc_value);
		g_object_unref (jsc_value);
	}

	json_node_unref (root);
}

static gboolean
host_new_window_requested_cb (gpointer user_data,
			      const gchar *uri,
			      gboolean is_user_initiated)
{
	/* Evolution opens such links itself (e.g. in the browser) */
	shim_decide_policy (user_data, uri,
		WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION,
		WEBKIT_NAVIGATION_TYPE_LINK_CLICKED, is_user_initiated);

	return TRUE;
}

typedef struct {
	WebView2Request *request;
	GInputStream *stream;
	GOutputStream *output;
	gchar *content_type;
	GError *error;
} SchemeResponse;

static void
scheme_response_free (SchemeResponse *response)
{
	g_clear_object (&response->stream);
	g_clear_object (&response->output);
	g_clear_error (&response->error);
	g_free (response->content_type);
	g_free (response);
}

static void
scheme_response_spliced_cb (GObject *source_object,
			    GAsyncResult *result,
			    gpointer user_data)
{
	SchemeResponse *response = user_data;
	GError *error = NULL;

	if (g_output_stream_splice_finish (G_OUTPUT_STREAM (source_object), result, &error) < 0) {
		webview2_request_fail (response->request, 500, error ? error->message : NULL);
		g_clear_error (&error);
	} else {
		GBytes *bytes;
		gchar *content_type;

		bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (response->output));
		content_type = shim_content_type_with_charset (response->content_type);

		SHIM_DEBUG ("response %s, %" G_GSIZE_FORMAT " bytes\n", content_type, g_bytes_get_size (bytes));

		webview2_request_respond (response->request, 200, "OK", content_type,
			g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));

		g_free (content_type);
		g_bytes_unref (bytes);
	}

	scheme_response_free (response);
}

/* in the main context: WebView2 objects belong to the UI thread */
static gboolean
scheme_response_start_cb (gpointer user_data)
{
	SchemeResponse *response = user_data;

	if (response->error || !response->stream) {
		SHIM_DEBUG ("response error: %s\n", response->error ? response->error->message : "no stream");
		webview2_request_fail (response->request, 404,
			response->error ? response->error->message : "Not Found");
		scheme_response_free (response);
		return G_SOURCE_REMOVE;
	}

	response->output = g_memory_output_stream_new_resizable ();
	g_output_stream_splice_async (response->output, response->stream,
		G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
		G_PRIORITY_DEFAULT, NULL, scheme_response_spliced_cb, response);

	return G_SOURCE_REMOVE;
}

static void
scheme_request_response_cb (WebKitURISchemeRequest *request,
			    GInputStream *stream,
			    gint64 stream_length,
			    const gchar *content_type,
			    const GError *error,
			    gpointer user_data)
{
	SchemeResponse *response = g_new0 (SchemeResponse, 1);

	response->request = user_data;
	response->stream = stream ? g_object_ref (stream) : NULL;
	response->content_type = g_strdup (content_type);
	response->error = error ? g_error_copy (error) : NULL;

	g_main_context_invoke (NULL, scheme_response_start_cb, response);
}

static void
host_resource_requested_cb (gpointer user_data,
			    WebView2Request *request,
			    const gchar *uri,
			    const gchar *method)
{
	WebKitWebView *web_view = user_data;
	WebKitWebViewPrivate *priv = web_view->priv;
	WebKitURISchemeRequest *scheme_request;

	SHIM_DEBUG ("request %s %s\n", method, uri);

	/* Remote resources of local (Evolution) pages go through the
	   "evo-http(s)" handlers when registered, as Evolution's WebKit web
	   extension does (remote content policy, proxy, caching). Real web
	   pages (OAuth sign-in) and their main resource load normally. */
	if (g_str_has_prefix (uri, "http:") || g_str_has_prefix (uri, "https:")) {
		const gchar *page_uri = priv->current_navigation_uri;
		gchar *evo_uri;

		if (g_strcmp0 (uri, page_uri) == 0 ||
		    (page_uri && (g_str_has_prefix (page_uri, "http:") || g_str_has_prefix (page_uri, "https:"))) ||
		    !_webkit_web_context_is_uri_scheme_registered (priv->web_context,
			g_str_has_prefix (uri, "https:") ? "evo-https" : "evo-http")) {
			webview2_request_continue (request);
			return;
		}

		evo_uri = g_strconcat ("evo-", uri, NULL);
		host_resource_requested_cb (user_data, request, evo_uri, method);
		g_free (evo_uri);

		return;
	}

	if (g_str_has_prefix (uri, SHIM_PAGE_SCHEME ":")) {
		ShimPage *page = g_hash_table_lookup (priv->pages, uri);

		if (page) {
			webview2_request_respond (request, 200, "OK", page->content_type,
				g_bytes_get_data (page->bytes, NULL), g_bytes_get_size (page->bytes));
		} else {
			webview2_request_fail (request, 404, "Not Found");
		}

		return;
	}

	scheme_request = _webkit_uri_scheme_request_new (web_view, uri, method,
		scheme_request_response_cb, request);

	/* FALSE: no handler for the scheme, the request was not touched */
	if (!_webkit_web_context_handle_uri_scheme_request (priv->web_context, scheme_request)) {
		GError *error = g_error_new (G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No handler for “%s”", uri);

		webkit_uri_scheme_request_finish_error (scheme_request, error);
		g_error_free (error);
	}

	g_object_unref (scheme_request);
}

static gboolean
host_context_menu_cb (gpointer user_data,
		      gint x,
		      gint y,
		      guint target_flags,
		      const gchar *link_uri,
		      const gchar *image_uri)
{
	WebKitWebView *web_view = user_data;
	GtkWidget *widget = GTK_WIDGET (web_view);
	GdkWindow *window = gtk_widget_get_window (widget);
	WebKitHitTestResult *hit_test_result;
	WebKitContextMenu *context_menu;
	GdkEvent *event;
	GdkSeat *seat;
	guint context = WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT;
	gint scale = gtk_widget_get_scale_factor (widget);
	gint root_x = 0, root_y = 0;
	gboolean handled = FALSE;

	if (!window)
		return TRUE;

	if (target_flags & WEBVIEW2_TARGET_LINK)
		context |= WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK;
	if (target_flags & WEBVIEW2_TARGET_IMAGE)
		context |= WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE;
	if (target_flags & WEBVIEW2_TARGET_EDITABLE)
		context |= WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE;
	if (target_flags & WEBVIEW2_TARGET_SELECTION)
		context |= WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION;

	hit_test_result = _webkit_hit_test_result_new (context, link_uri, NULL, NULL, image_uri, NULL);
	context_menu = webkit_context_menu_new ();

	/* WebView2 coordinates are physical pixels of the native window */
	x /= scale > 0 ? scale : 1;
	y /= scale > 0 ? scale : 1;
	gdk_window_get_root_coords (window, x, y, &root_x, &root_y);

	event = gdk_event_new (GDK_BUTTON_PRESS);
	event->button.window = g_object_ref (window);
	event->button.send_event = TRUE;
	event->button.time = GDK_CURRENT_TIME;
	event->button.x = x;
	event->button.y = y;
	event->button.x_root = root_x;
	event->button.y_root = root_y;
	event->button.button = 3;

	seat = gdk_display_get_default_seat (gdk_window_get_display (window));
	if (seat)
		gdk_event_set_device (event, gdk_seat_get_pointer (seat));

	g_signal_emit (web_view, signals[SIGNAL_CONTEXT_MENU], 0, context_menu, event, hit_test_result, &handled);

	gdk_event_free (event);
	g_object_unref (context_menu);
	g_object_unref (hit_test_result);

	/* the WebView2 default menus are disabled anyway */
	return TRUE;
}

static void
host_status_text_changed_cb (gpointer user_data,
			     const gchar *text)
{
	WebKitWebView *web_view = user_data;
	WebKitHitTestResult *hit_test_result;
	gboolean is_link = text && *text;

	hit_test_result = _webkit_hit_test_result_new (
		WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT | (is_link ? WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK : 0),
		is_link ? text : NULL, NULL, NULL, NULL, NULL);

	g_signal_emit (web_view, signals[SIGNAL_MOUSE_TARGET_CHANGED], 0, hit_test_result, 0);

	g_object_unref (hit_test_result);
}

static void
host_process_failed_cb (gpointer user_data,
			gint kind)
{
	g_signal_emit (user_data, signals[SIGNAL_WEB_PROCESS_TERMINATED], 0, WEBKIT_WEB_PROCESS_CRASHED);
}

static void
host_focus_changed_cb (gpointer user_data,
		       gboolean has_focus)
{
	WebKitWebView *web_view = user_data;

	if (has_focus && !gtk_widget_has_focus (GTK_WIDGET (web_view))) {
		web_view->priv->focus_from_webview = TRUE;
		gtk_widget_grab_focus (GTK_WIDGET (web_view));
		web_view->priv->focus_from_webview = FALSE;
	}
}

static const WebView2HostCallbacks host_callbacks = {
	host_navigation_starting_cb,
	host_content_loading_cb,
	host_navigation_completed_cb,
	host_title_changed_cb,
	host_web_message_cb,
	host_new_window_requested_cb,
	host_resource_requested_cb,
	host_context_menu_cb,
	host_status_text_changed_cb,
	host_process_failed_cb,
	host_focus_changed_cb
};

static void
user_content_changed_cb (WebKitUserContentManager *manager,
			 WebKitWebView *web_view)
{
	gchar *script;

	script = _webkit_user_content_manager_build_page_script (manager);
	webview2_host_set_page_script (web_view->priv->host, script);
	g_free (script);

	/* the current document keeps its page script: update its styles */
	script = _webkit_user_content_manager_build_style_update (manager);
	webview2_host_execute_script (web_view->priv->host, script, NULL, NULL);
	g_free (script);
}

/* serves content from the internal scheme and navigates to it */
static void
shim_load_page (WebKitWebView *web_view,
		GBytes *bytes,
		const gchar *content_type,
		const gchar *base_uri)
{
	WebKitWebViewPrivate *priv = web_view->priv;
	ShimPage *page = g_new0 (ShimPage, 1);
	gchar *uri;

	page->bytes = g_bytes_ref (bytes);
	page->content_type = g_strdup (content_type);

	uri = g_strdup_printf ("%s:page-%u", SHIM_PAGE_SCHEME, ++priv->page_serial);

	/* only the current page can still be requested */
	g_hash_table_remove_all (priv->pages);
	g_hash_table_insert (priv->pages, g_strdup (uri), page);

	webkit_web_view_set_uri_internal (web_view, base_uri ? base_uri : "about:blank");

	g_free (priv->own_navigation_uri);
	priv->own_navigation_uri = g_strdup (uri);
	g_free (priv->current_navigation_uri);
	priv->current_navigation_uri = g_strdup (uri);

	webview2_host_navigate (priv->host, uri);

	g_free (uri);
}

static void
webkit_web_view_set_property (GObject *object,
			      guint property_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
	WebKitWebViewPrivate *priv = web_view->priv;

	switch (property_id) {
	case PROP_WEB_CONTEXT:
		priv->web_context = g_value_dup_object (value);
		break;
	case PROP_RELATED_VIEW:
		priv->related_view = g_value_dup_object (value);
		break;
	case PROP_SETTINGS:
		/* construct property: NULL at construction means default */
		if (g_value_get_object (value))
			webkit_web_view_set_settings (web_view, g_value_get_object (value));
		break;
	case PROP_USER_CONTENT_MANAGER:
		priv->user_content_manager = g_value_dup_object (value);
		break;
	case PROP_ZOOM_LEVEL:
		webkit_web_view_set_zoom_level (web_view, g_value_get_double (value));
		break;
	case PROP_EDITABLE:
		webkit_web_view_set_editable (web_view, g_value_get_boolean (value));
		break;
	case PROP_IS_EPHEMERAL:
		priv->is_ephemeral = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_web_view_get_property (GObject *object,
			      guint property_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
	WebKitWebViewPrivate *priv = web_view->priv;

	switch (property_id) {
	case PROP_WEB_CONTEXT:
		g_value_set_object (value, priv->web_context);
		break;
	case PROP_RELATED_VIEW:
		g_value_set_object (value, priv->related_view);
		break;
	case PROP_SETTINGS:
		g_value_set_object (value, priv->settings);
		break;
	case PROP_USER_CONTENT_MANAGER:
		g_value_set_object (value, priv->user_content_manager);
		break;
	case PROP_TITLE:
		g_value_set_string (value, priv->title);
		break;
	case PROP_ESTIMATED_LOAD_PROGRESS:
		g_value_set_double (value, priv->estimated_load_progress);
		break;
	case PROP_URI:
		g_value_set_string (value, priv->uri);
		break;
	case PROP_ZOOM_LEVEL:
		g_value_set_double (value, priv->zoom_level);
		break;
	case PROP_IS_LOADING:
		g_value_set_boolean (value, priv->is_loading);
		break;
	case PROP_EDITABLE:
		g_value_set_boolean (value, priv->editable);
		break;
	case PROP_IS_EPHEMERAL:
		g_value_set_boolean (value, priv->is_ephemeral);
		break;
	case PROP_PAGE_ID:
		g_value_set_uint64 (value, priv->page_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_web_view_constructed (GObject *object)
{
	WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
	WebKitWebViewPrivate *priv = web_view->priv;
	static guint64 last_page_id = 0;

	G_OBJECT_CLASS (webkit_web_view_parent_class)->constructed (object);

	/* As in WebKitGTK: a related view shares its context and content
	   manager; otherwise the defaults are used */
	if (!priv->web_context) {
		priv->web_context = priv->related_view ?
			g_object_ref (webkit_web_view_get_context (priv->related_view)) :
			g_object_ref (webkit_web_context_get_default ());
	}

	if (!priv->user_content_manager) {
		priv->user_content_manager = priv->related_view ?
			g_object_ref (webkit_web_view_get_user_content_manager (priv->related_view)) :
			webkit_user_content_manager_new ();
	}

	if (!priv->settings)
		priv->settings = webkit_settings_new ();

	priv->page_id = ++last_page_id;

	/* the WebView2 lives in a native child window of the widget */
	gtk_widget_set_has_window (GTK_WIDGET (web_view), TRUE);
	gtk_widget_set_can_focus (GTK_WIDGET (web_view), TRUE);

	_webkit_web_context_ensure_web_extensions_initialized (priv->web_context);

	priv->jsc_context = jsc_context_new ();
	priv->pages = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, shim_page_free);
	priv->host = webview2_host_new (&host_callbacks, web_view);

	webview2_host_set_zoom (priv->host, priv->zoom_level);
	webview2_host_set_dev_tools_enabled (priv->host,
		webkit_settings_get_enable_developer_extras (priv->settings));

	priv->content_changed_handler_id = g_signal_connect (priv->user_content_manager,
		"webkit-shim-changed", G_CALLBACK (user_content_changed_cb), web_view);
	user_content_changed_cb (priv->user_content_manager, web_view);
}

static void
webkit_web_view_dispose (GObject *object)
{
	WebKitWebViewPrivate *priv = WEBKIT_WEB_VIEW (object)->priv;

	if (priv->content_changed_handler_id) {
		g_signal_handler_disconnect (priv->user_content_manager, priv->content_changed_handler_id);
		priv->content_changed_handler_id = 0;
	}

	g_clear_pointer (&priv->host, webview2_host_free);
	g_clear_object (&priv->jsc_context);
	g_clear_pointer (&priv->pages, g_hash_table_destroy);

	g_clear_object (&priv->find_controller);
	g_clear_object (&priv->inspector);
	g_clear_object (&priv->editor_state);
	g_clear_object (&priv->related_view);
	g_clear_object (&priv->settings);
	g_clear_object (&priv->user_content_manager);
	g_clear_object (&priv->web_context);

	G_OBJECT_CLASS (webkit_web_view_parent_class)->dispose (object);
}

static void
webkit_web_view_finalize (GObject *object)
{
	WebKitWebViewPrivate *priv = WEBKIT_WEB_VIEW (object)->priv;

	g_free (priv->uri);
	g_free (priv->title);
	g_free (priv->own_navigation_uri);
	g_free (priv->current_navigation_uri);
	g_strfreev (priv->cors_allowlist);

	G_OBJECT_CLASS (webkit_web_view_parent_class)->finalize (object);
}

/* GtkContainer without children: the web content is a native window */
static void
webkit_web_view_forall (GtkContainer *container,
			gboolean include_internals,
			GtkCallback callback,
			gpointer callback_data)
{
}

static void
webkit_web_view_realize (GtkWidget *widget)
{
	WebKitWebView *web_view = WEBKIT_WEB_VIEW (widget);
	GtkAllocation allocation;
	GdkWindowAttr attributes;
	GdkWindow *window;

	gtk_widget_set_realized (widget, TRUE);
	gtk_widget_get_allocation (widget, &allocation);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = allocation.x;
	attributes.y = allocation.y;
	attributes.width = allocation.width;
	attributes.height = allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK | GDK_FOCUS_CHANGE_MASK;

	window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes,
		GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);
	gtk_widget_register_window (widget, window);
	gtk_widget_set_window (widget, window);

	/* WebView2 needs a real HWND to be parented to */
	gdk_window_ensure_native (window);

	shim_ensure_environment (web_view);
	webview2_host_attach (web_view->priv->host, gdk_win32_window_get_handle (window));
}

static void
webkit_web_view_unrealize (GtkWidget *widget)
{
	WebKitWebView *web_view = WEBKIT_WEB_VIEW (widget);
	WebKitWebViewPrivate *priv = web_view->priv;
	gchar *script;

	/* the WebView2 dies with its parent window: start over with a new
	   host, which is attached again on the next realize */
	webview2_host_free (priv->host);
	priv->host = webview2_host_new (&host_callbacks, web_view);
	webview2_host_set_zoom (priv->host, priv->zoom_level);

	script = _webkit_user_content_manager_build_page_script (priv->user_content_manager);
	webview2_host_set_page_script (priv->host, script);
	g_free (script);

	if (priv->current_navigation_uri) {
		g_free (priv->own_navigation_uri);
		priv->own_navigation_uri = g_strdup (priv->current_navigation_uri);
		webview2_host_navigate (priv->host, priv->current_navigation_uri);
	}

	GTK_WIDGET_CLASS (webkit_web_view_parent_class)->unrealize (widget);
}

static void
webkit_web_view_size_allocate (GtkWidget *widget,
			       GtkAllocation *allocation)
{
	gtk_widget_set_allocation (widget, allocation);

	if (gtk_widget_get_realized (widget)) {
		gdk_window_move_resize (gtk_widget_get_window (widget),
			allocation->x, allocation->y, allocation->width, allocation->height);
		webview2_host_update_bounds (WEBKIT_WEB_VIEW (widget)->priv->host);
	}
}

static void
webkit_web_view_map (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (webkit_web_view_parent_class)->map (widget);

	webview2_host_set_visible (WEBKIT_WEB_VIEW (widget)->priv->host, TRUE);
	webview2_host_update_bounds (WEBKIT_WEB_VIEW (widget)->priv->host);
}

static void
webkit_web_view_unmap (GtkWidget *widget)
{
	webview2_host_set_visible (WEBKIT_WEB_VIEW (widget)->priv->host, FALSE);

	GTK_WIDGET_CLASS (webkit_web_view_parent_class)->unmap (widget);
}

static gboolean
webkit_web_view_focus_in_event (GtkWidget *widget,
				GdkEventFocus *event)
{
	WebKitWebView *web_view = WEBKIT_WEB_VIEW (widget);

	/* keyboard focus given by GTK goes to the web content */
	if (!web_view->priv->focus_from_webview)
		webview2_host_focus (web_view->priv->host);

	return GTK_WIDGET_CLASS (webkit_web_view_parent_class)->focus_in_event (widget, event);
}

static void
webkit_web_view_class_init (WebKitWebViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

	object_class->set_property = webkit_web_view_set_property;
	object_class->get_property = webkit_web_view_get_property;
	object_class->constructed = webkit_web_view_constructed;
	object_class->dispose = webkit_web_view_dispose;
	object_class->finalize = webkit_web_view_finalize;

	widget_class->realize = webkit_web_view_realize;
	widget_class->unrealize = webkit_web_view_unrealize;
	widget_class->size_allocate = webkit_web_view_size_allocate;
	widget_class->map = webkit_web_view_map;
	widget_class->unmap = webkit_web_view_unmap;
	widget_class->focus_in_event = webkit_web_view_focus_in_event;

	container_class->forall = webkit_web_view_forall;

	/* Properties. EWebView and EWebKitEditor replace "settings",
	   "user-content-manager" and "web-context" in their constructor(),
	   so these must be construct properties. */
	properties[PROP_WEB_CONTEXT] = g_param_spec_object ("web-context", NULL, NULL,
		WEBKIT_TYPE_WEB_CONTEXT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	properties[PROP_RELATED_VIEW] = g_param_spec_object ("related-view", NULL, NULL,
		WEBKIT_TYPE_WEB_VIEW, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	properties[PROP_SETTINGS] = g_param_spec_object ("settings", NULL, NULL,
		WEBKIT_TYPE_SETTINGS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);
	properties[PROP_USER_CONTENT_MANAGER] = g_param_spec_object ("user-content-manager", NULL, NULL,
		WEBKIT_TYPE_USER_CONTENT_MANAGER, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	properties[PROP_TITLE] = g_param_spec_string ("title", NULL, NULL,
		NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	properties[PROP_ESTIMATED_LOAD_PROGRESS] = g_param_spec_double ("estimated-load-progress", NULL, NULL,
		0.0, 1.0, 1.0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	properties[PROP_URI] = g_param_spec_string ("uri", NULL, NULL,
		NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	properties[PROP_ZOOM_LEVEL] = g_param_spec_double ("zoom-level", NULL, NULL,
		0.0, G_MAXDOUBLE, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	properties[PROP_IS_LOADING] = g_param_spec_boolean ("is-loading", NULL, NULL,
		FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	properties[PROP_EDITABLE] = g_param_spec_boolean ("editable", NULL, NULL,
		FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	properties[PROP_IS_EPHEMERAL] = g_param_spec_boolean ("is-ephemeral", NULL, NULL,
		FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	properties[PROP_PAGE_ID] = g_param_spec_uint64 ("page-id", NULL, NULL,
		0, G_MAXUINT64, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/* Signals. The marshallers are the generic one (NULL). */

	/* void (* load_changed) (WebKitWebView *web_view,
	                          WebKitLoadEvent load_event,
	                          gpointer user_data); */
	signals[SIGNAL_LOAD_CHANGED] = g_signal_new ("load-changed",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, load_changed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1, WEBKIT_TYPE_LOAD_EVENT);

	/* gboolean (* load_failed) (WebKitWebView *web_view,
	                             WebKitLoadEvent load_event,
	                             gchar *failing_uri,
	                             GError *error,
	                             gpointer user_data); */
	signals[SIGNAL_LOAD_FAILED] = g_signal_new ("load-failed",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, load_failed),
		g_signal_accumulator_true_handled, NULL, NULL,
		G_TYPE_BOOLEAN, 3,
		WEBKIT_TYPE_LOAD_EVENT,
		G_TYPE_STRING,
		G_TYPE_ERROR | G_SIGNAL_TYPE_STATIC_SCOPE);

	/* gboolean (* load_failed_with_tls_errors) (WebKitWebView *web_view,
	                                             gchar *failing_uri,
	                                             GTlsCertificate *certificate,
	                                             GTlsCertificateFlags errors,
	                                             gpointer user_data); */
	signals[SIGNAL_LOAD_FAILED_WITH_TLS_ERRORS] = g_signal_new ("load-failed-with-tls-errors",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, load_failed_with_tls_errors),
		g_signal_accumulator_true_handled, NULL, NULL,
		G_TYPE_BOOLEAN, 3,
		G_TYPE_STRING,
		G_TYPE_TLS_CERTIFICATE,
		G_TYPE_TLS_CERTIFICATE_FLAGS);

	/* GtkWidget * (* create) (WebKitWebView *web_view,
	                           WebKitNavigationAction *navigation_action,
	                           gpointer user_data); */
	signals[SIGNAL_CREATE] = g_signal_new ("create",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, create),
		webkit_web_view_accumulator_object_handled, NULL, NULL,
		GTK_TYPE_WIDGET, 1,
		WEBKIT_TYPE_NAVIGATION_ACTION | G_SIGNAL_TYPE_STATIC_SCOPE);

	/* void (* ready_to_show) (WebKitWebView *web_view, gpointer user_data); */
	signals[SIGNAL_READY_TO_SHOW] = g_signal_new ("ready-to-show",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, ready_to_show),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/* void (* run_as_modal) (WebKitWebView *web_view, gpointer user_data); */
	signals[SIGNAL_RUN_AS_MODAL] = g_signal_new ("run-as-modal",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (WebKitWebViewClass, run_as_modal),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/* void (* close) (WebKitWebView *web_view, gpointer user_data); */
	signals[SIGNAL_CLOSE] = g_signal_new ("close",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, close),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/* gboolean (* decide_policy) (WebKitWebView *web_view,
	                               WebKitPolicyDecision *decision,
	                               WebKitPolicyDecisionType decision_type,
	                               gpointer user_data); */
	signals[SIGNAL_DECIDE_POLICY] = g_signal_new ("decide-policy",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, decide_policy),
		g_signal_accumulator_true_handled, NULL, NULL,
		G_TYPE_BOOLEAN, 2,
		WEBKIT_TYPE_POLICY_DECISION,
		WEBKIT_TYPE_POLICY_DECISION_TYPE);

	/* void (* mouse_target_changed) (WebKitWebView *web_view,
	                                  WebKitHitTestResult *hit_test_result,
	                                  guint modifiers,
	                                  gpointer user_data); */
	signals[SIGNAL_MOUSE_TARGET_CHANGED] = g_signal_new ("mouse-target-changed",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, mouse_target_changed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		WEBKIT_TYPE_HIT_TEST_RESULT,
		G_TYPE_UINT);

	/* gboolean (* print) (WebKitWebView *web_view,
	                       WebKitPrintOperation *print_operation,
	                       gpointer user_data); */
	signals[SIGNAL_PRINT] = g_signal_new ("print",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, print),
		g_signal_accumulator_true_handled, NULL, NULL,
		G_TYPE_BOOLEAN, 1,
		WEBKIT_TYPE_PRINT_OPERATION);

	/* gboolean (* context_menu) (WebKitWebView *web_view,
	                              WebKitContextMenu *context_menu,
	                              GdkEvent *event,
	                              WebKitHitTestResult *hit_test_result,
	                              gpointer user_data); */
	signals[SIGNAL_CONTEXT_MENU] = g_signal_new ("context-menu",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, context_menu),
		g_signal_accumulator_true_handled, NULL, NULL,
		G_TYPE_BOOLEAN, 3,
		WEBKIT_TYPE_CONTEXT_MENU,
		GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE,
		WEBKIT_TYPE_HIT_TEST_RESULT);

	/* void (* context_menu_dismissed) (WebKitWebView *web_view, gpointer user_data); */
	signals[SIGNAL_CONTEXT_MENU_DISMISSED] = g_signal_new ("context-menu-dismissed",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, context_menu_dismissed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/* gboolean (* authenticate) (WebKitWebView *web_view,
	                              WebKitAuthenticationRequest *request,
	                              gpointer user_data); */
	signals[SIGNAL_AUTHENTICATE] = g_signal_new ("authenticate",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, authenticate),
		g_signal_accumulator_true_handled, NULL, NULL,
		G_TYPE_BOOLEAN, 1,
		WEBKIT_TYPE_AUTHENTICATION_REQUEST);

	/* void (* web_process_terminated) (WebKitWebView *web_view,
	                                    WebKitWebProcessTerminationReason reason,
	                                    gpointer user_data); */
	signals[SIGNAL_WEB_PROCESS_TERMINATED] = g_signal_new ("web-process-terminated",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WebKitWebViewClass, web_process_terminated),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		WEBKIT_TYPE_WEB_PROCESS_TERMINATION_REASON);
}

static void
webkit_web_view_init (WebKitWebView *web_view)
{
	web_view->priv = webkit_web_view_get_instance_private (web_view);
	web_view->priv->zoom_level = 1.0;
	web_view->priv->estimated_load_progress = 1.0;
	web_view->priv->background_color.red = 1.0;
	web_view->priv->background_color.green = 1.0;
	web_view->priv->background_color.blue = 1.0;
	web_view->priv->background_color.alpha = 1.0;
}

/* ------------------------------------------------------------------------ */
/* Construction and accessors                                               */
/* ------------------------------------------------------------------------ */

GtkWidget *
webkit_web_view_new (void)
{
	return g_object_new (WEBKIT_TYPE_WEB_VIEW, NULL);
}

GtkWidget *
webkit_web_view_new_with_context (WebKitWebContext *context)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_CONTEXT (context), NULL);

	return g_object_new (WEBKIT_TYPE_WEB_VIEW, "web-context", context, NULL);
}

GtkWidget *
webkit_web_view_new_with_settings (WebKitSettings *settings)
{
	g_return_val_if_fail (WEBKIT_IS_SETTINGS (settings), NULL);

	return g_object_new (WEBKIT_TYPE_WEB_VIEW, "settings", settings, NULL);
}

GtkWidget *
webkit_web_view_new_with_user_content_manager (WebKitUserContentManager *user_content_manager)
{
	g_return_val_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (user_content_manager), NULL);

	return g_object_new (WEBKIT_TYPE_WEB_VIEW, "user-content-manager", user_content_manager, NULL);
}

GtkWidget *
webkit_web_view_new_with_related_view (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	return g_object_new (WEBKIT_TYPE_WEB_VIEW,
		"related-view", web_view,
		"settings", webkit_web_view_get_settings (web_view),
		NULL);
}

WebKitWebContext *
webkit_web_view_get_context (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	return web_view->priv->web_context;
}

WebKitSettings *
webkit_web_view_get_settings (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	/* the settings exist from the construction on */
	if (!web_view->priv->settings)
		web_view->priv->settings = webkit_settings_new ();

	return web_view->priv->settings;
}

void
webkit_web_view_set_settings (WebKitWebView *web_view,
			      WebKitSettings *settings)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (WEBKIT_IS_SETTINGS (settings));

	if (g_set_object (&web_view->priv->settings, settings)) {
		/* TODO WebView2: apply the settings to ICoreWebView2Settings */
		g_object_notify_by_pspec (G_OBJECT (web_view), properties[PROP_SETTINGS]);
	}
}

WebKitUserContentManager *
webkit_web_view_get_user_content_manager (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	return web_view->priv->user_content_manager;
}

WebKitFindController *
webkit_web_view_get_find_controller (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	if (!web_view->priv->find_controller)
		web_view->priv->find_controller = _webkit_find_controller_new (web_view);

	return web_view->priv->find_controller;
}

WebKitWebInspector *
webkit_web_view_get_inspector (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	if (!web_view->priv->inspector)
		web_view->priv->inspector = _webkit_web_inspector_new (web_view);

	return web_view->priv->inspector;
}

WebKitEditorState *
webkit_web_view_get_editor_state (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	if (!web_view->priv->editor_state)
		web_view->priv->editor_state = _webkit_editor_state_new ();

	return web_view->priv->editor_state;
}

guint64
webkit_web_view_get_page_id (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), 0);

	return web_view->priv->page_id;
}

/* ------------------------------------------------------------------------ */
/* Loading                                                                  */
/* ------------------------------------------------------------------------ */

void
webkit_web_view_load_uri (WebKitWebView *web_view,
			  const gchar *uri)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (uri != NULL);

	g_free (web_view->priv->own_navigation_uri);
	web_view->priv->own_navigation_uri = g_strdup (uri);
	g_free (web_view->priv->current_navigation_uri);
	web_view->priv->current_navigation_uri = g_strdup (uri);

	webkit_web_view_set_uri_internal (web_view, uri);
	webview2_host_navigate (web_view->priv->host, uri);
}

void
webkit_web_view_load_html (WebKitWebView *web_view,
			   const gchar *content,
			   const gchar *base_uri)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (content != NULL);

	GBytes *bytes;

	/* relative URLs do not resolve against base_uri (internal scheme);
	   Evolution uses absolute URIs */
	bytes = g_bytes_new (content, strlen (content));
	shim_load_page (web_view, bytes, "text/html; charset=utf-8", base_uri);
	g_bytes_unref (bytes);
}

void
webkit_web_view_load_plain_text (WebKitWebView *web_view,
				 const gchar *plain_text)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (plain_text != NULL);

	GBytes *bytes;

	bytes = g_bytes_new (plain_text, strlen (plain_text));
	shim_load_page (web_view, bytes, "text/plain; charset=utf-8", NULL);
	g_bytes_unref (bytes);
}

void
webkit_web_view_load_bytes (WebKitWebView *web_view,
			    GBytes *bytes,
			    const gchar *mime_type,
			    const gchar *encoding,
			    const gchar *base_uri)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (bytes != NULL);

	gchar *content_type;

	content_type = encoding && *encoding ?
		g_strdup_printf ("%s; charset=%s", mime_type ? mime_type : "text/html", encoding) :
		g_strdup (mime_type ? mime_type : "text/html");

	shim_load_page (web_view, bytes, content_type, base_uri);

	g_free (content_type);
}

void
webkit_web_view_reload (WebKitWebView *web_view)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));

	webview2_host_reload (web_view->priv->host);
}

void
webkit_web_view_stop_loading (WebKitWebView *web_view)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));

	webview2_host_stop (web_view->priv->host);
}

gboolean
webkit_web_view_is_loading (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), FALSE);

	return web_view->priv->is_loading;
}

const gchar *
webkit_web_view_get_uri (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	return web_view->priv->uri;
}

const gchar *
webkit_web_view_get_title (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	return web_view->priv->title;
}

gdouble
webkit_web_view_get_estimated_load_progress (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), 0.0);

	return web_view->priv->estimated_load_progress;
}

gboolean
webkit_web_view_can_show_mime_type (WebKitWebView *web_view,
				    const gchar *mime_type)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), FALSE);
	g_return_val_if_fail (mime_type != NULL, FALSE);

	/* What Edge renders natively (Evolution asks before inlining
	   attachments); TODO WebView2: refine (PDF viewer, media codecs) */
	return g_str_has_prefix (mime_type, "text/") ||
		g_str_has_prefix (mime_type, "image/") ||
		g_ascii_strcasecmp (mime_type, "application/xhtml+xml") == 0 ||
		g_ascii_strcasecmp (mime_type, "application/pdf") == 0;
}

/* ------------------------------------------------------------------------ */
/* Appearance                                                               */
/* ------------------------------------------------------------------------ */

void
webkit_web_view_set_zoom_level (WebKitWebView *web_view,
				gdouble zoom_level)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));

	if (web_view->priv->zoom_level == zoom_level)
		return;

	web_view->priv->zoom_level = zoom_level;
	webview2_host_set_zoom (web_view->priv->host, zoom_level);

	g_object_notify_by_pspec (G_OBJECT (web_view), properties[PROP_ZOOM_LEVEL]);
}

gdouble
webkit_web_view_get_zoom_level (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), 1.0);

	return web_view->priv->zoom_level;
}

void
webkit_web_view_set_background_color (WebKitWebView *web_view,
				      const GdkRGBA *rgba)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (rgba != NULL);

	web_view->priv->background_color = *rgba;
	webview2_host_set_background (web_view->priv->host,
		(guint8) (CLAMP (rgba->red, 0.0, 1.0) * 255),
		(guint8) (CLAMP (rgba->green, 0.0, 1.0) * 255),
		(guint8) (CLAMP (rgba->blue, 0.0, 1.0) * 255),
		(guint8) (CLAMP (rgba->alpha, 0.0, 1.0) * 255));
}

void
webkit_web_view_get_background_color (WebKitWebView *web_view,
				      GdkRGBA *rgba)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (rgba != NULL);

	*rgba = web_view->priv->background_color;
}

void
webkit_web_view_set_cors_allowlist (WebKitWebView *web_view,
				    const gchar * const *allowlist)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));

	/* TODO WebView2: answer the custom-scheme requests with
	   Access-Control-Allow-Origin for these origins */
	g_strfreev (web_view->priv->cors_allowlist);
	web_view->priv->cors_allowlist = g_strdupv ((gchar **) allowlist);
}

/* ------------------------------------------------------------------------ */
/* Editing                                                                  */
/* ------------------------------------------------------------------------ */

void
webkit_web_view_set_editable (WebKitWebView *web_view,
			      gboolean editable)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));

	editable = editable ? TRUE : FALSE;
	if (web_view->priv->editable == editable)
		return;

	web_view->priv->editable = editable;
	/* the editor itself (e-editor.js) is ported in phase 4 */
	webview2_host_execute_script (web_view->priv->host,
		editable ? "document.designMode = \"on\";" : "document.designMode = \"off\";",
		NULL, NULL);

	g_object_notify_by_pspec (G_OBJECT (web_view), properties[PROP_EDITABLE]);
}

gboolean
webkit_web_view_is_editable (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), FALSE);

	return web_view->priv->editable;
}

void
webkit_web_view_execute_editing_command (WebKitWebView *web_view,
					 const gchar *command)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (command != NULL);

	gchar *literal, *script;

	literal = shim_js_string_literal (command);
	script = g_strdup_printf ("document.execCommand(%s);", literal);
	webview2_host_execute_script (web_view->priv->host, script, NULL, NULL);
	g_free (script);
	g_free (literal);
}

void
webkit_web_view_execute_editing_command_with_argument (WebKitWebView *web_view,
						       const gchar *command,
						       const gchar *argument)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (command != NULL);
	g_return_if_fail (argument != NULL);

	gchar *literal, *arg_literal, *script;

	literal = shim_js_string_literal (command);
	arg_literal = shim_js_string_literal (argument);
	script = g_strdup_printf ("document.execCommand(%s, false, %s);", literal, arg_literal);
	webview2_host_execute_script (web_view->priv->host, script, NULL, NULL);
	g_free (script);
	g_free (arg_literal);
	g_free (literal);
}

void
webkit_web_view_can_execute_editing_command (WebKitWebView *web_view,
					     const gchar *command,
					     GCancellable *cancellable,
					     GAsyncReadyCallback callback,
					     gpointer user_data)
{
	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (command != NULL);

	/* TODO WebView2: ExecuteScript("document.queryCommandEnabled(command)") */
	g_task_report_new_error (web_view, callback, user_data,
		webkit_web_view_can_execute_editing_command,
		G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		WEBKIT_SHIM_NOT_IMPLEMENTED_MESSAGE);
}

gboolean
webkit_web_view_can_execute_editing_command_finish (WebKitWebView *web_view,
						    GAsyncResult *result,
						    GError **error)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, web_view), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/* ------------------------------------------------------------------------ */
/* JavaScript                                                               */
/* ------------------------------------------------------------------------ */

static void
evaluate_javascript_done_cb (const gchar *json_result,
			     const gchar *error_message,
			     gpointer user_data)
{
	GTask *task = user_data;
	WebKitWebView *web_view = g_task_get_source_object (task);
	JsonNode *root;
	JsonObject *object;

	if (error_message) {
		g_task_return_new_error (task, WEBKIT_JAVASCRIPT_ERROR,
			WEBKIT_JAVASCRIPT_ERROR_SCRIPT_FAILED, "%s", error_message);
		g_object_unref (task);
		return;
	}

	root = json_from_string (json_result, NULL);
	object = root && JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;

	if (object && json_object_has_member (object, "e")) {
		SHIM_DEBUG ("script error: %s\n", json_object_get_string_member_with_default (object, "e", "?"));
		g_task_return_new_error (task, WEBKIT_JAVASCRIPT_ERROR,
			WEBKIT_JAVASCRIPT_ERROR_SCRIPT_FAILED, "%s",
			json_object_get_string_member_with_default (object, "e", "Script error"));
	} else {
		JsonNode *value = object ? json_object_get_member (object, "r") : NULL;

		/* undefined results are not serialized: null */
		g_task_return_pointer (task,
			_jsc_value_new_take_node (web_view->priv->jsc_context,
				value ? json_node_copy (value) : json_node_new (JSON_NODE_NULL)),
			g_object_unref);
	}

	if (root)
		json_node_unref (root);
	g_object_unref (task);
}

void
webkit_web_view_evaluate_javascript (WebKitWebView *web_view,
				     const gchar *script,
				     gssize length,
				     const gchar *world_name,
				     const gchar *source_uri,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer user_data)
{
	GTask *task = NULL;
	gchar *source, *literal, *wrapped;

	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (script != NULL);

	/* world_name: WebView2 has no isolated worlds, the script runs in the
	   page. It is evaluated by a global eval() so that exceptions come
	   back as {e: message} and the value as {r: value}. */
	source = length < 0 ? g_strdup (script) : g_strndup (script, length);
	SHIM_DEBUG ("eval %.160s\n", source);
	literal = shim_js_string_literal (source);
	wrapped = g_strdup_printf (
		"(function () { try { return { r: (0, eval)(%s) }; } "
		"catch (e) { return { e: String((e && e.message) || e) }; } })()", literal);

	if (callback) {
		task = g_task_new (web_view, cancellable, callback, user_data);
		g_task_set_source_tag (task, webkit_web_view_evaluate_javascript);
	}

	webview2_host_execute_script (web_view->priv->host, wrapped,
		task ? evaluate_javascript_done_cb : NULL, task);

	g_free (wrapped);
	g_free (literal);
	g_free (source);
}

JSCValue *
webkit_web_view_evaluate_javascript_finish (WebKitWebView *web_view,
					    GAsyncResult *result,
					    GError **error)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);
	g_return_val_if_fail (g_task_is_valid (result, web_view), NULL);

	return g_task_propagate_pointer (G_TASK (result), error);
}
