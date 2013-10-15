/**
 * @file accelerometer.h
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
#ifndef _ACCELEROMETER_H_
#define _ACCELEROMETER_H_

#define ACCELEROMETER_DISPLAY_ON_POLL_FREQ	250
#define ACCELEROMETER_DISPLAY_DIM_POLL_FREQ	5000
#define ACCELEROMETER_DISPLAY_OFF_POLL_FREQ	0

#define ACCELEROMETER_SYS_PATH_RX51		"/sys/class/i2c-adapter/i2c-3/3-001d/"

#define ACCELEROMETER_RATE_PATH_RX51		ACCELEROMETER_SYS_PATH_RX51 "rate"
#define ACCELEROMETER_SCALE_PATH_RX51		ACCELEROMETER_SYS_PATH_RX51 "scale"
#define ACCELEROMETER_THS_PATH_RX51		ACCELEROMETER_SYS_PATH_RX51 "ths"
#define ACCELEROMETER_DURATION_PATH_RX51	ACCELEROMETER_SYS_PATH_RX51 "duration"
#define ACCELEROMETER_SAMPLES_PATH_RX51		ACCELEROMETER_SYS_PATH_RX51 "samples"
#define ACCELEROMETER_COORD_PATH_RX51		ACCELEROMETER_SYS_PATH_RX51 "coord"

#define ACCELEROMETER_NORMAL_SCALE		"normal"
#define ACCELEROMETER_FULL_SCALE		"full"

/* A device underdoing physical acceleration isn't useful for measuring the
 * gravity vector, so reject readings whose magnitude is too low or too high.
 */
#define ACCELEROMETER_STABLE_MINSQ		800
#define ACCELEROMETER_STABLE_MAXSQ		1250

#define ACCELEROMETER_ALMOST_ONLY_THIS		800
#define ACCELEROMETER_ALMOST_NONE		120
#define ACCELEROMETER_PRETTY_LOW		200

#endif /* _ACCELEROMETER_H_ */
