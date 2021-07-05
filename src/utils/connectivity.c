/**
 * @file connectivity.c
 * Connectivity logic for the Mode Control Entity
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
#include <glib-object.h>
#include <conic.h>
#include "connectivity.h"

/** Is there an open connection or not? */
static gboolean connected = FALSE;

/**
 * Connection info handler
 *
 * @param connection Unused
 * @param event The connection event
 * @param user_data Unused
 */
static void connection_event_cb(ConIcConnection *connection,
				ConIcConnectionEvent *event,
				gpointer user_data)
{
	ConIcConnectionStatus status;

	(void)connection;
	(void)user_data;

	status = con_ic_connection_event_get_status(event);

	connected = (status == CON_IC_STATUS_CONNECTED) ? TRUE : FALSE;
}

/**
 * Check connectivity status
 *
 * @return TRUE if there's an open connection,
 *         FALSE if there's no open connection
 */
gboolean get_connectivity_status(void) G_GNUC_PURE;
gboolean get_connectivity_status(void)
{
	return connected;
}

/**
 * Init function for the connectivity component
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_connectivity_init(void)
{
	/* Create connection object */
	ConIcConnection *connection_object = con_ic_connection_new();

	/* Connect signal to receive connection events */
	g_signal_connect(G_OBJECT(connection_object), "connection-event",
			 G_CALLBACK(connection_event_cb), NULL);

	/* Set automatic events */
	g_object_set(G_OBJECT(connection_object), "automatic-connection-events",
			      TRUE, NULL);

	return TRUE;
}

/**
 * Exit function for the connectivity component
 *
 * @todo Unregister the connection events, etc.
 */
void mce_connectivity_exit(void)
{
	return;
}
