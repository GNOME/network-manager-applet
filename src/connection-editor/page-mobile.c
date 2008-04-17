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

#include <nm-setting-connection.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>

#include "page-mobile.h"
#include "nm-connection-editor.h"
#include "gconf-helpers.h"

G_DEFINE_TYPE (CEPageMobile, ce_page_mobile, CE_TYPE_PAGE)

#define CE_PAGE_MOBILE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_MOBILE, CEPageMobilePrivate))

typedef struct {
	NMSetting *setting;

	/* Common to GSM and CDMA */
	GtkEntry *number;
	GtkEntry *username;
	GtkEntry *password;

	/* GSM only */
	GtkEntry *apn;
	GtkEntry *network_id;
	GtkComboBox *network_type;
	GtkComboBox *band;
	GtkEntry *pin;
	GtkEntry *puk;

	gboolean disposed;
} CEPageMobilePrivate;

#define NET_TYPE_ANY         0
#define NET_TYPE_GPRS        1
#define NET_TYPE_GSM         2
#define NET_TYPE_PREFER_GPRS 3
#define NET_TYPE_PREFER_GSM  4

static void
mobile_private_init (CEPageMobile *self)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	GladeXML *xml;

	xml = CE_PAGE (self)->xml;

	priv->number = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_number"));
	priv->username = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_username"));
	priv->password = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_password"));

	priv->apn = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_apn"));
	priv->network_id = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_network_id"));
	priv->network_type = GTK_COMBO_BOX (glade_xml_get_widget (xml, "mobile_network_type"));
	priv->band = GTK_COMBO_BOX (glade_xml_get_widget (xml, "mobile_band"));

	priv->pin = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_pin"));
	priv->puk = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_puk"));
}

static GHashTable *
get_secrets (NMConnection *connection, const char *setting_name)
{
	const char *connection_id;
	GError *error = NULL;
	GHashTable *secrets;

	connection_id = g_object_get_data (G_OBJECT (connection), NMA_CONNECTION_ID_TAG);
	if (!connection_id)
		return NULL;

	secrets = nm_gconf_get_keyring_items (connection, connection_id,
	                                      setting_name,
	                                      FALSE,
	                                      &error);
	if (!secrets && error)
		g_error_free (error);

	return secrets;
}

static void
populate_gsm_ui (CEPageMobile *self, NMConnection *connection)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	NMSettingGsm *setting = NM_SETTING_GSM (priv->setting);
	int type_idx;
	GHashTable *secrets;
	GValue *value;

	if (setting->number)
		gtk_entry_set_text (priv->number, setting->number);

	if (setting->username)
		gtk_entry_set_text (priv->username, setting->username);

	if (setting->apn)
		gtk_entry_set_text (priv->apn, setting->apn);

	if (setting->network_id)
		gtk_entry_set_text (priv->network_id, setting->network_id);

	switch (setting->network_type) {
	case NM_GSM_NETWORK_GPRS:
		type_idx = NET_TYPE_GPRS;
		break;
	case NM_GSM_NETWORK_GSM:
		type_idx = NET_TYPE_GSM;
		break;
	case NM_GSM_NETWORK_PREFER_GPRS:
		type_idx = NET_TYPE_PREFER_GPRS;
		break;
	case NM_GSM_NETWORK_PREFER_GSM:
		type_idx = NET_TYPE_PREFER_GSM;
		break;
	case NM_GSM_NETWORK_ANY:
	default:
		type_idx = NET_TYPE_ANY;
		break;
	}
	gtk_combo_box_set_active (priv->network_type, type_idx);

	/* FIXME:  band */

	secrets = get_secrets (connection, nm_setting_get_name (priv->setting));

	if (setting->password)
		gtk_entry_set_text (priv->password, setting->password);
	else if (secrets) {
		value = g_hash_table_lookup (secrets, NM_SETTING_GSM_PASSWORD);
		if (value)
			gtk_entry_set_text (priv->password, g_value_get_string (value));
	}

	if (setting->pin)
		gtk_entry_set_text (priv->pin, setting->pin);
	else if (secrets) {
		value = g_hash_table_lookup (secrets, NM_SETTING_GSM_PIN);
		if (value)
			gtk_entry_set_text (priv->pin, g_value_get_string (value));
	}

	if (setting->puk)
		gtk_entry_set_text (priv->pin, setting->puk);
	else if (secrets) {
		value = g_hash_table_lookup (secrets, NM_SETTING_GSM_PUK);
		if (value)
			gtk_entry_set_text (priv->puk, g_value_get_string (value));
	}

	if (secrets)
		g_hash_table_destroy (secrets);
}

static void
populate_cdma_ui (CEPageMobile *self, NMConnection *connection)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	NMSettingCdma *setting = NM_SETTING_CDMA (priv->setting);
	GHashTable *secrets;
	GValue *value;

	if (setting->number)
		gtk_entry_set_text (priv->number, setting->number);

	if (setting->username)
		gtk_entry_set_text (priv->username, setting->username);

	secrets = get_secrets (connection, nm_setting_get_name (priv->setting));

	if (setting->password)
		gtk_entry_set_text (priv->password, setting->password);
	else if (secrets) {
		value = g_hash_table_lookup (secrets, NM_SETTING_CDMA_PASSWORD);
		if (value)
			gtk_entry_set_text (priv->password, g_value_get_string (value));
	}

	if (secrets)
		g_hash_table_destroy (secrets);

	/* Hide GSM specific widgets */
	gtk_widget_hide (glade_xml_get_widget (CE_PAGE (self)->xml, "mobile_basic_label"));
	gtk_widget_hide (glade_xml_get_widget (CE_PAGE (self)->xml, "mobile_advanced_vbox"));
}

static void
populate_ui (CEPageMobile *self, NMConnection *connection)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	if (NM_IS_SETTING_GSM (priv->setting))
		populate_gsm_ui (self, connection);
	else if (NM_IS_SETTING_CDMA (priv->setting))
		populate_cdma_ui (self, connection);
	else
		g_error ("Invalid setting");
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
show_passwords (GtkToggleButton *button, gpointer user_data)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (user_data);
	gboolean active;

	active = gtk_toggle_button_get_active (button);

	gtk_entry_set_visibility (priv->password, active);
	gtk_entry_set_visibility (priv->pin, active);
	gtk_entry_set_visibility (priv->puk, active);
}

CEPageMobile *
ce_page_mobile_new (NMConnection *connection)
{
	CEPageMobile *self;
	CEPageMobilePrivate *priv;
	CEPage *parent;
	NMSetting *setting;

	self = CE_PAGE_MOBILE (g_object_new (CE_TYPE_PAGE_MOBILE, NULL));
	parent = CE_PAGE (self);

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-mobile.glade", "MobilePage", NULL);
	if (!parent->xml) {
		g_warning ("%s: Couldn't load mobile page glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "MobilePage");
	if (!parent->page) {
		g_warning ("%s: Couldn't load mobile page from glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("Mobile Broadband"));

	mobile_private_init (self);
	priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_GSM);
	if (!setting)
		setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_CDMA);

	if (!setting) {
		/* FIXME: Support add. */
		g_warning ("Adding mobile conneciton not supported yet.");
		g_object_unref (self);
		return NULL;
	}

	priv->setting = nm_setting_duplicate (setting);
	populate_ui (self, connection);

	g_signal_connect (priv->number, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->username, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->password, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->apn, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->network_id, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->network_type, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->pin, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->puk, "changed", G_CALLBACK (stuff_changed), self);

	g_signal_connect (glade_xml_get_widget (parent->xml, "mobile_show_passwords"),
					  "toggled", G_CALLBACK (show_passwords), self);

	return self;
}

static const char *
nm_entry_get_text (GtkEntry *entry)
{
	const char *txt;

	txt = gtk_entry_get_text (entry);
	if (txt && strlen (txt) > 0)
		return txt;

	return NULL;
}

static void
gsm_ui_to_setting (CEPageMobile *self)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	int net_type;

	switch (gtk_combo_box_get_active (priv->network_type)) {
	case NET_TYPE_GPRS:
		net_type = NM_GSM_NETWORK_GPRS;
		break;
	case NET_TYPE_GSM:
		net_type = NM_GSM_NETWORK_GSM;
		break;
	case NET_TYPE_PREFER_GPRS:
		net_type = NM_GSM_NETWORK_PREFER_GPRS;
		break;
	case NET_TYPE_PREFER_GSM:
		net_type = NM_GSM_NETWORK_PREFER_GSM;
		break;
	case NET_TYPE_ANY:
	default:
		net_type = NM_GSM_NETWORK_ANY;
		break;
	}

	g_object_set (priv->setting,
				  NM_SETTING_GSM_NUMBER,   nm_entry_get_text (priv->number),
				  NM_SETTING_GSM_USERNAME, nm_entry_get_text (priv->username),
				  NM_SETTING_GSM_PASSWORD, nm_entry_get_text (priv->password),
				  NM_SETTING_GSM_APN, nm_entry_get_text (priv->apn),
				  NM_SETTING_GSM_NETWORK_ID, nm_entry_get_text (priv->network_id),
				  NM_SETTING_GSM_NETWORK_TYPE, net_type,
				  NM_SETTING_GSM_PIN, nm_entry_get_text (priv->pin),
				  NM_SETTING_GSM_PUK, nm_entry_get_text (priv->puk),
				  NULL);
}

static void
cdma_ui_to_setting (CEPageMobile *self)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	g_object_set (priv->setting,
				  NM_SETTING_CDMA_NUMBER,   nm_entry_get_text (priv->number),
				  NM_SETTING_CDMA_USERNAME, nm_entry_get_text (priv->username),
				  NM_SETTING_CDMA_PASSWORD, nm_entry_get_text (priv->password),
				  NULL);
}

static void
ui_to_setting (CEPageMobile *self)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	if (NM_IS_SETTING_GSM (priv->setting))
		gsm_ui_to_setting (self);
	else if (NM_IS_SETTING_CDMA (priv->setting))
		cdma_ui_to_setting (self);
	else
		g_error ("Invalid setting");
}

static gboolean
validate (CEPage *page)
{
	CEPageMobile *self = CE_PAGE_MOBILE (page);
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (priv->setting, NULL);
}

static void
update_connection (CEPage *page, NMConnection *connection)
{
	CEPageMobile *self = CE_PAGE_MOBILE (page);
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	ui_to_setting (self);
	g_object_ref (priv->setting); /* Add setting steals the reference. */
	nm_connection_add_setting (connection, priv->setting);
}

static void
ce_page_mobile_init (CEPageMobile *self)
{
}

static void
dispose (GObject *object)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (object);

	if (priv->disposed)
		return;

	priv->disposed = TRUE;
	g_object_unref (priv->setting);

	G_OBJECT_CLASS (ce_page_mobile_parent_class)->dispose (object);
}

static void
ce_page_mobile_class_init (CEPageMobileClass *mobile_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (mobile_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (mobile_class);

	g_type_class_add_private (object_class, sizeof (CEPageMobilePrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
	parent_class->update_connection = update_connection;
}
