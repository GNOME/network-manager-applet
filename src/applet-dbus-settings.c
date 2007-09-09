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

/*
 * AppletDbusSettings class implementation
 */

static GPtrArray *applet_dbus_settings_list_connections (NMSettings *settings);

G_DEFINE_TYPE (AppletDbusSettings, applet_dbus_settings, NM_TYPE_SETTINGS)

static void
applet_dbus_settings_init (AppletDbusSettings *applet_connection)
{
	applet_connection->conf_client = gconf_client_get_default ();
	applet_connection->connections = NULL;
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

	settings_class->list_connections = applet_dbus_settings_list_connections;
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

static GSList *
get_connections (AppletDbusSettings *applet_settings)
{
	GSList *cnc_list = NULL, *conf_list;

	/* get connections from GConf */
	conf_list = gconf_client_all_dirs (applet_settings->conf_client, GCONF_PATH_CONNECTIONS, NULL);
	if (!conf_list) {
		g_warning ("No wireless networks defined");
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

static GPtrArray *
applet_dbus_settings_list_connections (NMSettings *settings)
{
	GPtrArray *connections;
	GSList *iter;
	AppletDbusSettings *applet_settings = (AppletDbusSettings *) settings;

	g_return_val_if_fail (APPLET_IS_DBUS_SETTINGS (settings), NULL);

	if (!applet_settings->connections) {
		applet_settings->connections = get_connections (applet_settings);
		if (!applet_settings->connections) {
			g_warning ("No networks found in the configuration database");
			connections = g_ptr_array_sized_new (0);
			goto out;
		}
	}

	connections = g_ptr_array_sized_new (g_slist_length (applet_settings->connections));
	for (iter = applet_settings->connections; iter != NULL; iter = iter->next) {
		char * path = g_strdup (nm_connection_settings_get_dbus_object_path (NM_CONNECTION_SETTINGS (iter->data)));

		if (path == NULL)
			continue;
		g_ptr_array_add (connections, (gpointer)path);
	}

out:
	return connections;
}

/*
 * AppletDbusConnectionSettings class implementation
 */

static gchar *applet_dbus_connection_settings_get_id (NMConnectionSettings *connection);
static GHashTable *applet_dbus_connection_settings_get_settings (NMConnectionSettings *connection);
static GHashTable *applet_dbus_connection_settings_get_secrets (NMConnectionSettings *connection,
								const gchar *setting_name);

G_DEFINE_TYPE (AppletDbusConnectionSettings, applet_dbus_connection_settings, NM_TYPE_CONNECTION_SETTINGS)

static void
applet_dbus_connection_settings_init (AppletDbusConnectionSettings *applet_connection)
{
	applet_connection->conf_client = NULL;
	applet_connection->conf_dir = NULL;
	applet_connection->conf_notify_id = 0;
	applet_connection->id = NULL;
	applet_connection->connection = nm_connection_new ();
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
read_connection_from_gconf (AppletDbusConnectionSettings *applet_connection)
{
	NMConnection *connection;
	NMSettingConnection *con_setting;
	NMSettingWireless *wireless_setting;

	/* retrieve ID */
	g_free (applet_connection->id);
	applet_connection->id = NULL;
	nm_gconf_get_string_helper (applet_connection->conf_client,
						   applet_connection->conf_dir,
						   "name", "connection",
						   &applet_connection->id);

	/* info settings */
	connection = nm_connection_new ();

	con_setting = (NMSettingConnection *) nm_setting_connection_new ();
	con_setting->name = g_strdup (applet_connection->id);
	nm_gconf_get_string_helper (applet_connection->conf_client,
						   applet_connection->conf_dir,
						   "type", "connection",
						   &con_setting->devtype);
	nm_gconf_get_bool_helper (applet_connection->conf_client,
						 applet_connection->conf_dir,
						 "autoconnect", "connection",
						 &con_setting->autoconnect);

	nm_connection_add_setting (connection, (NMSetting *) con_setting);

	if (!strcmp (con_setting->devtype, "802-11-wireless")) {
		gchar *key;

		/* wireless settings */
		wireless_setting = (NMSettingWireless *) nm_setting_wireless_new ();
		nm_gconf_get_bytearray_helper (applet_connection->conf_client,
								 applet_connection->conf_dir,
								 "ssid", "802-11-wireless",
								 &wireless_setting->ssid);
		nm_gconf_get_string_helper (applet_connection->conf_client,
							   applet_connection->conf_dir,
							   "mode", "802-11-wireless",
							   &wireless_setting->mode);
		nm_gconf_get_string_helper (applet_connection->conf_client,
							   applet_connection->conf_dir,
							   "band", "802-11-wireless",
							   &wireless_setting->band);
		nm_gconf_get_int_helper (applet_connection->conf_client,
							applet_connection->conf_dir,
							"channel", "802-11-wireless",
							&wireless_setting->channel);
		nm_gconf_get_bytearray_helper (applet_connection->conf_client,
								 applet_connection->conf_dir,
								 "bssid", "802-11-wireless",
								 &wireless_setting->bssid);
		nm_gconf_get_int_helper (applet_connection->conf_client,
							applet_connection->conf_dir,
							"rate", "802-11-wireless",
							&wireless_setting->rate);
		nm_gconf_get_int_helper (applet_connection->conf_client,
							applet_connection->conf_dir,
							"tx_power", "802-11-wireless",
							&wireless_setting->tx_power);
		nm_gconf_get_bytearray_helper (applet_connection->conf_client,
								 applet_connection->conf_dir,
								 "mac-address", "802-11-wireless",
								 &wireless_setting->mac_address);
		nm_gconf_get_int_helper (applet_connection->conf_client,
							applet_connection->conf_dir,
							"mtu", "802-11-wireless",
							&wireless_setting->mtu);
		//wireless_setting->seen_bssids = /* FIXME */

		nm_connection_add_setting (connection, (NMSetting *) wireless_setting);

		/* wireless security settings */
		key = g_strdup_printf ("%s/802-11-wireless-security", applet_connection->conf_dir);
		if (gconf_client_dir_exists (applet_connection->conf_client, key, NULL)) {
			NMSettingWirelessSecurity *security_setting;

			wireless_setting->security = g_strdup ("802-11-wireless-security");
			security_setting = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "key-mgmt", "802-11-wireless-security",
								   &security_setting->key_mgmt);
			nm_gconf_get_int_helper (applet_connection->conf_client,
								applet_connection->conf_dir,
								"wep-tx-keyidx", "802-11-wireless-security",
								&security_setting->wep_tx_keyidx);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "auth-alg", "802-11-wireless-security",
								   &security_setting->auth_alg);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "proto", "802-11-wireless-security",
								   &security_setting->proto);
			nm_gconf_get_stringlist_helper (applet_connection->conf_client,
									  applet_connection->conf_dir,
									  "pairwise", "802-11-wireless-security",
									  &security_setting->pairwise);
			nm_gconf_get_stringlist_helper (applet_connection->conf_client,
									  applet_connection->conf_dir,
									  "group", "802-11-wireless-security",
									  &security_setting->group);
			nm_gconf_get_stringlist_helper (applet_connection->conf_client,
									  applet_connection->conf_dir,
									  "eap", "802-11-wireless-security",
									  &security_setting->eap);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "identity", "802-11-wireless-security",
								   &security_setting->identity);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "anonymous-identity", "802-11-wireless-security",
								   &security_setting->anonymous_identity);
			nm_gconf_get_bytearray_helper (applet_connection->conf_client,
									 applet_connection->conf_dir,
									 "ca-cert", "802-11-wireless-security",
									 &security_setting->ca_cert);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "ca-path", "802-11-wireless-security",
								   &security_setting->ca_path);
			nm_gconf_get_bytearray_helper (applet_connection->conf_client,
									 applet_connection->conf_dir,
									 "client-cert", "802-11-wireless-security",
									 &security_setting->client_cert);
			nm_gconf_get_bytearray_helper (applet_connection->conf_client,
									 applet_connection->conf_dir,
									 "private-key", "802-11-wireless-security",
									 &security_setting->private_key);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "phase1-peapver", "802-11-wireless-security",
								   &security_setting->phase1_peapver);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "phase1-peaplabel", "802-11-wireless-security",
								   &security_setting->phase1_peaplabel);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "phase1-fast-provisioning", "802-11-wireless-security",
								   &security_setting->phase1_fast_provisioning);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "phase2-auth", "802-11-wireless-security",
								   &security_setting->phase2_auth);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "phase2-autheap", "802-11-wireless-security",
								   &security_setting->phase2_autheap);
			nm_gconf_get_bytearray_helper (applet_connection->conf_client,
									 applet_connection->conf_dir,
									 "phase2-ca-cert", "802-11-wireless-security",
									 &security_setting->phase2_ca_cert);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "phase2-ca-path", "802-11-wireless-security",
								   &security_setting->phase2_ca_path);
			nm_gconf_get_bytearray_helper (applet_connection->conf_client,
									 applet_connection->conf_dir,
									 "phase2-client-cert", "802-11-wireless-security",
									 &security_setting->phase2_client_cert);
			nm_gconf_get_bytearray_helper (applet_connection->conf_client,
									 applet_connection->conf_dir,
									 "phase2-private-key", "802-11-wireless-security",
									 &security_setting->phase2_private_key);
			nm_gconf_get_string_helper (applet_connection->conf_client,
								   applet_connection->conf_dir,
								   "nai", "802-11-wireless-security",
								   &security_setting->nai);

			nm_connection_add_setting (connection, (NMSetting *) security_setting);
		}

		g_free (key);
	}

	/* remove old settings and use new ones */
	if (applet_connection->connection)
		g_object_unref (applet_connection->connection);
	applet_connection->connection = connection;
}

static void
connection_settings_changed_cb (GConfClient *conf_client, guint cnxn_id, GConfEntry *entry, gpointer user_data)
{
	GHashTable *settings;
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) user_data;

	/* FIXME: just update the modified field, no need to re-read all */
	read_connection_from_gconf (applet_connection);

	settings = nm_connection_to_hash (applet_connection->connection);
	nm_connection_settings_signal_updated (NM_CONNECTION_SETTINGS (applet_connection), settings);
	g_hash_table_destroy (settings);
}

NMConnectionSettings *
applet_dbus_connection_settings_new (GConfClient *conf_client, const gchar *conf_dir)
{
	AppletDbusConnectionSettings *applet_connection;
	AppletDBusManager * manager;

	g_return_val_if_fail (conf_client != NULL, NULL);
	g_return_val_if_fail (conf_dir != NULL, NULL);

	applet_connection = g_object_new (APPLET_TYPE_DBUS_CONNECTION_SETTINGS, NULL);
	applet_connection->conf_client = g_object_ref (conf_client);
	applet_connection->conf_dir = g_strdup (conf_dir);

	/* retrieve GConf data */
	read_connection_from_gconf (applet_connection);

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

	return g_strdup (applet_connection->id);
}

static
GHashTable *applet_dbus_connection_settings_get_settings (NMConnectionSettings *connection)
{
	GHashTable *settings;
	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;

	g_return_val_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection), NULL);

	settings = nm_connection_to_hash (applet_connection->connection);

	return settings;
}

static
GHashTable *applet_dbus_connection_settings_get_secrets (NMConnectionSettings *connection,
											  const gchar *setting_name)
{
	GHashTable *secrets = NULL;
	GList *found_list;
	GnomeKeyringResult ret;
	gchar *setting = NULL;

	AppletDbusConnectionSettings *applet_connection = (AppletDbusConnectionSettings *) connection;

	g_return_val_if_fail (APPLET_IS_DBUS_CONNECTION_SETTINGS (applet_connection), NULL);

	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
								   &found_list,
								   setting_name,
								   GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
								   setting,
								   NULL);
	if (ret == GNOME_KEYRING_RESULT_OK) {
		GList *elt;

		for (elt = found_list; elt != NULL; elt = elt->next) {
			GnomeKeyringFound *found = (GnomeKeyringFound *) elt->data;

			if (!secrets)
				secrets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

			g_hash_table_insert (secrets, g_strdup (setting_name), g_strdup (found->secret));
		}

		gnome_keyring_found_list_free (found_list);
	}

	return secrets;
}
