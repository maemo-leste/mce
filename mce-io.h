/**
 * @file mce-io.h
 * Headers for the generic I/O functionality for the Mode Control Entity
 * <p>
 * Copyright © 2007, 2009-2010 Nokia Corporation and/or its subsidiary(-ies).
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
#ifndef _MCE_IO_H_
#define _MCE_IO_H_

#include <glib.h>

/** Error policies for mce-io */
typedef enum {
	/** Exit on error */
	MCE_IO_ERROR_POLICY_EXIT,
	/** Warn about errors */
	MCE_IO_ERROR_POLICY_WARN,
	/** Silently ignore errors */
	MCE_IO_ERROR_POLICY_IGNORE
} error_policy_t;

/** Function pointer for I/O monitor callback */
typedef void (*iomon_cb)(gpointer data, gsize bytes_read);
typedef void (*iomon_error_cb)(gpointer data, const gchar* device, gconstpointer iomon_id, GError* err);

gboolean mce_read_string_from_file(const gchar *const file, gchar **string);
gboolean mce_read_number_string_from_file(const gchar *const file,
					  gulong *number);
gboolean mce_write_string_to_file(const gchar *const file,
				  const gchar *const string);
gboolean mce_write_number_string_to_file(const gchar *const file,
					 const gulong number);
void mce_suspend_io_monitor(gconstpointer io_monitor);
void mce_resume_io_monitor(gconstpointer io_monitor);
gconstpointer mce_register_io_monitor_string(const gint fd,
					     const gchar *const file,
					     error_policy_t error_policy,
					     gboolean rewind_policy,
					     iomon_cb callback,
					     iomon_error_cb remdev_callback,
					     gpointer remdev_data);
gconstpointer mce_register_io_monitor_chunk(const gint fd,
					    const gchar *const file,
					    error_policy_t error_policy,
					    gboolean rewind_policy,
					    iomon_cb callback,
					    gulong chunk_size,
					    iomon_error_cb remdev_callback,
					    gpointer remdev_data);
void mce_unregister_io_monitor(gconstpointer io_monitor);
const gchar *mce_get_io_monitor_name(gconstpointer io_monitor);
int mce_get_io_monitor_fd(gconstpointer io_monitor);

#endif /* _MCE_IO_H_ */
