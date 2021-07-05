#include "event-input-utils.h"

#include <glib.h>
#include <gio/gio.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/input.h>
#include "mce-log.h"

int mce_match_event_file_by_caps(const gchar *const filename,
				    const int *const ev_types,
				    const int *const ev_keys[])
{
	int ev_type, p, q;
	int version;
	unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
	int fd;

	if ((fd = open(filename, O_NONBLOCK | O_RDONLY)) == -1) {
		mce_log(LL_DEBUG, "Failed to open `%s', skipping", filename);

		/* Ignore error */
		errno = 0;
		return -1;
	}

	/* We use this ioctl to check if this device supports the input
	 * ioctl's
	*/
	if (ioctl(fd, EVIOCGVERSION, &version) < 0) {
		mce_log(LL_WARN,
			"match_event_file_by_caps: can't get version on `%s'",
			filename);
		goto EXIT;
	}

	memset(bit, 0, sizeof(bit));
	if (ioctl(fd, EVIOCGBIT(0, EV_MAX), bit[0]) < 0) {
		mce_log(LL_WARN,
			"match_event_file_by_caps: ioctl(EVIOCGBIT, EV_MAX) failed on `%s'",
			filename);
		goto EXIT;
	}

	for (p = 0; ev_types[p] != -1; p ++) {
		/* TODO: Could check that ev_types[p] is less than EV_MAX */
		ev_type = ev_types[p];

		/* event type not supported, try the next one */
		if (!test_bit(ev_type, bit[0]))
			continue;

		/* Get bits per event type */
		if (ioctl(fd, EVIOCGBIT(ev_type, KEY_MAX), bit[ev_type]) < 0) {
			mce_log(LL_WARN,
				"match_event_file_by_caps: ioctl(EVIOCGBIT, KEY_MAX) failed on `%s'",
				filename);
			goto EXIT;
		}

		for (q = 0; ev_keys[p][q] != -1; q ++) {
			/* TODO: Could check that q is less than KEY_MAX */

			/* succeed if at least one match is found */
			if (test_bit(ev_keys[p][q], bit[ev_type])) {
				mce_log(LL_DEBUG,
					"match_event_file_by_caps: match found on `%s'", filename);
				return fd;
			}
		}
	}

EXIT:
	close(fd);

	return -1;
}

/**
 * Try to match /dev/input event file to a specific driver
 *
 * @param filename A string containing the name of the event file
 * @param drivers An array of driver names
 * @return An open file descriptor on success, -1 on failure
 */
int mce_match_event_file(const gchar *const filename,
			    const gchar *const *const drivers)
{
	static char name[256];
	int fd = -1;
	int i;

	/* If we cannot open the file, abort */
	if ((fd = open(filename, O_NONBLOCK | O_RDONLY)) == -1) {
		mce_log(LL_DEBUG, "Failed to open `%s', skipping",
			filename);

		/* Ignore error */
		errno = 0;
		goto EXIT;
	}

	for (i = 0; drivers[i] != NULL; i++) {
		if (ioctl(fd, EVIOCGNAME(sizeof name), name) >= 0) {
			if (strcmp(name, drivers[i]) == 0) {
				/* We found our event file */
				mce_log(LL_DEBUG,
					"`%s' is `%s'",
					filename, drivers[i]);
				break;
			}
		} else {
			mce_log(LL_WARN,
				"ioctl(EVIOCGNAME) failed on `%s'",
				filename);
		}
	}

	/* If the scan terminated with drivers[i] == NULL,
	 * we didn't find any match
	 */
	if (drivers[i] == NULL) {
		close(fd);
		fd = -1;
	}

EXIT:
	return fd;
}

/**
 * Scan /dev/input for input event devices
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_scan_inputdevices(mce_input_match_callback match_callback)
{
	DIR *dir = NULL;
	struct dirent *direntry = NULL;
	gboolean status = FALSE;

	if ((dir = opendir(DEV_INPUT_PATH)) == NULL) {
		mce_log(LL_ERR, "opendir() failed; %s", g_strerror(errno));
		errno = 0;
		goto EXIT;
	}

	for (direntry = readdir(dir);
	     (direntry != NULL && telldir(dir));
	     direntry = readdir(dir)) {
		gchar *filename = NULL;

		if (strncmp(direntry->d_name, EVENT_FILE_PREFIX,
			    strlen(EVENT_FILE_PREFIX)) != 0) {
			mce_log(LL_DEBUG,
				"`%s/%s' skipped",
				DEV_INPUT_PATH,
				direntry->d_name);
			continue;
		}

		filename = g_strconcat(DEV_INPUT_PATH, "/",
				       direntry->d_name, NULL);
		match_callback(filename);
		g_free(filename);
	}

	/* Report, but ignore, errors when closing directory */
	if (closedir(dir) == -1) {
		mce_log(LL_ERR,
			"closedir() failed; %s", g_strerror(errno));
		errno = 0;
	}

	status = TRUE;

EXIT:
	return status;
}

