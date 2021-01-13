#include <glib.h>
#include <gmodule.h>
#include <stdbool.h>
#include "mce.h"
#include "mce-log.h"
#include "mce-dbus.h"
#include "datapipe.h"

/** Module name */
#define MODULE_NAME "inactivity-inhibit"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_NAME, NULL };

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 250
};

/** Maximum number of monitored services that calls blanking pause */
#define MAX_MONITORED_SERVICES			5

/**
 * Blank prevent timeout, in seconds;
 * Don't alter this, since this is part of the defined behaviour
 * for blanking inhibit that applications rely on
 */
#define BLANK_PREVENT_TIMEOUT			60	/* 60 seconds */

static GSList *blanking_pause_monitor_list = NULL;
static guint blank_prevent_timeout_cb_id = 0;

bool timed_inhibit = false;

static gboolean blank_prevent_timeout_cb(gpointer data)
{
	(void)data;
	blank_prevent_timeout_cb_id = 0;
	timed_inhibit = false;
	execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
					 USE_INDATA, CACHE_INDATA);
	return FALSE;
}

/**
 * Cancel blank prevention timeout
 */
static void cancel_blank_prevent(void)
{
	if (blank_prevent_timeout_cb_id != 0) {
		g_source_remove(blank_prevent_timeout_cb_id);
		blank_prevent_timeout_cb_id = 0;
		timed_inhibit = false;
		execute_datapipe(&device_inactive_pipe, GINT_TO_POINTER(FALSE),
						 USE_INDATA, CACHE_INDATA);
	}
}

/**
 * Prevent screen blanking for display_timeout seconds
 */
static void request_blanking_pause(void)
{
	cancel_blank_prevent();

	/* Setup new timeout */
	blank_prevent_timeout_cb_id =
		g_timeout_add_seconds(BLANK_PREVENT_TIMEOUT,
				      blank_prevent_timeout_cb, NULL);
	timed_inhibit = true;
}

/**
 * D-Bus callback used for monitoring the process that requested
 * blanking prevention; if that process exits, immediately
 * cancel the blanking timeout and resume normal operation
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean blanking_pause_owner_monitor_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;
	const gchar *old_name;
	const gchar *new_name;
	const gchar *service;
	DBusError error;

	/* Register error channel */
	dbus_error_init(&error);

	/* Extract result */
	if (dbus_message_get_args(msg, &error,
				  DBUS_TYPE_STRING, &service,
				  DBUS_TYPE_STRING, &old_name,
				  DBUS_TYPE_STRING, &new_name,
				  DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_ERR,
			"%s: Failed to get argument from %s.%s; %s", MODULE_NAME,
			"org.freedesktop.DBus", "NameOwnerChanged",
			error.message);
		dbus_error_free(&error);
		return status;
	}

	if (mce_dbus_owner_monitor_remove(old_name, &blanking_pause_monitor_list) == 0)
		cancel_blank_prevent();

	status = TRUE;

	return status;
}

static gboolean blanking_pause_req_dbus_cb(DBusMessage *const msg)
{
	dbus_bool_t no_reply = dbus_message_get_no_reply(msg);
	const gchar *sender = dbus_message_get_sender(msg);
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"%s: Received blanking pause request from %s", MODULE_NAME,
		(sender == NULL) ? "(unknown)" : sender);

	request_blanking_pause();

	if (mce_dbus_owner_monitor_add(sender,
				       blanking_pause_owner_monitor_dbus_cb,
				       &blanking_pause_monitor_list,
				       MAX_MONITORED_SERVICES) == -1) {
		mce_log(LL_INFO,
			"%s: Failed to add name owner monitoring for `%s'",
			MODULE_NAME, sender);
	}

	if (no_reply == FALSE) {
		DBusMessage *reply = dbus_new_method_reply(msg);

		status = dbus_send_message(reply);
	} else {
		status = TRUE;
	}

	return status;
}

static gpointer device_inactive_filter(gpointer data)
{
	gboolean device_inactive = GPOINTER_TO_INT(data);
	display_state_t display_state = datapipe_get_gint(display_state_pipe);
	
	if (device_inactive && timed_inhibit && display_state != MCE_DISPLAY_OFF) {
		mce_log(LL_DEBUG,
		"%s: Device inactive state preventedby %s", MODULE_NAME, MODULE_NAME);
		return GINT_TO_POINTER(FALSE);
	}
	
	return data;
}


/**
 * Init function for the inactivity inhibit module
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

	/* Append triggers/filters to datapipes */
	append_filter_to_datapipe(&device_inactive_pipe,
				  device_inactive_filter);
	
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				MCE_PREVENT_BLANK_REQ,
				NULL,
				DBUS_MESSAGE_TYPE_METHOD_CALL,
				blanking_pause_req_dbus_cb) == NULL)
		return NULL;
	
	
	return NULL;
}

/**
 * Exit function for the inactivity module
 *
 * @todo D-Bus unregistration
 *
 * @param module Unused
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_filter_from_datapipe(&device_inactive_pipe,
				    device_inactive_filter);

	/* Remove all timer sources */
	cancel_blank_prevent();

	return;
}
