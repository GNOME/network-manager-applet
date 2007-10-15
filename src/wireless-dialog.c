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
 * (C) Copyright 2007 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gtk/gtkcontainer.h>
#include <glade/glade.h>

#include <nm-client.h>
#include <nm-utils.h>
#include <nm-device-802-11-wireless.h>

#include "wireless-dialog.h"
#include "wireless-security.h"

#define D_NAME_COLUMN		0
#define D_DEV_COLUMN		1

#define S_NAME_COLUMN		0
#define S_SEC_COLUMN		1

static void
update_button_cb (GtkWidget *button,
                  gpointer user_data)
{
}

static void
device_combo_changed (GtkWidget *combo,
                      gpointer user_data)
{
}

static void
size_group_clear (GtkSizeGroup *group)
{
	GSList *children;
	GSList *iter;

	g_return_if_fail (group != NULL);

	children = gtk_size_group_get_widgets (group);
	for (iter = children; iter; iter = g_slist_next (iter))
		gtk_size_group_remove_widget (group, GTK_WIDGET (iter->data));
}

static void
size_group_add_permanent (GtkSizeGroup *group,
                          GladeXML *xml)
{
	GtkWidget *widget;

	g_return_if_fail (group != NULL);
	g_return_if_fail (xml != NULL);

	widget = glade_xml_get_widget (xml, "network_name_label");
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (xml, "security_combo_label");
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (xml, "device_label");
	gtk_size_group_add_widget (group, widget);
}

static void
security_combo_changed (GtkWidget *combo,
                        gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET (user_data);
	GladeXML *xml;
	GtkWidget *vbox;
	GList *elt, *children;
	GtkTreeIter iter;
	GtkTreeModel *model;
	WirelessSecurity *sec = NULL;
	GtkSizeGroup *group;

	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_assert (xml);

	vbox = glade_xml_get_widget (xml, "security_vbox");
	g_assert (vbox);

	group = g_object_get_data (G_OBJECT (dialog), "size-group");
	g_assert (group);
	size_group_clear (group);

	/* Remove any previous wireless security widgets */
	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (elt = children; elt; elt = g_list_next (elt))
		gtk_container_remove (GTK_CONTAINER (vbox), GTK_WIDGET (elt->data));

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter))
		gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &sec, -1);

	if (sec) {
		GtkWidget *sec_widget;

		sec_widget = wireless_security_get_widget (sec);
		g_assert (sec_widget);

		size_group_add_permanent (group, xml);
		wireless_security_add_to_size_group (sec, group);

		gtk_container_add (GTK_CONTAINER (vbox),
		                   sec_widget);
	}
}

GByteArray *
validate_dialog_ssid (GtkWidget *dialog)
{
	GladeXML *xml;
	GtkWidget *widget;
	const char *ssid;
	guint32 ssid_len;
	GByteArray *ssid_ba;

	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	widget = glade_xml_get_widget (xml, "network_name_entry");

	ssid = gtk_entry_get_text (GTK_ENTRY (widget));
	ssid_len = strlen (ssid);
	
	if (!ssid || !ssid_len || (ssid_len > 32))
		return NULL;

	ssid_len = strlen (ssid);
	ssid_ba = g_byte_array_sized_new (ssid_len);
	g_byte_array_append (ssid_ba, ssid, ssid_len);
	return ssid_ba;
}

static void
stuff_changed_cb (WirelessSecurity *sec, gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET (user_data);
	GladeXML *xml;
	GtkWidget *widget;
	GByteArray *ssid;
	gboolean valid = FALSE;
	
	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_assert (xml);

	ssid = validate_dialog_ssid (dialog);
	if (ssid) {
		valid = wireless_security_validate (sec, ssid);
		g_byte_array_free (ssid, TRUE);
	}
	widget = glade_xml_get_widget (xml, "ok_button");
	gtk_widget_set_sensitive (widget, valid);
}

static void
ssid_entry_changed (GtkWidget *entry, gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET (user_data);
	GtkTreeIter iter;
	WirelessSecurity *sec = NULL;
	GladeXML *xml;
	GtkWidget *combo;
	GtkTreeModel *model;
	gboolean valid = FALSE;
	GByteArray *ssid;
	GtkWidget *widget;

	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");

	ssid = validate_dialog_ssid (dialog);
	if (!ssid)
		goto out;

	combo = glade_xml_get_widget (xml, "security_combo");
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter))
		gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &sec, -1);

	if (sec)
		valid = wireless_security_validate (sec, ssid);
	else
		valid = TRUE;

out:
	widget = glade_xml_get_widget (xml, "ok_button");
	gtk_widget_set_sensitive (widget, valid);
}

static GtkTreeModel *
create_device_model (NMClient *client, guint32 *num)
{
	GtkListStore *model;
	GSList *devices;
	GSList *iter;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (num != NULL, NULL);

	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);
	*num = 0;

	devices = nm_client_get_devices (client);
	for (iter = devices; iter; iter = g_slist_next (iter)) {
		NMDevice *dev = (NMDevice *) iter->data;
		GtkTreeIter iter;
		char *name;

		/* Ignore unsupported devices */
		if (!(nm_device_get_capabilities (dev) & NM_DEVICE_CAP_NM_SUPPORTED))
			continue;

		if (!NM_IS_DEVICE_802_11_WIRELESS (dev))
			continue;

		name = nm_device_get_description (dev);
		if (!name)
			name = nm_device_get_iface (dev);

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
		                    D_NAME_COLUMN, name,
		                    D_DEV_COLUMN, g_object_ref (dev),
		                    -1);
		*num += 1;
	}
	g_slist_free (devices);

	g_object_ref (G_OBJECT (model));
	return GTK_TREE_MODEL (model);
}

static void
destroy_device_model (GtkTreeModel *model)
{
	GtkTreeIter	iter;

	g_return_if_fail (model != NULL);

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			char *str;
			NMDevice *dev;

			gtk_tree_model_get (model, &iter,
			                    D_NAME_COLUMN, &str,
			                    D_DEV_COLUMN, &dev,
			                    -1);
			g_free (str);
			g_object_unref (dev);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	g_object_unref (model);
}

static void
destroy_security_model (GtkTreeModel *model)
{
	GtkTreeIter	iter;

	g_return_if_fail (model != NULL);

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			WirelessSecurity *sec;

			gtk_tree_model_get (model, &iter,
			                    D_DEV_COLUMN, &sec,
			                    -1);
			wireless_security_destroy (sec);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	g_object_unref (model);
}

static void
add_security_item (GtkWidget *dialog,
                   WirelessSecurity *sec,
                   GtkListStore *model,
                   GtkTreeIter *iter,
                   const char *text)
{
	wireless_security_set_changed_notify (sec, stuff_changed_cb, dialog);
	gtk_list_store_append (model, iter);
	gtk_list_store_set (model, iter, S_NAME_COLUMN, text, S_SEC_COLUMN, sec, -1);
}

static gboolean
security_combo_init (const char *glade_file,
                     GtkWidget *combo,
                     NMDevice *device,
                     GtkWidget *dialog)
{
	GtkListStore *sec_model;
	GtkTreeIter iter;
	WirelessSecurityWEPKey *ws_wep_hex;
	WirelessSecurityWEPKey *ws_wep_ascii;
	WirelessSecurityWEPPassphrase *ws_wep_passphrase;
	WirelessSecurityLEAP *ws_leap;
	WirelessSecurityWPAPSK *ws_wpa_psk;
	WirelessSecurityWPAEAP *ws_wpa_eap;

	g_return_val_if_fail (combo != NULL, FALSE);
	g_return_val_if_fail (glade_file != NULL, FALSE);
	g_return_val_if_fail (device != NULL, FALSE);

	sec_model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);

	gtk_list_store_append (sec_model, &iter);
	gtk_list_store_set (sec_model, &iter,
	                    S_NAME_COLUMN, _("None"),
	                    S_SEC_COLUMN, NULL,
	                    -1);

	g_object_set_data_full (G_OBJECT (dialog),
	                        "security-model", sec_model,
	                        (GDestroyNotify) destroy_security_model);

	ws_wep_passphrase = ws_wep_passphrase_new (glade_file);
	if (ws_wep_passphrase) {
		add_security_item (dialog, WIRELESS_SECURITY (ws_wep_passphrase), sec_model,
		                   &iter, _("WEP 128-bit Passphrase"));
	}

	ws_wep_hex = ws_wep_key_new (glade_file, WEP_KEY_TYPE_HEX);
	if (ws_wep_hex) {
		add_security_item (dialog, WIRELESS_SECURITY (ws_wep_hex), sec_model,
		                   &iter, _("WEP 40/128-bit Hexadecimal"));
	}

	ws_wep_ascii = ws_wep_key_new (glade_file, WEP_KEY_TYPE_ASCII);
	if (ws_wep_ascii) {
		add_security_item (dialog, WIRELESS_SECURITY (ws_wep_ascii), sec_model,
		                   &iter, _("WEP 40/128-bit ASCII"));
	}

	ws_leap = ws_leap_new (glade_file);
	if (ws_leap) {
		add_security_item (dialog, WIRELESS_SECURITY (ws_leap), sec_model,
		                   &iter, _("LEAP"));
	}

	ws_wpa_psk = ws_wpa_psk_new (glade_file);
	if (ws_wpa_psk) {
		add_security_item (dialog, WIRELESS_SECURITY (ws_wpa_psk), sec_model,
		                   &iter, _("WPA Pre-Shared Key"));
	}

	ws_wpa_eap = ws_wpa_eap_new (glade_file);
	if (ws_wpa_eap) {
		add_security_item (dialog, WIRELESS_SECURITY (ws_wpa_eap), sec_model,
		                   &iter, _("WPA & WPA2 Enterprise"));
	}

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (sec_model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
	return TRUE;

error:
	g_object_unref (sec_model);
	return FALSE;
}

static GtkWidget *
dialog_init (GladeXML *xml,
             NMClient *nm_client,
             const char *glade_file)
{
	GtkWidget *dialog;
	GtkWidget *widget;
	GtkSizeGroup *group;
	GtkTreeModel *model;
	GtkTreeIter iter;
	guint32 num_devs = 0;
	char *label;
	NMDevice *dev;

	dialog = glade_xml_get_widget (xml, "wireless_dialog");
	if (!dialog) {
		nm_warning ("Couldn't find glade wireless_dialog widget.");
		return NULL;
	}

	g_object_set_data_full (G_OBJECT (dialog),
	                        "glade-xml", xml,
	                        (GDestroyNotify) g_object_unref);

	g_object_set_data_full (G_OBJECT (dialog),
	                        "nm-client", g_object_ref (nm_client),
	                        (GDestroyNotify) g_object_unref);

	widget = glade_xml_get_widget (xml, "ok_button");
	gtk_widget_grab_default (widget);
	gtk_widget_set_sensitive (widget, FALSE);
#if GTK_CHECK_VERSION(2,6,0)
	{
		GtkWidget *image = gtk_image_new_from_stock (GTK_STOCK_CONNECT, GTK_ICON_SIZE_BUTTON);
		gtk_button_set_image (GTK_BUTTON (widget), image);
	}
#endif

	widget = glade_xml_get_widget (xml, "network_name_entry");
	g_signal_connect (widget, "changed", G_CALLBACK (update_button_cb), dialog);
	gtk_widget_grab_focus (widget);

	model = create_device_model (nm_client, &num_devs);
	if (!model || (num_devs < 1)) {
		g_warning ("No wireless devices available.");
		destroy_device_model (model);
		return NULL;
	}
	g_object_set_data_full (G_OBJECT (dialog),
	                        "device-model", model,
	                        (GDestroyNotify) destroy_device_model);

	widget = glade_xml_get_widget (xml, "device_combo");
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), model);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  GTK_SIGNAL_FUNC (device_combo_changed), dialog);
	if (num_devs == 1) {
		gtk_widget_hide (glade_xml_get_widget (xml, "device_label"));
		gtk_widget_hide (widget);
	}

	gtk_window_set_title (GTK_WINDOW (dialog), _("Connect to Other Wireless Network"));
	label = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>\n\n%s",
	                         _("Existing wireless network"),
	                         _("Enter the name of the wireless network to which you wish to connect."));

	widget = glade_xml_get_widget (xml, "caption_label");
	gtk_label_set_markup (GTK_LABEL (widget), label);
	g_free (label);

	group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_set_ignore_hidden (group, TRUE);
	g_object_set_data_full (G_OBJECT (dialog),
	                        "size-group", group,
	                        (GDestroyNotify) g_object_unref);

	widget = glade_xml_get_widget (xml, "security_combo");
	g_assert (widget);
	gtk_tree_model_get_iter_first (model, &iter);
	gtk_tree_model_get (model, &iter, D_DEV_COLUMN, &dev, -1);

	if (!security_combo_init (glade_file, widget, dev, dialog)) {
		g_message ("Couldn't set up wireless security combo box.");
		gtk_widget_destroy (dialog);
		return NULL;
	}

	security_combo_changed (widget, dialog);
	g_signal_connect (G_OBJECT (widget), "changed", GTK_SIGNAL_FUNC (security_combo_changed), dialog);

	widget = glade_xml_get_widget (xml, "network_name_entry");
	g_signal_connect (G_OBJECT (widget), "changed", (GCallback) ssid_entry_changed, dialog);

	return dialog;
}

NMConnection *
nma_wireless_dialog_get_connection (GtkWidget *dialog, NMDevice **device)
{
	GladeXML *xml;
	GtkWidget *combo;
	GtkTreeModel *model;
	WirelessSecurity *sec = NULL;
	GtkTreeIter iter;
	NMConnection *connection;
	NMSettingWireless *s_wireless;

	g_return_val_if_fail (dialog != NULL, NULL);
	g_return_val_if_fail (device != NULL, NULL);
	g_return_val_if_fail (*device == NULL, NULL);

	connection = g_object_get_data (G_OBJECT (dialog), "connection");
	if (!connection) {
		NMSettingConnection *s_con;

		connection = nm_connection_new ();

		s_con = (NMSettingConnection *) nm_setting_connection_new ();
		s_con->type = g_strdup (NM_SETTING_WIRELESS);
		nm_connection_add_setting (connection, (NMSetting *) s_con);

		s_wireless = (NMSettingWireless *) nm_setting_wireless_new ();
		nm_connection_add_setting (connection, (NMSetting *) s_wireless);
	}

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, NM_SETTING_WIRELESS);
	s_wireless->ssid = validate_dialog_ssid (dialog);
	g_assert (s_wireless->ssid);

	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_assert (xml);

	combo = glade_xml_get_widget (xml, "security_combo");
	g_assert (combo);

	/* Fill security */
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter))
		gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &sec, -1);

	if (sec)
		wireless_security_fill_connection (sec, connection);

	/* Fill device */
	combo = glade_xml_get_widget (xml, "device_combo");
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter))
		gtk_tree_model_get (model, &iter, D_DEV_COLUMN, device, -1);

	return connection;
}

GtkWidget *
nma_wireless_dialog_new (const char *glade_file,
                         NMConnection *connection,
                         NMClient *nm_client)
{
	GtkWidget *	dialog;
	GladeXML *	xml;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "wireless_dialog", NULL);
	if (xml == NULL) {
		nma_schedule_warning_dialog (_("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		return;
	}

	dialog = dialog_init (xml, nm_client, glade_file);
	if (!dialog) {
		nm_warning ("Couldn't create wireless security dialog.");
		return;
	}

	return dialog;
}

