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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include "page-wireless.h"
#include "page-wireless-security.h"
#include "nm-connection-editor.h"
#include "gconf-helpers.h"


G_DEFINE_TYPE (CEPageWirelessSecurity, ce_page_wireless_security, CE_TYPE_PAGE)


#define S_NAME_COLUMN		0
#define S_SEC_COLUMN		1

static NMUtilsSecurityType
get_default_type_for_security (NMSettingWirelessSecurity *sec)
{
	g_return_val_if_fail (sec != NULL, NMU_SEC_NONE);

	/* No IEEE 802.1x */
	if (!strcmp (sec->key_mgmt, "none"))
		return NMU_SEC_STATIC_WEP;

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
	ce_page_changed (CE_PAGE (user_data));
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

static WirelessSecurity *
wireless_security_combo_get_active (CEPageWirelessSecurity *self)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	WirelessSecurity *sec = NULL;

	model = gtk_combo_box_get_model (self->security_combo);
	gtk_combo_box_get_active_iter (self->security_combo, &iter);
	gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &sec, -1);

	return sec;
}

static void
wireless_security_combo_changed (GtkComboBox *combo,
                                 gpointer user_data)
{
	CEPageWirelessSecurity *self = CE_PAGE_WIRELESS_SECURITY (user_data);
	GtkWidget *vbox;
	GList *elt, *children;
	WirelessSecurity *sec;

	vbox = glade_xml_get_widget (CE_PAGE (self)->xml, "wireless_security_vbox");
	g_assert (vbox);

	wsec_size_group_clear (self->group);

	/* Remove any previous wireless security widgets */
	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (elt = children; elt; elt = g_list_next (elt))
		gtk_container_remove (GTK_CONTAINER (vbox), GTK_WIDGET (elt->data));

	sec = wireless_security_combo_get_active (self);
	if (sec) {
		GtkWidget *sec_widget;
		GtkWidget *widget;

		sec_widget = wireless_security_get_widget (sec);
		g_assert (sec_widget);

		widget = glade_xml_get_widget (CE_PAGE (self)->xml, "wireless_security_combo_label");
		gtk_size_group_add_widget (self->group, widget);
		wireless_security_add_to_size_group (sec, self->group);

		gtk_container_add (GTK_CONTAINER (vbox), sec_widget);
		wireless_security_unref (sec);
	}

	ce_page_changed (CE_PAGE (self));
}

static void
add_security_item (CEPageWirelessSecurity *self,
                   WirelessSecurity *sec,
                   GtkListStore *model,
                   GtkTreeIter *iter,
                   const char *text)
{
	wireless_security_set_changed_notify (sec, stuff_changed_cb, self);
	gtk_list_store_append (model, iter);
	gtk_list_store_set (model, iter, S_NAME_COLUMN, text, S_SEC_COLUMN, sec, -1);
	wireless_security_unref (sec);
}

CEPageWirelessSecurity *
ce_page_wireless_security_new (NMConnection *connection)
{
	CEPageWirelessSecurity *self;
	CEPage *parent;
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
	GtkComboBox *combo;
	const char *connection_id;

	connection_id = g_object_get_data (G_OBJECT (connection), NMA_CONNECTION_ID_TAG);

	self = CE_PAGE_WIRELESS_SECURITY (g_object_new (CE_TYPE_PAGE_WIRELESS_SECURITY, NULL));
	parent = CE_PAGE (self);

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	if (!s_wireless) {
		g_warning ("%s: Connection didn't have a wireless setting!", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-wireless-security.glade", "WirelessSecurityPage", NULL);
	if (!parent->xml) {
		g_warning ("%s: Couldn't load wireless security page glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "WirelessSecurityPage");
	if (!parent->page) {
		g_warning ("%s: Couldn't load wireless security page from glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("Wireless Security"));

	self->group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	combo = GTK_COMBO_BOX (glade_xml_get_widget (parent->xml, "wireless_security_combo"));

	dev_caps =   NM_WIFI_DEVICE_CAP_CIPHER_WEP40
	           | NM_WIFI_DEVICE_CAP_CIPHER_WEP104
	           | NM_WIFI_DEVICE_CAP_CIPHER_TKIP
	           | NM_WIFI_DEVICE_CAP_CIPHER_CCMP
	           | NM_WIFI_DEVICE_CAP_WPA
	           | NM_WIFI_DEVICE_CAP_RSN;

	if (s_wireless->mode && !strcmp (s_wireless->mode, "adhoc"))
		is_adhoc = TRUE;

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, 
	                                               NM_TYPE_SETTING_WIRELESS_SECURITY));
	if (!s_wireless->security || strcmp (s_wireless->security, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME))
		s_wireless_sec = NULL;
	if (s_wireless_sec)
		default_type = get_default_type_for_security (s_wireless_sec);

	sec_model = gtk_list_store_new (2, G_TYPE_STRING, wireless_security_get_g_type ());

	if (nm_utils_security_valid (NMU_SEC_NONE, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		gtk_list_store_append (sec_model, &iter);
		gtk_list_store_set (sec_model, &iter,
		                    S_NAME_COLUMN, _("None"),
		                    -1);
		if (default_type == NMU_SEC_NONE)
			active = item;
		item++;
	}

	if (nm_utils_security_valid (NMU_SEC_STATIC_WEP, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityWEPKey *ws_wep;
		WEPKeyType default_wep_type = WEP_KEY_TYPE_PASSPHRASE;

		if (default_type == NMU_SEC_STATIC_WEP)
			default_wep_type = ws_wep_guess_key_type (connection, connection_id);

		ws_wep = ws_wep_key_new (glade_file, connection, connection_id, WEP_KEY_TYPE_PASSPHRASE, FALSE);
		if (ws_wep) {
			add_security_item (self, WIRELESS_SECURITY (ws_wep), sec_model,
			                   &iter, _("WEP 128-bit Passphrase"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP) && (default_wep_type == WEP_KEY_TYPE_PASSPHRASE))
				active = item;
			item++;
		}

		ws_wep = ws_wep_key_new (glade_file, connection, connection_id, WEP_KEY_TYPE_HEX, FALSE);
		if (ws_wep) {
			add_security_item (self, WIRELESS_SECURITY (ws_wep), sec_model,
			                   &iter, _("WEP 40/128-bit Hexadecimal"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP) && (default_wep_type == WEP_KEY_TYPE_HEX))
				active = item;
			item++;
		}

		ws_wep = ws_wep_key_new (glade_file, connection, connection_id, WEP_KEY_TYPE_ASCII, FALSE);
		if (ws_wep) {
			add_security_item (self, WIRELESS_SECURITY (ws_wep), sec_model,
			                   &iter, _("WEP 40/128-bit ASCII"));
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP) && (default_wep_type == WEP_KEY_TYPE_ASCII))
				active = item;
			item++;
		}
	}

	if (nm_utils_security_valid (NMU_SEC_LEAP, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityLEAP *ws_leap;

		ws_leap = ws_leap_new (glade_file, connection, connection_id);
		if (ws_leap) {
			add_security_item (self, WIRELESS_SECURITY (ws_leap), sec_model,
			                   &iter, _("LEAP"));
			if ((active < 0) && (default_type == NMU_SEC_LEAP))
				active = item;
			item++;
		}
	}

	if (nm_utils_security_valid (NMU_SEC_DYNAMIC_WEP, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityDynamicWEP *ws_dynamic_wep;

		ws_dynamic_wep = ws_dynamic_wep_new (glade_file, connection, connection_id);
		if (ws_dynamic_wep) {
			add_security_item (self, WIRELESS_SECURITY (ws_dynamic_wep), sec_model,
			                   &iter, _("Dynamic WEP (802.1x)"));
			if ((active < 0) && (default_type == NMU_SEC_DYNAMIC_WEP))
				active = item;
			item++;
		}
	}

	if (   nm_utils_security_valid (NMU_SEC_WPA_PSK, dev_caps, FALSE, is_adhoc, 0, 0, 0)
	    || nm_utils_security_valid (NMU_SEC_WPA2_PSK, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityWPAPSK *ws_wpa_psk;

		ws_wpa_psk = ws_wpa_psk_new (glade_file, connection, connection_id);
		if (ws_wpa_psk) {
			add_security_item (self, WIRELESS_SECURITY (ws_wpa_psk), sec_model,
			                   &iter, _("WPA & WPA2 Personal"));
			if ((active < 0) && ((default_type == NMU_SEC_WPA_PSK) || (default_type == NMU_SEC_WPA2_PSK)))
				active = item;
			item++;
		}
	}

	if (   nm_utils_security_valid (NMU_SEC_WPA_ENTERPRISE, dev_caps, FALSE, is_adhoc, 0, 0, 0)
	    || nm_utils_security_valid (NMU_SEC_WPA2_ENTERPRISE, dev_caps, FALSE, is_adhoc, 0, 0, 0)) {
		WirelessSecurityWPAEAP *ws_wpa_eap;

		ws_wpa_eap = ws_wpa_eap_new (glade_file, connection, connection_id);
		if (ws_wpa_eap) {
			add_security_item (self, WIRELESS_SECURITY (ws_wpa_eap), sec_model,
			                   &iter, _("WPA & WPA2 Enterprise"));
			if ((active < 0) && ((default_type == NMU_SEC_WPA_ENTERPRISE) || (default_type == NMU_SEC_WPA2_ENTERPRISE)))
				active = item;
			item++;
		}
	}

	gtk_combo_box_set_model (combo, GTK_TREE_MODEL (sec_model));
	gtk_combo_box_set_active (combo, active < 0 ? 0 : (guint32) active);
	g_object_unref (G_OBJECT (sec_model));

	self->security_combo = combo;

	wireless_security_combo_changed (combo, self);
	g_signal_connect (combo, "changed",
	                  G_CALLBACK (wireless_security_combo_changed),
	                  self);

	return self;
}

static void
ce_page_wireless_security_init (CEPageWirelessSecurity *self)
{
	self->disposed = FALSE;
}

static void
dispose (GObject *object)
{
	CEPageWirelessSecurity *self = CE_PAGE_WIRELESS_SECURITY (object);

	if (self->disposed)
		return;

	self->disposed = TRUE;

	if (self->group)
		g_object_unref (self->group);

	G_OBJECT_CLASS (ce_page_wireless_security_parent_class)->dispose (object);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageWirelessSecurity *self = CE_PAGE_WIRELESS_SECURITY (page);
	NMSettingWireless *s_wireless;
	WirelessSecurity *sec;
	gboolean valid = FALSE;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_assert (s_wireless);

	sec = wireless_security_combo_get_active (self);
	if (sec) {
		if (s_wireless->ssid) {
			/* FIXME: get failed property and error out of wireless security objects */
			valid = wireless_security_validate (sec, s_wireless->ssid);
			if (valid)
				wireless_security_fill_connection (sec, connection);
			else
				g_set_error (error, 0, 0, "Invalid wireless security");
		} else
			g_set_error (error, 0, 0, "Missing SSID");
	} else {
		/* No security, unencrypted */
		g_free (s_wireless->security);
		s_wireless->security = NULL;
		valid = TRUE;
	}

	return valid;
}

static void
ce_page_wireless_security_class_init (CEPageWirelessSecurityClass *wireless_security_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (wireless_security_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (wireless_security_class);

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
}
