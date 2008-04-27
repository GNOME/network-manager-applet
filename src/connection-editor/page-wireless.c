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
#include <math.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-wireless.h>
#include <nm-utils.h>

#include "page-wireless.h"
#include "utils.h"
#include "nm-connection-editor.h"

G_DEFINE_TYPE (CEPageWireless, ce_page_wireless, CE_TYPE_PAGE)

#define CE_PAGE_WIRELESS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_WIRELESS, CEPageWirelessPrivate))

typedef struct {
	NMSettingWireless *setting;

	GtkEntry *ssid;
	GtkEntry *bssid;
	GtkEntry *mac;
	GtkComboBox *mode;
	GtkComboBox *band;
	GtkSpinButton *channel;
	GtkSpinButton *rate;
	GtkSpinButton *tx_power;
	GtkSpinButton *mtu;

	GtkSizeGroup *group;

	int last_channel;
	gboolean disposed;
} CEPageWirelessPrivate;

static void
wireless_private_init (CEPageWireless *self)
{
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	GladeXML *xml;
	GtkWidget *widget;

	xml = CE_PAGE (self)->xml;

	priv->group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	priv->ssid     = GTK_ENTRY (glade_xml_get_widget (xml, "wireless_ssid"));
	priv->bssid    = GTK_ENTRY (glade_xml_get_widget (xml, "wireless_bssid"));
	priv->mac      = GTK_ENTRY (glade_xml_get_widget (xml, "wireless_mac"));
	priv->mode     = GTK_COMBO_BOX (glade_xml_get_widget (xml, "wireless_mode"));
	priv->band     = GTK_COMBO_BOX (glade_xml_get_widget (xml, "wireless_band"));
	priv->channel  = GTK_SPIN_BUTTON (glade_xml_get_widget (xml, "wireless_channel"));

	priv->rate     = GTK_SPIN_BUTTON (glade_xml_get_widget (xml, "wireless_rate"));
	widget = glade_xml_get_widget (xml, "rate_units");
	gtk_size_group_add_widget (priv->group, widget);

	priv->tx_power = GTK_SPIN_BUTTON (glade_xml_get_widget (xml, "wireless_tx_power"));
	widget = glade_xml_get_widget (xml, "tx_power_units");
	gtk_size_group_add_widget (priv->group, widget);

	priv->mtu      = GTK_SPIN_BUTTON (glade_xml_get_widget (xml, "wireless_mtu"));
	widget = glade_xml_get_widget (xml, "mtu_units");
	gtk_size_group_add_widget (priv->group, widget);
}

static gboolean
band_helper (CEPageWireless *self, gboolean *aband, gboolean *gband)
{
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);

	switch (gtk_combo_box_get_active (priv->band)) {
	case 1: /* A */
		*gband = FALSE;
		return TRUE;
	case 2: /* B/G */
		*aband = FALSE;
		return TRUE;
	default:
		return FALSE;
	}
}

static gint
channel_spin_input_cb (GtkSpinButton *spin, gdouble *new_val, gpointer user_data)
{
	CEPageWireless *self = CE_PAGE_WIRELESS (user_data);
	gdouble channel;
	guint32 int_channel = 0;
	gboolean aband = TRUE;
	gboolean gband = TRUE;

	if (!band_helper (self, &aband, &gband))
		return GTK_INPUT_ERROR;

	channel = g_strtod (gtk_entry_get_text (GTK_ENTRY (spin)), NULL);
	if (channel - floor (channel) < ceil (channel) - channel)
		int_channel = floor (channel);
	else
		int_channel = ceil (channel);

	if (utils_channel_to_freq (int_channel, aband ? "a" : "bg") == -1)
		return GTK_INPUT_ERROR;

	*new_val = channel;
	return TRUE;
}

static gint
channel_spin_output_cb (GtkSpinButton *spin, gpointer user_data)
{
	CEPageWireless *self = CE_PAGE_WIRELESS (user_data);
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	int channel;
	gchar *buf = NULL;
	guint32 freq;
	gboolean aband = TRUE;
	gboolean gband = TRUE;

	if (!band_helper (self, &aband, &gband))
		buf = g_strdup (_("default"));
	else {
		channel = gtk_spin_button_get_value_as_int (spin);
		if (channel == 0)
			buf = g_strdup (_("default"));
		else {
			freq = utils_channel_to_freq (channel, aband ? "a" : "bg");
			if (freq == -1) {
				int direction = 0;

				if (priv->last_channel < channel)
					direction = 1;
				else if (priv->last_channel > channel)
					direction = -1;
				channel = utils_find_next_channel (channel, direction, aband ? "a" : "bg");
				freq = utils_channel_to_freq (channel, aband ? "a" : "bg");
				if (freq == -1) {
					g_warning ("%s: invalid channel %d!", __func__, channel);
					gtk_spin_button_set_value (spin, 0);
					goto out;
				}
			}
			buf = g_strdup_printf (_("%u (%u MHz)"), channel, freq);
		}
		priv->last_channel = channel;
	}

	if (strcmp (buf, gtk_entry_get_text (GTK_ENTRY (spin))))
		gtk_entry_set_text (GTK_ENTRY (spin), buf);

out:
	g_free (buf);
	return TRUE;
}

static void
band_value_changed_cb (GtkComboBox *box, gpointer user_data)
{
	CEPageWireless *self = CE_PAGE_WIRELESS (user_data);
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	gboolean sensitive;

	priv->last_channel = 0;
	gtk_spin_button_set_value (priv->channel, 0);
 
 	switch (gtk_combo_box_get_active (GTK_COMBO_BOX (box))) {
 	case 1: /* A */
 	case 2: /* B/G */
		sensitive = TRUE;
 		break;
 	default:
		sensitive = FALSE;
 		break;
 	}
	
	gtk_widget_set_sensitive (GTK_WIDGET (priv->channel), sensitive);
}

static void
entry_changed (GtkEditable *entry, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
populate_ui (CEPageWireless *self)
{
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	NMSettingWireless *setting = priv->setting;
	int band_idx = 0;
	int rate_def;
	int tx_power_def;
	int mtu_def;
	char *utf8_ssid;

	rate_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_WIRELESS_RATE);
	g_signal_connect (priv->rate, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (rate_def));

	tx_power_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_WIRELESS_TX_POWER);
	g_signal_connect (priv->tx_power, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (tx_power_def));

	mtu_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_WIRELESS_MTU);
	g_signal_connect (priv->mtu, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (mtu_def));

	if (setting->ssid)
		utf8_ssid = nm_utils_ssid_to_utf8 ((const char *) setting->ssid->data, setting->ssid->len);
	else
		utf8_ssid = g_strdup ("");
	gtk_entry_set_text (priv->ssid, utf8_ssid);
	g_signal_connect (priv->ssid, "changed", G_CALLBACK (entry_changed), self);
	g_free (utf8_ssid);

	if (!strcmp (setting->mode ? setting->mode : "", "infrastructure"))
		gtk_combo_box_set_active (priv->mode, 0);
	else if (!strcmp (setting->mode ? setting->mode : "", "adhoc"))
		gtk_combo_box_set_active (priv->mode, 1);
	else
		gtk_combo_box_set_active (priv->mode, -1);

	g_signal_connect (priv->channel, "output",
	                  G_CALLBACK (channel_spin_output_cb),
	                  self);
	g_signal_connect (priv->channel, "input",
	                  G_CALLBACK (channel_spin_input_cb),
	                  self);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->channel), FALSE);
	if (setting->band) {
		if (!strcmp (setting->band ? setting->band : "", "a")) {
			band_idx = 1;
			gtk_widget_set_sensitive (GTK_WIDGET (priv->channel), TRUE);
		} else if (!strcmp (setting->band ? setting->band : "", "bg")) {
			band_idx = 2;
			gtk_widget_set_sensitive (GTK_WIDGET (priv->channel), TRUE);
		}
	}

	gtk_combo_box_set_active (priv->band, band_idx);
	g_signal_connect (priv->band, "changed",
	                  G_CALLBACK (band_value_changed_cb),
	                  self);

	/* Update the channel _after_ the band has been set so that it gets
	 * the right values */
	priv->last_channel = setting->channel;
	gtk_spin_button_set_value (priv->channel, (gdouble) setting->channel);

	/* BSSID */
	ce_page_mac_to_entry (setting->bssid, priv->bssid);
	g_signal_connect (priv->bssid, "changed", G_CALLBACK (entry_changed), self);

	/* MAC address */
	ce_page_mac_to_entry (setting->mac_address, priv->mac);
	g_signal_connect (priv->mac, "changed", G_CALLBACK (entry_changed), self);

	gtk_spin_button_set_value (priv->rate, (gdouble) setting->rate);
	gtk_spin_button_set_value (priv->tx_power, (gdouble) setting->tx_power);
	gtk_spin_button_set_value (priv->mtu, (gdouble) setting->mtu);
}

CEPageWireless *
ce_page_wireless_new (NMConnection *connection)
{
	CEPageWireless *self;
	CEPageWirelessPrivate *priv;
	CEPage *parent;
	NMSettingWireless *s_wireless;
	GtkWidget *widget;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	self = CE_PAGE_WIRELESS (g_object_new (CE_TYPE_PAGE_WIRELESS, NULL));
	parent = CE_PAGE (self);

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-wireless.glade", "WirelessPage", NULL);
	if (!parent->xml) {
		g_warning ("%s: Couldn't load wireless page glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "WirelessPage");
	if (!parent->page) {
		g_warning ("%s: Couldn't load wireless page from glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("Wireless"));

	wireless_private_init (self);
	priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS);
	if (s_wireless)
		priv->setting = NM_SETTING_WIRELESS (nm_setting_duplicate (NM_SETTING (s_wireless)));
	else
		priv->setting = NM_SETTING_WIRELESS (nm_setting_wireless_new ());

	populate_ui (self);

	/* Hide widgets we don't yet support */
	widget = glade_xml_get_widget (parent->xml, "wireless_band_label");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (parent->xml, "wireless_band");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (parent->xml, "wireless_channel_label");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (parent->xml, "wireless_channel");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (parent->xml, "wireless_tx_power_label");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (parent->xml, "wireless_tx_power_hbox");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (parent->xml, "wireless_rate_label");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (parent->xml, "wireless_rate_hbox");
	gtk_widget_hide (widget);

	return self;
}

GByteArray *
ce_page_wireless_get_ssid (CEPageWireless *self)
{
	CEPageWirelessPrivate *priv;
	const char *txt_ssid;
	GByteArray *ssid;

	g_return_val_if_fail (CE_IS_PAGE_WIRELESS (self), NULL);

	priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	txt_ssid = gtk_entry_get_text (priv->ssid);
	if (!txt_ssid || !strlen (txt_ssid))
		return NULL;

	ssid = g_byte_array_sized_new (strlen (txt_ssid));
	g_byte_array_append (ssid, (const guint8 *) txt_ssid, strlen (txt_ssid));

	return ssid;
}

static void
ui_to_setting (CEPageWireless *self)
{
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	GByteArray *ssid;
	GByteArray *bssid = NULL;
	GByteArray *mac = NULL;
	const char *mode;
	const char *band;

	ssid = ce_page_wireless_get_ssid (self);

	if (gtk_combo_box_get_active (priv->mode) == 1)
		mode = "adhoc";
	else
		mode = "infrastructure";

	switch (gtk_combo_box_get_active (priv->band)) {
	case 1:
		band = "a";
		break;
	case 2:
		band = "bg";
		break;
	case 0:
	default:
		band = NULL;
		break;
	}

	bssid = ce_page_entry_to_mac (priv->bssid, NULL);
	mac = ce_page_entry_to_mac (priv->mac, NULL);

	g_object_set (priv->setting,
				  NM_SETTING_WIRELESS_SSID, ssid,
				  NM_SETTING_WIRELESS_BSSID, bssid,
				  NM_SETTING_WIRELESS_MAC_ADDRESS, mac,
				  NM_SETTING_WIRELESS_MODE, mode,
				  NM_SETTING_WIRELESS_BAND, band,
				  NM_SETTING_WIRELESS_CHANNEL, gtk_spin_button_get_value_as_int (priv->channel),
				  NM_SETTING_WIRELESS_RATE, gtk_spin_button_get_value_as_int (priv->rate),
				  NM_SETTING_WIRELESS_TX_POWER, gtk_spin_button_get_value_as_int (priv->tx_power),
				  NM_SETTING_WIRELESS_MTU, gtk_spin_button_get_value_as_int (priv->mtu),
				  NULL);

	if (ssid)
		g_byte_array_free (ssid, TRUE);
	if (mac)
		g_byte_array_free (mac, TRUE);
	if (bssid)
		g_byte_array_free (bssid, TRUE);
}

static gboolean
validate (CEPage *page)
{
	CEPageWireless *self = CE_PAGE_WIRELESS (page);
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	char *security;
	gboolean success;
	gboolean invalid = FALSE;
	GByteArray *ignore;

	ignore = ce_page_entry_to_mac (priv->bssid, &invalid);
	if (invalid)
		return FALSE;

	ignore = ce_page_entry_to_mac (priv->mac, &invalid);
	if (invalid)
		return FALSE;

	ui_to_setting (self);

	/* A hack to not check the wireless security here */
	security = priv->setting->security;
	priv->setting->security = NULL;

	success = nm_setting_verify (NM_SETTING (priv->setting), NULL);
	priv->setting->security = security;

	return success;
}

static void
update_connection (CEPage *page, NMConnection *connection)
{
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (page);

	g_object_ref (priv->setting); /* Add setting steals the reference. */
	nm_connection_add_setting (connection, NM_SETTING (priv->setting));
}

static void
ce_page_wireless_init (CEPageWireless *self)
{
}

static void
dispose (GObject *object)
{
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (object);

	if (priv->disposed)
		return;

	priv->disposed = TRUE;
	g_object_unref (priv->setting);

	G_OBJECT_CLASS (ce_page_wireless_parent_class)->dispose (object);
}

static void
ce_page_wireless_class_init (CEPageWirelessClass *wireless_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (wireless_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (wireless_class);

	g_type_class_add_private (object_class, sizeof (CEPageWirelessPrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
	parent_class->update_connection = update_connection;
}
