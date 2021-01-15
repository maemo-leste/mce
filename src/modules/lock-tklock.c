/**
 * @file tklock.c
 * This file implements the touchscreen/keypad lock component
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
#include <glib/gstdio.h>
#include <gmodule.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <mce/mode-names.h>
#include <systemui/dbus-names.h>
#include <systemui/tklock-dbus-names.h>

#include "mce.h"
#include "mce-io.h"
#include "mce-log.h"
#include "datapipe.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "mce-rtconf.h"
#include "event-input.h"

#define MODULE_NAME		"lock-tklock"

#define MODULE_PROVIDES	"lock"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 1000
};

#ifndef MCE_GCONF_LOCK_PATH
/** Path to the GConf settings for the touchscreen/keypad lock */
#define MCE_GCONF_LOCK_PATH		"/system/osso/dsm/locks"
#endif /* MCE_GCONF_LOCK_PATH */

/** Default fallback setting for the touchscreen/keypad autolock */
#define DEFAULT_TK_AUTOLOCK		FALSE		/* FALSE / TRUE */

/** Path to the touchscreen/keypad autolock GConf setting */
#define MCE_GCONF_TK_AUTOLOCK_ENABLED_PATH	MCE_GCONF_LOCK_PATH "/touchscreen_keypad_autolock_enabled"

/** Name of D-Bus callback to provide to Touchscreen/Keypad Lock SystemUI */
#define MCE_TKLOCK_CB_REQ		"tklock_callback"
/** Delay before the touchscreen/keypad is unlocked */
#define MCE_TKLOCK_UNLOCK_DELAY		500		/**< 0.5 seconds */

#ifndef MCE_CONF_TKLOCK_GROUP
/** Name of Touchscreen/Keypad lock configuration group */
#define MCE_CONF_TKLOCK_GROUP		"TKLock"
#endif /* MCE_CONF_TKLOCK_GROUP */

/** Name of configuration key for touchscreen/keypad immediate blanking */
#define MCE_CONF_BLANK_IMMEDIATELY	"BlankImmediately"

/** Name of configuration key for touchscreen/keypad immediate dimming */
#define MCE_CONF_DIM_IMMEDIATELY	"DimImmediately"

/** Name of configuration key for touchscreen/keypad dim timeout */
#define MCE_CONF_DIM_DELAY		"DimDelay"

/** Name of configuration key for touchscreen immediate disabling */
#define MCE_CONF_TS_OFF_IMMEDIATELY	"DisableTSImmediately"

/** Name of configuration key for keyboard slide autolock */
#define MCE_CONF_AUTOLOCK_SLIDE_OPEN	"AutolockWhenSlideOpen"

/** Name of configuration key for lens cover triggered tklock unlocking */
#define MCE_CONF_LENS_COVER_UNLOCK	"LensCoverUnlock"

/** Default fallback setting for tklock immediate blanking */
#define DEFAULT_BLANK_IMMEDIATELY	FALSE		/* FALSE / TRUE */

/** Default fallback setting for tklock immediate dimming */
#define DEFAULT_DIM_IMMEDIATELY		FALSE		/* FALSE / TRUE */

/** Default visual lock blank timeout */
#define DEFAULT_VISUAL_BLANK_DELAY	5000		/* 5 seconds */

/** Default visual lock blank timeout */
#define DEFAULT_VISUAL_FORCED_BLANK_DELAY	30000	/* 30 seconds */

/** Default delay before the display dims */
#define DEFAULT_DIM_DELAY		3000		/* 3 seconds */

/** Default fallback setting for touchscreen immediate disabling */
#define DEFAULT_TS_OFF_IMMEDIATELY	TRUE		/* FALSE / TRUE */

/** Default fallback setting for autolock with open keyboard slide */
#define DEFAULT_AUTOLOCK_SLIDE_OPEN	FALSE		/* FALSE */

/** Default fallback setting for lens cover triggered tklock unlocking */
#define DEFAULT_LENS_COVER_UNLOCK	TRUE		/* TRUE */

/** Default fallback setting for proximity lock when callstate == ringing */
#define DEFAULT_PROXIMITY_LOCK_WHEN_RINGING	TRUE		/* TRUE */

#define DEFAULT_PROXIMITY_UNLOCK_DELAY 500 /* 0.5 second */

/**
 * TRUE if the touchscreen/keypad autolock is enabled,
 * FALSE if the touchscreen/keypad autolock is disabled
 */
static gboolean tk_autolock_enabled = DEFAULT_TK_AUTOLOCK;

/** GConf callback ID for the autolock entry */
static guint tk_autolock_enabled_cb_id = 0;

/** Blanking timeout ID for the visual tklock */
static guint tklock_visual_blank_timeout_cb_id = 0;

/** Forced blanking timeout ID for the visual tklock */
static guint tklock_visual_forced_blank_timeout_cb_id = 0;

/** Dimming timeout ID for the tklock */
static guint tklock_dim_timeout_cb_id = 0;

/** ID for touchscreen/keypad unlock source */
static guint tklock_unlock_timeout_cb_id = 0;

static guint tklock_disable_timeout_cb_id = 0;

static guint proximity_unlock_timeout_cb_id = 0;

/** Blank immediately on tklock instead of dim/blank */
static gboolean blank_immediately = DEFAULT_BLANK_IMMEDIATELY;

/** Dim immediately on tklock instead of timeout */
static gboolean dim_immediately = DEFAULT_DIM_IMMEDIATELY;

/** Touchscreen/keypad dim timeout */
static gint dim_delay = DEFAULT_DIM_DELAY;

/** Disable touchscreen immediately on tklock instead of at blank */
static gboolean disable_ts_immediately = DEFAULT_TS_OFF_IMMEDIATELY;

/** Inhibit autolock when slide is open */
static gboolean autolock_with_open_slide = DEFAULT_AUTOLOCK_SLIDE_OPEN;

/** Unlock the TKLock when the lens cover is opened */
static gboolean lens_cover_unlock = DEFAULT_LENS_COVER_UNLOCK;

/** Proximity based locking when the phone is ringing */
static gboolean proximity_lock_when_ringing = DEFAULT_PROXIMITY_LOCK_WHEN_RINGING;

/** Submode at the beginning of a call */
static submode_t saved_submode = MCE_INVALID_SUBMODE;
static submode_t call_submode = MCE_INVALID_SUBMODE;

/** TKLock UI state type */
typedef enum {
	/** No TKLock UI active */
	MCE_TKLOCK_UI_NONE = 0,
	/** Normal TKLock UI active */
	MCE_TKLOCK_UI_NORMAL = 1,
	/** Event eater UI active */
	MCE_TKLOCK_UI_EVENT_EATER = 2,
	/** Slider UI active */
	MCE_TKLOCK_UI_SLIDER = 3
} tklock_ui_state_t;

/** TKLock UI state */
static tklock_ui_state_t tklock_ui_state = MCE_TKLOCK_UI_NONE;

static guint unlock_attempts = 0;

/* Valid triggers for autorelock */

/** No autorelock triggers */
#define AUTORELOCK_NO_TRIGGERS	0
/** Autorelock on keyboard slide closed */
#define AUTORELOCK_KBD_SLIDE	(1 << 0)
/** Autorelock on lens cover */
#define AUTORELOCK_LENS_COVER	(1 << 1)
/** Autorelock on proximity sensor */
#define AUTORELOCK_ON_PROXIMITY	(1 << 2)

/** Inhibit proximity relock type */
typedef enum {
	/** Inhibit proximity relock */
	MCE_INHIBIT_PROXIMITY_RELOCK = 0,
	/** Allow proximity relock */
	MCE_ALLOW_PROXIMITY_RELOCK = 1,
	/** Temporarily inhibit proximity relock */
	MCE_TEMP_INHIBIT_PROXIMITY_RELOCK = 2
} inhibit_proximity_relock_t;

static gboolean ignore_proximity_events = TRUE;

/** Inhibit autorelock using proximity sensor */
static inhibit_proximity_relock_t inhibit_proximity_relock = MCE_ALLOW_PROXIMITY_RELOCK;

/** TKLock activated due to proximity */
static gboolean tklock_proximity = FALSE;

/** Autorelock triggers */
static gint autorelock_triggers = AUTORELOCK_NO_TRIGGERS;
static gboolean disable_eveater(gboolean silent);
static void set_tklock_state(lock_state_t lock_state);
static void touchscreen_trigger(gconstpointer const data);
static void cancel_tklock_dim_timeout(void);
static void cancel_tklock_unlock_timeout(void);
static void process_proximity_state(void);
static gboolean disable_tklock(gboolean silent);
static gboolean tklock_disable_timeout_cb(gpointer data);
/**
 * Query the event eater status
 *
 * @return TRUE if the event eater is enabled,
 *         FALSE if the event eater is disabled
 */
static gboolean is_eveater_enabled(void)
{
	return ((mce_get_submode_int32() & MCE_EVEATER_SUBMODE) != 0);
}

/**
 * Query the touchscreen/keypad lock status
 *
 * @return TRUE if the touchscreen/keypad lock is enabled,
 *         FALSE if the touchscreen/keypad lock is disabled
 */
static gboolean is_tklock_enabled(void)
{
	return ((mce_get_submode_int32() & MCE_TKLOCK_SUBMODE) != 0);
}

static void update_saved_submode(void)
{
	call_submode = MCE_INVALID_SUBMODE;
	if(is_tklock_enabled())
	{
		saved_submode |= MCE_TKLOCK_SUBMODE;
	}
	else
	{
		saved_submode = ~(~saved_submode | MCE_TKLOCK_SUBMODE);
	}
}


/**
 * Query the visual touchscreen/keypad lock status
 *
 * @return TRUE if the visual touchscreen/keypad lock is enabled,
 *         FALSE if the visual touchscreen/keypad lock is disabled
 */
static gboolean is_visual_tklock_enabled(void)
{
	return ((mce_get_submode_int32() & MCE_VISUAL_TKLOCK_SUBMODE) != 0);
}

/**
 * Query the autorelock status
 *
 * @return TRUE if the autorelock is enabled,
 *         FALSE if the autorelock is disabled
 */
static gboolean is_autorelock_enabled(void)
{
	return ((mce_get_submode_int32() & MCE_AUTORELOCK_SUBMODE) != 0);
}

static void get_submode(void)
{
	saved_submode = mce_get_submode_int32();
	call_submode = saved_submode;
}

/**
 * Enable auto-relock
 */
static void enable_autorelock(void)
{
	cover_state_t kbd_slide_state = datapipe_get_gint(keyboard_slide_pipe);
	cover_state_t lens_cover_state = datapipe_get_gint(lens_cover_pipe);

	if (autorelock_triggers != AUTORELOCK_ON_PROXIMITY) {
		/* Reset autorelock triggers */
		autorelock_triggers = AUTORELOCK_NO_TRIGGERS;

		/* If the keyboard slide is closed, use it as a trigger */
		if (kbd_slide_state == COVER_CLOSED)
			autorelock_triggers |= AUTORELOCK_KBD_SLIDE;

		/* If the lens cover is closed, use it as a trigger */
		if (lens_cover_state == COVER_CLOSED)
			autorelock_triggers |= AUTORELOCK_LENS_COVER;
	}

	/* Only setup touchscreen monitoring once,
	 * and only if there are autorelock triggers
	 * and it's not the proximity sensor
	 */
	if ((is_autorelock_enabled() == FALSE) &&
	    (autorelock_triggers != AUTORELOCK_NO_TRIGGERS) &&
	    (autorelock_triggers != AUTORELOCK_ON_PROXIMITY)) {
		append_input_trigger_to_datapipe(&touchscreen_pipe,
						 touchscreen_trigger);
	}

	mce_add_submode_int32(MCE_AUTORELOCK_SUBMODE);
}

/**
 * Disable auto-relock
 */
static void disable_autorelock(void)
{
	/* Touchscreen monitoring is only needed for the autorelock */
	remove_input_trigger_from_datapipe(&touchscreen_pipe,
					   touchscreen_trigger);
	mce_rem_submode_int32(MCE_AUTORELOCK_SUBMODE);

	/* Reset autorelock triggers */
	autorelock_triggers = AUTORELOCK_NO_TRIGGERS;
}

/**
 * Disable auto-relock based on policy
 */
static void disable_autorelock_policy(void)
{
	/* If the tklock is enabled
	 * or proximity autorelock is active, don't disable
	 */
	if ((is_tklock_enabled() == TRUE) ||
	    (autorelock_triggers == AUTORELOCK_ON_PROXIMITY))
		goto EXIT;

	disable_autorelock();

EXIT:
	return;
}

/**
 * Enable/disable touchscreen events
 *
 * @param enable TRUE enable events, FALSE disable events
 * @return TRUE on success, FALSE on failure
 */
static gboolean ts_event_control(gboolean enable)
{
	execute_datapipe(&touchscreen_suspend_pipe, GINT_TO_POINTER(!enable),
			USE_INDATA, CACHE_INDATA);

	return TRUE;
}

/**
 * Policy based enabling of touchscreen
 *
 * @return TRUE on success, FALSE on failure or partial failure
 */
static gboolean ts_enable_policy(void)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	cover_state_t lid_cover_state = datapipe_get_gint(lid_cover_pipe);
	alarm_ui_state_t alarm_ui_state =
				datapipe_get_gint(alarm_ui_state_pipe);
	gboolean status = FALSE;

	/* If the cover is closed, don't bother */
	if (lid_cover_state == COVER_CLOSED)
		goto EXIT2;

	if ((system_state == MCE_STATE_USER) ||
	    (alarm_ui_state == MCE_ALARM_UI_RINGING_INT32) ||
	    (alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32)) {
		if (ts_event_control(TRUE) == FALSE)
			goto EXIT;
	}

EXIT2:
	status = TRUE;

EXIT:
	return status;
}

/**
 * Policy based disabling of touchscreen
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean ts_disable_policy(void)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	alarm_ui_state_t alarm_ui_state =
				datapipe_get_gint(alarm_ui_state_pipe);
	submode_t submode = mce_get_submode_int32();
	gboolean status = FALSE;

	/* If we're in softoff submode, always disable */
	if ((submode & MCE_SOFTOFF_SUBMODE) != 0) {
		if (ts_event_control(FALSE) == FALSE)
			goto EXIT;

		goto EXIT2;
	}

	/* If the Alarm UI is visible, don't disable,
	 * unless the tklock UI is active
	 */
	if (((alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
	     (alarm_ui_state == MCE_ALARM_UI_RINGING_INT32)) &&
	    (tklock_ui_state != MCE_TKLOCK_UI_NORMAL)) {
		mce_log(LL_DEBUG,
			"Alarm UI visible; refusing to disable touchscreen "
			"and keypad events");
		goto EXIT2;
	}

	if (system_state != MCE_STATE_USER)
	{
		if (ts_event_control(FALSE) == FALSE)
					goto EXIT;
	} else if((display_state == MCE_DISPLAY_OFF) && (is_tklock_enabled() == TRUE)) {
		if (ts_event_control(FALSE) == FALSE)
					goto EXIT;
	} else if (is_tklock_enabled() == TRUE && disable_ts_immediately == TRUE) {
		if (ts_event_control(FALSE) == FALSE)
					goto EXIT;
	}

EXIT2:
	status = TRUE;

EXIT:
	if (status == FALSE) {
		mce_log(LL_ERR, "Failed to disable ts/kp events!");
	}

	return status;
}

/**
 * Synthesise activity, since activity is filtered when tklock is active;
 * also, the lock key doesn't normally generate activity
 */
static void synthesise_activity(void)
{
	(void)execute_datapipe(&device_inactive_pipe,
			       GINT_TO_POINTER(FALSE),
			       USE_INDATA, CACHE_INDATA);
}

static void cancel_tklock_disable_timeout(void)
{
	if (tklock_disable_timeout_cb_id)
	{
		g_source_remove(tklock_disable_timeout_cb_id);
		tklock_disable_timeout_cb_id = 0;
		mce_log(LL_DEBUG, "close_tklock_ui: remove timeout cb");
	}
}

/**
 * Send the touchscreen/keypad lock mode
 *
 * @param method_call A DBusMessage to reply to;
 *                    pass NULL to send a tklock mode signal instead
 * @return TRUE on success, FALSE on failure
 */
static gboolean mce_send_tklock_mode(DBusMessage *const method_call)
{
	DBusMessage *msg = NULL;
	const gchar *modestring;
	gboolean status = FALSE;

	if (is_tklock_enabled() == TRUE)
		modestring = MCE_TK_LOCKED;
	else
		modestring = MCE_TK_UNLOCKED;

	/* If method_call is set, send a reply,
	 * otherwise, send a signal
	 */
	if (method_call != NULL) {
		msg = dbus_new_method_reply(method_call);
	} else {
		/* tklock_mode_ind */
		msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
				      MCE_TKLOCK_MODE_SIG);
	}

	/* Append the new mode */
	if (dbus_message_append_args(msg,
				     DBUS_TYPE_STRING, &modestring,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append %sargument to D-Bus message "
			"for %s.%s",
			method_call ? "reply " : "",
			method_call ? MCE_REQUEST_IF :
				      MCE_SIGNAL_IF,
			method_call ? MCE_TKLOCK_MODE_GET :
				      MCE_TKLOCK_MODE_SIG);
		dbus_message_unref(msg);
		goto EXIT;
	}

	/* Send the message */
	status = dbus_send_message(msg);

EXIT:
	return status;
}

static gboolean open_tklock_ui(const dbus_uint32_t mode,
			       const dbus_bool_t silent)
{
	const gchar *const cb_service = MCE_SERVICE;
	const gchar *const cb_path = MCE_REQUEST_PATH;
	const gchar *const cb_interface = MCE_REQUEST_IF;
	const gchar *const cb_method = MCE_TKLOCK_CB_REQ;
	dbus_bool_t flicker_key = has_flicker_key;
	tklock_ui_state_t new_tklock_ui_state;
	DBusMessage *reply = NULL;
	gboolean status = FALSE;
	dbus_int32_t retval;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	switch (mode) {
	case TKLOCK_ENABLE:
		new_tklock_ui_state = MCE_TKLOCK_UI_NORMAL;
		break;

	case TKLOCK_ONEINPUT:
		new_tklock_ui_state = MCE_TKLOCK_UI_EVENT_EATER;
		break;

	case TKLOCK_ENABLE_VISUAL:
		new_tklock_ui_state = MCE_TKLOCK_UI_SLIDER;
		break;

	default:
		mce_log(LL_ERR, "tklock.c: Invalid TKLock UI mode requested");
		goto EXIT;
	}

	mce_log(LL_DEBUG, "tklock.c: opening tklock mode %i", new_tklock_ui_state);

	reply = dbus_send_with_block(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
				     SYSTEMUI_REQUEST_IF,
				     SYSTEMUI_TKLOCK_OPEN_REQ,
				     DEFAULT_DBUS_REPLY_TIMEOUT,
				     DBUS_TYPE_STRING, &cb_service,
				     DBUS_TYPE_STRING, &cb_path,
				     DBUS_TYPE_STRING, &cb_interface,
				     DBUS_TYPE_STRING, &cb_method,
				     DBUS_TYPE_UINT32, &mode,
				     DBUS_TYPE_BOOLEAN, &silent,
				     DBUS_TYPE_BOOLEAN, &flicker_key,
				     DBUS_TYPE_INVALID);

	if (reply == NULL)
		goto EXIT;

	/* Make sure we didn't get an error message */
	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		char *error_msg;

		/* If we got an error, it's a string */
		if (dbus_message_get_args(reply, &error,
					  DBUS_TYPE_STRING, &error_msg,
					  DBUS_TYPE_INVALID) == FALSE) {
			mce_log(LL_CRIT,
				"Failed to get error reply from %s.%s: %s",
				SYSTEMUI_REQUEST_IF, SYSTEMUI_TKLOCK_OPEN_REQ,
				error.message);
			dbus_error_free(&error);
		} else {
			mce_log(LL_ERR,
				"D-Bus call to %s.%s failed: %s",
				SYSTEMUI_REQUEST_IF, SYSTEMUI_TKLOCK_OPEN_REQ,
				error_msg);
		}

		goto EXIT2;
	}

	/* Extract reply */
	if (dbus_message_get_args(reply, &error,
				  DBUS_TYPE_INT32, &retval,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_ERR,
			"Failed to get reply argument from %s.%s; %s",
			SYSTEMUI_REQUEST_IF, SYSTEMUI_TKLOCK_OPEN_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT2;
	}

	status = TRUE;

	tklock_ui_state = new_tklock_ui_state;

EXIT2:
	dbus_message_unref(reply);

EXIT:
	return status;
}

static gboolean close_tklock_ui(const dbus_bool_t silent)
{
	const gchar *const cb_service = MCE_SERVICE;
	const gchar *const cb_path = MCE_REQUEST_PATH;
	const gchar *const cb_interface = MCE_REQUEST_IF;
	const gchar *const cb_method = MCE_TKLOCK_CB_REQ;
	DBusMessage *reply = NULL;
	gboolean status = FALSE;
	dbus_int32_t retval;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "tklock.c: closeing tklock");

	reply = dbus_send_with_block(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
				     SYSTEMUI_REQUEST_IF,
				     SYSTEMUI_TKLOCK_CLOSE_REQ,
				     DEFAULT_DBUS_REPLY_TIMEOUT,
				     DBUS_TYPE_STRING, &cb_service,
				     DBUS_TYPE_STRING, &cb_path,
				     DBUS_TYPE_STRING, &cb_interface,
				     DBUS_TYPE_STRING, &cb_method,
				     DBUS_TYPE_BOOLEAN, &silent,
				     DBUS_TYPE_INVALID);

	if (reply == NULL)
		goto EXIT;

	if (dbus_message_get_args(reply, &error,
				  DBUS_TYPE_INT32, &retval,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_ERR, "tklock.c: "
			"Failed to get reply argument from %s.%s; %s",
			SYSTEMUI_REQUEST_IF, SYSTEMUI_TKLOCK_CLOSE_REQ,
			error.message);
		dbus_message_unref(reply);
		dbus_error_free(&error);
		goto EXIT;
	}

	dbus_message_unref(reply);


	status = TRUE;

	tklock_ui_state = MCE_TKLOCK_UI_NONE;

EXIT:
	return status;
}

/**
 * Enable the touchscreen/keypad lock
 *
 * If the internal state indicates that the tklock is already enabled,
 * silent mode will always be used
 *
 * @param silent TRUE to disable infoprints, FALSE to enable infoprints
 * @return TRUE on success, FALSE on failure
 */
static gboolean enable_tklock(gboolean silent)
{
	gboolean status = FALSE;

	if (is_tklock_enabled() == TRUE) {
		mce_log(LL_DEBUG, "tklock.c: "
			"Touchscreen/keypad lock enabled "
			"when already enabled");
		silent = TRUE;
	}
	cancel_tklock_disable_timeout();
	if (open_tklock_ui(TKLOCK_ENABLE, silent) == FALSE)
	{
		mce_log(LL_DEBUG, "tklock.c: failed to open tklock ui");
		disable_tklock(TRUE);
		goto EXIT;
	}

	mce_add_submode_int32(MCE_TKLOCK_SUBMODE);
	mce_rem_submode_int32(MCE_EVEATER_SUBMODE);
	mce_rem_submode_int32(MCE_VISUAL_TKLOCK_SUBMODE);
	(void)mce_send_tklock_mode(NULL);

	enable_autorelock();

	status = TRUE;

EXIT:
	return status;
}

/**
 * Cancel timeout for visual touchscreen/keypad lock forced blanking
 */
static void cancel_tklock_visual_forced_blank_timeout(void)
{
	/* Remove the timer source for visual tklock forced blanking */
	if (tklock_visual_forced_blank_timeout_cb_id != 0) {
		g_source_remove(tklock_visual_forced_blank_timeout_cb_id);
		tklock_visual_forced_blank_timeout_cb_id = 0;
	}
}

/**
 * Cancel timeout for visual touchscreen/keypad lock blanking
 */
static void cancel_tklock_visual_blank_timeout(void)
{
	/* Remove the timer source for visual tklock blanking */
	if (tklock_visual_blank_timeout_cb_id != 0) {
		g_source_remove(tklock_visual_blank_timeout_cb_id);
		tklock_visual_blank_timeout_cb_id = 0;
	}
}

static gboolean tklock_visual_blank_timeout_cb(gpointer data)
{
	(void)data;

	cancel_tklock_visual_blank_timeout();
	cancel_tklock_visual_forced_blank_timeout();

	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_OFF),
			       USE_INDATA, CACHE_INDATA);

	return FALSE;
}

/**
 * Setup the timeout for touchscreen/keypad lock blanking
 */
static void setup_tklock_visual_blank_timeout(void)
{
	cancel_tklock_dim_timeout();
	cancel_tklock_visual_blank_timeout();

	/* Setup blank timeout */
	tklock_visual_blank_timeout_cb_id =
		g_timeout_add(DEFAULT_VISUAL_BLANK_DELAY,
			      tklock_visual_blank_timeout_cb, NULL);

	/* Setup forced blank timeout */
	if (tklock_visual_forced_blank_timeout_cb_id == 0) {
		tklock_visual_forced_blank_timeout_cb_id =
			g_timeout_add(DEFAULT_VISUAL_FORCED_BLANK_DELAY,
				      tklock_visual_blank_timeout_cb, NULL);
	}
}

/**
 * Timeout callback for touchscreen/keypad lock dim
 *
 * @param data A boolean passed as a pointer;
 *             TRUE to force blanking, FALSE to use normal blank timeout
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean tklock_dim_timeout_cb(gpointer data)
{
	gboolean force_blank = GPOINTER_TO_INT(data);

	tklock_dim_timeout_cb_id = 0;

	mce_log(LL_DEBUG, "tklock.c MCE_DISPLAY_DIM");
	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_DIM),
			       USE_INDATA, CACHE_INDATA);

	if ((force_blank == TRUE) || (blank_immediately == TRUE)) {
		mce_log(LL_DEBUG, "tklock.c MCE_DISPLAY_OFF");
		(void)execute_datapipe(&display_state_pipe,
				       GINT_TO_POINTER(MCE_DISPLAY_OFF),
				       USE_INDATA, CACHE_INDATA);
	}

	return FALSE;
}

/**
 * Cancel timeout for tklock dimming
 */
static void cancel_tklock_dim_timeout(void)
{
	/* Remove the timer source for tklock dimming */
	if (tklock_dim_timeout_cb_id != 0) {
		g_source_remove(tklock_dim_timeout_cb_id);
		tklock_dim_timeout_cb_id = 0;
	}
}

static void setup_tklock_dim_timeout(gint timeout, gboolean force_blank)
{
	gint ttimeout = (timeout == -1) ? dim_delay : timeout;

	cancel_tklock_visual_forced_blank_timeout();
	cancel_tklock_visual_blank_timeout();
	cancel_tklock_dim_timeout();

	/* Setup new timeout */
	tklock_dim_timeout_cb_id =
		g_timeout_add(ttimeout,
			      tklock_dim_timeout_cb,
			      GINT_TO_POINTER(force_blank));
}

static void setup_dim_blank_timeout_policy(gboolean force_blank)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);

	/* If the display is already blank, don't bother */
	if (display_state == MCE_DISPLAY_OFF)
		goto EXIT;

	if ((display_state == MCE_DISPLAY_DIM) ||
	    ((display_state == MCE_DISPLAY_ON) &&
	     ((dim_immediately == TRUE) ||
	      (blank_immediately == TRUE) ||
	      (force_blank == TRUE)))) {
		setup_tklock_dim_timeout(0, force_blank);
	} else {
		setup_tklock_dim_timeout(-1, FALSE);
	}

EXIT:
	return;
}

/**
 * Enable the touchscreen/keypad lock with policy
 *
 * If the internal state indicates that the tklock is already enabled,
 * silent mode will always be used
 *
 * @param force_blank Force immediate blanking
 * @return TRUE on success, FALSE on failure
 */
static gboolean enable_tklock_policy(gboolean force_blank)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "tklock.c: %s", __func__);
	/* If we're in any other state than USER, don't enable tklock */
	if (system_state != MCE_STATE_USER) {
		cancel_tklock_disable_timeout();
		status = TRUE;
		goto EXIT;
	}
	/* Enable lock */
	if (enable_tklock(force_blank |
			  dim_immediately |
			  blank_immediately) == FALSE)
		goto EXIT;

	setup_dim_blank_timeout_policy(force_blank);

	/* Disable touchscreen and keypad */
	(void)ts_disable_policy();

	status = TRUE;

EXIT:
	return status;
}

/**
 * Disable the touchscreen/keypad lock
 *
 * If the internal state indicates that the tklock is already disabled,
 * silent mode will always be used
 *
 * @param silent Enable without infoprint
 * @return TRUE on success, FALSE on failure
 */
static gboolean disable_tklock(gboolean silent)
{
	gboolean status = FALSE;

	/* On startup of MCE, we always disable
	 * the touchscreen/keypad lock and single event eater
	 */
	if (is_tklock_enabled() == FALSE) {
		mce_log(LL_DEBUG,
			"Touchscreen/keypad lock disabled "
			"when already disabled");
		silent = TRUE;
	}

	if (tklock_ui_state == MCE_TKLOCK_UI_EVENT_EATER)
		goto EXIT;

	if (close_tklock_ui(silent) == FALSE)
	{
		cancel_tklock_disable_timeout();
		if (tklock_disable_timeout_cb_id)
		{
			goto EXIT;
		}
		tklock_disable_timeout_cb_id =
		    g_timeout_add(500,tklock_disable_timeout_cb,
				  GINT_TO_POINTER(silent));
		goto EXIT;
	}

	/* Disable timeouts, just to be sure */
	cancel_tklock_disable_timeout();
	cancel_tklock_visual_forced_blank_timeout();
	cancel_tklock_visual_blank_timeout();
	cancel_tklock_unlock_timeout();
	cancel_tklock_dim_timeout();

	mce_rem_submode_int32(MCE_VISUAL_TKLOCK_SUBMODE);
	mce_rem_submode_int32(MCE_TKLOCK_SUBMODE);
	(void)mce_send_tklock_mode(NULL);
	(void)ts_event_control(TRUE);
	status = TRUE;

EXIT:
	return status;
}

/**
 * Enable the touchscreen/keypad single event eater
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean enable_eveater(void)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	alarm_ui_state_t alarm_ui_state =
				datapipe_get_gint(alarm_ui_state_pipe);
	gboolean status = TRUE;

	/* If we're in acting dead and no alarm is visible,
	 * don't activate the event eater
	 */
	if ((system_state == MCE_STATE_ACTDEAD) &&
	    ((alarm_ui_state != MCE_ALARM_UI_VISIBLE_INT32) ||
	     (alarm_ui_state != MCE_ALARM_UI_RINGING_INT32)))
		goto EXIT;

	/* If we're already showing a tklock UI, exit */
	if (tklock_ui_state != MCE_TKLOCK_UI_NONE) {
		mce_log(LL_DEBUG, "tklock.c: %s: tklock allready showing", __func__);
		goto EXIT;
	}

	if ((status = open_tklock_ui(TKLOCK_ONEINPUT, TRUE)) == TRUE) {
		mce_log(LL_DEBUG, "tklock.c: %s: eveater enabled", __func__);
		mce_add_submode_int32(MCE_EVEATER_SUBMODE);
	} else {
		mce_log(LL_WARN, "tklock.c: %s: tklock failed to open", __func__);
		disable_eveater(TRUE);
	}

EXIT:
	return status;
}

/**
 * Disable the touchscreen/keypad single event eater
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean disable_eveater(gboolean silent)
{
	gboolean status = FALSE;

	/* If the event eater isn't enabled, ignore the request */
	if (is_eveater_enabled() == FALSE) {
		status = TRUE;
		goto EXIT;
	}

	/* Only disable the UI if the active UI is the event eater */
	if (tklock_ui_state == MCE_TKLOCK_UI_EVENT_EATER) {
		if (close_tklock_ui(silent) == FALSE)
			goto EXIT;
	}

	mce_log(LL_WARN, "tklock.c: %s: eveater disabled", __func__);
	mce_rem_submode_int32(MCE_EVEATER_SUBMODE);
	status = TRUE;

EXIT:
	return status;
}

/**
 * Timeout callback for tklock unlock
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean tklock_unlock_timeout_cb(gpointer data)
{
	(void)data;

	tklock_unlock_timeout_cb_id = 0;

	set_tklock_state(LOCK_OFF);

	return FALSE;
}

/**
 * Cancel timeout for delayed unlocking of touchscreen/keypad lock
 */
static void cancel_tklock_unlock_timeout(void)
{
	/* Remove the timer source for delayed tklock unlocking */
	if (tklock_unlock_timeout_cb_id != 0) {
		g_source_remove(tklock_unlock_timeout_cb_id);
		tklock_unlock_timeout_cb_id = 0;
	}
}

/**
 * Setup a timeout for delayed unlocking of touchscreen/keypad lock
 */
static void setup_tklock_unlock_timeout(void)
{
	cancel_tklock_unlock_timeout();

	/* Setup new timeout */
	tklock_unlock_timeout_cb_id =
		g_timeout_add(MCE_TKLOCK_UNLOCK_DELAY,
			      tklock_unlock_timeout_cb, NULL);
}

static gboolean proximity_unlock_timeout_cb(gpointer data)
{
	(void)data;

	process_proximity_state();
	proximity_unlock_timeout_cb_id = 0;

	return FALSE;
}

static void cancel_proximity_unlock_timeout(void)
{
	if (proximity_unlock_timeout_cb_id != 0)
	{
		g_source_remove(proximity_unlock_timeout_cb_id);
		proximity_unlock_timeout_cb_id = 0;
	}
}

static void setup_proximity_unlock_timeout(void)
{
	cancel_proximity_unlock_timeout();

	proximity_unlock_timeout_cb_id =
		g_timeout_add(DEFAULT_PROXIMITY_UNLOCK_DELAY,
			      proximity_unlock_timeout_cb, NULL);
}

/**
 * Enable the touchscreen/keypad autolock
 *
 * Will enable touchscreen/keypad lock if tk_autolock_enabled is TRUE,
 * and enable the touchscreen/keypad single event eater if FALSE
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean enable_autokeylock(void)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	cover_state_t slide_state = datapipe_get_gint(keyboard_slide_pipe);
	alarm_ui_state_t alarm_ui_state =
				datapipe_get_gint(alarm_ui_state_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);
	submode_t submode = datapipe_get_gint(submode_pipe);
	gboolean status = TRUE;

	/* Don't enable automatic tklock during bootup */
	if ((submode & MCE_BOOTUP_SUBMODE) != 0)
		goto EXIT;

	if ((system_state == MCE_STATE_USER) &&
	    ((slide_state != COVER_OPEN) ||
	     (autolock_with_open_slide == TRUE)) &&
	    (tk_autolock_enabled == TRUE) &&
	    (alarm_ui_state != MCE_ALARM_UI_VISIBLE_INT32) &&
	    (alarm_ui_state != MCE_ALARM_UI_RINGING_INT32) &&
	    ((call_state == CALL_STATE_INVALID) ||
	     (call_state == CALL_STATE_NONE))) {
		if ((status = enable_tklock(TRUE)) == TRUE)
		{
			(void)ts_disable_policy();
		} else {
			disable_eveater(TRUE);
			disable_tklock(TRUE);
		}
	} else {
		if (((alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
		     (alarm_ui_state == MCE_ALARM_UI_RINGING_INT32)) &&
		    ((tklock_ui_state == MCE_TKLOCK_UI_NONE) ||
		     (tklock_ui_state == MCE_TKLOCK_UI_EVENT_EATER)))
			disable_autorelock();

		status = enable_eveater();
	}

EXIT:
	return status;
}

/**
 * State machine for lock change requests
 *
 * @param lock_state The requested touchscreen/keypad lock state
 */
static void set_tklock_state(lock_state_t lock_state)
{
	mce_log(LL_DEBUG, "tklock.c: %s lock state: %i", __func__, lock_state);
	switch (lock_state) {
	case LOCK_OFF:
		(void)disable_tklock(FALSE);
		(void)disable_eveater(TRUE);
		disable_autorelock();
		break;

	case LOCK_OFF_SILENT:
		(void)disable_tklock(TRUE);
		(void)disable_eveater(TRUE);
		disable_autorelock();
		break;

	case LOCK_OFF_DELAYED:
		setup_tklock_unlock_timeout();
		break;

	case LOCK_ON:
		(void)enable_tklock_policy(FALSE);
		break;

	case LOCK_ON_DIMMED:
		if (enable_tklock(FALSE) == TRUE)
			setup_tklock_dim_timeout(0, FALSE);
		break;

	case LOCK_ON_SILENT:
		(void)enable_tklock(TRUE);
		break;

	case LOCK_ON_SILENT_DIMMED:
		if (enable_tklock(TRUE) == TRUE)
			setup_tklock_dim_timeout(0, FALSE);
		break;

	case LOCK_TOGGLE:
		/* Touchscreen/keypad lock */
		if (is_tklock_enabled() == FALSE)
		{
			if (!is_eveater_enabled())
			{
				enable_tklock_policy(FALSE);
			}
			else
			{
				disable_eveater(TRUE);
				synthesise_activity();
			}
		}
		else
		{
			if ((is_tklock_enabled() == TRUE) &&
				(tklock_ui_state == MCE_TKLOCK_UI_NONE)) {
				(void)enable_tklock_policy(FALSE);
			} else {
				(void)disable_tklock(FALSE);
				disable_autorelock();
				synthesise_activity();
			}
		}

		break;

	default:
		break;
	}
}

/**
 * Visual touchscreen/keypad lock logic
 */
static void trigger_visual_tklock(void)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);

	mce_log(LL_DEBUG, "tklock.c: %s: %i, %i, %i", __func__, display_state == MCE_DISPLAY_OFF, is_tklock_enabled(), is_autorelock_enabled());

	if ((is_tklock_enabled() == FALSE) ||
	    (is_autorelock_enabled() == FALSE))
		goto EXIT;

	if (open_tklock_ui(TKLOCK_ENABLE_VISUAL, FALSE) == TRUE) {
		mce_add_submode_int32(MCE_VISUAL_TKLOCK_SUBMODE);
	}

	/* If visual tklock is enabled, reset the timeout */
	if (is_visual_tklock_enabled()) {
		setup_tklock_visual_blank_timeout();
		synthesise_activity();
	}

EXIT:
	return;
}

/**
 * D-Bus callback for the get tklock mode method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean tklock_mode_get_req_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "%s: Received tklock mode get request", MODULE_NAME);

	/* Try to send a reply that contains the current tklock mode */
	if (mce_send_tklock_mode(msg) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

/**
 * D-Bus callback for the tklock mode change method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean tklock_mode_change_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;
	gchar *mode = NULL;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received tklock mode change request");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &mode,
				  DBUS_TYPE_INVALID) == FALSE) {
		// XXX: should we return an error instead?
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_TKLOCK_MODE_CHANGE_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	/* Try to change to the requested tklock mode
	 * XXX: right now we silently ignore invalid modes;
	 * should we return an error?
	 */
	if (strcmp(MCE_TK_LOCKED, mode) == 0) {
		set_tklock_state(LOCK_ON);
	} else if (strcmp(MCE_TK_LOCKED_DIM, mode) == 0) {
		set_tklock_state(LOCK_ON_DIMMED);
	} else if (strcmp(MCE_TK_SILENT_LOCKED, mode) == 0) {
		set_tklock_state(LOCK_ON_SILENT);
	} else if (strcmp(MCE_TK_SILENT_LOCKED_DIM, mode) == 0) {
		set_tklock_state(LOCK_ON_SILENT_DIMMED);
	} else if (strcmp(MCE_TK_UNLOCKED, mode) == 0) {
		set_tklock_state(LOCK_OFF);
		synthesise_activity();
	} else if (strcmp(MCE_TK_SILENT_UNLOCKED, mode) == 0) {
		set_tklock_state(LOCK_OFF_SILENT);
		synthesise_activity();
	} else {
		mce_log(LL_ERR,
			"Received an invalid tklock mode; ignoring");
	}
	update_saved_submode();

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

EXIT:
	return status;
}

/**
 * D-Bus callback from SystemUI touchscreen/keypad lock
 *
 * @todo the calls to disable_tklock/open_tklock_ui need error handling
 *
 * @param msg D-Bus message with the lock status
 */
static gboolean systemui_tklock_dbus_cb(DBusMessage *const msg)
{
	dbus_int32_t result = INT_MAX;
	gboolean status = FALSE;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received tklock callback");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_INT32, &result,
				  DBUS_TYPE_INVALID) == FALSE) {
		// XXX: should we return an error instead?
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_TKLOCK_CB_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	mce_log(LL_DEBUG, "tklock callback value: %d", result);

	switch (result) {
	case TKLOCK_UNLOCK:
		/* Unlock the tklock */
		if ((tklock_ui_state == MCE_TKLOCK_UI_NORMAL) ||
		    (tklock_ui_state == MCE_TKLOCK_UI_SLIDER)) {
			(void)execute_datapipe(&tk_lock_pipe,
					       GINT_TO_POINTER(LOCK_OFF),
					       USE_INDATA, CACHE_INDATA);
		} else {
			disable_eveater(FALSE);
		}

		break;

	case TKLOCK_CLOSED:
	default:
		break;
	}

	status = TRUE;

EXIT:
	return status;
}

/**
 * rtconf callback for touchscreen/keypad lock related settings
 *
 * @param gcc Unused
 * @param id Connection ID from rtconf_client_notify_add()
 * @param entry The modified GConf entry
 * @param data Unused
 */
static void tklock_rtconf_cb(gchar *key, guint cb_id, void *user_data)
{
	(void)key;
	(void)user_data;

	if (cb_id == tk_autolock_enabled_cb_id) {
		mce_rtconf_get_bool(MCE_GCONF_TK_AUTOLOCK_ENABLED_PATH, &tk_autolock_enabled);
	} else {
		mce_log(LL_WARN, "%s: Spurious GConf value received; confused!", MODULE_NAME);
	}
}

/**
 * Process the proximity state
 */
static void process_proximity_state(void)
{
	cover_state_t slide_state = datapipe_get_gint(keyboard_slide_pipe);
	cover_state_t proximity_sensor_state =
				datapipe_get_gint(proximity_sensor_pipe);
	audio_route_t audio_route = datapipe_get_gint(audio_route_pipe);
	alarm_ui_state_t alarm_ui_state =
				datapipe_get_gint(alarm_ui_state_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);

	if (ignore_proximity_events && ((autorelock_triggers & AUTORELOCK_ON_PROXIMITY) == 0))
		goto EXIT;

	if(tklock_proximity && 
	   (autorelock_triggers & AUTORELOCK_ON_PROXIMITY))
	{
		if(!proximity_unlock_timeout_cb_id && (COVER_OPEN == proximity_sensor_state))
		{
			setup_proximity_unlock_timeout();
			goto EXIT;
		}
		else if (proximity_unlock_timeout_cb_id && (COVER_CLOSED == proximity_sensor_state))
		{
			cancel_proximity_unlock_timeout();
			goto EXIT;
		}
	}
	
	/* If there's an incoming call or an alarm is visible,
	 * the proximity sensor reports open, and the tklock
	 * or event eater is active, unblank and unlock the display
	 */
	if ((tklock_proximity &&
	     (inhibit_proximity_relock != MCE_ALLOW_PROXIMITY_RELOCK)) ||
	    (((call_state == CALL_STATE_RINGING) ||
	     ((alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
	      (alarm_ui_state == MCE_ALARM_UI_RINGING_INT32))) &&
	     (proximity_sensor_state == COVER_OPEN))) {
		(void)ts_enable_policy();

		if (is_tklock_enabled() || is_eveater_enabled()) {
			/* Disable tklock/event eater */
			if (close_tklock_ui(TRUE) == FALSE)
			{
				disable_eveater(TRUE);
				disable_tklock(TRUE);
				goto EXIT;
			}
			mce_log(LL_DEBUG, "%s: process_proximity_state: removing lock submodes", MODULE_NAME);
			mce_rem_submode_int32(MCE_EVEATER_SUBMODE);
			mce_rem_submode_int32(MCE_TKLOCK_SUBMODE);
			/* Disable timeouts, just to be sure */
			cancel_tklock_visual_forced_blank_timeout();
			cancel_tklock_visual_blank_timeout();
			cancel_tklock_unlock_timeout();
			cancel_tklock_dim_timeout();
		}

		/* Unblank screen */
		(void)execute_datapipe(&display_state_pipe,
				       GINT_TO_POINTER(MCE_DISPLAY_ON),
				       USE_INDATA, CACHE_INDATA);
		mce_send_tklock_mode(NULL);
		if ((alarm_ui_state != MCE_ALARM_UI_VISIBLE_INT32) ||
		    (alarm_ui_state != MCE_ALARM_UI_RINGING_INT32))
			autorelock_triggers = AUTORELOCK_ON_PROXIMITY;
		else
			autorelock_triggers = ~(~autorelock_triggers |
				                AUTORELOCK_ON_PROXIMITY);

		tklock_proximity = FALSE;
		goto EXIT;
	}

	/* If there's no incoming or active call, or the audio isn't
	 * routed to the handset or headset, or if the slide is open, exit
	 */
	if (((((call_state != CALL_STATE_RINGING) ||
	       (proximity_lock_when_ringing != TRUE)) &&
	      (call_state != CALL_STATE_ACTIVE)) ||
             ((audio_route != AUDIO_ROUTE_HANDSET) &&
	      (audio_route != AUDIO_ROUTE_HEADSET) &&
	      ((audio_route != AUDIO_ROUTE_SPEAKER) ||
	       (call_state != CALL_STATE_RINGING)))) ||
	    (slide_state == COVER_OPEN)) {
		goto EXIT;
	}

	switch (proximity_sensor_state) {
	case COVER_OPEN:
		if (autorelock_triggers == AUTORELOCK_ON_PROXIMITY) {
			if ((is_tklock_enabled() == TRUE) &&
			    (is_autorelock_enabled() == TRUE))
				/* Disable tklock */
				set_tklock_state(LOCK_OFF);

			/* Unblank screen */
			(void)execute_datapipe(&display_state_pipe,
					       GINT_TO_POINTER(MCE_DISPLAY_ON),
					       USE_INDATA, CACHE_INDATA);

			tklock_proximity = FALSE;
		}

		break;

	case COVER_CLOSED:
		if ((inhibit_proximity_relock == MCE_ALLOW_PROXIMITY_RELOCK) &&
		    (((is_tklock_enabled() == FALSE) &&
		      (is_autorelock_enabled() == FALSE)) ||
		     ((is_autorelock_enabled() == TRUE) &&
		      (autorelock_triggers == AUTORELOCK_ON_PROXIMITY)))) {
			(void)enable_tklock_policy(TRUE);

			if ((alarm_ui_state != MCE_ALARM_UI_VISIBLE_INT32) &&
			    (alarm_ui_state != MCE_ALARM_UI_RINGING_INT32))
				autorelock_triggers = AUTORELOCK_ON_PROXIMITY;

			tklock_proximity = TRUE;
		}

		break;

	default:
		break;
	}

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

	if (device_inactive == FALSE) {
		if ((is_tklock_enabled() == TRUE) &&
		    (tklock_visual_blank_timeout_cb_id != 0)) {
			setup_tklock_visual_blank_timeout();
		}
	}
}

/**
 * Datapipe trigger for the keyboard slide
 *
 * @param data COVER_OPEN if the keyboard slide is open,
 *             COVER_CLOSED if the keyboard slide is closed
 */
static void keyboard_slide_trigger(gconstpointer const data)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	cover_state_t kbd_slide_state = GPOINTER_TO_INT(data);

	if ((system_state != MCE_STATE_USER))
		goto EXIT;

	switch (kbd_slide_state) {
	case COVER_OPEN:
		if (is_tklock_enabled() == TRUE) {
			/* Only the trigger that caused the unlock
			 * should trigger autorelock
			 */
			if ((autorelock_triggers & AUTORELOCK_KBD_SLIDE) != 0)
				autorelock_triggers = AUTORELOCK_KBD_SLIDE;

			/* Disable tklock */
			(void)disable_tklock(FALSE);
			synthesise_activity();
		}
		else
		{
          if (is_eveater_enabled())
          {
            disable_eveater(TRUE);
            synthesise_activity();
          }
		}
		if (call_submode != MCE_INVALID_SUBMODE)
		{
			saved_submode &= ~MCE_TKLOCK_SUBMODE;
		}
		break;

	case COVER_CLOSED:
		if (((tk_autolock_enabled == TRUE) &&
		     (display_state == MCE_DISPLAY_OFF)) ||
		    ((is_autorelock_enabled() == TRUE) &&
		     ((autorelock_triggers & AUTORELOCK_KBD_SLIDE) != 0)))
			/* This will also reset the autorelock policy */
			(void)enable_tklock_policy(FALSE);
		if ( call_submode & MCE_TKLOCK_SUBMODE )
			saved_submode |= MCE_TKLOCK_SUBMODE;
		break;

	default:
		break;
	}

	process_proximity_state();

EXIT:
	return;
}

/**
 * Datapipe trigger for the [lock] flicker key
 *
 * @param data 1 if the key was pressed, 0 if the key was released
 */
static void lockkey_trigger(gconstpointer const data)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);

	/* Only react on the [lock] flicker key in USER state */
	if ((GPOINTER_TO_INT(data) == 1) && (system_state == MCE_STATE_USER)) {
		/* Using the flicker key during a call
		 * disables proximity based locking/unlocking
		 */
		if (call_state == CALL_STATE_ACTIVE) {
			autorelock_triggers = ~(~autorelock_triggers |
			                        AUTORELOCK_ON_PROXIMITY);
			inhibit_proximity_relock = MCE_INHIBIT_PROXIMITY_RELOCK;
		}

		/* Execute lock action */
		(void)execute_datapipe(&tk_lock_pipe,
				       GINT_TO_POINTER(LOCK_TOGGLE),
				       USE_INDATA, CACHE_INDATA);
	}
}

/**
 * Datapipe trigger for keypresses
 *
 * @param data Keypress state
 */
static void keypress_trigger(gconstpointer const data)
{
	submode_t submode = datapipe_get_gint(submode_pipe);
	struct input_event const *const *evp;
	struct input_event const *ev;

	/* Don't dereference until we know it's safe */
	if (data == NULL)
		goto EXIT;

	evp = data;
	ev = *evp;

	disable_autorelock_policy();

	if ((((submode & MCE_BOOTUP_SUBMODE) == 0) &&
	    (tklock_proximity == FALSE) &&
	    ((ev != NULL) &&
	     (ev->code == power_keycode) && (ev->value == 1))) ||
	     is_eveater_enabled()) {
		if (is_eveater_enabled()) {
			mce_log(LL_DEBUG, "disable eveater (TRUE) - power key");
			disable_eveater(TRUE);
			synthesise_activity();
		}
		else
			trigger_visual_tklock();
	}

EXIT:
	return;
}

/**
 * Datapipe trigger for touchscreen events
 *
 * @param data Unused
 */
static void touchscreen_trigger(gconstpointer const data)
{
	(void)data;

	disable_autorelock_policy();
}

/**
 * Handle system state change
 *
 * @param data The system state stored in a pointer
 */
static void system_state_trigger(gconstpointer data)
{
	system_state_t system_state = GPOINTER_TO_INT(data);

	switch (system_state) {
	case MCE_STATE_SHUTDOWN:
	case MCE_STATE_REBOOT:
	case MCE_STATE_ACTDEAD:
		(void)ts_disable_policy();
		break;

	case MCE_STATE_USER:
	default:
		(void)ts_enable_policy();
		break;
	}
}

/**
 * Handle display state change
 *
 * @param data The display state stored in a pointer
 */
static void display_state_trigger(gconstpointer data)
{
	alarm_ui_state_t alarm_ui_state =
				datapipe_get_gint(alarm_ui_state_pipe);
	static display_state_t old_display_state = MCE_DISPLAY_UNDEF;
	display_state_t display_state = GPOINTER_TO_INT(data);

	switch (display_state) {
	case MCE_DISPLAY_OFF:
		if (tklock_ui_state == MCE_TKLOCK_UI_NORMAL)
			break;
		if (((alarm_ui_state != MCE_ALARM_UI_RINGING_INT32) &&
		     (alarm_ui_state != MCE_ALARM_UI_RINGING_INT32)) &&
		    (is_tklock_enabled() == TRUE)) {
			if (enable_tklock(TRUE) == TRUE) {
				(void)ts_disable_policy();
			} else {
				disable_eveater(TRUE);
				disable_tklock(TRUE);
			}
		} else {
			(void)enable_autokeylock();
		}

		break;

	case MCE_DISPLAY_DIM:
		enable_eveater();

		/* If the display transitions from OFF or UNDEF,
		 * to DIM or ON, do policy based enable
		 */
		if ((old_display_state == MCE_DISPLAY_UNDEF) ||
		    (old_display_state == MCE_DISPLAY_OFF)) {
			(void)ts_enable_policy();
		}

		break;

	case MCE_DISPLAY_ON:
	default:
		/* If the display transitions from OFF or UNDEF,
		 * to DIM or ON, do policy based enable
		 */
		if ((old_display_state == MCE_DISPLAY_UNDEF) ||
		    (old_display_state == MCE_DISPLAY_OFF)) {
			(void)ts_enable_policy();
		}
		(void)disable_eveater(FALSE);
		break;
	}

	old_display_state = display_state;
}

/**
 * Handle alarm UI state change
 *
 * @param data The alarm state stored in a pointer
 */
static void alarm_ui_state_trigger(gconstpointer data)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	cover_state_t proximity_sensor_state =
				datapipe_get_gint(proximity_sensor_pipe);
	alarm_ui_state_t alarm_ui_state = GPOINTER_TO_INT(data);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);

	switch (alarm_ui_state) {
	case MCE_ALARM_UI_VISIBLE_INT32:
		tklock_proximity = FALSE;

		if (is_tklock_enabled() == TRUE) {
			/* Event eater is used when tklock is disabled,
			 * so make sure to disable it if we enable the tklock
			 */
			disable_eveater(TRUE);

			if (open_tklock_ui(TKLOCK_ENABLE, TRUE) == FALSE) {
				disable_tklock(TRUE);
				goto EXIT;
			}

			enable_autorelock();
			setup_dim_blank_timeout_policy(TRUE);
		} else if (is_eveater_enabled() == TRUE) {
			(void)ts_enable_policy();

			if (open_tklock_ui(TKLOCK_ONEINPUT, TRUE) == FALSE) {
				disable_eveater(TRUE);
				goto EXIT;
			}

			setup_dim_blank_timeout_policy(FALSE);
		}

		break;

	case MCE_ALARM_UI_RINGING_INT32:
		/* If the proximity state is "open",
		 * disable tklock/event eater UI and proximity sensor
		 */
		ignore_proximity_events = 0;
		get_submode();
		if (proximity_sensor_state == COVER_OPEN) {
			(void)ts_enable_policy();

			autorelock_triggers = ~(~autorelock_triggers |
				                AUTORELOCK_ON_PROXIMITY);
			tklock_proximity = FALSE;

			/* Disable tklock/event eater */
			if (close_tklock_ui(TRUE) == FALSE)
			{
				disable_eveater(TRUE);
				disable_tklock(TRUE);
				goto EXIT;
			}

			/* Disable timeouts, just to be sure */
			cancel_tklock_visual_forced_blank_timeout();
			cancel_tklock_visual_blank_timeout();
			cancel_tklock_unlock_timeout();
			cancel_tklock_dim_timeout();

			/* Unblank screen */
			(void)execute_datapipe(&display_state_pipe,
					       GINT_TO_POINTER(MCE_DISPLAY_ON),
					       USE_INDATA, CACHE_INDATA);
		} else {
			autorelock_triggers = (autorelock_triggers |
					       AUTORELOCK_ON_PROXIMITY);
			tklock_proximity = is_tklock_enabled();
		}

		break;

	case MCE_ALARM_UI_OFF_INT32:
		(void)ts_disable_policy();
		tklock_proximity = FALSE;
		ignore_proximity_events = (call_state == CALL_STATE_INVALID) || (call_state == CALL_STATE_NONE);
		mce_log(LL_DEBUG, "MCE_ALARM_UI_OFF_INT32(): ignore_proximity_events = %d",ignore_proximity_events);
		/* In acting dead the event eater is only
		 * used when showing the alarm UI
		 */
		if (system_state != MCE_STATE_USER) {
			disable_eveater(TRUE);
			return;
		} else if ((call_state == CALL_STATE_INVALID) || (call_state == CALL_STATE_NONE)) {
			if (!(saved_submode & MCE_TKLOCK_SUBMODE))
			{
				if (!(saved_submode & MCE_EVEATER_SUBMODE))
				{
					goto EXIT;
				}
				mce_add_submode_int32(MCE_EVEATER_SUBMODE);
				if (open_tklock_ui(TKLOCK_ONEINPUT, TRUE) == FALSE) {
					disable_eveater(TRUE);
					goto EXIT;
				}
				setup_dim_blank_timeout_policy(FALSE);
				goto EXIT;
			}
			mce_add_submode_int32(MCE_TKLOCK_SUBMODE);
			disable_eveater(TRUE);
			if (open_tklock_ui(TKLOCK_ENABLE, TRUE) == FALSE) {
				disable_tklock(TRUE);
				goto EXIT;
			}
			enable_autorelock();
			setup_dim_blank_timeout_policy(TRUE);
			goto EXIT;
		}
		if (!(saved_submode & MCE_TKLOCK_SUBMODE))
		{
			if (!(saved_submode & MCE_EVEATER_SUBMODE))
			{
				goto EXIT;
			}
			mce_add_submode_int32(MCE_EVEATER_SUBMODE);
			if (open_tklock_ui(TKLOCK_ONEINPUT, TRUE) == FALSE) {
				disable_eveater(TRUE);
				goto EXIT;
			}
			setup_dim_blank_timeout_policy(FALSE);
			goto EXIT;
		}
		disable_eveater(TRUE);
		set_tklock_state(LOCK_OFF);
		break;

	default:
		break;
	}

EXIT:
	return;
}

/**
 * Handle lid cover sensor state change
 *
 * @param data The lid cover state stored in a pointer
 */
static void lid_cover_trigger(gconstpointer data)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	cover_state_t lid_cover_state = GPOINTER_TO_INT(data);

	switch (lid_cover_state) {
	case COVER_OPEN:
		if (system_state == MCE_STATE_USER) {
			setup_tklock_unlock_timeout();
			synthesise_activity();
		}

		break;

	case COVER_CLOSED:
		if (system_state == MCE_STATE_USER) {
			if (enable_tklock_policy(FALSE) == TRUE) {
				/* Blank screen */
				(void)execute_datapipe(&display_state_pipe,
						       GINT_TO_POINTER(MCE_DISPLAY_OFF),
						       USE_INDATA, CACHE_INDATA);
			}
		}

		break;

	default:
		break;
	}
}

/**
 * Handle proximity sensor state change
 *
 * @param data Unused
 */
static void proximity_sensor_trigger(gconstpointer data)
{
	(void)data;

	process_proximity_state();
}

/**
 * Handle lens cover state change
 *
 * @param data The lens cover state stored in a pointer
 */
static void lens_cover_trigger(gconstpointer data)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	cover_state_t lens_cover_state = GPOINTER_TO_INT(data);

	if ((system_state != MCE_STATE_USER))
		goto EXIT;

	if (lens_cover_unlock == FALSE)
		goto EXIT;

	switch (lens_cover_state) {
	case COVER_OPEN:
		if (is_tklock_enabled() == TRUE) {
			/* Only the trigger that caused the unlock
			 * should trigger autorelock
			 */
			if ((autorelock_triggers & AUTORELOCK_LENS_COVER) != 0)
				autorelock_triggers = AUTORELOCK_LENS_COVER;

			/* Disable tklock */
			(void)disable_tklock(FALSE);
			synthesise_activity();
		}
		else
		{
			if (is_eveater_enabled())
			{
				disable_eveater(TRUE);
				synthesise_activity();
			}
		}

		break;

	case COVER_CLOSED:
		if ((is_autorelock_enabled() == TRUE) &&
		    ((autorelock_triggers & AUTORELOCK_LENS_COVER) != 0))
			/* This will also reset the autorelock policy */
			(void)enable_tklock_policy(FALSE);

		break;

	default:
		break;
	}

EXIT:
	return;
}

/**
 * Handle touchscreen/keypad lock state
 *
 * @param data The touchscreen/keypad lock state stored in a pointer
 */
static void tk_lock_trigger(gconstpointer data)
{
	lock_state_t tk_lock_state = GPOINTER_TO_INT(data);

	set_tklock_state(tk_lock_state);
	update_saved_submode();
}

/**
 * Handle submode change
 *
 * @param data The submode stored in a pointer
 */
static void submode_trigger(gconstpointer data)
{
	static submode_t old_submode = MCE_NORMAL_SUBMODE;
	submode_t submode = GPOINTER_TO_INT(data);

	/* If we transition from !softoff to softoff,
	 * disable touchscreen and keypad events,
	 * otherwise enable them
	 */
	if ((submode & MCE_SOFTOFF_SUBMODE) != 0 && (old_submode & MCE_SOFTOFF_SUBMODE) == 0)
		(void)ts_event_control(FALSE);
	else if ((submode & MCE_SOFTOFF_SUBMODE) == 0 && (old_submode & MCE_SOFTOFF_SUBMODE) != 0)
		(void)ts_event_control(TRUE);

	old_submode = submode;
}

/**
 * Handle call state change
 *
 * @param data The call state stored in a pointer
 */
static void call_state_trigger(gconstpointer data)
{
	static call_state_t old_call_state = CALL_STATE_INVALID;
	call_state_t call_state = GPOINTER_TO_INT(data);

	switch (call_state) {
	case CALL_STATE_RINGING:
		ignore_proximity_events = FALSE;
		if (proximity_lock_when_ringing == TRUE)
			inhibit_proximity_relock = MCE_ALLOW_PROXIMITY_RELOCK;

		/* Incoming call, update the submode,
		 * unless there's already a call ongoing
		 */
		if (old_call_state == CALL_STATE_ACTIVE) {
			break;
		}
		get_submode();
		break;

	case CALL_STATE_ACTIVE:
		ignore_proximity_events = FALSE;
		if (old_call_state != CALL_STATE_ACTIVE)
			inhibit_proximity_relock = MCE_ALLOW_PROXIMITY_RELOCK;

		/* If we're answering a call, don't alter anything */
		if (old_call_state == CALL_STATE_RINGING)
			break;

		/* Call initiated on our end, update the submode,
		 * unless we're just upgrading a normal call to
		 * an emergency call
		 */
		if (old_call_state != CALL_STATE_ACTIVE)
			get_submode();

		break;

	case CALL_STATE_NONE:
	default:
		/* Submode not set, update submode */
		if (saved_submode == MCE_INVALID_SUBMODE)
			get_submode();

		ignore_proximity_events = call_state == CALL_STATE_NONE;
		mce_log(LL_DEBUG, "CALL_STATE_NONE(): ignore_proximity_events = %d",ignore_proximity_events);

		if (autorelock_triggers == AUTORELOCK_ON_PROXIMITY)
			autorelock_triggers = AUTORELOCK_NO_TRIGGERS;

		tklock_proximity = FALSE;

		if ((saved_submode & MCE_TKLOCK_SUBMODE) != 0) {
			/* Enable the tklock again, show the banner */
			enable_tklock_policy(FALSE);
		} else {
			if (is_tklock_enabled())
				set_tklock_state(LOCK_OFF_SILENT);

			/* Unblank screen */
			(void)execute_datapipe(&display_state_pipe,
					       GINT_TO_POINTER(MCE_DISPLAY_ON),
					       USE_INDATA, CACHE_INDATA);
		}

		break;
	}

	process_proximity_state();
	old_call_state = call_state;
}

/**
 * Handle audio routing changes
 *
 * @param data The audio route stored in a pointer
 */
static void audio_route_trigger(gconstpointer data)
{
	audio_route_t audio_route = GPOINTER_TO_INT(data);

	switch (audio_route) {
	case AUDIO_ROUTE_HANDSET:
	case AUDIO_ROUTE_HEADSET:
		if (inhibit_proximity_relock ==
		    MCE_TEMP_INHIBIT_PROXIMITY_RELOCK)
			inhibit_proximity_relock = MCE_ALLOW_PROXIMITY_RELOCK;
		break;

	case AUDIO_ROUTE_SPEAKER:
	case AUDIO_ROUTE_UNDEF:
	default:
		if (inhibit_proximity_relock == MCE_ALLOW_PROXIMITY_RELOCK)
			inhibit_proximity_relock =
				MCE_TEMP_INHIBIT_PROXIMITY_RELOCK;
		break;
	}

	process_proximity_state();
}

static gboolean tklock_disable_timeout_cb(gpointer data)
{
	gboolean silent = GPOINTER_TO_INT(data);
	if (close_tklock_ui(silent) == TRUE)
	{
		tklock_disable_timeout_cb_id = 0;
		cancel_tklock_visual_forced_blank_timeout();
		cancel_tklock_visual_blank_timeout();
		cancel_tklock_unlock_timeout();
		cancel_tklock_dim_timeout();
		mce_rem_submode_int32(MCE_VISUAL_TKLOCK_SUBMODE);
		mce_rem_submode_int32(MCE_TKLOCK_SUBMODE);
		(void)mce_send_tklock_mode(NULL);
		(void)ts_event_control(TRUE);
		synthesise_activity();
		unlock_attempts = 0;
		return FALSE;
	}
	else
	{
		if (unlock_attempts > 4)
		{
			unlock_attempts = 0;
			mce_log(LL_DEBUG, "Error during unlocking device. Drop to unlock device...");
			return FALSE;
		}
		else
		{
			mce_log(LL_DEBUG, "Error during unlocking device. Trying to unlock device one more time");
			++unlock_attempts;
			return TRUE;
		}
	}
}

/**
 * Init function for the touchscreen/keypad lock component
 *
 * @todo the call to disable_tklock needs error handling
 * 
 */
G_MODULE_EXPORT const char *g_module_check_init(GModule * module);
const char *g_module_check_init(GModule * module)
{
	(void)module;
	gboolean status = FALSE;

	/* Close the touchscreen/keypad lock and event eater UI,
	 * to make sure MCE doesn't end up in a confused state
	 * if restarted
	 */
	// FIXME: error handling?
	(void)disable_tklock(TRUE);
	(void)disable_eveater(TRUE);
	disable_autorelock();

	/* Append triggers/filters to datapipes */
	append_input_trigger_to_datapipe(&device_inactive_pipe,
					 device_inactive_trigger);
	append_input_trigger_to_datapipe(&keyboard_slide_pipe,
					 keyboard_slide_trigger);
	append_input_trigger_to_datapipe(&lockkey_pipe,
					 lockkey_trigger);
	append_input_trigger_to_datapipe(&keypress_pipe,
					 keypress_trigger);
	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);
	append_output_trigger_to_datapipe(&alarm_ui_state_pipe,
					  alarm_ui_state_trigger);
	append_output_trigger_to_datapipe(&lid_cover_pipe,
					  lid_cover_trigger);
	append_output_trigger_to_datapipe(&proximity_sensor_pipe,
					  proximity_sensor_trigger);
	append_output_trigger_to_datapipe(&lens_cover_pipe,
					  lens_cover_trigger);
	append_output_trigger_to_datapipe(&tk_lock_pipe,
					  tk_lock_trigger);
	append_output_trigger_to_datapipe(&submode_pipe,
					  submode_trigger);
	append_output_trigger_to_datapipe(&call_state_pipe,
					  call_state_trigger);
	append_output_trigger_to_datapipe(&audio_route_pipe,
					  audio_route_trigger);

	/* Touchscreen/keypad autolock */
	/* Since we've set a default, error handling is unnecessary */
	(void)mce_rtconf_get_bool(MCE_GCONF_TK_AUTOLOCK_ENABLED_PATH, &tk_autolock_enabled);

	/* Touchscreen/keypad autolock enabled/disabled */
	if (mce_rtconf_notifier_add(MCE_GCONF_LOCK_PATH,
				   MCE_GCONF_TK_AUTOLOCK_ENABLED_PATH,
				   tklock_rtconf_cb, NULL,
				   &tk_autolock_enabled_cb_id) == FALSE)
		goto EXIT;

	/* get_tklock_mode */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_TKLOCK_MODE_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 tklock_mode_get_req_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_TKLOCK_MODE_CHANGE_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 tklock_mode_change_req_dbus_cb) == NULL)
		goto EXIT;

	/* tklock_callback */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_TKLOCK_CB_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 systemui_tklock_dbus_cb) == NULL)
		goto EXIT;

	/* Get configuration options */
	blank_immediately = mce_conf_get_bool(MCE_CONF_TKLOCK_GROUP,
					      MCE_CONF_BLANK_IMMEDIATELY,
					      DEFAULT_BLANK_IMMEDIATELY,
					      NULL);

	dim_immediately = mce_conf_get_bool(MCE_CONF_TKLOCK_GROUP,
					    MCE_CONF_DIM_IMMEDIATELY,
					    DEFAULT_DIM_IMMEDIATELY,
					    NULL);

	dim_delay = mce_conf_get_int(MCE_CONF_TKLOCK_GROUP,
				     MCE_CONF_DIM_DELAY,
				     DEFAULT_DIM_DELAY,
				     NULL);

	disable_ts_immediately = mce_conf_get_bool(MCE_CONF_TKLOCK_GROUP,
						   MCE_CONF_TS_OFF_IMMEDIATELY,
						   DEFAULT_TS_OFF_IMMEDIATELY,
						   NULL);

	autolock_with_open_slide =
		mce_conf_get_bool(MCE_CONF_TKLOCK_GROUP,
				  MCE_CONF_AUTOLOCK_SLIDE_OPEN,
				  DEFAULT_AUTOLOCK_SLIDE_OPEN,
				  NULL);

	lens_cover_unlock = mce_conf_get_bool(MCE_CONF_TKLOCK_GROUP,
					      MCE_CONF_LENS_COVER_UNLOCK,
					      DEFAULT_LENS_COVER_UNLOCK,
					      NULL);

	status = TRUE;

EXIT:
	return status ? NULL : "Failure";
}

/**
 * Exit function for the touchscreen/keypad lock component
 *
 * @todo D-Bus unregistration
 */
G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;
	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&audio_route_pipe,
					    audio_route_trigger);
	remove_output_trigger_from_datapipe(&call_state_pipe,
					    call_state_trigger);
	remove_output_trigger_from_datapipe(&submode_pipe,
					    submode_trigger);
	remove_output_trigger_from_datapipe(&tk_lock_pipe,
					    tk_lock_trigger);
	remove_output_trigger_from_datapipe(&lens_cover_pipe,
					    lens_cover_trigger);
	remove_output_trigger_from_datapipe(&proximity_sensor_pipe,
					    proximity_sensor_trigger);
	remove_output_trigger_from_datapipe(&lid_cover_pipe,
					    lid_cover_trigger);
	remove_output_trigger_from_datapipe(&alarm_ui_state_pipe,
					    alarm_ui_state_trigger);
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);
	remove_input_trigger_from_datapipe(&keypress_pipe,
					   keypress_trigger);
	remove_input_trigger_from_datapipe(&lockkey_pipe,
					   lockkey_trigger);
	remove_input_trigger_from_datapipe(&keyboard_slide_pipe,
					   keyboard_slide_trigger);
	remove_input_trigger_from_datapipe(&device_inactive_pipe,
					   device_inactive_trigger);

	/* This trigger is conditional; attempt to remove it anyway */
	remove_input_trigger_from_datapipe(&touchscreen_pipe,
					   touchscreen_trigger);

	/* Remove all timeout sources */
	cancel_tklock_visual_forced_blank_timeout();
	cancel_tklock_visual_blank_timeout();
	cancel_tklock_unlock_timeout();
	cancel_tklock_dim_timeout();

}
