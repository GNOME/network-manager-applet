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
#include <nm-setting-connection.h>
#include <nm-setting-wireless.h>

#include "applet.h"
#include "wireless-dialog.h"
#include "wireless-security.h"
#include "utils.h"

#define D_NAME_COLUMN		0
#define D_DEV_COLUMN		1

#define S_NAME_COLUMN		0
#define S_SEC_COLUMN		1

static void security_combo_changed (GtkWidget *combo, gpointer user_data);
static gboolean security_combo_init (const char *glade_file,
                                     GtkWidget *combo,
                                     NMDevice *device,
                                     GtkWidget *dialog,
                                     NMConnection *connection);

static void
device_combo_changed (GtkWidget *combo,
                      gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET (user_data);
	GtkTreeModel *model;
	GtkTreeIter iter;
	NMDevice *device = NULL;
	const char *glade_file;
	GtkWidget *security_combo;
	GladeXML *xml;
	NMConnection *connection;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter);
	gtk_tree_model_get (model, &iter, D_DEV_COLUMN, &device, -1);

	glade_file = g_object_get_data (G_OBJECT (dialog), "glade-file");
	g_assert (glade_file);

	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_assert (xml);

	connection = g_object_get_data (G_OBJECT (dialog), "connection");

	security_combo = glade_xml_get_widget (xml, "security_combo");
	g_assert (security_combo);
	if (!security_combo_init (glade_file, security_combo, device, dialog, connection)) {
		g_message ("Couldn't change wireless security combo box.");
		return;
	}

	security_combo_changed (security_combo, dialog);
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
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter);
	gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &sec, -1);
	if (sec) {
		GtkWidget *sec_widget;

		sec_widget = wireless_security_get_widget (sec);
		g_assert (sec_widget);

		size_group_add_permanent (group, xml);
		wireless_security_add_to_size_group (sec, group);

		gtk_container_add (GTK_CONTAINER (vbox), sec_widget);
		wireless_security_unref (sec);
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
	g_byte_array_append (ssid_ba, (unsigned char *) ssid, ssid_len);
	return ssid_ba;
}

static void
stuff_changed_cb (WirelessSecurity *sec, gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET (user_data);
	GladeXML *xml;
	GtkWidget *widget;
	GByteArray *ssid = NULL;
	gboolean free_ssid = TRUE;
	gboolean valid = FALSE;
	NMConnection *connection;
	
	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_assert (xml);

	connection = g_object_get_data (G_OBJECT (dialog), "connection");
	if (connection) {
		NMSettingWireless *s_wireless;
		s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
		g_assert (s_wireless);
		ssid = s_wireless->ssid;
		free_ssid = FALSE;
	} else {
		ssid = validate_dialog_ssid (dialog);
	}

	if (ssid) {
		valid = wireless_security_validate (sec, ssid);
		if (free_ssid)
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
	g_assert (xml);

	ssid = validate_dialog_ssid (dialog);
	if (!ssid)
		goto out;

	combo = glade_xml_get_widget (xml, "security_combo");
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter))
		gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &sec, -1);

	if (sec) {
		valid = wireless_security_validate (sec, ssid);
		wireless_security_unref (sec);
	} else {
		valid = TRUE;
	}

out:
	widget = glade_xml_get_widget (xml, "ok_button");
	gtk_widget_set_sensitive (widget, valid);
}

static void
add_device_to_model (GtkListStore *model,
                     GtkTreeIter *iter,
                     NMDevice *device)
{
	const char *desc;
	char *name = NULL;

	desc = utils_get_device_description (device);
	if (desc)
		name = g_strdup (desc);
	if (!name)
		name = nm_device_get_iface (device);

	gtk_list_store_append (model, iter);
	gtk_list_store_set (model, iter, D_NAME_COLUMN, name, D_DEV_COLUMN, device, -1);
}

static GtkTreeModel *
create_device_model (NMClient *client, NMDevice *use_this_device, guint32 *num)
{
	GtkListStore *model;
	GSList *devices;
	GSList *iter;
	GtkTreeIter tree_iter;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (num != NULL, NULL);

	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_OBJECT);
	*num = 0;

	if (use_this_device) {
		add_device_to_model (model, &tree_iter, use_this_device);
		*num = 1;
	} else {
		devices = nm_client_get_devices (client);
		for (iter = devices; iter; iter = g_slist_next (iter)) {
			NMDevice *dev = (NMDevice *) iter->data;

			/* Ignore unsupported devices */
			if (!(nm_device_get_capabilities (dev) & NM_DEVICE_CAP_NM_SUPPORTED))
				continue;

			if (!NM_IS_DEVICE_802_11_WIRELESS (dev))
				continue;

			add_device_to_model (model, &tree_iter, dev);
			*num += 1;
		}
		g_slist_free (devices);
	}

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

			gtk_tree_model_get (model, &iter, D_NAME_COLUMN, &str, -1);
			g_free (str);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	g_object_unref (model);
}

static NMUtilsSecurityType
get_default_type_for_security (NMSettingWirelessSecurity *sec,
                               guint32 ap_flags,
                               guint32 dev_caps)
{
	g_return_val_if_fail (sec != NULL, NMU_SEC_NONE);

	/* No IEEE 802.1x */
	if (!strcmp (sec->key_mgmt, "none")) {
		/* Static WEP */
		if (   sec->wep_tx_keyidx
		    || sec->wep_key0
		    || sec->wep_key1
		    || sec->wep_key2
		    || sec->wep_key3
		    || (ap_flags & NM_802_11_AP_FLAGS_PRIVACY)
		    || (sec->auth_alg && !strcmp (sec->auth_alg, "shared")))
			return NMU_SEC_STATIC_WEP;

		/* Unencrypted */
		return NMU_SEC_NONE;
	}

	if (   !strcmp (sec->key_mgmt, "ieee8021x")
	    && (ap_flags & NM_802_11_AP_FLAGS_PRIVACY)) {
		if (sec->auth_alg && !strcmp (sec->auth_alg, "leap"))
			return NMU_SEC_LEAP;
		return NMU_SEC_DYNAMIC_WEP;
	}

	if (   !strcmp (sec->key_mgmt, "wpa-none")
	    || !strcmp (sec->key_mgmt, "wpa-psk")) {
		if (ap_flags & NM_802_11_AP_FLAGS_PRIVACY) {
			if (sec->proto && !strcmp (sec->proto->data, "rsn"))
				return NMU_SEC_WPA2_PSK;
			else if (sec->proto && !strcmp (sec->proto->data, "wpa"))
				return NMU_SEC_WPA_PSK;
			else
				return NMU_SEC_WPA_PSK;
		}
	}

	if (   !strcmp (sec->key_mgmt, "wpa-eap")
	    && (ap_flags & NM_802_11_AP_FLAGS_PRIVACY)) {
			if (sec->proto && !strcmp (sec->proto->data, "rsn"))
				return NMU_SEC_WPA2_ENTERPRISE;
			else if (sec->proto && !strcmp (sec->proto->data, "wpa"))
				return NMU_SEC_WPA_ENTERPRISE;
			else
				return NMU_SEC_WPA_ENTERPRISE;
	}

	return NMU_SEC_INVALID;
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
	wireless_security_unref (sec);
}

static gboolean
security_combo_init (const char *glade_file,
                     GtkWidget *combo,
                     NMDevice *device,
                     GtkWidget *dialog,
                     NMConnection *connection)
{
	GtkListStore *sec_model;
	GtkTreeIter iter;
	NMAccessPoint *cur_ap;
	guint32 ap_flags = 0;
	guint32 ap_wpa = 0;
	guint32 ap_rsn = 0;
	guint32 dev_caps;
	NMSettingWirelessSecurity *wsec = NULL;
	NMUtilsSecurityType default_type = NMU_SEC_NONE;
	int active = -1;
	int item = 0;

	g_return_val_if_fail (combo != NULL, FALSE);
	g_return_val_if_fail (glade_file != NULL, FALSE);
	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (dialog != NULL, FALSE);

	/* The security options displayed are filtered based on device
	 * capabilities, and if provided, additionally by access point capabilities.
	 * If a connection is given, that connection's options should be selected
	 * by default.
	 */
	dev_caps = nm_device_802_11_wireless_get_capabilities (NM_DEVICE_802_11_WIRELESS (device));
	cur_ap = g_object_get_data (G_OBJECT (dialog), "ap");
	if (cur_ap != NULL) {
		ap_flags = nm_access_point_get_flags (cur_ap);
		ap_wpa = nm_access_point_get_wpa_flags (cur_ap);
		ap_rsn = nm_access_point_get_rsn_flags (cur_ap);
	}

	if (connection) {
		wsec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, 
										NM_TYPE_SETTING_WIRELESS_SECURITY));
		default_type = get_default_type_for_security (wsec, ap_flags, dev_caps);
	}

	sec_model = gtk_list_store_new (2, G_TYPE_STRING, wireless_security_get_g_type ());

	if (nm_utils_security_valid (NMU_SEC_NONE, dev_caps, !!cur_ap, ap_flags, ap_wpa, ap_rsn)) {
		gtk_list_store_append (sec_model, &iter);
		gtk_list_store_set (sec_model, &iter,
		                    S_NAME_COLUMN, _("None"),
		                    -1);
		if (default_type == NMU_SEC_NONE)
			active = item;
	}

	/* Don't show Static WEP if both the AP and the device are capable of WPA,
	 * even though technically it's possible to have this configuration.
	 */
	if (   nm_utils_security_valid (NMU_SEC_STATIC_WEP, dev_caps, !!cur_ap, ap_flags, ap_wpa, ap_rsn)
	    && ((!ap_wpa && !ap_rsn) || !(dev_caps & (NM_802_11_DEVICE_CAP_WPA | NM_802_11_DEVICE_CAP_RSN)))) {
		WirelessSecurityWEPKey *ws_wep_hex;
		WirelessSecurityWEPKey *ws_wep_ascii;
		WirelessSecurityWEPPassphrase *ws_wep_passphrase;

		ws_wep_passphrase = ws_wep_passphrase_new (glade_file);
		if (ws_wep_passphrase) {
			add_security_item (dialog, WIRELESS_SECURITY (ws_wep_passphrase), sec_model,
			                   &iter, _("WEP 128-bit Passphrase"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP))
				active = item++;
		}

		ws_wep_hex = ws_wep_key_new (glade_file, WEP_KEY_TYPE_HEX);
		if (ws_wep_hex) {
			add_security_item (dialog, WIRELESS_SECURITY (ws_wep_hex), sec_model,
			                   &iter, _("WEP 40/128-bit Hexadecimal"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP))
				active = item++;
		}

		ws_wep_ascii = ws_wep_key_new (glade_file, WEP_KEY_TYPE_ASCII);
		if (ws_wep_ascii) {
			add_security_item (dialog, WIRELESS_SECURITY (ws_wep_ascii), sec_model,
			                   &iter, _("WEP 40/128-bit ASCII"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP))
				active = item++;
		}
	}

	/* Don't show LEAP if both the AP and the device are capable of WPA,
	 * even though technically it's possible to have this configuration.
	 */
	if (   nm_utils_security_valid (NMU_SEC_LEAP, dev_caps, !!cur_ap, ap_flags, ap_wpa, ap_rsn)
	    && ((!ap_wpa && !ap_rsn) || !(dev_caps & (NM_802_11_DEVICE_CAP_WPA | NM_802_11_DEVICE_CAP_RSN)))) {
		WirelessSecurityLEAP *ws_leap;

		ws_leap = ws_leap_new (glade_file);
		if (ws_leap) {
			add_security_item (dialog, WIRELESS_SECURITY (ws_leap), sec_model,
			                   &iter, _("LEAP"));
			if ((active < 0) && (default_type == NMU_SEC_LEAP))
				active = item++;
		}
	}

	if (nm_utils_security_valid (NMU_SEC_DYNAMIC_WEP, dev_caps, !!cur_ap, ap_flags, ap_wpa, ap_rsn)) {
		WirelessSecurityDynamicWEP *ws_dynamic_wep;

		ws_dynamic_wep = ws_dynamic_wep_new (glade_file, (wsec && wsec->eap) ? wsec->eap->data : NULL);
		if (ws_dynamic_wep) {
			add_security_item (dialog, WIRELESS_SECURITY (ws_dynamic_wep), sec_model,
			                   &iter, _("Dynamic WEP (802.1x)"));
			if ((active < 0) && (default_type == NMU_SEC_DYNAMIC_WEP))
				active = item++;
		}
	}

	if (   nm_utils_security_valid (NMU_SEC_WPA_PSK, dev_caps, !!cur_ap, ap_flags, ap_wpa, ap_rsn)
	    || nm_utils_security_valid (NMU_SEC_WPA2_PSK, dev_caps, !!cur_ap, ap_flags, ap_wpa, ap_rsn)) {
		WirelessSecurityWPAPSK *ws_wpa_psk;

		ws_wpa_psk = ws_wpa_psk_new (glade_file);
		if (ws_wpa_psk) {
			add_security_item (dialog, WIRELESS_SECURITY (ws_wpa_psk), sec_model,
			                   &iter, _("WPA Pre-Shared Key"));
			if ((active < 0) && ((default_type == NMU_SEC_WPA_PSK) || (default_type == NMU_SEC_WPA2_PSK)))
				active = item++;
		}
	}

	if (   nm_utils_security_valid (NMU_SEC_WPA_ENTERPRISE, dev_caps, !!cur_ap, ap_flags, ap_wpa, ap_rsn)
	    || nm_utils_security_valid (NMU_SEC_WPA2_ENTERPRISE, dev_caps, !!cur_ap, ap_flags, ap_wpa, ap_rsn)) {
		WirelessSecurityWPAEAP *ws_wpa_eap;

		ws_wpa_eap = ws_wpa_eap_new (glade_file, (wsec && wsec->eap) ? wsec->eap->data : NULL);
		if (ws_wpa_eap) {
			add_security_item (dialog, WIRELESS_SECURITY (ws_wpa_eap), sec_model,
			                   &iter, _("WPA & WPA2 Enterprise"));
			if ((active < 0) && ((default_type == NMU_SEC_WPA_ENTERPRISE) || (default_type == NMU_SEC_WPA2_ENTERPRISE)))
				active = item++;
		}
	}

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (sec_model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), active < 0 ? 0 : (guint32) active);
	g_object_unref (G_OBJECT (sec_model));
	return TRUE;
}

static gboolean
dialog_init (GtkWidget *dialog,
             GladeXML *xml,
             NMClient *nm_client,
             const char *glade_file,
             NMConnection *connection)
{
	GtkWidget *widget;
	GtkSizeGroup *group;
	GtkTreeModel *model;
	GtkTreeIter iter;
	guint32 num_devs = 0;
	char *label;
	NMDevice *dev;
	gboolean success = FALSE;
	gboolean security_combo_focus = FALSE;

	/* If given a valid connection, hide the SSID bits */
	if (connection) {
		widget = glade_xml_get_widget (xml, "network_name_label");
		g_assert (widget);
		gtk_widget_hide (widget);

		widget = glade_xml_get_widget (xml, "network_name_entry");
		g_assert (widget);
		gtk_widget_hide (widget);

		security_combo_focus = TRUE;
	} else {
		widget = glade_xml_get_widget (xml, "network_name_entry");
		g_signal_connect (G_OBJECT (widget), "changed", (GCallback) ssid_entry_changed, dialog);
		gtk_widget_grab_focus (widget);
	}

	widget = glade_xml_get_widget (xml, "ok_button");
	gtk_widget_grab_default (widget);
	gtk_widget_set_sensitive (widget, FALSE);
#if GTK_CHECK_VERSION(2,6,0)
	{
		GtkWidget *image = gtk_image_new_from_stock (GTK_STOCK_CONNECT, GTK_ICON_SIZE_BUTTON);
		gtk_button_set_image (GTK_BUTTON (widget), image);
	}
#endif

	group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	g_object_set_data_full (G_OBJECT (dialog),
	                        "size-group", group,
	                        (GDestroyNotify) g_object_unref);

	/* If passed a device, don't show all devices, let create_device_model
	 * create a model with just the one we want.
	 */
	dev = g_object_get_data (G_OBJECT (dialog), "device");
	model = create_device_model (nm_client, dev, &num_devs);
	if (!model || (num_devs < 1)) {
		g_warning ("No wireless devices available.");
		destroy_device_model (model);
		return FALSE;
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
	gtk_tree_model_get_iter_first (model, &iter);
	gtk_tree_model_get (model, &iter, D_DEV_COLUMN, &dev, -1);

	widget = glade_xml_get_widget (xml, "security_combo");
	g_assert (widget);
	if (!security_combo_init (glade_file, widget, dev, dialog, connection)) {
		g_message ("Couldn't set up wireless security combo box.");
		goto out;
	}
	if (security_combo_focus)
		gtk_widget_grab_focus (widget);

	security_combo_changed (widget, dialog);
	g_signal_connect (G_OBJECT (widget), "changed", GTK_SIGNAL_FUNC (security_combo_changed), dialog);

	if (connection) {
		char *tmp;
		char *esc_ssid = NULL;
		NMSettingWireless *s_wireless;

		s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
		if (s_wireless && s_wireless->ssid)
			esc_ssid = nm_utils_ssid_to_utf8 ((const char *) s_wireless->ssid->data, s_wireless->ssid->len);

		tmp = g_strdup_printf (_("Passwords or encryption keys are required to access the wireless network '%s'."),
		                       esc_ssid ? esc_ssid : "<unknown>");
		gtk_window_set_title (GTK_WINDOW (dialog), _("Wireless Network Secrets Required"));
		label = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>\n\n%s",
		                         _("Secrets required by wireless network"),
		                         tmp);
		g_free (esc_ssid);
	} else {
		gtk_window_set_title (GTK_WINDOW (dialog), _("Connect to Other Wireless Network"));
		label = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>\n\n%s",
		                         _("Existing wireless network"),
		                         _("Enter the name of the wireless network to which you wish to connect."));
	}

	widget = glade_xml_get_widget (xml, "caption_label");
	gtk_label_set_markup (GTK_LABEL (widget), label);
	g_free (label);

	success = TRUE;

out:
	g_object_unref (dev);
	return success;
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
		s_con->type = g_strdup (NM_SETTING_WIRELESS_SETTING_NAME);
		nm_connection_add_setting (connection, (NMSetting *) s_con);

		s_wireless = (NMSettingWireless *) nm_setting_wireless_new ();
		s_wireless->ssid = validate_dialog_ssid (dialog);
		g_assert (s_wireless->ssid);

		nm_connection_add_setting (connection, (NMSetting *) s_wireless);
	}

	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_assert (xml);

	combo = glade_xml_get_widget (xml, "security_combo");
	g_assert (combo);

	/* Fill security */
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter);
	gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &sec, -1);
	if (sec) {
		wireless_security_fill_connection (sec, connection);
		wireless_security_unref (sec);
	}

	/* Fill device */
	combo = glade_xml_get_widget (xml, "device_combo");
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter);
	gtk_tree_model_get (model, &iter, D_DEV_COLUMN, device, -1);
	g_object_unref (*device);

	return connection;
}

GtkWidget *
nma_wireless_dialog_new (const char *glade_file,
                         NMClient *nm_client,
                         NMConnection *connection,
                         NMDevice *cur_device,
                         NMAccessPoint *cur_ap)
{
	GtkWidget *	dialog;
	GladeXML *	xml;
	gboolean success;

	g_return_val_if_fail (glade_file != NULL, NULL);

	/* Ensure device validity */
	if (cur_device) {
		guint32 dev_caps = nm_device_get_capabilities (cur_device);

		g_return_val_if_fail (dev_caps & NM_DEVICE_CAP_NM_SUPPORTED, NULL);
		g_return_val_if_fail (NM_IS_DEVICE_802_11_WIRELESS (cur_device), NULL);
	}

	xml = glade_xml_new (glade_file, "wireless_dialog", NULL);
	if (xml == NULL) {
		nma_schedule_warning_dialog (_("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		return NULL;
	}

	dialog = glade_xml_get_widget (xml, "wireless_dialog");
	if (!dialog) {
		nm_warning ("Couldn't find glade wireless_dialog widget.");
		g_object_unref (xml);
		return NULL;
	}

	g_object_set_data_full (G_OBJECT (dialog),
	                        "glade-file", g_strdup (glade_file),
	                        (GDestroyNotify) g_free);

	g_object_set_data_full (G_OBJECT (dialog),
	                        "glade-xml", xml,
	                        (GDestroyNotify) g_object_unref);

	g_object_set_data_full (G_OBJECT (dialog),
	                        "nm-client", g_object_ref (nm_client),
	                        (GDestroyNotify) g_object_unref);

	if (connection) {
		g_object_set_data_full (G_OBJECT (dialog),
		                        "connection", g_object_ref (connection),
		                        (GDestroyNotify) g_object_unref);
	}

	if (cur_device) {
		g_object_set_data_full (G_OBJECT (dialog),
		                        "device", g_object_ref (cur_device),
		                        (GDestroyNotify) g_object_unref);
	}

	if (cur_ap) {
		g_object_set_data_full (G_OBJECT (dialog),
		                        "ap", g_object_ref (cur_ap),
		                        (GDestroyNotify) g_object_unref);
	}

	success = dialog_init (dialog, xml, nm_client, glade_file, connection);
	if (!success) {
		nm_warning ("Couldn't create wireless security dialog.");
		gtk_widget_destroy (dialog);
		return NULL;
	}

	return dialog;
}

