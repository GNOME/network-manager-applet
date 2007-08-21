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
 * This applet used the GNOME Wireless Applet as a skeleton to build from.
 *
 * GNOME Wireless Applet Authors:
 *		Eskil Heyn Olsen <eskil@eskil.dk>
 *		Bastien Nocera <hadess@hadess.net> (Gnome2 port)
 *
 * (C) Copyright 2004-2005 Red Hat, Inc.
 * (C) Copyright 2001, 2002 Free Software Foundation
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomeui/libgnomeui.h>
#include <iwlib.h>
#include <wireless.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <nm-device-802-3-ethernet.h>
#include <nm-device-802-11-wireless.h>

#include <glade/glade.h>
#include <gconf/gconf-client.h>

#ifdef ENABLE_NOTIFY
#include <libnotify/notify.h>
#endif

#include "applet.h"
#include "applet-compat.h"
#include "applet-dbus.h"
#include "applet-dbus-info.h"
#include "applet-notifications.h"
#include "other-network-dialog.h"
#include "passphrase-dialog.h"
#include "menu-items.h"
#include "vpn-password-dialog.h"
#include "nm-utils.h"
#include "dbus-method-dispatcher.h"

#include "nm-connection.h"

/* Compat for GTK 2.6 */
#if (GTK_MAJOR_VERSION <= 2 && GTK_MINOR_VERSION == 6)
	#define GTK_STOCK_INFO			GTK_STOCK_DIALOG_INFO
#endif

static GObject *			nma_constructor (GType type, guint n_props, GObjectConstructParam *construct_props);
static void			nma_icons_init (NMApplet *applet);
static void				nma_icons_free (NMApplet *applet);
static void				nma_icons_zero (NMApplet *applet);
static gboolean			nma_icons_load_from_disk (NMApplet *applet);
static void			nma_finalize (GObject *object);

static void foo_client_vpn_state_change (NMClient *client,
                             NMVPNConnectionState state,
                             gpointer user_data);

G_DEFINE_TYPE(NMApplet, nma, G_TYPE_OBJECT)

/* Shamelessly ripped from the Linux kernel ieee80211 stack */
gboolean
nma_is_empty_ssid (const char * ssid, int len)
{
        /* Single white space is for Linksys APs */
        if (len == 1 && ssid[0] == ' ')
                return TRUE;

        /* Otherwise, if the entire ssid is 0, we assume it is hidden */
        while (len--) {
                if (ssid[len] != '\0')
                        return FALSE;
        }
        return TRUE;
}

const char *
nma_escape_ssid (const char * ssid, guint32 len)
{
	static char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
	const char *s = ssid;
	char *d = escaped;

	if (nma_is_empty_ssid (ssid, len)) {
		memcpy (escaped, "<hidden>", sizeof ("<hidden>"));
		return escaped;
	}

	len = MIN (len, (guint32) IW_ESSID_MAX_SIZE);
	while (len--) {
		if (*s == '\0') {
			*d++ = '\\';
			*d++ = '0';
			s++;
		} else {
			*d++ = *s++;
		}
	}
	*d = '\0';
	return escaped;
}


static NMDevice *
get_first_active_device (NMApplet *applet)
{
	GSList *list;
	GSList *iter;
	NMDevice *active_device = NULL;

	list = nm_client_get_devices (applet->nm_client);
	for (iter = list; iter; iter = iter->next) {
		NMDevice *device = NM_DEVICE (iter->data);

		if (nm_device_get_state (device) == NM_DEVICE_STATE_ACTIVATED) {
			active_device = device;
			break;
		}
	}

	g_slist_free (list);

	return active_device;
}

static void nma_init (NMApplet *applet)
{
	applet->animation_id = 0;
	applet->animation_step = 0;
	applet->nm_running = FALSE;
	applet->passphrase_dialog = NULL;
	applet->connection_timeout_id = 0;
	applet->icon_theme = NULL;
#ifdef ENABLE_NOTIFY
	applet->notification = NULL;
#endif
#ifdef HAVE_STATUS_ICON
	applet->size = -1;
#endif

	nma_icons_zero (applet);

/*	gtk_window_set_default_icon_from_file (ICONDIR"/NMApplet/wireless-applet.png", NULL); */
}

static void nma_class_init (NMAppletClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->constructor = nma_constructor;
	gobject_class->finalize = nma_finalize;
}

static GtkWidget * get_label (GtkWidget *info_dialog, GladeXML *xml, const char *name)
{
	GtkWidget *label;

	if (xml != NULL)
	{
		label = glade_xml_get_widget (xml, name);
		g_object_set_data (G_OBJECT (info_dialog), name, label);
	}
	else
		label = g_object_get_data (G_OBJECT (info_dialog), name);

	return label;
}

static void nma_show_socket_err (GtkWidget *info_dialog, const char *err)
{
	GtkWidget *error_dialog;

	error_dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (info_dialog), 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
			"<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s", _("Error displaying connection information:"), err);
	gtk_window_present (GTK_WINDOW (error_dialog));
	g_signal_connect_swapped (error_dialog, "response", G_CALLBACK (gtk_widget_destroy), error_dialog);
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

static gboolean
nma_update_info (NMApplet *applet)
{
	GtkWidget *info_dialog;
	GtkWidget *label;
	NMDevice *device;
	NMIP4Config *cfg;
	int speed;
	char *str;
	char *iface_and_type;
	GArray *dns;

	info_dialog = glade_xml_get_widget (applet->info_dialog_xml, "info_dialog");
	if (!info_dialog) {
		nma_show_socket_err (info_dialog, "Could not find some required resources (the glade file)!");
		return FALSE;
	}

	device = get_first_active_device (applet);
	if (!device) {
		nma_show_socket_err (info_dialog, _("No active connections!"));
		return FALSE;
	}

	cfg = nm_device_get_ip4_config (device);

	speed = 0;
	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		speed = nm_device_802_3_ethernet_get_speed (NM_DEVICE_802_3_ETHERNET (device));
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		speed = nm_device_802_11_wireless_get_bitrate (NM_DEVICE_802_11_WIRELESS (device));

	str = nm_device_get_iface (device);
	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		iface_and_type = g_strdup_printf (_("Wired Ethernet (%s)"), str);
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		iface_and_type = g_strdup_printf (_("Wireless Ethernet (%s)"), str);
	else
		iface_and_type = g_strdup (str);

	g_free (str);

	label = get_label (info_dialog, applet->info_dialog_xml, "label-interface");
	gtk_label_set_text (GTK_LABEL (label), iface_and_type);
	g_free (iface_and_type);

	label = get_label (info_dialog, applet->info_dialog_xml, "label-speed");
	if (speed) {
		str = g_strdup_printf (_("%d Mb/s"), speed);
		gtk_label_set_text (GTK_LABEL (label), str);
		g_free (str);
	} else
		gtk_label_set_text (GTK_LABEL (label), _("Unknown"));

	str = nm_device_get_driver (device);
	label = get_label (info_dialog, applet->info_dialog_xml, "label-driver");
	gtk_label_set_text (GTK_LABEL (label), str);
	g_free (str);

	label = get_label (info_dialog, applet->info_dialog_xml, "label-ip-address");
	gtk_label_set_text (GTK_LABEL (label),
					ip4_address_as_string (nm_ip4_config_get_address (cfg)));

	label = get_label (info_dialog, applet->info_dialog_xml, "label-broadcast-address");
	gtk_label_set_text (GTK_LABEL (label),
					ip4_address_as_string (nm_ip4_config_get_broadcast (cfg)));

	label = get_label (info_dialog, applet->info_dialog_xml, "label-subnet-mask");
	gtk_label_set_text (GTK_LABEL (label),
					ip4_address_as_string (nm_ip4_config_get_netmask (cfg)));

	label = get_label (info_dialog, applet->info_dialog_xml, "label-default-route");
	gtk_label_set_text (GTK_LABEL (label),
					ip4_address_as_string (nm_ip4_config_get_gateway (cfg)));

	dns = nm_ip4_config_get_nameservers (cfg);
	if (dns) {
		if (dns->len > 0) {
			label = get_label (info_dialog, applet->info_dialog_xml, "label-primary-dns");
			gtk_label_set_text (GTK_LABEL (label),
							ip4_address_as_string (g_array_index (dns, guint32, 0)));
		}

		if (dns->len > 1) {
			label = get_label (info_dialog, applet->info_dialog_xml, "label-secondary-dns");
			gtk_label_set_text (GTK_LABEL (label),
							ip4_address_as_string (g_array_index (dns, guint32, 0)));
		}

		g_array_free (dns, TRUE);
	}

	str = NULL;
	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		str = nm_device_802_3_ethernet_get_hw_address (NM_DEVICE_802_3_ETHERNET (device));
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		str = nm_device_802_11_wireless_get_hw_address (NM_DEVICE_802_11_WIRELESS (device));

	if (str) {
		label = get_label (info_dialog, applet->info_dialog_xml, "label-hardware-address");
		gtk_label_set_text (GTK_LABEL (label), str);
		g_free (str);
	}

	return TRUE;
}

static void
nma_show_info_cb (GtkMenuItem *mi, NMApplet *applet)
{
	GtkWidget *info_dialog;

	info_dialog = glade_xml_get_widget (applet->info_dialog_xml, "info_dialog");

	if (nma_update_info (applet)) {
		gtk_window_present (GTK_WINDOW (info_dialog));
		g_signal_connect_swapped (info_dialog, "response", G_CALLBACK (gtk_widget_hide), info_dialog);
	}
}

static void about_dialog_activate_link_cb (GtkAboutDialog *about,
                                           const gchar *url,
                                           gpointer data)
{
	gnome_url_show (url, NULL);
}

static void nma_about_cb (GtkMenuItem *mi, NMApplet *applet)
{
	static const gchar *authors[] =
	{
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

	static const gchar *artists[] =
	{
		"Diana Fong <dfong@redhat.com>",
		NULL
	};


	/* FIXME: unnecessary with libgnomeui >= 2.16.0 */
	static gboolean been_here = FALSE;
	if (!been_here)
	{
		been_here = TRUE;
		gtk_about_dialog_set_url_hook (about_dialog_activate_link_cb, NULL, NULL);
	}

	/* GTK 2.6 and later code */
	gtk_show_about_dialog (NULL,
	                       "name", _("NetworkManager Applet"),
	                       "version", VERSION,
	                       "copyright", _("Copyright \xc2\xa9 2004-2007 Red Hat, Inc.\n"
					                  "Copyright \xc2\xa9 2005-2007 Novell, Inc."),
	                       "comments", _("Notification area applet for managing your network devices and connections."),
	                       "website", "http://www.gnome.org/projects/NetworkManager/",
	                       "authors", authors,
	                       "artists", artists,
	                       "translator-credits", _("translator-credits"),
	                       "logo-icon-name", GTK_STOCK_NETWORK,
	                       NULL);
}


#ifndef ENABLE_NOTIFY
/*
 * nma_show_vpn_failure_dialog
 *
 * Present the VPN failure dialog.
 *
 */
static void
nma_show_vpn_failure_dialog (const char *title,
                              const char *msg)
{
	GtkWidget	*dialog;

	g_return_if_fail (title != NULL);
	g_return_if_fail (msg != NULL);

	dialog = gtk_message_dialog_new_with_markup (NULL, 0, GTK_MESSAGE_ERROR,
				GTK_BUTTONS_OK, msg, NULL);
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
	g_signal_connect (dialog, "close", G_CALLBACK (gtk_widget_destroy), NULL);

	/* Bash focus-stealing prevention in the face */
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gdk_x11_window_set_user_time (dialog->window, gtk_get_current_event_time ());
	gtk_widget_show_all (dialog);
}
#endif


/*
 * nma_schedule_vpn_failure_alert
 *
 * Schedule display of a VPN failure message.
 *
 */
void nma_show_vpn_failure_alert (NMApplet *applet, const char *member, const char *vpn_name, const char *error_msg)
{
	char *title = NULL;
	char *desc = NULL;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (member != NULL);
	g_return_if_fail (vpn_name != NULL);
	g_return_if_fail (error_msg != NULL);

	if (!strcmp (member, NM_DBUS_VPN_SIGNAL_LOGIN_FAILED))
	{
		title = g_strdup (_("VPN Login Failure"));
		desc = g_strdup_printf (_("Could not start the VPN connection '%s' due to a login failure."), vpn_name);
	}
	else if (!strcmp (member, NM_DBUS_VPN_SIGNAL_LAUNCH_FAILED))
	{
		title = g_strdup (_("VPN Start Failure"));
		desc = g_strdup_printf (_("Could not start the VPN connection '%s' due to a failure launching the VPN program."), vpn_name);
	}
	else if (!strcmp (member, NM_DBUS_VPN_SIGNAL_CONNECT_FAILED))
	{
		title = g_strdup (_("VPN Connect Failure"));
		desc = g_strdup_printf (_("Could not start the VPN connection '%s' due to a connection error."), vpn_name);
	}
	else if (!strcmp (member, NM_DBUS_VPN_SIGNAL_VPN_CONFIG_BAD))
	{
		title = g_strdup (_("VPN Configuration Error"));
		desc = g_strdup_printf (_("The VPN connection '%s' was not correctly configured."), vpn_name);
	}
	else if (!strcmp (member, NM_DBUS_VPN_SIGNAL_IP_CONFIG_BAD))
	{
		title = g_strdup (_("VPN Connect Failure"));
		desc = g_strdup_printf (_("Could not start the VPN connection '%s' because the VPN server did not return an adequate network configuration."), vpn_name);
	}

	if (title && desc)
	{
		char * msg;

#ifdef ENABLE_NOTIFY
		msg = g_strdup_printf ("\n%s\n%s", desc, error_msg);
		nma_send_event_notification (applet, NOTIFY_URGENCY_CRITICAL,
			title, msg, "gnome-lockscreen");
#else
		msg = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n"
			"%s\n\n%s", title, desc, error_msg);
		nma_show_vpn_failure_dialog (title, msg);
#endif
		g_free (msg);
	}

	g_free (title);
	g_free (desc);
}


#ifndef ENABLE_NOTIFY
/*
 * nma_show_vpn_login_banner_dialog
 *
 * Present the VPN login banner dialog.
 *
 */
static void
nma_show_vpn_login_banner_dialog (const char *title,
                                   const char *msg)
{
	GtkWidget	*dialog;

	g_return_if_fail (title != NULL);
	g_return_if_fail (msg != NULL);

	dialog = gtk_message_dialog_new_with_markup (NULL, 0, GTK_MESSAGE_INFO,
					GTK_BUTTONS_OK, msg, NULL);
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
	g_signal_connect (dialog, "close", G_CALLBACK (gtk_widget_destroy), NULL);

	/* Bash focus-stealing prevention in the face */
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gdk_x11_window_set_user_time (dialog->window, gtk_get_current_event_time ());
	gtk_widget_show_all (dialog);
}
#endif


/*
 * nma_schedule_vpn_login_banner
 *
 * Schedule a display of the VPN banner
 *
 */
void nma_show_vpn_login_banner (NMApplet *applet, const char *vpn_name, const char *banner)
{
	const char *	title;
	char *		msg;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (vpn_name != NULL);
	g_return_if_fail (banner != NULL);

	title = _("VPN Login Message");
#ifdef ENABLE_NOTIFY
	msg = g_strdup_printf ("\n%s", banner);
	/* gnome-lockscreen is a padlock; exactly what we want for a VPN */
	nma_send_event_notification (applet, NOTIFY_URGENCY_LOW,
		title, msg, "gnome-lockscreen");
#else
	msg = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
	                       title, banner);
	nma_show_vpn_login_banner_dialog (title, msg);
#endif
	g_free (msg);
}


/*
 * nma_get_first_active_vpn_connection
 *
 * Return the first active VPN connection, if any.
 *
 */
static NMVPNConnection *
nma_get_first_active_vpn_connection (NMApplet *applet)
{
	GSList *connections;
	GSList *iter;
	NMVPNConnection * connection = NULL;

	connections = nm_client_get_vpn_connections (applet->nm_client);
	for (iter = connections; iter; iter = iter->next) {
		NMVPNConnection *connection = NM_VPN_CONNECTION (iter->data);
		NMVPNConnectionState state = nm_vpn_connection_get_state (connection);

		if (state == NM_VPN_CONNECTION_STATE_ACTIVATED)
			goto out;
	}

out:
	g_slist_free (connections);
	return connection;
}


/*
 * show_warning_dialog
 *
 * pop up a warning or error dialog with certain text
 *
 */
static gboolean show_warning_dialog (char *mesg)
{
	GtkWidget	*	dialog;

	dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, mesg, NULL);

	/* Bash focus-stealing prevention in the face */
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gdk_x11_window_set_user_time (dialog->window, gtk_get_current_event_time ());
	gtk_window_present (GTK_WINDOW (dialog));

	g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
	g_free (mesg);

	return FALSE;
}


/*
 * nma_schedule_warning_dialog
 *
 * Run a warning dialog in the main event loop.
 *
 */
void nma_schedule_warning_dialog (NMApplet *applet, const char *msg)
{
	char *lcl_msg;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (msg != NULL);

	lcl_msg = g_strdup (msg);
	g_idle_add ((GSourceFunc) show_warning_dialog, lcl_msg);
}


typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMAccessPoint *ap;
} DeviceMenuItemInfo;

static void
device_menu_item_info_destroy (gpointer data)
{
	g_slice_free (DeviceMenuItemInfo, data);
}

/*
 * nma_menu_item_activate
 *
 * Signal function called when user clicks on a menu item
 *
 */
static void
nma_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	DeviceMenuItemInfo *info = (DeviceMenuItemInfo *) user_data;
	NMSetting *setting = NULL;

	if (NM_IS_DEVICE_802_3_ETHERNET (info->device)) {
		setting = nm_setting_wired_new ();
	} else if (NM_IS_DEVICE_802_11_WIRELESS (info->device)) {
		NMSettingWireless *wireless;
		GByteArray * ssid;

		setting = nm_setting_wireless_new ();
		wireless = (NMSettingWireless *) setting;
		wireless->mode = g_strdup ("infrastructure");

		ssid = nm_access_point_get_ssid (info->ap);
		wireless->ssid = g_byte_array_sized_new (ssid->len);
		g_byte_array_append (wireless->ssid, ssid->data, ssid->len);
		g_byte_array_free (ssid, TRUE);
	} else
		g_warning ("Unhandled device type '%s'", G_OBJECT_CLASS_NAME (info->device));

	if (setting) {
		NMConnection *connection;
		NMSettingInfo *s_info;

		connection = nm_connection_new ();
		nm_connection_add_setting (connection, setting);

		s_info = (NMSettingInfo *) nm_setting_info_new ();
		s_info->name = g_strdup ("Auto");
		s_info->devtype = g_strdup (setting->name);
		nm_connection_add_setting (connection, (NMSetting *) s_info);

		nm_device_activate (info->device, connection);
		nm_connection_destroy (connection);
	}

	nmi_dbus_signal_user_interface_activated (info->applet->connection);
}


/*
 * nma_menu_vpn_item_activate
 *
 * Signal function called when user clicks on a VPN menu item
 *
 */
static void
nma_menu_vpn_item_activate (GtkMenuItem *item, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMVPNConnection *vpn;
	NMVPNConnection *active_vpn;
	const char *name;
	GSList *passwords;

	vpn = NM_VPN_CONNECTION (g_object_get_data (G_OBJECT (item), "vpn"));
	g_assert (vpn);

	name = nm_vpn_connection_get_name (vpn);
	active_vpn = nma_get_first_active_vpn_connection (applet);

	if (vpn != active_vpn) {
		char *gconf_key;
		char *escaped_name;
		gboolean last_attempt_success;
		gboolean reprompt;

		escaped_name = gconf_escape_key (name, strlen (name));
		gconf_key = g_strdup_printf ("%s/%s/last_attempt_success", GCONF_PATH_VPN_CONNECTIONS, escaped_name);
		g_free (escaped_name);
		last_attempt_success = gconf_client_get_bool (applet->gconf_client, gconf_key, NULL);
		g_free (gconf_key);

		reprompt = ! last_attempt_success; /* it's obvious, but.. */

		if ((passwords = nma_vpn_request_password (applet, 
										   name, 
										   nm_vpn_connection_get_service (vpn), 
										   reprompt)) != NULL) {
			if (!nm_vpn_connection_activate (vpn, passwords)) {
				/* FIXME: show a dialog or something */
			}

			g_slist_foreach (passwords, (GFunc) g_free, NULL);
			g_slist_free (passwords);
		}
	}

	nmi_dbus_signal_user_interface_activated (applet->connection);
}


/*
 * nma_menu_configure_vpn_item_activate
 *
 * Signal function called when user clicks "Configure VPN..."
 *
 */
static void nma_menu_configure_vpn_item_activate (GtkMenuItem *item, gpointer user_data)
{
	NMApplet	*applet = (NMApplet *)user_data;
	const char *argv[] = { BINDIR "/nm-vpn-properties", NULL};

	g_return_if_fail (item != NULL);
	g_return_if_fail (applet != NULL);

	g_spawn_async (NULL, (gchar **) argv, NULL, 0, NULL, NULL, NULL, NULL);

	nmi_dbus_signal_user_interface_activated (applet->connection);
}

/*
 * nma_menu_disconnect_vpn_item_activate
 *
 * Signal function called when user clicks "Disconnect VPN..."
 *
 */
static void
nma_menu_disconnect_vpn_item_activate (GtkMenuItem *item, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GSList *iter;
	GSList *connections;

	/* The current design (both NM and applet) disallows multiple active VPN connections.
	   Which one of the connections should we deactivate here? Currently, all (which always means one).
	*/

	connections = nm_client_get_vpn_connections (applet->nm_client);
	for (iter = connections; iter; iter = iter->next) {
		NMVPNConnection *connection = NM_VPN_CONNECTION (iter->data);
		NMVPNConnectionState state = nm_vpn_connection_get_state (connection);

		if (   (state == NM_VPN_CONNECTION_STATE_ACTIVATED)
		    || nm_vpn_connection_is_activating (connection))
			nm_vpn_connection_deactivate (connection);
	}
	g_slist_free (connections);

	nmi_dbus_signal_user_interface_activated (applet->connection);
}


/*
 * nma_menu_add_separator_item
 *
 */
static void
nma_menu_add_separator_item (GtkMenuShell *menu)
{
	GtkWidget *menu_item;

	menu_item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (menu, menu_item);
	gtk_widget_show (menu_item);
}


/*
 * nma_menu_add_text_item
 *
 * Add a non-clickable text item to a menu
 *
 */
static void nma_menu_add_text_item (GtkWidget *menu, char *text)
{
	GtkWidget		*menu_item;

	g_return_if_fail (text != NULL);
	g_return_if_fail (menu != NULL);

	menu_item = gtk_menu_item_new_with_label (text);
	gtk_widget_set_sensitive (menu_item, FALSE);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}


/*
 * nma_menu_add_device_item
 *
 * Add a network device to the menu
 *
 */
static void
nma_menu_add_device_item (GtkWidget *menu,
					 NMDevice *device,
					 gint n_devices,
					 NMApplet *applet)
{
	GtkMenuItem *menu_item = NULL;

	if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		menu_item = wireless_menu_item_new (NM_DEVICE_802_11_WIRELESS (device), n_devices);
	} else if (NM_IS_DEVICE_802_3_ETHERNET (device))
		menu_item = wired_menu_item_new (NM_DEVICE_802_3_ETHERNET (device), n_devices);
	else
		g_warning ("Unhandled device type %s", G_OBJECT_CLASS_NAME (device));

	if (menu_item) {
		DeviceMenuItemInfo *info;

		info = g_slice_new (DeviceMenuItemInfo);
		info->applet = applet;
		info->device = device;
		info->ap = NULL;

		g_signal_connect_data (menu_item, "activate",
						   G_CALLBACK (nma_menu_item_activate),
						   info,
						   (GClosureNotify) device_menu_item_info_destroy, 0);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (menu_item));
		gtk_widget_show (GTK_WIDGET (menu_item));
	}
}


static void custom_essid_item_selected (GtkWidget *menu_item, NMApplet *applet)
{
	nma_other_network_dialog_run (applet, FALSE);
}


static void nma_menu_add_custom_essid_item (GtkWidget *menu, NMApplet *applet)
{
	GtkWidget *menu_item;
	GtkWidget *label;

	menu_item = gtk_menu_item_new ();
	label = gtk_label_new_with_mnemonic (_("_Connect to Other Wireless Network..."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_container_add (GTK_CONTAINER (menu_item), label);
	gtk_widget_show_all (menu_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	g_signal_connect (menu_item, "activate", G_CALLBACK (custom_essid_item_selected), applet);
}


static void new_network_item_selected (GtkWidget *menu_item, NMApplet *applet)
{
	nma_other_network_dialog_run (applet, TRUE);
}


static void
nma_menu_add_create_network_item (GtkWidget *menu, NMApplet *applet)
{
	GtkWidget *menu_item;
	GtkWidget *label;

	menu_item = gtk_menu_item_new ();
	label = gtk_label_new_with_mnemonic (_("Create _New Wireless Network..."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_container_add (GTK_CONTAINER (menu_item), label);
	gtk_widget_show_all (menu_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	g_signal_connect (menu_item, "activate", G_CALLBACK (new_network_item_selected), applet);
}


typedef struct {
	NMApplet *	applet;
	NMDevice *device;
	GByteArray *active_ssid;
	gboolean			has_encrypted;
	GtkWidget *		menu;
} AddNetworksCB;

/*
 * nma_add_networks_helper
 *
 */
static void
nma_add_networks_helper (gpointer data, gpointer user_data)
{
	NMAccessPoint *ap = NM_ACCESS_POINT (data);
	AddNetworksCB *cb_data = (AddNetworksCB *) user_data;
	NMNetworkMenuItem *	item;
	GtkCheckMenuItem *	gtk_item;
	DeviceMenuItemInfo *info;
	GByteArray * ssid;

	/* Don't add BSSs that hide their SSID */
	ssid = nm_access_point_get_ssid (ap);
	if (nma_is_empty_ssid (ssid->data, ssid->len))
		return;

	item = network_menu_item_new (cb_data->applet->encryption_size_group);
	gtk_item = network_menu_item_get_check_item (item);

	gtk_menu_shell_append (GTK_MENU_SHELL (cb_data->menu), GTK_WIDGET (gtk_item));
	network_menu_item_update (cb_data->applet, item, ap, cb_data->has_encrypted);
	if ((nm_device_get_state (cb_data->device) == NM_DEVICE_STATE_ACTIVATED)
	    && cb_data->active_ssid) {
		if (ssid && nma_same_ssid (ssid, cb_data->active_ssid))
			gtk_check_menu_item_set_active (gtk_item, TRUE);
		g_byte_array_free (ssid, TRUE);
	}

	info = g_slice_new (DeviceMenuItemInfo);
	info->applet = cb_data->applet;
	info->device = cb_data->device;
	info->ap = ap;

	g_signal_connect_data (gtk_item, "activate",
					   G_CALLBACK (nma_menu_item_activate),
					   info,
					   (GClosureNotify) device_menu_item_info_destroy, 0);

	gtk_widget_show (GTK_WIDGET (gtk_item));
}


/*
 * nma_has_encrypted_networks_helper
 *
 */
static void
nma_has_encrypted_networks_helper (gpointer data, gpointer user_data)
{
	NMAccessPoint *ap = NM_ACCESS_POINT (data);
	gboolean *has_encrypted = user_data;
	guint32 capabilities;

	capabilities = nm_access_point_get_capabilities (ap);
	if ((capabilities & NM_802_11_CAP_PROTO_WEP)
	    || (capabilities & NM_802_11_CAP_PROTO_WPA)
	    || (capabilities & NM_802_11_CAP_PROTO_WPA2))
		*has_encrypted = TRUE;
}


static gint
sort_wireless_networks (gconstpointer tmpa,
                        gconstpointer tmpb)
{
	NMAccessPoint * a = NM_ACCESS_POINT (tmpa);
	NMAccessPoint * b = NM_ACCESS_POINT (tmpb);
	GByteArray * a_ssid;
	GByteArray * b_ssid;
	int a_mode, b_mode, cmp;

	if (a && !b)
		return 1;
	if (b && !a)
		return -1;

	a_ssid = nm_access_point_get_ssid (a);
	b_ssid = nm_access_point_get_ssid (b);

	if (a_ssid && !b_ssid)
		return 1;
	if (b_ssid && !a_ssid)
		return -1;

	cmp = strncasecmp (a_ssid->data, b_ssid->data, MIN(a_ssid->len, b_ssid->len));
	if (cmp)
		return cmp;

	if (a_ssid->len > b_ssid->len)
		return 1;
	if (b_ssid->len > a_ssid->len)
		return -1;

	a_mode = nm_access_point_get_mode (a);
	b_mode = nm_access_point_get_mode (b);
	if (a_mode != b_mode) {
		if (a_mode == IW_MODE_INFRA)
			return 1;
		return -1;
	}
}

/*
 * nma_menu_device_add_networks
 *
 */
static void
nma_menu_device_add_networks (GtkWidget *menu, NMDevice *device, NMApplet *applet)
{
	GSList *networks;
	gboolean has_encrypted = FALSE;
	AddNetworksCB add_networks_cb;

	if (!NM_IS_DEVICE_802_11_WIRELESS (device) || !nm_client_wireless_get_enabled (applet->nm_client))
		return;

	networks = nm_device_802_11_wireless_get_networks (NM_DEVICE_802_11_WIRELESS (device));

	/* Check for any security */
	g_slist_foreach (networks, nma_has_encrypted_networks_helper, &has_encrypted);

	add_networks_cb.applet = applet;
	add_networks_cb.device = device;
	add_networks_cb.active_ssid = NULL;
	add_networks_cb.has_encrypted = has_encrypted;
	add_networks_cb.menu = menu;

	if (nm_device_get_state (device) == NM_DEVICE_STATE_ACTIVATED) {
		NMAccessPoint *ap;

		ap = nm_device_802_11_wireless_get_active_network (NM_DEVICE_802_11_WIRELESS (device));
		if (ap)
			add_networks_cb.active_ssid = nm_access_point_get_ssid (ap);
	}

	/* Add all networks in our network list to the menu */
	networks = g_slist_sort (networks, sort_wireless_networks);
	g_slist_foreach (networks, nma_add_networks_helper, &add_networks_cb);
	g_slist_free (networks);

	if (add_networks_cb.active_ssid)
		g_byte_array_free (add_networks_cb.active_ssid, TRUE);
}

static gint
sort_vpn_connections (gconstpointer tmpa,
                      gconstpointer tmpb)
{
	NMVPNConnection * a = NM_VPN_CONNECTION (tmpa);
	NMVPNConnection * b = NM_VPN_CONNECTION (tmpb);

	if (a && !b)
		return 1;
	if (b && !a)
		return -1;
	return strcmp (nm_vpn_connection_get_name (a),
	               nm_vpn_connection_get_name (b));
}

static void nma_menu_add_vpn_menu (GtkWidget *menu, NMApplet *applet)
{
	GtkMenuItem	*item;
	GtkMenu		*vpn_menu;
	GtkMenuItem	*other_item;
	GSList		*vpn_connections;
	GSList		*elt;
	NMVPNConnection *active_vpn;

	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_VPN Connections")));

	vpn_menu = GTK_MENU (gtk_menu_new ());
	vpn_connections = nm_client_get_vpn_connections (applet->nm_client);
	vpn_connections = g_slist_sort (vpn_connections, sort_vpn_connections);
	active_vpn = nma_get_first_active_vpn_connection (applet);

	for (elt = vpn_connections; elt; elt = g_slist_next (elt))
	{
		GtkCheckMenuItem	*vpn_item;
		NMVPNConnection	*vpn = NM_VPN_CONNECTION (elt->data);
		const char		*vpn_name = nm_vpn_connection_get_name (vpn);

		vpn_item = GTK_CHECK_MENU_ITEM (gtk_check_menu_item_new_with_label (vpn_name));
		/* temporarily do this until we support multiple VPN connections */
		gtk_check_menu_item_set_draw_as_radio (vpn_item, TRUE);

		g_object_set_data_full (G_OBJECT (vpn_item), "vpn",
						    g_object_ref (vpn),
						    (GDestroyNotify) g_object_unref);

		/* FIXME: all VPN items except the active one are disabled,
		 * due to a bug in the VPN handling code in NM.  See commit to
		 * src/vpn-manager/nm-vpn-service.c on 2006-02-28 by dcbw for
		 * more details.
		 */
		if (active_vpn)
		{
			if (active_vpn == vpn)
				gtk_check_menu_item_set_active (vpn_item, TRUE);
			else
				gtk_widget_set_sensitive (GTK_WIDGET (vpn_item), FALSE);
		}

		if (nm_client_get_state (applet->nm_client) != NM_STATE_CONNECTED)
			gtk_widget_set_sensitive (GTK_WIDGET (vpn_item), FALSE);

		g_signal_connect (G_OBJECT (vpn_item), "activate", G_CALLBACK (nma_menu_vpn_item_activate), applet);
		gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (vpn_item));
	}

	/* Draw a seperator, but only if we have VPN connections above it */
	if (vpn_connections) {
		other_item = GTK_MENU_ITEM (gtk_separator_menu_item_new ());
		gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (other_item));
	}

	other_item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Configure VPN...")));
	g_signal_connect (G_OBJECT (other_item), "activate", G_CALLBACK (nma_menu_configure_vpn_item_activate), applet);
	gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (other_item));

	other_item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Disconnect VPN...")));
	g_signal_connect (G_OBJECT (other_item), "activate", G_CALLBACK (nma_menu_disconnect_vpn_item_activate), applet);
	if (!active_vpn)
		gtk_widget_set_sensitive (GTK_WIDGET (other_item), FALSE);
	gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (other_item));

	gtk_menu_item_set_submenu (item, GTK_WIDGET (vpn_menu));

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (item));
	gtk_widget_show_all (GTK_WIDGET (item));

	g_slist_free (vpn_connections);
}


/** Returns TRUE if, and only if, we have VPN support installed
 *
 *  Algorithm: just check whether any files exist in the directory
 *  /etc/NetworkManager/VPN
 */
static gboolean is_vpn_available (void)
{
	GDir *dir;
	gboolean result;

	result = FALSE;
	if ((dir = g_dir_open (VPN_NAME_FILES_DIR, 0, NULL)) != NULL) {
		if (g_dir_read_name (dir) != NULL)
			result = TRUE;
		g_dir_close (dir);
	}

	return result;
}

gint
sort_devices (gconstpointer a, gconstpointer b)
{
	NMDevice *aa = NM_DEVICE (a);
	NMDevice *bb = NM_DEVICE (b);
	GType aa_type;
	GType bb_type;

	aa_type = G_OBJECT_TYPE (G_OBJECT (aa));
	bb_type = G_OBJECT_TYPE (G_OBJECT (bb));

	if (aa_type == bb_type) {
		char *aa_desc = nm_device_get_description (aa);
		char *bb_desc = nm_device_get_description (bb);
		gint ret;

		ret = strcmp (aa_desc, bb_desc);

		g_free (aa_desc);
		g_free (bb_desc);

		return ret;
	}

	if (aa_type == NM_TYPE_DEVICE_802_3_ETHERNET && bb_type == NM_TYPE_DEVICE_802_11_WIRELESS)
		return -1;
	if (aa_type == NM_TYPE_DEVICE_802_11_WIRELESS && bb_type == NM_TYPE_DEVICE_802_3_ETHERNET)
		return 1;

	return 0;
}

static void
nma_menu_add_devices (GtkWidget *menu, NMApplet *applet)
{
	GSList *devices;
	GSList *iter;
	gint n_wireless_interfaces = 0;
	gint n_wired_interfaces = 0;

	if (nm_client_get_state (applet->nm_client) == NM_STATE_ASLEEP) {
		nma_menu_add_text_item (menu, _("Networking disabled"));
		return;
	}

	devices = nm_client_get_devices (applet->nm_client);

	if (devices)
		devices = g_slist_sort (devices, sort_devices);

	for (iter = devices; iter; iter = iter->next) {
		NMDevice *device = NM_DEVICE (iter->data);

		/* Ignore unsupported devices */
		if (!(nm_device_get_capabilities (device) & NM_DEVICE_CAP_NM_SUPPORTED))
			continue;

		if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
			if (nm_client_wireless_get_enabled (applet->nm_client))
				n_wireless_interfaces++;
		} else if (NM_IS_DEVICE_802_3_ETHERNET (device))
			n_wired_interfaces++;
	}

	if (n_wired_interfaces == 0 && n_wireless_interfaces == 0) {
		nma_menu_add_text_item (menu, _("No network devices have been found"));
		goto out;
	}

	/* Add all devices in our device list to the menu */
	for (iter = devices; iter; iter = iter->next) {
		NMDevice *device = NM_DEVICE (iter->data);
		gint n_devices = 0;

		/* Ignore unsupported devices */
		if (!(nm_device_get_capabilities (device) & NM_DEVICE_CAP_NM_SUPPORTED))
			continue;

		if (NM_IS_DEVICE_802_11_WIRELESS (device))
			n_devices = n_wireless_interfaces;
		else if (NM_IS_DEVICE_802_3_ETHERNET (device))
			n_devices = n_wired_interfaces++;

		nma_menu_add_device_item (menu, device, n_devices, applet);
		nma_menu_device_add_networks (menu, device, applet);
	}

	/* Add the VPN and Dial Up menus and their associated seperator */
	if (is_vpn_available ()) {
		nma_menu_add_separator_item (GTK_MENU_SHELL (menu));
		nma_menu_add_vpn_menu (menu, applet);
	}

	if (n_wireless_interfaces > 0 && nm_client_wireless_get_enabled (applet->nm_client)) {
		/* Add the "Other wireless network..." entry */
		nma_menu_add_separator_item (GTK_MENU_SHELL (menu));
		nma_menu_add_custom_essid_item (menu, applet);
		nma_menu_add_create_network_item (menu, applet);
	}

 out:
	g_slist_free (devices);
}


static void
nma_set_wireless_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_wireless_set_enabled (applet->nm_client, state);
}


static void
nma_set_networking_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_sleep (applet->nm_client, !state);
}


/*
 * nma_menu_item_data_free
 *
 * Frees the "network" data tag on a menu item we've created
 *
 */
static void nma_menu_item_data_free (GtkWidget *menu_item, gpointer data)
{
	char	*tag;
	GtkMenu	*menu;

	g_return_if_fail (menu_item != NULL);
	g_return_if_fail (data != NULL);

	if ((tag = g_object_get_data (G_OBJECT (menu_item), "network")))
	{
		g_object_set_data (G_OBJECT (menu_item), "network", NULL);
		g_free (tag);
	}

	if ((tag = g_object_get_data (G_OBJECT (menu_item), "device")))
	{
		g_object_set_data (G_OBJECT (menu_item), "device", NULL);
		g_free (tag);
	}

	if ((tag = g_object_get_data (G_OBJECT (menu_item), "disconnect")))
	{
		g_object_set_data (G_OBJECT (menu_item), "disconnect", NULL);
		g_free (tag);
	}

	if ((menu = GTK_MENU (gtk_menu_item_get_submenu (GTK_MENU_ITEM (menu_item)))))
		gtk_container_foreach (GTK_CONTAINER (menu), nma_menu_item_data_free, menu);

	gtk_widget_destroy (menu_item);
}


/*
 * nma_dispose_menu_items
 *
 * Destroy the menu and each of its items data tags
 *
 */
static void nma_dropdown_menu_clear (GtkWidget *menu)
{
	g_return_if_fail (menu != NULL);

	/* Free the "network" data on each menu item, and destroy the item */
	gtk_container_foreach (GTK_CONTAINER (menu),
					   (GtkCallback) gtk_object_destroy,
					   NULL);
}


/*
 * nma_dropdown_menu_populate
 *
 * Set up our networks menu from scratch
 *
 */
static void nma_dropdown_menu_populate (GtkWidget *menu, NMApplet *applet)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);

	if (!applet->nm_running)
		nma_menu_add_text_item (menu, _("NetworkManager is not running..."));
	else
		nma_menu_add_devices (menu, applet);
}


/*
 * nma_dropdown_menu_show_cb
 *
 * Pop up the wireless networks menu
 *
 */
static void nma_dropdown_menu_show_cb (GtkWidget *menu, NMApplet *applet)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);

#ifdef HAVE_STATUS_ICON
	gtk_status_icon_set_tooltip (applet->status_icon, NULL);
#else
	gtk_tooltips_set_tip (applet->tooltips, applet->event_box, NULL, NULL);
#endif /* HAVE_STATUS_ICON */

	if (applet->dropdown_menu && (menu == applet->dropdown_menu))
	{
		nma_dropdown_menu_clear (applet->dropdown_menu);
		nma_dropdown_menu_populate (applet->dropdown_menu, applet);
		gtk_widget_show_all (applet->dropdown_menu);
	}

	nmi_dbus_signal_user_interface_activated (applet->connection);
}

/*
 * nma_dropdown_menu_create
 *
 * Create the applet's dropdown menu
 *
 */
static GtkWidget *
nma_dropdown_menu_create (GtkMenuItem *parent, NMApplet *applet)
{
	GtkWidget	*menu;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	menu = gtk_menu_new ();
	gtk_container_set_border_width (GTK_CONTAINER (menu), 0);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (parent), menu);
	g_signal_connect (menu, "show", G_CALLBACK (nma_dropdown_menu_show_cb), applet);

	return menu;
}


/*
 * nma_context_menu_update
 *
 */
static void
nma_context_menu_update (NMApplet *applet)
{
	NMState state;
	gboolean have_wireless = FALSE;

	state = nm_client_get_state (applet->nm_client);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->enable_networking_item),
							  state != NM_STATE_ASLEEP);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->stop_wireless_item),
							  nm_client_wireless_get_enabled (applet->nm_client));

	gtk_widget_set_sensitive (applet->info_menu_item,
						 state == NM_STATE_CONNECTED);

	if (state != NM_STATE_ASLEEP) {
		GSList *list;
		GSList *iter;
	
		list = nm_client_get_devices (applet->nm_client);
		for (iter = list; iter; iter = iter->next) {
			if (NM_IS_DEVICE_802_11_WIRELESS (iter->data)) {
				have_wireless = TRUE;
				break;
			}
		}
		g_slist_free (list);
	}

	if (have_wireless)
		gtk_widget_show_all (applet->stop_wireless_item);
	else
		gtk_widget_hide (applet->stop_wireless_item);
}


/*
 * nma_set_running
 *
 * Set whether NM is running to TRUE or FALSE.
 *
 */
void nma_set_running (NMApplet *applet, gboolean running)
{
	if (running == applet->nm_running)
		return;

	applet->nm_running = running;
}


/*
 * nma_context_menu_create
 *
 * Generate the contextual popup menu.
 *
 */
static GtkWidget *nma_context_menu_create (NMApplet *applet)
{
	GtkMenuShell *menu;
	GtkWidget	*menu_item;
	GtkWidget *image;

	g_return_val_if_fail (applet != NULL, NULL);

	menu = GTK_MENU_SHELL (gtk_menu_new ());

	/* 'Enable Networking' item */
	applet->enable_networking_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Networking"));
	g_signal_connect (applet->enable_networking_item,
				   "toggled",
				   G_CALLBACK (nma_set_networking_enabled_cb),
				   applet);
	gtk_menu_shell_append (menu, applet->enable_networking_item);

	/* 'Enable Wireless' item */
	applet->stop_wireless_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Wireless"));
	g_signal_connect (applet->stop_wireless_item,
				   "toggled",
				   G_CALLBACK (nma_set_wireless_enabled_cb),
				   applet);
	gtk_menu_shell_append (menu, applet->stop_wireless_item);

	/* 'Connection Information' item */
	applet->info_menu_item = gtk_image_menu_item_new_with_mnemonic (_("Connection _Information"));
	g_signal_connect (applet->info_menu_item,
				   "activate",
				   G_CALLBACK (nma_show_info_cb),
				   applet);
	image = gtk_image_new_from_stock (GTK_STOCK_INFO, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->info_menu_item), image);
	gtk_menu_shell_append (menu, applet->info_menu_item);

	/* Separator */
	nma_menu_add_separator_item (menu);

#if 0	/* FIXME: Implement the help callback, nma_help_cb()! */
	/* Help item */
	menu_item = gtk_image_menu_item_new_with_mnemonic (_("_Help"));
	g_signal_connect (menu_item, "activate", G_CALLBACK (nma_help_cb), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_HELP, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (menu, menu_item);
	gtk_widget_set_sensitive (menu_item, FALSE);
#endif

	/* About item */
	menu_item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	g_signal_connect (menu_item, "activate", G_CALLBACK (nma_about_cb), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (menu, menu_item);

	gtk_widget_show_all (GTK_WIDGET (menu));

	return GTK_WIDGET (menu);
}


#ifdef HAVE_STATUS_ICON

/*
 * nma_status_icon_screen_changed_cb:
 *
 * Handle screen change events for the status icon
 *
 */
static void nma_status_icon_screen_changed_cb (GtkStatusIcon *icon, GParamSpec *pspec, NMApplet *applet)
{
	nma_icons_init (applet);
}

/*
 * nma_status_icon_size_changed_cb:
 *
 * Handle size change events for the status icon
 *
 */
static gboolean nma_status_icon_size_changed_cb (GtkStatusIcon *icon, gint size, NMApplet *applet)
{
	nma_icons_free (applet);

	applet->size = size;
	nma_icons_load_from_disk (applet);

	return TRUE;
}

/*
 * nma_status_icon_activate_cb:
 *
 * Handle left clicks for the status icon
 *
 */
static void nma_status_icon_activate_cb (GtkStatusIcon *icon, NMApplet *applet)
{
	gtk_menu_popup (GTK_MENU (applet->dropdown_menu), NULL, NULL,
			gtk_status_icon_position_menu, icon,
			1, gtk_get_current_event_time ());
}

static void nma_status_icon_popup_menu_cb (GtkStatusIcon *icon, guint button, guint32 activate_time, NMApplet *applet)
{
	nma_context_menu_update (applet);
	gtk_menu_popup (GTK_MENU (applet->context_menu), NULL, NULL,
			gtk_status_icon_position_menu, icon,
			button, activate_time);
}

/*
 * nma_status_icon_popup_menu_cb:
 *
 * Handle right clicks for the status icon
 *
 */

#else /* !HAVE_STATUS_ICON */

/*
 * nma_menu_position_func
 *
 * Position main dropdown menu, adapted from netapplet
 *
 */
static void nma_menu_position_func (GtkMenu *menu G_GNUC_UNUSED, int *x, int *y, gboolean *push_in, gpointer user_data)
{
	int screen_w, screen_h, button_x, button_y, panel_w, panel_h;
	GtkRequisition requisition;
	GdkScreen *screen;
	NMApplet *applet = (NMApplet *)user_data;

	screen = gtk_widget_get_screen (applet->event_box);
	screen_w = gdk_screen_get_width (screen);
	screen_h = gdk_screen_get_height (screen);

	gdk_window_get_origin (applet->event_box->window, &button_x, &button_y);
	gtk_window_get_size (GTK_WINDOW (gtk_widget_get_toplevel (applet->event_box)), &panel_w, &panel_h);

	*x = button_x;

	/* Check to see if we would be placing the menu off of the end of the screen. */
	gtk_widget_size_request (GTK_WIDGET (menu), &requisition);
	if (button_y + panel_h + requisition.height >= screen_h)
		*y = button_y - requisition.height;
	else
		*y = button_y + panel_h;

	*push_in = TRUE;
}

/*
 * nma_toplevel_menu_button_press_cb
 *
 * Handle left/right-clicks for the dropdown and context popup menus
 *
 */
static gboolean nma_toplevel_menu_button_press_cb (GtkWidget *widget, GdkEventButton *event, NMApplet *applet)
{
	g_return_val_if_fail (applet != NULL, FALSE);

	switch (event->button)
	{
		case 1:
			gtk_widget_set_state (applet->event_box, GTK_STATE_SELECTED);
			gtk_menu_popup (GTK_MENU (applet->dropdown_menu), NULL, NULL, nma_menu_position_func, applet, event->button, event->time);
			return TRUE;
		case 3:
			nma_context_menu_update (applet);
			gtk_menu_popup (GTK_MENU (applet->context_menu), NULL, NULL, nma_menu_position_func, applet, event->button, event->time);
			return TRUE;
		default:
			g_signal_stop_emission_by_name (widget, "button_press_event");
			return FALSE;
	}

	return FALSE;
}


/*
 * nma_toplevel_menu_button_press_cb
 *
 * Handle left-unclick on the dropdown menu.
 *
 */
static void nma_dropdown_menu_deactivate_cb (GtkWidget *menu, NMApplet *applet)
{

	g_return_if_fail (applet != NULL);

	gtk_widget_set_state (applet->event_box, GTK_STATE_NORMAL);
}

/*
 * nma_theme_change_cb
 *
 * Destroy the popdown menu when the theme changes
 *
 */
static void nma_theme_change_cb (NMApplet *applet)
{
	g_return_if_fail (applet != NULL);

	if (applet->dropdown_menu)
		nma_dropdown_menu_clear (applet->dropdown_menu);

	if (applet->top_menu_item)
	{
		gtk_menu_item_remove_submenu (GTK_MENU_ITEM (applet->top_menu_item));
		applet->dropdown_menu = nma_dropdown_menu_create (GTK_MENU_ITEM (applet->top_menu_item), applet);
		g_signal_connect (applet->dropdown_menu, "deactivate", G_CALLBACK (nma_dropdown_menu_deactivate_cb), applet);
	}
}
#endif /* HAVE_STATUS_ICON */

/*
 * nma_setup_widgets
 *
 * Intialize the applet's widgets and packing, create the initial
 * menu of networks.
 *
 */
static void nma_setup_widgets (NMApplet *applet)
{
#ifdef HAVE_STATUS_ICON
	applet->status_icon = gtk_status_icon_new ();

	g_signal_connect (applet->status_icon, "notify::screen",
			  G_CALLBACK (nma_status_icon_screen_changed_cb), applet);
	g_signal_connect (applet->status_icon, "size-changed",
			  G_CALLBACK (nma_status_icon_size_changed_cb), applet);
	g_signal_connect (applet->status_icon, "activate",
			  G_CALLBACK (nma_status_icon_activate_cb), applet);
	g_signal_connect (applet->status_icon, "popup-menu",
			  G_CALLBACK (nma_status_icon_popup_menu_cb), applet);

#else
	applet->tray_icon = egg_tray_icon_new ("NetworkManager");
	g_object_ref (applet->tray_icon);
	gtk_object_sink (GTK_OBJECT (applet->tray_icon));

	/* Event box is the main applet widget */
	applet->event_box = gtk_event_box_new ();
	gtk_container_set_border_width (GTK_CONTAINER (applet->event_box), 0);
	g_signal_connect (applet->event_box, "button_press_event", G_CALLBACK (nma_toplevel_menu_button_press_cb), applet);

	applet->pixmap = gtk_image_new ();
	gtk_container_add (GTK_CONTAINER (applet->event_box), applet->pixmap);
	gtk_container_add (GTK_CONTAINER (applet->tray_icon), applet->event_box);
 	gtk_widget_show_all (GTK_WIDGET (applet->tray_icon));

	gtk_widget_show_all (GTK_WIDGET (applet->tray_icon));

#endif /* HAVE_STATUS_ICON */

	applet->top_menu_item = gtk_menu_item_new ();
	gtk_widget_set_name (applet->top_menu_item, "ToplevelMenu");
	gtk_container_set_border_width (GTK_CONTAINER (applet->top_menu_item), 0);

	applet->dropdown_menu = nma_dropdown_menu_create (GTK_MENU_ITEM (applet->top_menu_item), applet);
#ifndef HAVE_STATUS_ICON
	g_signal_connect (applet->dropdown_menu, "deactivate", G_CALLBACK (nma_dropdown_menu_deactivate_cb), applet);
#endif /* !HAVE_STATUS_ICON */

	applet->context_menu = nma_context_menu_create (applet);
	applet->encryption_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}


/*
 * nma_gconf_info_notify_callback
 *
 * Callback from gconf when wireless key/values have changed.
 *
 */
static void nma_gconf_info_notify_callback (GConfClient *client, guint connection_id, GConfEntry *entry, gpointer user_data)
{
	NMApplet *	applet = (NMApplet *)user_data;
	const char *		key = NULL;

	g_return_if_fail (client != NULL);
	g_return_if_fail (entry != NULL);
	g_return_if_fail (applet != NULL);

	if ((key = gconf_entry_get_key (entry)))
	{
		int	net_path_len = strlen (GCONF_PATH_WIRELESS_NETWORKS) + 1;

		if (strncmp (GCONF_PATH_WIRELESS_NETWORKS"/", key, net_path_len) == 0)
		{
			char 	*network = g_strdup ((key + net_path_len));
			char		*slash_pos;
			char		*unescaped_network;

			/* If its a key under the network name, zero out the slash so we
			 * are left with only the network name.
			 */
			unescaped_network = gconf_unescape_key (network, strlen (network));
			if ((slash_pos = strchr (unescaped_network, '/')))
				*slash_pos = '\0';

			nmi_dbus_signal_update_network (applet->connection, unescaped_network, NETWORK_TYPE_ALLOWED);
			g_free (unescaped_network);
			g_free (network);
		}
	}
}


/*
 * nma_gconf_vpn_connections_notify_callback
 *
 * Callback from gconf when VPN connection values have changed.
 *
 */
static void nma_gconf_vpn_connections_notify_callback (GConfClient *client, guint connection_id, GConfEntry *entry, gpointer user_data)
{
	NMApplet *	applet = (NMApplet *)user_data;
	const char *		key = NULL;

	/*g_debug ("Entering nma_gconf_vpn_connections_notify_callback, key='%s'", gconf_entry_get_key (entry));*/

	g_return_if_fail (client != NULL);
	g_return_if_fail (entry != NULL);
	g_return_if_fail (applet != NULL);

	if ((key = gconf_entry_get_key (entry)))
	{
		int	path_len = strlen (GCONF_PATH_VPN_CONNECTIONS) + 1;

		if (strncmp (GCONF_PATH_VPN_CONNECTIONS"/", key, path_len) == 0)
		{
			char 	 *name = g_strdup ((key + path_len));
			char		 *slash_pos;
			char	 	 *unescaped_name;
			char       *name_path;
			GConfValue *value;

			/* If its a key under the the VPN name, zero out the slash so we
			 * are left with only the VPN name.
			 */
			if ((slash_pos = strchr (name, '/')))
				*slash_pos = '\0';
			unescaped_name = gconf_unescape_key (name, strlen (name));

			/* Check here if the name entry is gone so we can remove the conn from the UI */
			name_path = g_strdup_printf ("%s/%s/name", GCONF_PATH_VPN_CONNECTIONS, name);
			gconf_client_clear_cache (client);
			value = gconf_client_get (client, name_path, NULL);
			if (value == NULL) {
				NMVPNConnection *connection;

				/*g_debug ("removing '%s' from UI", name_path);*/

				connection = nm_client_get_vpn_connection_by_name (applet->nm_client, unescaped_name);
				if (connection)
					nm_client_remove_vpn_connection (applet->nm_client, connection);
			} else {
				gconf_value_free (value);
			}
			g_free (name_path);

			nmi_dbus_signal_update_vpn_connection (applet->connection, unescaped_name);

			g_free (unescaped_name);
			g_free (name);
		}

	}
}

/*****************************************************************************/

static void
foo_update_icon (NMApplet *applet)
{
	GdkPixbuf	*pixbuf;
	GtkRequisition requisition;
	int i;

	if (!applet->icon_layers[0]) {
		pixbuf = g_object_ref (applet->no_connection_icon);
	} else {
		pixbuf = gdk_pixbuf_copy (applet->icon_layers[0]);

		for (i = ICON_LAYER_LINK + 1; i <= ICON_LAYER_MAX; i++) {
			GdkPixbuf *top = applet->icon_layers[i];

			if (!top)
				continue;

			gdk_pixbuf_composite (top, pixbuf, 0, 0, gdk_pixbuf_get_width (top),
							  gdk_pixbuf_get_height (top),
							  0, 0, 1.0, 1.0,
							  GDK_INTERP_NEAREST, 255);
		}
	}

	gtk_status_icon_set_from_pixbuf (applet->status_icon, pixbuf);
	g_object_unref (pixbuf);

	/* Add some padding to the applet to ensure the
	 * highlight has some space.
	 */
/* 	gtk_widget_set_size_request (GTK_WIDGET (applet), -1, -1); */
/* 	gtk_widget_size_request (GTK_WIDGET (applet), &requisition); */
/* 	gtk_widget_set_size_request (GTK_WIDGET (applet), requisition.width + 6, requisition.height + 2); */
}

static void
foo_set_icon (NMApplet *applet, GdkPixbuf *pixbuf, guint32 layer)
{
	if (layer > ICON_LAYER_MAX) {
		g_warning ("Tried to icon to invalid layer %d", layer);
		return;
	}

	if (applet->icon_layers[layer]) {
		g_object_unref (applet->icon_layers[layer]);
		applet->icon_layers[layer] = NULL;
	}

	if (pixbuf)
		applet->icon_layers[layer] = g_object_ref (pixbuf);

	foo_update_icon (applet);
}

/* Device independent code to set the status icon and tooltip */

typedef struct {
	NMApplet *applet;
	NMDeviceState state;
} FooAnimationTimeoutInfo;

static void
foo_animation_timeout_info_destroy (gpointer data)
{
	g_slice_free (FooAnimationTimeoutInfo, data);
}

static gboolean
foo_animation_timeout (gpointer data)
{
	FooAnimationTimeoutInfo *info = (FooAnimationTimeoutInfo *) data;
	NMApplet *applet = info->applet;
	int stage = -1;

	switch (info->state) {
	case NM_DEVICE_STATE_PREPARE:
		stage = 0;
		break;
	case NM_DEVICE_STATE_CONFIG:
		stage = 1;
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		stage = 2;
		break;
	default:
		break;
	}

	if (stage >= 0)
		foo_set_icon (applet,
				    applet->network_connecting_icons[stage][applet->animation_step],
				    ICON_LAYER_LINK);

	applet->animation_step++;
	if (applet->animation_step >= NUM_CONNECTING_FRAMES)
		applet->animation_step = 0;

	return TRUE;
}

static void
foo_common_state_change (NMDevice *device, NMDeviceState state, NMApplet *applet)
{
	FooAnimationTimeoutInfo *info;

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
	case NM_DEVICE_STATE_IP_CONFIG:
		info = g_slice_new (FooAnimationTimeoutInfo);
		info->applet = applet;
		info->state = state;
		applet->animation_step = 0;
		applet->animation_id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
										   100, foo_animation_timeout,
										   info,
										   foo_animation_timeout_info_destroy);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		break;
	default:
		break;
	}
}

/* Wireless device */

static void
foo_bssid_strength_changed (NMAccessPoint *ap, guint strength, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GdkPixbuf *pixbuf;
	GByteArray * ssid;
	char *tip;

	strength = CLAMP (strength, 0, 100);

	if (strength > 80)
		pixbuf = applet->wireless_100_icon;
	else if (strength > 55)
		pixbuf = applet->wireless_75_icon;
	else if (strength > 30)
		pixbuf = applet->wireless_50_icon;
	else if (strength > 5)
		pixbuf = applet->wireless_25_icon;
	else
		pixbuf = applet->wireless_00_icon;

	foo_set_icon (applet, pixbuf, ICON_LAYER_LINK);

	ssid = nm_access_point_get_ssid (ap);
	tip = g_strdup_printf (_("Wireless network connection to '%s' (%d%%)"),
	                       ssid ? nma_escape_ssid (ssid->data, ssid->len) : "(none)",
	                       strength);
	g_byte_array_free (ssid, TRUE);

	gtk_status_icon_set_tooltip (applet->status_icon, tip);
	g_free (tip);
}

static gboolean
foo_wireless_state_change (NMDevice80211Wireless *device, NMDeviceState state, NMApplet *applet)
{
	char *iface;
	NMAccessPoint *ap;
	GByteArray * ssid = NULL;
	char *tip = NULL;
	gboolean handled = FALSE;
	char * esc_ssid = "(none)";

	iface = nm_device_get_iface (NM_DEVICE (device));

	if (state == NM_DEVICE_STATE_PREPARE ||
	    state == NM_DEVICE_STATE_CONFIG ||
	    state == NM_DEVICE_STATE_IP_CONFIG ||
	    state == NM_DEVICE_STATE_NEED_AUTH ||
	    state == NM_DEVICE_STATE_ACTIVATED) {

		ap = nm_device_802_11_wireless_get_active_network (NM_DEVICE_802_11_WIRELESS (device));
		ssid = nm_access_point_get_ssid (ap);
		if (ssid) {
			esc_ssid = (char *) nma_escape_ssid (ssid->data, ssid->len);
			g_byte_array_free (ssid, TRUE);
		}
	}

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		tip = g_strdup_printf (_("Preparing device %s for the wireless network '%s'..."), iface, esc_ssid);
		break;
	case NM_DEVICE_STATE_CONFIG:
		tip = g_strdup_printf (_("Attempting to join the wireless network '%s'..."), esc_ssid);
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		tip = g_strdup_printf (_("Requesting a network address from the wireless network '%s'..."), esc_ssid);
		break;
	case NM_DEVICE_STATE_NEED_AUTH:
		tip = g_strdup_printf (_("Waiting for Network Key for the wireless network '%s'..."), esc_ssid);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		applet->current_ap = ap;
		applet->wireless_strength_monitor = g_signal_connect (ap, "strength-changed",
												    G_CALLBACK (foo_bssid_strength_changed),
												    applet);
		foo_bssid_strength_changed (ap, nm_access_point_get_strength (ap), applet);

#ifdef ENABLE_NOTIFY
		tip = g_strdup_printf (_("You are now connected to the wireless network '%s'."), esc_ssid);
		nma_send_event_notification (applet, NOTIFY_URGENCY_LOW, _("Connection Established"),
							    tip, "nm-device-wireless");
		g_free (tip);
#endif

		tip = g_strdup_printf (_("Wireless network connection to '%s'"), esc_ssid);

		handled = TRUE;
		break;
	case NM_DEVICE_STATE_DOWN:
	case NM_DEVICE_STATE_DISCONNECTED:
		if (applet->current_ap && applet->wireless_strength_monitor)
			g_signal_handler_disconnect (applet->current_ap, applet->wireless_strength_monitor);

		applet->current_ap = NULL;
		applet->wireless_strength_monitor = 0;
		break;
	default:
		break;
	}

	g_free (iface);

	if (tip) {
		gtk_status_icon_set_tooltip (applet->status_icon, tip);
		g_free (tip);
	}

	return handled;
}

/* Wired device */

static gboolean
foo_wired_state_change (NMDevice8023Ethernet *device, NMDeviceState state, NMApplet *applet)
{
	char *iface;
	char *tip = NULL;
	gboolean handled = FALSE;

	iface = nm_device_get_iface (NM_DEVICE (device));

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		tip = g_strdup_printf (_("Preparing device %s for the wired network..."), iface);
		break;
	case NM_DEVICE_STATE_CONFIG:
		tip = g_strdup_printf (_("Configuring device %s for the wired network..."), iface);
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		tip = g_strdup_printf (_("Requesting a network address from the wired network..."));
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		foo_set_icon (applet, applet->wired_icon, ICON_LAYER_LINK);
		tip = g_strdup (_("Wired network connection"));

#ifdef ENABLE_NOTIFY		
		nma_send_event_notification (applet, NOTIFY_URGENCY_LOW,
							    _("Connection Established"),
							    _("You are now connected to the wired network."),
							    "nm-device-wired");
#endif

		handled = TRUE;
		break;
	default:
		break;
	}

	g_free (iface);

	if (tip) {
		gtk_status_icon_set_tooltip (applet->status_icon, tip);
		g_free (tip);
	}

	return handled;
}

static void
foo_device_state_changed (NMDevice *device, NMDeviceState state, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	gboolean handled = FALSE;

	applet->animation_step = 0;
	if (applet->animation_id) {
		g_source_remove (applet->animation_id);
		applet->animation_id = 0;
	}

	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		handled = foo_wired_state_change (NM_DEVICE_802_3_ETHERNET (device), state, applet);
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		handled = foo_wireless_state_change (NM_DEVICE_802_11_WIRELESS (device), state, applet);

	if (!handled)
		foo_common_state_change (device, state, applet);
}

static void
foo_device_added_cb (NMClient *client, NMDevice *device, gpointer user_data)
{
	g_signal_connect (device, "state-changed",
				   G_CALLBACK (foo_device_state_changed),
				   user_data);

	foo_device_state_changed (device, nm_device_get_state (device), user_data);
}

static void
foo_add_initial_devices (gpointer data, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	foo_device_added_cb (applet->nm_client, NM_DEVICE (data), applet);
}

static void
foo_client_state_change (NMClient *client, NMState state, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GdkPixbuf *pixbuf = NULL;
	char *tip = NULL;

	switch (state) {
	case NM_STATE_UNKNOWN:
		break;
	case NM_STATE_ASLEEP:
		pixbuf = applet->no_connection_icon;
		tip = g_strdup (_("Networking disabled"));
		break;
	case NM_STATE_DISCONNECTED:
		pixbuf = applet->no_connection_icon;
		tip = g_strdup (_("No network connection"));

#ifdef ENABLE_NOTIFY
		nma_send_event_notification (applet, NOTIFY_URGENCY_NORMAL, _("Disconnected"),
							    _("The network connection has been disconnected."),
							    "nm-no-connection");
#endif

		break;
	default:
		break;
	}

	if (pixbuf)
		foo_set_icon (applet, pixbuf, ICON_LAYER_LINK);

	if (tip) {
		gtk_status_icon_set_tooltip (applet->status_icon, tip);
		g_free (tip);
	}

	/* Update VPN icon too to ensure that the VPN icon doesn't
	 * race with the main icon for layer 1
	 */
	foo_client_vpn_state_change (client, nm_client_get_vpn_state (client), applet);
}

static void
foo_setup_client_state_handlers (NMClient *client, NMApplet *applet)
{
	g_signal_connect (client, "state-change",
				   G_CALLBACK (foo_client_state_change),
				   applet);

	g_signal_connect (client, "device-added",
				   G_CALLBACK (foo_device_added_cb),
				   applet);
}

/* VPN */

static gboolean
foo_vpn_animation_timeout (gpointer data)
{
	NMApplet *applet = NM_APPLET (data);

	foo_set_icon (applet, applet->vpn_connecting_icons[applet->animation_step], ICON_LAYER_VPN);

	applet->animation_step++;
	if (applet->animation_step >= NUM_VPN_CONNECTING_FRAMES)
		applet->animation_step = 0;

	return TRUE;
}

static void
foo_client_vpn_state_change (NMClient *client,
                             NMVPNConnectionState state,
                             gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	char *tip = NULL;

	switch (state) {
	case NM_VPN_CONNECTION_STATE_ACTIVATED:
		if (applet->animation_id) {
			g_source_remove (applet->animation_id);
			applet->animation_id = 0;
		}
		foo_set_icon (applet, applet->vpn_lock_icon, ICON_LAYER_VPN);
		break;
	case NM_VPN_CONNECTION_STATE_PREPARE:
	case NM_VPN_CONNECTION_STATE_CONNECT:
	case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
		if (applet->animation_id == 0) {
			applet->animation_step = 0;
			applet->animation_id = g_timeout_add (100, foo_vpn_animation_timeout, applet);
		}
		break;
	default:
		if (applet->animation_id) {
			g_source_remove (applet->animation_id);
			applet->animation_id = 0;
		}
		foo_set_icon (applet, NULL, ICON_LAYER_VPN);
		break;
	}
}

static void
foo_setup_client_vpn_state_handlers (NMClient *client, NMApplet *applet)
{
	g_signal_connect (client, "vpn-state-change",
				   G_CALLBACK (foo_client_vpn_state_change),
				   applet);
}


static void
foo_manager_running (NMClient *client,
				 gboolean running,
				 gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	gtk_status_icon_set_visible (applet->status_icon, running);

	if (running) {
		g_message ("NM appeared");

		/* Force the icon update */
		foo_client_state_change (client, nm_client_get_state (client), applet);
		foo_client_vpn_state_change (client, nm_client_get_vpn_state (client), applet);
	} else {
		g_message ("NM disappeared");

		foo_client_state_change (client, NM_STATE_UNKNOWN, applet);
		foo_client_vpn_state_change (client, NM_VPN_CONNECTION_STATE_UNKNOWN, applet);
	}
}

static gboolean
foo_set_initial_state (gpointer data)
{
	NMApplet *applet = NM_APPLET (data);
	GSList *list;

	foo_manager_running (applet->nm_client, TRUE, applet);

	list = nm_client_get_devices (applet->nm_client);
	if (list) {
		g_slist_foreach (list, foo_add_initial_devices, applet);
		g_slist_free (list);
	}

	return FALSE;
}

static void
foo_client_setup (NMApplet *applet)
{
	NMClient *client;

	client = nm_client_new ();
	if (!client)
		return;

	applet->nm_client = client;

	foo_setup_client_state_handlers (client, applet);
	foo_setup_client_vpn_state_handlers (client, applet);
	g_signal_connect (client, "manager-running",
				   G_CALLBACK (foo_manager_running), applet);

	if (nm_client_manager_is_running (client))
		g_idle_add (foo_set_initial_state, applet);
}

/*****************************************************************************/

/*
 * nma_finalize
 *
 * Destroy the applet and clean up its data
 *
 */
static void nma_finalize (GObject *object)
{
	NMApplet *applet = NM_APPLET (object);

	if (applet->dropdown_menu)
		nma_dropdown_menu_clear (applet->dropdown_menu);
	if (applet->top_menu_item)
		gtk_menu_item_remove_submenu (GTK_MENU_ITEM (applet->top_menu_item));

	nma_icons_free (applet);

	nmi_passphrase_dialog_destroy (applet);
#ifdef ENABLE_NOTIFY
	if (applet->notification)
	{
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}
#endif

	nma_set_running (applet, FALSE);
	if (applet->connection_timeout_id) {
		g_source_remove (applet->connection_timeout_id);
		applet->connection_timeout_id = 0;
	}

	g_free (applet->glade_file);

	gconf_client_notify_remove (applet->gconf_client, applet->gconf_prefs_notify_id);
	gconf_client_notify_remove (applet->gconf_client, applet->gconf_vpn_notify_id);
	g_object_unref (applet->gconf_client);

	dbus_method_dispatcher_unref (applet->nmi_methods);

#ifdef HAVE_STATUS_ICON
	g_object_unref (applet->status_icon);
#else
	gtk_widget_destroy (GTK_WIDGET (applet->tray_icon));
	g_object_unref (applet->tray_icon);
#endif /* HAVE_STATUS_ICON */

	g_object_unref (applet->nm_client);

	G_OBJECT_CLASS (nma_parent_class)->finalize (object);
}


static GObject *nma_constructor (GType type, guint n_props, GObjectConstructParam *construct_props)
{
	GObject *obj;
	NMApplet *applet;

	obj = G_OBJECT_CLASS (nma_parent_class)->constructor (type, n_props, construct_props);
	applet =  NM_APPLET (obj);

#ifndef HAVE_STATUS_ICON
	applet->tooltips = gtk_tooltips_new ();
#endif

	applet->glade_file = g_build_filename (GLADEDIR, "applet.glade", NULL);
	if (!applet->glade_file || !g_file_test (applet->glade_file, G_FILE_TEST_IS_REGULAR))
	{
		nma_schedule_warning_dialog (applet, _("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		g_free (applet->glade_file);
		applet->glade_file = NULL;
		return NULL; // FIXMEchpe
	}

	applet->info_dialog_xml = glade_xml_new (applet->glade_file, "info_dialog", NULL);

	applet->gconf_client = gconf_client_get_default ();

	gconf_client_add_dir (applet->gconf_client, GCONF_PATH_WIRELESS, GCONF_CLIENT_PRELOAD_NONE, NULL);
	applet->gconf_prefs_notify_id = gconf_client_notify_add (applet->gconf_client, GCONF_PATH_WIRELESS,
						nma_gconf_info_notify_callback, applet, NULL, NULL);

	gconf_client_add_dir (applet->gconf_client, GCONF_PATH_VPN_CONNECTIONS, GCONF_CLIENT_PRELOAD_NONE, NULL);
	applet->gconf_vpn_notify_id = gconf_client_notify_add (applet->gconf_client, GCONF_PATH_VPN_CONNECTIONS,
						nma_gconf_vpn_connections_notify_callback, applet, NULL, NULL);

	/* Convert old-format stored network entries to the new format.
	 * Must be RUN BEFORE DBUS INITIALIZATION since we have to do
	 * synchronous calls against gnome-keyring.
	 */
	nma_compat_convert_oldformat_entries (applet->gconf_client);

	/* Load pixmaps and create applet widgets */
	nma_setup_widgets (applet);
	nma_icons_init (applet);
	
	foo_client_setup (applet);

	/* D-Bus init stuff */
	applet->nmi_methods = nmi_dbus_nmi_methods_setup ();
	nma_dbus_init_helper (applet);
	if (!applet->connection)
		nma_start_dbus_connection_watch (applet);

#ifndef HAVE_STATUS_ICON
	g_signal_connect (applet->tray_icon, "style-set", G_CALLBACK (nma_theme_change_cb), NULL);

	nma_icons_load_from_disk (applet);
#endif /* !HAVE_STATUS_ICON */

	return obj;
}


static void nma_icons_free (NMApplet *applet)
{
	int i;

	if (!applet->icons_loaded)
		return;

	for (i = 0; i <= ICON_LAYER_MAX; i++) {
		if (applet->icon_layers[i])
			g_object_unref (applet->icon_layers[i]);
	}

	if (applet->no_connection_icon)
		g_object_unref (applet->no_connection_icon);
	if (applet->wired_icon)
		g_object_unref (applet->wired_icon);
	if (applet->adhoc_icon)
		g_object_unref (applet->adhoc_icon);
	if (applet->vpn_lock_icon)
		g_object_unref (applet->vpn_lock_icon);

	if (applet->wireless_00_icon)
		g_object_unref (applet->wireless_00_icon);
	if (applet->wireless_25_icon)
		g_object_unref (applet->wireless_25_icon);
	if (applet->wireless_50_icon)
		g_object_unref (applet->wireless_50_icon);
	if (applet->wireless_75_icon)
		g_object_unref (applet->wireless_75_icon);
	if (applet->wireless_100_icon)
		g_object_unref (applet->wireless_100_icon);

	for (i = 0; i < NUM_CONNECTING_STAGES; i++) {
		int j;

		for (j = 0; j < NUM_CONNECTING_FRAMES; j++)
			if (applet->network_connecting_icons[i][j])
				g_object_unref (applet->network_connecting_icons[i][j]);
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++)
		if (applet->vpn_connecting_icons[i])
			g_object_unref (applet->vpn_connecting_icons[i]);

	nma_icons_zero (applet);
}

static void nma_icons_zero (NMApplet *applet)
{
	int i;

	applet->no_connection_icon = NULL;
	applet->wired_icon = NULL;
	applet->adhoc_icon = NULL;
	applet->vpn_lock_icon = NULL;

	applet->wireless_00_icon = NULL;
	applet->wireless_25_icon = NULL;
	applet->wireless_50_icon = NULL;
	applet->wireless_75_icon = NULL;
	applet->wireless_100_icon = NULL;

	for (i = 0; i < NUM_CONNECTING_STAGES; i++)
	{
		int j;

		for (j = 0; j < NUM_CONNECTING_FRAMES; j++)
			applet->network_connecting_icons[i][j] = NULL;
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++)
		applet->vpn_connecting_icons[i] = NULL;

	applet->icons_loaded = FALSE;
}

#define ICON_LOAD(x, y)	\
	{		\
		GError *err = NULL; \
		x = gtk_icon_theme_load_icon (applet->icon_theme, y, size, 0, &err); \
		if (x == NULL) { \
			success = FALSE; \
			g_warning ("Icon %s missing: %s", y, err->message); \
			g_error_free (err); \
			goto out; \
		} \
	}

static gboolean
nma_icons_load_from_disk (NMApplet *applet)
{
	int 		size, i;
	gboolean	success;

	/*
	 * NULL out the icons, so if we error and call nma_icons_free(), we don't hit stale
	 * data on the not-yet-reached icons.  This can happen off nma_icon_theme_changed().
	 */

	g_return_val_if_fail (!applet->icons_loaded, FALSE);

#ifdef HAVE_STATUS_ICON
	size = applet->size;
	if (size < 0)
		return FALSE;
#else
	size = 22; /* hard-coded */
#endif /* HAVE_STATUS_ICON */

	for (i = 0; i <= ICON_LAYER_MAX; i++)
		applet->icon_layers[i] = NULL;

	ICON_LOAD(applet->no_connection_icon, "nm-no-connection");
	ICON_LOAD(applet->wired_icon, "nm-device-wired");
	ICON_LOAD(applet->adhoc_icon, "nm-adhoc");
	ICON_LOAD(applet->vpn_lock_icon, "nm-vpn-lock");

	ICON_LOAD(applet->wireless_00_icon, "nm-signal-00");
	ICON_LOAD(applet->wireless_25_icon, "nm-signal-25");
	ICON_LOAD(applet->wireless_50_icon, "nm-signal-50");
	ICON_LOAD(applet->wireless_75_icon, "nm-signal-75");
	ICON_LOAD(applet->wireless_100_icon, "nm-signal-100");

	for (i = 0; i < NUM_CONNECTING_STAGES; i++)
	{
		int j;

		for (j = 0; j < NUM_CONNECTING_FRAMES; j++)
		{
			char *name;

			name = g_strdup_printf ("nm-stage%02d-connecting%02d", i+1, j+1);
			ICON_LOAD(applet->network_connecting_icons[i][j], name);
			g_free (name);
		}
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++)
	{
		char *name;

		name = g_strdup_printf ("nm-vpn-connecting%02d", i+1);
		ICON_LOAD(applet->vpn_connecting_icons[i], name);
		g_free (name);
	}

	success = TRUE;

out:
	if (!success)
	{
		char *msg = g_strdup(_("The NetworkManager applet could not find some required resources.  It cannot continue.\n"));
		show_warning_dialog (msg);
		nma_icons_free (applet);
	}

	return success;
}

static void nma_icon_theme_changed (GtkIconTheme *icon_theme, NMApplet *applet)
{
	nma_icons_free (applet);
	nma_icons_load_from_disk (applet);
}

static void nma_icons_init (NMApplet *applet)
{
	const char style[] =
		"style \"MenuBar\"\n"
		"{\n"
			"GtkMenuBar::shadow_type = GTK_SHADOW_NONE\n"
			"GtkMenuBar::internal-padding = 0\n"
		"}\n"
		"style \"MenuItem\"\n"
		"{\n"
			"xthickness=0\n"
			"ythickness=0\n"
		"}\n"
		"class \"GtkMenuBar\" style \"MenuBar\"\n"
		"widget \"*ToplevelMenu*\" style \"MenuItem\"\n";
	GdkScreen *screen;
	gboolean path_appended;

	if (applet->icon_theme)
	{
		g_signal_handlers_disconnect_by_func (applet->icon_theme,
						      G_CALLBACK (nma_icon_theme_changed),
						      applet);
	}

#ifdef HAVE_STATUS_ICON
#if GTK_CHECK_VERSION(2, 11, 0)
	screen = gtk_status_icon_get_screen (applet->status_icon);
#else
	screen = gdk_screen_get_default ();
#endif /* gtk 2.11.0 */
#else /* !HAVE_STATUS_ICON */
	screen = gtk_widget_get_screen (GTK_WIDGET (applet->tray_icon));
#endif /* HAVE_STATUS_ICON */
	
	applet->icon_theme = gtk_icon_theme_get_for_screen (screen);

	/* If not done yet, append our search path */
	path_appended = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (applet->icon_theme),
					 		    "NMAIconPathAppended"));
	if (path_appended == FALSE)
	{
		gtk_icon_theme_append_search_path (applet->icon_theme, ICONDIR);
		g_object_set_data (G_OBJECT (applet->icon_theme),
				   "NMAIconPathAppended",
				   GINT_TO_POINTER (TRUE));
	}

	g_signal_connect (applet->icon_theme, "changed", G_CALLBACK (nma_icon_theme_changed), applet);

	/* FIXME: Do we need to worry about other screens? */
	gtk_rc_parse_string (style);
}

NMApplet *nma_new ()
{
	return g_object_new (NM_TYPE_APPLET, NULL);
}
