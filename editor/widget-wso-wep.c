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
wep_show_toggled (GtkToggleButton *button, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	GtkEntry *entry;

	entry = GTK_ENTRY (glade_xml_get_widget (we_data->sub_xml, "wep_key_entry"));

	if (gtk_toggle_button_get_active (button)) {
		gchar *key;
		GnomeKeyringResult kresult;

		kresult = get_key_from_keyring (we_data->essid_value, &key);
		if (kresult == GNOME_KEYRING_RESULT_OK || kresult == GNOME_KEYRING_RESULT_NO_SUCH_KEYRING) {
			gtk_widget_set_sensitive (GTK_WIDGET (entry), TRUE);

			if (key) {
				gtk_entry_set_text (entry, key);
				g_free (key);
			}
		} else
			gtk_toggle_button_set_active (button, FALSE);

		if (kresult == GNOME_KEYRING_RESULT_DENIED)
			gtk_entry_set_text (entry, _("Unable to read key"));
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (entry), FALSE);
		gtk_entry_set_text (entry, "");
	}
}

static void
set_key_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	WE_DATA *we_data = (WE_DATA *) user_data;
	GladeXML *glade_xml;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkWindow *parent;
	GtkWidget *widget;
	gint we_cipher;
	gint result;

	glade_xml = glade_xml_new (we_data->glade_file, "wep_key_editor", NULL);

	store = gtk_list_store_new (1, G_TYPE_STRING);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, _("Hex"), -1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, _("ASCII"), -1);

	we_cipher = eh_gconf_client_get_int (we_data, "we_cipher");
	if (we_cipher == IW_AUTH_CIPHER_WEP104) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, _("Passphrase"), -1);
	}

	widget = glade_xml_get_widget (glade_xml, "wep_key_editor_combo");
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), GTK_TREE_MODEL (store));
	g_object_unref (store);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);

	parent = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (button), GTK_TYPE_WINDOW));

	widget = glade_xml_get_widget (glade_xml, "wep_key_editor");
	gtk_window_set_transient_for (GTK_WINDOW (widget), parent);
	result = gtk_dialog_run (GTK_DIALOG (widget));
	gtk_widget_hide (widget);

	if (result == GTK_RESPONSE_OK) {
		const gchar *key;
		GnomeKeyringResult kresult;

		widget = (glade_xml_get_widget (glade_xml, "wep_key_editor_entry"));
		key = gtk_entry_get_text (GTK_ENTRY (widget));

		/* FIXME: Nothing is done with the wep_key_editor_combo value ????? */

		if (key) {
			kresult = set_key_in_keyring (we_data->essid_value, key);

			if (kresult != GNOME_KEYRING_RESULT_OK) {
				GtkWidget *errorDialog = gtk_message_dialog_new (parent,
													    GTK_DIALOG_DESTROY_WITH_PARENT,
													    GTK_MESSAGE_ERROR,
													    GTK_BUTTONS_CLOSE,
													    _("Unable to set key"));
				gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (errorDialog),
												  _("There was a problem setting the wireless key to the gnome keyring. Error 0x%02X."),
												  (int) kresult);

				gtk_dialog_run (GTK_DIALOG (errorDialog));
				gtk_widget_destroy (errorDialog);
			}
		}
	}

	g_object_unref (glade_xml);
}

GtkWidget *
get_wep_widget (WE_DATA *we_data)
{
	GtkWidget *main_widget;
	GtkWidget *widget;
	gint intValue;

	we_data->sub_xml = glade_xml_new (we_data->glade_file, "wep_key_notebook", NULL);
	if (!we_data->sub_xml)
		return NULL;

	main_widget = glade_xml_get_widget (we_data->sub_xml, "wep_key_notebook");
	if (!main_widget)
		return NULL;

	widget = glade_xml_get_widget (we_data->sub_xml, "show_checkbutton");
	g_signal_connect (widget, "toggled", G_CALLBACK (wep_show_toggled), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "auth_method_combo");
	intValue = eh_gconf_client_get_int (we_data, "wep_auth_algorithm");
	if (intValue == IW_AUTH_ALG_SHARED_KEY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 1);
	else
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);

	g_signal_connect (widget, "changed", GTK_SIGNAL_FUNC (wep_auth_method_changed), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wep_set_key");
	gtk_widget_show (widget);
	g_signal_connect (widget, "clicked", G_CALLBACK (set_key_button_clicked_cb), we_data);

	return main_widget;
}
