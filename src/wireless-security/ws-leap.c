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
 * (C) Copyright 2007 - 2009 Red Hat, Inc.
 */

#include <glade/glade.h>
#include <string.h>
#include <nm-setting-wireless.h>

#include "wireless-security.h"
#include "utils.h"
#include "gconf-helpers.h"
#include "helpers.h"


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
	const char *leap_password = NULL, *leap_username = NULL;

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_assert (s_wireless);

	g_object_set (s_wireless, NM_SETTING_WIRELESS_SEC, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL);

	/* Blow away the old security setting by adding a clear one */
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);

	widget = glade_xml_get_widget (parent->xml, "leap_username_entry");
	leap_username = gtk_entry_get_text (GTK_ENTRY (widget));

	widget = glade_xml_get_widget (parent->xml, "leap_password_entry");
	leap_password = gtk_entry_get_text (GTK_ENTRY (widget));

	g_object_set (s_wireless_sec,
	              NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x",
	              NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "leap",
	              NM_SETTING_WIRELESS_SECURITY_LEAP_USERNAME, leap_username,
	              NM_SETTING_WIRELESS_SECURITY_LEAP_PASSWORD, leap_password,
	              NULL);
}

static void
update_secrets (WirelessSecurity *parent, NMConnection *connection)
{
	helper_fill_secret_entry (connection,
	                          parent->xml,
	                          "leap_password_entry",
	                          NM_TYPE_SETTING_WIRELESS_SECURITY,
	                          (HelperSecretFunc) nm_setting_wireless_security_get_leap_password);
}

WirelessSecurityLEAP *
ws_leap_new (const char *glade_file, NMConnection *connection)
{
	WirelessSecurityLEAP *sec;
	GtkWidget *widget;
	GladeXML *xml;
	NMSettingWirelessSecurity *wsec = NULL;

	g_return_val_if_fail (glade_file != NULL, NULL);

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
	                        update_secrets,
	                        destroy,
	                        xml,
	                        widget,
	                        "leap_username_entry");

	if (connection) {
		wsec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY));
		if (wsec) {
			const char *auth_alg;

			/* Ignore if wireless security doesn't specify LEAP */
			auth_alg = nm_setting_wireless_security_get_auth_alg (wsec);
			if (!auth_alg || strcmp (auth_alg, "leap"))
				wsec = NULL;
		}
	}

	widget = glade_xml_get_widget (xml, "leap_password_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);
	if (wsec)
		update_secrets (WIRELESS_SECURITY (sec), connection);

	widget = glade_xml_get_widget (xml, "leap_username_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);
	if (wsec)
		gtk_entry_set_text (GTK_ENTRY (widget), nm_setting_wireless_security_get_leap_username (wsec));

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  sec);

	return sec;
}

