/* This module is intended to handle quirks in the hardware
 * and firmware of motorola mapphone devices by implementing
 * workarounds.
 */
#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <glib/gstdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include "mce.h"
#include "mce-log.h"
#include "datapipe.h"

#define MODULE_NAME		"quirks-mapphone"

#define MODULE_PROVIDES	"quirks"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

static guint kick_timeout_cb_id = 0;
static display_state_t display_state;

static void display_state_trigger(gconstpointer data)
{
	(void)data;
	display_state_t display_state_tmp = datapipe_get_gint(display_state_pipe);
	
	if(display_state_tmp == display_state)
		return;
	display_state = display_state_tmp;

	int fd = open("/dev/gsmtty1", O_WRONLY);	
	if (fd < 0) {
		mce_log(LL_WARN, "%s: can not open gsmtty1", MODULE_NAME);
		return;
	}
	
	if (display_state == MCE_DISPLAY_ON) {
		const char * const msg = "U1234AT+SCRN=1\r";
		mce_log(LL_DEBUG, "%s: Setting modem state to SCRN=1", MODULE_NAME);
		if(write(fd, msg, strlen(msg)) < 0)
			mce_log(LL_WARN, "%s: can not set modem to screen on state", MODULE_NAME);
	} else {
		const char * const msg = "U1234AT+SCRN=0\r";
		mce_log(LL_DEBUG, "%s: Setting modem state to SCRN=0", MODULE_NAME);
		if(write(fd, msg, strlen(msg)) < 0)
			mce_log(LL_WARN, "%s: can not set modem to screen off state", MODULE_NAME);
	}
}

static gboolean inactivity_timeout_cb(gpointer data)
{
	(void)data;

	mce_log(LL_DEBUG, "%s: Kicking modem to avoid pm bug", MODULE_NAME);

	int fdUSB3 = open("/dev/ttyUSB3", O_RDWR);
	int fdUSB4 = open("/dev/ttyUSB4", O_RDWR);
	
	if (fdUSB3 >= 0)
		close(fdUSB3);
	else
		mce_log(LL_WARN, "%s: unable to kick ttyUSB3", MODULE_NAME);
	if (fdUSB4 >= 0)
		close(fdUSB4);
	else
		mce_log(LL_WARN, "%s: unable to kick ttyUSB4", MODULE_NAME);

	return TRUE;
}

G_MODULE_EXPORT const char *g_module_check_init(GModule * module);
const char *g_module_check_init(GModule * module)
{
	(void)module;

	mce_log(LL_DEBUG, "Initalizing %s", MODULE_NAME);

	display_state = datapipe_get_gint(display_state_pipe);
	append_output_trigger_to_datapipe(&display_state_pipe, display_state_trigger);

	kick_timeout_cb_id = g_timeout_add_seconds(600, inactivity_timeout_cb, NULL);
	inactivity_timeout_cb(NULL);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	remove_output_trigger_from_datapipe(&display_state_pipe, display_state_trigger);

	g_source_remove(kick_timeout_cb_id);
}
