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

#include <ctype.h>
#include <string.h>
#include <nm-setting-8021x.h>

#include "eap-method.h"
#include "wireless-security.h"
#include "gconf-helpers.h"
#include "helpers.h"

static void
show_toggled_cb (GtkCheckButton *button, EAPMethod *method)
{
	GtkWidget *widget;
	gboolean visible;

	widget = GTK_WIDGET (gtk_builder_get_object (method->builder, "eap_leap_password_entry"));
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static void
destroy (EAPMethod *parent)
{
	EAPMethodLEAP *method = (EAPMethodLEAP *) parent;

	g_slice_free (EAPMethodLEAP, method);
}

static gboolean
validate (EAPMethod *parent)
{
	GtkWidget *widget;
	const char *text;

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_leap_username_entry"));
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!text || !strlen (text))
		return FALSE;

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_leap_password_entry"));
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

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_leap_username_label"));
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_leap_password_label"));
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);
}

static void
fill_connection (EAPMethod *parent, NMConnection *connection)
{
	NMSetting8021x *s_8021x;
	GtkWidget *widget;

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	g_assert (s_8021x);

	nm_setting_802_1x_add_eap_method (s_8021x, "leap");

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_leap_username_entry"));
	g_assert (widget);
	g_object_set (s_8021x, NM_SETTING_802_1X_IDENTITY, gtk_entry_get_text (GTK_ENTRY (widget)), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_leap_password_entry"));
	g_assert (widget);
	g_object_set (s_8021x, NM_SETTING_802_1X_PASSWORD, gtk_entry_get_text (GTK_ENTRY (widget)), NULL);
}

static void
update_secrets (EAPMethod *parent, NMConnection *connection)
{
	helper_fill_secret_entry (connection,
	                          parent->builder,
	                          "eap_leap_password_entry",
	                          NM_TYPE_SETTING_802_1X,
	                          (HelperSecretFunc) nm_setting_802_1x_get_password);
}

EAPMethodLEAP *
eap_method_leap_new (const char *ui_file,
                     WirelessSecurity *parent,
                     NMConnection *connection)
{
	EAPMethodLEAP *method;
	GtkWidget *widget;
	GtkBuilder *builder;
	GError *error = NULL;

	g_return_val_if_fail (ui_file != NULL, NULL);

	builder = gtk_builder_new ();

	if (!gtk_builder_add_from_file (builder, ui_file, &error))
	{
		g_warning ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "eap_leap_notebook"));
	g_assert (widget);
	g_object_ref_sink (widget);

	method = g_slice_new0 (EAPMethodLEAP);
	if (!method) {
		g_object_unref (builder);
		g_object_unref (widget);
		return NULL;
	}

	eap_method_init (EAP_METHOD (method),
	                 validate,
	                 add_to_size_group,
	                 fill_connection,
	                 update_secrets,
	                 destroy,
	                 builder,
	                 widget,
	                 "eap_leap_username_entry");

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "eap_leap_username_entry"));
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

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "eap_leap_password_entry"));
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	/* Fill secrets, if any */
	if (connection)
		update_secrets (EAP_METHOD (method), connection);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "show_checkbutton_eapleap"));
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  method);

	return method;
}

