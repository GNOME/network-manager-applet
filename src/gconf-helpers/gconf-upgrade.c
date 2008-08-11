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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "wireless-helper.h"
#include <stdlib.h>
#include <netinet/in.h>
#include <errno.h>
#include <arpa/inet.h>

#include <gnome-keyring.h>
#include <nm-setting-connection.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>
#include <nm-setting-8021x.h>
#include <nm-setting-vpn.h>
#include <nm-setting-ip4-config.h>
#include <nm-utils.h>

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
                                const char *path,
                                const char *network,
                                NMSetting8021x **s_8021x)
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

	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	/* AFAICT, 0.6 reads this value from gconf, and then ignores it and always uses IW_AUTH_KEY_MGMT_802_1X */
	s_wireless_sec->key_mgmt = g_strdup ("ieee8021x"); /* FIXME? wpa-eap? */
	s_wireless_sec->proto = proto;
	s_wireless_sec->group = key_type; /* FIXME? */

	*s_8021x = (NMSetting8021x *) nm_setting_802_1x_new ();
	(*s_8021x)->eap = eap;
	(*s_8021x)->phase2_auth = phase2_type; /* FIXME? phase2_autheap? */
	(*s_8021x)->identity = identity;
	(*s_8021x)->anonymous_identity = anon_identity;

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

	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	s_wireless_sec->key_mgmt = key_mgmt;
	s_wireless_sec->leap_username = username;

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
	NMSetting8021x *s_8021x = NULL;
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
	s_con->id = g_strdup_printf ("Auto %s", essid);
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
			s_wireless_sec = nm_gconf_read_0_6_eap_settings (client, path, network, &s_8021x);
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
	if (s_8021x)
		nm_connection_add_setting (connection, (NMSetting *)s_8021x);

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

static void
nm_gconf_0_6_vpnc_settings (NMSettingVPN *s_vpn, GSList *vpn_data)
{
	GSList *iter;

	for (iter = vpn_data; iter && iter->next; iter = iter->next->next) {
		const char *key = iter->data;
		const char *value = iter->next->data;

		if (*value) {
			/* A string value */
			g_hash_table_insert (s_vpn->data, g_strdup (key), g_strdup (value));
		} else {
			/* A boolean; 0.6 treated key-without-value as "true" */
			g_hash_table_insert (s_vpn->data, g_strdup (key), g_strdup ("yes"));
		}
	}
}

static void
nm_gconf_0_6_openvpn_settings (NMSettingVPN *s_vpn, GSList *vpn_data)
{
	GSList *iter;

	for (iter = vpn_data; iter && iter->next; iter = iter->next->next) {
		const char *key = iter->data;
		const char *value = iter->next->data;

		if (!strcmp (key, "connection-type")) {
			if (!strcmp (value, "x509"))
				g_hash_table_insert (s_vpn->data, g_strdup (key), g_strdup ("tls"));
			else if (!strcmp (value, "shared-key"))
				g_hash_table_insert (s_vpn->data, g_strdup (key), g_strdup ("static-key"));
			else if (!strcmp (value, "password"))
				g_hash_table_insert (s_vpn->data, g_strdup (key), g_strdup ("password"));
		} else if (!strcmp (key, "comp-lzo")) {
			g_hash_table_insert (s_vpn->data, g_strdup (key), g_strdup ("yes"));
		} else if (!strcmp (key, "dev")) {
			if (!strcmp (value, "tap"))
				g_hash_table_insert (s_vpn->data, g_strdup ("tap-dev"), g_strdup ("yes"));
		} else if (!strcmp (key, "proto")) {
			if (!strcmp (value, "tcp"))
				g_hash_table_insert (s_vpn->data, g_strdup ("proto-tcp"), g_strdup ("yes"));
		} else
			g_hash_table_insert (s_vpn->data, g_strdup (key), g_strdup (value));
	}
}

static GSList *
convert_routes (GSList *str_routes)
{
	GSList *routes = NULL, *iter;

	for (iter = str_routes; iter; iter = g_slist_next (iter)) {
		struct in_addr tmp;
		char *p, *str_route;
		long int prefix = 32;

		str_route = g_strdup (iter->data);
		p = strchr (str_route, '/');
		if (!p || !(*(p + 1))) {
			g_warning ("Ignoring invalid route '%s'", str_route);
			goto next;
		}

		errno = 0;
		prefix = strtol (p + 1, NULL, 10);
		if (errno || prefix <= 0 || prefix > 32) {
			g_warning ("Ignoring invalid route '%s'", str_route);
			goto next;
		}

		/* don't pass the prefix to inet_pton() */
		*p = '\0';
		if (inet_pton (AF_INET, str_route, &tmp) > 0) {
			NMSettingIP4Route *route;

			route = g_new0 (NMSettingIP4Route, 1);
			route->address = tmp.s_addr;
			route->prefix = (guint32) prefix;

			routes = g_slist_append (routes, route);
		} else
			g_warning ("Ignoring invalid route '%s'", str_route);

next:
		g_free (str_route);
	}

	return routes;
}

static NMConnection *
nm_gconf_read_0_6_vpn_connection (GConfClient *client,
						    const char *dir)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingVPN *s_vpn;
	NMSettingIP4Config *s_ip4 = NULL;
	char *path, *network, *id = NULL, *service_name = NULL;
	GSList *str_routes = NULL, *vpn_data = NULL;

	path = g_path_get_dirname (dir);
	network = g_path_get_basename (dir);

	if (!get_mandatory_string_helper (client, path, "name", network, &id)) {
		g_free (path);
		g_free (network);
		return NULL;
	}
	if (!get_mandatory_string_helper (client, path, "service_name", network, &service_name)) {
		g_free (id);
		g_free (path);
		g_free (network);
		return NULL;
	}

	if (!nm_gconf_get_stringlist_helper (client, path, "routes", network, &str_routes))
		str_routes = NULL;
	if (!nm_gconf_get_stringlist_helper (client, path, "vpn_data", network, &vpn_data))
		vpn_data = NULL;

	s_con = (NMSettingConnection *)nm_setting_connection_new ();
	s_con->id = id;
	s_con->type = g_strdup ("vpn");

	s_vpn = (NMSettingVPN *)nm_setting_vpn_new ();
	s_vpn->service_type = service_name;

	if (!strcmp (service_name, "org.freedesktop.NetworkManager.vpnc"))
		nm_gconf_0_6_vpnc_settings (s_vpn, vpn_data);
	else if (!strcmp (service_name, "org.freedesktop.NetworkManager.openvpn"))
		nm_gconf_0_6_openvpn_settings (s_vpn, vpn_data);
	else
		g_warning ("unmatched service name %s\n", service_name);

	free_slist (vpn_data);
	g_free (path);
	g_free (network);

	if (str_routes) {
		s_ip4 = NM_SETTING_IP4_CONFIG (nm_setting_ip4_config_new ());
		s_ip4->routes = convert_routes (str_routes);
	}

	connection = nm_connection_new ();
	nm_connection_add_setting (connection, NM_SETTING (s_con));
	nm_connection_add_setting (connection, NM_SETTING (s_vpn));
	if (s_ip4)
		nm_connection_add_setting (connection, NM_SETTING (s_ip4));

	return connection;
}

static void
nm_gconf_write_0_6_connection (NMConnection *conn, GConfClient *client, int n)
{
	char *dir;
	char *id;

	dir = g_strdup_printf ("%s/%d", GCONF_PATH_CONNECTIONS, n);
	id = g_strdup_printf ("%d", n);
	nm_gconf_write_connection (conn, client, dir, id);
	g_free (dir);
	g_free (id);
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
			old_key = g_strdup_printf ("%s/vpn/service_type", (const char *) iter->data);
			gconf_client_unset (client, old_key, NULL);
			g_free (old_key);
		}
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

/* Changing the connection settings' 'name' property -> 'id' requires a rename
 * of the GConf key too.
 */
void
nm_gconf_migrate_0_7_connection_names (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *id = NULL;

		if (nm_gconf_get_string_helper (client, iter->data, "id", NM_SETTING_CONNECTION_SETTING_NAME, &id))
			g_free (id);
		else {
			char *value = NULL;

			if (nm_gconf_get_string_helper (client, iter->data, "name", NM_SETTING_CONNECTION_SETTING_NAME, &value)) {
				char *old_key;

				nm_gconf_set_string_helper (client, iter->data, "id", NM_SETTING_CONNECTION_SETTING_NAME, value);
				g_free (value);

				old_key = g_strdup_printf ("%s/" NM_SETTING_CONNECTION_SETTING_NAME "/name", (const char *) iter->data);
				gconf_client_unset (client, old_key, NULL);
				g_free (old_key);
			}
		}
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

static void
unset_ws_key (GConfClient *client, const char *dir, const char *key)
{
	char *old_key;

	old_key = g_strdup_printf ("%s/" NM_SETTING_WIRELESS_SECURITY_SETTING_NAME "/%s", dir, key);
	gconf_client_unset (client, old_key, NULL);
	g_free (old_key);
}

static void
copy_stringlist_to_8021x (GConfClient *client, const char *dir, const char *key)
{
	GSList *sa_val = NULL;

	if (!nm_gconf_get_stringlist_helper (client, dir, key, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, &sa_val))
		return;

	if (!nm_gconf_set_stringlist_helper (client, dir, key, NM_SETTING_802_1X_SETTING_NAME, sa_val))
		g_warning ("Could not convert string list value '%s' from wireless-security to 8021x setting", key);

	g_slist_foreach (sa_val, (GFunc) g_free, NULL);
	g_slist_free (sa_val);

	unset_ws_key (client, dir, key);
}

static void
copy_string_to_8021x (GConfClient *client, const char *dir, const char *key)
{
	char *val = NULL;

	if (!nm_gconf_get_string_helper (client, dir, key, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, &val))
		return;

	if (!nm_gconf_set_string_helper (client, dir, key, NM_SETTING_802_1X_SETTING_NAME, val))
		g_warning ("Could not convert string value '%s' from wireless-security to 8021x setting", key);

	g_free (val);

	unset_ws_key (client, dir, key);
}

static void
copy_bool_to_8021x (GConfClient *client, const char *dir, const char *key)
{
	gboolean val;

	if (!nm_gconf_get_bool_helper (client, dir, key, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, &val))
		return;

	if (val && !nm_gconf_set_bool_helper (client, dir, key, NM_SETTING_802_1X_SETTING_NAME, val))
		g_warning ("Could not convert string value '%s' from wireless-security to 8021x setting", key);

	unset_ws_key (client, dir, key);
}

static gboolean
try_convert_leap (GConfClient *client, const char *dir, const char *connection_id)
{
	char *val = NULL;
	GnomeKeyringResult ret;
	GList *found_list = NULL;
	GnomeKeyringFound *found;

	if (nm_gconf_get_string_helper (client, dir,
	                                NM_SETTING_WIRELESS_SECURITY_LEAP_USERNAME,
	                                NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                &val)) {
		/* Alredy converted */
		g_free (val);
		return TRUE;
	}

	if (!nm_gconf_get_string_helper (client, dir,
	                                 NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
	                                 NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                 &val))
		return FALSE;

	if (strcmp (val, "ieee8021x")) {
		g_free (val);
		return FALSE;
	}
	g_free (val);
	val = NULL;

	if (!nm_gconf_get_string_helper (client, dir,
	                                 NM_SETTING_WIRELESS_SECURITY_AUTH_ALG,
	                                 NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                 &val))
		return FALSE;

	if (strcmp (val, "leap")) {
		g_free (val);
		return FALSE;
	}
	g_free (val);
	val = NULL;

	/* Copy leap username */
	if (!nm_gconf_get_string_helper (client, dir,
	                                 "identity",
	                                 NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                 &val))
		return FALSE;

	if (!nm_gconf_set_string_helper (client, dir,
	                                 NM_SETTING_WIRELESS_SECURITY_LEAP_USERNAME,
	                                 NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                 val))
		g_warning ("Could not convert leap-username.");

	g_free (val);
	val = NULL;

	unset_ws_key (client, dir, NM_SETTING_802_1X_IDENTITY);

	if (!nm_gconf_get_string_helper (client, dir,
	                                 "id",
	                                 NM_SETTING_CONNECTION_SETTING_NAME,
	                                 &val))
		goto done;

	/* Copy the LEAP password */
	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      KEYRING_CID_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      val,
	                                      KEYRING_SN_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                      KEYRING_SK_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      "password",
	                                      NULL);
	if ((ret != GNOME_KEYRING_RESULT_OK) || (g_list_length (found_list) == 0))
		goto done;

	found = (GnomeKeyringFound *) found_list->data;
	nm_gconf_add_keyring_item (connection_id,
	                           val,
	                           NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                           NM_SETTING_WIRELESS_SECURITY_LEAP_PASSWORD,
	                           found->secret);
	gnome_keyring_item_delete_sync (found->keyring, found->item_id);

done:
	g_free (val);
	gnome_keyring_found_list_free (found_list);
	return TRUE;
}

static void
copy_keyring_to_8021x (GConfClient *client,
                       const char *dir,
                       const char *connection_id,
                       const char *key)
{
	char *name = NULL;
	GnomeKeyringResult ret;
	GList *found_list = NULL;
	GnomeKeyringFound *found;

	if (!nm_gconf_get_string_helper (client, dir,
	                                 "id",
	                                 NM_SETTING_CONNECTION_SETTING_NAME,
	                                 &name))
		return;

	/* Copy the LEAP password */
	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      KEYRING_CID_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      connection_id,
	                                      KEYRING_SN_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                      KEYRING_SK_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      key,
	                                      NULL);
	if ((ret != GNOME_KEYRING_RESULT_OK) || (g_list_length (found_list) == 0))
		goto done;

	found = (GnomeKeyringFound *) found_list->data;
	nm_gconf_add_keyring_item (connection_id, name, NM_SETTING_802_1X_SETTING_NAME, key, found->secret);

	gnome_keyring_item_delete_sync (found->keyring, found->item_id);

done:
	g_free (name);
	gnome_keyring_found_list_free (found_list);
}

void
nm_gconf_migrate_0_7_wireless_security (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *key_mgmt = NULL;
		GSList *eap = NULL;
		char *id = g_path_get_basename ((const char *) iter->data);

		if (!nm_gconf_get_string_helper (client, iter->data,
		                                 NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
		                                 NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
		                                 &key_mgmt))
			goto next;

		/* Only convert 802.1x-based connections */
		if (strcmp (key_mgmt, "ieee8021x") && strcmp (key_mgmt, "wpa-eap")) {
			g_free (key_mgmt);
			goto next;
		}
		g_free (key_mgmt);

		/* Leap gets converted differently */
		if (try_convert_leap (client, iter->data, id))
			goto next;

		/* Otherwise straight 802.1x */
		if (nm_gconf_get_stringlist_helper (client, iter->data,
		                                NM_SETTING_802_1X_EAP,
		                                NM_SETTING_802_1X_SETTING_NAME,
		                                &eap)) {
			/* Already converted */
			g_slist_foreach (eap, (GFunc) g_free, NULL);
			g_slist_free (eap);
			goto next;
		}

		copy_stringlist_to_8021x (client, iter->data, NM_SETTING_802_1X_EAP);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_IDENTITY);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_ANONYMOUS_IDENTITY);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_CA_PATH);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_PHASE1_PEAPVER);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_PHASE1_PEAPLABEL);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_PHASE1_FAST_PROVISIONING);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_PHASE2_AUTH);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_PHASE2_AUTHEAP);
		copy_string_to_8021x (client, iter->data, NM_SETTING_802_1X_PHASE2_CA_PATH);
		copy_string_to_8021x (client, iter->data, NMA_PATH_CA_CERT_TAG);
		copy_string_to_8021x (client, iter->data, NMA_PATH_CLIENT_CERT_TAG);
		copy_string_to_8021x (client, iter->data, NMA_PATH_PRIVATE_KEY_TAG);
		copy_string_to_8021x (client, iter->data, NMA_PATH_PHASE2_CA_CERT_TAG);
		copy_string_to_8021x (client, iter->data, NMA_PATH_PHASE2_CLIENT_CERT_TAG);
		copy_string_to_8021x (client, iter->data, NMA_PATH_PHASE2_PRIVATE_KEY_TAG);

		copy_bool_to_8021x (client, iter->data, NMA_CA_CERT_IGNORE_TAG);
		copy_bool_to_8021x (client, iter->data, NMA_PHASE2_CA_CERT_IGNORE_TAG);

		copy_keyring_to_8021x (client, iter->data, id, NM_SETTING_802_1X_PASSWORD);
		copy_keyring_to_8021x (client, iter->data, id, NM_SETTING_802_1X_PIN);
		copy_keyring_to_8021x (client, iter->data, id, NM_SETTING_802_1X_PSK);
		copy_keyring_to_8021x (client, iter->data, id, NMA_PRIVATE_KEY_PASSWORD_TAG);
		copy_keyring_to_8021x (client, iter->data, id, NMA_PHASE2_PRIVATE_KEY_PASSWORD_TAG);

next:
		g_free (id);
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

/*
 * Move all keyring items to use 'connection-id' instead of 'connection-name'
 */
void
nm_gconf_migrate_0_7_keyring_items (GConfClient *client)
{
	GSList *connections;
	GSList *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = g_slist_next (iter)) {
		GnomeKeyringResult ret;
		GList *found_list = NULL, *elt;
		char *name = NULL;
		char *id = g_path_get_basename (iter->data);

		if (!nm_gconf_get_string_helper (client, iter->data,
		                                 "id",
		                                 NM_SETTING_CONNECTION_SETTING_NAME,
		                                 &name))
			goto next;

		ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
		                                      &found_list,
		                                      "connection-name",
		                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
		                                      name,
		                                      NULL);
		if ((ret != GNOME_KEYRING_RESULT_OK) || (g_list_length (found_list) == 0))
			goto next;

		for (elt = found_list; elt; elt = g_list_next (elt)) {
			GnomeKeyringFound *found = (GnomeKeyringFound *) elt->data;
			char *setting_name = NULL;
			char *setting_key = NULL;
			int i;

			for (i = 0; found->attributes && (i < found->attributes->len); i++) {
				GnomeKeyringAttribute *attr;

				attr = &(gnome_keyring_attribute_list_index (found->attributes, i));
				if (!strcmp (attr->name, KEYRING_SN_TAG) && (attr->type == GNOME_KEYRING_ATTRIBUTE_TYPE_STRING))
					setting_name = attr->value.string;
				else if (!strcmp (attr->name, KEYRING_SK_TAG) && (attr->type == GNOME_KEYRING_ATTRIBUTE_TYPE_STRING))
					setting_key = attr->value.string;
			}

			if (setting_name && setting_key) {
				nm_gconf_add_keyring_item (id, name, setting_name, setting_key, found->secret);
				ret = gnome_keyring_item_delete_sync (found->keyring, found->item_id);
			}
		}
		gnome_keyring_found_list_free (found_list);

	next:
		g_free (name);
		g_free (id);
	}


}

void
nm_gconf_migrate_0_7_netmask_to_prefix (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *id = g_path_get_basename ((const char *) iter->data);
		GArray *array, *new;
		int i;
		gboolean need_update = FALSE;

		if (!nm_gconf_get_uint_array_helper (client, iter->data,
		                                     NM_SETTING_IP4_CONFIG_ADDRESSES,
		                                     NM_SETTING_IP4_CONFIG_SETTING_NAME,
		                                     &array))
			goto next;

		new = g_array_sized_new (FALSE, TRUE, sizeof (guint32), array->len);
		for (i = 0; i < array->len; i+=3) {
			guint32 addr, netmask, prefix, gateway;

			addr = g_array_index (array, guint32, i);
			g_array_append_val (new, addr);

			/* get the second element of the 3-number IP address tuple */
			netmask = g_array_index (array, guint32, i + 1);
			if (netmask > 32) {
				/* convert it */
				prefix = nm_utils_ip4_netmask_to_prefix (netmask);
				g_array_append_val (new, prefix);
				need_update = TRUE;
			} else {
				/* Probably already a prefix */
				g_array_append_val (new, netmask);
			}

			gateway = g_array_index (array, guint32, i + 2);
			g_array_append_val (new, gateway);
		}

		/* Update GConf */
		if (need_update) {
			nm_gconf_set_uint_array_helper (client, iter->data,
			                                NM_SETTING_IP4_CONFIG_ADDRESSES,
			                                NM_SETTING_IP4_CONFIG_SETTING_NAME,
			                                new);
		}
		g_array_free (new, TRUE);

next:
		g_free (id);
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

void
nm_gconf_migrate_0_7_ip4_method (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *id = g_path_get_basename ((const char *) iter->data);
		char *method = NULL;

		if (!nm_gconf_get_string_helper (client, iter->data,
		                                 NM_SETTING_IP4_CONFIG_METHOD,
		                                 NM_SETTING_IP4_CONFIG_SETTING_NAME,
		                                 &method))
			goto next;

		if (!strcmp (method, "autoip")) {
			nm_gconf_set_string_helper (client, iter->data,
			                            NM_SETTING_IP4_CONFIG_METHOD,
			                            NM_SETTING_IP4_CONFIG_SETTING_NAME,
			                            NM_SETTING_IP4_CONFIG_METHOD_LINK_LOCAL);
		} else if (!strcmp (method, "dhcp")) {
			nm_gconf_set_string_helper (client, iter->data,
			                            NM_SETTING_IP4_CONFIG_METHOD,
			                            NM_SETTING_IP4_CONFIG_SETTING_NAME,
			                            NM_SETTING_IP4_CONFIG_METHOD_AUTO);
		}

		g_free (method);

next:
		g_free (id);
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

#define IP4_KEY_IGNORE_DHCP_DNS "ignore-dhcp-dns"

void
nm_gconf_migrate_0_7_ignore_dhcp_dns (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *del_key;
		gboolean ignore_auto_dns = FALSE;

		if (!nm_gconf_get_bool_helper (client, iter->data,
		                               IP4_KEY_IGNORE_DHCP_DNS,
		                               NM_SETTING_IP4_CONFIG_SETTING_NAME,
		                               &ignore_auto_dns))
			continue;

		/* add new key with new name */
		if (ignore_auto_dns) {
			nm_gconf_set_bool_helper (client, iter->data,
			                          NM_SETTING_IP4_CONFIG_IGNORE_AUTO_DNS,
			                          NM_SETTING_IP4_CONFIG_SETTING_NAME,
			                          ignore_auto_dns);
		}

		/* delete old key */
		del_key = g_strdup_printf ("%s/%s/%s",
		                           (const char *) iter->data,
		                           NM_SETTING_IP4_CONFIG_SETTING_NAME,
		                           IP4_KEY_IGNORE_DHCP_DNS);
		gconf_client_unset (client, del_key, NULL);
		g_free (del_key);
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

static gboolean
convert_route (const char *in_route, NMSettingIP4Route *converted)
{
	struct in_addr tmp;
	char *p, *str_route;
	long int prefix = 32;
	gboolean success = FALSE;

	memset (converted, 0, sizeof (*converted));

	str_route = g_strdup (in_route);
	p = strchr (str_route, '/');
	if (!p || !(*(p + 1))) {
		g_warning ("Ignoring invalid route '%s'", str_route);
		goto out;
	}

	errno = 0;
	prefix = strtol (p + 1, NULL, 10);
	if (errno || prefix <= 0 || prefix > 32) {
		g_warning ("Ignoring invalid route '%s'", str_route);
		goto out;
	}

	/* don't pass the prefix to inet_pton() */
	*p = '\0';
	if (inet_pton (AF_INET, str_route, &tmp) <= 0) {
		g_warning ("Ignoring invalid route '%s'", str_route);
		goto out;
	}

	converted->address = tmp.s_addr;
	converted->prefix = (guint32) prefix;
	success = TRUE;

out:
	g_free (str_route);
	return success;
}

#define VPN_KEY_ROUTES "routes"

static void
free_one_route (gpointer data, gpointer user_data)
{
	g_array_free ((GArray *) data, TRUE);
}

void
nm_gconf_migrate_0_7_vpn_routes (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *del_key;
		GSList *old_routes = NULL, *routes_iter;
		GPtrArray *new_routes = NULL;

		if (!nm_gconf_get_stringlist_helper (client, iter->data,
		                                     VPN_KEY_ROUTES,
		                                     NM_SETTING_VPN_SETTING_NAME,
		                                     &old_routes))
			continue;

		/* Convert 'x.x.x.x/x' into a route structure */
		for (routes_iter = old_routes; routes_iter; routes_iter = g_slist_next (routes_iter)) {
			NMSettingIP4Route route;

			if (convert_route (routes_iter->data, &route)) {
				GArray *tmp_route;

				if (!new_routes)
					new_routes = g_ptr_array_sized_new (3);

				tmp_route = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 4);
				g_array_append_val (tmp_route, route.address);
				g_array_append_val (tmp_route, route.prefix);
				g_array_append_val (tmp_route, route.next_hop);
				g_array_append_val (tmp_route, route.metric);
				g_ptr_array_add (new_routes, tmp_route);
			}
		}

		if (new_routes) {
			char *method = NULL;

			/* Set new routes */
			nm_gconf_set_ip4_helper (client, iter->data,
			                         NM_SETTING_IP4_CONFIG_ROUTES,
			                         NM_SETTING_IP4_CONFIG_SETTING_NAME,
			                         4,
			                         new_routes);

			g_ptr_array_foreach (new_routes, (GFunc) free_one_route, NULL);
			g_ptr_array_free (new_routes, TRUE);

			/* To make a valid ip4 setting, need a method too */
			if (!nm_gconf_get_string_helper (client, iter->data,
			                                 NM_SETTING_IP4_CONFIG_METHOD,
			                                 NM_SETTING_IP4_CONFIG_SETTING_NAME,
			                                 &method)) {				
				/* If no method was specified, use 'auto' */
				nm_gconf_set_string_helper (client, iter->data,
				                            NM_SETTING_IP4_CONFIG_METHOD,
				                            NM_SETTING_IP4_CONFIG_SETTING_NAME,
				                            NM_SETTING_IP4_CONFIG_METHOD_AUTO);
			}
			g_free (method);
		}

		/* delete old key */
		del_key = g_strdup_printf ("%s/%s/%s",
		                           (const char *) iter->data,
		                           NM_SETTING_VPN_SETTING_NAME,
		                           VPN_KEY_ROUTES);
		gconf_client_unset (client, del_key, NULL);
		g_free (del_key);

		g_slist_foreach (old_routes, (GFunc) g_free, NULL);
		g_slist_free (old_routes);
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

void
nm_gconf_migrate_0_7_vpn_properties (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *path;
		GSList *properties, *props_iter;

		path = g_strdup_printf ("%s/vpn-properties", (const char *) iter->data);
		properties = gconf_client_all_entries (client, path, NULL);

		for (props_iter = properties; props_iter; props_iter = props_iter->next) {
			GConfEntry *entry = (GConfEntry *) props_iter->data;
			char *tmp;
			char *key_name = g_path_get_basename (entry->key);

			/* 'service-type' is reserved */
			if (!strcmp (key_name, NM_SETTING_VPN_SERVICE_TYPE))
				goto next;

			/* Don't convert the setting name */
			if (!strcmp (key_name, NM_SETTING_NAME))
				goto next;

			switch (entry->value->type) {
			case GCONF_VALUE_STRING:
				nm_gconf_set_string_helper (client, (const char *) iter->data,
				                            key_name,
				                            NM_SETTING_VPN_SETTING_NAME,
				                            gconf_value_get_string (entry->value));
				break;
			case GCONF_VALUE_INT:
				tmp = g_strdup_printf ("%d", gconf_value_get_int (entry->value));
				nm_gconf_set_string_helper (client, (const char *) iter->data,
				                            key_name,
				                            NM_SETTING_VPN_SETTING_NAME,
				                            tmp);
				g_free (tmp);
				break;
			case GCONF_VALUE_BOOL:
				tmp = gconf_value_get_bool (entry->value) ? "yes" : "no";
				nm_gconf_set_string_helper (client, (const char *) iter->data,
				                            key_name,
				                            NM_SETTING_VPN_SETTING_NAME,
				                            tmp);
				break;
			default:
				g_warning ("%s: don't know how to convert type %d",
				           __func__, entry->value->type);
				break;
			}

		next:
			g_free (key_name);
		}

		/* delete old vpn-properties dir */
		gconf_client_recursive_unset (client, path, 0, NULL);
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

static void
move_one_vpn_string_bool (GConfClient *client,
                          const char *path,
                          const char *old_key,
                          const char *new_key)
{
	char *del_key;
	char *value = NULL;

	if (!nm_gconf_get_string_helper (client, path,
	                                 old_key,
	                                 NM_SETTING_VPN_SETTING_NAME,
	                                 &value));
		return;

	if (value && !strcmp (value, "yes")) {
		nm_gconf_set_string_helper (client, path,
		                            new_key,
		                            NM_SETTING_VPN_SETTING_NAME,
		                            "yes");
	}
	g_free (value);

	/* delete old key */
	del_key = g_strdup_printf ("%s/%s/%s",
	                           path,
	                           NM_SETTING_VPN_SETTING_NAME,
	                           old_key);
	gconf_client_unset (client, del_key, NULL);
	g_free (del_key);
}

void
nm_gconf_migrate_0_7_openvpn_properties (GConfClient *client)
{
	GSList *connections, *iter;

	connections = gconf_client_all_dirs (client, GCONF_PATH_CONNECTIONS, NULL);
	for (iter = connections; iter; iter = iter->next) {
		char *old_type, *new_type = NULL, *service = NULL;

		if (!nm_gconf_get_string_helper (client, (const char *) iter->data,
		                                 NM_SETTING_VPN_SERVICE_TYPE,
		                                 NM_SETTING_VPN_SETTING_NAME,
		                                 &service))
			continue;

		if (!service || strcmp (service, "org.freedesktop.NetworkManager.openvpn"))
			continue;

		move_one_vpn_string_bool (client, iter->data, "dev", "tap-dev");
		move_one_vpn_string_bool (client, iter->data, "proto", "proto-tcp");

		if (!nm_gconf_get_string_helper (client, (const char *) iter->data,
		                                 "connection-type",
		                                 NM_SETTING_VPN_SETTING_NAME,
		                                 &old_type))
			continue;

		/* Convert connection type from old integer to new string */
		if (!strcmp (old_type, "0"))
			new_type = "tls";
		else if (!strcmp (old_type, "1"))
			new_type = "static-key";
		else if (!strcmp (old_type, "2"))
			new_type = "password";
		else if (!strcmp (old_type, "3"))
			new_type = "password-tls";

		if (new_type) {
			nm_gconf_set_string_helper (client, (const char *) iter->data,
			                            "connection-type",
			                            NM_SETTING_VPN_SETTING_NAME,
			                            new_type);
		}
	}
	free_slist (connections);

	gconf_client_suggest_sync (client, NULL);
}

