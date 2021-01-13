/* This module handles notifcation leds on devices without a hardware led controller capable of patterns.*/

#include <glib.h>
#include <gmodule.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-io.h"
#include "datapipe.h"
#include "mce.h"

#define MCE_CONF_LED_GROUP			"LED"
#define MCE_CONF_LED_PATTERNS		"LEDPatterns"
#define MCE_CONF_LED_GENERIC		"LEDGenericSoftware"
#define MCE_CONF_MONOCHROMIC		"Monochromic"
#define MCE_CONF_R		"RedSysfs"
#define MCE_CONF_G		"GreenSysfs"
#define MCE_CONF_B		"BlueSysfs"
#define MCE_CONF_W		"WhiteSysfs"

/** Module name */
#define MODULE_NAME		"led-sw"
#define MODULE_PROVIDES	"led"

#define LED_SYSFS_PATH "/sys/class/leds/"
#define LED_BRIGHTNESS_PATH "/brightness"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_PROVIDES, NULL };

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 100
};

typedef enum {
	PATTERN_PRIO_FIELD = 0,
	PATTERN_POLICY_FIELD = 1,
	PATTERN_TIMEOUT_FIELD = 2,
	PATTERN_R_FIELD = 3,
	PATTERN_G_FIELD = 4,
	PATTERN_B_FIELD = 5,
	PATTERN_ON_PERIOD_FIELD = 6,
	PATTERN_OFF_PERIOD_FIELD = 7,
	NUMBER_OF_PATTERN_FIELDS
} pattern_field;

typedef enum {
    POLICY_PLAY_DISPLAY_OFF = 0,
    POLICY_PLAY_DISPLAY_ON_OR_OFF = 1,
    POLICY_PLAY_DISPLAY_OFF_ACTDEAD = 2,
    POLICY_PLAY_DISPLAY_ON_ACTDEAD = 3,
    POLICY_PLAY_DISPLAY_OFF_OR_ACTDEAD = 4,
    POLICY_PLAY_ALWAYS = 5,
} policy_field;

struct led_pattern {
	char* name;
	uint8_t priority;
	uint8_t policy;
	unsigned int timeoutSec;
	uint8_t r;
	uint8_t g;
	uint8_t b;
	unsigned long onPeriodMs;
	unsigned long offPeriodMs;

	bool active;
	bool ledOn;
	bool foreground;

	unsigned int disableTimer;
	unsigned int periodTimer;
};

static struct led_pattern *led_patterns = NULL;
static unsigned int patterns_count = 0;
static bool monochromic = false;
static char* r_sysfs = NULL;
static char* g_sysfs = NULL;
static char* b_sysfs = NULL;
static char* w_sysfs = NULL;

static bool led_enabled;
static display_state_t display_state = { 0 };
static system_state_t system_state = { 0 };

static void update_patterns(void);

static bool init_patterns(void)
{
	struct led_pattern *led_pattern = NULL;
	char **patternlist = NULL;
	gsize length;

	patternlist = mce_conf_get_string_list(MCE_CONF_LED_GROUP,
					       MCE_CONF_LED_PATTERNS,
					       &length, NULL);

	if (patternlist == NULL) {
		mce_log(LL_WARN, "%s: Failed to configure led patterns", MODULE_NAME);
		return false;
	}

	led_patterns = malloc(length*sizeof(*led_pattern));
	if (!led_patterns) {
		g_strfreev(patternlist);
		return false;
	}

	for (int i = 0; patternlist[i]; ++i) {
		int *tmp;

		mce_log(LL_DEBUG, "%s: Getting led pattern for: %s",
			MODULE_NAME, patternlist[i]);

		tmp = mce_conf_get_int_list(MCE_CONF_LED_GENERIC,
					    patternlist[i], &length, NULL);
		if (tmp != NULL) {
			struct led_pattern *pattern;

			if (length != NUMBER_OF_PATTERN_FIELDS) {
				mce_log(LL_ERR, "%s: Skipping invalid led pattern: %s", MODULE_NAME, patternlist[i]);
				g_free(tmp);
				continue;
			}


			pattern = &led_patterns[patterns_count];
			pattern->name = strdup(patternlist[i]);
			pattern->priority = ABS(tmp[PATTERN_PRIO_FIELD]);
			pattern->policy = ABS(tmp[PATTERN_POLICY_FIELD]);
			pattern->timeoutSec = ABS(tmp[PATTERN_TIMEOUT_FIELD]);
			pattern->onPeriodMs = ABS(tmp[PATTERN_ON_PERIOD_FIELD]);
			pattern->offPeriodMs = ABS(tmp[PATTERN_OFF_PERIOD_FIELD]);
			pattern->r = ABS(tmp[PATTERN_R_FIELD]);
			pattern->g = ABS(tmp[PATTERN_G_FIELD]);
			pattern->b = ABS(tmp[PATTERN_B_FIELD]);

			pattern->active = false;
			pattern->foreground = false;
			pattern->ledOn = false;
			pattern->disableTimer = 0;
			pattern->periodTimer = 0;

			if (pattern->r == 0 && pattern->g == 0 && pattern->b == 0)
				mce_log(LL_INFO, "%s: Skipping led pattern with zero brightness: %s", MODULE_NAME, patternlist[i]);
			else
				++patterns_count;

			g_free(tmp);
		}
	}
	mce_log(LL_DEBUG, "%s: found %i patterns", MODULE_NAME, patterns_count);
	g_strfreev(patternlist);

	return true;
}

static void set_led(uint8_t r, uint8_t g, uint8_t b)
{
	if (!monochromic) {
		mce_write_number_string_to_glob(r_sysfs, r);
		mce_write_number_string_to_glob(g_sysfs, g);
		mce_write_number_string_to_glob(b_sysfs, b);
	} else {
		mce_write_number_string_to_glob(w_sysfs, (r+g+b)/3);
	}
}

static bool should_run_pattern(const struct led_pattern * const pattern)
{
	if (pattern->r == 0 && pattern->g == 0 && pattern->b == 0)
		return false;
	else if (pattern->policy == POLICY_PLAY_ALWAYS ||
		(pattern->policy == POLICY_PLAY_DISPLAY_ON_ACTDEAD && led_enabled))
		return true;
	else if (pattern->policy == POLICY_PLAY_DISPLAY_OFF_OR_ACTDEAD &&
		 (system_state == MCE_STATE_ACTDEAD
		  || display_state == MCE_DISPLAY_OFF))
		return true;
	else if (pattern->policy == POLICY_PLAY_DISPLAY_OFF_ACTDEAD &&
		 (system_state == MCE_STATE_ACTDEAD
		  && display_state == MCE_DISPLAY_OFF))
		return true;
	else if (pattern->policy == POLICY_PLAY_DISPLAY_ON_OR_OFF &&
		 (system_state != MCE_STATE_ACTDEAD))
		return true;
	else if (pattern->policy == POLICY_PLAY_DISPLAY_OFF &&
		 (system_state != MCE_STATE_ACTDEAD
		  && display_state == MCE_DISPLAY_OFF))
		return true;
	return false;
}

static gboolean disable_timeout_cb(gpointer data)
{
	struct led_pattern *pattern = (struct led_pattern *)data;

	set_led(0, 0, 0);
	pattern->ledOn = false;
	pattern->active = false;

	update_patterns();

	return false;
}

static void cancel_disable_timer(struct led_pattern *pattern)
{
	if (pattern->disableTimer != 0) {
		set_led(0, 0, 0);
		g_source_remove(pattern->disableTimer);
		pattern->disableTimer = 0;
		pattern->foreground = false;
	}
}

static void setup_disable_timer(struct led_pattern *pattern)
{
	cancel_disable_timer(pattern);

	if (pattern->timeoutSec > 0)
		pattern->disableTimer =
			g_timeout_add_seconds(pattern->timeoutSec, &disable_timeout_cb, pattern);
}

static gboolean period_timeout_cb(gpointer data)
{
	struct led_pattern *pattern = (struct led_pattern *)data;

	pattern->ledOn = !pattern->ledOn;

	if (pattern->ledOn)
		set_led(pattern->r, pattern->g, pattern->b);
	else
		set_led(0, 0, 0);

	pattern->periodTimer =
		g_timeout_add(pattern->ledOn ? pattern->onPeriodMs : pattern->offPeriodMs, &period_timeout_cb, pattern);

	 return false;
}

static void cancel_period_timer(struct led_pattern *pattern)
{
	if (pattern->periodTimer != 0) {
		set_led(0, 0, 0);
		g_source_remove(pattern->periodTimer);
		pattern->periodTimer = 0;
		pattern->foreground = false;
	}
}

static void setup_period_timer(struct led_pattern *pattern)
{
	cancel_period_timer(pattern);

	if (pattern->offPeriodMs > 0 && pattern->onPeriodMs > 0)
		pattern->periodTimer =
			g_timeout_add(pattern->ledOn ? pattern->onPeriodMs : pattern->offPeriodMs, &period_timeout_cb, pattern);
}

static void update_patterns(void)
{
	struct led_pattern *pattern_to_run = NULL;
	int prio = 256;
	for (unsigned int i = 0; i < patterns_count; ++i) {
		if (!led_patterns[i].active || !should_run_pattern(&led_patterns[i]) || prio < led_patterns[i].priority) {
			if (led_patterns[i].foreground) {
				cancel_period_timer(&led_patterns[i]);
				set_led(0, 0, 0);
				led_patterns[i].foreground = false;
				led_patterns[i].ledOn = false;
			}
			continue;
		}

		if (pattern_to_run != NULL) {
			cancel_period_timer(pattern_to_run);
			pattern_to_run->foreground = false;
		}

		prio = led_patterns[i].priority;
		pattern_to_run = &led_patterns[i];
	}

	if (pattern_to_run && pattern_to_run->foreground == false) {
		set_led(pattern_to_run->r, pattern_to_run->g, pattern_to_run->b);
		pattern_to_run->ledOn = true;
		pattern_to_run->foreground = true;
		setup_period_timer(pattern_to_run);
	}
}

static char *led_create_sysfs_path(const gchar *key)
{
	char *path = NULL;
	gchar* tmp = mce_conf_get_string(MCE_CONF_LED_GENERIC, key, NULL, NULL);
	if (!tmp) {
		mce_log(LL_ERR, "%s: %s is required to be defined", MODULE_NAME, key);
		return NULL;
	}
	path = malloc(strlen(LED_SYSFS_PATH)+strlen(tmp)+strlen(LED_BRIGHTNESS_PATH)+1);
	if (!path)
		return NULL;
	strcpy(path, LED_SYSFS_PATH);
	strcat(path, tmp);
	strcat(path, LED_BRIGHTNESS_PATH);
	if (g_access(path, W_OK) != 0) {
		mce_log(LL_ERR, "%s: Led sysfs path is invalid: %s", MODULE_NAME, path);
		free(path);
		return NULL;
	}
	return path;
}

static void system_state_trigger(gconstpointer data)
{
	(void)data;
	system_state = datapipe_get_gint(system_state_pipe);
	update_patterns();
}

static void display_state_trigger(gconstpointer data)
{
	(void)data;
	display_state = datapipe_get_gint(display_state_pipe);
	update_patterns();
}

static void led_enabled_trigger(gconstpointer data)
{
	(void)data;
	led_enabled = datapipe_get_gbool(led_enabled_pipe);
	update_patterns();
}


static void led_pattern_activate_trigger(gconstpointer data)
{
	const gchar * const name = (const gchar * const)data;
	if (name) {
		bool found = false;
		for (unsigned int i = 0; i < patterns_count; ++i) {
			if (strcmp(led_patterns[i].name, name) == 0) {
				found = true;
				led_patterns[i].active = true;
				update_patterns();
				setup_disable_timer(&led_patterns[i]);
				mce_log(LL_DEBUG, "%s: activate called on: %s", MODULE_NAME, name);
				break;
			}
		}

		if (!found)
			mce_log(LL_WARN, "%s: activate called on non exisiting pattern: %s", MODULE_NAME, name);
	}
}

static void led_pattern_deactivate_trigger(gconstpointer data)
{
	const gchar * const name = (const gchar * const)data;
	bool found = false;
	for (unsigned int i = 0; i < patterns_count; ++i) {
		if (strcmp(led_patterns[i].name, name) == 0) {
			found = true;
			led_patterns[i].active = false;
			update_patterns();
			cancel_disable_timer(&led_patterns[i]);
			mce_log(LL_DEBUG, "%s: deactivate called on: %s", MODULE_NAME, name);
			break;
		}
	}

	if (!found)
		mce_log(LL_WARN, "%s: deactivate called on non exisiting pattern: %s", MODULE_NAME, name);
}


/**
 * Init function for the LED logic module
 *
 * @todo XXX status needs to be set on error!
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;

	monochromic = mce_conf_get_bool(MCE_CONF_LED_GENERIC, MCE_CONF_MONOCHROMIC, false, NULL);
	if (monochromic) {
		w_sysfs = led_create_sysfs_path(MCE_CONF_W);
		if(!w_sysfs)
			return NULL;
	} else {
		r_sysfs = led_create_sysfs_path(MCE_CONF_R);
		g_sysfs = led_create_sysfs_path(MCE_CONF_G);
		b_sysfs = led_create_sysfs_path(MCE_CONF_B);
		if (!r_sysfs || !g_sysfs || !b_sysfs)
			return NULL;
	}

	if (!init_patterns())
		return NULL;

	led_enabled = datapipe_get_gbool(led_enabled_pipe);
	system_state = datapipe_get_gint(system_state_pipe);
	display_state = datapipe_get_gint(system_state_pipe);

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);
	append_output_trigger_to_datapipe(&led_pattern_activate_pipe,
					  led_pattern_activate_trigger);
	append_output_trigger_to_datapipe(&led_pattern_deactivate_pipe,
					  led_pattern_deactivate_trigger);
	append_output_trigger_to_datapipe(&led_enabled_pipe,
					  led_enabled_trigger);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{

	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&led_pattern_deactivate_pipe,
					    led_pattern_deactivate_trigger);
	remove_output_trigger_from_datapipe(&led_pattern_activate_pipe,
					    led_pattern_activate_trigger);
	remove_output_trigger_from_datapipe(&led_enabled_pipe,
					    led_enabled_trigger);
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);

	/* Don't disable the LED on shutdown/reboot/acting dead */
	if ((system_state != MCE_STATE_ACTDEAD) &&
	    (system_state != MCE_STATE_SHUTDOWN) &&
	    (system_state != MCE_STATE_REBOOT)) {
		set_led(0, 0, 0);
	}

	if (r_sysfs)
		free(r_sysfs);
	if (g_sysfs)
		free(g_sysfs);
	if (b_sysfs)
		free(b_sysfs);
	if (w_sysfs)
		free(w_sysfs);
	if (led_patterns) {
		for (unsigned int i = 0; i < patterns_count; ++i)
			free(led_patterns[i].name);
		free(led_patterns);
	}
}
