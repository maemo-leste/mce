/**
 * Copyright (C) 2013 Jonathan Wilson <jfwfreo@tpgi.com.au>
 *
 * These headers are free software; you can redistribute them
 * and/or modify them under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * These headers are distributed in the hope that they will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#ifndef DEVLOCK_H
#define DEVLOCK_H

typedef void (*autolock_notify)(gboolean enabled);
typedef void (*timeout_notify)(gint timeout);
void devlock_notify_remove(guint key);
gboolean devlock_timeout_notify_add(timeout_notify notify_func, guint *key, gchar *string);
gboolean devlock_autorelock_notify_add(autolock_notify notify_func, guint *key, gchar *string);
void set_passwd_total_failed_count(gint count);
void set_passwd_failed_count(gint count);
void get_passwd_total_failed_count(gint *count);
void get_passwd_failed_count(gint *count);
void set_timeout_key(gint timeout);
void get_timeout_key(gint *timeout);
void set_autolock_key(gboolean enabled);
void get_autolock_key(gboolean *enabled);

#endif
