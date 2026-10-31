/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * Enumeration GTypes, error domains and version information.
 */

#include "webkit-shim-private.h"

guint
webkit_get_major_version (void)
{
	return WEBKIT_MAJOR_VERSION;
}

guint
webkit_get_minor_version (void)
{
	return WEBKIT_MINOR_VERSION;
}

guint
webkit_get_micro_version (void)
{
	return WEBKIT_MICRO_VERSION;
}

G_DEFINE_QUARK (WebKitNetworkError, webkit_network_error)
G_DEFINE_QUARK (WebKitPolicyError, webkit_policy_error)
G_DEFINE_QUARK (WebKitJavascriptError, webkit_javascript_error)

/* Registers an enum (or flags) GType once, from a static value table */
#define DEFINE_ENUM_TYPE(func, TypeName, register_func, ValueType, ...) \
GType \
func (void) \
{ \
	static gsize type_id = 0; \
	if (g_once_init_enter (&type_id)) { \
		static const ValueType values[] = { __VA_ARGS__, { 0, NULL, NULL } }; \
		GType id = register_func (g_intern_static_string (TypeName), values); \
		g_once_init_leave (&type_id, id); \
	} \
	return type_id; \
}

#define V(value, nick) { value, #value, nick }

DEFINE_ENUM_TYPE (webkit_load_event_get_type, "WebKitLoadEvent",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_LOAD_STARTED, "started"),
	V (WEBKIT_LOAD_REDIRECTED, "redirected"),
	V (WEBKIT_LOAD_COMMITTED, "committed"),
	V (WEBKIT_LOAD_FINISHED, "finished"))

DEFINE_ENUM_TYPE (webkit_policy_decision_type_get_type, "WebKitPolicyDecisionType",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION, "navigation-action"),
	V (WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION, "new-window-action"),
	V (WEBKIT_POLICY_DECISION_TYPE_RESPONSE, "response"))

DEFINE_ENUM_TYPE (webkit_navigation_type_get_type, "WebKitNavigationType",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_NAVIGATION_TYPE_LINK_CLICKED, "link-clicked"),
	V (WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED, "form-submitted"),
	V (WEBKIT_NAVIGATION_TYPE_BACK_FORWARD, "back-forward"),
	V (WEBKIT_NAVIGATION_TYPE_RELOAD, "reload"),
	V (WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED, "form-resubmitted"),
	V (WEBKIT_NAVIGATION_TYPE_OTHER, "other"))

DEFINE_ENUM_TYPE (webkit_hit_test_result_context_get_type, "WebKitHitTestResultContext",
	g_flags_register_static, GFlagsValue,
	V (WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT, "document"),
	V (WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK, "link"),
	V (WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE, "image"),
	V (WEBKIT_HIT_TEST_RESULT_CONTEXT_MEDIA, "media"),
	V (WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE, "editable"),
	V (WEBKIT_HIT_TEST_RESULT_CONTEXT_SCROLLBAR, "scrollbar"),
	V (WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION, "selection"))

DEFINE_ENUM_TYPE (webkit_find_options_get_type, "WebKitFindOptions",
	g_flags_register_static, GFlagsValue,
	V (WEBKIT_FIND_OPTIONS_NONE, "none"),
	V (WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE, "case-insensitive"),
	V (WEBKIT_FIND_OPTIONS_AT_WORD_STARTS, "at-word-starts"),
	V (WEBKIT_FIND_OPTIONS_TREAT_MEDIAL_CAPITAL_AS_WORD_START, "treat-medial-capital-as-word-start"),
	V (WEBKIT_FIND_OPTIONS_BACKWARDS, "backwards"),
	V (WEBKIT_FIND_OPTIONS_WRAP_AROUND, "wrap-around"))

DEFINE_ENUM_TYPE (webkit_cache_model_get_type, "WebKitCacheModel",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER, "document-viewer"),
	V (WEBKIT_CACHE_MODEL_WEB_BROWSER, "web-browser"),
	V (WEBKIT_CACHE_MODEL_DOCUMENT_BROWSER, "document-browser"))

DEFINE_ENUM_TYPE (webkit_hardware_acceleration_policy_get_type, "WebKitHardwareAccelerationPolicy",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND, "on-demand"),
	V (WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS, "always"),
	V (WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER, "never"))

DEFINE_ENUM_TYPE (webkit_user_content_injected_frames_get_type, "WebKitUserContentInjectedFrames",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, "all-frames"),
	V (WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, "top-frame"))

DEFINE_ENUM_TYPE (webkit_user_style_level_get_type, "WebKitUserStyleLevel",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_USER_STYLE_LEVEL_USER, "user"),
	V (WEBKIT_USER_STYLE_LEVEL_AUTHOR, "author"))

DEFINE_ENUM_TYPE (webkit_user_script_injection_time_get_type, "WebKitUserScriptInjectionTime",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, "start"),
	V (WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, "end"))

DEFINE_ENUM_TYPE (webkit_print_operation_response_get_type, "WebKitPrintOperationResponse",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_PRINT_OPERATION_RESPONSE_PRINT, "print"),
	V (WEBKIT_PRINT_OPERATION_RESPONSE_CANCEL, "cancel"))

DEFINE_ENUM_TYPE (webkit_web_process_termination_reason_get_type, "WebKitWebProcessTerminationReason",
	g_enum_register_static, GEnumValue,
	V (WEBKIT_WEB_PROCESS_CRASHED, "crashed"),
	V (WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT, "exceeded-memory-limit"),
	V (WEBKIT_WEB_PROCESS_TERMINATED_BY_API, "terminated-by-api"))
