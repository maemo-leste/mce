/**
 * @file led.h
 * Headers for the LED module
 * <p>
 * Copyright © 2006-2010 Nokia Corporation and/or its subsidiary(-ies).
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
#ifndef _LED_H_
#define _LED_H_

#include <glib.h>

#define MCE_CONF_LED_GROUP			"LED"

#define MCE_CONF_LED_PATTERNS			"LEDPatterns"

#define MCE_CONF_LED_PATTERN_RX34_GROUP		"LEDPatternMonoRX34"

#define MCE_CONF_LED_PATTERN_RX44_GROUP		"LEDPatternNJoyRX44"

#define MCE_CONF_LED_PATTERN_RX48_GROUP		"LEDPatternNJoyRX48"

#define MCE_CONF_LED_PATTERN_RX51_GROUP		"LEDPatternLystiRX51"

#define MCE_GCONF_LED_PATH			"/system/osso/dsm/leds"

#define DEFAULT_PATTERN_ENABLED			TRUE

#define DEFAULT_NJOY_LED_CURRENT		2	/* 4.7 mA */

#define DEFAULT_LYSTI_RGB_LED_CURRENT		47	/* 4.7 mA */

#define MCE_MONO_LED_SYS_PATH			"/sys/class/leds/keypad"
#define MCE_MONO_LED_BRIGHTNESS_PATH		MCE_MONO_LED_SYS_PATH "/brightness"
#define MCE_LED_ON_PERIOD_PATH			MCE_MONO_LED_SYS_PATH "/delay_on"
#define MCE_LED_OFF_PERIOD_PATH			MCE_MONO_LED_SYS_PATH "/delay_off"
#define MCE_LED_TRIGGER_PATH			MCE_MONO_LED_SYS_PATH "/trigger"

#define MCE_LED_TRIGGER_TIMER			"timer"
#define MCE_LED_TRIGGER_NONE			"none"


#define MCE_NJOY_LED_SYS_PATH			"/sys/devices/platform/i2c_omap.2/i2c-0/0-0032"
#define MCE_NJOY_LED_BRIGHTNESS_PATH		MCE_NJOY_LED_SYS_PATH "/led_current"
#define MCE_NJOY_LED_MODE_PATH			MCE_NJOY_LED_SYS_PATH "/mode"
#define MCE_NJOY_LED_LOAD_PATH			MCE_NJOY_LED_SYS_PATH "/load"
#define MCE_NJOY_LED_COLOUR_PATH		MCE_NJOY_LED_SYS_PATH "/color"
#define MCE_NJOY_LED_CHANNELS_PATH		MCE_NJOY_LED_SYS_PATH "/active_channels"

#define MCE_LYSTI_DIRECT_SYS_PATH		"/sys/class/leds/lp5523"

#define MCE_LYSTI_DIRECT_KB1_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb1/led_current"
#define MCE_LYSTI_DIRECT_KB2_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb2/led_current"
#define MCE_LYSTI_DIRECT_KB3_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb3/led_current"
#define MCE_LYSTI_DIRECT_KB4_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb4/led_current"
#define MCE_LYSTI_DIRECT_KB5_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb5/led_current"
#define MCE_LYSTI_DIRECT_KB6_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb6/led_current"

#define MCE_LYSTI_DIRECT_R_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":r/led_current"
#define MCE_LYSTI_DIRECT_G_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":g/led_current"
#define MCE_LYSTI_DIRECT_B_LED_CURRENT_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":b/led_current"

#define MCE_LYSTI_DIRECT_KB1_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb1/brightness"
#define MCE_LYSTI_DIRECT_KB2_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb2/brightness"
#define MCE_LYSTI_DIRECT_KB3_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb3/brightness"
#define MCE_LYSTI_DIRECT_KB4_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb4/brightness"
#define MCE_LYSTI_DIRECT_KB5_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb5/brightness"
#define MCE_LYSTI_DIRECT_KB6_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":kb6/brightness"

#define MCE_LYSTI_DIRECT_R_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":r/brightness"
#define MCE_LYSTI_DIRECT_G_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":g/brightness"
#define MCE_LYSTI_DIRECT_B_BRIGHTNESS_PATH	MCE_LYSTI_DIRECT_SYS_PATH ":b/brightness"


#define MCE_LYSTI_ENGINE_SYS_PATH		"/sys/class/i2c-adapter/i2c-2/2-0032"

#define MCE_LYSTI_ENGINE1_MODE_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine1_mode"
#define MCE_LYSTI_ENGINE2_MODE_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine2_mode"
#define MCE_LYSTI_ENGINE3_MODE_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine3_mode"

#define MCE_LYSTI_ENGINE1_LOAD_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine1_load"
#define MCE_LYSTI_ENGINE2_LOAD_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine2_load"
#define MCE_LYSTI_ENGINE3_LOAD_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine3_load"

#define MCE_LYSTI_ENGINE1_LEDS_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine1_leds"
#define MCE_LYSTI_ENGINE2_LEDS_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine2_leds"
#define MCE_LYSTI_ENGINE3_LEDS_PATH		MCE_LYSTI_ENGINE_SYS_PATH "/engine3_leds"

#define MCE_LYSTI_KEYB6_MASK			(1 << 0)
#define MCE_LYSTI_KEYB5_MASK			(1 << 1)
#define MCE_LYSTI_RED_MASK			(1 << 2)
#define MCE_LYSTI_GREEN_MASK			(1 << 3)
#define MCE_LYSTI_BLUE_MASK			(1 << 4)
#define MCE_LYSTI_KEYB4_MASK			(1 << 5)
#define MCE_LYSTI_KEYB3_MASK			(1 << 6)
#define MCE_LYSTI_KEYB2_MASK			(1 << 7)
#define MCE_LYSTI_KEYB1_MASK			(1 << 8)

#define MCE_LED_DISABLED_MODE			"disabled"
#define MCE_LED_DIRECT_MODE			"direct"
#define MCE_LED_LOAD_MODE			"load"
#define MCE_LED_RUN_MODE			"run"

#define BRIGHTNESS_LEVEL_0			"0"	/**< off */
#define BRIGHTNESS_LEVEL_1			"12"	/**< faintest */
#define BRIGHTNESS_LEVEL_2			"24"	/**< level 2 */
#define BRIGHTNESS_LEVEL_3			"36"	/**< level 3 */
#define BRIGHTNESS_LEVEL_4			"48"	/**< level 4 */
#define BRIGHTNESS_LEVEL_5			"60"	/**< level 5 */
#define BRIGHTNESS_LEVEL_6			"72"	/**< level 6 */
#define BRIGHTNESS_LEVEL_7			"84"	/**< level 7 */
#define BRIGHTNESS_LEVEL_8			"96"	/**< level 8 */
#define BRIGHTNESS_LEVEL_9			"108"	/**< level 9 */
#define BRIGHTNESS_LEVEL_10			"120"	/**< level 10 */
#define BRIGHTNESS_LEVEL_11			"132"	/**< level 11 */
#define BRIGHTNESS_LEVEL_12			"144"	/**< level 12 */
#define BRIGHTNESS_LEVEL_13			"156"	/**< level 13 */
#define BRIGHTNESS_LEVEL_14			"168"	/**< level 14 */
#define BRIGHTNESS_LEVEL_15			"180"	/**< brightest */

#endif /* _LED_H_ */
