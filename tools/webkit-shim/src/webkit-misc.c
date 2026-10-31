/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitHitTestResult, WebKitFindController, WebKitPrintOperation,
 * WebKitWebInspector, WebKitContextMenu, WebKitEditorState and
 * WebKitAuthenticationRequest.
 */

#include "webkit-shim-private.h"

/* ------------------------------------------------------------------------ */
/* WebKitHitTestResult                                                      */
/* ------------------------------------------------------------------------ */

struct _WebKitHitTestResult {
	GObject parent;

	guint context;
	gchar *link_uri;
	gchar *link_title;
	gchar *link_label;
	gchar *image_uri;
	gchar *media_uri;
};

struct _WebKitHitTestResultClass {
	GObjectClass parent_class;
};

enum {
	HIT_PROP_0,
	HIT_PROP_CONTEXT,
	HIT_PROP_LINK_URI,
	HIT_PROP_LINK_TITLE,
	HIT_PROP_LINK_LABEL,
	HIT_PROP_IMAGE_URI,
	HIT_PROP_MEDIA_URI,
	HIT_N_PROPS
};

static GParamSpec *hit_props[HIT_N_PROPS];

G_DEFINE_TYPE (WebKitHitTestResult, webkit_hit_test_result, G_TYPE_OBJECT)

static void
webkit_hit_test_result_set_property (GObject *object,
				     guint property_id,
				     const GValue *value,
				     GParamSpec *pspec)
{
	WebKitHitTestResult *result = WEBKIT_HIT_TEST_RESULT (object);

	switch (property_id) {
	case HIT_PROP_CONTEXT:
		result->context = g_value_get_flags (value);
		break;
	case HIT_PROP_LINK_URI:
		g_free (result->link_uri);
		result->link_uri = g_value_dup_string (value);
		break;
	case HIT_PROP_LINK_TITLE:
		g_free (result->link_title);
		result->link_title = g_value_dup_string (value);
		break;
	case HIT_PROP_LINK_LABEL:
		g_free (result->link_label);
		result->link_label = g_value_dup_string (value);
		break;
	case HIT_PROP_IMAGE_URI:
		g_free (result->image_uri);
		result->image_uri = g_value_dup_string (value);
		break;
	case HIT_PROP_MEDIA_URI:
		g_free (result->media_uri);
		result->media_uri = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_hit_test_result_get_property (GObject *object,
				     guint property_id,
				     GValue *value,
				     GParamSpec *pspec)
{
	WebKitHitTestResult *result = WEBKIT_HIT_TEST_RESULT (object);

	switch (property_id) {
	case HIT_PROP_CONTEXT:
		g_value_set_flags (value, result->context);
		break;
	case HIT_PROP_LINK_URI:
		g_value_set_string (value, result->link_uri);
		break;
	case HIT_PROP_LINK_TITLE:
		g_value_set_string (value, result->link_title);
		break;
	case HIT_PROP_LINK_LABEL:
		g_value_set_string (value, result->link_label);
		break;
	case HIT_PROP_IMAGE_URI:
		g_value_set_string (value, result->image_uri);
		break;
	case HIT_PROP_MEDIA_URI:
		g_value_set_string (value, result->media_uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_hit_test_result_finalize (GObject *object)
{
	WebKitHitTestResult *result = WEBKIT_HIT_TEST_RESULT (object);

	g_free (result->link_uri);
	g_free (result->link_title);
	g_free (result->link_label);
	g_free (result->image_uri);
	g_free (result->media_uri);

	G_OBJECT_CLASS (webkit_hit_test_result_parent_class)->finalize (object);
}

static void
webkit_hit_test_result_class_init (WebKitHitTestResultClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	const GParamFlags flags = G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS;

	object_class->set_property = webkit_hit_test_result_set_property;
	object_class->get_property = webkit_hit_test_result_get_property;
	object_class->finalize = webkit_hit_test_result_finalize;

	hit_props[HIT_PROP_CONTEXT] = g_param_spec_flags ("context", NULL, NULL,
		WEBKIT_TYPE_HIT_TEST_RESULT_CONTEXT, WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT, flags);
	hit_props[HIT_PROP_LINK_URI] = g_param_spec_string ("link-uri", NULL, NULL, NULL, flags);
	hit_props[HIT_PROP_LINK_TITLE] = g_param_spec_string ("link-title", NULL, NULL, NULL, flags);
	hit_props[HIT_PROP_LINK_LABEL] = g_param_spec_string ("link-label", NULL, NULL, NULL, flags);
	hit_props[HIT_PROP_IMAGE_URI] = g_param_spec_string ("image-uri", NULL, NULL, NULL, flags);
	hit_props[HIT_PROP_MEDIA_URI] = g_param_spec_string ("media-uri", NULL, NULL, NULL, flags);

	g_object_class_install_properties (object_class, HIT_N_PROPS, hit_props);
}

static void
webkit_hit_test_result_init (WebKitHitTestResult *result)
{
}

WebKitHitTestResult *
_webkit_hit_test_result_new (guint context,
			     const gchar *link_uri,
			     const gchar *link_title,
			     const gchar *link_label,
			     const gchar *image_uri,
			     const gchar *media_uri)
{
	return g_object_new (WEBKIT_TYPE_HIT_TEST_RESULT,
		"context", context,
		"link-uri", link_uri,
		"link-title", link_title,
		"link-label", link_label,
		"image-uri", image_uri,
		"media-uri", media_uri,
		NULL);
}

guint
webkit_hit_test_result_get_context (WebKitHitTestResult *hit_test_result)
{
	g_return_val_if_fail (WEBKIT_IS_HIT_TEST_RESULT (hit_test_result), 0);

	return hit_test_result->context;
}

#define DEFINE_CONTEXT_IS(func, flag) \
gboolean \
webkit_hit_test_result_context_is_##func (WebKitHitTestResult *hit_test_result) \
{ \
	g_return_val_if_fail (WEBKIT_IS_HIT_TEST_RESULT (hit_test_result), FALSE); \
	return (hit_test_result->context & (flag)) != 0; \
}

DEFINE_CONTEXT_IS (link, WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK)
DEFINE_CONTEXT_IS (image, WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE)
DEFINE_CONTEXT_IS (media, WEBKIT_HIT_TEST_RESULT_CONTEXT_MEDIA)
DEFINE_CONTEXT_IS (editable, WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE)
DEFINE_CONTEXT_IS (selection, WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION)
DEFINE_CONTEXT_IS (scrollbar, WEBKIT_HIT_TEST_RESULT_CONTEXT_SCROLLBAR)

const gchar *
webkit_hit_test_result_get_link_uri (WebKitHitTestResult *hit_test_result)
{
	g_return_val_if_fail (WEBKIT_IS_HIT_TEST_RESULT (hit_test_result), NULL);

	return hit_test_result->link_uri;
}

const gchar *
webkit_hit_test_result_get_link_title (WebKitHitTestResult *hit_test_result)
{
	g_return_val_if_fail (WEBKIT_IS_HIT_TEST_RESULT (hit_test_result), NULL);

	return hit_test_result->link_title;
}

const gchar *
webkit_hit_test_result_get_link_label (WebKitHitTestResult *hit_test_result)
{
	g_return_val_if_fail (WEBKIT_IS_HIT_TEST_RESULT (hit_test_result), NULL);

	return hit_test_result->link_label;
}

const gchar *
webkit_hit_test_result_get_image_uri (WebKitHitTestResult *hit_test_result)
{
	g_return_val_if_fail (WEBKIT_IS_HIT_TEST_RESULT (hit_test_result), NULL);

	return hit_test_result->image_uri;
}

const gchar *
webkit_hit_test_result_get_media_uri (WebKitHitTestResult *hit_test_result)
{
	g_return_val_if_fail (WEBKIT_IS_HIT_TEST_RESULT (hit_test_result), NULL);

	return hit_test_result->media_uri;
}

/* ------------------------------------------------------------------------ */
/* WebKitFindController                                                     */
/* ------------------------------------------------------------------------ */

struct _WebKitFindController {
	GObject parent;

	WebKitWebView *web_view; /* weak */
	gchar *text;
	guint32 options;
	guint max_match_count;
};

struct _WebKitFindControllerClass {
	GObjectClass parent_class;
};

enum {
	FIND_PROP_0,
	FIND_PROP_TEXT,
	FIND_PROP_OPTIONS,
	FIND_PROP_MAX_MATCH_COUNT,
	FIND_PROP_WEB_VIEW,
	FIND_N_PROPS
};

enum {
	FIND_SIGNAL_FOUND_TEXT,
	FIND_SIGNAL_FAILED_TO_FIND_TEXT,
	FIND_SIGNAL_COUNTED_MATCHES,
	FIND_N_SIGNALS
};

static GParamSpec *find_props[FIND_N_PROPS];
static guint find_signals[FIND_N_SIGNALS];

G_DEFINE_TYPE (WebKitFindController, webkit_find_controller, G_TYPE_OBJECT)

static void
webkit_find_controller_set_property (GObject *object,
				     guint property_id,
				     const GValue *value,
				     GParamSpec *pspec)
{
	WebKitFindController *controller = WEBKIT_FIND_CONTROLLER (object);

	switch (property_id) {
	case FIND_PROP_WEB_VIEW:
		controller->web_view = g_value_get_object (value);
		if (controller->web_view)
			g_object_add_weak_pointer (G_OBJECT (controller->web_view), (gpointer *) &controller->web_view);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_find_controller_get_property (GObject *object,
				     guint property_id,
				     GValue *value,
				     GParamSpec *pspec)
{
	WebKitFindController *controller = WEBKIT_FIND_CONTROLLER (object);

	switch (property_id) {
	case FIND_PROP_TEXT:
		g_value_set_string (value, controller->text);
		break;
	case FIND_PROP_OPTIONS:
		g_value_set_flags (value, controller->options);
		break;
	case FIND_PROP_MAX_MATCH_COUNT:
		g_value_set_uint (value, controller->max_match_count);
		break;
	case FIND_PROP_WEB_VIEW:
		g_value_set_object (value, controller->web_view);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_find_controller_finalize (GObject *object)
{
	WebKitFindController *controller = WEBKIT_FIND_CONTROLLER (object);

	if (controller->web_view)
		g_object_remove_weak_pointer (G_OBJECT (controller->web_view), (gpointer *) &controller->web_view);
	g_free (controller->text);

	G_OBJECT_CLASS (webkit_find_controller_parent_class)->finalize (object);
}

static void
webkit_find_controller_class_init (WebKitFindControllerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = webkit_find_controller_set_property;
	object_class->get_property = webkit_find_controller_get_property;
	object_class->finalize = webkit_find_controller_finalize;

	find_props[FIND_PROP_TEXT] = g_param_spec_string ("text", NULL, NULL, NULL,
		G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	find_props[FIND_PROP_OPTIONS] = g_param_spec_flags ("options", NULL, NULL,
		WEBKIT_TYPE_FIND_OPTIONS, WEBKIT_FIND_OPTIONS_NONE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	find_props[FIND_PROP_MAX_MATCH_COUNT] = g_param_spec_uint ("max-match-count", NULL, NULL,
		0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	find_props[FIND_PROP_WEB_VIEW] = g_param_spec_object ("web-view", NULL, NULL,
		WEBKIT_TYPE_WEB_VIEW, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, FIND_N_PROPS, find_props);

	/* void (* found_text) (WebKitFindController *find_controller,
	                        guint match_count, gpointer user_data); */
	find_signals[FIND_SIGNAL_FOUND_TEXT] = g_signal_new ("found-text",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 1, G_TYPE_UINT);

	/* void (* failed_to_find_text) (WebKitFindController *find_controller,
	                                 gpointer user_data); */
	find_signals[FIND_SIGNAL_FAILED_TO_FIND_TEXT] = g_signal_new ("failed-to-find-text",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/* void (* counted_matches) (WebKitFindController *find_controller,
	                             guint match_count, gpointer user_data); */
	find_signals[FIND_SIGNAL_COUNTED_MATCHES] = g_signal_new ("counted-matches",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
webkit_find_controller_init (WebKitFindController *controller)
{
}

WebKitFindController *
_webkit_find_controller_new (WebKitWebView *web_view)
{
	return g_object_new (WEBKIT_TYPE_FIND_CONTROLLER, "web-view", web_view, NULL);
}

static void
webkit_find_controller_set_search (WebKitFindController *controller,
				   const gchar *search_text,
				   guint32 find_options,
				   guint max_match_count)
{
	GObject *object = G_OBJECT (controller);

	g_object_freeze_notify (object);

	if (g_strcmp0 (controller->text, search_text) != 0) {
		g_free (controller->text);
		controller->text = g_strdup (search_text);
		g_object_notify_by_pspec (object, find_props[FIND_PROP_TEXT]);
	}

	if (controller->options != find_options) {
		controller->options = find_options;
		g_object_notify_by_pspec (object, find_props[FIND_PROP_OPTIONS]);
	}

	if (controller->max_match_count != max_match_count) {
		controller->max_match_count = max_match_count;
		g_object_notify_by_pspec (object, find_props[FIND_PROP_MAX_MATCH_COUNT]);
	}

	g_object_thaw_notify (object);
}

void
webkit_find_controller_search (WebKitFindController *find_controller,
			       const gchar *search_text,
			       guint32 find_options,
			       guint max_match_count)
{
	g_return_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller));
	g_return_if_fail (search_text != NULL);

	webkit_find_controller_set_search (find_controller, search_text, find_options, max_match_count);

	/* TODO WebView2: highlight the matches (window.find() or a CSS
	   Custom Highlight script), then emit "found-text" with the match
	   count or "failed-to-find-text" */
}

void
webkit_find_controller_search_finish (WebKitFindController *find_controller)
{
	g_return_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller));

	/* TODO WebView2: clear the highlighting */
	webkit_find_controller_set_search (find_controller, NULL, find_controller->options, find_controller->max_match_count);
}

void
webkit_find_controller_search_next (WebKitFindController *find_controller)
{
	g_return_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller));

	/* TODO WebView2: move to the next match */
}

void
webkit_find_controller_search_previous (WebKitFindController *find_controller)
{
	g_return_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller));

	/* TODO WebView2: move to the previous match */
}

void
webkit_find_controller_count_matches (WebKitFindController *find_controller,
				      const gchar *search_text,
				      guint32 find_options,
				      guint max_match_count)
{
	g_return_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller));
	g_return_if_fail (search_text != NULL);

	webkit_find_controller_set_search (find_controller, search_text, find_options, max_match_count);

	/* TODO WebView2: count the matches, then emit "counted-matches" */
}

const gchar *
webkit_find_controller_get_search_text (WebKitFindController *find_controller)
{
	g_return_val_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller), NULL);

	return find_controller->text;
}

guint32
webkit_find_controller_get_options (WebKitFindController *find_controller)
{
	g_return_val_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller), WEBKIT_FIND_OPTIONS_NONE);

	return find_controller->options;
}

guint
webkit_find_controller_get_max_match_count (WebKitFindController *find_controller)
{
	g_return_val_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller), 0);

	return find_controller->max_match_count;
}

WebKitWebView *
webkit_find_controller_get_web_view (WebKitFindController *find_controller)
{
	g_return_val_if_fail (WEBKIT_IS_FIND_CONTROLLER (find_controller), NULL);

	return find_controller->web_view;
}

/* ------------------------------------------------------------------------ */
/* WebKitPrintOperation                                                     */
/* ------------------------------------------------------------------------ */

struct _WebKitPrintOperation {
	GObject parent;

	WebKitWebView *web_view;
	GtkPrintSettings *print_settings;
	GtkPageSetup *page_setup;
};

struct _WebKitPrintOperationClass {
	GObjectClass parent_class;
};

enum {
	PRINT_PROP_0,
	PRINT_PROP_WEB_VIEW,
	PRINT_PROP_PRINT_SETTINGS,
	PRINT_PROP_PAGE_SETUP,
	PRINT_N_PROPS
};

enum {
	PRINT_SIGNAL_FINISHED,
	PRINT_SIGNAL_FAILED,
	PRINT_N_SIGNALS
};

static GParamSpec *print_props[PRINT_N_PROPS];
static guint print_signals[PRINT_N_SIGNALS];

G_DEFINE_TYPE (WebKitPrintOperation, webkit_print_operation, G_TYPE_OBJECT)

static void
webkit_print_operation_set_property (GObject *object,
				     guint property_id,
				     const GValue *value,
				     GParamSpec *pspec)
{
	WebKitPrintOperation *operation = WEBKIT_PRINT_OPERATION (object);

	switch (property_id) {
	case PRINT_PROP_WEB_VIEW:
		operation->web_view = g_value_dup_object (value);
		break;
	case PRINT_PROP_PRINT_SETTINGS:
		webkit_print_operation_set_print_settings (operation, g_value_get_object (value));
		break;
	case PRINT_PROP_PAGE_SETUP:
		webkit_print_operation_set_page_setup (operation, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_print_operation_get_property (GObject *object,
				     guint property_id,
				     GValue *value,
				     GParamSpec *pspec)
{
	WebKitPrintOperation *operation = WEBKIT_PRINT_OPERATION (object);

	switch (property_id) {
	case PRINT_PROP_WEB_VIEW:
		g_value_set_object (value, operation->web_view);
		break;
	case PRINT_PROP_PRINT_SETTINGS:
		g_value_set_object (value, operation->print_settings);
		break;
	case PRINT_PROP_PAGE_SETUP:
		g_value_set_object (value, operation->page_setup);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_print_operation_dispose (GObject *object)
{
	WebKitPrintOperation *operation = WEBKIT_PRINT_OPERATION (object);

	g_clear_object (&operation->web_view);
	g_clear_object (&operation->print_settings);
	g_clear_object (&operation->page_setup);

	G_OBJECT_CLASS (webkit_print_operation_parent_class)->dispose (object);
}

static void
webkit_print_operation_class_init (WebKitPrintOperationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = webkit_print_operation_set_property;
	object_class->get_property = webkit_print_operation_get_property;
	object_class->dispose = webkit_print_operation_dispose;

	print_props[PRINT_PROP_WEB_VIEW] = g_param_spec_object ("web-view", NULL, NULL,
		WEBKIT_TYPE_WEB_VIEW, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	print_props[PRINT_PROP_PRINT_SETTINGS] = g_param_spec_object ("print-settings", NULL, NULL,
		GTK_TYPE_PRINT_SETTINGS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	print_props[PRINT_PROP_PAGE_SETUP] = g_param_spec_object ("page-setup", NULL, NULL,
		GTK_TYPE_PAGE_SETUP, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, PRINT_N_PROPS, print_props);

	/* void (* finished) (WebKitPrintOperation *print_operation,
	                      gpointer user_data); */
	print_signals[PRINT_SIGNAL_FINISHED] = g_signal_new ("finished",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/* void (* failed) (WebKitPrintOperation *print_operation,
	                    GError *error, gpointer user_data); */
	print_signals[PRINT_SIGNAL_FAILED] = g_signal_new ("failed",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 1, G_TYPE_ERROR | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
webkit_print_operation_init (WebKitPrintOperation *operation)
{
}

WebKitPrintOperation *
webkit_print_operation_new (WebKitWebView *web_view)
{
	g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), NULL);

	return g_object_new (WEBKIT_TYPE_PRINT_OPERATION, "web-view", web_view, NULL);
}

GtkPrintSettings *
webkit_print_operation_get_print_settings (WebKitPrintOperation *print_operation)
{
	g_return_val_if_fail (WEBKIT_IS_PRINT_OPERATION (print_operation), NULL);

	return print_operation->print_settings;
}

void
webkit_print_operation_set_print_settings (WebKitPrintOperation *print_operation,
					   GtkPrintSettings *print_settings)
{
	g_return_if_fail (WEBKIT_IS_PRINT_OPERATION (print_operation));
	g_return_if_fail (!print_settings || GTK_IS_PRINT_SETTINGS (print_settings));

	if (g_set_object (&print_operation->print_settings, print_settings))
		g_object_notify_by_pspec (G_OBJECT (print_operation), print_props[PRINT_PROP_PRINT_SETTINGS]);
}

GtkPageSetup *
webkit_print_operation_get_page_setup (WebKitPrintOperation *print_operation)
{
	g_return_val_if_fail (WEBKIT_IS_PRINT_OPERATION (print_operation), NULL);

	return print_operation->page_setup;
}

void
webkit_print_operation_set_page_setup (WebKitPrintOperation *print_operation,
				       GtkPageSetup *page_setup)
{
	g_return_if_fail (WEBKIT_IS_PRINT_OPERATION (print_operation));
	g_return_if_fail (!page_setup || GTK_IS_PAGE_SETUP (page_setup));

	if (g_set_object (&print_operation->page_setup, page_setup))
		g_object_notify_by_pspec (G_OBJECT (print_operation), print_props[PRINT_PROP_PAGE_SETUP]);
}

WebKitPrintOperationResponse
webkit_print_operation_run_dialog (WebKitPrintOperation *print_operation,
				   GtkWindow *parent)
{
	g_return_val_if_fail (WEBKIT_IS_PRINT_OPERATION (print_operation), WEBKIT_PRINT_OPERATION_RESPONSE_CANCEL);

	/* TODO WebView2: GtkPrintUnixDialog does not exist on Windows; use
	   ICoreWebView2_16::ShowPrintUI() or PrintToPdf() + the Windows print
	   dialog, then emit "finished"/"failed" */
	g_warning ("%s: %s", G_STRFUNC, WEBKIT_SHIM_NOT_IMPLEMENTED_MESSAGE);

	return WEBKIT_PRINT_OPERATION_RESPONSE_CANCEL;
}

static gboolean
webkit_print_operation_fail_idle_cb (gpointer user_data)
{
	WebKitPrintOperation *print_operation = user_data;
	GError *error;

	/* as in WebKitGTK: "failed" is followed by "finished" */
	error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, WEBKIT_SHIM_NOT_IMPLEMENTED_MESSAGE);
	g_signal_emit (print_operation, print_signals[PRINT_SIGNAL_FAILED], 0, error);
	g_error_free (error);

	g_signal_emit (print_operation, print_signals[PRINT_SIGNAL_FINISHED], 0);

	return G_SOURCE_REMOVE;
}

void
webkit_print_operation_print (WebKitPrintOperation *print_operation)
{
	g_return_if_fail (WEBKIT_IS_PRINT_OPERATION (print_operation));

	/* TODO WebView2: ICoreWebView2_16::Print() / PrintToPdf() with the
	   GtkPrintSettings translated to ICoreWebView2PrintSettings */
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, webkit_print_operation_fail_idle_cb,
		g_object_ref (print_operation), g_object_unref);
}

/* ------------------------------------------------------------------------ */
/* WebKitWebInspector                                                       */
/* ------------------------------------------------------------------------ */

struct _WebKitWebInspector {
	GObject parent;

	WebKitWebView *web_view; /* weak */
};

struct _WebKitWebInspectorClass {
	GObjectClass parent_class;
};

G_DEFINE_TYPE (WebKitWebInspector, webkit_web_inspector, G_TYPE_OBJECT)

static void
webkit_web_inspector_finalize (GObject *object)
{
	WebKitWebInspector *inspector = WEBKIT_WEB_INSPECTOR (object);

	if (inspector->web_view)
		g_object_remove_weak_pointer (G_OBJECT (inspector->web_view), (gpointer *) &inspector->web_view);

	G_OBJECT_CLASS (webkit_web_inspector_parent_class)->finalize (object);
}

static void
webkit_web_inspector_class_init (WebKitWebInspectorClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = webkit_web_inspector_finalize;
}

static void
webkit_web_inspector_init (WebKitWebInspector *inspector)
{
}

WebKitWebInspector *
_webkit_web_inspector_new (WebKitWebView *web_view)
{
	WebKitWebInspector *inspector;

	inspector = g_object_new (WEBKIT_TYPE_WEB_INSPECTOR, NULL);
	inspector->web_view = web_view;
	if (web_view)
		g_object_add_weak_pointer (G_OBJECT (web_view), (gpointer *) &inspector->web_view);

	return inspector;
}

void
webkit_web_inspector_show (WebKitWebInspector *inspector)
{
	g_return_if_fail (WEBKIT_IS_WEB_INSPECTOR (inspector));

	/* TODO WebView2: ICoreWebView2::OpenDevToolsWindow() */
}

void
webkit_web_inspector_close (WebKitWebInspector *inspector)
{
	g_return_if_fail (WEBKIT_IS_WEB_INSPECTOR (inspector));

	/* TODO WebView2: no API to close the DevTools window */
}

/* ------------------------------------------------------------------------ */
/* WebKitContextMenu                                                        */
/* ------------------------------------------------------------------------ */

struct _WebKitContextMenu {
	GObject parent;
};

struct _WebKitContextMenuClass {
	GObjectClass parent_class;
};

G_DEFINE_TYPE (WebKitContextMenu, webkit_context_menu, G_TYPE_OBJECT)

static void
webkit_context_menu_class_init (WebKitContextMenuClass *klass)
{
}

static void
webkit_context_menu_init (WebKitContextMenu *menu)
{
}

WebKitContextMenu *
webkit_context_menu_new (void)
{
	return g_object_new (WEBKIT_TYPE_CONTEXT_MENU, NULL);
}

guint
webkit_context_menu_get_n_items (WebKitContextMenu *menu)
{
	g_return_val_if_fail (WEBKIT_IS_CONTEXT_MENU (menu), 0);

	/* Evolution always builds its own menu and returns TRUE from
	   "context-menu"; the default WebView2 menu is disabled */
	return 0;
}

void
webkit_context_menu_remove_all (WebKitContextMenu *menu)
{
	g_return_if_fail (WEBKIT_IS_CONTEXT_MENU (menu));
}

/* ------------------------------------------------------------------------ */
/* WebKitEditorState                                                        */
/* ------------------------------------------------------------------------ */

struct _WebKitEditorState {
	GObject parent;

	guint typing_attributes;
	gboolean cut_available;
	gboolean copy_available;
	gboolean paste_available;
	gboolean undo_available;
	gboolean redo_available;
};

struct _WebKitEditorStateClass {
	GObjectClass parent_class;
};

enum {
	EDITOR_STATE_PROP_0,
	EDITOR_STATE_PROP_TYPING_ATTRIBUTES,
	EDITOR_STATE_N_PROPS
};

enum {
	EDITOR_STATE_SIGNAL_CHANGED,
	EDITOR_STATE_N_SIGNALS
};

static GParamSpec *editor_state_props[EDITOR_STATE_N_PROPS];
static guint editor_state_signals[EDITOR_STATE_N_SIGNALS];

G_DEFINE_TYPE (WebKitEditorState, webkit_editor_state, G_TYPE_OBJECT)

static void
webkit_editor_state_get_property (GObject *object,
				  guint property_id,
				  GValue *value,
				  GParamSpec *pspec)
{
	switch (property_id) {
	case EDITOR_STATE_PROP_TYPING_ATTRIBUTES:
		g_value_set_uint (value, WEBKIT_EDITOR_STATE (object)->typing_attributes);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_editor_state_class_init (WebKitEditorStateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = webkit_editor_state_get_property;

	editor_state_props[EDITOR_STATE_PROP_TYPING_ATTRIBUTES] = g_param_spec_uint ("typing-attributes", NULL, NULL,
		0, G_MAXUINT, WEBKIT_EDITOR_TYPING_ATTRIBUTE_NONE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, EDITOR_STATE_N_PROPS, editor_state_props);

	/* void (* changed) (WebKitEditorState *editor_state, gpointer user_data); */
	editor_state_signals[EDITOR_STATE_SIGNAL_CHANGED] = g_signal_new ("changed",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);
}

static void
webkit_editor_state_init (WebKitEditorState *editor_state)
{
	editor_state->typing_attributes = WEBKIT_EDITOR_TYPING_ATTRIBUTE_NONE;
}

WebKitEditorState *
_webkit_editor_state_new (void)
{
	return g_object_new (WEBKIT_TYPE_EDITOR_STATE, NULL);
}

void
_webkit_editor_state_update (WebKitEditorState *editor_state,
			     guint typing_attributes,
			     gboolean cut_available,
			     gboolean copy_available,
			     gboolean paste_available,
			     gboolean undo_available,
			     gboolean redo_available)
{
	g_return_if_fail (WEBKIT_IS_EDITOR_STATE (editor_state));

	editor_state->cut_available = cut_available;
	editor_state->copy_available = copy_available;
	editor_state->paste_available = paste_available;
	editor_state->undo_available = undo_available;
	editor_state->redo_available = redo_available;

	if (editor_state->typing_attributes != typing_attributes) {
		editor_state->typing_attributes = typing_attributes;
		g_object_notify_by_pspec (G_OBJECT (editor_state), editor_state_props[EDITOR_STATE_PROP_TYPING_ATTRIBUTES]);
	}

	g_signal_emit (editor_state, editor_state_signals[EDITOR_STATE_SIGNAL_CHANGED], 0);
}

guint
webkit_editor_state_get_typing_attributes (WebKitEditorState *editor_state)
{
	g_return_val_if_fail (WEBKIT_IS_EDITOR_STATE (editor_state), 0);

	return editor_state->typing_attributes;
}

gboolean
webkit_editor_state_is_cut_available (WebKitEditorState *editor_state)
{
	g_return_val_if_fail (WEBKIT_IS_EDITOR_STATE (editor_state), FALSE);

	return editor_state->cut_available;
}

gboolean
webkit_editor_state_is_copy_available (WebKitEditorState *editor_state)
{
	g_return_val_if_fail (WEBKIT_IS_EDITOR_STATE (editor_state), FALSE);

	return editor_state->copy_available;
}

gboolean
webkit_editor_state_is_paste_available (WebKitEditorState *editor_state)
{
	g_return_val_if_fail (WEBKIT_IS_EDITOR_STATE (editor_state), FALSE);

	return editor_state->paste_available;
}

gboolean
webkit_editor_state_is_undo_available (WebKitEditorState *editor_state)
{
	g_return_val_if_fail (WEBKIT_IS_EDITOR_STATE (editor_state), FALSE);

	return editor_state->undo_available;
}

gboolean
webkit_editor_state_is_redo_available (WebKitEditorState *editor_state)
{
	g_return_val_if_fail (WEBKIT_IS_EDITOR_STATE (editor_state), FALSE);

	return editor_state->redo_available;
}

/* ------------------------------------------------------------------------ */
/* WebKitAuthenticationRequest                                              */
/* ------------------------------------------------------------------------ */

struct _WebKitAuthenticationRequest {
	GObject parent;

	gchar *host;
};

struct _WebKitAuthenticationRequestClass {
	GObjectClass parent_class;
};

enum {
	AUTH_SIGNAL_CANCELLED,
	AUTH_N_SIGNALS
};

static guint auth_signals[AUTH_N_SIGNALS];

G_DEFINE_TYPE (WebKitAuthenticationRequest, webkit_authentication_request, G_TYPE_OBJECT)

static void
webkit_authentication_request_finalize (GObject *object)
{
	g_free (WEBKIT_AUTHENTICATION_REQUEST (object)->host);

	G_OBJECT_CLASS (webkit_authentication_request_parent_class)->finalize (object);
}

static void
webkit_authentication_request_class_init (WebKitAuthenticationRequestClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = webkit_authentication_request_finalize;

	/* void (* cancelled) (WebKitAuthenticationRequest *request, gpointer user_data); */
	auth_signals[AUTH_SIGNAL_CANCELLED] = g_signal_new ("cancelled",
		G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);
}

static void
webkit_authentication_request_init (WebKitAuthenticationRequest *request)
{
}

const gchar *
webkit_authentication_request_get_host (WebKitAuthenticationRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_AUTHENTICATION_REQUEST (request), NULL);

	return request->host;
}

void
webkit_authentication_request_cancel (WebKitAuthenticationRequest *request)
{
	g_return_if_fail (WEBKIT_IS_AUTHENTICATION_REQUEST (request));

	/* TODO WebView2: BasicAuthenticationRequested -> Cancel = TRUE */
	g_signal_emit (request, auth_signals[AUTH_SIGNAL_CANCELLED], 0);
}
