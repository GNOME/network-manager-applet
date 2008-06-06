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
#include <nm-setting-cdma.h>
#include <nm-setting-serial.h>
#include <nm-setting-ppp.h>
#include <nm-cdma-device.h>

#include "applet.h"
#include "applet-device-cdma.h"
#include "utils.h"

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMConnection *connection;
} CdmaMenuItemInfo;

static void
cdma_menu_item_info_destroy (gpointer data)
{
	g_slice_free (CdmaMenuItemInfo, data);
}

#define DEFAULT_CDMA_NAME _("Auto CDMA network connection")

static NMConnection *
cdma_new_auto_connection (NMDevice *device,
                          NMApplet *applet,
                          gpointer user_data)
{
	NMConnection *connection;
	NMSettingCdma *s_cdma;
	NMSettingSerial *s_serial;
	NMSettingPPP *s_ppp;
	NMSettingConnection *s_con;

	connection = nm_connection_new ();

	s_cdma = NM_SETTING_CDMA (nm_setting_cdma_new ());
	s_cdma->number = g_strdup ("#777"); /* De-facto standard for CDMA */
	nm_connection_add_setting (connection, NM_SETTING (s_cdma));

	/* Serial setting */
	s_serial = (NMSettingSerial *) nm_setting_serial_new ();
	s_serial->baud = 115200;
	s_serial->bits = 8;
	s_serial->parity = 'n';
	s_serial->stopbits = 1;
	nm_connection_add_setting (connection, NM_SETTING (s_serial));

	s_ppp = (NMSettingPPP *) nm_setting_ppp_new ();
	nm_connection_add_setting (connection, NM_SETTING (s_ppp));

	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	s_con->id = g_strdup (DEFAULT_CDMA_NAME);
	s_con->type = g_strdup (NM_SETTING (s_cdma)->name);
	s_con->autoconnect = FALSE;
	nm_connection_add_setting (connection, NM_SETTING (s_con));

	return connection;
}

static void
cdma_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	CdmaMenuItemInfo *info = (CdmaMenuItemInfo *) user_data;

	applet_menu_item_activate_helper (info->device,
	                                  info->connection,
	                                  "/",
	                                  info->applet,
	                                  user_data);
}

static void
add_connection_items (NMDevice *device,
                      GSList *connections,
                      NMConnection *active,
                      GtkWidget *menu,
                      NMApplet *applet)
{
	GSList *iter;
	CdmaMenuItemInfo *info;

	for (iter = connections; iter; iter = g_slist_next (iter)) {
		NMConnection *connection = NM_CONNECTION (iter->data);
		NMSettingConnection *s_con;
		GtkWidget *item;

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		item = gtk_check_menu_item_new_with_label (s_con->id);
		gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);

		if (connection == active)
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

		info = g_slice_new0 (CdmaMenuItemInfo);
		info->applet = applet;
		info->device = g_object_ref (G_OBJECT (device));
		info->connection = g_object_ref (connection);

		g_signal_connect_data (item, "activate",
		                       G_CALLBACK (cdma_menu_item_activate),
		                       info,
		                       (GClosureNotify) cdma_menu_item_info_destroy, 0);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}
}

static void
add_default_connection_item (NMDevice *device,
                             GtkWidget *menu,
                             NMApplet *applet)
{
	CdmaMenuItemInfo *info;
	GtkWidget *item;
	
	item = gtk_check_menu_item_new_with_label (DEFAULT_CDMA_NAME);
	gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);

	info = g_slice_new0 (CdmaMenuItemInfo);
	info->applet = applet;
	info->device = g_object_ref (G_OBJECT (device));

	g_signal_connect_data (item, "activate",
	                       G_CALLBACK (cdma_menu_item_activate),
	                       info,
	                       (GClosureNotify) cdma_menu_item_info_destroy, 0);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

static void
cdma_menu_item_deactivate (GtkMenuItem *item, gpointer user_data)
{
	CdmaMenuItemInfo *info = (CdmaMenuItemInfo *) user_data;
	NMActiveConnection *active = NULL;

	applet_find_active_connection_for_device (info->device, info->applet, &active);
	if (active)
		nm_client_deactivate_connection (info->applet->nm_client, active);
	else
		g_warning ("%s: couldn't find active connection to deactive", __func__);
}

static void
add_disconnect_item (NMDevice *device,
                     GtkWidget *menu,
                     NMApplet *applet)
{
	NMDeviceState state;
	GtkWidget *item;
	CdmaMenuItemInfo *info;

	state = nm_device_get_state (device);
	if (   state == NM_DEVICE_STATE_UNKNOWN
	    || state == NM_DEVICE_STATE_UNMANAGED
	    || state == NM_DEVICE_STATE_UNAVAILABLE
	    || state == NM_DEVICE_STATE_DISCONNECTED
	    || state == NM_DEVICE_STATE_FAILED)
		return;

	item = gtk_menu_item_new_with_label (_("Disconnect"));

	info = g_slice_new0 (CdmaMenuItemInfo);
	info->applet = applet;
	info->device = g_object_ref (G_OBJECT (device));

	g_signal_connect_data (item, "activate",
	                       G_CALLBACK (cdma_menu_item_deactivate),
	                       info,
	                       (GClosureNotify) cdma_menu_item_info_destroy, 0);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

static void
cdma_add_menu_item (NMDevice *device,
                    guint32 n_devices,
                    NMConnection *active,
                    GtkWidget *menu,
                    NMApplet *applet)
{
	char *text;
	GtkWidget *item;
	GSList *connections, *all;
	GtkWidget *label;
	char *bold_text;

	all = applet_get_all_connections (applet);
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	if (n_devices > 1) {
		char *desc;

		desc = (char *) utils_get_device_description (device);
		if (!desc)
			desc = (char *) nm_device_get_iface (device);
		g_assert (desc);

		if (g_slist_length (connections) > 1)
			text = g_strdup_printf (_("CDMA Connections (%s)"), desc);
		else
			text = g_strdup_printf (_("CDMA Network (%s)"), desc);
	} else {
		if (g_slist_length (connections) > 1)
			text = g_strdup (_("CDMA Connections"));
		else
			text = g_strdup (_("CDMA Network"));
	}

	item = gtk_menu_item_new_with_label (text);
	g_free (text);

	label = gtk_bin_get_child (GTK_BIN (item));
	bold_text = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>",
	                                     gtk_label_get_text (GTK_LABEL (label)));
	gtk_label_set_markup (GTK_LABEL (label), bold_text);
	g_free (bold_text);

	gtk_widget_set_sensitive (item, FALSE);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	/* Notify user of unmanaged device */
	if (!nm_device_get_managed (device)) {
		item = gtk_menu_item_new_with_label (_("device is unmanaged"));
		gtk_widget_set_sensitive (item, FALSE);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
		goto out;
	}

	if (g_slist_length (connections))
		add_connection_items (device, connections, active, menu, applet);
	else
		add_default_connection_item (device, menu, applet);
	add_disconnect_item (device, menu, applet);

out:
	g_slist_free (connections);
}

static void
cdma_device_state_changed (NMDevice *device,
                           NMDeviceState state,
                           NMApplet *applet)
{
	if (state == NM_DEVICE_STATE_ACTIVATED) {
		applet_do_notify (applet, NOTIFY_URGENCY_LOW,
					      _("Connection Established"),
						  _("You are now connected to the CDMA network."),
						  "nm-device-wwan", NULL, NULL, NULL, NULL);
	}
}

static GdkPixbuf *
cdma_get_icon (NMDevice *device,
               NMDeviceState state,
               char **tip,
               NMApplet *applet)
{
	GdkPixbuf *pixbuf = NULL;
	const char *iface;

	iface = nm_device_get_iface (NM_DEVICE (device));

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		*tip = g_strdup_printf (_("Dialing CDMA device %s..."), iface);
		break;
	case NM_DEVICE_STATE_CONFIG:
		*tip = g_strdup_printf (_("Running PPP on device %s..."), iface);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		*tip = g_strdup (_("CDMA connection"));
		pixbuf = applet->wwan_icon;
		break;
	default:
		break;
	}

	return pixbuf;
}

NMADeviceClass *
applet_device_cdma_get_class (NMApplet *applet)
{
	NMADeviceClass *dclass;

	dclass = g_slice_new0 (NMADeviceClass);
	if (!dclass)
		return NULL;

	dclass->new_auto_connection = cdma_new_auto_connection;
	dclass->add_menu_item = cdma_add_menu_item;
	dclass->device_state_changed = cdma_device_state_changed;
	dclass->get_icon = cdma_get_icon;

	return dclass;
}

