/*
 * WebKitGTK compatibility shim for Windows: WebView2 host (see webview2-host.h).
 *
 * One ICoreWebView2Environment is shared by the process (one user data
 * folder, custom schemes registered once); each host owns one controller in
 * a child HWND given by the GTK side.
 */

#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>

#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>

#include <functional>
#include <string>
#include <vector>

#include "webview2-host.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;

/* ------------------------------------------------------------------------ */
/* String helpers                                                           */
/* ------------------------------------------------------------------------ */

static std::wstring
to_wide (const gchar *utf8)
{
	if (!utf8)
		return std::wstring ();

	gunichar2 *utf16 = g_utf8_to_utf16 (utf8, -1, NULL, NULL, NULL);
	std::wstring result (utf16 ? reinterpret_cast<const wchar_t *> (utf16) : L"");
	g_free (utf16);

	return result;
}

/* takes ownership of a CoTaskMem string */
static gchar *
take_utf8 (LPWSTR wide)
{
	gchar *utf8 = NULL;

	if (wide) {
		utf8 = g_utf16_to_utf8 (reinterpret_cast<const gunichar2 *> (wide), -1, NULL, NULL, NULL);
		CoTaskMemFree (wide);
	}

	return utf8 ? utf8 : g_strdup ("");
}

/* ------------------------------------------------------------------------ */
/* Shared environment                                                       */
/* ------------------------------------------------------------------------ */

namespace {

struct Environment {
	std::wstring user_data_folder;
	std::vector<std::wstring> schemes;
	ComPtr<ICoreWebView2Environment> env;
	HRESULT error = S_OK;
	bool creating = false;
	/* waiting for the environment: called with it, or NULL on failure */
	std::vector<std::function<void (ICoreWebView2Environment *)>> waiters;
};

Environment &
environment ()
{
	static Environment instance;
	return instance;
}

void
environment_get (std::function<void (ICoreWebView2Environment *)> callback)
{
	Environment &e = environment ();

	if (e.env || FAILED (e.error)) {
		callback (e.env.Get ());
		return;
	}

	e.waiters.push_back (std::move (callback));

	if (e.creating)
		return;

	e.creating = true;

	/* STA required by WebView2; GDK usually initialized OLE already */
	CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);

	auto options = Make<CoreWebView2EnvironmentOptions> ();
	ComPtr<ICoreWebView2EnvironmentOptions4> options4;

	/* Evolution's pages span several local schemes (the page, mail:,
	 * evo-file:, cid:...) and its scripts reach into the frames of other
	 * schemes (e.g. to size the message frames), as WebKitGTK allows for
	 * these local schemes. Chromium sees different origins: the same-origin
	 * policy and site isolation are lifted. Remote content of messages is
	 * still filtered by Evolution (http(s) go through its evo-http(s)
	 * handlers, see the GTK side). */
	options->put_AdditionalBrowserArguments (L"--disable-web-security --disable-site-isolation-trials");

	if (SUCCEEDED (options.As (&options4)) && !e.schemes.empty ()) {
		std::vector<ComPtr<ICoreWebView2CustomSchemeRegistration>> registrations;
		std::vector<ICoreWebView2CustomSchemeRegistration *> raw;
		const WCHAR *allowed_origins[] = { L"*" };

		for (const auto &scheme : e.schemes) {
			auto registration = Make<CoreWebView2CustomSchemeRegistration> (scheme.c_str ());

			/* secure context, so that pages may use crypto/clipboard
			 * APIs; any origin may load the scheme's resources */
			registration->put_TreatAsSecure (TRUE);
			registration->put_HasAuthorityComponent (FALSE);
			registration->SetAllowedOrigins (1, allowed_origins);

			raw.push_back (registration.Get ());
			registrations.push_back (registration);
		}

		options4->SetCustomSchemeRegistrations ((UINT32) raw.size (), raw.data ());
	}

	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions (
		NULL,
		e.user_data_folder.empty () ? NULL : e.user_data_folder.c_str (),
		options.Get (),
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> (
			[] (HRESULT result, ICoreWebView2Environment *created) -> HRESULT {
				Environment &e = environment ();

				e.creating = false;
				if (SUCCEEDED (result) && created)
					e.env = created;
				else
					e.error = FAILED (result) ? result : E_FAIL;

				if (FAILED (e.error))
					g_warning ("WebView2: cannot create the environment (0x%08lx); is the WebView2 runtime installed?", (unsigned long) e.error);

				auto waiters = std::move (e.waiters);
				e.waiters.clear ();
				for (auto &waiter : waiters)
					waiter (e.env.Get ());

				return S_OK;
			}).Get ());

	if (FAILED (hr)) {
		e.creating = false;
		e.error = hr;
		g_warning ("WebView2: CreateCoreWebView2EnvironmentWithOptions failed (0x%08lx)", (unsigned long) hr);

		auto waiters = std::move (e.waiters);
		e.waiters.clear ();
		for (auto &waiter : waiters)
			waiter (NULL);
	}
}

} /* namespace */

void
webview2_environment_configure (const gchar *user_data_folder,
				const gchar * const *custom_schemes)
{
	Environment &e = environment ();

	if (e.env || e.creating) {
		g_warning ("WebView2: environment already created, configuration ignored");
		return;
	}

	e.user_data_folder = to_wide (user_data_folder);
	e.schemes.clear ();

	for (guint ii = 0; custom_schemes && custom_schemes[ii]; ii++)
		e.schemes.push_back (to_wide (custom_schemes[ii]));
}

/* ------------------------------------------------------------------------ */
/* Requests                                                                 */
/* ------------------------------------------------------------------------ */

struct _WebView2Request {
	ComPtr<ICoreWebView2Environment> env;
	ComPtr<ICoreWebView2WebResourceRequestedEventArgs> args;
	ComPtr<ICoreWebView2Deferral> deferral;
};

static void
request_complete (WebView2Request *request,
		  gint status_code,
		  const gchar *reason,
		  const gchar *content_type,
		  const guint8 *data,
		  gsize length)
{
	ComPtr<IStream> stream;
	ComPtr<ICoreWebView2WebResourceResponse> response;
	std::wstring headers;

	if (data)
		stream.Attach (SHCreateMemStream (data, (UINT) length));

	if (content_type && *content_type)
		headers = L"Content-Type: " + to_wide (content_type) + L"\r\n";
	/* custom-scheme resources are local content generated by Evolution */
	headers += L"Access-Control-Allow-Origin: *\r\nCache-Control: no-cache";

	if (SUCCEEDED (request->env->CreateWebResourceResponse (
		stream.Get (), status_code, to_wide (reason ? reason : "").c_str (),
		headers.c_str (), &response)))
		request->args->put_Response (response.Get ());

	request->deferral->Complete ();

	delete request;
}

void
webview2_request_respond (WebView2Request *request,
			  gint status_code,
			  const gchar *reason,
			  const gchar *content_type,
			  const guint8 *data,
			  gsize length)
{
	g_return_if_fail (request != NULL);

	request_complete (request, status_code, reason ? reason : "OK", content_type,
		data ? data : (const guint8 *) "", data ? length : 0);
}

void
webview2_request_continue (WebView2Request *request)
{
	g_return_if_fail (request != NULL);

	/* no response set: WebView2 performs the request */
	request->deferral->Complete ();

	delete request;
}

void
webview2_request_fail (WebView2Request *request,
		       gint status_code,
		       const gchar *reason)
{
	g_return_if_fail (request != NULL);

	request_complete (request, status_code, reason ? reason : "Error", "text/plain", NULL, 0);
}

/* ------------------------------------------------------------------------ */
/* Host                                                                     */
/* ------------------------------------------------------------------------ */

struct _WebView2Host {
	WebView2HostCallbacks callbacks;
	gpointer user_data;

	HWND parent = NULL;
	ComPtr<ICoreWebView2Environment> env;
	ComPtr<ICoreWebView2Controller> controller;
	ComPtr<ICoreWebView2> webview;

	bool attaching = false;
	bool closed = false;
	bool visible = true;
	bool dev_tools = false;
	double zoom = 1.0;
	COREWEBVIEW2_COLOR background = { 255, 255, 255, 255 };

	std::vector<std::wstring> document_scripts;
	/* replaceable page script: source, id of the registered copy, and a
	 * generation to drop ids of copies replaced meanwhile */
	std::wstring page_script;
	std::wstring page_script_id;
	guint page_script_generation = 0;
	/* commands issued before the controller is ready */
	std::vector<std::function<void ()>> pending;

	/* the host can be freed from inside a WebView2 callback */
	gint ref_count = 1;
};

static WebView2Host *
host_ref (WebView2Host *host)
{
	host->ref_count++;
	return host;
}

static void
host_unref (WebView2Host *host)
{
	if (--host->ref_count == 0)
		delete host;
}

WebView2Host *
webview2_host_new (const WebView2HostCallbacks *callbacks,
		   gpointer user_data)
{
	WebView2Host *host = new WebView2Host ();

	if (callbacks)
		host->callbacks = *callbacks;
	host->user_data = user_data;

	return host;
}

void
webview2_host_free (WebView2Host *host)
{
	if (!host)
		return;

	host->closed = true;
	host->pending.clear ();

	if (host->controller)
		host->controller->Close ();

	host->controller.Reset ();
	host->webview.Reset ();
	host->env.Reset ();

	host_unref (host);
}

gboolean
webview2_host_is_ready (WebView2Host *host)
{
	return host && host->webview != nullptr;
}

static void host_register_page_script (WebView2Host *host);

/* runs now when ready, otherwise once the controller exists */
static void
host_run (WebView2Host *host,
	  std::function<void ()> command)
{
	if (host->closed)
		return;

	if (host->webview)
		command ();
	else
		host->pending.push_back (std::move (command));
}

static void
host_apply_settings (WebView2Host *host)
{
	ComPtr<ICoreWebView2Settings> settings;

	if (FAILED (host->webview->get_Settings (&settings)))
		return;

	/* Evolution shows its own context menus, status and zoom */
	settings->put_AreDefaultContextMenusEnabled (FALSE);
	settings->put_IsStatusBarEnabled (FALSE);
	settings->put_IsZoomControlEnabled (FALSE);
	settings->put_AreDevToolsEnabled (host->dev_tools ? TRUE : FALSE);
	settings->put_IsWebMessageEnabled (TRUE);
	settings->put_IsScriptEnabled (TRUE);

	ComPtr<ICoreWebView2Settings3> settings3;
	if (SUCCEEDED (settings.As (&settings3)))
		settings3->put_AreBrowserAcceleratorKeysEnabled (FALSE);

	/* Force le schéma de couleurs clair dans WebView2 afin que le mode sombre
	 * de Windows 11 n'active pas un fond noir sous un texte noir dans les
	 * courriels contenant <meta name="color-scheme" content="light dark">. */
	ComPtr<ICoreWebView2_13> webview13;
	if (SUCCEEDED (host->webview.As (&webview13))) {
		ComPtr<ICoreWebView2Profile> profile;
		if (SUCCEEDED (webview13->get_Profile (&profile)) && profile)
			profile->put_PreferredColorScheme (COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);
	}
}

static void
host_connect_events (WebView2Host *host)
{
	ICoreWebView2 *webview = host->webview.Get ();
	EventRegistrationToken token;

	webview->add_NavigationStarting (
		Callback<ICoreWebView2NavigationStartingEventHandler> (
			[host] (ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
				LPWSTR uri = NULL;
				BOOL user_initiated = FALSE;
				gboolean cancel = FALSE;
				gchar *utf8;

				args->get_Uri (&uri);
				args->get_IsUserInitiated (&user_initiated);
				utf8 = take_utf8 (uri);

				if (host->callbacks.navigation_starting)
					host->callbacks.navigation_starting (host->user_data, utf8, user_initiated, &cancel);

				g_free (utf8);

				if (cancel)
					args->put_Cancel (TRUE);

				return S_OK;
			}).Get (), &token);

	webview->add_ContentLoading (
		Callback<ICoreWebView2ContentLoadingEventHandler> (
			[host] (ICoreWebView2 *, ICoreWebView2ContentLoadingEventArgs *) -> HRESULT {
				if (host->callbacks.content_loading)
					host->callbacks.content_loading (host->user_data);
				return S_OK;
			}).Get (), &token);

	webview->add_NavigationCompleted (
		Callback<ICoreWebView2NavigationCompletedEventHandler> (
			[host] (ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
				BOOL success = FALSE;
				COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;

				args->get_IsSuccess (&success);
				args->get_WebErrorStatus (&status);

				if (host->callbacks.navigation_completed)
					host->callbacks.navigation_completed (host->user_data, success, (gint) status);

				return S_OK;
			}).Get (), &token);

	webview->add_DocumentTitleChanged (
		Callback<ICoreWebView2DocumentTitleChangedEventHandler> (
			[host] (ICoreWebView2 *sender, IUnknown *) -> HRESULT {
				LPWSTR title = NULL;
				gchar *utf8;

				sender->get_DocumentTitle (&title);
				utf8 = take_utf8 (title);

				if (host->callbacks.title_changed)
					host->callbacks.title_changed (host->user_data, utf8);

				g_free (utf8);

				return S_OK;
			}).Get (), &token);

	webview->add_WebMessageReceived (
		Callback<ICoreWebView2WebMessageReceivedEventHandler> (
			[host] (ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
				LPWSTR message = NULL;
				gchar *utf8;

				if (FAILED (args->TryGetWebMessageAsString (&message)))
					return S_OK;

				utf8 = take_utf8 (message);

				if (host->callbacks.web_message)
					host->callbacks.web_message (host->user_data, utf8);

				g_free (utf8);

				return S_OK;
			}).Get (), &token);

	webview->add_NewWindowRequested (
		Callback<ICoreWebView2NewWindowRequestedEventHandler> (
			[host] (ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
				LPWSTR uri = NULL;
				BOOL user_initiated = FALSE;
				gboolean handled = FALSE;
				gchar *utf8;

				args->get_Uri (&uri);
				args->get_IsUserInitiated (&user_initiated);
				utf8 = take_utf8 (uri);

				if (host->callbacks.new_window_requested)
					handled = host->callbacks.new_window_requested (host->user_data, utf8, user_initiated);

				g_free (utf8);

				/* never let WebView2 open its own popup window */
				args->put_Handled (TRUE);
				(void) handled;

				return S_OK;
			}).Get (), &token);

	webview->add_WebResourceRequested (
		Callback<ICoreWebView2WebResourceRequestedEventHandler> (
			[host] (ICoreWebView2 *, ICoreWebView2WebResourceRequestedEventArgs *args) -> HRESULT {
				ComPtr<ICoreWebView2WebResourceRequest> req;
				LPWSTR uri = NULL, method = NULL;
				gchar *utf8_uri, *utf8_method;

				if (!host->callbacks.resource_requested || FAILED (args->get_Request (&req)))
					return S_OK;

				req->get_Uri (&uri);
				req->get_Method (&method);
				utf8_uri = take_utf8 (uri);
				utf8_method = take_utf8 (method);

				WebView2Request *request = new WebView2Request ();
				request->env = host->env;
				request->args = args;
				args->GetDeferral (&request->deferral);

				host->callbacks.resource_requested (host->user_data, request, utf8_uri, utf8_method);

				g_free (utf8_uri);
				g_free (utf8_method);

				return S_OK;
			}).Get (), &token);

	webview->add_ProcessFailed (
		Callback<ICoreWebView2ProcessFailedEventHandler> (
			[host] (ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
				COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;

				args->get_ProcessFailedKind (&kind);

				if (host->callbacks.process_failed)
					host->callbacks.process_failed (host->user_data, (gint) kind);

				return S_OK;
			}).Get (), &token);

	ComPtr<ICoreWebView2_11> webview11;
	if (SUCCEEDED (host->webview.As (&webview11))) {
		webview11->add_ContextMenuRequested (
			Callback<ICoreWebView2ContextMenuRequestedEventHandler> (
				[host] (ICoreWebView2 *, ICoreWebView2ContextMenuRequestedEventArgs *args) -> HRESULT {
					ComPtr<ICoreWebView2ContextMenuTarget> target;
					POINT location = { 0, 0 };
					guint flags = WEBVIEW2_TARGET_PAGE;
					gchar *link_uri = NULL, *image_uri = NULL;

					args->get_Location (&location);

					if (SUCCEEDED (args->get_ContextMenuTarget (&target))) {
						BOOL value = FALSE;
						LPWSTR str = NULL;
						COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND kind;

						if (SUCCEEDED (target->get_HasLinkUri (&value)) && value &&
						    SUCCEEDED (target->get_LinkUri (&str))) {
							flags |= WEBVIEW2_TARGET_LINK;
							link_uri = take_utf8 (str);
						}

						if (SUCCEEDED (target->get_Kind (&kind)) &&
						    kind == COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_IMAGE &&
						    SUCCEEDED (target->get_HasSourceUri (&value)) && value &&
						    SUCCEEDED (target->get_SourceUri (&str))) {
							flags |= WEBVIEW2_TARGET_IMAGE;
							image_uri = take_utf8 (str);
						}

						if (SUCCEEDED (target->get_IsEditable (&value)) && value)
							flags |= WEBVIEW2_TARGET_EDITABLE;
						if (SUCCEEDED (target->get_HasSelection (&value)) && value)
							flags |= WEBVIEW2_TARGET_SELECTION;
					}

					if (host->callbacks.context_menu &&
					    host->callbacks.context_menu (host->user_data, location.x, location.y, flags, link_uri, image_uri))
						args->put_Handled (TRUE);

					g_free (link_uri);
					g_free (image_uri);

					return S_OK;
				}).Get (), &token);
	}

	ComPtr<ICoreWebView2_12> webview12;
	if (SUCCEEDED (host->webview.As (&webview12))) {
		webview12->add_StatusBarTextChanged (
			Callback<ICoreWebView2StatusBarTextChangedEventHandler> (
				[host] (ICoreWebView2 *sender, IUnknown *) -> HRESULT {
					ComPtr<ICoreWebView2_12> wv12;
					LPWSTR text = NULL;
					gchar *utf8;

					if (FAILED (sender->QueryInterface (IID_PPV_ARGS (&wv12))) ||
					    FAILED (wv12->get_StatusBarText (&text)))
						return S_OK;

					utf8 = take_utf8 (text);

					if (host->callbacks.status_text_changed)
						host->callbacks.status_text_changed (host->user_data, utf8);

					g_free (utf8);

					return S_OK;
				}).Get (), &token);
	}

	/* messages posted by frames arrive on the frame objects */
	ComPtr<ICoreWebView2_4> webview4;
	if (SUCCEEDED (host->webview.As (&webview4))) {
		webview4->add_FrameCreated (
			Callback<ICoreWebView2FrameCreatedEventHandler> (
				[host] (ICoreWebView2 *, ICoreWebView2FrameCreatedEventArgs *args) -> HRESULT {
					ComPtr<ICoreWebView2Frame> frame;
					ComPtr<ICoreWebView2Frame2> frame2;
					EventRegistrationToken frame_token;

					if (FAILED (args->get_Frame (&frame)) || FAILED (frame.As (&frame2)))
						return S_OK;

					frame2->add_WebMessageReceived (
						Callback<ICoreWebView2FrameWebMessageReceivedEventHandler> (
							[host] (ICoreWebView2Frame *, ICoreWebView2WebMessageReceivedEventArgs *margs) -> HRESULT {
								LPWSTR message = NULL;
								gchar *utf8;

								if (FAILED (margs->TryGetWebMessageAsString (&message)))
									return S_OK;

								utf8 = take_utf8 (message);

								if (!host->closed && host->callbacks.web_message)
									host->callbacks.web_message (host->user_data, utf8);

								g_free (utf8);

								return S_OK;
							}).Get (), &frame_token);

					return S_OK;
				}).Get (), &token);
	}

	host->controller->add_GotFocus (
		Callback<ICoreWebView2FocusChangedEventHandler> (
			[host] (ICoreWebView2Controller *, IUnknown *) -> HRESULT {
				if (host->callbacks.focus_changed)
					host->callbacks.focus_changed (host->user_data, TRUE);
				return S_OK;
			}).Get (), &token);

	host->controller->add_LostFocus (
		Callback<ICoreWebView2FocusChangedEventHandler> (
			[host] (ICoreWebView2Controller *, IUnknown *) -> HRESULT {
				if (host->callbacks.focus_changed)
					host->callbacks.focus_changed (host->user_data, FALSE);
				return S_OK;
			}).Get (), &token);
}

static void
host_controller_created (WebView2Host *host,
			 ICoreWebView2Controller *controller)
{
	host->controller = controller;
	controller->get_CoreWebView2 (&host->webview);

	ComPtr<ICoreWebView2Controller2> controller2;
	if (SUCCEEDED (host->controller.As (&controller2)))
		controller2->put_DefaultBackgroundColor (host->background);

	host->controller->put_ZoomFactor (host->zoom);
	host->controller->put_IsVisible (host->visible ? TRUE : FALSE);
	webview2_host_update_bounds (host);

	host_apply_settings (host);

	/* every custom-scheme request goes to the GTK side, and http(s) ones
	 * too: the GTK side may route them through its own handlers (it
	 * answers with webview2_request_continue() otherwise) */
	for (const auto &scheme : environment ().schemes) {
		std::wstring filter = scheme + L":*";
		host->webview->AddWebResourceRequestedFilter (filter.c_str (), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
	}
	host->webview->AddWebResourceRequestedFilter (L"http://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
	host->webview->AddWebResourceRequestedFilter (L"https://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

	host_connect_events (host);

	for (const auto &script : host->document_scripts)
		host->webview->AddScriptToExecuteOnDocumentCreated (script.c_str (), NULL);

	host_register_page_script (host);

	auto pending = std::move (host->pending);
	host->pending.clear ();
	for (auto &command : pending)
		command ();
}

void
webview2_host_attach (WebView2Host *host,
		      gpointer parent_hwnd)
{
	g_return_if_fail (host != NULL);
	g_return_if_fail (parent_hwnd != NULL);

	host->parent = (HWND) parent_hwnd;

	if (host->attaching || host->controller)
		return;

	host->attaching = true;
	host_ref (host);

	environment_get ([host] (ICoreWebView2Environment *env) {
		if (host->closed || !env) {
			host->attaching = false;
			host_unref (host);
			return;
		}

		host->env = env;

		HRESULT hr = env->CreateCoreWebView2Controller (host->parent,
			Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> (
				[host] (HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
					host->attaching = false;

					if (!host->closed && SUCCEEDED (result) && controller)
						host_controller_created (host, controller);
					else if (FAILED (result))
						g_warning ("WebView2: cannot create the controller (0x%08lx)", (unsigned long) result);
					else if (controller)
						controller->Close ();

					host_unref (host);

					return S_OK;
				}).Get ());

		if (FAILED (hr)) {
			g_warning ("WebView2: CreateCoreWebView2Controller failed (0x%08lx)", (unsigned long) hr);
			host->attaching = false;
			host_unref (host);
		}
	});
}

void
webview2_host_update_bounds (WebView2Host *host)
{
	RECT rect;

	g_return_if_fail (host != NULL);

	if (!host->controller || !host->parent)
		return;

	if (GetClientRect (host->parent, &rect))
		host->controller->put_Bounds (rect);
}

void
webview2_host_set_visible (WebView2Host *host,
			   gboolean visible)
{
	g_return_if_fail (host != NULL);

	host->visible = visible ? true : false;

	if (host->controller)
		host->controller->put_IsVisible (visible ? TRUE : FALSE);
}

void
webview2_host_focus (WebView2Host *host)
{
	g_return_if_fail (host != NULL);

	if (host->controller)
		host->controller->MoveFocus (COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void
webview2_host_navigate (WebView2Host *host,
			const gchar *uri)
{
	g_return_if_fail (host != NULL);
	g_return_if_fail (uri != NULL);

	std::wstring wuri = to_wide (uri);

	host_run (host, [host, wuri] () {
		HRESULT hr = host->webview->Navigate (wuri.c_str ());

		if (FAILED (hr))
			g_warning ("WebView2: Navigate failed (0x%08lx)", (unsigned long) hr);
	});
}

void
webview2_host_reload (WebView2Host *host)
{
	g_return_if_fail (host != NULL);

	host_run (host, [host] () { host->webview->Reload (); });
}

void
webview2_host_stop (WebView2Host *host)
{
	g_return_if_fail (host != NULL);

	if (host->webview)
		host->webview->Stop ();
}

void
webview2_host_set_zoom (WebView2Host *host,
			gdouble zoom_factor)
{
	g_return_if_fail (host != NULL);

	host->zoom = zoom_factor > 0.0 ? zoom_factor : 1.0;

	if (host->controller)
		host->controller->put_ZoomFactor (host->zoom);
}

void
webview2_host_set_background (WebView2Host *host,
			      guint8 red,
			      guint8 green,
			      guint8 blue,
			      guint8 alpha)
{
	ComPtr<ICoreWebView2Controller2> controller2;

	g_return_if_fail (host != NULL);

	/* WebView2 supports only opaque or fully transparent backgrounds */
	host->background = { alpha < 255 ? (BYTE) 0 : (BYTE) 255, red, green, blue };

	if (host->controller && SUCCEEDED (host->controller.As (&controller2)))
		controller2->put_DefaultBackgroundColor (host->background);
}

void
webview2_host_set_dev_tools_enabled (WebView2Host *host,
				     gboolean enabled)
{
	g_return_if_fail (host != NULL);

	host->dev_tools = enabled ? true : false;

	if (host->webview)
		host_apply_settings (host);
}

void
webview2_host_open_dev_tools (WebView2Host *host)
{
	g_return_if_fail (host != NULL);

	host_run (host, [host] () { host->webview->OpenDevToolsWindow (); });
}

void
webview2_host_add_document_script (WebView2Host *host,
				   const gchar *script)
{
	g_return_if_fail (host != NULL);
	g_return_if_fail (script != NULL);

	std::wstring wscript = to_wide (script);

	host->document_scripts.push_back (wscript);

	if (host->webview)
		host->webview->AddScriptToExecuteOnDocumentCreated (wscript.c_str (), NULL);
}

/* (re)registers host->page_script with the web view */
static void
host_register_page_script (WebView2Host *host)
{
	guint generation = ++host->page_script_generation;

	if (!host->page_script_id.empty ()) {
		host->webview->RemoveScriptToExecuteOnDocumentCreated (host->page_script_id.c_str ());
		host->page_script_id.clear ();
	}

	if (host->page_script.empty ())
		return;

	host_ref (host);

	host->webview->AddScriptToExecuteOnDocumentCreated (host->page_script.c_str (),
		Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler> (
			[host, generation] (HRESULT result, LPCWSTR id) -> HRESULT {
				if (SUCCEEDED (result) && id) {
					if (host->closed || !host->webview) {
						/* nothing to do, the web view is gone */
					} else if (generation != host->page_script_generation) {
						/* replaced while being added */
						host->webview->RemoveScriptToExecuteOnDocumentCreated (id);
					} else {
						host->page_script_id = id;
					}
				}

				host_unref (host);

				return S_OK;
			}).Get ());
}

void
webview2_host_set_page_script (WebView2Host *host,
			       const gchar *script)
{
	g_return_if_fail (host != NULL);

	host->page_script = to_wide (script);

	if (host->webview)
		host_register_page_script (host);
}

void
webview2_host_execute_script (WebView2Host *host,
			      const gchar *script,
			      WebView2ScriptCallback callback,
			      gpointer user_data)
{
	g_return_if_fail (host != NULL);
	g_return_if_fail (script != NULL);

	std::wstring wscript = to_wide (script);

	if (host->closed) {
		if (callback)
			callback (NULL, "The web view was closed", user_data);
		return;
	}

	host_run (host, [host, wscript, callback, user_data] () {
		HRESULT hr = host->webview->ExecuteScript (wscript.c_str (),
			Callback<ICoreWebView2ExecuteScriptCompletedHandler> (
				[callback, user_data] (HRESULT result, LPCWSTR json) -> HRESULT {
					if (!callback)
						return S_OK;

					if (FAILED (result) || !json) {
						gchar *msg = g_strdup_printf ("ExecuteScript failed (0x%08lx)", (unsigned long) result);
						callback (NULL, msg, user_data);
						g_free (msg);
					} else {
						gchar *utf8 = g_utf16_to_utf8 (reinterpret_cast<const gunichar2 *> (json), -1, NULL, NULL, NULL);
						callback (utf8 ? utf8 : "null", NULL, user_data);
						g_free (utf8);
					}

					return S_OK;
				}).Get ());

		if (FAILED (hr) && callback)
			callback (NULL, "ExecuteScript could not be started", user_data);
	});
}
