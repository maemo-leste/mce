/**
 * @file devlock.h
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
#ifndef _DEVLOCK_H_
#define _DEVLOCK_H_

#include <glib.h>

#define MCE_CONF_DEVLOCK_GROUP			"DevLock"
#define MCE_CONF_DEVLOCK_DELAY_0		"DevLockDelay0"
#define MCE_CONF_DEVLOCK_DELAY_1		"DevLockDelay1"
#define MCE_CONF_DEVLOCK_DELAY_2		"DevLockDelay2"
#define MCE_CONF_DEVLOCK_DELAY_3		"DevLockDelay3"
#define MCE_CONF_DEVLOCK_SHUTDOWN_TIMEOUT	"DevLockShutdownTimeout"

#ifndef MCE_GCONF_LOCK_PATH
#define MCE_GCONF_LOCK_PATH		"/system/osso/dsm/locks"
#endif /* MCE_GCONF_LOCK_PATH */

#define DEFAULT_DEVICE_AUTOLOCK_ENABLED		FALSE
#define DEFAULT_DEVICE_AUTOLOCK_TIMEOUT		10
#define DEFAULT_DEVICE_LOCK_FAILED		0
#define DEFAULT_DEVICE_LOCK_TOTAL_FAILED	0

#define MCE_GCONF_DEVICE_AUTOLOCK_ENABLED_PATH	MCE_GCONF_LOCK_PATH "/devicelock_autolock_enabled"
#define MCE_GCONF_DEVICE_AUTOLOCK_TIMEOUT_PATH	MCE_GCONF_LOCK_PATH "/devicelock_autolock_timeout"
#define MCE_GCONF_DEVICE_LOCK_FAILED_PATH	MCE_GCONF_LOCK_PATH "/devicelock_failed"
#define MCE_GCONF_DEVICE_LOCK_TOTAL_FAILED_PATH	MCE_GCONF_LOCK_PATH "/devicelock_total_failed"

#define MCE_DEVLOCK_CB_REQ		"devlock_callback"

/** Default lock delays in seconds */
enum {
	DEFAULT_LOCK_DELAY_0 = 0,
	DEFAULT_LOCK_DELAY_1 = 1,
	DEFAULT_LOCK_DELAY_2 = 1,
	DEFAULT_LOCK_DELAY_3 = 5,
};

#define DEFAULT_SHUTDOWN_TIMEOUT	0

gboolean mce_devlock_init(void);
void mce_devlock_exit(void);

#endif /* _DEVLOCK_H_ */
