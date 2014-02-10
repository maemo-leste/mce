/**
 * @file mce-hal.c
 * Hardware Abstraction Layer for MCE
 * <p>
 * Copyright © 2009-2010 Nokia Corporation and/or its subsidiary(-ies).
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
#include <string.h>
#include "mce-log.h"
#include "mce-io.h"
#include "mce-hal.h"

/**
 * The product ID of the device
 */
static product_id_t product_id = PRODUCT_UNSET;

/**
 * Get product ID
 */
product_id_t get_product_id(void)
{
	gchar *tmp = NULL;

	if (product_id != PRODUCT_UNSET)
		goto EXIT;

	if (mce_read_string_from_file(COMPONENT_VERSION_PATH, &tmp) == FALSE) {
		if (mce_read_string_from_file(CPUINFO_PATH, &tmp) == FALSE)
			goto EXIT;
	}

	if (strstr(tmp, PRODUCT_SU18_STR) != NULL) {
		product_id = PRODUCT_SU18;
	} else if (strstr(tmp, PRODUCT_RX34_STR) != NULL) {
		product_id = PRODUCT_RX34;
	} else if (strstr(tmp, PRODUCT_RX44_STR) != NULL) {
		product_id = PRODUCT_RX44;
	} else if (strstr(tmp, PRODUCT_RX48_STR) != NULL) {
		product_id = PRODUCT_RX48;
	} else if (strstr(tmp, PRODUCT_RX51_STR) != NULL) {
		product_id = PRODUCT_RX51;
	} else {
		product_id = PRODUCT_UNKNOWN;
	}

	g_free(tmp);

EXIT:
	return product_id;
}
