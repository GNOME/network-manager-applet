/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * Copyright 2007 - 2014 Red Hat, Inc.
 */

#include <ctype.h>
#include <string.h>

#include "eap-method.h"
#include "wireless-security.h"
#include "helpers.h"
#include "utils.h"

struct _EAPMethodSimple {
	EAPMethod parent;

	WirelessSecurity *ws_parent;

	EAPMethodSimpleType type;
	EAPMethodSimpleFlags flags;

	GtkEntry *username_entry;
	GtkEntry *password_entry;
	GtkToggleButton *always_ask;
	GtkToggleButton *show_password;
};

static void
show_toggled_cb (GtkToggleButton *button, EAPMethodSimple *method)
{
	gboolean visible;

	visible = gtk_toggle_button_get_active (button);
	gtk_entry_set_visibility (method->password_entry, visible);
}

static gboolean
validate (EAPMethod *parent)
{
	EAPMethodSimple *method = (EAPMethodSimple *)parent;
	const char *text;

	text = gtk_entry_get_text (method->username_entry);
	if (!text || !strlen (text))
		return FALSE;

	/* Check if the password should always be requested */
	if (gtk_toggle_button_get_active (method->always_ask))
		return TRUE;

	text = gtk_entry_get_text (method->password_entry);
	if (!text || !strlen (text))
		return FALSE;

	return TRUE;
}

static void
add_to_size_group (EAPMethod *parent, GtkSizeGroup *group)
{
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_simple_username_label"));
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_simple_password_label"));
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);
}

typedef struct {
	const char *name;
	gboolean autheap_allowed;
} EapType;

/* Indexed by EAP_METHOD_SIMPLE_TYPE_* */
static const EapType eap_table[EAP_METHOD_SIMPLE_TYPE_LAST] = {
	[EAP_METHOD_SIMPLE_TYPE_PAP]       = { "pap",      FALSE },
	[EAP_METHOD_SIMPLE_TYPE_MSCHAP]    = { "mschap",   FALSE },
	[EAP_METHOD_SIMPLE_TYPE_MSCHAP_V2] = { "mschapv2", TRUE  },
	[EAP_METHOD_SIMPLE_TYPE_MD5]       = { "md5",      TRUE  },
	[EAP_METHOD_SIMPLE_TYPE_PWD]       = { "pwd",      TRUE  },
	[EAP_METHOD_SIMPLE_TYPE_CHAP]      = { "chap",     FALSE },
	[EAP_METHOD_SIMPLE_TYPE_GTC]       = { "gtc",      TRUE  },
};

static void
fill_connection (EAPMethod *parent, NMConnection *connection, NMSettingSecretFlags prev_flags)
{
	EAPMethodSimple *method = (EAPMethodSimple *) parent;
	NMSetting8021x *s_8021x;
	gboolean not_saved = FALSE;
	NMSettingSecretFlags flags = prev_flags;
	const EapType *eap_type;

	s_8021x = nm_connection_get_setting_802_1x (connection);
	g_assert (s_8021x);

	/* If this is the main EAP method, clear any existing methods because the
	 * user-selected on will replace it.
	 */
	if (parent->phase2 == FALSE)
		nm_setting_802_1x_clear_eap_methods (s_8021x);

	eap_type = &eap_table[method->type];
	if (parent->phase2) {
		/* If the outer EAP method (TLS, TTLS, PEAP, etc) allows inner/phase2
		 * EAP methods (which only TTLS allows) *and* the inner/phase2 method
		 * supports being an inner EAP method, then set PHASE2_AUTHEAP.
		 * Otherwise the inner/phase2 method goes into PHASE2_AUTH.
		 */
		if ((method->flags & EAP_METHOD_SIMPLE_FLAG_AUTHEAP_ALLOWED) && eap_type->autheap_allowed) {
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTHEAP, eap_type->name, NULL);
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, NULL, NULL);
		} else {
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, eap_type->name, NULL);
			g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTHEAP, NULL, NULL);
		}
	} else
		nm_setting_802_1x_add_eap_method (s_8021x, eap_type->name);

	g_object_set (s_8021x, NM_SETTING_802_1X_IDENTITY, gtk_entry_get_text (method->username_entry), NULL);

	/* Save the password always ask setting */
	not_saved = gtk_toggle_button_get_active (method->always_ask);

	flags &= ~(NM_SETTING_SECRET_FLAG_NOT_SAVED);
	if (not_saved)
		flags |= NM_SETTING_SECRET_FLAG_NOT_SAVED;
	nm_setting_set_secret_flags (NM_SETTING (s_8021x), NM_SETTING_802_1X_PASSWORD, flags, NULL);

	/* Fill the connection's password if we're in the applet so that it'll get
	 * back to NM.  From the editor though, since the connection isn't going
	 * back to NM in response to a GetSecrets() call, we don't save it if the
	 * user checked "Always Ask".
	 */
	if (!(method->flags & EAP_METHOD_SIMPLE_FLAG_IS_EDITOR) || not_saved == FALSE)
		g_object_set (s_8021x, NM_SETTING_802_1X_PASSWORD, gtk_entry_get_text (method->password_entry), NULL);

	/* Update secret flags and popup when editing the connection */
	if (!(method->flags & EAP_METHOD_SIMPLE_FLAG_SECRETS_ONLY)) {
		GtkWidget *passwd_entry = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_simple_password_entry"));
		g_assert (passwd_entry);

		utils_update_password_storage (NM_SETTING (s_8021x), flags, passwd_entry, parent->password_flags_name);
	}
}

static void
update_secrets (EAPMethod *parent, NMConnection *connection)
{
	helper_fill_secret_entry (connection,
	                          parent->builder,
	                          "eap_simple_password_entry",
	                          NM_TYPE_SETTING_802_1X,
	                          (HelperSecretFunc) nm_setting_802_1x_get_password);
}

static void
g_free_str0 (gpointer mem)
{
	/* g_free a char pointer and set it to 0 before (for passwords). */
	if (mem) {
		char *p = mem;
		memset (p, 0, strlen (p));
		g_free (p);
	}
}

static void
password_always_ask_changed (GtkToggleButton *button, EAPMethodSimple *method)
{
	char *password;
	gboolean always_ask;

	always_ask = gtk_toggle_button_get_active (button);

	if (always_ask) {
		password = g_strdup (gtk_entry_get_text (method->password_entry));
		gtk_entry_set_text (method->password_entry, "");
		g_object_set_data_full (G_OBJECT (method->password_entry), "password-old", password, g_free_str0);
	} else {
		password = g_object_get_data (G_OBJECT (method->password_entry), "password-old");
		gtk_entry_set_text (method->password_entry, password ? password : "");
		g_object_set_data(G_OBJECT (method->password_entry), "password-old", NULL);
	}

	if (always_ask) {
		/* we always clear this button and do not restore it
		 * (because we want to hide the password). */
		gtk_toggle_button_set_active (method->show_password, FALSE);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (method->password_entry), !always_ask);
	gtk_widget_set_sensitive (GTK_WIDGET (method->show_password), !always_ask);
}

/* Set the UI fields for user, password, always_ask and show_password to the
 * values as provided by method->ws_parent. */
static void
set_userpass_ui (EAPMethodSimple *method)
{
	if (method->ws_parent->username)
		gtk_entry_set_text (method->username_entry, method->ws_parent->username);
	else
		gtk_entry_set_text (method->username_entry, "");

	if (method->ws_parent->password && !method->ws_parent->always_ask)
		gtk_entry_set_text (method->password_entry, method->ws_parent->password);
	else
		gtk_entry_set_text (method->password_entry, "");

	gtk_toggle_button_set_active (method->always_ask, method->ws_parent->always_ask);
	gtk_toggle_button_set_active (method->show_password, method->ws_parent->show_password);
}

static void
widgets_realized (GtkWidget *widget, EAPMethodSimple *method)
{
	set_userpass_ui (method);
}

static void
widgets_unrealized (GtkWidget *widget, EAPMethodSimple *method)
{
	wireless_security_set_userpass (method->ws_parent,
	                                gtk_entry_get_text (method->username_entry),
	                                gtk_entry_get_text (method->password_entry),
	                                gtk_toggle_button_get_active (method->always_ask),
	                                gtk_toggle_button_get_active (method->show_password));

	/* clear the remembered password. If the user changes the scheme, it's gone. */
	g_object_set_data(G_OBJECT (method->password_entry), "password-old", NULL);
}

static void
destroy (EAPMethod *parent)
{
	EAPMethodSimple *method = (EAPMethodSimple *) parent;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_simple_notebook"));
	g_assert (widget);

	g_signal_handlers_disconnect_by_func (G_OBJECT (widget),
	                                      (GCallback) widgets_realized,
	                                      method);
	g_signal_handlers_disconnect_by_func (G_OBJECT (widget),
	                                      (GCallback) widgets_unrealized,
	                                      method);

	wireless_security_unref (method->ws_parent);
}

EAPMethodSimple *
eap_method_simple_new (WirelessSecurity *ws_parent,
                       NMConnection *connection,
                       EAPMethodSimpleType type,
                       EAPMethodSimpleFlags flags)
{
	EAPMethod *parent;
	EAPMethodSimple *method;
	GtkWidget *widget;

	parent = eap_method_init (sizeof (EAPMethodSimple),
	                          validate,
	                          add_to_size_group,
	                          fill_connection,
	                          update_secrets,
	                          destroy,
	                          UIDIR "/eap-method-simple.ui",
	                          "eap_simple_notebook",
	                          "eap_simple_username_entry",
	                          flags & EAP_METHOD_SIMPLE_FLAG_PHASE2);
	if (!parent)
		return NULL;

	parent->password_flags_name = NM_SETTING_802_1X_PASSWORD;
	method = (EAPMethodSimple *) parent;
	method->ws_parent = wireless_security_ref (ws_parent);
	method->flags = flags;
	method->type = type;
	g_assert (type < EAP_METHOD_SIMPLE_TYPE_LAST);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_simple_notebook"));
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "realize",
	                  (GCallback) widgets_realized,
	                  method);
	g_signal_connect (G_OBJECT (widget), "unrealize",
	                  (GCallback) widgets_unrealized,
	                  method);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_simple_username_entry"));
	g_assert (widget);
	method->username_entry = GTK_ENTRY (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  ws_parent);

	if (method->flags & EAP_METHOD_SIMPLE_FLAG_SECRETS_ONLY)
		gtk_widget_set_sensitive (widget, FALSE);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_simple_password_entry"));
	g_assert (widget);
	method->password_entry = GTK_ENTRY (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  ws_parent);

	/* Create password-storage popup menu for password entry under entry's secondary icon */
	utils_setup_password_storage (connection, NM_SETTING_802_1X_SETTING_NAME, widget, parent->password_flags_name);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "eap_password_always_ask"));
	g_assert (widget);
	method->always_ask = GTK_TOGGLE_BUTTON (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) wireless_security_changed_cb,
	                  ws_parent);
	if (flags & EAP_METHOD_SIMPLE_FLAG_IS_EDITOR) {
		/* We only desensitize the password entry from the editor, because
		 * from nm-applet if the entry was desensitized, there'd be no way to
		 * get the password back to NetworkManager when NM asked for it.  Since
		 * the editor only sets up the initial connection though, it's safe to
		 * do there.
		 */
		g_signal_connect (G_OBJECT (widget), "toggled",
		                  G_CALLBACK (password_always_ask_changed),
		                  method);
	}

	if (flags & EAP_METHOD_SIMPLE_FLAG_SECRETS_ONLY)
		gtk_widget_hide (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "show_checkbutton_eapsimple"));
	g_assert (widget);
	method->show_password = GTK_TOGGLE_BUTTON (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  method);

	/* Initialize the UI fields with the security settings from method->ws_parent.
	 * This will be done again when the widget gets realized. It must be done here as well,
	 * because the outer dialog will ask to 'validate' the connection before the security tab
	 * is shown/realized (to enable the 'Apply' button).
	 * As 'validate' accesses the contents of the UI fields, they must be initialized now, even
	 * if the widgets are not yet visible. */
	set_userpass_ui (method);

	return method;
}

