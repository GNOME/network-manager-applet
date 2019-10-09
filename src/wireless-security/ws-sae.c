// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2007 - 2019 Red Hat, Inc.
 */

#include "nm-default.h"
#include "nma-private.h"

#include <ctype.h>
#include <string.h>

#include "wireless-security.h"
#include "helpers.h"
#include "nma-ui-utils.h"
#include "utils.h"

#define WPA_PMK_LEN 32

struct _WirelessSecuritySAE {
	WirelessSecurity parent;

	gboolean editing_connection;
	const char *password_flags_name;
};

static void
show_toggled_cb (GtkCheckButton *button, WirelessSecurity *sec)
{
	GtkWidget *widget;
	gboolean visible;

	widget = GTK_WIDGET (gtk_builder_get_object (sec->builder, "psk_entry"));
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static gboolean
validate (WirelessSecurity *parent, GError **error)
{
	GtkWidget *entry;
	NMSettingSecretFlags secret_flags;
	const char *key;

	entry = GTK_WIDGET (gtk_builder_get_object (parent->builder, "psk_entry"));
	g_assert (entry);

	secret_flags = nma_utils_menu_to_secret_flags (entry);
	key = gtk_editable_get_text (GTK_EDITABLE (entry));

        if (   secret_flags & NM_SETTING_SECRET_FLAG_NOT_SAVED
            || secret_flags & NM_SETTING_SECRET_FLAG_NOT_REQUIRED) {
		/* All good. */
	} else if (key == NULL || key[0] == '\0') {
		widget_set_error (entry);
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("missing password"));
		return FALSE;
	}
	widget_unset_error (entry);

	return TRUE;
}

static void
add_to_size_group (WirelessSecurity *parent, GtkSizeGroup *group)
{
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "sae_type_label"));
	gtk_size_group_add_widget (group, widget);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "sae_label"));
	gtk_size_group_add_widget (group, widget);
}

static void
fill_connection (WirelessSecurity *parent, NMConnection *connection)
{
	WirelessSecuritySAE *sae = (WirelessSecuritySAE *) parent;
	GtkWidget *widget, *passwd_entry;
	const char *key;
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	NMSettingSecretFlags secret_flags;
	const char *mode;
	gboolean is_adhoc = FALSE;

	s_wireless = nm_connection_get_setting_wireless (connection);
	g_assert (s_wireless);

	mode = nm_setting_wireless_get_mode (s_wireless);
	if (mode && !strcmp (mode, "adhoc"))
		is_adhoc = TRUE;

	/* Blow away the old security setting by adding a clear one */
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "psk_entry"));
	passwd_entry = widget;
	key = gtk_editable_get_text (GTK_EDITABLE (widget));
	g_object_set (s_wireless_sec, NM_SETTING_WIRELESS_SECURITY_PSK, key, NULL);

	/* Save PSK_FLAGS to the connection */
	secret_flags = nma_utils_menu_to_secret_flags (passwd_entry);
	nm_setting_set_secret_flags (NM_SETTING (s_wireless_sec), NM_SETTING_WIRELESS_SECURITY_PSK,
	                             secret_flags, NULL);

	/* Update secret flags and popup when editing the connection */
	if (sae->editing_connection)
		nma_utils_update_password_storage (passwd_entry, secret_flags,
		                                   NM_SETTING (s_wireless_sec), sae->password_flags_name);

	wireless_security_clear_ciphers (connection);
	if (is_adhoc) {
		/* Ad-Hoc settings as specified by the supplicant */
		g_object_set (s_wireless_sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "sae", NULL);
		nm_setting_wireless_security_add_proto (s_wireless_sec, "rsn");
		nm_setting_wireless_security_add_pairwise (s_wireless_sec, "ccmp");
		nm_setting_wireless_security_add_group (s_wireless_sec, "ccmp");
	} else {
		g_object_set (s_wireless_sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "sae", NULL);

		/* Just leave ciphers and protocol empty, the supplicant will
		 * figure that out magically based on the AP IEs and card capabilities.
		 */
	}
}

static void
update_secrets (WirelessSecurity *parent, NMConnection *connection)
{
	helper_fill_secret_entry (connection,
	                          parent->builder,
	                          "psk_entry",
	                          NM_TYPE_SETTING_WIRELESS_SECURITY,
	                          (HelperSecretFunc) nm_setting_wireless_security_get_psk);
}

WirelessSecuritySAE *
ws_sae_new (NMConnection *connection, gboolean secrets_only)
{
	WirelessSecurity *parent;
	WirelessSecuritySAE *sec;
	NMSetting *setting = NULL;
	GtkWidget *widget;

	parent = wireless_security_init (sizeof (WirelessSecuritySAE),
	                                 validate,
	                                 add_to_size_group,
	                                 fill_connection,
	                                 update_secrets,
	                                 NULL,
	                                 "/org/freedesktop/network-manager-applet/ws-sae.ui",
	                                 "sae_notebook",
	                                 "psk_entry");
	if (!parent)
		return NULL;

	parent->adhoc_compatible = TRUE;
	sec = (WirelessSecuritySAE *) parent;
	sec->editing_connection = secrets_only ? FALSE : TRUE;
	sec->password_flags_name = NM_SETTING_WIRELESS_SECURITY_PSK;

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "psk_entry"));
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);
	gtk_editable_set_width_chars (GTK_EDITABLE (widget), 28);

	/* Create password-storage popup menu for password entry under entry's secondary icon */
	if (connection)
		setting = (NMSetting *) nm_connection_get_setting_wireless_security (connection);
	nma_utils_setup_password_storage (widget, 0, setting, sec->password_flags_name,
	                                  FALSE, secrets_only);

	/* Fill secrets, if any */
	if (connection)
		update_secrets (WIRELESS_SECURITY (sec), connection);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "show_checkbutton_sae"));
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  sec);

	/* Hide WPA/RSN for now since this can be autodetected by NM and the
	 * supplicant when connecting to the AP.
	 */

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "sae_type_combo"));
	g_assert (widget);
	gtk_widget_hide (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "sae_type_label"));
	g_assert (widget);
	gtk_widget_hide (widget);

	return sec;
}

