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
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <glade/glade.h>
#include <ctype.h>
#include <string.h>
#include <nm-setting-wireless.h>

#include "wireless-security.h"
#include "utils.h"
#include "gconf-helpers.h"
#include "helpers.h"

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

	g_slice_free (WirelessSecurityWPAPSK, sec);
}

static gboolean
validate (WirelessSecurity *parent, const GByteArray *ssid)
{
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
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "wpa_psk_type_label");
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "wpa_psk_label");
	gtk_size_group_add_widget (group, widget);
}

static void
fill_connection (WirelessSecurity *parent, NMConnection *connection)
{
	GtkWidget *widget;
	const char *key;
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	const char *mode;
	gboolean is_adhoc = FALSE;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_assert (s_wireless);

	mode = nm_setting_wireless_get_mode (s_wireless);
	if (mode && !strcmp (mode, "adhoc"))
		is_adhoc = TRUE;

	g_object_set (s_wireless, NM_SETTING_WIRELESS_SEC, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL);

	/* Blow away the old security setting by adding a clear one */
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);

	widget = glade_xml_get_widget (parent->xml, "wpa_psk_entry");
	key = gtk_entry_get_text (GTK_ENTRY (widget));
	g_object_set (s_wireless_sec, NM_SETTING_WIRELESS_SECURITY_PSK, key, NULL);

	wireless_security_clear_ciphers (connection);
	if (is_adhoc) {
		/* Ad-Hoc settings as specified by the supplicant */
		g_object_set (s_wireless_sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-none", NULL);
		nm_setting_wireless_security_add_proto (s_wireless_sec, "wpa");
		nm_setting_wireless_security_add_pairwise (s_wireless_sec, "none");

		/* Ad-hoc can only have _one_ group cipher... default to TKIP to be more
		 * compatible for now.  Maybe we'll support selecting CCMP later.
		 */
		nm_setting_wireless_security_add_group (s_wireless_sec, "tkip");
	} else {
		g_object_set (s_wireless_sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk", NULL);

		/* Just leave ciphers and protocol empty, the supplicant will
		 * figure that out magically based on the AP IEs and card capabilities.
		 */
	}
}

WirelessSecurityWPAPSK *
ws_wpa_psk_new (const char *glade_file, NMConnection *connection)
{
	WirelessSecurityWPAPSK *sec;
	GtkWidget *widget;
	GladeXML *xml;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "wpa_psk_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get wpa_psk_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "wpa_psk_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	sec = g_slice_new0 (WirelessSecurityWPAPSK);
	if (!sec) {
		g_object_unref (xml);
		g_object_unref (widget);
		return NULL;
	}

	wireless_security_init (WIRELESS_SECURITY (sec),
	                        validate,
	                        add_to_size_group,
	                        fill_connection,
	                        destroy,
	                        xml,
	                        widget,
	                        "wpa_psk_entry");

	widget = glade_xml_get_widget (xml, "wpa_psk_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);

	/* Fill secrets, if any */
	if (connection) {
		helper_fill_secret_entry (connection,
		                          GTK_ENTRY (widget),
		                          NM_TYPE_SETTING_WIRELESS_SECURITY,
		                          (HelperSecretFunc) nm_setting_wireless_security_get_psk,
		                          NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
		                          NM_SETTING_WIRELESS_SECURITY_PSK);
	}

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  sec);

	widget = glade_xml_get_widget (xml, "wpa_psk_type_combo");
	g_assert (widget);
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (xml, "wpa_psk_type_label");
	g_assert (widget);
	gtk_widget_hide (widget);

	return sec;
}

