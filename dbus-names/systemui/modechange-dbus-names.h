/**
 * @file modechange-dbus-names.h
 * DBus Interface to the System UI Mode Change plugin
 * <p>
 * This file is part of osso-systemui-modechange-dev
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
#define SYSTEMUI_MODECHANGE_OPEN_REQ "modechange_open"

#define SYSTEMUI_MODECHANGE_CLOSE_REQ "modechange_close"

typedef enum {
	MODECHANGE_TO_FLIGHTMODE = 0,
	MODECHANGE_TO_NORMALMODE = 1
} modechange_t;

typedef enum {
	MODECHANGE_RESPONSE_CANCEL = 0,
	MODECHANGE_RESPONSE_OK = 1
} modechange_response_t;
