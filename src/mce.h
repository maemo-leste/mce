/**
 * @file mce.h
 * Generic headers for Mode Control Entity
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
#ifndef _MCE_H_
#define _MCE_H_

#include <glib.h>
#include <locale.h>

#include "datapipe.h"

#ifdef ENABLE_NLS
#include <libintl.h>
/** _() to use when NLS is enabled */
#define _(__str)		gettext(__str)
#else
#undef bindtextdomain
/** Dummy bindtextdomain to use when NLS is disabled */
#define bindtextdomain(__domain, __directory)
#undef textdomain
/** Dummy textdomain to use when NLS is disabled */
#define textdomain(__domain)
/** Dummy _() to use when NLS is disabled */
#define _(__str)		__str
#endif /* ENABLE_NLS */

#define MCE_DEVLOCK_FILENAME		G_STRINGIFY(MCE_RUN_DIR) "/call"

/** Indicate enabled (sub)mode */
#define DISABLED_STRING			"yes"
/** Indicate disabled (sub)mode */
#define ENABLED_STRING			"no"

/* Names of LED patterns */

/** LED pattern used when powering on the device */
#define MCE_LED_PATTERN_POWER_ON		"PatternPowerOn"
/** LED pattern used when powering off the device */
#define MCE_LED_PATTERN_POWER_OFF		"PatternPowerOff"
/** LED pattern used when camera is active */
#define MCE_LED_PATTERN_CAMERA			"PatternWebcamActive"
/** LED pattern used to indicate that the device is on when idle */
#define MCE_LED_PATTERN_DEVICE_ON		"PatternDeviceOn"
/** LED pattern used when the device is in soft poweroff mode */
#define MCE_LED_PATTERN_DEVICE_SOFT_OFF		"PatternDeviceSoftOff"
/** LED pattern used when charging the battery */
#define MCE_LED_PATTERN_BATTERY_CHARGING	"PatternBatteryCharging"
/** LED pattern used when the battery is full */
#define MCE_LED_PATTERN_BATTERY_FULL		"PatternBatteryFull"
/** LED pattern used when the battery is low */
#define MCE_LED_PATTERN_BATTERY_LOW		"PatternBatteryLow"

#define MCE_VIBRATOR_PATTERN_POWER_KEY_PRESS	"PatternPowerKeyPress"

/** Module information */
typedef struct {
/** Name of the module */
	const gchar *const name;
/** Module dependencies */
	const gchar *const *const depends;
/** Module recommends */
	const gchar *const *const recommends;
/** Module provides */
	const gchar *const *const provides;
/** Module provides */
	const gchar *const *const enhances;
/** Module conflicts */
	const gchar *const *const conflicts;
/** Module replaces */
	const gchar *const *const replaces;
/** Module priority:
 * lower value == higher priority
 * This value is only used when modules conflict
 */
	const gint priority;
} module_info_struct;

/** The GMainLoop used by MCE */
extern GMainLoop *mainloop;

/** Used for invalid translations and values */
#define MCE_INVALID_TRANSLATION		-1

typedef enum {
	MCE_INVALID_MODE_INT32 = MCE_INVALID_TRANSLATION,
	MCE_NORMAL_MODE_INT32 = 0,
	MCE_NORMAL_MODE_CONFIRM_INT32 = 1,
	MCE_FLIGHT_MODE_INT32 = 2,
	MCE_FLIGHT_MODE_CONFIRM_INT32 = 3,
	MCE_OFFLINE_MODE_INT32 = MCE_FLIGHT_MODE_INT32,
	MCE_OFFLINE_MODE_CONFIRM_INT32 = MCE_FLIGHT_MODE_CONFIRM_INT32,
} device_mode_t;

/** Alarm UI states; integer representations */
typedef enum {
	/** Alarm UI state not valid */
	MCE_ALARM_UI_INVALID_INT32 = MCE_INVALID_TRANSLATION,
	/** Alarm UI not visible */
	MCE_ALARM_UI_OFF_INT32 = 0,
	/** Alarm UI visible and ringing */
	MCE_ALARM_UI_RINGING_INT32 = 1,
	/** Alarm UI visible but not ringing */
	MCE_ALARM_UI_VISIBLE_INT32 = 2,
} alarm_ui_state_t;

/** System sub-modes; several of these can be active at once */
typedef gint submode_t;

/** Submode invalid */
#define MCE_INVALID_SUBMODE		(1 << 31)
/** No submodes enabled */
#define MCE_NORMAL_SUBMODE		0
/** Touchscreen/Keypad lock enabled */
#define MCE_TKLOCK_SUBMODE		(1 << 0)
/** Device lock enabled */
#define MCE_DEVLOCK_SUBMODE		(1 << 1)
/** Modechange active */
#define MCE_MODECHG_SUBMODE		(1 << 2)
/** Device menu active */
#define MCE_DEVMENU_SUBMODE		(1 << 3)
/** Event eater enabled */
#define MCE_EVEATER_SUBMODE		(1 << 4)
/** Device emulates soft poweroff */
#define MCE_SOFTOFF_SUBMODE		(1 << 5)
/** Bootup in progress */
#define MCE_BOOTUP_SUBMODE		(1 << 6)
/** State transition in progress */
#define MCE_TRANSITION_SUBMODE		(1 << 7)
/** Device lock verify active */
#define MCE_VERIFY_SUBMODE		(1 << 8)
/** Touchscreen/Keypad autorelock active */
#define MCE_AUTORELOCK_SUBMODE		(1 << 9)
/** Visual Touchscreen/Keypad active */
#define MCE_VISUAL_TKLOCK_SUBMODE	(1 << 10)

/** System state */
typedef enum {
	MCE_STATE_UNDEF = -1,		/**< System state not set */
	MCE_STATE_SHUTDOWN = 0,		/**< System is in shutdown state */
	MCE_STATE_USER = 2,		/**< System is in user state */
	MCE_STATE_ACTDEAD = 5,		/**< System is in acting dead state */
	MCE_STATE_REBOOT = 6,		/**< System is in reboot state */
	MCE_STATE_BOOT = 9		/**< System is in bootup state */
} system_state_t;

typedef enum {
	MCE_POWER_REQ_UNDEF,
	MCE_POWER_REQ_OFF,
	MCE_POWER_REQ_SOFT_OFF,
	MCE_POWER_REQ_ON,
	MCE_POWER_REQ_SOFT_ON,
	MCE_POWER_REQ_REBOOT
} power_req_t;

/** Call state */
typedef enum {
	/** Invalid call state */
	CALL_STATE_INVALID = MCE_INVALID_TRANSLATION,
	/** No call on-going */
	CALL_STATE_NONE = 0,
	/** There's an incoming call ringing */
	CALL_STATE_RINGING = 1,
	/** There's an active call */
	CALL_STATE_ACTIVE = 2,
	/** The device is in service state */
	CALL_STATE_SERVICE = 3
} call_state_t;

/** Call type */
typedef enum {
	/** Invalid call type */
	INVALID_CALL = MCE_INVALID_TRANSLATION,
	/** The call is a normal call */
	NORMAL_CALL = 0,
	/** The call is an emergency call */
	EMERGENCY_CALL = 1
} call_type_t;

/** Display state */
typedef enum {
	MCE_DISPLAY_UNDEF = -1,		/**< Display state not set */
	MCE_DISPLAY_OFF	= 0,		/**< Display is off */
	MCE_DISPLAY_DIM = 1,		/**< Display is dimmed */
	MCE_DISPLAY_ON = 2		/**< Display is on */
} display_state_t;

/** Cover state */
typedef enum {
	COVER_UNDEF = -1,		/**< Cover state not set */
	COVER_CLOSED = 0,		/**< Cover is closed */
	COVER_OPEN = 1			/**< Cover is open */
} cover_state_t;

/** Lock state */
typedef enum {
	/** Lock state not set */
	LOCK_UNDEF = -1,
	/** Lock is disabled */
	LOCK_OFF = 0,
	/** Delayed unlock; write only */
	LOCK_OFF_DELAYED = 1,
	/** Silent unlock */
	LOCK_OFF_SILENT = 2,
	/** Lock is enabled */
	LOCK_ON = 3,
	/** Dimmed lock; write only */
	LOCK_ON_DIMMED = 4,
	/** Silent lock */
	LOCK_ON_SILENT = 5,
	/** Silent dimmed lock */
	LOCK_ON_SILENT_DIMMED = 6,
	/** Toggle lock state; write only */
	LOCK_TOGGLE = 7
} lock_state_t;

/** Battery status */
typedef enum {
	BATTERY_STATUS_UNDEF = -1,	/**< Battery status not known */
	BATTERY_STATUS_FULL = 0,	/**< Battery full */
	BATTERY_STATUS_OK = 1,		/**< Battery ok */
	BATTERY_STATUS_LOW = 2,		/**< Battery low */
	BATTERY_STATUS_EMPTY = 3,	/**< Battery empty */
} battery_status_t;

/** Camera button state */
typedef enum {
	CAMERA_BUTTON_UNDEF = -1,	/**< Camera button state not set */
	CAMERA_BUTTON_UNPRESSED = 0,	/**< Camera button not pressed */
	CAMERA_BUTTON_LAUNCH = 1,	/**< Camera button fully pressed */
} camera_button_state_t;

/** Audio route */
typedef enum {
	/** Audio route not defined */
	AUDIO_ROUTE_UNDEF = -1,
	/** Audio routed to handset */
	AUDIO_ROUTE_HANDSET = 0,
	/** Audio routed to speaker */
	AUDIO_ROUTE_SPEAKER = 1,
	/** Audio routed to headset */
	AUDIO_ROUTE_HEADSET = 2,
} audio_route_t;

/** USB cable state */
typedef enum {
	USB_CABLE_UNDEF = -1,		/**< Usb cable state not set */
	USB_CABLE_DISCONNECTED = 0,	/**< Cable is not connected */
	USB_CABLE_CONNECTED = 1		/**< Cable is connected */
} usb_cable_state_t;

/** State of device; read only */
extern datapipe_struct device_inactive_pipe;
/** LED pattern to activate; read only */
extern datapipe_struct led_pattern_activate_pipe;
/** LED pattern to deactivate; read only */
extern datapipe_struct led_pattern_deactivate_pipe;
/** LED enabled / disabled */
extern datapipe_struct led_enabled_pipe;
extern datapipe_struct vibrator_pattern_activate_pipe;
extern datapipe_struct vibrator_pattern_deactivate_pipe;
/** State of display; read only */
extern datapipe_struct display_state_pipe;
/**
 * Display brightness;
 * bits 0-7 is brightness in percent (0-100)
 * upper 8 bits is high brightness boost (0-2)
 */
extern datapipe_struct display_brightness_pipe;
/** A key has been pressed */
extern datapipe_struct keypress_pipe;
/** Touchscreen activity took place */
extern datapipe_struct touchscreen_pipe;
/** Touchscreen suspended or not */
extern datapipe_struct touchscreen_suspend_pipe;
/** The lock-key has been pressed; read only */
extern datapipe_struct lockkey_pipe;
/** Keyboard open/closed; read only */
extern datapipe_struct keyboard_slide_pipe;
/** Lid cover open/closed; read only */
extern datapipe_struct lid_cover_pipe;
/** Lens cover open/closed; read only */
extern datapipe_struct lens_cover_pipe;
/** Proximity sensor; read only */
extern datapipe_struct proximity_sensor_pipe;
/** Ambient light sensor, data in mlux */
extern datapipe_struct light_sensor_pipe;
/** The alarm UI state */
extern datapipe_struct alarm_ui_state_pipe;
/** The device state */
extern datapipe_struct system_state_pipe;
/** Pipe to request reboot/shutdown from the system power backend*/
extern datapipe_struct system_power_request_pipe;
extern datapipe_struct mode_pipe;
/** The device submode */
extern datapipe_struct submode_pipe;
/** The call state */
extern datapipe_struct call_state_pipe;
/** The call type */
extern datapipe_struct call_type_pipe;
extern datapipe_struct device_lock_pipe;
extern datapipe_struct device_lock_inhibit_pipe;
/** The touchscreen/keypad lock state */
extern datapipe_struct tk_lock_pipe;
/** Charger state; read only */
extern datapipe_struct charger_state_pipe;
/** Battery status; read only */
extern datapipe_struct battery_status_pipe;
/** Camera button; read only */
extern datapipe_struct camera_button_pipe;
/** The inactivity timeout; read only */
extern datapipe_struct inactivity_timeout_pipe;
/** Audio routing state; read only */
extern datapipe_struct audio_route_pipe;
/** USB cable has been connected/disconnected; read only */
extern datapipe_struct usb_cable_pipe;
extern datapipe_struct tvout_pipe;

/**
 * Default inactivity timeout, in seconds;
 * dim timeout: 30 seconds
 * blank timeout: 3 seconds
 *
 * Used in case the display module doesn't load for some reason
 */
#define DEFAULT_INACTIVITY_TIMEOUT	33

device_mode_t mce_get_device_mode_int32(void);
gboolean mce_set_device_mode_int32(const device_mode_t mode);
submode_t mce_get_submode_int32(void);
gboolean mce_add_submode_int32(const submode_t submode);
gboolean mce_rem_submode_int32(const submode_t submode);

void mce_startup_ui(void);

#endif /* _MCE_H_ */
