/*
 * input-ctrl.c
 *
 * Copyright (C) 2023 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
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

#include "mce.h"
#include "mce-io.h"
#include "mce-log.h"
#include "datapipe.h"
#include "event-input.h"
#include "event-input-utils.h"

#include <string.h>
#include <unistd.h>

/** Module name */
#define MODULE_NAME		"input-ctrl"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_NAME, NULL };

#define SYSFS_PATH "/sys/class/input/"

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {

	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 250
};

static void scan_input_devices_cb(const char *filename, gpointer user_data)
{
	GList **input_devices = user_data;

	*input_devices = g_list_prepend(*input_devices, g_strdup(filename));
}

static void inhibit_input_devices(gboolean inhibit)
{
	GList *devices = NULL;
	GList *l;

	mce_scan_inputdevices(scan_input_devices_cb, &devices);

	if (inhibit) {
		GSList *kbd_devs = mce_input_get_monitored_keyboard_devices();
		GHashTable *table = g_hash_table_new(g_str_hash, g_str_equal);
		GSList *sl;

		for (sl = kbd_devs; sl; sl = sl->next) {
			const gchar *data = mce_get_io_monitor_name(sl->data);

			g_hash_table_add(table, (gpointer)data);
		}

		for (l = devices; l;) {
			GList *next = l->next;

			if (g_hash_table_contains(table, l->data)) {
				mce_log(LL_DEBUG,
					"%s: Ignoring monitored device %s",
					MODULE_NAME, (gchar *)l->data);
				g_free(l->data);
				devices = g_list_delete_link(devices, l);
			}

			l = next;
		}

		g_hash_table_destroy(table);
	}

	for (l = devices; l; l = l->next) {
		gchar *path = g_strconcat(SYSFS_PATH, basename(l->data),
					  "/device/inhibited", NULL);

		if (access(path, F_OK) == 0) {
			mce_log(LL_DEBUG, "%s: %s device %s", MODULE_NAME,
				inhibit ? "inhibit" : "resume",
				(gchar *)l->data);

			mce_write_string_to_file(path, inhibit ? "1" : "0");
		} else {
			mce_log(LL_DEBUG,
				"%s: device %s does not support inhibit, kernel too old?",
				MODULE_NAME, (gchar *)l->data);
		}

		g_free(path);
	}

	g_list_free_full(devices, g_free);
}

/** @brief inhibit/resume all non-keyboard input devices
  *
  **/
static void input_control_trigger(gconstpointer data)
{
	inhibit_input_devices(!!GPOINTER_TO_INT(data));
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&touchscreen_suspend_pipe,
					  input_control_trigger);

	inhibit_input_devices(FALSE);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	remove_output_trigger_from_datapipe(&touchscreen_suspend_pipe,
					    input_control_trigger);

	inhibit_input_devices(FALSE);
}
