/**
 * @file devlock-blocker.c
 * devlock-blocker source code
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

#include <errno.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#include "mce.h"
#include "mce-log.h"

#define PRG_NAME			"devlock-blocker"

extern int optind;
extern char *optarg;

static const gchar *progname;

static GMainLoop *loop = NULL;

static DBusError dbus_error;
static gboolean error_initialised = FALSE;

static void usage(void)
{
	fprintf(stdout,
		_("Usage: %s [OPTION]...\n"
		  "Device lock blocker for MCE\n"
		  "\n"
		  "  -S, --session       use the session bus instead of the "
		  "system bus for D-Bus\n"
		  "      --verbose       increase debug message verbosity\n"
		  "      --quiet         decrease debug message verbosity\n"
		  "      --help          display this help and exit\n"
		  "      --version       output version information and exit\n"
		  "\n"
		  "Report bugs to <david.weinehall@nokia.com>\n"),
		progname);
}

static void version(void)
{
	fprintf(stdout, _("%s v%s\n%s"),
		progname,
		G_STRINGIFY(PRG_VERSION),
		_("Written by David Weinehall.\n"
		  "\n"
		  "Copyright (C) 2005-2008 Nokia Corporation.  "
		  "All rights reserved.\n"));
}

static gint init_locales(const gchar *const name)
{
	gint status = 0;

#ifdef ENABLE_NLS
	setlocale(LC_ALL, "");

	if ((bindtextdomain(name, LOCALEDIR) == 0) && (errno == ENOMEM)) {
		status = errno;
		goto EXIT;
	}

	if ((textdomain(name) == 0) && (errno == ENOMEM)) {
		status = errno;
		return 0;
	}

EXIT:
	if (status != 0) {
		fprintf(stderr,
			"%s: `%s' failed; %s. Aborting.\n",
			name, "init_locales", g_strerror(errno));
	} else {
		progname = name;
		errno = 0;
	}
#else
	progname = name;
#endif

	return status;
}

static DBusHandlerResult msg_handler(DBusConnection *const connection,
				     DBusMessage *const msg,
				     gpointer const user_data)
{
	(void)connection;
	(void)user_data;


	if (dbus_message_is_signal(msg, MCE_SIGNAL_IF,
				   MCE_DEVLOCK_MODE_SIG) == TRUE) {
		gchar *mode = NULL;

		mce_log(LL_DEBUG, "Received MCE devlock mode");

		if (dbus_message_get_args(msg, &dbus_error,
					  DBUS_TYPE_STRING, &mode,
					  DBUS_TYPE_INVALID) == FALSE) {
			mce_log(LL_CRIT,
				"Failed to get argument from %s.%s: %s",
				MCE_SIGNAL_IF, MCE_DEVLOCK_MODE_SIG,
				dbus_error.message);
			dbus_error_free(&dbus_error);
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}

		mce_log(LL_DEBUG, "New devlock mode: %s", mode);

		if (strcmp(mode, MCE_DEVICE_UNLOCKED) == 0)
			g_main_loop_quit(loop);
	} else {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

static gboolean dbus_init_message_handlers(DBusConnection *const connection)
{
	gboolean status = FALSE;

	dbus_bus_add_match(connection,
			   "type='signal',"
			   "interface='" MCE_SIGNAL_IF "'",
			   &dbus_error);

	if (dbus_error_is_set(&dbus_error) == TRUE) {
		mce_log(LL_CRIT, "Failed to add D-Bus match for "
			"'" MCE_SIGNAL_IF "'; %s",
			dbus_error.message);
		goto EXIT;
	}

	if (dbus_connection_add_filter(connection, msg_handler,
				       NULL, NULL) == FALSE) {
		mce_log(LL_CRIT, "Failed to add D-Bus filter");
		goto EXIT;
	}

	status = TRUE;

EXIT:
	return status;
}

int main(int argc, char **argv)
{
	int optc;
	int opt_index;

	int verbosity = LL_DEFAULT;
	int consolelog = MCE_LOG_SYSLOG;

	gint status = 0;

	DBusBusType bus_type = DBUS_BUS_SYSTEM;
	DBusConnection *dbus_connection = NULL;

	struct option const options[] = {
		{ "session", no_argument, 0, 'S' },
		{ "quiet", no_argument, 0, 'q' },
		{ "verbose", no_argument, 0, 'v' },
		{ "help", no_argument, 0, 'h' },
		{ "version", no_argument, 0, 'V' },
		{ 0, 0, 0, 0 }
	};

	if (init_locales(PRG_NAME) != 0)
		goto EXIT;

	while ((optc = getopt_long(argc, argv, "S",
				   options, &opt_index)) != -1) {
		switch (optc) {
		case 'S':
			bus_type = DBUS_BUS_SESSION;
			break;

		case 'q':
			if (verbosity > LL_CRIT)
				verbosity--;
			break;

		case 'v':
			if (verbosity < LL_DEBUG)
				verbosity++;
			break;

		case 'h':
			usage();
			goto EXIT;

		case 'V':
			version();
			goto EXIT;

		default:
			usage();
			status = EINVAL;
			goto EXIT;
		}
	}

	if ((argc - optind) > 0) {
		fprintf(stderr,
			_("%s: Too many arguments\n"
			  "Try: `%s --help' for more information.\n"),
			progname, progname);
		status = EINVAL;
		goto EXIT;
	}

	mce_log_open(PRG_NAME, LOG_DAEMON,
		     (consolelog == TRUE) ? MCE_LOG_SYSLOG : MCE_LOG_STDERR);
	mce_log_set_verbosity(verbosity);

	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);

	dbus_error_init(&dbus_error);
	error_initialised = TRUE;

	if ((dbus_connection = dbus_bus_get(bus_type,
					    &dbus_error)) == NULL) {
		mce_log(LL_CRIT, "Failed to open connection to message bus");
		status = EXIT_FAILURE;
		goto EXIT;
	} else {
		gchar *mode;

		DBusMessage *msg;
		DBusMessage *reply;

		mce_log(LL_DEBUG, "Querying MCE devlock mode");

		if ((msg = dbus_message_new_method_call(MCE_SERVICE,
							MCE_REQUEST_PATH,
							MCE_REQUEST_IF,
							MCE_DEVLOCK_MODE_GET)) == NULL) {
			mce_log(LL_CRIT, "Cannot allocate memory for "
				"D-Bus method call!");
			status = EXIT_FAILURE;
			goto EXIT;
		}

		mce_log(LL_DEBUG, "Got MCE devlock mode reply");
		reply = dbus_connection_send_with_reply_and_block(dbus_connection, msg, -1, &dbus_error);
		dbus_message_unref(msg);

		if ((dbus_error_is_set(&dbus_error) == TRUE) ||
		    (reply == NULL)) {
			mce_log(LL_CRIT, "Cannot call method %s; %s; exiting",
				MCE_DEVLOCK_MODE_GET,
				dbus_error.message);
			status = EXIT_FAILURE;
			goto EXIT;
		}

		if (dbus_message_get_args(reply, &dbus_error,
					  DBUS_TYPE_STRING, &mode,
					  DBUS_TYPE_INVALID) == FALSE) {
			mce_log(LL_CRIT,
				"Failed to get reply argument from %s.%s: %s",
				MCE_REQUEST_IF, MCE_DEVLOCK_MODE_GET,
				dbus_error.message);
			dbus_message_unref(reply);
			dbus_error_free(&dbus_error);
			status = EXIT_FAILURE;
			goto EXIT;
		}

		mce_log(LL_DEBUG, "New devlock mode: %s", mode);
		dbus_message_unref(reply);

		if (strcmp(mode, MCE_DEVICE_UNLOCKED) == 0)
			goto EXIT;
	}

	loop = g_main_loop_new(NULL, FALSE);

	dbus_connection_setup_with_g_main(dbus_connection, NULL);

	if (dbus_init_message_handlers(dbus_connection) == FALSE) {
		status = EXIT_FAILURE;
		goto EXIT;
	}

	g_main_loop_run(loop);

EXIT:
	if (loop != NULL)
		g_main_loop_unref(loop);

	if (dbus_connection != NULL) {
		mce_log(LL_DEBUG, "Unreferencing D-Bus connection");
		dbus_connection_unref(dbus_connection);
		dbus_connection = NULL;
	}

	if ((error_initialised == TRUE) &&
	    (dbus_error_is_set(&dbus_error) == TRUE)) {
		mce_log(LL_DEBUG, "Unregistering D-Bus error channel");
		dbus_error_free(&dbus_error);
	}

	mce_log(LL_INFO, "Exiting...");
	mce_log_close();

	return status;
}
