/*
 * JavaScriptCore-GLib compatibility shim for Windows (WebView2 backend).
 *
 * Subset of the javascriptcoregtk-4.1 API: JSCContext, JSCValue and
 * JSCException. WebView2 returns script results as JSON, so a JSCValue here
 * is a JSON value (json-glib node) attached to a context, not a live
 * reference into a JavaScript heap: objects are snapshots and functions
 * cannot be called.
 */

#ifndef JSC_SHIM_H
#define JSC_SHIM_H

#define __JSC_H_INSIDE__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _JSCContext		JSCContext;
typedef struct _JSCContextClass		JSCContextClass;
typedef struct _JSCValue		JSCValue;
typedef struct _JSCValueClass		JSCValueClass;
typedef struct _JSCException		JSCException;
typedef struct _JSCExceptionClass	JSCExceptionClass;

#define JSC_MAJOR_VERSION 2
#define JSC_MINOR_VERSION 44
#define JSC_MICRO_VERSION 0

#define JSC_CHECK_VERSION(major, minor, micro) \
	(JSC_MAJOR_VERSION > (major) || \
	 (JSC_MAJOR_VERSION == (major) && JSC_MINOR_VERSION > (minor)) || \
	 (JSC_MAJOR_VERSION == (major) && JSC_MINOR_VERSION == (minor) && \
	  JSC_MICRO_VERSION >= (micro)))

guint		jsc_get_major_version		(void);
guint		jsc_get_minor_version		(void);
guint		jsc_get_micro_version		(void);

/* JSCContext */

#define JSC_TYPE_CONTEXT		(jsc_context_get_type ())
#define JSC_CONTEXT(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), JSC_TYPE_CONTEXT, JSCContext))
#define JSC_CONTEXT_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), JSC_TYPE_CONTEXT, JSCContextClass))
#define JSC_IS_CONTEXT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), JSC_TYPE_CONTEXT))
#define JSC_IS_CONTEXT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), JSC_TYPE_CONTEXT))
#define JSC_CONTEXT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), JSC_TYPE_CONTEXT, JSCContextClass))

GType		jsc_context_get_type		(void);
JSCContext *	jsc_context_new			(void);
JSCContext *	jsc_context_get_current		(void);
JSCException *	jsc_context_get_exception	(JSCContext *context);
void		jsc_context_throw		(JSCContext *context,
						 const char *error_message);
void		jsc_context_throw_exception	(JSCContext *context,
						 JSCException *exception);
void		jsc_context_clear_exception	(JSCContext *context);
JSCValue *	jsc_context_evaluate		(JSCContext *context,
						 const char *code,
						 gssize length);
JSCValue *	jsc_context_get_global_object	(JSCContext *context);

/* JSCException */

#define JSC_TYPE_EXCEPTION		(jsc_exception_get_type ())
#define JSC_EXCEPTION(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), JSC_TYPE_EXCEPTION, JSCException))
#define JSC_EXCEPTION_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), JSC_TYPE_EXCEPTION, JSCExceptionClass))
#define JSC_IS_EXCEPTION(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), JSC_TYPE_EXCEPTION))
#define JSC_IS_EXCEPTION_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), JSC_TYPE_EXCEPTION))
#define JSC_EXCEPTION_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), JSC_TYPE_EXCEPTION, JSCExceptionClass))

GType		jsc_exception_get_type		(void);
JSCException *	jsc_exception_new		(JSCContext *context,
						 const char *message);
JSCException *	jsc_exception_new_with_name	(JSCContext *context,
						 const char *name,
						 const char *message);
const char *	jsc_exception_get_name		(JSCException *exception);
const char *	jsc_exception_get_message	(JSCException *exception);
guint		jsc_exception_get_line_number	(JSCException *exception);
guint		jsc_exception_get_column_number	(JSCException *exception);
const char *	jsc_exception_get_source_uri	(JSCException *exception);
const char *	jsc_exception_get_backtrace_string
						(JSCException *exception);
char *		jsc_exception_to_string		(JSCException *exception);
char *		jsc_exception_report		(JSCException *exception);

/* JSCValue */

#define JSC_TYPE_VALUE			(jsc_value_get_type ())
#define JSC_VALUE(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), JSC_TYPE_VALUE, JSCValue))
#define JSC_VALUE_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), JSC_TYPE_VALUE, JSCValueClass))
#define JSC_IS_VALUE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), JSC_TYPE_VALUE))
#define JSC_IS_VALUE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), JSC_TYPE_VALUE))
#define JSC_VALUE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), JSC_TYPE_VALUE, JSCValueClass))

GType		jsc_value_get_type		(void);
JSCContext *	jsc_value_get_context		(JSCValue *value);

JSCValue *	jsc_value_new_undefined		(JSCContext *context);
JSCValue *	jsc_value_new_null		(JSCContext *context);
JSCValue *	jsc_value_new_number		(JSCContext *context,
						 double number);
JSCValue *	jsc_value_new_boolean		(JSCContext *context,
						 gboolean value);
JSCValue *	jsc_value_new_string		(JSCContext *context,
						 const char *string);
JSCValue *	jsc_value_new_string_from_bytes	(JSCContext *context,
						 GBytes *bytes);
JSCValue *	jsc_value_new_array_from_garray	(JSCContext *context,
						 GPtrArray *array);
JSCValue *	jsc_value_new_array_from_strv	(JSCContext *context,
						 const char * const *strv);
JSCValue *	jsc_value_new_from_json		(JSCContext *context,
						 const char *json);

gboolean	jsc_value_is_undefined		(JSCValue *value);
gboolean	jsc_value_is_null		(JSCValue *value);
gboolean	jsc_value_is_number		(JSCValue *value);
gboolean	jsc_value_is_boolean		(JSCValue *value);
gboolean	jsc_value_is_string		(JSCValue *value);
gboolean	jsc_value_is_array		(JSCValue *value);
gboolean	jsc_value_is_object		(JSCValue *value);
gboolean	jsc_value_is_function		(JSCValue *value);
gboolean	jsc_value_is_constructor	(JSCValue *value);

double		jsc_value_to_double		(JSCValue *value);
gint32		jsc_value_to_int32		(JSCValue *value);
gboolean	jsc_value_to_boolean		(JSCValue *value);
char *		jsc_value_to_string		(JSCValue *value);
GBytes *	jsc_value_to_string_as_bytes	(JSCValue *value);
char *		jsc_value_to_json		(JSCValue *value,
						 guint indent);

gboolean	jsc_value_object_has_property	(JSCValue *value,
						 const char *name);
JSCValue *	jsc_value_object_get_property	(JSCValue *value,
						 const char *name);
void		jsc_value_object_set_property	(JSCValue *value,
						 const char *name,
						 JSCValue *property);
gboolean	jsc_value_object_delete_property
						(JSCValue *value,
						 const char *name);
gchar **	jsc_value_object_enumerate_properties
						(JSCValue *value);
JSCValue *	jsc_value_object_get_property_at_index
						(JSCValue *value,
						 guint index);
void		jsc_value_object_set_property_at_index
						(JSCValue *value,
						 guint index,
						 JSCValue *property);

G_END_DECLS

#undef __JSC_H_INSIDE__

#endif /* JSC_SHIM_H */
