/**
 * @file inactivity.c
 * Inactivity module -- this implements inactivity logic for MCE
 * <p>
 * Copyright © 2007-2009 Nokia Corporation and/or its subsidiary(-ies).
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
#include <stdbool.h>
#include "mce.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "mce-rtconf.h"
#include "datapipe.h"

#define DEFAULT_TIMEOUT			30	/* 30 seconds */

/** Path to the GConf settings for the display now also used by inactivity.c */
#ifndef MCE_GCONF_DISPLAY_PATH
#define MCE_GCONF_DISPLAY_PATH			"/system/osso/dsm/display"
#endif /* MCE_GCONF_DISPLAY_PATH */
#define MCE_GCONF_DISPLAY_DIM_TIMEOUT_PATH	MCE_GCONF_DISPLAY_PATH "/display_dim_timeout"
#define MCE_GCONF_BLANKING_INHIBIT_MODE_PATH	MCE_GCONF_DISPLAY_PATH "/inhibit_blank_mode"

/**
 * inactiveity prevent timeout, in seconds;
 * Don't alter this, since this is part of the defined behaviour
 * for blanking inhibit that applications rely on
 */
#define INACTIVITY_PREVENT_TIMEOUT			60	/* 60 seconds */

/** Module name */
#define MODULE_NAME		"inactivity"

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

/** Inhibit type */
typedef enum {
	/** Inhibit value invalid */
	INHIBIT_INVALID = -1,
	/** No inhibit */
	INHIBIT_OFF = 0,
	/** Default value */
	DEFAULT_BLANKING_INHIBIT_MODE = INHIBIT_OFF,
	/** Inhibit blanking; always keep on if charger connected */
	INHIBIT_STAY_ON_WITH_CHARGER = 1,
	/** Inhibit blanking; always keep on or dimmed if charger connected */
	INHIBIT_STAY_DIM_WITH_CHARGER = 2,
	/** Inhibit blanking; always keep on */
	INHIBIT_STAY_ON = 3,
	/** Inhibit blanking; always keep on or dimmed */
	INHIBIT_STAY_DIM = 4,
} inhibit_t;

/** Display blanking inhibit mode */
static inhibit_t inactivity_inhibit_mode = DEFAULT_BLANKING_INHIBIT_MODE;

/** ID for inactivity timeout source */
static guint inactivity_timeout_cb_id = 0;
static guint inactivity_timeout_gconf_cb_id = 0;
static guint inactivity_inhibit_gconf_cb_id = 0;

static gint inactivity_timeout = DEFAULT_TIMEOUT;

/**
 * Enable/Disable blanking inhibit,
 * based on charger status and inhibit mode
 *
 * @param timed_inhibit TRUE for timed inhibiting,
 *                      FALSE for triggered inhibiting
 */
static bool inactiveity_inhibited(void)
{
	bool blanking_inhibited = false;
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);
	gboolean charger_connected = datapipe_get_gint(charger_state_pipe);

	if ((call_state == CALL_STATE_RINGING) ||
	    ((charger_connected == TRUE) &&
	     ((system_state == MCE_STATE_ACTDEAD) ||
	      ((inactivity_inhibit_mode == INHIBIT_STAY_ON_WITH_CHARGER) ||
	       (inactivity_inhibit_mode == INHIBIT_STAY_DIM_WITH_CHARGER)))) ||
	    (inactivity_inhibit_mode == INHIBIT_STAY_ON) ||
	    (inactivity_inhibit_mode == INHIBIT_STAY_DIM)) {
		/* Always inhibit blanking */
		blanking_inhibited = true;
	}
	return blanking_inhibited;
}

/**
 * Send an inactivity status reply or signal
 *
 * @param method_call A DBusMessage to reply to;
 *                    pass NULL to send an inactivity status signal instead
 * @return TRUE on success, FALSE on failure
 */
static gboolean send_inactivity_status(DBusMessage *const method_call)
{
	DBusMessage *msg = NULL;
	dbus_bool_t device_inactive = datapipe_get_gbool(device_inactive_pipe);
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Sending inactivity status: %s",
		device_inactive ? "inactive" : "active");

	/* If method_call is set, send a reply,
	 * otherwise, send a signal
	 */
	if (method_call != NULL) {
		msg = dbus_new_method_reply(method_call);
	} else {
		/* system_inactivity_ind */
		msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
				      MCE_INACTIVITY_SIG);
	}

	/* Append the inactivity status */
	if (dbus_message_append_args(msg,
				     DBUS_TYPE_BOOLEAN, &device_inactive,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append %sargument to D-Bus message "
			"for %s.%s",
			method_call ? "reply " : "",
			method_call ? MCE_REQUEST_IF :
				      MCE_SIGNAL_IF,
			method_call ? MCE_INACTIVITY_STATUS_GET :
				      MCE_INACTIVITY_SIG);
		dbus_message_unref(msg);
		goto EXIT;
	}

	/* Send the message */
	status = dbus_send_message(msg);

EXIT:
	return status;
}

/**
 * D-Bus callback for the get inactivity status method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean inactivity_status_get_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "Received inactivity status get request");

	/* Try to send a reply that contains the current inactivity status */
	if (send_inactivity_status(msg) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

/**
 * Timeout callback for inactivity
 *
 * @param data Unused
 */
static gboolean inactivity_timeout_cb(gpointer data)
{
	(void)data;

	inactivity_timeout_cb_id = 0;
	
	if(inactiveity_inhibited())
		return TRUE;

	(void)execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(TRUE),
			       USE_INDATA, CACHE_INDATA);

	return FALSE;
}

/**
 * Cancel inactivity timeout
 */
static void cancel_inactivity_timeout(void)
{
	/* Remove inactivity timeout source */
	if (inactivity_timeout_cb_id != 0) {
		g_source_remove(inactivity_timeout_cb_id);
		inactivity_timeout_cb_id = 0;
	}
}

/**
 * Setup inactivity timeout
 */
static void setup_inactivity_timeout(void)
{
	mce_log(LL_DEBUG, "%s: device inactiveity timeout %i", MODULE_NAME, inactivity_timeout);

	cancel_inactivity_timeout();

	/* Sanitise timeout */
	if (inactivity_timeout <= 0)
		inactivity_timeout = 30;

	/* Setup new timeout */
	inactivity_timeout_cb_id = g_timeout_add_seconds(inactivity_timeout, inactivity_timeout_cb, NULL);
}

/**
 * Datapipe filter for inactivity
 *
 * @param data The unfiltered inactivity state;
 *             TRUE if the device is inactive,
 *             FALSE if the device is active
 * @return The filtered inactivity state;
 *             TRUE if the device is inactive,
 *             FALSE if the device is active
 */
static gpointer device_inactive_filter(gpointer data)
{
	static gboolean old_device_inactive = FALSE;
	gboolean device_inactive = GPOINTER_TO_INT(data);
	submode_t submode = mce_get_submode_int32();
	alarm_ui_state_t alarm_ui_state = datapipe_get_gint(alarm_ui_state_pipe);
	display_state_t display_state = datapipe_get_gint(display_state_pipe);

	/* Only send the inactivity status on dbus if it changed */
	if ((old_device_inactive != (device_inactive == TRUE)) &&
	    (((submode & MCE_TKLOCK_SUBMODE) == 0) ||
	     (device_inactive == TRUE)))
		send_inactivity_status(NULL);

	/* Only send the inactivity status on dbus if it changed */
	if ((device_inactive == FALSE) &&
	    ((submode & MCE_TKLOCK_SUBMODE) != 0) &&
	    ((submode & MCE_VISUAL_TKLOCK_SUBMODE) == 0) &&
	    (((alarm_ui_state != MCE_ALARM_UI_VISIBLE_INT32) &&
	      (alarm_ui_state != MCE_ALARM_UI_RINGING_INT32)) ||
	     (((submode & MCE_AUTORELOCK_SUBMODE) != 0) &&
	      (display_state == MCE_DISPLAY_OFF)))) {
		data = GINT_TO_POINTER(TRUE);
		device_inactive = GPOINTER_TO_INT(data);
	}
	
		/* We got activity; restart timeouts */
	if (device_inactive == FALSE)
		setup_inactivity_timeout();
	
	old_device_inactive = device_inactive;

	return data;
}

/**
 * Inactivity timeout trigger
 *
 * @param data Unused
 */
static void inactivity_timeout_trigger(gconstpointer data)
{
	(void)data;

	setup_inactivity_timeout();
}

/**
 * Handle display state change
 *
 * @param data The display state stored in a pointer
 */
static gpointer display_state_filter(gpointer data)
{
	display_state_t display_state = GPOINTER_TO_INT(data);

	switch (display_state) {
	case MCE_DISPLAY_OFF:
		cancel_inactivity_timeout();
		execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(TRUE), USE_INDATA, CACHE_INDATA);
		break;

	case MCE_DISPLAY_ON:
	default:
		setup_inactivity_timeout();
		break;
	}
	
	return data;
}


/**
 * rtconf callback for display related settings
 * 
 * @param key Unused
 * @param cb_id Connection ID from gconf_client_notify_add()
 * @param user_data Unused
 */
static void inactiveity_rtconf_cb(gchar *key, guint cb_id, void *user_data)
{
	(void)key;
	(void)user_data;

	if (cb_id == inactivity_timeout_gconf_cb_id) {
		mce_rtconf_get_int(MCE_GCONF_DISPLAY_DIM_TIMEOUT_PATH, &inactivity_timeout);
		mce_log(LL_DEBUG, "%s: inactivity_timeout set to %i", MODULE_NAME, inactivity_timeout);
	} else if (cb_id == inactivity_inhibit_gconf_cb_id) {
		mce_rtconf_get_int(MCE_GCONF_BLANKING_INHIBIT_MODE_PATH, &inactivity_inhibit_mode);
	} else {
		mce_log(LL_WARN, "%s: Spurious rtconf value received; confused!", MODULE_NAME);
	}
}

/**
 * Init function for the inactivity module
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

	/* Append triggers/filters to datapipes */
	append_filter_to_datapipe(&device_inactive_pipe,
				  device_inactive_filter);
	append_output_trigger_to_datapipe(&inactivity_timeout_pipe,
					  inactivity_timeout_trigger);
	append_filter_to_datapipe(&display_state_pipe,
					display_state_filter);
	
	/* Since we've set a default, error handling is unnecessary */
	mce_rtconf_get_int(MCE_GCONF_DISPLAY_DIM_TIMEOUT_PATH,
				&inactivity_timeout);

	if (mce_rtconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_DISPLAY_DIM_TIMEOUT_PATH,
				   inactiveity_rtconf_cb, NULL,
				   &inactivity_timeout_gconf_cb_id) == FALSE)
		goto EXIT;
	
	mce_rtconf_get_int(MCE_GCONF_BLANKING_INHIBIT_MODE_PATH,
				&inactivity_inhibit_mode);

	if (mce_rtconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_BLANKING_INHIBIT_MODE_PATH,
				   inactiveity_rtconf_cb, NULL,
				   &inactivity_inhibit_gconf_cb_id) == FALSE)
		goto EXIT;

	/* get_inactivity_status */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_INACTIVITY_STATUS_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 inactivity_status_get_dbus_cb) == NULL)
		goto EXIT;

	setup_inactivity_timeout();

EXIT:
	return NULL;
}

/**
 * Exit function for the inactivity module
 *
 * @todo D-Bus unregistration
 *
 * @param module Unused
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_filter_from_datapipe(&device_inactive_pipe,
				    device_inactive_filter);
	remove_output_trigger_from_datapipe(&inactivity_timeout_pipe,
					  inactivity_timeout_trigger);
	remove_filter_from_datapipe(&display_state_pipe,
					display_state_filter);

	/* Remove all timer sources */
	cancel_inactivity_timeout();

	return;
}
