/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

/*
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
key_mgmt_changed (GtkComboBox *combo, gpointer data)
{
	GtkTreeIter iter;

	if (gtk_combo_box_get_active_iter (combo, &iter)) {
		GtkTreeModel *model;
		char *value;

		model = gtk_combo_box_get_model (combo);
		gtk_tree_model_get (model, &iter, LEAP_KEY_MGMT_VALUE_COL, &value, -1);

		eh_gconf_client_set_string ((WE_DATA *) data, "leap_key_mgmt", value);
		g_free (value);
	}
}

static void
username_changed (GtkEntry *widget, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	const gchar *strValue;

	strValue = gtk_entry_get_text (widget);
	if (strValue)
		eh_gconf_client_set_string (we_data, "leap_username", strValue);
}

static gboolean
username_entry_focus_lost (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
	username_changed (GTK_ENTRY (widget), data);
	return FALSE;
}

static void
show_password_toggled (GtkToggleButton *button, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	GtkWidget *widget;

	widget = glade_xml_get_widget (we_data->sub_xml, "leap_password_entry");
	gtk_entry_set_visibility (GTK_ENTRY (widget), gtk_toggle_button_get_active (button));
}

static void
password_changed (GtkButton *button, gpointer user_data)
{
	WE_DATA *we_data = (WE_DATA *) user_data;
	GtkWidget *widget;
	const gchar *key;
	GnomeKeyringResult kresult;

	widget = glade_xml_get_widget (we_data->sub_xml, "leap_password_entry");
	key = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!key)
		return;

	kresult = set_key_in_keyring (we_data->essid_value, key);
	if (kresult != GNOME_KEYRING_RESULT_OK) {
		GtkWindow *parent;

		parent = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (button), GTK_TYPE_WINDOW));
		widget = gtk_message_dialog_new (parent,
										 GTK_DIALOG_DESTROY_WITH_PARENT,
										 GTK_MESSAGE_ERROR,
										 GTK_BUTTONS_CLOSE,
										 _("Unable to set password"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (widget),
										  _("There was a problem storing the password in the gnome keyring. Error 0x%02X."),
										  (int) kresult);

		gtk_dialog_run (GTK_DIALOG (widget));
		gtk_widget_destroy (widget);
	}
}

GtkWidget *
get_leap_widget (WE_DATA *we_data)
{
	GtkWidget *main_widget;
	GtkWidget *widget;
	char *username;
	char *key_mgmt;
	GtkTreeModel *tree_model;
	GtkTreeIter iter;
	GnomeKeyringResult kresult;
	gchar *key;

	we_data->sub_xml = glade_xml_new (we_data->glade_file, "leap_notebook", NULL);
	if (!we_data->sub_xml)
		return NULL;

	main_widget = glade_xml_get_widget (we_data->sub_xml, "leap_notebook");
	if (!main_widget) {
		g_object_unref (we_data->sub_xml);
		we_data->sub_xml = NULL;
		return NULL;
	}

	/* Try to grab key from the keyring */
	widget = glade_xml_get_widget (we_data->sub_xml, "leap_password_entry");
	kresult = get_key_from_keyring (we_data->essid_value, &key);
	if (kresult == GNOME_KEYRING_RESULT_OK || kresult == GNOME_KEYRING_RESULT_NO_SUCH_KEYRING) {
		if (key) {
			gtk_entry_set_text (GTK_ENTRY (widget), key);
			g_free (key);
		}
	}

	widget = glade_xml_get_widget (we_data->sub_xml, "leap_show_password");
	g_signal_connect (widget, "toggled", G_CALLBACK (show_password_toggled), we_data);

	/* Username */
	widget = glade_xml_get_widget (we_data->sub_xml, "leap_username_entry");
	username = eh_gconf_client_get_string (we_data, "leap_username");
	if (username) {
		gtk_entry_set_text (GTK_ENTRY (widget), username);
		g_free (username);
	} else
		gtk_entry_set_text (GTK_ENTRY (widget), "");

	g_signal_connect (widget, "activate", GTK_SIGNAL_FUNC (username_changed), we_data);
	g_signal_connect (widget, "focus-out-event", GTK_SIGNAL_FUNC (username_entry_focus_lost), we_data);

	/* Set password */
	widget = glade_xml_get_widget (we_data->sub_xml, "leap_set_password");
	gtk_widget_show (widget);
	g_signal_connect (widget, "clicked", GTK_SIGNAL_FUNC (password_changed), we_data);

	/* Key management combo box */
	widget = glade_xml_get_widget (we_data->sub_xml, "leap_key_mgmt_combobox");
	tree_model = wso_leap_create_key_mgmt_model ();
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), tree_model);
	g_object_unref (tree_model);

	key_mgmt = eh_gconf_client_get_string (we_data, "leap_key_mgmt");
	if (key_mgmt && wso_leap_key_mgmt_get_iter (tree_model, key_mgmt, &iter))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);

	g_free (key_mgmt);

	g_signal_connect (widget, "changed", GTK_SIGNAL_FUNC (key_mgmt_changed), we_data);

	return main_widget;
}
