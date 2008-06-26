/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Dan Williams <dcbw@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <glib.h>
#include <glib/gi18n.h>

#include <glade/glade.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkradiobutton.h>
#include <gtk/gtkbox.h>

#include <NetworkManager.h>
#include <nm-client.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-gsm-device.h>
#include <nm-cdma-device.h>

#include "mobile-wizard.h"
#include "utils.h"

#define DEVICE_TAG "device"
#define TYPE_TAG "setting-type"

static void
ok_clicked (GtkButton *button, gpointer user_data)
{
	gtk_dialog_response (GTK_DIALOG (user_data), GTK_RESPONSE_OK);
}

static void
cancel_clicked (GtkButton *button, gpointer user_data)
{
	gtk_dialog_response (GTK_DIALOG (user_data), GTK_RESPONSE_CANCEL);
}

typedef struct {
	NMClient *client;
	GladeXML *xml;
	GtkWidget *dialog;
	GtkWidget *radio;
} MobileWizardInfo;

static void
device_added_cb (NMClient *client, NMDevice *device, MobileWizardInfo *info)
{
	GtkWidget *vbox;
	GtkWidget *radio = NULL;
	char *desc = (char *) utils_get_device_description (device);

	vbox = glade_xml_get_widget (info->xml, "options_vbox");
	g_assert (vbox);

	if (G_OBJECT_TYPE (device) == NM_TYPE_GSM_DEVICE) {
		if (!desc)
			desc = _("Installed GSM device");

		radio = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (info->radio), desc);

		g_object_set_data (G_OBJECT (radio), TYPE_TAG,
		                   GUINT_TO_POINTER (NM_TYPE_SETTING_GSM));
		g_object_set_data_full (G_OBJECT (radio), DEVICE_TAG,
		                        g_object_ref (device),
		                        (GDestroyNotify) g_object_unref);
	} else if (G_OBJECT_TYPE (device) == NM_TYPE_CDMA_DEVICE) {
		if (!desc)
			desc = _("Installed CDMA device");

		radio = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (info->radio), desc);

		g_object_set_data (G_OBJECT (radio), TYPE_TAG,
		                   GUINT_TO_POINTER (NM_TYPE_SETTING_CDMA));
		g_object_set_data_full (G_OBJECT (radio), DEVICE_TAG,
		                        g_object_ref (device),
		                        (GDestroyNotify) g_object_unref);
	}

	if (radio) {
		gtk_box_pack_end (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
		gtk_widget_show_all (radio);
	}
}

static void
device_removed_cb (NMClient *client, NMDevice *device, MobileWizardInfo *info)
{
	GtkWidget *vbox, *prev = NULL;
	GList *children, *iter;

	vbox = glade_xml_get_widget (info->xml, "options_vbox");
	g_assert (vbox);

	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (iter = children; iter; iter = g_list_next (iter)) {
		if (g_object_get_data (G_OBJECT (iter->data), DEVICE_TAG) == device) {
			gtk_widget_destroy (GTK_WIDGET (iter->data));
			break;
		}
		prev = iter->data;
	}

	/* Select the item just above the removed item, or if there aren't
	 * any device-based items left in the list, the default one.
	 */
	if (!prev)
		prev = info->radio;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (prev), TRUE);
}

static void
add_initial_devices (MobileWizardInfo *info)
{
	GtkWidget *vbox;
	const GPtrArray *devices;
	int i;

	vbox = glade_xml_get_widget (info->xml, "options_vbox");
	g_assert (vbox);

	devices = nm_client_get_devices (info->client);
	for (i = 0; devices && (i < devices->len); i++)
		device_added_cb (info->client, g_ptr_array_index (devices, i), info);

	gtk_widget_show_all (vbox);
}

static GtkWidget *
add_generic_options (GladeXML *xml)
{
	GtkWidget *vbox;
	GtkWidget *radio;
	const char *gsm_desc = _("Create a GSM connection");
	const char *cdma_desc = _("Create a CDMA connection");

	vbox = glade_xml_get_widget (xml, "options_vbox");
	g_assert (vbox);

	radio = gtk_radio_button_new_with_label (NULL, cdma_desc);
	g_object_set_data (G_OBJECT (radio), TYPE_TAG, GUINT_TO_POINTER (NM_TYPE_SETTING_CDMA));
	gtk_box_pack_end (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
	gtk_widget_show (radio);

	radio = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (radio), gsm_desc);
	g_object_set_data (G_OBJECT (radio), TYPE_TAG, GUINT_TO_POINTER (NM_TYPE_SETTING_GSM));
	gtk_box_pack_end (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
	gtk_widget_show (radio);

	return radio;
}

static void
remove_all_devices (GladeXML *xml)
{
	GtkWidget *vbox;
	GList *children, *iter;

	vbox = glade_xml_get_widget (xml, "options_vbox");
	g_assert (vbox);

	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (iter = children; iter; iter = g_list_next (iter)) {
		if (g_object_get_data (G_OBJECT (iter->data), DEVICE_TAG))
			gtk_widget_destroy (GTK_WIDGET (iter->data));
	}
}

static void
manager_running_cb (NMClient *client, GParamSpec *pspec, MobileWizardInfo *info)
{
	if (nm_client_get_manager_running (client))
		add_initial_devices (info);
	else
		remove_all_devices (info->xml);
}

GType
mobile_wizard_ask_connection_type (void)
{
	MobileWizardInfo info;
	GType choice = 0;
	gint response;
	GtkWidget *widget;
	GtkWidget *vbox;

	info.xml = glade_xml_new (GLADEDIR "/ce-mobile-wizard.glade", "new_connection_dialog", NULL);
	if (!info.xml) {
		g_warning ("Could not load Glade file for new mobile broadband connection dialog");
		return 0;
	}

	info.dialog = glade_xml_get_widget (info.xml, "new_connection_dialog");
	if (!info.dialog) {
		g_warning ("Could not load Glade file for new mobile broadband connection dialog");
		return 0;
	}

	widget = glade_xml_get_widget (info.xml, "ok_button");
	g_signal_connect (widget, "clicked", G_CALLBACK (ok_clicked), info.dialog);

	widget = glade_xml_get_widget (info.xml, "cancel_button");
	g_signal_connect (widget, "clicked", G_CALLBACK (cancel_clicked), info.dialog);

	info.client = nm_client_new ();
	g_signal_connect (info.client, "device-added", G_CALLBACK (device_added_cb), &info);
	g_signal_connect (info.client, "device-removed", G_CALLBACK (device_removed_cb), &info);
	g_signal_connect (info.client, "notify::manager-running", G_CALLBACK (manager_running_cb), &info);

	vbox = glade_xml_get_widget (info.xml, "options_vbox");

	info.radio = add_generic_options (info.xml);

	/* If NM is running, add the mobile broadband devices it knows about */
	if (nm_client_get_manager_running (info.client))
		add_initial_devices (&info);

	/* Ask the user which type of connection they want to create */
	response = gtk_dialog_run (GTK_DIALOG (info.dialog));
	gtk_widget_hide (info.dialog);
	if (response == GTK_RESPONSE_OK) {
		GList *children, *iter;

		/* Get the radio button the user selected */
		children = gtk_container_get_children (GTK_CONTAINER (vbox));
		for (iter = children; iter; iter = g_list_next (iter)) {
			if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (iter->data))) {
				choice = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (iter->data), TYPE_TAG));
				break;
			}
		}
	}

	g_object_unref (info.client);
	g_object_unref (info.xml);	
	return choice;
}

