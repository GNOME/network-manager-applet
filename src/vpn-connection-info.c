/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <string.h>
#include <gconf/gconf-client.h>
#include "vpn-connection-info.h"

struct _VPNConnectionInfo {
	char *name;
	GConfClient *client;
};

#define NM_GCONF_VPN_CONNECTIONS_PATH "/system/networking/vpn_connections"

static VPNConnectionInfo *
vpn_connection_info_new (GConfClient *client, const char *name)
{
	VPNConnectionInfo *info;

	info = g_slice_new (VPNConnectionInfo);
	info->name = g_strdup (name);
	info->client = client;

	return info;
}

void
vpn_connection_info_destroy (VPNConnectionInfo *info)
{
	if (info) {
		g_free (info->name);
		g_object_unref (info->client);
		g_slice_free (VPNConnectionInfo, info);
	}
}

GSList *
vpn_connection_info_list (void)
{
	GConfClient *client;
	GSList *gconf_dirs;
	GSList *iter;
	int dir_strlen;
	GError *err = NULL;
	GSList *list = NULL;

	client = gconf_client_get_default ();

	gconf_dirs = gconf_client_all_dirs (client, NM_GCONF_VPN_CONNECTIONS_PATH, &err);
	if (err) {
		g_warning ("Could not list VPN connections: %s", err->message);
		g_error_free (err);
		goto out;
	}

	dir_strlen = strlen (NM_GCONF_VPN_CONNECTIONS_PATH) + 1;

	for (iter = gconf_dirs; iter; iter = iter->next) {
		char *path = (char *) iter->data;

		list = g_slist_prepend (list, vpn_connection_info_new (client, path + dir_strlen));
		g_free (path);
	}

	g_slist_free (gconf_dirs);

 out:
	g_object_unref (client);

	return list;
}

const char *
vpn_connection_info_get_name (VPNConnectionInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->name;
}

static char *
get_gconf_key (VPNConnectionInfo *info, const char *key)
{
	return g_strconcat (NM_GCONF_VPN_CONNECTIONS_PATH, "/", info->name, "/", key, NULL);
}

char *
vpn_connection_info_get_service (VPNConnectionInfo *info)
{
	char *str;
	char *key;
	GError *err;

	g_return_val_if_fail (info != NULL, NULL);

	err = NULL;
	key = get_gconf_key (info, "service_name");

	str = gconf_client_get_string (info->client, key, &err);
	g_free (key);

	if (err) {
		g_warning ("Can not retrieve service name: %s", err->message);
		g_error_free (err);
	}

	return str;
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

GHashTable *
vpn_connection_info_get_properties (VPNConnectionInfo *info)
{	
	GHashTable *properties;
	GSList *gconf_entries;
	GSList *iter;
	char *key;
	int prefix_len;
	GError *err = NULL;

	g_return_val_if_fail (info != NULL, NULL);

	key = g_strconcat (NM_GCONF_VPN_CONNECTIONS_PATH, "/", info->name, NULL);
	prefix_len = strlen (key);
	gconf_entries = gconf_client_all_entries (info->client, key, &err);
  	g_free (key);

	if (err) {
		g_warning ("Could not list get properties: %s", err->message);
		g_error_free (err);
		return NULL;
	}

	properties = g_hash_table_new_full (g_str_hash, g_str_equal,
								 (GDestroyNotify) g_free,
								 property_value_destroy);

	for (iter = gconf_entries; iter; iter = iter->next) {
		GConfEntry *entry = (GConfEntry *) iter->data;

		key = (char *) gconf_entry_get_key (entry);
		key += prefix_len + 1; /* get rid of the full path */

		if (!strcmp (key, "name") ||
		    !strcmp (key, "service_name") ||
		    !strcmp (key, "routes") ||
		    !strcmp (key, "last_attempt_success")) {
			gconf_entry_unref (entry);
			continue;
		}

		add_property (properties, key, gconf_entry_get_value (entry));
		gconf_entry_unref (entry);
	}

	g_slist_free (gconf_entries);

	return properties;
}

GSList *
vpn_connection_info_get_routes (VPNConnectionInfo *info)
{
	char *key;
	GSList *routes;
	GError *err;

	g_return_val_if_fail (info != NULL, NULL);

	err = NULL;
	key = g_strconcat (NM_GCONF_VPN_CONNECTIONS_PATH, "/", info->name, "routes", NULL);

	routes = gconf_client_get_list (info->client, key, GCONF_VALUE_STRING, &err);
	g_free (key);

	if (err) {
		g_warning ("Can not retrieve routes: %s", err->message);
		g_error_free (err);
	}

	return routes;
}

gboolean
vpn_connection_info_get_last_attempt_success (VPNConnectionInfo *info)
{
	char *key;
	gboolean success;
	GError *err;

	g_return_val_if_fail (info != NULL, FALSE);

	err = NULL;
	key = get_gconf_key (info, "last_attempt_success");

	success = gconf_client_get_bool (info->client, key, &err);
	g_free (key);

	if (err) {
		g_warning ("Can not retrieve last_attempt_success: %s", err->message);
		g_error_free (err);
	}

	return success;
}

void
vpn_connection_info_set_last_attempt_success (VPNConnectionInfo *info,
									 gboolean success)
{
	char *key;
	GError *err;

	g_return_if_fail (info != NULL);

	err = NULL;
	key = get_gconf_key (info, "last_attempt_success");

	success = gconf_client_set_bool (info->client, key, success, &err);
	g_free (key);

	if (err) {
		g_warning ("Can not set last_attempt_success: %s", err->message);
		g_error_free (err);
	}
}

VPNConnectionInfo *
vpn_connection_info_copy (VPNConnectionInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return vpn_connection_info_new (info->client, info->name);
}
