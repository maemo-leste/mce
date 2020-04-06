/**
 * @file filter-brightness-als.h
 * Headers for the Ambient Light Sensor level adjusting filter module
 * for display backlight, key backlight, and LED brightness
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
#ifndef _FILTER_BRIGHTNESS_ALS_H_
#define _FILTER_BRIGHTNESS_ALS_H_


#define ALS_PATH_RX44		"/sys/devices/platform/i2c_omap.2/i2c-0/0-0029"
#define ALS_LUX_PATH_RX44		ALS_PATH_RX44 "/lux"
#define ALS_CALIB0_PATH_RX44		ALS_PATH_RX44 "/calib0"
#define ALS_CALIB1_PATH_RX44		ALS_PATH_RX44 "/calib1"


#define ALS_PATH_RX51		"/sys/class/i2c-adapter/i2c-2/2-0029"
#define ALS_LUX_PATH_RX51		ALS_PATH_RX51 "/lux"
#define ALS_CALIB0_PATH_RX51		ALS_PATH_RX51 "/calib0"
#define ALS_CALIB1_PATH_RX51		ALS_PATH_RX51 "/calib1"

#define ALS_PATH_RX51_3x	"/sys/class/i2c-adapter/i2c-2/2-0029/iio:device1"
#define ALS_LUX_PATH_RX51_3x		ALS_PATH_RX51_3x "/in_illuminance0_input"
#define ALS_CALIB0_PATH_RX51_3x		ALS_PATH_RX51_3x "/in_intensity_both_calibscale"
#define ALS_CALIB1_PATH_RX51_3x		ALS_PATH_RX51_3x "/in_intensity_ir_calibscale"

#define ALS_PATH_DROID4		"/sys/class/i2c-adapter/i2c-1/1-0044/iio:device0"
#define ALS_LUX_PATH_DROID4		ALS_PATH_DROID4 "/in_illuminance_input"

/** Path to the GConf settings for the display */
#ifndef MCE_GCONF_DISPLAY_PATH
#define MCE_GCONF_DISPLAY_PATH			"/system/osso/dsm/display"
#endif /* MCE_GCONF_DISPLAY_PATH */
/** Path to the ALS enabled GConf setting */
#define MCE_GCONF_DISPLAY_ALS_ENABLED_PATH	MCE_GCONF_DISPLAY_PATH "/als_enabled"

/** Default ALS polling frequency when the display is on */
#define ALS_DISPLAY_ON_POLL_FREQ	1500		/* Milliseconds */
/** Default ALS polling frequency when the display is dimmed */
#define ALS_DISPLAY_DIM_POLL_FREQ	5000		/* Milliseconds */
/**
 * Default ALS polling frequency when the display is off
 *
 * 0 disables polling completely;
 * with hardware that supports power saving
 * in a better way, 60000 should be used
 */
#define ALS_DISPLAY_OFF_POLL_FREQ	0		/* Milliseconds */
/**
 * Define this to re-initialise the median filter on display blank;
 * this will trigger a re-read on wakeup
 */
#define ALS_DISPLAY_OFF_FLUSH_FILTER

/** Window size for the median filter */
#define MEDIAN_FILTER_WINDOW_SIZE	5

/** CAL identifier for the ALS calibration values */
#define ALS_CALIB_IDENTIFIER		"als_calib"

/** ALS profile */
typedef struct {
	/** Lower and upper bound for each brightness range */
	gint range[5][2];
	gint value[6];					/* brightness in % */
} als_profile_struct;

/* 5 entries, one for each ALS_PROFILE_* level (MINIMUM, ECONOMY, etc)
 * Each entry contains the range of luminance/lux values, and what brightness
 * percentage of the full LED brightness it should map to.
 *
 * A lux 'level' will be calculated according to the ranges in the structure.
 * This lux 'level' will then be used to pick a percentage in brighness of the
 * 'max_brightness' value of the device, and set the 'brightness' of the device
 * to max_brightness * percentage.
 *
 * For example, for the Droid 4, with the lowest (ALS_PROFILE_MINIMUM)
 * brightness profile, and a lux value of 900 (quite bright!), the lux level
 * would be 3 (min is 0, max is 4), and the brightness owuld be set to 80%.
 * In fact, any lux level above 3 would result in 80% brightness, per the
 * current (in this example, MINIMUM) profile.
 */

als_profile_struct display_als_profiles_droid4[] = {
    /*
     *
     * Observed lux values in Amsterdam, (max is 2000):
     *
     * - Dark room, at night = 0
     * - Normal day without much (direct) sunlight, inside, phone facing up 10
     * - Normal day without much (direct) sunlight, inside, phone facing outside 100
     * - Sitting in direct sunlight, Droid4 not facing the sun = ~ 400-600
     * - Sitting in direct sunlight, Droid4 facing sun = ~1200-2000
     *
     */
	{
		{
			{ 10, 50 },
			{ 150, 200 },
			{ 300, 400 },
			{ 800, 1200 },
			{ 1300, 2000 },
		}, {20, 30, 50, 80, 80, 80}
	}, {
		{
			{ 10, 50 },
			{ 150, 200 },
			{ 300, 600 },
			{ 800, 1200 },
			{ 1300, 2000 },
		}, {30, 50, 70, 80, 100, 100}
	}, {
		{
			{ 10, 50 },
			{ 150, 200 },
			{ 300, 600 },
			{ 800, 1200 },
			{ 1300, 2000 },
		}, {50, 60, 80, 100, 100, 100}
	}, {
		{
			{ 10, 50 },
			{ 150, 200 },
			{ 300, 600 },
			{ 800, 1200 },
			{ 1300, 2000 },
		}, { 60, 70, 100, 100, 100, 100}
	}, {
		{
			{ 32, 64 },
			{ 160, 320},
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 100, 100, 100, 0, 0, 0 }
	}
};

als_profile_struct display_als_profiles_rx51[] = {
	{
		{
			{ 24, 32 },
			{ 160, 320},
			{ 720, 1200 },
			{ 14400, 17600 },
			{ -1, -1 },
		}, { 3, 10, 30, 50, 1, 0 }
	}, {
		{
                        { 24, 40 },
                        { 100, 200},
                        { 300, 500 },
                        { 720, 1200 },
			{ -1, -1 },
		}, { 10, 20, 40, 60, 80, 0 }
	}, {
		{
                        { 24, 40 },
                        { 100, 200},
                        { 300, 500 },
                        { 720, 1200 },
			{ -1, -1 },
		}, { 17, 30, 60, 90, 100, 0 }
	}, {
		{
			{ 24, 40 },
                        { 50, 70 },
			{ 60, 80 },
			{ 100, 160 },
			{ 200, 300 },
		}, { 25, 40, 60, 75, 90, 100}
	}, {
		{
			{ 32, 64 },
			{ 160, 320},
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 100, 100, 100, 0, 0, 0 }
	}
};

als_profile_struct display_als_profiles_rx44[] = {
	{
		{
			{ 10000, 13000 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 5, 20, 0, 0, 0, 0 }
	}, {
		{
			{ 2, 4 },
			{ 24, 45 },
			{ 260, 400 },
			{ 10000, 13000 },
			{ -1, -1 },
		}, { 5, 20, 40, 50, 70, 0 }
	}, {
		{
			{ 2, 4 },
			{ 24, 45 },
			{ 260, 400 },
			{ 10000, 13000 },
			{ -1, -1 },
		}, { 10, 20, 50, 80, 100, 0 }
	}, {
		{
			{ 2, 4 },
			{ 24, 45 },
			{ 260, 400 },
			{ 10000, 13000 },
			{ -1, -1 },
		}, { 30, 60, 80, 90, 100, 0 }
	}, {
		{
			{ 2, 4 },
			{ 8, 12 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 50, 80, 100, 0, 0, 0 }
	}
};

als_profile_struct led_als_profiles_droid4[] = {
	{
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{
			{ 32, 64 },
			{ 100, 1000 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 5, 5, 5, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}
};


als_profile_struct led_als_profiles_rx51[] = {
	{
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{
			{ 32, 64 },
			{ 100, 1000 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 5, 5, 5, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}
};

als_profile_struct led_als_profiles_rx44[] = {
	{
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{
			{ 3, 5 },
			{ 15, 27 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 10, 30, 50, 0, 0, 0 }
	}, {
		{
			{ 3, 5 },
			{ 15, 27 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 30, 50, 100, 0, 0, 0 }
	}, {
		{
			{ 3, 5 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 50, 100, 0, 0, 0, 0 }
	}
};

als_profile_struct kbd_als_profiles_droid4[] = {
	{
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{
			{ 0, 5},
			{ 10, 2000 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 25, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}
};

als_profile_struct kbd_als_profiles_rx51[] = {
	{
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{
			{ 24, 40 },
			{ 100, 1000 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 50, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}
};

als_profile_struct kbd_als_profiles_rx44[] = {
	{
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}, {
		{
			{ 3, 5 },
			{ 15, 27 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 50, 100, 0, 0, 0, 0 }
	}, {
		{
			{ 3, 5 },
			{ 15, 27 },
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 80, 100, 0, 0, 0, 0 }
	}, {
		{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
		{ 0, 0, 0, 0, 0, 0 }
	}
};

typedef enum {
	ALS_PROFILE_MINIMUM = 0,		/**< Minimum profile */
	ALS_PROFILE_ECONOMY,			/**< Economy profile */
	ALS_PROFILE_NORMAL,			/**< Normal profile */
	ALS_PROFILE_BRIGHT,			/**< Bright profile */
	ALS_PROFILE_MAXIMUM			/**< Maximum profile */
} als_profile_t;

#endif /* _FILTER_BRIGHTNESS_ALS_H_ */
