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

#include <string.h>
#include <errno.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>
#include <glib.h>
#include <gnome-keyring.h>

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
nm_gconf_get_float_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					gfloat *value)
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
		if (gc_value->type == GCONF_VALUE_FLOAT)
		{
			*value = gconf_value_get_float (gc_value);
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

static void
property_value_destroy (gpointer data)
{
	GValue *value = (GValue *) data;

	g_value_unset (value);
	g_slice_free (GValue, data);
}


static void
add_property (GHashTable *properties, const char *key, GConfValue *gconf_value)
{
	GValue *value = NULL;

	if (!gconf_value)
		return;

	switch (gconf_value->type) {
	case GCONF_VALUE_STRING:
		value = g_slice_new0 (GValue);
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, gconf_value_get_string (gconf_value));
		break;
	case GCONF_VALUE_INT:
		value = g_slice_new0 (GValue);
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, gconf_value_get_int (gconf_value));
		break;
	case GCONF_VALUE_BOOL:
		value = g_slice_new0 (GValue);
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, gconf_value_get_bool (gconf_value));
		break;
	default:
		break;
	}

	if (value)
		g_hash_table_insert (properties, gconf_unescape_key (key, -1), value);
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
nm_gconf_set_float_helper (GConfClient *client,
                           const char *path,
                           const char *key,
                           const char *network,
                           gfloat value)
{
	char * gc_key;

	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);

	gc_key = g_strdup_printf ("%s/%s/%s", path, network, key);
	if (!gc_key) {
		g_warning ("Not enough memory to create gconf path");
		return FALSE;
	}
	gconf_client_set_float (client, gc_key, value, NULL);
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
	NMConnection *connection;
	GConfClient *client;
	const char *dir;
	guint32 dir_len;
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
			int int_val = 0;
			guint32 *uint_val = (guint32 *) value;
			if (nm_gconf_get_int_helper (info->client, info->dir, key,
			                             setting->name, &int_val)) {
				if (int_val < 0)
					g_warning ("Casting negative value (%i) to uint", int_val);
			}
			*uint_val = (guint32) int_val;
			break;
		}
		case NM_S_TYPE_UINT64: {
			guint64 *uint_val = (guint64 *) value;
			char *tmp_str = NULL;
			/* GConf doesn't do 64-bit values, so use strings instead */
			nm_gconf_get_string_helper (info->client, info->dir, key,
			                           setting->name, &tmp_str);
			if (!tmp_str)
				break;
			*uint_val = g_ascii_strtoull (tmp_str, NULL, 10);
			if ((*uint_val == G_MAXUINT64) && (errno == ERANGE))
				*uint_val = 0;
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
			nm_gconf_get_valuehash_helper (info->client, info->dir,
									 setting->name, vh_val);

			break;
		}
	}
}

static void
read_one_setting (gpointer data, gpointer user_data)
{
	char *name;
	ReadFromGConfInfo *info = (ReadFromGConfInfo *) user_data;
	NMSetting *setting;

	/* Setting name is the gconf directory name. Since "data" here contains
	   full gconf path plus separator ('/'), omit that. */
	name =  (char *) data + info->dir_len + 1;
	setting = nm_connection_create_setting (name);
	if (setting) {
		nm_setting_enumerate_values (setting,
							    read_one_setting_value_from_gconf,
							    info);
		nm_connection_add_setting (info->connection, setting);
	}

	g_free (data);
}

NMConnection *
nm_gconf_read_connection (GConfClient *client,
                          const char *dir)
{
	ReadFromGConfInfo info;
	GSList *list;
	GError *err = NULL;

	list = gconf_client_all_dirs (client, dir, &err);
	if (err) {
		g_warning ("Error while reading connection: %s", err->message);
		g_error_free (err);
		return NULL;
	}

	if (!list) {
		g_warning ("Invalid connection (empty)");
		return NULL;
	}

	info.connection = nm_connection_new ();
	info.client = client;
	info.dir = dir;
	info.dir_len = strlen (dir);

	g_slist_foreach (list, read_one_setting, &info);
	g_slist_free (list);

	return info.connection;
}


static void
add_keyring_item (const char *connection_name,
                  const char *setting_name,
                  const char *setting_key,
                  const char *secret)
{
	GnomeKeyringResult ret;
	char *display_name = NULL;
	GnomeKeyringAttributeList *attrs = NULL;
	guint32 id = 0;

	g_return_if_fail (connection_name != NULL);
	g_return_if_fail (setting_name != NULL);
	g_return_if_fail (setting_key != NULL);
	g_return_if_fail (secret != NULL);

	display_name = g_strdup_printf ("Network secret for %s/%s/%s",
	                                connection_name,
	                                setting_name,
	                                setting_key);

	attrs = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (attrs,
	                                            "connection-name",
	                                            connection_name);
	gnome_keyring_attribute_list_append_string (attrs,
	                                            "setting-name",
	                                            setting_name);
	gnome_keyring_attribute_list_append_string (attrs,
	                                            "setting-key",
	                                            setting_key);

	ret = gnome_keyring_item_create_sync (NULL,
	                                      GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      display_name,
	                                      attrs,
	                                      secret,
	                                      TRUE,
	                                      &id);

	gnome_keyring_attribute_list_free (attrs);
	g_free (display_name);
}

typedef struct CopyOneSettingValueInfo {
	GConfClient *client;
	const char *dir;
	const char *connection_name;
	KeyFilterFunc key_filter_func;
} CopyOneSettingValueInfo;

static void
copy_one_setting_value_to_gconf (NMSetting *setting,
                                 const char *key,
                                 guint32 type,
                                 void *value,
                                 gboolean secret,
                                 gpointer user_data)
{
	CopyOneSettingValueInfo *info = (CopyOneSettingValueInfo *) user_data;

	if (info->key_filter_func)
		if ((*info->key_filter_func)(setting->name, key) == FALSE)
			return;

	switch (type) {
		case NM_S_TYPE_STRING: {
			const char **str_val = (const char **) value;
			if (!*str_val)
				break;
			if (secret) {
				if (strlen (*str_val)) {
					add_keyring_item (info->connection_name, setting->name,
					                  key, *str_val);
				}
			} else {
				nm_gconf_set_string_helper (info->client, info->dir,
				                            key, setting->name, *str_val);
			}
			break;
		}
		case NM_S_TYPE_UINT32: {
			guint32 *uint_val = (guint32 *) value;
			if (!*uint_val)
				break;
			nm_gconf_set_int_helper (info->client, info->dir,
			                         key, setting->name, *uint_val);
			break;
		}
		case NM_S_TYPE_UINT64: {
			guint64 *uint_val = (guint64 *) value;
			char *numstr;
			if (!*uint_val)
				break;
			/* GConf doesn't do 64-bit values, so use strings instead */
			numstr = g_strdup_printf ("%" G_GUINT64_FORMAT, *uint_val);
			if (!numstr)
				break;
			nm_gconf_set_string_helper (info->client, info->dir,
			                            key, setting->name, numstr);
			g_free (numstr);
			break;
		}
		case NM_S_TYPE_BOOL: {
			gboolean *bool_val = (gboolean *) value;
			nm_gconf_set_bool_helper (info->client, info->dir,
			                          key, setting->name, *bool_val);
			break;
		}

		case NM_S_TYPE_BYTE_ARRAY: {
			GByteArray **ba_val = (GByteArray **) value;
			if (!*ba_val)
				break;
			nm_gconf_set_bytearray_helper (info->client, info->dir,
			                               key, setting->name, *ba_val);
			break;
		}

		case NM_S_TYPE_STRING_ARRAY: {
			GSList **sa_val = (GSList **) value;
			if (!*sa_val)
				break;
			nm_gconf_set_stringlist_helper (info->client, info->dir,
			                                key, setting->name, *sa_val);
			break;
		}

		case NM_S_TYPE_GVALUE_HASH: {
			GHashTable **vh_val = (GHashTable **) value;
			if (!*vh_val)
				break;
			nm_gconf_set_valuehash_helper (info->client, info->dir,
			                               setting->name, *vh_val);
			break;
		}
	}
}

void
nm_gconf_write_connection (NMConnection *connection,
                           GConfClient *client,
                           const char *dir,
                           KeyFilterFunc func)
{
	NMSettingConnection *s_connection;
	CopyOneSettingValueInfo info;

	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (client != NULL);
	g_return_if_fail (dir != NULL);

	s_connection = (NMSettingConnection *) nm_connection_get_setting (connection, NM_SETTING_CONNECTION);
	if (!s_connection)
		return;

	info.client = client;
	info.dir = dir;
	info.connection_name = s_connection->name;
	info.key_filter_func = func;
	nm_connection_for_each_setting_value (connection,
	                                      copy_one_setting_value_to_gconf,
	                                      &info);

}

