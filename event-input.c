/**
 * @file event-input.c
 * /dev/input event provider for the Mode Control Entity
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
#include <gio/gio.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/input.h>
#include "mce.h"
#include "event-input.h"
#include "mce-io.h"
#include "mce-log.h"
#include "datapipe.h"
#include "event-switches.h"
#include "event-input-utils.h"

guint16 power_keycode = POWER_BUTTON;

/** ID for touchscreen I/O monitor timeout source */
static guint touchscreen_io_monitor_timeout_cb_id = 0;

/** ID for keypress timeout source */
static guint keypress_repeat_timeout_cb_id = 0;

/** ID for misc timeout source */
static guint misc_io_monitor_timeout_cb_id = 0;

/** List of touchscreen input devices */
static GSList *touchscreen_dev_list = NULL;
/** List of keyboard input devices */
static GSList *keyboard_dev_list = NULL;
/** List of misc input devices */
static GSList *misc_dev_list = NULL;
/** List of switch input devices */
static GSList *switch_dev_list = NULL;

/** GFile pointer for the directory we monitor */
GFile *dev_input_gfp = NULL;
/** GFileMonitor pointer for the directory we monitor */
GFileMonitor *dev_input_gfmp = NULL;

static void update_inputdevices(const gchar *device, gboolean add);
static void remove_input_device(GSList **devices, const gchar *device);

/**
 * Wrapper function to call mce_suspend_io_monitor() from g_slist_foreach()
 *
 * @param io_monitor The I/O monitor to suspend
 * @param user_data Unused
 */
static void suspend_io_monitor(gpointer io_monitor, gpointer user_data)
{
	(void)user_data;

	mce_suspend_io_monitor(io_monitor);
}

/**
 * Wrapper function to call mce_resume_io_monitor() from g_slist_foreach()
 *
 * @param io_monitor The I/O monitor to resume
 * @param user_data Unused
 */
static void resume_io_monitor(gpointer io_monitor, gpointer user_data)
{
	(void)user_data;

	mce_resume_io_monitor(io_monitor);
}

/**
 * Wrapper function to call mce_unregister_io_monitor() from g_slist_foreach()
 *
 * @param io_monitor The I/O monitor to unregister
 * @param user_data Unused
 */
static void unregister_io_monitor(gpointer io_monitor, gpointer user_data)
{
	(void)user_data;
	mce_unregister_io_monitor(io_monitor);
}

/**
 * Timeout function for touchscreen I/O monitor reprogramming
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean touchscreen_io_monitor_timeout_cb(gpointer data)
{
	(void)data;

	touchscreen_io_monitor_timeout_cb_id = 0;

	/* Resume I/O monitors */
	if (touchscreen_dev_list != NULL) {
		g_slist_foreach(touchscreen_dev_list,
				(GFunc)resume_io_monitor, NULL);
	}

	return FALSE;
}

/**
 * Cancel timeout for touchscreen I/O monitor reprogramming
 */
static void cancel_touchscreen_io_monitor_timeout(void)
{
	if (touchscreen_io_monitor_timeout_cb_id != 0) {
		g_source_remove(touchscreen_io_monitor_timeout_cb_id);
		touchscreen_io_monitor_timeout_cb_id = 0;
	}
}

/**
 * Setup timeout for touchscreen I/O monitor reprogramming
 */
static void setup_touchscreen_io_monitor_timeout(void)
{
	cancel_touchscreen_io_monitor_timeout();

	/* Setup new timeout */
	touchscreen_io_monitor_timeout_cb_id =
		g_timeout_add_seconds(MONITORING_DELAY,
				      touchscreen_io_monitor_timeout_cb, NULL);
}

/**
 * I/O monitor callback for the touchscreen
 *
 * @param data The new data
 * @param bytes_read The number of bytes read
 */
static void touchscreen_cb(gpointer data, gsize bytes_read)
{
	submode_t submode = mce_get_submode_int32();
	struct input_event *ev;

	ev = data;

	/* Don't process invalid reads */
	if (bytes_read != sizeof (struct input_event)) {
		goto EXIT;
	}

	/* Ignore unwanted events */
	if (ev->type != EV_ABS) {
		goto EXIT;
	}

	mce_log(LL_DEBUG, "Got touchscreen event: %i,%i",ev->type, ev->code);
	
	/* Generate activity */
	mce_log(LL_DEBUG, "Setting inactive to false in %s %s %d",__FILE__, __func__, __LINE__);
	(void)execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
			       USE_INDATA, CACHE_INDATA);

	/* If visual tklock is active or autorelock isn't active,
	 * suspend I/O monitors
	 */
	if (((submode & MCE_VISUAL_TKLOCK_SUBMODE) != 0) ||
	    ((submode & MCE_AUTORELOCK_SUBMODE) == 0)) {
		if (touchscreen_dev_list != NULL) {
			g_slist_foreach(touchscreen_dev_list,
					(GFunc)suspend_io_monitor, NULL);
		}

		/* Setup a timeout I/O monitor reprogramming */
		setup_touchscreen_io_monitor_timeout();
	}

	/* Ignore non-pressure events */
	if (ev->code != ABS_PRESSURE) {
		goto EXIT;
	}

	/* For now there's no reason to cache the value,
	 * or indeed to send any kind of real value at all
	 *
	 * If the event eater is active, don't send anything
	 */
	if ((submode & MCE_EVEATER_SUBMODE) == 0) {
		(void)execute_datapipe(&touchscreen_pipe, NULL,
				       USE_INDATA, DONT_CACHE_INDATA);
	}

EXIT:
	return;
}

/**
 * Timeout function for keypress repeats
 * @note Empty function; we check the callback id
 *       for 0 to know if we've had a timeout or not
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean keypress_repeat_timeout_cb(gpointer data)
{
	(void)data;

	return FALSE;
}

/**
 * Cancel timeout for keypress repeats
 */
static void cancel_keypress_repeat_timeout(void)
{
	if (keypress_repeat_timeout_cb_id != 0) {
		g_source_remove(keypress_repeat_timeout_cb_id);
		keypress_repeat_timeout_cb_id = 0;
	}
}

/**
 * Setup timeout for touchscreen I/O monitoring
 */
static void setup_keypress_repeat_timeout(void)
{
	cancel_keypress_repeat_timeout();

	/* Setup new timeout */
	keypress_repeat_timeout_cb_id =
		g_timeout_add_seconds(MONITORING_DELAY,
				      keypress_repeat_timeout_cb, NULL);
}

/**
 * I/O monitor callback for keypresses
 *
 * @param data The new data
 * @param bytes_read The number of bytes read
 */
static void keypress_cb(gpointer data, gsize bytes_read)
{
	submode_t submode = mce_get_submode_int32();
	struct input_event *ev;

	ev = data;

	/* Don't process invalid reads */
	if (bytes_read != sizeof (struct input_event)) {
		goto EXIT;
	}

	/* Ignore non-keypress events */
	if (ev->type != EV_KEY) {
		goto EXIT;
	}

	if (ev->code == power_keycode) {
		ev->code = POWER_BUTTON;
	}

	mce_log(LL_DEBUG, "Got keyboard event: %i,%i",ev->type, ev->code);
	
	/* Generate activity:
	 * 1 - press (always)
	 * 2 - repeat (once a second)
	 */
	if ((ev->value == 0) || (ev->value == 1) ||
	    ((ev->value == 2) && (keypress_repeat_timeout_cb_id == 0))) {
		mce_log(LL_DEBUG, "send device_inactive_pipe -> FALSE");
		if (!(submode & MCE_EVEATER_SUBMODE))
		{
			mce_log(LL_DEBUG, "Setting inactive to false in %s %s %d",__FILE__, __func__, __LINE__);
			(void)execute_datapipe(&device_inactive_pipe,
					       GINT_TO_POINTER(FALSE),
					       USE_INDATA, CACHE_INDATA);
		}

		if (ev->value == 2) {
			setup_keypress_repeat_timeout();
		}
	}

	if ((ev->value == 1) || (ev->value == 0)) {
		(void)execute_datapipe(&keypress_pipe, &ev,
				       USE_INDATA, DONT_CACHE_INDATA);
	}

EXIT:
	return;
}

static void switch_call_cb(struct input_event *ev, iomon_cb callback,
			   const char *active, const char *inactive)
{
	callback((gpointer)(ev->value ? active : inactive),
		 ev->value ? strlen(active) : strlen(inactive));
}

/**
 * I/O monitor callback for switch
 *
 * @param data The new data
 * @param bytes_read The number of bytes read
 */
static void switch_cb(gpointer data, gsize bytes_read)
{
	struct input_event *ev;

	ev = data;

	/* Don't process invalid reads */
	if (bytes_read != sizeof (struct input_event)) {
		goto EXIT;
	}

	if (ev->type == EV_SW) {
		switch (ev->code) {
			case SW_KEYPAD_SLIDE: {
				switch_call_cb(ev, kbd_slide_cb,
					       MCE_KBD_SLIDE_OPEN,
					       MCE_KBD_SLIDE_CLOSED);
				goto EXIT;
			}
			case SW_CAMERA_LENS_COVER: {
				switch_call_cb(ev, camera_launch_button_cb,
					       MCE_LENS_COVER_CLOSED,
					       MCE_LENS_COVER_OPEN);

				goto EXIT;
			}
			default:
				break;
		}
	} else if (ev->type == EV_KEY) {
		switch (ev->code) {
			case KEY_SCREENLOCK: {
				switch_call_cb(ev, lockkey_cb,
					       MCE_FLICKER_KEY_ACTIVE,
					       MCE_FLICKER_KEY_INACTIVE);
				goto EXIT;
			}
			case KEY_CAMERA: {
				switch_call_cb(ev, camera_launch_button_cb,
					       MCE_CAM_LAUNCH_ACTIVE,
					       MCE_CAM_LAUNCH_INACTIVE);
				goto EXIT;
			}
			case KEY_CAMERA_FOCUS: {
				switch_call_cb(ev, generic_activity_cb,
					       MCE_CAM_FOCUS_ACTIVE,
					       MCE_CAM_FOCUS_INACTIVE);
				goto EXIT;
			}
		default:
			break;
		}
	}

EXIT:
	return;
}

/**
 * Timeout callback for misc event monitoring reprogramming
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean misc_io_monitor_timeout_cb(gpointer data)
{
	(void)data;

	misc_io_monitor_timeout_cb_id = 0;

	/* Resume I/O monitors */
	if (misc_dev_list != NULL) {
		g_slist_foreach(misc_dev_list,
				(GFunc)resume_io_monitor, NULL);
	}

	return FALSE;
}

/**
 * Cancel timeout for misc event I/O monitoring
 */
static void cancel_misc_io_monitor_timeout(void)
{
	if (misc_io_monitor_timeout_cb_id != 0) {
		g_source_remove(misc_io_monitor_timeout_cb_id);
		misc_io_monitor_timeout_cb_id = 0;
	}
}

/**
 * Setup timeout for misc event I/O monitoring
 */
static void setup_misc_io_monitor_timeout(void)
{
	cancel_misc_io_monitor_timeout();

	/* Setup new timeout */
	misc_io_monitor_timeout_cb_id =
		g_timeout_add_seconds(MONITORING_DELAY,
				      misc_io_monitor_timeout_cb, NULL);
}

/**
 * I/O monitor callback for misc /dev/input devices
 *
 * @param data Unused
 * @param bytes_read Unused
 */
static void misc_cb(gpointer data, gsize bytes_read)
{
	struct input_event *ev;

	ev = data;

	/* Don't process invalid reads */
	if (bytes_read != sizeof (struct input_event)) {
		goto EXIT;
	}
	/* ev->type for the jack sense is EV_SW */
	mce_log(LL_DEBUG, "Got misc event: %i,%i",ev->type, ev->code);

	/* Generate activity */
	mce_log(LL_DEBUG, "Setting inactive to false in %s %s %d",__FILE__, __func__, __LINE__);
	(void)execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
			       USE_INDATA, CACHE_INDATA);

	/* Suspend I/O monitors */
	if (misc_dev_list != NULL) {
		g_slist_foreach(misc_dev_list,
				(GFunc)suspend_io_monitor, NULL);
	}

	/* Setup a timeout I/O monitor reprogramming */
	setup_misc_io_monitor_timeout();

EXIT:
	return;
}

/**
 * Custom compare function used to find I/O monitor entries
 *
 * @param iomon_id An I/O monitor cookie
 * @param name The name to search for
 */
static gint iomon_name_compare(gconstpointer iomon_id,
			       gconstpointer name)
{
	const gchar *iomon_name = mce_get_io_monitor_name(iomon_id);

	return strcmp(iomon_name, name);
}

static void handle_device_error_cb(gpointer data, const gchar *device, gconstpointer iomon_id, GError *error) {
    GSList **devlist = (GSList **) data;
    (void)iomon_id;
    (void)error;
    remove_input_device(devlist, device);
}

static void register_io_monitor_chunk(const gint fd, const gchar *const file,
				 iomon_cb callback, GSList **devices)
{
	gconstpointer iomon = NULL;

	iomon = mce_register_io_monitor_chunk(fd, file,
					      MCE_IO_ERROR_POLICY_WARN, FALSE,
					      callback,
					      sizeof (struct input_event),
					      handle_device_error_cb,
					      (gpointer)devices);

	/* If we fail to register an I/O monitor,
	 * don't leak the file descriptor,
	 * and don't add the device to the list
	 */
	if (iomon == NULL) {
		if (fd != -1)
			close(fd);
	} else {
		*devices = g_slist_prepend(*devices, (gpointer)iomon);
	}
}

/**
 * Match and register I/O monitor
 */
static void match_and_register_io_monitor(const gchar *filename)
{
	int fd;
	gboolean match = FALSE;

	if ((fd = mce_match_event_file(filename, driver_blacklist)) != -1) {
		/* If the driver for the event file is blacklisted, skip it */
		close(fd);
		goto EXIT;
	} else if (strstr(filename, "event") == NULL) {
		/* Only open event* devices */
		goto EXIT;
	} else if ((fd = mce_match_event_file(filename,
					  touchscreen_event_drivers)) != -1) {
		mce_log(LL_DEBUG, "Registering %s as touchscreen fd: %i", filename, fd);
		register_io_monitor_chunk(fd, filename, touchscreen_cb,
					  &touchscreen_dev_list);
		match = TRUE;
	} else if ((fd = mce_match_event_file_by_caps(filename,
					  touch_event_types, touch_event_keys)) != -1) {
		mce_log(LL_DEBUG, "Registering %s as touchscreen fd: %i", filename, fd);
		register_io_monitor_chunk(fd, filename, touchscreen_cb,
					  &touchscreen_dev_list);
		match = TRUE;
	} else if ((fd = mce_match_event_file(filename,
					  keyboard_event_drivers)) != -1) {
		mce_log(LL_DEBUG, "Registering %s as keyboard fd: %i", filename, fd);
		register_io_monitor_chunk(fd, filename, keypress_cb,
					  &keyboard_dev_list);
		match = TRUE;
	} else if ((fd = mce_match_event_file_by_caps(filename, power_event_types,
					   power_event_keys)) != -1) {
		mce_log(LL_DEBUG, "Registering %s as keyboard fd: %i", filename, fd);
		register_io_monitor_chunk(fd, filename, keypress_cb,
					  &keyboard_dev_list);
		match = TRUE;
	}  else if ((fd = mce_match_event_file_by_caps(filename, keyboard_event_types,
					   keyboard_event_keys)) != -1) {
		mce_log(LL_DEBUG, "Registering %s as keyboard fd: %i", filename, fd);
		register_io_monitor_chunk(fd, filename, keypress_cb,
					  &keyboard_dev_list);
		match = TRUE;
	} else if ((fd = mce_match_event_file_by_caps(filename, switch_event_types,
					   switch_event_keys)) != -1) {
		mce_log(LL_DEBUG, "Registering %s as switchboard fd: %i", filename, fd);
		register_io_monitor_chunk(fd, filename, switch_cb,
					  &switch_dev_list);
		match = TRUE;
	}

	if (!match) {
		register_io_monitor_chunk(fd, filename, misc_cb,
					  &misc_dev_list);
		mce_log(LL_DEBUG, "Registering %s as misc input device fd: %i", filename, fd);
	}

EXIT:;
}

static void remove_input_device(GSList **devices, const gchar *device)
{
	gconstpointer iomon_id = NULL;
	GSList *list_entry = NULL;

	/* Try to find a matching device I/O monitor */
	list_entry = g_slist_find_custom(*devices, device,
					 iomon_name_compare);

	/* If we find one, obtain the iomon ID,
	 * remove the entry and finally unregister the I/O monitor
	 */
	if (list_entry != NULL) {
		iomon_id = list_entry->data;
		*devices = g_slist_remove(*devices, iomon_id);
		mce_unregister_io_monitor(iomon_id);
	}
}

/**
 * Update list of input devices
 * Remove the I/O monitor for the specified device (if existing)
 * and (re)open it if available
 *
 * @param device The device to add/remove
 * @param add TRUE if the device was added, FALSE if it was removed
 * @return TRUE on success, FALSE on failure
 */
static void update_inputdevices(const gchar *device, gboolean add)
{
	/* Try to find a matching touchscreen I/O monitor */
	remove_input_device(&touchscreen_dev_list, device);

	/* Try to find a matching keyboard I/O monitor */
	remove_input_device(&keyboard_dev_list, device);

	/* Try to find a matching switch I/O monitor */
	remove_input_device(&switch_dev_list, device);

	/* Try to find a matching misc I/O monitor */
	remove_input_device(&misc_dev_list, device);

	if (add == TRUE)
		match_and_register_io_monitor(device);
}

/**
 * Unregister monitors for touchscreen devices allocated by mce_scan_inputdevices
 */
static void unregister_touchscreen_devices(void) {
	if (touchscreen_dev_list != NULL) {
		mce_log(LL_DEBUG, "event-input: unbinding %u touchscreen devices", g_slist_length(touchscreen_dev_list));
		g_slist_foreach(touchscreen_dev_list, (GFunc)unregister_io_monitor, NULL);
		g_slist_free(touchscreen_dev_list);
		touchscreen_dev_list = NULL;
	}
}


/**
 * Unregister monitors for input devices allocated by mce_scan_inputdevices
 */
static void unregister_inputdevices(void)
{
	if (touchscreen_dev_list != NULL) {
		g_slist_foreach(touchscreen_dev_list,
				(GFunc)unregister_io_monitor, NULL);
		g_slist_free(touchscreen_dev_list);
		touchscreen_dev_list = NULL;
	}

	if (keyboard_dev_list != NULL) {
		g_slist_foreach(keyboard_dev_list,
				(GFunc)unregister_io_monitor, NULL);
		g_slist_free(keyboard_dev_list);
		keyboard_dev_list = NULL;
	}

	if (switch_dev_list != NULL) {
		g_slist_foreach(switch_dev_list,
				(GFunc)unregister_io_monitor, NULL);
		g_slist_free(switch_dev_list);
		switch_dev_list = NULL;
	}

	if (misc_dev_list != NULL) {
		g_slist_foreach(misc_dev_list,
				(GFunc)unregister_io_monitor, NULL);
		g_slist_free(misc_dev_list);
		misc_dev_list = NULL;
	}
}

/**
 * Callback for directory changes
 *
 * @param monitor Unused
 * @param file The file that changed
 * @param other_file Unused
 * @param event_type The event that occured
 * @param user_data Unused
 */
static void dir_changed_cb(GFileMonitor *monitor,
			   GFile *file, GFile *other_file,
			   GFileMonitorEvent event_type, gpointer user_data)
{
	(void)monitor;
	(void)other_file;
	(void)user_data;

	switch (event_type) {
	case G_FILE_MONITOR_EVENT_CREATED:
		if (g_file_query_file_type(file,
					   G_FILE_QUERY_INFO_NONE,
					   NULL) == G_FILE_TYPE_SPECIAL) {
			update_inputdevices(g_file_get_path(file), TRUE);
		}
		break;

	case G_FILE_MONITOR_EVENT_DELETED:
		if (g_file_query_file_type(file,
					   G_FILE_QUERY_INFO_NONE,
					   NULL) == G_FILE_TYPE_SPECIAL) {
			update_inputdevices(g_file_get_path(file), FALSE);
		}
		break;

	default:
		break;
	}
}

static void match_ts_only(const gchar* filename) {
	int fd;

	if ((fd = mce_match_event_file(filename, driver_blacklist)) != -1) {
		/* If the driver for the event file is blacklisted, skip it */
		close(fd);
		return;
	}

	if ((fd = mce_match_event_file(filename,
					  touchscreen_event_drivers)) != -1) {
		register_io_monitor_chunk(fd, filename, touchscreen_cb,
					  &touchscreen_dev_list);
		mce_log(LL_DEBUG, "Registering %s as touchscreen fd: %i", filename, fd);
		return;
	} else if ((fd = mce_match_event_file_by_caps(filename,
					  touch_event_types, touch_event_keys)) != -1) {
		register_io_monitor_chunk(fd, filename, touchscreen_cb,
					  &touchscreen_dev_list);
		mce_log(LL_DEBUG, "Registering %s as touchscreen fd: %i", filename, fd);
		return;
	}

	close(fd);
}

static void mce_reopen_touchscreen_devices(void) {
	if (touchscreen_dev_list == NULL)
		mce_scan_inputdevices(match_ts_only);
}


static void touchscreen_control_trigger(gconstpointer data) {
	gboolean enable = !GPOINTER_TO_INT(data);
	if (enable)
		mce_reopen_touchscreen_devices();
	else
		unregister_touchscreen_devices();
}

/**
 * Init function for the /dev/input event component
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_input_init(void)
{
	GError *error = NULL;
	gboolean status = FALSE;

#if !GLIB_CHECK_VERSION(2,35,0)
	g_type_init ();
#endif

	/* Retrieve a GFile pointer to the directory to monitor */
	dev_input_gfp = g_file_new_for_path(DEV_INPUT_PATH);

	/* Monitor the directory */
	if ((dev_input_gfmp = g_file_monitor_directory(dev_input_gfp,
						       G_FILE_MONITOR_NONE,
						       NULL, &error)) == NULL) {
		mce_log(LL_ERR,
			"Failed to add monitor for directory `%s'; %s",
			DEV_INPUT_PATH, error->message);
		goto EXIT;
	}

	/* XXX: There is a race condition here; if a file (dis)appears
	 *      after this scan, but before we start monitoring,
	 *      then we'll miss that device.  The race is miniscule though,
	 *      and any workarounds are likely to be cumbersome
	 */
	/* Find the initial set of input devices */
	if ((status = mce_scan_inputdevices(match_and_register_io_monitor)) == FALSE) {
		g_file_monitor_cancel(dev_input_gfmp);
		dev_input_gfmp = NULL;
		goto EXIT;
	}

	/* Connect "changed" signal for the directory monitor */
	g_signal_connect(G_OBJECT(dev_input_gfmp), "changed",
			 G_CALLBACK(dir_changed_cb), NULL);

	append_output_trigger_to_datapipe(&touchscreen_suspend_pipe,
					touchscreen_control_trigger);

EXIT:
	g_clear_error(&error);

	return status;
}

/**
 * Exit function for the /dev/input event component
 */
void mce_input_exit(void)
{
	if (dev_input_gfmp != NULL)
		g_file_monitor_cancel(dev_input_gfmp);

	remove_output_trigger_from_datapipe(&touchscreen_suspend_pipe,
					touchscreen_control_trigger);

	unregister_inputdevices();

	/* Remove all timer sources */
	cancel_touchscreen_io_monitor_timeout();
	cancel_keypress_repeat_timeout();
	cancel_misc_io_monitor_timeout();

	return;
}
