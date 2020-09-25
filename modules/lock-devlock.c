/**
 * @file devlock.c
 * <p>
 * Copyright © 2004-2010 Nokia Corporation and/or its subsidiary(-ies).
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

#include <stdlib.h>
#include <gmodule.h>
#include "libdevlock.h"
#include <mce/mode-names.h>
#include <systemui/dbus-names.h>
#include <systemui/devlock-dbus-names.h>
#include "mce.h"
#include "mce-io.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "mce-dsme.h"
#include "mce-gconf.h"
#include "datapipe.h"

#define MODULE_NAME		"lock-devlock"

#define MODULE_PROVIDES	"devlock"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 1000
};

#define MCE_CONF_DEVLOCK_GROUP			"DevLock"
#define MCE_CONF_DEVLOCK_DELAY_0		"DevLockDelay0"
#define MCE_CONF_DEVLOCK_DELAY_1		"DevLockDelay1"
#define MCE_CONF_DEVLOCK_DELAY_2		"DevLockDelay2"
#define MCE_CONF_DEVLOCK_DELAY_3		"DevLockDelay3"
#define MCE_CONF_DEVLOCK_SHUTDOWN_TIMEOUT	"DevLockShutdownTimeout"

#ifndef MCE_GCONF_LOCK_PATH
#define MCE_GCONF_LOCK_PATH		"/system/osso/dsm/locks"
#endif /* MCE_GCONF_LOCK_PATH */

#define DEFAULT_DEVICE_AUTOLOCK_ENABLED		FALSE
#define DEFAULT_DEVICE_AUTOLOCK_TIMEOUT		10
#define DEFAULT_DEVICE_LOCK_FAILED		0
#define DEFAULT_DEVICE_LOCK_TOTAL_FAILED	0

#define MCE_GCONF_DEVICE_AUTOLOCK_ENABLED_PATH	MCE_GCONF_LOCK_PATH "/devicelock_autolock_enabled"
#define MCE_GCONF_DEVICE_AUTOLOCK_TIMEOUT_PATH	MCE_GCONF_LOCK_PATH "/devicelock_autolock_timeout"
#define MCE_GCONF_DEVICE_LOCK_FAILED_PATH	MCE_GCONF_LOCK_PATH "/devicelock_failed"
#define MCE_GCONF_DEVICE_LOCK_TOTAL_FAILED_PATH	MCE_GCONF_LOCK_PATH "/devicelock_total_failed"

#define MCE_DEVLOCK_CB_REQ		"devlock_callback"

/** Default lock delays in seconds */
enum {
	DEFAULT_LOCK_DELAY_0 = 0,
	DEFAULT_LOCK_DELAY_1 = 1,
	DEFAULT_LOCK_DELAY_2 = 1,
	DEFAULT_LOCK_DELAY_3 = 5,
};

#define DEFAULT_SHUTDOWN_TIMEOUT	0

static gint device_lock_failed = DEFAULT_DEVICE_LOCK_FAILED;
static gint device_lock_total_failed = DEFAULT_DEVICE_LOCK_TOTAL_FAILED;

static gboolean device_autolock_enabled = DEFAULT_DEVICE_AUTOLOCK_ENABLED;
static guint devlock_autorelock_notify_cb_id = 0;

static gint device_autolock_timeout = DEFAULT_DEVICE_AUTOLOCK_TIMEOUT * 60;
static guint devlock_timeout_notify_cb_id = 0;

static guint device_autolock_timeout_cb_id = 0;

static gboolean devlock_query_enabled = TRUE;

static guint devlock_query_timeout_cb_id = 0;
static guint shutdown_timeout_cb_id = 0;

static gint lock_delay_0 = DEFAULT_LOCK_DELAY_0;
static gint lock_delay_1 = DEFAULT_LOCK_DELAY_1;
static gint lock_delay_2 = DEFAULT_LOCK_DELAY_2;
static gint lock_delay_3 = DEFAULT_LOCK_DELAY_3;

static gint shutdown_timeout = DEFAULT_SHUTDOWN_TIMEOUT;

static gboolean cached_call_active = FALSE;

static gboolean shutdown_confirmation_pending = FALSE;

static gboolean devicelock_ui_visible = FALSE;

static gboolean b_devlock_was_opened = FALSE;

static gboolean enable_devlock(void);

static gboolean is_device_autolock_enabled(void) G_GNUC_PURE;
static gboolean is_device_autolock_enabled(void)
{
	return device_autolock_enabled;
}

static gboolean is_devlock_enabled(void) G_GNUC_PURE;
static gboolean is_devlock_enabled(void)
{
	return ((mce_get_submode_int32() & MCE_DEVLOCK_SUBMODE) != 0);
}

static gboolean is_verify_enabled(void) G_GNUC_PURE;
static gboolean is_verify_enabled(void)
{
	return ((mce_get_submode_int32() & MCE_VERIFY_SUBMODE) != 0);
}

static gboolean device_autolock_timeout_cb(gpointer data)
{
	(void)data;

	device_autolock_timeout_cb_id = 0;

	(void)execute_datapipe(&device_lock_pipe,
			       GINT_TO_POINTER(LOCK_ON),
			       USE_INDATA, CACHE_INDATA);

	return FALSE;
}

static void cancel_device_autolock_timeout(void)
{
	if (device_autolock_timeout_cb_id != 0) {
		g_source_remove(device_autolock_timeout_cb_id);
		device_autolock_timeout_cb_id = 0;
	}
}

static void setup_device_autolock_timeout(void)
{
	system_state_t system_state = datapipe_get_gint(system_state_pipe);

	cancel_device_autolock_timeout();

	if (system_state != MCE_STATE_USER)
		return;

	if (is_device_autolock_enabled() == FALSE)
		return;

	device_autolock_timeout_cb_id =
		g_timeout_add_seconds(device_autolock_timeout,
				      device_autolock_timeout_cb, NULL);
}

static void devlock_autorelock_notify_cb(gboolean enabled)
{
	device_autolock_enabled = enabled;
}

static void devlock_timeout_notify_cb(gint timeout)
{
	device_autolock_timeout = 60 * timeout;
}

static gboolean mce_send_devlock_mode(DBusMessage *const method_call)
{
	DBusMessage *msg = NULL;
	const gchar *modestring;
	gboolean status = FALSE;

	if (is_devlock_enabled() == TRUE)
		modestring = MCE_DEVICE_LOCKED;
	else
		modestring = MCE_DEVICE_UNLOCKED;

	if (method_call != NULL)
		msg = dbus_new_method_reply(method_call);
	else
		msg = dbus_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
				      MCE_DEVLOCK_MODE_SIG);

	if (dbus_message_append_args(msg,
				     DBUS_TYPE_STRING, &modestring,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append %sargument to D-Bus message "
			"for %s.%s",
			method_call ? "reply " : "",
			method_call ? MCE_REQUEST_IF :
				      MCE_SIGNAL_IF,
			method_call ? MCE_DEVLOCK_MODE_GET :
				      MCE_DEVLOCK_MODE_SIG);
		dbus_message_unref(msg);
		goto EXIT;
	}

	status = dbus_send_message(msg);

EXIT:
	return status;
}

static void devlock_ui_open_reply_dbus_cb(DBusPendingCall *pending_call,
					  void *data)
{
	DBusMessage *reply;
	dbus_int32_t retval;
	DBusError error;

	dbus_error_init(&error);

	(void)data;

	mce_log(LL_DEBUG, "Received device lock UI reply");

	if ((reply = dbus_pending_call_steal_reply(pending_call)) == NULL) {
		mce_log(LL_ERR,
			"Device lock reply callback invoked, "
			"but no pending call available");
		goto EXIT;
	}

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		char *error_msg;

		if (dbus_message_get_args(reply, &error,
					  DBUS_TYPE_STRING, &error_msg,
					  DBUS_TYPE_INVALID) == FALSE) {
			mce_log(LL_CRIT,
				"Failed to get error reply argument "
				"from %s.%s: %s",
				SYSTEMUI_REQUEST_IF, SYSTEMUI_DEVLOCK_OPEN_REQ,
				error.message);
			dbus_error_free(&error);
		} else {
			mce_log(LL_ERR,
				"D-Bus call to %s.%s failed: %s",
				SYSTEMUI_REQUEST_IF, SYSTEMUI_DEVLOCK_OPEN_REQ,
				error_msg);
		}

		goto EXIT2;
	}

	if (dbus_message_get_args(reply, &error,
				  DBUS_TYPE_INT32, &retval,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get reply argument from %s.%s: %s",
			SYSTEMUI_REQUEST_IF, SYSTEMUI_DEVLOCK_OPEN_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT2;
	}

	switch (retval) {
	case DEVLOCK_REPLY_LOCKED:
		enable_devlock();
		devicelock_ui_visible = TRUE;
		break;

	case DEVLOCK_REPLY_VERIFY:
		mce_add_submode_int32(MCE_VERIFY_SUBMODE);
		devicelock_ui_visible = TRUE;
		break;

	case DEVLOCK_REPLY_FAILED:
		mce_log(LL_ERR,
			"Device lock already opened another by other process");
		mce_rem_submode_int32(MCE_VERIFY_SUBMODE);
		break;

	default:
		mce_log(LL_ERR,
			"Unknown return value received from the device lock");
		mce_rem_submode_int32(MCE_VERIFY_SUBMODE);
		break;
	}

EXIT2:
	dbus_message_unref(reply);

EXIT:
	dbus_pending_call_unref(pending_call);

	return;
}

static gboolean open_devlock_ui(const dbus_uint32_t mode)
{
	const gchar *const cb_service = MCE_SERVICE;
	const gchar *const cb_path = MCE_REQUEST_PATH;
	const gchar *const cb_interface = MCE_REQUEST_IF;
	const gchar *const cb_method = MCE_DEVLOCK_CB_REQ;

	return dbus_send(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
			 SYSTEMUI_REQUEST_IF, SYSTEMUI_DEVLOCK_OPEN_REQ,
			 devlock_ui_open_reply_dbus_cb,
			 DBUS_TYPE_STRING, &cb_service,
			 DBUS_TYPE_STRING, &cb_path,
			 DBUS_TYPE_STRING, &cb_interface,
			 DBUS_TYPE_STRING, &cb_method,
			 DBUS_TYPE_UINT32, &mode,
			 DBUS_TYPE_INVALID);
}

static gboolean close_devlock_ui(void)
{
	return dbus_send(SYSTEMUI_SERVICE, SYSTEMUI_REQUEST_PATH,
			 SYSTEMUI_REQUEST_IF, SYSTEMUI_DEVLOCK_CLOSE_REQ, NULL,
			 DBUS_TYPE_INVALID);
}

static gboolean shutdown_timeout_cb(gpointer data)
{
	(void)data;

	shutdown_timeout_cb_id = 0;

	mce_log(LL_WARN,
		"Requesting shutdown from devlock.c: shutdown_timeout_cb()");

	request_normal_shutdown();

	return FALSE;
}

static void cancel_shutdown_timeout(void)
{
	if (shutdown_timeout_cb_id != 0) {
		g_source_remove(shutdown_timeout_cb_id);
		shutdown_timeout_cb_id = 0;
	}
}

static void setup_shutdown_timeout(void)
{
	cancel_shutdown_timeout();

	if (shutdown_timeout > 0)
		shutdown_timeout_cb_id =
			g_timeout_add_seconds(shutdown_timeout,
					      shutdown_timeout_cb, NULL);
}

static gboolean devlock_query_timeout_cb(gpointer data)
{
	(void)data;

	devlock_query_timeout_cb_id = 0;

	open_devlock_ui(DEVLOCK_QUERY_ENABLE_QUIET);
	devlock_query_enabled = TRUE;

	return FALSE;
}

static void cancel_devlock_query_timeout(void)
{
	if (devlock_query_timeout_cb_id != 0) {
		g_source_remove(devlock_query_timeout_cb_id);
		devlock_query_timeout_cb_id = 0;
	}
}

static void setup_devlock_query_timeout(guint delay)
{
	cancel_devlock_query_timeout();

	devlock_query_timeout_cb_id =
		g_timeout_add_seconds(delay, devlock_query_timeout_cb, NULL);
}

static void devlock_delay(void)
{
	guint delay = 0;

	switch (device_lock_failed % 4) {
	default:
	case 0:
		delay = lock_delay_0;
		break;

	case 1:
		delay = lock_delay_1;
		break;

	case 2:
		delay = lock_delay_2;
		break;

	case 3:
		delay = lock_delay_3;
		break;
	}

	setup_devlock_query_timeout(delay);
	devlock_query_enabled = FALSE;
}

static void update_password_count(void)
{
	set_passwd_failed_count(device_lock_failed);
	set_passwd_total_failed_count(device_lock_total_failed);
}

static void enable_devlock_internal(void)
{
	if (device_lock_failed == 0) {
		device_lock_failed = 4;
		update_password_count();
	}
}

static void disable_devlock_internal(void)
{
	device_lock_failed = 0;
	update_password_count();
}

static gboolean enable_devlock(void)
{
	mce_add_submode_int32(MCE_DEVLOCK_SUBMODE);
	mce_rem_submode_int32(MCE_VERIFY_SUBMODE);
	(void)mce_send_devlock_mode(NULL);
	enable_devlock_internal();

	return TRUE;
}

static gboolean request_devlock(const dbus_uint32_t mode)
{
	if (devicelock_ui_visible == TRUE)
		return TRUE;

	mce_add_submode_int32(MCE_VERIFY_SUBMODE);

	return open_devlock_ui(mode);
}

static gboolean disable_devlock(void)
{
	gboolean status = FALSE;

	if (close_devlock_ui() == FALSE)
		goto EXIT;

	mce_rem_submode_int32(MCE_DEVLOCK_SUBMODE);
	mce_rem_submode_int32(MCE_VERIFY_SUBMODE);
	(void)mce_send_devlock_mode(NULL);
	devicelock_ui_visible = FALSE;
	status = TRUE;

EXIT:
	return status;
}

static gboolean devlock_mode_get_req_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG, "Received devlock mode get request");

	if (mce_send_devlock_mode(msg) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

static gboolean systemui_devlock_dbus_cb(DBusMessage *const msg)
{
	dbus_int32_t result = INT_MAX;
	gboolean status = FALSE;
	DBusError error;

	dbus_error_init(&error);

	mce_log(LL_DEBUG, "Received devlock callback");

	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_INT32, &result,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to get argument from %s.%s: %s",
			MCE_REQUEST_IF, MCE_DEVLOCK_CB_REQ,
			error.message);
		dbus_error_free(&error);
		goto EXIT;
	}

	mce_log(LL_DEBUG, "devlock callback value: %d", result);

	switch (result) {
	case DEVLOCK_RESPONSE_LOCKED:
		enable_devlock();
		break;

	case DEVLOCK_RESPONSE_SHUTDOWN:
		shutdown_confirmation_pending = FALSE;
		cancel_shutdown_timeout();

		mce_log(LL_WARN,
			"Requesting shutdown from devlock.c: "
			"systemui_devlock_dbus_cb()");

		request_normal_shutdown();
		break;

	case DEVLOCK_RESPONSE_NOSHUTDOWN:
		shutdown_confirmation_pending = FALSE;
		cancel_shutdown_timeout();
		open_devlock_ui(DEVLOCK_QUERY_ENABLE_QUIET);
		break;

	case DEVLOCK_RESPONSE_CORRECT:
		disable_devlock();
		disable_devlock_internal();
		break;

	case DEVLOCK_RESPONSE_INCORRECT:
		open_devlock_ui(DEVLOCK_QUERY_OPEN);
		devlock_delay();

		if (device_lock_failed < G_MAXINT)
			device_lock_failed++;

		if (device_lock_total_failed < G_MAXINT)
			device_lock_total_failed++;

		update_password_count();
		break;

	case DEVLOCK_RESPONSE_CANCEL:
		if (cached_call_active == TRUE) {
			(void)mce_send_devlock_mode(NULL);
		} else if (devlock_query_enabled == TRUE) {
			shutdown_confirmation_pending = TRUE;
			open_devlock_ui(DEVLOCK_QUERY_NOTE);
			setup_shutdown_timeout();
		}
		break;

	default:
		open_devlock_ui(DEVLOCK_QUERY_ENABLE_QUIET);
		break;
	}

	status = TRUE;

EXIT:
	return status;
}

static gboolean devlock_startup(void)
{
	static gboolean first_devlock_startup = TRUE;

	if ((is_device_autolock_enabled() == TRUE) &&
	    (first_devlock_startup == TRUE)) {
		first_devlock_startup = FALSE;
		mce_add_submode_int32(MCE_DEVLOCK_SUBMODE);
	}

	if (is_devlock_enabled() == TRUE) {
		if (request_devlock(DEVLOCK_QUERY_ENABLE_QUIET) == FALSE) {
			mce_log(LL_CRIT, "Failed to lock device");
			g_main_loop_quit(mainloop);
			exit(EXIT_FAILURE);
		}

		mce_log(LL_DEBUG, "Enabling device lock");
	}

	return TRUE;
}

static void devlock_shutdown(void)
{
	if ((is_devlock_enabled() == TRUE) || (device_lock_failed != 0))
		enable_devlock_internal();

	(void)disable_devlock();
	mce_log(LL_DEBUG, "Disabling device lock");

	shutdown_confirmation_pending = FALSE;
	cancel_device_autolock_timeout();
	cancel_shutdown_timeout();
}

static void device_inactive_trigger(gconstpointer data)
{
	gboolean device_lock_inhibit =
			datapipe_get_gbool(device_lock_inhibit_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);
	gboolean device_inactive = GPOINTER_TO_INT(data);

	if ((device_lock_inhibit == FALSE) &&
	    ((device_inactive == FALSE) || ((device_inactive == TRUE) && !device_autolock_timeout_cb_id)) &&
	    ((call_state != CALL_STATE_RINGING) &&
	     (call_state != CALL_STATE_ACTIVE)))
		setup_device_autolock_timeout();
}

static void device_lock_inhibit_trigger(gconstpointer data)
{
	gboolean device_inactive = datapipe_get_gbool(device_inactive_pipe);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);
	gboolean device_lock_inhibit = GPOINTER_TO_INT(data);

	if ((device_lock_inhibit == FALSE) &&
	    (device_inactive == TRUE) &&
	    ((call_state != CALL_STATE_RINGING) &&
	     (call_state != CALL_STATE_ACTIVE)) &&
	    (device_autolock_timeout_cb_id == 0))
		setup_device_autolock_timeout();
	else if (device_lock_inhibit == TRUE)
		cancel_device_autolock_timeout();
}

static void call_state_trigger(gconstpointer data)
{
	gboolean device_inactive = datapipe_get_gbool(device_inactive_pipe);
	gboolean device_lock_inhibit = datapipe_get_gbool(device_lock_inhibit_pipe);
	call_state_t callstate = GPOINTER_TO_INT(data);

	switch (callstate) {
	case CALL_STATE_RINGING:
	case CALL_STATE_ACTIVE:
		if ((is_verify_enabled() == TRUE) &&
		    (callstate == CALL_STATE_ACTIVE)) {
			mce_rem_submode_int32(MCE_VERIFY_SUBMODE);
			close_devlock_ui();
			b_devlock_was_opened = TRUE;
		}
		(void)mce_write_string_to_file(MCE_DEVLOCK_FILENAME,
					       DISABLED_STRING);
		cancel_device_autolock_timeout();
		cancel_shutdown_timeout();
		cached_call_active = TRUE;
		break;

	default:
		(void)mce_write_string_to_file(MCE_DEVLOCK_FILENAME,
					       ENABLED_STRING);
		if (b_devlock_was_opened == TRUE) {
			mce_add_submode_int32(MCE_VERIFY_SUBMODE);            
			open_devlock_ui(DEVLOCK_QUERY_ENABLE_QUIET);
			b_devlock_was_opened = FALSE;
		}

		if ((!device_lock_inhibit && !device_inactive) ||
		    (device_inactive == TRUE))
			setup_device_autolock_timeout();

		if (shutdown_confirmation_pending == TRUE)
			setup_shutdown_timeout();

		cached_call_active = FALSE;
		break;
	}
}

static void device_lock_trigger(gconstpointer data)
{
	lock_state_t device_lock_state = GPOINTER_TO_INT(data);

	switch (device_lock_state) {
	case LOCK_OFF:
		disable_devlock();
		break;

	case LOCK_ON:
		request_devlock(DEVLOCK_QUERY_ENABLE);
		break;

	default:
		break;
	}
}

static void system_state_trigger(gconstpointer data)
{
	static system_state_t old_system_state = MCE_STATE_UNDEF;
	system_state_t system_state = GPOINTER_TO_INT(data);

	switch (system_state) {
	case MCE_STATE_USER:
		if (old_system_state != MCE_STATE_ACTDEAD)
			devlock_startup();
		break;

	case MCE_STATE_SHUTDOWN:
	case MCE_STATE_ACTDEAD:
	case MCE_STATE_REBOOT:
		devlock_shutdown();
		break;

	default:
		break;
	}

	old_system_state = system_state;
}

G_MODULE_EXPORT const char *g_module_check_init(GModule * module);
const char *g_module_check_init(GModule * module)
{
	(void)module;
	gboolean status = FALSE;

	append_output_trigger_to_datapipe(&device_inactive_pipe,
					  device_inactive_trigger);
	append_output_trigger_to_datapipe(&device_lock_inhibit_pipe,
					  device_lock_inhibit_trigger);
	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);
	append_output_trigger_to_datapipe(&device_lock_pipe,
					  device_lock_trigger);
	append_output_trigger_to_datapipe(&call_state_pipe,
					  call_state_trigger);

	get_passwd_failed_count(&device_lock_failed);

	if (device_lock_failed != 0)
		mce_add_submode_int32(MCE_DEVLOCK_SUBMODE);

	get_passwd_total_failed_count(&device_lock_total_failed);

	get_autolock_key(&device_autolock_enabled);

	if (is_device_autolock_enabled() == TRUE)
		mce_add_submode_int32(MCE_DEVLOCK_SUBMODE);

	(void)mce_send_devlock_mode(NULL);

	if (devlock_autorelock_notify_add(devlock_autorelock_notify_cb,
					  &devlock_autorelock_notify_cb_id,
					  0) == FALSE) {
		mce_log(LL_WARN, "Devlock_autorelock_notify_add failed...");
		goto EXIT;
	}

	get_timeout_key(&device_autolock_timeout);

	device_autolock_timeout *= 60;

	if (devlock_timeout_notify_add(devlock_timeout_notify_cb,
				       &devlock_timeout_notify_cb_id,
				       0) == FALSE) {
		mce_log(LL_WARN, "devlock_timeout_notify_add failed...");
		goto EXIT;
	}

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEVLOCK_MODE_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 devlock_mode_get_req_dbus_cb) == NULL)
		goto EXIT;

	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_DEVLOCK_CB_REQ,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 systemui_devlock_dbus_cb) == NULL)
		goto EXIT;

	lock_delay_0 = mce_conf_get_int(MCE_CONF_DEVLOCK_GROUP,
					MCE_CONF_DEVLOCK_DELAY_0,
					DEFAULT_LOCK_DELAY_0,
					NULL);

	lock_delay_1 = mce_conf_get_int(MCE_CONF_DEVLOCK_GROUP,
					MCE_CONF_DEVLOCK_DELAY_1,
					DEFAULT_LOCK_DELAY_1,
					NULL);

	lock_delay_2 = mce_conf_get_int(MCE_CONF_DEVLOCK_GROUP,
					MCE_CONF_DEVLOCK_DELAY_2,
					DEFAULT_LOCK_DELAY_2,
					NULL);

	lock_delay_3 = mce_conf_get_int(MCE_CONF_DEVLOCK_GROUP,
					MCE_CONF_DEVLOCK_DELAY_3,
					DEFAULT_LOCK_DELAY_3,
					NULL);

	shutdown_timeout = mce_conf_get_int(MCE_CONF_DEVLOCK_GROUP,
					    MCE_CONF_DEVLOCK_SHUTDOWN_TIMEOUT,
					    DEFAULT_SHUTDOWN_TIMEOUT,
					    NULL);

	status = TRUE;

EXIT:
	return status ? NULL : "Failure";
}

G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;
	if (devlock_autorelock_notify_cb_id)
		devlock_notify_remove(devlock_autorelock_notify_cb_id);

	if (devlock_timeout_notify_cb_id)
		devlock_notify_remove(devlock_timeout_notify_cb_id);

	remove_output_trigger_from_datapipe(&call_state_pipe,
					    call_state_trigger);
	remove_output_trigger_from_datapipe(&device_lock_pipe,
					    device_lock_trigger);
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);
	remove_output_trigger_from_datapipe(&device_lock_inhibit_pipe,
					    device_lock_inhibit_trigger);
	remove_output_trigger_from_datapipe(&device_inactive_pipe,
					    device_inactive_trigger);

	cancel_device_autolock_timeout();
	cancel_devlock_query_timeout();
	cancel_shutdown_timeout();

	return;
}
