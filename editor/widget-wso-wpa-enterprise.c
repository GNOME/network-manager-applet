/* Wireless Security Option WPA/WPA2 Enterprise Widget
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

void wpa_eap_eap_method_changed(GtkWidget *combo, gpointer data);
void wpa_eap_key_type_changed(GtkWidget *combo, gpointer data);
void wpa_eap_show_toggled(GtkToggleButton *button, gpointer data);
void wpa_eap_private_key_changed(GtkWidget *button, gpointer data);
void wpa_eap_client_key_changed(GtkWidget *button, gpointer data);
void wpa_eap_ca_key_changed(GtkWidget *button, gpointer data);


void wpa_eap_password_entry_changed(GtkEntry *password_entry, gpointer data);
gboolean wpa_eap_password_entry_focus_lost(GtkWidget *widget,
								GdkEventFocus *event, gpointer data);
void wpa_eap_priv_password_entry_changed(GtkEntry *pass_entry, gpointer data);
gboolean wpa_eap_priv_password_entry_focus_lost(GtkWidget *widget,
								GdkEventFocus *event, gpointer data);

void wpa_eap_identity_entry_changed(GtkEntry *widget, gpointer data);
gboolean wpa_eap_identity_entry_focus_lost(GtkWidget *widget,
								GdkEventFocus *event, gpointer data);
void wpa_eap_anon_identity_entry_changed(GtkEntry *widget, gpointer data);
gboolean wpa_eap_anon_identity_entry_focus_lost(GtkWidget *widget,
								GdkEventFocus *event, gpointer data);

GtkWidget *get_wpa_enterprise_widget(WE_DATA *we_data)
{
	GtkWidget			*main_widget = NULL;
	GtkWidget			*widget = NULL;
	gint				intValue;
	gchar				*strValue;

	we_data->sub_xml = glade_xml_new(we_data->glade_file, 
									"wpa_eap_notebook", NULL);
	if(we_data->sub_xml == NULL)
		return NULL;

	main_widget = glade_xml_get_widget(we_data->sub_xml, "wpa_eap_notebook");
	if(main_widget != NULL)
		g_object_ref(G_OBJECT(main_widget));


	widget = glade_xml_get_widget(we_data->sub_xml, "show_checkbutton");
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  G_CALLBACK (wpa_eap_show_toggled), we_data);


	widget = glade_xml_get_widget(we_data->sub_xml, "wpa_eap_eap_method_combo");
	intValue = eh_gconf_client_get_int(we_data, "wpa_eap_eap_method");
	switch(intValue)
	{
		case NM_EAP_METHOD_MD5:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
			break;
		case NM_EAP_METHOD_MSCHAP:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 1);
			break;
		case NM_EAP_METHOD_OTP:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 2);
			break;
		case NM_EAP_METHOD_GTC:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 3);
			break;
		default:
		case NM_EAP_METHOD_PEAP:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 4);
			break;
		case NM_EAP_METHOD_TLS:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 5);
			break;
		case NM_EAP_METHOD_TTLS:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 6);
			break;
	}
	g_signal_connect( G_OBJECT(widget), "changed", 
			GTK_SIGNAL_FUNC (wpa_eap_eap_method_changed), we_data);


	widget = glade_xml_get_widget(we_data->sub_xml, "wpa_eap_key_type_combo");
	intValue = eh_gconf_client_get_int(we_data, "wpa_eap_key_type");
	switch(intValue)
	{
		default:
		case NM_AUTH_TYPE_WPA_PSK_AUTO:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
			break;
		case NM_AUTH_TYPE_WPA_PSK_TKIP:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 1);
			break;
		case NM_AUTH_TYPE_WPA_PSK_CCMP:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 2);
			break;
		case NM_AUTH_TYPE_WPA_EAP:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 3);
			break;
	}
	g_signal_connect( G_OBJECT(widget), "changed", 
			GTK_SIGNAL_FUNC (wpa_eap_key_type_changed), we_data);


	widget = glade_xml_get_widget(we_data->sub_xml, "wpa_eap_identity_entry");
	strValue = eh_gconf_client_get_string(we_data, "wpa_eap_identity");
	if(strValue != NULL)
	{
		gtk_entry_set_text(GTK_ENTRY(widget), strValue);
		g_free(strValue);
	}
	else
		gtk_entry_set_text(GTK_ENTRY(widget), "");
	g_signal_connect( G_OBJECT(widget), "activate", 
					GTK_SIGNAL_FUNC (wpa_eap_identity_entry_changed), we_data);
	g_signal_connect( G_OBJECT(widget), "focus-out-event", 
						GTK_SIGNAL_FUNC (wpa_eap_identity_entry_focus_lost), 
						we_data);

	widget = glade_xml_get_widget(we_data->sub_xml, 
									"wpa_eap_anon_identity_entry");
	strValue = eh_gconf_client_get_string(we_data, "wpa_eap_anon_identity");
	if(strValue != NULL)
	{
		gtk_entry_set_text(GTK_ENTRY(widget), strValue);
		g_free(strValue);
	}
	else
		gtk_entry_set_text(GTK_ENTRY(widget), "");
	g_signal_connect( G_OBJECT(widget), "activate", 
					GTK_SIGNAL_FUNC (wpa_eap_anon_identity_entry_changed), 
					we_data);
	g_signal_connect( G_OBJECT(widget), "focus-out-event", 
					GTK_SIGNAL_FUNC (wpa_eap_anon_identity_entry_focus_lost), 
					we_data);


	widget = glade_xml_get_widget(we_data->sub_xml, 
									"wpa_eap_private_key_file_chooser_button");
	strValue = eh_gconf_client_get_string(we_data, "wpa_eap_private_key_file");
	if(strValue != NULL)
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(widget), strValue);
	g_signal_connect( G_OBJECT(widget), "selection-changed", 
			GTK_SIGNAL_FUNC (wpa_eap_private_key_changed), we_data);



	widget = glade_xml_get_widget(we_data->sub_xml, 
									"wpa_eap_client_cert_file_chooser_button");
	strValue = eh_gconf_client_get_string(we_data, "wpa_eap_client_cert_file");
	if(strValue != NULL)
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(widget), strValue);
	g_signal_connect( G_OBJECT(widget), "selection-changed", 
			GTK_SIGNAL_FUNC (wpa_eap_client_key_changed), we_data);



	widget = glade_xml_get_widget(we_data->sub_xml, 
									"wpa_eap_ca_cert_file_chooser_button");
	strValue = eh_gconf_client_get_string(we_data, "wpa_eap_ca_cert_file");
	if(strValue != NULL)
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(widget), strValue);
	g_signal_connect( G_OBJECT(widget), "selection-changed", 
			GTK_SIGNAL_FUNC (wpa_eap_ca_key_changed), we_data);

	return main_widget;
}


void wpa_eap_eap_method_changed(GtkWidget *combo, gpointer data)
{
	WE_DATA *we_data;
	gint	intValue;

	we_data = data;

	intValue = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	switch(intValue)
	{
		case 0:
			eh_gconf_client_set_int(we_data, "wpa_eap_eap_method", 
												NM_EAP_METHOD_MD5);
			break;
		case 1:
			eh_gconf_client_set_int(we_data, "wpa_eap_eap_method", 
												NM_EAP_METHOD_MSCHAP);
			break;
		case 2:
			eh_gconf_client_set_int(we_data, "wpa_eap_eap_method", 
												NM_EAP_METHOD_OTP);
			break;
		case 3:
			eh_gconf_client_set_int(we_data, "wpa_eap_eap_method", 
												NM_EAP_METHOD_GTC);
			break;
		default:
		case 4:
			eh_gconf_client_set_int(we_data, "wpa_eap_eap_method", 
												NM_EAP_METHOD_PEAP);
			break;
		case 5:
			eh_gconf_client_set_int(we_data, "wpa_eap_eap_method", 
												NM_EAP_METHOD_TLS);
			break;
		case 6:
			eh_gconf_client_set_int(we_data, "wpa_eap_eap_method", 
												NM_EAP_METHOD_TTLS);
			break;
	
	}
}


void wpa_eap_key_type_changed(GtkWidget *combo, gpointer data)
{
	WE_DATA *we_data;
	gint	intValue;

	we_data = data;

	intValue = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	switch(intValue)
	{
		default:
		case 0:
			eh_gconf_client_set_int(we_data, "wpa_eap_key_type", 
												NM_AUTH_TYPE_WPA_PSK_AUTO);
			break;
		case 1:
			eh_gconf_client_set_int(we_data, "wpa_eap_key_type", 
												NM_AUTH_TYPE_WPA_PSK_TKIP);
			break;
		case 2:
			eh_gconf_client_set_int(we_data, "wpa_eap_key_type", 
												NM_AUTH_TYPE_WPA_PSK_CCMP);
			break;
		case 3:
			eh_gconf_client_set_int(we_data, "wpa_eap_key_type", 
												NM_AUTH_TYPE_WPA_EAP);
			break;
	}
}


void wpa_eap_private_key_changed(GtkWidget *button, gpointer data)
{
	WE_DATA *we_data;
	gchar	*strValue = NULL;

	we_data = data;

	strValue = gtk_file_chooser_get_filename(
								GTK_FILE_CHOOSER(button));
	if(strValue != NULL)
		eh_gconf_client_set_string(we_data, "wpa_eap_private_key_file", 
												strValue);
}


void wpa_eap_client_key_changed(GtkWidget *button, gpointer data)
{
	WE_DATA *we_data;
	gchar	*strValue = NULL;

	we_data = data;

	strValue = gtk_file_chooser_get_filename(
								GTK_FILE_CHOOSER(button));
	if(strValue != NULL)
		eh_gconf_client_set_string(we_data, "wpa_eap_client_cert_file", 
												strValue);
}


void wpa_eap_ca_key_changed(GtkWidget *button, gpointer data)
{
	WE_DATA *we_data;
	gchar	*strValue = NULL;

	we_data = data;

	strValue = gtk_file_chooser_get_filename(
								GTK_FILE_CHOOSER(button));
	if(strValue != NULL)
		eh_gconf_client_set_string(we_data, "wpa_eap_ca_cert_file", 
												strValue);
}

void wpa_eap_show_toggled(GtkToggleButton *button, gpointer data)
{
	GtkWidget *widget;
	WE_DATA *we_data;
	gint32	sid;
	gchar *key = NULL;
	GnomeKeyringResult kresult;

	we_data = data;

	widget = glade_xml_get_widget(we_data->sub_xml, "wpa_eap_passwd_entry");
	if(gtk_toggle_button_get_active(button))
	{
		kresult = get_eap_key_from_keyring(we_data->essid_value, &key);
		if (key != NULL)
		{
			gtk_entry_set_text(GTK_ENTRY(widget), key);
			g_free(key);
		}
		else
			gtk_entry_set_text(GTK_ENTRY(widget), "");

		gtk_widget_set_sensitive(widget, TRUE);
		gtk_entry_set_editable(GTK_ENTRY(widget), TRUE);

		sid = g_signal_connect( G_OBJECT(widget), "activate", 
					GTK_SIGNAL_FUNC (wpa_eap_password_entry_changed), we_data);
		g_object_set_data(G_OBJECT(widget), "password_activate_sid",
						GINT_TO_POINTER(sid));
		sid = g_signal_connect( G_OBJECT(widget), "focus-out-event", 
						GTK_SIGNAL_FUNC (wpa_eap_password_entry_focus_lost), 
						we_data);
		g_object_set_data(G_OBJECT(widget), "password_focus_out_sid",
						GINT_TO_POINTER(sid));
	}
	else
	{
		gtk_widget_set_sensitive(widget, FALSE);
		gtk_entry_set_editable(GTK_ENTRY(widget), FALSE);
		gtk_entry_set_text(GTK_ENTRY(widget), "");

		sid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), 
							"password_activate_sid"));
		g_signal_handler_disconnect(G_OBJECT(widget), sid);
		sid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), 
							"password_focus_out_sid"));
		g_signal_handler_disconnect(G_OBJECT(widget), sid);
	}


	widget = glade_xml_get_widget(we_data->sub_xml, 
									"wpa_eap_private_key_passwd_entry");
	if(gtk_toggle_button_get_active(button))
	{
		kresult = get_key_from_keyring(we_data->essid_value, &key);
		if(key != NULL)
		{
			gtk_entry_set_text(GTK_ENTRY(widget), key);
			g_free(key);
		}

		gtk_widget_set_sensitive(widget, TRUE);
		gtk_entry_set_editable(GTK_ENTRY(widget), TRUE);

		sid = g_signal_connect( G_OBJECT(widget), "activate", 
					GTK_SIGNAL_FUNC (wpa_eap_priv_password_entry_changed), 
					we_data);
		g_object_set_data(G_OBJECT(widget), "priv_password_activate_sid",
					GINT_TO_POINTER(sid));
		sid = g_signal_connect( G_OBJECT(widget), "focus-out-event", 
					GTK_SIGNAL_FUNC (wpa_eap_priv_password_entry_focus_lost), 
					we_data);
		g_object_set_data(G_OBJECT(widget), "priv_password_focus_out_sid",
					GINT_TO_POINTER(sid));
	}
	else
	{
		gtk_widget_set_sensitive(widget, FALSE);
		gtk_entry_set_editable(GTK_ENTRY(widget), FALSE);
		gtk_entry_set_text(GTK_ENTRY(widget), "");

		sid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), 
							"priv_password_activate_sid"));
		g_signal_handler_disconnect(G_OBJECT(widget), sid);
		sid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), 
							"priv_password_focus_out_sid"));
		g_signal_handler_disconnect(G_OBJECT(widget), sid);
	}

}


gboolean wpa_eap_password_entry_focus_lost(GtkWidget *widget,
								GdkEventFocus *event, gpointer data)
{
	wpa_eap_password_entry_changed(GTK_ENTRY(widget), data);
	return FALSE;
}

void wpa_eap_password_entry_changed(GtkEntry *password_entry, gpointer data)
{
	gchar *password;
	WE_DATA *we_data;

	we_data = data;

	password = (gchar *)gtk_entry_get_text(password_entry);
	if(password != NULL)
	{
		GnomeKeyringResult kresult;

		kresult = set_eap_key_in_keyring (we_data->essid_value, password);
		if(kresult != GNOME_KEYRING_RESULT_OK)
		{
			GtkWindow *parentWindow;

			parentWindow = GTK_WINDOW(gtk_widget_get_ancestor(
					GTK_WIDGET(password_entry), GTK_TYPE_WINDOW));

			GtkWidget *errorDialog = gtk_message_dialog_new (parentWindow,
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					_("Unable to set password"));
			gtk_message_dialog_format_secondary_text (
					GTK_MESSAGE_DIALOG (errorDialog),
					_("There was a problem storing the private password in the gnome keyring. Error 0x%02X."),
					(int)kresult);

			gtk_dialog_run (GTK_DIALOG (errorDialog));
			gtk_widget_destroy (errorDialog);
		}
	}
}


gboolean wpa_eap_priv_password_entry_focus_lost(GtkWidget *widget,
								GdkEventFocus *event, gpointer data)
{
	wpa_eap_priv_password_entry_changed(GTK_ENTRY(widget), data);
	return FALSE;
}

void wpa_eap_priv_password_entry_changed(GtkEntry *pass_entry, gpointer data)
{
	gchar *password;
	WE_DATA *we_data;

	we_data = data;

	password = (gchar *)gtk_entry_get_text(pass_entry);
	if(password != NULL)
	{
		GnomeKeyringResult kresult;

		kresult = set_key_in_keyring (we_data->essid_value, password);
		if(kresult != GNOME_KEYRING_RESULT_OK)
		{
			GtkWindow *parentWindow;

			parentWindow = GTK_WINDOW(gtk_widget_get_ancestor(
					GTK_WIDGET(pass_entry), GTK_TYPE_WINDOW));

			GtkWidget *errorDialog = gtk_message_dialog_new (parentWindow,
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					_("Unable to set password"));
			gtk_message_dialog_format_secondary_text (
					GTK_MESSAGE_DIALOG (errorDialog),
					_("There was a problem storing the private password in the gnome keyring. Error 0x%02X."),
					(int)kresult);

			gtk_dialog_run (GTK_DIALOG (errorDialog));
			gtk_widget_destroy (errorDialog);
		}
	}
}

gboolean wpa_eap_identity_entry_focus_lost(GtkWidget *widget,
								GdkEventFocus *event, gpointer data)
{
	wpa_eap_identity_entry_changed(GTK_ENTRY(widget), data);
	return FALSE;
}

void wpa_eap_identity_entry_changed(GtkEntry *widget, gpointer data)
{
	gchar *strValue;
	WE_DATA *we_data;

	we_data = data;

	strValue = (gchar *)gtk_entry_get_text(widget);
	if(strValue != NULL)
		eh_gconf_client_set_string(we_data, "wpa_eap_identity", strValue);
}

gboolean wpa_eap_anon_identity_entry_focus_lost(GtkWidget *widget,
								GdkEventFocus *event, gpointer data)
{
	wpa_eap_anon_identity_entry_changed(GTK_ENTRY(widget), data);
	return FALSE;
}

void wpa_eap_anon_identity_entry_changed(GtkEntry *widget, gpointer data)
{
	gchar *strValue;
	WE_DATA *we_data;

	we_data = data;

	strValue = (gchar *)gtk_entry_get_text(widget);
	if(strValue != NULL)
		eh_gconf_client_set_string(we_data, "wpa_eap_anon_identity", strValue);
}

