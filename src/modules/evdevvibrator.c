#include <glib.h>
#include <gmodule.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <linux/input.h>
#include "mce.h"
#include "mce-io.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "datapipe.h"
#include "event-input-utils.h"

#define MODULE_NAME		"evdevvibrator"

static const char *const provides[] = { MODULE_NAME, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

#define MCE_CONF_VIBRATOR_GROUP			"Vibrator"
#define MCE_CONF_VIBRATOR_PATTERNS		"VibratorPatterns"

typedef struct fffeatures {
	bool constant:1;	/* can render constant force effects */
	bool periodic:1;	/* can render periodic effects with the following waveforms: */
	bool square:1;		/* square waveform */
	bool triangle:1;	/* triangle waveform */
	bool sine:1;		/* sine waveform */
	bool saw_up:1;		/* sawtooth up waveform */
	bool saw_down:1;	/* sawtooth down waveform */
	bool custom:1;		/* custom waveform (not implemented) */
	bool ramp:1;		/* can render ramp effects */
	bool spring:1;		/* can simulate the presence of a spring */
	bool friction:1;	/* can simulate friction */
	bool damper:1;		/* can simulate damper effects */
	bool rumble:1;		/* rumble effects */
	bool inertia:1;		/* can simulate inertia */
	bool gain:1;		/* gain is adjustable */
	bool autocenter:1;	/* autocenter is adjustable */
} fffeatures;

int evdev_fd = -1;
bool vibratorArmed = true;

typedef struct pattern_t {
	char *name;
	int_fast32_t priority;
	int_fast32_t policy;
	int_fast32_t timeout;
	int_fast32_t repeat_count;
	int_fast32_t accel_period;
	int_fast32_t on_period;
	int_fast32_t decel_period;
	int_fast32_t off_period;
	uint_fast8_t speed;
	gboolean invalid;
} pattern_t;

typedef enum {
	PATTERN_PRIO_FIELD = 0,
	PATTERN_SCREEN_ON_FIELD = 1,
	PATTERN_TIMEOUT_FIELD = 2,
	PATTERN_REPEAT_COUNT_FIELD = 3,
	PATTERN_ACCEL_PERIOD_FIELD = 4,
	PATTERN_ON_PERIOD_FIELD = 5,
	PATTERN_DECEL_PERIOD_FIELD = 6,
	PATTERN_OFF_PERIOD_FIELD = 7,
	PATTERN_SPEED_FIELD = 8,
	NUMBER_OF_PATTERN_FIELDS
} pattern_field;

typedef enum {
    VIBRATE_POLICY_PLAY_DISPLAY_OFF = 0,
    VIBRATE_POLICY_PLAY_DISPLAY_ON_OR_OFF = 1,
    VIBRATE_POLICY_PLAY_DISPLAY_OFF_ACTDEAD = 2,
    VIBRATE_POLICY_PLAY_DISPLAY_ON_ACTDEAD = 3,
    VIBRATE_POLICY_PLAY_DISPLAY_OFF_OR_ACTDEAD = 4,
    VIBRATE_POLICY_PLAY_ALWAYS = 5,
} policy_field;


static pattern_t *patterns = NULL;
uint_fast32_t patternsCount = 0;

int_fast32_t priority = 256;
static unsigned int priority_timeout_cb_id = 0;

static display_state_t display_state = { 0 };
static system_state_t system_state = { 0 };
static call_state_t call_state = { 0 };

static bool first_run = true;

static bool bit_in_array(unsigned char *array, size_t bit)
{
	return array[bit / 8] & (1 << bit % 8);
}

static bool ff_gain_set(const int fd, const int gain)
{
	struct input_event event = {0};
	if (fd < 0)
		return false;
	event.type = EV_FF;
	event.code = FF_GAIN;
	event.value = 0xFFFFUL * gain / 100;
	return write(fd, &event, sizeof(event)) >= 0;
}

static bool ff_features_get(const int fd, struct fffeatures *features)
{
	unsigned char featuresBytes[1 + FF_MAX / 8];
	if (fd < 0)
		return false;
	memset(featuresBytes, 0, sizeof(featuresBytes));
	int ret =
	    ioctl(fd, EVIOCGBIT(EV_FF, sizeof(featuresBytes)), featuresBytes);
	if (ret < 0)
		return false;

	if (ret * sizeof(unsigned long) * 8 >= 16) {
		features->constant = bit_in_array(featuresBytes, FF_CONSTANT);
		features->periodic = bit_in_array(featuresBytes, FF_PERIODIC);
		features->square = bit_in_array(featuresBytes, FF_SQUARE);
		features->triangle = bit_in_array(featuresBytes, FF_TRIANGLE);
		features->sine = bit_in_array(featuresBytes, FF_SINE);
		features->saw_up = bit_in_array(featuresBytes, FF_SAW_UP);
		features->saw_down = bit_in_array(featuresBytes, FF_SAW_DOWN);
		features->custom = bit_in_array(featuresBytes, FF_CUSTOM);
		features->ramp = bit_in_array(featuresBytes, FF_RAMP);
		features->spring = bit_in_array(featuresBytes, FF_SPRING);
		features->friction = bit_in_array(featuresBytes, FF_FRICTION);
		features->damper = bit_in_array(featuresBytes, FF_DAMPER);
		features->rumble = bit_in_array(featuresBytes, FF_RUMBLE);
		features->inertia = bit_in_array(featuresBytes, FF_INERTIA);
		features->gain = bit_in_array(featuresBytes, FF_GAIN);
		features->autocenter = bit_in_array(featuresBytes, FF_AUTOCENTER);
	} else
		return false;

	return true;
}

static bool ff_device_run(const int fd, const int lengthMs, const int delayMs,
		   const int count, const uint8_t strength,
		   const short attackLengthMs, const short fadeLengthMs)
{
	static struct ff_effect effect;
	
	if (fd < 0)
		return false;

	if (first_run) {
		memset(&effect, 0, sizeof(struct ff_effect));
		effect.type = FF_PERIODIC;
		effect.id = -1;
		effect.u.periodic.waveform = FF_SINE;
		effect.u.periodic.period = 100;
		first_run = false;
	}

	effect.u.periodic.magnitude = (0x7fff * strength) / 255;
	effect.u.periodic.envelope.attack_length = attackLengthMs;
	effect.u.periodic.envelope.fade_length = fadeLengthMs;
	effect.replay.delay = delayMs;
	effect.replay.length = lengthMs;

	if (ioctl(fd, EVIOCSFF, &effect) == -1) {
		perror("Error at ioctl() in ff_device_run");
		return false;
	}

	struct input_event run_event;
	memset(&run_event, 0, sizeof(struct input_event));
	run_event.type = EV_FF;
	run_event.code = effect.id;
	run_event.value = count;

	if (write(fd, (const void *)&run_event, sizeof(run_event)) == -1) {
		return false;
	} else
		return true;
}

static int ff_device_open(const char *const deviceName)
{
	int inputDevice = open(deviceName, O_RDWR);
	if (inputDevice < 0)
		return -4;
	if (!ff_gain_set(inputDevice, 100)) {
		if (close(inputDevice) < 0) {
			mce_log(LL_ERR, "%s: Can not close %i errno: %s", 
					MODULE_NAME, inputDevice, strerror(errno));
		}
		return -1;
	}
	fffeatures features;
	if (!ff_features_get(inputDevice, &features)) {
		if (close(inputDevice) < 0) {
			mce_log(LL_ERR, "%s: Can not close %i errno: %s", 
					MODULE_NAME, inputDevice, strerror(errno));
		}
		return -2;
	}
	if (!features.periodic || !features.sine || !features.gain) {
		if (close(inputDevice) < 0) {
			mce_log(LL_ERR, "%s: Can not close %i errno: %s", 
					MODULE_NAME, inputDevice, strerror(errno));
		}
		return -3;
	}
	return inputDevice;
}

static bool ff_device_stop(const int fd)
{
	return ff_device_run(fd, 1, 0, 1, 0, 0, 0);
}

static gboolean priority_timeout_cb(gpointer data)
{
	(void)data;

	priority = 256;
	
	priority_timeout_cb_id = 0;

	return FALSE;
}

static void cancel_priority_timeout(void)
{
	if (priority_timeout_cb_id != 0) {
		g_source_remove(priority_timeout_cb_id);
		priority_timeout_cb_id = 0;
	}
}

static void setup_priority_timeout(unsigned int msec)
{
	cancel_priority_timeout();

	/* Setup new timeout */
	priority_timeout_cb_id = g_timeout_add(msec, priority_timeout_cb, NULL);
}


static pattern_t find_pattern(const char *const name)
{
	for (uint_fast32_t i = 0; i < patternsCount; ++i) {
		if (strcmp(name, patterns[i].name) == 0) {
			return patterns[i];
		}
	}
	pattern_t ivld = { 0 };
	ivld.invalid = true;
	return ivld;
}

static gboolean should_run_pattern(const pattern_t pattern)
{
	if (pattern.policy == VIBRATE_POLICY_PLAY_ALWAYS || pattern.policy == VIBRATE_POLICY_PLAY_DISPLAY_ON_ACTDEAD)
		return true;
	else if (pattern.policy == VIBRATE_POLICY_PLAY_DISPLAY_OFF_OR_ACTDEAD &&
		 (system_state == MCE_STATE_ACTDEAD
		  || display_state == MCE_DISPLAY_OFF))
		return true;
	else if (pattern.policy == VIBRATE_POLICY_PLAY_DISPLAY_OFF_ACTDEAD &&
		 (system_state == MCE_STATE_ACTDEAD
		  && display_state == MCE_DISPLAY_OFF))
		return true;
	else if (pattern.policy == VIBRATE_POLICY_PLAY_DISPLAY_ON_OR_OFF && system_state != MCE_STATE_ACTDEAD)
		return true;
	else if (pattern.policy == VIBRATE_POLICY_PLAY_DISPLAY_OFF &&
		 (system_state != MCE_STATE_ACTDEAD
		  && display_state == MCE_DISPLAY_OFF))
		return true;
	return false;
}

static gboolean run_pattern(const pattern_t pattern)
{
	if (!pattern.invalid && vibratorArmed && should_run_pattern(pattern)) {
		if (pattern.priority < priority) {
			priority = pattern.priority;
			int count;
			if (pattern.repeat_count != 0)
				count = pattern.repeat_count;
			else if (pattern.timeout != 0) {
				count =
				    (pattern.timeout * 1000ULL) /
				    (pattern.on_period + pattern.off_period) +
				    1;
			} else {
				count = INT_MAX;
			}
			if( ((int64_t)count)*(pattern.accel_period + pattern.on_period + pattern.decel_period) < INT_MAX )
				setup_priority_timeout((pattern.accel_period + pattern.on_period + pattern.decel_period)*count);
			return ff_device_run(evdev_fd,
					     pattern.accel_period +
					     pattern.on_period +
					     pattern.decel_period,
					     pattern.off_period, count,
					     pattern.speed,
					     pattern.accel_period,
					     pattern.decel_period);
		}
	}
	return true;
}

static gboolean vibrator_activate_pattern_dbus_cb(DBusMessage * const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	char *patternName = NULL;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "%s: Received activate vibrator pattern request", MODULE_NAME);

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &patternName,
				  DBUS_TYPE_INVALID) == false) {
		mce_log(LL_CRIT, "Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_ACTIVATE_VIBRATOR_PATTERN,
			error.message);
		dbus_error_free(&error);
		return false;
	}

	run_pattern(find_pattern(patternName));

	if (no_reply == false) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		return dbus_send_message(reply);
	}

	return true;
}

static gboolean vibrator_deactivate_pattern_dbus_cb(DBusMessage * const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	if (!ff_device_stop(evdev_fd))
		return false;
	
	cancel_priority_timeout();
	priority = 256;

	if (no_reply == false) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		return dbus_send_message(reply);
	}
	return true;
}

static gboolean vibrator_enable_dbus_cb(DBusMessage * const msg)
{
	vibratorArmed = true;
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);

	if (no_reply == false) {
		DBusMessage *reply = dbus_new_method_reply(msg);
		return dbus_send_message(reply);
	}
	return true;
}

static gboolean vibrator_disable_dbus_cb(DBusMessage * const msg)
{
	vibratorArmed = false;
	return vibrator_deactivate_pattern_dbus_cb(msg);
}

static void free_patterns(void)
{
	if (patterns) {
		for (uint_fast32_t i = 0; i < patternsCount; ++i) {
			free(patterns[i].name);
		}
	}
	free(patterns);
}

static gboolean init_patterns(void)
{
	struct pattern_t *pattern = NULL;
	char **patternlist = NULL;
	gsize length;

	free_patterns();

	patternlist = mce_conf_get_string_list(MCE_CONF_VIBRATOR_GROUP,
					       MCE_CONF_VIBRATOR_PATTERNS,
					       &length, NULL);

	if (patternlist == NULL) {
		mce_log(LL_WARN, "%s: Failed to configure vibrator patterns", MODULE_NAME);
		return false;
	}

	patterns = malloc(length * sizeof(pattern_t));
	if (!patterns) {
		g_strfreev(patternlist);
		return false;
	}

	for (int i = 0; patternlist[i]; ++i) {
		int *tmp;

		mce_log(LL_DEBUG, "%s: Getting Vibra pattern for: %s",
			MODULE_NAME, patternlist[i]);

		tmp = mce_conf_get_int_list(MCE_CONF_VIBRATOR_GROUP,
					    patternlist[i], &length, NULL);
		if (tmp != NULL) {
			if (length != NUMBER_OF_PATTERN_FIELDS) {
				mce_log(LL_ERR, "%s: Skipping invalid Vibra-pattern", MODULE_NAME);
				g_free(tmp);
				continue;
			}

			pattern = &patterns[patternsCount];
			pattern->name = strdup(patternlist[patternsCount]);
			pattern->priority = tmp[PATTERN_PRIO_FIELD];
			pattern->policy = tmp[PATTERN_SCREEN_ON_FIELD];
			pattern->timeout =
			    tmp[PATTERN_TIMEOUT_FIELD] ?
			    tmp[PATTERN_TIMEOUT_FIELD] : -1;
			pattern->repeat_count =
			    ABS(tmp[PATTERN_REPEAT_COUNT_FIELD]);
			pattern->accel_period =
			    ABS(tmp[PATTERN_ACCEL_PERIOD_FIELD]);
			pattern->on_period = ABS(tmp[PATTERN_ON_PERIOD_FIELD]);
			pattern->decel_period =
			    ABS(tmp[PATTERN_DECEL_PERIOD_FIELD]);
			pattern->off_period = ABS(tmp[PATTERN_OFF_PERIOD_FIELD]);
			pattern->speed = ABS(tmp[PATTERN_SPEED_FIELD]);
			pattern->invalid = false;
			
			++patternsCount;
			
			g_free(tmp);
		}
	}

	return true;
}

static gboolean vibrator_stop_manual_vibration_cb(DBusMessage * msg)
{
	return vibrator_deactivate_pattern_dbus_cb(msg);
}

static gboolean vibrator_start_manual_vibration_cb(DBusMessage * msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	int_fast32_t speed = 0;
	int_fast32_t duration = 0;
	gboolean retVal = false;

	DBusError error;
	dbus_error_init(&error);

	mce_log(LL_DEBUG, "%s: Received start manual vibration request", MODULE_NAME);
	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_INT32, &speed,
				  DBUS_TYPE_INT32, &duration,
				  DBUS_TYPE_INVALID) == false) {
		mce_log(LL_CRIT, "%s: Failed to get argument from %s.%s: %s", MODULE_NAME, 
			MCE_REQUEST_IF, MCE_ACTIVATE_VIBRATOR_PATTERN,
			error.message);
		dbus_error_free(&error);
		return false;
	} else {
		if (priority == 256)
		{
			setup_priority_timeout(duration);
			if (!ff_device_run
			    (evdev_fd, duration, 0, 1, speed, 0, 0)) {
				mce_log(LL_WARN, "%s: ff_device_run returned false", MODULE_NAME);
			}
		}
		if (no_reply == false) {
			DBusMessage *reply = dbus_new_method_reply(msg);
			retVal = dbus_send_message(reply);
		} else
			retVal = true;
	}

	return retVal;
}

static void system_state_trigger(gconstpointer data)
{
	(void)data;
	system_state = datapipe_get_gint(system_state_pipe);
}

static void display_state_trigger(gconstpointer data)
{
	(void)data;
	display_state = datapipe_get_gint(display_state_pipe);
}

static void call_state_trigger(gconstpointer data)
{
	(void)data;
	call_state = datapipe_get_gint(call_state_pipe);
}

static void vibrator_pattern_activate_trigger(gconstpointer data)
{
	run_pattern(find_pattern((const char *)data));
}

static void vibrator_pattern_deactivate_trigger(gconstpointer data)
{
	(void)data;
	ff_device_stop(evdev_fd);
}

static void scan_device_cb(const char *filename, gpointer user_data)
{
	(void)user_data;

	if (evdev_fd < 0) {
		evdev_fd = ff_device_open(filename);
		if (evdev_fd == -4) {
			mce_log(LL_DEBUG,
				"%s: Can not open %s errno: %s",
				MODULE_NAME, filename, strerror(errno));
		} else if (evdev_fd >= 0) {
			mce_log(LL_INFO, "%s: Using %s for force feedback", MODULE_NAME, filename);
		}
	}
}

G_MODULE_EXPORT const char *g_module_check_init(GModule * module);
const char *g_module_check_init(GModule * module)
{
	(void)module;

	mce_log(LL_DEBUG, "%s: Initalizing", MODULE_NAME);

	remove_output_trigger_from_datapipe(&vibrator_pattern_deactivate_pipe,
					    vibrator_pattern_deactivate_trigger);
	remove_output_trigger_from_datapipe(&vibrator_pattern_activate_pipe,
					    vibrator_pattern_activate_trigger);

	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);
	append_output_trigger_to_datapipe(&call_state_pipe, call_state_trigger);

	display_state = datapipe_get_gint(display_state_pipe);
	system_state = datapipe_get_gint(system_state_pipe);
	call_state = datapipe_get_gint(call_state_pipe);

	if (!init_patterns()) {
		mce_log(LL_CRIT, "%s: Adding patterns failed", MODULE_NAME);
		return NULL;
	}

	mce_scan_inputdevices(&scan_device_cb, NULL);

	if (evdev_fd < 0) {
		mce_log(LL_WARN,
			"%s: No usable force feedback device available, vibration disabled.", MODULE_NAME);
		return NULL;
	}

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ACTIVATE_VIBRATOR_PATTERN,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_activate_pattern_dbus_cb) == NULL) {
		mce_log(LL_CRIT, "%s: Adding %s debus handler failed",
			MODULE_NAME, MCE_ACTIVATE_VIBRATOR_PATTERN);
		return NULL;
	}

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEACTIVATE_VIBRATOR_PATTERN,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_deactivate_pattern_dbus_cb) == NULL) {
		mce_log(LL_CRIT, "%s: Adding %s debus handler failed",
			MODULE_NAME, MCE_DEACTIVATE_VIBRATOR_PATTERN);
		return NULL;
	}

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_ENABLE_VIBRATOR,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_enable_dbus_cb) == NULL) {
		mce_log(LL_CRIT, "%s: Adding %s debus handler failed",
			MODULE_NAME, MCE_ENABLE_VIBRATOR);
		return NULL;
	}

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DISABLE_VIBRATOR,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_disable_dbus_cb) == NULL) {
		mce_log(LL_CRIT, "%s: Adding %s debus handler failed",
			MODULE_NAME, MCE_DISABLE_VIBRATOR);
		return NULL;
	}

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_START_MANUAL_VIBRATION,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_start_manual_vibration_cb) == NULL) {
		mce_log(LL_CRIT, "%s: Adding %s debus handler failed",
			MODULE_NAME, MCE_START_MANUAL_VIBRATION);
		return NULL;
	}

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_STOP_MANUAL_VIBRATION,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 vibrator_stop_manual_vibration_cb) == NULL) {
		mce_log(LL_CRIT, "%s: Adding %s debus handler failed",
			MODULE_NAME, MCE_STOP_MANUAL_VIBRATION);
		return NULL;
	}

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	cancel_priority_timeout();

	free_patterns();

	remove_output_trigger_from_datapipe(&call_state_pipe,
					    call_state_trigger);
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);

}
