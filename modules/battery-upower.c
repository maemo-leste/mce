/**
* @file battery-upower.c
* Battery module -- this implements battery and charger logic for MCE
*
* Copyright (C) 2013 Jolla Ltd.
* Copyright (C) 2018 Arthur D. <spinal.by@gmail.com>
*
* @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
*
* mce is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License
* version 2.1 as published by the Free Software Foundation.
*
* mce is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with mce.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mce.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <upower.h>
#include <gmodule.h>

#define MODULE_NAME "battery_upower"

static const gchar *const provides[] = { MODULE_NAME, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name     = MODULE_NAME,
	.provides = provides,
	.priority = 100
};


#define UNUSED(x) (void)(x)

#if 0 // DEBUG: make all logging from this module "critical"
# undef mce_log
# define mce_log(LEV, FMT, ARGS...) \
		mce_log_file(LL_CRIT, __FILE__, __FUNCTION__, FMT , ## ARGS)
#endif

/** Delay from 1st property change to state machine update; [ms] */
#define UPDATE_DELAY 100

/** Whether to support legacy pattery low led pattern; nonzero for yes */
#define SUPPORT_BATTERY_LOW_LED_PATTERN 0

/** How long we want battery state to be forced after charger state changed */
#define FORCE_STATE_TIME 10

#define MCE_CONF_BATTERY_SECTION "battery"
#define MCE_CONF_CRIT_VOLTAGE_KEY "CriticalVoltage"
#define MCE_CONF_LOW_PERCENT_KEY "LowPercentage"


/** Skip these devices */
static const char* blacklist[] = {
	/* This driver should be removed from the kernel completely */
	"rx51-battery",
	/* Nokia N900 charger device is exposed as battery by UPower */
	"bq24150a-0",
	/* Droid4 line power device (driver doesn't send uevents) */
	"usb",
	/* End of list */
	NULL
};

/** Private data */
static struct {
	UpClient *client;
	UpDevice *battery;
	UpDevice *charger;
	gboolean  fallback;
	time_t    force_state;
	gdouble min_voltage;
	int low_percentage;
} private = {0};

/** Battery properties available via UPower */
struct {
	guint    state;
	gdouble  percentage;
	gdouble  voltage;
	gboolean charger_online;
} upowbat = {0};

/** Battery properties in mce statemachine compatible form */
struct mce_battery {
	/** Battery FULL/OK/LOW/EMPTY; for use with battery_status_pipe */
	int       status;
	/** Charger connected; for use with charger_state_pipe */
	gboolean  charger_connected;
} mcebat = {0};

/** Timer for processing battery status changes */
static guint mcebat_update_id = 0;


/**
* Initialize UPower battery state data
*/
static void
upowbat_init(void)
{
	upowbat.percentage = 50;
	upowbat.voltage    = 3.8;
	upowbat.state = UP_DEVICE_STATE_UNKNOWN;
}

/**
* Provide initial guess of mce battery status
*/
static void
mcebat_init(void)
{
	mcebat.status = BATTERY_STATUS_UNDEF;
	mcebat.charger_connected = FALSE;
}

/**
* Update UPower battery state data
*/
static void
upowbat_update(void)
{
	gdouble percentage;
	gdouble voltage;
	guint   state;

	if (private.battery == NULL)
		return;

	g_object_get(private.battery,
				"percentage", &percentage,
				"state", &state,
				"voltage", &voltage,
				NULL);

	if (upowbat.percentage != percentage) {
		mce_log(LL_DEBUG, "%s: Percentage: %d -> %d", MODULE_NAME, (int)upowbat.percentage, (int)percentage);
		upowbat.percentage = percentage;
	}
	
	if (upowbat.voltage != voltage) {
		mce_log(LL_DEBUG, "%s: Voltage: %f -> %f", MODULE_NAME, upowbat.voltage, voltage);
		upowbat.voltage = voltage;
	}

	if (time(NULL) < private.force_state) {
		if (upowbat.charger_online) {
			if (state == UP_DEVICE_STATE_DISCHARGING)
				state = UP_DEVICE_STATE_CHARGING;
		} else if (state == UP_DEVICE_STATE_CHARGING || state == UP_DEVICE_STATE_FULLY_CHARGED) {
			state = UP_DEVICE_STATE_DISCHARGING;
		}
	}

	if (upowbat.state != state) {
		if (upowbat.state == UP_DEVICE_STATE_FULLY_CHARGED && state == UP_DEVICE_STATE_CHARGING) {
			/* Prevent 'fully charged' -> 'charging' transition */
			return;
		}
		mce_log(LL_DEBUG, "%s: State: %d -> %d", MODULE_NAME, upowbat.state, state);
		upowbat.state = state;
	}
}

/**
* Update mce battery status from UPower battery data
*/
static void
mcebat_update_from_upowbat(void)
{
	mcebat.status = BATTERY_STATUS_OK;

	if (upowbat.state == UP_DEVICE_STATE_EMPTY || 
		upowbat.voltage < private.min_voltage)
		mcebat.status = BATTERY_STATUS_EMPTY;
	else if (upowbat.percentage < private.low_percentage)
		mcebat.status = BATTERY_STATUS_LOW;
	else if (upowbat.state == UP_DEVICE_STATE_FULLY_CHARGED)
		mcebat.status = BATTERY_STATUS_FULL;

	/* Try to guess charger state using battery state property */
	if (private.charger) {
		mcebat.charger_connected = upowbat.charger_online;
	} else {
		mcebat.charger_connected = upowbat.state == UP_DEVICE_STATE_CHARGING ||
								upowbat.state == UP_DEVICE_STATE_FULLY_CHARGED ||
								upowbat.state == UP_DEVICE_STATE_PENDING_CHARGE;
	}
}

static inline const char *
charger_state_repr(gboolean state)
{
	return state ? "on" : "off";
}

/**
* Process accumulated upower battery status changes
* @param user_data  (not used)
* @return  FALSE (to stop timer from repeating)
*/
static gboolean
mcebat_update_cb(gpointer user_data)
{
	struct mce_battery prev = mcebat;
	UNUSED(user_data);

	if (!mcebat_update_id)
		return FALSE;

	mcebat_update_id = 0;

	/* Update from UPower based information */
	upowbat_update();
	mcebat_update_from_upowbat();

	/* Process changes */
	if (prev.charger_connected != mcebat.charger_connected)
	{
		mce_log(LL_INFO, "%s: charger: %s -> %s", MODULE_NAME,
				charger_state_repr(prev.charger_connected),
				charger_state_repr(mcebat.charger_connected));

		/* Charger connected state */
		execute_datapipe(&charger_state_pipe,
						GINT_TO_POINTER(mcebat.charger_connected),
						USE_INDATA, CACHE_INDATA);

		/* Charging led pattern */
		if (mcebat.charger_connected) {
			gchar *pattern = g_strdup(MCE_LED_PATTERN_BATTERY_CHARGING);
			execute_datapipe(&led_pattern_activate_pipe, pattern, USE_CACHE, DONT_CACHE_INDATA);
			g_free(pattern);
		}
		else {
			execute_datapipe_output_triggers(&led_pattern_deactivate_pipe,
											MCE_LED_PATTERN_BATTERY_CHARGING,
											USE_INDATA);
		}

		/* Generate activity */
		execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
						USE_INDATA, CACHE_INDATA);
	}

	if (prev.status != mcebat.status) {
		mce_log(LL_INFO, "%s: status: %d -> %d", MODULE_NAME, prev.status, mcebat.status);

		/* Battery full led pattern */
		if (mcebat.status == BATTERY_STATUS_FULL) {
			gchar *pattern = g_strdup(MCE_LED_PATTERN_BATTERY_FULL);
			execute_datapipe(&led_pattern_activate_pipe, pattern, USE_CACHE, DONT_CACHE_INDATA);
			g_free(pattern);
		}
		else if (prev.status == BATTERY_STATUS_FULL) {
			execute_datapipe_output_triggers(&led_pattern_deactivate_pipe,
											MCE_LED_PATTERN_BATTERY_FULL,
											USE_INDATA);
		}

#if SUPPORT_BATTERY_LOW_LED_PATTERN
		/* Battery low led pattern */
		if (mcebat.status == BATTERY_STATUS_LOW ||
			mcebat.status == BATTERY_STATUS_EMPTY) {
			gchar *pattern = g_strdup(MCE_LED_PATTERN_BATTERY_LOW);
			execute_datapipe(&led_pattern_activate_pipe, pattern, USE_CACHE, DONT_CACHE_INDATA);
			g_free(pattern);
		}
		else {
			execute_datapipe_output_triggers(&led_pattern_deactivate_pipe,
											MCE_LED_PATTERN_BATTERY_LOW,
											USE_INDATA);
		}
#endif /* SUPPORT_BATTERY_LOW_LED_PATTERN */

		/* Battery charge state */
		execute_datapipe(&battery_status_pipe,
						GINT_TO_POINTER(mcebat.status),
						USE_INDATA, CACHE_INDATA);

	}

	return FALSE;
}

/**
* Cancel processing of upower battery status changes
*/
static void
mcebat_update_cancel(void)
{
	if (mcebat_update_id)
		g_source_remove(mcebat_update_id), mcebat_update_id = 0;
}

/**
* Initiate delayed processing of upower battery status changes
*/
static void
mcebat_update_schedule(void)
{
	if (!mcebat_update_id)
		mcebat_update_id = g_timeout_add(UPDATE_DELAY, mcebat_update_cb, 0);
}

/**
* Get UPower devices properties
*/
static void
xup_properties_get_all(void)
{
	if (private.battery == NULL)
		return;

	if (private.charger)
	{
		g_object_get(private.charger, "online", &upowbat.charger_online, NULL);
		private.force_state = time(NULL) + FORCE_STATE_TIME;

		if (upowbat.charger_online)
		{
			if (upowbat.state == UP_DEVICE_STATE_DISCHARGING)
				upowbat.state = UP_DEVICE_STATE_CHARGING;
		}
		else if (upowbat.state == UP_DEVICE_STATE_CHARGING)
			upowbat.state = UP_DEVICE_STATE_DISCHARGING;
	}

	mcebat_update_schedule();
}

/**
* Check UPower device and add it to private if appropriate.
* If there're multiple batteries/chargers, we take the first suggested.
*/
static void xup_check_device(UpDevice *dev)
{
	gchar *native_path;
	guint  kind;
	guint  technology;
	gint   i;

	g_object_get(dev,
				"native-path", &native_path,
				"kind"       , &kind,
				"technology" , &technology,
				NULL);

	for (i = 0;  blacklist[i] != NULL;  i++)
	{
		if (!g_strcmp0(native_path, blacklist[i]))
			return;
	}

	if (kind == UP_DEVICE_KIND_BATTERY)
	{
		if (private.battery == NULL &&
			technology != UP_DEVICE_TECHNOLOGY_UNKNOWN)
		{
			private.battery = g_object_ref(dev);
		}
		return;
	}

	if (kind == UP_DEVICE_KIND_LINE_POWER &&
		private.charger == NULL)
	{
		private.charger = g_object_ref(dev);
	}
}

/**
* Find battery/charger devices and add them to private
*/
static void
xup_find_devices(void)
{
	GPtrArray *devices;
	guint      i;

	devices = up_client_get_devices2(private.client);

	for (i = 0;  i < devices->len;  i++)
	{
		UpDevice *device = g_ptr_array_index(devices, i);

		xup_check_device(device);

		if (private.battery && private.charger)
			break;
	}

	g_ptr_array_unref (devices);

	xup_properties_get_all();
}

/**
* Handle battery properties changes
*/
static void
xup_battery_properties_changed_cb(UpDevice *battery,
								GParamSpec *pspec,
								gpointer user_data)
{
	UNUSED(battery);
	UNUSED(pspec);
	UNUSED(user_data);

	mcebat_update_schedule();
}

/**
* Handle charger property changes
*/
static void
xup_charger_state_changed_cb(UpDevice *charger,
							GParamSpec *pspec,
							gpointer user_data)
{
	UNUSED(pspec);
	UNUSED(user_data);

	g_object_get(charger, "online", &upowbat.charger_online, NULL);

	if (mcebat.charger_connected)
		upowbat.state = UP_DEVICE_STATE_CHARGING;
	else
		upowbat.state = UP_DEVICE_STATE_DISCHARGING;

	private.force_state = time(NULL) + FORCE_STATE_TIME;

	mcebat_update_schedule();
}

/**
* Connect signal handlers to charger device
*/
static void
xup_charger_connect_handlers(void)
{
	g_signal_connect(private.charger, "notify::online",
					G_CALLBACK(xup_charger_state_changed_cb),
					NULL);
}

/**
* Connect signal handlers to battery device
*/
static void
xup_battery_connect_handlers(void)
{
	g_signal_connect(private.battery, "notify::percentage",
					G_CALLBACK(xup_battery_properties_changed_cb),
					NULL);

	g_signal_connect(private.battery, "notify::state",
					G_CALLBACK(xup_battery_properties_changed_cb),
					NULL);
}

/**
* Disconnect signal handlers from charger device
*/
static void
xup_charger_disconnect_handlers(void)
{
	g_signal_handlers_disconnect_by_func(
		private.charger, xup_charger_state_changed_cb, NULL);
}

/**
* Disconnect signal handlers from battery device
*/
static void
xup_battery_disconnect_handlers(void)
{
	g_signal_handlers_disconnect_by_func(
		private.battery, xup_battery_properties_changed_cb, NULL);
}

/**
* Remove charger device
*/
static void
xup_charger_remove_dev(void)
{
	if (private.charger == NULL)
		return;

	xup_charger_disconnect_handlers();
	g_object_unref(private.charger);
	private.charger = NULL;
	upowbat.charger_online = FALSE;
	mcebat.charger_connected = FALSE;
}

/**
* Remove battery device
*/
static void
xup_battery_remove_dev(void)
{
	if (private.battery == NULL)
		return;

	xup_battery_disconnect_handlers();
	g_object_unref(private.battery);
	private.battery = NULL;
	upowbat_init();
	mcebat_init();
}

/**
* Handle "device-removed" event
*/
static void
xup_device_removed_cb(UpClient *client,
					const char *object_path,
					gpointer user_data)
{
	UNUSED(client);
	UNUSED(user_data);

	if (private.battery && !g_strcmp0(up_device_get_object_path(private.battery), object_path))
	{
		mce_log(LL_DEBUG, "Battery device removed: %s", object_path);
		xup_battery_remove_dev();
		xup_charger_disconnect_handlers();
		mcebat_update_schedule();
		return;
	}

	if (private.charger && !g_strcmp0(up_device_get_object_path(private.charger), object_path))
	{
		mce_log(LL_DEBUG, "Charger device removed: %s", object_path);
		xup_charger_remove_dev();
		mcebat_update_schedule();
	}
}

/**
* Handle "device-added" event
*/
static void
xup_device_added_cb(UpClient *client,
					UpDevice *device,
					gpointer user_data)
{
	gboolean had_battery = private.battery != NULL;
	gboolean had_charger = private.charger != NULL;

	UNUSED(user_data);
	UNUSED(client);

	if (had_battery && had_charger)
		return;

	xup_check_device(device);

	/* Battery device was added */
	if (!had_battery && private.battery)
	{
		mce_log(LL_DEBUG, "Battery device added: %s", up_device_get_object_path(device));
		xup_properties_get_all();
		xup_battery_connect_handlers();
		if (had_charger)
			xup_charger_connect_handlers();
		return;
	}

	/* Charger device was added */
	if (!had_charger && private.charger && had_battery)
	{
		mce_log(LL_DEBUG, "Charger device added: %s", up_device_get_object_path(device));
		xup_properties_get_all();
		xup_charger_connect_handlers();
	}
}

/**
* Add UPower handlers
*/
static void
xup_set_callbacks(void)
{
	g_signal_connect(private.client, "device-added", G_CALLBACK (xup_device_added_cb), NULL);
	g_signal_connect(private.client, "device-removed", G_CALLBACK (xup_device_removed_cb), NULL);

	if (private.battery)
	{
		xup_battery_connect_handlers();

		if (private.charger)
			xup_charger_connect_handlers();
	}
}


/**
* Init function for the battery and charger module
* @param module  unused
* @return  NULL on success, a string with an error message on failure
* @todo    status needs to be set on error!
*/
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	UNUSED(module);
	private.client = up_client_new();

	if (private.client == NULL)
		return NULL;

	/* Reset data used by the state machine */
	mcebat_init();
	upowbat_init();

	private.min_voltage = mce_conf_get_int(MCE_CONF_BATTERY_SECTION, MCE_CONF_CRIT_VOLTAGE_KEY, 0, NULL)/1000;
	private.low_percentage = mce_conf_get_int(MCE_CONF_BATTERY_SECTION, MCE_CONF_LOW_PERCENT_KEY, 5, NULL);

	/* Find battery/charger devices and add them to private */
	xup_find_devices();

	/* Add UPower callbacks */
	xup_set_callbacks();

	return NULL;
}

/**
* Exit function for the battery and charger module
* @param module  unused
*/
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	UNUSED(module);

	if (private.client == NULL)
		return;

	xup_battery_remove_dev();
	xup_charger_remove_dev();
	g_object_unref(private.client);
	mcebat_update_cancel();
}
