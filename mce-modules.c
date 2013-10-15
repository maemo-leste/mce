/**
 * @file mce-modules.c
 * Module handling for MCE
 * <p>
 * Copyright © 2007-2009 Nokia Corporation and/or its subsidiary(-ies).
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
#include "mce.h"
#include "mce-modules.h"
#include "mce-log.h"
#include "mce-conf.h"

/** List of all loaded modules */
static GSList *modules = NULL;

/**
 * Init function for the mce-modules component
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_modules_init(void)
{
	gchar **modlist = NULL;
	gsize length;
	gchar *path = NULL;

	/* Get the module path */
	path = mce_conf_get_string(MCE_CONF_MODULES_GROUP,
				   MCE_CONF_MODULES_PATH,
				   DEFAULT_MCE_MODULE_PATH,
				   NULL);

	/* Get the list modules to load */
	modlist = mce_conf_get_string_list(MCE_CONF_MODULES_GROUP,
					   MCE_CONF_MODULES_MODULES,
					   &length,
					   NULL);

	if (modlist != NULL) {
		gint i;

		for (i = 0; modlist[i]; i++) {
			GModule *module;
			gchar *tmp = g_module_build_path(path, modlist[i]);

			mce_log(LL_DEBUG,
				"Loading module: %s from %s",
				modlist[i], path);

			if ((module = g_module_open(tmp, 0)) != NULL) {
				module_info_struct *module_info = NULL;
				gpointer mip = NULL;

				if (g_module_symbol(module,
						    "module_info",
						    &mip) == FALSE) {
					mce_log(LL_ERR,
						"Failed to retrieve module "
						"information for: %s",
						modlist[i]);
					module_info = NULL;
				} else {
					module_info = (module_info_struct *)mip;
				}

				/* XXX: check dependencies, conflicts, et al */
				modules = g_slist_append(modules, module);
			} else {
				mce_log(LL_DEBUG,
					"Failed to load module: %s; skipping",
					modlist[i]);
			}

			g_free(tmp);
		}

		g_strfreev(modlist);
	}

	g_free(path);

	return TRUE;
}

/**
 * Exit function for the mce-modules component
 */
void mce_modules_exit(void)
{
	GModule *module;
	gint i;

	if (modules != NULL) {
		for (i = 0; (module = g_slist_nth_data(modules, i)) != NULL; i++) {
			g_module_close(module);
		}

		g_slist_free(modules);
		modules = NULL;
	}

	return;
}
