/*
 * WebKitGTK compatibility shim for Windows: WebView2 host.
 *
 * Thin C API over Microsoft Edge WebView2 (COM, implemented in
 * webview2-host.cpp), used by the GObject side of the shim. One host per
 * WebKitWebView. All calls and callbacks happen on the GTK main thread:
 * WebView2 delivers its events through the thread's message queue, which
 * GLib's main loop dispatches on Windows.
 *
 * Creation is asynchronous (shared environment, then the controller);
 * commands issued before the controller is ready are queued and replayed.
 */

#ifndef WEBVIEW2_HOST_H
#define WEBVIEW2_HOST_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _WebView2Host WebView2Host;
typedef struct _WebView2Request WebView2Request;

typedef enum {
	WEBVIEW2_TARGET_PAGE = 0,
	WEBVIEW2_TARGET_LINK = 1 << 0,
	WEBVIEW2_TARGET_IMAGE = 1 << 1,
	WEBVIEW2_TARGET_EDITABLE = 1 << 2,
	WEBVIEW2_TARGET_SELECTION = 1 << 3
} WebView2TargetFlags;

/* Events, all optional; user_data is the one given to webview2_host_new() */
typedef struct {
	/* set *cancel to TRUE to cancel the navigation */
	void		(* navigation_starting)	(gpointer user_data,
						 const gchar *uri,
						 gboolean is_user_initiated,
						 gboolean *cancel);
	void		(* content_loading)	(gpointer user_data);
	void		(* navigation_completed)
						(gpointer user_data,
						 gboolean success,
						 gint web_error_status);
	void		(* title_changed)	(gpointer user_data,
						 const gchar *title);
	/* window.chrome.webview.postMessage() payload, as a string */
	void		(* web_message)		(gpointer user_data,
						 const gchar *message);
	/* return TRUE when handled (no WebView2 popup window is opened) */
	gboolean	(* new_window_requested)
						(gpointer user_data,
						 const gchar *uri,
						 gboolean is_user_initiated);
	/* the request must be answered with webview2_request_respond() or
	 * webview2_request_fail(), possibly later */
	void		(* resource_requested)	(gpointer user_data,
						 WebView2Request *request,
						 const gchar *uri,
						 const gchar *method);
	/* coordinates in pixels of the host window client area;
	 * return TRUE when handled (no default WebView2 menu) */
	gboolean	(* context_menu)	(gpointer user_data,
						 gint x,
						 gint y,
						 guint target_flags,
						 const gchar *link_uri,
						 const gchar *image_uri);
	/* status bar text: the hovered link, or empty */
	void		(* status_text_changed)	(gpointer user_data,
						 const gchar *text);
	void		(* process_failed)	(gpointer user_data,
						 gint kind);
	/* the WebView2 child window took or lost the keyboard focus */
	void		(* focus_changed)	(gpointer user_data,
						 gboolean has_focus);
} WebView2HostCallbacks;

/* Script result: exactly one of json_result (JSON text of the value) and
 * error_message is set */
typedef void	(* WebView2ScriptCallback)	(const gchar *json_result,
						 const gchar *error_message,
						 gpointer user_data);

/* Environment: call before the first webview2_host_attach(); the custom
 * URI schemes cannot be changed afterwards (WebView2 limitation). */
void		webview2_environment_configure	(const gchar *user_data_folder,
						 const gchar * const *custom_schemes);

WebView2Host *	webview2_host_new		(const WebView2HostCallbacks *callbacks,
						 gpointer user_data);
void		webview2_host_free		(WebView2Host *host);

/* parent_hwnd: the HWND of the native GdkWindow of the widget */
void		webview2_host_attach		(WebView2Host *host,
						 gpointer parent_hwnd);
gboolean	webview2_host_is_ready		(WebView2Host *host);
/* resizes the WebView2 to the client area of the parent window */
void		webview2_host_update_bounds	(WebView2Host *host);
void		webview2_host_set_visible	(WebView2Host *host,
						 gboolean visible);
void		webview2_host_focus		(WebView2Host *host);

void		webview2_host_navigate		(WebView2Host *host,
						 const gchar *uri);
void		webview2_host_reload		(WebView2Host *host);
void		webview2_host_stop		(WebView2Host *host);
void		webview2_host_set_zoom		(WebView2Host *host,
						 gdouble zoom_factor);
void		webview2_host_set_background	(WebView2Host *host,
						 guint8 red,
						 guint8 green,
						 guint8 blue,
						 guint8 alpha);
void		webview2_host_set_dev_tools_enabled
						(WebView2Host *host,
						 gboolean enabled);
void		webview2_host_open_dev_tools	(WebView2Host *host);

/* script runs in the page; for each document created from now on */
void		webview2_host_add_document_script
						(WebView2Host *host,
						 const gchar *script);
/* like add_document_script(), but replaces the previously set page script */
void		webview2_host_set_page_script	(WebView2Host *host,
						 const gchar *script);
void		webview2_host_execute_script	(WebView2Host *host,
						 const gchar *script,
						 WebView2ScriptCallback callback,
						 gpointer user_data);

/* Requests */
void		webview2_request_respond	(WebView2Request *request,
						 gint status_code,
						 const gchar *reason,
						 const gchar *content_type,
						 const guint8 *data,
						 gsize length);
void		webview2_request_fail		(WebView2Request *request,
						 gint status_code,
						 const gchar *reason);
/* lets WebView2 load the resource itself (network) */
void		webview2_request_continue	(WebView2Request *request);

G_END_DECLS

#endif /* WEBVIEW2_HOST_H */
