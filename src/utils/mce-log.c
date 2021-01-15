/**
 * @file mce-log.c
 * Logging functions for Mode Control Entity
 * <p>
 * Copyright © 2006-2007, 2010 Nokia Corporation and/or its subsidiary(-ies).
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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif /* _BSD_SOURCE */
#include <syslog.h>
#include "mce-log.h"

static unsigned int logverbosity = LL_WARN;	/**< Log verbosity */
static int logtype = MCE_LOG_SYSLOG;		/**< Output for log messages */
static char *logname = NULL;

/**
 *
 * @param loglevel The level of severity for this message
 * @param fmt The format string for this message
 * @param ... Input to the format string
 */
void mce_log(const loglevel_t loglevel, const char *const fmt, ...)
{
	va_list args;

	va_start(args, fmt);

	if (logverbosity >= loglevel) {
		if (logtype == MCE_LOG_STDERR) {
			fprintf(stderr, "%s: ", logname);
			vfprintf(stderr, fmt, args);
			fprintf(stderr, "\n");
		} else {
			switch (loglevel) {
				case LL_DEBUG:
					vsyslog(LOG_DEBUG, fmt, args);
					break;

				case LL_ERR:
					vsyslog(LOG_ERR, fmt, args);
					break;

				case LL_CRIT:
					vsyslog(LOG_CRIT, fmt, args);
					break;

				case LL_INFO:
					vsyslog(LOG_INFO, fmt, args);
					break;

				case LL_WARN:
				default:
					vsyslog(LOG_WARNING, fmt, args);
													break;
			}
		}
	}

	va_end(args);
}

/**
 * Set log verbosity
 * messages with loglevel higher than or equal to verbosity will be logged
 *
 * @param verbosity minimum level for log level
 */
void mce_log_set_verbosity(const int verbosity)
{
	logverbosity = verbosity;
}

/**
 * Open log
 *
 * @param name identifier to use for log messages
 * @param facility the log facility; normally LOG_USER or LOG_DAEMON
 * @param type log type to use; MCE_LOG_STDERR or MCE_LOG_SYSLOG
 */
void mce_log_open(const char *const name, const int facility, const int type)
{
	logtype = type;

	if (logtype == MCE_LOG_SYSLOG)
		openlog(name, LOG_PID | LOG_NDELAY, facility);
	else
		logname = strdup(name);
}

/**
 * Close log
 */
void mce_log_close(void)
{
	if (logname)
		free(logname);

	if (logtype == MCE_LOG_SYSLOG)
		closelog();
}
