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
#include <nm-setting-pppoe.h>
#include <nm-device-ethernet.h>
#include <nm-utils.h>

#include "applet.h"
#include "applet-device-wired.h"
#include "wired-dialog.h"
#include "utils.h"
#include "gconf-helpers.h"

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
	char *uuid;

	connection = nm_connection_new ();

	s_wired = NM_SETTING_WIRED (nm_setting_wired_new ());
	nm_connection_add_setting (connection, NM_SETTING (s_wired));

	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	uuid = nm_utils_uuid_generate ();
	g_object_set (s_con,
				  NM_SETTING_CONNECTION_ID, DEFAULT_WIRED_NAME,
				  NM_SETTING_CONNECTION_TYPE, NM_SETTING (s_wired)->name,
				  NM_SETTING_CONNECTION_AUTOCONNECT, TRUE,
				  NM_SETTING_CONNECTION_UUID, uuid,
				  NULL);
	g_free (uuid);

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
		item = gtk_check_menu_item_new_with_label (nm_setting_connection_get_id (s_con));
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

	all = applet_get_all_connections (applet);
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	if (n_devices > 1) {
		char *desc = NULL;

		desc = (char *) utils_get_device_description (device);
		if (!desc)
			desc = (char *) nm_device_get_iface (device);
		g_assert (desc);

		if (g_slist_length (connections) > 1)
			text = g_strdup_printf (_("Wired Networks (%s)"), desc);
		else
			text = g_strdup_printf (_("Wired Network (%s)"), desc);
	} else {
		if (g_slist_length (connections) > 1)
			text = g_strdup (_("Wired Networks"));
		else
			text = g_strdup (_("Wired Network"));
	}

	item = gtk_menu_item_new_with_label (text);
	g_free (text);

	/* Only dim the item if the device supports carrier detection AND
	 * we know it doesn't have a link.
	 */
 	if (nm_device_get_capabilities (device) & NM_DEVICE_CAP_CARRIER_DETECT)
		carrier = nm_device_ethernet_get_carrier (NM_DEVICE_ETHERNET (device));

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
		add_connection_items (device, connections, carrier, active, menu, applet);
	else
		add_default_connection_item (device, carrier, menu, applet);

out:
	g_slist_free (connections);
}

static void
wired_device_state_changed (NMDevice *device,
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

		applet_do_notify (applet, NOTIFY_URGENCY_LOW,
					      _("Connection Established"),
						  str ? str : _("You are now connected to the wired network."),
						  "nm-device-wired", NULL, NULL, NULL, NULL);
		g_free (str);
	}
}

static GdkPixbuf *
wired_get_icon (NMDevice *device,
                NMDeviceState state,
                NMConnection *connection,
                char **tip,
                NMApplet *applet)
{
	GdkPixbuf *pixbuf = NULL;
	const char *iface;

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

	return pixbuf;
}

/* PPPoE */

typedef struct {
	GladeXML *xml;
	GtkEntry *username_entry;
	GtkEntry *service_entry;
	GtkEntry *password_entry;
	GtkWidget *ok_button;

	NMApplet *applet;
	NMConnection *connection;
	DBusGMethodInvocation *context;
} NMPppoeInfo;

static void
pppoe_verify (GtkEditable *editable, gpointer user_data)
{
	NMPppoeInfo *info = (NMPppoeInfo *) user_data;
	const char *s;
	gboolean valid = TRUE;

	s = gtk_entry_get_text (info->username_entry);
	if (!s || strlen (s) < 1)
		valid = FALSE;

	if (valid) {
		s = gtk_entry_get_text (info->password_entry);
		if (!s || strlen (s) < 1)
			valid = FALSE;
	}

	gtk_widget_set_sensitive (info->ok_button, valid);
}

static void
pppoe_update_setting (NMSettingPPPOE *pppoe, NMPppoeInfo *info)
{
	const char *s;

	s = gtk_entry_get_text (info->service_entry);
	if (s && strlen (s) < 1)
		s = NULL;

	g_object_set (pppoe,
				  NM_SETTING_PPPOE_USERNAME, gtk_entry_get_text (info->username_entry),
				  NM_SETTING_PPPOE_PASSWORD, gtk_entry_get_text (info->password_entry),
				  NM_SETTING_PPPOE_SERVICE, s,
				  NULL);
}

static void
pppoe_update_ui (NMConnection *connection, NMPppoeInfo *info)
{
	NMSettingPPPOE *s_pppoe;
	const char *s;

	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (info != NULL);

	s_pppoe = (NMSettingPPPOE *) nm_connection_get_setting (connection, NM_TYPE_SETTING_PPPOE);
	g_return_if_fail (s_pppoe != NULL);

	s = nm_setting_pppoe_get_username (s_pppoe);
	if (s)
		gtk_entry_set_text (info->username_entry, s);

	s = nm_setting_pppoe_get_service (s_pppoe);
	if (s)
		gtk_entry_set_text (info->service_entry, s);

	s = nm_setting_pppoe_get_password (s_pppoe);
	if (s)
		gtk_entry_set_text (info->password_entry, s);
	else {
		GHashTable *secrets;
		GError *error = NULL;
		GValue *value;

		/* Grab password from keyring if possible */
		secrets = nm_gconf_get_keyring_items (connection,
		                                      nm_setting_get_name (NM_SETTING (s_pppoe)),
		                                      FALSE,
		                                      &error);
		if (secrets) {
			value = g_hash_table_lookup (secrets, NM_SETTING_PPPOE_PASSWORD);
			if (value)
				gtk_entry_set_text (info->password_entry, g_value_get_string (value));
			g_hash_table_destroy (secrets);
		} else if (error)
			g_error_free (error);
	}
}

static NMPppoeInfo *
pppoe_info_new (GladeXML *xml)
{
	NMPppoeInfo *info;

	info = g_new0 (NMPppoeInfo, 1);
	info->xml = xml;
	
	info->username_entry = GTK_ENTRY (glade_xml_get_widget (xml, "dsl_username"));
	g_signal_connect (info->username_entry, "changed", G_CALLBACK (pppoe_verify), info);

	info->service_entry = GTK_ENTRY (glade_xml_get_widget (xml, "dsl_service"));

	info->password_entry = GTK_ENTRY (glade_xml_get_widget (xml, "dsl_password"));
	g_signal_connect (info->password_entry, "changed", G_CALLBACK (pppoe_verify), info);

	return info;
}

static void
pppoe_info_destroy (gpointer data, GObject *destroyed_object)
{
	NMPppoeInfo *info = (NMPppoeInfo *) data;

	g_object_unref (info->connection);
	g_object_unref (info->xml);
	
	g_free (info);
}

static void
get_pppoe_secrets_cb (GtkDialog *dialog,
					  gint response,
					  gpointer user_data)
{
	NMPppoeInfo *info = (NMPppoeInfo *) user_data;
	NMAGConfConnection *gconf_connection;
	NMSetting *setting;
	GHashTable *settings_hash;
	GHashTable *secrets;
	GError *err = NULL;

	if (response != GTK_RESPONSE_OK) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): canceled",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	setting = nm_connection_get_setting (info->connection, NM_TYPE_SETTING_PPPOE);
	pppoe_update_setting (NM_SETTING_PPPOE (setting), info);

	secrets = nm_setting_to_hash (setting);
	if (!secrets) {
		g_set_error (&err, NM_SETTINGS_ERROR, 1,
					 "%s.%d (%s): failed to hash setting '%s'.",
					 __FILE__, __LINE__, __func__, setting->name);
		goto done;
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
										   g_free, (GDestroyNotify) g_hash_table_destroy);

	g_hash_table_insert (settings_hash, g_strdup (setting->name), secrets);
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

	nm_connection_clear_secrets (info->connection);
	gtk_widget_hide (GTK_WIDGET (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
show_password_toggled (GtkToggleButton *button, gpointer user_data)
{
	NMPppoeInfo *info = (NMPppoeInfo *) user_data;
	GtkWidget *entry;

	entry = glade_xml_get_widget (info->xml, "dsl_password");
	if (gtk_toggle_button_get_active (button))
		gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);
	else
		gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
}

static gboolean
pppoe_get_secrets (NMDevice *device,
				   NMConnection *connection,
				   const char *specific_object,
				   const char *setting_name,
				   DBusGMethodInvocation *context,
				   NMApplet *applet,
				   GError **error)
{
	GladeXML *xml;
	GtkWidget *dialog;
	NMPppoeInfo *info;
	GtkWidget *w;

	xml = glade_xml_new (GLADEDIR "/ce-page-dsl.glade", "DslPage", NULL);
	if (!xml) {
		g_set_error (error, NM_SETTINGS_ERROR, 1,
					 "%s.%d (%s): couldn't display secrets UI",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	info = pppoe_info_new (xml);
	info->applet = applet;
	info->context = context;
	info->connection = g_object_ref (connection);

	/* Create the dialog */
	dialog = gtk_dialog_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), _("DSL authentication"));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	w = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	w = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);
	info->ok_button = w;

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
	                    glade_xml_get_widget (xml, "DslPage"),
	                    TRUE, TRUE, 0);

	pppoe_update_ui (connection, info);
	g_object_weak_ref (G_OBJECT (dialog), pppoe_info_destroy, info);

	w = glade_xml_get_widget (xml, "dsl_show_password");
	g_signal_connect (G_OBJECT (w), "toggled", G_CALLBACK (show_password_toggled), info);

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (get_pppoe_secrets_cb),
	                  info);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gtk_window_present (GTK_WINDOW (dialog));

	return TRUE;
}

/* 802.1x */

static void
get_8021x_secrets_cb (GtkDialog *dialog,
					  gint response,
					  gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	DBusGMethodInvocation *context;
	NMAGConfConnection *gconf_connection;
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
	gconf_connection = nma_gconf_settings_get_by_connection (applet->gconf_settings, connection);
	if (gconf_connection)
		nma_gconf_connection_save (gconf_connection);

done:
	if (err) {
		g_warning ("%s", err->message);

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
nm_8021x_get_secrets (NMDevice *device,
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
	                  G_CALLBACK (get_8021x_secrets_cb),
	                  applet);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gtk_window_present (GTK_WINDOW (dialog));

	return TRUE;
}

static gboolean
wired_get_secrets (NMDevice *device,
				   NMConnection *connection,
				   const char *specific_object,
				   const char *setting_name,
				   const char **hints,
				   DBusGMethodInvocation *context,
				   NMApplet *applet,
				   GError **error)
{
	NMSettingConnection *s_con;
	const char *connection_type;
	gboolean success = FALSE;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con) {
		g_set_error (error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): Invalid connection",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	connection_type = nm_setting_connection_get_connection_type (s_con);
	if (!strcmp (connection_type, NM_SETTING_WIRED_SETTING_NAME)) {
		success = nm_8021x_get_secrets (device, connection, specific_object, setting_name, context, applet, error);
	} else if (!strcmp (connection_type, NM_SETTING_PPPOE_SETTING_NAME))
		success = pppoe_get_secrets (device, connection, specific_object, setting_name, context, applet, error);

	return success;
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
