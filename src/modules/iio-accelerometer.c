/* DEPECIATED: THIS MODULE IS MODULE IS A LEGACY SUPPORT MODULE ONLY, DO NOT USE ITS INTERFACES IN NEW APPLICATIONS */

#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <mce/mode-names.h>
#include "mce.h"
#include "mce-io.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "datapipe.h"

#define MODULE_NAME		"iio-accelerometer"

#define MODULE_PROVIDES	"accelerometer"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

typedef enum {
	ORIENTATION_UNKNOWN,
	ORIENTATION_LANDSCAPE,
	ORIENTATION_PORTRAIT,
	ORIENTATION_FACE_DOWN,
	ORIENTATION_FACE_UP
} orientation_t;

static display_state_t display_state = { 0 };
static alarm_ui_state_t alarm_state = { 0 };
static call_state_t call_state = { 0 };

static unsigned int watch_id = 0;
static GDBusProxy *iio_proxy = NULL;

static GSList *accelerometer_listeners = NULL;

static orientation_t orientation = ORIENTATION_UNKNOWN;

static bool iio_accel_claim_policy(void)
{
	return g_slist_length(accelerometer_listeners) > 0 && 
	       (display_state != MCE_DISPLAY_OFF || 
	       alarm_state == MCE_ALARM_UI_RINGING_INT32 || 
	       call_state == CALL_STATE_RINGING);
}

static const char *iio_orientation_to_str(const orientation_t orit)
{
	switch (orit) {
		case ORIENTATION_LANDSCAPE: 
			return MCE_ORIENTATION_LANDSCAPE;
		case ORIENTATION_PORTRAIT: 
			return MCE_ORIENTATION_PORTRAIT;
		case ORIENTATION_FACE_DOWN:
			return MCE_ORIENTATION_FACE_DOWN;
		case ORIENTATION_FACE_UP:
			return MCE_ORIENTATION_FACE_UP;
		default:
			return MCE_ORIENTATION_UNKNOWN;
	}
}

static gboolean send_device_orientation(DBusMessage *const method_call)
{
	const gchar *srotation = iio_orientation_to_str(orientation);
	const gchar *sstand = MCE_ORIENTATION_OFF_STAND;
	const gchar *sface = (strcmp(iio_orientation_to_str(orientation), MCE_ORIENTATION_FACE_DOWN) == 0) ? MCE_ORIENTATION_FACE_DOWN : MCE_ORIENTATION_FACE_UP;
	dbus_int32_t maxInt = G_MAXINT32;
	DBusMessage *msg = NULL;
	
	if (method_call != NULL)
		msg = dbus_new_method_reply(method_call);
	else
		msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
				      MCE_DEVICE_ORIENTATION_SIG);

	if (dbus_message_append_args(msg,
				     DBUS_TYPE_STRING, &srotation,
				     DBUS_TYPE_STRING, &sstand,
				     DBUS_TYPE_STRING, &sface,
				     DBUS_TYPE_INT32, &maxInt,
				     DBUS_TYPE_INT32, &maxInt,
				     DBUS_TYPE_INT32, &maxInt,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT, "%s: "
			"Failed to append %sarguments to D-Bus message "
			"for %s.%s", MODULE_NAME,
			method_call ? "reply " : "",
			method_call ? MCE_REQUEST_IF :
				      MCE_SIGNAL_IF,
			method_call ? MCE_DEVICE_ORIENTATION_GET :
				      MCE_DEVICE_ORIENTATION_SIG);
		dbus_message_unref(msg);
		return FALSE;
	}

	return dbus_send_message(msg);
}

static void iio_accel_get_value(GDBusProxy * proxy)
{
	GVariant *v;
	v = g_dbus_proxy_get_cached_property (proxy, "AccelerometerOrientation");
	
	bool changed = false;

	if (strcmp(g_variant_get_string(v, NULL), "undefined") == 0) {
		orientation = ORIENTATION_UNKNOWN;
		changed = true;
	}
	else if (strcmp(g_variant_get_string(v, NULL), "normal") == 0) {
		orientation = ORIENTATION_LANDSCAPE;
		changed = true;
	}
	else if (strcmp(g_variant_get_string(v, NULL), "left-up") == 0) {
		orientation = ORIENTATION_PORTRAIT;
		changed = true;
	}
	else if (strcmp(g_variant_get_string(v, NULL), "face-up") == 0) {
		orientation = ORIENTATION_FACE_UP;
		changed = true;
	}
	else if (strcmp(g_variant_get_string(v, NULL), "face-down") == 0) {
		orientation = ORIENTATION_FACE_DOWN;
		changed = true;
	}
	g_variant_unref(v);
	
	if (changed) {
		mce_log(LL_DEBUG, "%s: orientation: %s", MODULE_NAME, iio_orientation_to_str(orientation));
		send_device_orientation(NULL);
	}
}

static void iio_accel_dbus_call_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	(void)source_object;
	bool claim = GPOINTER_TO_INT(user_data);
	GError *error = NULL;
	GVariant *ret = g_dbus_proxy_call_finish(iio_proxy, res, &error);

	if (!ret && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		mce_log(LL_WARN, "%s: failed to %s accelerometer %s", MODULE_NAME,
				claim ? "claim" : "release", error ? error->message : "");
		g_clear_pointer(&ret, g_variant_unref);
		return;
	}
	g_clear_pointer(&ret, g_variant_unref);

	if(claim)
		iio_accel_get_value(iio_proxy);
}

static bool iio_accel_claim_sensor(bool claim)
{
	static bool claimed = false;

	if (iio_proxy) {
		if (claim && !claimed) {
			mce_log(LL_DEBUG, "%s: ClaimAccelerometer", MODULE_NAME);
			g_dbus_proxy_call(iio_proxy, "ClaimAccelerometer", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
						   iio_accel_dbus_call_cb, GINT_TO_POINTER(true));
		} else if (!claim && claimed) {
			mce_log(LL_DEBUG, "%s: ReleaseAccelerometer", MODULE_NAME);
			g_dbus_proxy_call(iio_proxy, "ReleaseAccelerometer", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
						   iio_accel_dbus_call_cb, GINT_TO_POINTER(false));
		}
		claimed = claim;
	} else {
		claimed = false;
	}
	
	return true;
}

static void iio_accel_properties_changed(GDBusProxy * proxy,
				       GVariant * changed_properties, GStrv invalidated_properties, gpointer user_data)
{

	(void)proxy;
	(void)invalidated_properties;
	(void)user_data;
	GVariantDict dict;

	g_variant_dict_init(&dict, changed_properties);

	if (g_variant_dict_contains(&dict, "AccelerometerOrientation"))
		iio_accel_get_value(iio_proxy);

	g_variant_dict_clear(&dict);
}

static void iio_accel_sensors_appeared(GDBusConnection * connection, const gchar * name, const gchar * name_owner,
				     gpointer user_data)
{
	(void)name;
	(void)name_owner;
	(void)connection;
	(void)user_data;

	mce_log(LL_INFO, "%s: Found iio_sensor_proxy", MODULE_NAME);

	iio_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						  G_DBUS_PROXY_FLAGS_NONE,
						  NULL,
						  "net.hadess.SensorProxy",
						  "/net/hadess/SensorProxy", "net.hadess.SensorProxy", NULL, NULL);

	g_signal_connect(G_OBJECT(iio_proxy), "g-properties-changed", G_CALLBACK(iio_accel_properties_changed), NULL);

	if (iio_accel_claim_policy())
		iio_accel_claim_sensor(true);
}

static void iio_accel_sensors_vanished(GDBusConnection * connection, const gchar * name, gpointer user_data)
{
	(void)name;
	(void)user_data;
	(void)connection;
	if (iio_proxy) {
		g_clear_object(&iio_proxy);
		iio_proxy = NULL;
		mce_log(LL_WARN, "%s: connection to iio_sensor_proxy lost", MODULE_NAME);
		iio_accel_claim_sensor(false);
	}
}

static gboolean get_device_orientation_dbus_cb(DBusMessage *const method_call)
{
	return send_device_orientation(method_call);
}

static gboolean accelerometer_owner_monitor_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;
	const gchar *old_name;
	const gchar *new_name;
	const gchar *service;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &service,
				  DBUS_TYPE_STRING, &old_name,
				  DBUS_TYPE_STRING, &new_name,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_ERR,
			"%s: Failed to get argument from %s.%s; %s", MODULE_NAME,
			"org.freedesktop.DBus", "NameOwnerChanged",
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	mce_log(LL_DEBUG,
		"%s: Received accelerometer name owner change for %s", MODULE_NAME,
		old_name);

	if (mce_dbus_owner_monitor_remove(old_name, &accelerometer_listeners) == 0)
		iio_accel_claim_sensor(iio_accel_claim_policy());

	status = TRUE;

EXIT:
	return status;
}

static gboolean req_accelerometer_enable_dbus_cb(DBusMessage *const msg)
{
	gssize num;
	gboolean status = FALSE;
	const char  *sender = dbus_message_get_sender(msg);
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);

	if (sender == NULL) {
		mce_log(LL_CRIT, "%s: No sender in enable accelerometer request", MODULE_NAME);
		goto EXIT;
	}

	mce_log(LL_DEBUG, "%s: Received enable accelerometer request from %s", MODULE_NAME,
		sender);
	num = mce_dbus_owner_monitor_add(sender,
					 accelerometer_owner_monitor_dbus_cb,
					 &accelerometer_listeners,
				         10);

	if (num == -1) {
		mce_log(LL_INFO, "%s: "
			"Failed to add name accelerometer owner "
			"monitoring for `%s'", MODULE_NAME,
			sender);
	}
	
	iio_accel_claim_sensor(iio_accel_claim_policy());
	
	if (no_reply == FALSE && !get_device_orientation_dbus_cb(msg))
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}
static gboolean req_accelerometer_disable_dbus_cb(DBusMessage *const msg)
{

	gssize num;
	gboolean status = FALSE;
	const char  *sender = dbus_message_get_sender(msg);
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);

	if (sender == NULL) {
		mce_log(LL_CRIT, "%s: No sender in disable accelerometer request", MODULE_NAME);
		goto EXIT;
	}

	mce_log(LL_DEBUG, "%s: Received disable accelerometer request from %s", MODULE_NAME, sender);
	num = mce_dbus_owner_monitor_remove(sender, &accelerometer_listeners);

	if (num == -1) {
		mce_log(LL_INFO,
			"%s: Failed to remove '%s' from accelerometer "
			"owner monitoring list", MODULE_NAME,
			sender);
	} else if (num == 0) {
		iio_accel_claim_sensor(iio_accel_claim_policy());
	}

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

EXIT:
	return status;
}

static void display_state_trigger(gconstpointer data)
{
	display_state = GPOINTER_TO_INT(data);
	iio_accel_claim_sensor(iio_accel_claim_policy());
}

static void alarm_ui_state_trigger(gconstpointer data)
{
	alarm_state = GPOINTER_TO_INT(data);
	iio_accel_claim_sensor(iio_accel_claim_policy());
}

static void call_state_trigger(gconstpointer data)
{
	call_state = GPOINTER_TO_INT(data);
	iio_accel_claim_sensor(iio_accel_claim_policy());
}

G_MODULE_EXPORT const char *g_module_check_init(GModule * module);
const char *g_module_check_init(GModule * module)
{
	(void)module;

	mce_log(LL_DEBUG, "Initalizing %s", MODULE_NAME);
	mce_log(LL_INFO, "%s is a depreciated module, do not use its interfaces.", MODULE_NAME);
	
	append_input_trigger_to_datapipe(&display_state_pipe, display_state_trigger);
	append_output_trigger_to_datapipe(&alarm_ui_state_pipe, alarm_ui_state_trigger);
	append_output_trigger_to_datapipe(&call_state_pipe, call_state_trigger);
	
	call_state = datapipe_get_gint(call_state_pipe);
	display_state = datapipe_get_gint(display_state_pipe);
	alarm_state = datapipe_get_gint(alarm_ui_state_pipe);

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEVICE_ORIENTATION_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 get_device_orientation_dbus_cb) == NULL)
		return NULL;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ACCELEROMETER_ENABLE_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 req_accelerometer_enable_dbus_cb) == NULL)
		return NULL;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ACCELEROMETER_DISABLE_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 req_accelerometer_disable_dbus_cb) == NULL)
		return NULL;

	watch_id = g_bus_watch_name(G_BUS_TYPE_SYSTEM, "net.hadess.SensorProxy",
				    G_BUS_NAME_WATCHER_FLAGS_NONE,
				    iio_accel_sensors_appeared, iio_accel_sensors_vanished, NULL, NULL);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	remove_output_trigger_from_datapipe(&display_state_pipe, display_state_trigger);
	
	g_bus_unwatch_name(watch_id);
	
	if (iio_proxy) {
		g_clear_object(&iio_proxy);
		iio_proxy = NULL;
		iio_accel_claim_sensor(false);
	}
	
	mce_dbus_owner_monitor_remove_all(&accelerometer_listeners);

}
