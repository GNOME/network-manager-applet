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
#include <string.h>
#include <nm-setting-wireless.h>

#include "wireless-security.h"
#include "utils.h"
#include "gconf-helpers.h"


static void
show_toggled_cb (GtkCheckButton *button, WirelessSecurity *sec)
{
	GtkWidget *widget;
	gboolean visible;

	widget = glade_xml_get_widget (sec->xml, "leap_password_entry");
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static void
destroy (WirelessSecurity *parent)
{
	WirelessSecurityLEAP *sec = (WirelessSecurityLEAP *) parent;

	g_slice_free (WirelessSecurityLEAP, sec);
}

static gboolean
validate (WirelessSecurity *parent, const GByteArray *ssid)
{
	GtkWidget *entry;
	const char *text;

	entry = glade_xml_get_widget (parent->xml, "leap_username_entry");
	g_assert (entry);
	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (!text || !strlen (text))
		return FALSE;

	entry = glade_xml_get_widget (parent->xml, "leap_password_entry");
	g_assert (entry);
	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (!text || !strlen (text))
		return FALSE;

	return TRUE;
}

static void
add_to_size_group (WirelessSecurity *parent, GtkSizeGroup *group)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "leap_username_label");
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "leap_password_label");
	gtk_size_group_add_widget (group, widget);
}

static void
fill_connection (WirelessSecurity *parent, NMConnection *connection)
{
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	GtkWidget *widget;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_assert (s_wireless);

	if (s_wireless->security)
		g_free (s_wireless->security);
	s_wireless->security = g_strdup (NM_SETTING_WIRELESS_SECURITY_SETTING_NAME);

	/* Blow away the old security setting by adding a clear one */
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);

	s_wireless_sec->key_mgmt = g_strdup ("ieee8021x");
	s_wireless_sec->auth_alg = g_strdup ("leap");

	widget = glade_xml_get_widget (parent->xml, "leap_username_entry");
	s_wireless_sec->leap_username = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));

	widget = glade_xml_get_widget (parent->xml, "leap_password_entry");
	s_wireless_sec->leap_password = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));
}

WirelessSecurityLEAP *
ws_leap_new (const char *glade_file, NMConnection *connection, const char *connection_id)
{
	WirelessSecurityLEAP *sec;
	GtkWidget *widget;
	GladeXML *xml;
	NMSettingWirelessSecurity *wsec = NULL;

	g_return_val_if_fail (glade_file != NULL, NULL);
	if (connection)
		g_return_val_if_fail (connection_id != NULL, NULL);

	xml = glade_xml_new (glade_file, "leap_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get leap_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "leap_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	sec = g_slice_new0 (WirelessSecurityLEAP);
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
	                        widget);

	if (connection) {
		wsec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY));
		if (wsec) {
			/* Ignore if wireless security doesn't specify LEAP */
			if (!wsec->auth_alg || strcmp (wsec->auth_alg, "leap"))
				wsec = NULL;
		}
	}

	widget = glade_xml_get_widget (xml, "leap_password_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);
	if (wsec) {
		GHashTable *secrets;
		GError *error = NULL;
		GValue *value;

		secrets = nm_gconf_get_keyring_items (connection, connection_id,
		                                      NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
		                                      FALSE,
		                                      &error);
		if (secrets) {
			value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_LEAP_PASSWORD);
			if (value)
				gtk_entry_set_text (GTK_ENTRY (widget), g_value_get_string (value));
			g_hash_table_destroy (secrets);
		}
	}

	widget = glade_xml_get_widget (xml, "leap_username_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);
	if (wsec)
		gtk_entry_set_text (GTK_ENTRY (widget), wsec->leap_username);

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  sec);

	return sec;
}

