/**
 * @file splashscreen-dbus-names.h
 * DBus Interface to the System UI Splash Screen plugin
 * <p>
 * This file is part of osso-systemui-splashcreen-dev
 * <p>
 * Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>
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
#define SYSTEMUI_SPLASHSCREEN_OPEN_REQ "splashscreen_open"
#define SYSTEMUI_SPLASHSCREEN_CLOSE_REQ "splashscreen_close"

typedef enum {
	SPLASHSCREEN_ENABLE_BOOTUP = 1,
	SPLASHSCREEN_ENABLE_SHUTDOWN = 2
} splashscreen_t;
