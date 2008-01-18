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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkcheckmenuitem.h>

#include <nm-device.h>
#include <nm-setting-connection.h>
#include <nm-setting-wired.h>
#include <nm-device-802-3-ethernet.h>

#include "applet.h"
#include "applet-dbus-settings.h"
#include "applet-device-wired.h"
#include "utils.h"

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMConnection *connection;
} WiredMenuItemInfo;

static void
wired_menu_item_info_destroy (gpointer data)
{
	WiredMenuItemInfo *info = (WiredMenuItemInfo *) data;

	g_object_unref (G_OBJECT (info->device));
	if (info->connection)
		g_object_unref (G_OBJECT (info->connection));

	g_slice_free (WiredMenuItemInfo, data);
}

static NMConnection *
wired_new_auto_connection (NMDevice *device,
                           NMApplet *applet,
                           gpointer user_data)
{
	NMConnection *connection;
	NMSettingWired *s_wired = NULL;
	NMSettingConnection *s_con;

	connection = nm_connection_new ();

	s_wired = NM_SETTING_WIRED (nm_setting_wired_new ());
	nm_connection_add_setting (connection, NM_SETTING (s_wired));

	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	s_con->id = g_strdup (_("Auto Ethernet"));
	s_con->type = g_strdup (NM_SETTING (s_wired)->name);
	s_con->autoconnect = TRUE;
	nm_connection_add_setting (connection, NM_SETTING (s_con));

	return connection;
}

static void
wired_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	WiredMenuItemInfo *info = (WiredMenuItemInfo *) user_data;

	applet_menu_item_activate_helper (info->device,
	                                  info->connection,
	                                  "/",
	                                  info->applet,
	                                  user_data);
}

static void
wired_add_menu_item (NMDevice *device,
                     guint32 n_devices,
                     GtkWidget *menu,
                     NMApplet *applet)
{
	char *text;
	GtkCheckMenuItem *item;
	GSList *connections, *all, *iter;
	WiredMenuItemInfo *info;

	if (n_devices > 1) {
		const char *desc;
		char *dev_name = NULL;

		desc = utils_get_device_description (device);
		if (desc)
			dev_name = g_strdup (desc);
		if (!dev_name)
			dev_name = nm_device_get_iface (device);
		g_assert (dev_name);
		text = g_strdup_printf (_("Wired Network (%s)"), dev_name);
		g_free (dev_name);
	} else
		text = g_strdup (_("_Wired Network"));

	item = GTK_CHECK_MENU_ITEM (gtk_check_menu_item_new_with_mnemonic (text));
	g_free (text);

	all = applet_dbus_settings_get_all_connections (APPLET_DBUS_SETTINGS (applet->settings));
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	/* If there's only one connection, don't show the submenu */
	if (g_slist_length (connections) > 1) {
		GtkWidget *submenu;

		submenu = gtk_menu_new ();

		for (iter = connections; iter; iter = g_slist_next (iter)) {
			NMConnection *connection = NM_CONNECTION (iter->data);
			NMSettingConnection *s_con;
			GtkWidget *subitem;

			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			subitem = gtk_menu_item_new_with_label (s_con->id);

			info = g_slice_new0 (WiredMenuItemInfo);
			info->applet = applet;
			info->device = g_object_ref (G_OBJECT (device));
			info->connection = g_object_ref (connection);

			g_signal_connect_data (subitem, "activate",
			                       G_CALLBACK (wired_menu_item_activate),
			                       info,
			                       (GClosureNotify) wired_menu_item_info_destroy, 0);

			gtk_menu_shell_append (GTK_MENU_SHELL (submenu), GTK_WIDGET (subitem));
		}

		gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	} else {
		NMConnection *connection;

		info = g_slice_new0 (WiredMenuItemInfo);
		info->applet = applet;
		info->device = g_object_ref (G_OBJECT (device));

		if (g_slist_length (connections) == 1) {
			connection = NM_CONNECTION (g_slist_nth_data (connections, 0));
			info->connection = g_object_ref (G_OBJECT (connection));
		}

		g_signal_connect_data (item, "activate",
		                       G_CALLBACK (wired_menu_item_activate),
		                       info,
		                       (GClosureNotify) wired_menu_item_info_destroy, 0);
	}
	g_slist_free (connections);

	gtk_check_menu_item_set_draw_as_radio (item, TRUE);

	g_signal_handlers_block_matched (item, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
	                                 G_CALLBACK (wired_menu_item_activate), NULL);

	gtk_check_menu_item_set_active (item, nm_device_get_state (device) == NM_DEVICE_STATE_ACTIVATED);

	g_signal_handlers_unblock_matched (item, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
	                                   G_CALLBACK (wired_menu_item_activate), NULL);

	/* Only dim the item if the device supports carrier detection AND
	 * we know it doesn't have a link.
	 */
 	if (nm_device_get_capabilities (device) & NM_DEVICE_CAP_CARRIER_DETECT)
 		gtk_widget_set_sensitive (GTK_WIDGET (item), nm_device_get_carrier (device));

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (item));
	gtk_widget_show (GTK_WIDGET (item));
}

static void
wired_device_state_changed (NMDevice *device,
                            NMDeviceState state,
                            NMApplet *applet)
{
	if (state == NM_DEVICE_STATE_ACTIVATED) {
		applet_do_notify (applet, NOTIFY_URGENCY_LOW,
						  _("Connection Established"),
						  _("You are now connected to the wired network."),
						  "nm-device-wired");
	}
}

static GdkPixbuf *
wired_get_icon (NMDevice *device,
                NMDeviceState state,
                char **tip,
                NMApplet *applet)
{
	GdkPixbuf *pixbuf = NULL;
	char *iface;

	iface = nm_device_get_iface (NM_DEVICE (device));

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		*tip = g_strdup_printf (_("Preparing device %s for the wired network..."), iface);
		break;
	case NM_DEVICE_STATE_CONFIG:
		*tip = g_strdup_printf (_("Configuring device %s for the wired network..."), iface);
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		*tip = g_strdup_printf (_("Requesting a network address from the wired network..."));
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		pixbuf = applet->wired_icon;
		*tip = g_strdup (_("Wired network connection"));
		break;
	default:
		break;
	}

	g_free (iface);
	return pixbuf;
}

static void
add_default_wired_connection (AppletDbusSettings *settings)
{
	GSList *connections;
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingWired *s_wired;

	connections = applet_dbus_settings_list_connections (settings);
	if (g_slist_length (connections) > 0)
		return;

	connection = nm_connection_new ();

	s_con = (NMSettingConnection *) nm_setting_connection_new ();
	s_con->id = g_strdup ("Auto Ethernet");
	s_con->type = g_strdup ("802-3-ethernet");
	s_con->autoconnect = TRUE;

	s_wired = (NMSettingWired *) nm_setting_wired_new ();

	nm_connection_add_setting (connection, (NMSetting *) s_wired);
	nm_connection_add_setting (connection, (NMSetting *) s_con);

	applet_dbus_settings_user_add_connection (settings, connection);
}

NMADeviceClass *
applet_device_wired_get_class (NMApplet *applet)
{
	NMADeviceClass *dclass;

	dclass = g_slice_new0 (NMADeviceClass);
	if (!dclass)
		return NULL;

	dclass->new_auto_connection = wired_new_auto_connection;
	dclass->add_menu_item = wired_add_menu_item;
	dclass->device_state_changed = wired_device_state_changed;
	dclass->get_icon = wired_get_icon;

	add_default_wired_connection ((AppletDbusSettings *) applet->settings);

	return dclass;
}

