/**
 * @file vibrator.c
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
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mce.h"
#include "vibrator.h"
#include "mce-io.h"
#include "mce-hal.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "datapipe.h"

#define MODULE_NAME		"vibrator"

#define USER_MANUAL_PATTERN_NAME "PatternUserManual"

static const gchar *const provides[] = { MODULE_NAME, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

static GQueue *pattern_stack = NULL;
static gboolean vibrator_enabled = FALSE;

typedef enum {
	PATTERN_PRIO_FIELD = 0,
	PATTERN_SCREEN_ON_FIELD = 1,
	PATTERN_TIMEOUT_FIELD = 2,
	PATTERN_REPEAT_COUNT_FIELD = 3,
	PATTERN_ACCEL_PERIOD_FIELD = 4,
	PATTERN_ON_PERIOD_FIELD = 5,
	PATTERN_DECEL_PERIOD_FIELD = 6,
	PATTERN_OFF_PERIOD_FIELD = 7,
	PATTERN_SPEED_FIELD = 8,
	NUMBER_OF_PATTERN_FIELDS
} pattern_field;

typedef struct {
	gchar *name;
	gint priority;
	gint policy;
	gint timeout;
	gint repeat_count;
	gint accel_period;
	gint on_period;
	gint decel_period;
	gint off_period;
	gint speed;
	gboolean active;
} pattern_struct;

static pattern_struct *active_pattern = NULL;

typedef enum {
	VIBRATOR_TYPE_UNSET = -1,
	VIBRATOR_TYPE_NONE = 0,
	VIBRATOR_TYPE_VIBRA = 1,
} vibrator_type_t;

typedef enum {
	VIBRA_NOT_STARTED = 0,
	VIBRA_ON,
	VIBRA_OFF
} pattern_state_t;

static pattern_state_t pattern_state = VIBRA_NOT_STARTED;

#define VIBRA_ACCELERATE_SPEED		 255
#define VIBRA_DECELERATE_SPEED		-255

static gint repeat_counter = 0;

static guint vibrator_pattern_timeout_cb_id = 0;

static guint vibrator_period_timer_cb_id = 0;

static const gchar *vibrator_pattern_group = NULL;

static gboolean vibrator_period_timer_cb(gpointer data);
static void cancel_pattern_timeout(void);
static void vibrator_update_active_pattern(void);

static vibrator_type_t get_vibrator_type(void)
{
	static vibrator_type_t vibrator_type = VIBRATOR_TYPE_UNSET;
	product_id_t product_id = get_product_id();

	(void)product_id;

	if (vibrator_type != VIBRATOR_TYPE_UNSET)
		goto EXIT;

	if (g_access(MCE_VIBRA_PATH, W_OK) == 0) {
		vibrator_type = VIBRATOR_TYPE_VIBRA;
		vibrator_pattern_group = MCE_CONF_VIBRA_PATTERN_RX51_GROUP;
	} else {
		vibrator_type = VIBRATOR_TYPE_NONE;
	}

	mce_log(LL_DEBUG, "Vibrator-type: %d", vibrator_type);

EXIT:
	return vibrator_type;
}

static gint queue_find(gconstpointer data, gconstpointer userdata) G_GNUC_PURE;
static gint queue_find(gconstpointer data, gconstpointer userdata)
{
	const pattern_struct *psp;

	if ((data == NULL) || (userdata == NULL))
		return -1;

	psp = (const pattern_struct *)data;

	if (psp->name == NULL)
		return -1;

	return strcmp(psp->name, (const gchar *)userdata);
}

static gint queue_prio_compare(gconstpointer entry1,
			       gconstpointer entry2,
			       gpointer userdata) G_GNUC_PURE;
static gint queue_prio_compare(gconstpointer entry1,
			       gconstpointer entry2,
			       gpointer userdata)
{
	const pattern_struct *psp1 = (const pattern_struct *)entry1;
	const pattern_struct *psp2 = (const pattern_struct *)entry2;

	(void)userdata;

	return psp1->priority - psp2->priority;
}

static void vibra_disable_vibrator(void)
{
	(void)mce_write_string_to_file(MCE_VIBRA_PATH, "0 0");
}

static void cancel_period_timer(void)
{
	if (vibrator_period_timer_cb_id != 0) {
		g_source_remove(vibrator_period_timer_cb_id);
		vibrator_period_timer_cb_id = 0;
	}
}

static void disable_vibrator(void)
{
	cancel_period_timer();
	switch(get_vibrator_type())
	{
	case VIBRATOR_TYPE_VIBRA:
		vibra_disable_vibrator();
		break;
	default:
		break;
	}
}

static void vibra_program_vibrator(gint start_pulse, gint on_period,
				   gint stop_pulse, gint speed)
{
	gchar *tmp = NULL;
	gint print_format = 0;

	if (start_pulse != 0)
		print_format += 4;

	if (on_period != 0)
		print_format += 2;

	if (stop_pulse != 0)
		print_format += 1;

	switch (print_format) {
	case 0:
		tmp = g_strdup_printf("%d 0",
				      speed);
		break;
	case 1:
		tmp = g_strdup_printf("%d %d",
				      VIBRA_DECELERATE_SPEED, stop_pulse);
		break;

	case 2:
		tmp = g_strdup_printf("%d %d",
				      speed, on_period);
		break;

	case 3:
		tmp = g_strdup_printf("%d %d %d %d",
				      speed, on_period,
				      VIBRA_DECELERATE_SPEED, stop_pulse);
		break;

	case 4:
		tmp = g_strdup_printf("%d %d",
				      VIBRA_ACCELERATE_SPEED, start_pulse);
		break;

	case 5:
		tmp = g_strdup_printf("%d %d %d %d",
				      VIBRA_ACCELERATE_SPEED, start_pulse,
				      VIBRA_DECELERATE_SPEED, stop_pulse);
		break;

	case 6:
		tmp = g_strdup_printf("%d %d %d %d",
				      VIBRA_ACCELERATE_SPEED, start_pulse,
				      speed, on_period);
		break;

	case 7:
		tmp = g_strdup_printf("%d %d %d %d %d %d",
				      VIBRA_ACCELERATE_SPEED, start_pulse,
				      speed, on_period,
				      VIBRA_DECELERATE_SPEED, stop_pulse);
		break;

	default:
		break;

	}
	if (tmp != NULL) {
		(void)mce_write_string_to_file(MCE_VIBRA_PATH, tmp);
		g_free(tmp);
	}
}

static void program_vibrator(gint start_pulse, gint on_period,
			     gint stop_pulse, gint speed)
{
	switch (get_vibrator_type())
	{
	case VIBRATOR_TYPE_VIBRA:
		vibra_program_vibrator(start_pulse, on_period,
				       stop_pulse, speed);
		break;
	default:
		break;
	}
}

static void vibrator_state_machine(void)
{
	gint on_time = 0;
	gint start_pulse_time = 0;
	gint stop_pulse_time = 0;
	gint speed = 0;

	switch (pattern_state) {
	case VIBRA_NOT_STARTED:
		start_pulse_time = active_pattern->accel_period;
		on_time = active_pattern->on_period;
		stop_pulse_time = active_pattern->decel_period;
		speed = active_pattern->speed;
		repeat_counter = 0;
		pattern_state = VIBRA_ON;
		program_vibrator(start_pulse_time, on_time,
				 stop_pulse_time, speed);
		if (start_pulse_time + on_time + stop_pulse_time)
		{
			vibrator_period_timer_cb_id =
				g_timeout_add(start_pulse_time + on_time +
					      stop_pulse_time,
					      vibrator_period_timer_cb, NULL);
		}
		break;

	case VIBRA_ON:
		vibrator_period_timer_cb_id =
			g_timeout_add(active_pattern->off_period,
				      vibrator_period_timer_cb, NULL);
		pattern_state = VIBRA_OFF;
		break;

	case VIBRA_OFF:
	default:
		if ((repeat_counter + 1) == active_pattern->repeat_count) {
			active_pattern->active = FALSE;
			cancel_pattern_timeout();
			pattern_state = VIBRA_NOT_STARTED;
			vibrator_update_active_pattern();
		} else {
			start_pulse_time = active_pattern->accel_period;
			on_time = active_pattern->on_period;
			stop_pulse_time = active_pattern->decel_period;
			speed = active_pattern->speed;
			pattern_state = VIBRA_ON;
			program_vibrator(start_pulse_time, on_time,
					stop_pulse_time, speed);
			vibrator_period_timer_cb_id =
				g_timeout_add(start_pulse_time + on_time +
					      stop_pulse_time,
					      vibrator_period_timer_cb, NULL);
			if (active_pattern->repeat_count != 0)
				repeat_counter++;
		}
		break;
	}
}

static gboolean vibrator_period_timer_cb(gpointer data)
{
	(void)data;

	vibrator_state_machine();

	return FALSE;
}

static gboolean vibrator_pattern_timeout_cb(gpointer data)
{
	(void)data;

	active_pattern->active = FALSE;
	vibrator_update_active_pattern();

	return FALSE;
}

static void cancel_pattern_timeout(void)
{
	if (vibrator_pattern_timeout_cb_id != 0) {
		g_source_remove(vibrator_pattern_timeout_cb_id);
		vibrator_pattern_timeout_cb_id = 0;
	}
}

static void setup_pattern_timeout(gint timeout)
{
	cancel_pattern_timeout();
	vibrator_pattern_timeout_cb_id =
		g_timeout_add_seconds(timeout,
				      vibrator_pattern_timeout_cb, NULL);
}

static void vibrator_update_active_pattern(void)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);
	pattern_struct *new_active_pattern;
	gint i = 0;

	if (g_queue_is_empty(pattern_stack) == TRUE) {
		disable_vibrator();
		cancel_pattern_timeout();
		goto EXIT;
	}

	while ((new_active_pattern = g_queue_peek_nth(pattern_stack,
						      i++)) != NULL) {
		mce_log(LL_DEBUG,
			"pattern: %s, active: %d",
			new_active_pattern->name,
			new_active_pattern->active);

		if (new_active_pattern->active == FALSE)
			continue;

		if ((new_active_pattern->policy == 3) ||
		    (new_active_pattern->policy == 5))
			break;

		if (system_state == MCE_STATE_ACTDEAD) {
			if (new_active_pattern->policy == 4)
				break;

			if ((display_state == MCE_DISPLAY_OFF) &&
			    (new_active_pattern->policy == 2))
				break;

			continue;
		}

		if (display_state == MCE_DISPLAY_OFF)
			break;

		if (new_active_pattern->policy == 1)
			break;
	}

	if ((new_active_pattern == NULL) ||
	    ((vibrator_enabled == FALSE) && (new_active_pattern->policy != 5)) ||
	    (call_state == CALL_STATE_ACTIVE)) {
		active_pattern = NULL;

		if (new_active_pattern != NULL)
			new_active_pattern->active = FALSE;

		disable_vibrator();
		cancel_pattern_timeout();
		goto EXIT;
	}

	if (new_active_pattern != active_pattern) {
		disable_vibrator();
		cancel_pattern_timeout();

		if (new_active_pattern->timeout != -1) {
			setup_pattern_timeout(new_active_pattern->timeout);
		}

		active_pattern = new_active_pattern;
		pattern_state = VIBRA_NOT_STARTED;
		vibrator_state_machine();
	}

EXIT:
	return;
}

static void vibrator_activate_pattern(const gchar *const name)
{
	pattern_struct *psp;
	GList *glp;
	call_state_t call_state = datapipe_get_gint(call_state_pipe);

	if ((name != NULL) &&
	    ((glp = g_queue_find_custom(pattern_stack,
					name, queue_find)) != NULL)) {
		psp = (pattern_struct *)glp->data;

		if (call_state == CALL_STATE_ACTIVE) {
			mce_log(LL_DEBUG,
				"Ignored request to activate "
				"vibra pattern during active call");
		} else {
			psp->active = TRUE;
			vibrator_update_active_pattern();
			mce_log(LL_DEBUG,
				"Vibrator pattern %s activated",
				name);
		}
	} else {
		mce_log(LL_DEBUG,
			"Received request to activate "
			"a non-existing vibrator pattern");
	}
}

static void vibrator_deactivate_pattern(const gchar *const name)
{
	pattern_struct *psp;
	GList *glp;

	if ((name != NULL) &&
	    ((glp = g_queue_find_custom(pattern_stack,
					name, queue_find)) != NULL)) {
		psp = (pattern_struct *)glp->data;
		psp->active = FALSE;
		vibrator_update_active_pattern();
		mce_log(LL_DEBUG,
			"Vibrator pattern %s deactivated",
			name);
	} else {
		mce_log(LL_DEBUG,
			"Received request to deactivate "
			"a non-existing vibrator pattern");
	}
}

static void vibrator_enable(void)
{
	vibrator_update_active_pattern();
	vibrator_enabled = TRUE;
}

static void vibrator_disable(void)
{
	vibrator_enabled = FALSE;
	disable_vibrator();
	cancel_pattern_timeout();
}

static void system_state_trigger(gconstpointer data)
{
	(void)data;

	vibrator_update_active_pattern();
}

static void display_state_trigger(gconstpointer data)
{
	(void)data;

	vibrator_update_active_pattern();
}

static void call_state_trigger(gconstpointer data)
{
	(void)data;

	vibrator_update_active_pattern();
}

static void vibrator_pattern_activate_trigger(gconstpointer data)
{
	vibrator_activate_pattern((const gchar *)data);
}

static void vibrator_pattern_deactivate_trigger(gconstpointer data)
{
	vibrator_deactivate_pattern((const gchar *)data);
}

static gboolean vibrator_activate_pattern_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;
	gchar *pattern = NULL;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received activate vibrator pattern request");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &pattern,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_ACTIVATE_VIBRATOR_PATTERN,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	vibrator_activate_pattern(pattern);

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

EXIT:
	return status;
}

static gboolean vibrator_deactivate_pattern_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;
	gchar *pattern = NULL;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received deactivate vibrator pattern request");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &pattern,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_DEACTIVATE_VIBRATOR_PATTERN,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	vibrator_deactivate_pattern(pattern);

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

EXIT:
	return status;
}

static gboolean vibrator_enable_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status;

	mce_log(LL_DEBUG, "Received vibrator enable request");

	vibrator_enable();

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

static gboolean vibrator_disable_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status;

	mce_log(LL_DEBUG, "Received vibrator disable request");

	vibrator_disable();

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

static gboolean init_vibra_patterns(void)
{
	gchar **patternlist = NULL;
	gboolean status = FALSE;
	gsize length;
	gint i;

	patternlist = mce_conf_get_string_list(MCE_CONF_VIBRATOR_GROUP,
					       MCE_CONF_VIBRATOR_PATTERNS,
					       &length,
					       NULL);

	if (patternlist == NULL) {
		mce_log(LL_WARN,
			"Failed to configure vibrator patterns");
		status = TRUE;
		goto EXIT;
	}

	for (i = 0; patternlist[i]; i++) {
		gint *tmp;

		mce_log(LL_DEBUG,
			"Getting Vibra pattern for: %s",
			patternlist[i]);

		tmp = mce_conf_get_int_list(vibrator_pattern_group,
					    patternlist[i],
					    &length,
					    NULL);

		if (tmp != NULL) {
			pattern_struct *psp;

			if (length != NUMBER_OF_PATTERN_FIELDS) {
				mce_log(LL_ERR,
					"Skipping invalid Vibra-pattern");
				g_free(tmp);
				continue;
			}

			psp = g_slice_new(pattern_struct);

			if (!psp) {
				g_free(tmp);
				goto EXIT2;
			}

			psp->name = strdup(patternlist[i]);
			psp->priority = tmp[PATTERN_PRIO_FIELD];
			psp->policy = tmp[PATTERN_SCREEN_ON_FIELD];
			psp->timeout = tmp[PATTERN_TIMEOUT_FIELD] ? tmp[PATTERN_TIMEOUT_FIELD] : -1;
			psp->repeat_count = ABS(tmp[PATTERN_REPEAT_COUNT_FIELD]);
			psp->accel_period = ABS(tmp[PATTERN_ACCEL_PERIOD_FIELD]);
			psp->on_period = ABS(tmp[PATTERN_ON_PERIOD_FIELD]);
			psp->decel_period = ABS(tmp[PATTERN_DECEL_PERIOD_FIELD]);
			psp->off_period = ABS(tmp[PATTERN_OFF_PERIOD_FIELD]);
			psp->speed = ABS(tmp[PATTERN_SPEED_FIELD]);
			psp->active = FALSE;

			g_free(tmp);

			g_queue_insert_sorted(pattern_stack, psp,
					      queue_prio_compare,
					      NULL);
		}
	}

	status = TRUE;

EXIT2:
	g_strfreev(patternlist);

EXIT:
	return status;
}

static gboolean init_patterns(void)
{
	gboolean status;

	switch (get_vibrator_type())
	{
	case VIBRATOR_TYPE_VIBRA:
		status = init_vibra_patterns();
		break;
	default:
		status = TRUE;
		break;
	}

	return status;
}

static gboolean vibrator_stop_manual_vibration_cb(DBusMessage *msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	GList *glp;
	pattern_struct *psp;
	gboolean status;

	mce_log(LL_DEBUG, "Received stop manual vibration request");
    if ((glp = g_queue_find_custom(pattern_stack, USER_MANUAL_PATTERN_NAME, queue_find)) != NULL)
	{
		psp = (pattern_struct *)glp->data;
		psp->speed = 0;
		psp->on_period = 0;
		vibrator_deactivate_pattern(USER_MANUAL_PATTERN_NAME);
	}
	else
	{
		mce_log(LL_ERR,
			"USER_MANUAL_PATTERN_NAME is a non-existing vibrator pattern");
	}

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

static gboolean vibrator_start_manual_vibration_cb(DBusMessage *msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;
	GList *glp;
	pattern_struct *psp;
	DBusError error;
	gint speed = 0;
	gint duration = 0;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received start manual vibration request");
	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_INT32, &speed,
				  DBUS_TYPE_INT32, &duration,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_ACTIVATE_VIBRATOR_PATTERN,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}
	else
	{
		gint newonperiod = duration;
		gint newspeed = speed;
		mce_log(LL_DEBUG,
			"Manual pattern details: speed = %d, duration = %d ms",
			speed, duration);
		if ((glp = g_queue_find_custom(pattern_stack, USER_MANUAL_PATTERN_NAME, queue_find)) != NULL)
		{
			psp = (pattern_struct *)glp->data;
			if (psp->active)
				vibrator_deactivate_pattern(USER_MANUAL_PATTERN_NAME);

			if ((unsigned int)(newspeed + 255) > 510)
			{
				mce_log(LL_WARN,
					"Wrong speed requested (%d)", newspeed);
			}
			else
			{
				psp->speed = newspeed;
				psp->on_period = newonperiod;
				vibrator_activate_pattern(USER_MANUAL_PATTERN_NAME);
			}
		}
		else
		{
			mce_log(LL_ERR,
				"USER_MANUAL_PATTERN_NAME is a non-existing vibrator pattern");
		}

		if (no_reply == FALSE) {
			DBusMessage *reply = dbus_new_method_reply(msg);
			status = dbus_send_message(reply);
		} else {
			status = TRUE;
		}
	}

EXIT:
	return status;
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	gchar *status = NULL;

	(void)module;

	remove_output_trigger_from_datapipe(&vibrator_pattern_deactivate_pipe,
					    vibrator_pattern_deactivate_trigger);
	remove_output_trigger_from_datapipe(&vibrator_pattern_activate_pipe,
					    vibrator_pattern_activate_trigger);
	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);
	append_output_trigger_to_datapipe(&call_state_pipe,
					  call_state_trigger);

	pattern_stack = g_queue_new();

	if (init_patterns() == FALSE)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ACTIVATE_VIBRATOR_PATTERN,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_activate_pattern_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEACTIVATE_VIBRATOR_PATTERN,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_deactivate_pattern_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ENABLE_VIBRATOR,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_enable_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISABLE_VIBRATOR,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_disable_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_START_MANUAL_VIBRATION,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_start_manual_vibration_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_STOP_MANUAL_VIBRATION,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_stop_manual_vibration_cb) == NULL)
		goto EXIT;

	vibrator_enable();

EXIT:
	return status;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	vibrator_disable();

	remove_output_trigger_from_datapipe(&call_state_pipe,
					    call_state_trigger);
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);

	if (pattern_stack != NULL) {
		pattern_struct *psp;

		while ((psp = g_queue_pop_head(pattern_stack)) != NULL) {
			g_free(psp->name);
			g_slice_free(pattern_struct, psp);
		}

		g_queue_free(pattern_stack);
	}

	cancel_period_timer();
	cancel_pattern_timeout();
}
