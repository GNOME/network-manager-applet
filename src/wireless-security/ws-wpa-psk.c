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

#include <glade/glade.h>
#include <ctype.h>
#include <string.h>

#include "wireless-security.h"
#include "utils.h"
#include "sha1.h"

#define WPA_PMK_LEN 32

static void
show_toggled_cb (GtkCheckButton *button, WirelessSecurity *sec)
{
	GtkWidget *widget;
	gboolean visible;

	widget = glade_xml_get_widget (sec->xml, "wpa_psk_entry");
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static void
destroy (WirelessSecurity *parent)
{
	WirelessSecurityWPAPSK *sec = (WirelessSecurityWPAPSK *) parent;

	g_object_unref (parent->xml);
	g_slice_free (WirelessSecurityWPAPSK, sec);
}

static gboolean
validate (WirelessSecurity *parent, const GByteArray *ssid)
{
	WirelessSecurityWPAPSK *sec = (WirelessSecurityWPAPSK *) parent;
	GtkWidget *entry;
	const char *key;
	guint32 len;
	int i;

	entry = glade_xml_get_widget (parent->xml, "wpa_psk_entry");
	g_assert (entry);

	key = gtk_entry_get_text (GTK_ENTRY (entry));
	len = strlen (key);
	if ((len < 8) || (len > 64))
		return FALSE;

	if (len == 64) {
		/* Hex PSK */
		for (i = 0; i < len; i++) {
			if (!isxdigit (key[i]))
				return FALSE;
		}
	}

	/* passphrase can be between 8 and 63 characters inclusive */

	return TRUE;
}

static void
add_to_size_group (WirelessSecurity *parent, GtkSizeGroup *group)
{
	WirelessSecurityWPAPSK *sec = (WirelessSecurityWPAPSK *) parent;
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "wpa_psk_type_label");
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "wpa_psk_label");
	gtk_size_group_add_widget (group, widget);
}

static void
fill_connection (WirelessSecurity *parent, NMConnection *connection)
{
	WirelessSecurityWPAPSK *sec = (WirelessSecurityWPAPSK *) parent;
	GtkWidget *widget;
	const char *key;
	char *hashed = NULL;
	guint32 len;
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	char *proto;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, NM_SETTING_WIRELESS);
	g_assert (s_wireless);
	g_assert (s_wireless->ssid);

	if (s_wireless->security)
		g_free (s_wireless->security);
	s_wireless->security = g_strdup (NM_SETTING_WIRELESS_SECURITY);

	/* Blow away the old security setting by adding a clear one */
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);

	widget = glade_xml_get_widget (parent->xml, "wpa_psk_entry");
	key = gtk_entry_get_text (GTK_ENTRY (widget));

	len = strlen (key);
	if (len == 64) {
		/* Hex key */
		hashed = g_strdup (key);
	} else {
		/* passphrase */
		unsigned char *buf = g_malloc0 (WPA_PMK_LEN * 2);
		pbkdf2_sha1 (key, (char *) s_wireless->ssid->data, s_wireless->ssid->len,
		             4096, buf, WPA_PMK_LEN);
		hashed = utils_bin2hexstr (buf, WPA_PMK_LEN, WPA_PMK_LEN * 2);
		g_free (buf);
	}

	s_wireless_sec->psk = hashed;
	s_wireless_sec->key_mgmt = g_strdup ("wpa-psk");

	// FIXME: allow protocol selection and filter on device capabilities
	// FIXME: allow pairwise cipher selection and filter on device capabilities
	// FIXME: allow group cipher selection and filter on device capabilities
	ws_wpa_fill_default_ciphers (connection);
}

WirelessSecurityWPAPSK *
ws_wpa_psk_new (const char *glade_file)
{
	WirelessSecurityWPAPSK *sec;
	GtkWidget *widget;
	GladeXML *xml;

	g_return_val_if_fail (xml != NULL, NULL);

	xml = glade_xml_new (glade_file, "wpa_psk_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get wpa_psk_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "wpa_psk_notebook");
	g_assert (widget);

	sec = g_slice_new0 (WirelessSecurityWPAPSK);
	if (!sec) {
		g_object_unref (xml);
		return NULL;
	}

	WIRELESS_SECURITY (sec)->validate = validate;
	WIRELESS_SECURITY (sec)->add_to_size_group = add_to_size_group;
	WIRELESS_SECURITY (sec)->fill_connection = fill_connection;
	WIRELESS_SECURITY (sec)->destroy = destroy;

	WIRELESS_SECURITY (sec)->xml = xml;
	WIRELESS_SECURITY (sec)->ui_widget = g_object_ref (widget);

	widget = glade_xml_get_widget (xml, "wpa_psk_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  sec);

	return sec;
}

