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
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <string.h>
#include <netinet/ether.h>
#include <glib.h>
#include <iwlib.h>

#include <nm-device-802-3-ethernet.h>
#include <nm-device-802-11-wireless.h>
#include <nm-gsm-device.h>
#include <nm-cdma-device.h>
#include <nm-access-point.h>

#include <nm-setting-connection.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>
#include <nm-setting-8021x.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-setting-pppoe.h>
#include <nm-utils.h>

#include "crypto.h"
#include "utils.h"
#include "gconf-helpers.h"

/*
 * utils_bin2hexstr
 *
 * Convert a byte-array into a hexadecimal string.
 *
 * Code originally by Alex Larsson <alexl@redhat.com> and
 *  copyright Red Hat, Inc. under terms of the LGPL.
 *
 */
char *
utils_bin2hexstr (const char *bytes, int len, int final_len)
{
	static char	hex_digits[] = "0123456789abcdef";
	char *		result;
	int			i;

	g_return_val_if_fail (bytes != NULL, NULL);
	g_return_val_if_fail (len > 0, NULL);
	g_return_val_if_fail (len < 256, NULL);	/* Arbitrary limit */

	result = g_malloc0 (len * 2 + 1);
	for (i = 0; i < len; i++)
	{
		result[2*i] = hex_digits[(bytes[i] >> 4) & 0xf];
		result[2*i+1] = hex_digits[bytes[i] & 0xf];
	}
	/* Cut converted key off at the correct length for this cipher type */
	if (final_len > -1)
		result[final_len] = '\0';

	return result;
}

static char * vnd_ignore[] = {
	"Semiconductor",
	"Components",
	"Corporation",
	"Corp.",
	"Corp",
	"Inc.",
	"Inc",
	NULL
};

#define DESC_TAG "description"

const char *
utils_get_device_description (NMDevice *device)
{
	char *description = NULL;
	const char *dev_product;
	const char *dev_vendor;
	char *product = NULL;
	char *vendor = NULL;
	char *p;
	char **words;
	char **item;
	GString *str;
	gboolean need_space = FALSE;

	g_return_val_if_fail (device != NULL, NULL);

	description = g_object_get_data (G_OBJECT (device), DESC_TAG);
	if (description)
		return description;

	dev_product = nm_device_get_product (device);
	dev_vendor = nm_device_get_vendor (device);
	if (!dev_product || !dev_vendor)
		return NULL;

	/* Replace stupid '_' with ' ' */
	p = product = g_strdup (dev_product);
	while (*p) {
		if (*p == '_')
			*p = ' ';
		p++;
	}

	p = vendor = g_strdup (dev_vendor);
	while (*p) {
		if (*p == '_' || *p == ',')
			*p = ' ';
		p++;
	}

	str = g_string_new_len (NULL, strlen (vendor) + strlen (product));

	/* In a futile attempt to shorten the vendor ID, ignore certain words */
	words = g_strsplit (vendor, " ", 0);

	for (item = words; *item; item++) {
		int i = 0;
		gboolean ignore = FALSE;

		if (g_ascii_isspace (**item) || (**item == '\0'))
			continue;

		while (vnd_ignore[i] && !ignore) {
			if (!strcmp (*item, vnd_ignore[i]))
				ignore = TRUE;
			i++;
		}

		if (!ignore) {
			g_string_append (str, *item);
			if (need_space)
				g_string_append_c (str, ' ');
			need_space = TRUE;
		}
	}
	g_strfreev (words);

	g_string_append_c (str, ' ');
	g_string_append (str, product);
	description = str->str;
	g_string_free (str, FALSE);

	g_object_set_data_full (G_OBJECT (device),
	                        "description", description,
	                        (GDestroyNotify) g_free);

	g_free (product);
	g_free (vendor);
	return description;
}

static void
clear_one_byte_array_field (GByteArray **field)
{
	g_return_if_fail (field != NULL);

	if (!*field)
		return;
	g_byte_array_free (*field, TRUE);
	*field = NULL;
}

gboolean
utils_fill_one_crypto_object (NMConnection *connection,
                              const char *key_name,
                              gboolean is_private_key,
                              const char *password,
                              GByteArray **field,
                              GError **error)
{
	const char *filename;
	NMSettingConnection *s_con;
	guint32 ignore;

	g_return_val_if_fail (key_name != NULL, FALSE);
	g_return_val_if_fail (field != NULL, FALSE);

	clear_one_byte_array_field (field);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_return_val_if_fail (s_con != NULL, FALSE);

	filename = g_object_get_data (G_OBJECT (connection), key_name);
	if (!filename)
		return TRUE;

	if (is_private_key)
		g_return_val_if_fail (password != NULL, FALSE);

	if (is_private_key) {
		*field = crypto_get_private_key (filename, password, &ignore, error);
		if (error && *error)
			clear_one_byte_array_field (field);
	} else {
		*field = crypto_load_and_verify_certificate (filename, error);
		if (error && *error)
			clear_one_byte_array_field (field);
	}

	if (error && *error)
		return FALSE;
	return TRUE;
}

void
utils_fill_connection_certs (NMConnection *connection)
{
	NMSetting8021x *s_8021x;

	g_return_if_fail (connection != NULL);

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	if (!s_8021x)
		return;

	utils_fill_one_crypto_object (connection,
	                              NMA_PATH_CA_CERT_TAG,
	                              FALSE,
	                              NULL,
	                              &s_8021x->ca_cert,
	                              NULL);
	utils_fill_one_crypto_object (connection,
	                              NMA_PATH_CLIENT_CERT_TAG,
	                              FALSE,
	                              NULL,
	                              &s_8021x->client_cert,
	                              NULL);
	utils_fill_one_crypto_object (connection,
	                              NMA_PATH_PHASE2_CA_CERT_TAG,
	                              FALSE,
	                              NULL,
	                              &s_8021x->phase2_ca_cert,
	                              NULL);
	utils_fill_one_crypto_object (connection,
	                              NMA_PATH_PHASE2_CLIENT_CERT_TAG,
	                              FALSE,
	                              NULL,
	                              &s_8021x->phase2_client_cert,
	                              NULL);
}

void
utils_clear_filled_connection_certs (NMConnection *connection)
{
	NMSetting8021x *s_8021x;

	g_return_if_fail (connection != NULL);

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	if (!s_8021x)
		return;

	clear_one_byte_array_field (&s_8021x->ca_cert);
	clear_one_byte_array_field (&s_8021x->client_cert);
	clear_one_byte_array_field (&s_8021x->private_key);
	clear_one_byte_array_field (&s_8021x->phase2_ca_cert);
	clear_one_byte_array_field (&s_8021x->phase2_client_cert);
	clear_one_byte_array_field (&s_8021x->phase2_private_key);
}


struct cf_pair {
	guint32 chan;
	guint32 freq;
};

static struct cf_pair a_table[] = {
	/* A band */
	{  7, 5035 },
	{  8, 5040 },
	{  9, 5045 },
	{ 11, 5055 },
	{ 12, 5060 },
	{ 16, 5080 },
	{ 34, 5170 },
	{ 36, 5180 },
	{ 38, 5190 },
	{ 40, 5200 },
	{ 42, 5210 },
	{ 44, 5220 },
	{ 46, 5230 },
	{ 48, 5240 },
	{ 50, 5250 },
	{ 52, 5260 },
	{ 56, 5280 },
	{ 58, 5290 },
	{ 60, 5300 },
	{ 64, 5320 },
	{ 100, 5500 },
	{ 104, 5520 },
	{ 108, 5540 },
	{ 112, 5560 },
	{ 116, 5580 },
	{ 120, 5600 },
	{ 124, 5620 },
	{ 128, 5640 },
	{ 132, 5660 },
	{ 136, 5680 },
	{ 140, 5700 },
	{ 149, 5745 },
	{ 152, 5760 },
	{ 153, 5765 },
	{ 157, 5785 },
	{ 160, 5800 },
	{ 161, 5805 },
	{ 165, 5825 },
	{ 183, 4915 },
	{ 184, 4920 },
	{ 185, 4925 },
	{ 187, 4935 },
	{ 188, 4945 },
	{ 192, 4960 },
	{ 196, 4980 },
	{ 0, -1 }
};

static struct cf_pair bg_table[] = {
	/* B/G band */
	{ 1, 2412 },
	{ 2, 2417 },
	{ 3, 2422 },
	{ 4, 2427 },
	{ 5, 2432 },
	{ 6, 2437 },
	{ 7, 2442 },
	{ 8, 2447 },
	{ 9, 2452 },
	{ 10, 2457 },
	{ 11, 2462 },
	{ 12, 2467 },
	{ 13, 2472 },
	{ 14, 2484 },
	{ 0, -1 }
};

guint32
utils_freq_to_channel (guint32 freq)
{
	int i = 0;

	while (a_table[i].chan && (a_table[i].freq != freq))
		i++;
	if (a_table[i].chan)
		return a_table[i].chan;

	i = 0;
	while (bg_table[i].chan && (bg_table[i].freq != freq))
		i++;
	return bg_table[i].chan;
}

guint32
utils_channel_to_freq (guint32 channel, char *band)
{
	int i = 0;

	if (!strcmp (band, "a")) {
		while (a_table[i].chan && (a_table[i].chan != channel))
			i++;
		return a_table[i].freq;
	} else if (!strcmp (band, "bg")) {
		while (bg_table[i].chan && (bg_table[i].chan != channel))
			i++;
		return bg_table[i].freq;
	}

	return 0;
}

guint32
utils_find_next_channel (guint32 channel, int direction, char *band)
{
	size_t a_size = sizeof (a_table) / sizeof (struct cf_pair);
	size_t bg_size = sizeof (bg_table) / sizeof (struct cf_pair);
	struct cf_pair *pair = NULL;

	if (!strcmp (band, "a")) {
		if (channel < a_table[0].chan)
			return a_table[0].chan;
		if (channel > a_table[a_size - 2].chan)
			return a_table[a_size - 2].chan;
		pair = &a_table[0];
	} else if (!strcmp (band, "bg")) {
		if (channel < bg_table[0].chan)
			return bg_table[0].chan;
		if (channel > bg_table[bg_size - 2].chan)
			return bg_table[bg_size - 2].chan;
		pair = &bg_table[0];
	} else {
		g_assert_not_reached ();
		return 0;
	}

	while (pair->chan) {
		if (channel == pair->chan)
			return channel;
		if ((channel < (pair+1)->chan) && (channel > pair->chan)) {
			if (direction > 0)	
				return (pair+1)->chan;
			else
				return pair->chan;
		}
		pair++;
	}
	return 0;
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

static gboolean
connection_valid_for_wired (NMConnection *connection,
                            NMSettingConnection *s_con,
                            NMDevice *device,
                            gpointer specific_object)
{
	NMDevice8023Ethernet *ethdev = NM_DEVICE_802_3_ETHERNET (device);
	NMSettingWired *s_wired;
	const char *str_mac;
	struct ether_addr *bin_mac;

	if (!strcmp (s_con->type, NM_SETTING_PPPOE_SETTING_NAME))
		return TRUE;
	
	if (strcmp (s_con->type, NM_SETTING_WIRED_SETTING_NAME))
		return FALSE;

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRED));
	g_return_val_if_fail (s_wired != NULL, FALSE);

	/* Match MAC address */
	if (!s_wired->mac_address)
		return TRUE;

	str_mac = nm_device_802_3_ethernet_get_hw_address (ethdev);
	g_return_val_if_fail (str_mac != NULL, FALSE);

	bin_mac = ether_aton (str_mac);
	g_return_val_if_fail (bin_mac != NULL, FALSE);

	if (memcmp (bin_mac->ether_addr_octet, s_wired->mac_address->data, ETH_ALEN))
		return FALSE;

	return TRUE;
}

static gboolean
connection_valid_for_wireless (NMConnection *connection,
                               NMSettingConnection *s_con,
                               NMDevice *device,
                               gpointer specific_object)
{
	NMDevice80211Wireless *wdev = NM_DEVICE_802_11_WIRELESS (device);
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	guint32 wcaps;
	NMAccessPoint *ap;

	if (strcmp (s_con->type, NM_SETTING_WIRELESS_SETTING_NAME))
		return FALSE;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_return_val_if_fail (s_wireless != NULL, FALSE);

	/* Match MAC address */
	if (s_wireless->mac_address) {
		const char *str_mac;
		struct ether_addr *bin_mac;

		str_mac = nm_device_802_11_wireless_get_hw_address (wdev);
		g_return_val_if_fail (str_mac != NULL, FALSE);

		bin_mac = ether_aton (str_mac);
		g_return_val_if_fail (bin_mac != NULL, FALSE);

		if (memcmp (bin_mac->ether_addr_octet, s_wireless->mac_address->data, ETH_ALEN))
			return FALSE;
	}

	/* If an AP was given make sure that's compatible with the connection first */
	if (specific_object) {
		ap = NM_ACCESS_POINT (specific_object);
		g_assert (ap);

		if (!utils_check_ap_compatible (ap, connection))
			return FALSE;
	}

	if (!s_wireless->security || strcmp (s_wireless->security, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME))
		return TRUE; /* all devices can do unencrypted networks */

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY));
	if (!s_wireless_sec)
		return TRUE; /* all devices can do unencrypted networks */

	/* All devices should support static WEP */
	if (!strcmp (s_wireless_sec->key_mgmt, "none"))
		return TRUE;

	/* All devices should support legacy LEAP and Dynamic WEP */
	if (!strcmp (s_wireless_sec->key_mgmt, "ieee8021x"))
		return TRUE;

	/* Match security with device capabilities */
	wcaps = nm_device_802_11_wireless_get_capabilities (wdev);

	/* At this point, the device better have basic WPA support. */
	if (   !(wcaps & NM_802_11_DEVICE_CAP_WPA)
	    || !(wcaps & NM_802_11_DEVICE_CAP_CIPHER_TKIP))
		return FALSE;

	/* Check for only RSN */
	if (   (g_slist_length (s_wireless_sec->proto) == 1)
	    && !strcmp (s_wireless_sec->proto->data, "rsn")
	    && !(wcaps & NM_802_11_DEVICE_CAP_RSN))
		return FALSE;

	/* Check for only pairwise CCMP */
	if (   (g_slist_length (s_wireless_sec->pairwise) == 1)
	    && !strcmp (s_wireless_sec->pairwise->data, "ccmp")
	    && !(wcaps & NM_802_11_DEVICE_CAP_CIPHER_CCMP))
		return FALSE;

	/* Check for only group CCMP */
	if (   (g_slist_length (s_wireless_sec->group) == 1)
	    && !strcmp (s_wireless_sec->group->data, "ccmp")
	    && !(wcaps & NM_802_11_DEVICE_CAP_CIPHER_CCMP))
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
	
	if (strcmp (s_con->type, NM_SETTING_GSM_SETTING_NAME))
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
	
	if (strcmp (s_con->type, NM_SETTING_CDMA_SETTING_NAME))
		return FALSE;

	s_cdma = NM_SETTING_CDMA (nm_connection_get_setting (connection, NM_TYPE_SETTING_CDMA));
	g_return_val_if_fail (s_cdma != NULL, FALSE);

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
	g_return_val_if_fail (s_con->type != NULL, FALSE);

	if (NM_IS_DEVICE_802_3_ETHERNET (device))
		return connection_valid_for_wired (connection, s_con, device, specific_object);
	else if (NM_IS_DEVICE_802_11_WIRELESS (device))
		return connection_valid_for_wireless (connection, s_con, device, specific_object);
	else if (NM_IS_GSM_DEVICE (device))
		return connection_valid_for_gsm (connection, s_con, device, specific_object);
	else if (NM_IS_CDMA_DEVICE (device))
		return connection_valid_for_cdma (connection, s_con, device, specific_object);
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

