/* This module supplys a dbus interface to the notification led.*/

#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "mce-log.h"
#include "mce-dbus.h"
#include "datapipe.h"
#include "mce.h"

/** Module name */
#define MODULE_NAME		"led-dbus"
#define MODULE_PROVIDES	"led-dbus"

#define LED_SYSFS_PATH "/sys/class/leds/"
#define LED_BRIGHTNESS_PATH "/brightness"

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

	mce_log(LL_DEBUG, "Received activate LED pattern request");

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

	execute_datapipe_output_triggers(&led_pattern_activate_pipe, pattern, USE_INDATA);

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
}
