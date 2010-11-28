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

#include <glib/gi18n.h>
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

	g_object_set (s_wireless_sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "ieee8021x", NULL);

	nm_setting_wireless_security_add_pairwise (s_wireless_sec, "wep40");
	nm_setting_wireless_security_add_pairwise (s_wireless_sec, "wep104");
	nm_setting_wireless_security_add_group (s_wireless_sec, "wep40");
	nm_setting_wireless_security_add_group (s_wireless_sec, "wep104");
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

static void
update_secrets (WirelessSecurity *parent, NMConnection *connection)
{
	ws_802_1x_update_secrets (parent, "dynamic_wep_auth_combo", connection);
}

WirelessSecurityDynamicWEP *
ws_dynamic_wep_new (const char *ui_file,
                    NMConnection *connection,
                    gboolean is_editor)
{
	WirelessSecurityDynamicWEP *sec;
	GtkWidget *widget;
	GtkBuilder *builder;
	GError *error = NULL;

	g_return_val_if_fail (ui_file != NULL, NULL);

	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file (builder, ui_file, &error))
	{
		g_warning ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dynamic_wep_notebook"));
	g_assert (widget);
	g_object_ref_sink (widget);

	sec = g_slice_new0 (WirelessSecurityDynamicWEP);
	if (!sec) {
		g_object_unref (builder);
		g_object_unref (widget);
		return NULL;
	}

	wireless_security_init (WIRELESS_SECURITY (sec),
	                        validate,
	                        add_to_size_group,
	                        fill_connection,
	                        update_secrets,
	                        destroy,
	                        builder,
	                        widget,
	                        NULL);

	WIRELESS_SECURITY (sec)->nag_user = nag_user;

	widget = ws_802_1x_auth_combo_init (WIRELESS_SECURITY (sec),
	                                    ui_file,
	                                    "dynamic_wep_auth_combo",
	                                    (GCallback) auth_combo_changed_cb,
	                                    connection,
	                                    is_editor);
	auth_combo_changed_cb (widget, (gpointer) sec);

	return sec;
}

