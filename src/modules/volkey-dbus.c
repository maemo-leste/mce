/* This module provides various bits of device state on dbus
 * intended to eventually replace ke-recv
 */
/**
 * @file state-dbus.c
 * Copyright © 2021 Carl Philipp Klemm
 * @author Carl Philipp Klemm <carl@uvos.xyz>
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
#include <glib.h>
#include <gmodule.h>
#include <stdbool.h>
#include <linux/input.h>
#include "mce.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "datapipe.h"

/** Module name */
#define MODULE_NAME		"volkey-dbus"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_NAME, NULL };

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 100
};

static gboolean send_vol_key(bool volup, bool state)
{
	DBusMessage *msg = NULL;
	gboolean status = FALSE;
	
	dbus_bool_t state_b = state;

	mce_log(LL_DEBUG,
		"%s: Sending volume button: %s is %s", MODULE_NAME,
		volup ? "up" : "down", state ? "pressed" : "released");

	msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
					volup ? MCE_VOL_UP_KEY_SIG : MCE_VOL_DOWN_KEY_SIG);

	/* Append the inactivity status */
	if (dbus_message_append_args(msg,
				     DBUS_TYPE_BOOLEAN, &state_b,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append argument to D-Bus message for %s.%s",
			MCE_SIGNAL_IF, volup ? MCE_VOL_UP_KEY_SIG : MCE_VOL_DOWN_KEY_SIG);
		dbus_message_unref(msg);
		return status;
	}

	/* Send the message */
	status = dbus_send_message(msg);

	return status;
}

static void keypress_trigger(gconstpointer data)
{
	const struct input_event * const ev =
		*((const struct input_event * const *)data);

	if(ev->code == KEY_VOLUMEDOWN || ev->code == KEY_VOLUMEUP) {
		send_vol_key(ev->code == KEY_VOLUMEUP, ev->value == 1);
	}
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&keypress_pipe, keypress_trigger);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;
	
	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&keypress_pipe, keypress_trigger);
}
