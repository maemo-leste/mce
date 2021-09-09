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

static gboolean mce_modules_check_provides(module_info_struct *new_module_info)
{
	for (GSList *module = modules; module; module = module->next) {
		gpointer mip = NULL;
		module_info_struct *module_info;
		if (g_module_symbol(module->data, "module_info", &mip) == FALSE)
			continue;
		module_info = (module_info_struct*)mip;
		for (int i = 0; new_module_info->provides[i]; ++i) {
			for (int j = 0; module_info->provides[j]; ++j) {
				if (g_strcmp0(new_module_info->provides[i], module_info->provides[j]) == 0) {
					mce_log(LL_WARN, "Module %s has the same provides as module %s, and will not be loaded.",
							new_module_info->name, module_info->name);
					return FALSE;
				}
			}
		}
	}
	return TRUE;
}

static gboolean mce_modules_check_essential(void)
{
	gboolean foundRtconf = FALSE;
	
	for (GSList *module = modules; module; module = module->next) {
		gpointer mip = NULL;
		module_info_struct *module_info;
		if (g_module_symbol(module->data, "module_info", (void**)&mip) == FALSE)
			continue;
		module_info = (module_info_struct*)mip;
		for (int j = 0; module_info->provides[j]; ++j) {
			if (g_strcmp0("rtconf", module_info->provides[j]) == 0) {
				foundRtconf = TRUE;
				break;
			}
		}
	}
	
	if (!foundRtconf) {
		mce_log(LL_ERR, "Could not find nessecary rtconf module aborting.");
		return FALSE;
	}
	
	return TRUE;
}

static void mce_modules_load(gchar **modlist)
{
	gchar *path = NULL;
	int i;

	path = mce_conf_get_string(MCE_CONF_MODULES_GROUP,
				   MCE_CONF_MODULES_PATH,
				   DEFAULT_MCE_MODULE_PATH,
				   NULL);

	for (i = 0; modlist[i]; i++) {
		GModule *module;
		gchar *tmp = g_module_build_path(path, modlist[i]);

		mce_log(LL_DEBUG, "Loading module: %s from %s", modlist[i], path);

		if ((module = g_module_open(tmp, 0)) != NULL) {
			gpointer mip = NULL;
			gboolean blockLoad = FALSE;

			if (g_module_symbol(module, "module_info", &mip) == FALSE) {
				mce_log(LL_ERR, "Failed to retrieve module information for: %s", modlist[i]);
				g_module_close(module);
				blockLoad = TRUE;
			} else if (!mce_modules_check_provides((module_info_struct*)mip)) {
				g_module_close(module);
				blockLoad = TRUE;
			}

			if (!blockLoad)
				modules = g_slist_append(modules, module);
		} else {
			mce_log(LL_WARN, "Failed to load module: %s; skipping", modlist[i]);
		}

		g_free(tmp);
	}

	g_free(path);
}

/**
 * Init function for the mce-modules component
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_modules_init(void)
{
	gchar **modlist = NULL;
	gchar **modlist_device = NULL;
	gchar **modlist_user = NULL;
	gsize length;

	/* Get the list modules to load */
	modlist = mce_conf_get_string_list(MCE_CONF_MODULES_GROUP,
					   MCE_CONF_MODULES_MODULES,
					   &length,
					   NULL);

	modlist_device = mce_conf_get_string_list(MCE_CONF_MODULES_GROUP,
					   MCE_CONF_MODULES_DEVMODULES,
					   &length,
					   NULL);

	modlist_user = mce_conf_get_string_list(MCE_CONF_MODULES_GROUP,
					   MCE_CONF_MODULES_USRMODULES,
					   &length,
					   NULL);

	if (modlist)
		mce_modules_load(modlist);
	if (modlist_device)
		mce_modules_load(modlist_device);
	if (modlist_user)
		mce_modules_load(modlist_user);

	g_strfreev(modlist);
	g_strfreev(modlist_device);
	g_strfreev(modlist_user);

	return mce_modules_check_essential();
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
