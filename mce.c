/**
 * @file mce.c
 * Mode Control Entity - main file
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
#include <fcntl.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "mce.h"
#include "mce-log.h"
#include "mce-conf.h"
#include "mce-dbus.h"
#include "mce-dsme.h"
#include "mce-gconf.h"
#include "mce-modules.h"
#include "event-input.h"
#include "event-switches.h"
#include "connectivity.h"
#include "datapipe.h"
#include "modetransition.h"
#include "powerkey.h"

#ifdef ENABLE_SYSTEMD_SUPPORT
#include <systemd/sd-daemon.h>
#endif

/** Path to the lockfile */
#define MCE_LOCKFILE			"/var/run/mce.pid"
/** Name shown by --help etc. */
#define PRG_NAME			"mce"

extern int optind;			/**< Used by getopt */
extern char *optarg;			/**< Used by getopt */

static const gchar *progname;	/**< Used to store the name of the program */

/**
 * Display usage information
 */
static void usage(void)
{
	fprintf(stdout,
		_("Usage: %s [OPTION]...\n"
		  "Mode Control Entity\n"
		  "\n"
		  "  -d, --daemonflag    run MCE as a daemon\n"
#ifdef ENABLE_SYSTEMD_SUPPORT
		  "  -n, --systemd       notify systemd when started up\n"
#endif
		  "      --force-syslog  log to syslog even when not "
		  "daemonized\n"
		  "      --force-stderr  log to stderr even when daemonized\n"
		  "  -S, --session       use the session bus instead of the "
		  "system bus for D-Bus\n"
		  "      --quiet         decrease debug message verbosity\n"
		  "      --verbose       increase debug message verbosity\n"
		  "      --debug-mode    run even if dsme fails\n"
		  "      --help          display this help and exit\n"
		  "      --version       output version information and exit\n"
		  "\n"
		  "Report bugs to <david.weinehall@nokia.com>\n"),
		progname);
}

/**
 * Display version information
 */
static void version(void)
{
	fprintf(stdout, _("%s v%s\n%s"),
		progname,
		G_STRINGIFY(PRG_VERSION),
		_("Written by David Weinehall.\n"
		  "\n"
		  "Copyright (C) 2004-2009 Nokia Corporation.  "
		  "All rights reserved.\n"));
}

/**
 * Initialise locale support
 *
 * @param name The program name to output in usage/version information
 * @return 0 on success, non-zero on failure
 */
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
		goto EXIT;
	}

EXIT:
	/* In this error-message we don't use _(), since we don't
	 * know where the locales failed, and we probably won't
	 * get a reasonable result if we try to use them.
	 */
	if (status != 0) {
		fprintf(stderr,
			"%s: `%s' failed; %s. Aborting.\n",
			name, "init_locales", g_strerror(status));
	}

	if (errno != ENOMEM)
		errno = 0;
#endif /* ENABLE_NLS */
	progname = name;

	return status;
}

/**
 * Signal handler
 *
 * @param signr Signal type
 */
static void signal_handler(const gint signr)
{
	switch (signr) {
	case SIGUSR1:
		/* We'll probably want some way to communicate with MCE */
		break;

	case SIGHUP:
		/* Possibly for re-reading configuration? */
		break;

	case SIGTERM:
	case SIGINT:
		g_main_loop_quit(mainloop);
		break;

	default:
		/* Should never happen */
		break;
	}
}

/**
 * Daemonize the program
 *
 * @return TRUE if MCE is started during boot, FALSE otherwise
 */
static gboolean daemonize(void)
{
	gint retries = 0;
	gint i = 0;
	gchar str[10];

	if (getppid() == 1)
		goto EXIT;	/* Already daemonized */

	/* Detach from process group */
	switch (fork()) {
	case -1:
		/* Failure */
		mce_log(LL_CRIT, "daemonize: fork failed: %s",
			g_strerror(errno));
		mce_log_close();
		exit(EXIT_FAILURE);

	case 0:
		/* Child */
		break;

	default:
		/* Parent -- exit */
		exit(EXIT_SUCCESS);
	}

	/* Detach TTY */
	setsid();

	/* Close all file descriptors and redirect stdio to /dev/null */
	if ((i = getdtablesize()) == -1)
		i = 256;

	while (--i >= 0) {
		if (close(i) == -1) {
			if (retries > 10) {
				mce_log(LL_CRIT,
					"close() was interrupted more than "
					"10 times. Exiting.");
				mce_log_close();
				exit(EXIT_FAILURE);
			}

			if (errno == EINTR) {
				mce_log(LL_INFO,
					"close() was interrupted; retrying.");
				errno = 0;
				i++;
				retries++;
			} else if (errno == EBADF) {
				mce_log(LL_ERR,
					"Failed to close() fd %d; %s. "
					"Ignoring.",
					i + 1, g_strerror(errno));
				errno = 0;
			} else {
				mce_log(LL_CRIT,
					"Failed to close() fd %d; %s. "
					"Exiting.",
					i + 1, g_strerror(errno));
				mce_log_close();
				exit(EXIT_FAILURE);
			}
		} else {
			retries = 0;
		}
	}

	if ((i = open("/dev/null", O_RDWR)) == -1) {
		mce_log(LL_CRIT,
			"Cannot open `/dev/null'; %s. Exiting.",
			g_strerror(errno));
		mce_log_close();
		exit(EXIT_FAILURE);
	}

	if ((dup(i) == -1)) {
		mce_log(LL_CRIT,
			"Failed to dup() `/dev/null'; %s. Exiting.",
			g_strerror(errno));
		mce_log_close();
		exit(EXIT_FAILURE);
	}

	if ((dup(i) == -1)) {
		mce_log(LL_CRIT,
			"Failed to dup() `/dev/null'; %s. Exiting.",
			g_strerror(errno));
		mce_log_close();
		exit(EXIT_FAILURE);
	}

	/* Set umask */
	umask(022);

	/* Set working directory */
	if ((chdir("/tmp") == -1)) {
		mce_log(LL_CRIT,
			"Failed to chdir() to `/tmp'; %s. Exiting.",
			g_strerror(errno));
		mce_log_close();
		exit(EXIT_FAILURE);
	}

	/* Single instance */
	if ((i = open(MCE_LOCKFILE, O_RDWR | O_CREAT, 0640)) == -1) {
		mce_log(LL_CRIT,
			"Cannot open lockfile; %s. Exiting.",
			g_strerror(errno));
		mce_log_close();
		exit(EXIT_FAILURE);
	}

	if (lockf(i, F_TLOCK, 0) == -1) {
		mce_log(LL_CRIT, "Already running. Exiting.");
		mce_log_close();
		exit(EXIT_FAILURE);
	}

	sprintf(str, "%d\n", getpid());
	write(i, str, strlen(str));
	close(i);

	/* Ignore TTY signals */
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);

	/* Ignore child terminate signal */
	signal(SIGCHLD, SIG_IGN);

EXIT:
	return 0;
}

/**
 * Main
 *
 * @param argc Number of command line arguments
 * @param argv Array with command line arguments
 * @return 0 on success, non-zero on failure
 */
int main(int argc, char **argv)
{
	int optc;
	int opt_index;

	int verbosity = LL_DEFAULT;
	int logtype = -1;

	gint status = 0;
	gboolean daemonflag = FALSE;
	gboolean systembus = TRUE;
	gboolean debugmode = FALSE;
#ifdef ENABLE_SYSTEMD_SUPPORT
	gboolean systemd_notify = FALSE;
#endif

	const char optline[] = "dS";

	struct option const options[] = {
		{ "daemonflag", no_argument, 0, 'd' },
#ifdef ENABLE_SYSTEMD_SUPPORT
		{ "systemd", no_argument, 0, 'n' },
#endif
		{ "force-syslog", no_argument, 0, 's' },
		{ "force-stderr", no_argument, 0, 'T' },
		{ "session", no_argument, 0, 'S' },
		{ "quiet", no_argument, 0, 'q' },
		{ "verbose", no_argument, 0, 'v' },
		{ "debug-mode", no_argument, 0, 'D' },
		{ "help", no_argument, 0, 'h' },
		{ "version", no_argument, 0, 'V' },
		{ 0, 0, 0, 0 }
	};

	/* NULL the mainloop */
	mainloop = NULL;

	/* Initialise support for locales, and set the program-name */
	if (init_locales(PRG_NAME) != 0)
		goto EXIT;

	/* Parse the command-line options */
	while ((optc = getopt_long(argc, argv, optline,
				   options, &opt_index)) != -1) {
		switch (optc) {
		case 'd':
			daemonflag = TRUE;
			break;

#ifdef ENABLE_SYSTEMD_SUPPORT
		case 'n':
			systemd_notify = TRUE;
			break;
#endif

		case 's':
			if (logtype != -1) {
				usage();
				status = EINVAL;
				goto EXIT;
			}

			logtype = MCE_LOG_SYSLOG;
			break;

		case 'T':
			if (logtype != -1) {
				usage();
				status = EINVAL;
				goto EXIT;
			}

			logtype = MCE_LOG_STDERR;
			break;

		case 'S':
			systembus = FALSE;
			break;

		case 'q':
			if (verbosity > LL_NONE)
				verbosity--;
			break;

		case 'v':
			if (verbosity < LL_DEBUG)
				verbosity++;
			break;

		case 'D':
			debugmode = TRUE;
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

	/* We don't take any non-flag arguments */
	if ((argc - optind) > 0) {
		fprintf(stderr,
			_("%s: Too many arguments\n"
			  "Try: `%s --help' for more information.\n"),
			progname, progname);
		status = EINVAL;
		goto EXIT;
	}

	if (logtype == -1)
		logtype = (daemonflag == TRUE) ? MCE_LOG_SYSLOG :
						 MCE_LOG_STDERR;

	mce_log_open(PRG_NAME, LOG_DAEMON, logtype);
	mce_log_set_verbosity(verbosity);

	/* Daemonize if requested */
	if (daemonflag == TRUE)
		daemonize();

	signal(SIGUSR1, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	/* Initialise GType system */
#if !GLIB_CHECK_VERSION(2,35,0)
	g_type_init ();
#endif

	/* Register a mainloop */
	mainloop = g_main_loop_new(NULL, FALSE);

	/* Initialise subsystems */

	/* Get configuration options */
	/* ignore errors; this way the defaults will be used if
	 * the configuration file is invalid or unavailable
	 */
	(void)mce_conf_init();

	/* Initialise D-Bus */
	if (mce_dbus_init(systembus) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to initialise D-Bus");
		mce_log_close();
		exit(EXIT_FAILURE);
	}

	/* Initialise GConf
	 * pre-requisite: g_type_init()
	 */
	if (mce_gconf_init() == FALSE) {
		mce_log(LL_CRIT,
			"Cannot connect to default GConf engine");
		mce_log_close();
		exit(EXIT_FAILURE);
	}

	/* Setup all datapipes */
	setup_datapipe(&system_state_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(MCE_STATE_UNDEF));
	setup_datapipe(&mode_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(MCE_INVALID_MODE_INT32));
	setup_datapipe(&call_state_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(CALL_STATE_NONE));
	setup_datapipe(&call_type_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(NORMAL_CALL));
	setup_datapipe(&alarm_ui_state_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(MCE_ALARM_UI_INVALID_INT32));
	setup_datapipe(&submode_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(MCE_NORMAL_SUBMODE));
	setup_datapipe(&display_state_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(MCE_DISPLAY_UNDEF));
	setup_datapipe(&display_brightness_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&led_brightness_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&led_pattern_activate_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, NULL);
	setup_datapipe(&led_pattern_deactivate_pipe, READ_ONLY, FREE_CACHE,
		       0, NULL);
	setup_datapipe(&led_enabled_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(TRUE));
	setup_datapipe(&vibrator_pattern_activate_pipe, READ_ONLY, FREE_CACHE,
		       0, NULL);
	setup_datapipe(&vibrator_pattern_deactivate_pipe, READ_ONLY, FREE_CACHE,
		       0, NULL);
	setup_datapipe(&key_backlight_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&keypress_pipe, READ_WRITE, FREE_CACHE,
		       sizeof (struct input_event), NULL);
	setup_datapipe(&touchscreen_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&touchscreen_suspend_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&device_inactive_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(FALSE));
	setup_datapipe(&lockkey_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&keyboard_slide_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&lid_cover_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&lens_cover_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&proximity_sensor_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&light_sensor_pipe, READ_WRITE, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(-1));
	setup_datapipe(&device_lock_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(LOCK_UNDEF));
	setup_datapipe(&device_lock_inhibit_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(FALSE));
	setup_datapipe(&tk_lock_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(LOCK_UNDEF));
	setup_datapipe(&charger_state_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&battery_status_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(BATTERY_STATUS_UNDEF));
	setup_datapipe(&camera_button_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(CAMERA_BUTTON_UNDEF));
	setup_datapipe(&inactivity_timeout_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(DEFAULT_INACTIVITY_TIMEOUT));
	setup_datapipe(&audio_route_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(AUDIO_ROUTE_UNDEF));
	setup_datapipe(&usb_cable_pipe, READ_ONLY, DONT_FREE_CACHE,
		       0, GINT_TO_POINTER(0));
	setup_datapipe(&tvout_pipe, READ_ONLY, DONT_FREE_CACHE,
               0, GINT_TO_POINTER(FALSE));

	/* Initialise connectivity monitoring
	 * pre-requisite: g_type_init()
	 */
	if (mce_connectivity_init() == FALSE) {
		status = EXIT_FAILURE;
		goto EXIT;
	}

	/* Initialise mode management
	 * pre-requisite: mce_gconf_init()
	 * pre-requisite: mce_dbus_init()
	 */
	if (mce_mode_init() == FALSE) {
		status = EXIT_FAILURE;
		goto EXIT;
	}

	/* Initialise DSME
	 * pre-requisite: mce_gconf_init()
	 * pre-requisite: mce_dbus_init()
	 * pre-requisite: mce_mce_init()
	 */
	if (mce_dsme_init(debugmode) == FALSE) {
		if (debugmode == FALSE) {
			mce_log(LL_CRIT, "Cannot connect to DSME");
			status = EXIT_FAILURE;
			goto EXIT;
		}
	}

	/* Initialise powerkey driver */
	if (mce_powerkey_init() == FALSE) {
		status = EXIT_FAILURE;
		goto EXIT;
	}

	if (mce_input_init() == FALSE) {
		status = EXIT_FAILURE;
		goto EXIT;
	}

	/* Initialise switch driver */
	if (mce_switches_init() == FALSE) {
		status = EXIT_FAILURE;
		goto EXIT;
	}

	/* Load all modules */
	if (mce_modules_init() == FALSE) {
		status = EXIT_FAILURE;
		goto EXIT;
	}

	mce_startup_ui();

#ifdef ENABLE_SYSTEMD_SUPPORT
	/* Tell systemd that we have started up */
	if (systemd_notify) {
		mce_log(LL_INFO, "notifying systemd");
		sd_notify(0, "READY=1");
	}
#endif

	/* Run the main loop */
	g_main_loop_run(mainloop);

	/* If we get here, the main loop has terminated;
	 * either because we requested or because of an error
	 */
EXIT:
	/* Unload all modules */
	mce_modules_exit();

	/* Call the exit function for all components */
	mce_switches_exit();
	mce_input_exit();
	mce_powerkey_exit();
	mce_dsme_exit();
	mce_mode_exit();
	mce_connectivity_exit();

	/* Free all datapipes */
	free_datapipe(&tvout_pipe);
	free_datapipe(&usb_cable_pipe);
	free_datapipe(&audio_route_pipe);
	free_datapipe(&inactivity_timeout_pipe);
	free_datapipe(&battery_status_pipe);
	free_datapipe(&charger_state_pipe);
	free_datapipe(&tk_lock_pipe);
	free_datapipe(&device_lock_inhibit_pipe);
	free_datapipe(&device_lock_pipe);
	free_datapipe(&proximity_sensor_pipe);
	free_datapipe(&lens_cover_pipe);
	free_datapipe(&lid_cover_pipe);
	free_datapipe(&keyboard_slide_pipe);
	free_datapipe(&lockkey_pipe);
	free_datapipe(&device_inactive_pipe);
	free_datapipe(&touchscreen_suspend_pipe);
	free_datapipe(&touchscreen_pipe);
	free_datapipe(&keypress_pipe);
	free_datapipe(&key_backlight_pipe);
	free_datapipe(&vibrator_pattern_deactivate_pipe);
	free_datapipe(&vibrator_pattern_activate_pipe);
	free_datapipe(&led_pattern_deactivate_pipe);
	free_datapipe(&led_pattern_activate_pipe);
	free_datapipe(&led_brightness_pipe);
	free_datapipe(&display_brightness_pipe);
	free_datapipe(&display_state_pipe);
	free_datapipe(&submode_pipe);
	free_datapipe(&alarm_ui_state_pipe);
	free_datapipe(&call_type_pipe);
	free_datapipe(&call_state_pipe);
	free_datapipe(&mode_pipe);
	free_datapipe(&system_state_pipe);

	/* Call the exit function for all subsystems */
	mce_gconf_exit();
	mce_dbus_exit();
	mce_conf_exit();

	/* If the mainloop is initialised, unreference it */
	if (mainloop != NULL)
		g_main_loop_unref(mainloop);

	/* Log a farewell message and close the log */
	mce_log(LL_INFO, "Exiting...");
	mce_log_close();

	return status;
}
