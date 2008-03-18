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
#include <nm-setting-8021x.h>
#include <nm-device-802-3-ethernet.h>

#include "applet.h"
#include "applet-dbus-settings.h"
#include "applet-device-wired.h"
#include "wired-dialog.h"
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

#define DEFAULT_WIRED_NAME _("Auto Ethernet")

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
	s_con->id = g_strdup (DEFAULT_WIRED_NAME);
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
add_connection_items (NMDevice *device,
                      GSList *connections,
                      gboolean carrier,
                      NMConnection *active,
                      GtkWidget *menu,
                      NMApplet *applet)
{
	GSList *iter;
	WiredMenuItemInfo *info;

	for (iter = connections; iter; iter = g_slist_next (iter)) {
		NMConnection *connection = NM_CONNECTION (iter->data);
		NMSettingConnection *s_con;
		GtkWidget *item;

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		item = gtk_check_menu_item_new_with_label (s_con->id);
 		gtk_widget_set_sensitive (GTK_WIDGET (item), carrier);
		gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);

		if (connection == active)
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

		info = g_slice_new0 (WiredMenuItemInfo);
		info->applet = applet;
		info->device = g_object_ref (G_OBJECT (device));
		info->connection = g_object_ref (connection);

		g_signal_connect_data (item, "activate",
		                       G_CALLBACK (wired_menu_item_activate),
		                       info,
		                       (GClosureNotify) wired_menu_item_info_destroy, 0);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}
}

static void
add_default_connection_item (NMDevice *device,
                             gboolean carrier,
                             GtkWidget *menu,
                             NMApplet *applet)
{
	WiredMenuItemInfo *info;
	GtkWidget *item;
	
	item = gtk_check_menu_item_new_with_label (DEFAULT_WIRED_NAME);
	gtk_widget_set_sensitive (GTK_WIDGET (item), carrier);
	gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);

	info = g_slice_new0 (WiredMenuItemInfo);
	info->applet = applet;
	info->device = g_object_ref (G_OBJECT (device));

	g_signal_connect_data (item, "activate",
	                       G_CALLBACK (wired_menu_item_activate),
	                       info,
	                       (GClosureNotify) wired_menu_item_info_destroy, 0);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

static void
wired_add_menu_item (NMDevice *device,
                     guint32 n_devices,
                     NMConnection *active,
                     GtkWidget *menu,
                     NMApplet *applet)
{
	char *text;
	GtkWidget *item;
	GSList *connections, *all;
	gboolean carrier = TRUE;
	GtkWidget *label;
	char *bold_text;

	all = applet_dbus_settings_get_all_connections (APPLET_DBUS_SETTINGS (applet->settings));
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	if (n_devices > 1) {
		const char *desc;
		char *dev_name = NULL;

		desc = utils_get_device_description (device);
		if (desc)
			dev_name = g_strdup (desc);
		if (!dev_name)
			dev_name = nm_device_get_iface (device);
		g_assert (dev_name);

		if (g_slist_length (connections) > 1)
			text = g_strdup_printf (_("Wired Networks (%s)"), dev_name);
		else
			text = g_strdup_printf (_("Wired Network (%s)"), dev_name);
		g_free (dev_name);
	} else {
		if (g_slist_length (connections) > 1)
			text = g_strdup (_("Wired Networks"));
		else
			text = g_strdup (_("Wired Network"));
	}

	item = gtk_menu_item_new_with_label (text);
	g_free (text);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* Only dim the item if the device supports carrier detection AND
	 * we know it doesn't have a link.
	 */
 	if (nm_device_get_capabilities (device) & NM_DEVICE_CAP_CARRIER_DETECT)
		carrier = nm_device_802_3_ethernet_get_carrier (NM_DEVICE_802_3_ETHERNET (device));

	label = gtk_bin_get_child (GTK_BIN (item));
	bold_text = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>",
	                                     gtk_label_get_text (GTK_LABEL (label)));
	gtk_label_set_markup (GTK_LABEL (label), bold_text);
	g_free (bold_text);

	if (g_slist_length (connections))
		add_connection_items (device, connections, carrier, active, menu, applet);
	else
		add_default_connection_item (device, carrier, menu, applet);

	gtk_widget_set_sensitive (item, FALSE);
	gtk_widget_show (item);
	g_slist_free (connections);
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
get_secrets_dialog_response_cb (GtkDialog *dialog,
                                gint response,
                                gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	DBusGMethodInvocation *context;
	AppletExportedConnection *exported;
	NMConnection *connection = NULL;
	NMSetting *setting;
	GHashTable *settings_hash;
	GHashTable *secrets;
	GError *err = NULL;

	context = g_object_get_data (G_OBJECT (dialog), "dbus-context");
	if (!context) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't get dialog data",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	if (response != GTK_RESPONSE_OK) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): canceled",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	connection = nma_wired_dialog_get_connection (GTK_WIDGET (dialog));
	if (!connection) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't get connection from wired dialog.",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
	if (!setting) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
					 "%s.%d (%s): requested setting '802-1x' didn't"
					 " exist in the connection.",
					 __FILE__, __LINE__, __func__);
		goto done;
	}

	secrets = nm_setting_to_hash (setting);
	if (!secrets) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
					 "%s.%d (%s): failed to hash setting '%s'.",
					 __FILE__, __LINE__, __func__, setting->name);
		goto done;
	}

	utils_fill_connection_certs (connection);
	utils_clear_filled_connection_certs (connection);

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
										   g_free, (GDestroyNotify) g_hash_table_destroy);

	g_hash_table_insert (settings_hash, g_strdup (setting->name), secrets);
	dbus_g_method_return (context, settings_hash);
	g_hash_table_destroy (settings_hash);

	/* Save the connection back to GConf _after_ hashing it, because
	 * saving to GConf might trigger the GConf change notifiers, resulting
	 * in the connection being read back in from GConf which clears secrets.
	 */
	exported = applet_dbus_settings_user_get_by_connection (applet->settings, connection);
	if (exported)
		applet_exported_connection_save (exported);

done:
	if (err) {
		g_warning (err->message);

		if (context)
			dbus_g_method_return_error (context, err);

		g_error_free (err);
	}

	if (connection)
		nm_connection_clear_secrets (connection);
	gtk_widget_hide (GTK_WIDGET (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gboolean
wired_get_secrets (NMDevice *device,
				   NMConnection *connection,
				   const char *specific_object,
				   const char *setting_name,
				   DBusGMethodInvocation *context,
				   NMApplet *applet,
				   GError **error)
{
	GtkWidget *dialog;

	dialog = nma_wired_dialog_new (applet->glade_file,
								   applet->nm_client,
								   g_object_ref (connection),
								   device);
	if (!dialog) {
		g_set_error (error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't display secrets UI",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	g_object_set_data (G_OBJECT (dialog), "dbus-context", context);
	g_object_set_data_full (G_OBJECT (dialog),
	                        "setting-name", g_strdup (setting_name),
	                        (GDestroyNotify) g_free);

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (get_secrets_dialog_response_cb),
	                  applet);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gtk_window_present (GTK_WINDOW (dialog));

	return TRUE;
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
	dclass->get_secrets = wired_get_secrets;

	return dclass;
}
