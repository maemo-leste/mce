/**
 * @file powerkeymenu-dbus-names.h
 * DBus Interface to the System UI Powerkeymenu plugin
 * <p>
 * This file is part of osso-systemui-powerkeymenu
 * <p>
 * Copyright (C) 2012 Pali Rohár <pali.rohar@gmail.com>
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

#ifndef _SYSTEMUI_POWERKEYMENU_DBUS_NAMES_H
#define _SYSTEMUI_POWERKEYMENU_DBUS_NAMES_H

/**
 * Request to open power key menu.
 */
#define SYSTEMUI_POWERKEYMENU_OPEN_REQ     "powerkeymenu_open"

/**
 * Request to close power key menu.
 */
#define SYSTEMUI_POWERKEYMENU_CLOSE_REQ    "powerkeymenu_close"

typedef enum {
	MODE_NORMAL,
	MODE_FLIGHT
} power_key_mode;

/* Return values from systemui.xml */
typedef enum {
	POWER_KEY_MENU_RESPONSE_TKLOCK = 1,
	POWER_KEY_MENU_RESPONSE_NORMALMODE = 2,
	POWER_KEY_MENU_RESPONSE_FLIGHTMODE = 3,
	POWER_KEY_MENU_RESPONSE_DEVICELOCK = 4,
	POWER_KEY_MENU_RESPONSE_POWEROFF = 5,
	POWER_KEY_MENU_RESPONSE_REBOOT = 6,
	POWER_KEY_MENU_RESPONSE_SOFT_POWEROFF = 7,

	/* FIXME: Names may not be correct */
	POWER_KEY_MENU_RESPONSE_PROFILE_SILENT = 8,
	POWER_KEY_MENU_RESPONSE_PROFILE_GENERAL = 9,
	POWER_KEY_MENU_RESPONSE_END_CURRENT_TASK = 10
} power_key_menu_response;

#endif
