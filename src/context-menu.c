/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * Copyright (C) 2004 - 2015 Red Hat, Inc.
 * Copyright (C) 2005 - 2008 Novell, Inc.
 *
 * This applet used the GNOME Wireless Applet as a skeleton to build from.
 *
 * GNOME Wireless Applet Authors:
 *		Eskil Heyn Olsen <eskil@eskil.dk>
 *		Bastien Nocera <hadess@hadess.net> (Gnome2 port)
 *
 * Copyright 2001, 2002 Free Software Foundation
 */

#include "nm-default.h"

#include "applet.h"
#include "context-menu.h"
#include "applet-dialogs.h"
#include "menu-utils.h"
#include "utils.h"

static void
nma_set_wifi_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_wireless_set_enabled (applet->nm_client, state);
}

static void
nma_set_wwan_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_wwan_set_enabled (applet->nm_client, state);
}

static void
nma_set_networking_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_networking_set_enabled (applet->nm_client, state, NULL);
}


static void
nma_set_notifications_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));

	g_settings_set_boolean (applet->gsettings,
	                        PREF_DISABLE_CONNECTED_NOTIFICATIONS,
	                        !state);
	g_settings_set_boolean (applet->gsettings,
	                        PREF_DISABLE_DISCONNECTED_NOTIFICATIONS,
	                        !state);
	g_settings_set_boolean (applet->gsettings,
	                        PREF_DISABLE_VPN_NOTIFICATIONS,
	                        !state);
	g_settings_set_boolean (applet->gsettings,
	                        PREF_SUPPRESS_WIFI_NETWORKS_AVAILABLE,
	                        !state);
}

static gboolean
is_permission_yes (NMApplet *applet, NMClientPermission perm)
{
	if (   applet->permissions[perm] == NM_CLIENT_PERMISSION_RESULT_YES
	    || applet->permissions[perm] == NM_CLIENT_PERMISSION_RESULT_AUTH)
		return TRUE;
	return FALSE;
}

/*
 * nma_context_menu_update
 *
 */
void
nma_context_menu_update (NMApplet *applet)
{
	NMState state;
	gboolean net_enabled = TRUE;
	gboolean have_wifi = FALSE;
	gboolean have_wwan = FALSE;
	gboolean wifi_hw_enabled;
	gboolean wwan_hw_enabled;
	gboolean notifications_enabled = TRUE;
	gboolean sensitive = FALSE;

	state = nm_client_get_state (applet->nm_client);
	sensitive = (   state == NM_STATE_CONNECTED_LOCAL
	             || state == NM_STATE_CONNECTED_SITE
	             || state == NM_STATE_CONNECTED_GLOBAL);
	gtk_widget_set_sensitive (applet->info_menu_item, sensitive);

	/* Update checkboxes, and block 'toggled' signal when updating so that the
	 * callback doesn't get triggered.
	 */

	/* Enabled Networking */
	g_signal_handler_block (G_OBJECT (applet->networking_enabled_item),
	                        applet->networking_enabled_toggled_id);
	net_enabled = nm_client_networking_get_enabled (applet->nm_client);
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->networking_enabled_item),
	                                net_enabled && (state != NM_STATE_ASLEEP));
	g_signal_handler_unblock (G_OBJECT (applet->networking_enabled_item),
	                          applet->networking_enabled_toggled_id);
	gtk_widget_set_sensitive (applet->networking_enabled_item,
	                          is_permission_yes (applet, NM_CLIENT_PERMISSION_ENABLE_DISABLE_NETWORK));

	/* Enabled Wi-Fi */
	g_signal_handler_block (G_OBJECT (applet->wifi_enabled_item),
	                        applet->wifi_enabled_toggled_id);
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->wifi_enabled_item),
	                                nm_client_wireless_get_enabled (applet->nm_client));
	g_signal_handler_unblock (G_OBJECT (applet->wifi_enabled_item),
	                          applet->wifi_enabled_toggled_id);

	wifi_hw_enabled = nm_client_wireless_hardware_get_enabled (applet->nm_client);
	gtk_widget_set_sensitive (GTK_WIDGET (applet->wifi_enabled_item),
	                          wifi_hw_enabled && is_permission_yes (applet, NM_CLIENT_PERMISSION_ENABLE_DISABLE_WIFI));

	/* Enabled Mobile Broadband */
	g_signal_handler_block (G_OBJECT (applet->wwan_enabled_item),
	                        applet->wwan_enabled_toggled_id);
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->wwan_enabled_item),
	                                nm_client_wwan_get_enabled (applet->nm_client));
	g_signal_handler_unblock (G_OBJECT (applet->wwan_enabled_item),
	                          applet->wwan_enabled_toggled_id);

	wwan_hw_enabled = nm_client_wwan_hardware_get_enabled (applet->nm_client);
	gtk_widget_set_sensitive (GTK_WIDGET (applet->wwan_enabled_item),
	                          wwan_hw_enabled && is_permission_yes (applet, NM_CLIENT_PERMISSION_ENABLE_DISABLE_WWAN));

	if (!INDICATOR_ENABLED (applet)) {
		/* Enabled notifications */
		g_signal_handler_block (G_OBJECT (applet->notifications_enabled_item),
			                    applet->notifications_enabled_toggled_id);
		if (   g_settings_get_boolean (applet->gsettings, PREF_DISABLE_CONNECTED_NOTIFICATIONS)
			&& g_settings_get_boolean (applet->gsettings, PREF_DISABLE_DISCONNECTED_NOTIFICATIONS)
			&& g_settings_get_boolean (applet->gsettings, PREF_DISABLE_VPN_NOTIFICATIONS)
			&& g_settings_get_boolean (applet->gsettings, PREF_SUPPRESS_WIFI_NETWORKS_AVAILABLE))
			notifications_enabled = FALSE;
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->notifications_enabled_item), notifications_enabled);
		g_signal_handler_unblock (G_OBJECT (applet->notifications_enabled_item),
			                      applet->notifications_enabled_toggled_id);
	}

	/* Don't show wifi-specific stuff if wifi is off */
	if (state != NM_STATE_ASLEEP) {
		const GPtrArray *devices;
		int i;

		devices = nm_client_get_devices (applet->nm_client);
		for (i = 0; devices && (i < devices->len); i++) {
			NMDevice *candidate = g_ptr_array_index (devices, i);

			if (NM_IS_DEVICE_WIFI (candidate))
				have_wifi = TRUE;
			else if (NM_IS_DEVICE_MODEM (candidate))
				have_wwan = TRUE;
		}
	}

	if (have_wifi)
		gtk_widget_show_all (applet->wifi_enabled_item);
	else
		gtk_widget_hide (applet->wifi_enabled_item);

	if (have_wwan)
		gtk_widget_show_all (applet->wwan_enabled_item);
	else
		gtk_widget_hide (applet->wwan_enabled_item);
}

static void
ce_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	/* We are in the child process at this point */
	pid_t pid = getpid ();
	setpgid (pid, pid);
}

static void
nma_edit_connections_cb (void)
{
	char *argv[2];
	GError *error = NULL;
	gboolean success;

	argv[0] = BINDIR "/nm-connection-editor";
	argv[1] = NULL;

	success = g_spawn_async ("/", argv, NULL, 0, &ce_child_setup, NULL, NULL, &error);
	if (!success) {
		g_warning ("Error launching connection editor: %s", error->message);
		g_error_free (error);
	}
}

static void
applet_connection_info_cb (NMApplet *applet)
{
	applet_info_dialog_show (applet);
}

/*
 * nma_context_menu_populate
 *
 * Populate the contextual popup menu.
 *
 */
void
nma_context_menu_populate (NMApplet *applet, GtkMenu *menu)
{
	GtkMenuShell *menu_shell;
	guint id;
	static gboolean icons_shown = FALSE;

	g_assert (applet);
	g_assert (menu);

	menu_shell = GTK_MENU_SHELL (menu);

	if (G_UNLIKELY (icons_shown == FALSE)) {
		GtkSettings *settings = gtk_widget_get_settings (GTK_WIDGET (menu_shell));

		/* We always want our icons displayed */
		if (settings)
			g_object_set (G_OBJECT (settings), "gtk-menu-images", TRUE, NULL);
		icons_shown = TRUE;
	}

	/* 'Enable Networking' item */
	applet->networking_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Networking"));
	id = g_signal_connect (applet->networking_enabled_item,
	                       "toggled",
	                       G_CALLBACK (nma_set_networking_enabled_cb),
	                       applet);
	applet->networking_enabled_toggled_id = id;
	gtk_menu_shell_append (menu_shell, applet->networking_enabled_item);

	/* 'Enable Wi-Fi' item */
	applet->wifi_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Wi-Fi"));
	id = g_signal_connect (applet->wifi_enabled_item,
	                       "toggled",
	                       G_CALLBACK (nma_set_wifi_enabled_cb),
	                       applet);
	applet->wifi_enabled_toggled_id = id;
	gtk_menu_shell_append (menu_shell, applet->wifi_enabled_item);

	/* 'Enable Mobile Broadband' item */
	applet->wwan_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Mobile Broadband"));
	id = g_signal_connect (applet->wwan_enabled_item,
	                       "toggled",
	                       G_CALLBACK (nma_set_wwan_enabled_cb),
	                       applet);
	applet->wwan_enabled_toggled_id = id;
	gtk_menu_shell_append (menu_shell, applet->wwan_enabled_item);

	nma_menu_add_separator_item (GTK_WIDGET (menu_shell));

	if (!INDICATOR_ENABLED (applet)) {
		/* Toggle notifications item */
		applet->notifications_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable N_otifications"));
		id = g_signal_connect (applet->notifications_enabled_item,
			                   "toggled",
			                   G_CALLBACK (nma_set_notifications_enabled_cb),
			                   applet);
		applet->notifications_enabled_toggled_id = id;
		gtk_menu_shell_append (menu_shell, applet->notifications_enabled_item);

		nma_menu_add_separator_item (GTK_WIDGET (menu_shell));
	}

	/* 'Connection Information' item */
	applet->info_menu_item = gtk_menu_item_new_with_mnemonic (_("Connection _Information"));
	g_signal_connect_swapped (applet->info_menu_item,
	                          "activate",
	                          G_CALLBACK (applet_connection_info_cb),
	                          applet);
	gtk_menu_shell_append (menu_shell, applet->info_menu_item);

	/* 'Edit Connections...' item */
	applet->connections_menu_item = gtk_menu_item_new_with_mnemonic (_("Edit Connections…"));
	g_signal_connect (applet->connections_menu_item,
				   "activate",
				   G_CALLBACK (nma_edit_connections_cb),
				   applet);
	gtk_menu_shell_append (menu_shell, applet->connections_menu_item);

	/* Separator */
	nma_menu_add_separator_item (GTK_WIDGET (menu_shell));

	if (!INDICATOR_ENABLED (applet)) {
		/* About item */
		GtkWidget *menu_item;

		menu_item = gtk_menu_item_new_with_mnemonic (_("_About"));
		g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (applet_about_dialog_show), applet);
		gtk_menu_shell_append (menu_shell, menu_item);
	}

	gtk_widget_show_all (GTK_WIDGET (menu_shell));
}
