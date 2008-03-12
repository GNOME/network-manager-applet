/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <nm-device-802-3-ethernet.h>
#include <nm-device-802-11-wireless.h>
#include <nm-gsm-device.h>
#include <nm-cdma-device.h>

#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>

#include <gtk/gtk.h>
#include <gtk/gtkwidget.h>
#include <glade/glade.h>
#include <glib/gi18n.h>

#include "applet-dialogs.h"


static void
info_dialog_show_error (const char *err)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new_with_markup (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
			"<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s", _("Error displaying connection information:"), err);
	gtk_window_present (GTK_WINDOW (dialog));
	g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
}

static const gchar *
ip4_address_as_string (guint32 ip)
{
	struct in_addr tmp_addr;
	gchar *ip_string;

	tmp_addr.s_addr = ip;
	ip_string = inet_ntoa (tmp_addr);

	return ip_string;
}

static void
set_eap_info_label (GtkWidget *label, NMSettingWirelessSecurity *sec)
{
	GString *str = NULL;
	char *phase2_str = NULL;

	if (!strcmp (sec->key_mgmt, "ieee8021x"))
		str = g_string_new (_("Dynamic WEP"));
	else if (!strcmp (sec->key_mgmt, "wpa-eap"))
		str = g_string_new (_("WPA/WPA2"));
	else {
		str = g_string_new (_("Unknown"));
		goto out;
	}

	if (sec->eap && sec->eap->data) {
		char *eap_str = g_ascii_strup (sec->eap->data, -1);
		g_string_append (str, ", EAP-");
		g_string_append (str, eap_str);
		g_free (eap_str);
	}

	if (sec->phase2_auth)
		phase2_str = g_ascii_strup (sec->phase2_auth, -1);
	else if (sec->phase2_autheap)
		phase2_str = g_ascii_strup (sec->phase2_autheap, -1);

	if (phase2_str) {
		g_string_append (str, ", ");
		g_string_append (str, phase2_str);
		g_free (phase2_str);
	}
	
out:
	gtk_label_set_text (GTK_LABEL (label), str->str);

	g_free (phase2_str);
	g_string_free (str, TRUE);
}

static GtkWidget *
info_dialog_update (GladeXML *xml, NMDevice *device, NMConnection *connection)
{
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *label2;
	NMIP4Config *cfg;
	guint32 speed;
	char *str;
	char *iface_and_type;
	GArray *dns;

	g_return_val_if_fail (xml != NULL, NULL);
	g_return_val_if_fail (device != NULL, NULL);

	dialog = glade_xml_get_widget (xml, "info_dialog");
	if (!dialog) {
		info_dialog_show_error (_("Could not find some required resources (the glade file)!"));
		return NULL;
	}

	cfg = nm_device_get_ip4_config (device);

	speed = 0;
	if (NM_IS_DEVICE_802_3_ETHERNET (device)) {
		/* Wireless speed in Mb/s */
		speed = nm_device_802_3_ethernet_get_speed (NM_DEVICE_802_3_ETHERNET (device));
	} else if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		/* Wireless speed in b/s */
		speed = nm_device_802_11_wireless_get_bitrate (NM_DEVICE_802_11_WIRELESS (device));
		speed /= 1000000;
	}

	str = nm_device_get_iface (device);
	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		iface_and_type = g_strdup_printf (_("Ethernet (%s)"), str);
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		iface_and_type = g_strdup_printf (_("802.11 WiFi (%s)"), str);
	else if (NM_IS_GSM_DEVICE (device))
		iface_and_type = g_strdup_printf (_("GSM (%s)"), str);
	else if (NM_IS_CDMA_DEVICE (device))
		iface_and_type = g_strdup_printf (_("CDMA (%s)"), str);
	else
		iface_and_type = g_strdup (str);

	g_free (str);

	label = glade_xml_get_widget (xml, "label-interface");
	gtk_label_set_text (GTK_LABEL (label), iface_and_type);
	g_free (iface_and_type);

	str = NULL;
	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		str = g_strdup (nm_device_802_3_ethernet_get_hw_address (NM_DEVICE_802_3_ETHERNET (device)));
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		str = g_strdup (nm_device_802_11_wireless_get_hw_address (NM_DEVICE_802_11_WIRELESS (device)));

	label = glade_xml_get_widget (xml, "label-hardware-address");
	gtk_label_set_text (GTK_LABEL (label), str ? str : "");
	g_free (str);

	label = glade_xml_get_widget (xml, "label-security");
	label2 = glade_xml_get_widget (xml, "label-security-label");
	// FIXME: handle 802.1x wired auth too
	if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		NMSettingWireless *s_wireless;
		NMSettingWirelessSecurity *s_wireless_sec;

		s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
		s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY));
		if (   s_wireless
		    && s_wireless->security
		    && !strcmp (s_wireless->security, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME)
		    && s_wireless_sec) {

			if (!strcmp (s_wireless_sec->key_mgmt, "none")) {
				gtk_label_set_text (GTK_LABEL (label), _("WEP"));
			} else if (!strcmp (s_wireless_sec->key_mgmt, "wpa-none")) {
				gtk_label_set_text (GTK_LABEL (label), _("WPA/WPA2"));
			} else if (!strcmp (s_wireless_sec->key_mgmt, "wpa-psk")) {
				gtk_label_set_text (GTK_LABEL (label), _("WPA/WPA2"));
			} else {
				set_eap_info_label (label, s_wireless_sec);
			}
		} else {
			gtk_label_set_text (GTK_LABEL (label), _("None"));
		}

		gtk_widget_show (label);
		gtk_widget_show (label2);
	} else {
		gtk_widget_hide (label);
		gtk_widget_hide (label2);
	}

	label = glade_xml_get_widget (xml, "label-speed");
	if (speed) {
		str = g_strdup_printf (_("%u Mb/s"), speed);
		gtk_label_set_text (GTK_LABEL (label), str);
		g_free (str);
	} else
		gtk_label_set_text (GTK_LABEL (label), _("Unknown"));

	str = nm_device_get_driver (device);
	label = glade_xml_get_widget (xml, "label-driver");
	gtk_label_set_text (GTK_LABEL (label), str);
	g_free (str);

	label = glade_xml_get_widget (xml, "label-ip-address");
	gtk_label_set_text (GTK_LABEL (label),
					ip4_address_as_string (nm_ip4_config_get_address (cfg)));

	label = glade_xml_get_widget (xml, "label-broadcast-address");
	gtk_label_set_text (GTK_LABEL (label),
					ip4_address_as_string (nm_ip4_config_get_broadcast (cfg)));

	label = glade_xml_get_widget (xml, "label-subnet-mask");
	gtk_label_set_text (GTK_LABEL (label),
					ip4_address_as_string (nm_ip4_config_get_netmask (cfg)));

	label = glade_xml_get_widget (xml, "label-default-route");
	gtk_label_set_text (GTK_LABEL (label),
					ip4_address_as_string (nm_ip4_config_get_gateway (cfg)));

	dns = nm_ip4_config_get_nameservers (cfg);
	if (dns) {
		label = glade_xml_get_widget (xml, "label-primary-dns");
		if (dns->len > 0) {
			gtk_label_set_text (GTK_LABEL (label),
							ip4_address_as_string (g_array_index (dns, guint32, 0)));
		} else {
			gtk_label_set_text (GTK_LABEL (label), "");
		}

		label = glade_xml_get_widget (xml, "label-secondary-dns");
		label2 = glade_xml_get_widget (xml, "label-secondary-dns-label");
		if (dns->len > 1) {
			gtk_label_set_text (GTK_LABEL (label),
							ip4_address_as_string (g_array_index (dns, guint32, 1)));
			gtk_widget_show (label);
			gtk_widget_show (label2);
		} else {
			gtk_widget_hide (label);
			gtk_widget_hide (label2);
		}
		g_array_free (dns, TRUE);
	}

	return dialog;
}

void
applet_info_dialog_show (NMApplet *applet)
{
	GtkWidget *dialog;
	NMDevice *device;
	NMConnection *connection = NULL;

	device = applet_get_first_active_device (applet);
	if (device)
		connection = applet_find_active_connection_for_device (device, applet);

	if (!connection || !device) {
		info_dialog_show_error (_("No active connections!"));
		return;
	}

	dialog = info_dialog_update (applet->info_dialog_xml, device, connection);
	if (!dialog)
		return;

	g_signal_connect (dialog, "delete-event", G_CALLBACK (gtk_widget_hide_on_delete), dialog);
	g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_hide), dialog);
	gtk_widget_realize (dialog);
	gtk_window_present_with_time (GTK_WINDOW (dialog), gdk_x11_get_server_time (dialog->window));
}

static void 
about_dialog_handle_url_cb (GtkAboutDialog *about, const gchar *url, gpointer data)
{
	GError *error = NULL;
	gboolean ret;
	char *cmdline;
	GdkScreen *gscreen;
	GtkWidget *error_dialog;

	gscreen = gtk_window_get_screen (GTK_WINDOW (about));

	cmdline = g_strconcat ("gnome-open ", url, NULL);
	ret = gdk_spawn_command_line_on_screen (gscreen, cmdline, &error);
	g_free (cmdline);

	if (ret == TRUE)
		return;

	g_error_free (error);
	error = NULL;

	cmdline = g_strconcat ("xdg-open ", url, NULL);
	ret = gdk_spawn_command_line_on_screen (gscreen, cmdline, &error);
	g_free (cmdline);
	
	if (ret == FALSE) {
		error_dialog = gtk_message_dialog_new ( NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Failed to show url %s", error->message); 
		gtk_dialog_run (GTK_DIALOG (error_dialog));
		g_error_free (error);
	}

}

/* Make email in about dialog clickable */
static void 
about_dialog_handle_email_cb (GtkAboutDialog *about, const char *email_address, gpointer data)
{
	GError *error = NULL;
	gboolean ret;
	char *cmdline;
	GdkScreen *gscreen;
	GtkWidget *error_dialog;

	gscreen = gtk_window_get_screen (GTK_WINDOW (about));

	cmdline = g_strconcat ("gnome-open mailto:", email_address, NULL);
	ret = gdk_spawn_command_line_on_screen (gscreen, cmdline, &error);
	g_free (cmdline);

	if (ret == TRUE)
		return;

	g_error_free (error);
	error = NULL;

	cmdline = g_strconcat ("xdg-open mailto:", email_address, NULL);
	ret = gdk_spawn_command_line_on_screen (gscreen, cmdline, &error);
	g_free (cmdline);
	
	if (ret == FALSE) {
		error_dialog = gtk_message_dialog_new ( NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Failed to show url %s", error->message); 
		gtk_dialog_run (GTK_DIALOG (error_dialog));
		g_error_free (error);
	}
}

void
applet_about_dialog_show (NMApplet *applet)
{
	static const gchar *authors[] = {
		"The Red Hat Desktop Team, including:\n",
		"Christopher Aillon <caillon@redhat.com>",
		"Jonathan Blandford <jrb@redhat.com>",
		"John Palmieri <johnp@redhat.com>",
		"Ray Strode <rstrode@redhat.com>",
		"Colin Walters <walters@redhat.com>",
		"Dan Williams <dcbw@redhat.com>",
		"David Zeuthen <davidz@redhat.com>",
		"\nAnd others, including:\n",
		"Bill Moss <bmoss@clemson.edu>",
		"Tom Parker",
		"j@bootlab.org",
		"Peter Jones <pjones@redhat.com>",
		"Robert Love <rml@novell.com>",
		"Tim Niemueller <tim@niemueller.de>",
		NULL
	};

	static const gchar *artists[] = {
		"Diana Fong <dfong@redhat.com>",
		NULL
	};


	/* FIXME: unnecessary with libgnomeui >= 2.16.0 */
	static gboolean been_here = FALSE;
	if (!been_here) {
		been_here = TRUE;
		gtk_about_dialog_set_url_hook (about_dialog_handle_url_cb, NULL, NULL);
		gtk_about_dialog_set_email_hook (about_dialog_handle_email_cb, NULL, NULL);
	}

	gtk_show_about_dialog (NULL,
	                       "version", VERSION,
	                       "copyright", _("Copyright \xc2\xa9 2004-2008 Red Hat, Inc.\n"
					                  "Copyright \xc2\xa9 2005-2008 Novell, Inc."),
	                       "comments", _("Notification area applet for managing your network devices and connections."),
	                       "website", "http://www.gnome.org/projects/NetworkManager/",
	                       "website-label", _("NetworkManager Website"),
	                       "authors", authors,
	                       "artists", artists,
	                       "translator-credits", _("translator-credits"),
	                       "logo-icon-name", GTK_STOCK_NETWORK,
	                       NULL);
}


gboolean
applet_warning_dialog_show (const char *message)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, message, NULL);

	/* Bash focus-stealing prevention in the face */
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gdk_x11_window_set_user_time (dialog->window, gdk_x11_get_server_time (dialog->window));
	gtk_window_present (GTK_WINDOW (dialog));

	g_signal_connect_swapped (dialog, "response",
	                          G_CALLBACK (gtk_widget_destroy),
	                          dialog);
	return FALSE;
}

