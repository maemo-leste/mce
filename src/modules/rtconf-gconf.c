#include <glib.h>
#include <gconf/gconf-client.h>
#include <gmodule.h>
#include "mce.h"
#include "mce-rtconf.h"
#include "mce-log.h"

/** Module name */
#define MODULE_NAME		"rtconf-gconf"
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

/** Pointer to the GConf client */
static GConfClient *gconf_client = NULL;
/** List of GConf notifiers */
static GList *gconf_notifiers = NULL;

struct notifier {
	guint callback_id;
	mce_rtconf_callback callback;
};

/**
 * Set an integer GConf key to the specified value
 *
 * @param key The GConf key to set the value of
 * @param value The value to set the key to
 * @return TRUE on success, FALSE on failure
 */
static gboolean mce_gconf_set_int(const gchar * const key, const gint value)
{
	gboolean status = FALSE;

	if (gconf_client_set_int(gconf_client, key, value, NULL) == FALSE) {
		mce_log(LL_WARN, "Failed to write %s to GConf", key);
		goto EXIT;
	}

	/* synchronise if possible, ignore errors */
	gconf_client_suggest_sync(gconf_client, NULL);

	status = TRUE;

 EXIT:
	return status;
}

/**
 * Return a boolean from the specified GConf key
 *
 * @param key The GConf key to get the value from
 * @param[out] value Will contain the value on return, if successful
 * @return TRUE on success, FALSE on failure
 */
static gboolean mce_gconf_get_bool(const gchar * const key, gboolean * value)
{
	gboolean status = FALSE;
	GError *error = NULL;
	GConfValue *gcv;

	gcv = gconf_client_get(gconf_client, key, &error);

	if (gcv == NULL) {
		mce_log((error != NULL) ? LL_WARN : LL_INFO,
			"Could not retrieve %s from GConf; %s", key, (error != NULL) ? error->message : "Key not set");
		goto EXIT;
	}

	if (gcv->type != GCONF_VALUE_BOOL) {
		mce_log(LL_ERR,
			"GConf key %s should have type: %d, but has type: %d", key, GCONF_VALUE_BOOL, gcv->type);
		goto EXIT;
	}

	*value = gconf_value_get_bool(gcv);

	status = TRUE;

 EXIT:
	g_clear_error(&error);

	return status;
}

/**
 * Return an integer from the specified GConf key
 *
 * @param key The GConf key to get the value from
 * @param[out] value Will contain the value on return
 * @return TRUE on success, FALSE on failure
 */
static gboolean mce_gconf_get_int(const gchar * const key, gint * value)
{
	gboolean status = FALSE;
	GError *error = NULL;
	GConfValue *gcv;

	gcv = gconf_client_get(gconf_client, key, &error);

	if (gcv == NULL) {
		mce_log((error != NULL) ? LL_WARN : LL_INFO,
			"Could not retrieve %s from GConf; %s", key, (error != NULL) ? error->message : "Key not set");
		goto EXIT;
	}

	if (gcv->type != GCONF_VALUE_INT) {
		mce_log(LL_ERR, "GConf key %s should have type: %d, but has type: %d", key, GCONF_VALUE_INT, gcv->type);
		goto EXIT;
	}

	*value = gconf_value_get_int(gcv);

	status = TRUE;

 EXIT:
	g_clear_error(&error);

	return status;
}

static void mce_gconf_gconf_callback(GConfClient * client, guint cnxn_id, GConfEntry * entry, gpointer user_data)
{
	struct notifier *not;
	GList *l;

	(void)client;

	for (l = gconf_notifiers; l != NULL; l = l->next) {
		not = (struct notifier *)l->data;
		if (not->callback_id == cnxn_id) {
			not->callback(entry->key, cnxn_id, user_data);
			break;
		}
	}
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
static gboolean mce_gconf_notifier_add(const gchar * path, const gchar * key,
				       const mce_rtconf_callback callback, void *user_data, guint * cb_id)
{
	GError *error = NULL;
	gboolean status = FALSE;

	gconf_client_add_dir(gconf_client, path, GCONF_CLIENT_PRELOAD_NONE, &error);

	if (error != NULL) {
		mce_log(LL_CRIT,
			"Could not add %s to directories watched by "
			"GConf client setting from GConf; %s", path, error->message);
	}

	g_clear_error(&error);

	*cb_id = gconf_client_notify_add(gconf_client, key, mce_gconf_gconf_callback, user_data, NULL, &error);
	if (error != NULL) {
		mce_log(LL_CRIT, "Could not register notifier for %s; %s", key, error->message);
	}

	struct notifier *not = malloc(sizeof(*not));

	not->callback_id = *cb_id;
	not->callback = callback;

	gconf_notifiers = g_list_prepend(gconf_notifiers, not);
	status = TRUE;

	g_clear_error(&error);

	return status;
}

/**
 * Remove a GConf notifier
 *
 * @param cb_id The ID of the notifier to remove
 * @param user_data Unused
 */
static void mce_gconf_notifier_remove(guint cb_id)
{
	GList *l;

	gconf_client_notify_remove(gconf_client, cb_id);

	for (l = gconf_notifiers; l != NULL; l = l->next) {
		if (((struct notifier *)l->data)->callback_id == cb_id) {
			gconf_notifiers = g_list_remove(gconf_notifiers, l->data);
			break;
		}
	}
}

/**
 * Init function for the gconf module
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule * module);
const gchar *g_module_check_init(GModule * module)
{
	(void)module;

	/* Get the default GConf client */
	if ((gconf_client = gconf_client_get_default()) == FALSE) {
		mce_log(LL_CRIT, "Could not get default GConf client");
		return "Could not get default GConf client";
	}

	if (!mce_rtconf_backend_register(mce_gconf_set_int,
					 mce_gconf_get_int,
					 mce_gconf_get_bool, mce_gconf_notifier_add, mce_gconf_notifier_remove)) {
		mce_log(LL_WARN, "Could not set GConf as rtconf backend");
		return "Could not set GConf as rtconf backend";
	}

	return NULL;
}

static void mce_gconf_notifier_remove_cb(gpointer cb_id, gpointer user_data)
{
	(void)user_data;
	mce_gconf_notifier_remove(GPOINTER_TO_INT(cb_id));
}

/**
 * Exit function for the gconf module
 */
G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	if (gconf_client != NULL) {
		/* Free the list of GConf notifiers */
		if (gconf_notifiers != NULL) {
			g_list_foreach(gconf_notifiers, (GFunc) mce_gconf_notifier_remove_cb, NULL);
			gconf_notifiers = NULL;
		}

		/* Unreference GConf client */
		g_object_unref(gconf_client);
	}
}
