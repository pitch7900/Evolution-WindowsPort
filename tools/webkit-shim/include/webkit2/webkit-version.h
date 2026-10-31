/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * Version information. The shim advertises the WebKitGTK API level it mimics
 * (webkit2gtk-4.1, 2.44), not the version of the underlying WebView2 runtime.
 */

#ifndef WEBKIT_SHIM_VERSION_H
#define WEBKIT_SHIM_VERSION_H

#include <glib.h>

G_BEGIN_DECLS

#define WEBKIT_MAJOR_VERSION 2
#define WEBKIT_MINOR_VERSION 44
#define WEBKIT_MICRO_VERSION 0

#define WEBKIT_CHECK_VERSION(major, minor, micro) \
	(WEBKIT_MAJOR_VERSION > (major) || \
	 (WEBKIT_MAJOR_VERSION == (major) && WEBKIT_MINOR_VERSION > (minor)) || \
	 (WEBKIT_MAJOR_VERSION == (major) && WEBKIT_MINOR_VERSION == (minor) && \
	  WEBKIT_MICRO_VERSION >= (micro)))

guint		webkit_get_major_version	(void);
guint		webkit_get_minor_version	(void);
guint		webkit_get_micro_version	(void);

G_END_DECLS

#endif /* WEBKIT_SHIM_VERSION_H */
