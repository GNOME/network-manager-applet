/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * (C) Copyright 2006 Thiago Jung Bauermann <thiago.bauermann@gmail.com>
 */

/* This file is heavily based on wso-wpa-eap.c */

#include <glib.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <dbus/dbus.h>
#include <iwlib.h>

#include "wireless-security-option.h"
#include "wso-leap.h"
#include "wso-private.h"
#include "dbus-helpers.h"
#include "libnma/libnma.h"
#include "NetworkManager.h"


struct OptData
{
	const char *	username;
	const char *	passwd;
	const char *	key_mgmt;
};


static void
data_free_func (WirelessSecurityOption *opt)
{
	g_return_if_fail (opt != NULL);
	g_return_if_fail (opt->data != NULL);

	if (opt->data->key_mgmt) {
		   g_free((char *) opt->data->key_mgmt);
	}

	memset (opt->data, 0, sizeof (opt->data));
	g_free (opt->data);
}


static gboolean
append_dbus_params_func (WirelessSecurityOption *opt,
                         const char *ssid,
                         DBusMessage *message)
{
	GtkWidget *		entry;
	GtkTreeModel *		combo_model;
	GtkTreeIter		iter;
	DBusMessageIter	dbus_iter;

	g_return_val_if_fail (opt != NULL, FALSE);
	g_return_val_if_fail (opt->data != NULL, FALSE);

	entry = glade_xml_get_widget (opt->uixml, "leap_username_entry");
	opt->data->username = gtk_entry_get_text (GTK_ENTRY (entry));

	entry = glade_xml_get_widget (opt->uixml, "leap_password_entry");
	opt->data->passwd = gtk_entry_get_text (GTK_ENTRY (entry));

	entry = glade_xml_get_widget (opt->uixml, "leap_key_mgmt_combobox");
	combo_model = gtk_combo_box_get_model(GTK_COMBO_BOX(entry));
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(entry), &iter);
	gtk_tree_model_get(combo_model, &iter, LEAP_KEY_MGMT_VALUE_COL, &opt->data->key_mgmt, -1);

	dbus_message_iter_init_append (message, &dbus_iter);

	nmu_security_serialize_leap_with_cipher (&dbus_iter,
								      opt->data->username,
								      opt->data->passwd,
									 opt->data->key_mgmt);

	return TRUE;
}

static void show_password_cb (GtkToggleButton *button, GtkEntry *entry)
{
	gtk_entry_set_visibility (entry, gtk_toggle_button_get_active (button));
}

static GtkWidget *
widget_create_func (WirelessSecurityOption *opt,
                    GtkSignalFunc validate_cb,
                    gpointer user_data)
{
	GtkWidget *	entry;
	GtkWidget *	widget;
	GtkWidget *	key_mgmt;
	GtkTreeModel *	model;

	g_return_val_if_fail (opt != NULL, NULL);
	g_return_val_if_fail (opt->data != NULL, NULL);
	g_return_val_if_fail (validate_cb != NULL, NULL);

	widget = wso_widget_helper (opt);

	entry = glade_xml_get_widget (opt->uixml, "leap_username_entry");
	g_signal_connect (G_OBJECT (entry), "changed", validate_cb, user_data);

	entry = glade_xml_get_widget (opt->uixml, "leap_password_entry");
	g_signal_connect (G_OBJECT (entry), "changed", validate_cb, user_data);

	/* FIXME: ugh, this breaks everything and I have no idea why */
/* 	widget = glade_xml_get_widget (opt->uixml, "leap_show_password"); */
/* 	g_signal_connect (widget, "clicked", GTK_SIGNAL_FUNC (show_password_cb), entry); */

	/* set-up key_mgmt combo box */

	key_mgmt = glade_xml_get_widget (opt->uixml, "leap_key_mgmt_combobox");

	/* create tree model containing combo box items */
	model = wso_leap_create_key_mgmt_model ();
	gtk_combo_box_set_model(GTK_COMBO_BOX(key_mgmt), model);

	/* set default choice to be IEEE 802.1X */
	gtk_combo_box_set_active(GTK_COMBO_BOX(key_mgmt), 0);

	return widget;
}

static gboolean
validate_input_func (WirelessSecurityOption *opt,
                     const char *ssid,
                     IEEE_802_11_Cipher **out_cipher)
{
	return TRUE;
}


static gboolean
populate_from_dbus_func (WirelessSecurityOption *opt, DBusMessageIter *iter)
{
	char *username = NULL;
	char *password = NULL;
	char *key_mgmt = NULL;
	GtkWidget *w;

	if (!nmu_security_deserialize_leap (iter, &username, &password, &key_mgmt))
		return FALSE;

	if (username) {
		w = glade_xml_get_widget (opt->uixml, "leap_username_entry");
		gtk_entry_set_text (GTK_ENTRY (w), username);
	}

	if (password) {
		w = glade_xml_get_widget (opt->uixml, "leap_password_entry");
		gtk_entry_set_text (GTK_ENTRY (w), password);
	}

	if (key_mgmt) {
		GtkTreeModel *model;
		GtkTreeIter iter;
		gboolean valid;

		w = glade_xml_get_widget (opt->uixml, "leap_key_mgmt_combobox");
		model = gtk_combo_box_get_model (GTK_COMBO_BOX (w));

		valid = gtk_tree_model_get_iter_first (model, &iter);
		while (valid) {
			gchar *row = NULL;

			gtk_tree_model_get (model, &iter, 1, &row, -1);
			if (row && strcmp (row, key_mgmt) == 0) {
				gtk_combo_box_set_active_iter (GTK_COMBO_BOX (w), &iter);
				valid = FALSE;
			} else 
				valid = gtk_tree_model_iter_next (model, &iter);

			g_free (row);
		}
	}

	return TRUE;
}


WirelessSecurityOption *
wso_leap_new (const char *glade_file,
              int capabilities)
{
	WirelessSecurityOption * opt = NULL;

	g_return_val_if_fail (glade_file != NULL, NULL);

	opt = g_malloc0 (sizeof (WirelessSecurityOption));
	opt->name = g_strdup("LEAP");
	opt->widget_name = "leap_notebook";
	opt->data_free_func = data_free_func;
	opt->validate_input_func = validate_input_func;
	opt->widget_create_func = widget_create_func;
	opt->append_dbus_params_func = append_dbus_params_func;
	opt->populate_from_dbus_func = populate_from_dbus_func;

	if (!(opt->uixml = glade_xml_new (glade_file, opt->widget_name, NULL)))
	{
		wso_free (opt);
		return NULL;
	}

	/* Option-specific data */
	opt->data = g_malloc0 (sizeof (OptData));

	return opt;
}
