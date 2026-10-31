/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitURIRequest, WebKitNavigationAction, WebKitPolicyDecision and
 * WebKitNavigationPolicyDecision. These are plain data holders; the WebView2
 * backend creates them from NavigationStarting / NewWindowRequested and
 * reads the decision back with _webkit_policy_decision_get_result().
 */

#include "webkit-shim-private.h"

/* ------------------------------------------------------------------------ */
/* WebKitURIRequest                                                         */
/* ------------------------------------------------------------------------ */

struct _WebKitURIRequest {
	GObject parent;

	gchar *uri;
	gchar *http_method;
	SoupMessageHeaders *http_headers;
};

struct _WebKitURIRequestClass {
	GObjectClass parent_class;
};

enum {
	URI_REQUEST_PROP_0,
	URI_REQUEST_PROP_URI,
	URI_REQUEST_N_PROPS
};

static GParamSpec *uri_request_props[URI_REQUEST_N_PROPS];

G_DEFINE_TYPE (WebKitURIRequest, webkit_uri_request, G_TYPE_OBJECT)

static void
webkit_uri_request_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
	case URI_REQUEST_PROP_URI:
		webkit_uri_request_set_uri (WEBKIT_URI_REQUEST (object), g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_uri_request_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
	case URI_REQUEST_PROP_URI:
		g_value_set_string (value, WEBKIT_URI_REQUEST (object)->uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
webkit_uri_request_finalize (GObject *object)
{
	WebKitURIRequest *request = WEBKIT_URI_REQUEST (object);

	g_free (request->uri);
	g_free (request->http_method);
	g_clear_pointer (&request->http_headers, soup_message_headers_unref);

	G_OBJECT_CLASS (webkit_uri_request_parent_class)->finalize (object);
}

static void
webkit_uri_request_class_init (WebKitURIRequestClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = webkit_uri_request_set_property;
	object_class->get_property = webkit_uri_request_get_property;
	object_class->finalize = webkit_uri_request_finalize;

	uri_request_props[URI_REQUEST_PROP_URI] = g_param_spec_string ("uri", NULL, NULL,
		"about:blank", G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, URI_REQUEST_N_PROPS, uri_request_props);
}

static void
webkit_uri_request_init (WebKitURIRequest *request)
{
	request->http_method = g_strdup ("GET");
}

WebKitURIRequest *
webkit_uri_request_new (const gchar *uri)
{
	g_return_val_if_fail (uri != NULL, NULL);

	return g_object_new (WEBKIT_TYPE_URI_REQUEST, "uri", uri, NULL);
}

const gchar *
webkit_uri_request_get_uri (WebKitURIRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_URI_REQUEST (request), NULL);

	return request->uri;
}

void
webkit_uri_request_set_uri (WebKitURIRequest *request,
			    const gchar *uri)
{
	g_return_if_fail (WEBKIT_IS_URI_REQUEST (request));
	g_return_if_fail (uri != NULL);

	if (g_strcmp0 (request->uri, uri) == 0)
		return;

	/* TODO WebView2: in a NavigationStarting handler, a changed URI must
	   cancel the navigation and start a new one to the new URI. */
	g_free (request->uri);
	request->uri = g_strdup (uri);

	g_object_notify_by_pspec (G_OBJECT (request), uri_request_props[URI_REQUEST_PROP_URI]);
}

const gchar *
webkit_uri_request_get_http_method (WebKitURIRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_URI_REQUEST (request), NULL);

	return request->http_method;
}

SoupMessageHeaders *
webkit_uri_request_get_http_headers (WebKitURIRequest *request)
{
	g_return_val_if_fail (WEBKIT_IS_URI_REQUEST (request), NULL);

	if (!request->http_headers)
		request->http_headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_REQUEST);

	return request->http_headers;
}

/* ------------------------------------------------------------------------ */
/* WebKitNavigationAction                                                   */
/* ------------------------------------------------------------------------ */

struct _WebKitNavigationAction {
	WebKitURIRequest *request;
	WebKitNavigationType navigation_type;
	guint mouse_button;
	guint modifiers;
	gboolean is_user_gesture;
	gboolean is_redirect;
};

G_DEFINE_BOXED_TYPE (WebKitNavigationAction, webkit_navigation_action,
	webkit_navigation_action_copy, webkit_navigation_action_free)

WebKitNavigationAction *
_webkit_navigation_action_new (WebKitURIRequest *request,
			       WebKitNavigationType navigation_type,
			       guint mouse_button,
			       guint modifiers,
			       gboolean is_user_gesture)
{
	WebKitNavigationAction *navigation;

	g_return_val_if_fail (WEBKIT_IS_URI_REQUEST (request), NULL);

	navigation = g_slice_new0 (WebKitNavigationAction);
	navigation->request = g_object_ref (request);
	navigation->navigation_type = navigation_type;
	navigation->mouse_button = mouse_button;
	navigation->modifiers = modifiers;
	navigation->is_user_gesture = is_user_gesture;

	return navigation;
}

WebKitNavigationAction *
webkit_navigation_action_copy (WebKitNavigationAction *navigation)
{
	WebKitNavigationAction *copy;

	g_return_val_if_fail (navigation != NULL, NULL);

	copy = g_slice_dup (WebKitNavigationAction, navigation);
	if (copy->request)
		g_object_ref (copy->request);

	return copy;
}

void
webkit_navigation_action_free (WebKitNavigationAction *navigation)
{
	if (!navigation)
		return;

	g_clear_object (&navigation->request);
	g_slice_free (WebKitNavigationAction, navigation);
}

WebKitNavigationType
webkit_navigation_action_get_navigation_type (WebKitNavigationAction *navigation)
{
	g_return_val_if_fail (navigation != NULL, WEBKIT_NAVIGATION_TYPE_OTHER);

	return navigation->navigation_type;
}

guint
webkit_navigation_action_get_mouse_button (WebKitNavigationAction *navigation)
{
	g_return_val_if_fail (navigation != NULL, 0);

	return navigation->mouse_button;
}

guint
webkit_navigation_action_get_modifiers (WebKitNavigationAction *navigation)
{
	g_return_val_if_fail (navigation != NULL, 0);

	return navigation->modifiers;
}

WebKitURIRequest *
webkit_navigation_action_get_request (WebKitNavigationAction *navigation)
{
	g_return_val_if_fail (navigation != NULL, NULL);

	return navigation->request;
}

gboolean
webkit_navigation_action_is_user_gesture (WebKitNavigationAction *navigation)
{
	g_return_val_if_fail (navigation != NULL, FALSE);

	return navigation->is_user_gesture;
}

gboolean
webkit_navigation_action_is_redirect (WebKitNavigationAction *navigation)
{
	g_return_val_if_fail (navigation != NULL, FALSE);

	return navigation->is_redirect;
}

/* ------------------------------------------------------------------------ */
/* WebKitPolicyDecision                                                     */
/* ------------------------------------------------------------------------ */

typedef struct {
	WebKitShimPolicyResult result;
} WebKitPolicyDecisionPrivate;

struct _WebKitPolicyDecision {
	GObject parent;
};

struct _WebKitPolicyDecisionClass {
	GObjectClass parent_class;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WebKitPolicyDecision, webkit_policy_decision, G_TYPE_OBJECT)

static void
webkit_policy_decision_class_init (WebKitPolicyDecisionClass *klass)
{
}

static void
webkit_policy_decision_init (WebKitPolicyDecision *decision)
{
}

static void
webkit_policy_decision_set_result (WebKitPolicyDecision *decision,
				   WebKitShimPolicyResult result)
{
	WebKitPolicyDecisionPrivate *priv;

	g_return_if_fail (WEBKIT_IS_POLICY_DECISION (decision));

	priv = webkit_policy_decision_get_instance_private (decision);

	/* the first decision wins, as in WebKitGTK */
	if (priv->result == WEBKIT_SHIM_POLICY_UNDECIDED)
		priv->result = result;
}

WebKitShimPolicyResult
_webkit_policy_decision_get_result (WebKitPolicyDecision *decision)
{
	WebKitPolicyDecisionPrivate *priv;

	g_return_val_if_fail (WEBKIT_IS_POLICY_DECISION (decision), WEBKIT_SHIM_POLICY_UNDECIDED);

	priv = webkit_policy_decision_get_instance_private (decision);

	return priv->result;
}

void
webkit_policy_decision_use (WebKitPolicyDecision *decision)
{
	webkit_policy_decision_set_result (decision, WEBKIT_SHIM_POLICY_USE);
}

void
webkit_policy_decision_ignore (WebKitPolicyDecision *decision)
{
	webkit_policy_decision_set_result (decision, WEBKIT_SHIM_POLICY_IGNORE);
}

void
webkit_policy_decision_download (WebKitPolicyDecision *decision)
{
	webkit_policy_decision_set_result (decision, WEBKIT_SHIM_POLICY_DOWNLOAD);
}

/* ------------------------------------------------------------------------ */
/* WebKitNavigationPolicyDecision                                           */
/* ------------------------------------------------------------------------ */

struct _WebKitNavigationPolicyDecision {
	WebKitPolicyDecision parent;

	WebKitNavigationAction *navigation_action;
	gchar *frame_name;
};

struct _WebKitNavigationPolicyDecisionClass {
	WebKitPolicyDecisionClass parent_class;
};

G_DEFINE_TYPE (WebKitNavigationPolicyDecision, webkit_navigation_policy_decision, WEBKIT_TYPE_POLICY_DECISION)

static void
webkit_navigation_policy_decision_finalize (GObject *object)
{
	WebKitNavigationPolicyDecision *decision = WEBKIT_NAVIGATION_POLICY_DECISION (object);

	g_clear_pointer (&decision->navigation_action, webkit_navigation_action_free);
	g_free (decision->frame_name);

	G_OBJECT_CLASS (webkit_navigation_policy_decision_parent_class)->finalize (object);
}

static void
webkit_navigation_policy_decision_class_init (WebKitNavigationPolicyDecisionClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = webkit_navigation_policy_decision_finalize;
}

static void
webkit_navigation_policy_decision_init (WebKitNavigationPolicyDecision *decision)
{
}

WebKitPolicyDecision *
_webkit_navigation_policy_decision_new (WebKitNavigationAction *navigation_action,
					const gchar *frame_name)
{
	WebKitNavigationPolicyDecision *decision;

	g_return_val_if_fail (navigation_action != NULL, NULL);

	decision = g_object_new (WEBKIT_TYPE_NAVIGATION_POLICY_DECISION, NULL);
	decision->navigation_action = webkit_navigation_action_copy (navigation_action);
	decision->frame_name = g_strdup (frame_name);

	return WEBKIT_POLICY_DECISION (decision);
}

WebKitNavigationAction *
webkit_navigation_policy_decision_get_navigation_action (WebKitNavigationPolicyDecision *decision)
{
	g_return_val_if_fail (WEBKIT_IS_NAVIGATION_POLICY_DECISION (decision), NULL);

	return decision->navigation_action;
}

const gchar *
webkit_navigation_policy_decision_get_frame_name (WebKitNavigationPolicyDecision *decision)
{
	g_return_val_if_fail (WEBKIT_IS_NAVIGATION_POLICY_DECISION (decision), NULL);

	return decision->frame_name;
}
