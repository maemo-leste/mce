/* This module handles shutting down the device when the battery is low*/
#include <glib.h>
#include <gmodule.h>
#include <string.h>
#include "mce.h"
#include "homekey.h"
#include "mce-conf.h"
#include "mce-log.h"
#include "datapipe.h"

#define MODULE_NAME		"battery-guard"

static const gchar *const provides[] = { MODULE_NAME, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 250
};

static void battery_status_trigger(gconstpointer data)
{
	battery_status_t status = (battery_status_t)GPOINTER_TO_INT(data);
	call_state_t call_state = datapipe_get_gint(call_state_pipe);
	call_type_t  call_type  = datapipe_get_gint(call_type_pipe);
	
	if (status == BATTERY_STATUS_EMPTY && 
		(call_state == CALL_STATE_NONE || call_type != EMERGENCY_CALL)) {
		mce_log(LL_INFO, "%s: requesting power off due to low battery", MODULE_NAME);
		execute_datapipe(&system_power_request_pipe, GINT_TO_POINTER(MCE_POWER_REQ_OFF), USE_INDATA, CACHE_INDATA);
	}
	else if (status == BATTERY_STATUS_EMPTY) {
		mce_log(LL_WARN, "%s: Battery empty but no shutdown executed because of emergency call is in progress", MODULE_NAME);
		mce_log(LL_WARN, "%s: Battery damage possible", MODULE_NAME);
	}
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;

	append_input_trigger_to_datapipe(&battery_status_pipe, battery_status_trigger);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	remove_input_trigger_from_datapipe(&battery_status_pipe, battery_status_trigger);
}
