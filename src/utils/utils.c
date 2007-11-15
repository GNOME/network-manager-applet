/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <string.h>
#include <glib.h>

#include <nm-setting-connection.h>

#include "crypto.h"
#include "utils.h"

/*
 * utils_bin2hexstr
 *
 * Convert a byte-array into a hexadecimal string.
 *
 * Code originally by Alex Larsson <alexl@redhat.com> and
 *  copyright Red Hat, Inc. under terms of the LGPL.
 *
 */
char *
utils_bin2hexstr (const char *bytes, int len, int final_len)
{
	static char	hex_digits[] = "0123456789abcdef";
	char *		result;
	int			i;

	g_return_val_if_fail (bytes != NULL, NULL);
	g_return_val_if_fail (len > 0, NULL);
	g_return_val_if_fail (len < 256, NULL);	/* Arbitrary limit */

	result = g_malloc0 (len * 2 + 1);
	for (i = 0; i < len; i++)
	{
		result[2*i] = hex_digits[(bytes[i] >> 4) & 0xf];
		result[2*i+1] = hex_digits[bytes[i] & 0xf];
	}
	/* Cut converted key off at the correct length for this cipher type */
	if (final_len > -1)
		result[final_len] = '\0';

	return result;
}

static char * vnd_ignore[] = {
	"Semiconductor",
	"Components",
	"Corporation",
	"Corp.",
	"Corp",
	"Inc.",
	"Inc",
	NULL
};

#define DESC_TAG "description"

const char *
utils_get_device_description (NMDevice *device)
{
	char *description = NULL;
	char *product = NULL;
	char *vendor = NULL;
	char *p;
	char **words;
	char **item;
	GString *str;
	gboolean need_space = FALSE;

	g_return_val_if_fail (device != NULL, NULL);

	description = g_object_get_data (G_OBJECT (device), DESC_TAG);
	if (description)
		goto out;

	product = nm_device_get_product (device);
	vendor = nm_device_get_vendor (device);
	if (!product || !vendor)
		goto out;

	/* Replace stupid '_' with ' ' */
	p = product;
	while (*p) {
		if (*p == '_')
			*p = ' ';
		p++;
	}

	p = vendor;
	while (*p) {
		if (*p == '_' || *p == ',')
			*p = ' ';
		p++;
	}

	str = g_string_new_len (NULL, strlen (vendor) + strlen (product));

	/* In a futile attempt to shorten the vendor ID, ignore certain words */
	words = g_strsplit (vendor, " ", 0);

	for (item = words; *item; item++) {
		int i = 0;
		gboolean ignore = FALSE;

		if (g_ascii_isspace (**item) || (**item == '\0'))
			continue;

		while (vnd_ignore[i] && !ignore) {
			if (!strcmp (*item, vnd_ignore[i]))
				ignore = TRUE;
			i++;
		}

		if (!ignore) {
			g_string_append (str, *item);
			if (need_space)
				g_string_append_c (str, ' ');
			need_space = TRUE;
		}
	}
	g_strfreev (words);

	g_string_append_c (str, ' ');
	g_string_append (str, product);
	description = str->str;
	g_string_free (str, FALSE);

	g_object_set_data_full (G_OBJECT (device),
	                        "description", description,
	                        (GDestroyNotify) g_free);

out:
	g_free (product);
	g_free (vendor);
	return description;
}

static void
clear_one_byte_array_field (GByteArray **field)
{
	g_return_if_fail (field != NULL);

	if (!*field)
		return;
	g_byte_array_free (*field, TRUE);
	*field = NULL;
}

gboolean
utils_fill_one_crypto_object (NMConnection *connection,
                              const char *key_name,
                              gboolean is_private_key,
                              const char *password,
                              GByteArray **field,
                              GError **error)
{
	const char *filename;
	NMSettingConnection *s_con;
	guint32 ignore;

	g_return_if_fail (key_name != NULL);
	g_return_if_fail (field != NULL);

	clear_one_byte_array_field (field);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_return_if_fail (s_con != NULL);

	filename = g_object_get_data (G_OBJECT (connection), key_name);
	if (!filename)
		return;

	if (is_private_key)
		g_return_if_fail (password != NULL);

	if (is_private_key) {
		*field = crypto_get_private_key (filename, password, &ignore, error);
		if (error && *error)
			clear_one_byte_array_field (field);
	} else {
		*field = crypto_load_and_verify_certificate (filename, error);
		if (error && *error)
			clear_one_byte_array_field (field);
	}

	if (error && *error)
		return FALSE;
	return TRUE;
}

