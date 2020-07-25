/**
 * @file display.c
 * Display module -- this implements display handling for MCE
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
#include <gmodule.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <mce/mode-names.h>
#include "mce.h"
#include "display.h"
#include "mce-io.h"
#include "mce-lib.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "mce-gconf.h"
#include "datapipe.h"
#include "x11-utils.h"

/** Module name */
#define MODULE_NAME		"display"

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

/** GConf callback ID for display brightness setting */
static guint disp_brightness_gconf_cb_id = 0;

/** Display dimming timeout setting */
static gint disp_dim_timeout = DEFAULT_DIM_TIMEOUT;
/** GConf callback ID for display dimming timeout setting */
static guint disp_dim_timeout_gconf_cb_id = 0;

/** Display blanking timeout setting */
static gint disp_blank_timeout = DEFAULT_BLANK_TIMEOUT;
/** GConf callback ID for display blanking timeout setting */
static guint disp_blank_timeout_gconf_cb_id = 0;

/** ID for display blank prevention timer source */
static guint blank_prevent_timeout_cb_id = 0;

/** Display blank prevention timer */
static gint blank_prevent_timeout = BLANK_PREVENT_TIMEOUT;

/** Bootup dim additional timeout */
static gint bootup_dim_additional_timeout = 0;

static gint enable_power_saving = DEFAULT_ENABLE_POWER_SAVING;
static guint enable_power_saving_gconf_cb_id = 0;

/** Cached brightness */
static gint cached_brightness = -1;

/** Target brightness */
static gint target_brightness = -1;

static gint set_brightness = -1;

static const gchar *cabc_mode = CABC_MODE_DEFAULT;

/** Fadeout step length */
static gint brightness_fade_steplength = 2;

/** Brightness fade timeout callback ID */
static gint brightness_fade_timeout_cb_id = 0;
/** Display dimming timeout callback ID */
static gint dim_timeout_cb_id = 0;
/** Display blanking timeout callback ID */
static gint blank_timeout_cb_id = 0;

/** Charger state */
static gboolean charger_connected = FALSE;

/** Maximum display brightness */
static gint maximum_display_brightness = DEFAULT_MAXIMUM_DISPLAY_BRIGHTNESS;

static gchar *brightness_file = NULL;
static gchar *max_brightness_file = NULL;
static gchar *cabc_mode_file = NULL;
static gchar *cabc_available_modes_file = NULL;
static gboolean hw_display_fading = FALSE;

static gboolean is_tvout_state_changed = FALSE;

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
static inhibit_t blanking_inhibit_mode = DEFAULT_BLANKING_INHIBIT_MODE;
/** GConf callback ID for display blanking inhibit mode setting */
static guint blanking_inhibit_mode_gconf_cb_id = 0;

/** Blanking inhibited */
static gboolean blanking_inhibited = FALSE;
/** Dimming inhibited */
static gboolean dimming_inhibited = FALSE;

/** List of monitored blanking pause requesters */
static GSList *blanking_pause_monitor_list = NULL;

/** List of monitored CABC mode requesters */
static GSList *cabc_mode_monitor_list = NULL;

static void update_blanking_inhibit(gboolean timed_inhibit);
static void cancel_dim_timeout(void);

/** Display type */
typedef enum {
	/** Display type unset */
	DISPLAY_TYPE_UNSET = -1,
	/** No display available; XXX should never happen */
	DISPLAY_TYPE_NONE = 0,
	/** Generic display interface without CABC */
	DISPLAY_TYPE_GENERIC = 1,
	/** EID l4f00311 with CABC */
	DISPLAY_TYPE_L4F00311 = 2,
	/** Sony acx565akm with CABC */
	DISPLAY_TYPE_ACX565AKM = 3,
} display_type_t;

/**
 * CABC mapping; D-Bus API modes vs SysFS mode
 */
typedef struct {
	const gchar *const dbus;
	const gchar *const sysfs;
	gboolean available;
} cabc_mode_mapping_t;


/**
 * CABC mappings; D-Bus API modes vs SysFS mode
 */
cabc_mode_mapping_t cabc_mode_mapping[] = {
	{
		.dbus = MCE_CABC_MODE_OFF,
		.sysfs = CABC_MODE_OFF,
		.available = FALSE
	}, {
		.dbus = MCE_CABC_MODE_UI,
		.sysfs = CABC_MODE_UI,
		.available = FALSE
	}, {
		.dbus = MCE_CABC_MODE_STILL_IMAGE,
		.sysfs = CABC_MODE_STILL_IMAGE,
		.available = FALSE
	}, {
		.dbus = MCE_CABC_MODE_MOVING_IMAGE,
		.sysfs = CABC_MODE_MOVING_IMAGE,
		.available = FALSE
	}, {
		.dbus = NULL,
		.sysfs = NULL,
		.available = FALSE
	}
};

/**
 * Get the display type
 *
 * @return The display type
 */
static display_type_t get_display_type(void)
{
	static display_type_t display_type = DISPLAY_TYPE_UNSET;
        gchar *bright_file, *max_bright_file = NULL;
        const char *path;
        GDir* dir;

	/* If we have the display type already, return it */
	if (display_type != DISPLAY_TYPE_UNSET)
		goto EXIT;

	if (g_access(DISPLAY_CABC_PATH DISPLAY_ACX565AKM, W_OK) == 0) {
		display_type = DISPLAY_TYPE_ACX565AKM;
		hw_display_fading = FALSE;

		brightness_file = g_strconcat(DISPLAY_CABC_PATH, DISPLAY_ACX565AKM, DISPLAY_CABC_BRIGHTNESS_FILE, NULL);
		max_brightness_file = g_strconcat(DISPLAY_CABC_PATH, DISPLAY_ACX565AKM, DISPLAY_CABC_MAX_BRIGHTNESS_FILE, NULL);
		cabc_mode_file = g_strconcat(DISPLAY_CABC_PATH, DISPLAY_ACX565AKM, DISPLAY_CABC_MODE_FILE, NULL);
		cabc_available_modes_file = g_strconcat(DISPLAY_CABC_PATH, DISPLAY_ACX565AKM, DISPLAY_CABC_AVAILABLE_MODES_FILE, NULL);
	} else if (g_access(DISPLAY_CABC_PATH DISPLAY_L4F00311, W_OK) == 0) {
		display_type = DISPLAY_TYPE_L4F00311;
		hw_display_fading = FALSE;

		brightness_file = g_strconcat(DISPLAY_CABC_PATH, DISPLAY_L4F00311, DISPLAY_CABC_BRIGHTNESS_FILE, NULL);
		max_brightness_file = g_strconcat(DISPLAY_CABC_PATH, DISPLAY_L4F00311, DISPLAY_CABC_MAX_BRIGHTNESS_FILE, NULL);
		cabc_mode_file = g_strconcat(DISPLAY_CABC_PATH, DISPLAY_L4F00311, DISPLAY_CABC_MODE_FILE, NULL);
		cabc_available_modes_file = g_strconcat(DISPLAY_CABC_PATH, DISPLAY_L4F00311, DISPLAY_CABC_AVAILABLE_MODES_FILE, NULL);
	} else {
        /* Default to NONE, we might change it later if we can find a generic one */
        display_type = DISPLAY_TYPE_NONE;

        /* Attempt to find first entry in /backlight */
        dir = g_dir_open(DISPLAY_GENERIC_PATH, 0, NULL);
        if (dir) {
            path = g_dir_read_name(dir);
            if (path) {
                bright_file = g_strconcat(DISPLAY_GENERIC_PATH, path, DISPLAY_GENERIC_BRIGHTNESS_FILE, NULL);
                max_bright_file = g_strconcat(DISPLAY_GENERIC_PATH, path, DISPLAY_GENERIC_MAX_BRIGHTNESS_FILE, NULL);

                if ((g_access(bright_file, W_OK) == 0) && (g_access(max_bright_file, W_OK) == 0)) {
                    display_type = DISPLAY_TYPE_GENERIC;

                    /* These will be freed later on, during module unload */
                    brightness_file = bright_file;
                    max_brightness_file = max_bright_file;
                } else {
                    g_free(bright_file);
                    g_free(max_bright_file);
                }
            }
        }

        g_dir_close(dir);
    }

	mce_log(LL_DEBUG, "Display type: %d", display_type);

EXIT:
	return display_type;
}

/**
 * Set CABC mode
 */
static void set_cabc_mode(const gchar *const mode)
{
	static gboolean available_modes_scanned = FALSE;
	const gchar *tmp = NULL;
	gint i;

	if (cabc_available_modes_file == NULL)
		goto EXIT;

	/* Update the list of available modes against the list we support */
	if (available_modes_scanned == FALSE) {
		gchar *available_modes = NULL;

		available_modes_scanned = TRUE;

		if (mce_read_string_from_file(cabc_available_modes_file,
					      &available_modes) == FALSE)
			goto EXIT;

		for (i = 0; (tmp = cabc_mode_mapping[i].sysfs) != NULL; i++) {
			if (strstr_delim(available_modes, tmp, " ") != NULL)
				cabc_mode_mapping[i].available = TRUE;
		}

		g_free(available_modes);
	}

	/* If the requested mode is supported, use it */
	for (i = 0; (tmp = cabc_mode_mapping[i].sysfs) != NULL; i++) {
		if (cabc_mode_mapping[i].available == FALSE)
			continue;

		if (!strcmp(tmp, mode)) {
			if (enable_power_saving == TRUE)
				mce_write_string_to_file(cabc_mode_file, tmp);
			else
				mce_write_string_to_file(cabc_mode_file,
							 CABC_MODE_OFF);

			cabc_mode = tmp;
			break;
		}
	}

EXIT:
	return;
}

/**
 * Call the FBIOBLANK ioctl
 *
 * @param value The ioctl value to pass to the backlight
 * @return TRUE on success, FALSE on failure
 */
static gboolean backlight_ioctl(int value)
{
	static int old_value = FB_BLANK_UNBLANK;
	static int fd = -1;
	gboolean status = FALSE;

	if (fd == -1) {
		if ((fd = open(FB_DEVICE, O_RDWR)) == -1) {
			mce_log(LL_CRIT, "cannot open `%s'", FB_DEVICE);
			goto EXIT;
		}

		old_value = !value; /* force ioctl() */
	}

	if (value != old_value) {
		if (ioctl(fd, FBIOBLANK, value) == -1) {
			mce_log(LL_CRIT,
				"ioctl() FBIOBLANK (%d) failed on `%s'; %s",
				value, FB_DEVICE, g_strerror(errno));
			close(fd);
			fd = -1;

			/* Reset errno,
			 * to avoid false positives down the line
			 */
			errno = 0;

			goto EXIT;
		}

		old_value = value;
	}

	status = TRUE;

EXIT:
	return status;
}

/**
 * Timeout callback for the brightness fade
 *
 * @param data Unused
 * @return Returns TRUE to repeat, until the cached brightness has reached
 *         the destination value; when this happens, FALSE is returned
 */
static gboolean brightness_fade_timeout_cb(gpointer data)
{
	gboolean retval = TRUE;

	(void)data;

	if ((cached_brightness <= 0) && (target_brightness != 0)) {
		backlight_ioctl(FB_BLANK_UNBLANK);
		x11_force_dpms_display_level(TRUE);
	}

	if ((cached_brightness == -1) ||
	    (ABS(cached_brightness -
		 target_brightness) < brightness_fade_steplength)) {
		cached_brightness = target_brightness;
		retval = FALSE;
	} else if (target_brightness > cached_brightness) {
		cached_brightness += brightness_fade_steplength;
	} else {
		cached_brightness -= brightness_fade_steplength;
	}

	mce_write_number_string_to_file(brightness_file,
					cached_brightness);

	if (cached_brightness == 0) {
		backlight_ioctl(FB_BLANK_POWERDOWN);
		x11_force_dpms_display_level(FALSE);
	}

	if (retval == FALSE)
		 brightness_fade_timeout_cb_id = 0;

	return retval;
}

/**
 * Cancel the brightness fade timeout
 */
static void cancel_brightness_fade_timeout(void)
{
	/* Remove the timeout source for the display brightness fade */
	if (brightness_fade_timeout_cb_id != 0) {
		g_source_remove(brightness_fade_timeout_cb_id);
		brightness_fade_timeout_cb_id = 0;
	}
}

/**
 * Setup the brightness fade timeout
 *
 * @param step_time The time between each brightness step
 */
static void setup_brightness_fade_timeout(gint step_time)
{
	cancel_brightness_fade_timeout();

	/* Setup new timeout */
	brightness_fade_timeout_cb_id =
		g_timeout_add(step_time, brightness_fade_timeout_cb, NULL);
}

/**
 * Update brightness fade
 *
 * Will fade from current value to new value
 *
 * @param new_brightness The new brightness to fade to
 */
static void update_brightness_fade(gint new_brightness)
{
	gint step_time = 10;

	if (hw_display_fading == TRUE) {
		cancel_brightness_fade_timeout();
		cached_brightness = new_brightness;
		target_brightness = new_brightness;
		backlight_ioctl(FB_BLANK_UNBLANK);
		mce_write_number_string_to_file(brightness_file,
						new_brightness);
		goto EXIT;
	}

	/* If we're already fading towards the right brightness,
	 * don't change anything
	 */
	if (target_brightness == new_brightness)
		goto EXIT;

	target_brightness = new_brightness;

	brightness_fade_steplength = 2;

	setup_brightness_fade_timeout(step_time);

EXIT:
	return;
}

/**
 * Blank display
 */
static void display_blank(void)
{
	cancel_brightness_fade_timeout();
	cached_brightness = 0;
	target_brightness = 0;
	mce_write_number_string_to_file(brightness_file, 0);
	backlight_ioctl(FB_BLANK_POWERDOWN);
	x11_force_dpms_display_level(FALSE);
}

/**
 * Dim display
 */
static void display_dim(void)
{
	if (cached_brightness == 0) {
		backlight_ioctl(FB_BLANK_UNBLANK);
		x11_force_dpms_display_level(TRUE);
	}

	update_brightness_fade((maximum_display_brightness *
			        DEFAULT_DIM_BRIGHTNESS) / 100);
}

/**
 * Unblank display
 */
static void display_unblank(void)
{
	/* If we unblank, switch on display immediately */
	if (cached_brightness == 0) {
		cached_brightness = set_brightness;
		target_brightness = set_brightness;
		backlight_ioctl(FB_BLANK_UNBLANK);
		mce_write_number_string_to_file(brightness_file,
						set_brightness);
		x11_force_dpms_display_level(TRUE);
	} else {
		update_brightness_fade(set_brightness);
	}
}

/**
 * Display brightness trigger
 *
 * @note A brightness request is only sent if the value changed
 * @param data The display brightness stored in a pointer
 */
static void display_brightness_trigger(gconstpointer data)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	gint new_brightness = GPOINTER_TO_INT(data);

	/* If the pipe is choked, ignore the value */
	if (new_brightness == 0)
		goto EXIT;

	/* Adjust the value, since it's a percentage value */
	new_brightness = (maximum_display_brightness * new_brightness) / 100;

	/* If we're just rehashing the same brightness value, don't bother */
	if ((new_brightness == cached_brightness) && (cached_brightness != -1))
		goto EXIT;

	/* The value we have here is for non-dimmed screen only */
	set_brightness = new_brightness;

	if ((display_state == MCE_DISPLAY_OFF) ||
	    (display_state == MCE_DISPLAY_DIM))
		goto EXIT;

	update_brightness_fade(new_brightness);

EXIT:
	return;
}

/**
 * Timeout callback for display blanking
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean blank_timeout_cb(gpointer data)
{
	(void)data;

	blank_timeout_cb_id = 0;

	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_OFF),
			       USE_INDATA, CACHE_INDATA);

	return FALSE;
}

/**
 * Cancel the display blanking timeout
 */
static void cancel_blank_timeout(void)
{
	/* Remove the timeout source for display blanking */
	if (blank_timeout_cb_id != 0) {
		g_source_remove(blank_timeout_cb_id);
		blank_timeout_cb_id = 0;
	}
}

/**
 * Setup blank timeout
 */
static void setup_blank_timeout(void)
{
	cancel_blank_timeout();
	cancel_dim_timeout();

	if (blanking_inhibited == TRUE)
		return;

	/* Setup new timeout */
	blank_timeout_cb_id =
		g_timeout_add_seconds(disp_blank_timeout,
				      blank_timeout_cb, NULL);
}

/**
 * Timeout callback for display dimming
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean dim_timeout_cb(gpointer data)
{
	(void)data;

	dim_timeout_cb_id = 0;

	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_DIM),
			       USE_INDATA, CACHE_INDATA);

	return FALSE;
}

/**
 * Cancel display dimming timeout
 */
static void cancel_dim_timeout(void)
{
	/* Remove the timeout source for display dimming */
	if (dim_timeout_cb_id != 0) {
		g_source_remove(dim_timeout_cb_id);
		dim_timeout_cb_id = 0;
	}
}

/**
 * Setup dim timeout
 */
static void setup_dim_timeout(void)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	gint dim_timeout = disp_dim_timeout + bootup_dim_additional_timeout;

	cancel_blank_timeout();
	cancel_dim_timeout();

	if (dimming_inhibited == TRUE)
		return;

	if (system_state == MCE_STATE_ACTDEAD)
		dim_timeout = DEFAULT_ACTDEAD_DIM_TIMEOUT;

	/* Setup new timeout */
	dim_timeout_cb_id =
		g_timeout_add_seconds(dim_timeout,
				      dim_timeout_cb, NULL);
}

/**
 * Timeout callback for display blanking pause
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean blank_prevent_timeout_cb(gpointer data)
{
	(void)data;

	blank_prevent_timeout_cb_id = 0;

	update_blanking_inhibit(FALSE);
	(void)execute_datapipe(&device_lock_inhibit_pipe,
			       GINT_TO_POINTER(FALSE),
			       USE_INDATA, CACHE_INDATA);

	return FALSE;
}

/**
 * Cancel blank prevention timeout
 */
static void cancel_blank_prevent(void)
{
	if (blank_prevent_timeout_cb_id != 0) {
		g_source_remove(blank_prevent_timeout_cb_id);
		blank_prevent_timeout_cb_id = 0;
		mce_log(LL_DEBUG, "device_lock_inhibit_pipe - > FALSE");
		(void)execute_datapipe(&device_lock_inhibit_pipe,
				       GINT_TO_POINTER(FALSE),
				       USE_INDATA, CACHE_INDATA);
	}
}

/**
 * Prevent screen blanking for display_timeout seconds
 */
static void request_display_blanking_pause(void)
{
	/* Also cancels any old timeouts */
	update_blanking_inhibit(TRUE);
	(void)execute_datapipe(&device_lock_inhibit_pipe,
			       GINT_TO_POINTER(TRUE),
			       USE_INDATA, CACHE_INDATA);

	/* Setup new timeout */
	blank_prevent_timeout_cb_id =
		g_timeout_add_seconds(blank_prevent_timeout,
				      blank_prevent_timeout_cb, NULL);
}

/**
 * Enable/Disable blanking inhibit,
 * based on charger status and inhibit mode
 *
 * @param timed_inhibit TRUE for timed inhibiting,
 *                      FALSE for triggered inhibiting
 */
static void update_blanking_inhibit(gboolean timed_inhibit)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);

	if ((call_state == CALL_STATE_RINGING) ||
	    ((charger_connected == TRUE) &&
	     ((system_state == MCE_STATE_ACTDEAD) ||
	      ((blanking_inhibit_mode == INHIBIT_STAY_ON_WITH_CHARGER) ||
	       (blanking_inhibit_mode == INHIBIT_STAY_DIM_WITH_CHARGER)))) ||
	    (blanking_inhibit_mode == INHIBIT_STAY_ON) ||
	    (blanking_inhibit_mode == INHIBIT_STAY_DIM) ||
	    (timed_inhibit == TRUE)) {
		/* Always inhibit blanking */
		blanking_inhibited = TRUE;

		/* If the policy calls for it, also inhibit dimming;
		 * INHIBIT_STAY_ON{,WITH_CHARGER} doesn't affect the
		 * policy in acting dead though
		 */
		if ((((blanking_inhibit_mode == INHIBIT_STAY_ON_WITH_CHARGER) ||
		      (blanking_inhibit_mode == INHIBIT_STAY_ON)) &&
		     (system_state != MCE_STATE_ACTDEAD)) ||
		    (call_state == CALL_STATE_RINGING) ||
		    (timed_inhibit == TRUE)) {
			dimming_inhibited = TRUE;
		} else {
			dimming_inhibited = FALSE;
		}

		cancel_blank_prevent();
	} else if (blank_prevent_timeout_cb_id == 0) {
		blanking_inhibited = FALSE;
		dimming_inhibited = FALSE;
	}

	/* Reprogram timeouts, if necessary */
	if (display_state == MCE_DISPLAY_DIM)
		setup_blank_timeout();
	else if (display_state != MCE_DISPLAY_OFF)
		setup_dim_timeout();
}

/**
 * GConf callback for display related settings
 *
 * @param gcc Unused
 * @param id Connection ID from gconf_client_notify_add()
 * @param entry The modified GConf entry
 * @param data Unused
 */
static void display_gconf_cb(GConfClient *const gcc, const guint id,
			     GConfEntry *const entry, gpointer const data)
{
	GConfValue *gcv = gconf_entry_get_value(entry);

	(void)gcc;
	(void)data;

	/* Key is unset */
	if (gcv == NULL) {
		mce_log(LL_DEBUG,
			"GConf Key `%s' has been unset",
			gconf_entry_get_key(entry));
		goto EXIT;
	}

	if (id == disp_brightness_gconf_cb_id) {
		gint tmp = gconf_value_get_int(gcv);

		(void)execute_datapipe(&display_brightness_pipe,
				       GINT_TO_POINTER(tmp),
				       USE_INDATA, CACHE_INDATA);
	} else if (id == enable_power_saving_gconf_cb_id) {
		enable_power_saving = gconf_value_get_bool(gcv);
		set_cabc_mode(cabc_mode);
	} else if (id == disp_blank_timeout_gconf_cb_id) {
		disp_blank_timeout = gconf_value_get_int(gcv);
		mce_log(LL_DEBUG, "disp_blank_timeout set to %i", disp_blank_timeout);
		/* Update blank prevent */
		update_blanking_inhibit(FALSE);

		/* Update inactivity timeout */
		(void)execute_datapipe(&inactivity_timeout_pipe,
				       GINT_TO_POINTER(disp_dim_timeout +
						       disp_blank_timeout),
				       USE_INDATA, CACHE_INDATA);
	} else if (id == disp_dim_timeout_gconf_cb_id) {
		disp_dim_timeout = gconf_value_get_int(gcv);
		mce_log(LL_DEBUG, "disp_dim_timeout set to %i", disp_dim_timeout);
		/* Update blank prevent */
		update_blanking_inhibit(FALSE);

		/* Update inactivity timeout */
		(void)execute_datapipe(&inactivity_timeout_pipe,
				       GINT_TO_POINTER(disp_dim_timeout +
						       disp_blank_timeout),
				       USE_INDATA, CACHE_INDATA);
	} else if (id == blanking_inhibit_mode_gconf_cb_id) {
		blanking_inhibit_mode = gconf_value_get_int(gcv);

		/* Update blank prevent */
		update_blanking_inhibit(FALSE);
	} else {
		mce_log(LL_WARN,
			"Spurious GConf value received; confused!");
	}

EXIT:
	return;
}

/**
 * Send a display status reply or signal
 *
 * @param method_call A DBusMessage to reply to;
 *                    pass NULL to send a display status signal instead
 * @return TRUE on success, FALSE on failure
 */
static gboolean send_display_status(DBusMessage *const method_call)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	gboolean is_tvout_on = datapipe_get_gint(tvout_pipe);

	DBusMessage *msg = NULL;
	const gchar *state = NULL;
	gboolean status = FALSE;

	switch (display_state) {
	case MCE_DISPLAY_OFF:
		state = MCE_DISPLAY_OFF_STRING;
		break;

	case MCE_DISPLAY_DIM:
		state = MCE_DISPLAY_DIM_STRING;
		break;

	case MCE_DISPLAY_ON:
	default:
		state = MCE_DISPLAY_ON_STRING;
		break;
	}
	if ((is_tvout_state_changed == TRUE) && (display_state == MCE_DISPLAY_OFF)) {
		state = is_tvout_on ? MCE_DISPLAY_ON_STRING : MCE_DISPLAY_OFF_STRING;
	}
	mce_log(LL_DEBUG,
		"Sending display status: %s",
		state);

	/* If method_call is set, send a reply,
	 * otherwise, send a signal
	 */

	if ((is_tvout_on) && (display_state == MCE_DISPLAY_OFF) && (!is_tvout_state_changed)){
		goto EXIT;
	}
	else
	{
		if (method_call != NULL) {
			msg = dbus_new_method_reply(method_call);
		} else {
			/* display_status_ind */
			msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
						  MCE_DISPLAY_SIG);
		}
		/* Append the display status */
		if (dbus_message_append_args(msg,
						 DBUS_TYPE_STRING, &state,
						 DBUS_TYPE_INVALID) == FALSE) {
			mce_log(LL_CRIT,
				"Failed to append %sargument to D-Bus message "
				"for %s.%s",
				method_call ? "reply " : "",
				method_call ? MCE_REQUEST_IF :
						  MCE_SIGNAL_IF,
				method_call ? MCE_DISPLAY_STATUS_GET :
						  MCE_DISPLAY_SIG);
			dbus_message_unref(msg);
			goto EXIT;
		}
		/* Send the message */
		status = dbus_send_message(msg);
	}
EXIT:

	return status;
}

/**
 * D-Bus callback for the get display status method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_status_get_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display status get request");

	/* Try to send a reply that contains the current display status */
	if (send_display_status(msg) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

/**
 * Send a CABC status reply
 *
 * @param method_call A DBusMessage to reply to
 * @return TRUE on success, FALSE on failure
 */
static gboolean send_cabc_mode(DBusMessage *const method_call)
{
	const gchar *dbus_cabc_mode = NULL;
	DBusMessage *msg = NULL;
	gboolean status = FALSE;
	gint i;

	mce_log(LL_DEBUG,
		"Sending CABC mode: %s",
		cabc_mode);

	for (i = 0; (dbus_cabc_mode = cabc_mode_mapping[i].dbus) != NULL; i++) {
		if (!strcmp(dbus_cabc_mode, cabc_mode))
			break;
	}

	if (dbus_cabc_mode == NULL)
		dbus_cabc_mode = MCE_CABC_MODE_OFF;

	msg = dbus_new_method_reply(method_call);

	/* Append the CABC mode */
	if (dbus_message_append_args(msg,
				     DBUS_TYPE_STRING, &dbus_cabc_mode,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append reply argument to D-Bus message "
			"for %s.%s",
			MCE_REQUEST_IF, MCE_CABC_MODE_GET);
		dbus_message_unref(msg);
		goto EXIT;
	}

	/* Send the message */
	status = dbus_send_message(msg);

EXIT:
	return status;
}

/**
 * D-Bus callback for the get CABC mode method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean cabc_mode_get_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received CABC mode get request");

	/* Try to send a reply that contains the current CABC mode */
	if (send_cabc_mode(msg) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

/**
 * D-Bus callback for the display on method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_on_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	submode_t submode = mce_get_submode_int32();
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display on request");

	if ((submode & MCE_TKLOCK_SUBMODE) == 0) {
		mce_log(LL_DEBUG, "MCE_DISPLAY_ON in %s %s %d",__FILE__, __func__, __LINE__);
		(void)execute_datapipe(&display_state_pipe,
				       GINT_TO_POINTER(MCE_DISPLAY_ON),
				       USE_INDATA, CACHE_INDATA);
	}

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback for the display dim method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_dim_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	submode_t submode = mce_get_submode_int32();
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display dim request");

	/* If the tklock is active, ignore the request */
	if ((submode & MCE_TKLOCK_SUBMODE) == 0) {
		(void)execute_datapipe(&display_state_pipe,
				       GINT_TO_POINTER(MCE_DISPLAY_DIM),
				       USE_INDATA, CACHE_INDATA);
	}

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback for the display off method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_off_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received display off request");

	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_OFF),
			       USE_INDATA, CACHE_INDATA);

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback used for monitoring the process that requested
 * blanking prevention; if that process exits, immediately
 * cancel the blanking timeout and resume normal operation
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean blanking_pause_owner_monitor_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;
	const gchar *old_name;
	const gchar *new_name;
	const gchar *service;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	/* Extract result */
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

	if (mce_dbus_owner_monitor_remove(old_name,
					  &blanking_pause_monitor_list) == 0) {
		cancel_blank_prevent();
		update_blanking_inhibit(FALSE);
		(void)execute_datapipe(&device_lock_inhibit_pipe,
				       GINT_TO_POINTER(FALSE),
				       USE_INDATA, CACHE_INDATA);
	}

	status = TRUE;

EXIT:
	return status;
}

/**
 * D-Bus callback for display blanking prevent request method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean display_blanking_pause_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	const gchar *sender = dbus_message_get_sender(msg);
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received blanking pause request from %s",
		(sender == NULL) ? "(unknown)" : sender);

	request_display_blanking_pause();

	if (mce_dbus_owner_monitor_add(sender,
				       blanking_pause_owner_monitor_dbus_cb,
				       &blanking_pause_monitor_list,
				       MAX_MONITORED_SERVICES) == -1) {
		mce_log(LL_INFO,
			"Failed to add name owner monitoring for `%s'",
			sender);
	}

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

/**
 * D-Bus callback used for monitoring the process that requested
 * CABC mode change; if that process exits, immediately
 * restore the CABC mode to the default
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean cabc_mode_owner_monitor_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;
	const gchar *old_name;
	const gchar *new_name;
	const gchar *service;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	/* Extract result */
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

	/* Remove the name monitor for the CABC mode */
	mce_dbus_owner_monitor_remove_all(&cabc_mode_monitor_list);
	set_cabc_mode(CABC_MODE_DEFAULT);

	status = TRUE;

EXIT:
	return status;
}

/**
 * D-Bus callback for the set CABC mode method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean cabc_mode_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	const gchar *sender = dbus_message_get_sender(msg);
	const gchar *sysfs_cabc_mode = NULL;
	const gchar *dbus_cabc_mode = NULL;
	gboolean status = FALSE;
	gint i;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	mce_log(LL_DEBUG,
		"Received set CABC mode request from %s",
		(sender == NULL) ? "(unknown)" : sender);

	/* Extract result */
	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &dbus_cabc_mode,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_ERR,
			"Failed to get argument from %s.%s; %s",
			MCE_REQUEST_IF, MCE_CABC_MODE_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	for (i = 0; (sysfs_cabc_mode = cabc_mode_mapping[i].sysfs) != NULL; i++) {
		if (!strcmp(sysfs_cabc_mode, dbus_cabc_mode))
			break;
	}

	/* Use the default if the requested mode was invalid */
	if (sysfs_cabc_mode == NULL) {
		mce_log(LL_WARN,
			"Invalid CABC mode requested; using %s",
			CABC_MODE_DEFAULT);
		sysfs_cabc_mode = CABC_MODE_DEFAULT;
	}

	set_cabc_mode(sysfs_cabc_mode);

	/* We only ever monitor one owner; latest wins */
	mce_dbus_owner_monitor_remove_all(&cabc_mode_monitor_list);

	if (mce_dbus_owner_monitor_add(sender,
				       cabc_mode_owner_monitor_dbus_cb,
				       &cabc_mode_monitor_list,
				       1) == -1) {
		mce_log(LL_INFO,
			"Failed to add name owner monitoring for `%s'",
			sender);
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

/**
 * D-Bus callback for the desktop startup notification signal
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean desktop_startup_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	(void)msg;

	mce_log(LL_DEBUG,
		"Received desktop startup notification");

	execute_datapipe_output_triggers(&led_pattern_deactivate_pipe,
					 MCE_LED_PATTERN_POWER_ON, USE_INDATA);

	mce_rem_submode_int32(MCE_BOOTUP_SUBMODE);

	bootup_dim_additional_timeout = 0;

	/* Restore normal inactivity timeout */
	(void)execute_datapipe(&inactivity_timeout_pipe,
			       GINT_TO_POINTER(disp_dim_timeout +
					       disp_blank_timeout),
			       USE_INDATA, CACHE_INDATA);

	/* Update blank prevent */
	update_blanking_inhibit(FALSE);

	status = TRUE;

	return status;
}

/**
 * Handle display state change
 *
 * @param data The display state stored in a pointer
 */
static void display_state_trigger(gconstpointer data)
{
	/** Cached display state */
	static display_state_t cached_display_state = MCE_DISPLAY_UNDEF;
	display_state_t display_state = GPOINTER_TO_INT(data);

	switch (display_state) {
	case MCE_DISPLAY_OFF:
		cancel_dim_timeout();
		cancel_blank_timeout();
		break;

	case MCE_DISPLAY_DIM:
		setup_blank_timeout();
		break;

	case MCE_DISPLAY_ON:
	default:
		setup_dim_timeout();
		break;
	}

	/* If we already have the right state,
	 * we're done here
	 */
	if (cached_display_state == display_state)
		goto EXIT;

	switch (display_state) {
	case MCE_DISPLAY_OFF:
		display_blank();
		break;

	case MCE_DISPLAY_DIM:
		display_dim();
		break;

	case MCE_DISPLAY_ON:
	default:
		display_unblank();
		break;
	}

	/* This will send the correct state
	 * since the pipe contains the new value
	 */
	send_display_status(NULL);

	/* Update the cached value */
	cached_display_state = display_state;

EXIT:
	return;
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

	/* Avoid unnecessary updates:
	 * Note: this *must* be binary or/and,
	 *       not logical, else it won't work,
	 *       for (hopefully) obvious reasons
	 */
	if ((old_submode | submode) & MCE_TRANSITION_SUBMODE)
		update_blanking_inhibit(FALSE);

	submode = old_submode;
}

/**
 * Datapipe trigger for the charger state
 *
 * @param data TRUE if the charger was connected,
 *	       FALSE if the charger was disconnected
 */
static void charger_state_trigger(gconstpointer data)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);

	charger_connected = GPOINTER_TO_INT(data);

	if (system_state != MCE_STATE_ACTDEAD) {
		mce_log(LL_DEBUG, "MCE_DISPLAY_ON in %s %s %d",__FILE__, __func__, __LINE__);
		(void)execute_datapipe(&display_state_pipe,
				       GINT_TO_POINTER(MCE_DISPLAY_ON),
				       USE_INDATA, CACHE_INDATA);
	}

	update_blanking_inhibit(FALSE);
}

/**
 * Datapipe trigger for device inactivity
 *
 * @param data The inactivity stored in a pointer;
 *             TRUE if the device is inactive,
 *             FALSE if the device is active
 */
static void device_inactive_trigger(gconstpointer data)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	alarm_ui_state_t alarm_ui_state =
				datapipe_get_gint(alarm_ui_state_pipe);
	gboolean device_inactive = GPOINTER_TO_INT(data);

	/* Unblank screen on device activity,
	 * unless the device is in acting dead and no alarm is visible
	 */
	if (((system_state == MCE_STATE_USER) ||
	     ((system_state == MCE_STATE_ACTDEAD) &&
	      ((alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
	       (alarm_ui_state == MCE_ALARM_UI_RINGING_INT32)))) &&
	    (device_inactive == FALSE)) {
		mce_log(LL_DEBUG, "MCE_DISPLAY_ON in %s %s %d",__FILE__, __func__, __LINE__);
		(void)execute_datapipe(&display_state_pipe,
				       GINT_TO_POINTER(MCE_DISPLAY_ON),
				       USE_INDATA, CACHE_INDATA);
	}
}

static void tvout_trigger(gconstpointer data)
{
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	gboolean is_tvout_on = GPOINTER_TO_INT(data);
	
	mce_log(LL_DEBUG, "Recieved tvout state changing: is_tvout_on = %d", is_tvout_on);	
	
	if (display_state == MCE_DISPLAY_OFF) {
		is_tvout_state_changed = TRUE;
		send_display_status(NULL);
		is_tvout_state_changed = FALSE;
	}
	return;
}
static void call_state_trigger(gconstpointer data)
{
	(void)data;

	update_blanking_inhibit(FALSE);
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	gint disp_brightness = DEFAULT_DISP_BRIGHTNESS;
	submode_t submode = mce_get_submode_int32();
	gulong tmp;

	(void)module;

	/* Initialise the display type and the relevant paths */
	get_display_type();

	if ((submode & MCE_TRANSITION_SUBMODE) != 0) {
		mce_add_submode_int32(MCE_BOOTUP_SUBMODE);
		bootup_dim_additional_timeout = BOOTUP_DIM_ADDITIONAL_TIMEOUT;
	} else {
		bootup_dim_additional_timeout = 0;
	}

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&charger_state_pipe,
					  charger_state_trigger);
	append_output_trigger_to_datapipe(&display_brightness_pipe,
					  display_brightness_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);
	append_output_trigger_to_datapipe(&submode_pipe,
					  submode_trigger);
	append_output_trigger_to_datapipe(&device_inactive_pipe,
					  device_inactive_trigger);
	append_output_trigger_to_datapipe(&call_state_pipe,
					  call_state_trigger);
	append_output_trigger_to_datapipe(&tvout_pipe, 
					  tvout_trigger);
	/* Get maximum brightness */
	if (mce_read_number_string_from_file(max_brightness_file,
					     &tmp) == FALSE) {
		mce_log(LL_ERR,
			"Could not read the maximum brightness from %s; "
			"defaulting to %d",
			max_brightness_file,
			DEFAULT_MAXIMUM_DISPLAY_BRIGHTNESS);
		tmp = DEFAULT_MAXIMUM_DISPLAY_BRIGHTNESS;
	}

	maximum_display_brightness = tmp;

	(void)mce_gconf_get_bool(MCE_GCONF_ENABLE_POWER_SAVING_PATH,
				 &enable_power_saving);

	set_cabc_mode(CABC_MODE_DEFAULT);

	if (mce_gconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_ENABLE_POWER_SAVING_PATH,
				   display_gconf_cb,
				   &enable_power_saving_gconf_cb_id) == FALSE)
		goto EXIT;

	/* Display brightness */
	/* Since we've set a default, error handling is unnecessary */
	(void)mce_gconf_get_int(MCE_GCONF_DISPLAY_BRIGHTNESS_PATH,
				&disp_brightness);

	/* Use the current brightness as cached brightness on startup,
	 * and fade from that value
	 */
	if (mce_read_number_string_from_file(brightness_file,
					     &tmp) == FALSE) {
		mce_log(LL_ERR,
			"Could not read the current brightness from %s",
			brightness_file);
		cached_brightness = -1;
	} else {
		cached_brightness = tmp;
	}

	(void)execute_datapipe(&display_brightness_pipe,
			       GINT_TO_POINTER(disp_brightness),
			       USE_INDATA, CACHE_INDATA);

	if (mce_gconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_DISPLAY_BRIGHTNESS_PATH,
				   display_gconf_cb,
				   &disp_brightness_gconf_cb_id) == FALSE)
		goto EXIT;

	/* Display blank */
	/* Since we've set a default, error handling is unnecessary */
	(void)mce_gconf_get_int(MCE_GCONF_DISPLAY_BLANK_TIMEOUT_PATH,
				&disp_blank_timeout);

	if (mce_gconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_DISPLAY_BLANK_TIMEOUT_PATH,
				   display_gconf_cb,
				   &disp_blank_timeout_gconf_cb_id) == FALSE)
		goto EXIT;

	/* Display dim */
	/* Since we've set a default, error handling is unnecessary */
	(void)mce_gconf_get_int(MCE_GCONF_DISPLAY_DIM_TIMEOUT_PATH,
				&disp_dim_timeout);

	if (mce_gconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_DISPLAY_DIM_TIMEOUT_PATH,
				   display_gconf_cb,
				   &disp_dim_timeout_gconf_cb_id) == FALSE)
		goto EXIT;

	/* Update inactivity timeout */
	(void)execute_datapipe(&inactivity_timeout_pipe,
			       GINT_TO_POINTER(disp_dim_timeout +
					       disp_blank_timeout +
					       bootup_dim_additional_timeout),
			       USE_INDATA, CACHE_INDATA);

	/* Don't blank on charger */
	/* Since we've set a default, error handling is unnecessary */
	(void)mce_gconf_get_int(MCE_GCONF_BLANKING_INHIBIT_MODE_PATH,
				&blanking_inhibit_mode);

	if (mce_gconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_BLANKING_INHIBIT_MODE_PATH,
				   display_gconf_cb,
				   &blanking_inhibit_mode_gconf_cb_id) == FALSE)
		goto EXIT;

	/* get_display_status */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_STATUS_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_status_get_dbus_cb) == NULL)
		goto EXIT;

	/* get_cabc_mode */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_CABC_MODE_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 cabc_mode_get_dbus_cb) == NULL)
		goto EXIT;

	/* req_display_state_on */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_ON_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_on_req_dbus_cb) == NULL)
		goto EXIT;

	/* req_display_state_dim */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_DIM_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_dim_req_dbus_cb) == NULL)
		goto EXIT;

	/* req_display_state_off */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISPLAY_OFF_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_off_req_dbus_cb) == NULL)
		goto EXIT;

	/* req_display_blanking_pause */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_PREVENT_BLANK_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 display_blanking_pause_req_dbus_cb) == NULL)
		goto EXIT;

	/* req_cabc_mode */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_CABC_MODE_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 cabc_mode_req_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add("com.nokia.HildonDesktop",
				 "ready",
				 NULL,
				 DBUS_MESSAGE_TYPE_SIGNAL,
				 desktop_startup_dbus_cb) == NULL)
		goto EXIT;

	/* Request display on to get the state machine in sync */
	mce_log(LL_DEBUG, "MCE_DISPLAY_ON in %s %s %d",__FILE__, __func__, __LINE__);
	(void)execute_datapipe(&display_state_pipe,
			       GINT_TO_POINTER(MCE_DISPLAY_ON),
			       USE_INDATA, CACHE_INDATA);

EXIT:
	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&tvout_pipe, 
					  tvout_trigger);
	remove_output_trigger_from_datapipe(&call_state_pipe,
					    call_state_trigger);
	remove_output_trigger_from_datapipe(&device_inactive_pipe,
					    device_inactive_trigger);
	remove_output_trigger_from_datapipe(&submode_pipe,
					    submode_trigger);
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&display_brightness_pipe,
					    display_brightness_trigger);
	remove_output_trigger_from_datapipe(&charger_state_pipe,
					    charger_state_trigger);

	/* Free strings */
	g_free(brightness_file);
	g_free(max_brightness_file);
	g_free(cabc_mode_file);
	g_free(cabc_available_modes_file);

	/* Remove all timer sources */
	cancel_blank_prevent();
	cancel_brightness_fade_timeout();
	cancel_dim_timeout();
	cancel_blank_timeout();

	return;
}
