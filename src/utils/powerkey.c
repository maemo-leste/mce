/**
 * @file powerkey.c
 * Power key logic for the Mode Control Entity
 * <p>
 * Copyright © 2004-2010 Nokia Corporation and/or its subsidiary(-ies).
 * <p>
 * @author David Weinehall <david.weinehall@nokia.com>
 * @author Jonathan Wilson <jfwfreo@tpgi.com.au>
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
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <systemui/dbus-names.h>
#include <systemui/powerkeymenu-dbus-names.h>
#include "mce.h"
#include "powerkey.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "datapipe.h"

/**
 * The ID of the timeout used when determining
 * whether the key press was short or long
 */
static guint powerkey_timeout_cb_id = 0;

/**
 * The ID of the timeout used when determining
 * whether the key press was a double press
 */
static guint doublepress_timeout_cb_id = 0;

static gboolean initialised = FALSE;

static submode_t power_trigger_submode = MCE_INVALID_SUBMODE;

/** Time in milliseconds before the key press is considered medium */
static gint mediumdelay = DEFAULT_POWER_MEDIUM_DELAY;
/** Time in milliseconds before the key press is considered long */
static gint longdelay = DEFAULT_POWER_LONG_DELAY;
/** Timeout in milliseconds during which key press is considered double */
static gint doublepressdelay = DEFAULT_POWER_DOUBLE_DELAY;
/** Action to perform on a short key press */
static poweraction_t shortpressaction = DEFAULT_POWERKEY_SHORT_ACTION;
/** Action to perform on a long key press */
static poweraction_t longpressaction = DEFAULT_POWERKEY_LONG_ACTION;
/** Action to perform on a double key press */
static poweraction_t doublepressaction = DEFAULT_POWERKEY_DOUBLE_ACTION;

static void cancel_powerkey_timeout(void);


static gboolean can_show_menu(void)
{
	alarm_ui_state_t alarm_ui_state = datapipe_get_gint(alarm_ui_state_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);

	return ((MCE_ALARM_UI_VISIBLE_INT32 == alarm_ui_state) ||
				(MCE_ALARM_UI_RINGING_INT32 == alarm_ui_state) ||
				(CALL_STATE_SERVICE == call_state));
}

/**
 * Open/close the powerkey menu
 *
 * @param enable TRUE to open the powerkey menu, FALSE to close it
 * @return TRUE on success, FALSE on failure
 */
static gboolean device_menu(const gboolean enable)
{
	const gchar *const cb_service = MCE_SERVICE;
	const gchar *const cb_path = MCE_REQUEST_PATH;
	const gchar *const cb_interface = MCE_REQUEST_IF;
	const gchar *const cb_method = MCE_POWERKEY_CB_REQ;
	DBusMessage *reply = NULL;
	gboolean status = FALSE;
	dbus_int32_t retval;
	dbus_uint32_t mode;
	DBusError error;

	dbus_error_init(&error);

	mode = (datapipe_get_gint(mode_pipe) ==
		MCE_FLIGHT_MODE_INT32) ? MODE_FLIGHT : MODE_NORMAL;

	reply = dbus_send_with_block(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
				     SYSTEMUI_REQUEST_IF,
				     enable ? SYSTEMUI_POWERKEYMENU_OPEN_REQ :
					      SYSTEMUI_POWERKEYMENU_CLOSE_REQ,
				     DEFAULT_DBUS_REPLY_TIMEOUT,
				     DBUS_TYPE_STRING, &cb_service,
				     DBUS_TYPE_STRING, &cb_path,
				     DBUS_TYPE_STRING, &cb_interface,
				     DBUS_TYPE_STRING, &cb_method,
				     DBUS_TYPE_UINT32, &mode,
				     DBUS_TYPE_INVALID);

	if (reply == NULL)
		goto EXIT;

	if (dbus_message_get_args(reply, &error,
				  DBUS_TYPE_INT32, &retval,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get reply from %s.%s: %s",
			SYSTEMUI_REQUEST_IF,
			enable ? SYSTEMUI_POWERKEYMENU_OPEN_REQ :
				 SYSTEMUI_POWERKEYMENU_CLOSE_REQ,
			error.message);
		dbus_message_unref(reply);
		dbus_error_free(&error);
		goto EXIT;
	}

	dbus_message_unref(reply);

	switch (retval) {
	case -3:
		mce_add_submode_int32(MCE_DEVMENU_SUBMODE);
		break;

	case -2:
		mce_log(LL_ERR,
			"Device menu already opened another by other process");
		goto EXIT;

	case 0:
		mce_rem_submode_int32(MCE_DEVMENU_SUBMODE);
		break;

	default:
		mce_log(LL_ERR,
			"Unknown return value received from the device menu");
		goto EXIT;
	}

	status = TRUE;

EXIT:
	return status;
}

/**
 * Timeout callback for double key press
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean doublepress_timeout_cb(gpointer data)
{
	(void)data;

	doublepress_timeout_cb_id = 0;

	return FALSE;
}

/**
 * Cancel doublepress timeout
 */
static void cancel_doublepress_timeout(void)
{
	/* Remove the timeout source for the [power] double key press handler */
	if (doublepress_timeout_cb_id != 0) {
		g_source_remove(doublepress_timeout_cb_id);
		doublepress_timeout_cb_id = 0;
	}
}

static void setup_doublepress_timeout(void)
{
	cancel_doublepress_timeout();

	/* Setup new timeout */
	doublepress_timeout_cb_id =
		g_timeout_add(doublepressdelay, doublepress_timeout_cb, NULL);
}

static void generic_powerkey_handler(poweraction_t action)
{
	submode_t submode = mce_get_submode_int32();

	switch (action) {
	case POWER_DISABLED:
		break;

	case POWER_MENU:
		if ( can_show_menu() )
			break;

		if (submode == MCE_NORMAL_SUBMODE || submode == MCE_AUTORELOCK_SUBMODE) {
			(void)device_menu(TRUE);
		}

		break;

	case POWER_POWEROFF:
	default:
		if ((submode & MCE_DEVMENU_SUBMODE) != 0) {
			(void)device_menu(FALSE);
			mce_rem_submode_int32(MCE_DEVMENU_SUBMODE);
		}

		if ((submode & MCE_TKLOCK_SUBMODE) == 0) {
			mce_log(LL_WARN,
				"Requesting shutdown from "
				"powerkey.c: generic_powerkey_handler(); "
				"action: %d",
				action);

			execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_OFF), USE_INDATA, CACHE_INDATA);
		}

		break;

	case POWER_SOFT_POWEROFF:
		if ( can_show_menu() )
			break;

		if ((submode & MCE_DEVMENU_SUBMODE) != 0) {
			(void)device_menu(FALSE);
			mce_rem_submode_int32(MCE_DEVMENU_SUBMODE);
		}

		if ((submode & MCE_TKLOCK_SUBMODE) == 0) {
			execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_SOFT_OFF), USE_INDATA, CACHE_INDATA);
		}

		break;

	case POWER_TKLOCK:
		if ((submode & MCE_DEVMENU_SUBMODE) != 0) {
			(void)device_menu(FALSE);
			mce_rem_submode_int32(MCE_DEVMENU_SUBMODE);
		}

		if ((submode & MCE_TKLOCK_SUBMODE) == 0) {
			execute_datapipe(&tk_lock_pipe,
					 GINT_TO_POINTER(LOCK_ON),
					 USE_INDATA, CACHE_INDATA);
		}

		break;
	}
}

/**
 * Logic for short key press
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean handle_shortpress(void)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	gboolean status = FALSE;

	cancel_powerkey_timeout();

	if (doublepress_timeout_cb_id == 0) {
		setup_doublepress_timeout();

		if (system_state == MCE_STATE_USER) {
			generic_powerkey_handler(shortpressaction);
		}
	} else {
		cancel_doublepress_timeout();
		generic_powerkey_handler(doublepressaction);
	}

	status = TRUE;

//EXIT:
	return status;
}

/**
 * Logic for long key press
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean handle_longpress(void)
{
	system_state_t state = datapipe_get_gint(system_state_pipe);
	submode_t submode = mce_get_submode_int32();
	gboolean status = TRUE;

	powerkey_timeout_cb_id = 0;

	/* Ignore keypress if the alarm UI is visible */
	if ( can_show_menu() )
		goto EXIT;

	/* Ignore if we're already shutting down/rebooting */
	switch (state) {
	case MCE_STATE_SHUTDOWN:
	case MCE_STATE_REBOOT:
		status = FALSE;
		break;

	case MCE_STATE_ACTDEAD:
		execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_ON), USE_INDATA, CACHE_INDATA);
		break;

	case MCE_STATE_USER:
		/* If softoff is enabled, wake up
		 * Otherwise, perform long press action
		 */
		if ((submode & MCE_SOFTOFF_SUBMODE)) {
			execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_SOFT_ON), USE_INDATA, CACHE_INDATA);
		} else {
			generic_powerkey_handler(longpressaction);
		}

		break;

	default:
		if ((submode & MCE_DEVMENU_SUBMODE) != 0) {
			(void)device_menu(FALSE);
			mce_rem_submode_int32(MCE_DEVMENU_SUBMODE);
		}

		/* If no special cases are needed,
		 * just do a regular shutdown
		 */
		mce_log(LL_WARN,
			"Requesting shutdown from "
			"powerkey.c: handle_longpress(); state: %d",
			state);

		execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_OFF), USE_INDATA, CACHE_INDATA);
		break;
	}

EXIT:
	return status;
}

/**
 * Timeout callback for long key press
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean powerkey_timeout_cb(gpointer data)
{
        system_state_t system_state = datapipe_get_gint(system_state_pipe);
	submode_t submode = mce_get_submode_int32();

	(void)data;

	powerkey_timeout_cb_id = 0;

	if ((system_state == MCE_STATE_ACTDEAD) ||
	    ((submode & MCE_SOFTOFF_SUBMODE) != 0)) {
		execute_datapipe_output_triggers(&vibrator_pattern_deactivate_pipe, MCE_VIBRATOR_PATTERN_POWER_KEY_PRESS, USE_INDATA);
	}

	handle_longpress();

	return FALSE;
}

/**
 * Cancel powerkey timeout
 */
static void cancel_powerkey_timeout(void)
{
	/* Remove the timeout source for the [power] long key press handler */
	if (powerkey_timeout_cb_id != 0) {
		g_source_remove(powerkey_timeout_cb_id);
		powerkey_timeout_cb_id = 0;
	}
}

/**
 * Setup powerkey timeout
 */
static void setup_powerkey_timeout(gint powerkeydelay)
{
	cancel_powerkey_timeout();

	/* Setup new timeout */
	powerkey_timeout_cb_id =
		g_timeout_add(powerkeydelay, powerkey_timeout_cb, NULL);
}

/**
 * D-Bus callback for powerkey event triggering
 *
 * @param msg D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean trigger_powerkey_event_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	dbus_bool_t result;
	gboolean status = FALSE;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received [power] button trigger request");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_BOOLEAN, &result,
				  DBUS_TYPE_INVALID) == FALSE) {
		// XXX: should we return an error instead?
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_TRIGGER_POWERKEY_EVENT_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	mce_log(LL_DEBUG, "[power] button event trigger value: %d", result);

	if (result == TRUE) {
		handle_longpress();
	} else {
		handle_shortpress();
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

static gboolean systemui_device_menu_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	dbus_int32_t result = INT_MAX;
	gboolean status = FALSE;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received device menu callback");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_INT32, &result,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_POWERKEY_CB_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	mce_log(LL_DEBUG, "Device menu callback value: %d", result);

	mce_rem_submode_int32(MCE_DEVMENU_SUBMODE);

	switch (result) {
	case POWER_KEY_MENU_RESPONSE_TKLOCK:
		execute_datapipe(&tk_lock_pipe,
				 GINT_TO_POINTER(LOCK_ON),
				 USE_INDATA, CACHE_INDATA);
		break;

	case POWER_KEY_MENU_RESPONSE_DEVICELOCK:
		execute_datapipe(&device_lock_pipe,
				 GINT_TO_POINTER(LOCK_ON),
				 USE_INDATA, CACHE_INDATA);
		break;

	case POWER_KEY_MENU_RESPONSE_NORMALMODE:
		mce_set_device_mode_int32(MCE_NORMAL_MODE_INT32);
		break;

	case POWER_KEY_MENU_RESPONSE_FLIGHTMODE:
		mce_set_device_mode_int32(MCE_FLIGHT_MODE_INT32);
		break;

	case POWER_KEY_MENU_RESPONSE_REBOOT:
		execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_REBOOT), USE_INDATA, CACHE_INDATA);
		break;

	case POWER_KEY_MENU_RESPONSE_SOFT_POWEROFF:
		execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_SOFT_OFF), USE_INDATA, CACHE_INDATA);
		break;

	case POWER_KEY_MENU_RESPONSE_POWEROFF:
		mce_log(LL_WARN,
			"Requesting shutdown from "
			"powerkey.c: systemui_device_menu_dbus_cb(); "
			"result: %d",
			result);

		execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_OFF), USE_INDATA, CACHE_INDATA);
		break;

	case -6:
	case -4:
	case -1:
		break;

	case 0:
	default:
		break;
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

static void device_mode_trigger(gconstpointer data)
{
	submode_t submode = datapipe_get_gint(submode_pipe);

	(void)data;

	if ((submode & MCE_DEVMENU_SUBMODE) != 0) {
		(void)device_menu(TRUE);
	}
}

/**
 * Datapipe trigger for the [power] key
 *
 * @param data A pointer to the input_event struct
 */
static void powerkey_trigger(gconstpointer const data)
{
    system_state_t system_state = datapipe_get_gint(system_state_pipe);
	submode_t submode = mce_get_submode_int32();
	struct input_event const *const *evp;
	struct input_event const *ev;

	/* Don't dereference until we know it's safe */
	if (data == NULL)
		goto EXIT;

	evp = data;
	ev = *evp;

	if ((ev != NULL) && (ev->code == power_keycode)) {
		/* If set, the [power] key was pressed */
		if (ev->value == 1) {
			mce_log(LL_DEBUG, "[power] pressed");

			power_trigger_submode = mce_get_submode_int32();
			if (!(power_trigger_submode & MCE_EVEATER_SUBMODE))
			{
				/* Setup new timeout */
				if ((system_state == MCE_STATE_ACTDEAD) ||
				    ((submode & MCE_SOFTOFF_SUBMODE) != 0)) {
					execute_datapipe_output_triggers(&led_pattern_activate_pipe, MCE_LED_PATTERN_POWER_ON, USE_INDATA);
					execute_datapipe_output_triggers(&vibrator_pattern_activate_pipe, MCE_VIBRATOR_PATTERN_POWER_KEY_PRESS, USE_INDATA);
					/* Shorter delay for startup
					 * than for shutdown
					*/
					setup_powerkey_timeout(mediumdelay);
				} else {
					setup_powerkey_timeout(longdelay);
				}
			}
		} else if (ev->value == 0) {
			mce_log(LL_DEBUG, "[power] released");
			mce_log(LL_DEBUG, "powerkey_trigger: mce_get_submode_int32() = %d",mce_get_submode_int32());
			if (!(power_trigger_submode & MCE_EVEATER_SUBMODE))
			{
				mce_log(LL_DEBUG, "powerkey_trigger: MCE_EVEATER_SUBMODE != mce_get_submode_int32() ==  %d",mce_get_submode_int32());
			/* Short key press */
				if (powerkey_timeout_cb_id != 0) {
					handle_shortpress();
	
					if ((system_state == MCE_STATE_ACTDEAD) ||
					    ((submode & MCE_SOFTOFF_SUBMODE) != 0)) {
						execute_datapipe_output_triggers(&led_pattern_deactivate_pipe, MCE_LED_PATTERN_POWER_ON, USE_INDATA);
						execute_datapipe_output_triggers(&vibrator_pattern_deactivate_pipe, MCE_VIBRATOR_PATTERN_POWER_KEY_PRESS, USE_INDATA);
					}
				}
			}
		}
	}

EXIT:
	return;
}

static void call_state_trigger(gconstpointer data)
{
	submode_t submode = mce_get_submode_int32();
	call_state_t call_state = datapipe_get_gint(call_state_pipe);

	(void)data;

	if((CALL_STATE_SERVICE == call_state) && (submode & MCE_DEVMENU_SUBMODE) != 0)
	{
		(void)device_menu(FALSE);
		mce_rem_submode_int32(MCE_DEVMENU_SUBMODE);
	}
	;
}

/**
 * Parse the [power] action string
 *
 * @todo Implement this using string to enum mappings instead,
 *       to allow for better debugging messages and a generic parser
 *
 * @param string The string to parse
 * @param action A pointer to the variable to store the action in
 * @return TRUE if the string contained a valid action,
 *         FALSE if the action was invalid
 */
static gboolean parse_action(char *string, poweraction_t *action)
{
	gboolean status = FALSE;

	if (!strcmp(string, POWER_DISABLED_STR)) {
		*action = POWER_DISABLED;
	} else if (!strcmp(string, POWER_MENU_STR)) {
		*action = POWER_MENU;
	} else if (!strcmp(string, POWER_POWEROFF_STR)) {
		*action = POWER_POWEROFF;
	} else if (!strcmp(string, POWER_SOFT_POWEROFF_STR)) {
		*action = POWER_SOFT_POWEROFF;
	} else if (!strcmp(string, POWER_TKLOCK_STR)) {
		*action = POWER_TKLOCK;
	} else {
		mce_log(LL_WARN,
			"Unknown [power] action; "
			"using default");
		goto EXIT;
	}

	status = TRUE;

EXIT:
	return status;
}

/**
 * Init function for the powerkey component
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_powerkey_init(void)
{
	gboolean status = FALSE;
	gchar *tmp = NULL;

	(void)device_menu(FALSE);

	/* Append triggers/filters to datapipes */
	append_input_trigger_to_datapipe(&keypress_pipe,
					 powerkey_trigger);
	append_output_trigger_to_datapipe(&mode_pipe,
					  device_mode_trigger);
  append_output_trigger_to_datapipe(&call_state_pipe,
            call_state_trigger);

	initialised = 1;

	/* req_trigger_powerkey_event */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_TRIGGER_POWERKEY_EVENT_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 trigger_powerkey_event_req_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_POWERKEY_CB_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 systemui_device_menu_dbus_cb) == NULL)
		goto EXIT;

	/* Get configuration options */
	longdelay = mce_conf_get_int(MCE_CONF_POWERKEY_GROUP,
				     MCE_CONF_POWERKEY_LONG_DELAY,
				     DEFAULT_POWER_LONG_DELAY,
				     NULL);
	mediumdelay = mce_conf_get_int(MCE_CONF_POWERKEY_GROUP,
				       MCE_CONF_POWERKEY_MEDIUM_DELAY,
				       DEFAULT_POWER_MEDIUM_DELAY,
				       NULL);
	tmp = mce_conf_get_string(MCE_CONF_POWERKEY_GROUP,
				  MCE_CONF_POWERKEY_SHORT_ACTION,
				  "",
				  NULL);

	/* Since we've set a default, error handling is unnecessary */
	(void)parse_action(tmp, &shortpressaction);
	g_free(tmp);

	tmp = mce_conf_get_string(MCE_CONF_POWERKEY_GROUP,
				  MCE_CONF_POWERKEY_LONG_ACTION,
				  "",
				  NULL);

	/* Since we've set a default, error handling is unnecessary */
	(void)parse_action(tmp, &longpressaction);
	g_free(tmp);

	doublepressdelay = mce_conf_get_int(MCE_CONF_POWERKEY_GROUP,
					    MCE_CONF_POWERKEY_DOUBLE_DELAY,
					    DEFAULT_POWER_DOUBLE_DELAY,
					    NULL);
	tmp = mce_conf_get_string(MCE_CONF_POWERKEY_GROUP,
				  MCE_CONF_POWERKEY_DOUBLE_ACTION,
				  "",
				  NULL);

	/* Since we've set a default, error handling is unnecessary */
	(void)parse_action(tmp, &doublepressaction);
	g_free(tmp);

	status = TRUE;

EXIT:
	return status;
}

/**
 * Exit function for the powerkey component
 *
 * @todo D-Bus unregistration
 */
void mce_powerkey_exit(void)
{
	/* Remove triggers/filters from datapipes */
  remove_output_trigger_from_datapipe(&call_state_pipe,
              call_state_trigger);
	remove_output_trigger_from_datapipe(&mode_pipe,
					    device_mode_trigger);
	remove_input_trigger_from_datapipe(&keypress_pipe,
					   powerkey_trigger);

	/* Remove all timer sources */
	cancel_powerkey_timeout();
	cancel_doublepress_timeout();

	return;
}
