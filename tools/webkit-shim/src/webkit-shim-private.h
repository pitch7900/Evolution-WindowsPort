/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * Internal constructors and hooks, not part of the WebKitGTK API. The WebView2
 * backend uses them to turn WebView2 events into WebKitGTK objects and
 * signals (hit test results, policy decisions, URI scheme requests, script
 * messages, ...).
 */

#ifndef WEBKIT_SHIM_PRIVATE_H
#define WEBKIT_SHIM_PRIVATE_H

#include <json-glib/json-glib.h>
#include <webkit2/webkit2.h>

G_BEGIN_DECLS

/* Message used by every not-yet-implemented asynchronous operation */
#define WEBKIT_SHIM_NOT_IMPLEMENTED_MESSAGE "WebView2 backend not implemented yet"

/* JSC */
JSCValue *	_jsc_value_new_take_node	(JSCContext *context,
						 JsonNode *node);
JsonNode *	_jsc_value_get_node		(JSCValue *value);

/* WebKitJavascriptResult */
WebKitJavascriptResult *
		_webkit_javascript_result_new	(JSCValue *value);

/* WebKitURIRequest / WebKitNavigationAction / policy decisions */
WebKitNavigationAction *
		_webkit_navigation_action_new	(WebKitURIRequest *request,
						 WebKitNavigationType navigation_type,
						 guint mouse_button,
						 guint modifiers,
						 gboolean is_user_gesture);
WebKitPolicyDecision *
		_webkit_navigation_policy_decision_new
						(WebKitNavigationAction *navigation_action,
						 const gchar *frame_name);

/* Decision taken on a WebKitPolicyDecision */
typedef enum {
	WEBKIT_SHIM_POLICY_UNDECIDED,
	WEBKIT_SHIM_POLICY_USE,
	WEBKIT_SHIM_POLICY_IGNORE,
	WEBKIT_SHIM_POLICY_DOWNLOAD
} WebKitShimPolicyResult;

WebKitShimPolicyResult
		_webkit_policy_decision_get_result
						(WebKitPolicyDecision *decision);

/* WebKitHitTestResult */
WebKitHitTestResult *
		_webkit_hit_test_result_new	(guint context,
						 const gchar *link_uri,
						 const gchar *link_title,
						 const gchar *link_label,
						 const gchar *image_uri,
						 const gchar *media_uri);

/* WebKitURISchemeRequest; finish/finish_error hand the response over to
 * the callback, which the backend uses to complete WebResourceRequested */
typedef void (* WebKitShimURISchemeResponseFunc)
						(WebKitURISchemeRequest *request,
						 GInputStream *stream,
						 gint64 stream_length,
						 const gchar *content_type,
						 const GError *error,
						 gpointer user_data);

WebKitURISchemeRequest *
		_webkit_uri_scheme_request_new	(WebKitWebView *web_view,
						 const gchar *uri,
						 const gchar *http_method,
						 WebKitShimURISchemeResponseFunc response_func,
						 gpointer response_data);

/* Dispatches a request to the handler registered with
 * webkit_web_context_register_uri_scheme(); FALSE when none is registered */
gboolean	_webkit_web_context_handle_uri_scheme_request
						(WebKitWebContext *context,
						 WebKitURISchemeRequest *request);
void		_webkit_web_context_ensure_web_extensions_initialized
						(WebKitWebContext *context);
gboolean	_webkit_web_context_is_uri_scheme_registered
						(WebKitWebContext *context,
						 const gchar *scheme);
gchar **	_webkit_web_context_dup_uri_schemes
						(WebKitWebContext *context);

/* WebKitUserContentManager: emits "script-message-received::<name>" if
 * <name> was registered; returns whether it was */
gboolean	_webkit_user_content_manager_dispatch_script_message
						(WebKitUserContentManager *manager,
						 const gchar *name,
						 JSCValue *value);
/* script injected in every document and frame of the views using the
 * manager, and the script re-applying the style sheets to a loaded page */
gchar *		_webkit_user_content_manager_build_page_script
						(WebKitUserContentManager *manager);
gchar *		_webkit_user_content_manager_build_style_update
						(WebKitUserContentManager *manager);
GPtrArray *	_webkit_user_content_manager_get_style_sheets
						(WebKitUserContentManager *manager);
GPtrArray *	_webkit_user_content_manager_get_scripts
						(WebKitUserContentManager *manager);
const gchar *	_webkit_user_style_sheet_get_source
						(WebKitUserStyleSheet *user_style_sheet);
const gchar *	_webkit_user_script_get_source	(WebKitUserScript *user_script);

/* WebKitFindController / WebKitWebInspector / WebKitEditorState are owned
 * by the view */
WebKitFindController *
		_webkit_find_controller_new	(WebKitWebView *web_view);
WebKitWebInspector *
		_webkit_web_inspector_new	(WebKitWebView *web_view);
WebKitEditorState *
		_webkit_editor_state_new	(void);
void		_webkit_editor_state_update	(WebKitEditorState *editor_state,
						 guint typing_attributes,
						 gboolean cut_available,
						 gboolean copy_available,
						 gboolean paste_available,
						 gboolean undo_available,
						 gboolean redo_available);

G_END_DECLS

#endif /* WEBKIT_SHIM_PRIVATE_H */
