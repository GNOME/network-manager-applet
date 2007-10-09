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

#include <time.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <iwlib.h>
#include <wireless.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <nm-device-802-3-ethernet.h>
#include <nm-device-802-11-wireless.h>
#include <nm-utils.h>

#include <glade/glade.h>
#include <gconf/gconf-client.h>

#ifdef ENABLE_NOTIFY
#include <libnotify/notify.h>
#endif

#include "applet.h"
#include "menu-items.h"
#include "vpn-password-dialog.h"
#include "nm-utils.h"
#include "gnome-keyring-md5.h"
#include "applet-dbus-manager.h"

#include "nm-connection.h"
#include "vpn-connection-info.h"
#include "connection-editor/nm-connection-list.h"

/* Compat for GTK 2.6 */
#if (GTK_MAJOR_VERSION <= 2 && GTK_MINOR_VERSION == 6)
	#define GTK_STOCK_INFO			GTK_STOCK_DIALOG_INFO
#endif

static GObject * nma_constructor (GType type, guint n_props, GObjectConstructParam *construct_props);
static void      nma_icons_init (NMApplet *applet);
static void      nma_icons_free (NMApplet *applet);
static void      nma_icons_zero (NMApplet *applet);
static gboolean  nma_icons_load_from_disk (NMApplet *applet);
static void      nma_finalize (GObject *object);
static void      foo_set_icon (NMApplet *applet, GdkPixbuf *pixbuf, guint32 layer);
static void		 foo_update_icon (NMApplet *applet);
static void      foo_device_state_changed (NMDevice *device, NMDeviceState state, gpointer user_data, gboolean synthetic);
static void      foo_device_state_changed_cb (NMDevice *device, NMDeviceState state, gpointer user_data);
static void      foo_manager_running (NMClient *client, gboolean running, gpointer user_data, gboolean synthetic);
static void      foo_manager_running_cb (NMClient *client, gboolean running, gpointer user_data);
static void      foo_client_state_change (NMClient *client, NMState state, gpointer user_data, gboolean synthetic);
static gboolean  add_seen_bssid (AppletDbusConnectionSettings *connection, NMAccessPoint *ap);

static GtkWidget *
nma_menu_create (GtkMenuItem *parent, NMApplet *applet);

G_DEFINE_TYPE(NMApplet, nma, G_TYPE_OBJECT)

static NMDevice *
get_first_active_device (NMApplet *applet)
{
	GSList *iter;
	NMDevice *dev = NULL;

	if (!applet->active_connections)
		return NULL;

	for (iter = applet->active_connections; iter; iter = g_slist_next (iter)) {
		NMClientActiveConnection * act_con = (NMClientActiveConnection *) iter->data;

		if (act_con->devices)
			return g_slist_nth_data (act_con->devices, 0);
	}

	return dev;
}

static void nma_init (NMApplet *applet)
{
	applet->animation_id = 0;
	applet->animation_step = 0;
	applet->passphrase_dialog = NULL;
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

static void
nma_edit_connections_cb (GtkMenuItem *mi, NMApplet *applet)
{
	NMConnectionList *connection_list;

	connection_list = nm_connection_list_new ();
	nm_connection_list_run_and_close (connection_list);

	g_object_unref (connection_list);
}

static void about_dialog_activate_link_cb (GtkAboutDialog *about,
                                           const gchar *url,
                                           gpointer data)
{
	GError *error = NULL;
	gboolean ret;
	char *cmdline;
	GdkScreen *gscreen;
	GtkWidget *error_dialog;

	gscreen = gdk_screen_get_default();

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

static gboolean
match_cipher (const char * cipher,
              const char * expected,
              guint32 wpa_flags,
              guint32 rsn_flags,
              guint32 flag)
{
	if (strcmp (cipher, expected) != 0)
		return FALSE;

	if (!(wpa_flags & flag) && !(rsn_flags & flag))
		return FALSE;

	return TRUE;
}

static gboolean
security_compatible (NMAccessPoint *ap,
                     NMConnection *connection,
                     NMSettingWireless *s_wireless)
{
	NMSettingWirelessSecurity *s_wireless_sec;
	int mode;
	guint32 flags = nm_access_point_get_flags (ap);
	guint32 wpa_flags = nm_access_point_get_wpa_flags (ap);
	guint32 rsn_flags = nm_access_point_get_rsn_flags (ap);
	
	if (!s_wireless->security) {
		if (   (flags & NM_802_11_AP_FLAGS_PRIVACY)
		    || (wpa_flags != NM_802_11_AP_SEC_NONE)
		    || (rsn_flags != NM_802_11_AP_SEC_NONE))
			return FALSE;
		return TRUE;
	}

	if (strcmp (s_wireless->security, "802-11-wireless-security") != 0)
		return FALSE;

	s_wireless_sec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection, "802-11-wireless-security");
	if (s_wireless_sec == NULL || !s_wireless_sec->key_mgmt)
		return FALSE;

	/* Static WEP */
	if (!strcmp (s_wireless_sec->key_mgmt, "none")) {
		if (   !(flags & NM_802_11_AP_FLAGS_PRIVACY)
		    || (wpa_flags != NM_802_11_AP_SEC_NONE)
		    || (rsn_flags != NM_802_11_AP_SEC_NONE))
			return FALSE;
		return TRUE;
	}

	/* Adhoc WPA */
	mode = nm_access_point_get_mode (ap);
	if (!strcmp (s_wireless_sec->key_mgmt, "wpa-none")) {
		if (mode != IW_MODE_ADHOC)
			return FALSE;
		// FIXME: validate ciphers if the BSSID actually puts WPA/RSN IE in
		// it's beacon
		return TRUE;
	}

	/* Stuff after this point requires infrastructure */
	if (mode != IW_MODE_INFRA)
		return FALSE;

	/* Dynamic WEP or LEAP/Network EAP */
	if (!strcmp (s_wireless_sec->key_mgmt, "ieee8021x")) {
		// FIXME: should we allow APs that advertise WPA/RSN support here?
		if (   !(flags & NM_802_11_AP_FLAGS_PRIVACY)
		    || (wpa_flags != NM_802_11_AP_SEC_NONE)
		    || (rsn_flags != NM_802_11_AP_SEC_NONE))
			return FALSE;
		return TRUE;
	}

	/* WPA[2]-PSK */
	if (!strcmp (s_wireless_sec->key_mgmt, "wpa-psk")) {
		GSList * elt;
		gboolean found = FALSE;

		if (!s_wireless_sec->pairwise || !s_wireless_sec->group)
			return FALSE;

		if (   !(wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK)
		    && !(rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK))
			return FALSE;

		// FIXME: should handle WPA and RSN separately here to ensure that
		// if the Connection only uses WPA we don't match a cipher against
		// the AP's RSN IE instead

		/* Match at least one pairwise cipher with AP's capability */
		for (elt = s_wireless_sec->pairwise; elt; elt = g_slist_next (elt)) {
			if ((found = match_cipher (elt->data, "tkip", wpa_flags, rsn_flags, NM_802_11_AP_SEC_PAIR_TKIP)))
				break;
			if ((found = match_cipher (elt->data, "ccmp", wpa_flags, rsn_flags, NM_802_11_AP_SEC_PAIR_CCMP)))
				break;
		}
		if (!found)
			return FALSE;

		/* Match at least one group cipher with AP's capability */
		for (elt = s_wireless_sec->group; elt; elt = g_slist_next (elt)) {
			if ((found = match_cipher (elt->data, "wep40", wpa_flags, rsn_flags, NM_802_11_AP_SEC_GROUP_WEP40)))
				break;
			if ((found = match_cipher (elt->data, "wep104", wpa_flags, rsn_flags, NM_802_11_AP_SEC_GROUP_WEP104)))
				break;
			if ((found = match_cipher (elt->data, "tkip", wpa_flags, rsn_flags, NM_802_11_AP_SEC_GROUP_TKIP)))
				break;
			if ((found = match_cipher (elt->data, "ccmp", wpa_flags, rsn_flags, NM_802_11_AP_SEC_GROUP_CCMP)))
				break;
		}
		if (!found)
			return FALSE;

		return TRUE;
	}

	if (!strcmp (s_wireless_sec->key_mgmt, "wpa-eap")) {
		// FIXME: implement
	}

	return FALSE;
}

static gboolean
nm_ap_check_compatible (NMAccessPoint *ap,
                        NMConnection *connection)
{
	NMSettingWireless *s_wireless;
	const GByteArray *ssid;
	int mode;
	guint32 freq;

	g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, "802-11-wireless");
	if (s_wireless == NULL)
		return FALSE;
	
	ssid = nm_access_point_get_ssid (ap);
	if (!nm_utils_same_ssid (s_wireless->ssid, ssid, TRUE))
		return FALSE;

	// FIXME: BSSID check

	mode = nm_access_point_get_mode (ap);
	if (s_wireless->mode) {
		if (   !strcmp (s_wireless->mode, "infrastructure")
		    && (mode != IW_MODE_INFRA))
			return FALSE;
		if (   !strcmp (s_wireless->mode, "adhoc")
		    && (mode != IW_MODE_ADHOC))
			return FALSE;
	}

	freq = nm_access_point_get_frequency (ap);
	if (s_wireless->band) {
		if (!strcmp (s_wireless->band, "a")) {
			if (freq < 5170 || freq > 5825)
				return FALSE;
		} else if (!strcmp (s_wireless->band, "bg")) {
			if (freq < 2412 || freq > 2472)
				return FALSE;
		}
	}

	// FIXME: channel check

	return security_compatible (ap, connection, s_wireless);
}

static GSList *
add_ciphers_from_flags (guint32 flags, gboolean pairwise)
{
	GSList *ciphers = NULL;

	if (pairwise) {
		if (flags & NM_802_11_AP_SEC_PAIR_TKIP)
			ciphers = g_slist_append (ciphers, g_strdup ("tkip"));
		if (flags & NM_802_11_AP_SEC_PAIR_CCMP)
			ciphers = g_slist_append (ciphers, g_strdup ("ccmp"));
	} else {
		if (flags & NM_802_11_AP_SEC_GROUP_WEP40)
			ciphers = g_slist_append (ciphers, g_strdup ("wep40"));
		if (flags & NM_802_11_AP_SEC_GROUP_WEP104)
			ciphers = g_slist_append (ciphers, g_strdup ("wep104"));
		if (flags & NM_802_11_AP_SEC_GROUP_TKIP)
			ciphers = g_slist_append (ciphers, g_strdup ("tkip"));
		if (flags & NM_802_11_AP_SEC_GROUP_CCMP)
			ciphers = g_slist_append (ciphers, g_strdup ("ccmp"));
	}

	return ciphers;
}

static NMSettingWirelessSecurity *
get_security_for_ap (NMAccessPoint *ap, gboolean *supported)
{
	NMSettingWirelessSecurity *sec;
	int mode;
	guint32 flags;
	guint32 wpa_flags;
	guint32 rsn_flags;

	g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), NULL);
	g_return_val_if_fail (supported != NULL, NULL);
	g_return_val_if_fail (*supported == TRUE, NULL);

	sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();

	mode = nm_access_point_get_mode (ap);
	flags = nm_access_point_get_flags (ap);
	wpa_flags = nm_access_point_get_wpa_flags (ap);
	rsn_flags = nm_access_point_get_rsn_flags (ap);

	/* No security */
	if (   !(flags & NM_802_11_AP_FLAGS_PRIVACY)
	    && (wpa_flags == NM_802_11_AP_SEC_NONE)
	    && (rsn_flags == NM_802_11_AP_SEC_NONE))
		goto none;

	if (   (flags & NM_802_11_AP_FLAGS_PRIVACY)
	    && (wpa_flags == NM_802_11_AP_SEC_NONE)
	    && (rsn_flags == NM_802_11_AP_SEC_NONE)) {
		sec->key_mgmt = g_strdup ("none");
		sec->wep_tx_keyidx = 0;
		return sec;
	}

	/* Stuff after this point requires infrastructure */
	if (mode != IW_MODE_INFRA) {
		*supported = FALSE;
		goto none;
	}

	/* WPA2 PSK first */
	if (rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK) {
		sec->key_mgmt = g_strdup ("wpa-psk");
		sec->proto = g_strdup ("rsn");
		sec->pairwise = add_ciphers_from_flags (rsn_flags, FALSE);
		sec->group = add_ciphers_from_flags (rsn_flags, TRUE);
		return sec;
	}

	/* WPA PSK */
	if (wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK) {
		sec->key_mgmt = g_strdup ("wpa-psk");
		sec->proto = g_strdup ("wpa");
		sec->pairwise = add_ciphers_from_flags (wpa_flags, FALSE);
		sec->group = add_ciphers_from_flags (wpa_flags, TRUE);
		return sec;
	}

	*supported = FALSE;

none:
	nm_setting_destroy ((NMSetting *) sec);
	return NULL;
}

static gboolean
find_connection (NMConnectionSettings *applet_connection,
                 NMDevice *device,
                 NMAccessPoint *ap)
{
	NMConnection *connection;
	NMSettingConnection *s_con;

	connection = applet_dbus_connection_settings_get_connection (applet_connection);

	s_con = (NMSettingConnection *) nm_connection_get_setting (connection, "connection");
	if (!s_con)
		return FALSE;

	if (NM_IS_DEVICE_802_3_ETHERNET (device)) {
		if (strcmp (s_con->type, "802-3-ethernet"))
			return FALSE;
	} else if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		NMSettingWireless *s_wireless;
		const GByteArray *ap_ssid;

		if (strcmp (s_con->type, "802-11-wireless"))
			return FALSE;

		s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, "802-11-wireless");
		if (!s_wireless)
			return FALSE;

		ap_ssid = nm_access_point_get_ssid (ap);
		if (!nm_utils_same_ssid (s_wireless->ssid, ap_ssid, TRUE))
			return FALSE;

		if (!nm_ap_check_compatible (ap, connection))
			return FALSE;
	}

	return TRUE;
}

static void
activate_device_cb (gpointer user_data, GError *err)
{
	if (err) {
		nm_warning ("Device Activation failed: %s", err->message);
	}
}


/* This is a controlled list.  Want to add to it?  Stop.  Ask first. */
static const char * default_ssid_list[] =
{
	"linksys",
	"linksys-a",
	"linksys-g",
	"default",
	"belkin54g",
	"NETGEAR",
	NULL
};

static gboolean
is_manufacturer_default_ssid (const GByteArray *ssid)
{
	const char **default_ssid = default_ssid_list;

	while (*default_ssid) {
		if (ssid->len == strlen (*default_ssid)) {
			if (!memcmp (*default_ssid, ssid->data, ssid->len))
				return TRUE;
		}
		default_ssid++;
	}
	return FALSE;
}

static NMSetting *
new_auto_wireless_setting (NMConnection *connection,
                           char **connection_name,
                           gboolean *autoconnect,
                           NMDevice80211Wireless *device,
                           NMAccessPoint *ap)
{
	NMSettingWireless *s_wireless = NULL;
	NMSettingWirelessSecurity *s_wireless_sec = NULL;
	const GByteArray *ap_ssid;
	char buf[33];
	int buf_len;
	int mode;
	gboolean supported = TRUE;

	s_wireless = (NMSettingWireless *) nm_setting_wireless_new ();

	ap_ssid = nm_access_point_get_ssid (ap);
	s_wireless->ssid = g_byte_array_sized_new (ap_ssid->len);
	g_byte_array_append (s_wireless->ssid, ap_ssid->data, ap_ssid->len);

	/* Only default to autoconnect for APs that don't use the manufacturer
	 * default SSID.
	 */
	*autoconnect = !is_manufacturer_default_ssid (ap_ssid);

	memset (buf, 0, sizeof (buf));
	buf_len = MIN(ap_ssid->len, sizeof (buf));
	memcpy (buf, ap_ssid->data, buf_len);
	*connection_name = g_strdup_printf ("Auto %s", nm_utils_ssid_to_utf8 (buf, buf_len));

	mode = nm_access_point_get_mode (ap);
	if (mode == IW_MODE_ADHOC)
		s_wireless->mode = g_strdup ("adhoc");
	else if (mode == IW_MODE_INFRA)
		s_wireless->mode = g_strdup ("infrastructure");
	else
		g_assert_not_reached ();

	s_wireless_sec = get_security_for_ap (ap, &supported);
	if (!supported) {
		// FIXME: support WPA/WPA2 Enterprise and Dynamic WEP
		nm_setting_destroy ((NMSetting *) s_wireless);
		s_wireless = NULL;
	} else if (s_wireless_sec) {
		s_wireless->security = g_strdup (NM_SETTING_WIRELESS_SECURITY);
		nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);
	}

	return (NMSetting *) s_wireless;
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
	NMConnection *connection = NULL;
	char *con_path = NULL;
	NMSetting *setting = NULL;
	char *specific_object = "/";
	char *connection_name = NULL;
	GSList *elt, *connections;
	gboolean autoconnect = TRUE;

	connections = applet_dbus_settings_list_connections (APPLET_DBUS_SETTINGS (info->applet->settings));
	for (elt = connections; elt; elt = g_slist_next (elt)) {
		NMConnectionSettings *applet_connection = NM_CONNECTION_SETTINGS (elt->data);

		if (find_connection (applet_connection, info->device, info->ap)) {
			connection = applet_dbus_connection_settings_get_connection (applet_connection);
			con_path = (char *) nm_connection_settings_get_dbus_object_path (applet_connection);
			break;
		}
	}

	if (connection) {
		NMSettingConnection *s_con;
		s_con = (NMSettingConnection *) nm_connection_get_setting (connection, "connection");
		g_warning ("Found connection %s to activate at %s.", s_con->name, con_path);
	} else {
		connection = nm_connection_new ();

		if (NM_IS_DEVICE_802_3_ETHERNET (info->device)) {
			setting = nm_setting_wired_new ();
			specific_object = NULL;
			connection_name = g_strdup ("Auto Ethernet");
		} else if (NM_IS_DEVICE_802_11_WIRELESS (info->device)) {
			setting = new_auto_wireless_setting (connection,
			                                     &connection_name,
			                                     &autoconnect,
			                                     NM_DEVICE_802_11_WIRELESS (info->device),
			                                     info->ap);
		} else
			g_warning ("Unhandled device type '%s'", G_OBJECT_CLASS_NAME (info->device));

		if (setting) {
			AppletDbusConnectionSettings *exported_con;
			NMSettingConnection *s_con;

			s_con = (NMSettingConnection *) nm_setting_connection_new ();
			s_con->name = connection_name;
			s_con->type = g_strdup (setting->name);
			s_con->autoconnect = autoconnect;
			nm_connection_add_setting (connection, (NMSetting *) s_con);

			nm_connection_add_setting (connection, setting);

			exported_con = applet_dbus_settings_add_connection (APPLET_DBUS_SETTINGS (info->applet->settings),
			                                                    connection);
			if (exported_con) {
				con_path = (char *) nm_connection_settings_get_dbus_object_path (NM_CONNECTION_SETTINGS (exported_con));
			} else {
				nm_warning ("Couldn't create default connection.");
			}
		} else {
			g_object_unref (connection);
			connection = NULL;
		}
	}

	if (connection) {
		if (NM_IS_DEVICE_802_11_WIRELESS (info->device))
			specific_object = (char *) nm_object_get_path (NM_OBJECT (info->ap));

		nm_client_activate_device (info->applet->nm_client,
							  info->device,
							  NM_DBUS_SERVICE_USER_SETTINGS,
							  con_path,
							  (const char *) specific_object,
							  activate_device_cb,
							  info);
	}

//	nmi_dbus_signal_user_interface_activated (info->applet->connection);
}

#if ENABLE_NOTIFY
static void
nma_send_event_notification (NMApplet *applet, 
                              NotifyUrgency urgency,
                              const char *summary,
                              const char *message,
                              const char *icon)
{
	const char *notify_icon;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (summary != NULL);
	g_return_if_fail (message != NULL);

	if (!notify_is_initted ())
		notify_init ("NetworkManager");

	if (applet->notification != NULL) {
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}

	notify_icon = icon ? icon : GTK_STOCK_NETWORK;

#ifdef HAVE_STATUS_ICON
	applet->notification = notify_notification_new_with_status_icon (summary, message, notify_icon, applet->status_icon);
#else
	applet->notification = notify_notification_new (summary, message, notify_icon, GTK_WIDGET (applet->tray_icon));
#endif /* HAVE_STATUS_ICON */

	notify_notification_set_urgency (applet->notification, urgency);
	notify_notification_show (applet->notification, NULL);
}
#else
static void
nma_show_notification_dialog (const char *title,
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

static void
show_vpn_state (NMApplet *applet,
                NMVPNConnection *connection,
                NMVPNConnectionState state,
                NMVPNConnectionStateReason reason)
{
	const char *banner;
	char *title, *msg;

	switch (state) {
	case NM_VPN_CONNECTION_STATE_ACTIVATED:
		banner = nm_vpn_connection_get_banner (connection);
		if (banner && strlen (banner)) {
			title = _("VPN Login Message");
#ifdef ENABLE_NOTIFY
			msg = g_strdup_printf ("\n%s", banner);
			nma_send_event_notification (applet, NOTIFY_URGENCY_LOW,
								    title, msg, "gnome-lockscreen");
#else
			msg = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
							   title, banner);
			nma_show_notification_dialog (title, msg);
#endif
			g_free (msg);
		}
		break;
	default:
		break;
	}
}

static gboolean
vpn_animation_timeout (gpointer data)
{
	NMApplet *applet = NM_APPLET (data);

	foo_set_icon (applet, applet->vpn_connecting_icons[applet->animation_step], ICON_LAYER_VPN);

	applet->animation_step++;
	if (applet->animation_step >= NUM_VPN_CONNECTING_FRAMES)
		applet->animation_step = 0;

	return TRUE;
}

static void
vpn_connection_state_changed (NMVPNConnection *connection,
                              NMVPNConnectionState state,
                              NMVPNConnectionStateReason reason,
                              gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	switch (state) {
	case NM_VPN_CONNECTION_STATE_ACTIVATED:
		if (applet->animation_id) {
			g_source_remove (applet->animation_id);
			applet->animation_id = 0;
		}
		foo_set_icon (applet, applet->vpn_lock_icon, ICON_LAYER_VPN);
//		vpn_connection_info_set_last_attempt_success (info, TRUE);
		break;
	case NM_VPN_CONNECTION_STATE_PREPARE:
	case NM_VPN_CONNECTION_STATE_NEED_AUTH:
	case NM_VPN_CONNECTION_STATE_CONNECT:
	case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
		if (applet->animation_id == 0) {
			applet->animation_step = 0;
			applet->animation_id = g_timeout_add (100, vpn_animation_timeout, applet);
		}
		break;
	case NM_VPN_CONNECTION_STATE_FAILED:
//		vpn_connection_info_set_last_attempt_success (info, FALSE);
		/* Fall through */
	case NM_VPN_CONNECTION_STATE_DISCONNECTED:
		g_hash_table_remove (applet->vpn_connections, nm_vpn_connection_get_name (connection));
		/* Fall through */
	default:
		if (applet->animation_id) {
			g_source_remove (applet->animation_id);
			applet->animation_id = 0;
		}
		foo_set_icon (applet, NULL, ICON_LAYER_VPN);
		break;
	}

	show_vpn_state (applet, connection, state, reason);
}

static const char *
get_connection_name (AppletDbusConnectionSettings *settings)
{
	NMSettingConnection *conn;

	conn = (NMSettingConnection *) nm_connection_get_setting (settings->connection, NM_SETTING_CONNECTION);

	return conn->name;
}

static void
add_one_vpn_connection (NMApplet *applet, NMVPNConnection *connection)
{
	g_signal_connect (connection, "state-changed",
	                  G_CALLBACK (vpn_connection_state_changed),
	                  applet);

	g_hash_table_insert (applet->vpn_connections,
	                     g_strdup (nm_vpn_connection_get_name (connection)),
	                     connection);
}

static void
nma_menu_vpn_item_clicked (GtkMenuItem *item, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMConnectionSettings *connection_settings;
	NMVPNConnection *connection;
	const char *connection_name;
	NMDevice *device;

	connection_settings = NM_CONNECTION_SETTINGS (g_object_get_data (G_OBJECT (item), "connection"));
	g_assert (connection_settings);

	connection_name = get_connection_name ((AppletDbusConnectionSettings *) connection_settings);

	connection = (NMVPNConnection *) g_hash_table_lookup (applet->vpn_connections, connection_name);
	if (connection)
		return;

	/* Connection inactive, activate */
	device = get_first_active_device (applet);
	connection = nm_vpn_manager_connect (applet->vpn_manager,
								  NM_DBUS_SERVICE_USER_SETTINGS,
								  nm_connection_settings_get_dbus_object_path (connection_settings),
								  device);
	if (connection) {
		add_one_vpn_connection (applet, connection);
	} else {
		/* FIXME: show a dialog or something */
		g_warning ("Can't connect");
	}
		
//	nmi_dbus_signal_user_interface_activated (applet->connection);
}


/*
 * nma_menu_configure_vpn_item_activate
 *
 * Signal function called when user clicks "Configure VPN..."
 *
 */
static void
nma_menu_configure_vpn_item_activate (GtkMenuItem *item, gpointer user_data)
{
	const char *argv[] = { BINDIR "/nm-vpn-properties", NULL};

	g_spawn_async (NULL, (gchar **) argv, NULL, 0, NULL, NULL, NULL, NULL);

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

static void
find_first_vpn_connection (gpointer key, gpointer value, gpointer user_data)
{
	NMVPNConnection *connection = NM_VPN_CONNECTION (value);
	NMVPNConnection **first = (NMVPNConnection **) user_data;

	if (*first)
		return;

	*first = connection;
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
	NMVPNConnection *connection = NULL;

	g_hash_table_foreach (applet->vpn_connections, find_first_vpn_connection, &connection);
	if (!connection)
		return;

	nm_vpn_connection_disconnect (connection);	

//	nmi_dbus_signal_user_interface_activated (applet->connection);
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

	if (NM_IS_DEVICE_802_11_WIRELESS (device))
		menu_item = wireless_menu_item_new (NM_DEVICE_802_11_WIRELESS (device), n_devices);
	else if (NM_IS_DEVICE_802_3_ETHERNET (device))
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


#define AP_HASH_LEN 16

static guchar *
ap_hash (NMAccessPoint * ap)
{
	struct GnomeKeyringMD5Context ctx;
	unsigned char * digest = NULL;
	unsigned char md5_data[66];
	unsigned char input[33];
	const GByteArray * ssid;
	int mode;
	guint32 flags, wpa_flags, rsn_flags;

	g_return_val_if_fail (ap, NULL);

	mode = nm_access_point_get_mode (ap);
	flags = nm_access_point_get_flags (ap);
	wpa_flags = nm_access_point_get_wpa_flags (ap);
	rsn_flags = nm_access_point_get_rsn_flags (ap);

	memset (&input[0], 0, sizeof (input));

	ssid = nm_access_point_get_ssid (ap);
	if (ssid)
		memcpy (input, ssid->data, ssid->len);

	if (mode == IW_MODE_INFRA)
		input[32] |= (1 << 0);
	else if (mode == IW_MODE_ADHOC)
		input[32] |= (1 << 1);
	else
		input[32] |= (1 << 2);

	/* Separate out no encryption, WEP-only, and WPA-capable */
	if (  !(flags & NM_802_11_AP_FLAGS_PRIVACY)
	    && (wpa_flags == NM_802_11_AP_SEC_NONE)
	    && (rsn_flags == NM_802_11_AP_SEC_NONE))
		input[32] |= (1 << 3);
	else if (   (flags & NM_802_11_AP_FLAGS_PRIVACY)
	         && (wpa_flags == NM_802_11_AP_SEC_NONE)
	         && (rsn_flags == NM_802_11_AP_SEC_NONE))
		input[32] |= (1 << 4);
	else if (   !(flags & NM_802_11_AP_FLAGS_PRIVACY)
	         &&  (wpa_flags != NM_802_11_AP_SEC_NONE)
	         &&  (rsn_flags != NM_802_11_AP_SEC_NONE))
		input[32] |= (1 << 5);
	else
		input[32] |= (1 << 6);

	digest = g_malloc (sizeof (unsigned char) * AP_HASH_LEN);
	if (digest == NULL)
		goto out;

	gnome_keyring_md5_init (&ctx);
	memcpy (md5_data, input, sizeof (input));
	memcpy (&md5_data[33], input, sizeof (input));
	gnome_keyring_md5_update (&ctx, md5_data, sizeof (md5_data));
	gnome_keyring_md5_final (digest, &ctx);

out:
	return digest;
}

struct dup_data {
	NMDevice * device;
	GtkWidget * found;
	guchar * hash;
};

static void
find_duplicate (GtkWidget * widget,
                gpointer user_data)
{
	struct dup_data * data = (struct dup_data *) user_data;
	NMDevice *device;
	const guchar * hash;
	guint32 hash_len = 0;

	g_return_if_fail (data);
	g_return_if_fail (data->hash);

	if (data->found || !NM_IS_NETWORK_MENU_ITEM (widget))
		return;

	device = g_object_get_data (G_OBJECT (widget), "device");
	if (device != data->device)
		return;

	hash = nm_network_menu_item_get_hash (NM_NETWORK_MENU_ITEM (widget),
	                                      &hash_len);
	if (hash == NULL || hash_len != AP_HASH_LEN)
		return;

	if (memcmp (hash, data->hash, AP_HASH_LEN) == 0)
		data->found = widget;
}

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	GtkWidget *menu;
	NMAccessPoint *active_ap;
} AddNetworksCB;

static void
nma_add_networks_helper (gpointer data, gpointer user_data)
{
	NMAccessPoint *ap = NM_ACCESS_POINT (data);
	AddNetworksCB *cb_data = (AddNetworksCB *) user_data;
	NMApplet *applet = cb_data->applet;
	const GByteArray *ssid;
	gint8 strength;
	struct dup_data dup_data = { NULL, NULL };
	NMNetworkMenuItem *item = NULL;

	/* Don't add BSSs that hide their SSID */
	ssid = nm_access_point_get_ssid (ap);
	if (!ssid || nm_utils_is_empty_ssid (ssid->data, ssid->len))
		goto out;

	strength = nm_access_point_get_strength (ap);

	dup_data.found = NULL;
	dup_data.hash = ap_hash (ap);
	dup_data.device = cb_data->device;
	if (!dup_data.hash)
		goto out;
	gtk_container_foreach (GTK_CONTAINER (cb_data->menu),
	                       find_duplicate,
	                       &dup_data);

	if (dup_data.found) {
		item = NM_NETWORK_MENU_ITEM (dup_data.found);

		/* Just update strength if greater than what's there */
		if (nm_network_menu_item_get_strength (item) > strength)
			nm_network_menu_item_set_strength (item, strength);

		nm_network_menu_item_add_dupe (item, ap);
	} else {
		DeviceMenuItemInfo *info;
		GtkWidget *foo;

		foo = nm_network_menu_item_new (applet->encryption_size_group,
		                                dup_data.hash, AP_HASH_LEN);
		item = NM_NETWORK_MENU_ITEM (foo);
		nm_network_menu_item_set_ssid (item, (GByteArray *) ssid);
		nm_network_menu_item_set_strength (item, strength);
		nm_network_menu_item_set_detail (item, ap, applet->adhoc_icon);
		nm_network_menu_item_add_dupe (item, ap);

		g_object_set_data (G_OBJECT (item), "device", cb_data->device);

		gtk_menu_shell_append (GTK_MENU_SHELL (cb_data->menu), GTK_WIDGET (item));

		info = g_slice_new (DeviceMenuItemInfo);
		info->applet = applet;
		info->device = cb_data->device;
		info->ap = ap;

		g_signal_connect_data (GTK_WIDGET (item),
		                       "activate",
		                       G_CALLBACK (nma_menu_item_activate),
		                       info,
		                       (GClosureNotify) device_menu_item_info_destroy,
		                       0);

		gtk_widget_show_all (GTK_WIDGET (item));
	}

	if (cb_data->active_ap) {
		g_signal_handlers_block_matched (item, G_SIGNAL_MATCH_FUNC,
		                                 0, 0, NULL, G_CALLBACK (nma_menu_item_activate), NULL);

		if (nm_network_menu_item_find_dupe (item, cb_data->active_ap))
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

		g_signal_handlers_unblock_matched (item, G_SIGNAL_MATCH_FUNC,
		                                   0, 0, NULL, G_CALLBACK (nma_menu_item_activate), NULL);
	}

out:
	g_free (dup_data.hash);
}


static gint
sort_wireless_networks (gconstpointer tmpa,
                        gconstpointer tmpb)
{
	NMAccessPoint * a = NM_ACCESS_POINT (tmpa);
	NMAccessPoint * b = NM_ACCESS_POINT (tmpb);
	const GByteArray * a_ssid;
	const GByteArray * b_ssid;
	int a_mode, b_mode, i;

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

	if (a_ssid && b_ssid) {
		/* Can't use string compares because SSIDs are byte arrays and
		 * may legally contain embedded NULLs.
		 */
		for (i = 0; i < MIN(a_ssid->len, b_ssid->len); i++) {
			if (tolower(a_ssid->data[i]) > tolower(b_ssid->data[i]))
				return 1;
			else if (tolower(b_ssid->data[i]) > tolower(a_ssid->data[i]))
				return -1;
		}

		if (a_ssid->len > b_ssid->len)
			return 1;
		if (b_ssid->len > a_ssid->len)
			return -1;
	}

	a_mode = nm_access_point_get_mode (a);
	b_mode = nm_access_point_get_mode (b);
	if (a_mode != b_mode) {
		if (a_mode == IW_MODE_INFRA)
			return 1;
		return -1;
	}

	return 0;
}

/*
 * nma_menu_device_add_networks
 *
 */
static void
nma_menu_device_add_access_points (GtkWidget *menu,
                                   NMDevice *device,
                                   NMApplet *applet)
{
	NMDevice80211Wireless *wdev;
	GSList *aps;
	AddNetworksCB info;

	if (!NM_IS_DEVICE_802_11_WIRELESS (device) || !nm_client_wireless_get_enabled (applet->nm_client))
		return;

	wdev = NM_DEVICE_802_11_WIRELESS (device);
	aps = nm_device_802_11_wireless_get_access_points (wdev);

	memset (&info, 0, sizeof (info));
	info.applet = applet;
	info.device = device;
	info.menu = menu;
	info.active_ap = nm_device_802_11_wireless_get_active_access_point (wdev);

	/* Add all networks in our network list to the menu */
	aps = g_slist_sort (aps, sort_wireless_networks);
	g_slist_foreach (aps, nma_add_networks_helper, &info);
	g_slist_free (aps);
}

static gint
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
	GSList *devices = NULL;
	GSList *iter;
	gint n_wireless_interfaces = 0;
	gint n_wired_interfaces = 0;

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
		nma_menu_device_add_access_points (menu, device, applet);
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

static int
sort_vpn_connections (gconstpointer a, gconstpointer b)
{
	return strcmp (get_connection_name ((AppletDbusConnectionSettings *) a),
				get_connection_name ((AppletDbusConnectionSettings *) b));
}

static GSList *
get_vpn_connections (NMApplet *applet)
{
	GSList *all_connections;
	GSList *iter;
	GSList *list = NULL;

	all_connections = applet_dbus_settings_list_connections (APPLET_DBUS_SETTINGS (applet->settings));

	for (iter = all_connections; iter; iter = iter->next) {
		AppletDbusConnectionSettings *applet_settings = (AppletDbusConnectionSettings *) iter->data;
		NMSettingConnection *setting;

		setting = (NMSettingConnection *) nm_connection_get_setting (applet_settings->connection,
														 NM_SETTING_CONNECTION);
		if (!strcmp (setting->type, NM_SETTING_VPN))
			list = g_slist_prepend (list, applet_settings);
	}

	return g_slist_sort (list, sort_vpn_connections);
}

static void
nma_menu_add_vpn_submenu (GtkWidget *menu, NMApplet *applet)
{
	GtkMenu *vpn_menu;
	GtkMenuItem *item;
	GSList *list;
	GSList *iter;
	int num_vpn_active = 0;

	nma_menu_add_separator_item (GTK_MENU_SHELL (menu));

	vpn_menu = GTK_MENU (gtk_menu_new ());

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_VPN Connections")));
	gtk_menu_item_set_submenu (item, GTK_WIDGET (vpn_menu));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (item));

	list = get_vpn_connections (applet);
	num_vpn_active = g_hash_table_size (applet->vpn_connections);

	for (iter = list; iter; iter = iter->next) {
		AppletDbusConnectionSettings *applet_settings = (AppletDbusConnectionSettings *) iter->data;
		const char *connection_name = get_connection_name (applet_settings);

		item = GTK_MENU_ITEM (gtk_check_menu_item_new_with_label (connection_name));
		gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);

		/* If no VPN connections are active, draw all menu items enabled. If
		 * >= 1 VPN connections are active, only the active VPN menu item is
		 * drawn enabled.
		 */
		if ((num_vpn_active == 0) || g_hash_table_lookup (applet->vpn_connections, connection_name))
			gtk_widget_set_sensitive (GTK_WIDGET (item), TRUE);
		else
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

		if (g_hash_table_lookup (applet->vpn_connections, connection_name))
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

		g_object_set_data_full (G_OBJECT (item), "connection", 
						    g_object_ref (applet_settings),
						    (GDestroyNotify) g_object_unref);

		if (nm_client_get_state (applet->nm_client) != NM_STATE_CONNECTED)
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

		g_signal_connect (item, "activate", G_CALLBACK (nma_menu_vpn_item_clicked), applet);
		gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));
	}

	/* Draw a seperator, but only if we have VPN connections above it */
	if (list)
		nma_menu_add_separator_item (GTK_MENU_SHELL (vpn_menu));

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Configure VPN...")));
	g_signal_connect (item, "activate", G_CALLBACK (nma_menu_configure_vpn_item_activate), applet);
	gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Disconnect VPN...")));
	g_signal_connect (item, "activate", G_CALLBACK (nma_menu_disconnect_vpn_item_activate), applet);
	gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));
	if (num_vpn_active == 0)
		gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
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
 * nma_menu_clear
 *
 * Destroy the menu and each of its items data tags
 *
 */
static void nma_menu_clear (NMApplet *applet)
{
	g_return_if_fail (applet != NULL);

	if (applet->menu)
		gtk_widget_destroy (applet->menu);

#if GTK_CHECK_VERSION(2, 12, 0)
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (applet->top_menu_item), NULL);
#else
	gtk_menu_item_remove_submenu (GTK_MENU_ITEM (applet->top_menu_item));
#endif /* gtk 2.12.0 */

	applet->menu = nma_menu_create (GTK_MENU_ITEM (applet->top_menu_item), applet);
#ifndef HAVE_STATUS_ICON
	g_signal_connect (applet->menu, "deactivate", G_CALLBACK (nma_menu_deactivate_cb), applet);
#endif /* !HAVE_STATUS_ICON */
}


/*
 * nma_menu_show_cb
 *
 * Pop up the wireless networks menu
 *
 */
static void nma_menu_show_cb (GtkWidget *menu, NMApplet *applet)
{
	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);
	g_return_if_fail (applet->menu != NULL);

#ifdef HAVE_STATUS_ICON
	gtk_status_icon_set_tooltip (applet->status_icon, NULL);
#else
	gtk_tooltips_set_tip (applet->tooltips, applet->event_box, NULL, NULL);
#endif /* HAVE_STATUS_ICON */

	if (!nm_client_manager_is_running (applet->nm_client)) {
		nma_menu_add_text_item (menu, _("NetworkManager is not running..."));
		return;
	}

	if (nm_client_get_state (applet->nm_client) == NM_STATE_ASLEEP) {
		nma_menu_add_text_item (menu, _("Networking disabled"));
		return;
	}

	nma_menu_add_devices (menu, applet);
	nma_menu_add_vpn_submenu (menu, applet);

	gtk_widget_show_all (applet->menu);

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

/*
 * nma_menu_create
 *
 * Create the applet's dropdown menu
 *
 */
static GtkWidget *
nma_menu_create (GtkMenuItem *parent, NMApplet *applet)
{
	GtkWidget	*menu;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	menu = gtk_menu_new ();
	gtk_container_set_border_width (GTK_CONTAINER (menu), 0);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (parent), menu);
	g_signal_connect (menu, "show", G_CALLBACK (nma_menu_show_cb), applet);

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

	/* 'Edit Connections...' item */
	applet->connections_menu_item = gtk_menu_item_new_with_mnemonic (_("Edit Connections..."));
	g_signal_connect (applet->connections_menu_item,
				   "activate",
				   G_CALLBACK (nma_edit_connections_cb),
				   applet);
	gtk_menu_shell_append (menu, applet->connections_menu_item);

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
	GSList *list;
	gboolean running = FALSE;

	nma_icons_free (applet);

	applet->size = size;
	nma_icons_load_from_disk (applet);

	list = nm_client_get_devices (applet->nm_client);
	if (list) {
		GSList *elt;
		gboolean done = FALSE;

		for (elt = list; elt && !done; elt = g_slist_next (elt)) {
			NMDevice *dev = NM_DEVICE (elt->data);

			switch (nm_device_get_state (dev)) {
				case NM_DEVICE_STATE_PREPARE:
				case NM_DEVICE_STATE_CONFIG:
				case NM_DEVICE_STATE_NEED_AUTH:
				case NM_DEVICE_STATE_IP_CONFIG:
				case NM_DEVICE_STATE_ACTIVATED:
					foo_device_state_changed (dev,
					                          nm_device_get_state (dev),
					                          applet, TRUE);
					done = TRUE;
					break;
				default:
					break;
			}
		}
		g_slist_free (list);
	}

	running = nm_client_manager_is_running (applet->nm_client);
	foo_manager_running (applet->nm_client, running, applet, TRUE);
	foo_update_icon (applet);

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
	nma_menu_clear (applet);
	gtk_menu_popup (GTK_MENU (applet->menu), NULL, NULL,
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
			nma_menu_clear (applet);
			gtk_widget_set_state (applet->event_box, GTK_STATE_SELECTED);
			gtk_menu_popup (GTK_MENU (applet->menu), NULL, NULL, nma_menu_position_func, applet, event->button, event->time);
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
static void nma_menu_deactivate_cb (GtkWidget *menu, NMApplet *applet)
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

	nma_menu_clear (applet);
	if (applet->top_menu_item) {
#if GTK_CHECK_VERSION(2, 12, 0)
		gtk_menu_item_set_submenu (GTK_MENU_ITEM (applet->top_menu_item), NULL);
#else
		gtk_menu_item_remove_submenu (GTK_MENU_ITEM (applet->top_menu_item));
#endif /* gtk 2.12.0 */

		applet->menu = nma_menu_create (GTK_MENU_ITEM (applet->top_menu_item), applet);
		g_signal_connect (applet->menu, "deactivate", G_CALLBACK (nma_menu_deactivate_cb), applet);
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
static gboolean
nma_setup_widgets (NMApplet *applet)
{
	g_return_val_if_fail (NM_IS_APPLET (applet), FALSE);

#ifdef HAVE_STATUS_ICON
	applet->status_icon = gtk_status_icon_new ();
	if (!applet->status_icon)
		return FALSE;

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
	if (!applet->tray_icon)
		return FALSE;

	g_object_ref (applet->tray_icon);
	gtk_object_sink (GTK_OBJECT (applet->tray_icon));

	/* Event box is the main applet widget */
	applet->event_box = gtk_event_box_new ();
	if (!applet->event_box)
		return FALSE;
	gtk_container_set_border_width (GTK_CONTAINER (applet->event_box), 0);
	g_signal_connect (applet->event_box, "button_press_event", G_CALLBACK (nma_toplevel_menu_button_press_cb), applet);

	applet->pixmap = gtk_image_new ();
	if (!applet->pixmap)
		return FALSE;
	gtk_container_add (GTK_CONTAINER (applet->event_box), applet->pixmap);
	gtk_container_add (GTK_CONTAINER (applet->tray_icon), applet->event_box);
 	gtk_widget_show_all (GTK_WIDGET (applet->tray_icon));
#endif /* HAVE_STATUS_ICON */

	applet->top_menu_item = gtk_menu_item_new ();
	if (!applet->top_menu_item)
		return FALSE;
	gtk_widget_set_name (applet->top_menu_item, "ToplevelMenu");
	gtk_container_set_border_width (GTK_CONTAINER (applet->top_menu_item), 0);

	applet->menu = nma_menu_create (GTK_MENU_ITEM (applet->top_menu_item), applet);
	if (!applet->menu)
		return FALSE;
#ifndef HAVE_STATUS_ICON
	g_signal_connect (applet->menu, "deactivate", G_CALLBACK (nma_menu_deactivate_cb), applet);
#endif /* !HAVE_STATUS_ICON */

	applet->context_menu = nma_context_menu_create (applet);
	if (!applet->context_menu)
		return FALSE;
	applet->encryption_size_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);
	if (!applet->encryption_size_group)
		return FALSE;

	return TRUE;
}


/*****************************************************************************/

static void
foo_update_icon (NMApplet *applet)
{
	GdkPixbuf	*pixbuf;
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

	/* Ignore setting of the same icon as is already displayed */
	if (applet->icon_layers[layer] == pixbuf)
		return;

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
foo_bssid_strength_changed (NMAccessPoint *ap,
                            GParamSpec *pspec,
                            gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GdkPixbuf *pixbuf;
	const GByteArray * ssid;
	char *tip;
	guint32 strength = 0;

	strength = nm_access_point_get_strength (ap);
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
	                       ssid ? nm_utils_escape_ssid (ssid->data, ssid->len) : "(none)",
	                       strength);

	gtk_status_icon_set_tooltip (applet->status_icon, tip);
	g_free (tip);
}

static AppletDbusConnectionSettings *
get_connection_settings_for_device (NMDevice *device, NMApplet *applet)
{
	GSList *iter;

	for (iter = applet->active_connections; iter; iter = g_slist_next (iter)) {
		NMClientActiveConnection * act_con = (NMClientActiveConnection *) iter->data;
		AppletDbusConnectionSettings *connection_settings;

		if (strcmp (act_con->service_name, NM_DBUS_SERVICE_USER_SETTINGS) != 0)
			continue;

		if (!g_slist_find (act_con->devices, device))
			continue;

		connection_settings = applet_dbus_settings_get_by_dbus_path (APPLET_DBUS_SETTINGS (applet->settings),
		                                                             act_con->connection_path);
		if (!connection_settings || !connection_settings->connection)
			continue;

		return connection_settings;
	}
	return NULL;
}

static gboolean
foo_wireless_state_change (NMDevice80211Wireless *device, NMDeviceState state, NMApplet *applet, gboolean synthetic)
{
	AppletDbusConnectionSettings *connection_settings;
	char *iface;
	NMAccessPoint *ap = NULL;
	const GByteArray * ssid;
	char *tip = NULL;
	gboolean handled = FALSE;
	char * esc_ssid = "(none)";

	iface = nm_device_get_iface (NM_DEVICE (device));

	if (state == NM_DEVICE_STATE_PREPARE ||
	    state == NM_DEVICE_STATE_CONFIG ||
	    state == NM_DEVICE_STATE_IP_CONFIG ||
	    state == NM_DEVICE_STATE_NEED_AUTH ||
	    state == NM_DEVICE_STATE_ACTIVATED) {

		ap = nm_device_802_11_wireless_get_active_access_point (NM_DEVICE_802_11_WIRELESS (device));
		if (ap) {
			ssid = nm_access_point_get_ssid (ap);
			if (ssid)
				esc_ssid = (char *) nm_utils_escape_ssid (ssid->data, ssid->len);
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
		if (ap) {
			g_object_ref (applet->current_ap);
			g_signal_connect (ap,
			                  "notify::strength",
			                  G_CALLBACK (foo_bssid_strength_changed),
			                  applet);
			foo_bssid_strength_changed (ap, NULL, applet);

			/* Save this BSSID to seen-bssids list */
			connection_settings = get_connection_settings_for_device (NM_DEVICE (device), applet);
			if (connection_settings && add_seen_bssid (connection_settings, ap))
				applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (connection_settings));
		}

#ifdef ENABLE_NOTIFY
		if (!synthetic) {
			tip = g_strdup_printf (_("You are now connected to the wireless network '%s'."), esc_ssid);
			nma_send_event_notification (applet, NOTIFY_URGENCY_LOW, _("Connection Established"),
							    tip, "nm-device-wireless");
			g_free (tip);
		}
#endif

		tip = g_strdup_printf (_("Wireless network connection to '%s'"), esc_ssid);
		handled = TRUE;
		break;
	default:
		if (applet->current_ap) {
			g_signal_handlers_disconnect_by_func (applet->current_ap,
			                                      G_CALLBACK (foo_bssid_strength_changed),
			                                      applet);
			g_object_unref (applet->current_ap);
			applet->current_ap = NULL;
		}
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
foo_wired_state_change (NMDevice8023Ethernet *device, NMDeviceState state, NMApplet *applet, gboolean synthetic)
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
		if (!synthetic)
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
foo_device_state_changed_cb (NMDevice *device, NMDeviceState state, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	AppletDbusConnectionSettings *connection_settings;
	NMSettingConnection *s_con;

	foo_device_state_changed (device, state, (gpointer) applet, FALSE);

	if (state != NM_DEVICE_STATE_ACTIVATED)
		return;

	/* If the device activation was successful, update the corresponding
	 * connection object with a current timestamp.
	 */
	connection_settings = get_connection_settings_for_device (device, applet);
	if (!connection_settings)
		return;

	s_con = (NMSettingConnection *) nm_connection_get_setting (connection_settings->connection,
	                                                           NM_SETTING_CONNECTION);
	if (s_con && s_con->autoconnect) {
		s_con->timestamp = (guint64) time (NULL);
		applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (connection_settings));
	}
}

static void
clear_active_connections (NMApplet *applet)
{
	g_slist_foreach (applet->active_connections,
	                 (GFunc) nm_client_free_active_connection_element,
	                 NULL);
	g_slist_free (applet->active_connections);
	applet->active_connections = NULL;
}

static void
foo_device_state_changed (NMDevice *device, NMDeviceState state, gpointer user_data, gboolean synthetic)
{
	NMApplet *applet = NM_APPLET (user_data);
	gboolean handled = FALSE;

	applet->animation_step = 0;
	if (applet->animation_id) {
		g_source_remove (applet->animation_id);
		applet->animation_id = 0;
	}

	clear_active_connections (applet);
	applet->active_connections = nm_client_get_active_connections (applet->nm_client);

	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		handled = foo_wired_state_change (NM_DEVICE_802_3_ETHERNET (device), state, applet, synthetic);
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		handled = foo_wireless_state_change (NM_DEVICE_802_11_WIRELESS (device), state, applet, synthetic);

	if (!handled)
		foo_common_state_change (device, state, applet);
}

static gboolean
add_seen_bssid (AppletDbusConnectionSettings *connection,
                NMAccessPoint *ap)
{
	NMSettingWireless *s_wireless;
	gboolean found = FALSE;
	gboolean added = FALSE;
	char *lower_bssid;
	GSList *iter;
	
	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection->connection,
	                                                              NM_SETTING_WIRELESS);
	if (!s_wireless)
		return FALSE;

	lower_bssid = g_ascii_strdown (nm_access_point_get_hw_address (ap), -1);
	for (iter = s_wireless->seen_bssids; iter; iter = g_slist_next (iter)) {
		char *lower_seen_bssid = g_ascii_strdown (iter->data, -1);

		if (!strcmp (lower_seen_bssid, lower_bssid)) {
			found = TRUE;
			g_free (lower_seen_bssid);
			break;
		}
		g_free (lower_seen_bssid);
	}

	/* Add this AP's BSSID to the seen-BSSIDs list */
	if (!found) {
		s_wireless->seen_bssids = g_slist_prepend (s_wireless->seen_bssids,
		                                           g_strdup (lower_bssid));
		added = TRUE;
	}
	g_free (lower_bssid);
	return added;
}

static void
notify_active_ap_changed_cb (NMDevice80211Wireless *device,
                             GParamSpec *pspec,
                             NMApplet *applet)
{
	AppletDbusConnectionSettings *connection_settings = NULL;
	NMSettingWireless *s_wireless;
	NMAccessPoint *ap;
	const GByteArray *ssid;

	if (nm_device_get_state (NM_DEVICE (device)) != NM_DEVICE_STATE_ACTIVATED)
		return;

	ap = nm_device_802_11_wireless_get_active_access_point (device);
	if (!ap)
		return;

	connection_settings = get_connection_settings_for_device (NM_DEVICE (device), applet);
	if (!connection_settings)
		return;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection_settings->connection,
	                                                              NM_SETTING_WIRELESS);
	if (!s_wireless)
		return;

	ssid = nm_access_point_get_ssid (ap);
	if (!ssid || !nm_utils_same_ssid (s_wireless->ssid, ssid, TRUE))
		return;

	if (add_seen_bssid (connection_settings, ap))
		applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (connection_settings));		
}

static void
foo_device_added_cb (NMClient *client, NMDevice *device, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	g_signal_connect (device, "state-changed",
				   G_CALLBACK (foo_device_state_changed_cb),
				   user_data);

	if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		g_signal_connect (NM_DEVICE_802_11_WIRELESS (device),
		                  "notify::active-access-point",
		                  G_CALLBACK (notify_active_ap_changed_cb),
		                  applet);
	}

	foo_device_state_changed_cb (device, nm_device_get_state (device), user_data);
}

static void
foo_add_initial_devices (gpointer data, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	foo_device_added_cb (applet->nm_client, NM_DEVICE (data), applet);
}

static void
foo_client_state_change_cb (NMClient *client, NMState state, gpointer user_data)
{
	foo_client_state_change (client, state, user_data, FALSE);
}

static void
foo_client_state_change (NMClient *client, NMState state, gpointer user_data, gboolean synthetic)
{
	NMApplet *applet = NM_APPLET (user_data);
	GdkPixbuf *pixbuf = NULL;
	char *tip = NULL;

	switch (state) {
	case NM_STATE_UNKNOWN:
		/* Clear any VPN connections */
		if (applet->vpn_connections)
			g_hash_table_remove_all (applet->vpn_connections);
		clear_active_connections (applet);
		foo_set_icon (applet, NULL, ICON_LAYER_VPN);
		break;
	case NM_STATE_ASLEEP:
		pixbuf = applet->no_connection_icon;
		tip = g_strdup (_("Networking disabled"));
		break;
	case NM_STATE_DISCONNECTED:
		pixbuf = applet->no_connection_icon;
		tip = g_strdup (_("No network connection"));

#ifdef ENABLE_NOTIFY
		if (!synthetic)
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
}

static void
foo_setup_client_state_handlers (NMClient *client, NMApplet *applet)
{
	g_signal_connect (client, "state-change",
				   G_CALLBACK (foo_client_state_change_cb),
				   applet);

	g_signal_connect (client, "device-added",
				   G_CALLBACK (foo_device_added_cb),
				   applet);
}

static void
foo_manager_running_cb (NMClient *client,
				 gboolean running,
				 gpointer user_data)
{
	running ? g_message ("NM appeared") : g_message ("NM disappeared");
	foo_manager_running (client, running, user_data, FALSE);
}


static void
foo_manager_running (NMClient *client,
				 gboolean running,
				 gpointer user_data,
                                 gboolean synthetic)
{
	NMApplet *applet = NM_APPLET (user_data);

	gtk_status_icon_set_visible (applet->status_icon, running);

	if (running) {
		/* Force the icon update */
		foo_client_state_change (client, nm_client_get_state (client), applet, synthetic);
	} else {
		foo_client_state_change (client, NM_STATE_UNKNOWN, applet, synthetic);
	}
}

typedef struct AddVPNInfo {
	NMApplet *applet;
	NMVPNConnection *active;
} AddVPNInfo;

static void
foo_add_initial_vpn_connections (gpointer data, gpointer user_data)
{
	AddVPNInfo *info = (AddVPNInfo *) user_data;
	NMVPNConnection *connection = NM_VPN_CONNECTION (data);

	add_one_vpn_connection (info->applet, connection);
	if (info->active)
		return;

	switch (nm_vpn_connection_get_state (connection)) {
		case NM_VPN_CONNECTION_STATE_PREPARE:
		case NM_VPN_CONNECTION_STATE_NEED_AUTH:
		case NM_VPN_CONNECTION_STATE_CONNECT:
		case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
		case NM_VPN_CONNECTION_STATE_ACTIVATED:
			info->active = connection;
			break;
		default:
			break;
	}
}

static gboolean
foo_set_initial_state (gpointer data)
{
	NMApplet *applet = NM_APPLET (data);
	GSList *list;

	foo_manager_running (applet->nm_client, TRUE, applet, FALSE);

	list = nm_client_get_devices (applet->nm_client);
	if (list) {
		g_slist_foreach (list, foo_add_initial_devices, applet);
		g_slist_free (list);
	}

	list = nm_vpn_manager_get_connections (applet->vpn_manager);
	if (list) {
		AddVPNInfo info = { applet, NULL };

		g_slist_foreach (list, foo_add_initial_vpn_connections, &info);
		g_slist_free (list);

		// FIXME: don't just use the first active VPN connection
		if (info.active) {
			vpn_connection_state_changed (info.active,
			                              nm_vpn_connection_get_state (info.active),
			                              NM_VPN_CONNECTION_STATE_REASON_NONE,
			                              applet);
		}
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
	g_signal_connect (client, "manager-running",
				   G_CALLBACK (foo_manager_running_cb), applet);
	foo_manager_running (client, nm_client_manager_is_running (client), applet, TRUE);

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

	nma_menu_clear (applet);
	if (applet->top_menu_item)
#if GTK_CHECK_VERSION(2, 12, 0)
		gtk_menu_item_set_submenu (GTK_MENU_ITEM (applet->top_menu_item), NULL);
#else
		gtk_menu_item_remove_submenu (GTK_MENU_ITEM (applet->top_menu_item));
#endif /* gtk 2.12.0 */

	nma_icons_free (applet);

//	nmi_passphrase_dialog_destroy (applet);
#ifdef ENABLE_NOTIFY
	if (applet->notification) {
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}
#endif

	g_free (applet->glade_file);
	if (applet->info_dialog_xml)
		g_object_unref (applet->info_dialog_xml);
#ifndef HAVE_STATUS_ICON
	if (applet->tooltips)
		g_object_unref (applet->tooltips);
#endif

	g_object_unref (applet->gconf_client);

#ifdef HAVE_STATUS_ICON
	if (applet->status_icon)
		g_object_unref (applet->status_icon);
#else
	if (applet->tray_icon) {
		gtk_widget_destroy (GTK_WIDGET (applet->tray_icon));
		g_object_unref (applet->tray_icon);
	}
#endif /* HAVE_STATUS_ICON */

	g_hash_table_destroy (applet->vpn_connections);
	g_object_unref (applet->vpn_manager);
	g_object_unref (applet->nm_client);

	clear_active_connections (applet);

	if (applet->nm_client)
		g_object_unref (applet->nm_client);

	G_OBJECT_CLASS (nma_parent_class)->finalize (object);
}

static GObject *nma_constructor (GType type, guint n_props, GObjectConstructParam *construct_props)
{
	NMApplet *applet;
	AppletDBusManager * dbus_mgr;

	applet = NM_APPLET (G_OBJECT_CLASS (nma_parent_class)->constructor (type, n_props, construct_props));

#ifndef HAVE_STATUS_ICON
	applet->tooltips = gtk_tooltips_new ();
	if (!applet->tooltips)
		goto error;
#endif

	applet->glade_file = g_build_filename (GLADEDIR, "applet.glade", NULL);
	if (!applet->glade_file || !g_file_test (applet->glade_file, G_FILE_TEST_IS_REGULAR)) {
		nma_schedule_warning_dialog (applet,
		                             _("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		goto error;
	}

	applet->info_dialog_xml = glade_xml_new (applet->glade_file, "info_dialog", NULL);
	if (!applet->info_dialog_xml)
        goto error;

	applet->gconf_client = gconf_client_get_default ();
	if (!applet->gconf_client)
	    goto error;

	/* Load pixmaps and create applet widgets */
	if (!nma_setup_widgets (applet))
	    goto error;
	nma_icons_init (applet);

	applet->active_connections = NULL;
	
	dbus_mgr = applet_dbus_manager_get ();
	if (dbus_mgr == NULL) {
		nm_warning ("Couldn't initialize the D-Bus manager.");
		g_object_unref (applet);
		return NULL;
	}

	applet->settings = applet_dbus_settings_new ();

    /* Start our DBus service */
    if (!applet_dbus_manager_start_service (dbus_mgr)) {
		g_object_unref (applet);
		return NULL;
    }

	foo_client_setup (applet);
	applet->vpn_manager = nm_vpn_manager_new ();
	applet->vpn_connections = g_hash_table_new_full (g_str_hash, g_str_equal,
										    (GDestroyNotify) g_free,
										    (GDestroyNotify) g_object_unref);

#ifndef HAVE_STATUS_ICON
	g_signal_connect (applet->tray_icon, "style-set", G_CALLBACK (nma_theme_change_cb), NULL);

	nma_icons_load_from_disk (applet);
#endif /* !HAVE_STATUS_ICON */

	return G_OBJECT (applet);

error:
	g_object_unref (applet);
	return NULL;
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

NMApplet *
nm_applet_new ()
{
	return g_object_new (NM_TYPE_APPLET, NULL);
}

