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
#include "filter-brightness-als-iio.h"
#include "mce-io.h"
#include "mce-log.h"
#include "mce-rtconf.h"
#include "datapipe.h"

/** Module name */
#define MODULE_NAME		"filter-brightness-als-iio"

/** Functionality provided by this module */
static const gchar *const provides[] = {
	"display-brightness-filter",
	NULL
};

/** Functionality that this module enhances */
static const gchar *const enhances[] = {
	"display-brightness",
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

static gboolean als_enabled = TRUE;
static gint als_lux = -1;
static als_profile_struct *display_als_profiles = display_als_profiles_generic;

/** Display state */
static display_state_t display_state = MCE_DISPLAY_UNDEF;


/**
 * rtconf callback for ALS settings
 *
 * @param key Unused
 * @param cb_id Connection ID from gconf_client_notify_add()
 * @param user_data Unused
 */
static void als_rtconf_cb(gchar *key, guint cb_id, void *user_data)
{
	(void)key;
	(void)user_data;

	if (cb_id == als_enabled_gconf_cb_id)
		mce_rtconf_get_bool(MCE_GCONF_DISPLAY_ALS_ENABLED_PATH, &als_enabled);
	else
		mce_log(LL_WARN, "%s: Spurious GConf value received; confused!", MODULE_NAME);
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
	
	/* If the display is off, don't update its brightness */
	if (display_state == MCE_DISPLAY_OFF)
		return GINT_TO_POINTER(0);

	/* Safety net */
	if (raw < ALS_PROFILE_MINIMUM)
		raw = ALS_PROFILE_MINIMUM;
	else if (raw > ALS_PROFILE_MAXIMUM)
		raw = ALS_PROFILE_MAXIMUM;
	
	gint percentage;

	if (als_enabled == TRUE && als_lux > -1) {
		percentage = filter_data(display_als_profiles, raw,
					      als_lux, &display_als_level);

		raw = percentage;
	} else {
		percentage = (raw + 1) * 20;
	}

	return GINT_TO_POINTER(percentage);
}


static void als_trigger(gconstpointer data)
{
	(void)data;
	
	int new_als_lux = datapipe_get_gint(light_sensor_pipe);
	
	if(new_als_lux < 0)
		return;

	als_lux = new_als_lux;
	
	/* Re-filter the brightness */
	(void)execute_datapipe(&display_brightness_pipe, NULL, USE_CACHE, DONT_CACHE_INDATA);
}

/**
 * Handle display state change
 *
 * @param data The display stated stored in a pointer
 */
static void display_state_trigger(gconstpointer data)
{
	display_state = GPOINTER_TO_INT(data);
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
	append_filter_to_datapipe(&display_brightness_pipe, display_brightness_filter);
	append_output_trigger_to_datapipe(&display_state_pipe, display_state_trigger);
	append_output_trigger_to_datapipe(&light_sensor_pipe, als_trigger);

	/* ALS enabled */
	/* Since we've set a default, error handling is unnecessary */
	(void)mce_rtconf_get_bool(MCE_GCONF_DISPLAY_ALS_ENABLED_PATH, &als_enabled);
	
	

	if (mce_rtconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				   MCE_GCONF_DISPLAY_ALS_ENABLED_PATH,
				   als_rtconf_cb, NULL,
				   &als_enabled_gconf_cb_id) == FALSE)
		goto EXIT;

	(void)execute_datapipe(&display_brightness_pipe, NULL,
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
	remove_filter_from_datapipe(&display_brightness_pipe,
				    display_brightness_filter);
}
