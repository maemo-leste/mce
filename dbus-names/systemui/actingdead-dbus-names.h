/**
 * @file actingdead-dbus-names.h
 * DBus Interface to the System UI Actingdead plugin
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

#ifndef _SYSTEMUI_ACTINGDEAD_DBUS_NAMES_H
#define _SYSTEMUI_ACTINGDEAD_DBUS_NAMES_H

/**
 * Request to enable acting dead.
 */
#define SYSTEMUI_ACTINGDEAD_OPEN_REQ       "acting_dead_open"

/**
 * Request to close acting dead.
 */
#define SYSTEMUI_ACTINGDEAD_CLOSE_REQ      "acting_dead_close"

/**
 * Requests acting dead state.
 *
 * @return @c dbus_bool_t 1 if enabled, 0 if disabled.
 */
#define SYSTEMUI_ACTINGDEAD_GETSTATE_REQ   "acring_dead_getstate"

#endif
