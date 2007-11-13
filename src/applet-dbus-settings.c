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

static void
clear_one_byte_array_field (GByteArray **field)
{
	g_return_if_fail (field != NULL);

	if (!*field)
		return;
	g_byte_array_free (*field, TRUE);
	*field = NULL;
}

static void
fill_one_object (NMConnection *connection,
                 const char *key_name,
                 gboolean is_private_key,
                 const char *password,
                 GByteArray **field)
{
	const char *filename;
	GError *error = NULL;
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
		*field = crypto_get_private_key (filename, password, &ignore, &error);
		if (error) {
			g_warning ("Error: could not read private key '%s': %d %s.",
			           filename, error->code, error->message);
			clear_one_byte_array_field (field);
			g_clear_error (&error);
		}
	} else {
		*field = crypto_load_and_verify_certificate (filename, &error);
		if (error) {
			g_warning ("Error: could not read certificate '%s': %d %s.",
			           filename, error->code, error->message);
			clear_one_byte_array_field (field);
			g_clear_error (&error);
		}
	}
}

void
applet_dbus_settings_connection_fill_certs (AppletDbusConnectionSettings *applet_connection)
{
	NMSettingWirelessSecurity *s_wireless_sec;

	g_return_if_fail (applet_connection != NULL);
	g_return_if_fail (applet_connection->connection != NULL);

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (applet_connection->connection, 
															    NM_TYPE_SETTING_WIRELESS_SECURITY));
	if (!s_wireless_sec)
		return;

	fill_one_object (applet_connection->connection,
	                 NMA_PATH_CA_CERT_TAG,
	                 FALSE,
	                 NULL,
	                 &s_wireless_sec->ca_cert);
	fill_one_object (applet_connection->connection,
	                 NMA_PATH_CLIENT_CERT_TAG,
	                 FALSE,
	                 NULL,
	                 &s_wireless_sec->client_cert);
	fill_one_object (applet_connection->connection,
	                 NMA_PATH_PHASE2_CA_CERT_TAG,
	                 FALSE,
	                 NULL,
	                 &s_wireless_sec->phase2_ca_cert);
	fill_one_object (applet_connection->connection,
	                 NMA_PATH_PHASE2_CLIENT_CERT_TAG,
	                 FALSE,
	                 NULL,
	                 &s_wireless_sec->phase2_client_cert);
}

void
applet_dbus_settings_connection_clear_filled_certs (AppletDbusConnectionSettings *applet_connection)
{
	NMSettingWirelessSecurity *s_wireless_sec;

	g_return_if_fail (applet_connection != NULL);
	g_return_if_fail (applet_connection->connection != NULL);

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (applet_connection->connection, 
															    NM_TYPE_SETTING_WIRELESS_SECURITY));
	if (!s_wireless_sec)
		return;

	clear_one_byte_array_field (&s_wireless_sec->ca_cert);
	clear_one_byte_array_field (&s_wireless_sec->client_cert);
	clear_one_byte_array_field (&s_wireless_sec->private_key);
	clear_one_byte_array_field (&s_wireless_sec->phase2_ca_cert);
	clear_one_byte_array_field (&s_wireless_sec->phase2_client_cert);
	clear_one_byte_array_field (&s_wireless_sec->phase2_private_key);
}


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

static void
applet_dbus_settings_init (AppletDbusSettings *applet_settings)
{
	applet_settings->conf_client = gconf_client_get_default ();
	applet_settings->connections = NULL;

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

AppletDbusConnectionSettings *
applet_dbus_settings_get_by_connection (AppletDbusSettings *applet_settings,
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
		g_signal_connect (G_OBJECT (exported), "new-secrets-requested",
	                      (GCallback) connection_new_secrets_requested_cb,
	                      applet_settings);
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

	if (!nm_connection_verify (connection)) {
		g_warning ("Invalid connection read from GConf at %s.", applet_connection->conf_dir);
		goto invalid;
	}

	/* Ignore the GConf update if nothing changed */
	if (nm_connection_compare (applet_connection->connection, connection) == FALSE)
		return TRUE;

	if (applet_connection->connection)
		g_object_unref (applet_connection->connection);
	applet_connection->connection = connection;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (applet_connection->connection,
												   NM_TYPE_SETTING_CONNECTION));
	g_free (applet_connection->id);
	applet_connection->id = g_strdup (s_con->name);

	fill_vpn_user_name (applet_connection->connection);

	applet_dbus_settings_connection_fill_certs (applet_connection);
	settings = nm_connection_to_hash (applet_connection->connection);
	applet_dbus_settings_connection_clear_filled_certs (applet_connection);

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

	if (!nm_connection_verify (applet_connection->connection)) {
		g_warning ("Invalid connection read from GConf at %s.", conf_dir);
		g_object_unref (applet_connection);
		return NULL;
	}

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (applet_connection->connection,
												   NM_TYPE_SETTING_CONNECTION));
	applet_connection->id = g_strdup (s_con->name);

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

	if (!nm_connection_verify (connection)) {
		g_warning ("Invalid connection given.");
		return NULL;
	}

	applet_connection = g_object_new (APPLET_TYPE_DBUS_CONNECTION_SETTINGS, NULL);
	applet_connection->conf_client = g_object_ref (conf_client);
	applet_connection->conf_dir = g_strdup (conf_dir);
	applet_connection->connection = connection;

	applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (applet_connection));

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

	applet_dbus_settings_connection_fill_certs (applet_connection);
	settings = nm_connection_to_hash (applet_connection->connection);
	applet_dbus_settings_connection_clear_filled_certs (applet_connection);

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
	const char *privkey_path;
	GByteArray *array = NULL;
	const char *privkey_tag;
	const char *secret_name;
	gboolean success = FALSE;

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

	fill_one_object (connection, privkey_tag, TRUE, password, &array);
	if (!array || !array->len)
		goto out;

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

	if (g_hash_table_size (secrets) == 0) {
		g_set_error (error, NM_SETTINGS_ERROR, 1,
		             "%s.%d - Secrets were found for setting '%s' but none"
		             " were valid." __FILE__, __LINE__, setting_name);
		g_hash_table_destroy (secrets);
		secrets = NULL;
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
	if (!s_con || !s_con->name || !strlen (s_con->name) || !s_con->type) {
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
		         s_con->name, setting_name);
		nm_connection_clear_secrets (applet_connection->connection);
		goto get_secrets;
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
		goto get_secrets;
	}

	secrets = extract_secrets (applet_connection->connection,
	                           found_list, s_con->name, setting_name, &error);
	if (error) {
		g_warning (error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	} else {
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

