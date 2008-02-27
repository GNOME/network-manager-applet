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

static gboolean
band_helper (CEPageWireless *self, gboolean *aband, gboolean *gband)
{
	GtkWidget *band_combo;

	band_combo = glade_xml_get_widget (CE_PAGE (self)->xml, "wireless_band");

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

				if (self->last_channel < channel)
					direction = 1;
				else if (self->last_channel > channel)
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
		self->last_channel = channel;
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
	GtkWidget *widget;

	self->last_channel = 0;

	widget = glade_xml_get_widget (CE_PAGE (self)->xml, "wireless_channel");
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

CEPageWireless *
ce_page_wireless_new (NMConnection *connection)
{
	CEPageWireless *self;
	CEPage *parent;
	NMSettingWireless *s_wireless;
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

	self = CE_PAGE_WIRELESS (g_object_new (CE_TYPE_PAGE_WIRELESS, NULL));
	parent = CE_PAGE (self);

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	if (!s_wireless) {
		g_warning ("%s: Connection didn't have a wireless setting!", __func__);
		g_object_unref (self);
		return NULL;
	}

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

	rate = glade_xml_get_widget (parent->xml, "wireless_rate");
	rate_def = ce_get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_RATE);
	g_signal_connect (G_OBJECT (rate), "output",
	                  (GCallback) ce_spin_output_with_default,
	                  GINT_TO_POINTER (rate_def));

	tx_power = glade_xml_get_widget (parent->xml, "wireless_tx_power");
	tx_power_def = ce_get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_TX_POWER);
	g_signal_connect (G_OBJECT (tx_power), "output",
	                  (GCallback) ce_spin_output_with_default,
	                  GINT_TO_POINTER (tx_power_def));

	mtu = glade_xml_get_widget (parent->xml, "wireless_mtu");
	mtu_def = ce_get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_MTU);
	g_signal_connect (G_OBJECT (mtu), "output",
	                  (GCallback) ce_spin_output_with_default,
	                  GINT_TO_POINTER (mtu_def));

	ssid = glade_xml_get_widget (parent->xml, "wireless_ssid");
	utf8_ssid = nm_utils_ssid_to_utf8 ((const char *) s_wireless->ssid->data, s_wireless->ssid->len);
	gtk_entry_set_text (GTK_ENTRY (ssid), utf8_ssid);
	g_free (utf8_ssid);

	mode = glade_xml_get_widget (parent->xml, "wireless_mode");
	if (!strcmp (s_wireless->mode ? s_wireless->mode : "", "infrastructure"))
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), 0);
	else if (!strcmp (s_wireless->mode ? s_wireless->mode : "", "adhoc"))
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), 1);
	else
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), -1);

	channel = glade_xml_get_widget (parent->xml, "wireless_channel");
	g_signal_connect (G_OBJECT (channel), "output",
	                  (GCallback) channel_spin_output_cb,
	                  self);
	g_signal_connect (G_OBJECT (channel), "input",
	                  (GCallback) channel_spin_input_cb,
	                  self);

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
	band = glade_xml_get_widget (parent->xml, "wireless_band");
	gtk_combo_box_set_active (GTK_COMBO_BOX (band), band_idx);
	g_signal_connect (G_OBJECT (band), "changed",
	                  (GCallback) band_value_changed_cb,
	                  self);

	/* Update the channel _after_ the band has been set so that it gets
	 * the right values */
	self->last_channel = s_wireless->channel;
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (channel), (gdouble) s_wireless->channel);

	/* FIXME: BSSID */
	/* FIXME: MAC address */

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (rate), (gdouble) s_wireless->rate);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (tx_power), (gdouble) s_wireless->tx_power);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (mtu), (gdouble) s_wireless->mtu);

	return self;
}

GByteArray *
ce_page_wireless_get_ssid (CEPageWireless *self)
{
	GtkWidget *widget;
	const char *txt_ssid;
	GByteArray *ssid;

	g_return_val_if_fail (CE_IS_PAGE_WIRELESS (self), NULL);

	widget = glade_xml_get_widget (CE_PAGE (self)->xml, "wireless_ssid");
	g_return_val_if_fail (widget != NULL, NULL);

	txt_ssid = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!txt_ssid || !strlen (txt_ssid))
		return NULL;

	ssid = g_byte_array_sized_new (strlen (txt_ssid));
	g_byte_array_append (ssid, (const guint8 *) txt_ssid, strlen (txt_ssid));
	return ssid;
}

static void
ce_page_wireless_init (CEPageWireless *self)
{
}

static void
ce_page_wireless_class_init (CEPageWirelessClass *wired_class)
{
}

