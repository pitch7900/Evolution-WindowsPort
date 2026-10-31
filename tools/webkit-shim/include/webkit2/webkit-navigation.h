/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * Navigation: WebKitURIRequest, WebKitNavigationAction (boxed),
 * WebKitPolicyDecision and WebKitNavigationPolicyDecision.
 * WebView2 counterparts: NavigationStarting / NewWindowRequested events.
 */

#ifndef WEBKIT_SHIM_NAVIGATION_H
#define WEBKIT_SHIM_NAVIGATION_H

#include <libsoup/soup.h>
#include <webkit2/webkit-forward.h>
#include <webkit2/webkit-enums.h>

G_BEGIN_DECLS

/* WebKitURIRequest */

#define WEBKIT_TYPE_URI_REQUEST			(webkit_uri_request_get_type ())
#define WEBKIT_URI_REQUEST(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_URI_REQUEST, WebKitURIRequest))
#define WEBKIT_URI_REQUEST_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_URI_REQUEST, WebKitURIRequestClass))
#define WEBKIT_IS_URI_REQUEST(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_URI_REQUEST))
#define WEBKIT_IS_URI_REQUEST_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_URI_REQUEST))
#define WEBKIT_URI_REQUEST_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_URI_REQUEST, WebKitURIRequestClass))

GType		webkit_uri_request_get_type	(void);
WebKitURIRequest *
		webkit_uri_request_new		(const gchar *uri);
const gchar *	webkit_uri_request_get_uri	(WebKitURIRequest *request);
void		webkit_uri_request_set_uri	(WebKitURIRequest *request,
						 const gchar *uri);
const gchar *	webkit_uri_request_get_http_method
						(WebKitURIRequest *request);
SoupMessageHeaders *
		webkit_uri_request_get_http_headers
						(WebKitURIRequest *request);

/* WebKitNavigationAction (boxed) */

#define WEBKIT_TYPE_NAVIGATION_ACTION		(webkit_navigation_action_get_type ())

GType		webkit_navigation_action_get_type
						(void);
WebKitNavigationAction *
		webkit_navigation_action_copy	(WebKitNavigationAction *navigation);
void		webkit_navigation_action_free	(WebKitNavigationAction *navigation);
WebKitNavigationType
		webkit_navigation_action_get_navigation_type
						(WebKitNavigationAction *navigation);
guint		webkit_navigation_action_get_mouse_button
						(WebKitNavigationAction *navigation);
guint		webkit_navigation_action_get_modifiers
						(WebKitNavigationAction *navigation);
WebKitURIRequest *
		webkit_navigation_action_get_request
						(WebKitNavigationAction *navigation);
gboolean	webkit_navigation_action_is_user_gesture
						(WebKitNavigationAction *navigation);
gboolean	webkit_navigation_action_is_redirect
						(WebKitNavigationAction *navigation);

/* WebKitPolicyDecision */

#define WEBKIT_TYPE_POLICY_DECISION		(webkit_policy_decision_get_type ())
#define WEBKIT_POLICY_DECISION(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_POLICY_DECISION, WebKitPolicyDecision))
#define WEBKIT_POLICY_DECISION_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_POLICY_DECISION, WebKitPolicyDecisionClass))
#define WEBKIT_IS_POLICY_DECISION(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_POLICY_DECISION))
#define WEBKIT_IS_POLICY_DECISION_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_POLICY_DECISION))
#define WEBKIT_POLICY_DECISION_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_POLICY_DECISION, WebKitPolicyDecisionClass))

GType		webkit_policy_decision_get_type	(void);
void		webkit_policy_decision_download	(WebKitPolicyDecision *decision);
void		webkit_policy_decision_ignore	(WebKitPolicyDecision *decision);
void		webkit_policy_decision_use	(WebKitPolicyDecision *decision);

/* WebKitNavigationPolicyDecision */

#define WEBKIT_TYPE_NAVIGATION_POLICY_DECISION		(webkit_navigation_policy_decision_get_type ())
#define WEBKIT_NAVIGATION_POLICY_DECISION(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_NAVIGATION_POLICY_DECISION, WebKitNavigationPolicyDecision))
#define WEBKIT_NAVIGATION_POLICY_DECISION_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_NAVIGATION_POLICY_DECISION, WebKitNavigationPolicyDecisionClass))
#define WEBKIT_IS_NAVIGATION_POLICY_DECISION(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_NAVIGATION_POLICY_DECISION))
#define WEBKIT_IS_NAVIGATION_POLICY_DECISION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_NAVIGATION_POLICY_DECISION))
#define WEBKIT_NAVIGATION_POLICY_DECISION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_NAVIGATION_POLICY_DECISION, WebKitNavigationPolicyDecisionClass))

GType		webkit_navigation_policy_decision_get_type
						(void);
WebKitNavigationAction *
		webkit_navigation_policy_decision_get_navigation_action
						(WebKitNavigationPolicyDecision *decision);
const gchar *	webkit_navigation_policy_decision_get_frame_name
						(WebKitNavigationPolicyDecision *decision);

G_END_DECLS

#endif /* WEBKIT_SHIM_NAVIGATION_H */
