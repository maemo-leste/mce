#ifndef _BUTTON_BACKLIGHT_H_
#define _BUTTON_BACKLIGHT_H
#include <stdbool.h>

#define MCE_ALS_ENABLED_KEY	"als_enabled"

#define MCE_CONF_BACKLIGHT_GROUP	"Backlights"
#define MCE_CONF_CONFIGURED_LIGHTS	"ConfiguredLights"
#define MCE_CONF_COUNT_BACKLIGHT_FIELDS 6

#define MCE_BUTTON_BACKLIGHT_BRIGHTNESS_VALUES 5

#define LED_SYSFS_PATH "/sys/class/leds/"

#define LED_BRIGHTNESS_PATH "/brightness"

typedef enum {
	BACKLIGHT_HIDDEN_FIELD = 0,
	BACKLIGHT_IS_KEYBOARD_FIELD = 1,
	BACKLIGHT_ON_WHEN_DIMMED_FIELD = 2,
	BACKLIGHT_LOCKED_FIELD = 3,
	BACKLIGHT_FADE_TIME_FIELD = 4,
	BACKLIGHT_PROFILE_FIELD = 5,
	NUMBER_OF_PATTERN_FIELDS
} backlight_field;

struct brightness {
	int lux[MCE_BUTTON_BACKLIGHT_BRIGHTNESS_VALUES];
	int value[MCE_BUTTON_BACKLIGHT_BRIGHTNESS_VALUES];
};

struct button_backlight{
	char *file_sysfs;
	unsigned int value;
	bool hidden_by_slider;
	bool is_keyboard;
	bool on_when_dimmed;
	bool locked;
	unsigned int fade_time;
	const struct brightness *brightness_map;
};

#endif
