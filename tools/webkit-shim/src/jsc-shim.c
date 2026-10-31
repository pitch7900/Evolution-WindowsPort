/*
 * JavaScriptCore-GLib compatibility shim for Windows (WebView2 backend).
 *
 * WebView2's ExecuteScript() and WebMessageReceived deliver values as JSON,
 * so a JSCValue is a JSON value (JsonNode) bound to a JSCContext:
 *   - undefined is represented by a NULL node;
 *   - objects and arrays are snapshots (setting a property modifies the
 *     snapshot only), functions are not representable.
 * This is enough for Evolution, which only reads results of scripts and
 * script messages.
 */

#include <math.h>
#include <string.h>

#include "webkit-shim-private.h"

guint
jsc_get_major_version (void)
{
	return JSC_MAJOR_VERSION;
}

guint
jsc_get_minor_version (void)
{
	return JSC_MINOR_VERSION;
}

guint
jsc_get_micro_version (void)
{
	return JSC_MICRO_VERSION;
}

/* ------------------------------------------------------------------------ */
/* JSCException                                                             */
/* ------------------------------------------------------------------------ */

struct _JSCException {
	GObject parent;

	gchar *name;
	gchar *message;
	guint line_number;
	guint column_number;
	gchar *source_uri;
};

struct _JSCExceptionClass {
	GObjectClass parent_class;
};

G_DEFINE_TYPE (JSCException, jsc_exception, G_TYPE_OBJECT)

static void
jsc_exception_finalize (GObject *object)
{
	JSCException *exception = JSC_EXCEPTION (object);

	g_free (exception->name);
	g_free (exception->message);
	g_free (exception->source_uri);

	G_OBJECT_CLASS (jsc_exception_parent_class)->finalize (object);
}

static void
jsc_exception_class_init (JSCExceptionClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = jsc_exception_finalize;
}

static void
jsc_exception_init (JSCException *exception)
{
}

JSCException *
jsc_exception_new_with_name (JSCContext *context,
			     const char *name,
			     const char *message)
{
	JSCException *exception;

	g_return_val_if_fail (JSC_IS_CONTEXT (context), NULL);

	exception = g_object_new (JSC_TYPE_EXCEPTION, NULL);
	exception->name = g_strdup (name ? name : "Error");
	exception->message = g_strdup (message ? message : "");

	return exception;
}

JSCException *
jsc_exception_new (JSCContext *context,
		   const char *message)
{
	return jsc_exception_new_with_name (context, NULL, message);
}

const char *
jsc_exception_get_name (JSCException *exception)
{
	g_return_val_if_fail (JSC_IS_EXCEPTION (exception), NULL);

	return exception->name;
}

const char *
jsc_exception_get_message (JSCException *exception)
{
	g_return_val_if_fail (JSC_IS_EXCEPTION (exception), NULL);

	return exception->message;
}

guint
jsc_exception_get_line_number (JSCException *exception)
{
	g_return_val_if_fail (JSC_IS_EXCEPTION (exception), 0);

	return exception->line_number;
}

guint
jsc_exception_get_column_number (JSCException *exception)
{
	g_return_val_if_fail (JSC_IS_EXCEPTION (exception), 0);

	return exception->column_number;
}

const char *
jsc_exception_get_source_uri (JSCException *exception)
{
	g_return_val_if_fail (JSC_IS_EXCEPTION (exception), NULL);

	return exception->source_uri;
}

const char *
jsc_exception_get_backtrace_string (JSCException *exception)
{
	g_return_val_if_fail (JSC_IS_EXCEPTION (exception), NULL);

	return NULL;
}

char *
jsc_exception_to_string (JSCException *exception)
{
	g_return_val_if_fail (JSC_IS_EXCEPTION (exception), NULL);

	return g_strdup_printf ("%s: %s", exception->name, exception->message);
}

char *
jsc_exception_report (JSCException *exception)
{
	g_return_val_if_fail (JSC_IS_EXCEPTION (exception), NULL);

	return g_strdup_printf ("%s:%u:%u: %s: %s\n",
		exception->source_uri ? exception->source_uri : "",
		exception->line_number, exception->column_number,
		exception->name, exception->message);
}

/* ------------------------------------------------------------------------ */
/* JSCContext                                                               */
/* ------------------------------------------------------------------------ */

struct _JSCContext {
	GObject parent;

	JSCException *exception;
};

struct _JSCContextClass {
	GObjectClass parent_class;
};

G_DEFINE_TYPE (JSCContext, jsc_context, G_TYPE_OBJECT)

static void
jsc_context_dispose (GObject *object)
{
	JSCContext *context = JSC_CONTEXT (object);

	g_clear_object (&context->exception);

	G_OBJECT_CLASS (jsc_context_parent_class)->dispose (object);
}

static void
jsc_context_class_init (JSCContextClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = jsc_context_dispose;
}

static void
jsc_context_init (JSCContext *context)
{
}

JSCContext *
jsc_context_new (void)
{
	return g_object_new (JSC_TYPE_CONTEXT, NULL);
}

JSCContext *
jsc_context_get_current (void)
{
	/* There is no native JavaScript callback into the UI process with
	   WebView2, thus never a current context. */
	return NULL;
}

JSCException *
jsc_context_get_exception (JSCContext *context)
{
	g_return_val_if_fail (JSC_IS_CONTEXT (context), NULL);

	return context->exception;
}

void
jsc_context_throw_exception (JSCContext *context,
			     JSCException *exception)
{
	g_return_if_fail (JSC_IS_CONTEXT (context));
	g_return_if_fail (JSC_IS_EXCEPTION (exception));

	g_set_object (&context->exception, exception);
}

void
jsc_context_throw (JSCContext *context,
		   const char *error_message)
{
	JSCException *exception;

	g_return_if_fail (JSC_IS_CONTEXT (context));

	exception = jsc_exception_new (context, error_message);
	jsc_context_throw_exception (context, exception);
	g_object_unref (exception);
}

void
jsc_context_clear_exception (JSCContext *context)
{
	g_return_if_fail (JSC_IS_CONTEXT (context));

	g_clear_object (&context->exception);
}

JSCValue *
jsc_context_evaluate (JSCContext *context,
		      const char *code,
		      gssize length)
{
	g_return_val_if_fail (JSC_IS_CONTEXT (context), NULL);

	/* TODO WebView2: synchronous evaluation is not possible with
	   ExecuteScript(); scripts run through
	   webkit_web_view_evaluate_javascript(). */
	jsc_context_throw (context, WEBKIT_SHIM_NOT_IMPLEMENTED_MESSAGE);

	return jsc_value_new_undefined (context);
}

JSCValue *
jsc_context_get_global_object (JSCContext *context)
{
	g_return_val_if_fail (JSC_IS_CONTEXT (context), NULL);

	/* TODO WebView2: no live global object; an empty snapshot */
	return _jsc_value_new_take_node (context, json_node_init_object (json_node_alloc (), json_object_new ()));
}

/* ------------------------------------------------------------------------ */
/* JSCValue                                                                 */
/* ------------------------------------------------------------------------ */

struct _JSCValue {
	GObject parent;

	JSCContext *context;
	JsonNode *node; /* NULL for undefined */
};

struct _JSCValueClass {
	GObjectClass parent_class;
};

G_DEFINE_TYPE (JSCValue, jsc_value, G_TYPE_OBJECT)

static void
jsc_value_dispose (GObject *object)
{
	JSCValue *value = JSC_VALUE (object);

	g_clear_object (&value->context);
	g_clear_pointer (&value->node, json_node_unref);

	G_OBJECT_CLASS (jsc_value_parent_class)->dispose (object);
}

static void
jsc_value_class_init (JSCValueClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = jsc_value_dispose;
}

static void
jsc_value_init (JSCValue *value)
{
}

JSCValue *
_jsc_value_new_take_node (JSCContext *context,
			  JsonNode *node)
{
	JSCValue *value;

	g_return_val_if_fail (JSC_IS_CONTEXT (context), NULL);

	value = g_object_new (JSC_TYPE_VALUE, NULL);
	value->context = g_object_ref (context);
	value->node = node;

	return value;
}

JsonNode *
_jsc_value_get_node (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);

	return value->node;
}

JSCContext *
jsc_value_get_context (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);

	return value->context;
}

JSCValue *
jsc_value_new_undefined (JSCContext *context)
{
	return _jsc_value_new_take_node (context, NULL);
}

JSCValue *
jsc_value_new_null (JSCContext *context)
{
	return _jsc_value_new_take_node (context, json_node_new (JSON_NODE_NULL));
}

JSCValue *
jsc_value_new_number (JSCContext *context,
		      double number)
{
	return _jsc_value_new_take_node (context, json_node_init_double (json_node_alloc (), number));
}

JSCValue *
jsc_value_new_boolean (JSCContext *context,
		       gboolean value)
{
	return _jsc_value_new_take_node (context, json_node_init_boolean (json_node_alloc (), value ? TRUE : FALSE));
}

JSCValue *
jsc_value_new_string (JSCContext *context,
		      const char *string)
{
	if (!string)
		return jsc_value_new_null (context);

	return _jsc_value_new_take_node (context, json_node_init_string (json_node_alloc (), string));
}

JSCValue *
jsc_value_new_string_from_bytes (JSCContext *context,
				 GBytes *bytes)
{
	gchar *string;
	JSCValue *value;
	gsize size = 0;
	gconstpointer data;

	if (!bytes)
		return jsc_value_new_null (context);

	data = g_bytes_get_data (bytes, &size);
	string = g_strndup (data, size);
	value = jsc_value_new_string (context, string);
	g_free (string);

	return value;
}

JSCValue *
jsc_value_new_array_from_garray (JSCContext *context,
				 GPtrArray *array)
{
	JsonArray *json_array;
	guint ii;

	json_array = json_array_new ();

	for (ii = 0; array && ii < array->len; ii++) {
		JSCValue *item = g_ptr_array_index (array, ii);

		if (item && item->node)
			json_array_add_element (json_array, json_node_copy (item->node));
		else
			json_array_add_null_element (json_array);
	}

	return _jsc_value_new_take_node (context, json_node_init_array (json_node_alloc (), json_array));
}

JSCValue *
jsc_value_new_array_from_strv (JSCContext *context,
			       const char * const *strv)
{
	JsonArray *json_array;
	guint ii;

	json_array = json_array_new ();

	for (ii = 0; strv && strv[ii]; ii++)
		json_array_add_string_element (json_array, strv[ii]);

	return _jsc_value_new_take_node (context, json_node_init_array (json_node_alloc (), json_array));
}

JSCValue *
jsc_value_new_from_json (JSCContext *context,
			 const char *json)
{
	JsonNode *node;
	GError *error = NULL;

	g_return_val_if_fail (JSC_IS_CONTEXT (context), NULL);

	if (!json)
		return jsc_value_new_null (context);

	node = json_from_string (json, &error);
	if (error) {
		jsc_context_throw_exception (context, jsc_exception_new_with_name (context, "SyntaxError", error->message));
		g_clear_error (&error);

		return NULL;
	}

	/* json_from_string() returns NULL for an empty string */
	return _jsc_value_new_take_node (context, node);
}

static JsonNodeType
jsc_value_get_node_type (JSCValue *value)
{
	return value->node ? json_node_get_node_type (value->node) : JSON_NODE_NULL;
}

gboolean
jsc_value_is_undefined (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	return value->node == NULL;
}

gboolean
jsc_value_is_null (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	return value->node && JSON_NODE_HOLDS_NULL (value->node);
}

gboolean
jsc_value_is_number (JSCValue *value)
{
	GType value_type;

	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	if (!value->node || !JSON_NODE_HOLDS_VALUE (value->node))
		return FALSE;

	value_type = json_node_get_value_type (value->node);

	return value_type == G_TYPE_INT64 || value_type == G_TYPE_DOUBLE;
}

gboolean
jsc_value_is_boolean (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	return value->node && JSON_NODE_HOLDS_VALUE (value->node) &&
		json_node_get_value_type (value->node) == G_TYPE_BOOLEAN;
}

gboolean
jsc_value_is_string (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	return value->node && JSON_NODE_HOLDS_VALUE (value->node) &&
		json_node_get_value_type (value->node) == G_TYPE_STRING;
}

gboolean
jsc_value_is_array (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	return jsc_value_get_node_type (value) == JSON_NODE_ARRAY;
}

gboolean
jsc_value_is_object (JSCValue *value)
{
	JsonNodeType node_type;

	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	/* as in JavaScript, arrays are objects too */
	node_type = jsc_value_get_node_type (value);

	return value->node && (node_type == JSON_NODE_OBJECT || node_type == JSON_NODE_ARRAY);
}

gboolean
jsc_value_is_function (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	/* functions do not survive the JSON serialization */
	return FALSE;
}

gboolean
jsc_value_is_constructor (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	return FALSE;
}

double
jsc_value_to_double (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), NAN);

	if (!value->node)
		return NAN;

	switch (json_node_get_node_type (value->node)) {
	case JSON_NODE_NULL:
		return 0.0;
	case JSON_NODE_VALUE: {
		GType value_type = json_node_get_value_type (value->node);

		if (value_type == G_TYPE_INT64 || value_type == G_TYPE_DOUBLE)
			return json_node_get_double (value->node);
		if (value_type == G_TYPE_BOOLEAN)
			return json_node_get_boolean (value->node) ? 1.0 : 0.0;
		if (value_type == G_TYPE_STRING) {
			const gchar *str = json_node_get_string (value->node);
			gchar *endptr = NULL;
			gdouble number;

			while (str && g_ascii_isspace (*str))
				str++;
			if (!str || !*str)
				return 0.0;

			number = g_ascii_strtod (str, &endptr);
			while (endptr && g_ascii_isspace (*endptr))
				endptr++;

			return (endptr && !*endptr) ? number : NAN;
		}
		return NAN;
	}
	default:
		return NAN;
	}
}

gint32
jsc_value_to_int32 (JSCValue *value)
{
	gdouble number;

	g_return_val_if_fail (JSC_IS_VALUE (value), 0);

	/* ECMAScript ToInt32() */
	number = jsc_value_to_double (value);
	if (isnan (number) || isinf (number))
		return 0;

	number = fmod (trunc (number), 4294967296.0);
	if (number < 0)
		number += 4294967296.0;

	return (gint32) (guint32) number;
}

gboolean
jsc_value_to_boolean (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);

	if (!value->node)
		return FALSE;

	switch (json_node_get_node_type (value->node)) {
	case JSON_NODE_NULL:
		return FALSE;
	case JSON_NODE_VALUE: {
		GType value_type = json_node_get_value_type (value->node);

		if (value_type == G_TYPE_BOOLEAN)
			return json_node_get_boolean (value->node);
		if (value_type == G_TYPE_STRING) {
			const gchar *str = json_node_get_string (value->node);
			return str && *str;
		}
		if (value_type == G_TYPE_INT64 || value_type == G_TYPE_DOUBLE) {
			gdouble number = json_node_get_double (value->node);
			return number != 0.0 && !isnan (number);
		}
		return FALSE;
	}
	default:
		return TRUE;
	}
}

/* Number to string the way JavaScript prints it (shortest representation) */
static gchar *
jsc_number_to_string (gdouble number)
{
	gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];

	if (isnan (number))
		return g_strdup ("NaN");
	if (isinf (number))
		return g_strdup (number > 0 ? "Infinity" : "-Infinity");
	if (number == trunc (number) && fabs (number) < 1e21)
		return g_strdup_printf ("%.0f", number);

	return g_strdup (g_ascii_dtostr (buffer, sizeof (buffer), number));
}

static gchar *
jsc_node_to_string (JsonNode *node)
{
	if (!node)
		return g_strdup ("undefined");

	switch (json_node_get_node_type (node)) {
	case JSON_NODE_NULL:
		return g_strdup ("null");
	case JSON_NODE_OBJECT:
		return g_strdup ("[object Object]");
	case JSON_NODE_ARRAY: {
		JsonArray *array = json_node_get_array (node);
		GString *str = g_string_new ("");
		guint ii, len = json_array_get_length (array);

		/* Array.prototype.join(): null/undefined elements are empty */
		for (ii = 0; ii < len; ii++) {
			JsonNode *element = json_array_get_element (array, ii);

			if (ii > 0)
				g_string_append_c (str, ',');
			if (element && !JSON_NODE_HOLDS_NULL (element)) {
				gchar *tmp = jsc_node_to_string (element);
				g_string_append (str, tmp);
				g_free (tmp);
			}
		}

		return g_string_free (str, FALSE);
	}
	case JSON_NODE_VALUE: {
		GType value_type = json_node_get_value_type (node);

		if (value_type == G_TYPE_STRING)
			return g_strdup (json_node_get_string (node));
		if (value_type == G_TYPE_BOOLEAN)
			return g_strdup (json_node_get_boolean (node) ? "true" : "false");
		if (value_type == G_TYPE_INT64 || value_type == G_TYPE_DOUBLE)
			return jsc_number_to_string (json_node_get_double (node));
		break;
	}
	}

	return g_strdup ("");
}

char *
jsc_value_to_string (JSCValue *value)
{
	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);

	return jsc_node_to_string (value->node);
}

GBytes *
jsc_value_to_string_as_bytes (JSCValue *value)
{
	gchar *str;

	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);

	str = jsc_value_to_string (value);

	return g_bytes_new_take (str, strlen (str));
}

char *
jsc_value_to_json (JSCValue *value,
		   guint indent)
{
	JsonGenerator *generator;
	gchar *json;

	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);

	/* JSON.stringify(undefined) is undefined */
	if (!value->node)
		return NULL;

	generator = json_generator_new ();
	json_generator_set_root (generator, value->node);
	json_generator_set_pretty (generator, indent > 0);
	json_generator_set_indent (generator, indent);
	json = json_generator_to_data (generator, NULL);
	g_object_unref (generator);

	return json;
}

static JsonObject *
jsc_value_get_json_object (JSCValue *value)
{
	if (value->node && JSON_NODE_HOLDS_OBJECT (value->node))
		return json_node_get_object (value->node);

	return NULL;
}

static JsonArray *
jsc_value_get_json_array (JSCValue *value)
{
	if (value->node && JSON_NODE_HOLDS_ARRAY (value->node))
		return json_node_get_array (value->node);

	return NULL;
}

/* Array index from a property name ("0", "1", ...), or -1 */
static gint
jsc_property_name_to_index (const char *name)
{
	gchar *endptr = NULL;
	guint64 index;

	if (!name || !g_ascii_isdigit (*name))
		return -1;

	index = g_ascii_strtoull (name, &endptr, 10);
	if (!endptr || *endptr || index > G_MAXINT)
		return -1;

	return (gint) index;
}

gboolean
jsc_value_object_has_property (JSCValue *value,
			       const char *name)
{
	JsonObject *object;
	JsonArray *array;

	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	object = jsc_value_get_json_object (value);
	if (object)
		return json_object_has_member (object, name);

	array = jsc_value_get_json_array (value);
	if (array) {
		gint index = jsc_property_name_to_index (name);

		return g_str_equal (name, "length") ||
			(index >= 0 && (guint) index < json_array_get_length (array));
	}

	return FALSE;
}

JSCValue *
jsc_value_object_get_property (JSCValue *value,
			       const char *name)
{
	JsonObject *object;
	JsonArray *array;

	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	object = jsc_value_get_json_object (value);
	if (object) {
		JsonNode *member = json_object_get_member (object, name);

		return _jsc_value_new_take_node (value->context, member ? json_node_ref (member) : NULL);
	}

	array = jsc_value_get_json_array (value);
	if (array) {
		gint index;

		if (g_str_equal (name, "length"))
			return jsc_value_new_number (value->context, json_array_get_length (array));

		index = jsc_property_name_to_index (name);
		if (index >= 0)
			return jsc_value_object_get_property_at_index (value, (guint) index);
	}

	return jsc_value_new_undefined (value->context);
}

void
jsc_value_object_set_property (JSCValue *value,
			       const char *name,
			       JSCValue *property)
{
	JsonObject *object;

	g_return_if_fail (JSC_IS_VALUE (value));
	g_return_if_fail (name != NULL);
	g_return_if_fail (JSC_IS_VALUE (property));

	object = jsc_value_get_json_object (value);
	if (object) {
		/* JSON has no undefined, store null */
		json_object_set_member (object, name,
			property->node ? json_node_copy (property->node) : json_node_new (JSON_NODE_NULL));
	} else {
		gint index = jsc_property_name_to_index (name);

		if (index >= 0)
			jsc_value_object_set_property_at_index (value, (guint) index, property);
	}
}

gboolean
jsc_value_object_delete_property (JSCValue *value,
				  const char *name)
{
	JsonObject *object;

	g_return_val_if_fail (JSC_IS_VALUE (value), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	object = jsc_value_get_json_object (value);
	if (object && json_object_has_member (object, name)) {
		json_object_remove_member (object, name);
		return TRUE;
	}

	return FALSE;
}

gchar **
jsc_value_object_enumerate_properties (JSCValue *value)
{
	JsonObject *object;
	JsonArray *array;
	GPtrArray *names;

	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);

	names = g_ptr_array_new ();

	object = jsc_value_get_json_object (value);
	if (object) {
		GList *members, *link;

		members = json_object_get_members (object);
		for (link = members; link; link = g_list_next (link))
			g_ptr_array_add (names, g_strdup (link->data));
		g_list_free (members);
	}

	array = jsc_value_get_json_array (value);
	if (array) {
		guint ii, len = json_array_get_length (array);

		for (ii = 0; ii < len; ii++)
			g_ptr_array_add (names, g_strdup_printf ("%u", ii));
	}

	/* as in JSC: NULL when there are no enumerable properties */
	if (!names->len) {
		g_ptr_array_free (names, TRUE);
		return NULL;
	}

	g_ptr_array_add (names, NULL);

	return (gchar **) g_ptr_array_free (names, FALSE);
}

JSCValue *
jsc_value_object_get_property_at_index (JSCValue *value,
					guint index)
{
	JsonArray *array;
	JsonObject *object;

	g_return_val_if_fail (JSC_IS_VALUE (value), NULL);

	array = jsc_value_get_json_array (value);
	if (array) {
		if (index < json_array_get_length (array))
			return _jsc_value_new_take_node (value->context, json_node_ref (json_array_get_element (array, index)));

		return jsc_value_new_undefined (value->context);
	}

	object = jsc_value_get_json_object (value);
	if (object) {
		gchar name[16];
		JsonNode *member;

		g_snprintf (name, sizeof (name), "%u", index);
		member = json_object_get_member (object, name);

		return _jsc_value_new_take_node (value->context, member ? json_node_ref (member) : NULL);
	}

	return jsc_value_new_undefined (value->context);
}

void
jsc_value_object_set_property_at_index (JSCValue *value,
					guint index,
					JSCValue *property)
{
	JsonArray *array;
	JsonObject *object;
	JsonNode *node;

	g_return_if_fail (JSC_IS_VALUE (value));
	g_return_if_fail (JSC_IS_VALUE (property));

	node = property->node ? json_node_copy (property->node) : json_node_new (JSON_NODE_NULL);

	array = jsc_value_get_json_array (value);
	if (array) {
		/* JsonArray cannot replace an element: rebuild the array */
		JsonArray *copy = json_array_new ();
		guint ii, len = MAX (json_array_get_length (array), index + 1);

		for (ii = 0; ii < len; ii++) {
			if (ii == index)
				json_array_add_element (copy, node);
			else if (ii < json_array_get_length (array))
				json_array_add_element (copy, json_node_copy (json_array_get_element (array, ii)));
			else
				json_array_add_null_element (copy);
		}

		json_node_take_array (value->node, copy);
		return;
	}

	object = jsc_value_get_json_object (value);
	if (object) {
		gchar name[16];

		g_snprintf (name, sizeof (name), "%u", index);
		json_object_set_member (object, name, node);
		return;
	}

	json_node_unref (node);
}
