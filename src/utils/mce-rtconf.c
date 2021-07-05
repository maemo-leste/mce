#include <glib.h>
#include "mce.h"
#include "mce-log.h"
#include "mce-rtconf.h"

#define MODULE_NAME "rtconf"

gboolean(*mce_rtconf_set_int_backend) (const gchar * const key, const gint value);
gboolean(*mce_rtconf_get_int_backend) (const gchar * const key, gint * value);
gboolean(*mce_rtconf_get_bool_backend) (const gchar * const key, gboolean * value);
gboolean(*mce_rtconf_notifier_add_backend) (const gchar * path, const gchar * key,
					    const mce_rtconf_callback callback, void *user_data, guint * cb_id);
void (*mce_rtconf_notifier_remove_backend)(guint cb_id);

/**
 * Set an integer GConf key to the specified value
 *
 * @param key The GConf key to set the value of
 * @param value The value to set the key to
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_rtconf_set_int(const gchar * const key, const gint value)
{
	if (mce_rtconf_set_int_backend)
		return mce_rtconf_set_int_backend(key, value);

	mce_log(LL_WARN, "%s: %s used without backend", MODULE_NAME, __func__);
	return FALSE;
}

/**
 * Return a boolean from the specified GConf key
 *
 * @param key The GConf key to get the value from
 * @param[out] value Will contain the value on return, if successful
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_rtconf_get_bool(const gchar * const key, gboolean * value)
{
	if (mce_rtconf_get_bool_backend)
		return mce_rtconf_get_bool_backend(key, value);

	mce_log(LL_WARN, "%s: %s used without backend", MODULE_NAME, __func__);
	return FALSE;
}

/**
 * Return an integer from the specified GConf key
 *
 * @param key The GConf key to get the value from
 * @param[out] value Will contain the value on return
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_rtconf_get_int(const gchar * const key, gint * value)
{
	if (mce_rtconf_get_int_backend)
		return mce_rtconf_get_int_backend(key, value);

	mce_log(LL_WARN, "%s: %s used without backend", MODULE_NAME, __func__);
	return FALSE;
}

/**
 * Add a GConf notifier
 *
 * @param path The GConf directory to watch
 * @param key The GConf key to add the notifier for
 * @param callback The callback function
 * @param[out] cb_id Will contain the callback ID on return
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_rtconf_notifier_add(const gchar * path, const gchar * key,
				 const mce_rtconf_callback callback, void *user_data, guint * cb_id)
{
	if (mce_rtconf_notifier_add_backend)
		return mce_rtconf_notifier_add_backend(path, key, callback, user_data, cb_id);

	mce_log(LL_WARN, "%s: %s used without backend", MODULE_NAME, __func__);
	return FALSE;
}

/**
 * Remove a GConf notifier
 *
 * @param cb_id The ID of the notifier to remove
 * @param user_data Unused
 */
void mce_rtconf_notifier_remove(guint cb_id)
{
	if (mce_rtconf_notifier_remove_backend)
		mce_rtconf_notifier_remove_backend(cb_id);
	else
		mce_log(LL_WARN, "%s: %s used without backend", MODULE_NAME, __func__);
}

gboolean mce_rtconf_backend_register(gboolean(*set_int_backend) (const gchar * const key, const gint value),
				     gboolean(*get_int_backend) (const gchar * const key, gint * value),
				     gboolean(*get_bool_backend) (const gchar * const key, gboolean * value),
				     gboolean(*notifier_add_backend) (const gchar * path, const gchar * key,
								      const mce_rtconf_callback callback,
								      void *user_data, guint * cb_id),
				     void (*notifier_remove_backend)(guint cb_id))
{
	if(!mce_rtconf_set_int_backend) {
		mce_rtconf_set_int_backend = set_int_backend;
		mce_rtconf_get_int_backend = get_int_backend;
		mce_rtconf_get_bool_backend = get_bool_backend;
		mce_rtconf_notifier_add_backend = notifier_add_backend;
		mce_rtconf_notifier_remove_backend = notifier_remove_backend;
		return TRUE;
	}
	return FALSE;
}

void mce_rtconf_backend_unregister(void)
{
	mce_rtconf_set_int_backend = NULL;
	mce_rtconf_get_int_backend = NULL;
	mce_rtconf_get_bool_backend = NULL;
	mce_rtconf_notifier_add_backend = NULL;
	mce_rtconf_notifier_remove_backend = NULL;
}
