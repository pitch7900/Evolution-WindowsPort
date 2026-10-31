/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * Umbrella header replacing WebKitGTK's <webkit2/webkit2.h>. It declares the
 * subset of the webkit2gtk-4.1 API used by Evolution, with the exact
 * WebKitGTK signatures; see win32/msvc/webkit-api-usage.txt in the Evolution
 * sources for the inventory.
 */

#ifndef WEBKIT_SHIM_WEBKIT2_H
#define WEBKIT_SHIM_WEBKIT2_H

#define __WEBKIT2_H_INSIDE__

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <jsc/jsc.h>

#include <webkit2/webkit-version.h>
#include <webkit2/webkit-forward.h>
#include <webkit2/webkit-enums.h>
#include <webkit2/webkit-settings.h>
#include <webkit2/webkit-navigation.h>
#include <webkit2/webkit-user-content.h>
#include <webkit2/webkit-web-context.h>
#include <webkit2/webkit-misc.h>
#include <webkit2/webkit-web-view.h>

#undef __WEBKIT2_H_INSIDE__

#endif /* WEBKIT_SHIM_WEBKIT2_H */
