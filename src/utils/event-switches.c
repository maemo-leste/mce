/**
 * @file event-switches.c
 * Switch event provider for the Mode Control Entity
 * <p>
 * Copyright © 2007-2010 Nokia Corporation and/or its subsidiary(-ies).
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
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <stdlib.h>

#include "mce.h"
#include "event-switches.h"
#include "mce-io.h"
#include "datapipe.h"

/** ID for the lockkey I/O monitor */
static gconstpointer lockkey_iomon_id = NULL;

/** ID for the keyboard slide I/O monitor */
static gconstpointer kbd_slide_iomon_id = NULL;

/** ID for the cam focus I/O monitor */
static gconstpointer cam_focus_iomon_id = NULL;

/** ID for the cam launch I/O monitor */
static gconstpointer cam_launch_iomon_id = NULL;

/** ID for the lid cover I/O monitor */
static gconstpointer lid_cover_iomon_id = NULL;

static gconstpointer tahvo_usb_cable_iomon_id = NULL;

/** ID for the MUSB OMAP3 usb cable I/O monitor */
static gconstpointer musb_omap3_usb_cable_iomon_id = NULL;

/** ID for the mmc0 cover I/O monitor */
static gconstpointer mmc0_cover_iomon_id = NULL;

/** ID for the mmc cover I/O monitor */
static gconstpointer mmc_cover_iomon_id = NULL;

/** ID for the lens cover I/O monitor */
static gconstpointer lens_cover_iomon_id = NULL;

/** ID for the battery cover I/O monitor */
static gconstpointer bat_cover_iomon_id = NULL;

/** Does the device have a flicker key? */
gboolean has_flicker_key = FALSE;

/**
 * Generic I/O monitor callback that only generates activity
 *
 * @param data Unused
 * @param bytes_read Unused
 */
void generic_activity_cb(gpointer data, gsize bytes_read)
{
	(void)data;
	(void)bytes_read;

	/* Generate activity */
	(void)execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
			       USE_INDATA, CACHE_INDATA);
}

/**
 * I/O monitor callback for the camera launch button
 *
 * @param data Unused
 * @param bytes_read Unused
 */
void camera_launch_button_cb(gpointer data, gsize bytes_read)
{
	camera_button_state_t camera_button_state;

	(void)bytes_read;

	if (!strncmp(data, MCE_CAM_LAUNCH_ACTIVE,
		     strlen(MCE_CAM_LAUNCH_ACTIVE))) {
		camera_button_state = CAMERA_BUTTON_LAUNCH;
	} else {
		camera_button_state = CAMERA_BUTTON_UNPRESSED;
	}

	/* Generate activity */
	(void)execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
			       USE_INDATA, CACHE_INDATA);

	/* Update camera button state */
	(void)execute_datapipe(&camera_button_pipe,
			       GINT_TO_POINTER(camera_button_state),
			       USE_INDATA, CACHE_INDATA);
}

/**
 * I/O monitor callback for the lock flicker key
 *
 * @param data The new data
 * @param bytes_read Unused
 */
void lockkey_cb(gpointer data, gsize bytes_read)
{
	gint lockkey_state;

	(void)bytes_read;

	if (!strncmp(data, MCE_FLICKER_KEY_ACTIVE,
		     strlen(MCE_FLICKER_KEY_ACTIVE))) {
		lockkey_state = 1;
	} else {
		lockkey_state = 0;
	}

	(void)execute_datapipe(&lockkey_pipe,
			       GINT_TO_POINTER(lockkey_state),
			       USE_INDATA, CACHE_INDATA);
}

/**
 * I/O monitor callback for the keyboard slide
 *
 * @param data The new data
 * @param bytes_read Unused
 */
void kbd_slide_cb(gpointer data, gsize bytes_read)
{
	cover_state_t slide_state;

	(void)bytes_read;

	if (!strncmp(data, MCE_KBD_SLIDE_OPEN, strlen(MCE_KBD_SLIDE_OPEN))) {
		slide_state = COVER_OPEN;

		/* Generate activity */
		if ((mce_get_submode_int32() & MCE_EVEATER_SUBMODE) == 0) {
			(void)execute_datapipe(&device_inactive_pipe,
					       GINT_TO_POINTER(FALSE),
					       USE_INDATA, CACHE_INDATA);
		}
	} else {
		slide_state = COVER_CLOSED;
	}

	(void)execute_datapipe(&keyboard_slide_pipe,
			       GINT_TO_POINTER(slide_state),
			       USE_INDATA, CACHE_INDATA);
}

/**
 * I/O monitor callback for the lid cover
 *
 * @param data The new data
 * @param bytes_read Unused
 */
static void lid_cover_cb(gpointer data, gsize bytes_read)
{
	cover_state_t lid_cover_state;

	(void)bytes_read;

	if (!strncmp(data, MCE_LID_COVER_OPEN, strlen(MCE_LID_COVER_OPEN))) {
		lid_cover_state = COVER_OPEN;

		/* Generate activity */
		(void)execute_datapipe(&device_inactive_pipe,
				       GINT_TO_POINTER(FALSE),
				       USE_INDATA, CACHE_INDATA);
	} else {
		lid_cover_state = COVER_CLOSED;
	}

	(void)execute_datapipe(&lid_cover_pipe,
			       GINT_TO_POINTER(lid_cover_state),
			       USE_INDATA, CACHE_INDATA);
}

/**
 * I/O monitor callback for the USB cable
 *
 * @param data The new data
 * @param bytes_read Unused
 */
static void usb_cable_cb(gpointer data, gsize bytes_read)
{
	usb_cable_state_t cable_state;

	(void)bytes_read;

	if (!strncmp(data, MCE_TAHVO_USB_CABLE_CONNECTED,
		     strlen(MCE_TAHVO_USB_CABLE_CONNECTED)) ||
	    !strncmp(data, MCE_MUSB_USB_CABLE_CONNECTED,
		     strlen(MCE_MUSB_USB_CABLE_CONNECTED)) ||
	    !strncmp(data, MCE_MUSB_OMAP3_USB_CABLE_CONNECTED,
		     strlen(MCE_MUSB_OMAP3_USB_CABLE_CONNECTED))) {
		cable_state = USB_CABLE_CONNECTED;
	} else {
		cable_state = USB_CABLE_DISCONNECTED;
	}

	/* Generate activity */
	if ((mce_get_submode_int32() & MCE_EVEATER_SUBMODE) == 0) {
		(void)execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
				       USE_INDATA, CACHE_INDATA);
	}

	(void)execute_datapipe(&usb_cable_pipe,
			       GINT_TO_POINTER(cable_state),
			       USE_INDATA, CACHE_INDATA);
}

/**
 * I/O monitor callback for the lens cover
 *
 * @param data The new data
 * @param bytes_read Unused
 */
void lens_cover_cb(gpointer data, gsize bytes_read)
{
	cover_state_t lens_cover_state;

	(void)bytes_read;

	if (!strncmp(data, MCE_LENS_COVER_OPEN, strlen(MCE_LENS_COVER_OPEN))) {
		lens_cover_state = COVER_OPEN;
	} else {
		lens_cover_state = COVER_CLOSED;
	}

	/* Generate activity */
	if ((mce_get_submode_int32() & MCE_EVEATER_SUBMODE) == 0) {
		(void)execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
				       USE_INDATA, CACHE_INDATA);
	}

	(void)execute_datapipe(&lens_cover_pipe,
			       GINT_TO_POINTER(lens_cover_state),
			       USE_INDATA, CACHE_INDATA);
}

static void gpio_keys_foreach (gpointer data, gpointer user_data)
{
	gchar **s = (gchar **)user_data;
	gchar *tmp = *s, *key = g_strdup_printf("%d", GPOINTER_TO_INT(data));

	*s = g_strconcat(tmp, ",", key, NULL);
	g_free(key);
	g_free(tmp);
}

static gboolean gpio_keys_enable_switch(int type, gint key, gboolean disable)
{
	const gchar *path = ((type == EV_KEY) ?
				     MCE_GPIO_KEYS_DISABLED_KEYS_PATH :
				     MCE_GPIO_KEYS_DISABLED_SWITCHES_PATH);
	gchar *s, **array;
	GSList *l = NULL;
	gboolean rv;
	gpointer tmp;

	if (access(path, F_OK) == -1) {
		return FALSE;
	}

	if (!mce_read_string_from_file(path, &s)) {
		return FALSE;
	}

	s = g_strstrip(s);
	array = g_strsplit(s, ",", -1);
	g_free(s);

	if (array) {
		gchar **p, *c;

		for (p = array; *p; p ++) {
			if ((c = g_strrstr(*p, "-"))) {
				/* We have a range, expand it */
				gint i, j;

				*c = 0;
				j = atoi(c + 1) + 1;

				for (i = atoi(*p); i < j; i++) {
					l = g_slist_append(l, GINT_TO_POINTER(i));
				}
			} else {
				gint t = atoi(*p);
				l = g_slist_append(l, GINT_TO_POINTER(t));
			}
		}
	}

	g_strfreev(array);

	if (disable) {
		if (l && g_slist_find(l, GINT_TO_POINTER(key))) {
			/* Already disabled */
			g_slist_free(l);
			return TRUE;
		}

		l = g_slist_append(l, GINT_TO_POINTER(key));
	} else {
		if (!l || !g_slist_find(l, GINT_TO_POINTER(key))) {
			/* Already enabled */
			g_slist_free(l);
			return TRUE;
		}

		l = g_slist_remove(l, GINT_TO_POINTER(key));
	}

	if (l) {
		tmp = g_slist_nth_data(l, 0);
		s = g_strdup_printf("%d", GPOINTER_TO_INT(tmp));
		l = g_slist_remove(l, tmp);
		g_slist_foreach(l, gpio_keys_foreach, &s);
		g_slist_free(l);
	} else {
		s = g_strdup("");
	}

	g_strlcat(s, "\n", -1);
	rv = mce_write_string_to_file(path, s);

	g_free(s);

	return rv;
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

	if ((submode & MCE_TKLOCK_SUBMODE) != 0) {
		if ((old_submode & MCE_TKLOCK_SUBMODE) == 0) {
			mce_write_string_to_file(MCE_CAM_FOCUS_DISABLE_PATH, "1");
			mce_write_string_to_file(MCE_CAM_LAUNCH_DISABLE_PATH, "1");
			gpio_keys_enable_switch(EV_KEY, KEY_CAMERA, TRUE);
			gpio_keys_enable_switch(EV_KEY, KEY_CAMERA_FOCUS, TRUE);
		}
	} else {
		if ((old_submode & MCE_TKLOCK_SUBMODE) != 0) {
			mce_write_string_to_file(MCE_CAM_LAUNCH_DISABLE_PATH, "0");
			mce_write_string_to_file(MCE_CAM_FOCUS_DISABLE_PATH, "0");
			gpio_keys_enable_switch(EV_KEY, KEY_CAMERA, FALSE);
			gpio_keys_enable_switch(EV_KEY, KEY_CAMERA_FOCUS, FALSE);
		}
	}

	old_submode = submode;
}

static void handle_device_error_cb(gpointer data, const gchar *device, gconstpointer iomon_id, GError *error) {
    (void)data;
    (void)device;
    (void)error;

    mce_unregister_io_monitor(iomon_id);
}

/**
 * Init function for the switches component
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_switches_init(void)
{
	gboolean status = FALSE;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&submode_pipe,
					  submode_trigger);

	/* Set default values, in case these are not available */
	(void)execute_datapipe(&lid_cover_pipe,
			       GINT_TO_POINTER(COVER_OPEN),
			       USE_INDATA, CACHE_INDATA);

	/* Register I/O monitors */
	// FIXME: error handling?
	lockkey_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_FLICKER_KEY_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, lockkey_cb, handle_device_error_cb, NULL);
	kbd_slide_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_KBD_SLIDE_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, kbd_slide_cb, handle_device_error_cb, NULL);
	cam_focus_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_CAM_FOCUS_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, generic_activity_cb, handle_device_error_cb, NULL);
	cam_launch_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_CAM_LAUNCH_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, camera_launch_button_cb, handle_device_error_cb, NULL);
	lid_cover_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_LID_COVER_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, lid_cover_cb, handle_device_error_cb, NULL);
	musb_omap3_usb_cable_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_MUSB_OMAP3_USB_CABLE_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, usb_cable_cb, handle_device_error_cb, NULL);

	tahvo_usb_cable_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_TAHVO_USB_CABLE_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, usb_cable_cb, handle_device_error_cb, NULL);
	lens_cover_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_LENS_COVER_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, lens_cover_cb, handle_device_error_cb, NULL);
	mmc0_cover_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_MMC0_COVER_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, generic_activity_cb, handle_device_error_cb, NULL);
	mmc_cover_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_MMC_COVER_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, generic_activity_cb, handle_device_error_cb, NULL);
	bat_cover_iomon_id =
		mce_register_io_monitor_string(-1,
					       MCE_BATTERY_COVER_STATE_PATH,
					       MCE_IO_ERROR_POLICY_IGNORE,
					       TRUE, generic_activity_cb, handle_device_error_cb, NULL);

	if (lockkey_iomon_id != NULL)
		has_flicker_key = TRUE;

	status = TRUE;

	return status;
}

/**
 * Exit function for the switches component
 */
void mce_switches_exit(void)
{
	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&submode_pipe,
					    submode_trigger);

	/* Unregister I/O monitors */
	mce_unregister_io_monitor(bat_cover_iomon_id);
	mce_unregister_io_monitor(mmc_cover_iomon_id);
	mce_unregister_io_monitor(mmc0_cover_iomon_id);
	mce_unregister_io_monitor(lens_cover_iomon_id);
	mce_unregister_io_monitor(tahvo_usb_cable_iomon_id);
	mce_unregister_io_monitor(lid_cover_iomon_id);
	mce_unregister_io_monitor(cam_launch_iomon_id);
	mce_unregister_io_monitor(cam_focus_iomon_id);
	mce_unregister_io_monitor(kbd_slide_iomon_id);
	mce_unregister_io_monitor(lockkey_iomon_id);

	return;
}
