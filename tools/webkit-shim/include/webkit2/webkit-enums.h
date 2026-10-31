/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * Enumerations, flags, error domains and editing command names, with the
 * values of WebKitGTK 2.44 (webkit2gtk-4.1).
 */

#ifndef WEBKIT_SHIM_ENUMS_H
#define WEBKIT_SHIM_ENUMS_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	WEBKIT_LOAD_STARTED,
	WEBKIT_LOAD_REDIRECTED,
	WEBKIT_LOAD_COMMITTED,
	WEBKIT_LOAD_FINISHED
} WebKitLoadEvent;

typedef enum {
	WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION,
	WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION,
	WEBKIT_POLICY_DECISION_TYPE_RESPONSE
} WebKitPolicyDecisionType;

typedef enum {
	WEBKIT_NAVIGATION_TYPE_LINK_CLICKED,
	WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED,
	WEBKIT_NAVIGATION_TYPE_BACK_FORWARD,
	WEBKIT_NAVIGATION_TYPE_RELOAD,
	WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED,
	WEBKIT_NAVIGATION_TYPE_OTHER
} WebKitNavigationType;

typedef enum {
	WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT  = 1 << 1,
	WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK      = 1 << 2,
	WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE     = 1 << 3,
	WEBKIT_HIT_TEST_RESULT_CONTEXT_MEDIA     = 1 << 4,
	WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE  = 1 << 5,
	WEBKIT_HIT_TEST_RESULT_CONTEXT_SCROLLBAR = 1 << 6,
	WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION = 1 << 7
} WebKitHitTestResultContext;

typedef enum {
	WEBKIT_FIND_OPTIONS_NONE                               = 0,
	WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE                   = 1 << 0,
	WEBKIT_FIND_OPTIONS_AT_WORD_STARTS                     = 1 << 1,
	WEBKIT_FIND_OPTIONS_TREAT_MEDIAL_CAPITAL_AS_WORD_START = 1 << 2,
	WEBKIT_FIND_OPTIONS_BACKWARDS                          = 1 << 3,
	WEBKIT_FIND_OPTIONS_WRAP_AROUND                        = 1 << 4
} WebKitFindOptions;

typedef enum {
	WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER,
	WEBKIT_CACHE_MODEL_WEB_BROWSER,
	WEBKIT_CACHE_MODEL_DOCUMENT_BROWSER
} WebKitCacheModel;

typedef enum {
	WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND,
	WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS,
	WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
} WebKitHardwareAccelerationPolicy;

typedef enum {
	WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
	WEBKIT_USER_CONTENT_INJECT_TOP_FRAME
} WebKitUserContentInjectedFrames;

typedef enum {
	WEBKIT_USER_STYLE_LEVEL_USER,
	WEBKIT_USER_STYLE_LEVEL_AUTHOR
} WebKitUserStyleLevel;

typedef enum {
	WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
	WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END
} WebKitUserScriptInjectionTime;

typedef enum {
	WEBKIT_PRINT_OPERATION_RESPONSE_PRINT,
	WEBKIT_PRINT_OPERATION_RESPONSE_CANCEL
} WebKitPrintOperationResponse;

typedef enum {
	WEBKIT_WEB_PROCESS_CRASHED,
	WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT,
	WEBKIT_WEB_PROCESS_TERMINATED_BY_API
} WebKitWebProcessTerminationReason;

/* GTypes of the enumerations (used by signals and properties) */
#define WEBKIT_TYPE_LOAD_EVENT				(webkit_load_event_get_type ())
#define WEBKIT_TYPE_POLICY_DECISION_TYPE		(webkit_policy_decision_type_get_type ())
#define WEBKIT_TYPE_NAVIGATION_TYPE			(webkit_navigation_type_get_type ())
#define WEBKIT_TYPE_HIT_TEST_RESULT_CONTEXT		(webkit_hit_test_result_context_get_type ())
#define WEBKIT_TYPE_FIND_OPTIONS			(webkit_find_options_get_type ())
#define WEBKIT_TYPE_CACHE_MODEL				(webkit_cache_model_get_type ())
#define WEBKIT_TYPE_HARDWARE_ACCELERATION_POLICY	(webkit_hardware_acceleration_policy_get_type ())
#define WEBKIT_TYPE_USER_CONTENT_INJECTED_FRAMES	(webkit_user_content_injected_frames_get_type ())
#define WEBKIT_TYPE_USER_STYLE_LEVEL			(webkit_user_style_level_get_type ())
#define WEBKIT_TYPE_USER_SCRIPT_INJECTION_TIME		(webkit_user_script_injection_time_get_type ())
#define WEBKIT_TYPE_PRINT_OPERATION_RESPONSE		(webkit_print_operation_response_get_type ())
#define WEBKIT_TYPE_WEB_PROCESS_TERMINATION_REASON	(webkit_web_process_termination_reason_get_type ())

GType	webkit_load_event_get_type			(void) G_GNUC_CONST;
GType	webkit_policy_decision_type_get_type		(void) G_GNUC_CONST;
GType	webkit_navigation_type_get_type			(void) G_GNUC_CONST;
GType	webkit_hit_test_result_context_get_type		(void) G_GNUC_CONST;
GType	webkit_find_options_get_type			(void) G_GNUC_CONST;
GType	webkit_cache_model_get_type			(void) G_GNUC_CONST;
GType	webkit_hardware_acceleration_policy_get_type	(void) G_GNUC_CONST;
GType	webkit_user_content_injected_frames_get_type	(void) G_GNUC_CONST;
GType	webkit_user_style_level_get_type		(void) G_GNUC_CONST;
GType	webkit_user_script_injection_time_get_type	(void) G_GNUC_CONST;
GType	webkit_print_operation_response_get_type	(void) G_GNUC_CONST;
GType	webkit_web_process_termination_reason_get_type	(void) G_GNUC_CONST;

/* Error domains */
#define WEBKIT_NETWORK_ERROR	(webkit_network_error_quark ())
#define WEBKIT_POLICY_ERROR	(webkit_policy_error_quark ())
#define WEBKIT_JAVASCRIPT_ERROR	(webkit_javascript_error_quark ())

typedef enum {
	WEBKIT_NETWORK_ERROR_FAILED = 399,
	WEBKIT_NETWORK_ERROR_TRANSPORT = 300,
	WEBKIT_NETWORK_ERROR_UNKNOWN_PROTOCOL = 301,
	WEBKIT_NETWORK_ERROR_CANCELLED = 302,
	WEBKIT_NETWORK_ERROR_FILE_DOES_NOT_EXIST = 303
} WebKitNetworkError;

typedef enum {
	WEBKIT_POLICY_ERROR_FAILED = 199,
	WEBKIT_POLICY_ERROR_CANNOT_SHOW_MIME_TYPE = 100,
	WEBKIT_POLICY_ERROR_CANNOT_SHOW_URI = 101,
	WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE = 102,
	WEBKIT_POLICY_ERROR_CANNOT_USE_RESTRICTED_PORT = 103
} WebKitPolicyError;

typedef enum {
	WEBKIT_JAVASCRIPT_ERROR_SCRIPT_FAILED = 699,
	WEBKIT_JAVASCRIPT_ERROR_INVALID_PARAMETER = 600,
	WEBKIT_JAVASCRIPT_ERROR_INVALID_RESULT = 601
} WebKitJavascriptError;

GQuark	webkit_network_error_quark	(void);
GQuark	webkit_policy_error_quark	(void);
GQuark	webkit_javascript_error_quark	(void);

/* Editing commands (document.execCommand() names) */
#define WEBKIT_EDITING_COMMAND_CUT			"Cut"
#define WEBKIT_EDITING_COMMAND_COPY			"Copy"
#define WEBKIT_EDITING_COMMAND_PASTE			"Paste"
#define WEBKIT_EDITING_COMMAND_PASTE_AS_PLAIN_TEXT	"PasteAsPlainText"
#define WEBKIT_EDITING_COMMAND_SELECT_ALL		"SelectAll"
#define WEBKIT_EDITING_COMMAND_UNDO			"Undo"
#define WEBKIT_EDITING_COMMAND_REDO			"Redo"
#define WEBKIT_EDITING_COMMAND_INSERT_IMAGE		"InsertImage"
#define WEBKIT_EDITING_COMMAND_CREATE_LINK		"CreateLink"

G_END_DECLS

#endif /* WEBKIT_SHIM_ENUMS_H */
