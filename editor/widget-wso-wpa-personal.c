/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

/* Wireless Security Option WPA/WPA2 Personal Widget
 *
 * Calvin Gaisford <cgaisford@novell.com>
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
 * (C) Copyright 2006 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnome/libgnome.h>

#if !GTK_CHECK_VERSION(2,6,0)
#include <gnome.h>
#endif

#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <NetworkManager.h>
#include <cipher.h>
#include <cipher-wpa-psk-hex.h>
#include <cipher-wpa-psk-passphrase.h>

#include "widget-wso.h"
#include "libnma/libnma.h"

static void
wpa_psk_type_changed (GtkComboBox *combo, gpointer data)
{
	GtkTreeIter iter;

	if (gtk_combo_box_get_active_iter (combo, &iter)) {
		GtkTreeModel *model;
		int value;

		model = gtk_combo_box_get_model (combo);
		gtk_tree_model_get (model, &iter, WPA_KEY_TYPE_CIPHER_COL, &value, -1);

		eh_gconf_client_set_int ((WE_DATA *) data, "wpa_psk_key_mgt", value);
	}
}

static void
wpa_psk_key_entry_changed_cb (GtkEditable *editable, gpointer user_data)
{
	WE_DATA *we_data = (WE_DATA *) user_data;
	GtkEntry *entry = GTK_ENTRY (editable);
	GtkWidget *widget;
	const char *key;
	IEEE_802_11_Cipher *cipher_passphrase = NULL;
	gboolean cipher_passphrase_valid = FALSE;
	IEEE_802_11_Cipher *cipher_hex = NULL;
	gboolean cipher_hex_valid = FALSE;

	key = gtk_entry_get_text (entry);

	cipher_passphrase = g_object_get_data (G_OBJECT (entry), "cipher-passphrase");
	if (cipher_passphrase && (ieee_802_11_cipher_validate (cipher_passphrase, we_data->essid_value, key) == 0))
		cipher_passphrase_valid = TRUE;

	cipher_hex = g_object_get_data (G_OBJECT (entry), "cipher-hex");
	if (cipher_hex && (ieee_802_11_cipher_validate (cipher_hex, we_data->essid_value, key) == 0))
		cipher_hex_valid = TRUE;

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_psk_set_password");
	gtk_widget_set_sensitive (widget, (cipher_passphrase_valid || cipher_hex_valid) ? TRUE : FALSE);
}

static void
wpa_psk_show_toggled (GtkToggleButton *button, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	GtkWidget *widget;

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_psk_entry");
	gtk_entry_set_visibility (GTK_ENTRY (widget), gtk_toggle_button_get_active (button));
}

static void
wpa_psk_set_password_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	WE_DATA *we_data = (WE_DATA *) user_data;
	gint result;
	GtkWidget *entry;
	const gchar *key;
	char *hashed_key = NULL;
	GnomeKeyringResult kresult;
	IEEE_802_11_Cipher *cipher_passphrase = NULL;
	gboolean cipher_passphrase_valid = FALSE;
	IEEE_802_11_Cipher *cipher_hex = NULL;
	gboolean cipher_hex_valid = FALSE;

	entry = glade_xml_get_widget (we_data->sub_xml, "password_entry");
	key = gtk_entry_get_text (GTK_ENTRY (entry));
	if (!key)
		return;

	cipher_passphrase = g_object_get_data (G_OBJECT (entry), "cipher-passphrase");
	if (cipher_passphrase && (ieee_802_11_cipher_validate (cipher_passphrase, we_data->essid_value, key) == 0)) {
		hashed_key = ieee_802_11_cipher_hash (cipher_passphrase, we_data->essid_value, key);
		cipher_passphrase_valid = TRUE;
	}

	cipher_hex = g_object_get_data (G_OBJECT (entry), "cipher-hex");
	if (!cipher_passphrase_valid) {
		if (cipher_hex && (ieee_802_11_cipher_validate (cipher_hex, we_data->essid_value, key) == 0)) {
			hashed_key = ieee_802_11_cipher_hash (cipher_hex, we_data->essid_value, key);
			cipher_hex_valid = TRUE;
		}
	}

	if (!cipher_passphrase_valid && !cipher_hex_valid) {
		g_warning ("%s: Couldn't validate the WPA passphrase/key.", __func__);
		goto done;
	}

	kresult = set_key_in_keyring (we_data->essid_value, hashed_key);
	if (kresult != GNOME_KEYRING_RESULT_OK) {
		GtkWindow *parent;
		GtkWidget *dialog;

		parent = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (button), GTK_TYPE_WINDOW));
		dialog = gtk_message_dialog_new (parent,
										 GTK_DIALOG_DESTROY_WITH_PARENT,
										 GTK_MESSAGE_ERROR,
										 GTK_BUTTONS_CLOSE,
										 _("Unable to set password"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
										  _("There was a problem storing the password in the gnome keyring. Error 0x%02X."),
										  (int) kresult);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}

done:
	g_free (hashed_key);
}

GtkWidget *
get_wpa_personal_widget (WE_DATA *we_data)
{
	GtkWidget *main_widget;
	GtkWidget *widget;
	gint intValue;
	GtkTreeModel *tree_model;
	GtkTreeIter iter;
	int num_added;
	int capabilities = 0xFFFFFFFF;
	IEEE_802_11_Cipher *cipher_passphrase = NULL;
	IEEE_802_11_Cipher *cipher_hex = NULL;
	GnomeKeyringResult kresult;
	char *key = NULL;

	we_data->sub_xml = glade_xml_new (we_data->glade_file, "wpa_psk_notebook", NULL);
	if (!we_data->sub_xml)
		return NULL;

	main_widget = glade_xml_get_widget (we_data->sub_xml, "wpa_psk_notebook");
	if (!main_widget)
		return NULL;

	/* set the combo to match what is in gconf */
	widget = glade_xml_get_widget (we_data->sub_xml, "show_checkbutton");
	g_signal_connect (widget, "toggled", G_CALLBACK (wpa_psk_show_toggled), we_data);

	/* Key type combo */
	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_psk_type_combo");
	tree_model = wso_wpa_create_key_type_model (capabilities, TRUE, &num_added);
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), tree_model);
	g_object_unref (tree_model);

	intValue = eh_gconf_client_get_int (we_data, "wpa_psk_key_mgt");
	if (wso_wpa_key_type_get_iter (tree_model, (uint) intValue, &iter))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);

	g_signal_connect (widget, "changed", GTK_SIGNAL_FUNC (wpa_psk_type_changed), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_psk_entry");
	g_signal_connect (widget, "changed", GTK_SIGNAL_FUNC (wpa_psk_key_entry_changed_cb), we_data);
	cipher_passphrase = cipher_wpa_psk_passphrase_new ();
	cipher_hex = cipher_wpa_psk_hex_new ();
	g_object_set_data_full (G_OBJECT (widget), "cipher-passphrase", cipher_passphrase,
	                        (GDestroyNotify) ieee_802_11_cipher_unref);
	g_object_set_data_full (G_OBJECT (widget), "cipher-hex", cipher_hex,
	                        (GDestroyNotify) ieee_802_11_cipher_unref);

	/* Try to grab key from the keyring */
	kresult = get_key_from_keyring (we_data->essid_value, &key);
	if (kresult == GNOME_KEYRING_RESULT_OK || kresult == GNOME_KEYRING_RESULT_NO_SUCH_KEYRING) {
		if (key) {
			gtk_entry_set_text (GTK_ENTRY (widget), key);
			g_free (key);
		}
	}
	wpa_psk_key_entry_changed_cb (GTK_EDITABLE (widget), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_psk_set_password");
	g_signal_connect (widget, "clicked", G_CALLBACK (wpa_psk_set_password_button_clicked_cb), we_data);
	gtk_widget_show (widget);

	return main_widget;
}
