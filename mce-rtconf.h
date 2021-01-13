#ifndef _MCE_RTCONF_H_
#define _MCE_RTCONF_H_

#include <glib.h>

typedef void (*mce_rtconf_callback)(gchar * key, guint cb_id, void *user_data);

gboolean mce_rtconf_set_int(const gchar * const key, const gint value);
gboolean mce_rtconf_get_bool(const gchar * const key, gboolean * value);
gboolean mce_rtconf_get_int(const gchar * const key, gint * value);
gboolean mce_rtconf_notifier_add(const gchar * path, const gchar * key,
				 const mce_rtconf_callback callback, void *user_data, guint * cb_id);
void mce_rtconf_notifier_remove(guint cb_id);

gboolean mce_rtconf_backend_register(gboolean(*set_int_backend) (const gchar * const key, const gint value),
				     gboolean(*get_int_backend) (const gchar * const key, gint * value),
				     gboolean(*get_bool_backend) (const gchar * const key, gboolean * value),
				     gboolean(*notifier_add_backend) (const gchar * path, const gchar * key,
								      const mce_rtconf_callback callback,
								      void *user_data, guint * cb_id),
				     void (*notifier_remove_backend)(guint cb_id));

#endif				/* _MCE_RTCONF_H_ */
