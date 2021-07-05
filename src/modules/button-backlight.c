#include <glib.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gmodule.h>
#include <stdlib.h>
#include "mce.h"
#include "mce-io.h"
#include "mce-lib.h"
#include "mce-dbus.h"
#include "mce-log.h"
#include "datapipe.h"
#include "mce-conf.h"
#include "button-backlight.h"
#include "mce-rtconf.h"

#define MODULE_NAME		"button-backlight"

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

static guint als_enabled_gconf_cb_id = 0;
static display_state_t display_state = MCE_DISPLAY_UNDEF;
static system_state_t  system_state = MCE_STATE_USER;
static bool device_silder_open = false;
static int  als_lux = -1;
static gboolean als_enabled = true;

static struct button_backlight *button_backlights = NULL;
static unsigned int count_backlights = 0;

static void set_backlight_states(bool by_display_state)
{
	for (unsigned int i = 0; i < count_backlights; ++i) {
		struct button_backlight * const backlight = &button_backlights[i];
		if (by_display_state || !backlight->locked )
		{
			unsigned int brightness;
			if (als_enabled && als_lux > -1) {
				int j = 4;
				while(j > 0 && backlight->brightness_map->lux[j] > als_lux) 
					--j;
				brightness = backlight->brightness_map->value[j];
				if(backlight->brightness_map->lux[0] > als_lux) brightness = backlight->brightness_map->value[0];
			}
			else {
				brightness = backlight->brightness_map->value[0];
			}
			
			if (!device_silder_open && backlight->hidden_by_slider) 
				brightness = 0;
			else if (display_state != MCE_DISPLAY_ON && display_state != MCE_DISPLAY_DIM)
				brightness = 0;
			else if (display_state == MCE_DISPLAY_DIM && !backlight->on_when_dimmed)
				brightness = 0;
			else if (system_state != MCE_STATE_USER)
				brightness = 0;
			if(backlight->value != brightness) {
				mce_log(LL_DEBUG, "%s: setting %s to %i", MODULE_NAME, backlight->file_sysfs,  brightness);
				mce_write_number_string_to_glob(backlight->file_sysfs, brightness);
				backlight->value = brightness;
			}
		}
	}
}
/**
 * Datapipe trigger for the keyboard slide
 *
 * @param data The keyboard slide state stored in a pointer;
 *             COVER_OPEN if the keyboard is open,
 *             COVER_CLOSED if the keyboard is closed
 */
static void keyboard_slide_trigger(gconstpointer const data)
{
	if ((GPOINTER_TO_INT(data) == COVER_OPEN) &&
	    ((mce_get_submode_int32() & MCE_TKLOCK_SUBMODE) == 0)) {
		device_silder_open = true;
	} else {
		device_silder_open = false;
	}
	
	set_backlight_states(false);
}

/**
 * Datapipe trigger for display state
 *
 * @param data The display stated stored in a pointer
 */
static void display_state_trigger(gconstpointer data)
{
	display_state = GPOINTER_TO_INT(data);

	set_backlight_states(true);
}

/**
 * Handle system state change
 *
 * @param data The system state stored in a pointer
 */
static void system_state_trigger(gconstpointer data)
{
	system_state = GPOINTER_TO_INT(data);

	set_backlight_states(false);
}

static bool get_keyboard_light_state(void)
{
	for(unsigned int i = 0; i < count_backlights; ++i) {
		const struct button_backlight * const backlight = &button_backlights[i];
		if(backlight->is_keyboard)
			return backlight->value;
	}
	return false;
}

static gboolean get_keyboard_status_dbus_cb(DBusMessage *message)
{
	const gchar *state;
	DBusMessage *reply;

	mce_log(LL_DEBUG, "%s: Received keyboard status get request", MODULE_NAME);
	if (get_keyboard_light_state())
	{
		state = "on";
	}
	else
	{
		state = "off";
	}
	mce_log(LL_DEBUG, "Sending keyboard status: %s", state);
	if (!message)
	{
		mce_log(LL_WARN, "MCE couldn't send reply, passed method_call is equal to NULL");
		return FALSE;
	}
	else
	{
		reply = dbus_new_method_reply(message);
		if (dbus_message_append_args(reply, 's', &state, 0))
		{
			return dbus_send_message(reply) != 0;
		}
		else
		{
			mce_log(LL_CRIT,"Failed to append reply argument to D-Bus message for %s.%s",MCE_REQUEST_IF,MCE_KEYBOARD_STATUS_GET);
			dbus_message_unref(reply);
			return FALSE;
		}
	}
}

/**
 * rtconf callback for ALS settings
 *
 * @param gcc Unused
 * @param cb_id Connection ID from gconf_client_notify_add()
 * @param data Unused
 */
static void als_rtconf_cb(gchar *key, guint cb_id, void *user_data)
{
	(void)key;
	(void)user_data;

	if (cb_id == als_enabled_gconf_cb_id)
		mce_rtconf_get_bool(MCE_GCONF_DISPLAY_ALS_ENABLED_PATH, &als_enabled);
	else
		mce_log(LL_WARN, "%s: Spurious GConf value received; confused!", MODULE_NAME);
}

static void als_trigger(gconstpointer data)
{
	(void)data;
	
	int new_als_lux = datapipe_get_gint(light_sensor_pipe);
	
	if (new_als_lux < 0)
		return;

	als_lux = new_als_lux;
	
	set_backlight_states(false);
}


static gboolean init_backlights(void)
{
	char **backlightlist = NULL;
	gsize length;

	backlightlist = mce_conf_get_string_list(MCE_CONF_BACKLIGHT_GROUP,
					       MCE_CONF_CONFIGURED_LIGHTS,
					       &length, NULL);

	if (backlightlist == NULL) {
		mce_log(LL_WARN, "%s: Failed to configure button backlights", MODULE_NAME);
		return false;
	}

	button_backlights = malloc(length * sizeof(struct button_backlight));
	if (!button_backlights) {
		g_strfreev(backlightlist);
		return false;
	}

	for (int i = 0; backlightlist[i]; ++i) {
		int *tmp;
		struct button_backlight backlight;

		mce_log(LL_DEBUG, "%s: Getting config for: %s", MODULE_NAME, backlightlist[i]);

		tmp = mce_conf_get_int_list(MCE_CONF_BACKLIGHT_GROUP, backlightlist[i], &length, NULL);
		if (tmp != NULL) {
			if (length != MCE_CONF_COUNT_BACKLIGHT_FIELDS) {
				mce_log(LL_ERR,
					"Skipping invalid Backlight");
				g_free(tmp);
				continue;
			}

			backlight.file_sysfs = 
				malloc(strlen(LED_SYSFS_PATH)+strlen(backlightlist[i])+strlen(LED_BRIGHTNESS_PATH)+1);
			if (!backlight.file_sysfs) {
				g_free(tmp);
				mce_log(LL_CRIT, "Out of memory!");
				continue;
			}
			strcpy(backlight.file_sysfs, LED_SYSFS_PATH);
			strcat(backlight.file_sysfs, backlightlist[i]);
			strcat(backlight.file_sysfs, LED_BRIGHTNESS_PATH);

			if (g_access(backlight.file_sysfs, W_OK) == 0)
			{
				backlight.hidden_by_slider = tmp[BACKLIGHT_HIDDEN_FIELD];
				backlight.is_keyboard = tmp[BACKLIGHT_IS_KEYBOARD_FIELD];
				backlight.on_when_dimmed = tmp[BACKLIGHT_ON_WHEN_DIMMED_FIELD];
				backlight.locked = tmp[BACKLIGHT_LOCKED_FIELD];
				backlight.fade_time = tmp[BACKLIGHT_FADE_TIME_FIELD];
				int profile = tmp[BACKLIGHT_PROFILE_FIELD];
				if (profile > 1 || profile < 0) 
					profile = 0;
				if (profile == 0) 
					backlight.brightness_map = &brightness_map_kbd;
				else backlight.brightness_map = &brightness_map_btn;
				backlight.value = 0;
				
				mce_log(LL_DEBUG, "%s: %s %i %i %i %i %i %i", MODULE_NAME, 
						backlight.file_sysfs, backlight.hidden_by_slider, backlight.is_keyboard, backlight.on_when_dimmed, backlight.locked, backlight.fade_time, profile );
				
				button_backlights[count_backlights] = backlight;
				++count_backlights;
			} else {
				mce_log(LL_INFO, "%s: %s configured but dose not exist on this device.", MODULE_NAME, backlight.file_sysfs);
				free(backlight.file_sysfs);
			}

			g_free(tmp);
		}
	}

	return true;
}

/**
 * Init function for the keypad module
 *
 * @todo XXX status needs to be set on error!
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	gchar *status = NULL;

	(void)module;
	
	if (!init_backlights())
		return NULL;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&system_state_pipe, system_state_trigger);
	append_output_trigger_to_datapipe(&keyboard_slide_pipe, keyboard_slide_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe, display_state_trigger);
	append_output_trigger_to_datapipe(&light_sensor_pipe, als_trigger);

	mce_rtconf_get_bool(MCE_GCONF_DISPLAY_ALS_ENABLED_PATH, &als_enabled);
	
	if (mce_rtconf_notifier_add(MCE_GCONF_DISPLAY_PATH,
				MCE_GCONF_DISPLAY_ALS_ENABLED_PATH,
				als_rtconf_cb, NULL,
				&als_enabled_gconf_cb_id) == FALSE)
		return NULL;

	if (mce_dbus_handler_add(MCE_REQUEST_IF, MCE_KEYBOARD_STATUS_GET, NULL, 1u, get_keyboard_status_dbus_cb) ) {
		status = NULL;
	}
	else {
		mce_log(LL_WARN, "%s: Error in intialization of dbus handler", MODULE_NAME);
		status = NULL;
	}
	return status;
}

/**
 * Exit function for the keypad module
 *
 * @param module Unused
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&display_state_pipe, display_state_trigger);
	remove_output_trigger_from_datapipe(&keyboard_slide_pipe, keyboard_slide_trigger);
	remove_output_trigger_from_datapipe(&system_state_pipe, system_state_trigger);
	remove_output_trigger_from_datapipe(&light_sensor_pipe, als_trigger);

	for (unsigned int i = 0; i < count_backlights; ++i) {
		free(button_backlights[i].file_sysfs);
	}
	
	free(button_backlights);

	return;
}
