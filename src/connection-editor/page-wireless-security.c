/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <NetworkManager.h>
#include <nm-setting-connection.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>
#include <nm-utils.h>

#include "wireless-security.h"
#include "page-wireless-security.h"

#define S_NAME_COLUMN		0
#define S_SEC_COLUMN		1

static NMUtilsSecurityType
get_default_type_for_security (NMSettingWirelessSecurity *sec)
{
	if (!sec)
		return NMU_SEC_NONE;

	/* No IEEE 802.1x */
	if (!strcmp (sec->key_mgmt, "none")) {
		/* Static WEP */
		if (   sec->wep_tx_keyidx
		    || sec->wep_key0
		    || sec->wep_key1
		    || sec->wep_key2
		    || sec->wep_key3
		    || (sec->auth_alg && !strcmp (sec->auth_alg, "shared")))
			return NMU_SEC_STATIC_WEP;

		/* Unencrypted */
		return NMU_SEC_NONE;
	}

	if (!strcmp (sec->key_mgmt, "ieee8021x")) {
		if (sec->auth_alg && !strcmp (sec->auth_alg, "leap"))
			return NMU_SEC_LEAP;
		return NMU_SEC_DYNAMIC_WEP;
	}

	if (   !strcmp (sec->key_mgmt, "wpa-none")
	    || !strcmp (sec->key_mgmt, "wpa-psk")) {
		if (sec->proto && !strcmp (sec->proto->data, "rsn"))
			return NMU_SEC_WPA2_PSK;
		else if (sec->proto && !strcmp (sec->proto->data, "wpa"))
			return NMU_SEC_WPA_PSK;
		else
			return NMU_SEC_WPA_PSK;
	}

	if (!strcmp (sec->key_mgmt, "wpa-eap")) {
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
stuff_changed_cb (WirelessSecurity *sec, gpointer user_data)
{
	GtkWidget *page = GTK_WIDGET (user_data);
	GtkWidget *wireless_page;
	GtkWidget *widget;
	GladeXML *xml;
	const char *txt_ssid;
	GByteArray *ssid = NULL;
	gboolean valid = FALSE;

	// FIXME: hack until a real per-page validation system gets implemented
	wireless_page = g_object_get_data (G_OBJECT (page), "wireless-page");
	xml = g_object_get_data (G_OBJECT (wireless_page), "glade-xml");
	widget = glade_xml_get_widget (xml, "wireless_ssid");

	txt_ssid = gtk_entry_get_text (GTK_ENTRY (widget));
	if (txt_ssid && strlen (txt_ssid)) {
		ssid = g_byte_array_sized_new (strlen (txt_ssid));
		g_byte_array_append (ssid, (const guint8 *) txt_ssid, strlen (txt_ssid));
		valid = wireless_security_validate (sec, ssid);
		g_byte_array_free (ssid, TRUE);
	}

	widget = g_object_get_data (G_OBJECT (page), "ok-button");
	gtk_widget_set_sensitive (widget, valid);
}

static void
wsec_size_group_clear (GtkSizeGroup *group)
{
	GSList *children;
	GSList *iter;

	g_return_if_fail (group != NULL);

	children = gtk_size_group_get_widgets (group);
	for (iter = children; iter; iter = g_slist_next (iter))
		gtk_size_group_remove_widget (group, GTK_WIDGET (iter->data));
}

static void
wireless_security_combo_changed (GtkWidget *combo,
                                 gpointer user_data)
{
	GtkWidget *page = GTK_WIDGET (user_data);
	GtkWidget *vbox;
	GList *elt, *children;
	GtkTreeIter iter;
	GtkTreeModel *model;
	WirelessSecurity *sec = NULL;
	GladeXML *xml;
	GtkSizeGroup *wsec_group;

	xml = g_object_get_data (G_OBJECT (page), "glade-xml");
	wsec_group = g_object_get_data (G_OBJECT (page), "size-group");

	vbox = glade_xml_get_widget (xml, "wireless_security_vbox");
	g_assert (vbox);

	wsec_size_group_clear (wsec_group);

	/* Remove any previous wireless security widgets */
	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (elt = children; elt; elt = g_list_next (elt))
		gtk_container_remove (GTK_CONTAINER (vbox), GTK_WIDGET (elt->data));

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter);
	gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &sec, -1);
	if (sec) {
		GtkWidget *sec_widget;
		GtkWidget *widget;

		sec_widget = wireless_security_get_widget (sec);
		g_assert (sec_widget);

		widget = glade_xml_get_widget (xml, "wireless_security_combo_label");
		gtk_size_group_add_widget (wsec_group, widget);
		wireless_security_add_to_size_group (sec, wsec_group);

		gtk_container_add (GTK_CONTAINER (vbox), sec_widget);
		wireless_security_unref (sec);
	}
}

static void
add_security_item (GtkWidget *page,
                   WirelessSecurity *sec,
                   GtkListStore *model,
                   GtkTreeIter *iter,
                   const char *text)
{
	wireless_security_set_changed_notify (sec, stuff_changed_cb, page);
	gtk_list_store_append (model, iter);
	gtk_list_store_set (model, iter, S_NAME_COLUMN, text, S_SEC_COLUMN, sec, -1);
	wireless_security_unref (sec);
}

GtkWidget *
page_wireless_security_new (NMConnection *connection,
                            GtkWidget *ok_button,
                            GtkWidget *wireless_page,
                            const char **title)
{
	GladeXML *xml;
	GtkWidget *page;
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	gboolean is_adhoc = FALSE;
	GtkListStore *sec_model;
	GtkTreeIter iter;
	guint32 dev_caps = 0;
	NMUtilsSecurityType default_type = NMU_SEC_NONE;
	int active = -1;
	int item = 0;
	const char *glade_file = GLADEDIR "/applet.glade";
	GtkWidget *combo;
	GtkSizeGroup *wsec_group;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_assert (s_wireless);

	xml = glade_xml_new (GLADEDIR "/ce-page-wireless-security.glade", "WirelessSecurityPage", NULL);
	g_return_val_if_fail (xml != NULL, NULL);
	*title = _("Wireless Security");

	page = glade_xml_get_widget (xml, "WirelessSecurityPage");
	g_return_val_if_fail (page != NULL, NULL);
	g_object_set_data_full (G_OBJECT (page),
	                        "glade-xml", xml,
	                        (GDestroyNotify) g_object_unref);

	wsec_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	g_object_set_data_full (G_OBJECT (page),
	                        "size-group", wsec_group,
	                        (GDestroyNotify) g_object_unref);

	g_object_set_data (G_OBJECT (page), "ok-button", ok_button);
	g_object_set_data (G_OBJECT (page), "wireless-page", wireless_page);

	combo = glade_xml_get_widget (xml, "wireless_security_combo");

	dev_caps =   NM_802_11_DEVICE_CAP_CIPHER_WEP40
	           | NM_802_11_DEVICE_CAP_CIPHER_WEP104
	           | NM_802_11_DEVICE_CAP_CIPHER_TKIP
	           | NM_802_11_DEVICE_CAP_CIPHER_CCMP
	           | NM_802_11_DEVICE_CAP_WPA
	           | NM_802_11_DEVICE_CAP_RSN;

	if (s_wireless->mode && !strcmp (s_wireless->mode, "adhoc"))
		is_adhoc = TRUE;

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, 
	                                               NM_TYPE_SETTING_WIRELESS_SECURITY));
	default_type = get_default_type_for_security (s_wireless_sec);

	sec_model = gtk_list_store_new (2, G_TYPE_STRING, wireless_security_get_g_type ());

	if (nm_utils_security_valid (NMU_SEC_NONE, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		gtk_list_store_append (sec_model, &iter);
		gtk_list_store_set (sec_model, &iter,
		                    S_NAME_COLUMN, _("None"),
		                    -1);
		if (default_type == NMU_SEC_NONE)
			active = item;
	}

	if (nm_utils_security_valid (NMU_SEC_STATIC_WEP, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityWEPKey *ws_wep_hex;
		WirelessSecurityWEPKey *ws_wep_ascii;
		WirelessSecurityWEPPassphrase *ws_wep_passphrase;

		ws_wep_passphrase = ws_wep_passphrase_new (glade_file, connection);
		if (ws_wep_passphrase) {
			add_security_item (page, WIRELESS_SECURITY (ws_wep_passphrase), sec_model,
			                   &iter, _("WEP 128-bit Passphrase"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP))
				active = item++;
		}

		ws_wep_hex = ws_wep_key_new (glade_file, connection, WEP_KEY_TYPE_HEX);
		if (ws_wep_hex) {
			add_security_item (page, WIRELESS_SECURITY (ws_wep_hex), sec_model,
			                   &iter, _("WEP 40/128-bit Hexadecimal"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP))
				active = item++;
		}

		ws_wep_ascii = ws_wep_key_new (glade_file, connection, WEP_KEY_TYPE_ASCII);
		if (ws_wep_ascii) {
			add_security_item (page, WIRELESS_SECURITY (ws_wep_ascii), sec_model,
			                   &iter, _("WEP 40/128-bit ASCII"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP))
				active = item++;
		}
	}

	if (nm_utils_security_valid (NMU_SEC_LEAP, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityLEAP *ws_leap;

		ws_leap = ws_leap_new (glade_file, connection);
		if (ws_leap) {
			add_security_item (page, WIRELESS_SECURITY (ws_leap), sec_model,
			                   &iter, _("LEAP"));
			if ((active < 0) && (default_type == NMU_SEC_LEAP))
				active = item++;
		}
	}

	if (nm_utils_security_valid (NMU_SEC_DYNAMIC_WEP, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityDynamicWEP *ws_dynamic_wep;

		ws_dynamic_wep = ws_dynamic_wep_new (glade_file, connection);
		if (ws_dynamic_wep) {
			add_security_item (page, WIRELESS_SECURITY (ws_dynamic_wep), sec_model,
			                   &iter, _("Dynamic WEP (802.1x)"));
			if ((active < 0) && (default_type == NMU_SEC_DYNAMIC_WEP))
				active = item++;
		}
	}

	if (   nm_utils_security_valid (NMU_SEC_WPA_PSK, dev_caps, FALSE, is_adhoc, 0, 0, 0)
	    || nm_utils_security_valid (NMU_SEC_WPA2_PSK, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityWPAPSK *ws_wpa_psk;

		ws_wpa_psk = ws_wpa_psk_new (glade_file, connection);
		if (ws_wpa_psk) {
			add_security_item (page, WIRELESS_SECURITY (ws_wpa_psk), sec_model,
			                   &iter, _("WPA & WPA2 Personal"));
			if ((active < 0) && ((default_type == NMU_SEC_WPA_PSK) || (default_type == NMU_SEC_WPA2_PSK)))
				active = item++;
		}
	}

	if (   nm_utils_security_valid (NMU_SEC_WPA_ENTERPRISE, dev_caps, FALSE, is_adhoc, 0, 0, 0)
	    || nm_utils_security_valid (NMU_SEC_WPA2_ENTERPRISE, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityWPAEAP *ws_wpa_eap;

		ws_wpa_eap = ws_wpa_eap_new (glade_file, connection);
		if (ws_wpa_eap) {
			add_security_item (page, WIRELESS_SECURITY (ws_wpa_eap), sec_model,
			                   &iter, _("WPA & WPA2 Enterprise"));
			if ((active < 0) && ((default_type == NMU_SEC_WPA_ENTERPRISE) || (default_type == NMU_SEC_WPA2_ENTERPRISE)))
				active = item++;
		}
	}

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (sec_model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), active < 0 ? 0 : (guint32) active);
	g_object_unref (G_OBJECT (sec_model));

	wireless_security_combo_changed (combo, page);
	g_signal_connect (G_OBJECT (combo),
	                  "changed",
	                  GTK_SIGNAL_FUNC (wireless_security_combo_changed),
	                  page);

	return page;
}


