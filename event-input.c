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
	/* If we opened an fd to monitor, retrieve it to ensure
	 * that we can close it after unregistering the I/O monitor
	 */
	int fd = mce_get_io_monitor_fd(io_monitor);

	(void)user_data;

	mce_unregister_io_monitor(io_monitor);

	/* Close the fd if there is one */
	if (fd != -1)
		close(fd);
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

	/* Generate activity */
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

	/* Generate activity:
	 * 1 - press (always)
	 * 2 - repeat (once a second)
	 */
	if ((ev->value == 0) || (ev->value == 1) ||
	    ((ev->value == 2) && (keypress_repeat_timeout_cb_id == 0))) {
		mce_log(LL_DEBUG, "send device_inactive_pipe -> FALSE");
		if (!(submode & MCE_EVEATER_SUBMODE))
		{
			mce_log(LL_DEBUG, "send device_inactive_pipe -> FALSE");
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
				kbd_slide_cb((gpointer)(ev->value ? MCE_KBD_SLIDE_OPEN : MCE_KBD_SLIDE_CLOSED),
					     ev->value ? sizeof(MCE_KBD_SLIDE_OPEN) : sizeof(MCE_KBD_SLIDE_CLOSED));
				goto EXIT;
			}
			case SW_FRONT_PROXIMITY: {
				proximity_sensor_cb((gpointer)(ev->value ? MCE_PROXIMITY_SENSOR_CLOSED : MCE_PROXIMITY_SENSOR_OPEN),
					     ev->value ? sizeof(MCE_PROXIMITY_SENSOR_CLOSED) : sizeof(MCE_PROXIMITY_SENSOR_OPEN));
				goto EXIT;
			}
			case SW_CAMERA_LENS_COVER: {
				camera_launch_button_cb((gpointer)(ev->value ? MCE_CAM_LAUNCH_ACTIVE : MCE_CAM_LAUNCH_INACTIVE),
					     ev->value ? sizeof(MCE_CAM_LAUNCH_ACTIVE) : sizeof(MCE_CAM_LAUNCH_INACTIVE));
				goto EXIT;
			}
			default:
				break;
		}
	} else if (ev->type == EV_KEY) {
		switch (ev->code) {
			case KEY_SCREENLOCK: {
				lockkey_cb((gpointer)(ev->value ? MCE_FLICKER_KEY_ACTIVE :MCE_FLICKER_KEY_INACTIVE),
					   ev->value ? sizeof(MCE_FLICKER_KEY_ACTIVE) : sizeof(MCE_FLICKER_KEY_INACTIVE));
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
	mce_log(LL_DEBUG, "ev->type: %d", ev->type);

	/* Generate activity */
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

static int match_event_file_by_caps(const gchar *const filename,
				    const int *const ev_types,
				    const int *const ev_keys[])
{
	int ev_type, p, q;
	int version;
	unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
	int fd;

	if ((fd = open(filename, O_NONBLOCK | O_RDONLY)) == -1) {
		mce_log(LL_DEBUG, "Failed to open `%s', skipping", filename);

		/* Ignore error */
		errno = 0;
		return -1;
	}

	/* We use this ioctl to check if this device supports the input
	 * ioctl's
	*/
	if (ioctl(fd, EVIOCGVERSION, &version) < 0) {
		mce_log(LL_WARN,
			"match_event_file_by_caps: can't get version on `%s'",
			filename);
		goto EXIT;
	}

	memset(bit, 0, sizeof(bit));
	if (ioctl(fd, EVIOCGBIT(0, EV_MAX), bit[0]) < 0) {
		mce_log(LL_WARN,
			"match_event_file_by_caps: ioctl(EVIOCGBIT, EV_MAX) failed on `%s'",
			filename);
		goto EXIT;
	}

	for (p = 0; ev_types[p] != -1; p ++) {
		/* TODO: Could check that ev_types[p] is less than EV_MAX */
		ev_type = ev_types[p];

		/* event type not supported, try the next one */
		if (!test_bit(ev_type, bit[0]))
			continue;

		/* Get bits per event type */
		if (ioctl(fd, EVIOCGBIT(ev_type, KEY_MAX), bit[ev_type]) < 0) {
			mce_log(LL_WARN,
				"match_event_file_by_caps: ioctl(EVIOCGBIT, KEY_MAX) failed on `%s'",
				filename);
			goto EXIT;
		}

		for (q = 0; ev_keys[p][q] != -1; q ++) {
			/* TODO: Could check that q is less than KEY_MAX */

			/* succeed if at least one match is found */
			if (test_bit(ev_keys[p][q], bit[ev_type])) {
				mce_log(LL_DEBUG,
					"match_event_file_by_caps: match found on `%s'", filename);
				return fd;
			}
		}
	}

EXIT:
	close(fd);

	return -1;
}

/**
 * Try to match /dev/input event file to a specific driver
 *
 * @param filename A string containing the name of the event file
 * @param drivers An array of driver names
 * @return An open file descriptor on success, -1 on failure
 */
static int match_event_file(const gchar *const filename,
			    const gchar *const *const drivers)
{
	static char name[256];
	int fd = -1;
	int i;

	/* If we cannot open the file, abort */
	if ((fd = open(filename, O_NONBLOCK | O_RDONLY)) == -1) {
		mce_log(LL_DEBUG, "Failed to open `%s', skipping",
			filename);

		/* Ignore error */
		errno = 0;
		goto EXIT;
	}

	for (i = 0; drivers[i] != NULL; i++) {
		if (ioctl(fd, EVIOCGNAME(sizeof name), name) >= 0) {
			if (strcmp(name, drivers[i]) == 0) {
				/* We found our event file */
				mce_log(LL_DEBUG,
					"`%s' is `%s'",
					filename, drivers[i]);
				break;
			}
		} else {
			mce_log(LL_WARN,
				"ioctl(EVIOCGNAME) failed on `%s'",
				filename);
		}
	}

	/* If the scan terminated with drivers[i] == NULL,
	 * we didn't find any match
	 */
	if (drivers[i] == NULL) {
		close(fd);
		fd = -1;
	}

EXIT:
	return fd;
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

static void register_io_monitor_chunk(const gint fd, const gchar *const file,
				 iomon_cb callback, GSList *devices)
{
	gconstpointer iomon = NULL;

	iomon = mce_register_io_monitor_chunk(fd, file,
					      MCE_IO_ERROR_POLICY_WARN, FALSE,
					      callback,
					      sizeof (struct input_event));

	/* If we fail to register an I/O monitor,
	 * don't leak the file descriptor,
	 * and don't add the device to the list
	 */
	if (iomon == NULL) {
		if (fd != -1)
			close(fd);
	} else {
		devices = g_slist_prepend(devices, (gpointer)iomon);
	}
}

/**
 * Match and register I/O monitor
 */
static void match_and_register_io_monitor(const gchar *filename)
{
	int fd;
	gboolean match = FALSE;

	if ((fd = match_event_file(filename, driver_blacklist)) != -1) {
		/* If the driver for the event file is blacklisted, skip it */
		close(fd);
		goto EXIT;
	} else if ((fd = match_event_file(filename,
					  touchscreen_event_drivers)) != -1) {
		register_io_monitor_chunk(fd, filename, touchscreen_cb,
					  touchscreen_dev_list);
		match = TRUE;
	} else if ((fd = match_event_file(filename,
					  keyboard_event_drivers)) != -1) {
		register_io_monitor_chunk(fd, filename, keypress_cb,
					  keyboard_dev_list);
		match = TRUE;
	}

	if ((fd = match_event_file_by_caps(filename, switch_event_types,
					   switch_event_keys)) != -1) {
		register_io_monitor_chunk(fd, filename, switch_cb,
					  switch_dev_list);
		match = TRUE;
	}

	if (!match) {
		register_io_monitor_chunk(fd, filename, misc_cb,
					  misc_dev_list);
	}

EXIT:;
}

static void remove_input_device(GSList *devices, const gchar *device)
{
	gconstpointer iomon_id = NULL;
	GSList *list_entry = NULL;

	/* Try to find a matching device I/O monitor */
	list_entry = g_slist_find_custom(devices, device,
					 iomon_name_compare);

	/* If we find one, obtain the iomon ID,
	 * remove the entry and finally unregister the I/O monitor
	 */
	if (list_entry != NULL) {
		iomon_id = list_entry->data;
		devices = g_slist_remove(devices, iomon_id);
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
	remove_input_device(touchscreen_dev_list, device);

	/* Try to find a matching keyboard I/O monitor */
	remove_input_device(keyboard_dev_list, device);

	/* Try to find a matching switch I/O monitor */
	remove_input_device(switch_dev_list, device);

	/* Try to find a matching misc I/O monitor */
	remove_input_device(misc_dev_list, device);

	if (add == TRUE)
		match_and_register_io_monitor(device);
}

/**
 * Scan /dev/input for input event devices
 * @return TRUE on success, FALSE on failure
 */
static gboolean scan_inputdevices(void)
{
	DIR *dir = NULL;
	struct dirent *direntry = NULL;
	gboolean status = FALSE;

	if ((dir = opendir(DEV_INPUT_PATH)) == NULL) {
		mce_log(LL_ERR, "opendir() failed; %s", g_strerror(errno));
		errno = 0;
		goto EXIT;
	}

	for (direntry = readdir(dir);
	     (direntry != NULL && telldir(dir));
	     direntry = readdir(dir)) {
		gchar *filename = NULL;

		if (strncmp(direntry->d_name, EVENT_FILE_PREFIX,
			    strlen(EVENT_FILE_PREFIX)) != 0) {
			mce_log(LL_DEBUG,
				"`%s/%s' skipped",
				DEV_INPUT_PATH,
				direntry->d_name);
			continue;
		}

		filename = g_strconcat(DEV_INPUT_PATH, "/",
				       direntry->d_name, NULL);
		match_and_register_io_monitor(filename);
		g_free(filename);
	}

	/* Report, but ignore, errors when closing directory */
	if (closedir(dir) == -1) {
		mce_log(LL_ERR,
			"closedir() failed; %s", g_strerror(errno));
		errno = 0;
	}

	status = TRUE;

EXIT:
	return status;
}

/**
 * Unregister monitors for input devices allocated by scan_inputdevices
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
		update_inputdevices(g_file_get_path(file), TRUE);
		break;

	case G_FILE_MONITOR_EVENT_DELETED:
		update_inputdevices(g_file_get_path(file), FALSE);
		break;

	default:
		break;
	}
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

	g_type_init();

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
	if ((status = scan_inputdevices()) == FALSE) {
		g_file_monitor_cancel(dev_input_gfmp);
		dev_input_gfmp = NULL;
		goto EXIT;
	}

	/* Connect "changed" signal for the directory monitor */
	g_signal_connect(G_OBJECT(dev_input_gfmp), "changed",
			 G_CALLBACK(dir_changed_cb), NULL);

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

	unregister_inputdevices();

	/* Remove all timer sources */
	cancel_touchscreen_io_monitor_timeout();
	cancel_keypress_repeat_timeout();
	cancel_misc_io_monitor_timeout();

	return;
}
