#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/input.h>
#include "mce.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "datapipe.h"

#define MODULE_NAME		"lock-generic"

#define MODULE_PROVIDES	"lock"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

int autolock_timeout = 10;
bool autolock = true;
bool unlock_on_slide = false;
bool slidelock = false;
static guint autolock_cb_id = 0;

char *lock_command = NULL;

static void synthesise_activity(void)
{
	(void)execute_datapipe(&device_inactive_pipe,
			       GINT_TO_POINTER(FALSE),
			       USE_INDATA, CACHE_INDATA);
}

static void set_visual_lock( bool lock )
{
	if (lock_command)
	{
		pid_t pid = getpid();
		pid_t fpid = fork();
		if (fpid < 0) {
			mce_log(LL_CRIT, "%s: Failed to fork", MODULE_NAME);
			return;
		}
		
		if (getpid() != pid)
			execlp(lock_command, "", lock ? "lock" : "reset", (char *)NULL);
	}
}

static void set_lock( bool lock )
{
	if (lock) {
		mce_add_submode_int32(MCE_TKLOCK_SUBMODE);
		set_visual_lock(true);
		execute_datapipe(&touchscreen_suspend_pipe, GINT_TO_POINTER(true), USE_INDATA, CACHE_INDATA);
	}
	else {
		mce_rem_submode_int32(MCE_TKLOCK_SUBMODE);
		synthesise_activity();
		execute_datapipe(&touchscreen_suspend_pipe, GINT_TO_POINTER(false), USE_INDATA, CACHE_INDATA);
	}
}

static gboolean autolock_timeout_cb(gpointer data)
{
	(void)data;
	set_lock(true);
	return FALSE;
}

static void display_state_trigger(gconstpointer const data)
{
	display_state_t state = GPOINTER_TO_INT(data);
	submode_t submode = datapipe_get_gint(submode_pipe);
	
	if (state == MCE_DISPLAY_OFF && autolock && (submode & MCE_TKLOCK_SUBMODE) == 0) {
		autolock_cb_id = g_timeout_add(autolock_timeout, autolock_timeout_cb, NULL);
	}
	else if (state != MCE_DISPLAY_OFF && (submode & MCE_TKLOCK_SUBMODE) != 0) {
		if (autolock_cb_id) {
			g_source_remove(autolock_cb_id);
			autolock_cb_id = 0;
		}
		set_lock(false);
	}
}

static void tk_lock_trigger(gconstpointer data)
{
	lock_state_t lock_state = GPOINTER_TO_INT(data);
	
	switch (lock_state)
	{
		case LOCK_ON:
		case LOCK_ON_DIMMED:
		case LOCK_ON_SILENT:
		case LOCK_ON_SILENT_DIMMED:
			set_lock(true);
			execute_datapipe(&display_state_pipe, GINT_TO_POINTER(MCE_DISPLAY_OFF),USE_INDATA, CACHE_INDATA);
			break;
		case LOCK_OFF:
		case LOCK_OFF_SILENT:
		case LOCK_OFF_DELAYED:
			set_lock(false);
			set_visual_lock(false);
			execute_datapipe(&display_state_pipe, GINT_TO_POINTER(MCE_DISPLAY_ON),USE_INDATA, CACHE_INDATA);
			break;
		default:
			break;
	}
}

static void keyboard_slide_trigger(gconstpointer data)
{
	cover_state_t kbd_slide_state = GPOINTER_TO_INT(data);
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	
	if (kbd_slide_state == COVER_OPEN && display_state == MCE_DISPLAY_OFF) {
		execute_datapipe(&display_state_pipe, GINT_TO_POINTER(MCE_DISPLAY_ON),USE_INDATA, CACHE_INDATA);
		if (unlock_on_slide) 
			set_visual_lock(false);
	} else if (slidelock && kbd_slide_state == COVER_CLOSED && display_state == MCE_DISPLAY_ON) {
		execute_datapipe(&display_state_pipe, GINT_TO_POINTER(MCE_DISPLAY_OFF),USE_INDATA, CACHE_INDATA);
	}
}

/* this function dosen belong in lock, but tklock dose this so until refactor it stays here*/
static void powerkey_trigger(gconstpointer const data)
{
	static display_state_t display_state_prev = MCE_DISPLAY_UNDEF;
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	struct input_event const *const *evp;
	struct input_event const *ev;

	if (data == NULL)
		return;

	evp = data;
	ev = *evp;

	if (ev != NULL && ev->code == power_keycode) {
		if ( ev->value == 0 && display_state == MCE_DISPLAY_OFF && display_state_prev == MCE_DISPLAY_OFF) {
			display_state_prev = MCE_DISPLAY_UNDEF;
			execute_datapipe(&display_state_pipe, GINT_TO_POINTER(MCE_DISPLAY_ON),USE_INDATA, CACHE_INDATA);
		}
		else if (ev->value == 1) {
			display_state_prev = display_state;
		}
	}
	return;
}

static void call_alarm_state_trigger(gconstpointer const data)
{
	(void)data;
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	if (display_state == MCE_DISPLAY_OFF)
		execute_datapipe(&display_state_pipe, GINT_TO_POINTER(MCE_DISPLAY_ON),USE_INDATA, CACHE_INDATA);
	return;
}

G_MODULE_EXPORT const char *g_module_check_init(GModule * module);
const char *g_module_check_init(GModule * module)
{
	(void)module;

	mce_log(LL_DEBUG, "Initalizing %s", MODULE_NAME);

	autolock_timeout = mce_conf_get_int("LockGeneric", "AutolockTimout", 10, NULL);
	if (autolock_timeout < 0) 
		autolock_timeout = 10;
	
	autolock = mce_conf_get_bool("LockGeneric", "Autolock", true, NULL);
	slidelock = mce_conf_get_bool("LockGeneric", "LockOnSlide", false, NULL);
	lock_command = mce_conf_get_string("LockGeneric", "LockCommand", NULL, NULL);
	unlock_on_slide = mce_conf_get_string("LockGeneric", "UnlockOnSlide", false, NULL);
	
	append_output_trigger_to_datapipe(&display_state_pipe, display_state_trigger);
	append_output_trigger_to_datapipe(&tk_lock_pipe, tk_lock_trigger);
	append_output_trigger_to_datapipe(&keyboard_slide_pipe, keyboard_slide_trigger);
	append_input_trigger_to_datapipe(&keypress_pipe, powerkey_trigger);
	append_output_trigger_to_datapipe(&call_state_pipe, call_alarm_state_trigger);
	append_output_trigger_to_datapipe(&alarm_ui_state_pipe, call_alarm_state_trigger);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	remove_output_trigger_from_datapipe(&display_state_pipe, display_state_trigger);
	remove_output_trigger_from_datapipe(&tk_lock_pipe, tk_lock_trigger);
	remove_output_trigger_from_datapipe(&keyboard_slide_pipe, keyboard_slide_trigger);
}
