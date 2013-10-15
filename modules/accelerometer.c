/**
 * @file accelerometer.c
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
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "mce.h"
#include "accelerometer.h"
#include <mce/mode-names.h>
#include "mce-io.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "datapipe.h"
#include "median_filter.h"

#define MODULE_NAME		"accelerometer"

static const gchar *const provides[] = { MODULE_NAME, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

static const gchar *accelerometer_coord_path = NULL;
static const gchar *accelerometer_scale_path = NULL;
static gboolean accelerometer_polling_enabled = FALSE;
static gboolean accelerometer_hw_present = FALSE;

static GSList *accelerometer_listeners = NULL;

static display_state_t old_display_state = MCE_DISPLAY_UNDEF;
static display_state_t display_state = MCE_DISPLAY_UNDEF;

static gint accelerometer_poll_interval = ACCELEROMETER_DISPLAY_ON_POLL_FREQ;

static guint accelerometer_poll_timer_cb_id = 0;

static struct {
	const gchar *rotation;
	const gchar *stand;
	const gchar *face;
	dbus_int32_t x;
	dbus_int32_t y;
	dbus_int32_t z;
	dbus_int32_t scale;
} orientation;

typedef enum {
	ACCELEROMETER_TYPE_UNSET = -1,
	ACCELEROMETER_TYPE_NONE = 0,
	ACCELEROMETER_TYPE_RX51 = 1
} accelerometer_type_t;

static void setup_accelerometer_poll_timer(void);
static void cancel_accelerometer_poll_timer(void);

static accelerometer_type_t get_accelerometer_type(void)
{
	static accelerometer_type_t accelerometer_type =
					ACCELEROMETER_TYPE_UNSET;

	if (accelerometer_type != ACCELEROMETER_TYPE_UNSET)
		goto EXIT;

	if (g_access(ACCELEROMETER_COORD_PATH_RX51, W_OK) == 0) {
		accelerometer_type = ACCELEROMETER_TYPE_RX51;

		accelerometer_coord_path = ACCELEROMETER_COORD_PATH_RX51;
		accelerometer_scale_path = ACCELEROMETER_SCALE_PATH_RX51;
	} else {
		accelerometer_type = ACCELEROMETER_TYPE_NONE;
		accelerometer_coord_path = NULL;
		accelerometer_scale_path = NULL;
	}

	mce_log(LL_DEBUG, "Accelerometer-type: %d", accelerometer_type);

EXIT:
	return accelerometer_type;
}

static gboolean update_orientation(gboolean timer_scan)
{
	static const gchar *previous = NULL;
	static gboolean nested_call = FALSE;

	const gchar *newrot;
	const gchar *oldrot;
	gboolean good_vector;
	gchar *temp = NULL;
	gint gvector;

	orientation.scale = -1;
	oldrot = orientation.rotation;
	newrot = orientation.rotation;

	if (mce_read_string_from_file(accelerometer_scale_path, &temp) == TRUE) {
		if (!strcmp(temp, ACCELEROMETER_NORMAL_SCALE))
			orientation.scale = 2;
		else
			orientation.scale = 8;

		g_free(temp);
	}

	if (mce_read_string_from_file(accelerometer_coord_path, &temp) == TRUE) {
		if (sscanf(temp, "%d %d %d",
		    &orientation.x, &orientation.y, &orientation.z) != 3) {
			mce_log(LL_ERR,
				"Failed to parse orientation: %s",
				g_strerror(errno));
			orientation.scale = -1;
			errno = 0;
			goto EXIT;
		}
	} else {
		goto EXIT;
	}

	if (nested_call)
		goto EXIT;

	gvector = ((orientation.x * orientation.x) +
	           (orientation.y * orientation.y) +
		   (orientation.z * orientation.z)) / 1000;
	good_vector = ((gvector >= ACCELEROMETER_STABLE_MINSQ) &&
		       (gvector <= ACCELEROMETER_STABLE_MAXSQ)) ? TRUE : FALSE;

	if (orientation.face == NULL)
		orientation.face = MCE_ORIENTATION_FACE_UP;

	/* Transition from up to down only on very clear downward orientation.
	 * Transition from down to up as soon as it's slightly face up
	 */
	if (!strcmp(orientation.face, MCE_ORIENTATION_FACE_UP)) {
		if ((orientation.z > ACCELEROMETER_ALMOST_ONLY_THIS) &&
		    (ABS(orientation.x) < ACCELEROMETER_PRETTY_LOW) &&
		    (ABS(orientation.y) < ACCELEROMETER_PRETTY_LOW)) {
			orientation.face = MCE_ORIENTATION_FACE_DOWN;
		}
	} else if (orientation.z < -ACCELEROMETER_ALMOST_NONE) {
		orientation.face = MCE_ORIENTATION_FACE_UP;
	}


	if (orientation.stand == NULL)
		orientation.stand = MCE_ORIENTATION_OFF_STAND;

	if (!strcmp(orientation.stand, MCE_ORIENTATION_OFF_STAND)) {
		if ((ABS(orientation.x) < ACCELEROMETER_ALMOST_NONE) &&
		    (orientation.y < -400) &&
		    (orientation.z < -ACCELEROMETER_ALMOST_NONE))
			orientation.stand = MCE_ORIENTATION_ON_STAND;
	} else {
		if ((ABS(orientation.x) > 240) ||
		    (orientation.y > -200) ||
		    (orientation.z > 0))
			orientation.stand = MCE_ORIENTATION_OFF_STAND;
	}

	if (orientation.rotation == NULL)
		orientation.rotation = MCE_ORIENTATION_LANDSCAPE;

	if (good_vector == FALSE) {
		previous = NULL;
		goto EXIT;
	}

	/* Work out a new proposed rotation */
	if ((ABS(orientation.x) >= 200) || (ABS(orientation.y) >= 200)) {
		if (!strcmp(orientation.rotation, MCE_ORIENTATION_UNKNOWN)) {
			if ((ABS(orientation.y) > 400) &&
			    (ABS(orientation.y) > ABS(orientation.x))) {
				if (orientation.y < 0)
					newrot = MCE_ORIENTATION_LANDSCAPE;
				else
					newrot = MCE_ORIENTATION_LANDSCAPE_INVERTED;
			} else if ((ABS(orientation.x) > 400) &&
				   (ABS(orientation.x) > ABS(orientation.y))) {
				if (orientation.x < 0)
					newrot = MCE_ORIENTATION_PORTRAIT;
				else
					newrot = MCE_ORIENTATION_PORTRAIT_INVERTED;
			}
		} else if ((!strcmp(orientation.rotation,
				   MCE_ORIENTATION_LANDSCAPE)) ||
			   (!strcmp(orientation.rotation,
			     MCE_ORIENTATION_LANDSCAPE_INVERTED))) {
			if (ABS(orientation.x) > (ABS(orientation.y) + 300)) {
				if (orientation.x < 0)
					newrot = MCE_ORIENTATION_PORTRAIT;
				else
					newrot = MCE_ORIENTATION_PORTRAIT_INVERTED;
			} else {
				if (orientation.y < 0)
					newrot = MCE_ORIENTATION_LANDSCAPE;
				else
					newrot = MCE_ORIENTATION_LANDSCAPE_INVERTED;
			}
		} else if ((!strcmp(orientation.rotation,
				    MCE_ORIENTATION_PORTRAIT)) ||
			   (!strcmp(orientation.rotation,
			    MCE_ORIENTATION_PORTRAIT_INVERTED))) {
			if (ABS(orientation.y) > (ABS(orientation.x) + 300)) {
				if (orientation.y < 0)
					newrot = MCE_ORIENTATION_LANDSCAPE;
				else
					newrot = MCE_ORIENTATION_LANDSCAPE_INVERTED;
			} else {
				if (orientation.x < 0)
					newrot = MCE_ORIENTATION_PORTRAIT;
				else
					newrot = MCE_ORIENTATION_PORTRAIT_INVERTED;
			}
		}
	} else {
		newrot = MCE_ORIENTATION_UNKNOWN;
	}

	if (!timer_scan) {
		orientation.rotation = newrot;
	} else if (newrot != oldrot) {
		if (newrot == previous) {
			orientation.rotation = newrot;
			previous = NULL;

			if (((display_state == MCE_DISPLAY_ON) ||
			     (display_state == MCE_DISPLAY_DIM)) &&
			    (!strcmp(oldrot, MCE_ORIENTATION_LANDSCAPE) ||
			     !strcmp(orientation.rotation, MCE_ORIENTATION_LANDSCAPE)) &&
			    (!strcmp(oldrot, MCE_ORIENTATION_PORTRAIT) ||
			     !strcmp(orientation.rotation, MCE_ORIENTATION_PORTRAIT))) {
				nested_call = TRUE;

				(void)execute_datapipe(&device_inactive_pipe,
						        GINT_TO_POINTER(FALSE),
						        USE_INDATA, CACHE_INDATA);
				nested_call = FALSE;
			}
		} else {
			previous = newrot;
		}
	} else {
		previous = NULL;
	}

EXIT:
	g_free(temp);

	return (orientation.scale != -1) ? TRUE : FALSE;
}

static gboolean send_device_orientation(DBusMessage *const method_call,
					const gchar *const rotation,
					const gchar *const stand,
					const gchar *const face,
					dbus_int32_t x,
					dbus_int32_t y,
					dbus_int32_t z)
{
	DBusMessage *msg = NULL;
	const gchar *srotation;
	const gchar *sstand;
	const gchar *sface;
	dbus_int32_t sx, sy, sz;
	gboolean status = FALSE;

	if (rotation != NULL)
		srotation = rotation;
	else if (orientation.scale != -1)
		srotation = orientation.rotation;
	else
		srotation = MCE_ORIENTATION_LANDSCAPE;

	if (stand != NULL)
		sstand = stand;
	else if (orientation.scale != -1)
		sstand = orientation.stand;
	else
		sstand = MCE_ORIENTATION_OFF_STAND;

	if (face != NULL)
		sface = face;
	else if (orientation.scale != -1)
		sface = orientation.face;
	else
		sface = MCE_ORIENTATION_FACE_UP;

	if (x != G_MAXINT32)
		sx = x;
	else if (orientation.scale != -1)
		sx = orientation.x;
	else
		sx = 0;

	if (y != G_MAXINT32)
		sy = y;
	else if (orientation.scale != -1)
		sy = orientation.y;
	else
		sy = 0;

	if (z != G_MAXINT32)
		sz = z;
	else if (orientation.scale != -1)
		sz = orientation.z;
	else
		sz = 0;

	if (method_call != NULL)
		msg = dbus_new_method_reply(method_call);
	else
		msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
				      MCE_DEVICE_ORIENTATION_SIG);

	if (dbus_message_append_args(msg,
				     DBUS_TYPE_STRING, &srotation,
				     DBUS_TYPE_STRING, &sstand,
				     DBUS_TYPE_STRING, &sface,
				     DBUS_TYPE_INT32, &sx,
				     DBUS_TYPE_INT32, &sy,
				     DBUS_TYPE_INT32, &sz,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append %sarguments to D-Bus message "
			"for %s.%s",
			method_call ? "reply " : "",
			method_call ? MCE_REQUEST_IF :
				      MCE_SIGNAL_IF,
			method_call ? MCE_DEVICE_ORIENTATION_GET :
				      MCE_DEVICE_ORIENTATION_SIG);
		dbus_message_unref(msg);
		goto EXIT;
	}

	status = dbus_send_message(msg);

EXIT:
	return status;
}

static gboolean update_and_send_orientation(void)
{
	const gchar *oldrotation = orientation.rotation;
	const gchar *oldface = orientation.face;
	gboolean status;

	if ((status = update_orientation(TRUE)) == FALSE)
		goto EXIT;

	if ((oldrotation == orientation.rotation) &&
	    (oldface == orientation.face))
		goto EXIT;

	mce_log(LL_DEBUG, "Sending orientation change");
	send_device_orientation(NULL, NULL, NULL, NULL,
				G_MAXINT32, G_MAXINT32, G_MAXINT32);

EXIT:
	return status;
}


static void enable_accelerometer_polling(void)
{
	if (accelerometer_hw_present == TRUE) {
		mce_log(LL_DEBUG, "Accelerometer polling started");
		accelerometer_polling_enabled = TRUE;
		setup_accelerometer_poll_timer();
		update_orientation(FALSE);
	} else {
		mce_log(LL_DEBUG, "Accelerometer polling requested, but no supporting HW");
	}
}

static void disable_accelerometer_polling(void)
{
	mce_log(LL_DEBUG, "Accelerometer polling stopped due no listeners");
	cancel_accelerometer_poll_timer();
	accelerometer_polling_enabled = FALSE;
}

static gboolean get_device_orientation_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "Received get device orientation request");

	update_orientation(FALSE);

	if (send_device_orientation(msg,
				    NULL, NULL, NULL,
				    G_MAXINT32,
				    G_MAXINT32,
				    G_MAXINT32) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

static gboolean accelerometer_owner_monitor_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;
	const gchar *old_name;
	const gchar *new_name;
	const gchar *service;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &service,
				  DBUS_TYPE_STRING, &old_name,
				  DBUS_TYPE_STRING, &new_name,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_ERR,
			"Failed to get argument from %s.%s; %s",
			"org.freedesktop.DBus", "NameOwnerChanged",
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	mce_log(LL_DEBUG,
		"Received accelerometer name owner change for %s",
		old_name);

	if (mce_dbus_owner_monitor_remove(old_name,
					  &accelerometer_listeners) == 0)
		disable_accelerometer_polling();

	status = TRUE;

EXIT:
	return status;
}

static gboolean req_accelerometer_enable_dbus_cb(DBusMessage *const msg)
{
	gssize num;
	gboolean status = FALSE;
	const char  *sender = dbus_message_get_sender(msg);
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);

	if (sender == NULL) {
		mce_log(LL_CRIT, "No sender in enable accelerometer request");
		goto EXIT;
	}

	mce_log(LL_DEBUG,
		"Received enable accelerometer request from %s",
		sender);
	num = mce_dbus_owner_monitor_add(sender,
					 accelerometer_owner_monitor_dbus_cb,
					 &accelerometer_listeners,
				         10);

	if (num == -1) {
		mce_log(LL_INFO,
			"Failed to add name accelerometer owner "
			"monitoring for `%s'",
			sender);
	} else if (num == 1) {
		enable_accelerometer_polling();
	}

	if (no_reply == FALSE) {
		update_orientation(FALSE);

		if (send_device_orientation(msg,
					    NULL, NULL, NULL,
					    G_MAXINT32,
					    G_MAXINT32,
					    G_MAXINT32) == FALSE)
			goto EXIT;
	}

	status = TRUE;

EXIT:
	return status;
}
static gboolean req_accelerometer_disable_dbus_cb(DBusMessage *const msg)
{

	gssize num;
	gboolean status = FALSE;
	const char  *sender = dbus_message_get_sender(msg);
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);

	if (sender == NULL) {
		mce_log(LL_CRIT,
			"No sender in disable accelerometer request");
		goto EXIT;
	}

	mce_log(LL_DEBUG,
		"Received disable accelerometer request from %s",
		sender);
	num = mce_dbus_owner_monitor_remove(sender,
					    &accelerometer_listeners);

	if (num == -1) {
		mce_log(LL_INFO,
			"Failed to remove '%s' from accelerometer "
			"owner monitoring list",
			sender);
	} else if (num == 0) {
		disable_accelerometer_polling();
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

static gboolean accelerometer_poll_timer_cb(gpointer data)
{
	(void)data;

	update_and_send_orientation();

	return TRUE;
}

static void cancel_accelerometer_poll_timer(void)
{
	if (accelerometer_poll_timer_cb_id != 0) {
		g_source_remove(accelerometer_poll_timer_cb_id);
		accelerometer_poll_timer_cb_id = 0;
	}
	mce_log(LL_DEBUG, "Poll timer stopped");
}

static void setup_accelerometer_poll_timer(void)
{
	mce_log(LL_DEBUG, "accelerometer_poll_interval = %d",	accelerometer_poll_interval);
	cancel_accelerometer_poll_timer();

	if (accelerometer_poll_interval != 0) {
		accelerometer_poll_timer_cb_id =
			g_timeout_add(accelerometer_poll_interval,
				      accelerometer_poll_timer_cb,
				      NULL);
    		mce_log(LL_DEBUG, "Poll timer started, accelerometer_poll_timer_cb_id = %d", accelerometer_poll_timer_cb_id);
	}
}

static void update_accelerometer_poll_intervals(void)
{
	gint old_accelerometer_poll_interval;
	alarm_ui_state_t alarm_ui_state;
	call_state_t call_state;

	old_accelerometer_poll_interval = accelerometer_poll_interval;
	mce_log(LL_DEBUG, "old_accelerometer_poll_interval = %d",	old_accelerometer_poll_interval);
	
	alarm_ui_state = datapipe_get_gint(alarm_ui_state_pipe);
	call_state = datapipe_get_gint(call_state_pipe);

	mce_log(LL_DEBUG, "alarm_ui_state = %d",	alarm_ui_state);
	mce_log(LL_DEBUG, "call_state = %d",	call_state);

	if ((alarm_ui_state == MCE_ALARM_UI_RINGING_INT32) ||
	    (call_state == CALL_STATE_RINGING)) {
		accelerometer_poll_interval =
			ACCELEROMETER_DISPLAY_ON_POLL_FREQ;
	} else {
		switch (display_state) {
		case MCE_DISPLAY_OFF:
			accelerometer_poll_interval =
				ACCELEROMETER_DISPLAY_OFF_POLL_FREQ;
			break;

		case MCE_DISPLAY_DIM:
			accelerometer_poll_interval =
				ACCELEROMETER_DISPLAY_DIM_POLL_FREQ;
			break;

		case MCE_DISPLAY_UNDEF:
		case MCE_DISPLAY_ON:
		default:
			accelerometer_poll_interval =
				ACCELEROMETER_DISPLAY_ON_POLL_FREQ;
			break;
		}
	}

	if (accelerometer_polling_enabled == FALSE)
		goto EXIT;

	mce_log(LL_DEBUG, "accelerometer_poll_interval = %d",	accelerometer_poll_interval);
	if ((accelerometer_poll_interval != old_accelerometer_poll_interval) ||
	    (accelerometer_poll_timer_cb_id == 0))
		setup_accelerometer_poll_timer();

	if ((((old_display_state == MCE_DISPLAY_OFF) ||
	      (old_display_state == MCE_DISPLAY_UNDEF)) &&
	     ((display_state == MCE_DISPLAY_ON) ||
	      (display_state == MCE_DISPLAY_DIM))) ||
	    ((alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
	     (alarm_ui_state == MCE_ALARM_UI_RINGING_INT32))) {
		update_and_send_orientation();
	}

EXIT:
	return;
}

static void display_state_trigger(gconstpointer data)
{
	display_state = GPOINTER_TO_INT(data);
	mce_log(LL_DEBUG, "display_state = %d",	display_state);
	if(old_display_state != display_state)
	{
		update_accelerometer_poll_intervals();
		old_display_state = display_state;
	}
}

static void alarm_ui_state_trigger(gconstpointer data)
{
	(void)data;

	mce_log(LL_DEBUG, "alarm ui event");
	update_accelerometer_poll_intervals();
}

static void call_state_trigger(gconstpointer data)
{
	(void)data;

	mce_log(LL_DEBUG, "call state event");
	update_accelerometer_poll_intervals();
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;

	append_input_trigger_to_datapipe(&display_state_pipe,
					 display_state_trigger);
	append_output_trigger_to_datapipe(&alarm_ui_state_pipe,
					  alarm_ui_state_trigger);
	append_output_trigger_to_datapipe(&call_state_pipe,
					  call_state_trigger);

	accelerometer_polling_enabled = FALSE;

	if ((get_accelerometer_type() != ACCELEROMETER_TYPE_NONE) &&
	    (update_orientation(FALSE)) != FALSE) {
		accelerometer_hw_present = TRUE;
		accelerometer_poll_interval =
			ACCELEROMETER_DISPLAY_ON_POLL_FREQ;
	} else {
		accelerometer_hw_present = FALSE;
	}

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEVICE_ORIENTATION_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 get_device_orientation_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ACCELEROMETER_ENABLE_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 req_accelerometer_enable_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ACCELEROMETER_DISABLE_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 req_accelerometer_disable_dbus_cb) == NULL)
		goto EXIT;

EXIT:
	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	accelerometer_polling_enabled = FALSE;

	remove_output_trigger_from_datapipe(&call_state_pipe,
					    call_state_trigger);
	remove_output_trigger_from_datapipe(&alarm_ui_state_pipe,
					    alarm_ui_state_trigger);
	remove_input_trigger_from_datapipe(&display_state_pipe,
					   display_state_trigger);
	mce_dbus_owner_monitor_remove_all(&accelerometer_listeners);
	cancel_accelerometer_poll_timer();

	return;
}
