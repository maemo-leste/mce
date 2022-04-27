/**
 * @file mce-conf.c
 * Configuration option handling for MCE
 * <p>
 * Copyright © 2006-2009 Nokia Corporation and/or its subsidiary(-ies).
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mce.h"
#include "mce-conf.h"
#include "mce-log.h"

struct mce_conf_file
{
	gpointer keyfile;
	gchar *path;
	gchar *filename;
};

/** Pointer to the keyfile structure where config values are read from */
static struct mce_conf_file *conf_files = NULL;
static size_t mce_conf_file_count = 0;

static struct mce_conf_file *mce_conf_find_key_in_files(const gchar *group, const gchar *key) 
{
	GError *error = NULL;
	
	if (conf_files) {
		for (size_t i = mce_conf_file_count; i > 0; --i) {
			if (g_key_file_has_key(conf_files[i-1].keyfile, group, key, &error) && 
				error == NULL) {
				g_clear_error(&error);
				return &(conf_files[i-1]);
			}
			else {
				g_clear_error(&error);
				error = NULL;
			}
		}
 	}
	return NULL;
}

static gpointer mce_conf_decide_keyfile_to_use(const gchar *group, const gchar *key, gpointer keyfile)
{
	if (keyfile == NULL) {
		struct mce_conf_file *conf_file;
		conf_file = mce_conf_find_key_in_files(group, key);
		if (conf_file == NULL)
			mce_log(LL_WARN, "mce-conf: Could not get config key %s/%s", group, key);
		else
			keyfile = conf_file->keyfile;
	}

	return keyfile;
}

/**
 * Get a boolean configuration value
 *
 * @param group The configuration group to get the value from
 * @param key The configuration key to get the value of
 * @param defaultval The default value to use if the key isn't set
 * @param keyfileptr A keyfile pointer, or NULL to use the default keyfile
 * @return The configuration value on success, the default value on failure
 */
gboolean mce_conf_get_bool(const gchar *group, const gchar *key,
			   const gboolean defaultval, gpointer keyfileptr)
{
	gboolean tmp = FALSE;
	GError *error = NULL;

	keyfileptr = mce_conf_decide_keyfile_to_use(group, key, keyfileptr);
	if (keyfileptr == NULL) {
		tmp = defaultval;
		goto EXIT;
	}

	tmp = g_key_file_get_boolean(keyfileptr, group, key, &error);

	if (error != NULL) {
		mce_log(LL_WARN, "mce-conf: "
			"Could not get config key %s/%s; %s; "
			"defaulting to `%d'",
			group, key, error->message, defaultval);
		tmp = defaultval;
	}

	g_clear_error(&error);

EXIT:
	return tmp;
}

gboolean mce_conf_set_bool(const gchar *group, const gchar *key,
		      const gboolean val, gpointer keyfileptr)
{
	keyfileptr = mce_conf_decide_keyfile_to_use(group, key, keyfileptr);
	if (keyfileptr == NULL)
		return FALSE;

	g_key_file_set_boolean(keyfileptr, group, key, val);

	return TRUE;
}

/**
 * Get an integer configuration value
 *
 * @param group The configuration group to get the value from
 * @param key The configuration key to get the value of
 * @param defaultval The default value to use if the key isn't set
 * @param keyfileptr A keyfile pointer, or NULL to use the default keyfile
 * @return The configuration value on success, the default value on failure
 */
gint mce_conf_get_int(const gchar *group, const gchar *key,
		      const gint defaultval, gpointer keyfileptr)
{
	gint tmp = -1;
	GError *error = NULL;

	keyfileptr = mce_conf_decide_keyfile_to_use(group, key, keyfileptr);
	if (keyfileptr == NULL) {
		tmp = defaultval;
		goto EXIT;
	}

	tmp = g_key_file_get_integer(keyfileptr, group, key, &error);

	if (error != NULL) {
		mce_log(LL_WARN, "mce-conf: "
			"Could not get config key %s/%s; %s; "
			"defaulting to `%d'",
			group, key, error->message, defaultval);
		tmp = defaultval;
	}

	g_clear_error(&error);

EXIT:
	return tmp;
}

gboolean mce_conf_set_int(const gchar *group, const gchar *key,
		      const gint val, gpointer keyfileptr)
{
	keyfileptr = mce_conf_decide_keyfile_to_use(group, key, keyfileptr);
	if (keyfileptr == NULL)
		return FALSE;

	g_key_file_set_integer(keyfileptr, group, key, val);

	return TRUE;
}

/**
 * Get an integer list configuration value
 *
 * @param group The configuration group to get the value from
 * @param key The configuration key to get the value of
 * @param length The length of the list
 * @param keyfileptr A keyfile pointer, or NULL to use the default keyfile
 * @return The configuration value on success, NULL on failure
 */
gint *mce_conf_get_int_list(const gchar *group, const gchar *key,
			    gsize *length, gpointer keyfileptr)
{
	gint *tmp = NULL;
	GError *error = NULL;

	keyfileptr = mce_conf_decide_keyfile_to_use(group, key, keyfileptr);
	if (keyfileptr == NULL) {
		*length = 0;
		goto EXIT;
	}

	tmp = g_key_file_get_integer_list(keyfileptr, group, key,
					  length, &error);

	if (error != NULL) {
		mce_log(LL_WARN, "mce-conf: "
			"Could not get config key %s/%s; %s",
			group, key, error->message);
		*length = 0;
	}

	g_clear_error(&error);

EXIT:
	return tmp;
}

/**
 * Get a string configuration value
 *
 * @param group The configuration group to get the value from
 * @param key The configuration key to get the value of
 * @param defaultval The default value to use if the key isn't set
 * @param keyfileptr A keyfile pointer, or NULL to use the default keyfile
 * @return The configuration value on success, the default value on failure
 */
gchar *mce_conf_get_string(const gchar *group, const gchar *key,
			   const gchar *defaultval, gpointer keyfileptr)
{
	gchar *tmp = NULL;
	GError *error = NULL;
	
	keyfileptr = mce_conf_decide_keyfile_to_use(group, key, keyfileptr);
	if (keyfileptr == NULL) {
		if (defaultval != NULL)
			tmp = g_strdup(defaultval);
		goto EXIT;
	}

	tmp = g_key_file_get_string(keyfileptr, group, key, &error);

	if (error != NULL) {
		mce_log(LL_WARN, "mce-conf: "
			"Could not get config key %s/%s; %s; %s%s%s",
			group, key, error->message,
			defaultval ? "defaulting to `" : "no default set",
			defaultval ? defaultval : "",
			defaultval ? "'" : "");

		if (defaultval != NULL)
			tmp = g_strdup(defaultval);
	}

	g_clear_error(&error);

EXIT:
	return tmp;
}

/**
 * Get a string list configuration value
 *
 * @param group The configuration group to get the value from
 * @param key The configuration key to get the value of
 * @param length The length of the list
 * @param keyfileptr A keyfile pointer, or NULL to use the default keyfile
 * @return The configuration value on success, NULL on failure
 */
gchar **mce_conf_get_string_list(const gchar *group, const gchar *key,
				 gsize *length, gpointer keyfileptr)
{
	gchar **tmp = NULL;
	GError *error = NULL;

	keyfileptr = mce_conf_decide_keyfile_to_use(group, key, keyfileptr);
	if (keyfileptr == NULL) {
		*length = 0;
		goto EXIT;
	}

	tmp = g_key_file_get_string_list(keyfileptr, group, key,
					 length, &error);

	if (error != NULL) {
		mce_log(LL_WARN, "mce-conf: "
			"Could not get config key %s/%s; %s",
			group, key, error->message);
		*length = 0;
	}

	g_clear_error(&error);

EXIT:
	return tmp;
}

/**
 * Free configuration file
 *
 * @param keyfileptr A pointer to the keyfile to free
 */
void mce_conf_free_conf_file(gpointer keyfileptr)
{
	if (keyfileptr != NULL) {
		g_key_file_free(keyfileptr);
	}
}


static int mce_conf_compare_file_prio(const void *a, const void *b)
{
	const struct mce_conf_file *conf_a = (const struct mce_conf_file *) a;
	const struct mce_conf_file *conf_b = (const struct mce_conf_file *) b;
	
	if (conf_a->filename == NULL && conf_b->filename == NULL)
		return 0;
	if (conf_a->filename == NULL && conf_b->filename != NULL)
		return -1;
	if (conf_a->filename != NULL && conf_b->filename == NULL)
		return 1;
	if (strcmp(conf_a->filename, G_STRINGIFY(MCE_CONF_FILE)) == 0)
		return -1;
	if (strcmp(conf_b->filename, G_STRINGIFY(MCE_CONF_FILE)) == 0)
		return 1;

	return strverscmp(conf_a->filename, conf_b->filename);
}

/**
 * Read configuration file
 *
 * @param conffile The full path to the configuration file to read
 * @return A keyfile pointer on success, NULL on failure
 */
gpointer mce_conf_read_conf_file(const gchar *const conffile)
{
	GError *error = NULL;
	GKeyFile *keyfileptr;

	if ((keyfileptr = g_key_file_new()) == NULL)
		goto EXIT;

	if (g_key_file_load_from_file(keyfileptr, conffile,
				      G_KEY_FILE_NONE, &error) == FALSE) {
		mce_conf_free_conf_file(keyfileptr);
		keyfileptr = NULL;
		mce_log(LL_WARN, "mce-conf: Could not load %s; %s",
			conffile, error->message);
		goto EXIT;
	}

EXIT:
	g_clear_error(&error);

	return keyfileptr;
}

static gboolean mce_conf_is_ini_file(const char *filename)
{
	char *point_location = strrchr(filename, '.');
	if(point_location == NULL)
		return FALSE;
	else if(strcmp(point_location, ".ini") == 0)
		return TRUE;
	else
		return FALSE;
}

/**
 * Init function for the mce-conf component
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean mce_conf_init(void)
{
	DIR *dir = NULL;
	mce_conf_file_count = 1;
	struct dirent *direntry;

	gchar *override_dir_path = g_strconcat(G_STRINGIFY(MCE_CONF_DIR), "/", 
										 G_STRINGIFY(MCE_CONF_OVERRIDE_DIR), NULL);
	dir = opendir(override_dir_path);
	if (dir) {
		while ((direntry = readdir(dir)) != NULL && telldir(dir)) {
			if ((direntry->d_type == DT_REG || direntry->d_type == DT_LNK) && 
				mce_conf_is_ini_file(direntry->d_name))
				++mce_conf_file_count;
		}
		rewinddir(dir);
	} else {
		mce_log(LL_WARN, "mce-conf: could not open dir %s", override_dir_path);
	}
	g_free(override_dir_path);

	conf_files = calloc(mce_conf_file_count, sizeof(*conf_files));
	
	conf_files[0].filename = g_strdup(G_STRINGIFY(MCE_CONF_FILE));
	conf_files[0].path     = g_strconcat(G_STRINGIFY(MCE_CONF_DIR), "/", 
										 G_STRINGIFY(MCE_CONF_FILE), NULL);
	gpointer main_conf_file = mce_conf_read_conf_file(conf_files[0].path);
	if (main_conf_file == NULL) {
		mce_log(LL_ERR, "mce-conf: failed to open main config file %s %s", 
				conf_files[0].path, g_strerror(errno));
		g_free(conf_files[0].filename);
		g_free(conf_files[0].path);
		free(conf_files);
		conf_files = NULL;
		return FALSE;
	}
	conf_files[0].keyfile = main_conf_file;

	if (dir) {
		size_t i = 1;
		direntry = readdir(dir);
		while (direntry != NULL && i < mce_conf_file_count && telldir(dir)) {
			if ((direntry->d_type == DT_REG || direntry->d_type == DT_LNK) && 
				mce_conf_is_ini_file(direntry->d_name)) {
				conf_files[i].filename = g_strdup(direntry->d_name);
				conf_files[i].path     = g_strconcat(G_STRINGIFY(MCE_CONF_DIR), "/", 
											G_STRINGIFY(MCE_CONF_OVERRIDE_DIR), "/", 
											conf_files[i].filename, NULL);
				conf_files[i].keyfile  = mce_conf_read_conf_file(conf_files[i].path);
				 ++i;
			}
			direntry = readdir(dir);
		}
		closedir(dir);
		
		qsort(conf_files, mce_conf_file_count, sizeof(*conf_files), &mce_conf_compare_file_prio);
	}
	
	for (size_t i = 0; i < mce_conf_file_count; ++i)
		mce_log(LL_DEBUG, "mce-conf: found conf file %lu: %s", (unsigned long)i, conf_files[i].filename);

	return TRUE;
}

/**
 * Exit function for the mce-conf component
 */
void mce_conf_exit(void)
{
	for (size_t i = 0; i < mce_conf_file_count; ++i) {
		if (conf_files[i].filename)
			g_free(conf_files[i].filename);
		if (conf_files[i].path)
			g_free(conf_files[i].path);
		
		mce_conf_free_conf_file(conf_files[i].keyfile);
	}

	return;
}
