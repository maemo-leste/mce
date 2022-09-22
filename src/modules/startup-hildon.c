/*
 * startup-hildon.c
 *
 * Copyright (C) 2022 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <glib.h>
#include <gmodule.h>
#include <stdbool.h>
#include "mce.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "datapipe.h"

/** Module name */
#define MODULE_NAME "startup-hildon"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_NAME, NULL };

static gconstpointer dbus_handler = NULL;

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority - we want it loaded before display module */
	.priority = 240
};

static gpointer device_inactive_filter(gpointer data)
{
	gboolean device_inactive = GPOINTER_TO_INT(data);

	if (dbus_handler != NULL && device_inactive) {
		mce_log(LL_DEBUG, "%s: Device inactive state prevented by %s",
			MODULE_NAME, MODULE_NAME);
		return GINT_TO_POINTER(FALSE);
	}

	return data;
}

static void remove_dbus_handler(void)
{
	if (dbus_handler == NULL)
		return;

	mce_dbus_handler_remove(dbus_handler);
	dbus_handler = NULL;
	remove_filter_from_datapipe(&device_inactive_pipe,
				    device_inactive_filter);
}

/**
 * D-Bus callback for the desktop startup notification signal
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean desktop_startup_dbus_cb(DBusMessage *const msg)
{
	(void)msg;

	mce_log(LL_DEBUG, "Received desktop startup notification");

	execute_datapipe_output_triggers(&led_pattern_deactivate_pipe,
					 MCE_LED_PATTERN_POWER_ON, USE_INDATA);
	mce_rem_submode_int32(MCE_BOOTUP_SUBMODE);

	/* Start inactivity timeout */
	(void)execute_datapipe(&inactivity_timeout_pipe, NULL,
			       USE_CACHE, DONT_CACHE_INDATA);

	remove_dbus_handler();

	return TRUE;
}

/**
 * Init function for the hildon startup module
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	submode_t submode = mce_get_submode_int32();

	(void)module;

	if ((submode & MCE_TRANSITION_SUBMODE) != 0) {
		mce_add_submode_int32(MCE_BOOTUP_SUBMODE);
		dbus_handler = mce_dbus_handler_add("com.nokia.HildonDesktop",
						    "ready",
						    NULL,
						    DBUS_MESSAGE_TYPE_SIGNAL,
						    desktop_startup_dbus_cb);

		if (dbus_handler == NULL)
			return "mce_dbus_handler_add failed";

		append_filter_to_datapipe(&device_inactive_pipe,
					  device_inactive_filter);
	}

	return NULL;
}

/**
 * Exit function for the hildon startup module
 *
 * @todo D-Bus unregistration
 *
 * @param module Unused
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	remove_dbus_handler();

	return;
}
