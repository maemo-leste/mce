#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include "mce.h"
#include "mce-rtconf.h"
#include "mce-log.h"

/** Module name */
#define MODULE_NAME		"rtconf-gsettings"
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

static GSettings *gsettings_client = NULL;
static GList *gsettings_notifiers = NULL;

struct notifier {
	guint callback_id;
	mce_rtconf_callback callback;
	void *user_data;
	const gchar* key;
};

static gchar *mce_gsettings_translate_key(const gchar * const key)
{
	gchar *str = g_strdup(key);
	for(gchar *iter = str; *iter != '\0'; ++iter)
	{
		*iter = g_ascii_tolower(*iter);
		if(*iter == '_')
			*iter = '-';
	}
	return str;
}

static gboolean mce_gsettings_set_int(const gchar * const key, const gint value)
{
	gchar *gkey = mce_gsettings_translate_key(key);
	if (g_settings_set_int(gsettings_client, gkey, value) == FALSE) {
		mce_log(LL_WARN, "Failed to write %s to gesettings", key);
		g_free(gkey);
		return FALSE;
	}
	g_free(gkey);
	return TRUE;
}

static gboolean mce_gsettings_get_bool(const gchar * const key, gboolean * value)
{
	gchar *gkey = mce_gsettings_translate_key(key);
	*value = g_settings_get_boolean(gsettings_client, gkey);
	g_free(gkey);

	return TRUE;
}

static gboolean mce_gsettings_set_bool(const gchar * const key, const gboolean value)
{
	gchar *gkey = mce_gsettings_translate_key(key);
	gboolean ret = g_settings_set_boolean(gsettings_client, gkey, value);
	g_free(gkey);
	return ret;
}

static gboolean mce_gsettings_get_int(const gchar * const key, gint * value)
{
	gchar *gkey = mce_gsettings_translate_key(key);
	*value = g_settings_get_int(gsettings_client, gkey);
	g_free(gkey);

	return TRUE;
}

static void mce_gsettings_callback(GSettings *client, gchar* key, gpointer user_data)
{
	struct notifier *not;
	GList *l;

	(void)user_data;
	(void)client;

	for (l = gsettings_notifiers; l != NULL; l = l->next) {
		not = (struct notifier *)l->data;
		if (g_strcmp0(not->key, key) == 0) {
			not->callback(not->key, not->callback_id, not->user_data);
			break;
		}
	}
}

/**
 * Add a gesettings notifier
 *
 * @param path The gesettings directory to watch
 * @param key The gesettings key to add the notifier for
 * @param callback The callback function
 * @param[out] cb_id Will contain the callback ID on return
 * @return TRUE on success, FALSE on failure
 */
static gboolean mce_gsettings_notifier_add(const gchar * key,
				       const mce_rtconf_callback callback, void *user_data, guint *cb_id)
{
	static int cb_id_counter = 0;
	struct notifier *not = g_malloc0(sizeof(*not));
	
	*cb_id = cb_id_counter++;

	not->callback_id = *cb_id;
	not->callback = callback;
	not->user_data = user_data;
	not->key = mce_gsettings_translate_key(key);

	gsettings_notifiers = g_list_prepend(gsettings_notifiers, not);

	return TRUE;
}

/**
 * Remove a gesettings notifier
 *
 * @param cb_id The ID of the notifier to remove
 * @param user_data Unused
 */
static void mce_gsettings_notifier_remove(guint cb_id)
{
	GList *l;

	for (l = gsettings_notifiers; l != NULL; l = l->next) {
		if (((struct notifier *)l->data)->callback_id == cb_id) {
			gsettings_notifiers = g_list_remove(gsettings_notifiers, l->data);
			g_free(l->data);
			break;
		}
	}
}

/**
 * Init function for the gsettings module
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule * module);
const gchar *g_module_check_init(GModule * module)
{
	(void)module;

	/* Get the default gesettings client */
	if ((gsettings_client = g_settings_new("com.nokia.mce")) == FALSE) {
		mce_log(LL_CRIT, "Could not connect to gesettings");
		return "Could not connect to gesettings";
	}
	
	g_signal_connect(G_OBJECT(gsettings_client), "changed", G_CALLBACK(mce_gsettings_callback), NULL);

	if (!mce_rtconf_backend_register(mce_gsettings_set_int, mce_gsettings_get_int,
					 mce_gsettings_get_bool,  mce_gsettings_set_bool,
					 mce_gsettings_notifier_add,
					 mce_gsettings_notifier_remove)) {
		mce_log(LL_WARN, "Could not set gesettings as rtconf backend");
		return "Could not set gesettings as rtconf backend";
	}

	return NULL;
}

static void mce_gsettings_notifier_remove_cb(gpointer cb_id, gpointer user_data)
{
	(void)user_data;
	mce_gsettings_notifier_remove(GPOINTER_TO_INT(cb_id));
}

/**
 * Exit function for the gsettings module
 */
G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	if (gsettings_client != NULL) {
		/* Free the list of gesettings notifiers */
		if (gsettings_notifiers != NULL) {
			g_list_foreach(gsettings_notifiers, (GFunc) mce_gsettings_notifier_remove_cb, NULL);
			gsettings_notifiers = NULL;
		}

		/* Unreference gesettings client */
		g_object_unref(gsettings_client);
	}
}
