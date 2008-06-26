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

#include <glib/gi18n.h>
#include <glade/glade.h>
#include <ctype.h>
#include <string.h>
#include <nm-setting-wireless.h>

#include "wireless-security.h"
#include "eap-method.h"

static void
destroy (WirelessSecurity *parent)
{
	WirelessSecurityDynamicWEP *sec = (WirelessSecurityDynamicWEP *) parent;

	if (sec->size_group)
		g_object_unref (sec->size_group);
	g_slice_free (WirelessSecurityDynamicWEP, sec);
}

static gboolean
validate (WirelessSecurity *parent, const GByteArray *ssid)
{
	return ws_802_1x_validate (parent, "dynamic_wep_auth_combo");
}

static void
add_to_size_group (WirelessSecurity *parent, GtkSizeGroup *group)
{
	WirelessSecurityDynamicWEP *sec = (WirelessSecurityDynamicWEP *) parent;

	if (sec->size_group)
		g_object_unref (sec->size_group);
	sec->size_group = g_object_ref (group);

	ws_802_1x_add_to_size_group (parent,
	                             sec->size_group,
	                             "dynamic_wep_auth_label",
	                             "dynamic_wep_auth_combo");
}

static void
fill_connection (WirelessSecurity *parent, NMConnection *connection)
{
	NMSettingWirelessSecurity *s_wireless_sec;

	ws_802_1x_fill_connection (parent, "dynamic_wep_auth_combo", connection);

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, 
										  NM_TYPE_SETTING_WIRELESS_SECURITY));
	g_assert (s_wireless_sec);

	s_wireless_sec->key_mgmt = g_strdup ("ieee8021x");

	s_wireless_sec->pairwise = g_slist_append (s_wireless_sec->pairwise, g_strdup ("wep40"));
	s_wireless_sec->pairwise = g_slist_append (s_wireless_sec->pairwise, g_strdup ("wep104"));
	s_wireless_sec->group = g_slist_append (s_wireless_sec->group, g_strdup ("wep40"));
	s_wireless_sec->group = g_slist_append (s_wireless_sec->group, g_strdup ("wep104"));
}

static void
auth_combo_changed_cb (GtkWidget *combo, gpointer user_data)
{
	WirelessSecurity *parent = WIRELESS_SECURITY (user_data);
	WirelessSecurityDynamicWEP *sec = (WirelessSecurityDynamicWEP *) parent;

	ws_802_1x_auth_combo_changed (combo,
	                              parent,
	                              "dynamic_wep_method_vbox",
	                              sec->size_group);
}

static GtkWidget *
nag_user (WirelessSecurity *parent)
{
	return ws_802_1x_nag_user (parent, "dynamic_wep_auth_combo");
}

WirelessSecurityDynamicWEP *
ws_dynamic_wep_new (const char *glade_file,
                    NMConnection *connection,
                    const char *connection_id)
{
	WirelessSecurityDynamicWEP *sec;
	GtkWidget *widget;
	GladeXML *xml;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "dynamic_wep_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get dynamic_wep_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "dynamic_wep_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	sec = g_slice_new0 (WirelessSecurityDynamicWEP);
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

	WIRELESS_SECURITY (sec)->nag_user = nag_user;

	widget = ws_802_1x_auth_combo_init (WIRELESS_SECURITY (sec),
	                                    glade_file,
	                                    "dynamic_wep_auth_combo",
	                                    (GCallback) auth_combo_changed_cb,
	                                    connection,
	                                    connection_id);
	auth_combo_changed_cb (widget, (gpointer) sec);

	return sec;
}

