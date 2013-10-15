/**
 * @file homekey.h
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
#ifndef _HOMEKEY_H_
#define _HOMEKEY_H_

#include <glib.h>

typedef enum {
	HOME_DISABLED = 0,
	DEFAULT_HOMEKEY_SHORT_ACTION = 1,
	HOME_SHORTSIGNAL = 1,
	DEFAULT_HOMEKEY_LONG_ACTION = 2,
	HOME_LONGSIGNAL = 2
} homeaction_t;

#define MCE_CONF_HOMEKEY_GROUP		"HomeKey"

#define MCE_CONF_HOMEKEY_LONG_DELAY	"HomeKeyLongDelay"

#define MCE_CONF_HOMEKEY_SHORT_ACTION	"HomeKeyShortAction"

#define MCE_CONF_HOMEKEY_LONG_ACTION	"HomeKeyLongAction"

#define DEFAULT_HOME_LONG_DELAY		800

#endif /* _HOMEKEY_H_ */
