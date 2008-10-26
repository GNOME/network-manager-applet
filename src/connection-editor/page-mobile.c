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
#define NET_TYPE_3G          1
#define NET_TYPE_2G          2
#define NET_TYPE_PREFER_3G   3
#define NET_TYPE_PREFER_2G   4

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
	GError *error = NULL;
	GHashTable *secrets;

	secrets = nm_gconf_get_keyring_items (connection,
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
	GtkWidget *widget;
	const char *s;

	s = nm_setting_gsm_get_number (setting);
	if (s)
		gtk_entry_set_text (priv->number, s);

	s = nm_setting_gsm_get_username (setting);
	if (s)
		gtk_entry_set_text (priv->username, s);

	s = nm_setting_gsm_get_apn (setting);
	if (s)
		gtk_entry_set_text (priv->apn, s);

	s = nm_setting_gsm_get_network_id (setting);
	if (s)
		gtk_entry_set_text (priv->network_id, s);

	switch (nm_setting_gsm_get_network_type (setting)) {
	case NM_GSM_NETWORK_UMTS_HSPA:
		type_idx = NET_TYPE_3G;
		break;
	case NM_GSM_NETWORK_GPRS_EDGE:
		type_idx = NET_TYPE_2G;
		break;
	case NM_GSM_NETWORK_PREFER_UMTS_HSPA:
		type_idx = NET_TYPE_PREFER_3G;
		break;
	case NM_GSM_NETWORK_PREFER_GPRS_EDGE:
		type_idx = NET_TYPE_PREFER_2G;
		break;
	case NM_GSM_NETWORK_ANY:
	default:
		type_idx = NET_TYPE_ANY;
		break;
	}
	gtk_combo_box_set_active (priv->network_type, type_idx);

	/* Hide network type widgets; not supported yet */
	gtk_widget_hide (GTK_WIDGET (priv->network_type));
	widget = glade_xml_get_widget (CE_PAGE (self)->xml, "type_label");
	gtk_widget_hide (widget);

	/* Hide Band widgets; not supported yet */
	widget = glade_xml_get_widget (CE_PAGE (self)->xml, "mobile_band");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (CE_PAGE (self)->xml, "band_label");
	gtk_widget_hide (widget);

	secrets = get_secrets (connection, nm_setting_get_name (priv->setting));

	s = nm_setting_gsm_get_password (setting);
	if (s)
		gtk_entry_set_text (priv->password, s);
	else if (secrets) {
		value = g_hash_table_lookup (secrets, NM_SETTING_GSM_PASSWORD);
		if (value)
			gtk_entry_set_text (priv->password, g_value_get_string (value));
	}

	s = nm_setting_gsm_get_pin (setting);
	if (s)
		gtk_entry_set_text (priv->pin, s);
	else if (secrets) {
		value = g_hash_table_lookup (secrets, NM_SETTING_GSM_PIN);
		if (value)
			gtk_entry_set_text (priv->pin, g_value_get_string (value));
	}

	s = nm_setting_gsm_get_puk (setting);
	if (s)
		gtk_entry_set_text (priv->pin, s);
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
	const char *s;

	s = nm_setting_cdma_get_number (setting);
	if (s)
		gtk_entry_set_text (priv->number, s);

	s = nm_setting_cdma_get_username (setting);
	if (s)
		gtk_entry_set_text (priv->username, s);

	secrets = get_secrets (connection, nm_setting_get_name (priv->setting));

	s = nm_setting_cdma_get_password (setting);
	if (s)
		gtk_entry_set_text (priv->password, s);
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

	priv->setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_GSM);
	if (!priv->setting)
		priv->setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_CDMA);

	if (!priv->setting) {
		/* FIXME: Support add. */
		g_warning ("Adding mobile conneciton not supported yet.");
		g_object_unref (self);
		return NULL;
	}

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
	case NET_TYPE_3G:
		net_type = NM_GSM_NETWORK_UMTS_HSPA;
		break;
	case NET_TYPE_2G:
		net_type = NM_GSM_NETWORK_GPRS_EDGE;
		break;
	case NET_TYPE_PREFER_3G:
		net_type = NM_GSM_NETWORK_PREFER_UMTS_HSPA;
		break;
	case NET_TYPE_PREFER_2G:
		net_type = NM_GSM_NETWORK_PREFER_GPRS_EDGE;
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
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageMobile *self = CE_PAGE_MOBILE (page);
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (priv->setting, NULL, error);
}

static void
ce_page_mobile_init (CEPageMobile *self)
{
}

static void
ce_page_mobile_class_init (CEPageMobileClass *mobile_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (mobile_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (mobile_class);

	g_type_class_add_private (object_class, sizeof (CEPageMobilePrivate));

	/* virtual methods */
	parent_class->validate = validate;
}
