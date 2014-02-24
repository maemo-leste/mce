/**
 * @file filter-brightness-als.c
 * Ambient Light Sensor level adjusting filter module
 * for display backlight, key backlight, and LED brightness
 * This file implements a filter module for MCE
 * <p>
 * Copyright © 2007-2010 Nokia Corporation and/or its subsidiary(-ies).
 * <p>
 * @author David Weinehall <david.weinehall@nokia.com>
 * @author Tuomo Tanskanen <ext-tuomo.1.tanskanen@nokia.com>
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
#include <unistd.h>
#include <cal.h>
#include <stdlib.h>
#include "mce.h"
#include "filter-brightness-als.h"
#include "mce-io.h"
#include "mce-hal.h"
#include "mce-log.h"
#include "mce-gconf.h"
#include "datapipe.h"
#include "median_filter.h"

/** Module name */
#define MODULE_NAME		"filter-brightness-als"

/** Functionality provided by this module */
static const gchar *const provides[] = {
	"display-brightness-filter",
	"led-brightness-filter",
	"key-backlight-brightness-filter",
	NULL
};

/** Functionality that this module enhances */
static const gchar *const enhances[] = {
	"display-brightness",
	"led-brightness",
	"key-backlight-brightness",
	NULL
};

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module enhances */
	.enhances = enhances,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 100
};

/** GConf callback ID for ALS enabled */
static guint als_enabled_gconf_cb_id = 0;

static const gchar *als_lux_path = NULL;
static const gchar *als_calib0_path = NULL;
static const gchar *als_calib1_path = NULL;
static gboolean als_available = TRUE;
static gboolean als_enabled = TRUE;
static gint als_lux = -1;
static als_profile_struct *display_als_profiles = display_als_profiles_rx51;
static als_profile_struct *led_als_profiles = led_als_profiles_rx51;
static als_profile_struct *kbd_als_profiles = kbd_als_profiles_rx51;

/** Display state */
static display_state_t display_state = MCE_DISPLAY_UNDEF;

/** Median filter */
static median_filter_struct median_filter;

/** ALS poll interval */
static gint als_poll_interval = ALS_DISPLAY_ON_POLL_FREQ;

/** ID for ALS poll timer source */
static guint als_poll_timer_cb_id = 0;

/** Ambient Light Sensor type */
typedef enum {
	ALS_TYPE_UNSET = -1,
	ALS_TYPE_NONE = 0,
	ALS_TYPE_RX44 = 1,
	ALS_TYPE_RX51 = 2
} als_type_t;

static void cancel_als_poll_timer(void);

/**
 * GConf callback for ALS settings
 *
 * @param gcc Unused
 * @param id Connection ID from gconf_client_notify_add()
 * @param entry The modified GConf entry
 * @param data Unused
 */
static void als_gconf_cb(GConfClient *const gcc, const guint id,
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

	if (id == als_enabled_gconf_cb_id) {
		gint tmp = gconf_value_get_bool(gcv);

		/* Only care about the setting if there's an ALS available */
		if (als_available == TRUE)
			als_enabled = tmp;
	} else {
		mce_log(LL_WARN,
			"Spurious GConf value received; confused!");
	}

EXIT:
	return;
}

/**
 * Get the ALS type
 *
 * @return The ALS-type
 */
static als_type_t get_als_type(void)
{
	static als_type_t als_type = ALS_TYPE_UNSET;

	/* If we have the ALS-type already, return it */
	if (als_type != ALS_TYPE_UNSET)
		goto EXIT;

	if (g_access(ALS_LUX_PATH_RX44, W_OK) == 0) {
		als_type = ALS_TYPE_RX44;

		als_lux_path = ALS_LUX_PATH_RX44;
		als_calib0_path = ALS_CALIB0_PATH_RX44;
		als_calib1_path = ALS_CALIB1_PATH_RX44;
		display_als_profiles = display_als_profiles_rx44;
		led_als_profiles = led_als_profiles_rx44;
		kbd_als_profiles = kbd_als_profiles_rx44;
	} else if (g_access(ALS_LUX_PATH_RX51, W_OK) == 0) {
		als_type = ALS_TYPE_RX51;

		als_lux_path = ALS_LUX_PATH_RX51;
		als_calib0_path = ALS_CALIB0_PATH_RX51;
		als_calib1_path = ALS_CALIB1_PATH_RX51;
		display_als_profiles = display_als_profiles_rx51;
		led_als_profiles = led_als_profiles_rx51;
		kbd_als_profiles = kbd_als_profiles_rx51;
	} else if (g_access(ALS_LUX_PATH_RX51_3x, W_OK) == 0) {
		als_type = ALS_TYPE_RX51;

		als_lux_path = ALS_LUX_PATH_RX51_3x;
		als_calib0_path = ALS_CALIB0_PATH_RX51_3x;
		als_calib1_path = ALS_CALIB1_PATH_RX51_3x;
		display_als_profiles = display_als_profiles_rx51;
		led_als_profiles = led_als_profiles_rx51;
		kbd_als_profiles = kbd_als_profiles_rx51;
	} else {
		als_type = ALS_TYPE_NONE;
		als_lux_path = NULL;
	}
	mce_log(LL_DEBUG, "ALS-type: %d", als_type);

EXIT:
	return als_type;
}

/**
 * Calibrate the ALS using calibration values from CAL
 */
static void calibrate_als(void)
{
	struct cal *cal_data = NULL;

	/* Retrieve the calibration data stored in CAL */
	if (cal_init(&cal_data) >= 0) {
		void *ptr = NULL;
		unsigned long len;
		int retval;

		if ((retval = cal_read_block(cal_data, ALS_CALIB_IDENTIFIER,
					     &ptr, &len, 0)) == 0) {
			guint32 *als_calib = ptr;

			/* Correctness checks */
			if (len == (2 * sizeof (guint32))) {
				/* Write calibration values */
				mce_write_number_string_to_file(als_calib0_path,
								als_calib[0]);
				mce_write_number_string_to_file(als_calib1_path,
								als_calib[1]);
			} else {
				mce_log(LL_ERR,
					"Received incorrect number of ALS "
					"calibration values from CAL");
			}

			free(ptr);
		} else {
			mce_log(LL_ERR,
				"cal_read_block() (als_calib) failed; "
				"retval: %d",
				retval);
		}

		cal_finish(cal_data);
	} else {
		mce_log(LL_ERR,
			"cal_init() failed");
	}
}

static gint filter_data(als_profile_struct *profiles, als_profile_t profile,
			gint lux, gint *level)
{
	gint tmp = *level;

	if (tmp == -1)
		tmp = 0;
	else if (tmp > 5)
		tmp = 5;

	if ((profiles[profile].range[4][0] != -1) &&
	    (lux > profiles[profile].range[4][((5 - tmp) > 0) ? 1 : 0]))
		*level = 5;
	else if ((profiles[profile].range[3][0] != -1) &&
		 (lux > profiles[profile].range[3][((4 - tmp) > 0) ? 1 : 0]))
		*level = 4;
	else if ((profiles[profile].range[2][0] != -1) &&
		 (lux > profiles[profile].range[2][((3 - tmp) > 0) ? 1 : 0]))
		*level = 3;
	else if ((profiles[profile].range[1][0] != -1) &&
		 (lux > profiles[profile].range[1][((2 - tmp) > 0) ? 1 : 0]))
		*level = 2;
	else if ((profiles[profile].range[0][0] != -1) &&
		 (lux > profiles[profile].range[0][((1 - tmp) > 0) ? 1 : 0]))
		*level = 1;
	else
		*level = 0;

	return profiles[profile].value[*level];
}

/**
 * Ambient Light Sensor filter for display brightness
 *
 * @param data The un-processed brightness setting (1-5) stored in a pointer
 * @return The processed brightness value (percentage)
 */
static gpointer display_brightness_filter(gpointer data)
{
	/** Display ALS level */
	static gint display_als_level = -1;
	gint raw = GPOINTER_TO_INT(data) - 1;
	gpointer retval;

	/* If the display is off, don't update its brightness */
	if (display_state == MCE_DISPLAY_OFF) {
		raw = 0;
		goto EXIT;
	}

	/* Safety net */
	if (raw < ALS_PROFILE_MINIMUM)
		raw = ALS_PROFILE_MINIMUM;
	else if (raw > ALS_PROFILE_MAXIMUM)
		raw = ALS_PROFILE_MAXIMUM;

	if (als_enabled == TRUE) {
		gint percentage = filter_data(display_als_profiles, raw,
					      als_lux, &display_als_level);

		raw = percentage;
	} else {
		raw = (raw + 1) * 20;
	}

EXIT:
	retval = GINT_TO_POINTER(raw);

	return retval;
}

/**
 * Ambient Light Sensor filter for LED brightness
 *
 * @param data The un-processed brightness setting (1-5) stored in a pointer
 * @return The processed brightness value
 */
static gpointer led_brightness_filter(gpointer data)
{
	/** LED ALS level */
	static gint led_als_level = -1;
	gint brightness;

	if (als_enabled == TRUE) {
		/* XXX: this always uses the NORMAL profile */
		gint percentage = filter_data(led_als_profiles,
					      ALS_PROFILE_NORMAL,
					      als_lux, &led_als_level);
		brightness = (GPOINTER_TO_INT(data) * percentage) / 100;
	} else {
		brightness = GPOINTER_TO_INT(data);
	}

	return GINT_TO_POINTER(brightness);
}

/**
 * Ambient Light Sensor filter for keyboard backlight brightness
 *
 * @param data The un-processed brightness setting (1-5) stored in a pointer
 * @return The processed brightness value
 */
static gpointer key_backlight_filter(gpointer data)
{
	/** Keyboard ALS level */
	static gint kbd_als_level = -1;
	gint brightness = 0;

	if (GPOINTER_TO_INT(data) == 0)
		goto EXIT;

	if (als_enabled == TRUE) {
		/* XXX: this always uses the NORMAL profile */
		gint percentage = filter_data(kbd_als_profiles,
					      ALS_PROFILE_NORMAL,
					      als_lux, &kbd_als_level);
		brightness = (GPOINTER_TO_INT(data) * percentage) / 100;
	} else {
		brightness = GPOINTER_TO_INT(data);
	}

EXIT:
	return GINT_TO_POINTER(brightness);
}

/**
 * Read a value from the ALS and update the median filter
 *
 * @return the filtered result of the read,
 *         -1 on failure,
 *         -2 if the ALS is disabled
 */
static gint als_read_value_filtered(void)
{
	gulong tmp;
	gint filtered_read = -2;

	if (als_enabled == FALSE)
		goto EXIT;

	/* Read lux value from ALS */
	if (mce_read_number_string_from_file(als_lux_path, &tmp) == TRUE) {
		filtered_read = median_filter_map(&median_filter, tmp);
	} else {
		filtered_read = -1;
	}

EXIT:
	return filtered_read;
}

/**
 * Timer callback for polling of the Ambient Light Sensor
 *
 * @param data Unused
 * @return Always returns TRUE, for continuous polling,
           unless the ALS is disabled
 */
static gboolean als_poll_timer_cb(gpointer data)
{
	gboolean status = FALSE;
	gint old_lux;

	(void)data;

	old_lux = als_lux;

	/* Read lux value from ALS */
	if ((als_lux = als_read_value_filtered()) == -2)
		goto EXIT;

	if ((als_lux == -1) || (als_lux == old_lux))
		goto EXIT2;

	/* Re-filter the brightness */
	(void)execute_datapipe(&display_brightness_pipe, NULL,
			       USE_CACHE, DONT_CACHE_INDATA);
	(void)execute_datapipe(&led_brightness_pipe, NULL,
			       USE_CACHE, DONT_CACHE_INDATA);
	(void)execute_datapipe(&key_backlight_pipe, NULL,
			       USE_CACHE, DONT_CACHE_INDATA);

EXIT2:
	status = TRUE;

EXIT:
	if (status == FALSE)
		als_poll_timer_cb_id = 0;

	return status;
}

/**
 * Cancel Ambient Light Sensor poll timer
 */
static void cancel_als_poll_timer(void)
{
	if (als_poll_timer_cb_id != 0) {
		g_source_remove(als_poll_timer_cb_id);
		als_poll_timer_cb_id = 0;
	}
}

/**
 * Setup Ambient Light Sensor poll timer
 */
static void setup_als_poll_timer(void)
{
	cancel_als_poll_timer();

	if (als_poll_interval != 0) {
		als_poll_timer_cb_id =
			g_timeout_add(als_poll_interval,
				      als_poll_timer_cb,
				      NULL);
	}
}

/**
 * Handle display state change
 *
 * @param data The display stated stored in a pointer
 */
static void display_state_trigger(gconstpointer data)
{
	static display_state_t old_display_state = MCE_DISPLAY_UNDEF;
	gint old_als_poll_interval = als_poll_interval;
	display_state = GPOINTER_TO_INT(data);

	if (als_enabled == FALSE)
		goto EXIT;

	old_als_poll_interval = als_poll_interval;

	/* Update poll timeout */
	switch (display_state) {
	case MCE_DISPLAY_OFF:
		als_poll_interval = ALS_DISPLAY_OFF_POLL_FREQ;
		break;

	case MCE_DISPLAY_DIM:
		als_poll_interval = ALS_DISPLAY_DIM_POLL_FREQ;
		break;

	case MCE_DISPLAY_UNDEF:
	case MCE_DISPLAY_ON:
	default:
		als_poll_interval = ALS_DISPLAY_ON_POLL_FREQ;
		break;
	}

	/* Re-fill the median filter */
	if (((old_display_state == MCE_DISPLAY_OFF) ||
	     (old_display_state == MCE_DISPLAY_UNDEF)) &&
	    ((display_state == MCE_DISPLAY_ON) ||
	     (display_state == MCE_DISPLAY_DIM))) {
		/* Re-initialise the median filter */
		if (median_filter_init(&median_filter,
				       MEDIAN_FILTER_WINDOW_SIZE) == FALSE) {
			mce_log(LL_CRIT, "median_filter_init() failed");
			als_enabled = FALSE;
			cancel_als_poll_timer();
			goto EXIT;
		}

		/* Read lux value from ALS */
		if ((als_lux = als_read_value_filtered()) >= 0) {
			/* Re-filter the brightness */
			(void)execute_datapipe(&display_brightness_pipe, NULL,
					       USE_CACHE, DONT_CACHE_INDATA);
			(void)execute_datapipe(&led_brightness_pipe, NULL,
					       USE_CACHE, DONT_CACHE_INDATA);
			(void)execute_datapipe(&key_backlight_pipe, NULL,
					       USE_CACHE, DONT_CACHE_INDATA);
		}
	}

	/* Reprogram timer, if needed */
	if ((als_poll_interval != old_als_poll_interval) ||
	    (als_poll_timer_cb_id == 0))
		setup_als_poll_timer();

EXIT:
	old_display_state = display_state;

	return;
}

/**
 * Init function for the ALS filter
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
	append_filter_to_datapipe(&display_brightness_pipe,
				  display_brightness_filter);
	append_filter_to_datapipe(&led_brightness_pipe,
				  led_brightness_filter);
	append_filter_to_datapipe(&key_backlight_pipe,
				  key_backlight_filter);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);

	/* ALS enabled */
	/* Since we've set a default, error handling is unnecessary */
	(void)mce_gconf_get_bool(MCE_GCONF_DISPLAY_ALS_ENABLED_PATH,
				 &als_enabled);

	if (mce_gconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_DISPLAY_ALS_ENABLED_PATH,
				   als_gconf_cb,
				   &als_enabled_gconf_cb_id) == FALSE)
		goto EXIT;

	/* Initialise the median filter */
	if (median_filter_init(&median_filter,
			       MEDIAN_FILTER_WINDOW_SIZE) == FALSE) {
		mce_log(LL_CRIT, "median_filter_init() failed");
		goto EXIT;
	}

	/* Initial read of lux value from ALS */
	if ((get_als_type() != ALS_TYPE_NONE) &&
	    (als_lux = als_read_value_filtered()) >= 0) {
		/* Set initial polling interval */
		als_poll_interval = ALS_DISPLAY_ON_POLL_FREQ;

		/* Setup ALS polling */
		setup_als_poll_timer();

		calibrate_als();
	} else {
		als_lux = -1;
		als_available = FALSE;
		als_enabled = FALSE;
	}

	(void)execute_datapipe(&display_brightness_pipe, NULL,
			       USE_CACHE, DONT_CACHE_INDATA);
	(void)execute_datapipe(&led_brightness_pipe, NULL,
			       USE_CACHE, DONT_CACHE_INDATA);
	(void)execute_datapipe(&key_backlight_pipe, NULL,
			       USE_CACHE, DONT_CACHE_INDATA);

EXIT:
	return NULL;
}

/**
 * Exit function for the ALS filter
 *
 * @param module Unused
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	als_enabled = FALSE;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_filter_from_datapipe(&key_backlight_pipe,
				    key_backlight_filter);
	remove_filter_from_datapipe(&led_brightness_pipe,
				    led_brightness_filter);
	remove_filter_from_datapipe(&display_brightness_pipe,
				    display_brightness_filter);

	/* Remove all timer sources */
	cancel_als_poll_timer();

	return;
}
