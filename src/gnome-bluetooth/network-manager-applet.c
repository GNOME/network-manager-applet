/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2009  Bastien Nocera <hadess@hadess.net>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <net/ethernet.h>
#include <netinet/ether.h>
#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>
#include <bluetooth-plugin.h>
#include <nm-setting-connection.h>
#include <nm-setting-bluetooth.h>
#include <nm-setting-ip4-config.h>
#include <nm-utils.h>
#include <nma-gconf-settings.h>

static gboolean
has_config_widget (const char *bdaddr, const char **uuids)
{
	guint i;

	for (i = 0; uuids && uuids[i] != NULL; i++) {
		g_message ("has_config_widget %s %s", bdaddr, uuids[i]);
		if (g_str_equal (uuids[i], "PANU"))
			return TRUE;
	}
	return FALSE;
}

static GByteArray *
get_array_from_bdaddr (const char *str)
{
	struct ether_addr *addr;
	GByteArray *array;

	addr = ether_aton (str);
	if (addr) {
		array = g_byte_array_sized_new (ETH_ALEN);
		g_byte_array_append (array, (const guint8 *) addr->ether_addr_octet, ETH_ALEN);
		return array;
	}

	return NULL;
}

static NMExportedConnection *
add_setup (const char *bdaddr)
{
	NMConnection *connection;
	NMSetting *setting, *bt_setting, *ip_setting;
	NMAGConfSettings *gconf_settings;
	NMAGConfConnection *exported;
	GByteArray *mac;
	char *id, *uuid;

	mac = get_array_from_bdaddr (bdaddr);
	if (mac == NULL)
		return NULL;

	/* The connection */
	connection = nm_connection_new ();

	/* The connection settings */
	setting = nm_setting_connection_new ();
	id = g_strdup_printf ("%s %s", bdaddr, "PANU");
	uuid = nm_utils_uuid_generate ();
	g_object_set (G_OBJECT (setting),
	              NM_SETTING_CONNECTION_ID, id,
	              NM_SETTING_CONNECTION_UUID, uuid,
	              NM_SETTING_CONNECTION_TYPE, NM_SETTING_BLUETOOTH_SETTING_NAME,
	              NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
	              NULL);
	g_free (id);
	g_free (uuid);
	nm_connection_add_setting (connection, setting);

	/* The Bluetooth settings */
	bt_setting = nm_setting_bluetooth_new ();
	g_object_set (G_OBJECT (bt_setting),
	              NM_SETTING_BLUETOOTH_BDADDR, mac,
	              NM_SETTING_BLUETOOTH_TYPE, NM_SETTING_BLUETOOTH_TYPE_PANU,
	              NULL);
	g_byte_array_free (mac, TRUE);
	nm_connection_add_setting (connection, bt_setting);

	/* The IPv4 settings */
	ip_setting = nm_setting_ip4_config_new ();
	g_object_set (G_OBJECT (ip_setting),
	              NM_SETTING_IP4_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO,
	              NULL);
	nm_connection_add_setting (connection, ip_setting);

	gconf_settings = nma_gconf_settings_new ();
	exported = nma_gconf_settings_add_connection (gconf_settings, connection);

	if (exported != NULL)
		return NM_EXPORTED_CONNECTION (exported);
	return NULL;
}

static void
button_toggled (GtkToggleButton *button, gpointer user_data)
{
	NMExportedConnection *exported;
	const char *bdaddr;

	bdaddr = g_object_get_data (G_OBJECT (button), "bdaddr");
	g_assert (bdaddr);

	if (gtk_toggle_button_get_active (button) == FALSE) {
		exported = g_object_get_data (G_OBJECT (button), "conn");
		nm_exported_connection_delete (exported, NULL);
		g_object_set_data (G_OBJECT (button), "conn", NULL);
	} else {
		exported = add_setup (bdaddr);
		g_object_set_data (G_OBJECT (button), "conn", exported);
	}
}

static NMExportedConnection *
get_connection_for_bdaddr (const char *bdaddr)
{
	NMExportedConnection *result = NULL;
	NMSettings *settings;
	GSList *list, *l;
	GByteArray *array;

	array = get_array_from_bdaddr (bdaddr);
	if (array == NULL)
		return NULL;

	settings = NM_SETTINGS (nma_gconf_settings_new ());
	list = nm_settings_list_connections (settings);
	for (l = list; l != NULL; l = l->next) {
		NMExportedConnection *exported = l->data;
		NMConnection *conn = nm_exported_connection_get_connection (exported);
		NMSetting *setting;
		const char *type;
		const GByteArray *addr;

		setting = nm_connection_get_setting_by_name (conn, NM_SETTING_BLUETOOTH_SETTING_NAME);
		if (setting == NULL)
			continue;
		type = nm_setting_bluetooth_get_connection_type (NM_SETTING_BLUETOOTH (setting));
		if (g_strcmp0 (type, NM_SETTING_BLUETOOTH_TYPE_PANU) != 0)
			continue;
		addr = nm_setting_bluetooth_get_bdaddr (NM_SETTING_BLUETOOTH (setting));
		if (addr == NULL || memcmp (addr->data, array->data, addr->len) != 0)
			continue;
		result = exported;
		break;
	}
	g_slist_free (list);

	return result;
}

static GtkWidget *
get_config_widgets (const char *bdaddr, const char **uuids)
{
	GtkWidget *button;
	NMExportedConnection *conn;

	button = gtk_check_button_new_with_label (_("Access the Internet using your mobile phone"));
	g_object_set_data_full (G_OBJECT (button),
	                        "bdaddr", g_strdup (bdaddr),
	                        (GDestroyNotify) g_free);
	conn = get_connection_for_bdaddr (bdaddr);
	if (conn != NULL) {
		g_object_set_data (G_OBJECT (button), "conn", conn);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
	}
	g_signal_connect (G_OBJECT (button), "toggled", G_CALLBACK (button_toggled), NULL);

	return button;
}

static void
device_removed (const char *bdaddr)
{
	NMExportedConnection *exported;

	g_message ("Device '%s' got removed", bdaddr);

	// FIXME: don't just delete any random PAN conenction for this
	// bdaddr, actually delete the one this plugin created
	exported = get_connection_for_bdaddr (bdaddr);
	if (exported)
		nm_exported_connection_delete (exported, NULL);
}

static GbtPluginInfo plugin_info = {
	"network-manager-applet",
	has_config_widget,
	get_config_widgets,
	device_removed
};

GBT_INIT_PLUGIN(plugin_info)

