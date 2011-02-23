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
 * (C) Copyright 2008 - 2010 Red Hat, Inc.
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

G_DEFINE_TYPE (CEPageWireless, ce_page_wireless, CE_TYPE_PAGE)

#define CE_PAGE_WIRELESS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_WIRELESS, CEPageWirelessPrivate))

typedef struct {
	NMSettingWireless *setting;

	GtkEntry *ssid;
	GtkEntry *bssid;
	GtkEntry *device_mac;    /* Permanent MAC of the device */
	GtkEntry *cloned_mac;    /* Cloned MAC - used for MAC spoofing */
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
	GtkBuilder *builder;
	GtkWidget *widget;

	builder = CE_PAGE (self)->builder;

	priv->group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	priv->ssid     = GTK_ENTRY (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_ssid")));
	priv->bssid    = GTK_ENTRY (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_bssid")));
	priv->device_mac = GTK_ENTRY (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_device_mac")));
	priv->cloned_mac = GTK_ENTRY (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_cloned_mac")));
	priv->mode     = GTK_COMBO_BOX (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_mode")));
	priv->band     = GTK_COMBO_BOX (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_band")));
	priv->channel  = GTK_SPIN_BUTTON (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_channel")));

	priv->rate     = GTK_SPIN_BUTTON (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_rate")));
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "rate_units"));
	gtk_size_group_add_widget (priv->group, widget);

	priv->tx_power = GTK_SPIN_BUTTON (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_tx_power")));
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "tx_power_units"));
	gtk_size_group_add_widget (priv->group, widget);

	priv->mtu      = GTK_SPIN_BUTTON (GTK_WIDGET (gtk_builder_get_object (builder, "wireless_mtu")));
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "mtu_units"));
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

	if (nm_utils_wifi_channel_to_freq (int_channel, aband ? "a" : "bg") == -1)
		return GTK_INPUT_ERROR;

	*new_val = channel;
	return 1;
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
			int direction = 0;
			freq = nm_utils_wifi_channel_to_freq (channel, aband ? "a" : "bg");
			if (freq == -1) {
				if (priv->last_channel < channel)
					direction = 1;
				else if (priv->last_channel > channel)
					direction = -1;
				channel = nm_utils_wifi_find_next_channel (channel, direction, aband ? "a" : "bg");
				gtk_spin_button_set_value (spin, channel);
				freq = nm_utils_wifi_channel_to_freq (channel, aband ? "a" : "bg");
				if (freq == -1) {
					g_warning ("%s: invalid channel %d!", __func__, channel);
					gtk_spin_button_set_value (spin, 0);
					goto out;
				}

			}
			/* Set spin button to zero to go to "default" from the lowest channel */
			if (direction == -1 && priv->last_channel == channel) {
				buf = g_strdup_printf (_("default"));
				gtk_spin_button_set_value (spin, 0);
				channel = 0;
			} else
				buf = g_strdup_printf (_("%u (%u MHz)"), channel, freq);
		}
		priv->last_channel = channel;
	}

	if (strcmp (buf, gtk_entry_get_text (GTK_ENTRY (spin))))
		gtk_entry_set_text (GTK_ENTRY (spin), buf);

out:
	g_free (buf);
	return 1;
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

	ce_page_changed (CE_PAGE (self));
}

static void
mode_combo_changed_cb (GtkComboBox *combo,
                       gpointer user_data)
{
	CEPageWireless *self = CE_PAGE_WIRELESS (user_data);
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	CEPage *parent = CE_PAGE (self);
	GtkWidget *widget;
	gboolean show;

 	switch (gtk_combo_box_get_active (GTK_COMBO_BOX (combo))) {
 	case 1: /* adhoc */
		show = TRUE;
 		break;
 	default: /* infrastructure */
		show = FALSE;
 		break;
 	}

	if (show) {
		widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_band_label"));
		gtk_widget_show (widget);
		gtk_widget_show (GTK_WIDGET (priv->band));
		widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_channel_label"));
		gtk_widget_show (widget);
		gtk_widget_show (GTK_WIDGET (priv->channel));
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_band_label"));
		gtk_widget_hide (widget);
		gtk_widget_hide (GTK_WIDGET (priv->band));
		widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_channel_label"));
		gtk_widget_hide (widget);
		gtk_widget_hide (GTK_WIDGET (priv->channel));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_band_label"));
	gtk_widget_set_sensitive (GTK_WIDGET (widget), show);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->band), show);
	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_channel_label"));
	gtk_widget_set_sensitive (GTK_WIDGET (widget), show);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->channel), show);

	ce_page_changed (CE_PAGE (self));
}

static void
populate_ui (CEPageWireless *self)
{
	CEPageWirelessPrivate *priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);
	NMSettingWireless *setting = priv->setting;
	const GByteArray *ssid = NULL;
	const char *mode = NULL;
	const char *band = NULL;
	int band_idx = 0;
	int rate_def;
	int tx_power_def;
	int mtu_def;
	char *utf8_ssid;

	rate_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_WIRELESS_RATE);
	g_signal_connect (priv->rate, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (rate_def));
	g_signal_connect_swapped (priv->rate, "value-changed", G_CALLBACK (ce_page_changed), self);

	tx_power_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_WIRELESS_TX_POWER);
	g_signal_connect (priv->tx_power, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (tx_power_def));
	g_signal_connect_swapped (priv->tx_power, "value-changed", G_CALLBACK (ce_page_changed), self);

	mtu_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_WIRELESS_MTU);
	g_signal_connect (priv->mtu, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (mtu_def));
	g_signal_connect_swapped (priv->mtu, "value-changed", G_CALLBACK (ce_page_changed), self);

	g_object_get (setting,
				  NM_SETTING_WIRELESS_SSID, &ssid,
				  NM_SETTING_WIRELESS_MODE, &mode,
				  NM_SETTING_WIRELESS_BAND, &band,
				  NULL);

	if (ssid)
		utf8_ssid = nm_utils_ssid_to_utf8 (ssid);
	else
		utf8_ssid = g_strdup ("");
	gtk_entry_set_text (priv->ssid, utf8_ssid);
	g_signal_connect_swapped (priv->ssid, "changed", G_CALLBACK (ce_page_changed), self);
	g_free (utf8_ssid);

	/* Default to Infrastructure */
	gtk_combo_box_set_active (priv->mode, 0);
	if (mode && !strcmp (mode, "adhoc"))
		gtk_combo_box_set_active (priv->mode, 1);
	mode_combo_changed_cb (priv->mode, self);
	g_signal_connect (priv->mode, "changed", G_CALLBACK (mode_combo_changed_cb), self);

	g_signal_connect (priv->channel, "output",
	                  G_CALLBACK (channel_spin_output_cb),
	                  self);
	g_signal_connect (priv->channel, "input",
	                  G_CALLBACK (channel_spin_input_cb),
	                  self);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->channel), FALSE);
	if (band) {
		if (!strcmp (band ? band : "", "a")) {
			band_idx = 1;
			gtk_widget_set_sensitive (GTK_WIDGET (priv->channel), TRUE);
		} else if (!strcmp (band ? band : "", "bg")) {
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
	priv->last_channel = nm_setting_wireless_get_channel (setting);
	gtk_spin_button_set_value (priv->channel, (gdouble) priv->last_channel);
	g_signal_connect_swapped (priv->channel, "value-changed", G_CALLBACK (ce_page_changed), self);

	/* BSSID */
	ce_page_mac_to_entry (nm_setting_wireless_get_bssid (setting), priv->bssid);
	g_signal_connect_swapped (priv->bssid, "changed", G_CALLBACK (ce_page_changed), self);

	/* Device MAC address */
	ce_page_mac_to_entry (nm_setting_wireless_get_mac_address (setting), priv->device_mac);
	g_signal_connect_swapped (priv->device_mac, "changed", G_CALLBACK (ce_page_changed), self);

	/* Cloned MAC address */
	ce_page_mac_to_entry (nm_setting_wireless_get_cloned_mac_address (setting), priv->cloned_mac);
	g_signal_connect_swapped (priv->cloned_mac, "changed", G_CALLBACK (ce_page_changed), self);

	gtk_spin_button_set_value (priv->rate, (gdouble) nm_setting_wireless_get_rate (setting));
	gtk_spin_button_set_value (priv->tx_power, (gdouble) nm_setting_wireless_get_tx_power (setting));
	gtk_spin_button_set_value (priv->mtu, (gdouble) nm_setting_wireless_get_mtu (setting));
}

static void
finish_setup (CEPageWireless *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPage *parent = CE_PAGE (self);
	GtkWidget *widget;

	if (error)
		return;

	populate_ui (self);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_tx_power_label"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_tx_power_hbox"));
	gtk_widget_hide (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_rate_label"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wireless_rate_hbox"));
	gtk_widget_hide (widget);
}

CEPage *
ce_page_wireless_new (NMConnection *connection,
                      GtkWindow *parent_window,
                      const char **out_secrets_setting_name,
                      GError **error)
{
	CEPageWireless *self;
	CEPageWirelessPrivate *priv;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	self = CE_PAGE_WIRELESS (ce_page_new (CE_TYPE_PAGE_WIRELESS,
	                                      connection,
	                                      parent_window,
	                                      UIDIR "/ce-page-wireless.ui",
	                                      "WirelessPage",
	                                      _("Wireless")));
	if (!self) {
		g_set_error_literal (error, 0, 0, _("Could not load WiFi user interface."));
		return NULL;
	}

	wireless_private_init (self);
	priv = CE_PAGE_WIRELESS_GET_PRIVATE (self);

	priv->setting = (NMSettingWireless *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS);
	if (!priv->setting) {
		priv->setting = NM_SETTING_WIRELESS (nm_setting_wireless_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
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
	GByteArray *device_mac = NULL;
	GByteArray *cloned_mac = NULL;
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
	device_mac = ce_page_entry_to_mac (priv->device_mac, NULL);
	cloned_mac = ce_page_entry_to_mac (priv->cloned_mac, NULL);

	g_object_set (priv->setting,
				  NM_SETTING_WIRELESS_SSID, ssid,
				  NM_SETTING_WIRELESS_BSSID, bssid,
				  NM_SETTING_WIRELESS_MAC_ADDRESS, device_mac,
				  NM_SETTING_WIRELESS_CLONED_MAC_ADDRESS, cloned_mac,
				  NM_SETTING_WIRELESS_MODE, mode,
				  NM_SETTING_WIRELESS_BAND, band,
				  NM_SETTING_WIRELESS_CHANNEL, gtk_spin_button_get_value_as_int (priv->channel),
				  NM_SETTING_WIRELESS_RATE, gtk_spin_button_get_value_as_int (priv->rate),
				  NM_SETTING_WIRELESS_TX_POWER, gtk_spin_button_get_value_as_int (priv->tx_power),
				  NM_SETTING_WIRELESS_MTU, gtk_spin_button_get_value_as_int (priv->mtu),
				  NULL);

	if (ssid)
		g_byte_array_free (ssid, TRUE);
	if (device_mac)
		g_byte_array_free (device_mac, TRUE);
	if (cloned_mac)
		g_byte_array_free (cloned_mac, TRUE);
	if (bssid)
		g_byte_array_free (bssid, TRUE);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
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
	if (ignore)
		g_byte_array_free (ignore, TRUE);

	ignore = ce_page_entry_to_mac (priv->device_mac, &invalid);
	if (invalid)
		return FALSE;
	if (ignore)
		g_byte_array_free (ignore, TRUE);

	ignore = ce_page_entry_to_mac (priv->cloned_mac, &invalid);
	if (invalid)
		return FALSE;
	if (ignore)
		g_byte_array_free (ignore, TRUE);

	ui_to_setting (self);

	/* A hack to not check the wireless security here */
	security = g_strdup (nm_setting_wireless_get_security (priv->setting));
	g_object_set (priv->setting, NM_SETTING_WIRELESS_SEC, NULL, NULL);

	success = nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
	g_object_set (priv->setting, NM_SETTING_WIRELESS_SEC, security, NULL);
	g_free (security);

	return success;
}

static void
ce_page_wireless_init (CEPageWireless *self)
{
}

static void
ce_page_wireless_class_init (CEPageWirelessClass *wireless_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (wireless_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (wireless_class);

	g_type_class_add_private (object_class, sizeof (CEPageWirelessPrivate));

	/* virtual methods */
	parent_class->validate = validate;
}


void
wifi_connection_new (GtkWindow *parent,
                     PageNewConnectionResultFunc result_func,
                     PageGetConnectionsFunc get_connections_func,
                     gpointer user_data)
{
	NMConnection *connection;
	NMSetting *s_wifi;

	connection = ce_page_new_connection (_("Wireless connection %d"),
	                                     NM_SETTING_WIRELESS_SETTING_NAME,
	                                     TRUE,
	                                     get_connections_func,
	                                     user_data);
	s_wifi = nm_setting_wireless_new ();
	g_object_set (s_wifi, NM_SETTING_WIRELESS_MODE, "infrastructure", NULL);
	nm_connection_add_setting (connection, s_wifi);

	(*result_func) (connection, FALSE, NULL, user_data);
}


