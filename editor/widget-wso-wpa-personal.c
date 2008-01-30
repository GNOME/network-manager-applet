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

#include "widget-wso.h"

void wpa_psk_type_changed(GtkWidget *combo, gpointer data);
void wpa_psk_show_toggled(GtkToggleButton *button, gpointer data);
void wpa_psk_set_password_button_clicked_cb(GtkButton *button, 
												gpointer user_data);

GtkWidget *get_wpa_personal_widget(WE_DATA *we_data)
{
	GtkWidget			*main_widget = NULL;
	GtkWidget			*widget = NULL;
	gint				intValue;

	we_data->sub_xml = glade_xml_new(we_data->glade_file, 
									"wpa_psk_notebook", NULL);
	if(we_data->sub_xml == NULL)
		return NULL;

	main_widget = glade_xml_get_widget(we_data->sub_xml, "wpa_psk_notebook");
	if(main_widget != NULL)
		g_object_ref(G_OBJECT(main_widget));

	// set the combo to match what is in gconf

	widget = glade_xml_get_widget(we_data->sub_xml, "show_checkbutton");
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  G_CALLBACK (wpa_psk_show_toggled), we_data);

	widget = glade_xml_get_widget(we_data->sub_xml, "wpa_psk_type_combo");
	intValue = eh_gconf_client_get_int(we_data, "we_cipher");
	if (intValue == IW_AUTH_CIPHER_CCMP)
		gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 1);
	else if (intValue == IW_AUTH_CIPHER_TKIP)
		gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 2);
	else	// set to auto
		gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
		
	g_signal_connect( G_OBJECT(widget), "changed", 
			GTK_SIGNAL_FUNC (wpa_psk_type_changed), we_data);

	widget = glade_xml_get_widget(we_data->sub_xml, "wpa_psk_set_password");
	g_signal_connect (G_OBJECT (widget), "clicked",
	                  	G_CALLBACK (wpa_psk_set_password_button_clicked_cb), 
						we_data);

	return main_widget;
}


void wpa_psk_type_changed(GtkWidget *combo, gpointer data)
{
	WE_DATA *we_data;
	gint	intValue;

	we_data = data;

	intValue = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	if(intValue == 1)
		eh_gconf_client_set_int(we_data, "we_cipher", IW_AUTH_CIPHER_CCMP);
	else if(intValue == 2)
		eh_gconf_client_set_int(we_data, "we_cipher", IW_AUTH_CIPHER_TKIP);
	else
		eh_gconf_client_set_int(we_data, "we_cipher", 0);
}



void wpa_psk_show_toggled(GtkToggleButton *button, gpointer data)
{
	GtkWidget *widget;
	WE_DATA *we_data;

	we_data = data;

	widget = glade_xml_get_widget(we_data->sub_xml, "wpa_psk_entry");

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


void wpa_psk_set_password_button_clicked_cb(GtkButton *button, 
												gpointer user_data)
{
	GladeXML			*glade_xml;
	GtkWidget			*dialog;
	GtkWidget			*keyEntry;
	WE_DATA				*we_data;
	GtkWindow			*parentWindow;

	we_data = user_data;

	glade_xml = glade_xml_new(we_data->glade_file, 
									"set_password_dialog", NULL);

	dialog = glade_xml_get_widget (glade_xml, "set_password_dialog");

	keyEntry = glade_xml_get_widget (glade_xml, "password_entry");

	parentWindow = GTK_WINDOW(gtk_widget_get_ancestor(
					GTK_WIDGET(button), GTK_TYPE_WINDOW));

	gtk_window_set_transient_for(GTK_WINDOW(dialog), parentWindow);

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
						_("Unable to set password"));
				gtk_message_dialog_format_secondary_text (
						GTK_MESSAGE_DIALOG (errorDialog),
						_("There was a problem storing the password in the gnome keyring. Error 0x%02X."),
						(int)kresult);

				gtk_dialog_run (GTK_DIALOG (errorDialog));
				gtk_widget_destroy (errorDialog);
			}
		}
	}

	gtk_widget_destroy(dialog);
	g_free(glade_xml);
}

