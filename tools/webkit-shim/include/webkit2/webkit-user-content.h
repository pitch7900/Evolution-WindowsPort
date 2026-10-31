/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * User content: WebKitUserContentManager, WebKitUserStyleSheet,
 * WebKitUserScript and WebKitJavascriptResult (boxed).
 * WebView2 counterparts: AddScriptToExecuteOnDocumentCreated for scripts and
 * style sheets, window.chrome.webview.postMessage + WebMessageReceived for
 * the "script-message-received" signal.
 */

#ifndef WEBKIT_SHIM_USER_CONTENT_H
#define WEBKIT_SHIM_USER_CONTENT_H

#include <jsc/jsc.h>
#include <webkit2/webkit-forward.h>
#include <webkit2/webkit-enums.h>

G_BEGIN_DECLS

/* WebKitJavascriptResult (boxed) */

#define WEBKIT_TYPE_JAVASCRIPT_RESULT		(webkit_javascript_result_get_type ())

GType		webkit_javascript_result_get_type
						(void);
WebKitJavascriptResult *
		webkit_javascript_result_ref	(WebKitJavascriptResult *js_result);
void		webkit_javascript_result_unref	(WebKitJavascriptResult *js_result);
JSCValue *	webkit_javascript_result_get_js_value
						(WebKitJavascriptResult *js_result);

/* WebKitUserStyleSheet (boxed) */

#define WEBKIT_TYPE_USER_STYLE_SHEET		(webkit_user_style_sheet_get_type ())

GType		webkit_user_style_sheet_get_type
						(void);
WebKitUserStyleSheet *
		webkit_user_style_sheet_new	(const gchar *source,
						 WebKitUserContentInjectedFrames injected_frames,
						 WebKitUserStyleLevel level,
						 const gchar * const *allow_list,
						 const gchar * const *block_list);
WebKitUserStyleSheet *
		webkit_user_style_sheet_ref	(WebKitUserStyleSheet *user_style_sheet);
void		webkit_user_style_sheet_unref	(WebKitUserStyleSheet *user_style_sheet);

/* WebKitUserScript (boxed) */

#define WEBKIT_TYPE_USER_SCRIPT			(webkit_user_script_get_type ())

GType		webkit_user_script_get_type	(void);
WebKitUserScript *
		webkit_user_script_new		(const gchar *source,
						 WebKitUserContentInjectedFrames injected_frames,
						 WebKitUserScriptInjectionTime injection_time,
						 const gchar * const *allow_list,
						 const gchar * const *block_list);
WebKitUserScript *
		webkit_user_script_ref		(WebKitUserScript *user_script);
void		webkit_user_script_unref	(WebKitUserScript *user_script);

/* WebKitUserContentManager */

#define WEBKIT_TYPE_USER_CONTENT_MANAGER		(webkit_user_content_manager_get_type ())
#define WEBKIT_USER_CONTENT_MANAGER(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_USER_CONTENT_MANAGER, WebKitUserContentManager))
#define WEBKIT_USER_CONTENT_MANAGER_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_USER_CONTENT_MANAGER, WebKitUserContentManagerClass))
#define WEBKIT_IS_USER_CONTENT_MANAGER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_USER_CONTENT_MANAGER))
#define WEBKIT_IS_USER_CONTENT_MANAGER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_USER_CONTENT_MANAGER))
#define WEBKIT_USER_CONTENT_MANAGER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_USER_CONTENT_MANAGER, WebKitUserContentManagerClass))

GType		webkit_user_content_manager_get_type
						(void);
WebKitUserContentManager *
		webkit_user_content_manager_new	(void);
void		webkit_user_content_manager_add_style_sheet
						(WebKitUserContentManager *manager,
						 WebKitUserStyleSheet *stylesheet);
void		webkit_user_content_manager_remove_style_sheet
						(WebKitUserContentManager *manager,
						 WebKitUserStyleSheet *stylesheet);
void		webkit_user_content_manager_remove_all_style_sheets
						(WebKitUserContentManager *manager);
void		webkit_user_content_manager_add_script
						(WebKitUserContentManager *manager,
						 WebKitUserScript *script);
void		webkit_user_content_manager_remove_script
						(WebKitUserContentManager *manager,
						 WebKitUserScript *script);
void		webkit_user_content_manager_remove_all_scripts
						(WebKitUserContentManager *manager);
gboolean	webkit_user_content_manager_register_script_message_handler
						(WebKitUserContentManager *manager,
						 const gchar *name);
void		webkit_user_content_manager_unregister_script_message_handler
						(WebKitUserContentManager *manager,
						 const gchar *name);

G_END_DECLS

#endif /* WEBKIT_SHIM_USER_CONTENT_H */
