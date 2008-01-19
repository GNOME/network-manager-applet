/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
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
 * (C) Copyright 2004-2005 Red Hat, Inc.
 */

#include <string.h>
#include <gnome-keyring.h>
#include <nm-connection.h>
#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>
#include <nm-setting-wireless.h>

#include "applet.h"
#include "applet-dbus-settings.h"
#include "applet-dbus-manager.h"
#include "gconf-helpers.h"
#include "nm-utils.h"
#include "vpn-password-dialog.h"
#include "applet-marshal.h"
#include "crypto.h"
#include "utils.h"

#define DBUS_TYPE_G_ARRAY_OF_OBJECT_PATH (dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH))
#define DBUS_TYPE_G_STRING_VARIANT_HASHTABLE (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))
#define DBUS_TYPE_G_DICT_OF_DICTS (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, DBUS_TYPE_G_STRING_VARIANT_HASHTABLE))

static NMConnectionSettings * applet_dbus_connection_settings_new_from_connection (GConfClient *conf_client,
                                                                                   const gchar *conf_dir,
                                                                                   NMConnection *connection);

static void connections_changed_cb (GConfClient *conf_client,
                                    guint cnxn_id,
                                    GConfEntry *entry,
                                    gpointer user_data);

static const char *applet_dbus_connection_settings_get_gconf_path (NMConnectionSettings *connection);

static AppletDbusConnectionSettings *applet_dbus_settings_get_by_gconf_path (AppletDbusSettings *applet_settings,
                                                                             const char *path);

static gboolean applet_dbus_connection_settings_changed (AppletDbusConnectionSettings *connection,
                                                         GConfEntry *entry);


enum {
	SETTINGS_NEW_SECRETS_REQUESTED,
	SETTINGS_LAST_SIGNAL
};

static guint settings_signals[SETTINGS_LAST_SIGNAL] = { 0 };


/*
 * AppletDbusSettings class implementation
 */

static GPtrArray *list_connections (NMSettings *settings);

G_DEFINE_TYPE (AppletDbusSettings, applet_dbus_settings, NM_TYPE_SETTINGS)

#define APPLET_CONNECTION_PROXY_TAG "dbus-proxy"

typedef struct GetSettingsInfo {
	AppletDbusSettings *applet_settings;
	NMConnection *connection;
	DBusGProxy *proxy;
	DBusGProxyCall *call;
} GetSettingsInfo;

static void
free_get_settings_info (gpointer data)
{
	GetSettingsInfo *info = (GetSettingsInfo *) data;

	if (info->applet_settings) {
		g_object_unref (info->applet_settings);
		info->applet_settings = NULL;
	}
	if (info->connection) {
		g_object_unref (info->connection);
		info->connection = NULL;
	}
	g_slice_free (GetSettingsInfo, data);	
}

static void
connection_get_settings_cb  (DBusGProxy *proxy,
                             DBusGProxyCall *call_id,
                             gpointer user_data)
{
	GetSettingsInfo *info = (GetSettingsInfo *) user_data;
	GError *err = NULL;
	GHashTable *settings = NULL;
	NMConnection *connection;
	AppletDbusSettings *applet_settings;

	g_return_if_fail (info != NULL);

	if (!dbus_g_proxy_end_call (proxy, call_id, &err,
	                            DBUS_TYPE_G_DICT_OF_DICTS, &settings,
	                            G_TYPE_INVALID)) {
		nm_warning ("Couldn't retrieve connection settings: %s.", err->message);
		g_error_free (err);
		goto out;
	}

	applet_settings = info->applet_settings;
	connection = info->connection;
 	if (connection == NULL) {
		const char *path = dbus_g_proxy_get_path (proxy);

		connection = nm_connection_new_from_hash (settings);
		if (connection == NULL)
			goto out;

		g_object_set_data_full (G_OBJECT (connection),
		                        APPLET_CONNECTION_PROXY_TAG,
		                        proxy,
		                        (GDestroyNotify) g_object_unref);
		g_hash_table_insert (applet_settings->system_connections,
		                     g_strdup (path),
		                     connection);
	} else {
		// FIXME: merge settings? or just replace?
		nm_warning ("%s (#%d): implement merge settings", __func__, __LINE__);
	}

out:
	if (settings)
		g_hash_table_destroy (settings);

	return;
}

static void
connection_removed_cb (DBusGProxy *proxy, gpointer user_data)
{
	AppletDbusSettings *applet_settings = APPLET_DBUS_SETTINGS (user_data);

	g_hash_table_remove (applet_settings->system_connections,
	                     dbus_g_proxy_get_path (proxy));
}

static void
connection_updated_cb (DBusGProxy *proxy, GHashTable *settings, gpointer user_data)
{
	AppletDbusSettings *applet_settings = APPLET_DBUS_SETTINGS (user_data);
	NMConnection *new_connection;
	NMConnection *old_connection;
	const char *path = dbus_g_proxy_get_path (proxy);
	gboolean valid = FALSE;

	old_connection = g_hash_table_lookup (applet_settings->system_connections, path);
	if (!old_connection)
		return;

	new_connection = nm_connection_new_from_hash (settings);
	if (!new_connection) {
		/* New connection invalid, remove existing connection */
		g_hash_table_remove (applet_settings->system_connections, path);
		return;
	}
	g_object_unref (new_connection);

	valid = nm_connection_replace_settings (old_connection, settings);
	if (!valid)
		g_hash_table_remove (applet_settings->system_connections, path);
}

static void
new_connection_cb (DBusGProxy *proxy,
                   const char *path,
                   AppletDbusSettings *applet_settings)
{
	struct GetSettingsInfo *info;
	DBusGProxy *con_proxy;
	AppletDBusManager *dbus_mgr;
	DBusGConnection *g_connection;
	DBusGProxyCall *call;

	dbus_mgr = applet_dbus_manager_get ();
	g_connection = applet_dbus_manager_get_connection (dbus_mgr);
	con_proxy = dbus_g_proxy_new_for_name (g_connection,
	                                       dbus_g_proxy_get_bus_name (proxy),
	                                       path,
	                                       NM_DBUS_IFACE_SETTINGS_CONNECTION);
	if (!con_proxy) {
		nm_warning ("Error: could not init user connection proxy");
		g_object_unref (dbus_mgr);
		return;
	}

	dbus_g_proxy_add_signal (con_proxy, "Updated",
	                         DBUS_TYPE_G_DICT_OF_DICTS,
	                         G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (con_proxy, "Updated",
	                             G_CALLBACK (connection_updated_cb),
	                             applet_settings,
	                             NULL);

	dbus_g_proxy_add_signal (con_proxy, "Removed", G_TYPE_INVALID, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (con_proxy, "Removed",
	                             G_CALLBACK (connection_removed_cb),
	                             applet_settings,
	                             NULL);

	info = g_slice_new0 (GetSettingsInfo);
	info->applet_settings = g_object_ref (applet_settings);
	call = dbus_g_proxy_begin_call (con_proxy, "GetSettings",
	                                connection_get_settings_cb,
	                                info,
	                                free_get_settings_info,
	                                G_TYPE_INVALID);
	info->call = call;
	info->proxy = con_proxy;
}

static void
list_connections_cb  (DBusGProxy *proxy,
                      DBusGProxyCall *call_id,
                      gpointer user_data)
{
	AppletDbusSettings *applet_settings = APPLET_DBUS_SETTINGS (user_data);
	GError *err = NULL;
	GPtrArray *ops;
	int i;

	if (!dbus_g_proxy_end_call (proxy, call_id, &err,
	                            DBUS_TYPE_G_ARRAY_OF_OBJECT_PATH, &ops,
	                            G_TYPE_INVALID)) {
		nm_warning ("Couldn't retrieve system connections: %s.", err->message);
		g_error_free (err);
		return;
	}

	for (i = 0; i < ops->len; i++)
		new_connection_cb (proxy, g_ptr_array_index (ops, i), applet_settings);
}

static void
query_system_connections (AppletDbusSettings *applet_settings)
{
	DBusGProxyCall *call;

	g_return_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings));

	if (!applet_settings->system_proxy) {
		AppletDBusManager *dbus_mgr;
		DBusGConnection *g_connection;
		DBusGProxy *proxy;

		dbus_mgr = applet_dbus_manager_get ();
		g_connection = applet_dbus_manager_get_connection (dbus_mgr);
		proxy = dbus_g_proxy_new_for_name (g_connection,
		                                   NM_DBUS_SERVICE_SYSTEM_SETTINGS,
		                                   NM_DBUS_PATH_SETTINGS,
		                                   NM_DBUS_IFACE_SETTINGS);
		g_object_unref (dbus_mgr);
		if (!proxy) {
			nm_warning ("Error: could not init system settings proxy");
			return;
		}

		dbus_g_proxy_add_signal (proxy,
		                         "NewConnection",
		                         DBUS_TYPE_G_OBJECT_PATH,
		                         G_TYPE_INVALID);

		dbus_g_proxy_connect_signal (proxy, "NewConnection",
		                             G_CALLBACK (new_connection_cb),
		                             applet_settings,
		                             NULL);
		applet_settings->system_proxy = proxy;
	}

	/* grab connections */
	call = dbus_g_proxy_begin_call (applet_settings->system_proxy, "ListConnections",
	                                list_connections_cb,
	                                applet_settings,
	                                NULL,
	                                G_TYPE_INVALID);
}

static void
destroy_system_connections (AppletDbusSettings *applet_settings)
{
	g_return_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings));

	g_hash_table_remove_all (applet_settings->system_connections);
}

static void
applet_dbus_settings_name_owner_changed (AppletDBusManager *mgr,
                                         const char *name,
                                         const char *old,
                                         const char *new,
                                         gpointer user_data)
{
	AppletDbusSettings *applet_settings = APPLET_DBUS_SETTINGS (user_data);
	gboolean old_owner_good = (old && (strlen (old) > 0));
	gboolean new_owner_good = (new && (strlen (new) > 0));

	if (strcmp (name, NM_DBUS_SERVICE_SYSTEM_SETTINGS) == 0) {
		if (!old_owner_good && new_owner_good) {
			/* System Settings service appeared, update stuff */
			query_system_connections (applet_settings);
		} else {
			/* System Settings service disappeared, throw them away (?) */
			destroy_system_connections (applet_settings);
		}
	}
}

static gboolean
initial_get_connections (gpointer user_data)
{
	AppletDbusSettings *applet_settings = APPLET_DBUS_SETTINGS (user_data);
	AppletDBusManager *dbus_mgr;

	dbus_mgr = applet_dbus_manager_get ();
	if (applet_dbus_manager_name_has_owner (dbus_mgr, NM_DBUS_SERVICE_SYSTEM_SETTINGS))
		query_system_connections (applet_settings);
	g_object_unref (dbus_mgr);

	return FALSE;
}

static void
applet_dbus_settings_init (AppletDbusSettings *applet_settings)
{
	AppletDBusManager *dbus_mgr;

	applet_settings->conf_client = gconf_client_get_default ();
	applet_settings->connections = NULL;
	applet_settings->system_connections = g_hash_table_new_full (g_str_hash,
	                                                             g_str_equal,
	                                                             g_free,
	                                                             g_object_unref);

	gconf_client_add_dir (applet_settings->conf_client,
	                      GCONF_PATH_CONNECTIONS,
	                      GCONF_CLIENT_PRELOAD_NONE,
	                      NULL);

	applet_settings->conf_notify_id =
		gconf_client_notify_add (applet_settings->conf_client,
		                         GCONF_PATH_CONNECTIONS,
		                         (GConfClientNotifyFunc) connections_changed_cb,
		                         applet_settings,
		                         NULL, NULL);

	dbus_mgr = applet_dbus_manager_get ();
	g_signal_connect (dbus_mgr,
	                  "name-owner-changed",
	                  G_CALLBACK (applet_dbus_settings_name_owner_changed),
	                  APPLET_DBUS_SETTINGS (applet_settings));
	g_object_unref (dbus_mgr);

	g_idle_add ((GSourceFunc) initial_get_connections, applet_settings);
}

static void
applet_dbus_settings_finalize (GObject *object)
{
	AppletDbusSettings *applet_settings = (AppletDbusSettings *) object;

	if (applet_settings->conf_notify_id != 0) {
		gconf_client_notify_remove (applet_settings->conf_client, applet_settings->conf_notify_id);
		gconf_client_remove_dir (applet_settings->conf_client, GCONF_PATH_CONNECTIONS, NULL);
		applet_settings->conf_notify_id = 0;
	}

	if (applet_settings->connections) {
		g_slist_foreach (applet_settings->connections, (GFunc) g_object_unref, NULL);
		g_slist_free (applet_settings->connections);
		applet_settings->connections = NULL;
	}

	if (applet_settings->conf_client) {
		g_object_unref (applet_settings->conf_client);
		applet_settings->conf_client = NULL;
	}

	destroy_system_connections (applet_settings);
	if (applet_settings->system_proxy) {
		g_object_unref (applet_settings->system_proxy);
		applet_settings->system_proxy = NULL;
	}

	G_OBJECT_CLASS (applet_dbus_settings_parent_class)->finalize (object);
}

static void
applet_dbus_settings_class_init (AppletDbusSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMSettingsClass *settings_class = NM_SETTINGS_CLASS (klass);

	/* virtual methods */
	object_class->finalize = applet_dbus_settings_finalize;

	settings_class->list_connections = list_connections;

	/* Signals */
	settings_signals[SETTINGS_NEW_SECRETS_REQUESTED] =
		g_signal_new ("new-secrets-requested",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (AppletDbusSettingsClass, new_secrets_requested),
					  NULL, NULL,
					  applet_marshal_VOID__OBJECT_STRING_POINTER_BOOLEAN_POINTER,
					  G_TYPE_NONE, 5,
					  G_TYPE_OBJECT, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN, G_TYPE_POINTER);
}

NMSettings *
applet_dbus_settings_new (void)
{
	NMSettings *settings;
	AppletDBusManager * manager;

	settings = g_object_new (applet_dbus_settings_get_type (), NULL);

	manager = applet_dbus_manager_get ();
	dbus_g_connection_register_g_object (applet_dbus_manager_get_connection (manager),
										 NM_DBUS_PATH_SETTINGS,
										 G_OBJECT (settings));
	g_object_unref (manager);

	return settings;
}

static void
connection_new_secrets_requested_cb (AppletDbusConnectionSettings *applet_connection,
                                     const char *setting_name,
                                     const char **hints,
                                     gboolean ask_user,
                                     DBusGMethodInvocation *context,
                                     gpointer user_data)
{
	AppletDbusSettings *settings = APPLET_DBUS_SETTINGS (user_data);

	/* Re-emit the signal to listeners so they don't have to know about
	 * every single connection
	 */
	g_signal_emit (settings,
	               settings_signals[SETTINGS_NEW_SECRETS_REQUESTED],
	               0,
	               applet_connection,
	               setting_name,
	               hints,
	               ask_user,
	               context);
}

static void
connections_changed_cb (GConfClient *conf_client,
                        guint cnxn_id,
                        GConfEntry *entry,
                        gpointer user_data)
{
	AppletDbusSettings *settings = APPLET_DBUS_SETTINGS (user_data);
	char **dirs = NULL;
	guint len;
	char *path = NULL;
	AppletDbusConnectionSettings *connection;
	gboolean valid = FALSE;

	dirs = g_strsplit (gconf_entry_get_key (entry), "/", -1);
	len = g_strv_length (dirs);
	if (len < 5)
		goto out;

	if (   strcmp (dirs[0], "")
	    || strcmp (dirs[1], "system")
	    || strcmp (dirs[2], "networking")
	    || strcmp (dirs[3], "connections"))
		goto out;

	path = g_strconcat ("/", dirs[1], "/", dirs[2], "/", dirs[3], "/", dirs[4], NULL);
	connection = applet_dbus_settings_get_by_gconf_path (settings, path);

	if (connection == NULL) {
		NMConnectionSettings *exported;

		/* Maybe a new connection */
		exported = applet_dbus_connection_settings_new (settings->conf_client, path);
		if (exported) {
			g_signal_connect (G_OBJECT (exported), "new-secrets-requested",
		                      (GCallback) connection_new_secrets_requested_cb,
		                      settings);
			settings->connections = g_slist_append (settings->connections, exported);
			nm_settings_signal_new_connection (NM_SETTINGS (settings),
			                                   NM_CONNECTION_SETTINGS (exported));
		}
	} else {
		/* Updated or removed connection */
		valid = applet_dbus_connection_settings_changed (connection, entry);
		if (!valid)
			settings->connections = g_slist_remove (settings->connections, connection);
	}

out:
	g_free (path);
	g_strfreev (dirs);
}

AppletDbusConnectionSettings *
applet_dbus_settings_user_get_by_dbus_path (AppletDbusSettings *applet_settings,
                                            const char *path)
{
	GSList *elt;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	for (elt = applet_settings->connections; elt; elt = g_slist_next (elt)) {
		const char * sc_path = nm_connection_settings_get_dbus_object_path (elt->data);
		if (!strcmp (sc_path, path))
			return APPLET_DBUS_CONNECTION_SETTINGS (elt->data);
	}

	return NULL;
}

struct FindSystemInfo {
	NMConnection *connection;
	const char *path;
};

static void
find_system_by_path (gpointer key, gpointer data, gpointer user_data)
{
	struct FindSystemInfo *info = (struct FindSystemInfo *) user_data;
	NMConnection *connection = NM_CONNECTION (data);

	if (!info->path && (info->connection == connection))
		info->path = (const char *) key;
}

const char *
applet_dbus_settings_system_get_dbus_path (AppletDbusSettings *settings,
                                           NMConnection *connection)
{
	struct FindSystemInfo info;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (settings), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	info.connection = connection;
	info.path = NULL;

	g_hash_table_foreach (settings->system_connections, find_system_by_path, &info);
	return info.path;
}

NMConnection *
applet_dbus_settings_system_get_by_dbus_path (AppletDbusSettings *settings,
                                              const char *path)
{
	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (settings), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	return g_hash_table_lookup (settings->system_connections, path);
}

AppletDbusConnectionSettings *
applet_dbus_settings_user_get_by_connection (AppletDbusSettings *applet_settings,
                                             NMConnection *connection)
{
	GSList *elt;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	for (elt = applet_settings->connections; elt; elt = g_slist_next (elt)) {
		NMConnection *list_con;

		list_con = applet_dbus_connection_settings_get_connection (elt->data);
		if (connection == list_con)
			return APPLET_DBUS_CONNECTION_SETTINGS (elt->data);
	}

	return NULL;
}

static AppletDbusConnectionSettings *
applet_dbus_settings_get_by_gconf_path (AppletDbusSettings *applet_settings,
                                        const char *path)
{
	GSList *elt;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	for (elt = applet_settings->connections; elt; elt = g_slist_next (elt)) {
		const char * sc_path = applet_dbus_connection_settings_get_gconf_path (elt->data);
		if (!strcmp (sc_path, path))
			return APPLET_DBUS_CONNECTION_SETTINGS (elt->data);
	}

	return NULL;
}

static GSList *
get_connections (AppletDbusSettings *applet_settings)
{
	GSList *cnc_list = NULL, *conf_list;

	/* get connections from GConf */
	conf_list = nm_gconf_get_all_connections (applet_settings->conf_client);
	if (!conf_list) {
		g_warning ("No connections defined");
		return NULL;
	}

	while (conf_list != NULL) {
		NMConnectionSettings *connection;
		gchar *dir = (gchar *) conf_list->data;

		connection = applet_dbus_connection_settings_new (applet_settings->conf_client, dir);
		if (connection) {
			cnc_list = g_slist_append (cnc_list, connection);
			g_signal_connect (G_OBJECT (connection), "new-secrets-requested",
		                      (GCallback) connection_new_secrets_requested_cb,
		                      applet_settings);
		}

		conf_list = g_slist_remove (conf_list, dir);
		g_free (dir);
	}

	return cnc_list;
}

static void
update_user_connections (AppletDbusSettings *applet_settings)
{
	g_return_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings));

	if (applet_settings->connections)
		return;

	applet_settings->connections = get_connections (applet_settings);
	if (!applet_settings->connections)
		g_warning ("No networks found in the configuration database");
}

GSList *
applet_dbus_settings_list_connections (AppletDbusSettings *applet_settings)
{
	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);

	update_user_connections (applet_settings);

	return applet_settings->connections;
}

static void
add_system_connection (gpointer key, gpointer value, gpointer user_data)
{
	GSList **list = (GSList **) user_data;

	*list = g_slist_append (*list, NM_CONNECTION (value));
}

GSList *
applet_dbus_settings_get_all_connections (AppletDbusSettings *applet_settings)
{
	GSList *connections = NULL, *iter;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);

	g_hash_table_foreach (applet_settings->system_connections,
	                      add_system_connection,
	                      &connections);

	update_user_connections (applet_settings);
	for (iter = applet_settings->connections; iter; iter = g_slist_next (iter)) {
		NMConnectionSettings *cs = NM_CONNECTION_SETTINGS (iter->data);
		NMConnection *con = applet_dbus_connection_settings_get_connection (cs);

		connections = g_slist_append (connections, con);
	}

	return connections;
}

static GPtrArray *
list_connections (NMSettings *settings)
{
	GSList *list;
	GSList *iter;
	GPtrArray *connections;
	AppletDbusSettings *applet_settings = (AppletDbusSettings *) settings;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (settings), NULL);

	list = applet_dbus_settings_list_connections (applet_settings);
	connections = g_ptr_array_sized_new (g_slist_length (list));

	for (iter = list; iter != NULL; iter = iter->next) {
		char * path = g_strdup (nm_connection_settings_get_dbus_object_path (NM_CONNECTION_SETTINGS (iter->data)));

		if (path)
			g_ptr_array_add (connections, (gpointer) path);
	}

	return connections;
}

AppletDbusConnectionSettings *
applet_dbus_settings_user_add_connection (AppletDbusSettings *applet_settings,
                                          NMConnection *connection)
{
	NMConnectionSettings *exported;
	guint32 i = 0;
	char * path = NULL;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	/* Find free GConf directory */
	while (i++ < G_MAXUINT32) {
		char buf[255];

		snprintf (&buf[0], 255, GCONF_PATH_CONNECTIONS"/%d", i);
		if (!gconf_client_dir_exists (applet_settings->conf_client, buf, NULL)) {
			path = g_strdup_printf (buf);
			break;
		}
	};

	if (path == NULL) {
		nm_warning ("Couldn't find free GConf directory for new connection.");
		return NULL;
	}

	exported = applet_dbus_connection_settings_new_from_connection (applet_settings->conf_client,
	                                                                path,
	                                                                connection);
	if (exported) {
		g_signal_connect (G_OBJECT (exported), "new-secrets-requested",
	                      (GCallback) connection_new_secrets_requested_cb,
	                      applet_settings);
		applet_settings->connections = g_slist_append (applet_settings->connections, exported);
		nm_settings_signal_new_connection (NM_SETTINGS (applet_settings),
		                                   NM_CONNECTION_SETTINGS (exported));

		/* Must save connection to GConf _after_ adding it to the connections
		 * list to avoid races with GConf notifications.
		 */
		applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (exported));
	}

	g_free (path);
	return APPLET_DBUS_CONNECTION_SETTINGS (exported);
}


/*
 * AppletDbusConnectionSettings class implementation
 */

static gchar *applet_dbus_connection_settings_get_id (NMConnectionSettings *connection);
static GHashTable *applet_dbus_connection_settings_get_settings (NMConnectionSettings *connection);
static void applet_dbus_connection_settings_get_secrets (NMConnectionSettings *connection,
                                                         const gchar *setting_name,
                                                         const gchar **hints,
                                                         gboolean request_new,
                                                         DBusGMethodInvocation *context);

G_DEFINE_TYPE (AppletDbusConnectionSettings, applet_dbus_connection_settings, NM_TYPE_CONNECTION_SETTINGS)

enum {
	CONNECTION_NEW_SECRETS_REQUESTED,
	CONNECTION_LAST_SIGNAL
};

static guint connection_signals[CONNECTION_LAST_SIGNAL] = { 0 };

static void
applet_dbus_connection_settings_init (AppletDbusConnectionSettings *applet_connection)
{
	applet_connection->conf_client = NULL;
	applet_connection->conf_dir = NULL;
	applet_connection->id = NULL;
	applet_connection->connection = NULL;
}

static void
applet_dbus_connection_settings_finalize (GObject *object)
{
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) object;

	if (applet_connection->conf_client) {
		g_object_unref (applet_connection->conf_client);
		applet_connection->conf_client = NULL;
	}

	if (applet_connection->conf_dir) {
		g_free (applet_connection->conf_dir);
		applet_connection->conf_dir = NULL;
	}

	if (applet_connection->id) {
		g_free (applet_connection->id);
		applet_connection->id = NULL;
	}

	if (applet_connection->connection) {
		g_object_unref (applet_connection->connection);
		applet_connection->connection = NULL;
	}

	G_OBJECT_CLASS (applet_dbus_connection_settings_parent_class)->finalize (object);
}

static void
applet_dbus_connection_settings_class_init (AppletDbusConnectionSettingsClass *applet_connection_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (applet_connection_class);
	NMConnectionSettingsClass *connection_class = NM_CONNECTION_SETTINGS_CLASS (applet_connection_class);

	/* virtual methods */
	object_class->finalize = applet_dbus_connection_settings_finalize;

	connection_class->get_id = applet_dbus_connection_settings_get_id;
	connection_class->get_settings = applet_dbus_connection_settings_get_settings;
	connection_class->get_secrets = applet_dbus_connection_settings_get_secrets;

	/* Signals */
	connection_signals[CONNECTION_NEW_SECRETS_REQUESTED] =
		g_signal_new ("new-secrets-requested",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (AppletDbusConnectionSettingsClass, new_secrets_requested),
					  NULL, NULL,
					  applet_marshal_VOID__STRING_POINTER_BOOLEAN_POINTER,
					  G_TYPE_NONE, 4,
					  G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN, G_TYPE_POINTER);
}

static void
fill_vpn_user_name (NMConnection *connection)
{
	const char *user_name;
	NMSettingVPN *s_vpn;

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	if (!s_vpn)
		return;

	g_free (s_vpn->user_name);
	user_name = g_get_user_name ();
	g_assert (g_utf8_validate (user_name, -1, NULL));
	s_vpn->user_name = g_strdup (user_name);
}

static gboolean
applet_dbus_connection_settings_changed (AppletDbusConnectionSettings *applet_connection,
                                         GConfEntry *entry)
{
	GHashTable *settings;
	NMConnection *connection;
	NMSettingConnection *s_con;

	/* FIXME: just update the modified field, no need to re-read all */
	connection = nm_gconf_read_connection (applet_connection->conf_client,
	                                       applet_connection->conf_dir);
	if (!connection) {
		g_warning ("No connection read from GConf at %s.", applet_connection->conf_dir);
		goto invalid;
	}

	utils_fill_connection_certs (connection);
	if (!nm_connection_verify (connection)) {
		utils_clear_filled_connection_certs (connection);
		g_warning ("Invalid connection read from GConf at %s.", applet_connection->conf_dir);
		goto invalid;
	}
	utils_clear_filled_connection_certs (connection);

	/* Ignore the GConf update if nothing changed */
	if (nm_connection_compare (applet_connection->connection, connection))
		return TRUE;

	if (applet_connection->connection) {
		GHashTable *new_settings;

		new_settings = nm_connection_to_hash (connection);
		nm_connection_replace_settings (applet_connection->connection, new_settings);
		g_object_unref (connection);
	} else
		applet_connection->connection = connection;


	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (applet_connection->connection,
												   NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);
	g_free (applet_connection->id);
	applet_connection->id = g_strdup (s_con->id);

	fill_vpn_user_name (applet_connection->connection);

	utils_fill_connection_certs (applet_connection->connection);
	settings = nm_connection_to_hash (applet_connection->connection);
	utils_clear_filled_connection_certs (applet_connection->connection);

	nm_connection_settings_signal_updated (NM_CONNECTION_SETTINGS (applet_connection), settings);
	g_hash_table_destroy (settings);
	return TRUE;

invalid:
	nm_connection_settings_signal_removed (NM_CONNECTION_SETTINGS (applet_connection));
	return FALSE;
}

NMConnectionSettings *
applet_dbus_connection_settings_new (GConfClient *conf_client, const gchar *conf_dir)
{
	AppletDbusConnectionSettings *applet_connection;
	AppletDBusManager * manager;
	NMSettingConnection *s_con;

	g_return_val_if_fail (conf_client != NULL, NULL);
	g_return_val_if_fail (conf_dir != NULL, NULL);

	applet_connection = g_object_new (APPLET_TYPE_DBUS_CONNECTION_SETTINGS, NULL);
	applet_connection->conf_client = g_object_ref (conf_client);
	applet_connection->conf_dir = g_strdup (conf_dir);

	/* retrieve GConf data */
	applet_connection->connection = nm_gconf_read_connection (conf_client, conf_dir);
	if (!applet_connection->connection) {
		g_warning ("No connection read from GConf at %s.", conf_dir);
		g_object_unref (applet_connection);
		return NULL;
	}

	utils_fill_connection_certs (applet_connection->connection);
	if (!nm_connection_verify (applet_connection->connection)) {
		utils_clear_filled_connection_certs (applet_connection->connection);
		g_warning ("Invalid connection read from GConf at %s.", conf_dir);
		g_object_unref (applet_connection);
		return NULL;
	}
	utils_clear_filled_connection_certs (applet_connection->connection);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (applet_connection->connection,
												   NM_TYPE_SETTING_CONNECTION));
	applet_connection->id = g_strdup (s_con->id);

	fill_vpn_user_name (applet_connection->connection);

	manager = applet_dbus_manager_get ();
	nm_connection_settings_register_object ((NMConnectionSettings *) applet_connection,
	                                        applet_dbus_manager_get_connection (manager));
	g_object_unref (manager);

	return (NMConnectionSettings *) applet_connection;
}

void
applet_dbus_connection_settings_save (NMConnectionSettings *connection)
{
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;

	g_return_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection));

	nm_gconf_write_connection (applet_connection->connection,
	                           applet_connection->conf_client,
	                           applet_connection->conf_dir);
	gconf_client_notify (applet_connection->conf_client, applet_connection->conf_dir);
	gconf_client_suggest_sync (applet_connection->conf_client, NULL);
}

static NMConnectionSettings *
applet_dbus_connection_settings_new_from_connection (GConfClient *conf_client,
                                                     const gchar *conf_dir,
                                                     NMConnection *connection)
{
	AppletDbusConnectionSettings *applet_connection;
	AppletDBusManager * manager;

	g_return_val_if_fail (conf_client != NULL, NULL);
	g_return_val_if_fail (conf_dir != NULL, NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	utils_fill_connection_certs (connection);
	if (!nm_connection_verify (connection)) {
		utils_clear_filled_connection_certs (connection);
		g_warning ("Invalid connection given.");
		return NULL;
	}
	utils_clear_filled_connection_certs (connection);

	applet_connection = g_object_new (APPLET_TYPE_DBUS_CONNECTION_SETTINGS, NULL);
	applet_connection->conf_client = g_object_ref (conf_client);
	applet_connection->conf_dir = g_strdup (conf_dir);
	applet_connection->connection = connection;

	fill_vpn_user_name (applet_connection->connection);

	manager = applet_dbus_manager_get ();
	nm_connection_settings_register_object ((NMConnectionSettings *) applet_connection,
	                                        applet_dbus_manager_get_connection (manager));
	g_object_unref (manager);

	return (NMConnectionSettings *) applet_connection;
}

static gchar *
applet_dbus_connection_settings_get_id (NMConnectionSettings *connection)
{
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;

	g_return_val_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (applet_connection->connection), NULL);

	return g_strdup (applet_connection->id);
}

static const char *
applet_dbus_connection_settings_get_gconf_path (NMConnectionSettings *connection)
{
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;

	g_return_val_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (applet_connection->connection), NULL);

	return applet_connection->conf_dir;
}

NMConnection *
applet_dbus_connection_settings_get_connection (NMConnectionSettings *connection)
{
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;

	g_return_val_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (applet_connection->connection), NULL);

	return applet_connection->connection;
}

static
GHashTable *applet_dbus_connection_settings_get_settings (NMConnectionSettings *connection)
{
	GHashTable *settings;
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;

	g_return_val_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (applet_connection->connection), NULL);

	utils_fill_connection_certs (applet_connection->connection);
	settings = nm_connection_to_hash (applet_connection->connection);
	utils_clear_filled_connection_certs (applet_connection->connection);

	return settings;
}

static GValue *
string_to_gvalue (const char *str)
{
	GValue *val;

	val = g_slice_new0 (GValue);
	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, str);

	return val;
}

static GValue *
byte_array_to_gvalue (const GByteArray *array)
{
	GValue *val;

	val = g_slice_new0 (GValue);
	g_value_init (val, DBUS_TYPE_G_UCHAR_ARRAY);
	g_value_set_boxed (val, array);

	return val;
}

static void
destroy_gvalue (gpointer data)
{
	GValue *value = (GValue *) data;

	g_value_unset (value);
	g_slice_free (GValue, value);
}

static gboolean
get_one_private_key (NMConnection *connection,
                     const char *tag,
                     const char *password,
                     GHashTable *secrets)
{
	GByteArray *array = NULL;
	const char *privkey_tag;
	const char *secret_name;
	gboolean success = FALSE;
	GError *error = NULL;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (tag != NULL, FALSE);
	g_return_val_if_fail (password != NULL, FALSE);

	if (!strcmp (tag, NMA_PRIVATE_KEY_PASSWORD_TAG)) {
		privkey_tag = NMA_PATH_PRIVATE_KEY_TAG;
		secret_name = NM_SETTING_WIRELESS_SECURITY_PRIVATE_KEY;
	} else if (!strcmp (tag, NMA_PHASE2_PRIVATE_KEY_PASSWORD_TAG)) {
		privkey_tag = NMA_PATH_PHASE2_PRIVATE_KEY_TAG;
		secret_name = NM_SETTING_WIRELESS_SECURITY_PHASE2_PRIVATE_KEY;
	} else {
		g_warning ("Unknown private key password type '%s'", tag);
		return FALSE;
	}

	utils_fill_one_crypto_object (connection, privkey_tag, TRUE, password, &array, &error);
	if (error) {
		g_warning ("Couldn't read private key: %s", error->message);
		g_clear_error (&error);
	} else if (!array || !array->len) {
		g_warning ("Couldn't read private key; unknown reason.");
		goto out;
	}

	g_hash_table_insert (secrets,
	                     g_strdup (secret_name),
	                     byte_array_to_gvalue (array));
	success = TRUE;

out:
	if (array) {
		/* Try not to leave the decrypted private key around in memory */
		memset (array->data, 0, array->len);
		g_byte_array_free (array, TRUE);
	}
	return success;
}

static GHashTable *
extract_secrets (NMConnection *connection,
                 GList *found_list,
                 const char *connection_name,
                 const char *setting_name,
                 GError **error)
{
	GHashTable *secrets;
	GList *iter;

	g_return_val_if_fail (setting_name != NULL, NULL);
	g_return_val_if_fail (error != NULL, NULL);
	g_return_val_if_fail (*error == NULL, NULL);

	secrets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, destroy_gvalue);

	for (iter = found_list; iter != NULL; iter = g_list_next (iter)) {
		GnomeKeyringFound *found = (GnomeKeyringFound *) iter->data;
		int i;
		const char * key_name = NULL;

		for (i = 0; i < found->attributes->len; i++) {
			GnomeKeyringAttribute *attr;

			attr = &(gnome_keyring_attribute_list_index (found->attributes, i));
			if (   (strcmp (attr->name, "setting-key") == 0)
			    && (attr->type == GNOME_KEYRING_ATTRIBUTE_TYPE_STRING)) {
				key_name = attr->value.string;
				break;
			}
		}

		if (key_name == NULL) {
			g_set_error (error, NM_SETTINGS_ERROR, 1,
			             "%s.%d - Internal error; keyring item '%s/%s' didn't "
			             "have a 'setting-key' attribute.",
			             __FILE__, __LINE__, connection_name, setting_name);
			break;
		}

		if (   !strcmp (setting_name, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME)
		    && (   !strcmp (key_name, NMA_PRIVATE_KEY_PASSWORD_TAG)
		        || !strcmp (key_name, NMA_PHASE2_PRIVATE_KEY_PASSWORD_TAG))) {
			/* Private key passwords aren't passed to NM, they are used
			 * to decrypt the private key and send _that_ to NM.
			 */
			if (!get_one_private_key (connection, key_name, found->secret, secrets))
				g_warning ("Couldn't retrieve and decrypt private key.");
		} else {
			/* Ignore older obsolete keyring keys that we don't want to leak
			 * through to NM.
			 */
			if (   strcmp (key_name, "private-key-passwd")
			    && strcmp (key_name, "phase2-private-key-passwd")) {
				g_hash_table_insert (secrets,
				                     g_strdup (key_name),
				                     string_to_gvalue (found->secret));
			}
		}
	}

	return secrets;
}

static void
applet_dbus_connection_settings_get_secrets (NMConnectionSettings *connection,
                                             const gchar *setting_name,
                                             const gchar **hints,
                                             gboolean request_new,
                                             DBusGMethodInvocation *context)
{
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;
	GError *error = NULL;
	GHashTable *secrets = NULL;
	GList *found_list = NULL;
	GnomeKeyringResult ret;
	NMSettingConnection *s_con;
	NMSetting *setting;

	g_return_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection));
	g_return_if_fail (NM_IS_CONNECTION (applet_connection->connection));
	g_return_if_fail (setting_name != NULL);

	setting = nm_connection_get_setting_by_name (applet_connection->connection, setting_name);
	if (!setting) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d - Connection didn't have requested setting '%s'.",
		             __FILE__, __LINE__, setting_name);
		g_warning (error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (applet_connection->connection,
												   NM_TYPE_SETTING_CONNECTION));
	if (!s_con || !s_con->id || !strlen (s_con->id) || !s_con->type) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d - Connection didn't have required '"
		             NM_SETTING_CONNECTION_SETTING_NAME
		             "' setting , or the connection name was invalid.",
		             __FILE__, __LINE__);
		g_warning (error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	/* VPN passwords are handled by the VPN plugin's auth dialog */
	if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME))
		goto get_secrets;

	if (request_new) {
		nm_info ("New secrets for %s/%s requested; ask the user",
		         s_con->id, setting_name);
		nm_connection_clear_secrets (applet_connection->connection);
		goto get_secrets;
	}

	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      "connection-name",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      s_con->id,
	                                      "setting-name",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      setting_name,
	                                      NULL);
	if ((ret != GNOME_KEYRING_RESULT_OK) || (g_list_length (found_list) == 0)) {
		nm_info ("No keyring secrets found for %s/%s; ask the user",
		         s_con->id, setting_name);
		goto get_secrets;
	}

	secrets = extract_secrets (applet_connection->connection,
	                           found_list, s_con->id, setting_name, &error);
	if (error) {
		g_warning (error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	} else {
		if (g_hash_table_size (secrets) == 0) {
			g_hash_table_destroy (secrets);
			g_warning ("%s.%d - Secrets were found for setting '%s' but none"
		               " were valid.", __FILE__, __LINE__, setting_name);
			goto get_secrets;
		}
		dbus_g_method_return (context, secrets);
		g_hash_table_destroy (secrets);
	}

	gnome_keyring_found_list_free (found_list);
	return;

get_secrets:
	g_signal_emit (applet_connection,
	               connection_signals[CONNECTION_NEW_SECRETS_REQUESTED],
	               0,
	               setting_name,
	               hints,
	               request_new,
	               context);
}

