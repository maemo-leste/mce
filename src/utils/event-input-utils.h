#ifndef _EVENT_INPUT_UTILS_H_
#define _EVENT_INPUT_UTILS_H_

#include <glib.h>

/** Path to the input device directory */
#define DEV_INPUT_PATH			"/dev/input"
/** Prefix for event files */
#define EVENT_FILE_PREFIX		"event"

#define BITS_PER_LONG			(sizeof(long) * 8)
#define NBITS(x)			((((x) - 1) / BITS_PER_LONG) + 1)
#define OFF(x)				((x) % BITS_PER_LONG)
#define BIT(x)				(1UL << OFF(x))
#define LONG(x)				((x)/BITS_PER_LONG)
#define test_bit(bit, array)		((array[LONG(bit)] >> OFF(bit)) & 1)

typedef void (*mce_input_match_callback) (const char* filename, gpointer user_data);

int mce_match_event_file_by_caps(const gchar *const filename, const int *const ev_types, const int *const ev_keys[]);

int mce_match_event_file(const gchar *const filename, const gchar *const *const drivers);

gboolean mce_scan_inputdevices(mce_input_match_callback match_callback, gpointer user_data);

#endif /* _EVENT_INPUT_UTILS_H_ */
