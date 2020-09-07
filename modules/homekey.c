/**
 * @file homekey.c
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
#include <glib.h>
#include <gmodule.h>
#include <string.h>
#include <linux/input.h>
#include "mce.h"
#include "homekey.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "datapipe.h"

#define MODULE_NAME		"homekey"

static const gchar *const provides[] = { MODULE_NAME, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 250
};

static guint16 home_keycode = KEY_F5;

static gint longdelay = DEFAULT_HOME_LONG_DELAY;
static homeaction_t shortpressaction = DEFAULT_HOMEKEY_SHORT_ACTION;
static homeaction_t longpressaction = DEFAULT_HOMEKEY_LONG_ACTION;

static guint homekey_timeout_cb_id = 0;

static void send_home_key_signal(const gboolean longpress)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);

	if ((system_state == MCE_STATE_USER) &&
	    (mce_get_submode_int32() == MCE_NORMAL_SUBMODE)) {
		dbus_send(NULL, MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
			  longpress == TRUE ? MCE_HOME_KEY_LONG_SIG :
					      MCE_HOME_KEY_SIG,
			  NULL,
			  DBUS_TYPE_INVALID);
	}
}

static gboolean homekey_timeout_cb(gpointer data)
{
	(void)data;

	homekey_timeout_cb_id = 0;

	switch (longpressaction) {
	case HOME_DISABLED:
		break;

	case HOME_SHORTSIGNAL:
		send_home_key_signal(FALSE);
		break;

	case HOME_LONGSIGNAL:
	default:
		send_home_key_signal(TRUE);
		break;
	}

	return FALSE;
}

static void cancel_homekey_timeout(void)
{
	if (homekey_timeout_cb_id != 0) {
		g_source_remove(homekey_timeout_cb_id);
		homekey_timeout_cb_id = 0;
	}
}

static void setup_homekey_timeout(void)
{
	cancel_homekey_timeout();

	homekey_timeout_cb_id =
		g_timeout_add(longdelay, homekey_timeout_cb, NULL);
}

static void homekey_trigger(gconstpointer const data)
{
	struct input_event const *const *evp;
	struct input_event const *ev;

	if (data == NULL)
		goto EXIT;

	evp = data;
	ev = *evp;

	if ((ev != NULL) && (ev->code == home_keycode)) {
		if (ev->value == 1) {
			mce_log(LL_DEBUG, "[home] pressed");

			setup_homekey_timeout();
		} else if (ev->value == 0) {
			
			if (homekey_timeout_cb_id != 0) {
				cancel_homekey_timeout();

				switch (shortpressaction) {
				case HOME_DISABLED:
					break;

				case HOME_SHORTSIGNAL:
				default:
					send_home_key_signal(FALSE);
					break;

				case HOME_LONGSIGNAL:
					send_home_key_signal(TRUE);
					break;
				}
			}
		}
	}

EXIT:
	return;
}

static gboolean parse_action(char *string, homeaction_t *action)
{
	gboolean status = FALSE;

	if (!strcmp(string, "disabled")) {
		*action = HOME_DISABLED;
	} else if (!strcmp(string, "shortsignal")) {
		*action = HOME_SHORTSIGNAL;
	} else if (!strcmp(string, "longsignal")) {
		*action = HOME_LONGSIGNAL;
	} else {
		mce_log(LL_WARN,
			"Unknown [home] action");
		goto EXIT;
	}

	status = TRUE;

EXIT:
	return status;
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	gchar *tmp = NULL;

	(void)module;

	append_input_trigger_to_datapipe(&keypress_pipe,
					 homekey_trigger);

	longdelay = mce_conf_get_int(MCE_CONF_HOMEKEY_GROUP,
				     MCE_CONF_HOMEKEY_LONG_DELAY,
				     DEFAULT_HOME_LONG_DELAY,
				     NULL);
	tmp = mce_conf_get_string(MCE_CONF_HOMEKEY_GROUP,
				  MCE_CONF_HOMEKEY_SHORT_ACTION,
				  "",
				  NULL);

	(void)parse_action(tmp, &shortpressaction);
	g_free(tmp);

	tmp = mce_conf_get_string(MCE_CONF_HOMEKEY_GROUP,
				  MCE_CONF_HOMEKEY_LONG_ACTION,
				  "",
				  NULL);

	(void)parse_action(tmp, &longpressaction);
	g_free(tmp);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	remove_input_trigger_from_datapipe(&keypress_pipe,
					   homekey_trigger);

	cancel_homekey_timeout();

	return;
}
