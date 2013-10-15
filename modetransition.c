/**
 * @file modetransition.c
 * This file implements the mode transition component
 * (normal/flight mode/off)
 * of the Mode Control Entity
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mce/mode-names.h>
#include <systemui/dbus-names.h>
#include <systemui/actingdead-dbus-names.h>
#include <systemui/modechange-dbus-names.h>
#include <systemui/splashscreen-dbus-names.h>

#include "mce.h"
#include "modetransition.h"
#include "mce-io.h"
#include "mce-lib.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "datapipe.h"
#include "connectivity.h"

static const mce_translation_t device_mode_translation[] = {
	{
		.number = MCE_NORMAL_MODE_INT32,
		.string = MCE_NORMAL_MODE
	}, {
		.number = MCE_NORMAL_MODE_CONFIRM_INT32,
		.string = MCE_NORMAL_MODE
	}, {
		.number = MCE_NORMAL_MODE_CONFIRM_INT32,
		.string = MCE_NORMAL_MODE MCE_CONFIRM_SUFFIX
	}, {
		.number = MCE_FLIGHT_MODE_INT32,
		.string = MCE_FLIGHT_MODE
	}, {
		.number = MCE_FLIGHT_MODE_CONFIRM_INT32,
		.string = MCE_FLIGHT_MODE
	}, {
		.number = MCE_FLIGHT_MODE_CONFIRM_INT32,
		.string = MCE_FLIGHT_MODE MCE_CONFIRM_SUFFIX
	}, {
		.number = MCE_OFFLINE_MODE_INT32,
		.string = MCE_OFFLINE_MODE
	}, {
		.number = MCE_OFFLINE_MODE_CONFIRM_INT32,
		.string = MCE_OFFLINE_MODE
	}, {
		.number = MCE_OFFLINE_MODE_CONFIRM_INT32,
		.string = MCE_OFFLINE_MODE MCE_CONFIRM_SUFFIX
	}, {
		.number = MCE_INVALID_TRANSLATION,
		.string = MCE_INVALID_MODE
	}
};

static device_mode_t mcedevmode = MCE_NORMAL_MODE_INT32;

static guint32 transition = 0;

/** Global (ewww) reply pointer for mode change requests */
static DBusMessage *modereply = NULL;

static gboolean set_raw_device_mode(const device_mode_t mode);
static gboolean device_mode_send(DBusMessage *const method_call,
				 const gchar *const mode);

static gboolean mode_confirm(const dbus_uint32_t mode,
				  const gboolean open_dialog)
{
	const gchar *const cb_service = MCE_SERVICE;
	const gchar *const cb_path = MCE_REQUEST_PATH;
	const gchar *const cb_interface = MCE_REQUEST_IF;
	const gchar *const cb_method = MCE_MODECHG_CB_REQ;

	mce_log(LL_DEBUG,
		"Mode confirmation dialog (mode: %d, open/close: %d)",
		mode, open_dialog);

	/* com.nokia.system_ui.request.modechange_{open,close} */
	if (open_dialog == TRUE) {
		return dbus_send(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
				 SYSTEMUI_REQUEST_IF,
				 SYSTEMUI_MODECHANGE_OPEN_REQ, NULL,
				 DBUS_TYPE_STRING, &cb_service,
				 DBUS_TYPE_STRING, &cb_path,
				 DBUS_TYPE_STRING, &cb_interface,
				 DBUS_TYPE_STRING, &cb_method,
				 DBUS_TYPE_UINT32, &mode,
				 DBUS_TYPE_INVALID);
	} else {
		return dbus_send(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
				 SYSTEMUI_REQUEST_IF,
				 SYSTEMUI_MODECHANGE_CLOSE_REQ, NULL,
				 DBUS_TYPE_INVALID);
	}
}

static gboolean send_reply(dbus_bool_t result)
{
	gboolean status = FALSE;

	/* If there's no need to send a reply, don't try to */
	if (modereply == NULL) {
		status = TRUE;
		goto EXIT;
	}

	/* Append the reply */
	if (dbus_message_append_args(modereply,
				     DBUS_TYPE_BOOLEAN, &result,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append reply argument to "
			"D-Bus message for %s.%s",
			MCE_REQUEST_IF, MCE_DEVICE_MODE_CHANGE_REQ);
		dbus_message_unref(modereply);
		modereply = NULL;
		goto EXIT;
	}

	status = dbus_send_message(modereply);
	modereply = NULL;

EXIT:
	return status;
}

static gboolean mode_abort(void)
{
	gboolean status = FALSE;

	/* Close the confirmation dialog */
	status = mode_confirm(0, FALSE);

	/* Exit mode change submode */
	mce_rem_submode_int32(MCE_MODECHG_SUBMODE);

	return status;
}

static gboolean modechange_dbus_cb(DBusMessage *const msg)
{
	dbus_int32_t result = INT_MAX;
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received modechange callback");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_INT32, &result,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_MODECHG_CB_REQ,
			error.message);
		dbus_error_free(&error);

		mode_abort();
		goto EXIT;
	}

	mce_log(LL_DEBUG, "Modechange callback value: %d", result);

	switch (result) {
	case MODECHANGE_RESPONSE_OK:
		if (transition == MODECHANGE_TO_FLIGHTMODE)
			set_raw_device_mode(MCE_FLIGHT_MODE_INT32);
		else
			set_raw_device_mode(MCE_NORMAL_MODE_INT32);

		break;

	case MODECHANGE_RESPONSE_CANCEL:
	default:
		mode_abort();
		device_mode_send(NULL, NULL);
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

static gboolean powerup_splash(const gboolean enable)
{
	const dbus_uint32_t splashtype = SPLASHSCREEN_ENABLE_BOOTUP;

	mce_log(LL_DEBUG, "Calling bootup splashscreen (%d)", enable);

	return dbus_send(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
			 SYSTEMUI_REQUEST_IF,
			 enable ? SYSTEMUI_SPLASHSCREEN_OPEN_REQ :
				  SYSTEMUI_SPLASHSCREEN_CLOSE_REQ, NULL,
			 DBUS_TYPE_UINT32, &splashtype,
			 DBUS_TYPE_INVALID);
}

static gboolean shutdown_splash(const gboolean enable,
				    const dbus_bool_t sound)
{
	const dbus_uint32_t splashtype = SPLASHSCREEN_ENABLE_SHUTDOWN;

	mce_log(LL_DEBUG, "Calling shutdown splashscreen (%d)", enable);

	return dbus_send(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
			 SYSTEMUI_REQUEST_IF,
			 enable ? SYSTEMUI_SPLASHSCREEN_OPEN_REQ :
				  SYSTEMUI_SPLASHSCREEN_CLOSE_REQ, NULL,
			 DBUS_TYPE_UINT32, &splashtype,
			 DBUS_TYPE_BOOLEAN, &sound,
			 DBUS_TYPE_INVALID);
}

static gboolean show_acting_dead_ui(const gboolean enable)
{
	mce_log(LL_DEBUG, "Calling acting dead UI (%d)", enable);

	return dbus_send(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
			 SYSTEMUI_REQUEST_IF,
			 enable ? SYSTEMUI_ACTINGDEAD_OPEN_REQ :
				  SYSTEMUI_ACTINGDEAD_CLOSE_REQ, NULL,
			 DBUS_TYPE_INVALID);
}

static gboolean mce_set_submode_int32(const submode_t submode)
{
	execute_datapipe(&submode_pipe, GINT_TO_POINTER(submode),
			 USE_INDATA, CACHE_INDATA);
	mce_log(LL_DEBUG, "Submode changed to %d", submode);

	return TRUE;
}

/**
 * Add flags to the MCE submode
 *
 * @param submode submode(s) to add OR:ed together
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_add_submode_int32(const submode_t submode)
{
	submode_t old_submode = datapipe_get_gint(submode_pipe);

	return mce_set_submode_int32(old_submode | submode);
}

/**
 * Remove flags from the MCE submode
 *
 * @param submode submode(s) to remove OR:ed together
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_rem_submode_int32(const submode_t submode)
{
	submode_t old_submode = datapipe_get_gint(submode_pipe);

	return mce_set_submode_int32(old_submode & ~submode);
}

/**
 * Return all set MCE submode flags
 *
 * @return All set submode flags OR:ed together
 */
submode_t mce_get_submode_int32(void) G_GNUC_PURE;
submode_t mce_get_submode_int32(void)
{
	submode_t submode = datapipe_get_gint(submode_pipe);

	return submode;
}

device_mode_t mce_get_device_mode_int32(void) G_GNUC_PURE;
device_mode_t mce_get_device_mode_int32(void)
{
	return mcedevmode;
}

static gboolean device_mode_send(DBusMessage *const method_call,
				 const gchar *const mode)
{
	DBusMessage *msg = NULL;
	const gchar *smode;
	device_mode_t device_mode = mce_get_device_mode_int32();
	gboolean status = FALSE;

	if (mode != NULL)
		smode = mode;
	else
		smode = mce_translate_int_to_string(device_mode_translation,
						    device_mode);

	if (method_call != NULL) {
		msg = dbus_new_method_reply(method_call);
	} else {
		msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
				      MCE_DEVICE_MODE_SIG);
	}

	if (dbus_message_append_args(msg,
				     DBUS_TYPE_STRING, &smode,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append %sargument to D-Bus message "
			"for %s.%s",
			method_call ? "reply " : "",
			method_call ? MCE_REQUEST_IF :
				      MCE_SIGNAL_IF,
			method_call ? MCE_DEVICE_MODE_GET :
				      MCE_DEVICE_MODE_SIG);
		dbus_message_unref(msg);
		goto EXIT;
	}

	status = dbus_send_message(msg);

EXIT:
	return status;
}

static gboolean save_mce_mode_to_file(const gchar *const mode)
{
	return mce_write_string_to_file(MCE_MODE_FILENAME, mode);
}

static gboolean set_raw_device_mode(const device_mode_t mode)
{
	const gchar *smode = NULL;
	gboolean status = FALSE;

	smode = mce_translate_int_to_string(device_mode_translation, mode);

	if ((status = device_mode_send(NULL, smode)) == FALSE)
		goto EXIT;

	mce_log(LL_INFO, "Mode changed to %s", smode);

	if (mce_get_device_mode_int32() != mode)
		save_mce_mode_to_file(smode);

	mcedevmode = mode;
	execute_datapipe(&mode_pipe, GINT_TO_POINTER(mode),
			 USE_INDATA, CACHE_INDATA);

	mce_rem_submode_int32(MCE_MODECHG_SUBMODE);

EXIT:
	return status;
}

gboolean mce_set_device_mode_int32(const device_mode_t mode)
{
	gboolean result = FALSE;
	gboolean status = FALSE;

	if (mode == MCE_INVALID_MODE_INT32)
		goto EXIT;

	if ((mce_get_submode_int32() & MCE_MODECHG_SUBMODE) != 0)
		goto EXIT;

	if (mode == mce_get_device_mode_int32()) {
		device_mode_send(NULL, NULL);
		result = TRUE;
		goto EXIT;
	}

	switch (mode) {
	case MCE_NORMAL_MODE_CONFIRM_INT32:
		transition = MODECHANGE_TO_NORMALMODE;
		result = mode_confirm(transition, TRUE);

		if (result == TRUE) {
			mce_add_submode_int32(MCE_MODECHG_SUBMODE);
		}

		break;

	case MCE_FLIGHT_MODE_CONFIRM_INT32:
		if (get_connectivity_status() == TRUE) {
			transition = MODECHANGE_TO_FLIGHTMODE;
			result = mode_confirm(transition, TRUE);

			if (result == TRUE) {
				mce_add_submode_int32(MCE_MODECHG_SUBMODE);
			}
		} else {
			result = set_raw_device_mode(mode);
		}

		break;

	default:
		result = set_raw_device_mode(mode);
		break;
	}

EXIT:
	status = send_reply(result);

	return status;
}

static gboolean set_mce_mode_string(const gchar *const mode)
{
	device_mode_t newmode;

	newmode = mce_translate_string_to_int(device_mode_translation, mode);

	return mce_set_device_mode_int32(newmode);
}

void mce_startup_ui(void)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);

	if (system_state == MCE_STATE_ACTDEAD) {
		if (show_acting_dead_ui(TRUE) == FALSE) {
			g_main_loop_quit(mainloop);
			exit(EXIT_FAILURE);
		}
	}

	execute_datapipe(&system_state_pipe, NULL,
			 USE_CACHE, CACHE_INDATA);
}

static gboolean mode_change_req_dbus_cb(DBusMessage *const msg)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	gboolean status = FALSE;
	gchar *mode = NULL;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received mode change request");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &mode,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_DEVICE_MODE_CHANGE_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	modereply = dbus_new_method_reply(msg);

	if (system_state == MCE_STATE_USER)
		status = set_mce_mode_string(mode);
	else
		status = send_reply(FALSE);

EXIT:
	return status;
}

static gboolean get_mode_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "Received mode get request");

	if (device_mode_send(msg, NULL) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

static gboolean startup_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	(void)msg;

	mce_log(LL_DEBUG, "Received SystemUI startup indication");

	mce_startup_ui();

	status = TRUE;

	return status;
}

static gboolean restore_mce_mode(void)
{
	gchar *mode = NULL;
	gboolean status;

	if (mce_read_string_from_file(MCE_MODE_FILENAME, &mode) == FALSE) {
		status = mce_set_device_mode_int32(MCE_FLIGHT_MODE_INT32);
	} else {
		device_mode_t newmode;
		int i = 0;

		while (g_ascii_isalnum(mode[i]) != 0)
			i++;

		mode[i] = '\0';

		newmode = mce_translate_string_to_int(device_mode_translation,
						      mode);

		if (newmode == MCE_INVALID_TRANSLATION)
			newmode = MCE_FLIGHT_MODE_INT32;

		status = mce_set_device_mode_int32(newmode);
		g_free(mode);
	}

	return status;
}

/**
 * Handle system state change
 *
 * @param data The system state stored in a pointer
 */
static void system_state_trigger(gconstpointer data)
{
	static system_state_t old_system_state = MCE_STATE_UNDEF;
	system_state_t system_state = GPOINTER_TO_INT(data);

	switch (system_state) {
	case MCE_STATE_USER:
		if (old_system_state == MCE_STATE_ACTDEAD) {
			if ((mce_get_submode_int32() &
			     MCE_DEVLOCK_SUBMODE) == 0) {
				if (powerup_splash(TRUE) == FALSE) {
					mce_log(LL_ERR,
						"Failed to open "
						"power up splashscreen");
				}
				execute_datapipe_output_triggers(&led_pattern_deactivate_pipe, MCE_LED_PATTERN_BATTERY_CHARGING, USE_INDATA);
				execute_datapipe_output_triggers(&led_pattern_deactivate_pipe, MCE_LED_PATTERN_BATTERY_FULL, USE_INDATA);
				execute_datapipe_output_triggers(&led_pattern_deactivate_pipe, MCE_LED_PATTERN_POWER_ON, USE_INDATA);
				execute_datapipe_output_triggers(&vibrator_pattern_deactivate_pipe, MCE_VIBRATOR_PATTERN_POWER_KEY_PRESS, USE_INDATA);
			}
		}

		break;

	case MCE_STATE_SHUTDOWN:
	case MCE_STATE_REBOOT:
		/* Actions to perform when shutting down/rebooting
		 * from anything else than acting dead
		 */
		if ((old_system_state == MCE_STATE_USER) ||
		    (old_system_state == MCE_STATE_BOOT) ||
		    (old_system_state == MCE_STATE_UNDEF)) {
			if (shutdown_splash(TRUE, TRUE) == FALSE) {
				mce_log(LL_ERR,
					"Failed to open "
					"shutdown splashscreen");
			}

			execute_datapipe_output_triggers(&led_pattern_deactivate_pipe, MCE_LED_PATTERN_DEVICE_ON, USE_INDATA);
			execute_datapipe_output_triggers(&led_pattern_activate_pipe, MCE_LED_PATTERN_POWER_OFF, USE_INDATA);
		}

		/* If we're shutting down/rebooting from acting dead,
		 * blank the screen
		 */
		if (old_system_state == MCE_STATE_ACTDEAD) {
			execute_datapipe(&display_state_pipe,
					 GINT_TO_POINTER(MCE_DISPLAY_OFF),
					 USE_INDATA, CACHE_INDATA);
		}

		break;

	case MCE_STATE_ACTDEAD:
		if (show_acting_dead_ui(TRUE) == FALSE) {
			mce_log(LL_ERR,
				"Failed to open acting dead UI");
		}

		break;

	case MCE_STATE_UNDEF:
		goto EXIT;

	default:
		break;
	}

	mce_log(LL_DEBUG,
		"dsmestate set to: %d (old: %d)",
		system_state, old_system_state);

	old_system_state = system_state;

EXIT:
	return;
}

/**
 * Init function for the modetransition component
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_mode_init(void)
{
	gboolean status = FALSE;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);

	if (access(MCE_DEVLOCK_FILENAME, F_OK) == -1) {
		if (errno == ENOENT) {
			mce_log(LL_DEBUG, "Bootup mode enabled");
			mce_add_submode_int32(MCE_TRANSITION_SUBMODE);
			errno = 0;

			(void)mce_write_string_to_file(MCE_DEVLOCK_FILENAME,
						       ENABLED_STRING);
			mce_log(LL_DEBUG, "device_lock_inhibit_pipe - > TRUE");
			(void)execute_datapipe(&device_lock_inhibit_pipe,
					       GINT_TO_POINTER(TRUE),
					       USE_INDATA, CACHE_INDATA);
		} else {
			mce_log(LL_CRIT,
				"access() failed: %s. Exiting.",
				g_strerror(errno));
			goto EXIT;
		}
	}

	restore_mce_mode();

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEVICE_MODE_CHANGE_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 mode_change_req_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEVICE_MODE_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 get_mode_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_MODECHG_CB_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 modechange_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(SYSTEMUI_SIGNAL_IF,
				 SYSTEMUI_STARTED_SIG,
				 NULL,
				 DBUS_MESSAGE_TYPE_SIGNAL,
				 startup_dbus_cb) == NULL)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

/**
 * Exit function for the modetransition component
 *
 * @todo D-Bus unregistration
 */
void mce_mode_exit(void)
{
	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);

	return;
}
