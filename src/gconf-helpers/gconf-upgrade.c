/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager -- Network link manager
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <iwlib.h>
#include <wireless.h>

#include <nm-setting-connection.h>
#include <nm-setting-wireless.h>
#include <nm-setting-vpn.h>
#include <nm-setting-vpn-properties.h>
#include "gconf-upgrade.h"
#include "gconf-helpers.h"

#include "nm-connection.h"

/* NM 0.6 compat defines */

#define NM_AUTH_TYPE_WPA_PSK_AUTO 0x00000000
#define NM_AUTH_TYPE_NONE         0x00000001
#define NM_AUTH_TYPE_WEP40        0x00000002
#define NM_AUTH_TYPE_WPA_PSK_TKIP 0x00000004
#define NM_AUTH_TYPE_WPA_PSK_CCMP 0x00000008
#define NM_AUTH_TYPE_WEP104       0x00000010
#define NM_AUTH_TYPE_WPA_EAP      0x00000020
#define NM_AUTH_TYPE_LEAP         0x00000040

#define NM_EAP_METHOD_MD5         0x00000001
#define NM_EAP_METHOD_MSCHAP      0x00000002
#define NM_EAP_METHOD_OTP         0x00000004
#define NM_EAP_METHOD_GTC         0x00000008
#define NM_EAP_METHOD_PEAP        0x00000010
#define NM_EAP_METHOD_TLS         0x00000020
#define NM_EAP_METHOD_TTLS        0x00000040

#define NM_PHASE2_AUTH_NONE       0x00000000
#define NM_PHASE2_AUTH_PAP        0x00010000
#define NM_PHASE2_AUTH_MSCHAP     0x00020000
#define NM_PHASE2_AUTH_MSCHAPV2   0x00030000
#define NM_PHASE2_AUTH_GTC        0x00040000

static void
free_slist (GSList *slist)
{
	GSList *i;

	for (i = slist; i; i = i->next)
		g_free (i->data);
	g_slist_free (slist);
}

struct flagnames {
	const char * const name;
	guint value;
};

/* Reads an enum value stored as an integer and returns the
 * corresponding string from @names.
 */
static gboolean
get_enum_helper (GConfClient             *client,
			  const char              *path,
			  const char              *key,
			  const char              *network,
			  const struct flagnames  *names,
			  char                   **value)
{
	int ival, i;

	if (!nm_gconf_get_int_helper (client, path, key, network, &ival)) {
		g_warning ("Missing key '%s' on NM 0.6 connection %s", key, network);
		return FALSE;
	}

	for (i = 0; names[i].name; i++) {
		if (names[i].value == ival) {
			*value = g_strdup (names[i].name);
			return TRUE;
		}
	}

	g_warning ("Bad value '%d' for key '%s' on NM 0.6 connection %s", ival, key, network);
	return FALSE;
}

/* Reads a bitfield value stored as an integer and returns a list of
 * names from @names corresponding to the bits that are set.
 */
static gboolean
get_bitfield_helper (GConfClient             *client,
				 const char              *path,
				 const char              *key,
				 const char              *network,
				 const struct flagnames  *names,
				 GSList                 **value)
{
	int ival, i;

	if (!nm_gconf_get_int_helper (client, path, key, network, &ival)) {
		g_warning ("Missing key '%s' on NM 0.6 connection %s", key, network);
		return FALSE;
	}

	*value = NULL;
	for (i = 0; names[i].name; i++) {
		if (names[i].value & ival) {
			*value = g_slist_prepend (*value, g_strdup (names[i].name));
			ival = ival & ~names[i].value;
		}
	}

	if (ival) {
		free_slist (*value);
		g_warning ("Bad value '%d' for key '%s' on NM 0.6 connection %s", ival, key, network);
		return FALSE;
	}

	return TRUE;
}

static gboolean
get_mandatory_string_helper (GConfClient  *client,
					    const char   *path,
					    const char   *key,
					    const char   *network,
					    char        **value)
{
	if (!nm_gconf_get_string_helper (client, path, key, network, value)) {
		g_warning ("Missing key '%s' on NM 0.6 connection %s", key, network);
		return FALSE;
	}
	return TRUE;
}

static const struct flagnames wep_auth_algorithms[] = {
	{ "open",   IW_AUTH_ALG_OPEN_SYSTEM },
	{ "shared", IW_AUTH_ALG_SHARED_KEY },
	{ NULL, 0 }
};

static NMSettingWirelessSecurity *
nm_gconf_read_0_6_wep_settings (GConfClient *client,
						  const char *path, const char *network)
{
	NMSettingWirelessSecurity *s_wireless_sec;
	char *auth_alg;

	if (!get_enum_helper (client, path, "wep_auth_algorithm", network, wep_auth_algorithms, &auth_alg))
		return NULL;

	s_wireless_sec = (NMSettingWirelessSecurity *)nm_setting_wireless_security_new ();
	s_wireless_sec->key_mgmt = g_strdup ("none");
	s_wireless_sec->wep_tx_keyidx = 0;
	s_wireless_sec->auth_alg = auth_alg;

	return s_wireless_sec;
}

static const struct flagnames wpa_versions[] = {
	{ "wpa", IW_AUTH_WPA_VERSION_WPA },
	{ "rsn", IW_AUTH_WPA_VERSION_WPA2 },
	{ NULL, 0 }
};

static const struct flagnames wpa_key_mgmt[] = {
	{ "ieee8021x", IW_AUTH_KEY_MGMT_802_1X },
	{ "wpa-psk",   IW_AUTH_KEY_MGMT_PSK },
	{ NULL, 0 }
};

static NMSettingWirelessSecurity *
nm_gconf_read_0_6_wpa_settings (GConfClient *client,
						  const char *path, const char *network)
{
	NMSettingWirelessSecurity *s_wireless_sec;
	GSList *proto, *pairwise, *group;
	char *key_mgmt;

	if (!get_bitfield_helper (client, path, "wpa_psk_wpa_version", network, wpa_versions, &proto))
		return NULL;
	if (!get_enum_helper (client, path, "wpa_psk_key_mgt", network, wpa_key_mgmt, &key_mgmt)) {
		free_slist (proto);
		return NULL;
	}

	/* Allow all ciphers */
	pairwise = g_slist_prepend (NULL, "tkip");
	pairwise = g_slist_prepend (pairwise, "ccmp");
	group = g_slist_prepend (NULL, "wep40");
	group = g_slist_prepend (group, "wep104");
	group = g_slist_prepend (group, "tkip");
	group = g_slist_prepend (group, "ccmp");

	s_wireless_sec = (NMSettingWirelessSecurity *)nm_setting_wireless_security_new ();
	s_wireless_sec->key_mgmt = key_mgmt;
	s_wireless_sec->proto = proto;
	s_wireless_sec->pairwise = pairwise;
	s_wireless_sec->group = group;

	return s_wireless_sec;
}

static const struct flagnames eap_methods[] = {
	{ "md5",    NM_EAP_METHOD_MD5 },
	{ "mschap", NM_EAP_METHOD_MSCHAP },
	{ "otp",    NM_EAP_METHOD_OTP },
	{ "gtc",    NM_EAP_METHOD_GTC },
	{ "peap",   NM_EAP_METHOD_PEAP },
	{ "tls",    NM_EAP_METHOD_TLS },
	{ "ttls",   NM_EAP_METHOD_TTLS },
	{ NULL, 0 }
};

static const struct flagnames eap_key_types[] = {
	{ "wep40",  IW_AUTH_CIPHER_WEP40 },
	{ "wep104", IW_AUTH_CIPHER_WEP104 },
	{ "tkip",   IW_AUTH_CIPHER_TKIP },
	{ "ccmp",   IW_AUTH_CIPHER_CCMP },
	{ NULL, 0 }
};

static const struct flagnames eap_phase2_types[] = {
	{ "pap",      NM_PHASE2_AUTH_PAP },
	{ "mschap",   NM_PHASE2_AUTH_MSCHAP },
	{ "mschapv2", NM_PHASE2_AUTH_MSCHAPV2 },
	{ "gtc",      NM_PHASE2_AUTH_GTC },
	{ NULL, 0 }
};

static NMSettingWirelessSecurity *
nm_gconf_read_0_6_eap_settings (GConfClient *client,
						  const char *path, const char *network)
{
	NMSettingWirelessSecurity *s_wireless_sec;
	GSList *eap = NULL, *key_type = NULL, *proto = NULL;
	char *phase2_type = NULL, *identity = NULL, *anon_identity = NULL;

	if (!get_bitfield_helper (client, path, "wpa_eap_eap_method", network, eap_methods, &eap))
		goto fail;
	if (!get_bitfield_helper (client, path, "wpa_eap_key_type", network, eap_key_types, &key_type))
		goto fail;
	if (!get_enum_helper (client, path, "wpa_eap_phase2_type", network, eap_phase2_types, &phase2_type))
		goto fail;
	if (!get_bitfield_helper (client, path, "wpa_eap_wpa_version", network, wpa_versions, &proto))
		goto fail;

	if (!get_mandatory_string_helper (client, path, "wpa_eap_identity", network, &identity))
		goto fail;
	nm_gconf_get_string_helper (client, path, "wpa_eap_anon_identity", network, &anon_identity);

	s_wireless_sec = (NMSettingWirelessSecurity *)nm_setting_wireless_security_new ();
	/* AFAICT, 0.6 reads this value from gconf, and then ignores it and always uses IW_AUTH_KEY_MGMT_802_1X */
	s_wireless_sec->key_mgmt = g_strdup ("ieee8021x"); /* FIXME? wpa-eap? */
	s_wireless_sec->proto = proto;
	s_wireless_sec->eap = eap;
	s_wireless_sec->group = key_type; /* FIXME? */
	s_wireless_sec->phase2_auth = phase2_type; /* FIXME? phase2_autheap? */
	s_wireless_sec->identity = identity;
	s_wireless_sec->password = g_strdup ("");
	s_wireless_sec->anonymous_identity = anon_identity;

	return s_wireless_sec;

fail:
	free_slist (proto);
	free_slist (eap);
	free_slist (key_type);
	g_free (phase2_type);
	g_free (identity);
	g_free (anon_identity);

	return NULL;
}

static NMSettingWirelessSecurity *
nm_gconf_read_0_6_leap_settings (GConfClient *client,
						   const char *path, const char *network)
{
	NMSettingWirelessSecurity *s_wireless_sec;
	char *username = NULL, *key_mgmt = NULL;

	if (!get_mandatory_string_helper (client, path, "leap_username", network, &username))
		return NULL;
	if (!get_mandatory_string_helper (client, path, "leap_key_mgmt", network, &key_mgmt)) {
		g_free (username);
		return NULL;
	}

	s_wireless_sec = (NMSettingWirelessSecurity *)nm_setting_wireless_security_new ();
	s_wireless_sec->key_mgmt = key_mgmt;
	s_wireless_sec->identity = username;

	return s_wireless_sec;
}

static NMConnection *
nm_gconf_read_0_6_wireless_connection (GConfClient *client,
							    const char *dir)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	char *path, *network, *essid = NULL;
	int timestamp, we_cipher;
	GSList *bssids = NULL;
	char *private_key_path = NULL, *client_cert_path = NULL, *ca_cert_path = NULL;

	path = g_path_get_dirname (dir);
	network = g_path_get_basename (dir);

	if (!get_mandatory_string_helper (client, path, "essid", network, &essid)) {
		g_free (path);
		g_free (network);
		return NULL;
	}

	if (!nm_gconf_get_int_helper (client, path, "timestamp", network, &timestamp))
		timestamp = 0;
	if (!nm_gconf_get_stringlist_helper (client, path, "bssids", network, &bssids))
		bssids = NULL;
	if (!nm_gconf_get_int_helper (client, path, "we_cipher", network, &we_cipher))
		we_cipher = NM_AUTH_TYPE_NONE;

	s_con = (NMSettingConnection *)nm_setting_connection_new ();
	s_con->name = g_strdup_printf ("Auto %s", essid);
	s_con->type = g_strdup ("802-11-wireless");
	s_con->autoconnect = (timestamp != 0);
	s_con->timestamp = timestamp;

	s_wireless = (NMSettingWireless *)nm_setting_wireless_new ();
	s_wireless->ssid = g_byte_array_new ();
	g_byte_array_append (s_wireless->ssid, (unsigned char *)essid, strlen (essid));
	g_free (essid);
	s_wireless->mode = g_strdup ("infrastructure");
	s_wireless->seen_bssids = bssids;

	if (we_cipher != NM_AUTH_TYPE_NONE) {
		s_wireless->security = g_strdup ("802-11-wireless-security");

		switch (we_cipher) {
		case NM_AUTH_TYPE_WEP40:
		case NM_AUTH_TYPE_WEP104:
			s_wireless_sec = nm_gconf_read_0_6_wep_settings (client, path, network);
			break;
		case NM_AUTH_TYPE_WPA_PSK_AUTO:
		case NM_AUTH_TYPE_WPA_PSK_TKIP:
		case NM_AUTH_TYPE_WPA_PSK_CCMP:
			s_wireless_sec = nm_gconf_read_0_6_wpa_settings (client, path, network);
			break;
		case NM_AUTH_TYPE_WPA_EAP:
			s_wireless_sec = nm_gconf_read_0_6_eap_settings (client, path, network);
			break;
		case NM_AUTH_TYPE_LEAP:
			s_wireless_sec = nm_gconf_read_0_6_leap_settings (client, path, network);
			break;
		default:
			g_warning ("Unknown NM 0.6 auth type %d on connection %s", we_cipher, dir);
			s_wireless_sec = NULL;
			break;
		}

		if (!s_wireless_sec) {
			g_object_unref (s_con);
			g_object_unref (s_wireless);
			g_free (path);
			g_free (network);
			return NULL;
		}
	} else
		s_wireless_sec = NULL;

	connection = nm_connection_new ();
	nm_connection_add_setting (connection, (NMSetting *)s_con);
	nm_connection_add_setting (connection, (NMSetting *)s_wireless);
	if (s_wireless_sec)
		nm_connection_add_setting (connection, (NMSetting *)s_wireless_sec);

	/* Would be better in nm_gconf_read_0_6_eap_settings, except that
	 * the connection object doesn't exist at that point. Hrmph.
	 */
	if (nm_gconf_get_string_helper (client, path, "wpa_eap_private_key_file", network, &private_key_path))
		g_object_set_data_full (G_OBJECT (connection), NMA_PATH_PRIVATE_KEY_TAG, private_key_path, g_free);
	if (nm_gconf_get_string_helper (client, path, "wpa_eap_client_cert_file", network, &client_cert_path))
		g_object_set_data_full (G_OBJECT (connection), NMA_PATH_CLIENT_CERT_TAG, client_cert_path, g_free);
	if (nm_gconf_get_string_helper (client, path, "wpa_eap_ca_cert_file", network, &ca_cert_path))
		g_object_set_data_full (G_OBJECT (connection), NMA_PATH_CA_CERT_TAG, ca_cert_path, g_free);

	g_free (path);
	g_free (network);

	return connection;
}

static NMSettingVPNProperties *
nm_gconf_0_6_vpnc_settings (GSList *vpn_data)
{
	NMSettingVPNProperties *s_vpn_props;
	GSList *iter;
	const char *key, *value;
	GValue *gvalue;

	s_vpn_props = (NMSettingVPNProperties *)nm_setting_vpn_properties_new ();
	for (iter = vpn_data; iter && iter->next; iter = iter->next->next) {
		key = iter->data;
		value = iter->next->data;

		gvalue = g_slice_new0 (GValue);
		if (*value) {
			g_value_init (gvalue, G_TYPE_STRING);
			g_value_set_string (gvalue, value);
		} else {
			g_value_init (gvalue, G_TYPE_BOOLEAN);
			g_value_set_boolean (gvalue, TRUE);
		}
		g_hash_table_insert (s_vpn_props->data, g_strdup (key), gvalue);
	}

	return s_vpn_props;
}

static const struct flagnames openvpn_contypes[] = {
	{ "x509",       0 /* NM_OPENVPN_CONTYPE_X509 */ },
	{ "shared-key", 1 /* NM_OPENVPN_CONTYPE_SHAREDKEY */ },
	{ "password",   2 /* NM_OPENVPN_CONTYPE_PASSWORD */ },
	{ NULL, 0 }
};

static NMSettingVPNProperties *
nm_gconf_0_6_openvpn_settings (GSList *vpn_data)
{
	NMSettingVPNProperties *s_vpn_props;
	GSList *iter;
	const char *key, *value;
	GValue *gvalue;
	int i;

	s_vpn_props = (NMSettingVPNProperties *)nm_setting_vpn_properties_new ();
	for (iter = vpn_data; iter && iter->next; iter = iter->next->next) {
		key = iter->data;
		value = iter->next->data;

		gvalue = g_slice_new0 (GValue);
		if (!strcmp (key, "port")) {
			g_value_init (gvalue, G_TYPE_UINT);
			g_value_set_uint (gvalue, atoi (value));
		} else if (!strcmp (key, "connection-type")) {
			g_value_init (gvalue, G_TYPE_INT);
			for (i = 0; openvpn_contypes[i].name; i++) {
				if (!strcmp (openvpn_contypes[i].name, value))
					g_value_set_int (gvalue, openvpn_contypes[i].value);
			}
		} else if (!strcmp (key, "comp-lzo")) {
			g_value_init (gvalue, G_TYPE_BOOLEAN);
			g_value_set_boolean (gvalue, !strcmp (value, "yes"));
		} else if (!strcmp (key, "dev")) {
			g_value_init (gvalue, G_TYPE_BOOLEAN);
			g_value_set_boolean (gvalue, !strcmp (value, "tap")); 
		} else if (!strcmp (key, "proto")) {
			g_value_init (gvalue, G_TYPE_BOOLEAN);
			g_value_set_boolean (gvalue, !strcmp (value, "tcp")); 
		} else {
			g_value_init (gvalue, G_TYPE_STRING);
			g_value_set_string (gvalue, value);
		}
		g_hash_table_insert (s_vpn_props->data, g_strdup (key), gvalue);
	}

	return s_vpn_props;
}

static NMConnection *
nm_gconf_read_0_6_vpn_connection (GConfClient *client,
						    const char *dir)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingVPN *s_vpn;
	NMSettingVPNProperties *s_vpn_props;
	char *path, *network, *name = NULL, *service_name = NULL;
	GSList *routes = NULL, *vpn_data = NULL;

	path = g_path_get_dirname (dir);
	network = g_path_get_basename (dir);

	if (!get_mandatory_string_helper (client, path, "name", network, &name)) {
		g_free (path);
		g_free (network);
		return NULL;
	}
	if (!get_mandatory_string_helper (client, path, "service_name", network, &service_name)) {
		g_free (name);
		g_free (path);
		g_free (network);
		return NULL;
	}

	if (!nm_gconf_get_stringlist_helper (client, path, "routes", network, &routes))
		routes = NULL;
	if (!nm_gconf_get_stringlist_helper (client, path, "vpn_data", network, &vpn_data))
		routes = NULL;

	s_con = (NMSettingConnection *)nm_setting_connection_new ();
	s_con->name = name;
	s_con->type = g_strdup ("vpn");

	s_vpn = (NMSettingVPN *)nm_setting_vpn_new ();
	s_vpn->service_type = service_name;
	s_vpn->routes = routes;

	if (!strcmp (service_name, "org.freedesktop.NetworkManager.vpnc"))
		s_vpn_props = nm_gconf_0_6_vpnc_settings (vpn_data);
	else if (!strcmp (service_name, "org.freedesktop.NetworkManager.openvpn"))
		s_vpn_props = nm_gconf_0_6_openvpn_settings (vpn_data);
	else {
		printf ("unmatched service name %s\n", service_name);
		s_vpn_props = NULL;
	}

	free_slist (vpn_data);
	g_free (path);
	g_free (network);

	connection = nm_connection_new ();
	nm_connection_add_setting (connection, (NMSetting *)s_con);
	nm_connection_add_setting (connection, (NMSetting *)s_vpn);
	if (s_vpn_props)
		nm_connection_add_setting (connection, (NMSetting *)s_vpn_props);

	return connection;
}

static void
nm_gconf_write_0_6_connection (NMConnection *conn, GConfClient *client, int n)
{
	char *dir;

	dir = g_strdup_printf ("%s/%d", GCONF_PATH_CONNECTIONS, n);
	nm_gconf_write_connection (conn, client, dir, NULL);
	g_free (dir);
}

#define GCONF_PATH_0_6_WIRELESS_NETWORKS "/system/networking/wireless/networks"
#define GCONF_PATH_0_6_VPN_CONNECTIONS   "/system/networking/vpn_connections"

void
nm_gconf_migrate_0_6_connections (GConfClient *client)
{
	GSList *connections, *iter;
	NMConnection *conn;
	int n;

	n = 1;

	connections = gconf_client_all_dirs (client, GCONF_PATH_0_6_WIRELESS_NETWORKS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		conn = nm_gconf_read_0_6_wireless_connection (client, iter->data);
		if (conn) {
			nm_gconf_write_0_6_connection (conn, client, n++);
			g_object_unref (conn);
		}
	}
	free_slist (connections);

	connections = gconf_client_all_dirs (client, GCONF_PATH_0_6_VPN_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		conn = nm_gconf_read_0_6_vpn_connection (client, iter->data);
		if (conn) {
			nm_gconf_write_0_6_connection (conn, client, n++);
			g_object_unref (conn);
		}
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

/* Converting NMSetting objects to GObject resulted in a change of the
 * service_type key to service-type.  Fix that up.
 */
void
nm_gconf_migrate_0_7_vpn_connections (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *value = NULL;

		if (nm_gconf_get_string_helper (client, iter->data, "service_type", "vpn", &value)) {
			char *old_key;

			nm_gconf_set_string_helper (client, iter->data, "service-type", "vpn", value);
			old_key = g_strdup_printf ("%s/vpn/service_type", iter->data);
			gconf_client_unset (client, old_key, NULL);
			g_free (old_key);
		}
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

