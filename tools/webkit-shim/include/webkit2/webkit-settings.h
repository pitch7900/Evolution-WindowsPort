/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitSettings: a property bag with the property names and defaults of
 * WebKitGTK. Values are stored; mapping them to ICoreWebView2Settings is the
 * job of the WebView2 backend.
 */

#ifndef WEBKIT_SHIM_SETTINGS_H
#define WEBKIT_SHIM_SETTINGS_H

#include <webkit2/webkit-forward.h>
#include <webkit2/webkit-enums.h>

G_BEGIN_DECLS

#define WEBKIT_TYPE_SETTINGS		(webkit_settings_get_type ())
#define WEBKIT_SETTINGS(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), WEBKIT_TYPE_SETTINGS, WebKitSettings))
#define WEBKIT_SETTINGS_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), WEBKIT_TYPE_SETTINGS, WebKitSettingsClass))
#define WEBKIT_IS_SETTINGS(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), WEBKIT_TYPE_SETTINGS))
#define WEBKIT_IS_SETTINGS_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), WEBKIT_TYPE_SETTINGS))
#define WEBKIT_SETTINGS_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), WEBKIT_TYPE_SETTINGS, WebKitSettingsClass))

GType		webkit_settings_get_type	(void);

WebKitSettings *webkit_settings_new		(void);
WebKitSettings *webkit_settings_new_with_settings
						(const gchar *first_setting_name,
						 ...) G_GNUC_NULL_TERMINATED;

guint32		webkit_settings_font_size_to_points
						(guint32 pixels);
guint32		webkit_settings_font_size_to_pixels
						(guint32 points);

gboolean	webkit_settings_get_enable_javascript
						(WebKitSettings *settings);
void		webkit_settings_set_enable_javascript
						(WebKitSettings *settings,
						 gboolean enabled);
gboolean	webkit_settings_get_auto_load_images
						(WebKitSettings *settings);
void		webkit_settings_set_auto_load_images
						(WebKitSettings *settings,
						 gboolean enabled);
gboolean	webkit_settings_get_enable_developer_extras
						(WebKitSettings *settings);
void		webkit_settings_set_enable_developer_extras
						(WebKitSettings *settings,
						 gboolean enabled);
gboolean	webkit_settings_get_enable_write_console_messages_to_stdout
						(WebKitSettings *settings);
void		webkit_settings_set_enable_write_console_messages_to_stdout
						(WebKitSettings *settings,
						 gboolean enabled);
gboolean	webkit_settings_get_allow_file_access_from_file_urls
						(WebKitSettings *settings);
void		webkit_settings_set_allow_file_access_from_file_urls
						(WebKitSettings *settings,
						 gboolean allowed);
gboolean	webkit_settings_get_enable_caret_browsing
						(WebKitSettings *settings);
void		webkit_settings_set_enable_caret_browsing
						(WebKitSettings *settings,
						 gboolean enabled);
guint32		webkit_settings_get_minimum_font_size
						(WebKitSettings *settings);
void		webkit_settings_set_minimum_font_size
						(WebKitSettings *settings,
						 guint32 font_size);
guint32		webkit_settings_get_default_font_size
						(WebKitSettings *settings);
void		webkit_settings_set_default_font_size
						(WebKitSettings *settings,
						 guint32 font_size);
guint32		webkit_settings_get_default_monospace_font_size
						(WebKitSettings *settings);
void		webkit_settings_set_default_monospace_font_size
						(WebKitSettings *settings,
						 guint32 font_size);
const gchar *	webkit_settings_get_default_font_family
						(WebKitSettings *settings);
void		webkit_settings_set_default_font_family
						(WebKitSettings *settings,
						 const gchar *default_font_family);
const gchar *	webkit_settings_get_monospace_font_family
						(WebKitSettings *settings);
void		webkit_settings_set_monospace_font_family
						(WebKitSettings *settings,
						 const gchar *monospace_font_family);
const gchar *	webkit_settings_get_default_charset
						(WebKitSettings *settings);
void		webkit_settings_set_default_charset
						(WebKitSettings *settings,
						 const gchar *default_charset);
const gchar *	webkit_settings_get_user_agent	(WebKitSettings *settings);
void		webkit_settings_set_user_agent	(WebKitSettings *settings,
						 const gchar *user_agent);
WebKitHardwareAccelerationPolicy
		webkit_settings_get_hardware_acceleration_policy
						(WebKitSettings *settings);
void		webkit_settings_set_hardware_acceleration_policy
						(WebKitSettings *settings,
						 WebKitHardwareAccelerationPolicy policy);

G_END_DECLS

#endif /* WEBKIT_SHIM_SETTINGS_H */
