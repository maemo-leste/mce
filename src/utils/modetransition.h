/**
 * @file modetransition.h
 * Headers for the mode transition component of the Mode Control Entity
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
#ifndef _MODETRANSITION_H_
#define _MODETRANSITION_H_

#include <glib.h>

#define STRING2(x) #x
#define STRING(x) STRING2(x)

#define MCE_MODE_FILENAME		G_STRINGIFY(MCE_VAR_DIR) "/mode"
#pragma message STRING(MCE_MODE_FILENAME)

#define SPLASH_DELAY			500		/**< 0.5 seconds */
#define ACTDEAD_DELAY			1500		/**< 1.5 seconds */
#define POWERUP_DELAY			3500		/**< 3.5 seconds */

#define MCE_MODECHG_CB_REQ		"modechange_callback"

/* When MCE is made modular, this will be handled differently */
gboolean mce_mode_init(void);
void mce_mode_exit(void);

#endif /* _MODETRANSITION_H_ */
