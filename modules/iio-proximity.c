#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mce.h"
#include "mce-io.h"
#include "mce-hal.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "datapipe.h"

#define MODULE_NAME		"iio-proximity"

#define MODULE_PROVIDES	"proximity"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

static unsigned int watch_id;
static GDBusProxy *iio_proxy;

static call_state_t call_state;
static alarm_ui_state_t alarm_ui_state;

static bool iio_prox_claim_policy(void)
{
	return (call_state == CALL_STATE_RINGING) ||
			(call_state == CALL_STATE_ACTIVE) ||
			(alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
			(alarm_ui_state == MCE_ALARM_UI_RINGING_INT32);
}

static bool iio_prox_get_value(GDBusProxy * proxy)
{
	GVariant *v;
	v = g_dbus_proxy_get_cached_property(proxy, "ProximityNear");
	/*todo: Handle units other than lux? */
	bool prox = g_variant_get_boolean(v);

	mce_log(LL_DEBUG, "%s: proximity %s", MODULE_NAME, prox ? "near" : "far");
	return prox;
}

static bool iio_prox_claim_sensor(bool claim)
{
	static bool claimed = false;
	GError *error = NULL;
	GVariant *ret = NULL;

	if (iio_proxy) {
		if (claim && !claimed) {
			mce_log(LL_DEBUG, "%s: Claim proximity sensor", MODULE_NAME);
			ret =
			    g_dbus_proxy_call_sync(iio_proxy, "ClaimProximity", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
						   &error);
			if (!ret && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
				mce_log(LL_WARN, "%s: failed to claim proximity sensor %s", MODULE_NAME,
					error->message);
				g_clear_pointer(&ret, g_variant_unref);
				return false;
			}
			g_clear_pointer(&ret, g_variant_unref);

			bool prox = iio_prox_get_value(iio_proxy);
			execute_datapipe(&proximity_sensor_pipe, GINT_TO_POINTER(prox ? COVER_CLOSED : COVER_OPEN), USE_INDATA, CACHE_INDATA);
		} else if (!claim && claimed) {
			mce_log(LL_DEBUG, "%s: Release proximity sensor", MODULE_NAME);
			ret =
			    g_dbus_proxy_call_sync(iio_proxy, "ReleaseProximity", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
						   &error);
			if (!ret && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
				mce_log(LL_WARN, "%s: failed to relese proximity sensor %s", MODULE_NAME,
					error->message);
				g_clear_pointer(&ret, g_variant_unref);
				return false;
			}
			g_clear_pointer(&ret, g_variant_unref);
			
			execute_datapipe(&proximity_sensor_pipe, GINT_TO_POINTER(COVER_OPEN), USE_INDATA, CACHE_INDATA);
		}
		claimed = claim;
	} else {
		claimed = false;
	}
	
	return true;
}

static void iio_prox_properties_changed(GDBusProxy * proxy,
				       GVariant * changed_properties, GStrv invalidated_properties, gpointer user_data)
{
	GVariantDict dict;
	
	(void)proxy;
	(void)invalidated_properties;
	(void)user_data;
	
	g_variant_dict_init(&dict, changed_properties);

	if (g_variant_dict_contains(&dict, "ProximityNear")) {
		bool prox = iio_prox_get_value(iio_proxy);
		execute_datapipe(&proximity_sensor_pipe, GINT_TO_POINTER(prox ? COVER_CLOSED : COVER_OPEN), USE_INDATA, CACHE_INDATA);
	}

	g_variant_dict_clear(&dict);
}

static void iio_sensors_appeared(GDBusConnection * connection, const gchar * name, const gchar * name_owner,
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

	g_signal_connect(G_OBJECT(iio_proxy), "g-properties-changed", G_CALLBACK(iio_prox_properties_changed), NULL);

	if (iio_prox_claim_policy())
		iio_prox_claim_sensor(true);
}

static void iio_sensors_vanished(GDBusConnection * connection, const gchar * name, gpointer user_data)
{
	(void)name;
	(void)user_data;
	(void)connection;
	if (iio_proxy) {
		g_clear_object(&iio_proxy);
		iio_proxy = NULL;
		mce_log(LL_WARN, "%s: connection to iio_sensor_proxy lost", MODULE_NAME);
		iio_prox_claim_sensor(false);
	}
}

static void call_state_trigger(gconstpointer data)
{
	(void)data;
	call_state = datapipe_get_gint(call_state_pipe);
	iio_prox_claim_sensor(iio_prox_claim_policy());
}

static void alarm_ui_state_trigger(gconstpointer data)
{
	(void)data;
	alarm_ui_state = datapipe_get_gint(alarm_ui_state_pipe);
	iio_prox_claim_sensor(iio_prox_claim_policy());
}

G_MODULE_EXPORT const char *g_module_check_init(GModule * module);
const char *g_module_check_init(GModule * module)
{
	(void)module;

	mce_log(LL_DEBUG, "Initalizing %s", MODULE_NAME);

	append_output_trigger_to_datapipe(&call_state_pipe, call_state_trigger);
	append_output_trigger_to_datapipe(&alarm_ui_state_pipe, alarm_ui_state_trigger);

	call_state = datapipe_get_gint(call_state_pipe);
	alarm_ui_state = datapipe_get_gint(alarm_ui_state_pipe);

	watch_id = g_bus_watch_name(G_BUS_TYPE_SYSTEM, "net.hadess.SensorProxy",
				    G_BUS_NAME_WATCHER_FLAGS_NONE,
				    iio_sensors_appeared, iio_sensors_vanished, NULL, NULL);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	remove_output_trigger_from_datapipe(&alarm_ui_state_pipe, alarm_ui_state_trigger);
	remove_output_trigger_from_datapipe(&call_state_pipe, call_state_trigger);
	
	g_bus_unwatch_name(watch_id);
	if (iio_proxy) {
		g_clear_object(&iio_proxy);
		iio_proxy = NULL;
	}

}
