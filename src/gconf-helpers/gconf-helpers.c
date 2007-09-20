/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager -- Network link manager
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
 * (C) Copyright 2005 Red Hat, Inc.
 */

#include <gconf/gconf.h>
#include <gconf/gconf-client.h>
#include <glib.h>

#include "gconf-helpers.h"


gboolean
nm_gconf_get_int_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					int *value)
{
	char *		gc_key;
	GConfValue *	gc_value;
	gboolean		success = FALSE;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if ((gc_value = gconf_client_get (client, gc_key, NULL)))
	{
		if (gc_value->type == GCONF_VALUE_INT)
		{
			*value = gconf_value_get_int (gc_value);
			success = TRUE;
		}
		gconf_value_free (gc_value);
	}
	g_free (gc_key);

	return success;
}


gboolean
nm_gconf_get_string_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					char **value)
{
	char *		gc_key;
	GConfValue *	gc_value;
	gboolean		success = FALSE;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (*value == NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if ((gc_value = gconf_client_get (client, gc_key, NULL)))
	{
		if (gc_value->type == GCONF_VALUE_STRING)
		{
			*value = g_strdup (gconf_value_get_string (gc_value));
			success = TRUE;
		}
		gconf_value_free (gc_value);
	}
	g_free (gc_key);

	return success;
}


gboolean
nm_gconf_get_bool_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					gboolean *value)
{
	char *		gc_key;
	GConfValue *	gc_value;
	gboolean		success = FALSE;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if ((gc_value = gconf_client_get (client, gc_key, NULL)))
	{
		if (gc_value->type == GCONF_VALUE_BOOL)
		{
			*value = gconf_value_get_bool (gc_value);
			success = TRUE;
		}
		gconf_value_free (gc_value);
	}
	g_free (gc_key);

	return success;
}

gboolean
nm_gconf_get_stringlist_helper (GConfClient *client,
				const char *path,
				const char *key,
				const char *network,
				GSList **value)
{
	char *gc_key;
	GConfValue *gc_value;
	gboolean success = FALSE;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if (!(gc_value = gconf_client_get (client, gc_key, NULL)))
		goto out;

	if (gc_value->type == GCONF_VALUE_LIST
	    && gconf_value_get_list_type (gc_value) == GCONF_VALUE_STRING)
	{
		GSList *elt;

		for (elt = gconf_value_get_list (gc_value); elt != NULL; elt = g_slist_next (elt))
		{
			const char *string = gconf_value_get_string ((GConfValue *) elt->data);

			*value = g_slist_append (*value, g_strdup (string));
		}

		success = TRUE;
	}

out:
	g_free (gc_key);
	return success;
}

gboolean
nm_gconf_get_bytearray_helper (GConfClient *client,
			       const char *path,
			       const char *key,
			       const char *network,
			       GByteArray **value)
{
	char *gc_key;
	GConfValue *gc_value;
	GByteArray *array;
	gboolean success = FALSE;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if (!(gc_value = gconf_client_get (client, gc_key, NULL)))
		goto out;

	if (gc_value->type == GCONF_VALUE_LIST
	    && gconf_value_get_list_type (gc_value) == GCONF_VALUE_INT)
	{
		GSList *elt;

		array = g_byte_array_new ();
		for (elt = gconf_value_get_list (gc_value); elt != NULL; elt = g_slist_next (elt))
		{
			int i = gconf_value_get_int ((GConfValue *) elt->data);
			unsigned char val = (unsigned char) (i & 0xFF);

			if (i < 0 || i > 255) {
				g_log (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
				       "value %d out-of-range for a byte value", i);
				g_byte_array_free (array, TRUE);
				goto out;
			}

			g_byte_array_append (array, (const unsigned char *) &val, sizeof (val));
		}

		*value = array;
		success = TRUE;
	}

out:
	g_free (gc_key);
	return success;
}

gboolean
nm_gconf_get_valuehash_helper (GConfClient *client,
						 const char *path,
						 const char *network,
						 GHashTable **value)
{
	char *gc_key;
	GSList *gconf_entries;
	GSList *iter;
	int prefix_len;

	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s", path, network);
	prefix_len = strlen (gc_key);
	gconf_entries = gconf_client_all_entries (client, gc_key, NULL);
	g_free (gc_key);

	if (!gconf_entries)
		return FALSE;

	*value = g_hash_table_new_full (g_str_hash, g_str_equal,
							  (GDestroyNotify) g_free,
							  property_value_destroy);

	for (iter = gconf_entries; iter; iter = iter->next) {
		GConfEntry *entry = (GConfEntry *) iter->data;

		gc_key = (char *) gconf_entry_get_key (entry);
		gc_key += prefix_len + 1; /* get rid of the full path */

		add_property (*value, gc_key, gconf_entry_get_value (entry));
		gconf_entry_free (entry);
	}

	g_slist_free (gconf_entries);

	return TRUE;
}

gboolean
nm_gconf_set_int_helper (GConfClient *client,
                         const char *path,
                         const char *key,
                         const char *network,
                         int value)
{
	char * gc_key;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if (!gc_key) {
		g_warning ("Not enough memory to create gconf path");
		return FALSE;
	}
	gconf_client_set_int (client, gc_key, value, NULL);
	g_free (gc_key);
	return TRUE;
}

gboolean
nm_gconf_set_string_helper (GConfClient *client,
                            const char *path,
                            const char *key,
                            const char *network,
                            const char *value)
{
	char * gc_key;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if (!gc_key) {
		g_warning ("Not enough memory to create gconf path");
		return FALSE;
	}
	gconf_client_set_string (client, gc_key, value, NULL);
	g_free (gc_key);
	return TRUE;
}

gboolean
nm_gconf_set_bool_helper (GConfClient *client,
                          const char *path,
                          const char *key,
                          const char *network,
                          gboolean value)
{
	char * gc_key;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if (!gc_key) {
		g_warning ("Not enough memory to create gconf path");
		return FALSE;
	}
	gconf_client_set_bool (client, gc_key, value, NULL);
	g_free (gc_key);
	return TRUE;
}

gboolean
nm_gconf_set_stringlist_helper (GConfClient *client,
                                const char *path,
                                const char *key,
                                const char *network,
                                GSList *value)
{
	char *gc_key;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if (!gc_key) {
		g_warning ("Not enough memory to create gconf path");
		return FALSE;
	}

	gconf_client_set_list (client, gc_key, GCONF_VALUE_STRING, value, NULL);
	g_free (gc_key);
	return TRUE;
}

gboolean
nm_gconf_set_bytearray_helper (GConfClient *client,
                               const char *path,
                               const char *key,
                               const char *network,
                               GByteArray *value)
{
	char *gc_key;
	int i;
	GSList *list = NULL;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if (!gc_key) {
		g_warning ("Not enough memory to create gconf path");
		return FALSE;
	}

	for (i = 0; i < value->len; i++)
		list = g_slist_append(list, GINT_TO_POINTER ((int) value->data[i]));

	gconf_client_set_list (client, gc_key, GCONF_VALUE_INT, list, NULL);

	g_slist_free (list);
	g_free (gc_key);
	return TRUE;
}

typedef struct {
	GConfClient *client;
	char *path;
} WritePropertiesInfo;

static void
write_properties (gpointer key, gpointer val, gpointer user_data)
{
	GValue *value = (GValue *) val;
	WritePropertiesInfo *info = (WritePropertiesInfo *) user_data;
	char *esc_key;
	char *full_key;

	esc_key = gconf_escape_key ((char *) key, -1);
	full_key = g_strconcat (info->path, "/", esc_key, NULL);
	g_free (esc_key);

	if (G_VALUE_HOLDS_STRING (value))
		gconf_client_set_string (info->client, full_key, g_value_get_string (value), NULL);
	else if (G_VALUE_HOLDS_INT (value))
		gconf_client_set_int (info->client, full_key, g_value_get_int (value), NULL);
	else if (G_VALUE_HOLDS_BOOLEAN (value))
		gconf_client_set_bool (info->client, full_key, g_value_get_boolean (value), NULL);
	else
		g_warning ("Don't know how to write '%s' to gconf", G_VALUE_TYPE_NAME (value));

	g_free (full_key);
}

gboolean
nm_gconf_set_valuehash_helper (GConfClient *client,
						 const char *path,
						 const char *network,
						 GHashTable *value)
{
	char *gc_key;
	int i;
	WritePropertiesInfo info;

	g_return_val_if_fail (network != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s", path, network);
	if (!gc_key) {
		g_warning ("Not enough memory to create gconf path");
		return FALSE;
	}

	info.client = client;
	info.path = gc_key;

	g_hash_table_foreach (value, write_properties, &info);

	g_free (gc_key);
	return TRUE;
}

typedef struct ReadFromGConfInfo {
	GConfClient *client;
	const char *dir;
} ReadFromGConfInfo;

static void
read_one_setting_value_from_gconf (NMSetting *setting,
                                   const char *key,
                                   guint32 type,
                                   void *value,
                                   gboolean secret,
                                   gpointer user_data)
{
	ReadFromGConfInfo *info = (ReadFromGConfInfo *) user_data;

	switch (type) {
		case NM_S_TYPE_STRING: {
			char **str_val = (char **) value;
			nm_gconf_get_string_helper (info->client, info->dir, key,
			                            setting->name, str_val);
			break;
		}
		case NM_S_TYPE_UINT32: {
			guint32 *uint_val = (guint32 *) value;
			nm_gconf_get_int_helper (info->client, info->dir, key,
			                         setting->name, uint_val);
			break;
		}
		case NM_S_TYPE_BOOL: {
			gboolean *bool_val = (gboolean *) value;
			nm_gconf_get_bool_helper (info->client, info->dir, key,
			                          setting->name, bool_val);
			break;
		}

		case NM_S_TYPE_BYTE_ARRAY: {
			GByteArray **ba_val = (GByteArray **) value;
			nm_gconf_get_bytearray_helper (info->client, info->dir, key,
			                               setting->name, ba_val);
			break;
		}

		case NM_S_TYPE_STRING_ARRAY: {
			GSList **sa_val = (GSList **) value;
			nm_gconf_get_stringlist_helper (info->client, info->dir, key,
			                                setting->name, sa_val);
			break;
		}

		case NM_S_TYPE_GVALUE_HASH: {
			GHashTable **vh_val = (GHashTable **) value;
			nm_gconf_get_valuehash_helper (applet_connection->conf_client,
									 applet_connection->conf_dir,
									 setting->name,
									 vh_val);

			break;
		}
	}
}

NMConnection *
nm_gconf_read_connection (GConfClient *client,
                          const char *dir)
{
	ReadFromGConfInfo *info;
	NMConnection *connection;
	NMSetting *setting;
	char *key;

	info = g_slice_new0 (ReadFromGConfInfo);
	info->client = client;
	info->dir = dir;

	/* connection settings */
	connection = nm_connection_new ();

	setting = nm_setting_connection_new ();
	nm_setting_enumerate_values (setting,
	                             read_one_setting_value_from_gconf,
	                             info);
	nm_connection_add_setting (connection, setting);

	/* wireless settings */
	key = g_strdup_printf ("%s/802-11-wireless", dir);
	if (gconf_client_dir_exists (client, key, NULL)) {
		setting = nm_setting_wireless_new ();
		nm_setting_enumerate_values (setting,
		                             read_one_setting_value_from_gconf,
		                             info);
		nm_connection_add_setting (connection, setting);
	}
	g_free (key);

	/* wireless security settings */
	key = g_strdup_printf ("%s/802-11-wireless-security", dir);
	if (gconf_client_dir_exists (client, key, NULL)) {
		setting = nm_setting_wireless_security_new ();
		nm_setting_enumerate_values (setting,
		                             read_one_setting_value_from_gconf,
		                             info);
		nm_connection_add_setting (connection, setting);
	}
	g_free (key);

	/* wired settings */
	key = g_strdup_printf ("%s/802-3-ethernet", dir);
	if (gconf_client_dir_exists (client, key, NULL)) {
		setting = nm_setting_wired_new ();
		nm_setting_enumerate_values (setting,
		                             read_one_setting_value_from_gconf,
		                             info);
		nm_connection_add_setting (connection, setting);
	}
	g_free (key);

	/* VPN settings */
	key = g_strdup_printf ("%s/vpn", applet_connection->conf_dir);
	if (gconf_client_dir_exists (applet_connection->conf_client, key, NULL)) {
		setting = nm_setting_vpn_new ();
		nm_setting_enumerate_values (setting,
		                             read_one_setting_value_from_gconf,
		                             applet_connection);
		nm_connection_add_setting (connection, setting);
	}
	g_free (key);

	/* VPN properties settings */
	key = g_strdup_printf ("%s/vpn-properties", applet_connection->conf_dir);
	if (gconf_client_dir_exists (applet_connection->conf_client, key, NULL)) {
		setting = nm_setting_vpn_properties_new ();
		nm_setting_enumerate_values (setting,
		                             read_one_setting_value_from_gconf,
		                             applet_connection);
		nm_connection_add_setting (connection, setting);
	}
	g_free (key);

	g_slice_free (ReadFromGConfInfo, info);
	return connection;
}
