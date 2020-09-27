/**
 * @file keypad.c
 * Keypad module -- this handles the keypress logic for MCE
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
#include <gmodule.h>
#include <stdlib.h>
#include <linux/input.h>
#include "mce.h"
#include "keypad.h"
#include "mce-io.h"
#include "mce-lib.h"
#include "mce-dbus.h"
#include "mce-log.h"
#include "datapipe.h"
#include "mce-conf.h"

/** Module name */
#define MODULE_NAME		"keypad"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_NAME, NULL };

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
 * The ID of the timeout used for the key backlight
 */
static guint key_backlight_timeout_cb_id = 0;

static gboolean keyboard_light_state = FALSE;

/** Default backlight brightness */
static gint key_backlight_timeout = DEFAULT_KEY_BACKLIGHT_TIMEOUT;

static gint key_backlight_fadetime = DEFAULT_KEY_BACKLIGHT_FADETIME;

static void cancel_key_backlight_timeout(void);

static void set_lysti_backlight_brightness(guint fadetime, guint brightness)
{
	                       /* remux|bright| fade | stop
			        * xxxx   xx            xxxx */
	static gchar pattern[] = "9d80" "4000" "0000" "0000";
	static gchar convert[] = "0123456789abcdef";

	pattern[6] = convert[(brightness & 0xf0) >> 4];
	pattern[7] = convert[brightness & 0xf];

	if (brightness == 0) {
		switch (fadetime) {
		case 0:
			pattern[8] = '0';
			pattern[9] = '0';
			break;

		case 50:
			pattern[8] = '0';
			pattern[9] = '7';
			break;

		default:
		case 100:
			pattern[8] = '0';
			pattern[9] = 'd';
			break;

		case 150:
			pattern[8] = '1';
			pattern[9] = '3';
			break;

		case 200:
			pattern[8] = '1';
			pattern[9] = 'b';
			break;

		case 250:
			pattern[8] = '2';
			pattern[9] = '1';
			break;
		}

		pattern[10] = 'f';
		pattern[11] = 'f';
		keyboard_light_state = FALSE;
		mce_log(LL_DEBUG, "keyboard_light_state = %d after full fade", keyboard_light_state);
	} else {
		pattern[8] = '0';
		pattern[9] = '0';
		pattern[10] = '0';
		pattern[11] = '0';
		keyboard_light_state = TRUE;
		mce_log(LL_DEBUG, "keyboard_light_state = %d when no fade", keyboard_light_state);
	}

	/* Disable engine 3 */
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE3_MODE_PATH,
				       MCE_LED_DISABLED_MODE);

	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB1_BRIGHTNESS_PATH, 0);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB2_BRIGHTNESS_PATH, 0);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB3_BRIGHTNESS_PATH, 0);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB4_BRIGHTNESS_PATH, 0);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB5_BRIGHTNESS_PATH, 0);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB6_BRIGHTNESS_PATH, 0);

	/* Set backlight LED current */
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB1_LED_CURRENT_PATH, DEFAULT_LYSTI_BACKLIGHT_LED_CURRENT);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB2_LED_CURRENT_PATH, DEFAULT_LYSTI_BACKLIGHT_LED_CURRENT);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB3_LED_CURRENT_PATH, DEFAULT_LYSTI_BACKLIGHT_LED_CURRENT);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB4_LED_CURRENT_PATH, DEFAULT_LYSTI_BACKLIGHT_LED_CURRENT);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB5_LED_CURRENT_PATH, DEFAULT_LYSTI_BACKLIGHT_LED_CURRENT);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_KB6_LED_CURRENT_PATH, DEFAULT_LYSTI_BACKLIGHT_LED_CURRENT);

	/* Engine 3 */
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE3_MODE_PATH,
				       MCE_LED_LOAD_MODE);
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE3_LEDS_PATH,
				       bin_to_string(MCE_LYSTI_KEYB1_MASK |
						     MCE_LYSTI_KEYB2_MASK |
						     MCE_LYSTI_KEYB3_MASK |
						     MCE_LYSTI_KEYB4_MASK |
						     MCE_LYSTI_KEYB5_MASK |
						     MCE_LYSTI_KEYB6_MASK));
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE3_LOAD_PATH,
				       pattern);
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE3_MODE_PATH,
				       MCE_LED_RUN_MODE);
}
static void set_backlight_brightness(gconstpointer data)
{
	static gint cached_brightness = -1;
	gint new_brightness = GPOINTER_TO_INT(data);

	/* If we're just rehashing the same brightness value, don't bother */
	if ((new_brightness == cached_brightness) && (cached_brightness != -1))
		goto EXIT;

	cached_brightness = new_brightness;
	set_lysti_backlight_brightness(key_backlight_fadetime, new_brightness);

EXIT:
	return;
}

/**
 * Disable key backlight
 */
static void disable_key_backlight(void)
{
	cancel_key_backlight_timeout();

	execute_datapipe(&key_backlight_pipe, GINT_TO_POINTER(0),
			 USE_INDATA, CACHE_INDATA);
}

/**
 * Timeout callback for key backlight
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean key_backlight_timeout_cb(gpointer data)
{
	(void)data;

	key_backlight_timeout_cb_id = 0;

	disable_key_backlight();

	return FALSE;
}

static void cancel_key_backlight_timeout(void)
{
	if (key_backlight_timeout_cb_id != 0) {
		g_source_remove(key_backlight_timeout_cb_id);
		key_backlight_timeout_cb_id = 0;
	}
}

static void setup_key_backlight_timeout(void)
{
	cancel_key_backlight_timeout();

	/* Setup a new timeout */
	key_backlight_timeout_cb_id =
		g_timeout_add_seconds(key_backlight_timeout,
				      key_backlight_timeout_cb, NULL);
}

/**
 * Enable key backlight
 */
static void enable_key_backlight(void)
{
	cancel_key_backlight_timeout();

	/* Only enable the key backlight if the slide is open */
	if (datapipe_get_gint(keyboard_slide_pipe) != COVER_OPEN)
		goto EXIT;

	setup_key_backlight_timeout();

	/* If the backlight is off, turn it on */
	if (datapipe_get_guint(key_backlight_pipe) == 0) {
		execute_datapipe(&key_backlight_pipe,
				 GINT_TO_POINTER(DEFAULT_KEY_BACKLIGHT_LEVEL),
				 USE_INDATA, CACHE_INDATA);
	}

EXIT:
	return;
}

/**
 * Policy based enabling of key backlight
 */
static void enable_key_backlight_policy(void)
{
	cover_state_t kbd_slide_state = datapipe_get_gint(keyboard_slide_pipe);
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	alarm_ui_state_t alarm_ui_state =
		datapipe_get_gint(alarm_ui_state_pipe);

	/* If the keyboard slide isn't open, there's no point in enabling
	 * the backlight
	 *
	 * XXX: this policy will have to change if/when we get devices
	 * with external keypads that needs to be backlit, but for now
	 * that's not an issue
	 */
	if (kbd_slide_state != COVER_OPEN)
		goto EXIT;

	if ((system_state == MCE_STATE_USER) ||
	    ((alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
	     (alarm_ui_state == MCE_ALARM_UI_RINGING_INT32)))
		enable_key_backlight();

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
static void device_inactive_trigger(gconstpointer const data)
{
	gboolean device_inactive = GPOINTER_TO_INT(data);

	if (device_inactive == FALSE)
		enable_key_backlight_policy();
}

/**
 * Datapipe trigger for the keyboard slide
 *
 * @param data The keyboard slide state stored in a pointer;
 *             COVER_OPEN if the keyboard is open,
 *             COVER_CLOSED if the keyboard is closed
 */
static void keyboard_slide_trigger(gconstpointer const data)
{
	if ((GPOINTER_TO_INT(data) == COVER_OPEN) &&
	    ((mce_get_submode_int32() & MCE_TKLOCK_SUBMODE) == 0)) {
		enable_key_backlight_policy();
	} else {
		disable_key_backlight();
	}
}

/**
 * Datapipe trigger for display state
 *
 * @param data The display stated stored in a pointer
 */
static void display_state_trigger(gconstpointer data)
{
	static display_state_t old_display_state = MCE_DISPLAY_UNDEF;
	display_state_t display_state = GPOINTER_TO_INT(data);

	switch (display_state) {
	case MCE_DISPLAY_OFF:
	case MCE_DISPLAY_DIM:
		disable_key_backlight();
		break;

	case MCE_DISPLAY_ON:
		if (old_display_state == MCE_DISPLAY_OFF)
			enable_key_backlight_policy();

		break;

	case MCE_DISPLAY_UNDEF:
	default:
		break;
	}

	old_display_state = display_state;
}

/**
 * Handle system state change
 *
 * @param data The system state stored in a pointer
 */
static void system_state_trigger(gconstpointer data)
{
	system_state_t system_state = GPOINTER_TO_INT(data);

	/* If we're changing to another state than USER,
	 * disable the key backlight
	 */
	if (system_state != MCE_STATE_USER)
		disable_key_backlight();
}

static gboolean get_keyboard_status_dbus_cb(DBusMessage *message)
{
	const gchar *state;
	DBusMessage *reply;

	mce_log(LL_DEBUG, "Received keyboard status get request");
	if (keyboard_light_state)
	{
		state = "on";
	}
	else
	{
		state = "off";
	}
	mce_log(LL_DEBUG, "Sending keyboard status: %s", state);
	if (!message)
	{
		mce_log(LL_WARN, "MCE couldn't send reply, passed method_call is equal to NULL");
		return FALSE;
	}
	else
	{
		reply = dbus_new_method_reply(message);
		if (dbus_message_append_args(reply, 's', &state, 0))
		{
			return dbus_send_message(reply) != 0;
		}
		else
		{
			mce_log(LL_CRIT,"Failed to append reply argument to D-Bus message for %s.%s",MCE_REQUEST_IF,MCE_KEYBOARD_STATUS_GET);
			dbus_message_unref(reply);
			return FALSE;
		}
	}
}

/**
 * Init function for the keypad module
 *
 * @todo XXX status needs to be set on error!
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	gchar *status = NULL;

	(void)module;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);
	append_output_trigger_to_datapipe(&key_backlight_pipe,
					  set_backlight_brightness);
	append_output_trigger_to_datapipe(&device_inactive_pipe,
					  device_inactive_trigger);
	append_output_trigger_to_datapipe(&keyboard_slide_pipe,
					  keyboard_slide_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);

	/* Get configuration options */
	key_backlight_timeout =
		mce_conf_get_int(MCE_CONF_KEYPAD_GROUP,
				 MCE_CONF_KEY_BACKLIGHT_TIMEOUT,
				 DEFAULT_KEY_BACKLIGHT_TIMEOUT,
				 NULL);

	key_backlight_fadetime =
		mce_conf_get_int(MCE_CONF_KEYPAD_GROUP,
			         MCE_CONF_KEY_BACKLIGHT_FADETIME,
			         DEFAULT_KEY_BACKLIGHT_FADETIME,
			         NULL);
	if ( mce_dbus_handler_add(MCE_REQUEST_IF, MCE_KEYBOARD_STATUS_GET, NULL, 1u, get_keyboard_status_dbus_cb) )
	{
		status = NULL;
	}
	else
	{
		mce_log(LL_WARN, "Error in intialization of dbus handler");
		status = NULL;
	}
	return status;
}

/**
 * Exit function for the keypad module
 *
 * @param module Unused
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&keyboard_slide_pipe,
					    keyboard_slide_trigger);
	remove_output_trigger_from_datapipe(&device_inactive_pipe,
					    device_inactive_trigger);
	remove_output_trigger_from_datapipe(&key_backlight_pipe,
					    set_backlight_brightness);
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);

	/* Remove all timer sources */
	cancel_key_backlight_timeout();

	return;
}
