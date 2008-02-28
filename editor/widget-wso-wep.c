/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

/* Wireless Security Option WEP Widget
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
#include <cipher-wep-ascii.h>
#include <cipher-wep-passphrase.h>
#include <cipher-wep-hex.h>

#include "widget-wso.h"

static void
wep_auth_method_changed (GtkComboBox *combo, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	gint	intValue;

	intValue = gtk_combo_box_get_active (combo);
	if (intValue == 0)
		eh_gconf_client_set_int (we_data, "wep_auth_algorithm", IW_AUTH_ALG_OPEN_SYSTEM);
	else
		eh_gconf_client_set_int(we_data, "wep_auth_algorithm", IW_AUTH_ALG_SHARED_KEY);
}

static void
show_password_toggled (GtkToggleButton *button, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	GtkWidget *widget;

	widget = glade_xml_get_widget (we_data->sub_xml, "wep_key_entry");
	gtk_entry_set_visibility (GTK_ENTRY (widget), gtk_toggle_button_get_active (button));
}

static void
wep_key_entry_changed_cb (GtkEditable *editable, gpointer user_data)
{
	WE_DATA *we_data = (WE_DATA *) user_data;
	GtkEntry *entry = GTK_ENTRY (editable);
	GtkWidget *widget;
	const char *key;
	IEEE_802_11_Cipher *cipher128 = NULL;
	gboolean cipher128_valid = FALSE;
	IEEE_802_11_Cipher *cipher64 = NULL;
	gboolean cipher64_valid = FALSE;

	key = gtk_entry_get_text (entry);

	cipher128 = g_object_get_data (G_OBJECT (entry), "cipher128");
	if (cipher128 && (ieee_802_11_cipher_validate (cipher128, we_data->essid_value, key) == 0))
		cipher128_valid = TRUE;

	cipher64 = g_object_get_data (G_OBJECT (entry), "cipher64");
	if (cipher64 && (ieee_802_11_cipher_validate (cipher64, we_data->essid_value, key) == 0))
		cipher64_valid = TRUE;

	widget = glade_xml_get_widget (we_data->sub_xml, "wep_set_key");
	gtk_widget_set_sensitive (widget, (cipher128_valid || cipher64_valid) ? TRUE : FALSE);
}

static void
set_key_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	WE_DATA *we_data = (WE_DATA *) user_data;
	GtkWindow *parent;
	GtkWidget *widget;
	gint we_cipher;
	gint result;
	const gchar *key;
	char *hashed_key = NULL;
	GnomeKeyringResult kresult;
	IEEE_802_11_Cipher *cipher128 = NULL;
	gboolean cipher128_valid = FALSE;
	IEEE_802_11_Cipher *cipher64 = NULL;
	gboolean cipher64_valid = FALSE;

	widget = glade_xml_get_widget (we_data->sub_xml, "wep_key_entry");
	key = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!key)
		return;

	cipher128 = g_object_get_data (G_OBJECT (widget), "cipher128");
	if (cipher128 && (ieee_802_11_cipher_validate (cipher128, we_data->essid_value, key) == 0)) {
		hashed_key = ieee_802_11_cipher_hash (cipher128, we_data->essid_value, key);
		cipher128_valid = TRUE;
	}

	cipher64 = g_object_get_data (G_OBJECT (widget), "cipher64");
	if (!cipher128_valid) {
		if (cipher64 && (ieee_802_11_cipher_validate (cipher64, we_data->essid_value, key) == 0)) {
			hashed_key = ieee_802_11_cipher_hash (cipher64, we_data->essid_value, key);
			cipher64_valid = TRUE;
		}
	}

	if (!cipher128_valid && !cipher64_valid) {
		g_warning ("%s: Couldn't validate the WEP key.", __func__);
		goto done;
	}

	kresult = set_key_in_keyring (we_data->essid_value, hashed_key);
	if (kresult == GNOME_KEYRING_RESULT_OK) {
		eh_gconf_client_set_int(we_data, "we_cipher",
			ieee_802_11_cipher_get_we_cipher (cipher128_valid ? cipher128 : cipher64));
	} else {
		GtkWidget *dialog;
		GtkWindow *parent;

		parent = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (button), GTK_TYPE_WINDOW));
		dialog = gtk_message_dialog_new (parent,
										 GTK_DIALOG_DESTROY_WITH_PARENT,
										 GTK_MESSAGE_ERROR,
										 GTK_BUTTONS_CLOSE,
										 _("Unable to set key"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
										  _("There was a problem setting the wireless key to the gnome keyring. Error 0x%02X."),
										  (int) kresult);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}

done:
	g_free (hashed_key);
}

GtkWidget *
get_wep_widget (WE_DATA *we_data)
{
	GtkWidget *main_widget;
	GtkWidget *widget;
	GtkWidget *entry;
	gchar *key;
	gint intValue;
	IEEE_802_11_Cipher *cipher128 = NULL;
	IEEE_802_11_Cipher *cipher64 = NULL;

	we_data->sub_xml = glade_xml_new (we_data->glade_file, "wep_key_notebook", NULL);
	if (!we_data->sub_xml)
		return NULL;

	main_widget = glade_xml_get_widget (we_data->sub_xml, "wep_key_notebook");
	if (!main_widget) {
		g_object_unref (we_data->sub_xml);
		we_data->sub_xml = NULL;
		return NULL;
	}

	entry = glade_xml_get_widget (we_data->sub_xml, "wep_key_entry");
	g_signal_connect (entry, "changed", G_CALLBACK (wep_key_entry_changed_cb), we_data);

	switch (we_data->sec_option) {
		case SEC_OPTION_WEP_PASSPHRASE:
			cipher128 = cipher_wep128_passphrase_new ();
			break;
		case SEC_OPTION_WEP_HEX:
			cipher128 = cipher_wep128_hex_new ();
			cipher64 = cipher_wep64_hex_new ();
			break;
		case SEC_OPTION_WEP_ASCII:
			cipher128 = cipher_wep128_ascii_new ();
			cipher64 = cipher_wep64_ascii_new ();
			break;
		default:
			g_assert_not_reached ();
			break;
	}
	g_object_set_data_full (G_OBJECT (entry), "cipher128", cipher128,
	                        (GDestroyNotify) ieee_802_11_cipher_unref);
	g_object_set_data_full (G_OBJECT (entry), "cipher64", cipher64,
	                        (GDestroyNotify) ieee_802_11_cipher_unref);

	widget = glade_xml_get_widget (we_data->sub_xml, "show_checkbutton");
	g_signal_connect (widget, "toggled", G_CALLBACK (show_password_toggled), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wep_set_key");
	gtk_widget_show (widget);
	g_signal_connect (widget, "clicked", G_CALLBACK (set_key_button_clicked_cb), we_data);

	/* Try to grab key from the keyring, but only if the user chose Hex,
	 * because the key is always stored as hex in the keyring.
	 */
	if (we_data->sec_option == SEC_OPTION_WEP_HEX) {
		GnomeKeyringResult kresult;

		kresult = get_key_from_keyring (we_data->essid_value, &key);
		if (kresult == GNOME_KEYRING_RESULT_OK || kresult == GNOME_KEYRING_RESULT_NO_SUCH_KEYRING) {
			if (key) {
				gtk_entry_set_text (GTK_ENTRY (entry), key);
				g_free (key);
			}
		}
	}
	wep_key_entry_changed_cb (GTK_EDITABLE (entry), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "auth_method_combo");
	intValue = eh_gconf_client_get_int (we_data, "wep_auth_algorithm");
	if (intValue == IW_AUTH_ALG_SHARED_KEY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 1);
	else
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);

	g_signal_connect (widget, "changed", GTK_SIGNAL_FUNC (wep_auth_method_changed), we_data);

	return main_widget;
}
