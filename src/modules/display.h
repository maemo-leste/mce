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

/** Path to the SysFS entry for the generic display interface */
#define DISPLAY_GENERIC_PATH			"/sys/class/backlight/"
/** Generic brightness file */
#define DISPLAY_GENERIC_BRIGHTNESS_FILE		"/brightness"
/** Generic maximum brightness file */
#define DISPLAY_GENERIC_MAX_BRIGHTNESS_FILE	"/max_brightness"

#define MCE_CONF_DISPLAY_GROUP "Display"
#define MCE_CONF_DISPLAY_BLANK_KEY "DimToBlankTimeout"

#define MCE_BRIGHTNESS_KEY	"display_brightness"

#define DEFAULT_DISP_BRIGHTNESS			3	/* 60% */
#define DEFAULT_BLANK_TIMEOUT			3	/* 3 seconds */
#define DEFAULT_ACTDEAD_DIM_TIMEOUT		5	/* 5 seconds */
#define BOOTUP_DIM_ADDITIONAL_TIMEOUT		60	/* 60 seconds */

/**
 * Default maximum brightness;
 * used if the maximum brightness cannot be read from SysFS
 */
#define DEFAULT_MAXIMUM_DISPLAY_BRIGHTNESS	127
#define DEFAULT_DIM_BRIGHTNESS			10
#define DEFAULT_ENABLE_POWER_SAVING		TRUE

#endif /* _DISPLAY_H_ */
