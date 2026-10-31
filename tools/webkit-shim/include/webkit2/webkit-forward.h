/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * Forward declarations of every type, so that the headers can reference each
 * other regardless of inclusion order (WebKitWebView <-> WebKitFindController,
 * WebKitPrintOperation, ...). Instance structures are opaque, except
 * WebKitWebView and its class which Evolution subclasses (EWebView,
 * EWebKitEditor), see webkit-web-view.h.
 */

#ifndef WEBKIT_SHIM_FORWARD_H
#define WEBKIT_SHIM_FORWARD_H

#include <glib-object.h>

G_BEGIN_DECLS

/* GObject types */
typedef struct _WebKitWebView			WebKitWebView;
typedef struct _WebKitWebViewClass		WebKitWebViewClass;
typedef struct _WebKitWebViewPrivate		WebKitWebViewPrivate;

typedef struct _WebKitSettings			WebKitSettings;
typedef struct _WebKitSettingsClass		WebKitSettingsClass;

typedef struct _WebKitWebContext		WebKitWebContext;
typedef struct _WebKitWebContextClass		WebKitWebContextClass;

typedef struct _WebKitUserContentManager	WebKitUserContentManager;
typedef struct _WebKitUserContentManagerClass	WebKitUserContentManagerClass;

typedef struct _WebKitFindController		WebKitFindController;
typedef struct _WebKitFindControllerClass	WebKitFindControllerClass;

typedef struct _WebKitPrintOperation		WebKitPrintOperation;
typedef struct _WebKitPrintOperationClass	WebKitPrintOperationClass;

typedef struct _WebKitPolicyDecision		WebKitPolicyDecision;
typedef struct _WebKitPolicyDecisionClass	WebKitPolicyDecisionClass;

typedef struct _WebKitNavigationPolicyDecision		WebKitNavigationPolicyDecision;
typedef struct _WebKitNavigationPolicyDecisionClass	WebKitNavigationPolicyDecisionClass;

typedef struct _WebKitHitTestResult		WebKitHitTestResult;
typedef struct _WebKitHitTestResultClass	WebKitHitTestResultClass;

typedef struct _WebKitURIRequest		WebKitURIRequest;
typedef struct _WebKitURIRequestClass		WebKitURIRequestClass;

typedef struct _WebKitURISchemeRequest		WebKitURISchemeRequest;
typedef struct _WebKitURISchemeRequestClass	WebKitURISchemeRequestClass;

typedef struct _WebKitWebInspector		WebKitWebInspector;
typedef struct _WebKitWebInspectorClass		WebKitWebInspectorClass;

typedef struct _WebKitContextMenu		WebKitContextMenu;
typedef struct _WebKitContextMenuClass		WebKitContextMenuClass;

typedef struct _WebKitEditorState		WebKitEditorState;
typedef struct _WebKitEditorStateClass		WebKitEditorStateClass;

typedef struct _WebKitAuthenticationRequest		WebKitAuthenticationRequest;
typedef struct _WebKitAuthenticationRequestClass	WebKitAuthenticationRequestClass;

/* Boxed types */
typedef struct _WebKitJavascriptResult		WebKitJavascriptResult;
typedef struct _WebKitNavigationAction		WebKitNavigationAction;
typedef struct _WebKitUserStyleSheet		WebKitUserStyleSheet;
typedef struct _WebKitUserScript		WebKitUserScript;

G_END_DECLS

#endif /* WEBKIT_SHIM_FORWARD_H */
