#include <glib.h>
#include <gmodule.h>
#include "mce.h"
#include "mce-conf.h"
#include "mce-rtconf.h"
#include "mce-log.h"

/** Module name */
#define MODULE_NAME		"rtconf-ini"
#define MODULE_PROVIDES	"rtconf"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_PROVIDES, NULL };

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 250
};

#define RTCONF_INI_KEY_FILE_PATH "/etc/mce/rtconf.ini"
#define RTCONF_INI_GROUP "Rtconf"

static gpointer keyfile;

static gboolean rtconf_ini_set_int(const gchar * const key, const gint value)
{
	(void)key;
	(void)value;
	return TRUE;
}

static gboolean rtconf_ini_get_bool(const gchar * const key, gboolean * value)
{
	if(!keyfile)
		return FALSE;

	mce_log(LL_DEBUG, "%s: getting bool key %s", MODULE_NAME, key);
	*value = mce_conf_get_bool(RTCONF_INI_GROUP, key, FALSE, keyfile);

	return TRUE;
}

static gboolean rtconf_ini_get_int(const gchar * const key, gint * value)
{
	if(!keyfile)
		return FALSE;

	mce_log(LL_DEBUG, "%s: getting int key %s", MODULE_NAME, key);
	*value = mce_conf_get_int(RTCONF_INI_GROUP, key, 0, keyfile);

	return TRUE;
}

static gboolean rtconf_ini_notifier_add(const gchar * path, const gchar * key,
				       const mce_rtconf_callback callback, void *user_data, guint * cb_id)
{
	(void)path;
	(void)key;
	(void)callback;
	(void)user_data;
	(void)cb_id;
	return TRUE;
}

static void rtconf_ini_notifier_remove(guint cb_id)
{
	(void)cb_id;
}

/**
 * Init function for the rtconf-ini module
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule * module);
const gchar *g_module_check_init(GModule * module)
{
	(void)module;

	keyfile = mce_conf_read_conf_file(RTCONF_INI_KEY_FILE_PATH);

	if (!keyfile) {
		mce_log(LL_WARN, "%s: %s not available", MODULE_NAME, RTCONF_INI_KEY_FILE_PATH);
	} else if (!mce_rtconf_backend_register(rtconf_ini_set_int,
									 rtconf_ini_get_int,
									 rtconf_ini_get_bool, 
									 rtconf_ini_notifier_add, 
									 rtconf_ini_notifier_remove)) {
		mce_log(LL_WARN, "Could not set rtconf-ini as rtconf backend");
		return "Could not set rtconf-ini as rtconf backend";
	}

	return NULL;
}

/**
 * Exit function for the rtconf-ini module
 */
G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	if (keyfile)
		mce_conf_free_conf_file(keyfile);

	mce_rtconf_backend_unregister();
}
