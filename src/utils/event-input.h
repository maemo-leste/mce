/**
 * @file event-input.h
 * Headers for the /dev/input event provider for the Mode Control Entity
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
#ifndef _EVENT_INPUT_H_
#define _EVENT_INPUT_H_

#include <glib.h>

#include <linux/input.h>	/* KEY_POWER */

/**
 * List of drivers that provide touchscreen events
 */
static const gchar *const touchscreen_event_drivers[] = {
	/** TSC2005 touchscreen */
	"TSC2005 touchscreen",

	/** TSC2301 touchscreen */
	"TSC2301 touchscreen",

	/** ADS784x touchscreen */
	"ADS784x touchscreen",

	/* Motorola Droid 4 touchscreen */
	"Atmel maXTouch Touchscreen",

	/** No more entries */
	NULL
};

/**
 * List of drivers that provide keyboard events
 */
static const gchar *const keyboard_event_drivers[] = {
	/** Legacy input layer name for the TWL4030 keyboard/keypad */
	"omap_twl4030keypad",

	/** Generic input layer name for keyboard/keypad */
	"Internal keyboard",

	/** Input layer name for the LM8323 keypad */
	"LM8323 keypad",

	/** Generic input layer name for keypad */
	"Internal keypad",

	/** Input layer name for the TSC2301 keypad */
	"TSC2301 keypad",

	/** Legacy generic input layer name for keypad */
	"omap-keypad",

	/** Input layer name for standard PC keyboards */
	"AT Translated Set 2 keyboard",

	/** Input layer name for the Triton 2 power button */
	"triton2-pwrbutton",
	"twl4030_pwrbutton",

	/** Input layer name for the Retu powerbutton */
	"retu-pwrbutton",

	/* Droid4 power button */
	"cpcap-pwrbutton",

	/** No more entries */
	NULL
};

/**
 * List of event types for switch monitor
 */
static const int switch_event_types[] = {
	EV_SW,
	EV_KEY,
	/** No more entries */
	-1
};

/**
 * List of key types for switch monitor
 */
static const int event_switches[] = {
	SW_CAMERA_LENS_COVER,
	SW_KEYPAD_SLIDE,
	SW_FRONT_PROXIMITY,
	-1
};

static const int event_keys[] = {
	KEY_SCREENLOCK,
	KEY_CAMERA,
	KEY_CAMERA_FOCUS,
	-1
};

static const int *const switch_event_keys[]= {
	event_switches,
	event_keys
};


/**
 * List of event types for power keys
 */
static const int power_event_types[] = {
	EV_KEY,
	/** No more entries */
	-1
};

/**
 * List of key types for power monitor
 */
static const int power_switches[] = {
	KEY_POWER,
	-1
};

static const int *const power_event_keys[]= {
	power_switches,
};

/**
 * List of event types for touchscreen keys
 */
static const int touch_event_types[] = {
	EV_KEY,
	/** No more entries */
	-1
};

/**
 * List of key types for touchscreen monitor
 */
static const int touch_keys[] = {
	BTN_TOUCH,
	-1
};

static const int *const touch_event_keys[]= {
	touch_keys,
};

static const int keyboard_event_types[] = {
	EV_KEY,
	/** No more entries */
	-1
};

static const int keyboard_keys[]= {
	KEY_Q,
	KEY_W,
	KEY_E,
	KEY_R,
	KEY_T,
	KEY_Y,
	KEY_U,
	KEY_I,
	KEY_O,
	KEY_P,
	KEY_A,
	KEY_S,
	KEY_D,
	KEY_F,
	KEY_G,
	KEY_H,
	KEY_J,
	KEY_K,
	KEY_L,
	KEY_Z,
	KEY_X,
	KEY_C,
	KEY_V,
	KEY_B,
	KEY_N,
	KEY_M,
	-1
};

static const int *const keyboard_event_keys[]= {
	keyboard_keys,
};

/**
 * List of drivers that we should not monitor
 */
static const gchar *const driver_blacklist[] = {
	/** Input layer name for the ST LIS302DL accelerometer */
	"ST LIS302DL Accelerometer",
	"ST LIS3LV02DL Accelerometer",

	/** No more entries */
	NULL
};

#define MONITORING_DELAY		1


/* When MCE is made modular, this will be handled differently */
gboolean mce_input_init(void);
void mce_input_exit(void);

#endif /* _EVENT_INPUT_H_ */
