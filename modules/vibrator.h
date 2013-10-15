/**
 * @file vibrator.h
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
#ifndef _VIBRA_H_
#define _VIBRA_H_

#include <glib.h>

#define MCE_CONF_VIBRATOR_GROUP			"Vibrator"

#define MCE_CONF_VIBRATOR_PATTERNS		"VibratorPatterns"

#define MCE_CONF_VIBRA_PATTERN_RX51_GROUP	"VibraPatternRX51"

#define MCE_VIBRA_SYS_PATH			"/sys/class/i2c-adapter/i2c-1/1-0048/twl4030_vibra"
#define MCE_VIBRA_PATH			MCE_VIBRA_SYS_PATH "/pulse"

#endif /* _VIBRA_H_ */
