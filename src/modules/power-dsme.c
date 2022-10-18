/**
 * @file mce-dsme.c
 * Interface code and logic between
 * DSME (the Device State Management Entity)
 * and MCE (the Mode Control Entity)
 * <p>
 * Copyright © 2004-2010 Nokia Corporation and/or its subsidiary(-ies).
 * <p>
 * @author David Weinehall <david.weinehall@nokia.com>
 * @author Ismo Laitinen <ismo.laitinen@nokia.com>
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
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <dsme/state.h>
#include <dsme/messages.h>
#include <dsme/protocol.h>
#include <dsme/processwd.h>
#include <mce/mode-names.h>
#include <gmodule.h>
#include "mce.h"
#include "mce-lib.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "mce-conf.h"
#include "datapipe.h"
#include "connectivity.h"

#define MODULE_NAME		"power-dsme"

#define MODULE_PROVIDES	"power"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

#define TRANSITION_DELAY		1000		/**< 1 second */

#define MCE_CONF_SOFTPOWEROFF_GROUP	"SoftPowerOff"

#define MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_CHARGER "ConnectivityPolicyCharger"

#define MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_BATTERY "ConnectivityPolicyBattery"

#define MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_POWERON "ConnectivityPolicyPowerOn"

#define MCE_CONF_SOFTPOWEROFF_CHARGER_POLICY_CONNECT "ChargerPolicyConnect"

#define SOFTOFF_CONNECTIVITY_FORCE_OFFLINE_STR		"forceoffline"
#define SOFTOFF_CONNECTIVITY_SOFT_OFFLINE_STR		"softoffline"
#define SOFTOFF_CONNECTIVITY_RETAIN_STR			"retain"
#define SOFTOFF_CHARGER_CONNECT_WAKEUP_STR		"wakeup"
#define SOFTOFF_CHARGER_CONNECT_IGNORE_STR		"ignore"

/** Soft poweroff connectivity policies */
enum {
	/** Policy not set */
	SOFTOFF_CONNECTIVITY_INVALID = MCE_INVALID_TRANSLATION,
	/** Retain connectivity */
	SOFTOFF_CONNECTIVITY_RETAIN = 0,
	/** Default setting when charger connected */
	DEFAULT_SOFTOFF_CONNECTIVITY_CHARGER = SOFTOFF_CONNECTIVITY_RETAIN,
	/** Go to offline mode if no connections are open */
	SOFTOFF_CONNECTIVITY_SOFT_OFFLINE = 1,
	/** Go to offline mode */
	SOFTOFF_CONNECTIVITY_FORCE_OFFLINE = 2,
	/** Default setting when running on battery */
	DEFAULT_SOFTOFF_CONNECTIVITY_BATTERY = SOFTOFF_CONNECTIVITY_FORCE_OFFLINE,
};

/** Soft poweron connectivity policies */
enum {
	/** Stay in offline mode */
	SOFTOFF_CONNECTIVITY_OFFLINE = 0,
	/** Default setting */
	DEFAULT_SOFTOFF_CONNECTIVITY_POWERON = SOFTOFF_CONNECTIVITY_OFFLINE,
	/** Restore previous mode */
	SOFTOFF_CONNECTIVITY_RESTORE = 1,
};

/** Soft poweroff charger connect policy */
enum {
	/** Stay in offline mode */
	SOFTOFF_CHARGER_CONNECT_WAKEUP = 0,
	/** Restore previous mode */
	SOFTOFF_CHARGER_CONNECT_IGNORE = 1,
	/** Default setting */
	DEFAULT_SOFTOFF_CHARGER_CONNECT = SOFTOFF_CHARGER_CONNECT_IGNORE,
};

/** Charger state */
static gboolean charger_connected = FALSE;

/** Pointer to the dsmesock connection */
static dsmesock_connection_t *dsme_conn = NULL;
/** TRUE if dsme is disabled (for debugging), FALSE if dsme is enabled */
static gboolean dsme_disabled = FALSE;

/** ID for state transition timer source */
static guint transition_timeout_cb_id = 0;

/** Soft poweroff connectivity policy when connected to charger */
static gint softoff_connectivity_policy_charger =
					DEFAULT_SOFTOFF_CONNECTIVITY_CHARGER;

/** Soft poweroff connectivity policy when running on battery */
static gint softoff_connectivity_policy_battery =
					DEFAULT_SOFTOFF_CONNECTIVITY_BATTERY;

/** Soft poweroff connectivity policy on poweron */
static gint softoff_connectivity_policy_poweron =
					DEFAULT_SOFTOFF_CONNECTIVITY_POWERON;

/** Soft poweroff charger connect policy */
static gint softoff_charger_connect_policy =
					DEFAULT_SOFTOFF_CHARGER_CONNECT;

static device_mode_t previous_mode = MCE_INVALID_MODE_INT32;

/** Mapping of soft poweroff connectivity integer <-> policy string */
static const mce_translation_t soft_poweroff_connectivity_translation[] = {
	{
		.number = SOFTOFF_CONNECTIVITY_RETAIN,
		.string = SOFTOFF_CONNECTIVITY_RETAIN_STR,
	}, {
		.number = SOFTOFF_CONNECTIVITY_SOFT_OFFLINE,
		.string = SOFTOFF_CONNECTIVITY_SOFT_OFFLINE_STR,
	}, {
		.number = SOFTOFF_CONNECTIVITY_FORCE_OFFLINE,
		.string = SOFTOFF_CONNECTIVITY_FORCE_OFFLINE_STR,
	}, { /* MCE_INVALID_TRANSLATION marks the end of this array */
		.number = MCE_INVALID_TRANSLATION,
		.string = NULL
	}
};

/** Mapping of soft poweron connectivity integer <-> policy string */
static const mce_translation_t soft_poweron_connectivity_translation[] = {
	{
		.number = SOFTOFF_CONNECTIVITY_RETAIN,
		.string = SOFTOFF_CONNECTIVITY_RETAIN_STR,
	}, {
		.number = SOFTOFF_CONNECTIVITY_SOFT_OFFLINE,
		.string = SOFTOFF_CONNECTIVITY_SOFT_OFFLINE_STR,
	}, {
		.number = SOFTOFF_CONNECTIVITY_FORCE_OFFLINE,
		.string = SOFTOFF_CONNECTIVITY_FORCE_OFFLINE_STR,
	}, { /* MCE_INVALID_TRANSLATION marks the end of this array */
		.number = MCE_INVALID_TRANSLATION,
		.string = NULL
	}
};

/** Mapping of soft poweroff charger connect integer <-> policy string */
static const mce_translation_t soft_poweroff_charger_connect_translation[] = {
	{
		.number = SOFTOFF_CHARGER_CONNECT_WAKEUP,
		.string = SOFTOFF_CHARGER_CONNECT_WAKEUP_STR,
	}, {
		.number = SOFTOFF_CHARGER_CONNECT_IGNORE,
		.string = SOFTOFF_CHARGER_CONNECT_IGNORE_STR,
	}, { /* MCE_INVALID_TRANSLATION marks the end of this array */
		.number = MCE_INVALID_TRANSLATION,
		.string = NULL
	}
};

static gboolean init_dsmesock(void);

/**
 * Generic send function for dsmesock messages
 * XXX: How should we handle sending failures?
 *
 * @param msg A pointer to the message to send
 */
static void mce_dsme_send(gpointer msg)
{
	if (dsme_disabled == TRUE)
		goto EXIT;

	if (dsme_conn == NULL) {
		mce_log(LL_CRIT,
			"Attempt to use dsme_conn uninitialised; aborting!");
		g_main_loop_quit(mainloop);
		exit(EXIT_FAILURE);
	}

	if ((dsmesock_send(dsme_conn, msg)) == -1) {
		mce_log(LL_CRIT,
			"dsmesock_send error: %s",
			g_strerror(errno));
#ifdef MCE_DSME_ERROR_POLICY
		g_main_loop_quit(mainloop);
		exit(EXIT_FAILURE);
#endif /* MCE_DSME_ERROR_POLICY */
	}

EXIT:
	return;
}

/**
 * Send pong message to the DSME process watchdog
 */
static void dsme_send_pong(void)
{
	/* Set up the message */
	DSM_MSGTYPE_PROCESSWD_PONG msg =
          DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_PONG);
	msg.pid = getpid();

	/* Send the message */
	mce_dsme_send(&msg);
	mce_log(LL_DEBUG,
		"DSM_MSGTYPE_PROCESSWD_PONG sent to DSME");
}

/**
 * Register to DSME process watchdog
 */
static void dsme_init_processwd(void)
{
	/* Set up the message */
	DSM_MSGTYPE_PROCESSWD_CREATE msg =
          DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_CREATE);
	msg.pid = getpid();

	/* Send the message */
	mce_dsme_send(&msg);
	mce_log(LL_DEBUG,
		"DSM_MSGTYPE_PROCESSWD_CREATE sent to DSME");
}

/**
 * Unregister from DSME process watchdog
 */
static void dsme_exit_processwd(void)
{
	/* Set up the message */
	DSM_MSGTYPE_PROCESSWD_DELETE msg =
          DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_DELETE);
	msg.pid = getpid();

	/* Send the message */
	mce_dsme_send(&msg);
	mce_log(LL_DEBUG,
		"DSM_MSGTYPE_PROCESSWD_DELETE sent to DSME");
}

/**
 * Send system state inquiry
 */
static void query_system_state(void)
{
	/* Set up the message */
	DSM_MSGTYPE_STATE_QUERY msg = DSME_MSG_INIT(DSM_MSGTYPE_STATE_QUERY);

	/* Send the message */
	mce_dsme_send(&msg);
	mce_log(LL_DEBUG,
		"DSM_MSGTYPE_STATE_QUERY sent to DSME");
}

/**
 * Request powerup
 */
static void request_powerup(void)
{
	/* Set up the message */
	DSM_MSGTYPE_POWERUP_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_POWERUP_REQ);

	/* Send the message */
	mce_dsme_send(&msg);
	mce_log(LL_DEBUG,
		"DSM_MSGTYPE_POWERUP_REQ sent to DSME");
}

/**
 * Request reboot
 */
static void request_reboot(void)
{
	/* Set up the message */
	DSM_MSGTYPE_REBOOT_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_REBOOT_REQ);

	/* Send the message */
	mce_dsme_send(&msg);
	mce_log(LL_DEBUG,
		"DSM_MSGTYPE_REBOOT_REQ sent to DSME");
}

/**
 * Request soft poweron
 */
static void request_soft_poweron(void)
{
	/* Disable the soft poweroff LED pattern */
	execute_datapipe_output_triggers(&led_pattern_deactivate_pipe,
					 MCE_LED_PATTERN_DEVICE_SOFT_OFF,
					 USE_INDATA);

	mce_rem_submode_int32(MCE_SOFTOFF_SUBMODE);
	execute_datapipe(&display_state_pipe,
			 GINT_TO_POINTER(MCE_DISPLAY_ON),
			 USE_INDATA, CACHE_INDATA);

	/* Connectivity policy */
	switch (softoff_connectivity_policy_poweron) {
	case SOFTOFF_CONNECTIVITY_FORCE_OFFLINE:
		mce_set_device_mode_int32(previous_mode);
		break;

	case SOFTOFF_CONNECTIVITY_OFFLINE:
	default:
		/* Do nothing */
		break;
	}
}

/**
 * Request soft poweroff
 */
static void request_soft_poweroff(void)
{
	gboolean connected;
	gint policy;

	if (charger_connected == TRUE)
		policy = softoff_connectivity_policy_charger;
	else
		policy = softoff_connectivity_policy_battery;

	connected = get_connectivity_status();

	/* Connectivity policy */
	switch (policy) {
	case SOFTOFF_CONNECTIVITY_SOFT_OFFLINE:
		/* If there are open connections, abort */
		if (connected == TRUE)
			break;

		/* Fall-through */
	case SOFTOFF_CONNECTIVITY_FORCE_OFFLINE:
		previous_mode = mce_get_device_mode_int32();

		/* Go offline */
		mce_set_device_mode_int32(MCE_FLIGHT_MODE_INT32);
		break;

	case SOFTOFF_CONNECTIVITY_RETAIN:
	default:
		break;
	}

	mce_add_submode_int32(MCE_SOFTOFF_SUBMODE);

	/* Enable the soft poweroff LED pattern */
	gchar *pattern = g_strdup(MCE_LED_PATTERN_DEVICE_SOFT_OFF);
	execute_datapipe(&led_pattern_activate_pipe, pattern, USE_INDATA, DONT_CACHE_INDATA);
	g_free(pattern);
}

/**
 * Timeout callback for transition
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean transition_timeout_cb(gpointer data)
{
	(void)data;

	transition_timeout_cb_id = 0;

	mce_rem_submode_int32(MCE_TRANSITION_SUBMODE);

	return FALSE;
}

/**
 * Cancel state transition timeout
 */
static void cancel_state_transition_timeout(void)
{
	/* Remove the timeout source for state transitions */
	if (transition_timeout_cb_id != 0) {
		g_source_remove(transition_timeout_cb_id);
		transition_timeout_cb_id = 0;
	}
}

/**
 * Setup state transition timeout
 */
static void setup_transition_timeout(void)
{
	cancel_state_transition_timeout();

	/* Setup new timeout */
	transition_timeout_cb_id =
		g_timeout_add(TRANSITION_DELAY, transition_timeout_cb, NULL);
}

/**
 * Request normal shutdown
 */
static void request_normal_shutdown(void)
{
	/* Set up the message */
	DSM_MSGTYPE_SHUTDOWN_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
	/* Send the message */
	mce_dsme_send(&msg);
	mce_log(LL_DEBUG,
		"DSM_MSGTYPE_SHUTDOWN_REQ (DSME_NORMAL_SHUTDOWN) "
		"sent to DSME");
}

/**
 * Convert DSME dsme state
 * to a state enum that we can export to datapipes
 *
 * @param dsmestate The DSME dsme_state_t with the value to convert
 * @return the converted value
 */
static system_state_t normalise_dsme_state(dsme_state_t dsmestate)
{
	system_state_t state = MCE_STATE_UNDEF;

	switch (dsmestate) {
	case DSME_STATE_SHUTDOWN:
		state = MCE_STATE_SHUTDOWN;
		break;

	case DSME_STATE_USER:
		state = MCE_STATE_USER;
		break;

	case DSME_STATE_ACTDEAD:
		state = MCE_STATE_ACTDEAD;
		break;

	case DSME_STATE_REBOOT:
		state = MCE_STATE_REBOOT;
		break;

	case DSME_STATE_BOOT:
		state = MCE_STATE_BOOT;
		break;

	case DSME_STATE_NOT_SET:
		break;

	case DSME_STATE_TEST:
		mce_log(LL_WARN,
			"Received DSME_STATE_TEST; treating as undefined");
		break;

	case DSME_STATE_MALF:
		mce_log(LL_WARN,
			"Received DSME_STATE_MALF; treating as undefined");
		break;

	case DSME_STATE_LOCAL:
		mce_log(LL_WARN,
			"Received DSME_STATE_LOCAL; treating as undefined");
		break;

	default:
		mce_log(LL_ERR,
			"Received an unknown state from DSME; "
			"treating as undefined");
		break;
	}

	return state;
}

/**
 * Callback for pending I/O from dsmesock
 *
 * XXX: is the error policy reasonable?
 *
 * @param source Unused
 * @param condition Unused
 * @param data Unused
 * @return TRUE on success, FALSE on failure
 */
static gboolean io_data_ready_cb(GIOChannel *source,
				 GIOCondition condition,
				 gpointer data)
{
	dsmemsg_generic_t *msg;
	DSM_MSGTYPE_STATE_CHANGE_IND *msg2;
	system_state_t oldstate = datapipe_get_gint(system_state_pipe);
	system_state_t newstate = MCE_STATE_UNDEF;

	(void)source;
	(void)condition;
	(void)data;

	if (dsme_disabled == TRUE)
		goto EXIT;

	if ((msg = (dsmemsg_generic_t *)dsmesock_receive(dsme_conn)) == NULL)
		goto EXIT;

        if (DSMEMSG_CAST(DSM_MSGTYPE_CLOSE, msg)) {
		/* DSME socket closed: try once to reopen;
		 * if that fails, exit
		 */
		mce_log(LL_ERR,
			"DSME socket closed; trying to reopen");

		if ((init_dsmesock()) == FALSE) {
			g_main_loop_quit(mainloop);
			exit(EXIT_FAILURE);
		}
        } else if (DSMEMSG_CAST(DSM_MSGTYPE_PROCESSWD_PING, msg)) {
		dsme_send_pong();
        } else if ((msg2 = DSMEMSG_CAST(DSM_MSGTYPE_STATE_CHANGE_IND, msg))) {
		newstate = normalise_dsme_state(msg2->state);
		mce_log(LL_DEBUG,
			"DSME device state change: %d",
			newstate);

		/* If we're changing to a different state,
		 * add the transition flag, UNLESS the old state
		 * was MCE_STATE_UNDEF
		 */
		if ((oldstate != newstate) && (oldstate != MCE_STATE_UNDEF))
			mce_add_submode_int32(MCE_TRANSITION_SUBMODE);

		switch (newstate) {
		case MCE_STATE_USER:
		{
			gchar *pattern = g_strdup(MCE_LED_PATTERN_DEVICE_ON);
			execute_datapipe(&led_pattern_activate_pipe, pattern, USE_INDATA, DONT_CACHE_INDATA);
			g_free(pattern);
			break;
		}

		case MCE_STATE_ACTDEAD:
		case MCE_STATE_BOOT:
		case MCE_STATE_UNDEF:
			mce_rem_submode_int32(MCE_MODECHG_SUBMODE);
			break;

		case MCE_STATE_SHUTDOWN:
		case MCE_STATE_REBOOT:
			mce_rem_submode_int32(MCE_MODECHG_SUBMODE);
			execute_datapipe_output_triggers(&led_pattern_deactivate_pipe, MCE_LED_PATTERN_DEVICE_ON, USE_INDATA);
			break;

		default:
			break;
		}

		execute_datapipe(&system_state_pipe,
				 GINT_TO_POINTER(newstate),
				 USE_INDATA, CACHE_INDATA);
        } else {
		mce_log(LL_DEBUG,
			"Unknown message type (%x) received from DSME!",
			msg->type_); /* <- unholy access of a private member */
	}

	free(msg);

EXIT:
	return TRUE;
}

/**
 * Callback for I/O errors from dsmesock
 *
 * @param source Unused
 * @param condition Unused
 * @param data Unused
 * @return Will never return; if there is an I/O-error we exit the mainloop
 */
static gboolean io_error_cb(GIOChannel *source,
			    GIOCondition condition,
			    gpointer data) G_GNUC_NORETURN;

static gboolean io_error_cb(GIOChannel *source,
			    GIOCondition condition,
			    gpointer data)
{
	/* Silence warnings */
	(void)source;
	(void)condition;
	(void)data;

	/* DSME socket closed/error */
	mce_log(LL_CRIT,
		"DSME socket closed/error, exiting...");
	g_main_loop_quit(mainloop);
	exit(EXIT_FAILURE);
}

/**
 * D-Bus callback for the init done notification signal
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean init_done_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	(void)msg;

	mce_log(LL_DEBUG,
		"Received init done notification");

	if ((mce_get_submode_int32() & MCE_TRANSITION_SUBMODE)) {
		setup_transition_timeout();
	}

	mce_log(LL_DEBUG, "Send device_lock_inhibit_pipe(FALSE)");
	(void)execute_datapipe(&device_lock_inhibit_pipe,
			       GINT_TO_POINTER(FALSE),
			       USE_INDATA, CACHE_INDATA);

	status = TRUE;

//EXIT:
	return status;
}

/**
 * Datapipe trigger for the charger state
 *
 * @param data TRUE if the charger was connected,
 *	       FALSE if the charger was disconnected
 */
static void charger_state_trigger(gconstpointer const data)
{
	submode_t submode = mce_get_submode_int32();

	charger_connected = GPOINTER_TO_INT(data);

	if ((submode & MCE_SOFTOFF_SUBMODE) != 0) {
		if (softoff_charger_connect_policy == SOFTOFF_CHARGER_CONNECT_WAKEUP) {
			request_soft_poweron();
		}
	}
}

/**
 * Initialise dsmesock connection
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean init_dsmesock(void)
{
	GIOChannel *ioch;
	gboolean status = FALSE;

	if (dsme_conn == NULL) {
		if ((dsme_conn = dsmesock_connect()) == NULL) {
			mce_log(LL_CRIT,
				"Failed to open DSME socket");
			goto EXIT;
		}
	}

	if ((ioch = g_io_channel_unix_new(dsme_conn->fd)) == NULL) {
		mce_log(LL_CRIT,
			"Failed to set up I/O channel for DSME socket");
		goto EXIT;
	}

	(void)g_io_add_watch(ioch, G_IO_IN | G_IO_PRI,
			     io_data_ready_cb, NULL);
	(void)g_io_add_watch(ioch, G_IO_ERR | G_IO_HUP,
			     io_error_cb, NULL);

	/* Query the current system state; if the mainloop isn't running,
	 * this will trigger an update when the mainloop starts
	 */
	query_system_state();

	status = TRUE;

EXIT:
	return status;
}

static void system_power_request_trigger(gconstpointer data)
{
	power_req_t request = (power_req_t)data;
	
	switch (request)
	{
		case MCE_POWER_REQ_OFF:
			request_normal_shutdown();
			break;
		case MCE_POWER_REQ_SOFT_OFF:
			request_soft_poweroff();
			break;
		case MCE_POWER_REQ_ON:
			request_powerup();
			break;
		case MCE_POWER_REQ_SOFT_ON:
			request_soft_poweron();
			break;
		case MCE_POWER_REQ_REBOOT:
			request_reboot();
			break;
		case MCE_POWER_REQ_UNDEF:
		default:
			break;
	}
}

/**
 * Init function for the power-dsme component
 *
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;
	gboolean status = FALSE;
	gchar *tmp = NULL;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&charger_state_pipe,
					  charger_state_trigger);
	
	append_output_trigger_to_datapipe(&system_power_request_pipe,
					  system_power_request_trigger);

	mce_log(LL_DEBUG,
		"Connecting to DSME sock");

	if (init_dsmesock() == FALSE) {
		goto EXIT;
	}

	/* Register with DSME's process watchdog */
	dsme_init_processwd();

	/* init_done */
	if (mce_dbus_handler_add("com.nokia.startup.signal",
				 "init_done",
				 NULL,
				 DBUS_MESSAGE_TYPE_SIGNAL,
				 init_done_dbus_cb) == NULL)
		goto EXIT;

	/* Get configuration options */
	tmp = mce_conf_get_string(MCE_CONF_SOFTPOWEROFF_GROUP,
				  MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_CHARGER,
				  "",
				  NULL);

	softoff_connectivity_policy_charger = mce_translate_string_to_int_with_default(soft_poweroff_connectivity_translation, tmp, DEFAULT_SOFTOFF_CONNECTIVITY_CHARGER);
	g_free(tmp);

	tmp = mce_conf_get_string(MCE_CONF_SOFTPOWEROFF_GROUP,
				  MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_BATTERY,
				  "",
				  NULL);

	softoff_connectivity_policy_battery = mce_translate_string_to_int_with_default(soft_poweroff_connectivity_translation, tmp, DEFAULT_SOFTOFF_CONNECTIVITY_BATTERY);
	g_free(tmp);

	tmp = mce_conf_get_string(MCE_CONF_SOFTPOWEROFF_GROUP,
				  MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_POWERON,
				  "",
				  NULL);

	softoff_connectivity_policy_poweron = mce_translate_string_to_int_with_default(soft_poweron_connectivity_translation, tmp, DEFAULT_SOFTOFF_CONNECTIVITY_POWERON);
	g_free(tmp);

	tmp = mce_conf_get_string(MCE_CONF_SOFTPOWEROFF_GROUP,
				  MCE_CONF_SOFTPOWEROFF_CHARGER_POLICY_CONNECT,
				  "",
				  NULL);

	softoff_charger_connect_policy = mce_translate_string_to_int_with_default(soft_poweroff_charger_connect_translation, tmp, DEFAULT_SOFTOFF_CHARGER_CONNECT);
	g_free(tmp);

	status = TRUE;

EXIT:
	return status ? NULL : "dsme failed to initalize";
}

/**
 * Exit function for the mce-dsme component
 *
 * @todo D-Bus unregistration
 * @todo trigger unregistration
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;
	if (dsme_conn != NULL) {
		mce_log(LL_DEBUG,
			"Disabling DSME process watchdog");
		dsme_exit_processwd();

		mce_log(LL_DEBUG,
			"Closing DSME sock");
		dsmesock_close(dsme_conn);
	}

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&charger_state_pipe,
					    charger_state_trigger);
	remove_output_trigger_from_datapipe(&system_power_request_pipe,
					    system_power_request_trigger);
	/* Remove all timer sources */
	cancel_state_transition_timeout();

	return;
}
