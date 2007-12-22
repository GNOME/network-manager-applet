/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
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
 * (C) Copyright 2004-2005 Red Hat, Inc.
 */

#include <string.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtklabel.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-setting-vpn.h>

#include "nm-connection-editor.h"

G_DEFINE_TYPE (NMConnectionEditor, nm_connection_editor, G_TYPE_OBJECT)

static void
dialog_response_cb (GtkDialog *dialog, guint response, gpointer user_data)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static int
get_property_default (NMSetting *setting, const char *property_name)
{
	GParamSpec *spec;
	GValue value = { 0, };

	spec = g_object_class_find_property (G_OBJECT_GET_CLASS (setting), property_name);
	g_return_val_if_fail (spec != NULL, -1);

	g_value_init (&value, spec->value_type);
	g_param_value_set_default (spec, &value);

	if (G_VALUE_HOLDS_CHAR (&value))
		return (int) g_value_get_char (&value);
	else if (G_VALUE_HOLDS_INT (&value))
		return g_value_get_int (&value);
	else if (G_VALUE_HOLDS_INT64 (&value))
		return (int) g_value_get_int64 (&value);
	else if (G_VALUE_HOLDS_LONG (&value))
		return (int) g_value_get_long (&value);
	else if (G_VALUE_HOLDS_UINT (&value))
		return (int) g_value_get_uint (&value);
	else if (G_VALUE_HOLDS_UINT64 (&value))
		return (int) g_value_get_uint64 (&value);
	else if (G_VALUE_HOLDS_ULONG (&value))
		return (int) g_value_get_ulong (&value);
	else if (G_VALUE_HOLDS_UCHAR (&value))
		return (int) g_value_get_uchar (&value);
	g_return_val_if_fail (FALSE, 0);
	return 0;
}

static inline GtkWidget *
get_widget (NMConnectionEditor *editor, const char *name)
{
	GtkWidget *widget;

	g_return_val_if_fail (editor != NULL, NULL);

	widget = glade_xml_get_widget (editor->gui, name);
	g_return_val_if_fail (widget != NULL, NULL);
	return widget;
}

static void
add_page (NMConnectionEditor *editor,
          const char *page_name,
          const char *label_text)
{
	GtkWidget *notebook;
	GtkWidget *page;
	GtkWidget *label;

	notebook = get_widget (editor, "notebook");
	page = get_widget (editor, page_name);
	label = gtk_label_new (label_text);
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page, label);
}

static void
nm_connection_editor_update_title (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;

	g_return_if_fail (editor != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	if (s_con->id) {
		char *title = g_strdup_printf (_("Editing %s"), s_con->id);
		gtk_window_set_title (GTK_WINDOW (editor->dialog), title);
		g_free (title);
	} else
		gtk_window_set_title (GTK_WINDOW (editor->dialog), _("Editing unamed connection"));
}

static void
connection_name_changed (GtkEditable *editable, gpointer user_data)
{
	NMSettingConnection *s_con;
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (user_data);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	if (s_con)
		g_object_set (G_OBJECT (s_con), NM_SETTING_CONNECTION_ID, gtk_entry_get_text (GTK_ENTRY (editable)), NULL);
	nm_connection_editor_update_title (editor);
}

#if 0
static void
connection_autoconnect_changed (GtkToggleButton *button, gpointer user_data)
{
	NMSettingConnection *s_connection;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_connection = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	if (s_connection)
		s_connection->autoconnect = gtk_toggle_button_get_active (button);
}

static void
ethernet_port_changed (GtkComboBox *combo, gpointer user_data)
{
	NMSettingWired *s_wired;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRED));
	if (s_wired) {
		if (s_wired->port)
			g_free (s_wired->port);

		switch (gtk_combo_box_get_active (combo)) {
		case 0 : s_wired->port = g_strdup ("tp"); break;
		case 1 : s_wired->port = g_strdup ("aui"); break;
		case 2 : s_wired->port = g_strdup ("bnc"); break;
		case 3 : s_wired->port = g_strdup ("mii"); break;
		}
	}
}

static void
ethernet_speed_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWired *s_wired;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRED));
	if (s_wired)
		s_wired->speed = gtk_spin_button_get_value_as_int (button);
}

static void
ethernet_duplex_changed (GtkToggleButton *button, gpointer user_data)
{
	NMSettingWired *s_wired;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRED));
	if (s_wired) {
		if (s_wired->duplex)
			g_free (s_wired->duplex);

		if (gtk_toggle_button_get_active (button))
			s_wired->duplex = g_strdup ("full");
		else
			s_wired->duplex = g_strdup ("half");
	}
}

static void
ethernet_autonegotiate_changed (GtkToggleButton *button, gpointer user_data)
{
	NMSettingWired *s_wired;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRED));
	if (s_wired)
		s_wired->auto_negotiate = gtk_toggle_button_get_active (button);
}

static void
ethernet_mtu_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWired *s_wired;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRED));
	if (s_wired)
		s_wired->mtu = gtk_spin_button_get_value_as_int (button);
}

static void
wireless_mode_changed (GtkComboBox *combo, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRELESS));
	if (s_wireless) {
		if (s_wireless->mode)
			g_free (s_wireless->mode);

		switch (gtk_combo_box_get_active (combo)) {
		case 0 : s_wireless->mode = g_strdup ("infrastructure"); break;
		case 1 : s_wireless->mode = g_strdup ("adhoc"); break;
		}
	}
}

static void
wireless_band_changed (GtkComboBox *combo, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRELESS));
	if (s_wireless) {
		if (s_wireless->band)
			g_free (s_wireless->band);

		switch (gtk_combo_box_get_active (combo)) {
		case 0 : s_wireless->band = g_strdup ("a"); break;
		case 1 : s_wireless->band = g_strdup ("bg"); break;
		}
	}
}

static void
wireless_channel_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRELESS));
	if (s_wireless)
		s_wireless->channel = gtk_spin_button_get_value_as_int (button);
}

static void
wireless_rate_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRELESS));
	if (s_wireless)
		s_wireless->rate = gtk_spin_button_get_value_as_int (button);
}

static void
wireless_tx_power_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRELESS));
	if (s_wireless)
		s_wireless->tx_power = gtk_spin_button_get_value_as_int (button);
}

static void
wireless_mtu_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRELESS));
	if (s_wireless)
		s_wireless->mtu = gtk_spin_button_get_value_as_int (button);
}
#endif

static void
nm_connection_editor_init (NMConnectionEditor *editor)
{
	GtkWidget *widget;

	/* load GUI */
	editor->gui = glade_xml_new (GLADEDIR "/nm-connection-editor.glade", NULL, NULL);
	if (!editor->gui) {
		g_warning ("Could not load Glade file for connection editor");
		return;
	}

	editor->dialog = glade_xml_get_widget (editor->gui, "NMConnectionEditor");
	g_signal_connect (G_OBJECT (editor->dialog), "response", G_CALLBACK (dialog_response_cb), editor);

	widget = glade_xml_get_widget (editor->gui, "connection_name");
	g_signal_connect (G_OBJECT (widget), "changed",
				   G_CALLBACK (connection_name_changed), editor);
}

static void
nm_connection_editor_finalize (GObject *object)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (object);

	if (editor->connection)
		g_object_unref (G_OBJECT (editor->connection));

	gtk_widget_destroy (editor->dialog);
	g_object_unref (editor->gui);

	G_OBJECT_CLASS (nm_connection_editor_parent_class)->finalize (object);
}

static void
nm_connection_editor_class_init (NMConnectionEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/* virtual methods */
	object_class->finalize = nm_connection_editor_finalize;
}

NMConnectionEditor *
nm_connection_editor_new (NMConnection *connection)
{
	NMConnectionEditor *editor;

	g_return_val_if_fail (connection != NULL, NULL);

	editor = g_object_new (NM_TYPE_CONNECTION_EDITOR, NULL);
	nm_connection_editor_set_connection (editor, connection);

	return editor;
}

NMConnection *
nm_connection_editor_get_connection (NMConnectionEditor *editor)
{
	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), NULL);

	return editor->connection;
}

static void
fill_connection_values (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	GtkWidget *name;
	GtkWidget *autoconnect;

	name = get_widget (editor, "connection_name");
	autoconnect = get_widget (editor, "connection_autoconnect");

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	if (s_con) {
		gtk_entry_set_text (GTK_ENTRY (name), s_con->id);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoconnect), s_con->autoconnect);
	} else {
		gtk_entry_set_text (GTK_ENTRY (name), NULL);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoconnect), FALSE);
	}
}

static void
add_wired_page (NMConnectionEditor *editor)
{
	NMSettingWired *s_wired;
	GtkWidget *port;
	GtkWidget *speed;
	GtkWidget *duplex;
	GtkWidget *autoneg;
	GtkWidget *mtu;

	add_page (editor, "WiredPage", _("Wired"));

	port = get_widget (editor, "ethernet_port");
	speed = get_widget (editor, "ethernet_speed");
	duplex = get_widget (editor, "ethernet_duplex");
	autoneg = get_widget (editor, "ethernet_autonegotiate");
	mtu = get_widget (editor, "ethernet_mtu");

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRED));
	if (s_wired) {
		int port_idx = 0;

		if (s_wired->port) {
			if (!strcmp (s_wired->port, "tp"))
				port_idx = 1;
			else if (!strcmp (s_wired->port, "aui"))
				port_idx = 2;
			else if (!strcmp (s_wired->port, "bnc"))
				port_idx = 3;
			else if (!strcmp (s_wired->port, "mii"))
				port_idx = 4;
		}
		gtk_combo_box_set_active (GTK_COMBO_BOX (port), port_idx);

		gtk_spin_button_set_value (GTK_SPIN_BUTTON (speed), (gdouble) s_wired->speed);

		if (!strcmp (s_wired->duplex ? s_wired->duplex : "", "full"))
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (duplex), TRUE);
		else
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (duplex), FALSE);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoneg), s_wired->auto_negotiate);
		/* FIXME: MAC address */
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (mtu), (gdouble) s_wired->mtu);
	} else {
		gtk_combo_box_set_active (GTK_COMBO_BOX (port), -1);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (speed), (gdouble) 0);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (duplex), FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoneg), FALSE);
		/* FIXME: MAC address */
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (mtu), (gdouble) 0);
	}
}

static void
spin_value_changed_cb (GtkSpinButton *button, gpointer user_data)
{
	int defvalue = GPOINTER_TO_INT (user_data);

	if (gtk_spin_button_get_value_as_int (button) == defvalue)
		gtk_entry_set_text (GTK_ENTRY (button), _("default"));
}

static void
add_wireless_page (NMConnectionEditor *editor)
{
	NMSettingWireless *s_wireless;
	int band_idx = 0;
	GtkWidget *mode;
	GtkWidget *band;
	GtkWidget *channel;
	int channel_def;
	GtkWidget *rate;
	int rate_def;
	GtkWidget *tx_power;
	int tx_power_def;
	GtkWidget *mtu;
	int mtu_def;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_WIRELESS));
	g_return_if_fail (s_wireless != NULL);

	add_page (editor, "WirelessPage", _("Wireless"));

	mode = get_widget (editor, "wireless_mode");
	band = get_widget (editor, "wireless_band");

	channel = get_widget (editor, "wireless_channel");
	channel_def = get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_CHANNEL);
	g_signal_connect (G_OBJECT (channel), "changed",
	                  (GCallback) spin_value_changed_cb,
	                  GINT_TO_POINTER (channel_def));

	rate = get_widget (editor, "wireless_rate");
	rate_def = get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_RATE);
	g_signal_connect (G_OBJECT (rate), "changed",
	                  (GCallback) spin_value_changed_cb,
	                  GINT_TO_POINTER (rate_def));

	tx_power = get_widget (editor, "wireless_tx_power");
	tx_power_def = get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_TX_POWER);
	g_signal_connect (G_OBJECT (tx_power), "changed",
	                  (GCallback) spin_value_changed_cb,
	                  GINT_TO_POINTER (tx_power_def));

	mtu = get_widget (editor, "wireless_mtu");
	mtu_def = get_property_default (NM_SETTING (s_wireless), NM_SETTING_WIRELESS_MTU);
	g_signal_connect (G_OBJECT (mtu), "changed",
	                  (GCallback) spin_value_changed_cb,
	                  GINT_TO_POINTER (mtu_def));

	/* FIXME: SSID */

	if (!strcmp (s_wireless->mode ? s_wireless->mode : "", "infrastructure"))
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), 0);
	else if (!strcmp (s_wireless->mode ? s_wireless->mode : "", "adhoc"))
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), 1);
	else
		gtk_combo_box_set_active (GTK_COMBO_BOX (mode), -1);

	if (s_wireless->band) {
		if (!strcmp (s_wireless->band ? s_wireless->band : "", "a"))
			band_idx = 1;
		else if (!strcmp (s_wireless->band ? s_wireless->band : "", "bg"))
			band_idx = 2;
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (band), band_idx);

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (channel), (gdouble) s_wireless->channel);
	spin_value_changed_cb (GTK_SPIN_BUTTON (channel), GINT_TO_POINTER (channel_def));

	/* FIXME: BSSID */
	/* FIXME: MAC address */

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (rate), (gdouble) s_wireless->rate);
	spin_value_changed_cb (GTK_SPIN_BUTTON (rate), GINT_TO_POINTER (rate_def));

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (tx_power), (gdouble) s_wireless->tx_power);
	spin_value_changed_cb (GTK_SPIN_BUTTON (tx_power), GINT_TO_POINTER (tx_power_def));

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (mtu), (gdouble) s_wireless->mtu);
	spin_value_changed_cb (GTK_SPIN_BUTTON (mtu), GINT_TO_POINTER (mtu_def));
}

static void
add_ip4_pages (NMConnectionEditor *editor)
{
	NMSettingIP4Config *s_ip4;

	add_page (editor, "IP4Page", _("IPv4 Settings"));
	add_page (editor, "IP4AddressPage", _("IPv4 Addresses"));

	s_ip4 = NM_SETTING_IP4_CONFIG (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_IP4_CONFIG));
	if (s_ip4) {
	} else {
	}
}

void
nm_connection_editor_set_connection (NMConnectionEditor *editor, NMConnection *connection)
{
	NMSettingConnection *s_con;
//	GtkWidget *widget;

	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));
	g_return_if_fail (connection != NULL);

	/* clean previous connection */
	if (editor->connection) {
		g_object_unref (G_OBJECT (editor->connection));
		editor->connection = NULL;
	}

	editor->connection = (NMConnection *) g_object_ref (connection);
	nm_connection_editor_update_title (editor);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	if (!strcmp (s_con->type, NM_SETTING_WIRED_SETTING_NAME)) {
		add_wired_page (editor);
		add_ip4_pages (editor);
	} else if (!strcmp (s_con->type, NM_SETTING_WIRELESS_SETTING_NAME)) {
		add_wireless_page (editor);
		add_ip4_pages (editor);
	} else if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME)) {
		add_ip4_pages (editor);
	} else {
		g_warning ("Unhandled setting type '%s'", s_con->type);
	}

	/* set the UI */
	fill_connection_values (editor);
}

void
nm_connection_editor_show (NMConnectionEditor *editor)
{
	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));

	gtk_widget_show (editor->dialog);
}

gint
nm_connection_editor_run_and_close (NMConnectionEditor *editor)
{
	gint result;

	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), GTK_RESPONSE_CANCEL);

	result = gtk_dialog_run (GTK_DIALOG (editor->dialog));
	gtk_widget_hide (editor->dialog);

	return result;
}
