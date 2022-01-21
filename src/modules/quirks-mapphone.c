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
#include "mce-conf.h"

#define CPU1_ONLINE_PATH	"/sys/devices/system/cpu/cpu1/online"
#define GSMTTY1_PATH 		"/dev/gsmtty1"

#define MODULE_NAME		"quirks-mapphone"
#define MODULE_PROVIDES		"quirks"

static const char *const provides[] = { MODULE_PROVIDES, NULL };

G_MODULE_EXPORT module_info_struct module_info = {
	.name = MODULE_NAME,
	.provides = provides,
	.priority = 100
};

static guint kick_timeout_cb_id = 0;
static display_state_t display_state;
static bool offline_cpu;
static GCancellable *cancellable = NULL;

static void modem_close_cb(GObject *source_object, GAsyncResult *res,
			   gpointer user_data)
{
	(void)user_data;

	g_output_stream_close_finish(G_OUTPUT_STREAM(source_object), res, NULL);
	g_object_unref(source_object);
}

static void modem_write_cb(GObject *source_object, GAsyncResult *res,
			   gpointer user_data)
{
	GOutputStream *os = G_OUTPUT_STREAM(source_object);
	GError *error = NULL;
	gsize size = g_output_stream_write_finish(os, res, &error);

	if (size != GPOINTER_TO_SIZE(user_data) || error) {
		bool is_canceled;

		if (error) {
			is_canceled = g_error_matches(error, G_IO_ERROR,
						      G_IO_ERROR_CANCELLED);
		} else {
			is_canceled = FALSE;
		}

		if (!error || !is_canceled) {
			mce_log(LL_WARN, "%s: can not set modem state [%s]",
				MODULE_NAME,
				error ? error->message : "No error");
		}

		if (!is_canceled)
			g_clear_object(&cancellable);
	} else {
		mce_log(LL_INFO, "%s: Modem state set", MODULE_NAME);
		g_clear_object(&cancellable);
	}

	g_clear_error(&error);
	g_output_stream_close_async(os, 0, NULL, modem_close_cb, NULL);
}

static void modem_append_cb(GObject *source_object, GAsyncResult *res,
			    gpointer user_data)
{
	GError *error = NULL;
	GFileOutputStream *os = g_file_append_to_finish(G_FILE(source_object),
							res, &error);

	if (os) {
		display_state_t state = GPOINTER_TO_INT(user_data);
		const char *const msg = state == MCE_DISPLAY_ON ?
						"U1234AT+SCRN=1\r" :
						"U1234AT+SCRN=0\r";
		const gsize len = strlen(msg);

		mce_log(LL_DEBUG, "%s: Setting modem state to SCRN=%s",
			MODULE_NAME,
			state == MCE_DISPLAY_ON ? "1" : "0");

		g_output_stream_write_async(G_OUTPUT_STREAM(os), msg, len, 0,
					    cancellable, modem_write_cb,
					    GSIZE_TO_POINTER(len));
	} else {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			mce_log(LL_WARN, "%s: can not open "GSMTTY1_PATH" [%s]",
				MODULE_NAME, error->message);
			g_clear_object(&cancellable);
		}
	}

	g_clear_error(&error);
}

static void display_state_trigger(gconstpointer data)
{
	display_state_t new_state = GPOINTER_TO_INT(data);
	display_state_t old_state = display_state;
	int fd;

	if (new_state == old_state)
		return;

	display_state = new_state;

	if ((new_state == MCE_DISPLAY_ON && old_state == MCE_DISPLAY_DIM) ||
	    new_state == MCE_DISPLAY_DIM) {
		return;
	}

	GFile *file = g_file_new_for_path(GSMTTY1_PATH);

	if (cancellable) {
		g_cancellable_cancel(cancellable);
		g_object_unref(cancellable);
	}

	cancellable = g_cancellable_new();

	g_file_append_to_async(file, G_FILE_CREATE_NONE, 0, cancellable,
			       modem_append_cb, GINT_TO_POINTER(display_state));
	g_object_unref(file);

	if (offline_cpu) {
		fd = open(CPU1_ONLINE_PATH, O_WRONLY);

		if (fd < 0) {
			mce_log(LL_WARN, "%s: can not open "
				CPU1_ONLINE_PATH, MODULE_NAME);
		} else {
			if (display_state == MCE_DISPLAY_ON) {
				const char *const msg = "1";

				mce_log(LL_DEBUG, "%s: Turning on cpu1",
					MODULE_NAME);

				if (write(fd, msg, strlen(msg)) < 0) {
					mce_log(LL_WARN,
						"%s: can not turn on cpu1",
						MODULE_NAME);
				}
			} else {
				const char *const msg = "0";

				mce_log(LL_DEBUG, "%s: Turning off cpu1",
					MODULE_NAME);

				if (write(fd, msg, strlen(msg)) < 0) {
					mce_log(LL_WARN,
						"%s: can not turn off cpu1",
						MODULE_NAME);
				}
			}

			close(fd);
		}
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

	offline_cpu = mce_conf_get_bool("QuirksMapphone", "OfflineCpu", true, NULL);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule * module);
void g_module_unload(GModule * module)
{
	(void)module;

	remove_output_trigger_from_datapipe(&display_state_pipe, display_state_trigger);

	display_state_trigger(GINT_TO_POINTER(MCE_DISPLAY_ON));

	g_source_remove(kick_timeout_cb_id);
}
