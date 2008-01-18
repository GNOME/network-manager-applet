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
#include <nm-setting-gsm.h>
#include <nm-setting-serial.h>
#include <nm-setting-ppp.h>
#include <nm-gsm-device.h>

#include "applet.h"
#include "applet-dbus-settings.h"
#include "applet-device-gsm.h"
#include "utils.h"

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMConnection *connection;
} GSMMenuItemInfo;

static void
gsm_menu_item_info_destroy (gpointer data)
{
	g_slice_free (GSMMenuItemInfo, data);
}

static NMConnection *
gsm_new_auto_connection (NMDevice *device,
                         NMApplet *applet,
                         gpointer user_data)
{
	NMConnection *connection;
	NMSettingGsm *s_gsm;
	NMSettingSerial *s_serial;
	NMSettingPPP *s_ppp;
	NMSettingConnection *s_con;

	connection = nm_connection_new ();

	s_gsm = NM_SETTING_GSM (nm_setting_gsm_new ());
	s_gsm->number = g_strdup ("*99#"); /* This should be a sensible default as it's seems to be quite standard */

	/* Serial setting */
	s_serial = (NMSettingSerial *) nm_setting_serial_new ();
	s_serial->baud = 115200;
	s_serial->bits = 8;
	s_serial->parity = 'n';
	s_serial->stopbits = 1;
	nm_connection_add_setting (connection, NM_SETTING (s_serial));

	s_ppp = (NMSettingPPP *) nm_setting_ppp_new ();
	s_ppp->usepeerdns = TRUE; /* This is probably a good default as well */
	nm_connection_add_setting (connection, NM_SETTING (s_ppp));

	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	s_con->id = g_strdup (_("Auto GSM dialup connection"));
	s_con->type = g_strdup (NM_SETTING (s_gsm)->name);
	s_con->autoconnect = FALSE;
	nm_connection_add_setting (connection, NM_SETTING (s_con));

	return connection;
}

static void
gsm_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	GSMMenuItemInfo *info = (GSMMenuItemInfo *) user_data;

	applet_menu_item_activate_helper (info->device,
	                                  info->connection,
	                                  "/",
	                                  info->applet,
	                                  user_data);
}

static void
gsm_add_menu_item (NMDevice *device,
                   guint32 n_devices,
                   GtkWidget *menu,
                   NMApplet *applet)
{
	GSMMenuItemInfo *info;
	char *text;
	GtkCheckMenuItem *item;
	GSList *connections, *all, *iter;

	if (n_devices > 1) {
		const char *desc;
		char *dev_name = NULL;

		desc = utils_get_device_description (device);
		if (desc)
			dev_name = g_strdup (desc);
		if (!dev_name)
			dev_name = nm_device_get_iface (device);
		g_assert (dev_name);
		text = g_strdup_printf (_("GSM Modem (%s)"), dev_name);
		g_free (dev_name);
	} else
		text = g_strdup (_("_GSM Modem"));

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

			info = g_slice_new0 (GSMMenuItemInfo);
			info->applet = applet;
			info->device = g_object_ref (G_OBJECT (device));
			info->connection = g_object_ref (connection);

			g_signal_connect_data (subitem, "activate",
			                       G_CALLBACK (gsm_menu_item_activate),
			                       info,
			                       (GClosureNotify) gsm_menu_item_info_destroy, 0);

			gtk_menu_shell_append (GTK_MENU_SHELL (submenu), GTK_WIDGET (subitem));
		}

		gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	} else {
		NMConnection *connection;

		info = g_slice_new0 (GSMMenuItemInfo);
		info->applet = applet;
		info->device = g_object_ref (G_OBJECT (device));

		if (g_slist_length (connections) == 1) {
			connection = NM_CONNECTION (g_slist_nth_data (connections, 0));
			info->connection = g_object_ref (G_OBJECT (connection));
		}

		g_signal_connect_data (item, "activate",
		                       G_CALLBACK (gsm_menu_item_activate),
		                       info,
		                       (GClosureNotify) gsm_menu_item_info_destroy, 0);
	}
	g_slist_free (connections);

	gtk_check_menu_item_set_draw_as_radio (item, TRUE);
	gtk_check_menu_item_set_active (item, nm_device_get_state (device) == NM_DEVICE_STATE_ACTIVATED);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (item));
	gtk_widget_show (GTK_WIDGET (item));

	info = g_slice_new (GSMMenuItemInfo);
	info->applet = applet;
	info->device = device;
	info->connection = NULL; // FIXME

	g_signal_connect_data (item, "activate",
	                       G_CALLBACK (gsm_menu_item_activate),
	                       info,
	                       (GClosureNotify) gsm_menu_item_info_destroy, 0);
}

static void
gsm_device_state_changed (NMDevice *device,
                          NMDeviceState state,
                          NMApplet *applet)
{
	if (state == NM_DEVICE_STATE_ACTIVATED) {
		applet_do_notify (applet, NOTIFY_URGENCY_LOW,
					      _("Connection Established"),
						  _("You are now connected to the GSM network."),
						  "nm-adhoc");
	}
}

static GdkPixbuf *
gsm_get_icon (NMDevice *device,
              NMDeviceState state,
              char **tip,
              NMApplet *applet)
{
	GdkPixbuf *pixbuf = NULL;
	char *iface;

	iface = nm_device_get_iface (NM_DEVICE (device));

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		*tip = g_strdup_printf (_("Dialing GSM device %s..."), iface);
		break;
	case NM_DEVICE_STATE_CONFIG:
		*tip = g_strdup_printf (_("Running PPP on device %s..."), iface);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		*tip = g_strdup (_("GSM connection"));
		// FIXME: get a real icon
		pixbuf = applet->adhoc_icon;
		break;
	default:
		break;
	}

	g_free (iface);
	return pixbuf;
}

NMADeviceClass *
applet_device_gsm_get_class (NMApplet *applet)
{
	NMADeviceClass *dclass;

	dclass = g_slice_new0 (NMADeviceClass);
	if (!dclass)
		return NULL;

	dclass->new_auto_connection = gsm_new_auto_connection;
	dclass->add_menu_item = gsm_add_menu_item;
	dclass->device_state_changed = gsm_device_state_changed;
	dclass->get_icon = gsm_get_icon;

	return dclass;
}

