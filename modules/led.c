/**
 * @file led.c
 * LED module -- this handles the LED logic for MCE
 * <p>
 * Copyright © 2006-2010 Nokia Corporation and/or its subsidiary(-ies).
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
#include <glib/gstdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mce.h"
#include "led.h"
#include "mce-io.h"
#include "mce-lib.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "mce-gconf.h"
#include "datapipe.h"

/** Module name */
#define MODULE_NAME		"led"

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

/** The pattern queue */
static GQueue *pattern_stack = NULL;
/** The D-Bus controlled LED switch */
static gboolean led_enabled = FALSE;

/** Fields in the patterns */
typedef enum {
	PATTERN_PRIO_FIELD = 0,
	PATTERN_SCREEN_ON_FIELD = 1,
	PATTERN_TIMEOUT_FIELD = 2,
	PATTERN_ON_PERIOD_FIELD = 3,
	PATTERN_R_CHANNEL_FIELD = 3,
	PATTERN_MUXING_FIELD = 3,
	PATTERN_OFF_PERIOD_FIELD = 4,
	PATTERN_G_CHANNEL_FIELD = 4,
	PATTERN_E1_CHANNEL_FIELD = 4,
	PATTERN_BRIGHTNESS_FIELD = 5,
	PATTERN_B_CHANNEL_FIELD = 5,
	PATTERN_E2_CHANNEL_FIELD = 5,
	NUMBER_OF_PATTERN_FIELDS
} pattern_field;

/**
 * Size of each LED channel
 *
 * Multiply the channel size by 2 since we store hexadecimal ASCII
 */
#define CHANNEL_SIZE		32 * 2

/** Structure holding LED patterns */
typedef struct {
	gchar *name;			/**< Pattern name */
	gint priority;			/**< Pattern priority */
	gint policy;			/**< Show pattern when screen is on? */
	gint timeout;			/**< Timeout in seconds */
	gint on_period;			/**< Pattern on-period in ms  */
	gint off_period;		/**< Pattern off-period in ms  */
	gint brightness;		/**< Pattern brightness */
	gboolean active;		/**< Is the pattern active? */
	gboolean enabled;		/**< Is the pattern enabled? */
	guint engine1_mux;		/**< Muxing for engine 1 */
	guint engine2_mux;		/**< Muxing for engine 2 */
	/** Pattern for the R-channel/engine 1 */
	gchar channel1[CHANNEL_SIZE + 1];
	/** Pattern for the G-channel/engine 2 */
	gchar channel2[CHANNEL_SIZE + 1];
	/** Pattern for the B-channel */
	gchar channel3[CHANNEL_SIZE + 1];
	guint gconf_cb_id;		/**< Callback ID for GConf entry */
} pattern_struct;

/** Pointer to the top pattern */
static pattern_struct *active_pattern = NULL;
/** The active brightness */
static gint active_brightness = -1;

/** Currently driven leds */
static guint current_lysti_led_pattern = 0;

/** LED type */
typedef enum {
	LED_TYPE_UNSET = -1,
	LED_TYPE_NONE = 0,
	LED_TYPE_LYSTI = 3,
} led_type_t;

/**
 * The ID of the LED timer
 */
static guint led_pattern_timeout_cb_id = 0;

/**
 * The configuration group containing the LED pattern
 */
static const gchar *led_pattern_group = NULL;

static void cancel_pattern_timeout(void);

/**
 * Get the LED type
 *
 * @return The LED type
 */
static led_type_t get_led_type(void)
{
	static led_type_t led_type = LED_TYPE_UNSET;

	/* If we have the LED type already, return it */
	if (led_type != LED_TYPE_UNSET)
		goto EXIT;

	if (g_access(MCE_LYSTI_ENGINE1_MODE_PATH, W_OK) == 0) {
		led_type = LED_TYPE_LYSTI;

		led_pattern_group = MCE_CONF_LED_PATTERN_RX51_GROUP;
	} else {
		led_type = LED_TYPE_NONE;
	}

	mce_log(LL_DEBUG, "LED type: %d", led_type);

EXIT:
	return led_type;
}

/**
 * Custom find function to get a particular entry in the pattern stack
 *
 * @param data The pattern_struct entry
 * @param userdata The pattern name
 */
static gint queue_find(gconstpointer data, gconstpointer userdata) G_GNUC_PURE;
static gint queue_find(gconstpointer data, gconstpointer userdata)
{
	pattern_struct *psp;

	if (data == NULL || userdata == NULL)
		return -1;

	psp = (pattern_struct *)data;

	if (psp->name == NULL)
		return -1;

	return strcmp(psp->name, (gchar *)userdata);
}

/**
 * Custom compare function used for priority insertions
 *
 * @param entry1 Queue entry 1
 * @param entry2 Queue entry 2
 * @param userdata The pattern name
 */
static gint queue_prio_compare(gconstpointer entry1,
			       gconstpointer entry2,
			       gpointer userdata) G_GNUC_PURE;
static gint queue_prio_compare(gconstpointer entry1,
			       gconstpointer entry2,
			       gpointer userdata)
{
	pattern_struct *psp1 = (pattern_struct *)entry1;
	pattern_struct *psp2 = (pattern_struct *)entry2;

	(void)userdata;

	return psp1->priority - psp2->priority;
}

static void lysti_set_brightness(gint brightness)
{
	guint r_brightness = 0;
	guint g_brightness = 0;
	guint b_brightness = 0;

	if (brightness < -1 || brightness > 50) {
		mce_log(LL_WARN, "Invalid brightness value %d", brightness);
		return;
	}

	if (brightness != -1) {
		if (active_brightness == brightness)
			return;

		active_brightness = brightness;
	}

	if (current_lysti_led_pattern & MCE_LYSTI_RED_MASK) {
		/* Red is on, tweaking is needed */
		if ((current_lysti_led_pattern & MCE_LYSTI_GREEN_MASK) &&
		    (current_lysti_led_pattern & MCE_LYSTI_BLUE_MASK)) {
			/* White */
			r_brightness = (unsigned)active_brightness * 4;
			r_brightness = (r_brightness < 50) ? r_brightness : 50;
			g_brightness = r_brightness / 4;
			b_brightness = r_brightness / 4;
		} else if (current_lysti_led_pattern & MCE_LYSTI_GREEN_MASK) {
			/* Orange */
			r_brightness = (unsigned)active_brightness * 10;
			r_brightness = (r_brightness < 50) ? r_brightness : 50;
			g_brightness = r_brightness / 10;
			b_brightness = 0;
		} else {
			/* Violet */
			r_brightness = (unsigned)active_brightness * 4;
			r_brightness = (r_brightness < 50) ? r_brightness : 50;
			b_brightness = r_brightness / 4;
			g_brightness = 0;
		}
	} else {
		/* When red is not on, we use brightness as is */
		r_brightness = (unsigned)active_brightness;
		g_brightness = (unsigned)active_brightness;
		b_brightness = (unsigned)active_brightness;
	}

	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_R_LED_CURRENT_PATH, r_brightness);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_G_LED_CURRENT_PATH, g_brightness);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_B_LED_CURRENT_PATH, b_brightness);

	mce_log(LL_DEBUG, "Brightness set to %d (%d, %d, %d)",
		active_brightness, r_brightness, g_brightness, b_brightness);
}

/**
 * Disable the Lysti-LED
 */
static void lysti_disable_led(void)
{
	/* Disable engine 1 */
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE1_MODE_PATH,
				       MCE_LED_DISABLED_MODE);

	/* Disable engine 2 */
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE2_MODE_PATH,
				       MCE_LED_DISABLED_MODE);

	/* Turn off all three leds */
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_R_BRIGHTNESS_PATH, 0);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_G_BRIGHTNESS_PATH, 0);
	(void)mce_write_number_string_to_file(MCE_LYSTI_DIRECT_B_BRIGHTNESS_PATH, 0);
}

/**
 * Disable the LED
 */
static void disable_led(void)
{
	cancel_pattern_timeout();

	switch (get_led_type()) {
	case LED_TYPE_LYSTI:
		lysti_disable_led();
		break;

	default:
		break;
	}
}

/**
 * Timeout callback for LED patterns
 *
 * @param data Unused
 * @return Always returns FALSE to disable timeout
 */
static gboolean led_pattern_timeout_cb(gpointer data)
{
	(void)data;

	led_pattern_timeout_cb_id = 0;

	disable_led();
	active_pattern->active = FALSE;

	return FALSE;
}

/**
 * Cancel pattern timeout
 */
static void cancel_pattern_timeout(void)
{
	/* Remove old timeout */
	if (led_pattern_timeout_cb_id != 0) {
		g_source_remove(led_pattern_timeout_cb_id);
		led_pattern_timeout_cb_id = 0;
	}
}

/**
 * Setup pattern timeout
 */
static void setup_pattern_timeout(gint timeout)
{
	cancel_pattern_timeout();

	/* Setup new timeout */
	led_pattern_timeout_cb_id =
		g_timeout_add_seconds(timeout, led_pattern_timeout_cb, NULL);
}

/**
 * Setup and activate a new Lysti-LED pattern
 *
 * @param pattern A pointer to a pattern_struct with the new pattern
 */
static void lysti_program_led(const pattern_struct *const pattern)
{
	/* Disable old LED patterns */
	lysti_disable_led();

	/* Load new patterns, one engine at a time */

	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE1_MODE_PATH,
				       MCE_LED_LOAD_MODE);
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE1_LEDS_PATH,
				       bin_to_string(pattern->engine1_mux));
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE1_LOAD_PATH,
				       pattern->channel1);

	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE2_MODE_PATH,
				       MCE_LED_LOAD_MODE);
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE2_LEDS_PATH,
				       bin_to_string(pattern->engine2_mux));
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE2_LOAD_PATH,
				       pattern->channel2);

	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE2_MODE_PATH,
				       MCE_LED_RUN_MODE);
	(void)mce_write_string_to_file(MCE_LYSTI_ENGINE1_MODE_PATH,
				       MCE_LED_RUN_MODE);

        /* Save what colors we are driving */
        current_lysti_led_pattern = pattern->engine1_mux | pattern->engine2_mux;

        /* Update color hue according what leds are driven */
        lysti_set_brightness(-1);
}

/**
 * Setup and activate a new LED pattern
 *
 * @param pattern A pointer to a pattern_struct with the new pattern
 */
static void program_led(const pattern_struct *const pattern)
{
	switch (get_led_type()) {
	case LED_TYPE_LYSTI:
		lysti_program_led(pattern);

	default:
		break;
	}
}

/**
 * Recalculate active pattern and update the pattern timer
 */
static void led_update_active_pattern(void)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	pattern_struct *new_active_pattern;
	gint i = 0;

	if (g_queue_is_empty(pattern_stack) == TRUE) {
		disable_led();
		goto EXIT;
	}

	while ((new_active_pattern = g_queue_peek_nth(pattern_stack,
						      i++)) != NULL) {
		mce_log(LL_DEBUG,
			"pattern: %s, active: %d, enabled: %d",
			new_active_pattern->name,
			new_active_pattern->active,
			new_active_pattern->enabled);

		/* If the pattern is deactivated, ignore */
		if (new_active_pattern->active == FALSE)
			continue;

		/* If the pattern is disabled through GConf, ignore */
		if (new_active_pattern->enabled == FALSE)
			continue;

		/* Always show pattern with visibility 3 or 5 */
		if ((new_active_pattern->policy == 3) ||
		    (new_active_pattern->policy == 5))
			break;

		/* Acting dead behaviour */
		if (system_state == MCE_STATE_ACTDEAD) {
			/* If we're in acting dead,
			 * show patterns with visibility 4
			 */
			if (new_active_pattern->policy == 4)
				break;

			/* If we're in acting dead
			 * and the display is off, show pattern
			 */
			if ((display_state == MCE_DISPLAY_OFF) &&
			    (new_active_pattern->policy == 2))
				break;

			/* If the display is on and visibility is 2,
			 * or if visibility is 1/0, ignore pattern
			 */
			continue;
		}

		/* If the display is off, we can use any active pattern */
		if (display_state == MCE_DISPLAY_OFF)
			break;

		/* If the pattern should be shown with screen on, use it */
		if (new_active_pattern->policy == 1)
			break;
	}

	if ((new_active_pattern == NULL) ||
	    ((led_enabled == FALSE) &&
	     (new_active_pattern->policy != 5))) {
		active_pattern = NULL;
		disable_led();
		cancel_pattern_timeout();
		goto EXIT;
	}

	/* Only reprogram the pattern and timer if the pattern changed */
	if (new_active_pattern != active_pattern) {
		disable_led();
		cancel_pattern_timeout();

		if (new_active_pattern->timeout != -1) {
			setup_pattern_timeout(new_active_pattern->timeout);
		}

		program_led(new_active_pattern);
	}

	active_pattern = new_active_pattern;

EXIT:
	return;
}

/**
 * Activate a pattern in the pattern-stack
 *
 * @param name The name of the pattern to activate
 */
static void led_activate_pattern(const gchar *const name)
{
	pattern_struct *psp;
	GList *glp;

	if ((name != NULL) &&
	    ((glp = g_queue_find_custom(pattern_stack,
					name, queue_find)) != NULL)) {
		psp = (pattern_struct *)glp->data;
		psp->active = TRUE;
		led_update_active_pattern();
		mce_log(LL_DEBUG,
			"LED pattern %s activated",
			name);
	} else {
		mce_log(LL_DEBUG,
			"Received request to activate "
			"a non-existing LED pattern");
	}
}

/**
 * Deactivate a pattern in the pattern-stack
 *
 * @param name The name of the pattern to deactivate
 */
static void led_deactivate_pattern(const gchar *const name)
{
	pattern_struct *psp;
	GList *glp;

	if ((name != NULL) &&
	    ((glp = g_queue_find_custom(pattern_stack,
					name, queue_find)) != NULL)) {
		psp = (pattern_struct *)glp->data;
		psp->active = FALSE;
		led_update_active_pattern();
		mce_log(LL_DEBUG,
			"LED pattern %s deactivated",
			name);
	} else {
		mce_log(LL_DEBUG,
			"Received request to deactivate "
			"a non-existing LED pattern");
	}
}

/**
 * Enable the LED
 */
static void led_enable(void)
{
	led_update_active_pattern();
	led_enabled = TRUE;
}

/**
 * Disable the LED
 */
static void led_disable(void)
{
	led_enabled = FALSE;
	disable_led();
}

/**
 * Handle system state change
 *
 * @param data Unused
 */
static void system_state_trigger(gconstpointer data)
{
	(void)data;

	led_update_active_pattern();
}

/**
 * Handle display state change
 *
 * @param data Unused
 */
static void display_state_trigger(gconstpointer data)
{
	(void)data;

	led_update_active_pattern();
}

/**
 * Handle led brightness change
 *
 * @param data The LED brightness stored in a pointer
 */
static void led_brightness_trigger(gconstpointer data)
{
	gint led_brightness = GPOINTER_TO_INT(data);

	switch (get_led_type()) {
	case LED_TYPE_LYSTI:
		lysti_set_brightness(led_brightness);
		break;

	default:
		break;
	}
}

/**
 * Handle LED pattern activate requests
 *
 * @param data The pattern name
 */
static void led_pattern_activate_trigger(gconstpointer data)
{
	led_activate_pattern((gchar *)data);
}

/**
 * Handle LED pattern deactivate requests
 *
 * @param data The pattern name
 */
static void led_pattern_deactivate_trigger(gconstpointer data)
{
	led_deactivate_pattern((gchar *)data);
}

/**
 * Custom find function to get a GConf callback ID in the pattern stack
 *
 * @param data The pattern_struct entry
 * @param userdata The pattern name
 */
static gint gconf_cb_find(gconstpointer data, gconstpointer userdata)
{
	pattern_struct *psp;

	if ((data == NULL) || (userdata == NULL))
		return -1;

	psp = (pattern_struct *)data;

	return psp->gconf_cb_id != *(guint *)userdata;
}

/**
 * GConf callback for LED related settings
 *
 * @param gcc Unused
 * @param id Connection ID from gconf_client_notify_add()
 * @param entry The modified GConf entry
 * @param data Unused
 */
static void led_gconf_cb(GConfClient *const gcc, const guint id,
			 GConfEntry *const entry, gpointer const data)
{
	GConfValue *gcv = gconf_entry_get_value(entry);
	pattern_struct *psp = NULL;
	GList *glp = NULL;

	(void)gcc;
	(void)data;

	/* Key is unset */
	if (gcv == NULL) {
		mce_log(LL_DEBUG,
			"GConf Key `%s' has been unset",
			gconf_entry_get_key(entry));
		goto EXIT;
	}

	if ((glp = g_queue_find_custom(pattern_stack,
				       &id, gconf_cb_find)) != NULL) {
		psp = (pattern_struct *)glp->data;
		psp->enabled = gconf_value_get_bool(gcv);
		led_update_active_pattern();
	} else {
		mce_log(LL_WARN, "Spurious GConf value received; confused!");
	}

EXIT:
	return;
}

/**
 * Get the enabled/disabled value from GConf and set up a notifier
 */
static gboolean pattern_get_enabled(const gchar *const patternname,
				    guint *gconf_cb_id)
{
	gboolean retval = DEFAULT_PATTERN_ENABLED;
	gchar *path = gconf_concat_dir_and_key(MCE_GCONF_LED_PATH,
					       patternname);

	/* Since we've set a default, error handling is unnecessary */
	(void)mce_gconf_get_bool(path, &retval);

	if (mce_gconf_notifier_add(MCE_GCONF_LED_PATH, path,
				   led_gconf_cb, gconf_cb_id) == FALSE)
		goto EXIT;

EXIT:
	g_free(path);

	return retval;
}

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
		// XXX: should we return an error instead?
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_ACTIVATE_LED_PATTERN,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	led_activate_pattern(pattern);

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

	mce_log(LL_DEBUG, "Received deactivate LED pattern request");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &pattern,
				  DBUS_TYPE_INVALID) == FALSE) {
		// XXX: should we return an error instead?
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_DEACTIVATE_LED_PATTERN,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	led_deactivate_pattern(pattern);

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

	led_enable();

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

//EXIT:
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

	led_disable();

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

//EXIT:
	return status;
}

static gboolean init_lysti_patterns(void)
{
	gchar **patternlist = NULL;
	gboolean status = FALSE;
	gsize length;
	gint i;

	/* Get the list of valid LED patterns */
	patternlist = mce_conf_get_string_list(MCE_CONF_LED_GROUP,
					       MCE_CONF_LED_PATTERNS,
					       &length,
					       NULL);

	/* Treat failed conf-value reads as if they were due to invalid keys
	 * rather than failed allocations; let future allocation attempts fail
	 * instead; otherwise we'll miss the real invalid key failures
	 */
	if (patternlist == NULL) {
		mce_log(LL_WARN,
			"Failed to configure LED patterns");
		status = TRUE;
		goto EXIT;
	}

	for (i = 0; patternlist[i]; i++) {
		gchar **tmp;

		mce_log(LL_DEBUG,
			"Getting LED pattern for: %s",
			patternlist[i]);

		tmp = mce_conf_get_string_list(led_pattern_group,
					       patternlist[i],
					       &length,
					       NULL);

		if (tmp != NULL) {
			pattern_struct *psp;
			guint engine1_mux;
			guint engine2_mux;

			if ((length != NUMBER_OF_PATTERN_FIELDS) ||
			    (strlen(tmp[PATTERN_E1_CHANNEL_FIELD]) >
			     CHANNEL_SIZE) ||
			    (strlen(tmp[PATTERN_E2_CHANNEL_FIELD]) >
			     CHANNEL_SIZE)) {
				mce_log(LL_ERR,
					"Skipping invalid LED-pattern");
				g_strfreev(tmp);
				continue;
			}

			engine1_mux = 0;
			engine2_mux = 0;

			if (strchr(tmp[PATTERN_MUXING_FIELD], 'r'))
				engine1_mux |= MCE_LYSTI_RED_MASK;

			if (strchr(tmp[PATTERN_MUXING_FIELD], 'R'))
				engine2_mux |= MCE_LYSTI_RED_MASK;

			if (strchr(tmp[PATTERN_MUXING_FIELD], 'g'))
				engine1_mux |= MCE_LYSTI_GREEN_MASK;

			if (strchr(tmp[PATTERN_MUXING_FIELD], 'G'))
				engine2_mux |= MCE_LYSTI_GREEN_MASK;

			if (strchr(tmp[PATTERN_MUXING_FIELD], 'b'))
				engine1_mux |= MCE_LYSTI_BLUE_MASK;

			if (strchr(tmp[PATTERN_MUXING_FIELD], 'B'))
				engine2_mux |= MCE_LYSTI_BLUE_MASK;

			if ((engine1_mux & engine2_mux) != 0) {
				mce_log(LL_ERR,
					"Same LED muxed to multiple engines; "
					"skipping invalid LED-pattern");
				g_strfreev(tmp);
				continue;
			}

			psp = g_slice_new(pattern_struct);

			if (!psp) {
				g_strfreev(tmp);
				goto EXIT2;
			}

			psp->priority = strtoul(tmp[PATTERN_PRIO_FIELD],
						NULL, 10);
			psp->policy = strtoul(tmp[PATTERN_SCREEN_ON_FIELD],
						 NULL, 10);

			if ((psp->timeout = strtoul(tmp[PATTERN_TIMEOUT_FIELD],
						    NULL, 10)) == 0)
				psp->timeout = -1;

			/* Catch all error checking for all three strtoul */
			if ((errno == EINVAL) || (errno == ERANGE)) {
				/* Reset errno,
				 * to avoid false positives further down
				 */
				g_strfreev(tmp);
				g_slice_free(pattern_struct, psp);
				continue;
			}

			psp->engine1_mux = engine1_mux;
			psp->engine2_mux = engine2_mux;

			strncpy(psp->channel1,
			       tmp[PATTERN_E1_CHANNEL_FIELD],
			       CHANNEL_SIZE);
			strncpy(psp->channel2,
			       tmp[PATTERN_E2_CHANNEL_FIELD],
			       CHANNEL_SIZE);

			psp->active = FALSE;

			psp->enabled = pattern_get_enabled(patternlist[i],
							   &(psp->gconf_cb_id));

			psp->name = strdup(patternlist[i]);

			g_strfreev(tmp);

			g_queue_insert_sorted(pattern_stack, psp,
					      queue_prio_compare,
					      NULL);
		}
	}

	/* Set the LED brightness */
	execute_datapipe(&led_brightness_pipe,
			 GINT_TO_POINTER(DEFAULT_LYSTI_RGB_LED_CURRENT),
			 USE_INDATA, CACHE_INDATA);

	status = TRUE;

EXIT2:
	g_strfreev(patternlist);

EXIT:
	return status;
}

/**
 * Init patterns for the LED
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean init_patterns(void)
{
	gboolean status;

	switch (get_led_type()) {
	case LED_TYPE_LYSTI:
		status = init_lysti_patterns();
		break;

	default:
		status = TRUE;
		break;
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
	gchar *status = NULL;

	(void)module;

	/* Disable Nokia N900 bq24150a stat pin messing with LED */
	if (access(BQ24150A_STAT_PIN_SYS_PATH, W_OK) != -1)
		mce_write_string_to_file(BQ24150A_STAT_PIN_SYS_PATH, "0");

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);
	append_output_trigger_to_datapipe(&led_brightness_pipe,
					  led_brightness_trigger);
	append_output_trigger_to_datapipe(&led_pattern_activate_pipe,
					  led_pattern_activate_trigger);
	append_output_trigger_to_datapipe(&led_pattern_deactivate_pipe,
					  led_pattern_deactivate_trigger);

	pattern_stack = g_queue_new();

	if (init_patterns() == FALSE)
		goto EXIT;

	/* req_led_pattern_activate */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ACTIVATE_LED_PATTERN,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 led_activate_pattern_dbus_cb) == NULL)
		goto EXIT;

	/* req_led_pattern_deactivate */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEACTIVATE_LED_PATTERN,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 led_deactivate_pattern_dbus_cb) == NULL)
		goto EXIT;

	/* req_led_enable */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ENABLE_LED,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 led_enable_dbus_cb) == NULL)
		goto EXIT;

	/* req_led_disable */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISABLE_LED,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 led_disable_dbus_cb) == NULL)
		goto EXIT;

	led_enable();

EXIT:
	return status;
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
	system_state_t system_state = datapipe_get_gint(system_state_pipe);

	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&led_pattern_deactivate_pipe,
					    led_pattern_deactivate_trigger);
	remove_output_trigger_from_datapipe(&led_pattern_activate_pipe,
					    led_pattern_activate_trigger);
	remove_output_trigger_from_datapipe(&led_brightness_pipe,
					    led_brightness_trigger);
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);

	/* Don't disable the LED on shutdown/reboot/acting dead */
	if ((system_state != MCE_STATE_ACTDEAD) &&
	    (system_state != MCE_STATE_SHUTDOWN) &&
	    (system_state != MCE_STATE_REBOOT)) {
		led_disable();
	}

	/* Free the pattern stack */
	if (pattern_stack != NULL) {
		pattern_struct *psp;

		while ((psp = g_queue_pop_head(pattern_stack)) != NULL) {
			mce_gconf_notifier_remove(GINT_TO_POINTER(psp->gconf_cb_id), NULL);
			g_free(psp->name);
			g_slice_free(pattern_struct, psp);
		}

		g_queue_free(pattern_stack);
	}

	/* Remove all timer sources */
	cancel_pattern_timeout();

	return;
}
