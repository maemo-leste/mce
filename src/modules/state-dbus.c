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
#include "mce.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "mce-rtconf.h"
#include "datapipe.h"

/** Module name */
#define MODULE_NAME		"state-dbus"

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

static gconstpointer keyboard_status_cookie;

static gboolean send_keyboard_status(DBusMessage *const method_call)
{
	DBusMessage *msg = NULL;
	dbus_bool_t slide_state = datapipe_get_gbool(keyboard_slide_pipe) == COVER_OPEN;
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"%s: Sending slide state: %s", MODULE_NAME,
		slide_state ? "open" : "closed");

	/* If method_call is set, send a reply,
	 * otherwise, send a signal
	 */
	if (method_call != NULL) {
		msg = dbus_new_method_reply(method_call);
	} else {
		/* system_inactivity_ind */
		msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
				      MCE_KEYBOARD_SLIDE_GET);
	}

	/* Append the inactivity status */
	if (dbus_message_append_args(msg,
				     DBUS_TYPE_BOOLEAN, &slide_state,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append %s argument to D-Bus message "
			"for %s.%s",
			method_call ? "reply " : "",
			method_call ? MCE_REQUEST_IF :
				      MCE_SIGNAL_IF,
			method_call ? MCE_INACTIVITY_STATUS_GET :
				      MCE_KEYBOARD_SLIDE_GET);
		dbus_message_unref(msg);
		return status;
	}

	/* Send the message */
	status = dbus_send_message(msg);

	return status;
}

static gboolean keyboard_status_get_dbus_cb(DBusMessage *const msg)
{
	mce_log(LL_DEBUG, "%s: Received keyboard status get request", MODULE_NAME);

	/* Try to send a reply that contains the current inactivity status */
	return send_keyboard_status(msg);
}

static void keyboard_slide_trigger(gconstpointer data)
{
	(void)data;
	send_keyboard_status(NULL);
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&keyboard_slide_pipe,
				  keyboard_slide_trigger);

	/* get_inactivity_status */
	keyboard_status_cookie = mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_KEYBOARD_SLIDE_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 keyboard_status_get_dbus_cb);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;
	
	if (keyboard_status_cookie)
		mce_dbus_handler_remove(keyboard_status_cookie);
	
	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&keyboard_slide_pipe,
					  keyboard_slide_trigger);
}
