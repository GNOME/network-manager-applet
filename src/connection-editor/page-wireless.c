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

static gboolean
band_helper (GtkWidget *page, gboolean *aband, gboolean *gband)
{
	GladeXML *xml;
	GtkWidget *band_combo;

	xml = g_object_get_data (G_OBJECT (page), "glade-xml");
	band_combo = glade_xml_get_widget (xml, "wireless_band");

	switch (gtk_combo_box_get_active (GTK_COMBO_BOX (band_combo))) {
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
	GtkWidget *page = GTK_WIDGET (user_data);
	gdouble channel;
	guint32 int_channel = 0;
	gboolean aband = TRUE;
	gboolean gband = TRUE;

	if (!band_helper (page, &aband, &gband))
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
	GtkWidget *page = GTK_WIDGET (user_data);
	int channel;
	gchar *buf = NULL;
	guint32 freq;
	gboolean aband = TRUE;
	gboolean gband = TRUE;
	guint32 last_channel;

	last_channel = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (page), "last-channel"));

	if (!band_helper (page, &aband, &gband))
		buf = g_strdup (_("default"));
	else {
		channel = gtk_spin_button_get_value_as_int (spin);
		if (channel == 0)
			buf = g_strdup (_("default"));
		else {
			freq = utils_channel_to_freq (channel, aband ? "a" : "bg");
			if (freq == -1) {
				int direction = 0;

				if (last_channel < channel)
					direction = 1;
				else if (last_channel > channel)
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
		g_object_set_data (G_OBJECT (page), "last-channel", GUINT_TO_POINTER (channel));
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
	GtkWidget *page = GTK_WIDGET (user_data);
	GtkWidget *widget;
	GladeXML *xml;

	g_object_set_data (G_OBJECT (page), "last-channel", GUINT_TO_POINTER (0));

	xml = g_object_get_data (G_OBJECT (page), "glade-xml");
	widget = glade_xml_get_widget (xml, "wireless_channel");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), 0);

	switch (gtk_combo_box_get_active (GTK_COMBO_BOX (box))) {
	case 1: /* A */
	case 2: /* B/G */
		gtk_widget_set_sensitive (widget, TRUE);
		break;
	default:
		gtk_widget_set_sensitive (widget, FALSE);
		break;
	}
}

GtkWidget *
page_wireless_new (NMConnection *connection, const char **title)
{
	GladeXML *xml;
	NMSettingWireless *s_wireless;
	GtkWidget *page;
	GtkWidget *mode;
	GtkWidget *band;
	GtkWidget *channel;
	GtkWidget *rate;
	GtkWidget *ssid;
	int band_idx = 0;
	int rate_def;
	GtkWidget *tx_power;
	int tx_power_def;
	GtkWidget *mtu;
	int mtu_def;
	char *utf8_ssid;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_return_val_if_fail (s_wireless != NULL, NULL);

	xml = glade_xml_new (GLADEDIR "/ce-page-wireless.glade", "WirelessPage", NULL);
	g_return_val_if_fail (xml != NULL, NULL);
	*title = _("Wireless");

	page = glade_xml_get_widget (xml, "WirelessPage");
	g_return_val_if_fail (page != NULL, NULL);
	g_object_set_data_full (G_OBJECT (page),
	                        "glade-xml", xml,
	                        (GDestroyNotify) g_object_unref);

	rate = glade_xml_get_widget (xml, "wireless_rate");
	rate_def = ce_get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_RATE);
	g_signal_connect (G_OBJECT (rate), "output",
	                  (GCallback) ce_spin_output_with_default,
	                  GINT_TO_POINTER (rate_def));

	tx_power = glade_xml_get_widget (xml, "wireless_tx_power");
	tx_power_def = ce_get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_TX_POWER);
	g_signal_connect (G_OBJECT (tx_power), "output",
	                  (GCallback) ce_spin_output_with_default,
	                  GINT_TO_POINTER (tx_power_def));

	mtu = glade_xml_get_widget (xml, "wireless_mtu");
	mtu_def = ce_get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_MTU);
	g_signal_connect (G_OBJECT (mtu), "output",
	                  (GCallback) ce_spin_output_with_default,
	                  GINT_TO_POINTER (mtu_def));

	ssid = glade_xml_get_widget (xml, "wireless_ssid");
	utf8_ssid = nm_utils_ssid_to_utf8 ((const char *) s_wireless->ssid->data, s_wireless->ssid->len);
	gtk_entry_set_text (GTK_ENTRY (ssid), utf8_ssid);
	g_free (utf8_ssid);

	mode = glade_xml_get_widget (xml, "wireless_mode");
	if (!strcmp (s_wireless->mode ? s_wireless->mode : "", "infrastructure"))
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), 0);
	else if (!strcmp (s_wireless->mode ? s_wireless->mode : "", "adhoc"))
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), 1);
	else
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), -1);

	channel = glade_xml_get_widget (xml, "wireless_channel");
	g_signal_connect (G_OBJECT (channel), "output",
	                  (GCallback) channel_spin_output_cb,
	                  page);
	g_signal_connect (G_OBJECT (channel), "input",
	                  (GCallback) channel_spin_input_cb,
	                  page);

	gtk_widget_set_sensitive (channel, FALSE);
	if (s_wireless->band) {
		if (!strcmp (s_wireless->band ? s_wireless->band : "", "a")) {
			band_idx = 1;
			gtk_widget_set_sensitive (channel, TRUE);
		} else if (!strcmp (s_wireless->band ? s_wireless->band : "", "bg")) {
			band_idx = 2;
			gtk_widget_set_sensitive (channel, TRUE);
		}
	}
	band = glade_xml_get_widget (xml, "wireless_band");
	gtk_combo_box_set_active (GTK_COMBO_BOX (band), band_idx);
	g_signal_connect (G_OBJECT (band), "changed",
	                  (GCallback) band_value_changed_cb,
	                  page);

	/* Update the channel _after_ the band has been set so that it gets
	 * the right values */
	g_object_set_data (G_OBJECT (page), "last-channel", GUINT_TO_POINTER (s_wireless->channel));
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (channel), (gdouble) s_wireless->channel);

	/* FIXME: BSSID */
	/* FIXME: MAC address */

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (rate), (gdouble) s_wireless->rate);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (tx_power), (gdouble) s_wireless->tx_power);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (mtu), (gdouble) s_wireless->mtu);

	return page;
}


