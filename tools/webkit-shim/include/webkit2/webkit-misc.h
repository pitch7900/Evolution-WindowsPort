/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * Smaller helper objects of WebKitWebView: WebKitHitTestResult,
 * WebKitFindController, WebKitPrintOperation, WebKitWebInspector,
 * WebKitContextMenu, WebKitEditorState and WebKitAuthenticationRequest.
 */

#ifndef WEBKIT_SHIM_MISC_H
#define WEBKIT_SHIM_MISC_H

#include <gtk/gtk.h>
#include <webkit2/webkit-forward.h>
#include <webkit2/webkit-enums.h>

G_BEGIN_DECLS

/* WebKitHitTestResult */

#define WEBKIT_TYPE_HIT_TEST_RESULT		(webkit_hit_test_result_get_type ())
#define WEBKIT_HIT_TEST_RESULT(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_HIT_TEST_RESULT, WebKitHitTestResult))
#define WEBKIT_HIT_TEST_RESULT_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_HIT_TEST_RESULT, WebKitHitTestResultClass))
#define WEBKIT_IS_HIT_TEST_RESULT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_HIT_TEST_RESULT))
#define WEBKIT_IS_HIT_TEST_RESULT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_HIT_TEST_RESULT))
#define WEBKIT_HIT_TEST_RESULT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_HIT_TEST_RESULT, WebKitHitTestResultClass))

GType		webkit_hit_test_result_get_type	(void);
guint		webkit_hit_test_result_get_context
						(WebKitHitTestResult *hit_test_result);
gboolean	webkit_hit_test_result_context_is_link
						(WebKitHitTestResult *hit_test_result);
gboolean	webkit_hit_test_result_context_is_image
						(WebKitHitTestResult *hit_test_result);
gboolean	webkit_hit_test_result_context_is_media
						(WebKitHitTestResult *hit_test_result);
gboolean	webkit_hit_test_result_context_is_editable
						(WebKitHitTestResult *hit_test_result);
gboolean	webkit_hit_test_result_context_is_selection
						(WebKitHitTestResult *hit_test_result);
gboolean	webkit_hit_test_result_context_is_scrollbar
						(WebKitHitTestResult *hit_test_result);
const gchar *	webkit_hit_test_result_get_link_uri
						(WebKitHitTestResult *hit_test_result);
const gchar *	webkit_hit_test_result_get_link_title
						(WebKitHitTestResult *hit_test_result);
const gchar *	webkit_hit_test_result_get_link_label
						(WebKitHitTestResult *hit_test_result);
const gchar *	webkit_hit_test_result_get_image_uri
						(WebKitHitTestResult *hit_test_result);
const gchar *	webkit_hit_test_result_get_media_uri
						(WebKitHitTestResult *hit_test_result);

/* WebKitFindController */

#define WEBKIT_TYPE_FIND_CONTROLLER		(webkit_find_controller_get_type ())
#define WEBKIT_FIND_CONTROLLER(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_FIND_CONTROLLER, WebKitFindController))
#define WEBKIT_FIND_CONTROLLER_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_FIND_CONTROLLER, WebKitFindControllerClass))
#define WEBKIT_IS_FIND_CONTROLLER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_FIND_CONTROLLER))
#define WEBKIT_IS_FIND_CONTROLLER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_FIND_CONTROLLER))
#define WEBKIT_FIND_CONTROLLER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_FIND_CONTROLLER, WebKitFindControllerClass))

GType		webkit_find_controller_get_type	(void);
void		webkit_find_controller_search	(WebKitFindController *find_controller,
						 const gchar *search_text,
						 guint32 find_options,
						 guint max_match_count);
void		webkit_find_controller_search_finish
						(WebKitFindController *find_controller);
void		webkit_find_controller_search_next
						(WebKitFindController *find_controller);
void		webkit_find_controller_search_previous
						(WebKitFindController *find_controller);
void		webkit_find_controller_count_matches
						(WebKitFindController *find_controller,
						 const gchar *search_text,
						 guint32 find_options,
						 guint max_match_count);
const gchar *	webkit_find_controller_get_search_text
						(WebKitFindController *find_controller);
guint32		webkit_find_controller_get_options
						(WebKitFindController *find_controller);
guint		webkit_find_controller_get_max_match_count
						(WebKitFindController *find_controller);
WebKitWebView *	webkit_find_controller_get_web_view
						(WebKitFindController *find_controller);

/* WebKitPrintOperation */

#define WEBKIT_TYPE_PRINT_OPERATION		(webkit_print_operation_get_type ())
#define WEBKIT_PRINT_OPERATION(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_PRINT_OPERATION, WebKitPrintOperation))
#define WEBKIT_PRINT_OPERATION_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_PRINT_OPERATION, WebKitPrintOperationClass))
#define WEBKIT_IS_PRINT_OPERATION(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_PRINT_OPERATION))
#define WEBKIT_IS_PRINT_OPERATION_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_PRINT_OPERATION))
#define WEBKIT_PRINT_OPERATION_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_PRINT_OPERATION, WebKitPrintOperationClass))

GType		webkit_print_operation_get_type	(void);
WebKitPrintOperation *
		webkit_print_operation_new	(WebKitWebView *web_view);
GtkPrintSettings *
		webkit_print_operation_get_print_settings
						(WebKitPrintOperation *print_operation);
void		webkit_print_operation_set_print_settings
						(WebKitPrintOperation *print_operation,
						 GtkPrintSettings *print_settings);
GtkPageSetup *	webkit_print_operation_get_page_setup
						(WebKitPrintOperation *print_operation);
void		webkit_print_operation_set_page_setup
						(WebKitPrintOperation *print_operation,
						 GtkPageSetup *page_setup);
WebKitPrintOperationResponse
		webkit_print_operation_run_dialog
						(WebKitPrintOperation *print_operation,
						 GtkWindow *parent);
void		webkit_print_operation_print	(WebKitPrintOperation *print_operation);

/* WebKitWebInspector */

#define WEBKIT_TYPE_WEB_INSPECTOR		(webkit_web_inspector_get_type ())
#define WEBKIT_WEB_INSPECTOR(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_WEB_INSPECTOR, WebKitWebInspector))
#define WEBKIT_WEB_INSPECTOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_WEB_INSPECTOR, WebKitWebInspectorClass))
#define WEBKIT_IS_WEB_INSPECTOR(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_WEB_INSPECTOR))
#define WEBKIT_IS_WEB_INSPECTOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_WEB_INSPECTOR))
#define WEBKIT_WEB_INSPECTOR_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_WEB_INSPECTOR, WebKitWebInspectorClass))

GType		webkit_web_inspector_get_type	(void);
void		webkit_web_inspector_show	(WebKitWebInspector *inspector);
void		webkit_web_inspector_close	(WebKitWebInspector *inspector);

/* WebKitContextMenu (items are not supported by the shim) */

#define WEBKIT_TYPE_CONTEXT_MENU		(webkit_context_menu_get_type ())
#define WEBKIT_CONTEXT_MENU(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_CONTEXT_MENU, WebKitContextMenu))
#define WEBKIT_CONTEXT_MENU_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_CONTEXT_MENU, WebKitContextMenuClass))
#define WEBKIT_IS_CONTEXT_MENU(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_CONTEXT_MENU))
#define WEBKIT_IS_CONTEXT_MENU_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_CONTEXT_MENU))
#define WEBKIT_CONTEXT_MENU_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_CONTEXT_MENU, WebKitContextMenuClass))

GType		webkit_context_menu_get_type	(void);
WebKitContextMenu *
		webkit_context_menu_new		(void);
guint		webkit_context_menu_get_n_items	(WebKitContextMenu *menu);
void		webkit_context_menu_remove_all	(WebKitContextMenu *menu);

/* WebKitEditorState */

#define WEBKIT_TYPE_EDITOR_STATE		(webkit_editor_state_get_type ())
#define WEBKIT_EDITOR_STATE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_EDITOR_STATE, WebKitEditorState))
#define WEBKIT_EDITOR_STATE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_EDITOR_STATE, WebKitEditorStateClass))
#define WEBKIT_IS_EDITOR_STATE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_EDITOR_STATE))
#define WEBKIT_IS_EDITOR_STATE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_EDITOR_STATE))
#define WEBKIT_EDITOR_STATE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_EDITOR_STATE, WebKitEditorStateClass))

typedef enum {
	WEBKIT_EDITOR_TYPING_ATTRIBUTE_NONE          = 1 << 1,
	WEBKIT_EDITOR_TYPING_ATTRIBUTE_BOLD          = 1 << 2,
	WEBKIT_EDITOR_TYPING_ATTRIBUTE_ITALIC        = 1 << 3,
	WEBKIT_EDITOR_TYPING_ATTRIBUTE_UNDERLINE     = 1 << 4,
	WEBKIT_EDITOR_TYPING_ATTRIBUTE_STRIKETHROUGH = 1 << 5
} WebKitEditorTypingAttributes;

GType		webkit_editor_state_get_type	(void);
guint		webkit_editor_state_get_typing_attributes
						(WebKitEditorState *editor_state);
gboolean	webkit_editor_state_is_cut_available
						(WebKitEditorState *editor_state);
gboolean	webkit_editor_state_is_copy_available
						(WebKitEditorState *editor_state);
gboolean	webkit_editor_state_is_paste_available
						(WebKitEditorState *editor_state);
gboolean	webkit_editor_state_is_undo_available
						(WebKitEditorState *editor_state);
gboolean	webkit_editor_state_is_redo_available
						(WebKitEditorState *editor_state);

/* WebKitAuthenticationRequest (parameter of WebKitWebView::authenticate) */

#define WEBKIT_TYPE_AUTHENTICATION_REQUEST		(webkit_authentication_request_get_type ())
#define WEBKIT_AUTHENTICATION_REQUEST(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_AUTHENTICATION_REQUEST, WebKitAuthenticationRequest))
#define WEBKIT_AUTHENTICATION_REQUEST_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_AUTHENTICATION_REQUEST, WebKitAuthenticationRequestClass))
#define WEBKIT_IS_AUTHENTICATION_REQUEST(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_AUTHENTICATION_REQUEST))
#define WEBKIT_IS_AUTHENTICATION_REQUEST_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_AUTHENTICATION_REQUEST))
#define WEBKIT_AUTHENTICATION_REQUEST_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_AUTHENTICATION_REQUEST, WebKitAuthenticationRequestClass))

GType		webkit_authentication_request_get_type
						(void);
const gchar *	webkit_authentication_request_get_host
						(WebKitAuthenticationRequest *request);
void		webkit_authentication_request_cancel
						(WebKitAuthenticationRequest *request);

G_END_DECLS

#endif /* WEBKIT_SHIM_MISC_H */
