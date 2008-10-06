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
#include <nm-utils.h>

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

#define DEFAULT_CDMA_NAME _("Auto Mobile Broadband (CDMA) connection")

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
	s_con->uuid = nm_utils_uuid_generate ();
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

		text = g_strdup_printf (_("Mobile Broadband (%s)"), desc);
	} else {
		text = g_strdup (_("Mobile Broadband"));
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
			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			if (s_con && s_con->id)
				str = g_strdup_printf (_("You are now connected to '%s'."), s_con->id);
		}

		applet_do_notify (applet, NOTIFY_URGENCY_LOW,
					      _("Connection Established"),
						  str ? str : _("You are now connected to the CDMA network."),
						  "nm-device-wwan", NULL, NULL, NULL, NULL);
		g_free (str);
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
		*tip = g_strdup_printf (_("Dialing mobile broadband device %s..."), iface);
		break;
	case NM_DEVICE_STATE_CONFIG:
		*tip = g_strdup_printf (_("Starting PPP on device %s..."), iface);
		break;
	case NM_DEVICE_STATE_NEED_AUTH:
		*tip = g_strdup_printf (_("Waiting for user authentication on device '%s'..."), iface);
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

typedef struct {
	DBusGMethodInvocation *context;
	NMApplet *applet;
	NMConnection *connection;
	GtkWidget *ok_button;
	GtkEntry *secret_entry;
	char *secret_name;
} NMCdmaInfo;

static void
get_cdma_secrets_cb (GtkDialog *dialog,
                     gint response,
                     gpointer user_data)
{
	NMCdmaInfo *info = (NMCdmaInfo *) user_data;
	NMAGConfConnection *gconf_connection;
	NMSettingCdma *setting;
	GHashTable *settings_hash;
	GHashTable *secrets;
	GError *err = NULL;

	if (response != GTK_RESPONSE_OK) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): canceled",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	setting = NM_SETTING_CDMA (nm_connection_get_setting (info->connection, NM_TYPE_SETTING_CDMA));

	if (!strcmp (info->secret_name, NM_SETTING_CDMA_PASSWORD)) {
		g_free (setting->password);
		setting->password = g_strdup (gtk_entry_get_text (info->secret_entry));
	}

	secrets = nm_setting_to_hash (NM_SETTING (setting));
	if (!secrets) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
				   "%s.%d (%s): failed to hash setting '%s'.",
				   __FILE__, __LINE__, __func__, NM_SETTING (setting)->name);
		goto done;
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
								    g_free, (GDestroyNotify) g_hash_table_destroy);

	g_hash_table_insert (settings_hash, g_strdup (NM_SETTING (setting)->name), secrets);
	dbus_g_method_return (info->context, settings_hash);
	g_hash_table_destroy (settings_hash);

	/* Save the connection back to GConf _after_ hashing it, because
	 * saving to GConf might trigger the GConf change notifiers, resulting
	 * in the connection being read back in from GConf which clears secrets.
	 */
	gconf_connection = nma_gconf_settings_get_by_connection (info->applet->gconf_settings, info->connection);
	if (gconf_connection)
		nma_gconf_connection_save (gconf_connection);

 done:
	if (err) {
		g_warning ("%s", err->message);
		dbus_g_method_return_error (info->context, err);
		g_error_free (err);
	}

	gtk_widget_hide (GTK_WIDGET (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));

	nm_connection_clear_secrets (info->connection);
	g_object_unref (info->connection);
	g_free (info->secret_name);
	g_free (info);
}


static void
ask_for_password (NMDevice *device,
                  NMConnection *connection,
                  DBusGMethodInvocation *context,
                  NMApplet *applet)
{
	GtkDialog *dialog;
	GtkWidget *w;
	GtkBox *box;
	char *dev_str;
	NMCdmaInfo *info;
	NMSettingConnection *s_con;
	char *tmp;

	info = g_new (NMCdmaInfo, 1);
	info->context = context;
	info->applet = applet;
	info->connection = g_object_ref (connection);
	info->secret_name = g_strdup (NM_SETTING_CDMA_PASSWORD);

	dialog = GTK_DIALOG (gtk_dialog_new ());
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Mobile broadband network password"));

	w = gtk_dialog_add_button (dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	w = gtk_dialog_add_button (dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
	info->ok_button = w;
	gtk_window_set_default (GTK_WINDOW (dialog), info->ok_button);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con->id);
	tmp = g_strdup_printf (_("A password is required to connect to '%s'."), s_con->id);
	w = gtk_label_new (tmp);
	g_free (tmp);
	gtk_box_pack_start (GTK_BOX (dialog->vbox), w, TRUE, TRUE, 0);

	dev_str = g_strdup_printf ("<b>%s</b>", utils_get_device_description (device));
	w = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (w), dev_str);
	g_free (dev_str);
	gtk_box_pack_start (GTK_BOX (dialog->vbox), w, TRUE, TRUE, 0);

	w = gtk_alignment_new (0.5, 0.5, 0, 1.0);
	gtk_box_pack_start (GTK_BOX (dialog->vbox), w, TRUE, TRUE, 0);

	box = GTK_BOX (gtk_hbox_new (FALSE, 6));
	gtk_container_set_border_width (GTK_CONTAINER (box), 6);
	gtk_container_add (GTK_CONTAINER (w), GTK_WIDGET (box));

	gtk_box_pack_start (box, gtk_label_new (_("Password:")), FALSE, FALSE, 0);

	w = gtk_entry_new ();
	info->secret_entry = GTK_ENTRY (w);
	gtk_entry_set_activates_default (GTK_ENTRY (w), TRUE);
	gtk_box_pack_start (box, w, FALSE, FALSE, 0);

	gtk_widget_show_all (dialog->vbox);

	g_signal_connect (dialog, "response",
				   G_CALLBACK (get_cdma_secrets_cb),
				   info);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (GTK_WIDGET (dialog));
	gtk_window_present (GTK_WINDOW (dialog));
}

static gboolean
cdma_get_secrets (NMDevice *device,
                 NMConnection *connection,
                 const char *specific_object,
                 const char *setting_name,
                 const char **hints,
                 DBusGMethodInvocation *context,
                 NMApplet *applet,
                 GError **error)
{
	if (!hints || !g_strv_length ((char **) hints)) {
		g_set_error (error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): missing secrets hints.",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	if (!strcmp (hints[0], NM_SETTING_CDMA_PASSWORD))
		ask_for_password (device, connection, context, applet);
	else {
		g_set_error (error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): unknown secrets hint '%s'.",
		             __FILE__, __LINE__, __func__, hints[0]);
		return FALSE;
	}

	return TRUE;
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
	dclass->get_secrets = cdma_get_secrets;

	return dclass;
}

