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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2008 Red Hat, Inc.
 * (C) Copyright 2008 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <nm-device.h>
#include <nm-setting-connection.h>
#include <nm-setting-bluetooth.h>
#include <nm-device-bt.h>
#include <nm-utils.h>

#include "applet.h"
#include "applet-device-bt.h"
#include "wired-dialog.h"
#include "utils.h"
#include "gconf-helpers.h"

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMConnection *connection;
} BtMenuItemInfo;

static void
bt_menu_item_info_destroy (gpointer data)
{
	BtMenuItemInfo *info = data;

	g_object_unref (G_OBJECT (info->device));
	if (info->connection)
		g_object_unref (G_OBJECT (info->connection));

	g_slice_free (BtMenuItemInfo, data);
}

static gboolean
bt_new_auto_connection (NMDevice *device,
                        gpointer dclass_data,
                        AppletNewAutoConnectionCallback callback,
                        gpointer callback_data)
{

	// FIXME: call gnome-bluetooth setup wizard
	return FALSE;
}

static void
bt_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	BtMenuItemInfo *info = user_data;

	applet_menu_item_activate_helper (info->device,
	                                  info->connection,
	                                  "/",
	                                  info->applet,
	                                  user_data);
}


typedef enum {
	ADD_ACTIVE = 1,
	ADD_INACTIVE = 2,
} AddActiveInactiveEnum;

static void
add_connection_items (NMDevice *device,
                      GSList *connections,
                      gboolean carrier,
                      NMConnection *active,
                      AddActiveInactiveEnum flag,
                      GtkWidget *menu,
                      NMApplet *applet)
{
	GSList *iter;
	BtMenuItemInfo *info;

	for (iter = connections; iter; iter = g_slist_next (iter)) {
		NMConnection *connection = NM_CONNECTION (iter->data);
		NMSettingConnection *s_con;
		GtkWidget *item;

		if (active == connection) {
			if ((flag & ADD_ACTIVE) == 0)
				continue;
		} else {
			if ((flag & ADD_INACTIVE) == 0)
				continue;
		}

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		item = gtk_image_menu_item_new_with_label (nm_setting_connection_get_id (s_con));
		gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
 		gtk_widget_set_sensitive (GTK_WIDGET (item), carrier);

		info = g_slice_new0 (BtMenuItemInfo);
		info->applet = applet;
		info->device = g_object_ref (G_OBJECT (device));
		info->connection = g_object_ref (connection);

		g_signal_connect_data (item, "activate",
		                       G_CALLBACK (bt_menu_item_activate),
		                       info,
		                       (GClosureNotify) bt_menu_item_info_destroy, 0);

		applet_menu_item_favorize_helper (GTK_BIN (item), applet->favorites_icon, TRUE);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}
}

static void
bt_add_menu_item (NMDevice *device,
                  guint32 n_devices,
                  NMConnection *active,
                  GtkWidget *menu,
                  NMApplet *applet)
{
	const char *text;
	GtkWidget *item;
	GSList *connections, *all;
	gboolean carrier = TRUE;
	char *bold_text;

	all = applet_get_all_connections (applet);
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	text = nm_device_bt_get_name (NM_DEVICE_BT (device));
	if (!text) {
		text = utils_get_device_description (device);
		if (!text)
			text = nm_device_get_iface (device);
		g_assert (text);
	}

	item = gtk_menu_item_new_with_label ("");
	bold_text = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", text);
	gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child (GTK_BIN (item))), bold_text);
	g_free (bold_text);

	gtk_widget_set_sensitive (item, FALSE);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	if (active)
		applet_menu_item_add_complex_separator_helper (menu, applet, _("Active"), NULL, -1);

	if (g_slist_length (connections))
		add_connection_items (device, connections, carrier, active, ADD_ACTIVE, menu, applet);

	/* Notify user of unmanaged or unavailable device */
	item = nma_menu_device_get_menu_item (device, applet, NULL);
	if (item) {
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	if (!nma_menu_device_check_unusable (device)) {
		/* Add menu items for existing bluetooth connections for this device */
		if (g_slist_length (connections)) {
			applet_menu_item_add_complex_separator_helper (menu, applet, _("Available"), NULL, -1);
			add_connection_items (device, connections, carrier, active, ADD_INACTIVE, menu, applet);
		}
	}

	g_slist_free (connections);
}

static void
bt_device_state_changed (NMDevice *device,
                         NMDeviceState new_state,
                         NMDeviceState old_state,
                         NMDeviceStateReason reason,
                         NMApplet *applet)
{
	if (new_state == NM_DEVICE_STATE_ACTIVATED) {
		NMConnection *connection;
		NMSettingConnection *s_con = NULL;
		char *str = NULL;

		connection = applet_find_active_connection_for_device (device, applet, NULL);
		if (connection) {
			const char *id;
			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			id = s_con ? nm_setting_connection_get_id (s_con) : NULL;
			if (id)
				str = g_strdup_printf (_("You are now connected to '%s'."), id);
		}

		applet_do_notify_with_pref (applet,
		                            _("Connection Established"),
		                            str ? str : _("You are now connected to the mobile broadband network."),
		                            "nm-device-wwan",
		                            PREF_DISABLE_CONNECTED_NOTIFICATIONS);
		g_free (str);
	}
}

static GdkPixbuf *
bt_get_icon (NMDevice *device,
             NMDeviceState state,
             NMConnection *connection,
             char **tip,
             NMApplet *applet)
{
	NMSettingConnection *s_con;
	GdkPixbuf *pixbuf = NULL;
	const char *id;

	id = nm_device_get_iface (NM_DEVICE (device));
	if (connection) {
		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		id = nm_setting_connection_get_id (s_con);
	}

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		*tip = g_strdup_printf (_("Preparing mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_CONFIG:
		*tip = g_strdup_printf (_("Configuring mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_NEED_AUTH:
		*tip = g_strdup_printf (_("User authentication required for mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		*tip = g_strdup_printf (_("Requesting a network address for '%s'..."), id);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		pixbuf = applet->wwan_icon;
		*tip = g_strdup_printf (_("Mobile broadband connection '%s' active"), id);
		break;
	default:
		break;
	}

	return pixbuf;
}

NMADeviceClass *
applet_device_bt_get_class (NMApplet *applet)
{
	NMADeviceClass *dclass;

	dclass = g_slice_new0 (NMADeviceClass);
	if (!dclass)
		return NULL;

	dclass->new_auto_connection = bt_new_auto_connection;
	dclass->add_menu_item = bt_add_menu_item;
	dclass->device_state_changed = bt_device_state_changed;
	dclass->get_icon = bt_get_icon;

	return dclass;
}
