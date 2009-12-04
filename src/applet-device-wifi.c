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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include "wireless-helper.h"
#include <ctype.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <nm-device.h>
#include <nm-access-point.h>
#include <nm-setting-connection.h>
#include <nm-setting-wireless.h>
#include <nm-device-wifi.h>
#include <nm-setting-8021x.h>
#include <nm-utils.h>

#include "applet.h"
#include "applet-device-wifi.h"
#include "ap-menu-item.h"
#include "utils.h"
#include "wireless-dialog.h"

#define PREF_SUPPRESS_WIRELESS_NEWORKS_AVAILABLE    APPLET_PREFS_PATH "/suppress-wireless-networks-available"

#define ACTIVE_AP_TAG "active-ap"

static void wireless_dialog_response_cb (GtkDialog *dialog, gint response, gpointer user_data);

static NMAccessPoint *update_active_ap (NMDevice *device, NMDeviceState state, NMApplet *applet);

static void
show_ignore_focus_stealing_prevention (GtkWidget *widget)
{
	gtk_widget_realize (widget);
	gtk_widget_show (widget);
	gtk_window_present_with_time (GTK_WINDOW (widget), gdk_x11_get_server_time (widget->window));
}

static void
other_wireless_activate_cb (GtkWidget *menu_item,
                            NMApplet *applet)
{
	GtkWidget *dialog;

	dialog = nma_wireless_dialog_new_for_other (applet);
	if (!dialog)
		return;

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (wireless_dialog_response_cb),
	                  applet);

	show_ignore_focus_stealing_prevention (dialog);
}

void
nma_menu_add_hidden_network_item (GtkWidget *menu, NMApplet *applet)
{
	GtkWidget *menu_item;
	GtkWidget *label;

	menu_item = gtk_menu_item_new ();
	label = gtk_label_new_with_mnemonic (_("_Connect to Hidden Wireless Network..."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_container_add (GTK_CONTAINER (menu_item), label);
	gtk_widget_show_all (menu_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	g_signal_connect (menu_item, "activate", G_CALLBACK (other_wireless_activate_cb), applet);
}

static void
new_network_activate_cb (GtkWidget *menu_item, NMApplet *applet)
{
	GtkWidget *dialog;

	dialog = nma_wireless_dialog_new_for_create (applet);
	if (!dialog)
		return;

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (wireless_dialog_response_cb),
	                  applet);

	show_ignore_focus_stealing_prevention (dialog);
}

void
nma_menu_add_create_network_item (GtkWidget *menu, NMApplet *applet)
{
	GtkWidget *menu_item;
	GtkWidget *label;
	GError *error = NULL;
	gboolean disabled;

	menu_item = gtk_menu_item_new ();
	label = gtk_label_new_with_mnemonic (_("Create _New Wireless Network..."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_container_add (GTK_CONTAINER (menu_item), label);
	gtk_widget_show_all (menu_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	g_signal_connect (menu_item, "activate", G_CALLBACK (new_network_activate_cb), applet);

	/* FIXME: should really use PolicyKit and NM permissions here instead
	 * using using GConf mandatory settings.  But this works for now.
	 */
	disabled = gconf_client_get_bool (applet->gconf_client, PREF_DISABLE_WIFI_CREATE, &error);
	if (error || disabled)
		gtk_widget_set_sensitive (GTK_WIDGET (menu_item), FALSE);
	g_clear_error (&error);
}



typedef struct {
	NMApplet *applet;
	NMDeviceWifi *device;
	NMAccessPoint *ap;
	NMConnection *connection;
} WirelessMenuItemInfo;

static void
wireless_menu_item_info_destroy (gpointer data)
{
	WirelessMenuItemInfo *info = (WirelessMenuItemInfo *) data;

	g_object_unref (G_OBJECT (info->device));
	g_object_unref (G_OBJECT (info->ap));

	if (info->connection)
		g_object_unref (G_OBJECT (info->connection));

	g_slice_free (WirelessMenuItemInfo, data);
}

static const char * default_ssid_list[] = {
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

static void
add_ciphers_from_flags (NMSettingWirelessSecurity *sec,
                        guint32 flags,
                        gboolean pairwise)
{
	if (pairwise) {
		if (flags & NM_802_11_AP_SEC_PAIR_TKIP)
			nm_setting_wireless_security_add_pairwise (sec, "tkip");
		if (flags & NM_802_11_AP_SEC_PAIR_CCMP)
			nm_setting_wireless_security_add_pairwise (sec, "ccmp");
	} else {
		if (flags & NM_802_11_AP_SEC_GROUP_WEP40)
			nm_setting_wireless_security_add_group (sec, "wep40");
		if (flags & NM_802_11_AP_SEC_GROUP_WEP104)
			nm_setting_wireless_security_add_group (sec, "wep104");
		if (flags & NM_802_11_AP_SEC_GROUP_TKIP)
			nm_setting_wireless_security_add_group (sec, "tkip");
		if (flags & NM_802_11_AP_SEC_GROUP_CCMP)
			nm_setting_wireless_security_add_group (sec, "ccmp");
	}
}

static NMSettingWirelessSecurity *
get_security_for_ap (NMAccessPoint *ap,
                     guint32 dev_caps,
                     gboolean *supported,
                     NMSetting8021x **s_8021x)
{
	NMSettingWirelessSecurity *sec;
	NM80211Mode mode;
	guint32 flags;
	guint32 wpa_flags;
	guint32 rsn_flags;

	g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), NULL);
	g_return_val_if_fail (supported != NULL, NULL);
	g_return_val_if_fail (*supported == TRUE, NULL);
	g_return_val_if_fail (s_8021x != NULL, NULL);
	g_return_val_if_fail (*s_8021x == NULL, NULL);

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
	if (flags & NM_802_11_AP_FLAGS_PRIVACY) {
		if ((dev_caps & NM_WIFI_DEVICE_CAP_RSN) || (dev_caps & NM_WIFI_DEVICE_CAP_WPA)) {
			/* If the device can do WPA/RSN but the AP has no WPA/RSN informatoin
			 * elements, it must be LEAP or static/dynamic WEP.
			 */
			if ((wpa_flags == NM_802_11_AP_SEC_NONE) && (rsn_flags == NM_802_11_AP_SEC_NONE)) {
				g_object_set (sec,
				              NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "none",
				              NM_SETTING_WIRELESS_SECURITY_WEP_TX_KEYIDX, 0,
				              NULL);
				return sec;
			}
			/* Otherwise, the AP supports WPA or RSN, which is preferred */
		} else {
			/* Device can't do WPA/RSN, but can at least pass through the
			 * WPA/RSN information elements from a scan.  Since Privacy was
			 * advertised, LEAP or static/dynamic WEP must be in use.
			 */
			g_object_set (sec,
			              NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "none",
			              NM_SETTING_WIRELESS_SECURITY_WEP_TX_KEYIDX, 0,
			              NULL);
			return sec;
		}
	}

	/* Stuff after this point requires infrastructure */
	if (mode != NM_802_11_MODE_INFRA) {
		*supported = FALSE;
		goto none;
	}

	/* WPA2 PSK first */
	if (   (rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK)
	    && (dev_caps & NM_WIFI_DEVICE_CAP_RSN)) {
		g_object_set (sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", NULL);
		nm_setting_wireless_security_add_proto (sec, "rsn");
		add_ciphers_from_flags (sec, rsn_flags, TRUE);
		add_ciphers_from_flags (sec, rsn_flags, FALSE);
		return sec;
	}

	/* WPA PSK */
	if (   (wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK)
	    && (dev_caps & NM_WIFI_DEVICE_CAP_WPA)) {
		g_object_set (sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", NULL);
		nm_setting_wireless_security_add_proto (sec, "wpa");
		add_ciphers_from_flags (sec, wpa_flags, TRUE);
		add_ciphers_from_flags (sec, wpa_flags, FALSE);
		return sec;
	}

	/* WPA2 Enterprise */
	if (   (rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
	    && (dev_caps & NM_WIFI_DEVICE_CAP_RSN)) {
		g_object_set (sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-eap", NULL);
		nm_setting_wireless_security_add_proto (sec, "rsn");
		add_ciphers_from_flags (sec, rsn_flags, TRUE);
		add_ciphers_from_flags (sec, rsn_flags, FALSE);

		*s_8021x = NM_SETTING_802_1X (nm_setting_802_1x_new ());
		nm_setting_802_1x_add_eap_method (*s_8021x, "ttls");
		g_object_set (*s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "mschapv2", NULL);
		return sec;
	}

	/* WPA Enterprise */
	if (   (wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
	    && (dev_caps & NM_WIFI_DEVICE_CAP_WPA)) {
		g_object_set (sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-eap", NULL);
		nm_setting_wireless_security_add_proto (sec, "wpa");
		add_ciphers_from_flags (sec, wpa_flags, TRUE);
		add_ciphers_from_flags (sec, wpa_flags, FALSE);

		*s_8021x = NM_SETTING_802_1X (nm_setting_802_1x_new ());
		nm_setting_802_1x_add_eap_method (*s_8021x, "ttls");
		g_object_set (*s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "mschapv2", NULL);
		return sec;
	}

	*supported = FALSE;

none:
	g_object_unref (sec);
	return NULL;
}

static gboolean
wireless_new_auto_connection (NMDevice *device,
                              gpointer dclass_data,
                              AppletNewAutoConnectionCallback callback,
                              gpointer callback_data)
{
	WirelessMenuItemInfo *info = (WirelessMenuItemInfo *) dclass_data;
	NMConnection *connection = NULL;
	NMSettingConnection *s_con = NULL;
	NMSettingWireless *s_wireless = NULL;
	NMSettingWirelessSecurity *s_wireless_sec = NULL;
	NMSetting8021x *s_8021x = NULL;
	const GByteArray *ap_ssid;
	char *id;
	char buf[33];
	int buf_len;
	NM80211Mode mode;
	guint32 dev_caps;
	gboolean supported = TRUE;

	if (!info->ap) {
		g_warning ("%s: AP not set", __func__);
		return FALSE;
	}

	s_wireless = (NMSettingWireless *) nm_setting_wireless_new ();

	ap_ssid = nm_access_point_get_ssid (info->ap);
	g_object_set (s_wireless, NM_SETTING_WIRELESS_SSID, ap_ssid, NULL);

	mode = nm_access_point_get_mode (info->ap);
	if (mode == NM_802_11_MODE_ADHOC)
		g_object_set (s_wireless, NM_SETTING_WIRELESS_MODE, "adhoc", NULL);
	else if (mode == NM_802_11_MODE_INFRA)
		g_object_set (s_wireless, NM_SETTING_WIRELESS_MODE, "infrastructure", NULL);
	else
		g_assert_not_reached ();

	dev_caps = nm_device_wifi_get_capabilities (NM_DEVICE_WIFI (device));
	s_wireless_sec = get_security_for_ap (info->ap, dev_caps, &supported, &s_8021x);
	if (!supported) {
		g_warning ("Unsupported AP configuration: dev_caps 0x%X, ap_flags 0x%X, "
		           "wpa_flags 0x%X, rsn_flags 0x%x, mode %d",
		           dev_caps,
		           nm_access_point_get_flags (info->ap),
		           nm_access_point_get_wpa_flags (info->ap),
		           nm_access_point_get_rsn_flags (info->ap),
		           nm_access_point_get_mode (info->ap));
		g_object_unref (s_wireless);
		return FALSE;
	} else if (s_wireless_sec)
		g_object_set (s_wireless, NM_SETTING_WIRELESS_SEC, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL);

	connection = nm_connection_new ();
	nm_connection_add_setting (connection, NM_SETTING (s_wireless));
	if (s_wireless_sec)
		nm_connection_add_setting (connection, NM_SETTING (s_wireless_sec));
	if (s_8021x)
		nm_connection_add_setting (connection, NM_SETTING (s_8021x));

	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	g_object_set (s_con,
			    NM_SETTING_CONNECTION_TYPE, nm_setting_get_name (NM_SETTING (s_wireless)),
			    NM_SETTING_CONNECTION_AUTOCONNECT, !is_manufacturer_default_ssid (ap_ssid),
			    NULL);

	memset (buf, 0, sizeof (buf));
	buf_len = MIN(ap_ssid->len, sizeof (buf) - 1);
	memcpy (buf, ap_ssid->data, buf_len);
	id = g_strdup_printf ("Auto %s", nm_utils_ssid_to_utf8 (buf, buf_len));
	g_object_set (s_con, NM_SETTING_CONNECTION_ID, id, NULL);
	g_free (id);

	id = nm_utils_uuid_generate ();
	g_object_set (s_con, NM_SETTING_CONNECTION_UUID, id, NULL);
	g_free (id);

	nm_connection_add_setting (connection, NM_SETTING (s_con));

	(*callback) (connection, TRUE, FALSE, callback_data);
	return TRUE;
}

static void
wireless_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	WirelessMenuItemInfo *info = (WirelessMenuItemInfo *) user_data;
	const char *specific_object = NULL;

	if (info->ap)
		specific_object = nm_object_get_path (NM_OBJECT (info->ap));
	applet_menu_item_activate_helper (NM_DEVICE (info->device),
	                                  info->connection,
	                                  specific_object ? specific_object : "/",
	                                  info->applet,
	                                  user_data);
}

struct dup_data {
	NMDevice *device;
	NMNetworkMenuItem *found;
	char *hash;
};

static void
find_duplicate (gpointer d, gpointer user_data)
{
	struct dup_data * data = (struct dup_data *) user_data;
	NMDevice *device;
	const char *hash;
	GtkWidget *widget = GTK_WIDGET (d);

	g_assert (d && widget);
	g_return_if_fail (data);
	g_return_if_fail (data->hash);

	if (data->found || !NM_IS_NETWORK_MENU_ITEM (widget))
		return;

	device = g_object_get_data (G_OBJECT (widget), "device");
	if (NM_DEVICE (device) != data->device)
		return;

	hash = nm_network_menu_item_get_hash (NM_NETWORK_MENU_ITEM (widget));
	if (hash && (strcmp (hash, data->hash) == 0))
		data->found = NM_NETWORK_MENU_ITEM (widget);
}

static GSList *
filter_connections_for_access_point (GSList *connections, NMDeviceWifi *device, NMAccessPoint *ap)
{
	GSList *ap_connections = NULL;
	GSList *iter;

	for (iter = connections; iter; iter = g_slist_next (iter)) {
		NMConnection *candidate = NM_CONNECTION (iter->data);

		if (utils_connection_valid_for_device (candidate, NM_DEVICE (device), (gpointer) ap))
			ap_connections = g_slist_append (ap_connections, candidate);
	}
	return ap_connections;
}

static NMNetworkMenuItem *
create_new_ap_item (NMDeviceWifi *device,
                    NMAccessPoint *ap,
                    struct dup_data *dup_data,
                    GSList *connections,
                    NMApplet *applet)
{
	WirelessMenuItemInfo *info;
	GSList *iter;
	NMNetworkMenuItem *item = NULL;
	GSList *ap_connections = NULL;
	const GByteArray *ssid;
	guint32 dev_caps;

	ap_connections = filter_connections_for_access_point (connections, device, ap);

	item = NM_NETWORK_MENU_ITEM (nm_network_menu_item_new (dup_data->hash,
	                                                       !!g_slist_length (ap_connections)));
	gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);

	ssid = nm_access_point_get_ssid (ap);
	nm_network_menu_item_set_ssid (item, (GByteArray *) ssid);

	dev_caps = nm_device_wifi_get_capabilities (device);
	nm_network_menu_item_set_detail (item, ap, applet->adhoc_icon, dev_caps);
	nm_network_menu_item_best_strength (item, nm_access_point_get_strength (ap), applet);
	nm_network_menu_item_add_dupe (item, ap);

	g_object_set_data (G_OBJECT (item), "device", NM_DEVICE (device));

	/* If there's only one connection, don't show the submenu */
	if (g_slist_length (ap_connections) > 1) {
		GtkWidget *submenu;

		submenu = gtk_menu_new ();

		for (iter = ap_connections; iter; iter = g_slist_next (iter)) {
			NMConnection *connection = NM_CONNECTION (iter->data);
			NMSettingConnection *s_con;
			GtkWidget *subitem;

			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			subitem = gtk_menu_item_new_with_label (nm_setting_connection_get_id (s_con));

			info = g_slice_new0 (WirelessMenuItemInfo);
			info->applet = applet;
			info->device = g_object_ref (G_OBJECT (device));
			info->ap = g_object_ref (G_OBJECT (ap));
			info->connection = g_object_ref (G_OBJECT (connection));

			g_signal_connect_data (subitem, "activate",
			                       G_CALLBACK (wireless_menu_item_activate),
			                       info,
			                       (GClosureNotify) wireless_menu_item_info_destroy, 0);

			gtk_menu_shell_append (GTK_MENU_SHELL (submenu), GTK_WIDGET (subitem));
		}

		gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	} else {
		NMConnection *connection;

		info = g_slice_new0 (WirelessMenuItemInfo);
		info->applet = applet;
		info->device = g_object_ref (G_OBJECT (device));
		info->ap = g_object_ref (G_OBJECT (ap));

		if (g_slist_length (ap_connections) == 1) {
			connection = NM_CONNECTION (g_slist_nth_data (ap_connections, 0));
			info->connection = g_object_ref (G_OBJECT (connection));
		}

		g_signal_connect_data (GTK_WIDGET (item),
		                       "activate",
		                       G_CALLBACK (wireless_menu_item_activate),
		                       info,
		                       (GClosureNotify) wireless_menu_item_info_destroy,
		                       0);
	}

	g_slist_free (ap_connections);
	return item;
}

static NMNetworkMenuItem *
get_menu_item_for_ap (NMDeviceWifi *device,
                      NMAccessPoint *ap,
                      GSList *connections,
                      GSList *menu_list,
                      NMApplet *applet)
{
	const GByteArray *ssid;
	struct dup_data dup_data = { NULL, NULL };

	/* Don't add BSSs that hide their SSID */
	ssid = nm_access_point_get_ssid (ap);
	if (!ssid || nm_utils_is_empty_ssid (ssid->data, ssid->len))
		return NULL;

	/* Find out if this AP is a member of a larger network that all uses the
	 * same SSID and security settings.  If so, we'll already have a menu item
	 * for this SSID, so just update that item's strength and add this AP to
	 * menu item's duplicate list.
	 */
	dup_data.found = NULL;
	dup_data.hash = g_object_get_data (G_OBJECT (ap), "hash");
	g_return_val_if_fail (dup_data.hash != NULL, NULL);

	dup_data.device = NM_DEVICE (device);
	g_slist_foreach (menu_list, find_duplicate, &dup_data);

	if (dup_data.found) {
		nm_network_menu_item_best_strength (dup_data.found, nm_access_point_get_strength (ap), applet);
		nm_network_menu_item_add_dupe (dup_data.found, ap);
		return NULL;
	}

	return create_new_ap_item (device, ap, &dup_data, connections, applet);
}

static gint
sort_by_name (gconstpointer tmpa, gconstpointer tmpb)
{
	NMNetworkMenuItem *a = NM_NETWORK_MENU_ITEM (tmpa);
	NMNetworkMenuItem *b = NM_NETWORK_MENU_ITEM (tmpb);
	const char *a_ssid, *b_ssid;
	gboolean a_adhoc, b_adhoc;
	int i;

	if (a && !b)
		return 1;
	else if (!a && b)
		return -1;
	else if (a == b)
		return 0;

	a_ssid = nm_network_menu_item_get_ssid (a);
	b_ssid = nm_network_menu_item_get_ssid (b);

	if (a_ssid && !b_ssid)
		return 1;
	if (b_ssid && !a_ssid)
		return -1;

	if (a_ssid && b_ssid) {
		i = g_ascii_strcasecmp (a_ssid, b_ssid);
		if (i != 0)
			return i;
	}

	/* If the names are the same, sort infrastructure APs first */
	a_adhoc = nm_network_menu_item_get_is_adhoc (a);
	b_adhoc = nm_network_menu_item_get_is_adhoc (b);
	if (a_adhoc && !b_adhoc)
		return 1;
	else if (!a_adhoc && b_adhoc)
		return -1;

	return 0;
}

/* Sort menu items for the top-level menu:
 * 1) whether there's a saved connection or not
 *    a) sort alphabetically within #1
 * 2) encrypted without a saved connection
 * 3) unencrypted without a saved connection
 */
static gint
sort_toplevel (gconstpointer tmpa, gconstpointer tmpb)
{
	NMNetworkMenuItem *a = NM_NETWORK_MENU_ITEM (tmpa);
	NMNetworkMenuItem *b = NM_NETWORK_MENU_ITEM (tmpb);
	gboolean a_fave, b_fave;

	if (a && !b)
		return 1;
	else if (!a && b)
		return -1;
	else if (a == b)
		return 0;

	a_fave = nm_network_menu_item_get_has_connections (a);
	b_fave = nm_network_menu_item_get_has_connections (b);

	/* Items with a saved connection first */
	if (a_fave && !b_fave)
		return -1;
	else if (!a_fave && b_fave)
		return 1;
	else if (!a_fave && !b_fave) {
		gboolean a_enc = nm_network_menu_item_get_is_encrypted (a);
		gboolean b_enc = nm_network_menu_item_get_is_encrypted (b);

		/* If neither item has a saved connection, sort by encryption */
		if (a_enc && !b_enc)
			return -1;
		else if (!a_enc && b_enc)
			return 1;
	}

	/* For all other cases (both have saved connections, both are encrypted, or
	 * both are unencrypted) just sort by name.
	 */
	return sort_by_name (a, b);
}

static void
wireless_add_menu_item (NMDevice *device,
                        guint32 n_devices,
                        NMConnection *active,
                        GtkWidget *menu,
                        NMApplet *applet)
{
	NMDeviceWifi *wdev;
	char *text;
	const GPtrArray *aps;
	int i;
	NMAccessPoint *active_ap = NULL;
	GSList *connections = NULL, *all, *iter;
	gboolean wireless_enabled = TRUE;
	GSList *menu_items = NULL;  /* All menu items we'll be adding */
	NMNetworkMenuItem *item, *active_item = NULL;
	GtkWidget *widget;

	wdev = NM_DEVICE_WIFI (device);
	aps = nm_device_wifi_get_access_points (wdev);

	if (n_devices > 1) {
		char *desc;

		desc = (char *) utils_get_device_description (device);
		if (!desc)
			desc = (char *) nm_device_get_iface (device);
		g_assert (desc);

		if (aps && aps->len > 1)
			text = g_strdup_printf (_("Wireless Networks (%s)"), desc);
		else
			text = g_strdup_printf (_("Wireless Network (%s)"), desc);
	} else
		text = g_strdup (ngettext ("Wireless Network", "Wireless Networks", aps ? aps->len : 0));

	widget = applet_menu_item_create_device_item_helper (device, applet, text);
	g_free (text);

	gtk_widget_set_sensitive (widget, FALSE);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
	gtk_widget_show (widget);

	all = applet_get_all_connections (applet);
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	/* Add the active AP if we're connected to something and the device is available */
	if (!nma_menu_device_check_unusable (device)) {
		active_ap = nm_device_wifi_get_active_access_point (wdev);
		if (active_ap) {
			active_item = item = get_menu_item_for_ap (wdev, active_ap, connections, NULL, applet);
			nm_network_menu_item_set_active (item, TRUE);
			menu_items = g_slist_append (menu_items, item);

			gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (item));
			gtk_widget_show_all (GTK_WIDGET (item));
		}
	}

	/* Notify user of unmanaged or unavailable device */
	wireless_enabled = nm_client_wireless_get_enabled (applet->nm_client);
	widget = nma_menu_device_get_menu_item (device, applet, wireless_enabled ? NULL : _("wireless is disabled"));
	if (widget) {
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		gtk_widget_show (widget);
	}

	/* If disabled or rfkilled or whatever, nothing left to do */
	if (nma_menu_device_check_unusable (device))
		goto out;

	/* Create menu items for the rest of the APs */
	for (i = 0; aps && (i < aps->len); i++) {
		NMAccessPoint *ap = g_ptr_array_index (aps, i);

		item = get_menu_item_for_ap (wdev, ap, connections, menu_items, applet);
		if (item)
			menu_items = g_slist_append (menu_items, item);
	}

	/* Now remove the active AP item from the list, as we've already dealt with
	 * it.  (Needed it when creating menu items for the rest of the APs though
	 * to ensure duplicate APs are handled correctly)
	 */
	if (active_item)
		menu_items = g_slist_remove (menu_items, active_item);

	/* Sort all the rest of the menu items for the top-level menu */
	menu_items = g_slist_sort (menu_items, sort_toplevel);

	if (g_slist_length (menu_items)) {
		GSList *submenu_items = NULL;
		GSList *topmenu_items = NULL;
		guint32 num_for_toplevel = 5;

		applet_menu_item_add_complex_separator_helper (menu, applet, _("Available"), -1);

		if (g_slist_length (menu_items) == (num_for_toplevel + 1))
			num_for_toplevel++;

		/* Add the first 5 APs (or 6 if there are only 6 total) from the sorted
		 * toplevel list.
		 */
		for (iter = menu_items; iter && num_for_toplevel; iter = g_slist_next (iter)) {
			topmenu_items = g_slist_append (topmenu_items, iter->data);
			num_for_toplevel--;
			submenu_items = iter->next;
		}
		topmenu_items = g_slist_sort (topmenu_items, sort_by_name);

		for (iter = topmenu_items; iter; iter = g_slist_next (iter)) {
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (iter->data));
			gtk_widget_show_all (GTK_WIDGET (iter->data));
		}
		g_slist_free (topmenu_items);
		topmenu_items = NULL;

		/* If there are any submenu items, make a submenu for those */
		if (submenu_items) {
			GtkWidget *subitem, *submenu;
			GSList *sorted_subitems;

			subitem = gtk_menu_item_new_with_mnemonic (_("More networks"));
			submenu = gtk_menu_new ();
			gtk_menu_item_set_submenu (GTK_MENU_ITEM (subitem), submenu);

			/* Sort the subitems alphabetically */
			sorted_subitems = g_slist_copy (submenu_items);
			sorted_subitems = g_slist_sort (sorted_subitems, sort_by_name);

			/* And add the rest to the submenu */
			for (iter = sorted_subitems; iter; iter = g_slist_next (iter))
				gtk_menu_shell_append (GTK_MENU_SHELL (submenu), GTK_WIDGET (iter->data));
			g_slist_free (sorted_subitems);

			gtk_menu_shell_append (GTK_MENU_SHELL (menu), subitem);
			gtk_widget_show_all (subitem);
		}
	}

out:
	g_slist_free (menu_items);
	g_slist_free (connections);
}

static gboolean
add_seen_bssid (NMSettingsConnectionInterface *connection, NMAccessPoint *ap)
{
	NMSettingWireless *s_wireless;
	const char *bssid;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (NM_CONNECTION (connection), NM_TYPE_SETTING_WIRELESS));
	if (!s_wireless)
		return FALSE;

	bssid = nm_access_point_get_hw_address (ap);
	if (!bssid || !utils_ether_addr_valid (ether_aton (bssid)))
		return FALSE;

	return nm_setting_wireless_add_seen_bssid (s_wireless, bssid);
}

static void
bssid_update_cb (NMSettingsConnectionInterface *connection,
                 GError *error,
                 gpointer user_data)
{
	if (error) {
		g_warning ("%s: failed to update connection seen BSSIDs: (%d) %s",
		           __func__, error->code, error->message);
	}
}

static void
notify_active_ap_changed_cb (NMDeviceWifi *device,
                             GParamSpec *pspec,
                             NMApplet *applet)
{
	NMSettingsConnectionInterface *connection;
	NMSettingWireless *s_wireless;
	NMAccessPoint *new;
	const GByteArray *ssid;
	NMDeviceState state;

	state = nm_device_get_state (NM_DEVICE (device));

	new = update_active_ap (NM_DEVICE (device), state, applet);
	if (!new || (state != NM_DEVICE_STATE_ACTIVATED))
		return;

	connection = applet_get_exported_connection_for_device (NM_DEVICE (device), applet);
	if (!connection)
		return;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (NM_CONNECTION (connection), NM_TYPE_SETTING_WIRELESS));
	if (!s_wireless)
		return;

	ssid = nm_access_point_get_ssid (new);
	if (!ssid || !nm_utils_same_ssid (nm_setting_wireless_get_ssid (s_wireless), ssid, TRUE))
		return;

	if (add_seen_bssid (connection, new))
		nm_settings_connection_interface_update (connection, bssid_update_cb, NULL);

	applet_schedule_update_icon (applet);
}

static void
add_hash_to_ap (NMAccessPoint *ap)
{
	char *hash;

	hash = utils_hash_ap (nm_access_point_get_ssid (ap),
	                      nm_access_point_get_mode (ap),
	                      nm_access_point_get_flags (ap),
	                      nm_access_point_get_wpa_flags (ap),
	                      nm_access_point_get_rsn_flags (ap));
	g_object_set_data_full (G_OBJECT (ap), "hash", hash, (GDestroyNotify) g_free);
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
wifi_available_dont_show_cb (NotifyNotification *notify,
			                 gchar *id,
			                 gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	if (!id || strcmp (id, "dont-show"))
		return;

	gconf_client_set_bool (applet->gconf_client,
	                       PREF_SUPPRESS_WIRELESS_NEWORKS_AVAILABLE,
	                       TRUE,
	                       NULL);
}


struct ap_notification_data 
{
	NMApplet *applet;
	NMDeviceWifi *device;
	guint id;
	gulong last_notification_time;
	guint new_con_id;
};

/* Scan the list of access points, looking for the case where we have no
 * known (i.e. autoconnect) access points, but we do have unknown ones.
 * 
 * If we find one, notify the user.
 */
static gboolean
idle_check_avail_access_point_notification (gpointer datap)
{	
	struct ap_notification_data *data = datap;
	NMApplet *applet = data->applet;
	NMDeviceWifi *device = data->device;
	int i;
	const GPtrArray *aps;
	GSList *all_connections;
	GSList *connections;
	GTimeVal timeval;
	gboolean have_unused_access_point = FALSE;
	gboolean have_no_autoconnect_points = TRUE;

	if (nm_client_get_state (data->applet->nm_client) != NM_STATE_DISCONNECTED)
		return FALSE;

	if (nm_device_get_state (NM_DEVICE (device)) != NM_DEVICE_STATE_DISCONNECTED)
		return FALSE;

	g_get_current_time (&timeval);
	if ((timeval.tv_sec - data->last_notification_time) < 60*60) /* Notify at most once an hour */
		return FALSE;	

	all_connections = applet_get_all_connections (applet);
	connections = utils_filter_connections_for_device (NM_DEVICE (device), all_connections);
	g_slist_free (all_connections);	
	all_connections = NULL;

	aps = nm_device_wifi_get_access_points (device);
	for (i = 0; aps && (i < aps->len); i++) {
		NMAccessPoint *ap = aps->pdata[i];
		GSList *ap_connections = filter_connections_for_access_point (connections, device, ap);
		GSList *iter;
		gboolean is_autoconnect = FALSE;

		for (iter = ap_connections; iter; iter = g_slist_next (iter)) {
			NMConnection *connection = NM_CONNECTION (iter->data);
			NMSettingConnection *s_con;

			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			if (nm_setting_connection_get_autoconnect (s_con))  {
				is_autoconnect = TRUE;
				break;
			}
		}
		g_slist_free (ap_connections);

		if (!is_autoconnect)
			have_unused_access_point = TRUE;
		else
			have_no_autoconnect_points = FALSE;
	}

	if (!(have_unused_access_point && have_no_autoconnect_points))
		return FALSE;

	/* Avoid notifying too often */
	g_get_current_time (&timeval);
	data->last_notification_time = timeval.tv_sec;

	applet_do_notify (applet,
	                  NOTIFY_URGENCY_LOW,
	                  _("Wireless Networks Available"),
	                  _("Click on this icon to connect to a wireless network"),
	                  "nm-device-wireless",
	                  "dont-show",
	                  _("Don't show this message again"),
	                  wifi_available_dont_show_cb,
	                  applet);
	return FALSE;
}

static void
queue_avail_access_point_notification (NMDevice *device)
{
	struct ap_notification_data *data;

	data = g_object_get_data (G_OBJECT (device), "notify-wireless-avail-data");	
	if (data->id != 0)
		return;

	if (gconf_client_get_bool (data->applet->gconf_client,
	                           PREF_SUPPRESS_WIRELESS_NEWORKS_AVAILABLE,
	                           NULL))
		return;

	data->id = g_timeout_add_seconds (3, idle_check_avail_access_point_notification, data);
}

static void
access_point_added_cb (NMDeviceWifi *device,
                       NMAccessPoint *ap,
                       gpointer user_data)
{
	NMApplet *applet = NM_APPLET  (user_data);

	add_hash_to_ap (ap);
	g_signal_connect (G_OBJECT (ap),
	                  "notify",
	                  G_CALLBACK (notify_ap_prop_changed_cb),
	                  applet);
	
	queue_avail_access_point_notification (NM_DEVICE (device));
}

static void
access_point_removed_cb (NMDeviceWifi *device,
                         NMAccessPoint *ap,
                         gpointer user_data)
{
	NMApplet *applet = NM_APPLET  (user_data);
	NMAccessPoint *old;

	/* If this AP was the active AP, make sure ACTIVE_AP_TAG gets cleared from
	 * its device.
	 */
	old = g_object_get_data (G_OBJECT (device), ACTIVE_AP_TAG);
	if (old == ap) {
		g_object_set_data (G_OBJECT (device), ACTIVE_AP_TAG, NULL);
		applet_schedule_update_icon (applet);
	}
}

static void
on_new_connection (NMSettingsInterface *settings,
                   NMSettingsConnectionInterface *connection,
                   gpointer datap)
{
	struct ap_notification_data *data = datap;
	queue_avail_access_point_notification (NM_DEVICE (data->device));
}

static void
free_ap_notification_data (gpointer user_data)
{
	struct ap_notification_data *data = user_data;
	NMSettingsInterface *settings = applet_get_settings (data->applet);

	if (data->id)
		g_source_remove (data->id);

	if (settings)
		g_signal_handler_disconnect (settings, data->new_con_id);
	memset (data, 0, sizeof (*data));
	g_free (data);
}

static void
wireless_device_added (NMDevice *device, NMApplet *applet)
{
	NMDeviceWifi *wdev = NM_DEVICE_WIFI (device);
	const GPtrArray *aps;
	int i;
	struct ap_notification_data *data;
	guint id;

	g_signal_connect (wdev,
	                  "notify::" NM_DEVICE_WIFI_ACTIVE_ACCESS_POINT,
	                  G_CALLBACK (notify_active_ap_changed_cb),
	                  applet);

	g_signal_connect (wdev,
	                  "access-point-added",
	                  G_CALLBACK (access_point_added_cb),
	                  applet);

	g_signal_connect (wdev,
	                  "access-point-removed",
	                  G_CALLBACK (access_point_removed_cb),
	                  applet);

	/* Now create the per-device hooks for watching for available wireless
	 * connections.
	 */
	data = g_new0 (struct ap_notification_data, 1);
	data->applet = applet;
	data->device = wdev;
	/* We also need to hook up to the settings to find out when we have new connections
	 * that might be candididates.  Keep the ID around so we can disconnect
	 * when the device is destroyed.
	 */ 
	id = g_signal_connect (applet_get_settings (applet),
	                       NM_SETTINGS_INTERFACE_NEW_CONNECTION,
	                       G_CALLBACK (on_new_connection),
	                       data);
	data->new_con_id = id;
	g_object_set_data_full (G_OBJECT (wdev), "notify-wireless-avail-data",
	                        data, free_ap_notification_data);

	queue_avail_access_point_notification (device);

	/* Hash all APs this device knows about */
	aps = nm_device_wifi_get_access_points (wdev);
	for (i = 0; aps && (i < aps->len); i++)
		add_hash_to_ap (g_ptr_array_index (aps, i));
}

static void
bssid_strength_changed (NMAccessPoint *ap, GParamSpec *pspec, gpointer user_data)
{
	applet_schedule_update_icon (NM_APPLET (user_data));
}

static NMAccessPoint *
update_active_ap (NMDevice *device, NMDeviceState state, NMApplet *applet)
{
	NMAccessPoint *new = NULL, *old;

	if (state == NM_DEVICE_STATE_PREPARE ||
	    state == NM_DEVICE_STATE_CONFIG ||
	    state == NM_DEVICE_STATE_IP_CONFIG ||
	    state == NM_DEVICE_STATE_NEED_AUTH ||
	    state == NM_DEVICE_STATE_ACTIVATED) {
		new = nm_device_wifi_get_active_access_point (NM_DEVICE_WIFI (device));
	}

	old = g_object_get_data (G_OBJECT (device), ACTIVE_AP_TAG);
	if (new && (new == old))
		return new;   /* no change */

	if (old) {
		g_signal_handlers_disconnect_by_func (old, G_CALLBACK (bssid_strength_changed), applet);
		g_object_set_data (G_OBJECT (device), ACTIVE_AP_TAG, NULL);
	}

	if (new) {
		g_object_set_data (G_OBJECT (device), ACTIVE_AP_TAG, new);

		/* monitor this AP's signal strength for updating the applet icon */
		g_signal_connect (new,
		                  "notify::" NM_ACCESS_POINT_STRENGTH,
		                  G_CALLBACK (bssid_strength_changed),
		                  applet);
	}

	return new;
}

static void
wireless_device_state_changed (NMDevice *device,
                               NMDeviceState new_state,
                               NMDeviceState old_state,
                               NMDeviceStateReason reason,
                               NMApplet *applet)
{
	NMSettingsConnectionInterface *connection;
	NMAccessPoint *new = NULL;
	char *msg;
	char *esc_ssid = NULL;

	new = update_active_ap (device, new_state, applet);

	if (new_state == NM_DEVICE_STATE_DISCONNECTED)
		queue_avail_access_point_notification (device);

	if (new_state != NM_DEVICE_STATE_ACTIVATED)
		return;

	if (new) {
		const GByteArray *ssid = nm_access_point_get_ssid (new);

		if (ssid)
			esc_ssid = nm_utils_ssid_to_utf8 ((const char *) ssid->data, ssid->len);

		/* Save this BSSID to seen-bssids list */
		connection = applet_get_exported_connection_for_device (device, applet);
		if (connection && add_seen_bssid (connection, new))
			nm_settings_connection_interface_update (connection, bssid_update_cb, NULL);
	}

	msg = g_strdup_printf (_("You are now connected to the wireless network '%s'."),
	                       esc_ssid ? esc_ssid : _("(none)"));
	applet_do_notify_with_pref (applet, _("Connection Established"),
	                            msg, "nm-device-wireless",
	                            PREF_DISABLE_CONNECTED_NOTIFICATIONS);
	g_free (msg);
	g_free (esc_ssid);
}

static GdkPixbuf *
wireless_get_icon (NMDevice *device,
                   NMDeviceState state,
                   NMConnection *connection,
                   char **tip,
                   NMApplet *applet)
{
	NMSettingConnection *s_con;
	NMAccessPoint *ap;
	GdkPixbuf *pixbuf = NULL;
	const char *id;
	char *ssid = NULL;

	ap = g_object_get_data (G_OBJECT (device), ACTIVE_AP_TAG);
	if (ap) {
		const GByteArray *tmp;

		tmp = nm_access_point_get_ssid (ap);
		if (tmp)
			ssid = nm_utils_ssid_to_utf8 ((const char *) tmp->data, tmp->len);
	}

	if (!ssid)
		ssid = g_strdup (_("(none)"));

	id = nm_device_get_iface (device);
	if (connection) {
		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		id = nm_setting_connection_get_id (s_con);
	}

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		*tip = g_strdup_printf (_("Preparing wireless network connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_CONFIG:
		*tip = g_strdup_printf (_("Configuring wireless network connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_NEED_AUTH:
		*tip = g_strdup_printf (_("User authentication required for wireless network '%s'..."), id);
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		*tip = g_strdup_printf (_("Requesting a wireless network address for '%s'..."), id);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		if (ap) {
			guint32 strength;

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

			*tip = g_strdup_printf (_("Wireless network connection '%s' active: %s (%d%%)"),
			                        id, ssid, strength);
		} else {
			pixbuf = applet->wireless_00_icon;
			*tip = g_strdup_printf (_("Wireless network connection '%s' active"), id);
		}
		break;
	default:
		break;
	}

	g_free (ssid);
	return pixbuf;
}

static void
activate_device_cb (gpointer user_data, const char *path, GError *error)
{
	if (error)
		nm_warning ("Device Activation failed: %s", error->message);
	applet_schedule_update_icon (NM_APPLET (user_data));
}

static gboolean
wireless_dialog_close (gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET (user_data);

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	return FALSE;
}

static void
nag_dialog_response_cb (GtkDialog *nag_dialog,
                        gint response,
                        gpointer user_data)
{
	NMAWirelessDialog *wireless_dialog = NMA_WIRELESS_DIALOG (user_data);

	if (response == GTK_RESPONSE_NO) {  /* user opted not to correct the warning */
		nma_wireless_dialog_set_nag_ignored (wireless_dialog, TRUE);
		g_idle_add (wireless_dialog_close, wireless_dialog);
	}
}

static void
update_cb (NMSettingsConnectionInterface *connection,
           GError *error,
           gpointer user_data)
{
	if (error) {
		g_warning ("%s: failed to update connection: (%d) %s",
		           __func__, error->code, error->message);
	}
}

static void
wireless_dialog_response_cb (GtkDialog *foo,
                             gint response,
                             gpointer user_data)
{
	NMAWirelessDialog *dialog = NMA_WIRELESS_DIALOG (foo);
	NMApplet *applet = NM_APPLET (user_data);
	NMConnection *connection = NULL, *fuzzy_match = NULL;
	NMDevice *device = NULL;
	NMAccessPoint *ap = NULL;
	const char *service = NM_DBUS_SERVICE_USER_SETTINGS;

	if (response != GTK_RESPONSE_OK)
		goto done;

	if (!nma_wireless_dialog_get_nag_ignored (dialog)) {
		GtkWidget *nag_dialog;

		/* Nag the user about certificates or whatever.  Only destroy the dialog
		 * if no nagging was done.
		 */
		nag_dialog = nma_wireless_dialog_nag_user (dialog);
		if (nag_dialog) {
			gtk_window_set_transient_for (GTK_WINDOW (nag_dialog), GTK_WINDOW (dialog));
			g_signal_connect (nag_dialog, "response",
			                  G_CALLBACK (nag_dialog_response_cb),
			                  dialog);
			return;
		}
	}

	/* nma_wireless_dialog_get_connection() returns a connection with the
	 * refcount incremented, so the caller must remember to unref it.
	 */
	connection = nma_wireless_dialog_get_connection (dialog, &device, &ap);
	g_assert (connection);
	g_assert (device);

	/* If it's a system connection we just need to tell NM to activate it */
	if (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_SYSTEM) {
		service = NM_DBUS_SERVICE_SYSTEM_SETTINGS;
		goto activate;
	}

	if (NMA_IS_GCONF_CONNECTION (connection)) {
		/* Not a new or system connection, save the updated settings to GConf */
		nm_settings_connection_interface_update (NM_SETTINGS_CONNECTION_INTERFACE (connection),
		                                         update_cb,
		                                         NULL);
	} else {
		GSList *all, *iter;

		/* Find a similar connection and use that instead */
		all = applet_get_all_connections (applet);
		for (iter = all; iter; iter = g_slist_next (iter)) {
			if (nm_connection_compare (connection,
			                           NM_CONNECTION (iter->data),
			                           (NM_SETTING_COMPARE_FLAG_FUZZY | NM_SETTING_COMPARE_FLAG_IGNORE_ID))) {
				fuzzy_match = g_object_ref (NM_CONNECTION (iter->data));
				break;
			}
		}
		g_slist_free (all);

		if (fuzzy_match) {
			if (nm_connection_get_scope (fuzzy_match) == NM_CONNECTION_SCOPE_SYSTEM) {
				// FIXME: do something other than just use the system connection?
			} else {
				NMSettingWirelessSecurity *s_wireless_sec;

				/* Copy secrets & wireless security */
				s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY));
				if (s_wireless_sec) {
					GHashTable *hash;
					NMSetting *dup_setting;

					hash = nm_setting_to_hash (NM_SETTING (s_wireless_sec));
					dup_setting = nm_setting_new_from_hash (NM_TYPE_SETTING_WIRELESS_SECURITY, hash);
					g_hash_table_destroy (hash);
					nm_connection_add_setting (fuzzy_match, dup_setting);
				}
			}

			/* Balance nma_wireless_dialog_get_connection() */
			g_object_unref (connection);
			connection = g_object_ref (fuzzy_match);
		} else {
			/* Entirely new connection */
			NMAGConfConnection *new_gconf_connection;
			NMSettingConnection *s_con;
			char *id;

			/* Update a new connection's name and autoconnect status */
			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			id = (char *) nm_setting_connection_get_id (s_con);

			if (!id) {
				NMSettingWireless *s_wireless;
				const GByteArray *ssid;
				const char *mode;

				s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
				ssid = nm_setting_wireless_get_ssid (s_wireless);

				id = nm_utils_ssid_to_utf8 ((const char *) ssid->data, ssid->len);
				g_object_set (s_con, NM_SETTING_CONNECTION_ID, id, NULL);
				g_free (id);

				// FIXME: don't autoconnect until the connection is successful at least once
				/* Don't autoconnect adhoc networks by default for now */
				mode = nm_setting_wireless_get_mode (s_wireless);
				if (!mode || !strcmp (mode, "infrastructure"))
					g_object_set (s_con, NM_SETTING_CONNECTION_AUTOCONNECT, TRUE, NULL);
			}

			/* Export it over D-Bus */
			new_gconf_connection = nma_gconf_settings_add_connection (applet->gconf_settings, connection);
			if (!new_gconf_connection) {
				nm_warning ("Couldn't create new network connection.");
				goto done;
			}

			/* Balance nma_wireless_dialog_get_connection() */
			g_object_unref (connection);
			connection = g_object_ref (new_gconf_connection);
		}
	}

activate:
	nm_client_activate_connection (applet->nm_client,
	                               service,
	                               nm_connection_get_path (connection),
	                               device,
	                               ap ? nm_object_get_path (NM_OBJECT (ap)) : NULL,
	                               activate_device_cb,
	                               applet);

done:
	/* Balance nma_wireless_dialog_get_connection() */
	if (connection)
		g_object_unref (connection);

	gtk_widget_hide (GTK_WIDGET (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
wireless_get_more_info (NMDevice *device,
                        NMConnection *connection,
                        NMApplet *applet,
                        gpointer user_data)
{
	WirelessMenuItemInfo *info = (WirelessMenuItemInfo *) user_data;
	GtkWidget *dialog;

	dialog = nma_wireless_dialog_new (applet, connection, device, info->ap);
	g_return_if_fail (dialog != NULL);

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (wireless_dialog_response_cb),
	                  applet);

	show_ignore_focus_stealing_prevention (dialog);
}

static gboolean
add_one_setting (GHashTable *settings,
                 NMConnection *connection,
                 NMSetting *setting,
                 GError **error)
{
	GHashTable *secrets;

	g_return_val_if_fail (settings != NULL, FALSE);
	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (setting != NULL, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);
	g_return_val_if_fail (*error == NULL, FALSE);

	secrets = nm_setting_to_hash (setting);
	if (secrets) {
		g_hash_table_insert (settings, g_strdup (nm_setting_get_name (setting)), secrets);
	} else {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): failed to hash setting '%s'.",
		             __FILE__, __LINE__, __func__, nm_setting_get_name (setting));
	}

	return secrets ? TRUE : FALSE;
}

typedef struct {
	NMApplet *applet;
	NMActiveConnection *active_connection;
	GtkWidget *dialog;
	GtkWidget *nag_dialog;
	NMANewSecretsRequestedFunc callback;
	gpointer callback_data;
	char *setting_name;
} NMWifiInfo;

static void
destroy_wifi_dialog (gpointer user_data, GObject *finalized)
{
	NMWifiInfo *info = user_data;

	gtk_widget_hide (info->dialog);
	gtk_widget_destroy (info->dialog);
	g_free (info->setting_name);
	g_free (info);
}

static void
get_secrets_dialog_response_cb (GtkDialog *foo,
                                gint response,
                                gpointer user_data)
{
	NMWifiInfo *info = user_data;
	NMAWirelessDialog *dialog = NMA_WIRELESS_DIALOG (info->dialog);
	NMConnection *connection = NULL;
	NMSettingWirelessSecurity *s_wireless_sec;
	NMDevice *device = NULL;
	GHashTable *settings = NULL;
	const char *key_mgmt, *auth_alg;
	GError *error = NULL;

	/* Handle the nag dialog specially; don't want to clear the NMActiveConnection
	 * destroy handler yet if the main dialog isn't going away.
	 */
	if ((response == GTK_RESPONSE_OK) && !nma_wireless_dialog_get_nag_ignored (dialog)) {
		GtkWidget *widget;

		/* Nag the user about certificates or whatever.  Only destroy the dialog
		 * if no nagging was done.
		 */
		widget = nma_wireless_dialog_nag_user (dialog);
		if (widget) {
			gtk_window_set_transient_for (GTK_WINDOW (widget), GTK_WINDOW (dialog));
			g_signal_connect (widget, "response",
			                  G_CALLBACK (nag_dialog_response_cb),
			                  dialog);
			return;
		}
	}

	/* Got a user response, clear the NMActiveConnection destroy handler for
	 * this dialog since this function will now take over dialog destruction.
	 */
	g_object_weak_unref (G_OBJECT (info->active_connection), destroy_wifi_dialog, info);

	if (response != GTK_RESPONSE_OK) {
		g_set_error (&error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_SECRETS_REQUEST_CANCELED,
		             "%s.%d (%s): canceled",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	connection = nma_wireless_dialog_get_connection (dialog, &device, NULL);
	if (!connection) {
		g_set_error (&error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): couldn't get connection from wireless dialog.",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	/* Second-guess which setting NM wants secrets for. */
	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY));
	if (!s_wireless_sec) {
		g_set_error (&error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INVALID_CONNECTION,
		             "%s.%d (%s): requested setting '802-11-wireless-security'"
		             " didn't exist in the connection.",
		             __FILE__, __LINE__, __func__);
		goto done;  /* Unencrypted */
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                  g_free, (GDestroyNotify) g_hash_table_destroy);
	if (!settings) {
		g_set_error (&error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): not enough memory to return secrets.",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	/* If the user chose an 802.1x-based auth method, return 802.1x secrets,
	 * not wireless secrets.  Can happen with Dynamic WEP, because NM doesn't
	 * know the capabilities of the AP (since Dynamic WEP APs don't broadcast
	 * beacons), and therefore defaults to requesting WEP secrets from the
	 * wireless-security setting, not the 802.1x setting.
	 */
	key_mgmt = nm_setting_wireless_security_get_key_mgmt (s_wireless_sec);
	if (!strcmp (key_mgmt, "ieee8021x") || !strcmp (key_mgmt, "wpa-eap")) {
		/* LEAP secrets aren't in the 802.1x setting */
		auth_alg = nm_setting_wireless_security_get_auth_alg (s_wireless_sec);
		if (!auth_alg || strcmp (auth_alg, "leap")) {
			NMSetting8021x *s_8021x;

			s_8021x = (NMSetting8021x *) nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
			if (!s_8021x) {
				g_set_error (&error,
				             NM_SETTINGS_INTERFACE_ERROR,
				             NM_SETTINGS_INTERFACE_ERROR_INVALID_CONNECTION,
				             "%s.%d (%s): requested setting '802-1x' didn't"
				             " exist in the connection.",
				             __FILE__, __LINE__, __func__);
				goto done;
			}

			/* Add the 802.1x setting */
			if (!add_one_setting (settings, connection, NM_SETTING (s_8021x), &error))
				goto done;
		}
	}

	/* Add the 802-11-wireless-security setting no matter what */
	if (!add_one_setting (settings, connection, NM_SETTING (s_wireless_sec), &error))
		goto done;

	info->callback (NM_SETTINGS_CONNECTION_INTERFACE (connection), settings, NULL, info->callback_data);

	/* Save the connection back to GConf _after_ hashing it, because
	 * saving to GConf might trigger the GConf change notifiers, resulting
	 * in the connection being read back in from GConf which clears secrets.
	 */
	if (NMA_IS_GCONF_CONNECTION (connection)) {
		nm_settings_connection_interface_update (NM_SETTINGS_CONNECTION_INTERFACE (connection),
		                                         update_cb,
		                                         NULL);
	}

done:
	if (settings)
		g_hash_table_destroy (settings);

	if (error) {
		g_warning ("%s", error->message);
		info->callback (NM_SETTINGS_CONNECTION_INTERFACE (connection), NULL, error, info->callback_data);
		g_error_free (error);
	}

	if (connection)
		nm_connection_clear_secrets (connection);

	destroy_wifi_dialog (info, NULL);
}

static gboolean
wireless_get_secrets (NMDevice *device,
                      NMSettingsConnectionInterface *connection,
                      NMActiveConnection *active_connection,
                      const char *setting_name,
                      const char **hints,
                      NMANewSecretsRequestedFunc callback,
                      gpointer callback_data,
                      NMApplet *applet,
                      GError **error)
{
	NMWifiInfo *info;
	NMAccessPoint *ap;
	const char *specific_object;

	if (!setting_name || !active_connection) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): setting name and active connection object required",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	specific_object = nm_active_connection_get_specific_object (active_connection);
	if (!specific_object) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): could not determine AP for specific object",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	info = g_malloc0 (sizeof (NMWifiInfo));

	ap = nm_device_wifi_get_access_point_by_path (NM_DEVICE_WIFI (device), specific_object);
	info->dialog = nma_wireless_dialog_new (applet, NM_CONNECTION (connection), device, ap);
	if (!info->dialog) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): couldn't display secrets UI",
		             __FILE__, __LINE__, __func__);
		g_free (info);
		return FALSE;
	}

	info->applet = applet;
	info->active_connection = active_connection;
	info->callback = callback;
	info->callback_data = callback_data;
	info->setting_name = g_strdup (setting_name);

	g_signal_connect (info->dialog, "response",
	                  G_CALLBACK (get_secrets_dialog_response_cb),
	                  info);

	/* Attach a destroy notifier to the NMActiveConnection so we can destroy
	 * the dialog when the active connection goes away.
	 */
	g_object_weak_ref (G_OBJECT (active_connection), destroy_wifi_dialog, info);

	show_ignore_focus_stealing_prevention (info->dialog);
	return TRUE;
}

NMADeviceClass *
applet_device_wifi_get_class (NMApplet *applet)
{
	NMADeviceClass *dclass;

	dclass = g_slice_new0 (NMADeviceClass);
	if (!dclass)
		return NULL;

	dclass->new_auto_connection = wireless_new_auto_connection;
	dclass->add_menu_item = wireless_add_menu_item;
	dclass->device_added = wireless_device_added;
	dclass->device_state_changed = wireless_device_state_changed;
	dclass->get_icon = wireless_get_icon;
	dclass->get_more_info = wireless_get_more_info;
	dclass->get_secrets = wireless_get_secrets;

	return dclass;
}

