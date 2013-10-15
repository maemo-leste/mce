/**
 * @file tklock.h
 * Headers for the touchscreen/keypad lock component
 * of the Mode Control Entity
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
#ifndef _TKLOCK_H_
#define _TKLOCK_H_

#include <glib.h>

#ifndef MCE_GCONF_LOCK_PATH
/** Path to the GConf settings for the touchscreen/keypad lock */
#define MCE_GCONF_LOCK_PATH		"/system/osso/dsm/locks"
#endif /* MCE_GCONF_LOCK_PATH */


#define DISABLE_KEYPAD_PREVENT_TIMEOUT			60	/* 60 seconds */

#define MCE_RX51_KEYBOARD_SYSFS_DISABLE_PATH		"/sys/class/i2c-adapter/i2c-1/1-004a/twl4030_keypad/disable_kp"
#define MCE_KEYPAD_SYSFS_DISABLE_PATH			"/sys/devices/platform/omap2_mcspi.1/spi1.0/disable_kp"
#define MCE_RX44_KEYBOARD_SYSFS_DISABLE_PATH		"/sys/devices/platform/i2c_omap.2/i2c-0/0-0045/disable_kp"

#define MCE_TOUCHSCREEN_SYSFS_DISABLE_PATH		"/sys/devices/platform/omap2_mcspi.1/spi1.0/disable_ts"

/** Default fallback setting for the touchscreen/keypad autolock */
#define DEFAULT_TK_AUTOLOCK		FALSE		/* FALSE / TRUE */

/** Path to the touchscreen/keypad autolock GConf setting */
#define MCE_GCONF_TK_AUTOLOCK_ENABLED_PATH	MCE_GCONF_LOCK_PATH "/touchscreen_keypad_autolock_enabled"

/** Name of D-Bus callback to provide to Touchscreen/Keypad Lock SystemUI */
#define MCE_TKLOCK_CB_REQ		"tklock_callback"
/** Delay before the touchscreen/keypad is unlocked */
#define MCE_TKLOCK_UNLOCK_DELAY		500		/**< 0.5 seconds */

#ifndef MCE_CONF_TKLOCK_GROUP
/** Name of Touchscreen/Keypad lock configuration group */
#define MCE_CONF_TKLOCK_GROUP		"TKLock"
#endif /* MCE_CONF_TKLOCK_GROUP */

/** Name of configuration key for touchscreen/keypad immediate blanking */
#define MCE_CONF_BLANK_IMMEDIATELY	"BlankImmediately"

/** Name of configuration key for touchscreen/keypad immediate dimming */
#define MCE_CONF_DIM_IMMEDIATELY	"DimImmediately"

/** Name of configuration key for touchscreen/keypad dim timeout */
#define MCE_CONF_DIM_DELAY		"DimDelay"

/** Name of configuration key for touchscreen immediate disabling */
#define MCE_CONF_TS_OFF_IMMEDIATELY	"DisableTSImmediately"

/** Name of configuration key for keypad immediate disabling */
#define MCE_CONF_KP_OFF_IMMEDIATELY	"DisableKPImmediately"

/** Name of configuration key for keyboard slide autolock */
#define MCE_CONF_AUTOLOCK_SLIDE_OPEN	"AutolockWhenSlideOpen"

/** Name of configuration key for lens cover triggered tklock unlocking */
#define MCE_CONF_LENS_COVER_UNLOCK	"LensCoverUnlock"

/** Default fallback setting for tklock immediate blanking */
#define DEFAULT_BLANK_IMMEDIATELY	FALSE		/* FALSE / TRUE */

/** Default fallback setting for tklock immediate dimming */
#define DEFAULT_DIM_IMMEDIATELY		FALSE		/* FALSE / TRUE */

/** Default visual lock blank timeout */
#define DEFAULT_VISUAL_BLANK_DELAY	5000		/* 5 seconds */

/** Default visual lock blank timeout */
#define DEFAULT_VISUAL_FORCED_BLANK_DELAY	30000	/* 30 seconds */

/** Default delay before the display dims */
#define DEFAULT_DIM_DELAY		3000		/* 3 seconds */

/** Default fallback setting for touchscreen immediate disabling */
#define DEFAULT_TS_OFF_IMMEDIATELY	TRUE		/* FALSE / TRUE */

/** Default fallback setting for keypad immediate disabling */
#define DEFAULT_KP_OFF_IMMEDIATELY	2		/* 0 / 1 / 2 */

/** Default fallback setting for autolock with open keyboard slide */
#define DEFAULT_AUTOLOCK_SLIDE_OPEN	FALSE		/* FALSE */

/** Default fallback setting for lens cover triggered tklock unlocking */
#define DEFAULT_LENS_COVER_UNLOCK	TRUE		/* TRUE */

/** Default fallback setting for proximity lock when callstate == ringing */
#define DEFAULT_PROXIMITY_LOCK_WHEN_RINGING	TRUE		/* TRUE */

#define DEFAULT_PROXIMITY_UNLOCK_DELAY 500 /* 0.5 second */

/* when MCE is made modular, this will be handled differently
 */
gboolean mce_tklock_init(void);
void mce_tklock_exit(void);

#endif /* _TKLOCK_H_ */
