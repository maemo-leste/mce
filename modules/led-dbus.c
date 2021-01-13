/* This module supplys a dbus interface to the notification led.*/

#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "mce-log.h"
#include "mce-dbus.h"
#include "mce-rtconf.h"
#include "datapipe.h"
#include "mce.h"
#include "mce-conf.h"

/** Module name */
#define MODULE_NAME		"led-dbus"
#define MODULE_PROVIDES	"led-dbus"

#define LED_SYSFS_PATH "/sys/class/leds/"
#define LED_BRIGHTNESS_PATH "/brightness"

#define MCE_CONF_LED_GROUP			"LED"
#define MCE_CONF_LED_PATTERNS		"LEDPatterns"

#define MCE_GCONF_LED_PATH			"/system/osso/dsm/leds"

#define DEFAULT_PATTERN_ENABLED			true

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_PROVIDES, NULL };

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 100
};

struct led_pattern {
	char* name;
	unsigned int gconf_cb_id;
	bool enabled;
};

static struct led_pattern *led_patterns = NULL;
static unsigned int patterns_count = 0;

/**
 * D-Bus callback for the activate LED pattern method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean led_activate_pattern_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;
	gchar *pattern = NULL;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	mce_log(LL_DEBUG, "%s: Received activate LED pattern request", MODULE_NAME);

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &pattern,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_ACTIVATE_LED_PATTERN,
			error.message);
		dbus_error_free(&error);
		return FALSE;
	}

	execute_datapipe(&led_pattern_activate_pipe, pattern, USE_INDATA, CACHE_INDATA);

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback for the deactivate LED pattern method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean led_deactivate_pattern_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;
	gchar *pattern = NULL;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	mce_log(LL_DEBUG, "%s: Received deactivate LED pattern request", MODULE_NAME);

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &pattern,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT, "%s: "
			"Failed to get argument from %s.%s: %s", MODULE_NAME,
			MCE_REQUEST_IF, MCE_DEACTIVATE_LED_PATTERN,
			error.message);
		dbus_error_free(&error);
		return FALSE;
	}
	execute_datapipe_output_triggers(&led_pattern_deactivate_pipe, pattern, USE_INDATA);

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback for the enable LED method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean led_enable_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "Received LED enable request");

	execute_datapipe_output_triggers(&led_enabled_pipe, GINT_TO_POINTER(true), USE_INDATA);

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback for the disable LED method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean led_disable_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "Received LED disable request");

	execute_datapipe_output_triggers(&led_enabled_pipe, GINT_TO_POINTER(false), USE_INDATA);

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

static struct led_pattern *find_pattern_id(unsigned int id)
{
	for (unsigned int i = 0; i < patterns_count; ++i) {
		if (led_patterns[i].gconf_cb_id == id)
			return &(led_patterns[i]);
	}
	return NULL;
}

static struct led_pattern *find_pattern_name(const gchar* name)
{
	for (unsigned int i = 0; i < patterns_count; ++i) {
		if (strcmp(led_patterns[i].name, name) == 0)
			return &(led_patterns[i]);
	}
	return NULL;
}

static void led_rtconf_cb(gchar *key, guint cb_id, void *user_data)
{
	(void)user_data;
	
	static struct led_pattern* pattern = NULL;
	
	if ((pattern = find_pattern_id(cb_id))) {
		gboolean tmp;
		if (mce_rtconf_get_bool(key, &tmp)) { 
			pattern->enabled = tmp;
			mce_log(LL_DEBUG, "%s: pattern %s id %u %s", 
					MODULE_NAME, pattern->name, cb_id, pattern->enabled ? "enabled" : "disabled");
			if (!pattern->enabled)
			execute_datapipe(&led_pattern_deactivate_pipe, pattern->name, USE_CACHE, CACHE_INDATA);
		}
	} else {
		mce_log(LL_WARN, "%s: Spurious rtconf value received; confused!", MODULE_NAME);
	}
}

static gboolean pattern_get_enabled_conf(const gchar *const patternname,
				    guint *gconf_cb_id)
{
	gboolean retval = DEFAULT_PATTERN_ENABLED;
	gchar *path = g_strconcat(MCE_GCONF_LED_PATH, "/", patternname, NULL);

	if (!mce_rtconf_get_bool(path, &retval))
		mce_log(LL_INFO, "%s: getting enabled status for %s from rtconf failed", MODULE_NAME, patternname);
	
	mce_log(LL_DEBUG, "%s: %s %s", MODULE_NAME, patternname, retval ? "enabled" : "disabled");
	mce_rtconf_notifier_add(MCE_GCONF_LED_PATH, path, led_rtconf_cb, NULL, gconf_cb_id);
	
	g_free(path);

	return retval;
}

static bool init_patterns(void)
{
	struct led_pattern *led_pattern = NULL;
	char **patternlist = NULL;
	gsize length;

	patternlist = mce_conf_get_string_list(MCE_CONF_LED_GROUP,
					       MCE_CONF_LED_PATTERNS,
					       &length, NULL);

	if (patternlist == NULL) {
		mce_log(LL_WARN, "%s: Failed to configure led patterns", MODULE_NAME);
		return false;
	}

	led_patterns = malloc(length*sizeof(*led_pattern));
	if (!led_patterns) {
		g_strfreev(patternlist);
		return false;
	}

	for (int i = 0; patternlist[i]; ++i) {
		struct led_pattern *pattern;
		
		pattern = &led_patterns[patterns_count];
		pattern->gconf_cb_id = 0;
		pattern->enabled = pattern_get_enabled_conf(patternlist[i], &pattern->gconf_cb_id);
		pattern->name = strdup(patternlist[i]);
		++patterns_count;
	}
	mce_log(LL_DEBUG, "%s: found %i patterns", MODULE_NAME, patterns_count);
	g_strfreev(patternlist);

	return true;
}

static gpointer led_pattern_activate_filter(gpointer data)
{
	const gchar * const name = (const gchar * const)data;
	if (name) {
		mce_log(LL_DEBUG, "%s: %s", MODULE_NAME, name);
		static struct led_pattern* pattern = NULL;
		if ((pattern = find_pattern_name(name))) {
			mce_log(LL_DEBUG, "%s: found name: %s", MODULE_NAME, name);
			if(pattern->enabled) return data;
		} else {
			mce_log(LL_DEBUG, "%s: did not find name: %s", MODULE_NAME, name);
		}
	}
	return NULL;
}

/**
 * Init function for the LED logic module
 *
 * @todo XXX status needs to be set on error!
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;
	
	/* req_led_pattern_activate */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ACTIVATE_LED_PATTERN,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 led_activate_pattern_dbus_cb) == NULL) {
			mce_log(LL_CRIT, "%s: Adding %s debus handler failed", MODULE_NAME, MCE_ACTIVATE_LED_PATTERN);
			return NULL;
		}

	/* req_led_pattern_deactivate */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEACTIVATE_LED_PATTERN,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 led_deactivate_pattern_dbus_cb) == NULL) {
			mce_log(LL_CRIT, "%s: Adding %s debus handler failed", MODULE_NAME, MCE_DEACTIVATE_LED_PATTERN);
			return NULL;
		}

	/* req_led_enable */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ENABLE_LED,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 led_enable_dbus_cb) == NULL) {
			mce_log(LL_CRIT, "%s: Adding %s debus handler failed", MODULE_NAME, MCE_ENABLE_LED);
			return NULL;
		}

	/* req_led_disable */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISABLE_LED,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 led_disable_dbus_cb) == NULL) {
			mce_log(LL_CRIT, "%s: Adding %s debus handler failed", MODULE_NAME, MCE_DISABLE_LED);
			return NULL;
		}

	if (init_patterns()) {
		append_filter_to_datapipe(&led_pattern_activate_pipe, led_pattern_activate_filter);
	}
	
	return NULL;
}

/**
 * Exit function for the LED logic module
 *
 * @todo D-Bus unregistration
 *
 * @param module Unused
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;
	remove_filter_from_datapipe(&led_pattern_activate_pipe, led_pattern_activate_filter);
	if (led_patterns) {
		for (unsigned int i = 0; i < patterns_count; ++i)
			free(led_patterns[i].name);
		free(led_patterns);
	}
}
