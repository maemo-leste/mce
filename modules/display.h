/**
 * @file display.h
 * Headers for the display module
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
#ifndef _DISPLAY_H_
#define _DISPLAY_H_

/** Path to the SysFS entry for the CABC controls */
#define DISPLAY_CABC_PATH			"/sys/class/backlight"
/** CABC brightness file */
#define DISPLAY_CABC_BRIGHTNESS_FILE		"/brightness"
/** CABC maximum brightness file */
#define DISPLAY_CABC_MAX_BRIGHTNESS_FILE	"/max_brightness"
/** CABC mode file */
#define DISPLAY_CABC_MODE_FILE			"/cabc_mode"
/** CABC available modes file */
#define DISPLAY_CABC_AVAILABLE_MODES_FILE	"/cabc_available_modes"

/** The name of the directory for the Sony acx565akm display */
#define DISPLAY_ACX565AKM			"/acx565akm"
/** The name of the directory for the EID l4f00311 display */
#define DISPLAY_L4F00311			"/l4f00311"

/** CABC name for CABC disabled */
#define CABC_MODE_OFF				"off"
/** CABC name for UI mode */
#define CABC_MODE_UI				"ui"
/** CABC name for still image mode */
#define CABC_MODE_STILL_IMAGE			"still-image"
/** CABC name for moving image mode */
#define CABC_MODE_MOVING_IMAGE			"moving-image"

/** Default CABC mode */
#define CABC_MODE_DEFAULT			CABC_MODE_MOVING_IMAGE

/** Path to the SysFS entry for the generic display interface */
#define DISPLAY_GENERIC_PATH			"/sys/class/backlight/"
/** Generic brightness file */
#define DISPLAY_GENERIC_BRIGHTNESS_FILE		"/brightness"
/** Generic maximum brightness file */
#define DISPLAY_GENERIC_MAX_BRIGHTNESS_FILE	"/max_brightness"

/** Path to the framebuffer device */
#define FB_DEVICE				"/dev/fb0"

/** Path to the GConf settings for the display */
#ifndef MCE_GCONF_DISPLAY_PATH
#define MCE_GCONF_DISPLAY_PATH			"/system/osso/dsm/display"
#endif /* MCE_GCONF_DISPLAY_PATH */
#define MCE_GCONF_DISPLAY_BRIGHTNESS_PATH	MCE_GCONF_DISPLAY_PATH "/display_brightness"
#define MCE_GCONF_DISPLAY_DIM_TIMEOUT_PATH	MCE_GCONF_DISPLAY_PATH "/display_dim_timeout"
#define MCE_GCONF_DISPLAY_BLANK_TIMEOUT_PATH	MCE_GCONF_DISPLAY_PATH "/display_blank_timeout"
#define MCE_GCONF_BLANKING_INHIBIT_MODE_PATH	MCE_GCONF_DISPLAY_PATH "/inhibit_blank_mode"
#define MCE_GCONF_ENABLE_POWER_SAVING_PATH	MCE_GCONF_DISPLAY_PATH "/enable_power_saving"

#define DEFAULT_DISP_BRIGHTNESS			3	/* 60% */
#define DEFAULT_BLANK_TIMEOUT			3	/* 3 seconds */
#define DEFAULT_DIM_TIMEOUT			30	/* 30 seconds */
#define DEFAULT_ACTDEAD_DIM_TIMEOUT		5	/* 5 seconds */
#define BOOTUP_DIM_ADDITIONAL_TIMEOUT		60	/* 60 seconds */

/**
 * Blank prevent timeout, in seconds;
 * Don't alter this, since this is part of the defined behaviour
 * for blanking inhibit that applications rely on
 */
#define BLANK_PREVENT_TIMEOUT			60	/* 60 seconds */

/**
 * Default maximum brightness;
 * used if the maximum brightness cannot be read from SysFS
 */
#define DEFAULT_MAXIMUM_DISPLAY_BRIGHTNESS	127
#define DEFAULT_DIM_BRIGHTNESS			3
#define DEFAULT_ENABLE_POWER_SAVING		TRUE

/** Maximum number of monitored services that calls blanking pause */
#define MAX_MONITORED_SERVICES			5

#endif /* _DISPLAY_H_ */
