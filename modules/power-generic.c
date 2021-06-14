#include <stdlib.h>
#include <gmodule.h>

#include "mce.h"
#include "mce-log.h"
#include "datapipe.h"


#define MODULE_NAME		"power-generic"

#define MODULE_PROVIDES	"power"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

static void system_power_request_trigger(gconstpointer data)
{
	power_req_t request = (power_req_t)data;
	
	call_state_t call_state = datapipe_get_gint(call_state_pipe);
	call_type_t  call_type  = datapipe_get_gint(call_type_pipe);
	
	if ((call_state == CALL_STATE_NONE || call_type != EMERGENCY_CALL)) {
		mce_log(LL_WARN, "%s: Not changing power state during energency call", MODULE_NAME);
		return;
	}
	
	switch (request) {
		case MCE_POWER_REQ_OFF:
		case MCE_POWER_REQ_SOFT_OFF:
			execute_datapipe(&system_state_pipe, GINT_TO_POINTER(MCE_STATE_SHUTDOWN), USE_INDATA, CACHE_INDATA);
			system("poweroff");
			break;
		case MCE_POWER_REQ_REBOOT:
			execute_datapipe(&system_state_pipe, GINT_TO_POINTER(MCE_STATE_SHUTDOWN), USE_INDATA, CACHE_INDATA);
			system("reboot");
			break;
		case MCE_POWER_REQ_UNDEF:
		default:
			break;
	}
}

/**
 * Init function for the power-dsme component
 *
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&system_power_request_pipe,
					  system_power_request_trigger);
	
	execute_datapipe(&system_state_pipe, GINT_TO_POINTER(MCE_STATE_USER), USE_INDATA, CACHE_INDATA);

	return NULL;
}

/**
 * Exit function for the mce-dsme component
 *
 * @todo D-Bus unregistration
 * @todo trigger unregistration
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&system_power_request_pipe,
					    system_power_request_trigger);
}
