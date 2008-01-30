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

void wep_auth_method_changed(GtkWidget *combo, gpointer data);
void wep_show_toggled(GtkToggleButton *button, gpointer data);
void set_key_button_clicked_cb(GtkButton *button, gpointer user_data);

GtkWidget *get_wep_widget(WE_DATA *we_data)
{
	GtkWidget			*main_widget = NULL;
	GtkWidget			*widget = NULL;
	gint				intValue;

	we_data->sub_xml = glade_xml_new(we_data->glade_file, 
									"wep_notebook", NULL);
	if(we_data->sub_xml == NULL)
		return NULL;

	main_widget = glade_xml_get_widget(we_data->sub_xml, "wep_notebook");
	if(main_widget != NULL)
		g_object_ref(G_OBJECT(main_widget));


	widget = glade_xml_get_widget(we_data->sub_xml, "wep_show_checkbutton");
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  G_CALLBACK (wep_show_toggled), we_data);

	widget = glade_xml_get_widget(we_data->sub_xml, "auth_method_combo");
	intValue = eh_gconf_client_get_int(we_data, "wep_auth_algorithm");
	if(intValue == IW_AUTH_ALG_SHARED_KEY)
		gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 1);
	else
		gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
		
	g_signal_connect( G_OBJECT(widget), "changed", 
			GTK_SIGNAL_FUNC (wep_auth_method_changed), we_data);

	widget = glade_xml_get_widget(we_data->sub_xml, "wep_key_label");
	gtk_label_set_text(GTK_LABEL(widget), _("Key"));

	widget = glade_xml_get_widget(we_data->sub_xml, "wep_show_checkbutton");
	gtk_button_set_label(GTK_BUTTON(widget), _("_Show Key"));

	widget = glade_xml_get_widget(we_data->sub_xml, "wep_set_key_button");
	g_signal_connect (G_OBJECT (widget), "clicked",
	                  G_CALLBACK (set_key_button_clicked_cb), we_data);

	return main_widget;
}


void wep_auth_method_changed(GtkWidget *combo, gpointer data)
{
	WE_DATA *we_data;
	gint	intValue;

	we_data = data;

	intValue = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	if(intValue == 0)
		eh_gconf_client_set_int(we_data, "wep_auth_algorithm",
									IW_AUTH_ALG_OPEN_SYSTEM);
	else
		eh_gconf_client_set_int(we_data, "wep_auth_algorithm", 
									IW_AUTH_ALG_SHARED_KEY);
}



void wep_show_toggled(GtkToggleButton *button, gpointer data)
{
	GtkWidget *widget;
	WE_DATA *we_data;

	we_data = data;

	widget = glade_xml_get_widget(we_data->sub_xml, "wep_key_entry");

	if(gtk_toggle_button_get_active(button))
	{
		gchar *key;
		GnomeKeyringResult kresult;

		kresult = get_key_from_keyring(we_data->essid_value, &key);
		if(kresult == GNOME_KEYRING_RESULT_OK ||
				kresult == GNOME_KEYRING_RESULT_NO_SUCH_KEYRING)
		{
			gtk_widget_set_sensitive(widget, TRUE);

			if(key != NULL)
			{
				gtk_entry_set_text(GTK_ENTRY(widget), key);
				g_free(key);
			}
		}
		else
			gtk_toggle_button_set_active(button, FALSE);
		if(kresult == GNOME_KEYRING_RESULT_DENIED)
		{
			gtk_entry_set_text(GTK_ENTRY(widget), _("Unable to read key"));
		}
	}
	else
	{
		gtk_widget_set_sensitive(widget, FALSE);
		gtk_entry_set_text(GTK_ENTRY(widget), "");
	}
}


void set_key_button_clicked_cb(GtkButton *button, gpointer user_data)
{
	GladeXML			*glade_xml;
	GtkWidget			*dialog;
	GtkWidget			*keyEntry;
	GtkWidget			*formatCombo;
	WE_DATA				*we_data;
	GtkListStore      *store;
	GtkTreeIter			iter;
	GtkWindow			*parentWindow;

	we_data = user_data;

	glade_xml = glade_xml_new(we_data->glade_file, 
									"wep_key_editor", NULL);


	dialog = glade_xml_get_widget (glade_xml, "wep_key_editor");

	keyEntry = glade_xml_get_widget (glade_xml, "wep_key_editor_entry");

	formatCombo = glade_xml_get_widget (glade_xml, "wep_key_editor_combo");

	parentWindow = GTK_WINDOW(gtk_widget_get_ancestor(
					GTK_WIDGET(button), GTK_TYPE_WINDOW));

	gtk_window_set_transient_for(GTK_WINDOW(dialog), parentWindow);

	store = gtk_list_store_new (1, G_TYPE_STRING);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
						0, _("Hex"), 
						-1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
						0, _("ASCII"), 
						-1);

	gint we_cipher = eh_gconf_client_get_int(we_data, "we_cipher");
	if(we_cipher == IW_AUTH_CIPHER_WEP104)
	{
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 
				0, _("Passphrase"), 
				-1);
	}

	gtk_combo_box_set_model(GTK_COMBO_BOX(formatCombo), 
									GTK_TREE_MODEL (store));

	gtk_combo_box_set_active(GTK_COMBO_BOX(formatCombo), 0);

	g_object_unref (store);

	gint result = gtk_dialog_run (GTK_DIALOG (dialog));

	if(result == GTK_RESPONSE_OK)
	{
		const gchar *key;
		GnomeKeyringResult kresult;

		key = gtk_entry_get_text(GTK_ENTRY(keyEntry));

		if(key != NULL)
		{
			kresult = set_key_in_keyring (we_data->essid_value, key);

			if(kresult != GNOME_KEYRING_RESULT_OK)
			{
				GtkWidget *errorDialog = gtk_message_dialog_new (parentWindow,
						GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_CLOSE,
						_("Unable to set key"));
				gtk_message_dialog_format_secondary_text (
						GTK_MESSAGE_DIALOG (errorDialog),
						_("There was a problem setting the wireless key to the gnome keyring. Error 0x%02X."),
						(int)kresult);

				gtk_dialog_run (GTK_DIALOG (errorDialog));
				gtk_widget_destroy (errorDialog);
			}
		}
	}

	gtk_widget_destroy(dialog);
	g_free(glade_xml);
}


