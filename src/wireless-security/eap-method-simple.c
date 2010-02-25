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
#include <ctype.h>
#include <string.h>
#include <nm-setting-8021x.h>
#include <nm-setting-connection.h>

#include "eap-method.h"
#include "wireless-security.h"
#include "gconf-helpers.h"
#include "helpers.h"

static void
show_toggled_cb (GtkCheckButton *button, EAPMethod *method)
{
	GtkWidget *widget;
	gboolean visible;

	widget = glade_xml_get_widget (method->xml, "eap_simple_password_entry");
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static void
destroy (EAPMethod *parent)
{
	EAPMethodSimple *method = (EAPMethodSimple *) parent;

	g_slice_free (EAPMethodSimple, method);
}

static gboolean
validate (EAPMethod *parent)
{
	GtkWidget *widget;
	const char *text;

	widget = glade_xml_get_widget (parent->xml, "eap_simple_username_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!text || !strlen (text))
		return FALSE;

	/* Check if the password should always be requested */
	widget = glade_xml_get_widget (parent->xml, "eap_password_always_ask");
	g_assert (widget);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		return TRUE;

	widget = glade_xml_get_widget (parent->xml, "eap_simple_password_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!text || !strlen (text))
		return FALSE;

	return TRUE;
}

static void
add_to_size_group (EAPMethod *parent, GtkSizeGroup *group)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "eap_simple_username_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_simple_password_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);
}

static void
fill_connection (EAPMethod *parent, NMConnection *connection)
{
	EAPMethodSimple *method = (EAPMethodSimple *) parent;
	NMSetting8021x *s_8021x;
	GtkWidget *widget;
	NMSettingConnection *s_con;
	gboolean always_ask;

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	g_assert (s_8021x);

	switch (method->type) {
		case EAP_METHOD_SIMPLE_TYPE_PAP:
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "pap", NULL);
			break;
		case EAP_METHOD_SIMPLE_TYPE_MSCHAP:
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "mschap", NULL);
			break;
		case EAP_METHOD_SIMPLE_TYPE_MSCHAP_V2:
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "mschapv2", NULL);
			break;
		case EAP_METHOD_SIMPLE_TYPE_MD5:
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "md5", NULL);
			break;
		case EAP_METHOD_SIMPLE_TYPE_CHAP:
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "chap", NULL);
			break;
		case EAP_METHOD_SIMPLE_TYPE_GTC:
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "gtc", NULL);
			break;
		default:
			g_assert_not_reached ();
			break;
	}

	widget = glade_xml_get_widget (parent->xml, "eap_simple_username_entry");
	g_assert (widget);
	g_object_set (s_8021x, NM_SETTING_802_1X_IDENTITY, gtk_entry_get_text (GTK_ENTRY (widget)), NULL);

	widget = glade_xml_get_widget (parent->xml, "eap_password_always_ask");
	g_assert (widget);
	always_ask = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	if (always_ask) {
		widget = glade_xml_get_widget (parent->xml, "eap_simple_password_entry");
		g_assert (widget);
		g_object_set (s_8021x, NM_SETTING_802_1X_PASSWORD, gtk_entry_get_text (GTK_ENTRY (widget)), NULL);
	}

	/* Save the password always ask setting */
	s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);
	nm_gconf_set_8021x_password_always_ask (nm_setting_connection_get_uuid (s_con), always_ask);
}

static void
update_secrets (EAPMethod *parent, NMConnection *connection)
{
	helper_fill_secret_entry (connection,
	                          parent->xml,
	                          "eap_simple_password_entry",
	                          NM_TYPE_SETTING_802_1X,
	                          (HelperSecretFunc) nm_setting_802_1x_get_password);
}

static void
password_always_ask_changed (GtkButton *button, EAPMethodSimple *method)
{
	EAPMethod *parent = (EAPMethod *) method;
	GtkWidget *password_entry;
	GtkWidget *show_checkbox;
	gboolean always_ask;

	always_ask = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

	password_entry = glade_xml_get_widget (parent->xml, "eap_simple_password_entry");
	g_assert (password_entry);

	show_checkbox = glade_xml_get_widget (parent->xml, "show_checkbutton");
	g_assert (show_checkbox);

	if (always_ask) {
		gtk_entry_set_text (GTK_ENTRY (password_entry), "");
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_checkbox), FALSE);
	}

	gtk_widget_set_sensitive (password_entry, !always_ask);
	gtk_widget_set_sensitive (show_checkbox, !always_ask);
}


EAPMethodSimple *
eap_method_simple_new (const char *glade_file,
                       WirelessSecurity *parent,
                       NMConnection *connection,
                       EAPMethodSimpleType type)
{
	EAPMethodSimple *method;
	GtkWidget *widget;
	GladeXML *xml;
	gboolean always_ask = FALSE;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "eap_simple_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get eap_simple_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "eap_simple_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	method = g_slice_new0 (EAPMethodSimple);
	if (!method) {
		g_object_unref (xml);
		g_object_unref (widget);
		return NULL;
	}

	eap_method_init (EAP_METHOD (method),
	                 validate,
	                 add_to_size_group,
	                 fill_connection,
	                 update_secrets,
	                 destroy,
	                 xml,
	                 widget,
	                 "eap_simple_username_entry");

	method->type = type;

	widget = glade_xml_get_widget (xml, "eap_simple_username_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
	if (connection) {
		NMSetting8021x *s_8021x;

		s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
		if (s_8021x && nm_setting_802_1x_get_identity (s_8021x))
			gtk_entry_set_text (GTK_ENTRY (widget), nm_setting_802_1x_get_identity (s_8021x));
	}

	widget = glade_xml_get_widget (xml, "eap_simple_password_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	/* Fill secrets, if any */
	if (connection)
		update_secrets (EAP_METHOD (method), connection);

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  method);

	/* Sensitize/desensitize the password entry based on whether it should
	 * always be asked for when connecting or not.
	 */
	widget = glade_xml_get_widget (xml, "eap_password_always_ask");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  G_CALLBACK (password_always_ask_changed),
	                  method);

	if (connection) {
		NMSettingConnection *s_con;
		const char *uuid;

		s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
		g_assert (s_con);

		uuid = nm_setting_connection_get_uuid (s_con);
		always_ask = nm_gconf_get_8021x_password_always_ask (uuid);
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), always_ask);

	return method;
}

