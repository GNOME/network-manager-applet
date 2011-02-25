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
 * (C) Copyright 2007 - 2011 Red Hat, Inc.
 */

#include <config.h>
#include <string.h>
#include <netinet/ether.h>
#include <glib.h>

#include <nm-device-ethernet.h>
#include <nm-device-wifi.h>
#include <nm-device-bt.h>
#include <nm-device-modem.h>
#include <nm-device-wimax.h>
#include <nm-access-point.h>

#include <nm-setting-connection.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>
#include <nm-setting-8021x.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-setting-pppoe.h>
#include <nm-setting-bluetooth.h>
#include <nm-setting-wimax.h>
#include <nm-utils.h>

#include "utils.h"
#include "gconf-helpers.h"

static char *ignored_words[] = {
	"Semiconductor",
	"Components",
	"Corporation",
	"Communications",
	"Company",
	"Corp.",
	"Corp",
	"Co.",
	"Inc.",
	"Inc",
	"Incorporated",
	"Ltd.",
	"Limited.",
	"Intel?",
	"chipset",
	"adapter",
	"[hex]",
	"NDIS",
	"Module",
	NULL
};

static char *ignored_phrases[] = {
	"Multiprotocol MAC/baseband processor",
	"Wireless LAN Controller",
	"Wireless LAN Adapter",
	"Wireless Adapter",
	"Network Connection",
	"Wireless Cardbus Adapter",
	"Wireless CardBus Adapter",
	"54 Mbps Wireless PC Card",
	"Wireless PC Card",
	"Wireless PC",
	"PC Card with XJACK(r) Antenna",
	"Wireless cardbus",
	"Wireless LAN PC Card",
	"Technology Group Ltd.",
	"Communication S.p.A.",
	"Business Mobile Networks BV",
	"Mobile Broadband Minicard Composite Device",
	"Mobile Communications AB",
	"(PC-Suite Mode)",
	NULL
};

static char *
fixup_desc_string (const char *desc)
{
	char *p, *temp;
	char **words, **item;
	GString *str;

	p = temp = g_strdup (desc);
	while (*p) {
		if (*p == '_' || *p == ',')
			*p = ' ';
		p++;
	}

	/* Attempt to shorten ID by ignoring certain phrases */
	for (item = ignored_phrases; *item; item++) {
		guint32 ignored_len = strlen (*item);

		p = strstr (temp, *item);
		if (p)
			memmove (p, p + ignored_len, strlen (p + ignored_len) + 1); /* +1 for the \0 */
	}

	/* Attmept to shorten ID by ignoring certain individual words */
	words = g_strsplit (temp, " ", 0);
	str = g_string_new_len (NULL, strlen (temp));
	g_free (temp);

	for (item = words; *item; item++) {
		int i = 0;
		gboolean ignore = FALSE;

		if (g_ascii_isspace (**item) || (**item == '\0'))
			continue;

		while (ignored_words[i] && !ignore) {
			if (!strcmp (*item, ignored_words[i]))
				ignore = TRUE;
			i++;
		}

		if (!ignore) {
			if (str->len)
				g_string_append_c (str, ' ');
			g_string_append (str, *item);
		}
	}
	g_strfreev (words);

	temp = str->str;
	g_string_free (str, FALSE);

	return temp;
}

#define DESC_TAG "description"

const char *
utils_get_device_description (NMDevice *device)
{
	char *description = NULL;
	const char *dev_product;
	const char *dev_vendor;
	char *product = NULL;
	char *vendor = NULL;
	GString *str;

	g_return_val_if_fail (device != NULL, NULL);

	description = g_object_get_data (G_OBJECT (device), DESC_TAG);
	if (description)
		return description;

	dev_product = nm_device_get_product (device);
	dev_vendor = nm_device_get_vendor (device);
	if (!dev_product || !dev_vendor)
		return NULL;

	product = fixup_desc_string (dev_product);
	vendor = fixup_desc_string (dev_vendor);

	str = g_string_new_len (NULL, strlen (vendor) + strlen (product) + 1);

	/* Another quick hack; if all of the fixed up vendor string
	 * is found in product, ignore the vendor.
	 */
	if (!strcasestr (product, vendor)) {
		g_string_append (str, vendor);
		g_string_append_c (str, ' ');
	}

	g_string_append (str, product);
	g_free (product);
	g_free (vendor);

	description = str->str;
	g_string_free (str, FALSE);

	g_object_set_data_full (G_OBJECT (device),
	                        "description", description,
	                        (GDestroyNotify) g_free);

	return description;
}

/*
 * utils_ether_addr_valid
 *
 * Compares an Ethernet address against known invalid addresses.
 *
 */
gboolean
utils_ether_addr_valid (const struct ether_addr *test_addr)
{
	guint8 invalid_addr1[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	guint8 invalid_addr2[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	guint8 invalid_addr3[ETH_ALEN] = {0x44, 0x44, 0x44, 0x44, 0x44, 0x44};
	guint8 invalid_addr4[ETH_ALEN] = {0x00, 0x30, 0xb4, 0x00, 0x00, 0x00}; /* prism54 dummy MAC */

	g_return_val_if_fail (test_addr != NULL, FALSE);

	/* Compare the AP address the card has with invalid ethernet MAC addresses. */
	if (!memcmp (test_addr->ether_addr_octet, &invalid_addr1, ETH_ALEN))
		return FALSE;

	if (!memcmp (test_addr->ether_addr_octet, &invalid_addr2, ETH_ALEN))
		return FALSE;

	if (!memcmp (test_addr->ether_addr_octet, &invalid_addr3, ETH_ALEN))
		return FALSE;

	if (!memcmp (test_addr->ether_addr_octet, &invalid_addr4, ETH_ALEN))
		return FALSE;

	if (test_addr->ether_addr_octet[0] & 1)			/* Multicast addresses */
		return FALSE;
	
	return TRUE;
}

static gboolean
utils_check_ap_compatible (NMAccessPoint *ap,
                           NMConnection *connection)
{
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	const GByteArray *setting_bssid;
	const char *setting_mode;
	const char *setting_band;
	NM80211Mode ap_mode;
	guint32 freq, channel;

	g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	if (s_wireless == NULL)
		return FALSE;
	
	if (!nm_utils_same_ssid (nm_setting_wireless_get_ssid (s_wireless), nm_access_point_get_ssid (ap), TRUE))
		return FALSE;

	setting_bssid = nm_setting_wireless_get_bssid (s_wireless);
	if (setting_bssid) {
		struct ether_addr ap_addr;
		const char *hw_addr;

		hw_addr = nm_access_point_get_hw_address (ap);
		if (hw_addr && ether_aton_r (hw_addr, &ap_addr)) {
			if (memcmp (setting_bssid->data, &ap_addr, ETH_ALEN))
				return FALSE;
		}
	}

	ap_mode = nm_access_point_get_mode (ap);
	setting_mode = nm_setting_wireless_get_mode (s_wireless);
	if (setting_mode) {
		if (   !strcmp (setting_mode, "infrastructure")
		    && (ap_mode != NM_802_11_MODE_INFRA))
			return FALSE;
		if (   !strcmp (setting_mode, "adhoc")
		    && (ap_mode != NM_802_11_MODE_ADHOC))
			return FALSE;
	}

	freq = nm_access_point_get_frequency (ap);
	setting_band = nm_setting_wireless_get_band (s_wireless);
	if (setting_band) {
		if (!strcmp (setting_band, "a")) {
			if (freq < 4915 || freq > 5825)
				return FALSE;
		} else if (!strcmp (setting_band, "bg")) {
			if (freq < 2412 || freq > 2484)
				return FALSE;
		}
	}

	channel = nm_setting_wireless_get_channel (s_wireless);
	if (channel) {
		guint32 ap_chan = nm_utils_wifi_freq_to_channel (freq);

		if (channel != ap_chan)
			return FALSE;
	}

	s_wireless_sec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection,
	                                                                          NM_TYPE_SETTING_WIRELESS_SECURITY);

	return nm_setting_wireless_ap_security_compatible (s_wireless,
	                                                   s_wireless_sec,
	                                                   nm_access_point_get_flags (ap),
	                                                   nm_access_point_get_wpa_flags (ap),
	                                                   nm_access_point_get_rsn_flags (ap),
	                                                   nm_access_point_get_mode (ap));
}

static gboolean
connection_valid_for_wired (NMConnection *connection,
                            NMSettingConnection *s_con,
                            NMDevice *device,
                            gpointer specific_object)
{
	NMDeviceEthernet *ethdev = NM_DEVICE_ETHERNET (device);
	NMSettingWired *s_wired;
	const char *str_mac;
	struct ether_addr *bin_mac;
	const char *connection_type;
	const GByteArray *setting_mac;
	gboolean is_pppoe = FALSE;

	connection_type = nm_setting_connection_get_connection_type (s_con);
	if (!strcmp (connection_type, NM_SETTING_PPPOE_SETTING_NAME))
		is_pppoe = TRUE;
	
	if (!is_pppoe && strcmp (connection_type, NM_SETTING_WIRED_SETTING_NAME))
		return FALSE;

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRED));
	if (!is_pppoe && !s_wired)
		return FALSE;

	if (s_wired) {
		/* Match MAC address */
		setting_mac = nm_setting_wired_get_mac_address (s_wired);
		if (!setting_mac)
			return TRUE;

		str_mac = nm_device_ethernet_get_permanent_hw_address (ethdev);
		g_return_val_if_fail (str_mac != NULL, FALSE);

		bin_mac = ether_aton (str_mac);
		g_return_val_if_fail (bin_mac != NULL, FALSE);

		if (memcmp (bin_mac->ether_addr_octet, setting_mac->data, ETH_ALEN))
			return FALSE;
	}

	return TRUE;
}

static gboolean
connection_valid_for_wireless (NMConnection *connection,
                               NMSettingConnection *s_con,
                               NMDevice *device,
                               gpointer specific_object)
{
	NMDeviceWifi *wdev = NM_DEVICE_WIFI (device);
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	const GByteArray *setting_mac;
	const char *setting_security, *key_mgmt;
	guint32 wcaps;
	NMAccessPoint *ap;

	if (strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_WIRELESS_SETTING_NAME))
		return FALSE;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_return_val_if_fail (s_wireless != NULL, FALSE);

	/* Match MAC address */
	setting_mac = nm_setting_wireless_get_mac_address (s_wireless);
	if (setting_mac) {
		const char *str_mac;
		struct ether_addr *bin_mac;

		str_mac = nm_device_wifi_get_permanent_hw_address (wdev);
		g_return_val_if_fail (str_mac != NULL, FALSE);

		bin_mac = ether_aton (str_mac);
		g_return_val_if_fail (bin_mac != NULL, FALSE);

		if (memcmp (bin_mac->ether_addr_octet, setting_mac->data, ETH_ALEN))
			return FALSE;
	}

	/* If an AP was given make sure that's compatible with the connection first */
	if (specific_object) {
		ap = NM_ACCESS_POINT (specific_object);
		g_assert (ap);

		if (!utils_check_ap_compatible (ap, connection))
			return FALSE;
	}

	setting_security = nm_setting_wireless_get_security (s_wireless);
	if (!setting_security || strcmp (setting_security, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME))
		return TRUE; /* all devices can do unencrypted networks */

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY));
	if (!s_wireless_sec)
		return TRUE; /* all devices can do unencrypted networks */

	key_mgmt = nm_setting_wireless_security_get_key_mgmt (s_wireless_sec);

	/* All devices should support static WEP */
	if (!strcmp (key_mgmt, "none"))
		return TRUE;

	/* All devices should support legacy LEAP and Dynamic WEP */
	if (!strcmp (key_mgmt, "ieee8021x"))
		return TRUE;

	/* Match security with device capabilities */
	wcaps = nm_device_wifi_get_capabilities (wdev);

	/* At this point, the device better have basic WPA support. */
	if (   !(wcaps & NM_WIFI_DEVICE_CAP_WPA)
	    || !(wcaps & NM_WIFI_DEVICE_CAP_CIPHER_TKIP))
		return FALSE;

	/* Check for only RSN */
	if (   (nm_setting_wireless_security_get_num_protos (s_wireless_sec) == 1)
	    && !strcmp (nm_setting_wireless_security_get_proto (s_wireless_sec, 0), "rsn")
	    && !(wcaps & NM_WIFI_DEVICE_CAP_RSN))
		return FALSE;

	/* Check for only pairwise CCMP */
	if (   (nm_setting_wireless_security_get_num_pairwise (s_wireless_sec) == 1)
	    && !strcmp (nm_setting_wireless_security_get_pairwise (s_wireless_sec, 0), "ccmp")
	    && !(wcaps & NM_WIFI_DEVICE_CAP_CIPHER_CCMP))
		return FALSE;

	/* Check for only group CCMP */
	if (   (nm_setting_wireless_security_get_num_groups (s_wireless_sec) == 1)
	    && !strcmp (nm_setting_wireless_security_get_group (s_wireless_sec, 0), "ccmp")
	    && !(wcaps & NM_WIFI_DEVICE_CAP_CIPHER_CCMP))
		return FALSE;

	return TRUE;
}

static gboolean
connection_valid_for_gsm (NMConnection *connection,
                          NMSettingConnection *s_con,
                          NMDevice *device,
                          gpointer specific_object)
{
	NMSettingGsm *s_gsm;
	
	if (strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_GSM_SETTING_NAME))
		return FALSE;

	s_gsm = NM_SETTING_GSM (nm_connection_get_setting (connection, NM_TYPE_SETTING_GSM));
	g_return_val_if_fail (s_gsm != NULL, FALSE);

	return TRUE;
}

static gboolean
connection_valid_for_cdma (NMConnection *connection,
                           NMSettingConnection *s_con,
                           NMDevice *device,
                           gpointer specific_object)
{
	NMSettingCdma *s_cdma;
	
	if (strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_CDMA_SETTING_NAME))
		return FALSE;

	s_cdma = NM_SETTING_CDMA (nm_connection_get_setting (connection, NM_TYPE_SETTING_CDMA));
	g_return_val_if_fail (s_cdma != NULL, FALSE);

	return TRUE;
}

static guint32
get_connection_bt_type (NMConnection *connection)
{
	NMSettingBluetooth *s_bt;
	const char *bt_type;

	s_bt = (NMSettingBluetooth *) nm_connection_get_setting (connection, NM_TYPE_SETTING_BLUETOOTH);
	if (!s_bt)
		return NM_BT_CAPABILITY_NONE;

	bt_type = nm_setting_bluetooth_get_connection_type (s_bt);
	g_assert (bt_type);

	if (!strcmp (bt_type, NM_SETTING_BLUETOOTH_TYPE_DUN))
		return NM_BT_CAPABILITY_DUN;
	else if (!strcmp (bt_type, NM_SETTING_BLUETOOTH_TYPE_PANU))
		return NM_BT_CAPABILITY_NAP;

	return NM_BT_CAPABILITY_NONE;
}

static gboolean
connection_valid_for_bt (NMConnection *connection,
                         NMSettingConnection *s_con,
                         NMDevice *device,
                         gpointer specific_object)
{
	NMSettingBluetooth *s_bt;
	const GByteArray *array;
	char *str;
	const char *hw_addr;
	int addr_match = FALSE;
	guint32 bt_type;

	if (strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_BLUETOOTH_SETTING_NAME))
		return FALSE;

	s_bt = NM_SETTING_BLUETOOTH (nm_connection_get_setting (connection, NM_TYPE_SETTING_BLUETOOTH));
	if (!s_bt)
		return FALSE;

	array = nm_setting_bluetooth_get_bdaddr (s_bt);
	if (!array || (array->len != ETH_ALEN))
		return FALSE;

	bt_type = get_connection_bt_type (connection);
	if (!(bt_type & nm_device_bt_get_capabilities (NM_DEVICE_BT (device))))
		return FALSE;

	str = g_strdup_printf ("%02X:%02X:%02X:%02X:%02X:%02X",
	                       array->data[0], array->data[1], array->data[2],
	                       array->data[3], array->data[4], array->data[5]);
	hw_addr = nm_device_bt_get_hw_address (NM_DEVICE_BT (device));
	if (hw_addr)
		addr_match = !g_ascii_strcasecmp (hw_addr, str);
	g_free (str);

	return addr_match;
}

static gboolean
connection_valid_for_wimax (NMConnection *connection,
                            NMSettingConnection *s_con,
                            NMDevice *device,
                            gpointer specific_object)
{
	NMDeviceWimax *wimax = NM_DEVICE_WIMAX (device);
	NMSettingWimax *s_wimax;
	const char *str_mac;
	struct ether_addr *bin_mac;
	const char *connection_type;
	const GByteArray *setting_mac;

	connection_type = nm_setting_connection_get_connection_type (s_con);
	if (strcmp (connection_type, NM_SETTING_WIMAX_SETTING_NAME))
		return FALSE;

	s_wimax = (NMSettingWimax *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIMAX);
	if (!s_wimax)
		return FALSE;

	if (s_wimax) {
		/* Match MAC address */
		setting_mac = nm_setting_wimax_get_mac_address (s_wimax);
		if (!setting_mac)
			return TRUE;

		str_mac = nm_device_wimax_get_hw_address (wimax);
		g_return_val_if_fail (str_mac != NULL, FALSE);

		bin_mac = ether_aton (str_mac);
		g_return_val_if_fail (bin_mac != NULL, FALSE);

		if (memcmp (bin_mac->ether_addr_octet, setting_mac->data, ETH_ALEN))
			return FALSE;
	}

	return TRUE;
}

gboolean
utils_connection_valid_for_device (NMConnection *connection,
                                   NMDevice *device,
                                   gpointer specific_object)
{
	NMSettingConnection *s_con;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (device != NULL, FALSE);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_return_val_if_fail (s_con != NULL, FALSE);
	g_return_val_if_fail (nm_setting_connection_get_connection_type (s_con) != NULL, FALSE);

	if (NM_IS_DEVICE_ETHERNET (device))
		return connection_valid_for_wired (connection, s_con, device, specific_object);
	else if (NM_IS_DEVICE_WIFI (device))
		return connection_valid_for_wireless (connection, s_con, device, specific_object);
	else if (NM_IS_DEVICE_MODEM (device)) {
		NMDeviceModemCapabilities caps;

		caps = nm_device_modem_get_current_capabilities (NM_DEVICE_MODEM (device));
		if (caps & NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS)
			return connection_valid_for_gsm (connection, s_con, device, specific_object);
		else if (caps & NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)
			return connection_valid_for_cdma (connection, s_con, device, specific_object);
		else
			g_warning ("Unhandled modem capabilities 0x%X", caps);
	} else if (NM_IS_DEVICE_BT (device))
		return connection_valid_for_bt (connection, s_con, device, specific_object);
	else if (NM_IS_DEVICE_WIMAX (device))
		return connection_valid_for_wimax (connection, s_con, device, specific_object);
	else
		g_warning ("Unknown device type '%s'", g_type_name (G_OBJECT_TYPE(device)));

	return FALSE;
}

GSList *
utils_filter_connections_for_device (NMDevice *device, GSList *connections)
{
	GSList *iter;
	GSList *filtered = NULL;

	for (iter = connections; iter; iter = g_slist_next (iter)) {
		NMConnection *connection = NM_CONNECTION (iter->data);

		if (utils_connection_valid_for_device (connection, device, NULL))
			filtered = g_slist_append (filtered, connection);
	}

	return filtered;
}

gboolean
utils_mac_valid (const struct ether_addr *addr)
{
	guint8 invalid1[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	guint8 invalid2[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	guint8 invalid3[ETH_ALEN] = {0x44, 0x44, 0x44, 0x44, 0x44, 0x44};
	guint8 invalid4[ETH_ALEN] = {0x00, 0x30, 0xb4, 0x00, 0x00, 0x00}; /* prism54 dummy MAC */

	g_return_val_if_fail (addr != NULL, FALSE);

	/* Compare the AP address the card has with invalid ethernet MAC addresses. */
	if (!memcmp (addr->ether_addr_octet, &invalid1, ETH_ALEN))
		return FALSE;

	if (!memcmp (addr->ether_addr_octet, &invalid2, ETH_ALEN))
		return FALSE;

	if (!memcmp (addr->ether_addr_octet, &invalid3, ETH_ALEN))
		return FALSE;

	if (!memcmp (addr->ether_addr_octet, &invalid4, ETH_ALEN))
		return FALSE;

	if (addr->ether_addr_octet[0] & 1) /* Multicast addresses */
		return FALSE;
	
	return TRUE;
}

char *
utils_ether_ntop (const struct ether_addr *mac)
{
	/* we like leading zeros and all-caps, instead
	 * of what glibc's ether_ntop() gives us
	 */
	return g_strdup_printf ("%02X:%02X:%02X:%02X:%02X:%02X",
	                        mac->ether_addr_octet[0], mac->ether_addr_octet[1],
	                        mac->ether_addr_octet[2], mac->ether_addr_octet[3],
	                        mac->ether_addr_octet[4], mac->ether_addr_octet[5]);
}


char *
utils_next_available_name (GSList *connections, const char *format)
{
	GSList *names = NULL, *iter;
	char *cname = NULL;
	int i = 0;

	for (iter = connections; iter; iter = g_slist_next (iter)) {
		NMConnection *candidate = NM_CONNECTION (iter->data);
		NMSettingConnection *s_con;
		const char *id;

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (candidate, NM_TYPE_SETTING_CONNECTION));
		id = nm_setting_connection_get_id (s_con);
		g_assert (id);
		names = g_slist_append (names, (gpointer) id);
	}	

	/* Find the next available unique connection name */
	while (!cname && (i++ < 10000)) {
		char *temp;
		gboolean found = FALSE;

		temp = g_strdup_printf (format, i);
		for (iter = names; iter; iter = g_slist_next (iter)) {
			if (!strcmp (iter->data, temp)) {
				found = TRUE;
				break;
			}
		}
		if (!found)
			cname = temp;
		else
			g_free (temp);
	}

	g_slist_free (names);
	return cname;
}

char *
utils_hash_ap (const GByteArray *ssid,
               NM80211Mode mode,
               guint32 flags,
               guint32 wpa_flags,
               guint32 rsn_flags)
{
	unsigned char input[66];

	memset (&input[0], 0, sizeof (input));

	if (ssid)
		memcpy (input, ssid->data, ssid->len);

	if (mode == NM_802_11_MODE_INFRA)
		input[32] |= (1 << 0);
	else if (mode == NM_802_11_MODE_ADHOC)
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

	/* duplicate it */
	memcpy (&input[33], &input[0], 32);
	return g_compute_checksum_for_data (G_CHECKSUM_MD5, input, sizeof (input));
}

typedef struct {
	const char *tag;
	const char *replacement;
} Tag;

static Tag escaped_tags[] = {
	{ "<center>", NULL },
	{ "</center>", NULL },
	{ "<p>", "\n" },
	{ "</p>", NULL },
	{ "<B>", "<b>" },
	{ "</B>", "</b>" },
	{ "<I>", "<i>" },
	{ "</I>", "</i>" },
	{ "<u>", "<u>" },
	{ "</u>", "</u>" },
	{ "&", "&amp;" },
	{ NULL, NULL }
};

char *
utils_escape_notify_message (const char *src)
{
	const char *p = src;
	GString *escaped;

	/* Filter the source text and get rid of some HTML tags since the
	 * notification spec only allows a subset of HTML.  Substitute
	 * HTML code for characters like & that are invalid in HTML.
	 */

	escaped = g_string_sized_new (strlen (src) + 5);
	while (*p) {
		Tag *t = &escaped_tags[0];
		gboolean found = FALSE;

		while (t->tag) {
			if (strncasecmp (p, t->tag, strlen (t->tag)) == 0) {
				p += strlen (t->tag);
				if (t->replacement)
					g_string_append (escaped, t->replacement);
				found = TRUE;
				break;
			}
			t++;
		}
		if (!found)
			g_string_append_c (escaped, *p++);
	}

	return g_string_free (escaped, FALSE);
}

GnomeKeyringAttributeList *
utils_create_keyring_add_attr_list (NMConnection *connection,
                                    const char *connection_uuid,
                                    const char *connection_id,
                                    const char *setting_name,
                                    const char *setting_key,
                                    char **out_display_name)
{
	GnomeKeyringAttributeList *attrs = NULL;
	NMSettingConnection *s_con;

	if (connection) {
		s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
		g_return_val_if_fail (s_con != NULL, NULL);
		connection_uuid = nm_setting_connection_get_uuid (s_con);
		connection_id = nm_setting_connection_get_id (s_con);
	}

	g_return_val_if_fail (connection_uuid != NULL, NULL);
	g_return_val_if_fail (connection_id != NULL, NULL);
	g_return_val_if_fail (setting_name != NULL, NULL);
	g_return_val_if_fail (setting_key != NULL, NULL);

	if (out_display_name) {
		*out_display_name = g_strdup_printf ("Network secret for %s/%s/%s",
		                                     connection_id,
		                                     setting_name,
		                                     setting_key);
	}

	attrs = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (attrs,
	                                            KEYRING_UUID_TAG,
	                                            connection_uuid);
	gnome_keyring_attribute_list_append_string (attrs,
	                                            KEYRING_SN_TAG,
	                                            setting_name);
	gnome_keyring_attribute_list_append_string (attrs,
	                                            KEYRING_SK_TAG,
	                                            setting_key);
	return attrs;
}

