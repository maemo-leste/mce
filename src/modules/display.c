/**
 * @file display.c
 * Display module -- this implements display handling for MCE
 * <p>
 * Copyright © 2007-2010 Nokia Corporation and/or its subsidiary(-ies).
 * <p>
 * @author David Weinehall <david.weinehall@nokia.com>
 * @author Jonathan Wilson <jfwfreo@tpgi.com.au>
 * @author Carl Philipp Klemm <carl@uvos.xyz>
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
#include <gmodule.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <mce/mode-names.h>
#include "mce.h"
#include "display.h"
#include "mce-io.h"
#include "mce-lib.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "mce-rtconf.h"
#include "mce-conf.h"
#include "datapipe.h"

/** Module name */
#define MODULE_NAME		"display"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_NAME, NULL };

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 250
};

static gint dim_brightness;

/** GConf callback ID for display brightness setting */
static guint disp_brightness_gconf_cb_id = 0;

/** Display blanking timeout setting */
static gint disp_blank_timeout = DEFAULT_BLANK_TIMEOUT;

/** Cached brightness */
static gint cached_brightness = -1;

/** Target brightness */
static gint target_brightness = -1;

static gint set_brightness = -1;

static gint set_brightness_unfiltered = -1;

/** Fadeout step length */
static gint brightness_fade_steplength = 2;

/** Brightness fade timeout callback ID */
static gint brightness_fade_timeout_cb_id = 0;
/** Display blanking timeout callback ID */
static gint blank_timeout_cb_id = 0;

/** Maximum display brightness */
static gint maximum_display_brightness = DEFAULT_MAXIMUM_DISPLAY_BRIGHTNESS;

static gchar *brightness_file = NULL;
static gchar *max_brightness_file = NULL;
static gboolean hw_display_fading = FALSE;

static gboolean is_tvout_state_changed = FALSE;

static gboolean display_brightness_dbus_signal(void);

/**
 * Timeout callback for the brightness fade
 *
 * @param data Unused
 * @return Returns TRUE to repeat, until the cached brightness has reached
 *         the destination value; when this happens, FALSE is returned
 */
static gboolean brightness_fade_timeout_cb(gpointer data)
{
	gboolean retval = TRUE;

	(void)data;

	if ((cached_brightness == -1) ||
	    (ABS(cached_brightness -
		 target_brightness) < brightness_fade_steplength)) {
		cached_brightness = target_brightness;
		retval = FALSE;
	} else if (target_brightness > cached_brightness) {
		cached_brightness += brightness_fade_steplength;
	} else {
		cached_brightness -= brightness_fade_steplength;
	}

	mce_write_number_string_to_file(brightness_file,
					cached_brightness);

	if (retval == FALSE)
		 brightness_fade_timeout_cb_id = 0;

	return retval;
}

/**
 * Cancel the brightness fade timeout
 */
static void cancel_brightness_fade_timeout(void)
{
	/* Remove the timeout source for the display brightness fade */
	if (brightness_fade_timeout_cb_id != 0) {
		g_source_remove(brightness_fade_timeout_cb_id);
		brightness_fade_timeout_cb_id = 0;
	}
}

/**
 * Setup the brightness fade timeout
 *
 * @param step_time The time between each brightness step
 */
static void setup_brightness_fade_timeout(gint step_time)
{
	cancel_brightness_fade_timeout();

	/* Setup new timeout */
	brightness_fade_timeout_cb_id =
		g_timeout_add(step_time, brightness_fade_timeout_cb, NULL);
}

/**
 * Update brightness fade
 *
 * Will fade from current value to new value
 *
 * @param new_brightness The new brightness to fade to
 */
static void update_brightness_fade(gint new_brightness)
{
	gint step_time = 10;

	if (hw_display_fading == TRUE) {
		cancel_brightness_fade_timeout();
		cached_brightness = new_brightness;
		target_brightness = new_brightness;
		mce_write_number_string_to_file(brightness_file,
						new_brightness);
		goto EXIT;
	}

	/* If we're already fading towards the right brightness,
	 * don't change anything
	 */
	if (target_brightness == new_brightness)
		goto EXIT;

	target_brightness = new_brightness;

	brightness_fade_steplength = 2;

	setup_brightness_fade_timeout(step_time);

EXIT:
	return;
}

/**
 * Blank display
 */
static void display_blank(void)
{
	cancel_brightness_fade_timeout();
	cached_brightness = 0;
	target_brightness = 0;
	mce_write_number_string_to_file(brightness_file, 0);
}

/**
 * Dim display
 */
static void display_dim(void)
{
	update_brightness_fade((maximum_display_brightness * dim_brightness) / 100);
}

/**
 * Unblank display
 */
static void display_unblank(void)
{
	/* If we unblank, switch on display immediately */
	if (cached_brightness == 0) {
		cached_brightness = set_brightness;
		target_brightness = set_brightness;
		mce_write_number_string_to_file(brightness_file,
						set_brightness);
	} else {
		update_brightness_fade(set_brightness);
	}
}

/**
 * Display brightness trigger
 *
 * @note A brightness request is only sent if the value changed
 * @param data The display brightness stored in a pointer
 */
static void display_brightness_trigger(gconstpointer data)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	gint new_brightness = GPOINTER_TO_INT(data);

	/* If the pipe is choked, ignore the value */
	if (new_brightness == 0)
		goto EXIT;

	/* Adjust the value, since it's a percentage value */
	new_brightness = (maximum_display_brightness * new_brightness) / 100;

	/* If we're just rehashing the same brightness value, don't bother */
	if ((new_brightness == cached_brightness) && (cached_brightness != -1))
		goto EXIT;

	/* The value we have here is for non-dimmed screen only */
	set_brightness = new_brightness;

	if ((display_state == MCE_DISPLAY_OFF) ||
	    (display_state == MCE_DISPLAY_DIM))
		goto EXIT;

	update_brightness_fade(new_brightness);

EXIT:
	return;
}

/**
 * Timeout callback for display blanking
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean blank_timeout_cb(gpointer data)
{
	(void)data;

	blank_timeout_cb_id = 0;

	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_OFF),
			       USE_INDATA, CACHE_INDATA);

	return FALSE;
}

/**
 * Cancel the display blanking timeout
 */
static void cancel_blank_timeout(void)
{
	/* Remove the timeout source for display blanking */
	if (blank_timeout_cb_id != 0) {
		g_source_remove(blank_timeout_cb_id);
		blank_timeout_cb_id = 0;
	}
}

/**
 * Setup blank timeout
 */
static void setup_blank_timeout(void)
{
	cancel_blank_timeout();

	/* Setup new timeout */
	blank_timeout_cb_id =
		g_timeout_add_seconds(disp_blank_timeout,
				      blank_timeout_cb, NULL);
}

/**
 * rtconf callback for display related settings
 *
 * @param key Unused
 * @param cb_id Connection ID from gconf_client_notify_add()
 * @param user_data Unused
 */
static void display_rtconf_cb(const gchar *key, guint cb_id, void *user_data)
{
	(void)key;
	(void)user_data;

	if (cb_id == disp_brightness_gconf_cb_id) {
		gint tmp;
		if (mce_rtconf_get_int(MCE_BRIGHTNESS_KEY, &tmp)) {
			execute_datapipe(&display_brightness_pipe, GINT_TO_POINTER(tmp), USE_INDATA, CACHE_INDATA);
			set_brightness_unfiltered = tmp;
			display_brightness_dbus_signal();
		}
	} else {
		mce_log(LL_WARN, "%s: Spurious rtconf value received; confused!", MODULE_NAME);
	}
}

/**
 * Send a display status reply or signal
 *
 * @param method_call A DBusMessage to reply to;
 *                    pass NULL to send a display status signal instead
 * @return TRUE on success, FALSE on failure
 */
static gboolean send_display_status(DBusMessage *const method_call)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	gboolean is_tvout_on = datapipe_get_gint(tvout_pipe);

	DBusMessage *msg = NULL;
	const gchar *state = NULL;
	gboolean status = FALSE;

	switch (display_state) {
	case MCE_DISPLAY_OFF:
		state = MCE_DISPLAY_OFF_STRING;
		break;

	case MCE_DISPLAY_DIM:
		state = MCE_DISPLAY_DIM_STRING;
		break;

	case MCE_DISPLAY_ON:
	default:
		state = MCE_DISPLAY_ON_STRING;
		break;
	}
	if ((is_tvout_state_changed == TRUE) && (display_state == MCE_DISPLAY_OFF)) {
		state = is_tvout_on ? MCE_DISPLAY_ON_STRING : MCE_DISPLAY_OFF_STRING;
	}
	mce_log(LL_DEBUG,
		"%s: Sending display status: %s", MODULE_NAME,
		state);

	/* If method_call is set, send a reply,
	 * otherwise, send a signal
	 */

	if ((is_tvout_on) && (display_state == MCE_DISPLAY_OFF) && (!is_tvout_state_changed)){
		goto EXIT;
	}
	else
	{
		if (method_call != NULL) {
			msg = dbus_new_method_reply(method_call);
		} else {
			/* display_status_ind */
			msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
						  MCE_DISPLAY_SIG);
		}
		/* Append the display status */
		if (dbus_message_append_args(msg,
						 DBUS_TYPE_STRING, &state,
						 DBUS_TYPE_INVALID) == FALSE) {
			mce_log(LL_CRIT,
				"Failed to append %sargument to D-Bus message "
				"for %s.%s",
				method_call ? "reply " : "",
				method_call ? MCE_REQUEST_IF :
						  MCE_SIGNAL_IF,
				method_call ? MCE_DISPLAY_STATUS_GET :
						  MCE_DISPLAY_SIG);
			dbus_message_unref(msg);
			goto EXIT;
		}
		/* Send the message */
		status = dbus_send_message(msg);
	}
EXIT:

	return status;
}

/**
 * D-Bus callback for the get display status method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_status_get_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display status get request");

	/* Try to send a reply that contains the current display status */
	if (send_display_status(msg) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

/**
 * D-Bus callback for the display on method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_on_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	submode_t submode = mce_get_submode_int32();
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display on request");

	if ((submode & MCE_TKLOCK_SUBMODE) == 0) {
		mce_log(LL_DEBUG, "MCE_DISPLAY_ON in %s %s %d",__FILE__, __func__, __LINE__);
		(void)execute_datapipe(&display_state_pipe,
				       GINT_TO_POINTER(MCE_DISPLAY_ON),
				       USE_INDATA, CACHE_INDATA);
	}

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback for the display dim method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_dim_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	submode_t submode = mce_get_submode_int32();
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display dim request");

	/* If the tklock is active, ignore the request */
	if ((submode & MCE_TKLOCK_SUBMODE) == 0) {
		(void)execute_datapipe(&display_state_pipe,
				       GINT_TO_POINTER(MCE_DISPLAY_DIM),
				       USE_INDATA, CACHE_INDATA);
	}

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback for the display off method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_off_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display off request");

	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_OFF),
			       USE_INDATA, CACHE_INDATA);

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

static gboolean display_brightness_dbus_signal(void)
{
	DBusMessage *msg = NULL;
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"%s: Sending display brightness state: %i", MODULE_NAME,
		set_brightness_unfiltered);

	msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
					MCE_DISPLAY_BRIGTNESS_SIG);

	dbus_int32_t tmp = set_brightness_unfiltered;
	/* Append the inactivity status */
	if (dbus_message_append_args(msg,
					 DBUS_TYPE_INT32, &tmp,
					 DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT, "%s: "
			"Failed to append reply argument to D-Bus message "
			"for %s.%s", MODULE_NAME,
						 MCE_SIGNAL_IF,
						 MCE_DISPLAY_BRIGTNESS_SIG);
		dbus_message_unref(msg);
		return status;
	}

	/* Send the message */
	status = dbus_send_message(msg);

	return status;
}

static gboolean display_brightness_set_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "%s: Received display brightness set request", MODULE_NAME);

	dbus_int32_t brightness;
	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_INT32, &brightness,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_DISPLAY_BRIGTNESS_SET,
			error.message);
		dbus_error_free(&error);
		return FALSE;
	}

	set_brightness_unfiltered = brightness;
	execute_datapipe(&display_brightness_pipe, GINT_TO_POINTER(brightness), USE_INDATA, CACHE_INDATA);
	mce_rtconf_set_int(MCE_BRIGHTNESS_KEY, set_brightness_unfiltered);
	display_brightness_dbus_signal();

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

static gboolean display_brightness_get_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display brightness get request");

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		dbus_int32_t tmp = set_brightness_unfiltered;
		if (!dbus_message_append_args(reply,
							 DBUS_TYPE_INT32, &tmp,
							 DBUS_TYPE_INVALID)) {
			mce_log(LL_ERR, "%s: Failed to append dbus arguments", MODULE_NAME);
			return FALSE;
		}
		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * Handle display state change
 *
 * @param data The display state stored in a pointer
 */
static void display_state_trigger(gconstpointer data)
{
	/** Cached display state */
	static display_state_t cached_display_state = MCE_DISPLAY_UNDEF;
	display_state_t display_state = GPOINTER_TO_INT(data);

	switch (display_state) {
	case MCE_DISPLAY_OFF:
		cancel_blank_timeout();
		break;

	case MCE_DISPLAY_DIM:
		setup_blank_timeout();
		break;

	case MCE_DISPLAY_ON:
	default:
		cancel_blank_timeout();
		break;
	}

	/* If we already have the right state,
	 * we're done here
	 */
	if (cached_display_state == display_state)
		goto EXIT;

	switch (display_state) {
	case MCE_DISPLAY_OFF:
		display_blank();
		break;

	case MCE_DISPLAY_DIM:
		display_dim();
		break;

	case MCE_DISPLAY_ON:
	default:
		display_unblank();
		break;
	}

	/* This will send the correct state
	 * since the pipe contains the new value
	 */
	send_display_status(NULL);

	/* Update the cached value */
	cached_display_state = display_state;

EXIT:
	return;
}

/**
 * Datapipe trigger for device inactivity
 *
 * @param data The inactivity stored in a pointer;
 *             TRUE if the device is inactive,
 *             FALSE if the device is active
 */
static void device_inactive_trigger(gconstpointer data)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	alarm_ui_state_t alarm_ui_state = datapipe_get_gint(alarm_ui_state_pipe);
	gboolean device_inactive = GPOINTER_TO_INT(data);

	/* Unblank screen on device activity,
	 * unless the device is in acting dead and no alarm is visible
	 */
	if (((system_state == MCE_STATE_USER) ||
		((system_state == MCE_STATE_ACTDEAD) &&
		((alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
		(alarm_ui_state == MCE_ALARM_UI_RINGING_INT32)))) &&
		(device_inactive == FALSE)) {
		(void)execute_datapipe(&display_state_pipe,
					GINT_TO_POINTER(MCE_DISPLAY_ON),
					USE_INDATA, CACHE_INDATA);
	} else if ((system_state == MCE_STATE_USER || system_state == MCE_STATE_ACTDEAD) &&
				device_inactive == TRUE && display_state == MCE_DISPLAY_ON) {
		(void)execute_datapipe(&display_state_pipe,
					GINT_TO_POINTER(MCE_DISPLAY_DIM),
					USE_INDATA, CACHE_INDATA);
	}
}

static void tvout_trigger(gconstpointer data)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	gboolean is_tvout_on = GPOINTER_TO_INT(data);
	
	mce_log(LL_DEBUG, "Recieved tvout state changing: is_tvout_on = %d", is_tvout_on);	
	
	if (display_state == MCE_DISPLAY_OFF) {
		is_tvout_state_changed = TRUE;
		send_display_status(NULL);
		is_tvout_state_changed = FALSE;
	}
	return;
}

/**
 * Get the display type
 *
 * @return The display type
 */
static bool get_display(void)
{
	bool ret = false;
	gchar *bright_file = NULL;
	gchar *max_bright_file = NULL;
	const char *path;
	GDir* dir;
	
	/* Attempt to find first entry in /backlight */
	dir = g_dir_open(DISPLAY_GENERIC_PATH, 0, NULL);
	if (dir) {
		path = g_dir_read_name(dir);
		if (path) {
			bright_file = g_strconcat(DISPLAY_GENERIC_PATH, path, DISPLAY_GENERIC_BRIGHTNESS_FILE, NULL);
			max_bright_file = g_strconcat(DISPLAY_GENERIC_PATH, path, DISPLAY_GENERIC_MAX_BRIGHTNESS_FILE, NULL);

			if ((g_access(bright_file, W_OK) == 0) && (g_access(max_bright_file, W_OK) == 0)) {
				/* These will be freed later on, during module unload */
				brightness_file = bright_file;
				max_brightness_file = max_bright_file;
				ret = true;
			} else {
				g_free(bright_file);
				g_free(max_bright_file);
			}
		}
	}

	g_dir_close(dir);
	
	if (bright_file)
		mce_log(LL_DEBUG, "%s: using %s as backlight brightness", MODULE_NAME, bright_file);

	return ret;
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	gint disp_brightness = DEFAULT_DISP_BRIGHTNESS;
	gulong tmp;

	(void)module;
	
	if (!get_display()) {
		/* TODO - do not provide brightness interface if we can't
		 control it. Be very careful when implementing the change as
		 there might be mission critical applications that rely on it.
		 */
		mce_log(LL_WARN, "%s: Could not find display backlight", MODULE_NAME);
	}

	dim_brightness = mce_conf_get_int("DisplayBrightness", "Dim", DEFAULT_DIM_BRIGHTNESS, NULL);

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&display_brightness_pipe,
					  display_brightness_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);
	append_output_trigger_to_datapipe(&device_inactive_pipe,
					  device_inactive_trigger);
	append_output_trigger_to_datapipe(&tvout_pipe, 
					  tvout_trigger);
	/* Get maximum brightness */
	if (mce_read_number_string_from_file(max_brightness_file,
					     &tmp) == FALSE) {
		mce_log(LL_ERR,
			"%s: Could not read the maximum brightness from %s; "
			"defaulting to %d", MODULE_NAME,
			max_brightness_file,
			DEFAULT_MAXIMUM_DISPLAY_BRIGHTNESS);
		tmp = DEFAULT_MAXIMUM_DISPLAY_BRIGHTNESS;
	}

	maximum_display_brightness = tmp;

	/* Display brightness */
	/* Since we've set a default, error handling is unnecessary */
	(void)mce_rtconf_get_int(MCE_BRIGHTNESS_KEY,
				&disp_brightness);

	set_brightness_unfiltered = disp_brightness;

	/* Use the current brightness as cached brightness on startup,
	 * and fade from that value
	 */
	if (mce_read_number_string_from_file(brightness_file,
					     &tmp) == FALSE) {
		mce_log(LL_ERR,
			"%s: Could not read the current brightness from %s", MODULE_NAME, brightness_file);
		cached_brightness = -1;
	} else {
		cached_brightness = tmp;
	}

	(void)execute_datapipe(&display_brightness_pipe,
			       GINT_TO_POINTER(disp_brightness),
			       USE_INDATA, CACHE_INDATA);

	if (mce_rtconf_notifier_add(MCE_BRIGHTNESS_KEY,
				   display_rtconf_cb, NULL,
				   &disp_brightness_gconf_cb_id) == FALSE)
		goto EXIT;

	/* Display blank */
	/* Since we've set a default, error handling is unnecessary */
	disp_blank_timeout = mce_conf_get_int(MCE_CONF_DISPLAY_GROUP, MCE_CONF_DISPLAY_BLANK_KEY,
				DEFAULT_BLANK_TIMEOUT, NULL);
	
	/* get_display_status */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_STATUS_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_status_get_dbus_cb) == NULL)
		goto EXIT;

	/* req_display_state_on */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_ON_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_on_req_dbus_cb) == NULL)
		goto EXIT;

	/* req_display_state_dim */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_DIM_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_dim_req_dbus_cb) == NULL)
		goto EXIT;

	/* req_display_state_off */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_OFF_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_off_req_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_BRIGTNESS_SET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_brightness_set_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_BRIGTNESS_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_brightness_get_dbus_cb) == NULL)
		goto EXIT;

	/* Request display on to get the state machine in sync */
	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_ON),
			       USE_INDATA, CACHE_INDATA);

EXIT:
	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&tvout_pipe, 
					  tvout_trigger);
	remove_output_trigger_from_datapipe(&device_inactive_pipe,
					    device_inactive_trigger);
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&display_brightness_pipe,
					    display_brightness_trigger);

	/* Free strings */
	g_free(brightness_file);
	g_free(max_brightness_file);

	/* Remove all timer sources */
	cancel_brightness_fade_timeout();
	cancel_blank_timeout();

	return;
}
