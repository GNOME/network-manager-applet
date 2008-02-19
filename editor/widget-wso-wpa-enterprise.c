/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

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
#include "libnma/libnma.h"

static void
wpa_eap_eap_method_changed (GtkComboBox *combo, gpointer data)
{
	GtkTreeIter iter;

	if (gtk_combo_box_get_active_iter (combo, &iter)) {
		GtkTreeModel *model;
		int value;

		model = gtk_combo_box_get_model (combo);
		gtk_tree_model_get (model, &iter, WPA_KEY_TYPE_CIPHER_COL, &value, -1);

		eh_gconf_client_set_int ((WE_DATA *) data, "wpa_eap_eap_method", value);
	}
}

static void
wpa_eap_key_type_changed (GtkComboBox *combo, gpointer data)
{
	GtkTreeIter iter;

	if (gtk_combo_box_get_active_iter (combo, &iter)) {
		GtkTreeModel *model;
		int value;

		model = gtk_combo_box_get_model (combo);
		gtk_tree_model_get (model, &iter, WPA_KEY_TYPE_CIPHER_COL, &value, -1);

		eh_gconf_client_set_int ((WE_DATA *) data, "wpa_eap_key_type", value);
	}
}

static void
wpa_eap_phase2_type_changed (GtkComboBox *combo, gpointer data)
{
	GtkTreeIter iter;

	if (gtk_combo_box_get_active_iter (combo, &iter)) {
		GtkTreeModel *model;
		int value;

		model = gtk_combo_box_get_model (combo);
		gtk_tree_model_get (model, &iter, WPA_EAP_VALUE_COL, &value, -1);

		eh_gconf_client_set_int ((WE_DATA *) data, "wpa_eap_phase2_type", value);
	}
}

static void
wpa_eap_private_key_changed (GtkFileChooser *chooser, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	gchar *strValue;

	strValue = gtk_file_chooser_get_filename (chooser);
	if (strValue) {
		eh_gconf_client_set_string (we_data, "wpa_eap_private_key_file", strValue);
		g_free (strValue);
	}
}

static void
wpa_eap_client_key_changed (GtkFileChooser *chooser, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	gchar *strValue;

	strValue = gtk_file_chooser_get_filename (chooser);
	if (strValue) {
		eh_gconf_client_set_string (we_data, "wpa_eap_client_cert_file", strValue);
		g_free (strValue);
	}
}

static void
wpa_eap_ca_key_changed (GtkFileChooser *chooser, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	gchar *strValue;

	strValue = gtk_file_chooser_get_filename (chooser);
	if (strValue) {
		eh_gconf_client_set_string (we_data, "wpa_eap_ca_cert_file", strValue);
		g_free (strValue);
	}
}

static void
wpa_eap_password_entry_changed (GtkEntry *password_entry, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	const gchar *password;

	password = gtk_entry_get_text (password_entry);
	if (password) {
		GnomeKeyringResult kresult;

		kresult = set_eap_key_in_keyring (we_data->essid_value, password);
		if (kresult != GNOME_KEYRING_RESULT_OK) {
			GtkWindow *parentWindow;
			GtkWidget *errorDialog;

			parentWindow = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (password_entry), GTK_TYPE_WINDOW));
			errorDialog = gtk_message_dialog_new (parentWindow,
										   GTK_DIALOG_DESTROY_WITH_PARENT,
										   GTK_MESSAGE_ERROR,
										   GTK_BUTTONS_CLOSE,
										   _("Unable to set password"));
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (errorDialog),
											  _("There was a problem storing the private password in the gnome keyring. Error 0x%02X."),
											  (int) kresult);

			gtk_dialog_run (GTK_DIALOG (errorDialog));
			gtk_widget_destroy (errorDialog);
		}
	}
}

static gboolean
wpa_eap_password_entry_focus_lost (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
	wpa_eap_password_entry_changed (GTK_ENTRY (widget), data);
	return FALSE;
}

static void
wpa_eap_priv_password_entry_changed (GtkEntry *pass_entry, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	const gchar *password;

	password = gtk_entry_get_text (pass_entry);
	if (password) {
		GnomeKeyringResult kresult;

		kresult = set_key_in_keyring (we_data->essid_value, password);
		if (kresult != GNOME_KEYRING_RESULT_OK) {
			GtkWindow *parentWindow;
			GtkWidget *errorDialog;

			parentWindow = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (pass_entry), GTK_TYPE_WINDOW));
			errorDialog = gtk_message_dialog_new (parentWindow,
										   GTK_DIALOG_DESTROY_WITH_PARENT,
										   GTK_MESSAGE_ERROR,
										   GTK_BUTTONS_CLOSE,
										   _("Unable to set password"));
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (errorDialog),
											  _("There was a problem storing the private password in the gnome keyring. Error 0x%02X."),
											  (int) kresult);

			gtk_dialog_run (GTK_DIALOG (errorDialog));
			gtk_widget_destroy (errorDialog);
		}
	}
}

static gboolean
wpa_eap_priv_password_entry_focus_lost (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
	wpa_eap_priv_password_entry_changed (GTK_ENTRY (widget), data);
	return FALSE;
}

static void
wpa_eap_show_toggled (GtkToggleButton *button, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	GtkWidget *widget;
	gint32	sid;
	gchar *key = NULL;
	GnomeKeyringResult kresult;

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_passwd_entry");
	if (gtk_toggle_button_get_active (button)) {
		kresult = get_eap_key_from_keyring (we_data->essid_value, &key);
		if (key) {
			gtk_entry_set_text (GTK_ENTRY (widget), key);
			g_free (key);
		} else
			gtk_entry_set_text (GTK_ENTRY (widget), "");

		gtk_widget_set_sensitive (widget, TRUE);
		gtk_entry_set_editable (GTK_ENTRY (widget), TRUE);

		sid = g_signal_connect (widget, "activate", 
						    GTK_SIGNAL_FUNC (wpa_eap_password_entry_changed), we_data);
		g_object_set_data (G_OBJECT (widget), "password_activate_sid", GINT_TO_POINTER (sid));
		sid = g_signal_connect (widget, "focus-out-event", 
						    GTK_SIGNAL_FUNC (wpa_eap_password_entry_focus_lost), 
						    we_data);
		g_object_set_data (G_OBJECT (widget), "password_focus_out_sid", GINT_TO_POINTER (sid));
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
		gtk_entry_set_editable (GTK_ENTRY (widget), FALSE);
		gtk_entry_set_text (GTK_ENTRY (widget), "");

		sid = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "password_activate_sid"));
		g_signal_handler_disconnect (widget, sid);
		sid = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "password_focus_out_sid"));
		g_signal_handler_disconnect (widget, sid);
	}

	widget = glade_xml_get_widget(we_data->sub_xml, "wpa_eap_private_key_passwd_entry");
	if (gtk_toggle_button_get_active(button)) {
		kresult = get_key_from_keyring (we_data->essid_value, &key);
		if(key) {
			gtk_entry_set_text (GTK_ENTRY (widget), key);
			g_free (key);
		}

		gtk_widget_set_sensitive (widget, TRUE);
		gtk_entry_set_editable (GTK_ENTRY (widget), TRUE);

		sid = g_signal_connect (widget, "activate", 
						    GTK_SIGNAL_FUNC (wpa_eap_priv_password_entry_changed), 
						    we_data);
		g_object_set_data (G_OBJECT (widget), "priv_password_activate_sid", GINT_TO_POINTER (sid));
		sid = g_signal_connect (widget, "focus-out-event", 
						    GTK_SIGNAL_FUNC (wpa_eap_priv_password_entry_focus_lost), 
						    we_data);
		g_object_set_data (G_OBJECT (widget), "priv_password_focus_out_sid", GINT_TO_POINTER(sid));
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
		gtk_entry_set_editable (GTK_ENTRY (widget), FALSE);
		gtk_entry_set_text (GTK_ENTRY (widget), "");

		sid = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "priv_password_activate_sid"));
		g_signal_handler_disconnect(widget, sid);
		sid = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "priv_password_focus_out_sid"));
		g_signal_handler_disconnect (widget, sid);
	}
}

static void
wpa_eap_identity_entry_changed (GtkEntry *widget, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	const gchar *strValue;

	strValue = gtk_entry_get_text (widget);
	if (strValue)
		eh_gconf_client_set_string (we_data, "wpa_eap_identity", strValue);
}

static void
wpa_eap_anon_identity_entry_changed (GtkEntry *widget, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	const gchar *val;

	val = gtk_entry_get_text (widget);
	if (val)
		eh_gconf_client_set_string (we_data, "wpa_eap_anon_identity", val);
}

static gboolean
wpa_eap_identity_entry_focus_lost (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
	wpa_eap_identity_entry_changed (GTK_ENTRY (widget), data);
	return FALSE;
}

static gboolean
wpa_eap_anon_identity_entry_focus_lost (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
	wpa_eap_anon_identity_entry_changed (GTK_ENTRY (widget), data);
	return FALSE;
}

GtkWidget *
get_wpa_enterprise_widget (WE_DATA *we_data)
{
	GtkWidget			*main_widget = NULL;
	GtkWidget			*widget = NULL;
	gint				intValue;
	gchar				*strValue;
	GtkTreeModel *tree_model;
	GtkTreeIter iter;
	GtkCellRenderer *renderer;
	int num_added;
	int capabilities = 0xFFFFFFFF;

	we_data->sub_xml = glade_xml_new (we_data->glade_file, "wpa_eap_notebook", NULL);
	if (!we_data->sub_xml)
		return NULL;

	main_widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_notebook");
	if (!main_widget)
		return NULL;

	widget = glade_xml_get_widget (we_data->sub_xml, "show_checkbutton");
	g_signal_connect (widget, "toggled", G_CALLBACK (wpa_eap_show_toggled), we_data);

	renderer = gtk_cell_renderer_text_new ();

	/* EAP method combo */
	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_eap_method_combo");
	tree_model = wso_wpa_create_eap_method_model ();
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), tree_model);
	g_object_unref (tree_model);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (widget), renderer, "text", 0, NULL);

	intValue = eh_gconf_client_get_int (we_data, "wpa_eap_eap_method");
	if (wso_wpa_eap_method_get_iter (tree_model, (uint) intValue, &iter))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);

	g_signal_connect (widget, "changed", GTK_SIGNAL_FUNC (wpa_eap_eap_method_changed), we_data);

	/* EAP key type combo */
	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_key_type_combo");
	tree_model = wso_wpa_create_key_type_model (capabilities, TRUE, &num_added);
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), tree_model);
	g_object_unref (tree_model);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (widget), renderer, "text", 0, NULL);

	intValue = eh_gconf_client_get_int (we_data, "wpa_eap_key_type");
	if (wso_wpa_key_type_get_iter (tree_model, (uint) intValue, &iter))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);

	g_signal_connect (widget, "changed", GTK_SIGNAL_FUNC (wpa_eap_key_type_changed), we_data);

	/* EAP phase2 type combo */
	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_phase2_type_combo");
	tree_model = wso_wpa_create_phase2_type_model (capabilities, &num_added);
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), tree_model);
	g_object_unref (tree_model);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (widget), renderer, "text", 0, NULL);

	intValue = eh_gconf_client_get_int (we_data, "wpa_eap_phase2_type");
	if (wso_wpa_phase2_type_get_iter (tree_model, (uint) intValue, &iter))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);

	g_signal_connect (widget, "changed", GTK_SIGNAL_FUNC (wpa_eap_phase2_type_changed), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_identity_entry");
	strValue = eh_gconf_client_get_string (we_data, "wpa_eap_identity");
	if (strValue) {
		gtk_entry_set_text (GTK_ENTRY (widget), strValue);
		g_free (strValue);
	} else
		gtk_entry_set_text (GTK_ENTRY (widget), "");

	g_signal_connect (widget, "activate", GTK_SIGNAL_FUNC (wpa_eap_identity_entry_changed), we_data);
	g_signal_connect (widget, "focus-out-event", GTK_SIGNAL_FUNC (wpa_eap_identity_entry_focus_lost), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_anon_identity_entry");
	strValue = eh_gconf_client_get_string (we_data, "wpa_eap_anon_identity");
	if (strValue) {
		gtk_entry_set_text (GTK_ENTRY (widget), strValue);
		g_free (strValue);
	} else
		gtk_entry_set_text (GTK_ENTRY (widget), "");

	g_signal_connect (widget, "activate", GTK_SIGNAL_FUNC (wpa_eap_anon_identity_entry_changed), we_data);
	g_signal_connect (widget, "focus-out-event", GTK_SIGNAL_FUNC (wpa_eap_anon_identity_entry_focus_lost), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_private_key_file_chooser_button");
	strValue = eh_gconf_client_get_string (we_data, "wpa_eap_private_key_file");
	if (strValue) {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), strValue);
		g_free (strValue);
	}

	g_signal_connect (widget, "selection-changed", GTK_SIGNAL_FUNC (wpa_eap_private_key_changed), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_client_cert_file_chooser_button");
	strValue = eh_gconf_client_get_string (we_data, "wpa_eap_client_cert_file");
	if (strValue) {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), strValue);
		g_free (strValue);
	}

	g_signal_connect (widget, "selection-changed", GTK_SIGNAL_FUNC (wpa_eap_client_key_changed), we_data);

	widget = glade_xml_get_widget (we_data->sub_xml, "wpa_eap_ca_cert_file_chooser_button");
	strValue = eh_gconf_client_get_string (we_data, "wpa_eap_ca_cert_file");
	if (strValue) {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), strValue);
		g_free (strValue);
	}

	g_signal_connect (widget, "selection-changed", GTK_SIGNAL_FUNC (wpa_eap_ca_key_changed), we_data);

	return main_widget;
}
