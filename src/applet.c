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
#include <netinet/ether.h>

#include <nm-device-802-3-ethernet.h>
#include <nm-device-802-11-wireless.h>
#include <nm-utils.h>
#include <nm-connection.h>
#include <nm-setting-connection.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-setting-serial.h>
#include <nm-setting-gsm.h>
#include <nm-setting-ppp.h>
#include <nm-setting-vpn.h>
#include <nm-setting-vpn-properties.h>

#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <gnome-keyring.h>
#include <libnotify/notify.h>

#include "applet.h"
#include "menu-items.h"
#include "applet-dialogs.h"
#include "vpn-password-dialog.h"
#include "nm-utils.h"
#include "gnome-keyring-md5.h"
#include "applet-dbus-manager.h"
#include "wireless-dialog.h"
#include "utils.h"
#include "crypto.h"

#include "gconf-helpers.h"

#include "vpn-connection-info.h"
#include "connection-editor/nm-connection-list.h"

static void nma_icons_init (NMApplet *applet);
static void nma_icons_free (NMApplet *applet);
static gboolean nma_icons_load (NMApplet *applet);

static void      foo_set_icon (NMApplet *applet, GdkPixbuf *pixbuf, guint32 layer);
static void		 foo_update_icon (NMApplet *applet);
static void      foo_device_state_changed (NMDevice *device, NMDeviceState state, gpointer user_data, gboolean synthetic);
static void      foo_device_state_changed_cb (NMDevice *device, NMDeviceState state, gpointer user_data);
static void      foo_manager_running (NMClient *client, gboolean running, gpointer user_data, gboolean synthetic);
static void      foo_manager_running_cb (NMClient *client, gboolean running, gpointer user_data);
static void      foo_client_state_change (NMClient *client, NMState state, gpointer user_data, gboolean synthetic);
static gboolean  add_seen_bssid (AppletDbusConnectionSettings *connection, NMAccessPoint *ap);
static GtkWidget *nma_menu_create (NMApplet *applet);
static void      wireless_dialog_response_cb (GtkDialog *dialog, gint response, gpointer user_data);


G_DEFINE_TYPE(NMApplet, nma, G_TYPE_OBJECT)

NMDevice *
applet_get_first_active_device (NMApplet *applet)
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
nm_ap_check_compatible (NMAccessPoint *ap,
                        NMConnection *connection)
{
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	const GByteArray *ssid;
	int mode;
	guint32 freq;

	g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	if (s_wireless == NULL)
		return FALSE;
	
	ssid = nm_access_point_get_ssid (ap);
	if (!nm_utils_same_ssid (s_wireless->ssid, ssid, TRUE))
		return FALSE;

	if (s_wireless->bssid) {
		struct ether_addr ap_addr;

		if (ether_aton_r (nm_access_point_get_hw_address (ap), &ap_addr)) {
			if (memcmp (s_wireless->bssid->data, &ap_addr, ETH_ALEN))
				return FALSE;
		}
	}

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

	s_wireless_sec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection,
															    NM_TYPE_SETTING_WIRELESS_SECURITY);

	return nm_setting_wireless_ap_security_compatible (s_wireless,
											 s_wireless_sec,
											 nm_access_point_get_flags (ap),
											 nm_access_point_get_wpa_flags (ap),
											 nm_access_point_get_rsn_flags (ap),
											 nm_access_point_get_mode (ap));
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
get_security_for_ap (NMAccessPoint *ap, guint32 dev_caps, gboolean *supported)
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

	/* Static WEP, Dynamic WEP, or LEAP */
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
	if (   (rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK)
	    && (dev_caps & NM_802_11_DEVICE_CAP_RSN)) {
		sec->key_mgmt = g_strdup ("wpa-psk");
		sec->proto = g_slist_append (sec->proto, g_strdup ("rsn"));
		sec->pairwise = add_ciphers_from_flags (rsn_flags, TRUE);
		sec->group = add_ciphers_from_flags (rsn_flags, FALSE);
		return sec;
	}

	/* WPA PSK */
	if (   (wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK)
	    && (dev_caps & NM_802_11_DEVICE_CAP_WPA)) {
		sec->key_mgmt = g_strdup ("wpa-psk");
		sec->proto = g_slist_append (sec->proto, g_strdup ("wpa"));
		sec->pairwise = add_ciphers_from_flags (wpa_flags, TRUE);
		sec->group = add_ciphers_from_flags (wpa_flags, FALSE);
		return sec;
	}

	/* WPA2 Enterprise */
	if (   (rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
	    && (dev_caps & NM_802_11_DEVICE_CAP_RSN)) {
		sec->key_mgmt = g_strdup ("wpa-eap");
		sec->proto = g_slist_append (sec->proto, g_strdup ("rsn"));
		sec->pairwise = add_ciphers_from_flags (rsn_flags, TRUE);
		sec->group = add_ciphers_from_flags (rsn_flags, FALSE);
		sec->eap = g_slist_append (sec->eap, g_strdup ("ttls"));
		sec->phase2_auth = g_strdup ("mschapv2");
		return sec;
	}

	/* WPA Enterprise */
	if (   (wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
	    && (dev_caps & NM_802_11_DEVICE_CAP_WPA)) {
		sec->key_mgmt = g_strdup ("wpa-eap");
		sec->proto = g_slist_append (sec->proto, g_strdup ("wpa"));
		sec->pairwise = add_ciphers_from_flags (wpa_flags, TRUE);
		sec->group = add_ciphers_from_flags (wpa_flags, FALSE);
		sec->eap = g_slist_append (sec->eap, g_strdup ("ttls"));
		sec->phase2_auth = g_strdup ("mschapv2");
		return sec;
	}

	*supported = FALSE;

none:
	g_object_unref (sec);
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

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con)
		return FALSE;

	if (NM_IS_DEVICE_802_3_ETHERNET (device)) {
		if (strcmp (s_con->type, NM_SETTING_WIRED_SETTING_NAME))
			return FALSE;
	} else if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		NMSettingWireless *s_wireless;
		const GByteArray *ap_ssid;

		if (strcmp (s_con->type, NM_SETTING_WIRELESS_SETTING_NAME))
			return FALSE;

		s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
		if (!s_wireless)
			return FALSE;

		ap_ssid = nm_access_point_get_ssid (ap);
		if (!nm_utils_same_ssid (s_wireless->ssid, ap_ssid, TRUE))
			return FALSE;

		if (!nm_ap_check_compatible (ap, connection))
			return FALSE;
	} else if (NM_IS_GSM_DEVICE (device)) {
		if (strcmp (s_con->type, NM_SETTING_GSM_SETTING_NAME))
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
	guint32 dev_caps;
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

	dev_caps = nm_device_802_11_wireless_get_capabilities (device);
	s_wireless_sec = get_security_for_ap (ap, dev_caps, &supported);
	if (!supported) {
		g_object_unref (s_wireless);
		s_wireless = NULL;
	} else if (s_wireless_sec) {
		s_wireless->security = g_strdup (NM_SETTING_WIRELESS_SECURITY_SETTING_NAME);
		nm_connection_add_setting (connection, NM_SETTING (s_wireless_sec));
	}

	return (NMSetting *) s_wireless;
}

static NMSetting *
new_auto_gsm_setting (NMConnection *connection, char **connection_name)
{
	NMSettingGsm *s_gsm;
	NMSettingSerial *s_serial;
	NMSettingPPP *s_ppp;

	s_gsm = (NMSettingGsm *) nm_setting_gsm_new ();
	s_gsm->number = g_strdup ("*99#"); /* This should be a sensible default as it's seems to be quite standard */

	*connection_name = g_strdup ("Auto GSM dialup connection");

	/* Serial setting */
	s_serial = (NMSettingSerial *) nm_setting_serial_new ();
	s_serial->baud = 115200;
	s_serial->bits = 8;
	s_serial->parity = 'n';
	s_serial->stopbits = 1;
	nm_connection_add_setting (connection, NM_SETTING (s_serial));

	s_ppp = (NMSettingPPP *) nm_setting_ppp_new ();
	s_ppp->usepeerdns = TRUE; /* This is probably a good default as well */
	nm_connection_add_setting (connection, NM_SETTING (s_ppp));

	return (NMSetting *) s_gsm;
}

static NMConnection *
new_auto_connection (NMDevice *device, NMAccessPoint *ap)
{
	NMConnection *connection;
	NMSetting *setting = NULL;
	NMSettingConnection *s_con;
	char *connection_id = NULL;
	gboolean autoconnect = TRUE;

	g_return_val_if_fail (device != NULL, NULL);

	connection = nm_connection_new ();

	if (NM_IS_DEVICE_802_3_ETHERNET (device)) {
		setting = nm_setting_wired_new ();
		connection_id = g_strdup ("Auto Ethernet");
	} else if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		if (!ap) {
			g_warning ("AP not set");
			g_object_unref (connection);
			return NULL;
		}

		setting = new_auto_wireless_setting (connection,
		                                     &connection_id,
		                                     &autoconnect,
		                                     NM_DEVICE_802_11_WIRELESS (device),
		                                     ap);
	} else if (NM_IS_GSM_DEVICE (device)) {
		setting = new_auto_gsm_setting (connection, &connection_id);
		autoconnect = FALSE; /* Never automatically activate GSM modems, we could get sued for that */
	} else {
		g_warning ("Unhandled device type '%s'", G_OBJECT_CLASS_NAME (device));
		g_object_unref (connection);
		return NULL;
	}

	g_assert (setting);

	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	s_con->id = connection_id;
	s_con->type = g_strdup (setting->name);
	s_con->autoconnect = autoconnect;
	nm_connection_add_setting (connection, (NMSetting *) s_con);

	nm_connection_add_setting (connection, setting);

	return connection;
}

static void
get_more_info (NMApplet *applet,
               NMDevice *device,
               NMAccessPoint *ap,
               NMConnection *connection)
{
	GtkWidget *dialog;

	dialog = nma_wireless_dialog_new (applet->glade_file,
	                                  applet->nm_client,
	                                  connection,
	                                  device,
	                                  ap,
	                                  FALSE);
	g_return_if_fail (dialog != NULL);

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (wireless_dialog_response_cb),
	                  applet);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
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
	AppletDbusSettings *applet_settings = APPLET_DBUS_SETTINGS (info->applet->settings);
	NMConnection *connection = NULL;
	char *con_path = NULL;
	const char *specific_object;
	GSList *elt, *connections;

	connections = applet_dbus_settings_list_connections (applet_settings);
	for (elt = connections; elt; elt = g_slist_next (elt)) {
		NMConnectionSettings *applet_connection = NM_CONNECTION_SETTINGS (elt->data);

		if (find_connection (applet_connection, info->device, info->ap)) {
			NMSettingConnection *s_con;

			connection = applet_dbus_connection_settings_get_connection (applet_connection);
			con_path = (char *) nm_connection_settings_get_dbus_object_path (applet_connection);

			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			g_message ("Found connection '%s' to activate at %s.", s_con->id, con_path);
			break;
		}
	}

	if (!connection) {
		AppletDbusConnectionSettings *exported_con;

		connection = new_auto_connection (info->device, info->ap);
		if (!connection) {
			nm_warning ("Couldn't create default connection.");
			return;
		}

		exported_con = applet_dbus_settings_add_connection (applet_settings, connection);
		if (exported_con)
			con_path = (char *) nm_connection_settings_get_dbus_object_path (NM_CONNECTION_SETTINGS (exported_con));
		else {
			/* If the setting isn't valid, because it needs more authentication
			 * or something, ask the user for it.
			 */
			nm_warning ("Invalid connection; asking for more information.");
			get_more_info (info->applet, info->device, info->ap, connection);
			return;
		}
	}

	if (NM_IS_DEVICE_802_11_WIRELESS (info->device))
		specific_object = nm_object_get_path (NM_OBJECT (info->ap));
	else
		specific_object = "/";

	nm_client_activate_device (info->applet->nm_client,
	                           info->device,
	                           NM_DBUS_SERVICE_USER_SETTINGS,
	                           con_path,
	                           specific_object,
	                           activate_device_cb,
	                           info);

//	nmi_dbus_signal_user_interface_activated (info->applet->connection);
}

static void
applet_do_notify (NMApplet *applet, 
                  NotifyUrgency urgency,
                  const char *summary,
                  const char *message,
                  const char *icon)
{
	const char *notify_icon;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (summary != NULL);
	g_return_if_fail (message != NULL);

#if GTK_CHECK_VERSION(2, 11, 0)
	if (!gtk_status_icon_is_embedded (applet->status_icon))
		return;
#endif

	if (!notify_is_initted ())
		notify_init ("NetworkManager");

	if (applet->notification != NULL) {
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}

	notify_icon = icon ? icon : GTK_STOCK_NETWORK;

	applet->notification = notify_notification_new_with_status_icon (summary, message, notify_icon, applet->status_icon);

	notify_notification_set_urgency (applet->notification, urgency);
	notify_notification_show (applet->notification, NULL);
}

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
			msg = g_strdup_printf ("\n%s", banner);
			applet_do_notify (applet, NOTIFY_URGENCY_LOW, title, msg, "gnome-lockscreen");
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
get_connection_id (AppletDbusConnectionSettings *settings)
{
	NMSettingConnection *conn;

	conn = NM_SETTING_CONNECTION (nm_connection_get_setting (settings->connection, NM_TYPE_SETTING_CONNECTION));
	return conn->id;
}

static void
add_one_vpn_connection (NMApplet *applet, NMVPNConnection *connection)
{
	const char *name;

	/* If the connection didn't have a name, it failed or something.
	 * Should probably have better behavior here; the applet shouldn't depend
	 * on the name itself to match the internal VPN Connection from GConf to
	 * the VPN connection exported by NM.
	 */
	name = nm_vpn_connection_get_name (connection);
	g_return_if_fail (name != NULL);

	g_signal_connect (connection, "state-changed",
	                  G_CALLBACK (vpn_connection_state_changed),
	                  applet);

	g_hash_table_insert (applet->vpn_connections,
	                     g_strdup (name),
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

	connection_name = get_connection_id ((AppletDbusConnectionSettings *) connection_settings);

	connection = (NMVPNConnection *) g_hash_table_lookup (applet->vpn_connections, connection_name);
	if (connection)
		/* Connection already active; do nothing */
		return;

	/* Connection inactive, activate */
	device = applet_get_first_active_device (applet);
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
	else if (NM_IS_GSM_DEVICE (device))
		menu_item = gsm_menu_item_new (NM_GSM_DEVICE (device), n_devices);
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

static gboolean
ow_dialog_close (gpointer user_data)
{
	GtkWidget *ow_dialog = GTK_WIDGET (user_data);

	gtk_dialog_response (GTK_DIALOG (ow_dialog), GTK_RESPONSE_OK);
	return FALSE;
}

#define NAG_IGNORED_TAG "nag-ignored"

static void
nag_dialog_response_cb (GtkDialog *nag_dialog,
                        gint response,
                        gpointer user_data)
{
	GtkWidget *ow_dialog = GTK_WIDGET (user_data);

	if (response == GTK_RESPONSE_NO) {  /* user opted not to correct the warning */
		g_object_set_data (G_OBJECT (ow_dialog),
		                   NAG_IGNORED_TAG,
		                   GUINT_TO_POINTER (TRUE));
		g_idle_add (ow_dialog_close, ow_dialog);
	}
}

static void
wireless_dialog_response_cb (GtkDialog *dialog,
                             gint response,
                             gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMConnection *connection = NULL;
	NMDevice *device = NULL;
	NMAccessPoint *ap = NULL;
	NMSettingConnection *s_con;
	AppletDbusConnectionSettings *exported_con = NULL;
	const char *con_path;
	gboolean ignored = FALSE;

	if (response != GTK_RESPONSE_OK)
		goto done;

	ignored = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (dialog), NAG_IGNORED_TAG));
	if (!ignored) {
		GtkWidget *nag_dialog;

		/* Nag the user about certificates or whatever.  Only destroy the dialog
		 * if no nagging was done.
		 */
		nag_dialog = nma_wireless_dialog_nag_user (GTK_WIDGET (dialog));
		if (nag_dialog) {
			gtk_window_set_transient_for (GTK_WINDOW (nag_dialog), GTK_WINDOW (dialog));
			g_signal_connect (nag_dialog, "response",
			                  G_CALLBACK (nag_dialog_response_cb),
			                  dialog);
			return;
		}
	}

	connection = nma_wireless_dialog_get_connection (GTK_WIDGET (dialog), &device, &ap);
	g_assert (connection);
	g_assert (device);
	// FIXME: find a compatible connection in the current connection list before adding
	// a new connection

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con->id) {
		NMSettingWireless *s_wireless;
		char *ssid;

		s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
		ssid = nm_utils_ssid_to_utf8 ((const char *) s_wireless->ssid->data, s_wireless->ssid->len);
		s_con->id = g_strdup_printf ("Auto %s", ssid);
		g_free (ssid);

		// FIXME: don't autoconnect until the connection is successful at least once
		/* Don't autoconnect adhoc networks by default for now */
		if (!s_wireless->mode || !strcmp (s_wireless->mode, "infrastructure"))
			s_con->autoconnect = TRUE;
	}

	exported_con = applet_dbus_settings_get_by_connection (APPLET_DBUS_SETTINGS (applet->settings),
	                                                       connection);
	if (!exported_con) {
		exported_con = applet_dbus_settings_add_connection (APPLET_DBUS_SETTINGS (applet->settings),
		                                                    connection);
		if (!exported_con) {
			nm_warning ("Couldn't create other network connection.");
			goto done;
		}
	} else {
		/* Save the updated settings to GConf */
		applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (exported_con));
	}

	con_path = nm_connection_settings_get_dbus_object_path (NM_CONNECTION_SETTINGS (exported_con));
	nm_client_activate_device (applet->nm_client,
	                           device,
	                           NM_DBUS_SERVICE_USER_SETTINGS,
	                           con_path,
	                           ap ? nm_object_get_path (NM_OBJECT (ap)) : NULL,
	                           activate_device_cb,
	                           applet);

done:
	gtk_widget_hide (GTK_WIDGET (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
other_wireless_activate_cb (GtkWidget *menu_item,
                            NMApplet *applet)
{
	GtkWidget *dialog;

	dialog = nma_wireless_dialog_new (applet->glade_file,
	                                  applet->nm_client,
	                                  NULL,
	                                  NULL,
	                                  NULL,
	                                  FALSE);
	if (!dialog)
		return;

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (wireless_dialog_response_cb),
	                  applet);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gdk_x11_window_set_user_time (dialog->window, gtk_get_current_event_time ());
	gtk_window_present (GTK_WINDOW (dialog));
}


static void nma_menu_add_other_network_item (GtkWidget *menu, NMApplet *applet)
{
	GtkWidget *menu_item;
	GtkWidget *label;

	menu_item = gtk_menu_item_new ();
	label = gtk_label_new_with_mnemonic (_("_Connect to Other Wireless Network..."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_container_add (GTK_CONTAINER (menu_item), label);
	gtk_widget_show_all (menu_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	g_signal_connect (menu_item, "activate", G_CALLBACK (other_wireless_activate_cb), applet);
}


static void new_network_item_selected (GtkWidget *menu_item, NMApplet *applet)
{
	GtkWidget *dialog;

	dialog = nma_wireless_dialog_new (applet->glade_file,
	                                  applet->nm_client,
	                                  NULL,
	                                  NULL,
	                                  NULL,
	                                  TRUE);
	if (!dialog)
		return;

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (wireless_dialog_response_cb),
	                  applet);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gdk_x11_window_set_user_time (dialog->window, gtk_get_current_event_time ());
	gtk_window_present (GTK_WINDOW (dialog));
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
		return;

	strength = nm_access_point_get_strength (ap);

	dup_data.found = NULL;
	dup_data.hash = g_object_get_data (G_OBJECT (ap), "hash");
	if (!dup_data.hash)
		return;
	dup_data.device = cb_data->device;
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
		const char *foo;
		char *aa_desc = NULL;
		char *bb_desc = NULL;
		gint ret;

		foo = utils_get_device_description (aa);
		if (foo)
			aa_desc = g_strdup (foo);
		if (!aa_desc)
			aa_desc = nm_device_get_iface (aa);

		foo = utils_get_device_description (bb);
		if (foo)
			bb_desc = g_strdup (foo);
		if (!bb_desc)
			bb_desc = nm_device_get_iface (bb);

		if (!aa_desc && bb_desc) {
			g_free (bb_desc);
			return -1;
		} else if (aa_desc && !bb_desc) {
			g_free (aa_desc);
			return 1;
		} else if (!aa_desc && !bb_desc) {
			return 0;
		}

		g_assert (aa_desc);
		g_assert (bb_desc);
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
		nma_menu_add_other_network_item (menu, applet);
		nma_menu_add_create_network_item (menu, applet);
	}

 out:
	g_slist_free (devices);
}

static int
sort_vpn_connections (gconstpointer a, gconstpointer b)
{
	return strcmp (get_connection_id ((AppletDbusConnectionSettings *) a),
				get_connection_id ((AppletDbusConnectionSettings *) b));
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
		NMSettingConnection *s_con;

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (applet_settings->connection,
		                                                          NM_TYPE_SETTING_CONNECTION));
		if (strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME))
			/* Not a VPN connection */
			continue;

		if (!nm_connection_get_setting (applet_settings->connection, NM_TYPE_SETTING_VPN_PROPERTIES)) {
			const char *name = NM_SETTING (s_con)->name;
			g_warning ("%s: VPN connection '%s' didn't have requires vpn-properties setting.", __func__, name);
			continue;
		}

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
		const char *connection_name = get_connection_id (applet_settings);

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
	applet->menu = nma_menu_create (applet);
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

	gtk_status_icon_set_tooltip (applet->status_icon, NULL);

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

	gtk_widget_show_all (menu);

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

/*
 * nma_menu_create
 *
 * Create the applet's dropdown menu
 *
 */
static GtkWidget *
nma_menu_create (NMApplet *applet)
{
	GtkWidget	*menu;

	g_return_val_if_fail (applet != NULL, NULL);

	menu = gtk_menu_new ();
	gtk_container_set_border_width (GTK_CONTAINER (menu), 0);
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
	gboolean wireless_hw_enabled;

	state = nm_client_get_state (applet->nm_client);
	wireless_hw_enabled = nm_client_wireless_hardware_get_enabled (applet->nm_client);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->enable_networking_item),
							  state != NM_STATE_ASLEEP);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->stop_wireless_item),
							  nm_client_wireless_get_enabled (applet->nm_client));
	gtk_widget_set_sensitive (GTK_WIDGET (applet->stop_wireless_item),
	                          wireless_hw_enabled);

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

static void
nma_edit_connections_cb (GtkMenuItem *mi, NMApplet *applet)
{
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
	g_signal_connect_swapped (applet->info_menu_item,
	                          "activate",
	                          G_CALLBACK (applet_info_dialog_show),
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
	g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (applet_about_dialog_show), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (menu, menu_item);

	gtk_widget_show_all (GTK_WIDGET (menu));

	return GTK_WIDGET (menu);
}


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
	nma_icons_load (applet);

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

	applet->menu = nma_menu_create (applet);
	if (!applet->menu)
		return FALSE;

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
			                  "notify::" NM_ACCESS_POINT_STRENGTH,
			                  G_CALLBACK (foo_bssid_strength_changed),
			                  applet);
			foo_bssid_strength_changed (ap, NULL, applet);

			/* Save this BSSID to seen-bssids list */
			connection_settings = get_connection_settings_for_device (NM_DEVICE (device), applet);
			if (connection_settings && add_seen_bssid (connection_settings, ap))
				applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (connection_settings));
		}

		if (!synthetic) {
			tip = g_strdup_printf (_("You are now connected to the wireless network '%s'."), esc_ssid);
			applet_do_notify (applet, NOTIFY_URGENCY_LOW, _("Connection Established"),
							  tip, "nm-device-wireless");
			g_free (tip);
		}

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

		if (!synthetic) {
			applet_do_notify (applet, NOTIFY_URGENCY_LOW,
							  _("Connection Established"),
							  _("You are now connected to the wired network."),
							  "nm-device-wired");
		}

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

/* GSM device */

static gboolean
foo_gsm_state_change (NMGsmDevice *device, NMDeviceState state, NMApplet *applet, gboolean synthetic)
{
	char *iface;
	char *tip = NULL;
	gboolean handled = FALSE;

	iface = nm_device_get_iface (NM_DEVICE (device));

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		tip = g_strdup_printf (_("Dialing GSM device %s..."), iface);
		break;
	case NM_DEVICE_STATE_CONFIG:
		tip = g_strdup_printf (_("Running PPP on device %s..."), iface);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		foo_set_icon (applet, applet->gsm_icon, ICON_LAYER_LINK);
		tip = g_strdup (_("GSM connection"));
		if (!synthetic) {
			applet_do_notify (applet, NOTIFY_URGENCY_LOW,
						      _("Connection Established"),
							  _("You are now connected to the GSM network."),
							  "nm-adhoc");
		}

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

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection_settings->connection,
												   NM_TYPE_SETTING_CONNECTION));
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
	else if (NM_IS_GSM_DEVICE (device))
		handled = foo_gsm_state_change (NM_GSM_DEVICE (device), state, applet, synthetic);

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
	const char *bssid;
	
	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection->connection,
													 NM_TYPE_SETTING_WIRELESS));
	if (!s_wireless)
		return FALSE;

	bssid = nm_access_point_get_hw_address (ap);
	if (!bssid || !utils_ether_addr_valid (ether_aton (bssid)))
		return FALSE;

	lower_bssid = g_ascii_strdown (bssid, -1);
	if (!lower_bssid)
		return FALSE;

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

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection_settings->connection,
													 NM_TYPE_SETTING_WIRELESS));
	if (!s_wireless)
		return;

	ssid = nm_access_point_get_ssid (ap);
	if (!ssid || !nm_utils_same_ssid (s_wireless->ssid, ssid, TRUE))
		return;

	if (add_seen_bssid (connection_settings, ap))
		applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (connection_settings));		
}

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

static void
add_hash_to_ap (NMAccessPoint *ap)
{
	guchar *hash = ap_hash (ap);
	g_object_set_data_full (G_OBJECT (ap),
	                        "hash", hash,
	                        (GDestroyNotify) g_free);
}

static void
notify_ap_prop_changed_cb (NMAccessPoint *ap,
                           GParamSpec *pspec,
                           NMApplet *applet)
{
	const char *prop = g_param_spec_get_name (pspec);

	if (   !strcmp (prop, NM_ACCESS_POINT_FLAGS)
	    || !strcmp (prop, NM_ACCESS_POINT_WPA_FLAGS)
	    || !strcmp (prop, NM_ACCESS_POINT_RSN_FLAGS)
	    || !strcmp (prop, NM_ACCESS_POINT_SSID)
	    || !strcmp (prop, NM_ACCESS_POINT_FREQUENCY)
	    || !strcmp (prop, NM_ACCESS_POINT_MODE)) {
		add_hash_to_ap (ap);
	}
}

static void
access_point_added_cb (NMDevice80211Wireless *device,
                       NMAccessPoint *ap,
                       gpointer user_data)
{
	NMApplet *applet = NM_APPLET  (user_data);

	add_hash_to_ap (ap);
	g_signal_connect (G_OBJECT (ap),
	                  "notify",
	                  G_CALLBACK (notify_ap_prop_changed_cb),
	                  applet);
}

static void
foo_device_added_cb (NMClient *client, NMDevice *device, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	g_signal_connect (device, "state-changed",
				   G_CALLBACK (foo_device_state_changed_cb),
				   user_data);

	if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		NMDevice80211Wireless *wdev = NM_DEVICE_802_11_WIRELESS (device);
		GSList *aps, *iter;

		g_signal_connect (wdev,
		                  "notify::" NM_DEVICE_802_11_WIRELESS_ACTIVE_ACCESS_POINT,
		                  G_CALLBACK (notify_active_ap_changed_cb),
		                  applet);

		g_signal_connect (wdev,
		                  "access-point-added",
		                  G_CALLBACK (access_point_added_cb),
		                  applet);

		/* Hash all APs this device knows about */
		aps = nm_device_802_11_wireless_get_access_points (wdev);
		for (iter = aps; iter; iter = g_slist_next (iter)) {
			NMAccessPoint *ap = NM_ACCESS_POINT (iter->data);
			add_hash_to_ap (ap);
		}
		g_slist_free (aps);
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
		if (!synthetic) {
			applet_do_notify (applet, NOTIFY_URGENCY_NORMAL, _("Disconnected"),
							  _("The network connection has been disconnected."),
							  "nm-no-connection");
		}
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

static void
get_secrets_dialog_response_cb (GtkDialog *dialog,
                                gint response,
                                gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	DBusGMethodInvocation *context;
	AppletDbusConnectionSettings *applet_connection;
	NMConnection *connection = NULL;
	NMDevice *device = NULL;
	GHashTable *setting_hash;
	const char *setting_name;
	NMSetting *setting;
	GError *error = NULL;
	gboolean ignored;

	context = g_object_get_data (G_OBJECT (dialog), "dbus-context");
	setting_name = g_object_get_data (G_OBJECT (dialog), "setting-name");
	if (!context || !setting_name) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't get dialog data",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	if (response != GTK_RESPONSE_OK) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): canceled",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	ignored = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (dialog), NAG_IGNORED_TAG));
	if (!ignored) {
		GtkWidget *widget;

		/* Nag the user about certificates or whatever.  Only destroy the dialog
		 * if no nagging was done.
		 */
		widget = nma_wireless_dialog_nag_user (GTK_WIDGET (dialog));
		if (widget) {
			gtk_window_set_transient_for (GTK_WINDOW (widget), GTK_WINDOW (dialog));
			g_signal_connect (widget, "response",
			                  G_CALLBACK (nag_dialog_response_cb),
			                  dialog);
			return;
		}
	}

	connection = nma_wireless_dialog_get_connection (GTK_WIDGET (dialog), &device, NULL);
	if (!connection) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't get connection from wireless dialog.",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	setting = nm_connection_get_setting_by_name (connection, setting_name);
	if (!setting) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): requested setting '%s' didn't exist in the "
		             " connection.",
		             __FILE__, __LINE__, __func__, setting_name);
		goto done;
	}

	utils_fill_connection_certs (connection);
	setting_hash = nm_setting_to_hash (setting);
	utils_clear_filled_connection_certs (connection);

	if (!setting_hash) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): failed to hash setting '%s'.",
		             __FILE__, __LINE__, __func__, setting_name);
		goto done;
	}

	dbus_g_method_return (context, setting_hash);
	g_hash_table_destroy (setting_hash);

	/* Save the connection back to GConf _after_ hashing it, because
	 * saving to GConf might trigger the GConf change notifiers, resulting
	 * in the connection being read back in from GConf which clears secrets.
	 */
	applet_connection = applet_dbus_settings_get_by_connection (APPLET_DBUS_SETTINGS (applet->settings), connection);
	if (applet_connection)
		applet_dbus_connection_settings_save (NM_CONNECTION_SETTINGS (applet_connection));

done:
	if (error) {
		g_warning (error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	if (connection)
		nm_connection_clear_secrets (connection);
	gtk_widget_hide (GTK_WIDGET (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}


static NMDevice *
get_connection_details (AppletDbusConnectionSettings *applet_connection,
                        NMApplet *applet,
                        NMAccessPoint **ap)
{
	GSList *iter;
	NMClientActiveConnection *act_con = NULL;
	NMDevice *device;

	g_return_val_if_fail (applet_connection != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	/* Ensure the active connection list is up-to-date */
	if (g_slist_length (applet->active_connections) == 0)
		applet->active_connections = nm_client_get_active_connections (applet->nm_client);

	for (iter = applet->active_connections; iter; iter = g_slist_next (iter)) {
		NMClientActiveConnection * tmp_con = (NMClientActiveConnection *) iter->data;
		const char *con_path;

		if (!strcmp (tmp_con->service_name, NM_DBUS_SERVICE_USER_SETTINGS)) {
			con_path = nm_connection_settings_get_dbus_object_path ((NMConnectionSettings *) applet_connection);
			if (!strcmp (con_path, tmp_con->connection_path)) {
				act_con = tmp_con;
				break;
			}
		}
	}

	if (!act_con)
		return NULL;

	device = NM_DEVICE (act_con->devices->data);
	if (NM_IS_DEVICE_802_11_WIRELESS (device)) {
		*ap = nm_device_802_11_wireless_get_access_point_by_path (NM_DEVICE_802_11_WIRELESS (device),
		                                                          act_con->specific_object);
	}

	return device;
}

static void
applet_settings_new_secrets_requested_cb (AppletDbusSettings *settings,
                                          AppletDbusConnectionSettings *applet_connection,
                                          const char *setting_name,
                                          const char **hints,
                                          gboolean ask_user,
                                          DBusGMethodInvocation *context,
                                          gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GtkWidget *dialog;
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMAccessPoint *ap = NULL;
	NMDevice *device;

	connection = applet_dbus_connection_settings_get_connection ((NMConnectionSettings *) applet_connection);
	g_return_if_fail (connection != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (applet_connection->connection,
												   NM_TYPE_SETTING_CONNECTION));
	g_return_if_fail (s_con != NULL);
	g_return_if_fail (s_con->type != NULL);

	if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME)) {
		nma_vpn_request_password (connection, setting_name, ask_user, context);
		return;
	}

	device = get_connection_details (applet_connection, applet, &ap);
	if (!device || (NM_IS_DEVICE_802_11_WIRELESS (device) && !ap)) {
		GError *error = NULL;
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't find details for connection",
		             __FILE__, __LINE__, __func__);
		g_warning (error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	/* FIXME: Handle other device types */

	dialog = nma_wireless_dialog_new (applet->glade_file,
	                                  applet->nm_client,
	                                  connection,
	                                  device,
	                                  ap,
	                                  FALSE);
	g_return_if_fail (dialog != NULL);
	g_object_set_data (G_OBJECT (dialog), "dbus-context", context);
	g_object_set_data_full (G_OBJECT (dialog),
	                        "setting-name", g_strdup (setting_name),
	                        (GDestroyNotify) g_free);

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (get_secrets_dialog_response_cb),
	                  applet);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
}


static void
applet_add_default_ethernet_connection (AppletDbusSettings *settings)
{
	GSList *connections;
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingWired *s_wired;

	connections = applet_dbus_settings_list_connections (settings);
	if (g_slist_length (connections) > 0)
		return;

	connection = nm_connection_new ();

	s_con = (NMSettingConnection *) nm_setting_connection_new ();
	s_con->id = g_strdup ("Auto Ethernet");
	s_con->type = g_strdup ("802-3-ethernet");
	s_con->autoconnect = TRUE;

	s_wired = (NMSettingWired *) nm_setting_wired_new ();

	nm_connection_add_setting (connection, (NMSetting *) s_wired);
	nm_connection_add_setting (connection, (NMSetting *) s_con);

	applet_dbus_settings_add_connection (settings, connection);
}

/*****************************************************************************/

static void nma_icons_zero (NMApplet *applet)
{
	int i, j;

	applet->no_connection_icon = NULL;
	applet->wired_icon = NULL;
	applet->adhoc_icon = NULL;
	applet->gsm_icon = NULL;
	applet->vpn_lock_icon = NULL;

	applet->wireless_00_icon = NULL;
	applet->wireless_25_icon = NULL;
	applet->wireless_50_icon = NULL;
	applet->wireless_75_icon = NULL;
	applet->wireless_100_icon = NULL;

	for (i = 0; i < NUM_CONNECTING_STAGES; i++) {
		for (j = 0; j < NUM_CONNECTING_FRAMES; j++)
			applet->network_connecting_icons[i][j] = NULL;
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++)
		applet->vpn_connecting_icons[i] = NULL;

	applet->icons_loaded = FALSE;
}

static void nma_icons_free (NMApplet *applet)
{
	int i, j;

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
	if (applet->gsm_icon)
		g_object_unref (applet->gsm_icon);
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
		for (j = 0; j < NUM_CONNECTING_FRAMES; j++)
			if (applet->network_connecting_icons[i][j])
				g_object_unref (applet->network_connecting_icons[i][j]);
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++)
		if (applet->vpn_connecting_icons[i])
			g_object_unref (applet->vpn_connecting_icons[i]);

	nma_icons_zero (applet);
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
nma_icons_load (NMApplet *applet)
{
	int 		size, i;
	gboolean	success;

	/*
	 * NULL out the icons, so if we error and call nma_icons_free(), we don't hit stale
	 * data on the not-yet-reached icons.  This can happen off nma_icon_theme_changed().
	 */

	g_return_val_if_fail (!applet->icons_loaded, FALSE);

	size = applet->size;
	if (size < 0)
		return FALSE;

	for (i = 0; i <= ICON_LAYER_MAX; i++)
		applet->icon_layers[i] = NULL;

	ICON_LOAD(applet->no_connection_icon, "nm-no-connection");
	ICON_LOAD(applet->wired_icon, "nm-device-wired");
	ICON_LOAD(applet->adhoc_icon, "nm-adhoc");
	ICON_LOAD(applet->gsm_icon, "nm-adhoc"); /* FIXME: Until there's no GSM device icon */
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
	if (!success) {
		applet_warning_dialog_show (_("The NetworkManager applet could not find some required resources.  It cannot continue.\n"));
		nma_icons_free (applet);
	}

	return success;
}

static void nma_icon_theme_changed (GtkIconTheme *icon_theme, NMApplet *applet)
{
	nma_icons_free (applet);
	nma_icons_load (applet);
}

static void nma_icons_init (NMApplet *applet)
{
	GdkScreen *screen;
	gboolean path_appended;

	if (applet->icon_theme) {
		g_signal_handlers_disconnect_by_func (applet->icon_theme,
						      G_CALLBACK (nma_icon_theme_changed),
						      applet);
	}

#if GTK_CHECK_VERSION(2, 11, 0)
	screen = gtk_status_icon_get_screen (applet->status_icon);
#else
	screen = gdk_screen_get_default ();
#endif /* gtk 2.11.0 */
	
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
}

static GObject *
constructor (GType type,
             guint n_props,
             GObjectConstructParam *construct_props)
{
	NMApplet *applet;
	AppletDBusManager * dbus_mgr;
	GError *error = NULL;

	if (!crypto_init (&error)) {
		g_warning ("Couldn't initilize crypto system: %d %s",
		           error->code, error->message);
		g_error_free (error);
		return NULL;
	}

	applet = NM_APPLET (G_OBJECT_CLASS (nma_parent_class)->constructor (type, n_props, construct_props));

	g_set_application_name (_("NetworkManager Applet"));
	gtk_window_set_default_icon_name (GTK_STOCK_NETWORK);

	applet->glade_file = g_build_filename (GLADEDIR, "applet.glade", NULL);
	if (!applet->glade_file || !g_file_test (applet->glade_file, G_FILE_TEST_IS_REGULAR)) {
		applet_warning_dialog_show (_("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
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
	g_signal_connect (G_OBJECT (applet->settings), "new-secrets-requested",
	                  (GCallback) applet_settings_new_secrets_requested_cb,
	                  applet);

	applet_add_default_ethernet_connection ((AppletDbusSettings *) applet->settings);

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

	return G_OBJECT (applet);

error:
	g_object_unref (applet);
	return NULL;
}

static void finalize (GObject *object)
{
	NMApplet *applet = NM_APPLET (object);

	nma_menu_clear (applet);
	nma_icons_free (applet);

	if (applet->notification) {
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}

	g_free (applet->glade_file);
	if (applet->info_dialog_xml)
		g_object_unref (applet->info_dialog_xml);

	g_object_unref (applet->gconf_client);

	if (applet->status_icon)
		g_object_unref (applet->status_icon);

	g_hash_table_destroy (applet->vpn_connections);
	g_object_unref (applet->vpn_manager);
	g_object_unref (applet->nm_client);

	clear_active_connections (applet);

	if (applet->nm_client)
		g_object_unref (applet->nm_client);

	crypto_deinit ();

	G_OBJECT_CLASS (nma_parent_class)->finalize (object);
}

static void nma_init (NMApplet *applet)
{
	applet->animation_id = 0;
	applet->animation_step = 0;
	applet->icon_theme = NULL;
	applet->notification = NULL;
	applet->size = -1;

	nma_icons_zero (applet);
}

static void nma_class_init (NMAppletClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->constructor = constructor;
	gobject_class->finalize = finalize;
}

NMApplet *
nm_applet_new ()
{
	return g_object_new (NM_TYPE_APPLET, NULL);
}

