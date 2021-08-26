/**
 * @file mode-names.h
 * Defines for names of various modes and submodes for Mode Control Entity
 * <p>
 * This file is part of mce-dev
 * <p>
 * Copyright © 2004-2009 Nokia Corporation.
 * <p>
 * @author David Weinehall <david.weinehall@nokia.com>
 *
 * These headers are free software; you can redistribute them
 * and/or modify them under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * These headers are distributed in the hope that they will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#ifndef _MCE_MODE_NAMES_H_
#define _MCE_MODE_NAMES_H_

/** Normal device mode */
#define MCE_NORMAL_MODE				"normal"
/** Offline device mode; RF's disabled */
#define MCE_FLIGHT_MODE				"flight"
/** Offline device mode; RF's disabled; alias for flight mode */
#define MCE_OFFLINE_MODE			"offline"
/** Invalid device mode; this should NEVER occur! */
#define MCE_INVALID_MODE			"invalid"
/** Confirm suffix;
 * append this to your mode request to have a confirmation
 * dialog shown when this is mandated by policy
 */
#define MCE_CONFIRM_SUFFIX			"-dialog"

/** No ongoing call */
#define MCE_CALL_STATE_NONE			"none"
/** Call ringing */
#define MCE_CALL_STATE_RINGING			"ringing"
/** Call on-going */
#define MCE_CALL_STATE_ACTIVE			"active"
/**
 * Service operation on-going
 * use to prevent calls from being initiated;
 * will not prevent emergency calls
 */
#define MCE_CALL_STATE_SERVICE			"service"

/** Normal call */
#define MCE_NORMAL_CALL				"normal"
/** Emergency call  */
#define MCE_EMERGENCY_CALL			"emergency"

/** Device locked */
#define MCE_DEVICE_LOCKED			"locked"
/** Device unlocked */
#define MCE_DEVICE_UNLOCKED			"unlocked"

/** Touchscreen/Keypad locked */
#define MCE_TK_LOCKED				"locked"
/** Touchscreen/Keypad silently locked */
#define MCE_TK_SILENT_LOCKED			"silent-locked"
/** Touchscreen/Keypad locked with fadeout */
#define MCE_TK_LOCKED_DIM			"locked-dim"
/** Touchscreen/Keypad silently locked with fadeout */
#define MCE_TK_SILENT_LOCKED_DIM		"silent-locked-dim"
/** Touchscreen/Keypad unlocked */
#define MCE_TK_UNLOCKED				"unlocked"
/** Touchscreen/Keypad silently unlocked */
#define MCE_TK_SILENT_UNLOCKED			"silent-unlocked"

/** Display state name for display on */
#define MCE_DISPLAY_ON_STRING			"on"
/** Display state name for display dim */
#define MCE_DISPLAY_DIM_STRING			"dimmed"
/** Display state name for display off */
#define MCE_DISPLAY_OFF_STRING			"off"

/** Keyboard state name for keyboard light on */
#define MCE_KEYBOARD_ON_STRING			"on"
/** Keyboard state name for keyboard light off */
#define MCE_KEYBOARD_OFF_STRING			"off"

/** CABC name for CABC disabled */
#define MCE_CABC_MODE_OFF			"off"
/** CABC name for UI mode */
#define MCE_CABC_MODE_UI			"ui"
/** CABC name for still image mode */
#define MCE_CABC_MODE_STILL_IMAGE		"still-image"
/** CABC name for moving image mode */
#define MCE_CABC_MODE_MOVING_IMAGE		"moving-image"

/** Device rotation name for portrait orientation */
#define MCE_ORIENTATION_PORTRAIT		"portrait"
/** Device rotation name for landscape orientation */
#define MCE_ORIENTATION_LANDSCAPE		"landscape"
/** Device rotation name for inverted portrait orientation */
#define MCE_ORIENTATION_PORTRAIT_INVERTED	"portrait (inverted)"
/** Device rotation name for inverted landscape orientation */
#define MCE_ORIENTATION_LANDSCAPE_INVERTED	"landscape (inverted)"
/** Device rotation name for on stand */
#define MCE_ORIENTATION_ON_STAND		"on_stand"
/** Device rotation name for off stand */
#define MCE_ORIENTATION_OFF_STAND		"off_stand"
/** Device rotation name for facing up */
#define MCE_ORIENTATION_FACE_UP			"face_up"
/** Device rotation name for facing down */
#define MCE_ORIENTATION_FACE_DOWN		"face_down"
/** Device rotation name for unknown */
#define MCE_ORIENTATION_UNKNOWN			"unknown"

#endif /* _MCE_MODE_NAMES_H_ */
