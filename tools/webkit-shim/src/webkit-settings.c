/*
 * WebKitGTK compatibility shim for Windows (WebView2 backend).
 *
 * WebKitSettings: table-driven property bag with WebKitGTK's property names
 * and defaults. Accessors go through the GObject properties, so that
 * "notify::" handlers and GBindings (EWebView binds "enable-caret-browsing")
 * work the same whichever way a value is changed.
 *
 * TODO WebView2: the backend applies the relevant values to
 * ICoreWebView2Settings (developer extras -> AreDevToolsEnabled,
 * enable-javascript -> IsScriptEnabled, ...) and the fonts/charset through
 * user style sheets; it listens to "notify" to follow changes.
 */

#include <stdarg.h>

#include "webkit-shim-private.h"

typedef enum {
	SETTING_BOOLEAN,
	SETTING_UINT,
	SETTING_STRING,
	SETTING_ENUM
} SettingType;

typedef struct {
	const gchar *name;
	SettingType type;
	gboolean default_boolean;
	guint default_uint;
	const gchar *default_string;
} SettingInfo;

#define BOOL_SETTING(name, def)	{ name, SETTING_BOOLEAN, def, 0, NULL }
#define UINT_SETTING(name, def)	{ name, SETTING_UINT, FALSE, def, NULL }
#define STR_SETTING(name, def)	{ name, SETTING_STRING, FALSE, 0, def }
#define ENUM_SETTING(name, def)	{ name, SETTING_ENUM, FALSE, def, NULL }

#define DEFAULT_USER_AGENT \
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15"

/* Property ids are the index in this table plus one */
static const SettingInfo settings_info[] = {
	BOOL_SETTING ("enable-javascript", TRUE),
	BOOL_SETTING ("enable-javascript-markup", TRUE),
	BOOL_SETTING ("auto-load-images", TRUE),
	BOOL_SETTING ("load-icons-ignoring-image-load-setting", FALSE),
	BOOL_SETTING ("enable-offline-web-application-cache", TRUE),
	BOOL_SETTING ("enable-html5-local-storage", TRUE),
	BOOL_SETTING ("enable-html5-database", TRUE),
	BOOL_SETTING ("enable-xss-auditor", TRUE),
	BOOL_SETTING ("enable-frame-flattening", FALSE),
	BOOL_SETTING ("enable-plugins", FALSE),
	BOOL_SETTING ("enable-java", FALSE),
	BOOL_SETTING ("javascript-can-open-windows-automatically", FALSE),
	BOOL_SETTING ("javascript-can-access-clipboard", FALSE),
	BOOL_SETTING ("enable-hyperlink-auditing", TRUE),
	BOOL_SETTING ("enable-fullscreen", TRUE),
	BOOL_SETTING ("print-backgrounds", TRUE),
	BOOL_SETTING ("enable-webaudio", TRUE),
	BOOL_SETTING ("enable-webgl", TRUE),
	BOOL_SETTING ("allow-modal-dialogs", FALSE),
	BOOL_SETTING ("zoom-text-only", FALSE),
	BOOL_SETTING ("media-playback-requires-user-gesture", FALSE),
	BOOL_SETTING ("media-playback-allows-inline", TRUE),
	BOOL_SETTING ("draw-compositing-indicators", FALSE),
	BOOL_SETTING ("enable-site-specific-quirks", TRUE),
	BOOL_SETTING ("enable-page-cache", TRUE),
	BOOL_SETTING ("enable-smooth-scrolling", TRUE),
	BOOL_SETTING ("enable-accelerated-2d-canvas", FALSE),
	BOOL_SETTING ("enable-write-console-messages-to-stdout", FALSE),
	BOOL_SETTING ("enable-media-stream", FALSE),
	BOOL_SETTING ("enable-mock-capture-devices", FALSE),
	BOOL_SETTING ("enable-spatial-navigation", FALSE),
	BOOL_SETTING ("enable-mediasource", TRUE),
	BOOL_SETTING ("enable-encrypted-media", FALSE),
	BOOL_SETTING ("enable-media-capabilities", FALSE),
	BOOL_SETTING ("allow-file-access-from-file-urls", FALSE),
	BOOL_SETTING ("allow-universal-access-from-file-urls", FALSE),
	BOOL_SETTING ("allow-top-navigation-to-data-urls", FALSE),
	BOOL_SETTING ("enable-developer-extras", FALSE),
	BOOL_SETTING ("enable-resizable-text-areas", TRUE),
	BOOL_SETTING ("enable-tabs-to-links", TRUE),
	BOOL_SETTING ("enable-dns-prefetching", FALSE),
	BOOL_SETTING ("enable-caret-browsing", FALSE),
	BOOL_SETTING ("enable-media", TRUE),
	BOOL_SETTING ("enable-back-forward-navigation-gestures", FALSE),
	BOOL_SETTING ("disable-web-security", FALSE),
	BOOL_SETTING ("enable-webrtc", FALSE),
	UINT_SETTING ("default-font-size", 16),
	UINT_SETTING ("default-monospace-font-size", 13),
	UINT_SETTING ("minimum-font-size", 0),
	STR_SETTING ("default-font-family", "sans-serif"),
	STR_SETTING ("monospace-font-family", "monospace"),
	STR_SETTING ("serif-font-family", "serif"),
	STR_SETTING ("sans-serif-font-family", "sans-serif"),
	STR_SETTING ("cursive-font-family", "serif"),
	STR_SETTING ("fantasy-font-family", "serif"),
	STR_SETTING ("pictograph-font-family", "serif"),
	STR_SETTING ("default-charset", "iso-8859-1"),
	STR_SETTING ("user-agent", DEFAULT_USER_AGENT),
	ENUM_SETTING ("hardware-acceleration-policy", WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND)
};

#define N_SETTINGS G_N_ELEMENTS (settings_info)

struct _WebKitSettings {
	GObject parent;

	GValue values[N_SETTINGS];
};

struct _WebKitSettingsClass {
	GObjectClass parent_class;
};

G_DEFINE_TYPE (WebKitSettings, webkit_settings, G_TYPE_OBJECT)

static GParamSpec *settings_pspecs[N_SETTINGS + 1];

static void
webkit_settings_set_property (GObject *object,
			      guint property_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	WebKitSettings *settings = WEBKIT_SETTINGS (object);

	if (property_id < 1 || property_id > N_SETTINGS) {
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		return;
	}

	/* user-agent: NULL or empty restores the default, as in WebKitGTK */
	if (settings_info[property_id - 1].type == SETTING_STRING &&
	    g_str_equal (pspec->name, "user-agent") &&
	    (!g_value_get_string (value) || !*g_value_get_string (value))) {
		g_value_set_string (&settings->values[property_id - 1], DEFAULT_USER_AGENT);
		return;
	}

	g_value_copy (value, &settings->values[property_id - 1]);
}

static void
webkit_settings_get_property (GObject *object,
			      guint property_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	WebKitSettings *settings = WEBKIT_SETTINGS (object);

	if (property_id < 1 || property_id > N_SETTINGS) {
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		return;
	}

	g_value_copy (&settings->values[property_id - 1], value);
}

static void
webkit_settings_finalize (GObject *object)
{
	WebKitSettings *settings = WEBKIT_SETTINGS (object);
	guint ii;

	for (ii = 0; ii < N_SETTINGS; ii++)
		g_value_unset (&settings->values[ii]);

	G_OBJECT_CLASS (webkit_settings_parent_class)->finalize (object);
}

static void
webkit_settings_class_init (WebKitSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	guint ii;

	object_class->set_property = webkit_settings_set_property;
	object_class->get_property = webkit_settings_get_property;
	object_class->finalize = webkit_settings_finalize;

	for (ii = 0; ii < N_SETTINGS; ii++) {
		const SettingInfo *info = &settings_info[ii];
		GParamFlags flags = G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS;
		GParamSpec *pspec = NULL;

		switch (info->type) {
		case SETTING_BOOLEAN:
			pspec = g_param_spec_boolean (info->name, NULL, NULL, info->default_boolean, flags);
			break;
		case SETTING_UINT:
			pspec = g_param_spec_uint (info->name, NULL, NULL, 0, G_MAXUINT, info->default_uint, flags);
			break;
		case SETTING_STRING:
			pspec = g_param_spec_string (info->name, NULL, NULL, info->default_string, flags);
			break;
		case SETTING_ENUM:
			pspec = g_param_spec_enum (info->name, NULL, NULL,
				WEBKIT_TYPE_HARDWARE_ACCELERATION_POLICY, info->default_uint, flags);
			break;
		}

		settings_pspecs[ii + 1] = pspec;
	}

	g_object_class_install_properties (object_class, N_SETTINGS + 1, settings_pspecs);
}

static void
webkit_settings_init (WebKitSettings *settings)
{
	guint ii;

	for (ii = 0; ii < N_SETTINGS; ii++)
		g_value_init (&settings->values[ii], G_PARAM_SPEC_VALUE_TYPE (settings_pspecs[ii + 1]));
}

WebKitSettings *
webkit_settings_new (void)
{
	return g_object_new (WEBKIT_TYPE_SETTINGS, NULL);
}

WebKitSettings *
webkit_settings_new_with_settings (const gchar *first_setting_name,
				   ...)
{
	WebKitSettings *settings;
	va_list args;

	va_start (args, first_setting_name);
	settings = WEBKIT_SETTINGS (g_object_new_valist (WEBKIT_TYPE_SETTINGS, first_setting_name, args));
	va_end (args);

	return settings;
}

/* WebKitGTK converts with the screen DPI; WebView2 works in CSS pixels at
   96 DPI and scales itself, so the CSS conversion is the right one here. */
guint32
webkit_settings_font_size_to_points (guint32 pixels)
{
	return (guint32) (pixels * 72.0 / 96.0 + 0.5);
}

guint32
webkit_settings_font_size_to_pixels (guint32 points)
{
	return (guint32) (points * 96.0 / 72.0 + 0.5);
}

/* Typed accessors, all routed through the GObject properties */

static const GValue *
webkit_settings_get_value (WebKitSettings *settings,
			   const gchar *name)
{
	guint ii;

	for (ii = 0; ii < N_SETTINGS; ii++) {
		if (g_str_equal (settings_info[ii].name, name))
			return &settings->values[ii];
	}

	g_warn_if_reached ();

	return NULL;
}

static gboolean
get_boolean (WebKitSettings *settings,
	     const gchar *name)
{
	const GValue *value;

	g_return_val_if_fail (WEBKIT_IS_SETTINGS (settings), FALSE);

	value = webkit_settings_get_value (settings, name);

	return value ? g_value_get_boolean (value) : FALSE;
}

static guint32
get_uint (WebKitSettings *settings,
	  const gchar *name)
{
	const GValue *value;

	g_return_val_if_fail (WEBKIT_IS_SETTINGS (settings), 0);

	value = webkit_settings_get_value (settings, name);

	return value ? g_value_get_uint (value) : 0;
}

static const gchar *
get_string (WebKitSettings *settings,
	    const gchar *name)
{
	const GValue *value;

	g_return_val_if_fail (WEBKIT_IS_SETTINGS (settings), NULL);

	value = webkit_settings_get_value (settings, name);

	return value ? g_value_get_string (value) : NULL;
}

#define DEFINE_BOOLEAN_ACCESSORS(func_name, prop_name) \
gboolean \
webkit_settings_get_##func_name (WebKitSettings *settings) \
{ \
	return get_boolean (settings, prop_name); \
} \
void \
webkit_settings_set_##func_name (WebKitSettings *settings, \
				 gboolean enabled) \
{ \
	g_return_if_fail (WEBKIT_IS_SETTINGS (settings)); \
	if (get_boolean (settings, prop_name) != (enabled ? TRUE : FALSE)) \
		g_object_set (settings, prop_name, enabled ? TRUE : FALSE, NULL); \
}

#define DEFINE_UINT_ACCESSORS(func_name, prop_name) \
guint32 \
webkit_settings_get_##func_name (WebKitSettings *settings) \
{ \
	return get_uint (settings, prop_name); \
} \
void \
webkit_settings_set_##func_name (WebKitSettings *settings, \
				 guint32 font_size) \
{ \
	g_return_if_fail (WEBKIT_IS_SETTINGS (settings)); \
	if (get_uint (settings, prop_name) != font_size) \
		g_object_set (settings, prop_name, (guint) font_size, NULL); \
}

#define DEFINE_STRING_ACCESSORS(func_name, prop_name) \
const gchar * \
webkit_settings_get_##func_name (WebKitSettings *settings) \
{ \
	return get_string (settings, prop_name); \
} \
void \
webkit_settings_set_##func_name (WebKitSettings *settings, \
				 const gchar *value) \
{ \
	g_return_if_fail (WEBKIT_IS_SETTINGS (settings)); \
	if (g_strcmp0 (get_string (settings, prop_name), value) != 0) \
		g_object_set (settings, prop_name, value, NULL); \
}

DEFINE_BOOLEAN_ACCESSORS (enable_javascript, "enable-javascript")
DEFINE_BOOLEAN_ACCESSORS (auto_load_images, "auto-load-images")
DEFINE_BOOLEAN_ACCESSORS (enable_developer_extras, "enable-developer-extras")
DEFINE_BOOLEAN_ACCESSORS (enable_write_console_messages_to_stdout, "enable-write-console-messages-to-stdout")
DEFINE_BOOLEAN_ACCESSORS (allow_file_access_from_file_urls, "allow-file-access-from-file-urls")
DEFINE_BOOLEAN_ACCESSORS (enable_caret_browsing, "enable-caret-browsing")

DEFINE_UINT_ACCESSORS (minimum_font_size, "minimum-font-size")
DEFINE_UINT_ACCESSORS (default_font_size, "default-font-size")
DEFINE_UINT_ACCESSORS (default_monospace_font_size, "default-monospace-font-size")

DEFINE_STRING_ACCESSORS (default_font_family, "default-font-family")
DEFINE_STRING_ACCESSORS (monospace_font_family, "monospace-font-family")
DEFINE_STRING_ACCESSORS (default_charset, "default-charset")
DEFINE_STRING_ACCESSORS (user_agent, "user-agent")

WebKitHardwareAccelerationPolicy
webkit_settings_get_hardware_acceleration_policy (WebKitSettings *settings)
{
	const GValue *value;

	g_return_val_if_fail (WEBKIT_IS_SETTINGS (settings), WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND);

	value = webkit_settings_get_value (settings, "hardware-acceleration-policy");

	return value ? (WebKitHardwareAccelerationPolicy) g_value_get_enum (value) : WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND;
}

void
webkit_settings_set_hardware_acceleration_policy (WebKitSettings *settings,
						  WebKitHardwareAccelerationPolicy policy)
{
	g_return_if_fail (WEBKIT_IS_SETTINGS (settings));

	if (webkit_settings_get_hardware_acceleration_policy (settings) != policy)
		g_object_set (settings, "hardware-acceleration-policy", policy, NULL);
}
