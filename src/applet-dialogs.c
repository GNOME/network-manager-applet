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
 * (C) Copyright 2008 - 2010 Red Hat, Inc.
 */

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>

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
#include <nm-setting-ip6-config.h>
#include <nm-utils.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "applet-dialogs.h"
#include "utils.h"
#include "nma-bling-spinner.h"


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

static gchar *
ip6_address_as_string (const struct in6_addr *ip)
{
	char buf[INET6_ADDRSTRLEN];

	memset (&buf, '\0', sizeof (buf));

	if (inet_ntop (AF_INET6, ip, buf, INET6_ADDRSTRLEN)) {
		return g_strdup (buf);
	} else {
		int j;
		GString *ip6_str = g_string_new (NULL);
		g_string_append_printf (ip6_str, "%02X", ip->s6_addr[0]);
		for (j = 1; j < 16; j++)
			g_string_append_printf (ip6_str, " %02X", ip->s6_addr[j]);
		nm_warning ("%s: error converting IP6 address %s",
		            __func__, ip6_str->str);
		g_string_free (ip6_str, TRUE);
		return NULL;
	}
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
create_info_group_label (const char *text, gboolean selectable)
{
	GtkWidget *label;
	char *markup;

	label = create_info_label (NULL, selectable);
	markup = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", text);
	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (markup);

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
			label = g_strdup (C_("Wifi/wired security", "None"));
		}
	} else if (!strcmp (connection_type, NM_SETTING_WIRED_SETTING_NAME)) {
		NMSetting8021x *s_8021x;

		s_8021x = (NMSetting8021x *) nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
		if (s_8021x)
			label = get_eap_label (NULL, s_8021x);
		else
			label = g_strdup (C_("Wifi/wired security", "None"));
	}

	w = create_info_label (label ? label : C_("Wifi/wired security", "Unknown"), TRUE);
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

	gtk_label_set_text (GTK_LABEL (speed_label), str ? str : C_("Speed", "Unknown"));
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
	const char *iface, *method;
	NMIP4Config *ip4_config;
	NMIP6Config *ip6_config;
	const GArray *dns;
	const GSList *dns6;
	NMIP4Address *def_addr = NULL;
	NMIP6Address *def6_addr = NULL;
	NMSettingIP6Config *s_ip6;
	guint32 hostmask, network, bcast, netmask;
	int i, row = 0;
	SpeedInfo* info = NULL;
	GtkWidget* speed_label;
	const GSList *addresses;

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


	/*--- General ---*/
	gtk_table_attach (table, create_info_group_label (_("General"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	row++;

	gtk_table_attach (table, create_info_label (_("Interface:"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (table, create_info_label (str, TRUE),
	                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	g_free (str);
	row++;

	/* Hardware address */
	str = NULL;
	if (NM_IS_DEVICE_ETHERNET (device))
		str = g_strdup (nm_device_ethernet_get_hw_address (NM_DEVICE_ETHERNET (device)));
	else if (NM_IS_DEVICE_WIFI (device))
		str = g_strdup (nm_device_wifi_get_hw_address (NM_DEVICE_WIFI (device)));

	gtk_table_attach (table, create_info_label (_("Hardware Address:"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (table, create_info_label (str, TRUE),
	                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	g_free (str);
	row++;

	/* Driver */
	gtk_table_attach (table, create_info_label (_("Driver:"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (table, create_info_label (nm_device_get_driver (device), TRUE),
	                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
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

	gtk_label_set_text (GTK_LABEL(speed_label), str ? str : C_("Speed", "Unknown"));
	g_free (str);

	gtk_table_attach (table, create_info_label (_("Speed:"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (table, speed_label,
	                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	row++;

	/* Security */
	gtk_table_attach (table, create_info_label (_("Security:"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (table, create_info_label_security (connection),
	                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	row++;

	/* Empty line */
	gtk_table_attach (table, gtk_label_new (""), 0, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	row++;

	/*--- IPv4 ---*/
	gtk_table_attach (table, create_info_group_label (_("IPv4"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);

	row++;

	ip4_config = nm_device_get_ip4_config (device);
	addresses = nm_ip4_config_get_addresses (ip4_config);
	if (g_slist_length ((GSList *) addresses))
		def_addr = addresses->data;

	/* Address */
	gtk_table_attach (table, create_info_label (_("IP Address:"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	str = def_addr ? ip4_address_as_string (nm_ip4_address_get_address (def_addr)) : g_strdup (C_("Address", "Unknown"));
	gtk_table_attach (table, create_info_label (str, TRUE),
	                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	g_free (str);
	row++;

	/* Broadcast */
	if (def_addr) {
		netmask = nm_utils_ip4_prefix_to_netmask (nm_ip4_address_get_prefix (def_addr));
		network = ntohl (nm_ip4_address_get_address (def_addr)) & ntohl (netmask);
		hostmask = ~ntohl (netmask);
		bcast = htonl (network | hostmask);
	}

	gtk_table_attach (table, create_info_label (_("Broadcast Address:"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	str = def_addr ? ip4_address_as_string (bcast) : g_strdup (C_("Address", "Unknown"));
	gtk_table_attach (table, create_info_label (str, TRUE),
	                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	g_free (str);
	row++;

	/* Prefix */
	gtk_table_attach (table, create_info_label (_("Subnet Mask:"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	str = def_addr ? ip4_address_as_string (netmask) : g_strdup (C_("Subnet Mask", "Unknown"));
	gtk_table_attach (table, create_info_label (str, TRUE),
	                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	g_free (str);
	row++;

	/* Gateway */
	if (def_addr && nm_ip4_address_get_gateway (def_addr)) {
		gtk_table_attach (table, create_info_label (_("Default Route:"), FALSE),
		                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		str = ip4_address_as_string (nm_ip4_address_get_gateway (def_addr));
		gtk_table_attach (table, create_info_label (str, TRUE),
		                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		g_free (str);
		row++;
	}

	/* DNS */
	dns = def_addr ? nm_ip4_config_get_nameservers (ip4_config) : NULL;
	if (dns && dns->len) {
		gtk_table_attach (table, create_info_label (_("Primary DNS:"), FALSE),
		                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		str = ip4_address_as_string (g_array_index (dns, guint32, 0));
		gtk_table_attach (table, create_info_label (str, TRUE),
		                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		g_free (str);
		row++;

		if (dns->len > 1) {
			gtk_table_attach (table, create_info_label (_("Secondary DNS:"), FALSE),
			                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
			str = ip4_address_as_string (g_array_index (dns, guint32, 1));
			gtk_table_attach (table, create_info_label (str, TRUE),
			                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
			g_free (str);
			row++;
		}

		if (dns->len > 2) {
			gtk_table_attach (table, create_info_label (_("Ternary DNS:"), FALSE),
			                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
			str = ip4_address_as_string (g_array_index (dns, guint32, 2));
			gtk_table_attach (table, create_info_label (str, TRUE),
			                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
			g_free (str);
			row++;
		}
	}

	/* Empty line */
	gtk_table_attach (table, gtk_label_new (""), 0, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	row++;

	/*--- IPv6 ---*/
	gtk_table_attach (table, create_info_group_label (_("IPv6"), FALSE),
	                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
	row++;

	s_ip6 = (NMSettingIP6Config *) nm_connection_get_setting (connection, NM_TYPE_SETTING_IP6_CONFIG);
	if (s_ip6)
		 method = nm_setting_ip6_config_get_method (s_ip6);

	if (!method || !strcmp (method, NM_SETTING_IP6_CONFIG_METHOD_IGNORE)) {
		gtk_table_attach (table, create_info_label (_("Ignored"), FALSE),
		                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		row++;
	}

	ip6_config = nm_device_get_ip6_config (device);
	if (ip6_config) {
		addresses = nm_ip6_config_get_addresses (ip6_config);
		if (g_slist_length ((GSList *) addresses))
			def6_addr = addresses->data;
	}

	/* Address */
	if (def6_addr) {
		char *tmp_addr;
		guint32 prefix;

		gtk_table_attach (table, create_info_label (_("IP Address:"), FALSE),
		                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		tmp_addr = ip6_address_as_string (nm_ip6_address_get_address (def6_addr));
		prefix = nm_ip6_address_get_prefix (def6_addr);
		str = g_strdup_printf ("%s/%d", tmp_addr, prefix);
		g_free (tmp_addr);

		gtk_table_attach (table, create_info_label (str, TRUE),
		                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		g_free (str);
		row++;
	}

	/* Gateway */
	if (def6_addr && nm_ip6_address_get_gateway (def6_addr)) {
		gtk_table_attach (table, create_info_label (_("Default Route:"), FALSE),
		                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		str = ip6_address_as_string (nm_ip6_address_get_gateway (def6_addr));
		gtk_table_attach (table, create_info_label (str, TRUE),
		                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		g_free (str);
		row++;
	}

	/* DNS */
	dns6 = def6_addr ? nm_ip6_config_get_nameservers (ip6_config) : NULL;

	for (i = 0; dns6 && i < 3 ; dns6 = g_slist_next (dns6), i++) {
		char *label[] = { "Primary DNS:", "Secondary DNS:", "Ternary DNS:" };

		gtk_table_attach (table, create_info_label (_(label[i]), FALSE),
		                  0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		str = ip6_address_as_string (dns6->data);
		gtk_table_attach (table, create_info_label (str, TRUE),
		                  1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
		g_free (str);
		row++;
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

	notebook = GTK_NOTEBOOK (GTK_WIDGET (gtk_builder_get_object (applet->info_dialog_ui, "info_notebook")));

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

	return GTK_WIDGET (gtk_builder_get_object (applet->info_dialog_ui, "info_dialog"));
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
	gtk_window_present_with_time (GTK_WINDOW (dialog),
		gdk_x11_get_server_time (gtk_widget_get_window (dialog)));
}

#if !GTK_CHECK_VERSION(2,23,0)
static void 
about_dialog_handle_url_cb (GtkAboutDialog *about, const gchar *url, gpointer data)
{
	gboolean ret;
	char *cmdline;
	GdkScreen *screen;

	screen = gtk_window_get_screen (GTK_WINDOW (about));

	cmdline = g_strconcat ("gnome-open ", url, NULL);
	ret = gdk_spawn_command_line_on_screen (screen, cmdline, NULL);
	g_free (cmdline);

	if (ret == FALSE) {
		cmdline = g_strconcat ("xdg-open ", url, NULL);
		ret = gdk_spawn_command_line_on_screen (screen, cmdline, NULL);
		g_free (cmdline);
	}
}
#endif

void
applet_about_dialog_show (NMApplet *applet)
{
#if !GTK_CHECK_VERSION(2,23,0)
	gtk_about_dialog_set_url_hook (about_dialog_handle_url_cb, NULL, NULL);
#endif
	gtk_show_about_dialog (NULL,
	                       "version", VERSION,
	                       "copyright", _("Copyright \xc2\xa9 2004-2011 Red Hat, Inc.\n"
	                                      "Copyright \xc2\xa9 2005-2008 Novell, Inc.\n"
	                                      "and many other community contributors and translators"),
	                       "comments", _("Notification area applet for managing your network devices and connections."),
	                       "website", "http://www.gnome.org/projects/NetworkManager/",
	                       "website-label", _("NetworkManager Website"),
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
	gtk_window_present_with_time (GTK_WINDOW (dialog),
		gdk_x11_get_server_time (gtk_widget_get_window (dialog)));

	g_signal_connect_swapped (dialog, "response",
	                          G_CALLBACK (gtk_widget_destroy),
	                          dialog);
	return dialog;
}

GtkWidget *
applet_mobile_password_dialog_new (NMDevice *device,
                                   NMConnection *connection,
                                   GtkEntry **out_secret_entry)
{
	GtkDialog *dialog;
	GtkWidget *w;
	GtkBox *box = NULL, *vbox = NULL;
	char *dev_str;
	NMSettingConnection *s_con;
	char *tmp;
	const char *id;

	dialog = GTK_DIALOG (gtk_dialog_new ());
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Mobile broadband network password"));

	w = gtk_dialog_add_button (dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	w = gtk_dialog_add_button (dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default (GTK_WINDOW (dialog), w);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	id = nm_setting_connection_get_id (s_con);
	g_assert (id);
	tmp = g_strdup_printf (_("A password is required to connect to '%s'."), id);
	w = gtk_label_new (tmp);
	g_free (tmp);

	vbox = GTK_BOX (gtk_dialog_get_content_area (dialog));

	gtk_box_pack_start (vbox, w, TRUE, TRUE, 0);

	dev_str = g_strdup_printf ("<b>%s</b>", utils_get_device_description (device));
	w = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (w), dev_str);
	g_free (dev_str);
	gtk_box_pack_start (vbox, w, TRUE, TRUE, 0);

	w = gtk_alignment_new (0.5, 0.5, 0, 1.0);
	gtk_box_pack_start (vbox, w, TRUE, TRUE, 0);

	box = GTK_BOX (gtk_hbox_new (FALSE, 6));
	gtk_container_set_border_width (GTK_CONTAINER (box), 6);
	gtk_container_add (GTK_CONTAINER (w), GTK_WIDGET (box));

	gtk_box_pack_start (box, gtk_label_new (_("Password:")), FALSE, FALSE, 0);

	w = gtk_entry_new ();
	*out_secret_entry = GTK_ENTRY (w);
	gtk_entry_set_activates_default (GTK_ENTRY (w), TRUE);
	gtk_box_pack_start (box, w, FALSE, FALSE, 0);

	gtk_widget_show_all (GTK_WIDGET (vbox));
	return GTK_WIDGET (dialog);
}

/**********************************************************************/

static void
mpd_entry_changed (GtkWidget *widget, gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET (user_data);
	GtkBuilder *builder = g_object_get_data (G_OBJECT (dialog), "builder");
	GtkWidget *entry;
	guint32 minlen;
	gboolean valid = FALSE;
	const char *text, *text2 = NULL, *text3 = NULL;
	gboolean match23;

	g_return_if_fail (builder != NULL);

	entry = GTK_WIDGET (gtk_builder_get_object (builder, "code1_entry"));
	if (g_object_get_data (G_OBJECT (entry), "active")) {
		minlen = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (entry), "minlen"));
		text = gtk_entry_get_text (GTK_ENTRY (entry));
		if (text && (strlen (text) < minlen))
			goto done;
	}

	entry = GTK_WIDGET (gtk_builder_get_object (builder, "code2_entry"));
	if (g_object_get_data (G_OBJECT (entry), "active")) {
		minlen = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (entry), "minlen"));
		text2 = gtk_entry_get_text (GTK_ENTRY (entry));
		if (text2 && (strlen (text2) < minlen))
			goto done;
	}

	entry = GTK_WIDGET (gtk_builder_get_object (builder, "code3_entry"));
	if (g_object_get_data (G_OBJECT (entry), "active")) {
		minlen = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (entry), "minlen"));
		text3 = gtk_entry_get_text (GTK_ENTRY (entry));
		if (text3 && (strlen (text3) < minlen))
			goto done;
	}

	/* Validate 2 & 3 if they are supposed to be the same */
	match23 = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (dialog), "match23"));
	if (match23) {
		if (!text2 || !text3 || strcmp (text2, text3))
			goto done;
	}

	valid = TRUE;

done:
	/* Clear any error text in the progress label now that the user has changed something */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progress_label"));
	gtk_label_set_text (GTK_LABEL (widget), "");

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "unlock_button"));
	g_warn_if_fail (widget != NULL);
	gtk_widget_set_sensitive (widget, valid);
	if (valid)
		gtk_widget_grab_default (widget);
}

void
applet_mobile_pin_dialog_destroy (GtkWidget *widget)
{
	gtk_widget_hide (widget);
	gtk_widget_destroy (widget);
}

static void
mpd_cancel_dialog (GtkDialog *dialog)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_CANCEL);
}

static void
show_toggled_cb (GtkWidget *button, gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET (user_data);
	gboolean show;
	GtkWidget *widget;
	GtkBuilder *builder;

	builder = g_object_get_data (G_OBJECT (dialog), "builder");
	g_return_if_fail (builder != NULL);

	show = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code1_entry"));
	gtk_entry_set_visibility (GTK_ENTRY (widget), show);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code2_entry"));
	gtk_entry_set_visibility (GTK_ENTRY (widget), show);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code3_entry"));
	gtk_entry_set_visibility (GTK_ENTRY (widget), show);
}

GtkWidget *
applet_mobile_pin_dialog_new (const char *title,
                              const char *header,
                              const char *desc,
                              const char *show_password_label)
{
	char *str;
	GtkWidget *dialog;
	GtkWidget *widget;
	GError *error = NULL;
	GtkBuilder *builder;

	g_return_val_if_fail (title != NULL, NULL);
	g_return_val_if_fail (header != NULL, NULL);
	g_return_val_if_fail (desc != NULL, NULL);
	g_return_val_if_fail (show_password_label != NULL, NULL);

	builder = gtk_builder_new ();

	if (!gtk_builder_add_from_file (builder, UIDIR "/gsm-unlock.ui", &error)) {
		g_warning ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
		g_object_unref (builder);
		return NULL;
	}

	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "unlock_dialog"));
	if (!dialog) {
		g_object_unref (builder);
		g_return_val_if_fail (dialog != NULL, NULL);
	}

	g_object_set_data_full (G_OBJECT (dialog), "builder", builder, (GDestroyNotify) g_object_unref);

	gtk_window_set_title (GTK_WINDOW (dialog), title);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "header_label"));
	str = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>", header);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_label_set_markup (GTK_LABEL (widget), str);
	g_free (str);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "desc_label"));
	gtk_label_set_text (GTK_LABEL (widget), desc);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "show_password_checkbutton"));
	gtk_button_set_label (GTK_BUTTON (widget), show_password_label);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);
	g_signal_connect (widget, "toggled", G_CALLBACK (show_toggled_cb), dialog);
	show_toggled_cb (widget, dialog);

	g_signal_connect (dialog, "delete-event", G_CALLBACK (mpd_cancel_dialog), NULL);

	mpd_entry_changed (NULL, dialog);

	return dialog;
}

void
applet_mobile_pin_dialog_present (GtkWidget *dialog, gboolean now)
{
	GtkBuilder *builder;
	GtkWidget *widget;

	g_return_if_fail (dialog != NULL);
	builder = g_object_get_data (G_OBJECT (dialog), "builder");
	g_return_if_fail (builder != NULL);

	gtk_widget_show_all (dialog);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progress_hbox"));
	gtk_widget_hide (widget);

	/* Hide inactive entries */

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code2_entry"));
	if (!g_object_get_data (G_OBJECT (widget), "active")) {
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "code2_label"));
		gtk_widget_hide (widget);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code3_entry"));
	if (!g_object_get_data (G_OBJECT (widget), "active")) {
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "code3_label"));
		gtk_widget_hide (widget);
	}

	/* Need to resize the dialog after hiding widgets */
	gtk_window_resize (GTK_WINDOW (dialog), 400, 100);

	/* Show the dialog */
	gtk_widget_realize (dialog);
	if (now)
		gtk_window_present_with_time (GTK_WINDOW (dialog),
			gdk_x11_get_server_time (gtk_widget_get_window (dialog)));
	else
		gtk_window_present (GTK_WINDOW (dialog));
}

static void
mpd_entry_filter (GtkEntry *entry,
                  const char *text,
                  gint length,
                  gint *position,
                  gpointer user_data)
{
	GtkEditable *editable = GTK_EDITABLE (entry);
	int i, count = 0;
	gchar *result = g_malloc0 (length);

	/* Digits only */
	for (i = 0; i < length; i++) {
		if (isdigit (text[i]))
			result[count++] = text[i];
	}

	if (count > 0) {
		g_signal_handlers_block_by_func (G_OBJECT (editable),
		                                 G_CALLBACK (mpd_entry_filter),
		                                 user_data);
		gtk_editable_insert_text (editable, result, count, position);
		g_signal_handlers_unblock_by_func (G_OBJECT (editable),
		                                   G_CALLBACK (mpd_entry_filter),
		                                   user_data);
	}
	g_signal_stop_emission_by_name (G_OBJECT (editable), "insert-text");
	g_free (result);
}

static void
mpd_set_entry (GtkWidget *dialog,
               const char *entry_name,
               const char *label_name,
               const char *label,
               guint32 minlen,
               guint32 maxlen)
{
	GtkBuilder *builder;
	GtkWidget *widget;
	gboolean entry2_active = FALSE;
	gboolean entry3_active = FALSE;

	g_return_if_fail (dialog != NULL);
	builder = g_object_get_data (G_OBJECT (dialog), "builder");
	g_return_if_fail (builder != NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, label_name));
	gtk_label_set_text (GTK_LABEL (widget), label);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, entry_name));
	g_signal_connect (widget, "changed", G_CALLBACK (mpd_entry_changed), dialog);
	g_signal_connect (widget, "insert-text", G_CALLBACK (mpd_entry_filter), NULL);

	if (maxlen)
		gtk_entry_set_max_length (GTK_ENTRY (widget), maxlen);
	g_object_set_data (G_OBJECT (widget), "minlen", GUINT_TO_POINTER (minlen));

	/* Tag it so we know it's active */
	g_object_set_data (G_OBJECT (widget), "active", GUINT_TO_POINTER (1));

	/* Make a single-entry dialog look better */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code2_entry"));
	entry2_active = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "active"));
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code3_entry"));
	entry3_active = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "active"));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "table14"));
	if (entry2_active || entry3_active)
		gtk_table_set_row_spacings (GTK_TABLE (widget), 6);
	else
		gtk_table_set_row_spacings (GTK_TABLE (widget), 0);

	mpd_entry_changed (NULL, dialog);
}

void
applet_mobile_pin_dialog_set_entry1 (GtkWidget *dialog,
                                     const char *label,
                                     guint32 minlen,
                                     guint32 maxlen)
{
	mpd_set_entry (dialog, "code1_entry", "code1_label", label, minlen, maxlen);
}

void
applet_mobile_pin_dialog_set_entry2 (GtkWidget *dialog,
                                     const char *label,
                                     guint32 minlen,
                                     guint32 maxlen)
{
	mpd_set_entry (dialog, "code2_entry", "code2_label", label, minlen, maxlen);
}

void
applet_mobile_pin_dialog_set_entry3 (GtkWidget *dialog,
                                     const char *label,
                                     guint32 minlen,
                                     guint32 maxlen)
{
	mpd_set_entry (dialog, "code3_entry", "code3_label", label, minlen, maxlen);
}

void applet_mobile_pin_dialog_match_23 (GtkWidget *dialog, gboolean match)
{
	g_return_if_fail (dialog != NULL);

	g_object_set_data (G_OBJECT (dialog), "match23", GUINT_TO_POINTER (match));
}

static const char *
mpd_get_entry (GtkWidget *dialog, const char *entry_name)
{
	GtkBuilder *builder;
	GtkWidget *widget;

	g_return_val_if_fail (dialog != NULL, NULL);
	builder = g_object_get_data (G_OBJECT (dialog), "builder");
	g_return_val_if_fail (builder != NULL, NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, entry_name));
	return gtk_entry_get_text (GTK_ENTRY (widget));
}

const char *
applet_mobile_pin_dialog_get_entry1 (GtkWidget *dialog)
{
	return mpd_get_entry (dialog, "code1_entry");
}

const char *
applet_mobile_pin_dialog_get_entry2 (GtkWidget *dialog)
{
	return mpd_get_entry (dialog, "code2_entry");
}

const char *
applet_mobile_pin_dialog_get_entry3 (GtkWidget *dialog)
{
	return mpd_get_entry (dialog, "code3_entry");
}

void
applet_mobile_pin_dialog_start_spinner (GtkWidget *dialog, const char *text)
{
	GtkBuilder *builder;
	GtkWidget *spinner, *widget, *hbox, *align;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (text != NULL);

	builder = g_object_get_data (G_OBJECT (dialog), "builder");
	g_return_if_fail (builder != NULL);

	spinner = nma_bling_spinner_new ();
	g_return_if_fail (spinner != NULL);
	g_object_set_data (G_OBJECT (dialog), "spinner", spinner);

	align = GTK_WIDGET (gtk_builder_get_object (builder, "spinner_alignment"));
	gtk_container_add (GTK_CONTAINER (align), spinner);
	nma_bling_spinner_start (NMA_BLING_SPINNER (spinner));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progress_label"));
	gtk_label_set_text (GTK_LABEL (widget), text);
	gtk_widget_show (widget);

	hbox = GTK_WIDGET (gtk_builder_get_object (builder, "progress_hbox"));
	gtk_widget_show_all (hbox);

	/* Desensitize everything while spinning */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code1_entry"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code2_entry"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code3_entry"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "unlock_button"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "unlock_cancel_button"));
	gtk_widget_set_sensitive (widget, FALSE);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "show_password_checkbutton"));
	gtk_widget_set_sensitive (widget, FALSE);
}

void
applet_mobile_pin_dialog_stop_spinner (GtkWidget *dialog, const char *text)
{
	GtkBuilder *builder;
	GtkWidget *spinner, *widget, *align;

	g_return_if_fail (dialog != NULL);

	builder = g_object_get_data (G_OBJECT (dialog), "builder");
	g_return_if_fail (builder != NULL);

	spinner = g_object_get_data (G_OBJECT (dialog), "spinner");
	g_return_if_fail (spinner != NULL);
	nma_bling_spinner_stop (NMA_BLING_SPINNER (spinner));
	g_object_set_data (G_OBJECT (dialog), "spinner", NULL);

	/* Remove it from the alignment */
	align = GTK_WIDGET (gtk_builder_get_object (builder, "spinner_alignment"));
	gtk_container_remove (GTK_CONTAINER (align), spinner);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progress_label"));
	if (text) {
		gtk_label_set_text (GTK_LABEL (widget), text);
		gtk_widget_show (widget);
	} else
		gtk_widget_hide (widget);

	/* Resensitize stuff */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code1_entry"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code2_entry"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "code3_entry"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "unlock_button"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "unlock_cancel_button"));
	gtk_widget_set_sensitive (widget, TRUE);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "show_password_checkbutton"));
	gtk_widget_set_sensitive (widget, TRUE);
}

