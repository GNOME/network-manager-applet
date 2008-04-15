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
#include <nm-setting-wireless-security.h>
#include <nm-setting-8021x.h>

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

static AppletExportedConnection * applet_exported_connection_new_from_connection (GConfClient *conf_client,
                                                                                  const gchar *conf_dir,
                                                                                  NMConnection *connection);

static void connections_changed_cb (GConfClient *conf_client,
                                    guint cnxn_id,
                                    GConfEntry *entry,
                                    gpointer user_data);

static const char *applet_exported_connection_get_gconf_path (AppletExportedConnection *exported);

static gboolean applet_exported_connection_changed (AppletExportedConnection *connection,
                                                    GConfEntry *entry);


enum {
	SETTINGS_NEW_SECRETS_REQUESTED,
	SETTINGS_SYSTEM_SETTINGS_CHANGED,

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

		nm_connection_set_scope (connection, NM_CONNECTION_SCOPE_SYSTEM);
		nm_connection_set_path (connection, path);

		g_object_set_data_full (G_OBJECT (connection),
		                        APPLET_CONNECTION_PROXY_TAG,
		                        proxy,
		                        (GDestroyNotify) g_object_unref);
		g_hash_table_insert (applet_settings->system_connections,
		                     g_strdup (path),
		                     connection);

		g_signal_emit (applet_settings,
		               settings_signals[SETTINGS_SYSTEM_SETTINGS_CHANGED],
		               0);
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

	g_signal_emit (applet_settings,
	               settings_signals[SETTINGS_SYSTEM_SETTINGS_CHANGED],
	               0);
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

	g_signal_emit (applet_settings,
	               settings_signals[SETTINGS_SYSTEM_SETTINGS_CHANGED],
	               0);
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

	settings_signals[SETTINGS_SYSTEM_SETTINGS_CHANGED] =
		g_signal_new ("system-settings-changed",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (AppletDbusSettingsClass, system_settings_changed),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__VOID,
					  G_TYPE_NONE, 0);
}

AppletDbusSettings *
applet_dbus_settings_new (void)
{
	AppletDbusSettings *settings;
	AppletDBusManager * manager;

	settings = APPLET_DBUS_SETTINGS (g_object_new (applet_dbus_settings_get_type (), NULL));

	manager = applet_dbus_manager_get ();
	dbus_g_connection_register_g_object (applet_dbus_manager_get_connection (manager),
										 NM_DBUS_PATH_SETTINGS,
										 G_OBJECT (settings));
	g_object_unref (manager);

	return settings;
}

static void
connection_new_secrets_requested_cb (AppletExportedConnection *exported,
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
	               exported,
	               setting_name,
	               hints,
	               ask_user,
	               context);
}

static AppletExportedConnection *
applet_dbus_settings_get_by_gconf_path (AppletDbusSettings *applet_settings,
                                        const char *path)
{
	GSList *elt;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	for (elt = applet_settings->connections; elt; elt = g_slist_next (elt)) {
		AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (elt->data);
		const char *gconf_path;

		gconf_path = applet_exported_connection_get_gconf_path (exported);
		if (gconf_path && !strcmp (gconf_path, path))
			return exported;
	}

	return NULL;
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
	AppletExportedConnection *exported;
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
	exported = applet_dbus_settings_get_by_gconf_path (settings, path);
	if (exported == NULL) {
		/* Maybe a new connection */
		exported = applet_exported_connection_new (settings->conf_client, path);
		if (exported) {
			g_signal_connect (G_OBJECT (exported), "new-secrets-requested",
		                      (GCallback) connection_new_secrets_requested_cb,
		                      settings);
			settings->connections = g_slist_append (settings->connections, exported);
			nm_settings_signal_new_connection (NM_SETTINGS (settings),
			                                   NM_EXPORTED_CONNECTION (exported));
		}
	} else {
		/* Updated or removed connection */
		valid = applet_exported_connection_changed (exported, entry);
		if (!valid)
			settings->connections = g_slist_remove (settings->connections, exported);
	}

out:
	g_free (path);
	g_strfreev (dirs);
}

AppletExportedConnection *
applet_dbus_settings_user_get_by_dbus_path (AppletDbusSettings *applet_settings,
                                            const char *path)
{
	GSList *elt;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	for (elt = applet_settings->connections; elt; elt = g_slist_next (elt)) {
		AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (elt->data);
		NMConnection *connection;
		const char *sc_path;

		connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
		sc_path = nm_connection_get_path (connection);
		if (!sc_path) {
			g_warning ("%s: connection in exported list didn't have a D-Bus path.", __func__);
			continue;
		}

		if (!strcmp (sc_path, path))
			return exported;
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

AppletExportedConnection *
applet_dbus_settings_user_get_by_connection (AppletDbusSettings *applet_settings,
                                             NMConnection *connection)
{
	GSList *elt;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	for (elt = applet_settings->connections; elt; elt = g_slist_next (elt)) {
		AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (elt->data);
		NMConnection *list_con;

		list_con = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
		if (connection == list_con)
			return exported;
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
		AppletExportedConnection *exported;
		gchar *dir = (gchar *) conf_list->data;

		exported = applet_exported_connection_new (applet_settings->conf_client, dir);
		if (exported) {
			cnc_list = g_slist_append (cnc_list, exported);
			g_signal_connect (G_OBJECT (exported), "new-secrets-requested",
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
		AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (iter->data);
		NMConnection *connection;

		connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
		connections = g_slist_append (connections, connection);
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
		AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (iter->data);
		NMConnection *connection;
		char *path;

		connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
		path = g_strdup (nm_connection_get_path (connection));
		if (path)
			g_ptr_array_add (connections, (gpointer) path);
	}

	return connections;
}

AppletExportedConnection *
applet_dbus_settings_user_add_connection (AppletDbusSettings *applet_settings,
                                          NMConnection *connection)
{
	AppletExportedConnection *exported;
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

	exported = applet_exported_connection_new_from_connection (applet_settings->conf_client,
	                                                           path,
	                                                           connection);
	g_free (path);
	if (!exported)
		return NULL;

	g_signal_connect (G_OBJECT (exported), "new-secrets-requested",
                      (GCallback) connection_new_secrets_requested_cb,
                      applet_settings);
	applet_settings->connections = g_slist_append (applet_settings->connections, exported);
	nm_settings_signal_new_connection (NM_SETTINGS (applet_settings),
	                                   NM_EXPORTED_CONNECTION (exported));

	/* Must save connection to GConf _after_ adding it to the connections
	 * list to avoid races with GConf notifications.
	 */
	applet_exported_connection_save (exported);

	return exported;
}


/*
 * AppletExportedConnection class implementation
 */

static GHashTable *applet_exported_connection_get_settings (NMExportedConnection *connection);
static void applet_exported_connection_get_secrets (NMExportedConnection *connection,
                                                    const gchar *setting_name,
                                                    const gchar **hints,
                                                    gboolean request_new,
                                                    DBusGMethodInvocation *context);

G_DEFINE_TYPE (AppletExportedConnection, applet_exported_connection, NM_TYPE_EXPORTED_CONNECTION)

enum {
	CONNECTION_NEW_SECRETS_REQUESTED,
	CONNECTION_LAST_SIGNAL
};

static guint connection_signals[CONNECTION_LAST_SIGNAL] = { 0 };

static void
applet_exported_connection_init (AppletExportedConnection *exported)
{
	exported->conf_client = NULL;
	exported->conf_dir = NULL;
}

static void
applet_exported_connection_dispose (GObject *object)
{
	AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (object);
	NMConnection *connection;

	if (exported->conf_client) {
		g_object_unref (exported->conf_client);
		exported->conf_client = NULL;
	}

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	if (connection)
		g_object_set_data (G_OBJECT (connection), NMA_CONNECTION_ID_TAG, NULL);

	G_OBJECT_CLASS (applet_exported_connection_parent_class)->dispose (object);
}

static void
applet_exported_connection_finalize (GObject *object)
{
	AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (object);

	g_free (exported->conf_dir);
	exported->conf_dir = NULL;

	g_free (exported->id);
	exported->id = NULL;

	G_OBJECT_CLASS (applet_exported_connection_parent_class)->finalize (object);
}

static const char *
real_get_id (NMExportedConnection *connection)
{
	AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (connection);

	return exported->id;
}

static void
applet_exported_connection_class_init (AppletExportedConnectionClass *exported_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (exported_class);
	NMExportedConnectionClass *connection_class = NM_EXPORTED_CONNECTION_CLASS (exported_class);

	/* virtual methods */
	object_class->dispose = applet_exported_connection_dispose;
	object_class->finalize = applet_exported_connection_finalize;

	connection_class->get_settings = applet_exported_connection_get_settings;
	connection_class->get_secrets = applet_exported_connection_get_secrets;
	connection_class->get_id = real_get_id;

	/* Signals */
	connection_signals[CONNECTION_NEW_SECRETS_REQUESTED] =
		g_signal_new ("new-secrets-requested",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (AppletExportedConnectionClass, new_secrets_requested),
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
applet_exported_connection_changed (AppletExportedConnection *exported,
                                    GConfEntry *entry)
{
	GHashTable *settings;
	NMConnection *wrapped_connection;
	NMConnection *gconf_connection;
	GHashTable *new_settings;

	wrapped_connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	if (!wrapped_connection) {
		g_warning ("Exported connection for '%s' didn't wrap a connection.", exported->conf_dir);
		goto invalid;
	}

	/* FIXME: just update the modified field, no need to re-read all */
	gconf_connection = nm_gconf_read_connection (exported->conf_client,
	                                             exported->conf_dir);
	if (!gconf_connection) {
		g_warning ("No connection read from GConf at %s.", exported->conf_dir);
		goto invalid;
	}

	utils_fill_connection_certs (gconf_connection);
	if (!nm_connection_verify (gconf_connection)) {
		utils_clear_filled_connection_certs (gconf_connection);
		g_warning ("Invalid connection read from GConf at %s.", exported->conf_dir);
		goto invalid;
	}
	utils_clear_filled_connection_certs (gconf_connection);

	/* Ignore the GConf update if nothing changed */
	if (nm_connection_compare (wrapped_connection, gconf_connection, COMPARE_FLAGS_EXACT))
		return TRUE;

	new_settings = nm_connection_to_hash (gconf_connection);
	nm_connection_replace_settings (wrapped_connection, new_settings);
	g_object_unref (gconf_connection);

	fill_vpn_user_name (wrapped_connection);

	utils_fill_connection_certs (wrapped_connection);
	settings = nm_connection_to_hash (wrapped_connection);
	utils_clear_filled_connection_certs (wrapped_connection);

	nm_exported_connection_signal_updated (NM_EXPORTED_CONNECTION (exported), settings);
	g_hash_table_destroy (settings);
	return TRUE;

invalid:
	nm_exported_connection_signal_removed (NM_EXPORTED_CONNECTION (exported));
	return FALSE;
}

AppletExportedConnection *
applet_exported_connection_new (GConfClient *conf_client, const gchar *conf_dir)
{
	AppletExportedConnection *exported;
	AppletDBusManager *manager;
	NMConnection *gconf_connection = NULL;

	g_return_val_if_fail (conf_client != NULL, NULL);
	g_return_val_if_fail (conf_dir != NULL, NULL);

	/* retrieve GConf data */
	gconf_connection = nm_gconf_read_connection (conf_client, conf_dir);
	if (!gconf_connection) {
		g_warning ("No connection read from GConf at %s.", conf_dir);
		return NULL;
	}

	exported = g_object_new (APPLET_TYPE_EXPORTED_CONNECTION,
	                         NM_EXPORTED_CONNECTION_CONNECTION, gconf_connection,
	                         NULL);
	exported->conf_client = g_object_ref (conf_client);
	exported->conf_dir = g_strdup (conf_dir);
	exported->id = g_path_get_basename (conf_dir);
	g_object_set_data (G_OBJECT (gconf_connection),
	                   NMA_CONNECTION_ID_TAG, exported->id);

	utils_fill_connection_certs (gconf_connection);
	if (!nm_connection_verify (gconf_connection)) {
		utils_clear_filled_connection_certs (gconf_connection);
		g_warning ("Invalid connection read from GConf at %s.", conf_dir);
		g_object_unref (exported);
		exported = NULL;
		goto out;
	}
	utils_clear_filled_connection_certs (gconf_connection);

	fill_vpn_user_name (gconf_connection);

	manager = applet_dbus_manager_get ();
	nm_exported_connection_register_object (NM_EXPORTED_CONNECTION (exported),
	                                        NM_CONNECTION_SCOPE_USER,
	                                        applet_dbus_manager_get_connection (manager));
	g_object_unref (manager);

out:
	g_object_unref (gconf_connection);
	return exported;
}

void
applet_exported_connection_save (AppletExportedConnection *exported)
{
	NMConnection *connection;

	g_return_if_fail (APPLET_IS_EXPORTED_CONNECTION (exported));

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	nm_gconf_write_connection (connection,
	                           exported->conf_client,
	                           exported->conf_dir,
	                           exported->id);
	gconf_client_notify (exported->conf_client, exported->conf_dir);
	gconf_client_suggest_sync (exported->conf_client, NULL);
}

static AppletExportedConnection *
applet_exported_connection_new_from_connection (GConfClient *conf_client,
                                                const gchar *conf_dir,
                                                NMConnection *connection)
{
	AppletExportedConnection *exported;
	AppletDBusManager *manager;

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

	exported = g_object_new (APPLET_TYPE_EXPORTED_CONNECTION,
	                         NM_EXPORTED_CONNECTION_CONNECTION, connection,
	                         NULL);
	exported->conf_client = g_object_ref (conf_client);
	exported->conf_dir = g_strdup (conf_dir);
	exported->id = g_path_get_basename (conf_dir);
	g_object_set_data (G_OBJECT (connection),
	                   NMA_CONNECTION_ID_TAG, exported->id);

	fill_vpn_user_name (connection);

	manager = applet_dbus_manager_get ();
	nm_exported_connection_register_object (NM_EXPORTED_CONNECTION (exported),
	                                        NM_CONNECTION_SCOPE_USER,
	                                        applet_dbus_manager_get_connection (manager));
	g_object_unref (manager);

	return exported;
}

static const char *
applet_exported_connection_get_gconf_path (AppletExportedConnection *exported)
{
	NMConnection *connection;

	g_return_val_if_fail (APPLET_IS_EXPORTED_CONNECTION (exported), NULL);

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	return exported->conf_dir;
}

static GHashTable *
applet_exported_connection_get_settings (NMExportedConnection *parent)
{
	AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (parent);
	NMConnection *connection;
	GHashTable *settings;

	g_return_val_if_fail (APPLET_IS_EXPORTED_CONNECTION (exported), NULL);

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	utils_fill_connection_certs (connection);
	settings = nm_connection_to_hash (connection);
	utils_clear_filled_connection_certs (connection);

	return settings;
}

static void
applet_exported_connection_get_secrets (NMExportedConnection *parent,
                                        const gchar *setting_name,
                                        const gchar **hints,
                                        gboolean request_new,
                                        DBusGMethodInvocation *context)
{
	AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (parent);
	NMConnection *connection;
	GError *error = NULL;
	GHashTable *settings = NULL;
	GHashTable *secrets = NULL;
	NMSettingConnection *s_con;
	NMSetting *setting;
	const char *id = NULL;

	g_return_if_fail (APPLET_IS_EXPORTED_CONNECTION (exported));
	g_return_if_fail (setting_name != NULL);

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	g_return_if_fail (NM_IS_CONNECTION (connection));

	setting = nm_connection_get_setting_by_name (connection, setting_name);
	if (!setting) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d - Connection didn't have requested setting '%s'.",
		             __FILE__, __LINE__, setting_name);
		g_warning (error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
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
		nm_connection_clear_secrets (connection);
		goto get_secrets;
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                  g_free, (GDestroyNotify) g_hash_table_destroy);

	id = nm_exported_connection_get_id (parent);
	secrets = nm_gconf_get_keyring_items (connection, id, setting_name, FALSE, &error);
	if (!secrets) {
		if (error) {
			nm_warning ("Error getting secrets: %s", error->message);
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else {
			nm_info ("No keyring secrets found for %s/%s; asking user.",
			         s_con->id, setting_name);
			goto get_secrets;
		}
	} else {
		if (g_hash_table_size (secrets) == 0) {
			g_hash_table_destroy (secrets);
			g_warning ("%s.%d - Secrets were found for setting '%s' but none"
		               " were valid.", __FILE__, __LINE__, setting_name);
			goto get_secrets;
		} else {
			g_hash_table_insert (settings, g_strdup (setting_name), secrets);
			dbus_g_method_return (context, settings);
		}
	}

	g_hash_table_destroy (settings);
	return;

get_secrets:
	g_signal_emit (exported,
	               connection_signals[CONNECTION_NEW_SECRETS_REQUESTED],
	               0,
	               setting_name,
	               hints,
	               request_new,
	               context);
}

