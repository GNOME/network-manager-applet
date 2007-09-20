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

#include <gtk/gtkcombobox.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtktogglebutton.h>
#include "nm-connection-editor.h"

G_DEFINE_TYPE (NMConnectionEditor, nm_connection_editor, G_TYPE_OBJECT)

static void
dialog_response_cb (GtkDialog *dialog, guint response, gpointer user_data)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
connection_name_changed (GtkEditable *editable, gpointer user_data)
{
	NMSettingConnection *s_connection;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_connection = (NMSettingConnection *) nm_connection_get_setting (editor->connection, NM_SETTING_CONNECTION);
	if (s_connection) {
		if (s_connection->name)
			g_free (s_connection->name);
		s_connection->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (editable)));
	}
}

static void
connection_autoconnect_changed (GtkToggleButton *button, gpointer user_data)
{
	NMSettingConnection *s_connection;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_connection = (NMSettingConnection *) nm_connection_get_setting (editor->connection, NM_SETTING_CONNECTION);
	if (s_connection)
		s_connection->autoconnect = gtk_toggle_button_get_active (button);
}

static void
ethernet_port_changed (GtkComboBox *combo, gpointer user_data)
{
	NMSettingWired *s_wired;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wired = (NMSettingWired *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRED);
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

	s_wired = (NMSettingWired *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRED);
	if (s_wired)
		s_wired->speed = gtk_spin_button_get_value_as_int (button);
}

static void
ethernet_duplex_changed (GtkToggleButton *button, gpointer user_data)
{
	NMSettingWired *s_wired;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wired = (NMSettingWired *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRED);
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

	s_wired = (NMSettingWired *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRED);
	if (s_wired)
		s_wired->auto_negotiate = gtk_toggle_button_get_active (button);
}

static void
ethernet_mtu_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWired *s_wired;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wired = (NMSettingWired *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRED);
	if (s_wired)
		s_wired->mtu = gtk_spin_button_get_value_as_int (button);
}

static void
wireless_mode_changed (GtkComboBox *combo, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRELESS);
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

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRELESS);
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

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRELESS);
	if (s_wireless)
		s_wireless->channel = gtk_spin_button_get_value_as_int (button);
}

static void
wireless_rate_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRELESS);
	if (s_wireless)
		s_wireless->rate = gtk_spin_button_get_value_as_int (button);
}

static void
wireless_tx_power_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRELESS);
	if (s_wireless)
		s_wireless->tx_power = gtk_spin_button_get_value_as_int (button);
}

static void
wireless_mtu_changed (GtkSpinButton *button, gpointer user_data)
{
	NMSettingWireless *s_wireless;
	NMConnectionEditor *editor = (NMConnectionEditor *) user_data;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRELESS);
	if (s_wireless)
		s_wireless->mtu = gtk_spin_button_get_value_as_int (button);
}

static void
nm_connection_editor_init (NMConnectionEditor *editor)
{
	/* load GUI */
	editor->gui = glade_xml_new (GLADEDIR "/nm-connection-editor.glade", "NMConnectionEditor", NULL);
	if (!editor->gui) {
		g_warning ("Could not load Glade file for connection editor");
		return;
	}

	editor->dialog = glade_xml_get_widget (editor->gui, "NMConnectionEditor");
	g_signal_connect (G_OBJECT (editor->dialog), "response", G_CALLBACK (dialog_response_cb), editor);

	editor->connection_name = glade_xml_get_widget (editor->gui, "connection_name");
	g_signal_connect (G_OBJECT (editor->connection_name), "changed",
				   G_CALLBACK (connection_name_changed), editor);

	editor->connection_autoconnect = glade_xml_get_widget (editor->gui, "connection_autoconnect");
	g_signal_connect (G_OBJECT (editor->connection_autoconnect), "toggled",
				   G_CALLBACK (connection_autoconnect_changed), editor);

	editor->ethernet_port = glade_xml_get_widget (editor->gui, "ethernet_port");
	g_signal_connect (G_OBJECT (editor->ethernet_port), "changed",
				   G_CALLBACK (ethernet_port_changed), editor);

	editor->ethernet_speed = glade_xml_get_widget (editor->gui, "ethernet_speed");
	g_signal_connect (G_OBJECT (editor->ethernet_speed), "value-changed",
				   G_CALLBACK (ethernet_speed_changed), editor);

	editor->ethernet_duplex = glade_xml_get_widget (editor->gui, "ethernet_duplex");
	g_signal_connect (G_OBJECT (editor->ethernet_duplex), "toggled",
				   G_CALLBACK (ethernet_duplex_changed), editor);

	editor->ethernet_autonegotiate = glade_xml_get_widget (editor->gui, "ethernet_autonegotiate");
	g_signal_connect (G_OBJECT (editor->ethernet_autonegotiate), "toggled",
				   G_CALLBACK (ethernet_autonegotiate_changed), editor);

	editor->ethernet_mtu = glade_xml_get_widget (editor->gui, "ethernet_mtu");
	g_signal_connect (G_OBJECT (editor->ethernet_mtu), "value-changed",
				   G_CALLBACK (ethernet_mtu_changed), editor);

	editor->wireless_mode = glade_xml_get_widget (editor->gui, "wireless_mode");
	g_signal_connect (G_OBJECT (editor->wireless_mode), "changed",
				   G_CALLBACK (wireless_mode_changed), editor);

	editor->wireless_band = glade_xml_get_widget (editor->gui, "wireless_band");
	g_signal_connect (G_OBJECT (editor->wireless_band), "changed",
				   G_CALLBACK (wireless_band_changed), editor);

	editor->wireless_channel = glade_xml_get_widget (editor->gui, "wireless_channel");
	g_signal_connect (G_OBJECT (editor->wireless_channel), "value-changed",
				   G_CALLBACK (wireless_channel_changed), editor);

	editor->wireless_rate = glade_xml_get_widget (editor->gui, "wireless_rate");
	g_signal_connect (G_OBJECT (editor->wireless_rate), "value-changed",
				   G_CALLBACK (wireless_rate_changed), editor);

	editor->wireless_tx_power = glade_xml_get_widget (editor->gui, "wireless_tx_power");
	g_signal_connect (G_OBJECT (editor->wireless_tx_power), "value-changed",
				   G_CALLBACK (wireless_tx_power_changed), editor);

	editor->wireless_mtu = glade_xml_get_widget (editor->gui, "wireless_mtu");
	g_signal_connect (G_OBJECT (editor->wireless_mtu), "value-changed",
				   G_CALLBACK (wireless_mtu_changed), editor);
}

static void
nm_connection_editor_finalize (GObject *object)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (object);

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
nm_connection_editor_new (NMConnection *connection, NMConnectionEditorPage pages)
{
	NMConnectionEditor *editor;

	editor = g_object_new (NM_TYPE_CONNECTION_EDITOR, NULL);
	if (connection != NULL)
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
	NMSettingConnection *s_connection;

	s_connection = (NMSettingConnection *) nm_connection_get_setting (editor->connection, NM_SETTING_CONNECTION);
	if (s_connection) {
		gtk_entry_set_text (GTK_ENTRY (editor->connection_name), s_connection->name);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->connection_autoconnect), s_connection->autoconnect);
	} else {
		gtk_entry_set_text (GTK_ENTRY (editor->connection_name), NULL);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->connection_autoconnect), FALSE);
	}
}

static void
fill_ethernet_values (NMConnectionEditor *editor)
{
	NMSettingWired *s_wired;

	s_wired = (NMSettingWired *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRED);
	if (s_wired) {
		if (!strcmp (s_wired->port, "tp"))
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->ethernet_port), 0);
		else if (!strcmp (s_wired->port, "aui"))
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->ethernet_port), 1);
		else if (!strcmp (s_wired->port, "bnc"))
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->ethernet_port), 2);
		else if (!strcmp (s_wired->port, "mii"))
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->ethernet_port), 3);
		else
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->ethernet_port), -1);

		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->ethernet_speed), (gdouble) s_wired->speed);

		if (!strcmp (s_wired->duplex ? s_wired->duplex : "", "full"))
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->ethernet_duplex), TRUE);
		else
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->ethernet_duplex), FALSE);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->ethernet_autonegotiate), s_wired->auto_negotiate);
		/* FIXME: MAC address */
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->ethernet_mtu), (gdouble) s_wired->mtu);
	} else {
		gtk_combo_box_set_active (GTK_COMBO_BOX (editor->ethernet_port), -1);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->ethernet_speed), (gdouble) 0);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->ethernet_duplex), FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->ethernet_autonegotiate), FALSE);
		/* FIXME: MAC address */
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->ethernet_mtu), (gdouble) 0);
	}
}

static void
fill_wireless_values (NMConnectionEditor *editor)
{
	NMSettingWireless *s_wireless;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (editor->connection, NM_SETTING_WIRELESS);
	if (s_wireless) {
		/* FIXME: SSID */

		if (!strcmp (s_wireless->mode ? s_wireless->mode : "", "infrastructure"))
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->wireless_mode), 0);
		else if (!strcmp (s_wireless->mode ? s_wireless->mode : "", "adhoc"))
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->wireless_mode), 1);
		else
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->wireless_mode), -1);

		if (!strcmp (s_wireless->band ? s_wireless->band : "", "a"))
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->wireless_band), 0);
		else if (!strcmp (s_wireless->band ? s_wireless->band : "", "bg"))
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->wireless_band), 1);
		else
			gtk_combo_box_set_active (GTK_COMBO_BOX (editor->wireless_band), -1);

		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->wireless_channel), (gdouble) s_wireless->channel);
		/* FIXME: BSSID */
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->wireless_rate), (gdouble) s_wireless->rate);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->wireless_tx_power), (gdouble) s_wireless->tx_power);
		/* FIXME: MAC address */
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->wireless_mtu), (gdouble) s_wireless->mtu);
	} else {
		/* FIXME: SSID */
		gtk_combo_box_set_active (GTK_COMBO_BOX (editor->wireless_mode), -1);
		gtk_combo_box_set_active (GTK_COMBO_BOX (editor->wireless_band), -1);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->wireless_channel), (gdouble) 0);
		/* FIXME: BSSID */
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->wireless_rate), (gdouble) 0);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->wireless_tx_power), (gdouble) 0);
		/* FIXME: MAC address */
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->wireless_mtu), (gdouble) 0);
	}
}

static void
fill_ip4_values (NMConnectionEditor *editor)
{
	NMSettingIP4Config *s_ip4;

	s_ip4 = (NMSettingIP4Config *) nm_connection_get_setting (editor->connection, NM_SETTING_IP4_CONFIG);
	if (s_ip4) {
	} else {
	}
}

void
nm_connection_editor_set_connection (NMConnectionEditor *editor, NMConnection *connection)
{
	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));

	/* clean previous connection */
	if (editor->connection) {
		g_object_unref (G_OBJECT (editor->connection));
		editor->connection = NULL;
	}

	editor->connection = (NMConnection *) g_object_ref (connection);

	/* set the UI */
	fill_connection_values (editor);
	fill_ethernet_values (editor);
	fill_wireless_values (editor);
	fill_ip4_values (editor);
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
