/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include "applet.h"
#include "applet-dbus-info.h"
#include "passphrase-dialog.h"
#include "nm-utils.h"
#include "NetworkManager.h"
#include "wireless-security-manager.h"

typedef struct {
	NMApplet *applet;
	DBusMessage *message;
	GladeXML *xml;
	NetworkDevice *device;
	WirelessNetwork *net;
	WirelessSecurityManager *wsm;
} PassphraseDialogInfo;

static void
passphrase_dialog_destroy (gpointer data, GObject *destroyed_object)
{
	PassphraseDialogInfo *info = (PassphraseDialogInfo *) data;

	info->applet->passphrase_dialog = NULL;

	network_device_unref (info->device);
	wireless_network_unref (info->net);
	dbus_message_unref (info->message);
	wsm_free (info->wsm);
	g_object_unref (info->xml);

	g_free (info);
}

static void update_button_cb (GtkWidget *unused, PassphraseDialogInfo *info)
{
	gboolean		enable = FALSE;
	const char *	ssid = NULL;
	GtkWidget *	button;
	GtkComboBox *	security_combo;

	if ((ssid = wireless_network_get_essid (info->net)) != NULL)
	{
		/* Validate the wireless security choices */
		security_combo = GTK_COMBO_BOX (glade_xml_get_widget (info->xml, "security_combo"));
		enable = wsm_validate_active (info->wsm, security_combo, ssid);
	}

	button = glade_xml_get_widget (info->xml, "login_button");
	gtk_widget_set_sensitive (button, enable);
}

/*
 * nmi_passphrase_dialog_security_combo_changed
 *
 * Replace the current wireless security widgets with new ones
 * according to what the user chose.
 *
 */
static void nmi_passphrase_dialog_security_combo_changed (GtkWidget *security_combo, gpointer user_data)
{
	PassphraseDialogInfo *info = (PassphraseDialogInfo *) user_data;
	GtkWidget *	wso_widget;
	GtkWidget *	vbox;
	GList *		children;
	GList *		elt;

	vbox = GTK_WIDGET (glade_xml_get_widget (info->xml, "wireless_security_vbox"));

	/* Remove any previous wireless security widgets */
	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (elt = children; elt; elt = g_list_next (elt))
	{
		GtkWidget * child = GTK_WIDGET (elt->data);

		if (wso_is_wso_widget (child))
			gtk_container_remove (GTK_CONTAINER (vbox), child);
	}

	g_list_free (children);

	/* Determine and add the correct wireless security widget to the dialog */
	wso_widget = wsm_get_widget_for_active (info->wsm, GTK_COMBO_BOX (security_combo),
									GTK_SIGNAL_FUNC (update_button_cb), info);
	if (wso_widget)
		gtk_container_add (GTK_CONTAINER (vbox), wso_widget);

	update_button_cb (NULL, info);
}


/*
 * nmi_passphrase_dialog_response_received
 *
 * response handler; grab the passphrase and return it
 * to NetworkManager if it was given to us, else return
 * a cancellation message to NetworkManager.
 * Either way, get rid of the dialog.
 */
static void
nmi_passphrase_dialog_response_received (GtkWidget *dialog,
                                         gint response,
                                         gpointer user_data)
{
	PassphraseDialogInfo *info = (PassphraseDialogInfo *) user_data;
	GtkComboBox *		security_combo;
	WirelessSecurityOption *	opt;
	NMGConfWSO *		gconf_wso;

	if (response != GTK_RESPONSE_OK)
	{
		DBusMessage *	reply;

		reply = dbus_message_new_error (info->message, NMI_DBUS_USER_KEY_CANCELED_ERROR, "Request was cancelled.");
		dbus_connection_send (info->applet->connection, reply, NULL);
		goto out;
	}

	security_combo = GTK_COMBO_BOX (glade_xml_get_widget (info->xml, "security_combo"));
	opt = wsm_get_option_for_active (info->wsm, security_combo);

	gconf_wso = nm_gconf_wso_new_from_wso (opt, wireless_network_get_essid (info->net));

	/* Return new security information to NM */
	nmi_dbus_return_user_key (info->applet->connection, info->message, gconf_wso);
	g_object_unref (gconf_wso);

out:
	gtk_widget_destroy (dialog);
}


/*
 * nmi_passphrase_dialog_new
 *
 * Create a new passphrase dialog instance.
 */
GtkWidget *
nmi_passphrase_dialog_new (NMApplet *applet,
                           NetworkDevice *dev,
                           WirelessNetwork *net,
                           DBusMessage *message)
{
	GtkWidget *				dialog;
	GtkButton *				ok_button;
	GtkWidget *				label;
	GladeXML *				xml;
	WirelessSecurityManager *	wsm;
	GtkComboBox *				security_combo;
	const char *				orig_label_text;
	char *					new_label_text;
	guint32					caps;
	PassphraseDialogInfo *info;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (dev != NULL, NULL);
	g_return_val_if_fail (net != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	if (applet->passphrase_dialog)
		gtk_widget_destroy (applet->passphrase_dialog);

	wsm = wsm_new (applet->glade_file);

	caps = network_device_get_type_capabilities (dev);
	caps &= wireless_network_get_capabilities (net);
	if (!wsm_set_capabilities (wsm, caps))
	{
		GtkWidget *error_dialog;

		error_dialog = gtk_message_dialog_new_with_markup (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
							"<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
							_("Error connecting to wireless network"),
							_("The requested wireless network requires security capabilities unsupported by your hardware."));
		gtk_window_present (GTK_WINDOW (error_dialog));
		g_signal_connect_swapped (error_dialog, "response", G_CALLBACK (gtk_widget_destroy), error_dialog);

		wsm_free (wsm);

		return NULL;
	}


	if (!(xml = glade_xml_new (applet->glade_file, "passphrase_dialog", NULL)))
	{
		nma_schedule_warning_dialog (applet, _("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		wsm_free (wsm);
		return NULL;
	}

	dialog = glade_xml_get_widget (xml, "passphrase_dialog");
	gtk_widget_hide (dialog);

	info = g_new (PassphraseDialogInfo, 1);
	info->applet = applet;
	info->message = dbus_message_ref (message);
	info->xml = xml;
	info->device = network_device_ref (dev);
	info->net = wireless_network_ref (net);
	info->wsm = wsm;

	g_object_weak_ref (G_OBJECT (dialog), passphrase_dialog_destroy, info);

	ok_button = GTK_BUTTON (glade_xml_get_widget (xml, "login_button"));
	gtk_widget_grab_default (GTK_WIDGET (ok_button));

	/* Insert the Network name into the dialog text */
	label = glade_xml_get_widget (xml, "label1");
	orig_label_text = gtk_label_get_label (GTK_LABEL (label));
	new_label_text = g_strdup_printf (orig_label_text, wireless_network_get_essid (net));
	gtk_label_set_label (GTK_LABEL (label), new_label_text);
	g_free (new_label_text);

	gtk_widget_set_sensitive (GTK_WIDGET (ok_button), FALSE);

	security_combo = GTK_COMBO_BOX (glade_xml_get_widget (xml, "security_combo"));
	wsm_update_combo (wsm, security_combo);

	g_signal_connect (security_combo, "changed", GTK_SIGNAL_FUNC (nmi_passphrase_dialog_security_combo_changed), info);
	nmi_passphrase_dialog_security_combo_changed (GTK_WIDGET (security_combo), info);

	g_signal_connect (dialog, "response", GTK_SIGNAL_FUNC (nmi_passphrase_dialog_response_received), info);

	/* Bash focus-stealing prevention in the face */
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (GTK_WIDGET (dialog));
	gdk_x11_window_set_user_time (dialog->window, gdk_x11_get_server_time (dialog->window));
	gtk_window_present (GTK_WINDOW (dialog));

	applet->passphrase_dialog = dialog;

	return dialog;
}
