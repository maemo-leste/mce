/**
 * @file mce-dsme.h
 * Headers for the DSME<->MCE interface and logic
 * <p>
 * Copyright © 2004-2009 Nokia Corporation and/or its subsidiary(-ies).
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
#ifndef _MCE_DSME_H_
#define _MCE_DSME_H_

#include <glib.h>

#define TRANSITION_DELAY		1000		/**< 1 second */

#define MCE_CONF_SOFTPOWEROFF_GROUP	"SoftPowerOff"

#define MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_CHARGER "ConnectivityPolicyCharger"

#define MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_BATTERY "ConnectivityPolicyBattery"

#define MCE_CONF_SOFTPOWEROFF_CONNECTIVITY_POLICY_POWERON "ConnectivityPolicyPowerOn"

#define MCE_CONF_SOFTPOWEROFF_CHARGER_POLICY_CONNECT "ChargerPolicyConnect"


#define MCE_IS_USB_MASS_STORAGE_CONNECTED_FLAG_0 "/sys/devices/platform/musb_hdrc/gadget/gadget-lun0/file"

#define MCE_IS_USB_MASS_STORAGE_CONNECTED_FLAG_1 "/sys/devices/platform/musb_hdrc/gadget/gadget-lun1/file"

#define SOFTOFF_CONNECTIVITY_FORCE_OFFLINE_STR		"forceoffline"
#define SOFTOFF_CONNECTIVITY_SOFT_OFFLINE_STR		"softoffline"
#define SOFTOFF_CONNECTIVITY_RETAIN_STR			"retain"
#define SOFTOFF_CHARGER_CONNECT_WAKEUP_STR		"wakeup"
#define SOFTOFF_CHARGER_CONNECT_IGNORE_STR		"ignore"

/** Soft poweroff connectivity policies */
enum {
	/** Policy not set */
	SOFTOFF_CONNECTIVITY_INVALID = MCE_INVALID_TRANSLATION,
	/** Retain connectivity */
	SOFTOFF_CONNECTIVITY_RETAIN = 0,
	/** Default setting when charger connected */
	DEFAULT_SOFTOFF_CONNECTIVITY_CHARGER = SOFTOFF_CONNECTIVITY_RETAIN,
	/** Go to offline mode if no connections are open */
	SOFTOFF_CONNECTIVITY_SOFT_OFFLINE = 1,
	/** Go to offline mode */
	SOFTOFF_CONNECTIVITY_FORCE_OFFLINE = 2,
	/** Default setting when running on battery */
	DEFAULT_SOFTOFF_CONNECTIVITY_BATTERY = SOFTOFF_CONNECTIVITY_FORCE_OFFLINE,
};

/** Soft poweron connectivity policies */
enum {
	/** Stay in offline mode */
	SOFTOFF_CONNECTIVITY_OFFLINE = 0,
	/** Default setting */
	DEFAULT_SOFTOFF_CONNECTIVITY_POWERON = SOFTOFF_CONNECTIVITY_OFFLINE,
	/** Restore previous mode */
	SOFTOFF_CONNECTIVITY_RESTORE = 1,
};

/** Soft poweroff charger connect policy */
enum {
	/** Stay in offline mode */
	SOFTOFF_CHARGER_CONNECT_WAKEUP = 0,
	/** Restore previous mode */
	SOFTOFF_CHARGER_CONNECT_IGNORE = 1,
	/** Default setting */
	DEFAULT_SOFTOFF_CHARGER_CONNECT = SOFTOFF_CHARGER_CONNECT_IGNORE,
};

void request_powerup(void);
void request_reboot(void);
void request_soft_poweron(void);
void request_soft_poweroff(void);
void request_normal_shutdown(void);

/* When MCE is made modular, this will be handled differently */
gboolean mce_dsme_init(gboolean debug_mode);
void mce_dsme_exit(void);

#endif /* _MCE_DSME_H_ */
