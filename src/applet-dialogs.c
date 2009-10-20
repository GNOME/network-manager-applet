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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <nm-device-ethernet.h>
#include <nm-device-wifi.h>
#include <nm-gsm-device.h>
#include <nm-cdma-device.h>

#include <nm-setting-connection.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>
#include <nm-setting-wired.h>
#include <nm-setting-8021x.h>
#include <nm-setting-ip4-config.h>
#include <nm-utils.h>

#include <gtk/gtk.h>
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

static char *
ip4_address_as_string (guint32 ip)
{
	char *ip_string;
	struct in_addr tmp_addr;

	tmp_addr.s_addr = ip;
	ip_string = g_malloc0 (INET_ADDRSTRLEN + 1);
	if (!inet_ntop (AF_INET, &tmp_addr, ip_string, INET_ADDRSTRLEN))
		strcpy (ip_string, "(none)");
	return ip_string;
}

static char *
get_eap_label (NMSettingWirelessSecurity *sec,
			   NMSetting8021x *s_8021x)
{
	GString *str = NULL;
	char *phase2_str = NULL;

	if (sec) {
		const char *key_mgmt = nm_setting_wireless_security_get_key_mgmt (sec);
		const char *auth_alg = nm_setting_wireless_security_get_auth_alg (sec);

		if (!strcmp (key_mgmt, "ieee8021x")) {
			if (auth_alg && !strcmp (auth_alg, "leap"))
				str = g_string_new (_("LEAP"));
			else
				str = g_string_new (_("Dynamic WEP"));
		} else if (!strcmp (key_mgmt, "wpa-eap"))
			str = g_string_new (_("WPA/WPA2"));
		else
			return NULL;
	} else if (s_8021x)
		str = g_string_new ("802.1x");

	if (!s_8021x)
		goto out;

	if (nm_setting_802_1x_get_num_eap_methods (s_8021x)) {
		char *eap_str = g_ascii_strup (nm_setting_802_1x_get_eap_method (s_8021x, 0), -1);
		g_string_append_printf (str, ", EAP-%s", eap_str);
		g_free (eap_str);
	}

	if (nm_setting_802_1x_get_phase2_auth (s_8021x))
		phase2_str = g_ascii_strup (nm_setting_802_1x_get_phase2_auth (s_8021x), -1);
	else if (nm_setting_802_1x_get_phase2_autheap (s_8021x))
		phase2_str = g_ascii_strup (nm_setting_802_1x_get_phase2_autheap (s_8021x), -1);

	if (phase2_str) {
		g_string_append (str, ", ");
		g_string_append (str, phase2_str);
		g_free (phase2_str);
	}
	
out:
	return g_string_free (str, FALSE);
}

static NMConnection *
get_connection_for_active (NMApplet *applet, NMActiveConnection *active)
{
	GSList *list, *iter;
	NMConnection *connection = NULL;
	NMConnectionScope scope;
	const char *path;

	scope = nm_active_connection_get_scope (active);
	g_return_val_if_fail (scope != NM_CONNECTION_SCOPE_UNKNOWN, NULL);

	path = nm_active_connection_get_connection (active);
	g_return_val_if_fail (path != NULL, NULL);

	list = applet_get_all_connections (applet);
	for (iter = list; iter; iter = g_slist_next (iter)) {
		NMConnection *candidate = NM_CONNECTION (iter->data);

		if (   (nm_connection_get_scope (candidate) == scope)
			   && !strcmp (nm_connection_get_path (candidate), path)) {
			connection = candidate;
			break;
		}
	}

	g_slist_free (list);

	return connection;
}

static GtkWidget *
create_info_label (const char *text, gboolean selectable)
{
	GtkWidget *label;

	label = gtk_label_new (text ? text : "");
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
	gtk_label_set_selectable (GTK_LABEL (label), selectable);
	return label;
}

static GtkWidget *
create_info_label_security (NMConnection *connection)
{
	NMSettingConnection *s_con;
	char *label = NULL;
	GtkWidget *w;
	const char *connection_type;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	connection_type = nm_setting_connection_get_connection_type (s_con);
	if (!strcmp (connection_type, NM_SETTING_WIRELESS_SETTING_NAME)) {
		NMSettingWireless *s_wireless;
		NMSettingWirelessSecurity *s_wireless_sec;
		NMSetting8021x *s_8021x;
		const char *security;

		s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
		s_wireless_sec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection, 
																				  NM_TYPE_SETTING_WIRELESS_SECURITY);
		s_8021x = (NMSetting8021x *) nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
		security = s_wireless ? nm_setting_wireless_get_security (s_wireless) : NULL;

		if (security && !strcmp (security, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME) && s_wireless_sec) {
			const char *key_mgmt = nm_setting_wireless_security_get_key_mgmt (s_wireless_sec);

			if (!strcmp (key_mgmt, "none"))
				label = g_strdup (_("WEP"));
			else if (!strcmp (key_mgmt, "wpa-none"))
				label = g_strdup (_("WPA/WPA2"));
			else if (!strcmp (key_mgmt, "wpa-psk"))
				label = g_strdup (_("WPA/WPA2"));
			else
				label = get_eap_label (s_wireless_sec, s_8021x);
		} else {
			label = g_strdup (_("None"));
		}
	} else if (!strcmp (connection_type, NM_SETTING_WIRED_SETTING_NAME)) {
		NMSetting8021x *s_8021x;

		s_8021x = (NMSetting8021x *) nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
		if (s_8021x)
			label = get_eap_label (NULL, s_8021x);
		else
			label = g_strdup (_("None"));
	}

	w = create_info_label (label ? label : _("Unknown"), TRUE);
	g_free (label);

	return w;
}

static GtkWidget *
create_info_notebook_label (NMConnection *connection, gboolean is_default)
{
	GtkWidget *label;
	NMSettingConnection *s_con;
	GString *str;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	str = g_string_new (nm_setting_connection_get_id (s_con));

	if (is_default)
		str = g_string_append (str, " (default)");

	label = gtk_label_new (str->str);
	g_string_free (str, TRUE);

	return label;
}

typedef struct {
	NMDevice *device;
	GtkWidget *label;
	guint32 id;
} SpeedInfo;

static void
device_destroyed (gpointer data, GObject *device_ptr)
{
	SpeedInfo *info = data;

	/* Device is destroyed, notify handler won't fire
	 * anymore anyway.  Let the label destroy handler
	 * know it doesn't have to disconnect the callback.
	 */
	info->device = NULL;
	info->id = 0;
}

static void
label_destroyed (gpointer data, GObject *label_ptr)
{
	SpeedInfo *info = data;
	/* Remove the notify handler from the device */
	if (info->device) {
		if (info->id)
			g_signal_handler_disconnect (info->device, info->id);
		/* destroy our info data */
		g_object_weak_unref (G_OBJECT (info->device), device_destroyed, info);
		memset (info, 0, sizeof (SpeedInfo));
		g_free (info);
	}
}

static void
bitrate_changed_cb (GObject *device, GParamSpec *pspec, gpointer user_data)
{
	GtkWidget *speed_label = GTK_WIDGET (user_data);
	guint32 bitrate = 0;
	char *str = NULL;

	bitrate = nm_device_wifi_get_bitrate (NM_DEVICE_WIFI (device)) / 1000;
	if (bitrate)
		str = g_strdup_printf (_("%u Mb/s"), bitrate);

	gtk_label_set_text (GTK_LABEL (speed_label), str ? str : _("Unknown"));
	g_free (str);
}

static void
info_dialog_add_page (GtkNotebook *notebook,
					  NMConnection *connection,
					  gboolean is_default,
					  NMDevice *device)
{
	GtkTable *table;
	guint32 speed = 0;
	char *str;
	const char *iface;
	NMIP4Config *ip4_config;
	const GArray *dns;
	NMIP4Address *def_addr;
	guint32 hostmask, network, bcast, netmask;
	int row = 0;
	SpeedInfo* info = NULL;
	GtkWidget* speed_label;

	table = GTK_TABLE (gtk_table_new (12, 2, FALSE));
	gtk_table_set_col_spacings (table, 12);
	gtk_table_set_row_spacings (table, 6);
	gtk_container_set_border_width (GTK_CONTAINER (table), 12);

	/* Interface */
	iface = nm_device_get_iface (device);
	if (NM_IS_DEVICE_ETHERNET (device))
		str = g_strdup_printf (_("Ethernet (%s)"), iface);
	else if (NM_IS_DEVICE_WIFI (device))
		str = g_strdup_printf (_("802.11 WiFi (%s)"), iface);
	else if (NM_IS_GSM_DEVICE (device))
		str = g_strdup_printf (_("GSM (%s)"), iface);
	else if (NM_IS_CDMA_DEVICE (device))
		str = g_strdup_printf (_("CDMA (%s)"), iface);
	else
		str = g_strdup (iface);

	gtk_table_attach_defaults (table,
							   create_info_label (_("Interface:"), FALSE),
							   0, 1, row, row + 1);
	gtk_table_attach_defaults (table,
							   create_info_label (str, TRUE),
							   1, 2, row, row + 1);
	g_free (str);
	row++;

	/* Hardware address */
	str = NULL;
	if (NM_IS_DEVICE_ETHERNET (device))
		str = g_strdup (nm_device_ethernet_get_hw_address (NM_DEVICE_ETHERNET (device)));
	else if (NM_IS_DEVICE_WIFI (device))
		str = g_strdup (nm_device_wifi_get_hw_address (NM_DEVICE_WIFI (device)));

	gtk_table_attach_defaults (table,
							   create_info_label (_("Hardware Address:"), FALSE),
							   0, 1, row, row + 1);
	gtk_table_attach_defaults (table,
							   create_info_label (str, TRUE),
							   1, 2, row, row + 1);
	g_free (str);
	row++;

	/* Driver */
	gtk_table_attach_defaults (table,
							   create_info_label (_("Driver:"), FALSE),
							   0, 1, row, row + 1);
	gtk_table_attach_defaults (table,
							   create_info_label (nm_device_get_driver (device), TRUE),
							   1, 2, row, row + 1);
	row++;

	speed_label = create_info_label ("", TRUE);

	/* Speed */
	str = NULL;
	if (NM_IS_DEVICE_ETHERNET (device)) {
		/* Wired speed in Mb/s */
		speed = nm_device_ethernet_get_speed (NM_DEVICE_ETHERNET (device));
	} else if (NM_IS_DEVICE_WIFI (device)) {
		/* Wireless speed in Kb/s */
		speed = nm_device_wifi_get_bitrate (NM_DEVICE_WIFI (device)) / 1000;

		/* Listen for wifi speed changes */
		info = g_malloc0 (sizeof (SpeedInfo));
		info->device = device;
		info->label = speed_label;
		info->id = g_signal_connect (device,
		                             "notify::" NM_DEVICE_WIFI_BITRATE,
		                             G_CALLBACK (bitrate_changed_cb),
		                             speed_label);

		g_object_weak_ref (G_OBJECT(speed_label), label_destroyed, info);
		g_object_weak_ref (G_OBJECT(device), device_destroyed, info);
	}

	if (speed)
		str = g_strdup_printf (_("%u Mb/s"), speed);

	gtk_label_set_text (GTK_LABEL(speed_label), str ? str : _("Unknown"));
	g_free (str);

	gtk_table_attach_defaults (table,
							   create_info_label (_("Speed:"), FALSE),
							   0, 1, row, row + 1);
	gtk_table_attach_defaults (table,
							   speed_label,
							   1, 2, row, row + 1);
	row++;

	/* Security */
	gtk_table_attach_defaults (table,
							   create_info_label (_("Security:"), FALSE),
							   0, 1, row, row + 1);
	gtk_table_attach_defaults (table,
							   create_info_label_security (connection),
							   1, 2, row, row + 1);
	row++;

	/* Empty line */
	gtk_table_attach_defaults (table,
							   gtk_label_new (""),
							   0, 2, row, row + 1);
	row++;

	/* IP4 */

	ip4_config = nm_device_get_ip4_config (device);
	def_addr = nm_ip4_config_get_addresses (ip4_config)->data;

	/* Address */
	gtk_table_attach_defaults (table,
							   create_info_label (_("IP Address:"), FALSE),
							   0, 1, row, row + 1);
	str = ip4_address_as_string (nm_ip4_address_get_address (def_addr));
	gtk_table_attach_defaults (table,
							   create_info_label (str, TRUE),
							   1, 2, row, row + 1);
	g_free (str);
	row++;

	/* Broadcast */
	netmask = nm_utils_ip4_prefix_to_netmask (nm_ip4_address_get_prefix (def_addr));
	network = ntohl (nm_ip4_address_get_address (def_addr)) & ntohl (netmask);
	hostmask = ~ntohl (netmask);
	bcast = htonl (network | hostmask);

	gtk_table_attach_defaults (table,
							   create_info_label (_("Broadcast Address:"), FALSE),
							   0, 1, row, row + 1);
	str = ip4_address_as_string (bcast);
	gtk_table_attach_defaults (table,
							   create_info_label (str, TRUE),
							   1, 2, row, row + 1);
	g_free (str);
	row++;

	/* Prefix */
	gtk_table_attach_defaults (table,
							   create_info_label (_("Subnet Mask:"), FALSE),
							   0, 1, row, row + 1);
	str = ip4_address_as_string (netmask);
	gtk_table_attach_defaults (table,
							   create_info_label (str, TRUE),
							   1, 2, row, row + 1);
	g_free (str);
	row++;

	/* Gateway */
	if (nm_ip4_address_get_gateway (def_addr)) {
		gtk_table_attach_defaults (table,
								   create_info_label (_("Default Route:"), FALSE),
								   0, 1, row, row + 1);
		str = ip4_address_as_string (nm_ip4_address_get_gateway (def_addr));
		gtk_table_attach_defaults (table,
								   create_info_label (str, TRUE),
								   1, 2, row, row + 1);
		g_free (str);
		row++;
	}

	/* DNS */
	dns = nm_ip4_config_get_nameservers (ip4_config);

	if (dns && dns->len) {
		gtk_table_attach_defaults (table,
								   create_info_label (_("Primary DNS:"), FALSE),
								   0, 1, row, row + 1);
		str = ip4_address_as_string (g_array_index (dns, guint32, 0));
		gtk_table_attach_defaults (table,
								   create_info_label (str, TRUE),
								   1, 2, row, row + 1);
		g_free (str);
		row++;

		if (dns->len > 1) {
			gtk_table_attach_defaults (table,
									   create_info_label (_("Secondary DNS:"), FALSE),
									   0, 1, row, row + 1);
			str = ip4_address_as_string (g_array_index (dns, guint32, 1));
			gtk_table_attach_defaults (table,
									   create_info_label (str, TRUE),
									   1, 2, row, row + 1);
			g_free (str);
			row++;
		}
	}

	gtk_notebook_append_page (notebook, GTK_WIDGET (table),
							  create_info_notebook_label (connection, is_default));

	gtk_widget_show_all (GTK_WIDGET (table));
}

static GtkWidget *
info_dialog_update (NMApplet *applet)
{
	GtkNotebook *notebook;
	const GPtrArray *connections;
	int i;
	int pages = 0;

	notebook = GTK_NOTEBOOK (glade_xml_get_widget (applet->info_dialog_xml, "info_notebook"));

	/* Remove old pages */
	for (i = gtk_notebook_get_n_pages (notebook); i > 0; i--)
		gtk_notebook_remove_page (notebook, -1);

	/* Add new pages */
	connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; connections && (i < connections->len); i++) {
		NMActiveConnection *active_connection = g_ptr_array_index (connections, i);
		NMConnection *connection;
		const GPtrArray *devices;

		if (nm_active_connection_get_state (active_connection) != NM_ACTIVE_CONNECTION_STATE_ACTIVATED)
			continue;

		devices = nm_active_connection_get_devices (active_connection);
		if (!devices || !devices->len) {
			g_warning ("Active connection %s had no devices!",
					   nm_object_get_path (NM_OBJECT (active_connection)));
			continue;
		}

		connection = get_connection_for_active (applet, active_connection);
		if (!connection) {
			g_warning ("%s: couldn't find the default active connection's NMConnection!", __func__);
			continue;
		}
			
		info_dialog_add_page (notebook,
							  connection,
							  nm_active_connection_get_default (active_connection),
							  g_ptr_array_index (devices, 0));
		pages++;
	}

	if (pages == 0) {
		/* Shouldn't really happen but ... */
		info_dialog_show_error (_("No valid active connections found!"));
		return NULL;
	}

	return glade_xml_get_widget (applet->info_dialog_xml, "info_dialog");
}

void
applet_info_dialog_show (NMApplet *applet)
{
	GtkWidget *dialog;

	dialog = info_dialog_update (applet);
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


GtkWidget *
applet_warning_dialog_show (const char *message)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, message, NULL);

	/* Bash focus-stealing prevention in the face */
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_window_set_default_icon_name (GTK_STOCK_DIALOG_ERROR);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Missing resources"));
	gtk_widget_realize (dialog);
	gtk_widget_show (dialog);
	gtk_window_present_with_time (GTK_WINDOW (dialog), gdk_x11_get_server_time (dialog->window));

	g_signal_connect_swapped (dialog, "response",
	                          G_CALLBACK (gtk_widget_destroy),
	                          dialog);
	return dialog;
}

