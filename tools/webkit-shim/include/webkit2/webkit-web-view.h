/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitWebView. In WebKitGTK it derives from WebKitWebViewBase, itself a
 * GtkContainer; here it derives directly from GtkContainer. The instance and
 * class structures are public because Evolution subclasses the view
 * (EWebView, EWebKitEditor).
 *
 * WebView2 counterpart: an ICoreWebView2Controller hosted in a child HWND of
 * the toplevel GdkWindow, sized to the widget allocation.
 */

#ifndef WEBKIT_SHIM_WEB_VIEW_H
#define WEBKIT_SHIM_WEB_VIEW_H

#include <gtk/gtk.h>
#include <jsc/jsc.h>
#include <webkit2/webkit-forward.h>
#include <webkit2/webkit-enums.h>
#include <webkit2/webkit-settings.h>
#include <webkit2/webkit-navigation.h>
#include <webkit2/webkit-user-content.h>
#include <webkit2/webkit-web-context.h>
#include <webkit2/webkit-misc.h>

G_BEGIN_DECLS

#define WEBKIT_TYPE_WEB_VIEW		(webkit_web_view_get_type ())
#define WEBKIT_WEB_VIEW(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_WEB_VIEW, WebKitWebView))
#define WEBKIT_WEB_VIEW_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_WEB_VIEW, WebKitWebViewClass))
#define WEBKIT_IS_WEB_VIEW(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_WEB_VIEW))
#define WEBKIT_IS_WEB_VIEW_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_WEB_VIEW))
#define WEBKIT_WEB_VIEW_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_WEB_VIEW, WebKitWebViewClass))

struct _WebKitWebView {
	GtkContainer parent;

	/*< private >*/
	WebKitWebViewPrivate *priv;
};

/* Class closures of the signals, in WebKitGTK order for those that exist in
 * the shim; the remaining WebKitGTK slots are folded into the padding. */
struct _WebKitWebViewClass {
	GtkContainerClass parent;

	void		(* load_changed)		(WebKitWebView *web_view,
							 WebKitLoadEvent load_event);
	gboolean	(* load_failed)			(WebKitWebView *web_view,
							 WebKitLoadEvent load_event,
							 const gchar *failing_uri,
							 GError *error);
	GtkWidget *	(* create)			(WebKitWebView *web_view,
							 WebKitNavigationAction *navigation_action);
	void		(* ready_to_show)		(WebKitWebView *web_view);
	void		(* run_as_modal)		(WebKitWebView *web_view);
	void		(* close)			(WebKitWebView *web_view);
	gboolean	(* decide_policy)		(WebKitWebView *web_view,
							 WebKitPolicyDecision *decision,
							 WebKitPolicyDecisionType type);
	void		(* mouse_target_changed)	(WebKitWebView *web_view,
							 WebKitHitTestResult *hit_test_result,
							 guint modifiers);
	gboolean	(* print)			(WebKitWebView *web_view,
							 WebKitPrintOperation *print_operation);
	gboolean	(* context_menu)		(WebKitWebView *web_view,
							 WebKitContextMenu *context_menu,
							 GdkEvent *event,
							 WebKitHitTestResult *hit_test_result);
	void		(* context_menu_dismissed)	(WebKitWebView *web_view);
	gboolean	(* authenticate)		(WebKitWebView *web_view,
							 WebKitAuthenticationRequest *request);
	gboolean	(* load_failed_with_tls_errors)	(WebKitWebView *web_view,
							 const gchar *failing_uri,
							 GTlsCertificate *certificate,
							 GTlsCertificateFlags errors);
	void		(* web_process_terminated)	(WebKitWebView *web_view,
							 WebKitWebProcessTerminationReason reason);

	/*< private >*/
	gpointer padding[24];
};

GType		webkit_web_view_get_type	(void);

GtkWidget *	webkit_web_view_new		(void);
GtkWidget *	webkit_web_view_new_with_context
						(WebKitWebContext *context);
GtkWidget *	webkit_web_view_new_with_settings
						(WebKitSettings *settings);
GtkWidget *	webkit_web_view_new_with_user_content_manager
						(WebKitUserContentManager *user_content_manager);
GtkWidget *	webkit_web_view_new_with_related_view
						(WebKitWebView *web_view);

WebKitWebContext *
		webkit_web_view_get_context	(WebKitWebView *web_view);
WebKitSettings *
		webkit_web_view_get_settings	(WebKitWebView *web_view);
void		webkit_web_view_set_settings	(WebKitWebView *web_view,
						 WebKitSettings *settings);
WebKitUserContentManager *
		webkit_web_view_get_user_content_manager
						(WebKitWebView *web_view);
WebKitFindController *
		webkit_web_view_get_find_controller
						(WebKitWebView *web_view);
WebKitWebInspector *
		webkit_web_view_get_inspector	(WebKitWebView *web_view);
WebKitEditorState *
		webkit_web_view_get_editor_state
						(WebKitWebView *web_view);
guint64		webkit_web_view_get_page_id	(WebKitWebView *web_view);

/* Loading */
void		webkit_web_view_load_uri	(WebKitWebView *web_view,
						 const gchar *uri);
void		webkit_web_view_load_html	(WebKitWebView *web_view,
						 const gchar *content,
						 const gchar *base_uri);
void		webkit_web_view_load_plain_text	(WebKitWebView *web_view,
						 const gchar *plain_text);
void		webkit_web_view_load_bytes	(WebKitWebView *web_view,
						 GBytes *bytes,
						 const gchar *mime_type,
						 const gchar *encoding,
						 const gchar *base_uri);
void		webkit_web_view_reload		(WebKitWebView *web_view);
void		webkit_web_view_stop_loading	(WebKitWebView *web_view);
gboolean	webkit_web_view_is_loading	(WebKitWebView *web_view);
const gchar *	webkit_web_view_get_uri		(WebKitWebView *web_view);
const gchar *	webkit_web_view_get_title	(WebKitWebView *web_view);
gdouble		webkit_web_view_get_estimated_load_progress
						(WebKitWebView *web_view);
gboolean	webkit_web_view_can_show_mime_type
						(WebKitWebView *web_view,
						 const gchar *mime_type);

/* Appearance */
void		webkit_web_view_set_zoom_level	(WebKitWebView *web_view,
						 gdouble zoom_level);
gdouble		webkit_web_view_get_zoom_level	(WebKitWebView *web_view);
void		webkit_web_view_set_background_color
						(WebKitWebView *web_view,
						 const GdkRGBA *rgba);
void		webkit_web_view_get_background_color
						(WebKitWebView *web_view,
						 GdkRGBA *rgba);
void		webkit_web_view_set_cors_allowlist
						(WebKitWebView *web_view,
						 const gchar * const *allowlist);

/* Editing */
void		webkit_web_view_set_editable	(WebKitWebView *web_view,
						 gboolean editable);
gboolean	webkit_web_view_is_editable	(WebKitWebView *web_view);
void		webkit_web_view_execute_editing_command
						(WebKitWebView *web_view,
						 const gchar *command);
void		webkit_web_view_execute_editing_command_with_argument
						(WebKitWebView *web_view,
						 const gchar *command,
						 const gchar *argument);
void		webkit_web_view_can_execute_editing_command
						(WebKitWebView *web_view,
						 const gchar *command,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	webkit_web_view_can_execute_editing_command_finish
						(WebKitWebView *web_view,
						 GAsyncResult *result,
						 GError **error);

/* JavaScript */
void		webkit_web_view_evaluate_javascript
						(WebKitWebView *web_view,
						 const gchar *script,
						 gssize length,
						 const gchar *world_name,
						 const gchar *source_uri,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
JSCValue *	webkit_web_view_evaluate_javascript_finish
						(WebKitWebView *web_view,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* WEBKIT_SHIM_WEB_VIEW_H */
