/**
 * @file keypad.h
 * Headers for the keypad module
 * <p>
 * Copyright © 2004-2009 Nokia Corporation and/or its subsidiary(-ies).
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
#ifndef _KEYPAD_H_
#define _KEYPAD_H_

#include <glib.h>

#define MCE_KEYPAD_BACKLIGHT_SYS_PATH			"/sys/class/leds/cover"
#define MCE_KEYPAD_BACKLIGHT_BRIGHTNESS_SYS_PATH	MCE_KEYPAD_BACKLIGHT_SYS_PATH "/brightness"
#define MCE_KEYPAD_BACKLIGHT_FADETIME_SYS_PATH		MCE_KEYPAD_BACKLIGHT_SYS_PATH "/time"

#define MCE_KEYBOARD_BACKLIGHT_SYS_PATH			"/sys/class/leds/keyboard"
#define MCE_KEYBOARD_BACKLIGHT_BRIGHTNESS_SYS_PATH	MCE_KEYBOARD_BACKLIGHT_SYS_PATH "/brightness"
#define MCE_KEYBOARD_BACKLIGHT_FADETIME_SYS_PATH	MCE_KEYBOARD_BACKLIGHT_SYS_PATH "/time"

#include "led.h"

/** Default Lysti backlight LED current */
#define DEFAULT_LYSTI_BACKLIGHT_LED_CURRENT		50	/* 5 mA */

/** Default key backlight brightness */
#define DEFAULT_KEY_BACKLIGHT_LEVEL			255

/** Default key backlight timeout in seconds */
#define DEFAULT_KEY_BACKLIGHT_TIMEOUT			30	/* 30 s */

/** Default key backlight fadeout time in milliseconds */
#define DEFAULT_KEY_BACKLIGHT_FADETIME			100	/* 100 ms */

#ifndef MCE_CONF_KEYPAD_GROUP
/** Name of Keypad configuration group */
#define MCE_CONF_KEYPAD_GROUP		"KeyPad"
#endif /* MCE_CONF_KEYPAD_GROUP */

#define MCE_CONF_KEY_BACKLIGHT_TIMEOUT	"BacklightTimeout"

#define MCE_CONF_KEY_BACKLIGHT_FADETIME	"BacklightFadeTime"

#endif /* _KEYPAD_H_ */
