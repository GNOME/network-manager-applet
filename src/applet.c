/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * This applet used the GNOME Wireless Applet as a skeleton to build from.
 *
 * GNOME Wireless Applet Authors:
 *		Eskil Heyn Olsen <eskil@eskil.dk>
 *		Bastien Nocera <hadess@hadess.net> (Gnome2 port)
 *
 * (C) Copyright 2004-2008 Red Hat, Inc.
 * (C) Copyright 2001, 2002 Free Software Foundation
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <time.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "wireless-helper.h"
#include <unistd.h>
#include <sys/socket.h>

#include <NetworkManagerVPN.h>
#include <nm-device-802-3-ethernet.h>
#include <nm-device-802-11-wireless.h>
#include <nm-gsm-device.h>
#include <nm-cdma-device.h>
#include <nm-utils.h>
#include <nm-connection.h>
#include <nm-vpn-connection.h>
#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>
#include <nm-setting-vpn-properties.h>

#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <gnome-keyring.h>
#include <libnotify/notify.h>

#include "applet.h"
#include "applet-device-wired.h"
#include "applet-device-wireless.h"
#include "applet-device-gsm.h"
#include "applet-device-cdma.h"
#include "applet-dialogs.h"
#include "vpn-password-dialog.h"
#include "applet-dbus-manager.h"
#include "utils.h"
#include "crypto.h"
#include "gconf-helpers.h"
#include "vpn-connection-info.h"


G_DEFINE_TYPE(NMApplet, nma, G_TYPE_OBJECT)

NMDevice *
applet_get_first_active_device (NMApplet *applet)
{
	GSList *iter;
	NMDevice *dev = NULL;

	if (g_slist_length (applet->active_connections) == 0)
		return NULL;

	for (iter = applet->active_connections; iter; iter = g_slist_next (iter)) {
		NMClientActiveConnection * act_con = (NMClientActiveConnection *) iter->data;

		if (act_con->devices)
			return g_slist_nth_data (act_con->devices, 0);
	}

	return dev;
}

static inline NMADeviceClass *
get_device_class (NMDevice *device, NMApplet *applet)
{
	g_return_val_if_fail (device != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		return applet->wired_class;
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		return applet->wireless_class;
	else if (NM_IS_GSM_DEVICE (device))
		return applet->gsm_class;
	else if (NM_IS_CDMA_DEVICE (device))
		return applet->cdma_class;
	else
		g_message ("%s: Unknown device type '%s'", __func__, G_OBJECT_TYPE_NAME (device));
	return NULL;
}

static void
activate_device_cb (gpointer user_data, GError *err)
{
	if (err)
		nm_warning ("Device Activation failed: %s", err->message);
}

void
applet_menu_item_activate_helper (NMDevice *device,
                                  NMConnection *connection,
                                  const char *specific_object,
                                  NMApplet *applet,
                                  gpointer user_data)
{
	AppletDbusSettings *applet_settings = APPLET_DBUS_SETTINGS (applet->settings);
	AppletExportedConnection *exported = NULL;
	char *con_path = NULL;
	gboolean is_system = FALSE;

	g_return_if_fail (NM_IS_DEVICE (device));

	if (connection) {
		exported = applet_dbus_settings_user_get_by_connection (applet_settings, connection);
		if (exported) {
			con_path = (char *) nm_connection_get_path (connection);
		} else {
			con_path = (char *) applet_dbus_settings_system_get_dbus_path (applet_settings, connection);
			if (con_path)
				is_system = TRUE;
			else
				return;
		}
	} else {
		NMADeviceClass *dclass = get_device_class (device, applet);

		/* If no connection was given, create a new default connection for this
		 * device type.
		 */
		g_assert (dclass);
		connection = dclass->new_auto_connection (device, applet, user_data);
		if (!connection) {
			nm_warning ("Couldn't create default connection.");
			return;
		}

		exported = applet_dbus_settings_user_add_connection (applet_settings, connection);
		if (!exported) {
			/* If the setting isn't valid, because it needs more authentication
			 * or something, ask the user for it.
			 */

			nm_warning ("Invalid connection; asking for more information.");
			if (dclass->get_more_info)
				dclass->get_more_info (device, connection, applet, user_data);
			g_object_unref (connection);
			return;
		}

		con_path = (char *) nm_connection_get_path (connection);
		g_object_unref (connection);
	}

	g_assert (con_path);

	/* Finally, tell NM to activate the connection */
	nm_client_activate_device (applet->nm_client,
	                           device,
	                           is_system ? NM_DBUS_SERVICE_SYSTEM_SETTINGS : NM_DBUS_SERVICE_USER_SETTINGS,
	                           con_path,
	                           specific_object,
	                           activate_device_cb,
	                           user_data);
}

void
applet_do_notify (NMApplet *applet, 
                  NotifyUrgency urgency,
                  const char *summary,
                  const char *message,
                  const char *icon)
{
	const char *notify_icon;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (summary != NULL);
	g_return_if_fail (message != NULL);

#if GTK_CHECK_VERSION(2, 11, 0)
	if (!gtk_status_icon_is_embedded (applet->status_icon))
		return;
#endif

	if (!notify_is_initted ())
		notify_init ("NetworkManager");

	if (applet->notification != NULL) {
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}

	notify_icon = icon ? icon : GTK_STOCK_NETWORK;

	applet->notification = notify_notification_new_with_status_icon (summary, message, notify_icon, applet->status_icon);

	notify_notification_set_urgency (applet->notification, urgency);
	notify_notification_show (applet->notification, NULL);
}

static gboolean
animation_timeout (gpointer data)
{
	applet_schedule_update_icon (NM_APPLET (data));
	return TRUE;
}

static void
start_animation_timeout (NMApplet *applet)
{
	if (applet->animation_id == 0) {
		applet->animation_step = 0;
		applet->animation_id = g_timeout_add (100, animation_timeout, applet);
	}
}

static void
clear_animation_timeout (NMApplet *applet)
{
	if (applet->animation_id) {
		g_source_remove (applet->animation_id);
		applet->animation_id = 0;
		applet->animation_step = 0;
	}
}

static void
update_connection_timestamp (NMApplet *applet, NMDevice *device, NMVPNConnection *vpn)
{
	AppletExportedConnection *exported = NULL;
	NMConnection *connection;
	NMSettingConnection *s_con;
	const char *path;

	if (device)
		exported = applet_get_exported_connection_for_device (device, applet);
	else if (vpn) {
		path = g_object_get_data (G_OBJECT (vpn), "dbus-path");
		exported = applet_dbus_settings_user_get_by_dbus_path (applet->settings, path);
	}

	if (!exported)
		return;

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	g_assert (connection);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	s_con->timestamp = (guint64) time (NULL);
	applet_exported_connection_save (exported);
}

static void
vpn_connection_state_changed (NMVPNConnection *connection,
                              NMVPNConnectionState state,
                              NMVPNConnectionStateReason reason,
                              gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	const char *banner;
	char *title, *msg;

	switch (state) {
	case NM_VPN_CONNECTION_STATE_PREPARE:
	case NM_VPN_CONNECTION_STATE_NEED_AUTH:
	case NM_VPN_CONNECTION_STATE_CONNECT:
	case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
		start_animation_timeout (applet);
		break;
	case NM_VPN_CONNECTION_STATE_ACTIVATED:
		banner = nm_vpn_connection_get_banner (connection);
		if (banner && strlen (banner)) {
			title = _("VPN Login Message");
			msg = g_strdup_printf ("\n%s", banner);
			applet_do_notify (applet, NOTIFY_URGENCY_LOW, title, msg, "gnome-lockscreen");
			g_free (msg);
		}
		update_connection_timestamp (applet, NULL, connection);
		clear_animation_timeout (applet);
		break;
	case NM_VPN_CONNECTION_STATE_FAILED:
	case NM_VPN_CONNECTION_STATE_DISCONNECTED:
	default:
		g_hash_table_remove (applet->vpn_connections, nm_vpn_connection_get_name (connection));
		clear_animation_timeout (applet);
		break;
	}

	applet_schedule_update_icon (applet);
}

static const char *
get_connection_id (AppletExportedConnection *exported)
{
	NMSettingConnection *conn;
	NMConnection *connection;

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	g_return_val_if_fail (connection != NULL, NULL);

	conn = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_return_val_if_fail (conn != NULL, NULL);

	return conn->id;
}

static void
add_one_vpn_connection (NMVPNConnection *connection,
                        const char *path,
                        NMApplet *applet)
{
	const char *name;

	/* If the connection didn't have a name, it failed or something.
	 * Should probably have better behavior here; the applet shouldn't depend
	 * on the name itself to match the internal VPN Connection from GConf to
	 * the VPN connection exported by NM.
	 */
	name = nm_vpn_connection_get_name (connection);
	g_return_if_fail (name != NULL);

	g_signal_connect (connection, "state-changed",
	                  G_CALLBACK (vpn_connection_state_changed),
	                  applet);

	g_hash_table_insert (applet->vpn_connections,
	                     g_strdup (name),
	                     connection);

	g_object_set_data_full (G_OBJECT (connection),
	                        "dbus-path", g_strdup (path), (GDestroyNotify) g_free);
}

static void
nma_menu_vpn_item_clicked (GtkMenuItem *item, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMExportedConnection *exported;
	NMConnection *wrapped;
	NMVPNConnection *connection;
	const char *connection_name;
	NMDevice *device;

	exported = NM_EXPORTED_CONNECTION (g_object_get_data (G_OBJECT (item), "connection"));
	g_assert (exported);

	connection_name = get_connection_id (APPLET_EXPORTED_CONNECTION (exported));

	connection = (NMVPNConnection *) g_hash_table_lookup (applet->vpn_connections, connection_name);
	if (connection)
		/* Connection already active; do nothing */
		return;

	/* Connection inactive, activate */
	device = applet_get_first_active_device (applet);
	wrapped = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	connection = nm_vpn_manager_connect (applet->vpn_manager,
								  NM_DBUS_SERVICE_USER_SETTINGS,
								  nm_connection_get_path (wrapped),
								  device);
	if (connection) {
		add_one_vpn_connection (connection, nm_connection_get_path (wrapped), applet);
	} else {
		/* FIXME: show a dialog or something */
		g_warning ("Can't connect");
	}
		
	applet_schedule_update_icon (applet);
//	nmi_dbus_signal_user_interface_activated (applet->connection);
}


/*
 * nma_menu_configure_vpn_item_activate
 *
 * Signal function called when user clicks "Configure VPN..."
 *
 */
static void
nma_menu_configure_vpn_item_activate (GtkMenuItem *item, gpointer user_data)
{
	const char *argv[] = { BINDIR "/nm-vpn-properties", NULL};

	g_spawn_async (NULL, (gchar **) argv, NULL, 0, NULL, NULL, NULL, NULL);

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

static void
find_first_vpn_connection (gpointer key, gpointer value, gpointer user_data)
{
	NMVPNConnection *connection = NM_VPN_CONNECTION (value);
	NMVPNConnection **first = (NMVPNConnection **) user_data;

	if (*first)
		return;

	*first = connection;
}

/*
 * nma_menu_disconnect_vpn_item_activate
 *
 * Signal function called when user clicks "Disconnect VPN..."
 *
 */
static void
nma_menu_disconnect_vpn_item_activate (GtkMenuItem *item, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMVPNConnection *connection = NULL;

	g_hash_table_foreach (applet->vpn_connections, find_first_vpn_connection, &connection);
	if (!connection)
		return;

	nm_vpn_connection_disconnect (connection);	

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

/*
 * nma_menu_add_separator_item
 *
 */
static void
nma_menu_add_separator_item (GtkWidget *menu)
{
	GtkWidget *menu_item;

	menu_item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}


/*
 * nma_menu_add_text_item
 *
 * Add a non-clickable text item to a menu
 *
 */
static void nma_menu_add_text_item (GtkWidget *menu, char *text)
{
	GtkWidget		*menu_item;

	g_return_if_fail (text != NULL);
	g_return_if_fail (menu != NULL);

	menu_item = gtk_menu_item_new_with_label (text);
	gtk_widget_set_sensitive (menu_item, FALSE);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}

static gint
sort_devices (gconstpointer a, gconstpointer b)
{
	NMDevice *aa = NM_DEVICE (a);
	NMDevice *bb = NM_DEVICE (b);
	GType aa_type;
	GType bb_type;

	aa_type = G_OBJECT_TYPE (G_OBJECT (aa));
	bb_type = G_OBJECT_TYPE (G_OBJECT (bb));

	if (aa_type == bb_type) {
		const char *foo;
		char *aa_desc = NULL;
		char *bb_desc = NULL;
		gint ret;

		foo = utils_get_device_description (aa);
		if (foo)
			aa_desc = g_strdup (foo);
		if (!aa_desc)
			aa_desc = nm_device_get_iface (aa);

		foo = utils_get_device_description (bb);
		if (foo)
			bb_desc = g_strdup (foo);
		if (!bb_desc)
			bb_desc = nm_device_get_iface (bb);

		if (!aa_desc && bb_desc) {
			g_free (bb_desc);
			return -1;
		} else if (aa_desc && !bb_desc) {
			g_free (aa_desc);
			return 1;
		} else if (!aa_desc && !bb_desc) {
			return 0;
		}

		g_assert (aa_desc);
		g_assert (bb_desc);
		ret = strcmp (aa_desc, bb_desc);

		g_free (aa_desc);
		g_free (bb_desc);

		return ret;
	}

	if (aa_type == NM_TYPE_DEVICE_802_3_ETHERNET && bb_type == NM_TYPE_DEVICE_802_11_WIRELESS)
		return -1;
	if (aa_type == NM_TYPE_DEVICE_802_3_ETHERNET && bb_type == NM_TYPE_GSM_DEVICE)
		return -1;
	if (aa_type == NM_TYPE_DEVICE_802_3_ETHERNET && bb_type == NM_TYPE_CDMA_DEVICE)
		return -1;

	if (aa_type == NM_TYPE_GSM_DEVICE && bb_type == NM_TYPE_CDMA_DEVICE)
		return -1;
	if (aa_type == NM_TYPE_GSM_DEVICE && bb_type == NM_TYPE_DEVICE_802_11_WIRELESS)
		return -1;

	if (aa_type == NM_TYPE_CDMA_DEVICE && bb_type == NM_TYPE_DEVICE_802_11_WIRELESS)
		return -1;

	return 1;
}

NMConnection *
applet_find_active_connection_for_device (NMDevice *device, NMApplet *applet)
{
	NMConnection *connection = NULL;
	GSList *iter;

	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);
	g_return_val_if_fail (NM_IS_APPLET (applet), NULL);

	for (iter = applet->active_connections; iter; iter = g_slist_next (iter)) {
		NMClientActiveConnection *con = (NMClientActiveConnection *) iter->data;

		if (!g_slist_find (con->devices, device))
			continue;

		if (!strcmp (con->service_name, NM_DBUS_SERVICE_SYSTEM_SETTINGS)) {
			connection = applet_dbus_settings_system_get_by_dbus_path (APPLET_DBUS_SETTINGS (applet->settings), con->connection_path);
		} else if (!strcmp (con->service_name, NM_DBUS_SERVICE_USER_SETTINGS)) {
			AppletExportedConnection *tmp;

			tmp = applet_dbus_settings_user_get_by_dbus_path (APPLET_DBUS_SETTINGS (applet->settings), con->connection_path);
			if (tmp) {
				connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (tmp));
				break;
			}
		}
	}

	return connection;
}

static guint32
nma_menu_add_devices (GtkWidget *menu, NMApplet *applet)
{
	GSList *devices = NULL;
	GSList *iter;
	gint n_wireless_interfaces = 0;
	gint n_wired_interfaces = 0;

	devices = nm_client_get_devices (applet->nm_client);

	if (devices)
		devices = g_slist_sort (devices, sort_devices);

	for (iter = devices; iter; iter = iter->next) {
		NMDevice *device = NM_DEVICE (iter->data);

		/* Ignore unsupported devices */
		if (!(nm_device_get_capabilities (device) & NM_DEVICE_CAP_NM_SUPPORTED))
			continue;

		if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
			if (nm_client_wireless_get_enabled (applet->nm_client))
				n_wireless_interfaces++;
		} else if (NM_IS_DEVICE_802_3_ETHERNET (device))
			n_wired_interfaces++;
	}

	if (n_wired_interfaces == 0 && n_wireless_interfaces == 0) {
		nma_menu_add_text_item (menu, _("No network devices have been found"));
		goto out;
	}

	/* Add all devices in our device list to the menu */
	for (iter = devices; iter; iter = iter->next) {
		NMDevice *device = NM_DEVICE (iter->data);
		gint n_devices = 0;
		NMADeviceClass *dclass;
		NMConnection *active;

		/* Ignore unsupported devices */
		if (!(nm_device_get_capabilities (device) & NM_DEVICE_CAP_NM_SUPPORTED))
			continue;

		if (NM_IS_DEVICE_802_11_WIRELESS (device))
			n_devices = n_wireless_interfaces;
		else if (NM_IS_DEVICE_802_3_ETHERNET (device))
			n_devices = n_wired_interfaces++;

		active = applet_find_active_connection_for_device (device, applet);

		dclass = get_device_class (device, applet);
		if (dclass)
			dclass->add_menu_item (device, n_devices, active, menu, applet);
	}

 out:
	g_slist_free (devices);
	return n_wireless_interfaces;
}

static int
sort_vpn_connections (gconstpointer a, gconstpointer b)
{
	return strcmp (get_connection_id ((AppletExportedConnection *) a),
				get_connection_id ((AppletExportedConnection *) b));
}

static GSList *
get_vpn_connections (NMApplet *applet)
{
	GSList *all_connections;
	GSList *iter;
	GSList *list = NULL;

	all_connections = applet_dbus_settings_list_connections (APPLET_DBUS_SETTINGS (applet->settings));

	for (iter = all_connections; iter; iter = iter->next) {
		AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (iter->data);
		NMConnection *connection;
		NMSettingConnection *s_con;

		connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		if (strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME))
			/* Not a VPN connection */
			continue;

		if (!nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN_PROPERTIES)) {
			const char *name = NM_SETTING (s_con)->name;
			g_warning ("%s: VPN connection '%s' didn't have requires vpn-properties setting.", __func__, name);
			continue;
		}

		list = g_slist_prepend (list, exported);
	}

	return g_slist_sort (list, sort_vpn_connections);
}

static void
nma_menu_add_vpn_submenu (GtkWidget *menu, NMApplet *applet)
{
	GtkMenu *vpn_menu;
	GtkMenuItem *item;
	GSList *list;
	GSList *iter;
	int num_vpn_active = 0;

	nma_menu_add_separator_item (menu);

	vpn_menu = GTK_MENU (gtk_menu_new ());

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_VPN Connections")));
	gtk_menu_item_set_submenu (item, GTK_WIDGET (vpn_menu));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (item));

	list = get_vpn_connections (applet);
	num_vpn_active = g_hash_table_size (applet->vpn_connections);

	for (iter = list; iter; iter = iter->next) {
		AppletExportedConnection *exported = APPLET_EXPORTED_CONNECTION (iter->data);
		const char *connection_name = get_connection_id (exported);

		item = GTK_MENU_ITEM (gtk_check_menu_item_new_with_label (connection_name));
		gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);

		/* If no VPN connections are active, draw all menu items enabled. If
		 * >= 1 VPN connections are active, only the active VPN menu item is
		 * drawn enabled.
		 */
		if ((num_vpn_active == 0) || g_hash_table_lookup (applet->vpn_connections, connection_name))
			gtk_widget_set_sensitive (GTK_WIDGET (item), TRUE);
		else
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

		if (g_hash_table_lookup (applet->vpn_connections, connection_name))
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

		g_object_set_data_full (G_OBJECT (item), "connection", 
						    g_object_ref (exported),
						    (GDestroyNotify) g_object_unref);

		if (nm_client_get_state (applet->nm_client) != NM_STATE_CONNECTED)
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

		g_signal_connect (item, "activate", G_CALLBACK (nma_menu_vpn_item_clicked), applet);
		gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));
	}

	/* Draw a seperator, but only if we have VPN connections above it */
	if (list)
		nma_menu_add_separator_item (GTK_WIDGET (vpn_menu));

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Configure VPN...")));
	g_signal_connect (item, "activate", G_CALLBACK (nma_menu_configure_vpn_item_activate), applet);
	gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Disconnect VPN...")));
	g_signal_connect (item, "activate", G_CALLBACK (nma_menu_disconnect_vpn_item_activate), applet);
	gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));
	if (num_vpn_active == 0)
		gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
}


static void
nma_set_wireless_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_wireless_set_enabled (applet->nm_client, state);
}


static void
nma_set_networking_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_sleep (applet->nm_client, !state);
}

/*
 * nma_menu_show_cb
 *
 * Pop up the wireless networks menu
 *
 */
static void nma_menu_show_cb (GtkWidget *menu, NMApplet *applet)
{
	guint32 n_wireless;

	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);

	gtk_status_icon_set_tooltip (applet->status_icon, NULL);

	if (!nm_client_manager_is_running (applet->nm_client)) {
		nma_menu_add_text_item (menu, _("NetworkManager is not running..."));
		return;
	}

	if (nm_client_get_state (applet->nm_client) == NM_STATE_ASLEEP) {
		nma_menu_add_text_item (menu, _("Networking disabled"));
		return;
	}

	n_wireless = nma_menu_add_devices (menu, applet);

	nma_menu_add_vpn_submenu (menu, applet);

	if (n_wireless > 0 && nm_client_wireless_get_enabled (applet->nm_client)) {
		/* Add the "Other wireless network..." entry */
		nma_menu_add_separator_item (menu);
		nma_menu_add_other_network_item (menu, applet);
		nma_menu_add_create_network_item (menu, applet);
	}

	gtk_widget_show_all (menu);

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

/*
 * nma_menu_create
 *
 * Create the applet's dropdown menu
 *
 */
static GtkWidget *
nma_menu_create (NMApplet *applet)
{
	GtkWidget	*menu;

	g_return_val_if_fail (applet != NULL, NULL);

	menu = gtk_menu_new ();
	gtk_container_set_border_width (GTK_CONTAINER (menu), 0);
	g_signal_connect (menu, "show", G_CALLBACK (nma_menu_show_cb), applet);
	return menu;
}

/*
 * nma_menu_clear
 *
 * Destroy the menu and each of its items data tags
 *
 */
static void nma_menu_clear (NMApplet *applet)
{
	g_return_if_fail (applet != NULL);

	if (applet->menu)
		gtk_widget_destroy (applet->menu);
	applet->menu = nma_menu_create (applet);
}


/*
 * nma_context_menu_update
 *
 */
static void
nma_context_menu_update (NMApplet *applet)
{
	NMState state;
	gboolean have_wireless = FALSE;
	gboolean wireless_hw_enabled;

	state = nm_client_get_state (applet->nm_client);
	wireless_hw_enabled = nm_client_wireless_hardware_get_enabled (applet->nm_client);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->enable_networking_item),
							  state != NM_STATE_ASLEEP);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->stop_wireless_item),
							  nm_client_wireless_get_enabled (applet->nm_client));
	gtk_widget_set_sensitive (GTK_WIDGET (applet->stop_wireless_item),
	                          wireless_hw_enabled);

	gtk_widget_set_sensitive (applet->info_menu_item,
						 state == NM_STATE_CONNECTED);

	if (state != NM_STATE_ASLEEP) {
		GSList *list;
		GSList *iter;
	
		list = nm_client_get_devices (applet->nm_client);
		for (iter = list; iter; iter = iter->next) {
			if (NM_IS_DEVICE_802_11_WIRELESS (iter->data)) {
				have_wireless = TRUE;
				break;
			}
		}
		g_slist_free (list);
	}

	if (have_wireless)
		gtk_widget_show_all (applet->stop_wireless_item);
	else
		gtk_widget_hide (applet->stop_wireless_item);
}

static void
ce_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	/* We are in the child process at this point */
	pid_t pid = getpid ();
	setpgid (pid, pid);
}

static void
nma_edit_connections_cb (GtkMenuItem *mi, NMApplet *applet)
{
	char *argv[2];
	GError *error = NULL;
	gboolean success;

	argv[0] = BINDIR "/nm-connection-editor";
	argv[1] = NULL;

	success = g_spawn_async ("/", argv, NULL, 0, &ce_child_setup, NULL, NULL, &error);
	if (!success) {
		g_warning ("Error launching connection editor: %s", error->message);
		g_error_free (error);
	}
}

/*
 * nma_context_menu_create
 *
 * Generate the contextual popup menu.
 *
 */
static GtkWidget *nma_context_menu_create (NMApplet *applet)
{
	GtkMenuShell *menu;
	GtkWidget	*menu_item;
	GtkWidget *image;

	g_return_val_if_fail (applet != NULL, NULL);

	menu = GTK_MENU_SHELL (gtk_menu_new ());

	/* 'Enable Networking' item */
	applet->enable_networking_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Networking"));
	g_signal_connect (applet->enable_networking_item,
				   "toggled",
				   G_CALLBACK (nma_set_networking_enabled_cb),
				   applet);
	gtk_menu_shell_append (menu, applet->enable_networking_item);

	/* 'Enable Wireless' item */
	applet->stop_wireless_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Wireless"));
	g_signal_connect (applet->stop_wireless_item,
				   "toggled",
				   G_CALLBACK (nma_set_wireless_enabled_cb),
				   applet);
	gtk_menu_shell_append (menu, applet->stop_wireless_item);

	nma_menu_add_separator_item (GTK_WIDGET (menu));

	/* 'Connection Information' item */
	applet->info_menu_item = gtk_image_menu_item_new_with_mnemonic (_("Connection _Information"));
	g_signal_connect_swapped (applet->info_menu_item,
	                          "activate",
	                          G_CALLBACK (applet_info_dialog_show),
	                          applet);
	image = gtk_image_new_from_stock (GTK_STOCK_INFO, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->info_menu_item), image);
	gtk_menu_shell_append (menu, applet->info_menu_item);

	/* 'Edit Connections...' item */
	applet->connections_menu_item = gtk_image_menu_item_new_with_mnemonic (_("Edit Connections..."));
	g_signal_connect (applet->connections_menu_item,
				   "activate",
				   G_CALLBACK (nma_edit_connections_cb),
				   applet);
	image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->connections_menu_item), image);
	gtk_menu_shell_append (menu, applet->connections_menu_item);

	/* Separator */
	nma_menu_add_separator_item (GTK_WIDGET (menu));

#if 0	/* FIXME: Implement the help callback, nma_help_cb()! */
	/* Help item */
	menu_item = gtk_image_menu_item_new_with_mnemonic (_("_Help"));
	g_signal_connect (menu_item, "activate", G_CALLBACK (nma_help_cb), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_HELP, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (menu, menu_item);
	gtk_widget_set_sensitive (menu_item, FALSE);
#endif

	/* About item */
	menu_item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (applet_about_dialog_show), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (menu, menu_item);

	gtk_widget_show_all (GTK_WIDGET (menu));

	return GTK_WIDGET (menu);
}


/*****************************************************************************/

static void
foo_set_icon (NMApplet *applet, GdkPixbuf *pixbuf, guint32 layer)
{
	int i;

	if (layer > ICON_LAYER_MAX) {
		g_warning ("Tried to icon to invalid layer %d", layer);
		return;
	}

	/* Ignore setting of the same icon as is already displayed */
	if (applet->icon_layers[layer] == pixbuf)
		return;

	if (applet->icon_layers[layer]) {
		g_object_unref (applet->icon_layers[layer]);
		applet->icon_layers[layer] = NULL;
	}

	if (pixbuf)
		applet->icon_layers[layer] = g_object_ref (pixbuf);

	if (!applet->icon_layers[0]) {
		pixbuf = g_object_ref (applet->no_connection_icon);
	} else {
		pixbuf = gdk_pixbuf_copy (applet->icon_layers[0]);

		for (i = ICON_LAYER_LINK + 1; i <= ICON_LAYER_MAX; i++) {
			GdkPixbuf *top = applet->icon_layers[i];

			if (!top)
				continue;

			gdk_pixbuf_composite (top, pixbuf, 0, 0, gdk_pixbuf_get_width (top),
							  gdk_pixbuf_get_height (top),
							  0, 0, 1.0, 1.0,
							  GDK_INTERP_NEAREST, 255);
		}
	}

	gtk_status_icon_set_from_pixbuf (applet->status_icon, pixbuf);
	g_object_unref (pixbuf);
}


AppletExportedConnection *
applet_get_exported_connection_for_device (NMDevice *device, NMApplet *applet)
{
	GSList *iter;

	for (iter = applet->active_connections; iter; iter = g_slist_next (iter)) {
		NMClientActiveConnection * act_con = (NMClientActiveConnection *) iter->data;
		AppletExportedConnection *exported;

		if (strcmp (act_con->service_name, NM_DBUS_SERVICE_USER_SETTINGS) != 0)
			continue;

		if (!g_slist_find (act_con->devices, device))
			continue;

		exported = applet_dbus_settings_user_get_by_dbus_path (applet->settings, act_con->connection_path);
		if (!exported || !nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported)))
			continue;

		return exported;
	}
	return NULL;
}

static void
applet_common_device_state_change (NMDevice *device,
                                   NMDeviceState state,
                                   NMApplet *applet)
{
	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
	case NM_DEVICE_STATE_NEED_AUTH:
	case NM_DEVICE_STATE_IP_CONFIG:
		start_animation_timeout (applet);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		/* If the device activation was successful, update the corresponding
		 * connection object with a current timestamp.
		 */
		update_connection_timestamp (applet, device, NULL);
		/* Fall through */
	default:
		clear_animation_timeout (applet);
		break;
	}
}

static void
clear_active_connections (NMApplet *applet)
{
	g_slist_foreach (applet->active_connections,
	                 (GFunc) nm_client_free_active_connection_element,
	                 NULL);
	g_slist_free (applet->active_connections);
	applet->active_connections = NULL;
}

static void
foo_device_state_changed_cb (NMDevice *device, NMDeviceState state, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMADeviceClass *dclass;

	clear_active_connections (applet);
	applet->active_connections = nm_client_get_active_connections (applet->nm_client);

	dclass = get_device_class (device, applet);
	g_assert (dclass);

	dclass->device_state_changed (device, state, applet);
	applet_common_device_state_change (device, state, applet);

	applet_schedule_update_icon (applet);
}

static void
foo_device_added_cb (NMClient *client, NMDevice *device, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMADeviceClass *dclass;

	dclass = get_device_class (device, applet);
	g_return_if_fail (dclass != NULL);

	if (dclass->device_added)
		dclass->device_added (device, applet);

	g_signal_connect (device, "state-changed",
				   G_CALLBACK (foo_device_state_changed_cb),
				   user_data);

	foo_device_state_changed_cb	(device, nm_device_get_state (device), applet);
}

static void
foo_client_state_change_cb (NMClient *client, NMState state, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	switch (state) {
	case NM_STATE_DISCONNECTED:
		applet_do_notify (applet, NOTIFY_URGENCY_NORMAL, _("Disconnected"),
						  _("The network connection has been disconnected."),
						  "nm-no-connection");
		/* Fall through */
	case NM_STATE_UNKNOWN:
		/* Clear any VPN connections */
		if (applet->vpn_connections)
			g_hash_table_remove_all (applet->vpn_connections);
		clear_active_connections (applet);
		break;
	default:
		break;
	}

	applet_schedule_update_icon (applet);
}

static void
foo_manager_running_cb (NMClient *client,
                        gboolean running,
                        gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	if (running) {
		g_message ("NM appeared");
	} else {
		g_message ("NM disappeared");
		clear_animation_timeout (applet);
	}

	applet->nm_running = running;
	applet_schedule_update_icon (applet);
}

static gboolean
foo_set_initial_state (gpointer data)
{
	NMApplet *applet = NM_APPLET (data);
	GSList *list, *iter;

	list = nm_client_get_devices (applet->nm_client);
	for (iter = list; iter; iter = g_slist_next (iter))
		foo_device_added_cb (applet->nm_client, NM_DEVICE (iter->data), applet);
	g_slist_free (list);

	list = nm_vpn_manager_get_connections (applet->vpn_manager);
	if (list) {
		g_slist_foreach (list, (GFunc) add_one_vpn_connection, applet);
		g_slist_free (list);
	}

	applet_schedule_update_icon (applet);

	return FALSE;
}

static void
foo_client_setup (NMApplet *applet)
{
	applet->nm_client = nm_client_new ();
	if (!applet->nm_client)
		return;

	g_signal_connect (applet->nm_client, "state-changed",
	                  G_CALLBACK (foo_client_state_change_cb),
	                  applet);
	g_signal_connect (applet->nm_client, "device-added",
	                  G_CALLBACK (foo_device_added_cb),
	                  applet);
	g_signal_connect (applet->nm_client, "manager-running",
	                  G_CALLBACK (foo_manager_running_cb),
	                  applet);

	applet->nm_running = nm_client_manager_is_running (applet->nm_client);
	if (applet->nm_running)
		g_idle_add (foo_set_initial_state, applet);
}

static GdkPixbuf *
applet_common_get_device_icon (NMDeviceState state, NMApplet *applet)
{
	GdkPixbuf *pixbuf = NULL;
	int stage = -1;

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		stage = 0;
		break;
	case NM_DEVICE_STATE_CONFIG:
		stage = 1;
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		stage = 2;
		break;
	default:
		break;
	}

	if (stage >= 0) {
		pixbuf = applet->network_connecting_icons[stage][applet->animation_step];
		applet->animation_step++;
		if (applet->animation_step >= NUM_CONNECTING_FRAMES)
			applet->animation_step = 0;
	}

	return pixbuf;
}

static GdkPixbuf *
applet_get_device_icon_for_state (NMApplet *applet, char **tip)
{
	NMDevice *device;
	GdkPixbuf *pixbuf = NULL;
	NMDeviceState state = NM_DEVICE_STATE_UNKNOWN;
	NMADeviceClass *dclass;

	// FIXME: handle multiple device states here

	device = applet_get_first_active_device (applet);
	if (!device)
		goto out;

	state = nm_device_get_state (device);

	dclass = get_device_class (device, applet);
	if (dclass)
		pixbuf = dclass->get_icon (device, state, tip, applet);

out:
	if (!pixbuf)
		pixbuf = applet_common_get_device_icon (state, applet);
	return pixbuf;
}

static void
free_vpn_connections (gpointer data, gpointer user_data)
{
	if (data != user_data)
		g_object_unref (G_OBJECT (data));
}

static NMVPNConnection *
applet_get_first_active_vpn_connection (NMApplet *applet,
                                        NMVPNConnectionState *out_state)
{
	NMVPNConnection *vpn_connection = NULL;
	GSList *list, *iter;

	list = nm_vpn_manager_get_connections (applet->vpn_manager);
	for (iter = list; iter; iter = g_slist_next (iter)) {
		NMVPNConnection *connection = NM_VPN_CONNECTION (iter->data);
		NMVPNConnectionState state;

		state = nm_vpn_connection_get_state (connection);
		if (   state == NM_VPN_CONNECTION_STATE_PREPARE
		    || state == NM_VPN_CONNECTION_STATE_NEED_AUTH
		    || state == NM_VPN_CONNECTION_STATE_CONNECT
		    || state == NM_VPN_CONNECTION_STATE_IP_CONFIG_GET
		    || state == NM_VPN_CONNECTION_STATE_ACTIVATED) {
			*out_state = state;
			vpn_connection = connection;
			break;
		}
	}

	g_slist_foreach (list, free_vpn_connections, vpn_connection);
	g_slist_free (list);
	return vpn_connection;
}

static gboolean
applet_update_icon (gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GdkPixbuf *pixbuf = NULL;
	NMState state;
	char *tip = NULL;
	NMVPNConnection *vpn_connection;
	NMVPNConnectionState vpn_state = NM_VPN_SERVICE_STATE_UNKNOWN;

	applet->update_icon_id = 0;

	gtk_status_icon_set_visible (applet->status_icon, applet->nm_running);
	
	/* Handle device state first */

	state = nm_client_get_state (applet->nm_client);
	if (!applet->nm_running)
		state = NM_STATE_UNKNOWN;

	switch (state) {
	case NM_STATE_UNKNOWN:
	case NM_STATE_ASLEEP:
		pixbuf = applet->no_connection_icon;
		tip = g_strdup (_("Networking disabled"));
		break;
	case NM_STATE_DISCONNECTED:
		pixbuf = applet->no_connection_icon;
		tip = g_strdup (_("No network connection"));
		break;
	default:
		pixbuf = applet_get_device_icon_for_state (applet, &tip);
		break;
	}

	foo_set_icon (applet, pixbuf, ICON_LAYER_LINK);
	if (tip) {
		gtk_status_icon_set_tooltip (applet->status_icon, tip);
		g_free (tip);
	}

	/* VPN state next */
	pixbuf = NULL;
	vpn_connection = applet_get_first_active_vpn_connection (applet, &vpn_state);

	switch (vpn_state) {
	case NM_VPN_CONNECTION_STATE_ACTIVATED:
		pixbuf = applet->vpn_lock_icon;
		break;
	case NM_VPN_CONNECTION_STATE_PREPARE:
	case NM_VPN_CONNECTION_STATE_NEED_AUTH:
	case NM_VPN_CONNECTION_STATE_CONNECT:
	case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
		pixbuf = applet->vpn_connecting_icons[applet->animation_step];
		applet->animation_step++;
		if (applet->animation_step >= NUM_VPN_CONNECTING_FRAMES)
			applet->animation_step = 0;
		break;
	default:
		break;
	}

	foo_set_icon (applet, pixbuf, ICON_LAYER_VPN);
	return FALSE;
}

void
applet_schedule_update_icon (NMApplet *applet)
{
	if (!applet->update_icon_id)
		applet->update_icon_id = g_idle_add (applet_update_icon, applet);
}

static NMDevice *
find_active_device (AppletExportedConnection *exported,
                    NMApplet *applet,
                    const char **specific_object)
{
	GSList *iter;

	g_return_val_if_fail (exported != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	/* Ensure the active connection list is up-to-date */
	if (g_slist_length (applet->active_connections) == 0)
		applet->active_connections = nm_client_get_active_connections (applet->nm_client);

	/* Look through the active connection list trying to find the D-Bus
	 * object path of applet_connection.
	 */
	for (iter = applet->active_connections; iter; iter = g_slist_next (iter)) {
		NMClientActiveConnection *con = (NMClientActiveConnection *) iter->data;
		NMConnection *connection;
		const char *con_path;

		if (strcmp (con->service_name, NM_DBUS_SERVICE_USER_SETTINGS))
			continue;

		connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
		con_path = nm_connection_get_path (connection);
		if (!strcmp (con_path, con->connection_path)) {
			*specific_object = con->specific_object;
			return NM_DEVICE (con->devices->data);
		}
	}

	return NULL;
}

static void
applet_settings_new_secrets_requested_cb (AppletDbusSettings *settings,
                                          AppletExportedConnection *exported,
                                          const char *setting_name,
                                          const char **hints,
                                          gboolean ask_user,
                                          DBusGMethodInvocation *context,
                                          gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMDevice *device;
	NMADeviceClass *dclass;
	GError *error = NULL;
	const char *specific_object = NULL;

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	g_return_if_fail (connection != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_return_if_fail (s_con != NULL);
	g_return_if_fail (s_con->type != NULL);

	/* VPN secrets get handled a bit differently */
	if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME)) {
		nma_vpn_request_password (connection, setting_name, ask_user, context);
		return;
	}

	/* Find the active device for this connection */
	device = find_active_device (exported, applet, &specific_object);
	if (!device) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't find details for connection",
		             __FILE__, __LINE__, __func__);
		goto error;
	}

	dclass = get_device_class (device, applet);
	if (!dclass) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): device type unknown",
		             __FILE__, __LINE__, __func__);
		goto error;
	}

	if (!dclass->get_secrets) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): no secrets found",
		             __FILE__, __LINE__, __func__);
		goto error;
	}

	/* Let the device class handle secrets */
	if (!dclass->get_secrets (device, connection, specific_object, setting_name,
	                          context, applet, &error))
		goto error;

	return;

error:
	g_warning (error->message);
	dbus_g_method_return_error (context, error);
	g_error_free (error);
}

/*****************************************************************************/

#define CLEAR_ICON(x) \
	if (x) { \
		g_object_unref (x); \
		x = NULL; \
	}

static void nma_icons_free (NMApplet *applet)
{
	int i, j;

	if (!applet->icons_loaded)
		return;

	for (i = 0; i <= ICON_LAYER_MAX; i++)
		CLEAR_ICON(applet->icon_layers[i]);

	CLEAR_ICON(applet->no_connection_icon);
	CLEAR_ICON(applet->wired_icon);
	CLEAR_ICON(applet->adhoc_icon);
	CLEAR_ICON(applet->wwan_icon);
	CLEAR_ICON(applet->vpn_lock_icon);
	CLEAR_ICON(applet->wireless_00_icon);
	CLEAR_ICON(applet->wireless_25_icon);
	CLEAR_ICON(applet->wireless_50_icon);
	CLEAR_ICON(applet->wireless_75_icon);
	CLEAR_ICON(applet->wireless_100_icon);

	for (i = 0; i < NUM_CONNECTING_STAGES; i++) {
		for (j = 0; j < NUM_CONNECTING_FRAMES; j++)
			CLEAR_ICON(applet->network_connecting_icons[i][j]);
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++)
		CLEAR_ICON(applet->vpn_connecting_icons[i]);

	for (i = 0; i <= ICON_LAYER_MAX; i++)
		CLEAR_ICON(applet->icon_layers[i]);

	applet->icons_loaded = FALSE;
}

#define ICON_LOAD(x, y)	\
	{ \
		x = gtk_icon_theme_load_icon (applet->icon_theme, y, applet->size, 0, &err); \
		if (x == NULL) { \
			g_warning ("Icon %s missing: %s", y, err->message); \
			g_error_free (err); \
			goto out; \
		} \
	}

static gboolean
nma_icons_load (NMApplet *applet)
{
	int i, j;
	GError *err = NULL;

	g_return_val_if_fail (!applet->icons_loaded, FALSE);

	if (applet->size < 0)
		return FALSE;

	ICON_LOAD(applet->no_connection_icon, "nm-no-connection");
	ICON_LOAD(applet->wired_icon, "nm-device-wired");
	ICON_LOAD(applet->adhoc_icon, "nm-adhoc");
	ICON_LOAD(applet->wwan_icon, "nm-device-wwan"); /* FIXME: Until there's no WWAN device icon */
	ICON_LOAD(applet->vpn_lock_icon, "nm-vpn-lock");

	ICON_LOAD(applet->wireless_00_icon, "nm-signal-00");
	ICON_LOAD(applet->wireless_25_icon, "nm-signal-25");
	ICON_LOAD(applet->wireless_50_icon, "nm-signal-50");
	ICON_LOAD(applet->wireless_75_icon, "nm-signal-75");
	ICON_LOAD(applet->wireless_100_icon, "nm-signal-100");

	for (i = 0; i < NUM_CONNECTING_STAGES; i++) {
		for (j = 0; j < NUM_CONNECTING_FRAMES; j++) {
			char *name;

			name = g_strdup_printf ("nm-stage%02d-connecting%02d", i+1, j+1);
			ICON_LOAD(applet->network_connecting_icons[i][j], name);
			g_free (name);
		}
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++) {
		char *name;

		name = g_strdup_printf ("nm-vpn-connecting%02d", i+1);
		ICON_LOAD(applet->vpn_connecting_icons[i], name);
		g_free (name);
	}

	applet->icons_loaded = TRUE;

out:
	if (!applet->icons_loaded) {
		applet_warning_dialog_show (_("The NetworkManager applet could not find some required resources.  It cannot continue.\n"));
		nma_icons_free (applet);
	}

	return applet->icons_loaded;
}

static void nma_icon_theme_changed (GtkIconTheme *icon_theme, NMApplet *applet)
{
	nma_icons_free (applet);
	nma_icons_load (applet);
}

static void nma_icons_init (NMApplet *applet)
{
	GdkScreen *screen;
	gboolean path_appended;

	if (applet->icon_theme) {
		g_signal_handlers_disconnect_by_func (applet->icon_theme,
						      G_CALLBACK (nma_icon_theme_changed),
						      applet);
		g_object_unref (G_OBJECT (applet->icon_theme));
	}

#if GTK_CHECK_VERSION(2, 11, 0)
	screen = gtk_status_icon_get_screen (applet->status_icon);
#else
	screen = gdk_screen_get_default ();
#endif /* gtk 2.11.0 */
	
	applet->icon_theme = gtk_icon_theme_get_for_screen (screen);

	/* If not done yet, append our search path */
	path_appended = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (applet->icon_theme),
					 		    "NMAIconPathAppended"));
	if (path_appended == FALSE) {
		gtk_icon_theme_append_search_path (applet->icon_theme, ICONDIR);
		g_object_set_data (G_OBJECT (applet->icon_theme),
				   "NMAIconPathAppended",
				   GINT_TO_POINTER (TRUE));
	}

	g_signal_connect (applet->icon_theme, "changed", G_CALLBACK (nma_icon_theme_changed), applet);
}

static void
status_icon_screen_changed_cb (GtkStatusIcon *icon,
                               GParamSpec *pspec,
                               NMApplet *applet)
{
	nma_icons_init (applet);
	nma_icon_theme_changed (NULL, applet);
}

static gboolean
status_icon_size_changed_cb (GtkStatusIcon *icon,
                             gint size,
                             NMApplet *applet)
{
	applet->size = size;

	nma_icons_free (applet);
	nma_icons_load (applet);

	applet_schedule_update_icon (applet);

	return TRUE;
}

static void
status_icon_activate_cb (GtkStatusIcon *icon, NMApplet *applet)
{
	nma_menu_clear (applet);
	gtk_menu_popup (GTK_MENU (applet->menu), NULL, NULL,
			gtk_status_icon_position_menu, icon,
			1, gtk_get_current_event_time ());
}

static void
status_icon_popup_menu_cb (GtkStatusIcon *icon,
                           guint button,
                           guint32 activate_time,
                           NMApplet *applet)
{
	nma_context_menu_update (applet);
	gtk_menu_popup (GTK_MENU (applet->context_menu), NULL, NULL,
			gtk_status_icon_position_menu, icon,
			button, activate_time);
}

static gboolean
setup_widgets (NMApplet *applet)
{
	g_return_val_if_fail (NM_IS_APPLET (applet), FALSE);

	applet->status_icon = gtk_status_icon_new ();
	if (!applet->status_icon)
		return FALSE;

	g_signal_connect (applet->status_icon, "notify::screen",
			  G_CALLBACK (status_icon_screen_changed_cb), applet);
	g_signal_connect (applet->status_icon, "size-changed",
			  G_CALLBACK (status_icon_size_changed_cb), applet);
	g_signal_connect (applet->status_icon, "activate",
			  G_CALLBACK (status_icon_activate_cb), applet);
	g_signal_connect (applet->status_icon, "popup-menu",
			  G_CALLBACK (status_icon_popup_menu_cb), applet);

	applet->menu = nma_menu_create (applet);
	if (!applet->menu)
		return FALSE;

	applet->context_menu = nma_context_menu_create (applet);
	if (!applet->context_menu)
		return FALSE;
	applet->encryption_size_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);
	if (!applet->encryption_size_group)
		return FALSE;

	return TRUE;
}

static GObject *
constructor (GType type,
             guint n_props,
             GObjectConstructParam *construct_props)
{
	NMApplet *applet;
	AppletDBusManager * dbus_mgr;
	GError *error = NULL;

	if (!crypto_init (&error)) {
		g_warning ("Couldn't initilize crypto system: %d %s",
		           error->code, error->message);
		g_error_free (error);
		return NULL;
	}

	applet = NM_APPLET (G_OBJECT_CLASS (nma_parent_class)->constructor (type, n_props, construct_props));

	g_set_application_name (_("NetworkManager Applet"));
	gtk_window_set_default_icon_name (GTK_STOCK_NETWORK);

	applet->glade_file = g_build_filename (GLADEDIR, "applet.glade", NULL);
	if (!applet->glade_file || !g_file_test (applet->glade_file, G_FILE_TEST_IS_REGULAR)) {
		applet_warning_dialog_show (_("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		goto error;
	}

	applet->info_dialog_xml = glade_xml_new (applet->glade_file, "info_dialog", NULL);
	if (!applet->info_dialog_xml)
        goto error;

	applet->gconf_client = gconf_client_get_default ();
	if (!applet->gconf_client)
	    goto error;

	/* Load pixmaps and create applet widgets */
	if (!setup_widgets (applet))
	    goto error;
	nma_icons_init (applet);

	applet->active_connections = NULL;
	
	dbus_mgr = applet_dbus_manager_get ();
	if (dbus_mgr == NULL) {
		nm_warning ("Couldn't initialize the D-Bus manager.");
		g_object_unref (applet);
		return NULL;
	}

	applet->settings = applet_dbus_settings_new ();
	g_signal_connect (G_OBJECT (applet->settings), "new-secrets-requested",
	                  (GCallback) applet_settings_new_secrets_requested_cb,
	                  applet);

    /* Start our DBus service */
    if (!applet_dbus_manager_start_service (dbus_mgr)) {
		g_object_unref (applet);
		return NULL;
    }

	/* Initialize device classes */
	applet->wired_class = applet_device_wired_get_class (applet);
	g_assert (applet->wired_class);

	applet->wireless_class = applet_device_wireless_get_class (applet);
	g_assert (applet->wireless_class);

	applet->gsm_class = applet_device_gsm_get_class (applet);
	g_assert (applet->gsm_class);

	applet->cdma_class = applet_device_cdma_get_class (applet);
	g_assert (applet->cdma_class);

	foo_client_setup (applet);
	applet->vpn_manager = nm_vpn_manager_new ();
	applet->vpn_connections = g_hash_table_new_full (g_str_hash, g_str_equal,
										    (GDestroyNotify) g_free,
										    (GDestroyNotify) g_object_unref);

	return G_OBJECT (applet);

error:
	g_object_unref (applet);
	return NULL;
}

static void finalize (GObject *object)
{
	NMApplet *applet = NM_APPLET (object);

	g_slice_free (NMADeviceClass, applet->wired_class);
	g_slice_free (NMADeviceClass, applet->wireless_class);
	g_slice_free (NMADeviceClass, applet->gsm_class);

	if (applet->update_icon_id)
		g_source_remove (applet->update_icon_id);

	nma_menu_clear (applet);
	nma_icons_free (applet);

	if (applet->notification) {
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}

	g_free (applet->glade_file);
	if (applet->info_dialog_xml)
		g_object_unref (applet->info_dialog_xml);

	g_object_unref (applet->gconf_client);

	if (applet->status_icon)
		g_object_unref (applet->status_icon);

	g_hash_table_destroy (applet->vpn_connections);
	g_object_unref (applet->vpn_manager);
	g_object_unref (applet->nm_client);

	clear_active_connections (applet);

	crypto_deinit ();

	G_OBJECT_CLASS (nma_parent_class)->finalize (object);
}

static void nma_init (NMApplet *applet)
{
	applet->animation_id = 0;
	applet->animation_step = 0;
	applet->icon_theme = NULL;
	applet->notification = NULL;
	applet->size = -1;
}

static void nma_class_init (NMAppletClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->constructor = constructor;
	gobject_class->finalize = finalize;
}

NMApplet *
nm_applet_new ()
{
	return g_object_new (NM_TYPE_APPLET, NULL);
}

