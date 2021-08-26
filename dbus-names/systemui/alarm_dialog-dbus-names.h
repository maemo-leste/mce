/* @file alarm_dialog-dbus-names.h
* DBus Interface to the System UI Alarm plugin
* <p>
* This file is part of osso-systemui-dbus-dev
* <p>
* Copyright (C) 2004-2006 Nokia Corporation.
* <p>
* Contact person: David Weinehall <david.weinehall@nokia.com>
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

#ifndef _SYSTEMUI_ALARM_DIALOG_DBUS_NAMES_H
#define _SYSTEMUI_ALARM_DIALOG_DBUS_NAMES_H

typedef enum {
  ALARM_DIALOG_RESPONSE_SNOOZE =1,
  ALARM_DIALOG_RESPONSE_DISMISS,
  ALARM_DIALOG_RESPONSE_POWERUP,
  ALARM_DIALOG_RESPONSE_POWERDOWN,
  ALARM_DIALOG_ON_SCREEN,
  ALARM_DIALOG_NOT_RINGING,
  ALARM_DIALOG_NOT_ON_SCREEN
} alarm_dialog_status;

typedef enum {
  ALARM_MODE_NORMAL,
  ALARM_MODE_NOSNOOZE,
  ALARM_MODE_SWITCHON
} alarm_mode;

/**
 * Requests to show an alarm or powerup dialog.
 * The callback will be called with a @c dbus_int32_t with value from
 * #alarm_dialog_status.
 *
 * @param cb_service @c dbus_string_t Service to be called when dialog is
 * closed.
 * @param cb_path @c dbus_string_t Path to be called when dialog is closed.
 * @param cb_iface @c dbus_string_t Interface to be called when dialog is
 * closed.
 * @param cb_method @c dbus_string_t Method to be called when dialog is closed.
 * @param mode @c dbus_uint32_t Mode of the dialog with value from #alarm_mode.
 * @param message @c dbus_string_t Message shown in the dialog. (Only if #mode
 * != #ALARM_MODE_SWITCHON)
 * @param sound @c dbus_string_t Sound played when the dialog is shown. (Only
 * if #mode != #ALARM_MODE_SWITCHON)
 * @param image @c dbus_string_t Image shown in the dialog (icon name or full
 * path to file. (Only if #mode != #ALARM_MODE_SWITCHON, optional)
 * @param title @c dbus_string_t Title of the dialog. (Only if #mode !=
 * #ALARM_MODE_SWITCHON, optional)
 * @param time @c dbus_uint32_t Time to show in the dialog (as time_t).
 * (Only if #mode != #ALARM_MODE_SWITCHON, optional)
 * @return @c dbus_int32_t #GTK_RESPONSE_ACCEPT on success,
 * #GTK_RESPONSE_REJECT on failure.
 */
#define SYSTEMUI_ALARM_OPEN_REQ "alarm_open"

/**
 * Requests to close currently showin alarm dialog.
 */
#define SYSTEMUI_ALARM_CLOSE_REQ "alarm_close"


/**
 * Signals the current alarm dialog status
 */
#define SYSTEMUI_ALARM_DIALOG_STATUS_SIG "alarm_dialog_status"


#endif
