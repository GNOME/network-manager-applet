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
#include <dbus/dbus-glib.h>
#include <nm-setting-connection.h>
#include <nm-setting-wireless-security.h>

#include "gconf-helpers.h"
#include "gconf-upgrade.h"


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
		else if (gc_value->type == GCONF_VALUE_STRING && !*gconf_value_get_string (gc_value))
		{
			/* This is a kludge to deal with VPN connections migrated from NM 0.6 */
			*value = TRUE;
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

	if (!value)
		return TRUE;

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

	if (!value)
		return TRUE;

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

GSList *
nm_gconf_get_all_connections (GConfClient *client)
{
	GSList *connections;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	if (!connections) {
		nm_gconf_migrate_0_6_connections (client);
		connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	}

	return connections;
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
                                   const GValue *value,
                                   gboolean secret,
                                   gpointer user_data)
{
	ReadFromGConfInfo *info = (ReadFromGConfInfo *) user_data;
	GType type = G_VALUE_TYPE (value);

	/* Binary certificate and key data doesn't get stored in GConf.  Instead,
	 * the path to the certificate gets stored in a special key and the
	 * certificate is read and stuffed into the setting right before
	 * the connection is sent to NM
	 */
	if (NM_IS_SETTING_WIRELESS_SECURITY (setting)) {
		if (   !strcmp (key, "ca-cert")
		    || !strcmp (key, "client-cert")
		    || !strcmp (key, "private-key")
		    || !strcmp (key, "phase2-ca-cert")
		    || !strcmp (key, "phase2-client-cert")
		    || !strcmp (key, "phase2-private-key")) {
			char *path_key;
			char *path_value = NULL;

			path_key = g_strdup_printf ("nma-path-%s", key);
			if (nm_gconf_get_string_helper (info->client, info->dir, path_key,
			                                setting->name, &path_value)) {
				g_object_set_data_full (G_OBJECT (info->connection),
				                        path_key, path_value,
				                        (GDestroyNotify) g_free);
			}
			g_free (path_key);
			return;
		}
	}

	if (type == G_TYPE_STRING) {
		char *str_val = NULL;

		if (nm_gconf_get_string_helper (info->client, info->dir, key, setting->name, &str_val))
			g_object_set (setting, key, str_val, NULL);
	} else if (type == G_TYPE_UINT) {
		int int_val = 0;

		if (nm_gconf_get_int_helper (info->client, info->dir, key, setting->name, &int_val)) {
			if (int_val < 0)
				g_warning ("Casting negative value (%i) to uint", int_val);

			g_object_set (setting, key, int_val, NULL);
		}
	} else if (type == G_TYPE_UINT64) {
		char *tmp_str = NULL;

		/* GConf doesn't do 64-bit values, so use strings instead */
		if (nm_gconf_get_string_helper (info->client, info->dir, key, setting->name, &tmp_str) && tmp_str) {
			guint64 uint_val = g_ascii_strtoull (tmp_str, NULL, 10);
			
			if (!(uint_val == G_MAXUINT64 && errno == ERANGE))
				g_object_set (setting, key, uint_val, NULL);
		}
	} else if (type == G_TYPE_BOOLEAN) {
		gboolean bool_val;

		if (nm_gconf_get_bool_helper (info->client, info->dir, key, setting->name, &bool_val))
			g_object_set (setting, key, bool_val, NULL);
	} else if (type == DBUS_TYPE_G_UCHAR_ARRAY) {
		GByteArray *ba_val = NULL;

		if (nm_gconf_get_bytearray_helper (info->client, info->dir, key, setting->name, &ba_val))
			g_object_set (setting, key, ba_val, NULL);
	} else if (type == dbus_g_type_get_collection ("GSList", G_TYPE_STRING)) {
		GSList *sa_val = NULL;

		if (nm_gconf_get_stringlist_helper (info->client, info->dir, key, setting->name, &sa_val))
			g_object_set (setting, key, sa_val, NULL);
	} else if (type == dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)) {
		GHashTable *vh_val = NULL;

		if (nm_gconf_get_valuehash_helper (info->client, info->dir, setting->name, &vh_val))
			g_object_set (setting, key, vh_val, NULL);
	} else
		g_warning ("Unhandled setting property type: '%s'", G_VALUE_TYPE_NAME (value));
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
	NMConnection *connection;
	GConfClient *client;
	const char *dir;
	const char *connection_name;
	KeyFilterFunc key_filter_func;
} CopyOneSettingValueInfo;

static void
copy_one_setting_value_to_gconf (NMSetting *setting,
                                 const char *key,
						   const GValue *value,
                                 gboolean secret,
                                 gpointer user_data)
{
	CopyOneSettingValueInfo *info = (CopyOneSettingValueInfo *) user_data;
	GType type = G_VALUE_TYPE (value);

	if (info->key_filter_func)
		if ((*info->key_filter_func)(setting->name, key) == FALSE)
			return;

	/* Binary certificate and key data doesn't get stored in GConf.  Instead,
	 * the path to the certificate gets stored in a special key and the
	 * certificate is read and stuffed into the setting right before
	 * the connection is sent to NM
	 */
	if (NM_IS_SETTING_WIRELESS_SECURITY (setting)) {
		if (   !strcmp (key, "ca-cert")
		    || !strcmp (key, "client-cert")
		    || !strcmp (key, "private-key")
		    || !strcmp (key, "phase2-ca-cert")
		    || !strcmp (key, "phase2-client-cert")
		    || !strcmp (key, "phase2-private-key")) {
			char *path_key;
			char *path_value = NULL;

			path_key = g_strdup_printf ("nma-path-%s", key);
			path_value = g_object_get_data (G_OBJECT (info->connection), path_key);
			if (path_value != NULL) {
				nm_gconf_set_string_helper (info->client, info->dir, path_key,
									   setting->name, path_value);
			}
			g_free (path_key);
		}
	}

	if (type == G_TYPE_STRING) {
		const char *str_val = g_value_get_string (value);
		if (str_val) {
			if (secret) {
				if (strlen (str_val))
					add_keyring_item (info->connection_name, setting->name, key, str_val);
			} else
				nm_gconf_set_string_helper (info->client, info->dir, key, setting->name, str_val);
		}
	} else if (type == G_TYPE_UINT) {
		nm_gconf_set_int_helper (info->client, info->dir,
							key, setting->name,
							g_value_get_uint (value));
	} else if (type == G_TYPE_UINT64) {
		char *numstr;

		/* GConf doesn't do 64-bit values, so use strings instead */
		numstr = g_strdup_printf ("%" G_GUINT64_FORMAT, g_value_get_uint64 (value));
		nm_gconf_set_string_helper (info->client, info->dir,
							   key, setting->name, numstr);
		g_free (numstr);
	} else if (type == G_TYPE_BOOLEAN) {
		nm_gconf_set_bool_helper (info->client, info->dir,
							 key, setting->name,
							 g_value_get_boolean (value));
	} else if (type == DBUS_TYPE_G_UCHAR_ARRAY) {
		nm_gconf_set_bytearray_helper (info->client, info->dir,
								 key, setting->name,
								 (GByteArray *) g_value_get_boxed (value));
	} else if (type == dbus_g_type_get_collection ("GSList", G_TYPE_STRING)) {
		nm_gconf_set_stringlist_helper (info->client, info->dir,
								  key, setting->name,
								  (GSList *) g_value_get_boxed (value));
	} else if (type == dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)) {
		nm_gconf_set_valuehash_helper (info->client, info->dir,
								 setting->name,
								 (GHashTable *) g_value_get_boxed (value));
	} else
		g_warning ("Unhandled type '%s'", g_type_name (type));
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

	s_connection = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_connection)
		return;

	info.connection = connection;
	info.client = client;
	info.dir = dir;
	info.connection_name = s_connection->name;
	info.key_filter_func = func;
	nm_connection_for_each_setting_value (connection,
	                                      copy_one_setting_value_to_gconf,
	                                      &info);

}

