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
#include "applet.h"
#include "applet-dbus-settings.h"
#include "applet-dbus-manager.h"
#include "gconf-helpers.h"
#include "nm-utils.h"
#include "password-dialog.h"

static NMConnectionSettings * applet_dbus_connection_settings_new_from_connection (GConfClient *conf_client,
                                                                                   const gchar *conf_dir,
                                                                                   NMConnection *connection);

/*
 * AppletDbusSettings class implementation
 */

static GPtrArray *list_connections (NMSettings *settings);

G_DEFINE_TYPE (AppletDbusSettings, applet_dbus_settings, NM_TYPE_SETTINGS)

static void
applet_dbus_settings_init (AppletDbusSettings *applet_settings)
{
	applet_settings->conf_client = gconf_client_get_default ();
	applet_settings->connections = NULL;
}

static void
applet_dbus_settings_finalize (GObject *object)
{
	AppletDbusSettings *applet_settings = (AppletDbusSettings *) object;

	if (applet_settings->conf_client) {
		g_object_unref (applet_settings->conf_client);
		applet_settings->conf_client = NULL;
	}

	if (applet_settings->connections) {
		g_slist_foreach (applet_settings->connections, (GFunc) g_object_unref, NULL);
		g_slist_free (applet_settings->connections);
		applet_settings->connections = NULL;
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

AppletDbusConnectionSettings *
applet_dbus_settings_get_by_dbus_path (AppletDbusSettings *applet_settings,
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

static GSList *
get_connections (AppletDbusSettings *applet_settings)
{
	GSList *cnc_list = NULL, *conf_list;

	/* get connections from GConf */
	conf_list = gconf_client_all_dirs (applet_settings->conf_client, GCONF_PATH_CONNECTIONS, NULL);
	if (!conf_list) {
		g_warning ("No connections defined");
		return NULL;
	}

	while (conf_list != NULL) {
		NMConnectionSettings *connection;
		gchar *dir = (gchar *) conf_list->data;

		connection = applet_dbus_connection_settings_new (applet_settings->conf_client, dir);
		if (connection)
			cnc_list = g_slist_append (cnc_list, connection);

		conf_list = g_slist_remove (conf_list, dir);
		g_free (dir);
	}

	return cnc_list;
}

GSList *
applet_dbus_settings_list_connections (AppletDbusSettings *applet_settings)
{
	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (applet_settings), NULL);

	if (!applet_settings->connections) {
		applet_settings->connections = get_connections (applet_settings);
		if (!applet_settings->connections)
			g_warning ("No networks found in the configuration database");
	}

	return applet_settings->connections;
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
applet_dbus_settings_add_connection (AppletDbusSettings *applet_settings,
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
		applet_settings->connections = g_slist_append (applet_settings->connections, exported);
		nm_settings_signal_new_connection (NM_SETTINGS (applet_settings),
		                                   NM_CONNECTION_SETTINGS (exported));
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
                                                         gboolean request_new,
                                                         DBusGMethodInvocation *context);

G_DEFINE_TYPE (AppletDbusConnectionSettings, applet_dbus_connection_settings, NM_TYPE_CONNECTION_SETTINGS)

static void
applet_dbus_connection_settings_init (AppletDbusConnectionSettings *applet_connection)
{
	applet_connection->conf_client = NULL;
	applet_connection->conf_dir = NULL;
	applet_connection->conf_notify_id = 0;
	applet_connection->id = NULL;
	applet_connection->connection = NULL;
}

static void
applet_dbus_connection_settings_finalize (GObject *object)
{
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) object;

	if (applet_connection->conf_notify_id != 0) {
		gconf_client_notify_remove (applet_connection->conf_client, applet_connection->conf_notify_id);
		gconf_client_remove_dir (applet_connection->conf_client, applet_connection->conf_dir, NULL);
		applet_connection->conf_notify_id = 0;
	}

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
}

static void
connection_settings_changed_cb (GConfClient *conf_client,
                                guint cnxn_id,
                                GConfEntry *entry,
                                gpointer user_data)
{
	GHashTable *settings;
	NMConnection *connection;
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) user_data;
	NMSettingConnection *s_con;

	/* FIXME: just update the modified field, no need to re-read all */
	connection = nm_gconf_read_connection (applet_connection->conf_client,
	                                       applet_connection->conf_dir);
	if (!connection) {
		g_warning ("Invalid connection read from GConf at %s.", applet_connection->conf_dir);
		return;
	}

	s_con = (NMSettingConnection *) nm_connection_get_setting (applet_connection->connection,
	                                                            "connection");
	if (applet_connection->id)
		g_free (applet_connection->id);
	applet_connection->id = g_strdup (s_con->name);

	if (applet_connection->connection)
		g_object_unref (applet_connection->connection);
	applet_connection->connection = connection;

	settings = nm_connection_to_hash (applet_connection->connection);
	nm_connection_settings_signal_updated (NM_CONNECTION_SETTINGS (applet_connection), settings);
	g_hash_table_destroy (settings);
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
		g_warning ("Invalid connection read from GConf at %s.", conf_dir);
		g_object_unref (applet_connection);
		return NULL;
	}

	s_con = (NMSettingConnection *) nm_connection_get_setting (applet_connection->connection,
	                                                            "connection");
	applet_connection->id = g_strdup (s_con->name);

	/* set GConf notifications */
	gconf_client_add_dir (conf_client, conf_dir, GCONF_CLIENT_PRELOAD_NONE, NULL);
	applet_connection->conf_notify_id =
		gconf_client_notify_add (conf_client, conf_dir,
					 (GConfClientNotifyFunc) connection_settings_changed_cb,
					 applet_connection,
					 NULL, NULL);

	manager = applet_dbus_manager_get ();
	nm_connection_settings_register_object ((NMConnectionSettings *) applet_connection,
	                                        applet_dbus_manager_get_connection (manager));
	g_object_unref (manager);

	return (NMConnectionSettings *) applet_connection;
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

	applet_connection = g_object_new (APPLET_TYPE_DBUS_CONNECTION_SETTINGS, NULL);
	applet_connection->conf_client = g_object_ref (conf_client);
	applet_connection->conf_dir = g_strdup (conf_dir);
	applet_connection->connection = connection;

	nm_gconf_write_connection (connection,
	                           applet_connection->conf_client,
	                           applet_connection->conf_dir);
	gconf_client_suggest_sync (applet_connection->conf_client, NULL);

	/* set GConf notifications */
	gconf_client_add_dir (conf_client, conf_dir, GCONF_CLIENT_PRELOAD_NONE, NULL);
	applet_connection->conf_notify_id =
		gconf_client_notify_add (conf_client, conf_dir,
					 (GConfClientNotifyFunc) connection_settings_changed_cb,
					 applet_connection,
					 NULL, NULL);

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

	settings = nm_connection_to_hash (applet_connection->connection);

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

static void
destroy_gvalue (gpointer data)
{
	GValue *value = (GValue *) data;

	g_value_unset (value);
	g_slice_free (GValue, value);
}

static void
get_secrets (NMConnection *connection,
             const char *setting_name,
             DBusGMethodInvocation *context)
{
	GtkWidget *dialog;

	dialog = g_object_get_data (G_OBJECT (connection), "dialog");
	if (!dialog)
		dialog = nma_password_dialog_new (connection, setting_name, context);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void
applet_dbus_connection_settings_get_secrets (NMConnectionSettings *connection,
                                             const gchar *setting_name,
                                             gboolean request_new,
                                             DBusGMethodInvocation *context)
{
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;
	GError *error;
	GHashTable *secrets = NULL;
	GList *found_list = NULL;
	GnomeKeyringResult ret;
	NMSettingConnection *s_con;
	NMSetting *setting;
	GList *elt;

	g_return_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection));
	g_return_if_fail (NM_IS_CONNECTION (applet_connection->connection));
	g_return_if_fail (setting_name != NULL);

	setting = nm_connection_get_setting (applet_connection->connection, setting_name);
	if (!setting) {
		nm_warning ("Connection didn't have requested setting '%s'.", setting_name);
		error = nm_settings_new_error ("%s.%d - Connection didn't have "
		                               "requested setting '%s'.",
		                               __FILE__, __LINE__, setting_name);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	s_con = (NMSettingConnection *) nm_connection_get_setting (applet_connection->connection,
	                                                           "connection");
	if (!s_con || !s_con->name || !strlen (s_con->name) || !s_con->type) {
		nm_warning ("Connection didn't have a valid required '%s' setting, "
		            "or the connection name was invalid.", NM_SETTING_CONNECTION);
		error = nm_settings_new_error ("%s.%d - Connection didn't have required"
		                               " 'connection' setting, or the connection"
		                               " name was invalid.",
		                               __FILE__, __LINE__);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	/* VPN passwords are handled by the VPN plugin's auth dialog */
	if (!strcmp (s_con->type, "vpn")) {
		nma_vpn_request_password (applet_connection->connection, setting_name, request_new, context);
		return;
	}

	if (request_new) {
		nm_info ("New secrets for %s/%s requested; ask the user",
		         s_con->name, setting_name);
		nm_connection_clear_secrets (applet_connection->connection);
		get_secrets (applet_connection->connection, setting_name, context);
		return;
	}

	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      "connection-name",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      s_con->name,
	                                      "setting-name",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      setting_name,
	                                      NULL);
	if ((ret != GNOME_KEYRING_RESULT_OK) || (g_list_length (found_list) == 0)) {
		nm_info ("No keyring secrets found for %s/%s; ask the user",
		         s_con->name, setting_name);
		get_secrets (applet_connection->connection, setting_name, context);
		return;
	}

	for (elt = found_list; elt != NULL; elt = elt->next) {
		GnomeKeyringFound *found = (GnomeKeyringFound *) elt->data;
		int i;
		const char * key_name = NULL;

		if (!secrets)
			secrets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, destroy_gvalue);

		for (i = 0; i < found->attributes->len; i++) {
			GnomeKeyringAttribute *attr;

			attr = &(gnome_keyring_attribute_list_index (found->attributes, i));
			if (   (strcmp (attr->name, "setting-key") == 0)
			    && (attr->type == GNOME_KEYRING_ATTRIBUTE_TYPE_STRING)) {
				key_name = attr->value.string;
				break;
			}
		}

		if (key_name != NULL) {
			g_hash_table_insert (secrets,
			                     g_strdup (key_name),
			                     string_to_gvalue (found->secret));
			dbus_g_method_return (context, secrets);
		} else {
			nm_warning ("Keyring item '%s/%s' didn't have a 'setting-key' attribute.",
			            s_con->name, setting_name);
			error = nm_settings_new_error ("%s.%d - Internal error, couldn't "
			                               " find secret.",
			                               __FILE__, __LINE__);
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		}

		if (secrets)
			g_hash_table_destroy (secrets);
	}

	gnome_keyring_found_list_free (found_list);
}
