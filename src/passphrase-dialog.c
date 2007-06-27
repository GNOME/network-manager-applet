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
#include <iwlib.h>
#include <wireless.h>

#include "applet.h"
#include "applet-dbus-info.h"
#include "passphrase-dialog.h"
#include "nm-utils.h"
#include "NetworkManager.h"
#include "wireless-security-manager.h"

static GladeXML *get_dialog_xml (GtkWidget *dialog)
{
	g_return_val_if_fail (dialog != NULL, NULL);

	return (GladeXML *) g_object_get_data (G_OBJECT (dialog), "glade-xml");
}

static void update_button_cb (GtkWidget *unused, GtkDialog *dialog)
{
	gboolean enable = FALSE;
	GByteArray * ssid;
	GtkWidget *	button;
	GladeXML *	xml;
	WirelessSecurityManager * wsm;
	GtkComboBox *	security_combo;
	NMAccessPoint *ap;

	g_return_if_fail (dialog != NULL);
	xml = get_dialog_xml (GTK_WIDGET (dialog));
	g_return_if_fail (xml != NULL);
	wsm = (WirelessSecurityManager *) g_object_get_data (G_OBJECT (dialog), "wireless-security-manager");
	g_return_if_fail (wsm != NULL);

	if ((ap = g_object_get_data (G_OBJECT (dialog), "access-point")) &&
	    (ssid = nm_access_point_get_ssid (ap)))
	{
		char buf[IW_ESSID_MAX_SIZE + 1];

		memset (buf, 0, sizeof (buf));
		memcpy (buf, ssid->data, MIN (ssid->len, sizeof (buf) - 1));

		/* Validate the wireless security choices */
		security_combo = GTK_COMBO_BOX (glade_xml_get_widget (xml, "security_combo"));
		enable = wsm_validate_active (wsm, security_combo, buf);
		g_byte_array_free (ssid, TRUE);
	}

	button = glade_xml_get_widget (xml, "login_button");
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
	GtkDialog *	dialog = (GtkDialog *) user_data;
	WirelessSecurityManager * wsm;
	GtkWidget *	wso_widget;
	GladeXML *	xml;
	GtkWidget *	vbox;
	GList *		elt;

	g_return_if_fail (dialog != NULL);
	xml = get_dialog_xml (GTK_WIDGET (dialog));
	g_return_if_fail (xml != NULL);

	wsm = g_object_get_data (G_OBJECT (dialog), "wireless-security-manager");
	g_return_if_fail (wsm != NULL);

	vbox = GTK_WIDGET (glade_xml_get_widget (xml, "wireless_security_vbox"));

	/* Remove any previous wireless security widgets */
	for (elt = gtk_container_get_children (GTK_CONTAINER (vbox)); elt; elt = g_list_next (elt))
	{
		GtkWidget * child = GTK_WIDGET (elt->data);

		if (wso_is_wso_widget (child))
			gtk_container_remove (GTK_CONTAINER (vbox), child);
	}

	/* Determine and add the correct wireless security widget to the dialog */
	wso_widget = wsm_get_widget_for_active (wsm, GTK_COMBO_BOX (security_combo), GTK_SIGNAL_FUNC (update_button_cb), dialog);
	if (wso_widget)
		gtk_container_add (GTK_CONTAINER (vbox), wso_widget);

	update_button_cb (NULL, dialog);
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
	NMApplet *	applet;
	GladeXML *		xml;
	GtkComboBox *		security_combo;
	DBusMessage *		message;
	WirelessSecurityManager *wsm;
	WirelessSecurityOption *	opt;
	NMAccessPoint *ap;
	NMGConfWSO *		gconf_wso;
	GByteArray * ssid;
	char buf[IW_ESSID_MAX_SIZE + 1];

	message = (DBusMessage *) g_object_get_data (G_OBJECT (dialog), "dbus-message");
	g_assert (message);

	applet = (NMApplet *) g_object_get_data (G_OBJECT (dialog), "applet");
	g_assert (applet);

	if (response != GTK_RESPONSE_OK)
	{
		DBusMessage *	reply;

		reply = dbus_message_new_error (message, NMI_DBUS_USER_KEY_CANCELED_ERROR, "Request was cancelled.");
		dbus_connection_send (applet->connection, reply, NULL);
		goto out;
	}

	xml = get_dialog_xml (dialog);
	g_assert (xml);

	wsm = g_object_get_data (G_OBJECT (dialog), "wireless-security-manager");
	g_assert (wsm);

	security_combo = GTK_COMBO_BOX (glade_xml_get_widget (xml, "security_combo"));
	opt = wsm_get_option_for_active (wsm, security_combo);

	ap = NM_ACCESS_POINT (g_object_get_data (G_OBJECT (dialog), "access-point"));
	g_assert (ap);

	ssid = nm_access_point_get_ssid (ap);
	memset (buf, 0, sizeof (buf));
	if (ssid)
		memcpy (buf, ssid->data, MIN (ssid->len, sizeof (buf) - 1));
	gconf_wso = nm_gconf_wso_new_from_wso (opt, buf);
	if (ssid)
		g_byte_array_free (ssid, TRUE);

	/* Return new security information to NM */
	nmi_dbus_return_user_key (applet->connection, message, gconf_wso);
	g_object_unref (G_OBJECT (gconf_wso));

out:
	nmi_passphrase_dialog_destroy (applet);
}


/*
 * nmi_passphrase_dialog_new
 *
 * Create a new passphrase dialog instance and tie it
 * to the given UID.
 */
GtkWidget *
nmi_passphrase_dialog_new (NMApplet *applet,
                           guint32 uid,
                           NMDevice80211Wireless *dev,
                           NMAccessPoint *ap,
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
	GByteArray *			ssid;
	guint32					caps;

	g_return_val_if_fail (NM_IS_APPLET (applet), NULL);
	g_return_val_if_fail (NM_IS_DEVICE_802_11_WIRELESS (dev), NULL);
	g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), NULL);
	g_return_val_if_fail (message != NULL, NULL);

	wsm = wsm_new (applet->glade_file);

	caps = nm_device_802_11_wireless_get_capabilities (dev);
	caps &= nm_access_point_get_capabilities (ap);
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

	g_object_set_data_full (G_OBJECT (dialog), "wireless-security-manager", (gpointer) wsm, (GDestroyNotify) wsm_free);
	g_object_set_data_full (G_OBJECT (dialog), "glade-xml", xml, (GDestroyNotify) g_object_unref);
	g_object_set_data (G_OBJECT (dialog), "applet", applet);
	g_object_set_data (G_OBJECT (dialog), "uid", GINT_TO_POINTER (uid));

	ok_button = GTK_BUTTON (glade_xml_get_widget (xml, "login_button"));
	gtk_widget_grab_default (GTK_WIDGET (ok_button));

	/* Insert the Network name into the dialog text */
	label = glade_xml_get_widget (xml, "label1");
	orig_label_text = gtk_label_get_label (GTK_LABEL (label));

	ssid = nm_access_point_get_ssid (ap);
	new_label_text = g_strdup_printf (orig_label_text,
	                                  nma_escape_ssid (ssid->data, ssid->len));
	g_byte_array_free (ssid, TRUE);
	gtk_label_set_label (GTK_LABEL (label), new_label_text);
	g_free (new_label_text);

	g_object_set_data_full (G_OBJECT (dialog), "access-point",
							g_object_ref (ap),
							(GDestroyNotify) g_object_unref);
	dbus_message_ref (message);
	g_object_set_data_full (G_OBJECT (dialog), "dbus-message", message,
							(GDestroyNotify) dbus_message_unref);

	gtk_widget_set_sensitive (GTK_WIDGET (ok_button), FALSE);

	security_combo = GTK_COMBO_BOX (glade_xml_get_widget (xml, "security_combo"));
	wsm_update_combo (wsm, security_combo);

	g_signal_connect (G_OBJECT (security_combo), "changed", GTK_SIGNAL_FUNC (nmi_passphrase_dialog_security_combo_changed), dialog);
	nmi_passphrase_dialog_security_combo_changed (GTK_WIDGET (security_combo), dialog);

	g_signal_connect (G_OBJECT (dialog), "response", GTK_SIGNAL_FUNC (nmi_passphrase_dialog_response_received), dialog);

	/* Bash focus-stealing prevention in the face */
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gdk_x11_window_set_user_time (dialog->window, gtk_get_current_event_time ());
	gtk_window_present (GTK_WINDOW (dialog));

	return dialog;
}


/*
 * nmi_passphrase_dialog_destroy
 *
 * Dispose of the passphrase dialog and its data
 *
 */
void nmi_passphrase_dialog_destroy (NMApplet *applet)
{
	/* FIXME: Get rid of this funciton, gtk_widget_destroy (dialog) is enough */

	g_return_if_fail (applet != NULL);

	if (!applet->passphrase_dialog)
		return;

	gtk_widget_hide (applet->passphrase_dialog);
	gtk_widget_destroy (applet->passphrase_dialog);
	applet->passphrase_dialog = NULL;
}
