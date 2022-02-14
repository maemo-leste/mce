#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mce.h"
#include "mce-io.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "datapipe.h"

#define MODULE_NAME		"iio-als"

#define MODULE_PROVIDES	"als"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

static display_state_t display_state = { 0 };

static unsigned int watch_id = 0;
static GDBusProxy *iio_proxy = NULL;

static int cal_scale = 1000;

/* TODO: Handle units other than lux, or check that unit is Lux?
 * iio-sensor-proxy doesn't support other units at the moment, but it might
 * in the future.
 */
static int iio_als_get_light_value(GDBusProxy * proxy)
{
	(void)proxy;
	GVariant *v;
	GVariant *unit;
	v = g_dbus_proxy_get_cached_property(iio_proxy, "LightLevel");
	unit = g_dbus_proxy_get_cached_property(iio_proxy, "LightLevelUnit");
	double mlux = g_variant_get_double(v)*cal_scale;
	if (mlux < 0)
		mlux = 0.0;

	g_variant_unref(v);
	g_variant_unref(unit);

	mce_log(LL_DEBUG, "%s: Light level: %lf mlux", MODULE_NAME, mlux);
	return (int)mlux;
}

static void iio_als_dbus_call_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	(void)source_object;
	bool claim = GPOINTER_TO_INT(user_data);
	GError *error = NULL;
	GVariant *ret = g_dbus_proxy_call_finish(iio_proxy, res, &error);

	if (!ret && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		mce_log(LL_WARN, "%s: failed to %s ambient light sensor %s", MODULE_NAME,
				claim ? "claim" : "release", error ? error->message : "");
		g_clear_pointer(&ret, g_variant_unref);
		return;
	}
	g_clear_pointer(&ret, g_variant_unref);

	if(claim) {
		int ilux = iio_als_get_light_value(iio_proxy);
		execute_datapipe(&light_sensor_pipe, GINT_TO_POINTER(ilux), USE_INDATA, CACHE_INDATA);
	}
}

static bool iio_als_claim_light_sensor(bool claim)
{
	static bool claimed = false;

	if (iio_proxy) {
		if (claim && !claimed) {
			mce_log(LL_DEBUG, "%s: ClaimLight", MODULE_NAME);
			g_dbus_proxy_call(iio_proxy, "ClaimLight", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
						   iio_als_dbus_call_cb, GINT_TO_POINTER(true));
		} else if (!claim && claimed) {
			mce_log(LL_DEBUG, "%s: ReleaseLight", MODULE_NAME);
			g_dbus_proxy_call(iio_proxy, "ReleaseLight", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
						    iio_als_dbus_call_cb, GINT_TO_POINTER(false));
		}
		claimed = claim;
	} else {
		claimed = false;
	}
	
	return true;
}

static void display_state_trigger(gconstpointer data)
{
	(void)data;
	display_state = datapipe_get_gint(display_state_pipe);
	iio_als_claim_light_sensor(display_state == MCE_DISPLAY_ON);
}

static void iio_als_properties_changed(GDBusProxy * proxy,
				       GVariant * changed_properties, GStrv invalidated_properties, gpointer user_data)
{
	(void)proxy;
	(void)invalidated_properties;
	(void)user_data;
	GVariantDict dict;

	g_variant_dict_init(&dict, changed_properties);

	if (g_variant_dict_contains(&dict, "LightLevel")) {

		int ilux = (int)iio_als_get_light_value(iio_proxy);
		(void)execute_datapipe(&light_sensor_pipe, GINT_TO_POINTER(ilux), USE_INDATA, CACHE_INDATA);

	}

	g_variant_dict_clear(&dict);
}

static void iio_als_sensors_appeared(GDBusConnection * connection, const gchar * name, const gchar * name_owner,
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

	g_signal_connect(G_OBJECT(iio_proxy), "g-properties-changed", G_CALLBACK(iio_als_properties_changed), NULL);

	if (display_state == MCE_DISPLAY_ON)
		iio_als_claim_light_sensor(true);
}

static void iio_als_sensors_vanished(GDBusConnection * connection, const gchar * name, gpointer user_data)
{
	(void)name;
	(void)user_data;
	(void)connection;
	if (iio_proxy) {
		g_clear_object(&iio_proxy);
		iio_proxy = NULL;
		mce_log(LL_WARN, "%s: connection to iio_sensor_proxy lost", MODULE_NAME);
		iio_als_claim_light_sensor(false);
	}
}

G_MODULE_EXPORT const char *g_module_check_init(GModule * module);
const char *g_module_check_init(GModule * module)
{
	(void)module;

	mce_log(LL_DEBUG, "Initalizing %s", MODULE_NAME);

	cal_scale = mce_conf_get_int("IioAls", "CalScale", 1000, NULL);
	if (cal_scale < 0) 
		cal_scale = 1000;

	append_output_trigger_to_datapipe(&display_state_pipe, display_state_trigger);

	display_state = datapipe_get_gint(display_state_pipe);

	watch_id = g_bus_watch_name(G_BUS_TYPE_SYSTEM, "net.hadess.SensorProxy",
				    G_BUS_NAME_WATCHER_FLAGS_NONE,
				    iio_als_sensors_appeared, iio_als_sensors_vanished, NULL, NULL);

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
		iio_als_claim_light_sensor(false);
	}

}
