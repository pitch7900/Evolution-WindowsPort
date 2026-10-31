/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitUserContentManager, WebKitUserStyleSheet, WebKitUserScript and
 * WebKitJavascriptResult.
 *
 * The manager keeps the style sheets, scripts and registered message handler
 * names, and builds from them one "page script"
 * (_webkit_user_content_manager_build_page_script()) that each view injects
 * in every document and frame (AddScriptToExecuteOnDocumentCreated): it
 * defines window.webkit.messageHandlers.<name>.postMessage() on top of
 * window.chrome.webview.postMessage(), adds the style sheets in a <style>
 * element and runs the user scripts. Views re-inject it on the
 * "webkit-shim-changed" signal; WebMessageReceived ends in
 * _webkit_user_content_manager_dispatch_script_message().
 */

#include "webkit-shim-private.h"

/* ------------------------------------------------------------------------ */
/* WebKitJavascriptResult                                                   */
/* ------------------------------------------------------------------------ */

struct _WebKitJavascriptResult {
	gint ref_count;
	JSCValue *value;
};

G_DEFINE_BOXED_TYPE (WebKitJavascriptResult, webkit_javascript_result,
	webkit_javascript_result_ref, webkit_javascript_result_unref)

WebKitJavascriptResult *
_webkit_javascript_result_new (JSCValue *value)
{
	WebKitJavascriptResult *js_result;

	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);

	js_result = g_slice_new0 (WebKitJavascriptResult);
	js_result->ref_count = 1;
	js_result->value = g_object_ref (value);

	return js_result;
}

WebKitJavascriptResult *
webkit_javascript_result_ref (WebKitJavascriptResult *js_result)
{
	g_return_val_if_fail (js_result != NULL, NULL);

	g_atomic_int_inc (&js_result->ref_count);

	return js_result;
}

void
webkit_javascript_result_unref (WebKitJavascriptResult *js_result)
{
	g_return_if_fail (js_result != NULL);

	if (g_atomic_int_dec_and_test (&js_result->ref_count)) {
		g_clear_object (&js_result->value);
		g_slice_free (WebKitJavascriptResult, js_result);
	}
}

JSCValue *
webkit_javascript_result_get_js_value (WebKitJavascriptResult *js_result)
{
	g_return_val_if_fail (js_result != NULL, NULL);

	return js_result->value;
}

/* ------------------------------------------------------------------------ */
/* WebKitUserStyleSheet / WebKitUserScript                                  */
/* ------------------------------------------------------------------------ */

/* Both boxed types share this layout */
typedef struct {
	gint ref_count;
	gchar *source;
	WebKitUserContentInjectedFrames injected_frames;
	gint level_or_time; /* WebKitUserStyleLevel or WebKitUserScriptInjectionTime */
	gchar **allow_list;
	gchar **block_list;
} UserContent;

static UserContent *
user_content_new (const gchar *source,
		  WebKitUserContentInjectedFrames injected_frames,
		  gint level_or_time,
		  const gchar * const *allow_list,
		  const gchar * const *block_list)
{
	UserContent *content;

	content = g_slice_new0 (UserContent);
	content->ref_count = 1;
	content->source = g_strdup (source);
	content->injected_frames = injected_frames;
	content->level_or_time = level_or_time;
	content->allow_list = g_strdupv ((gchar **) allow_list);
	content->block_list = g_strdupv ((gchar **) block_list);

	return content;
}

static gpointer
user_content_ref (gpointer ptr)
{
	UserContent *content = ptr;

	g_return_val_if_fail (content != NULL, NULL);

	g_atomic_int_inc (&content->ref_count);

	return content;
}

static void
user_content_unref (gpointer ptr)
{
	UserContent *content = ptr;

	g_return_if_fail (content != NULL);

	if (g_atomic_int_dec_and_test (&content->ref_count)) {
		g_free (content->source);
		g_strfreev (content->allow_list);
		g_strfreev (content->block_list);
		g_slice_free (UserContent, content);
	}
}

struct _WebKitUserStyleSheet {
	UserContent content;
};

struct _WebKitUserScript {
	UserContent content;
};

G_DEFINE_BOXED_TYPE (WebKitUserStyleSheet, webkit_user_style_sheet,
	webkit_user_style_sheet_ref, webkit_user_style_sheet_unref)

G_DEFINE_BOXED_TYPE (WebKitUserScript, webkit_user_script,
	webkit_user_script_ref, webkit_user_script_unref)

WebKitUserStyleSheet *
webkit_user_style_sheet_new (const gchar *source,
			     WebKitUserContentInjectedFrames injected_frames,
			     WebKitUserStyleLevel level,
			     const gchar * const *allow_list,
			     const gchar * const *block_list)
{
	g_return_val_if_fail (source != NULL, NULL);

	return (WebKitUserStyleSheet *) user_content_new (source, injected_frames, level, allow_list, block_list);
}

WebKitUserStyleSheet *
webkit_user_style_sheet_ref (WebKitUserStyleSheet *user_style_sheet)
{
	return user_content_ref (user_style_sheet);
}

void
webkit_user_style_sheet_unref (WebKitUserStyleSheet *user_style_sheet)
{
	user_content_unref (user_style_sheet);
}

const gchar *
_webkit_user_style_sheet_get_source (WebKitUserStyleSheet *user_style_sheet)
{
	g_return_val_if_fail (user_style_sheet != NULL, NULL);

	return user_style_sheet->content.source;
}

WebKitUserScript *
webkit_user_script_new (const gchar *source,
			WebKitUserContentInjectedFrames injected_frames,
			WebKitUserScriptInjectionTime injection_time,
			const gchar * const *allow_list,
			const gchar * const *block_list)
{
	g_return_val_if_fail (source != NULL, NULL);

	return (WebKitUserScript *) user_content_new (source, injected_frames, injection_time, allow_list, block_list);
}

WebKitUserScript *
webkit_user_script_ref (WebKitUserScript *user_script)
{
	return user_content_ref (user_script);
}

void
webkit_user_script_unref (WebKitUserScript *user_script)
{
	user_content_unref (user_script);
}

const gchar *
_webkit_user_script_get_source (WebKitUserScript *user_script)
{
	g_return_val_if_fail (user_script != NULL, NULL);

	return user_script->content.source;
}

/* ------------------------------------------------------------------------ */
/* WebKitUserContentManager                                                 */
/* ------------------------------------------------------------------------ */

struct _WebKitUserContentManager {
	GObject parent;

	GPtrArray *style_sheets; /* WebKitUserStyleSheet * */
	GPtrArray *scripts; /* WebKitUserScript * */
	GHashTable *message_handlers; /* gchar *name ~> NULL */
};

struct _WebKitUserContentManagerClass {
	GObjectClass parent_class;
};

enum {
	UCM_SIGNAL_SCRIPT_MESSAGE_RECEIVED,
	UCM_SIGNAL_SHIM_CHANGED,
	UCM_N_SIGNALS
};

static guint ucm_signals[UCM_N_SIGNALS];

G_DEFINE_TYPE (WebKitUserContentManager, webkit_user_content_manager, G_TYPE_OBJECT)

static void
webkit_user_content_manager_finalize (GObject *object)
{
	WebKitUserContentManager *manager = WEBKIT_USER_CONTENT_MANAGER (object);

	g_ptr_array_unref (manager->style_sheets);
	g_ptr_array_unref (manager->scripts);
	g_hash_table_destroy (manager->message_handlers);

	G_OBJECT_CLASS (webkit_user_content_manager_parent_class)->finalize (object);
}

static void
webkit_user_content_manager_class_init (WebKitUserContentManagerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = webkit_user_content_manager_finalize;

	/* void (* script_message_received) (WebKitUserContentManager *manager,
	                                     WebKitJavascriptResult *js_result,
	                                     gpointer user_data);
	   detailed by the handler name */
	ucm_signals[UCM_SIGNAL_SCRIPT_MESSAGE_RECEIVED] = g_signal_new ("script-message-received",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		WEBKIT_TYPE_JAVASCRIPT_RESULT | G_SIGNAL_TYPE_STATIC_SCOPE);

	/* Shim only (not WebKitGTK API): style sheets, scripts or message
	   handlers changed; the views re-inject their page script */
	ucm_signals[UCM_SIGNAL_SHIM_CHANGED] = g_signal_new ("webkit-shim-changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);
}

static void
webkit_user_content_manager_changed (WebKitUserContentManager *manager)
{
	g_signal_emit (manager, ucm_signals[UCM_SIGNAL_SHIM_CHANGED], 0);
}

static void
webkit_user_content_manager_init (WebKitUserContentManager *manager)
{
	manager->style_sheets = g_ptr_array_new_with_free_func ((GDestroyNotify) webkit_user_style_sheet_unref);
	manager->scripts = g_ptr_array_new_with_free_func ((GDestroyNotify) webkit_user_script_unref);
	manager->message_handlers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

WebKitUserContentManager *
webkit_user_content_manager_new (void)
{
	return g_object_new (WEBKIT_TYPE_USER_CONTENT_MANAGER, NULL);
}

void
webkit_user_content_manager_add_style_sheet (WebKitUserContentManager *manager,
					     WebKitUserStyleSheet *stylesheet)
{
	g_return_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager));
	g_return_if_fail (stylesheet != NULL);

	g_ptr_array_add (manager->style_sheets, webkit_user_style_sheet_ref (stylesheet));
	webkit_user_content_manager_changed (manager);
}

void
webkit_user_content_manager_remove_style_sheet (WebKitUserContentManager *manager,
						WebKitUserStyleSheet *stylesheet)
{
	g_return_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager));
	g_return_if_fail (stylesheet != NULL);

	if (g_ptr_array_remove (manager->style_sheets, stylesheet))
		webkit_user_content_manager_changed (manager);
}

void
webkit_user_content_manager_remove_all_style_sheets (WebKitUserContentManager *manager)
{
	g_return_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager));

	g_ptr_array_set_size (manager->style_sheets, 0);
	webkit_user_content_manager_changed (manager);
}

void
webkit_user_content_manager_add_script (WebKitUserContentManager *manager,
					WebKitUserScript *script)
{
	g_return_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager));
	g_return_if_fail (script != NULL);

	g_ptr_array_add (manager->scripts, webkit_user_script_ref (script));
	webkit_user_content_manager_changed (manager);
}

void
webkit_user_content_manager_remove_script (WebKitUserContentManager *manager,
					   WebKitUserScript *script)
{
	g_return_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager));
	g_return_if_fail (script != NULL);

	if (g_ptr_array_remove (manager->scripts, script))
		webkit_user_content_manager_changed (manager);
}

void
webkit_user_content_manager_remove_all_scripts (WebKitUserContentManager *manager)
{
	g_return_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager));

	g_ptr_array_set_size (manager->scripts, 0);
	webkit_user_content_manager_changed (manager);
}

gboolean
webkit_user_content_manager_register_script_message_handler (WebKitUserContentManager *manager,
							     const gchar *name)
{
	g_return_val_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (g_hash_table_contains (manager->message_handlers, name))
		return FALSE;

	g_hash_table_add (manager->message_handlers, g_strdup (name));
	webkit_user_content_manager_changed (manager);

	return TRUE;
}

void
webkit_user_content_manager_unregister_script_message_handler (WebKitUserContentManager *manager,
							       const gchar *name)
{
	g_return_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager));
	g_return_if_fail (name != NULL);

	if (g_hash_table_remove (manager->message_handlers, name))
		webkit_user_content_manager_changed (manager);
}

gboolean
_webkit_user_content_manager_dispatch_script_message (WebKitUserContentManager *manager,
						      const gchar *name,
						      JSCValue *value)
{
	WebKitJavascriptResult *js_result;

	g_return_val_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	if (!g_hash_table_contains (manager->message_handlers, name))
		return FALSE;

	js_result = _webkit_javascript_result_new (value);
	g_signal_emit (manager, ucm_signals[UCM_SIGNAL_SCRIPT_MESSAGE_RECEIVED],
		g_quark_from_string (name), js_result);
	webkit_javascript_result_unref (js_result);

	return TRUE;
}

GPtrArray *
_webkit_user_content_manager_get_style_sheets (WebKitUserContentManager *manager)
{
	g_return_val_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager), NULL);

	return manager->style_sheets;
}

GPtrArray *
_webkit_user_content_manager_get_scripts (WebKitUserContentManager *manager)
{
	g_return_val_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager), NULL);

	return manager->scripts;
}

/* JSON string literal, safe to embed in a script */
static void
append_js_string (GString *script,
		  const gchar *value)
{
	JsonNode *node = json_node_new (JSON_NODE_VALUE);
	gchar *literal;

	json_node_set_string (node, value ? value : "");
	literal = json_to_string (node, FALSE);
	g_string_append (script, literal);

	g_free (literal);
	json_node_unref (node);
}

/* Applies the current style sheets to an already loaded document (and its
 * same-origin frames); the page script defines the function */
gchar *
_webkit_user_content_manager_build_style_update (WebKitUserContentManager *manager)
{
	GString *script = g_string_new ("if (window.__webkitShimSetStyles) window.__webkitShimSetStyles(");
	GString *css = g_string_new ("");
	guint ii;

	g_return_val_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager), NULL);

	for (ii = 0; ii < manager->style_sheets->len; ii++) {
		g_string_append (css, _webkit_user_style_sheet_get_source (manager->style_sheets->pdata[ii]));
		g_string_append_c (css, '\n');
	}

	append_js_string (script, css->str);
	g_string_append (script, ", true);");

	g_string_free (css, TRUE);

	return g_string_free (script, FALSE);
}

gchar *
_webkit_user_content_manager_build_page_script (WebKitUserContentManager *manager)
{
	GString *script;
	GHashTableIter iter;
	gpointer key;
	gchar *styles;
	guint ii;

	g_return_val_if_fail (WEBKIT_IS_USER_CONTENT_MANAGER (manager), NULL);

	script = g_string_new ("(function () {\n");

	/* window.webkit.messageHandlers.<name>.postMessage(value): the message
	 * is {name, value} as JSON. Frames post through the top window when
	 * they have no chrome.webview of their own (same-origin frames). */
	g_string_append (script,
		"\tvar names = [");
	g_hash_table_iter_init (&iter, manager->message_handlers);
	for (ii = 0; g_hash_table_iter_next (&iter, &key, NULL); ii++) {
		if (ii)
			g_string_append_c (script, ',');
		append_js_string (script, key);
	}
	g_string_append (script, "];\n"
		"\tfunction post(name, value) {\n"
		"\t\tvar w = window, target = null;\n"
		"\t\ttry { target = (w.chrome && w.chrome.webview) || (w.top && w.top.chrome && w.top.chrome.webview); } catch (e) {}\n"
		"\t\tif (target) target.postMessage(JSON.stringify({ name: name, value: value === undefined ? null : value }));\n"
		"\t}\n"
		"\twindow.webkit = window.webkit || {};\n"
		"\tvar handlers = {};\n"
		"\tnames.forEach(function (name) {\n"
		"\t\thandlers[name] = { postMessage: function (value) { post(name, value); } };\n"
		"\t});\n"
		"\twindow.webkit.messageHandlers = handlers;\n"
		/* page errors, printed by the view with WEBKIT_SHIM_DEBUG=1 */
		"\twindow.addEventListener(\"error\", function (e) {\n"
		"\t\tpost(\"__webkit_shim_error\", String(e.message) + \" (\" + e.filename + \":\" + e.lineno + \")\");\n"
		"\t});\n");

	/* style sheets in a <style> element, kept up to date.
	 * On préfixe par des couleurs claires par défaut pour les classes d'Evolution
	 * afin d'éviter tout rendu noir sur fond noir avant l'exécution des scripts JSC. */
	g_string_append (script,
		"\tvar defaultCss = \":root { color-scheme: light !important; } \"\n"
		"\t\t\".-e-web-view-background-color { background-color: #ffffff; } \"\n"
		"\t\t\".-e-web-view-text-color, .e-web-view-text-color { color: #1a1a1a; }\\n\";\n"
		"\tvar css = \"\";\n"
		"\tfunction applyStyles() {\n"
		"\t\tvar root = document.head || document.documentElement;\n"
		"\t\tif (!root) return;\n"
		"\t\tvar style = document.getElementById(\"webkit-shim-user-styles\");\n"
		"\t\tif (!style) {\n"
		"\t\t\tstyle = document.createElement(\"style\");\n"
		"\t\t\tstyle.id = \"webkit-shim-user-styles\";\n"
		"\t\t}\n"
		"\t\tif (style.parentNode !== root) root.insertBefore(style, root.firstChild);\n"
		"\t\tstyle.textContent = defaultCss + css;\n"
		"\t}\n"
		"\twindow.__webkitShimSetStyles = function (value, frames) {\n"
		"\t\tcss = value;\n"
		"\t\tapplyStyles();\n"
		"\t\tif (frames) for (var i = 0; i < window.frames.length; i++) {\n"
		"\t\t\ttry { if (window.frames[i].__webkitShimSetStyles) window.frames[i].__webkitShimSetStyles(value, true); } catch (e) {}\n"
		"\t\t}\n"
		"\t};\n");

	/* initial styles of the new document (it has no frames yet) */
	styles = _webkit_user_content_manager_build_style_update (manager);
	g_string_append_printf (script, "\t%s\n", styles);
	g_free (styles);

	g_string_append (script,
		"\tdocument.addEventListener(\"DOMContentLoaded\", applyStyles);\n"
		"})();\n");

	/* user scripts run in the global scope, like WebKit user scripts */
	for (ii = 0; ii < manager->scripts->len; ii++) {
		g_string_append (script, _webkit_user_script_get_source (manager->scripts->pdata[ii]));
		g_string_append_c (script, '\n');
	}

	return g_string_free (script, FALSE);
}
