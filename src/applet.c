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
 * This applet used the GNOME Wireless Applet as a skeleton to build from.
 *
 * GNOME Wireless Applet Authors:
 *		Eskil Heyn Olsen <eskil@eskil.dk>
 *		Bastien Nocera <hadess@hadess.net> (Gnome2 port)
 *
 * (C) Copyright 2004-2008 Red Hat, Inc.
 * (C) Copyright 2001, 2002 Free Software Foundation
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <time.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "wireless-helper.h"
#include <unistd.h>
#include <sys/socket.h>

#include <NetworkManagerVPN.h>
#include <nm-device-ethernet.h>
#include <nm-device-wifi.h>
#include <nm-gsm-device.h>
#include <nm-cdma-device.h>
#include <nm-utils.h>
#include <nm-connection.h>
#include <nm-vpn-connection.h>
#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>
#include <nm-active-connection.h>
#include <nm-dbus-settings-system.h>
#include <nm-setting-wireless.h>

#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <gnome-keyring.h>
#include <libnotify/notify.h>

#include "applet.h"
#include "applet-device-wired.h"
#include "applet-device-wifi.h"
#include "applet-device-gsm.h"
#include "applet-device-cdma.h"
#include "applet-dialogs.h"
#include "vpn-password-dialog.h"
#include "applet-dbus-manager.h"
#include "utils.h"
#include "gconf-helpers.h"


G_DEFINE_TYPE(NMApplet, nma, G_TYPE_OBJECT)

static NMActiveConnection *
applet_get_best_activating_connection (NMApplet *applet, NMDevice **device)
{
	NMActiveConnection *best = NULL;
	NMDevice *best_dev = NULL;
	const GPtrArray *connections;
	int i;

	g_return_val_if_fail (NM_IS_APPLET (applet), NULL);
	g_return_val_if_fail (device != NULL, NULL);
	g_return_val_if_fail (*device == NULL, NULL);

	connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; connections && (i < connections->len); i++) {
		NMActiveConnection *candidate = g_ptr_array_index (connections, i);
		const GPtrArray *devices;
		NMDevice *candidate_dev;

		if (nm_active_connection_get_state (candidate) != NM_ACTIVE_CONNECTION_STATE_ACTIVATING)
			continue;

		devices = nm_active_connection_get_devices (candidate);
		if (!devices || !devices->len)
			continue;

		candidate_dev = g_ptr_array_index (devices, 0);
		if (!best_dev) {
			best_dev = candidate_dev;
			best = candidate;
			continue;
		}

		if (NM_IS_DEVICE_WIFI (best_dev)) {
			if (NM_IS_DEVICE_ETHERNET (candidate)) {
				best_dev = candidate_dev;
				best = candidate;
			}
		} else if (NM_IS_CDMA_DEVICE (best_dev)) {
			if (   NM_IS_DEVICE_ETHERNET (candidate)
			    || NM_IS_DEVICE_WIFI (candidate)) {
				best_dev = candidate_dev;
				best = candidate;
			}
		} else if (NM_IS_GSM_DEVICE (best_dev)) {
			if (   NM_IS_DEVICE_ETHERNET (candidate)
			    || NM_IS_DEVICE_WIFI (candidate)
			    || NM_IS_CDMA_DEVICE (candidate)) {
				best_dev = candidate_dev;
				best = candidate;
			}
		}
	}

	*device = best_dev;
	return best;
}

static NMActiveConnection *
applet_get_default_active_connection (NMApplet *applet, NMDevice **device)
{
	NMActiveConnection *default_ac = NULL;
	NMDevice *non_default_device = NULL;
	NMActiveConnection *non_default_ac = NULL;
	const GPtrArray *connections;
	int i;

	g_return_val_if_fail (NM_IS_APPLET (applet), NULL);
	g_return_val_if_fail (device != NULL, NULL);
	g_return_val_if_fail (*device == NULL, NULL);

	connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; connections && (i < connections->len); i++) {
		NMActiveConnection *candidate = g_ptr_array_index (connections, i);
		const GPtrArray *devices;

		devices = nm_active_connection_get_devices (candidate);
		if (!devices || !devices->len)
			continue;

		if (nm_active_connection_get_default (candidate)) {
			if (!default_ac) {
				*device = g_ptr_array_index (devices, 0);
				default_ac = candidate;
			}
		} else {
			if (!non_default_ac) {
				non_default_device = g_ptr_array_index (devices, 0);
				non_default_ac = candidate;
			}
		}
	}

	/* Prefer the default connection if one exists, otherwise return the first
	 * non-default connection.
	 */
	if (!default_ac && non_default_ac) {
		default_ac = non_default_ac;
		*device = non_default_device;
	}
	return default_ac;
}

static void
exported_connection_to_connection (gpointer data, gpointer user_data)
{
	GSList **list = (GSList **) user_data;

	*list = g_slist_prepend (*list, nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (data)));
}

NMSettings *
applet_get_settings (NMApplet *applet)
{
	return NM_SETTINGS (applet->gconf_settings);
}

GSList *
applet_get_all_connections (NMApplet *applet)
{
	GSList *list;
	GSList *connections = NULL;

	list = nm_settings_list_connections (NM_SETTINGS (applet->dbus_settings));
	g_slist_foreach (list, exported_connection_to_connection, &connections);
	g_slist_free (list);

	list = nm_settings_list_connections (NM_SETTINGS (applet->gconf_settings));
	g_slist_foreach (list, exported_connection_to_connection, &connections);
	g_slist_free (list);

	return connections;
}

static NMConnection *
applet_get_connection_for_active (NMApplet *applet, NMActiveConnection *active)
{
	GSList *list, *iter;
	NMConnection *connection = NULL;
	NMConnectionScope scope;
	const char *path;

	scope = nm_active_connection_get_scope (active);
	g_return_val_if_fail (scope != NM_CONNECTION_SCOPE_UNKNOWN, NULL);

	path = nm_active_connection_get_connection (active);
	g_return_val_if_fail (path != NULL, NULL);

	list = applet_get_all_connections (applet);
	for (iter = list; iter; iter = g_slist_next (iter)) {
		NMConnection *candidate = NM_CONNECTION (iter->data);

		if (   (nm_connection_get_scope (candidate) == scope)
			   && !strcmp (nm_connection_get_path (candidate), path)) {
			connection = candidate;
			break;
		}
	}

	g_slist_free (list);

	return connection;
}

static NMActiveConnection *
applet_get_active_for_connection (NMApplet *applet, NMConnection *connection)
{
	const GPtrArray *active_list;
	int i;
	const char *cpath;
	NMConnectionScope scope;

	scope = nm_connection_get_scope (connection);
	g_return_val_if_fail (scope != NM_CONNECTION_SCOPE_UNKNOWN, NULL);

	cpath = nm_connection_get_path (connection);
	g_return_val_if_fail (cpath != NULL, NULL);

	active_list = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; active_list && (i < active_list->len); i++) {
		NMActiveConnection *active = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_list, i));

		if (   (nm_active_connection_get_scope (active) == scope)
		    && !strcmp (nm_active_connection_get_connection (active), cpath))
			return active;
	}
	return NULL;
}

static inline NMADeviceClass *
get_device_class (NMDevice *device, NMApplet *applet)
{
	g_return_val_if_fail (device != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	if (NM_IS_DEVICE_ETHERNET (device))
		return applet->wired_class;
	else if (NM_IS_DEVICE_WIFI (device))
		return applet->wifi_class;
	else if (NM_IS_GSM_DEVICE (device))
		return applet->gsm_class;
	else if (NM_IS_CDMA_DEVICE (device))
		return applet->cdma_class;
	else
		g_message ("%s: Unknown device type '%s'", __func__, G_OBJECT_TYPE_NAME (device));
	return NULL;
}

static gboolean
is_system_connection (NMConnection *connection)
{
	return (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_SYSTEM) ? TRUE : FALSE;
}

static void
activate_connection_cb (gpointer user_data, const char *path, GError *error)
{
	if (error)
		nm_warning ("Connection activation failed: %s", error->message);

	applet_schedule_update_icon (NM_APPLET (user_data));
}

void
applet_menu_item_activate_helper (NMDevice *device,
                                  NMConnection *connection,
                                  const char *specific_object,
                                  NMApplet *applet,
                                  gpointer user_data)
{
	NMAGConfConnection *exported;
	const char *con_path;
	gboolean is_system = FALSE;

	g_return_if_fail (NM_IS_DEVICE (device));

	if (connection) {
		is_system = is_system_connection (connection);
	} else {
		NMADeviceClass *dclass = get_device_class (device, applet);

		/* If no connection was given, create a new default connection for this
		 * device type.
		 */
		g_assert (dclass);
		connection = dclass->new_auto_connection (device, applet, user_data);
		if (!connection) {
			nm_warning ("Couldn't create default connection.");
			return;
		}

		exported = nma_gconf_settings_add_connection (applet->gconf_settings, connection);
		if (!exported) {
			/* If the setting isn't valid, because it needs more authentication
			 * or something, ask the user for it.
			 */

			nm_warning ("Invalid connection; asking for more information.");
			if (dclass->get_more_info)
				dclass->get_more_info (device, connection, applet, user_data);
			g_object_unref (connection);
			return;
		}
		g_object_unref (connection);
	}

	g_assert (connection);
	con_path = nm_connection_get_path (connection);
	g_assert (con_path);

	/* Finally, tell NM to activate the connection */
	nm_client_activate_connection (applet->nm_client,
	                               is_system ? NM_DBUS_SERVICE_SYSTEM_SETTINGS : NM_DBUS_SERVICE_USER_SETTINGS,
	                               con_path,
	                               device,
	                               specific_object,
	                               activate_connection_cb,
	                               applet);
}

static void
applet_clear_notify (NMApplet *applet)
{
	if (applet->notification == NULL)
		return;

	notify_notification_close (applet->notification, NULL);
	g_object_unref (applet->notification);
	applet->notification = NULL;
}

void
applet_do_notify (NMApplet *applet,
                  NotifyUrgency urgency,
                  const char *summary,
                  const char *message,
                  const char *icon,
                  const char *action1,
                  const char *action1_label,
                  NotifyActionCallback action1_cb,
                  gpointer action1_user_data)
{
	NotifyNotification *notify;
	GError *error = NULL;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (summary != NULL);
	g_return_if_fail (message != NULL);

#if GTK_CHECK_VERSION(2, 11, 0)
	if (!gtk_status_icon_is_embedded (applet->status_icon))
		return;
#endif

	applet_clear_notify (applet);

	notify = notify_notification_new (summary, message,
	                                  icon ? icon : GTK_STOCK_NETWORK, NULL);
	applet->notification = notify;

	notify_notification_attach_to_status_icon (notify, applet->status_icon);
	notify_notification_set_urgency (notify, urgency);

	if (action1) {
		notify_notification_add_action (notify, action1, action1_label,
		                                action1_cb, action1_user_data, NULL);
	}

	if (!notify_notification_show (notify, &error)) {
		g_warning ("Failed to show notification: %s", error->message);
		g_error_free (error);
	}
}

static gboolean
animation_timeout (gpointer data)
{
	applet_schedule_update_icon (NM_APPLET (data));
	return TRUE;
}

static void
start_animation_timeout (NMApplet *applet)
{
	if (applet->animation_id == 0) {
		applet->animation_step = 0;
		applet->animation_id = g_timeout_add (100, animation_timeout, applet);
	}
}

static void
clear_animation_timeout (NMApplet *applet)
{
	if (applet->animation_id) {
		g_source_remove (applet->animation_id);
		applet->animation_id = 0;
		applet->animation_step = 0;
	}
}

static void
update_connection_timestamp (NMActiveConnection *active,
                             NMConnection *connection,
                             NMApplet *applet)
{
	NMAGConfConnection *gconf_connection;
	const char *path;
	NMSettingConnection *s_con;

	if (nm_active_connection_get_scope (active) != NM_CONNECTION_SCOPE_USER)
		return;

	path = nm_connection_get_path (connection);
	gconf_connection = nma_gconf_settings_get_by_dbus_path (applet->gconf_settings, path);
	if (!gconf_connection)
		return;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	s_con->timestamp = (guint64) time (NULL);
	nma_gconf_connection_save (gconf_connection);
}

static char *
make_vpn_failure_message (NMVPNConnection *vpn,
                          NMVPNConnectionStateReason reason,
                          NMApplet *applet)
{
	NMConnection *connection;
	NMSettingConnection *s_con;

	g_return_val_if_fail (vpn != NULL, NULL);

	connection = applet_get_connection_for_active (applet, NM_ACTIVE_CONNECTION (vpn));
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));

	switch (reason) {
	case NM_VPN_CONNECTION_STATE_REASON_DEVICE_DISCONNECTED:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the network connection was interrupted."), s_con->id);
	case NM_VPN_CONNECTION_STATE_REASON_SERVICE_STOPPED:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service stopped unexpectedly."), s_con->id);
	case NM_VPN_CONNECTION_STATE_REASON_IP_CONFIG_INVALID:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service returned invalid configuration."), s_con->id);
	case NM_VPN_CONNECTION_STATE_REASON_CONNECT_TIMEOUT:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the connection attempt timed out."), s_con->id);
	case NM_VPN_CONNECTION_STATE_REASON_SERVICE_START_TIMEOUT:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service did not start in time."), s_con->id);
	case NM_VPN_CONNECTION_STATE_REASON_SERVICE_START_FAILED:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service failed to start."), s_con->id);
	case NM_VPN_CONNECTION_STATE_REASON_NO_SECRETS:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because there were no valid VPN secrets."), s_con->id);
	case NM_VPN_CONNECTION_STATE_REASON_LOGIN_FAILED:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because of invalid VPN secrets."), s_con->id);

	default:
		break;
	}

	return g_strdup_printf (_("\nThe VPN connection '%s' failed."), s_con->id);
}

static char *
make_vpn_disconnection_message (NMVPNConnection *vpn,
                                NMVPNConnectionStateReason reason,
                                NMApplet *applet)
{
	NMConnection *connection;
	NMSettingConnection *s_con;

	g_return_val_if_fail (vpn != NULL, NULL);

	connection = applet_get_connection_for_active (applet, NM_ACTIVE_CONNECTION (vpn));
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));

	switch (reason) {
	case NM_VPN_CONNECTION_STATE_REASON_DEVICE_DISCONNECTED:
		return g_strdup_printf (_("\nThe VPN connection '%s' disconnected because the network connection was interrupted."), s_con->id);
	case NM_VPN_CONNECTION_STATE_REASON_SERVICE_STOPPED:
		return g_strdup_printf (_("\nThe VPN connection '%s' disconnected because the VPN service stopped."), s_con->id);
	default:
		break;
	}

	return g_strdup_printf (_("\nThe VPN connection '%s' disconnected."), s_con->id);
}

static void
vpn_connection_state_changed (NMVPNConnection *vpn,
                              NMVPNConnectionState state,
                              NMVPNConnectionStateReason reason,
                              gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMConnection *connection;
	const char *banner;
	char *title = NULL, *msg = NULL;
	gboolean clear_timeout = TRUE;

	switch (state) {
	case NM_VPN_CONNECTION_STATE_PREPARE:
	case NM_VPN_CONNECTION_STATE_NEED_AUTH:
	case NM_VPN_CONNECTION_STATE_CONNECT:
	case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
		start_animation_timeout (applet);
		clear_timeout = FALSE;
		break;
	case NM_VPN_CONNECTION_STATE_ACTIVATED:
		banner = nm_vpn_connection_get_banner (vpn);
		if (banner && strlen (banner)) {
			title = _("VPN Login Message");
			msg = g_strdup_printf ("\n%s", banner);
			applet_do_notify (applet, NOTIFY_URGENCY_LOW, title, msg,
			                  "gnome-lockscreen", NULL, NULL, NULL, NULL);
			g_free (msg);
		}

		connection = applet_get_connection_for_active (applet, NM_ACTIVE_CONNECTION (vpn));
		if (connection)
			update_connection_timestamp (NM_ACTIVE_CONNECTION (vpn), connection, applet);
		break;
	case NM_VPN_CONNECTION_STATE_FAILED:
		title = _("VPN Connection Failed");
		msg = make_vpn_failure_message (vpn, reason, applet);
		applet_do_notify (applet, NOTIFY_URGENCY_LOW, title, msg,
		                  "gnome-lockscreen", NULL, NULL, NULL, NULL);
		g_free (msg);
		break;
	case NM_VPN_CONNECTION_STATE_DISCONNECTED:
		if (reason != NM_VPN_CONNECTION_STATE_REASON_USER_DISCONNECTED) {
			title = _("VPN Connection Failed");
			msg = make_vpn_disconnection_message (vpn, reason, applet);
			applet_do_notify (applet, NOTIFY_URGENCY_LOW, title, msg,
			                  "gnome-lockscreen", NULL, NULL, NULL, NULL);
			g_free (msg);
		}
		break;
	default:
		break;
	}

	if (clear_timeout)
		clear_animation_timeout (applet);
	applet_schedule_update_icon (applet);
}

static const char *
get_connection_id (NMConnection *connection)
{
	NMSettingConnection *s_con;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_return_val_if_fail (s_con != NULL, NULL);

	return s_con->id;
}

typedef struct {
	NMApplet *applet;
	char *vpn_name;
} VPNActivateInfo;

static void
activate_vpn_cb (gpointer user_data, const char *path, GError *error)
{
	VPNActivateInfo *info = (VPNActivateInfo *) user_data;
	char *title, *msg, *name;

	if (error) {
		clear_animation_timeout (info->applet);

		title = _("VPN Connection Failed");

		/* dbus-glib GError messages _always_ have two NULLs, the D-Bus error
		 * name comes after the first NULL.  Find it.
		 */
		name = error->message + strlen (error->message) + 1;
		if (strstr (name, "ServiceStartFailed")) {
			msg = g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service failed to start.\n\n%s"),
			                       info->vpn_name, error->message);
		} else {
			msg = g_strdup_printf (_("\nThe VPN connection '%s' failed to start.\n\n%s"),
			                       info->vpn_name, error->message);
		}

		applet_do_notify (info->applet, NOTIFY_URGENCY_LOW, title, msg,
		                  "gnome-lockscreen", NULL, NULL, NULL, NULL);
		g_free (msg);

		nm_warning ("VPN Connection activation failed: (%s) %s", name, error->message);
	}

	applet_schedule_update_icon (info->applet);
	g_free (info->vpn_name);
	g_free (info);
}

static void
nma_menu_vpn_item_clicked (GtkMenuItem *item, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	VPNActivateInfo *info;
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMActiveConnection *active;
	NMDevice *device = NULL;
	gboolean is_system;

	active = applet_get_default_active_connection (applet, &device);
	if (!active || !device) {
		g_warning ("%s: no active connection or device.", __func__);
		return;
	}

	connection = NM_CONNECTION (g_object_get_data (G_OBJECT (item), "connection"));
	if (!connection) {
		g_warning ("%s: no connection associated with menu item!", __func__);
		return;
	}

	if (applet_get_active_for_connection (applet, connection))
		/* Connection already active; do nothing */
		return;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	info = g_malloc0 (sizeof (VPNActivateInfo));
	info->applet = applet;
	info->vpn_name = g_strdup (s_con->id);

	/* Connection inactive, activate */
	is_system = is_system_connection (connection);
	nm_client_activate_connection (applet->nm_client,
	                               is_system ? NM_DBUS_SERVICE_SYSTEM_SETTINGS : NM_DBUS_SERVICE_USER_SETTINGS,
	                               nm_connection_get_path (connection),
	                               device,
	                               nm_object_get_path (NM_OBJECT (active)),
	                               activate_vpn_cb,
	                               info);
	start_animation_timeout (applet);
		
//	nmi_dbus_signal_user_interface_activated (applet->connection);
}


/*
 * nma_menu_configure_vpn_item_activate
 *
 * Signal function called when user clicks "Configure VPN..."
 *
 */
static void
nma_menu_configure_vpn_item_activate (GtkMenuItem *item, gpointer user_data)
{
	const char *argv[] = { BINDIR "/nm-connection-editor", "--type", NM_SETTING_VPN_SETTING_NAME, NULL};

	g_spawn_async (NULL, (gchar **) argv, NULL, 0, NULL, NULL, NULL, NULL);

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

static NMActiveConnection *
applet_get_first_active_vpn_connection (NMApplet *applet,
                                        NMVPNConnectionState *out_state)
{
	const GPtrArray *active_list;
	int i;

	active_list = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; active_list && (i < active_list->len); i++) {
		NMActiveConnection *candidate;
		NMConnection *connection;
		NMSettingConnection *s_con;

		candidate = g_ptr_array_index (active_list, i);

		connection = applet_get_connection_for_active (applet, candidate);
		if (!connection)
			continue;

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		g_assert (s_con);

		if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME)) {
			if (out_state)
				*out_state = nm_vpn_connection_get_vpn_state (NM_VPN_CONNECTION (candidate));
			return candidate;
		}
	}

	return NULL;
}

/*
 * nma_menu_disconnect_vpn_item_activate
 *
 * Signal function called when user clicks "Disconnect VPN..."
 *
 */
static void
nma_menu_disconnect_vpn_item_activate (GtkMenuItem *item, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMActiveConnection *active_vpn = NULL;
	NMVPNConnectionState state = NM_VPN_CONNECTION_STATE_UNKNOWN;

	active_vpn = applet_get_first_active_vpn_connection (applet, &state);
	if (active_vpn)
		nm_client_deactivate_connection (applet->nm_client, active_vpn);
	else
		g_warning ("%s: deactivate clicked but no active VPN connection could be found.", __func__);
//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

/*
 * nma_menu_add_separator_item
 *
 */
static void
nma_menu_add_separator_item (GtkWidget *menu)
{
	GtkWidget *menu_item;

	menu_item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}


/*
 * nma_menu_add_text_item
 *
 * Add a non-clickable text item to a menu
 *
 */
static void nma_menu_add_text_item (GtkWidget *menu, char *text)
{
	GtkWidget		*menu_item;

	g_return_if_fail (text != NULL);
	g_return_if_fail (menu != NULL);

	menu_item = gtk_menu_item_new_with_label (text);
	gtk_widget_set_sensitive (menu_item, FALSE);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}

static gint
sort_devices (gconstpointer a, gconstpointer b)
{
	NMDevice *aa = NM_DEVICE (a);
	NMDevice *bb = NM_DEVICE (b);
	GType aa_type;
	GType bb_type;

	aa_type = G_OBJECT_TYPE (G_OBJECT (aa));
	bb_type = G_OBJECT_TYPE (G_OBJECT (bb));

	if (aa_type == bb_type) {
		char *aa_desc = NULL;
		char *bb_desc = NULL;

		aa_desc = (char *) utils_get_device_description (aa);
		if (!aa_desc)
			aa_desc = (char *) nm_device_get_iface (aa);

		bb_desc = (char *) utils_get_device_description (bb);
		if (!bb_desc)
			bb_desc = (char *) nm_device_get_iface (bb);

		if (!aa_desc && bb_desc)
			return -1;
		else if (aa_desc && !bb_desc)
			return 1;
		else if (!aa_desc && !bb_desc)
			return 0;

		g_assert (aa_desc);
		g_assert (bb_desc);
		return strcmp (aa_desc, bb_desc);
	}

	if (aa_type == NM_TYPE_DEVICE_ETHERNET && bb_type == NM_TYPE_DEVICE_WIFI)
		return -1;
	if (aa_type == NM_TYPE_DEVICE_ETHERNET && bb_type == NM_TYPE_GSM_DEVICE)
		return -1;
	if (aa_type == NM_TYPE_DEVICE_ETHERNET && bb_type == NM_TYPE_CDMA_DEVICE)
		return -1;

	if (aa_type == NM_TYPE_GSM_DEVICE && bb_type == NM_TYPE_CDMA_DEVICE)
		return -1;
	if (aa_type == NM_TYPE_GSM_DEVICE && bb_type == NM_TYPE_DEVICE_WIFI)
		return -1;

	if (aa_type == NM_TYPE_CDMA_DEVICE && bb_type == NM_TYPE_DEVICE_WIFI)
		return -1;

	return 1;
}

static gboolean
nm_g_ptr_array_contains (const GPtrArray *haystack, gpointer needle)
{
	int i;

	for (i = 0; haystack && (i < haystack->len); i++) {
		if (g_ptr_array_index (haystack, i) == needle)
			return TRUE;
	}
	return FALSE;
}

NMConnection *
applet_find_active_connection_for_device (NMDevice *device,
                                          NMApplet *applet,
                                          NMActiveConnection **out_active)
{
	const GPtrArray *active_connections;
	NMConnection *connection = NULL;
	int i;

	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);
	g_return_val_if_fail (NM_IS_APPLET (applet), NULL);
	if (out_active)
		g_return_val_if_fail (*out_active == NULL, NULL);

	active_connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; active_connections && (i < active_connections->len); i++) {
		NMActiveConnection *active;
		const char *service_name;
		const char *connection_path;
		const GPtrArray *devices;

		active = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_connections, i));
		devices = nm_active_connection_get_devices (active);
		service_name = nm_active_connection_get_service_name (active);
		connection_path = nm_active_connection_get_connection (active);

		if (!nm_g_ptr_array_contains (devices, device))
			continue;

		if (!strcmp (service_name, NM_DBUS_SERVICE_SYSTEM_SETTINGS)) {
			NMDBusConnection *tmp;

			tmp = nm_dbus_settings_get_connection_by_path (applet->dbus_settings, connection_path);
			if (tmp)
				connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (tmp));
		} else if (!strcmp (service_name, NM_DBUS_SERVICE_USER_SETTINGS)) {
			NMAGConfConnection *tmp;

			tmp = nma_gconf_settings_get_by_dbus_path (applet->gconf_settings, connection_path);
			if (tmp) {
				connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (tmp));
				if (out_active)
					*out_active = active;
				break;
			}
		}
	}

	return connection;
}

static guint32
nma_menu_add_devices (GtkWidget *menu, NMApplet *applet)
{
	const GPtrArray *temp = NULL;
	GSList *devices = NULL, *iter = NULL;
	gint n_wifi_devices = 0;
	gint n_usable_wifi_devices = 0;
	gint n_wired_devices = 0;
	gint n_cdma_devices = 0;
	gint n_gsm_devices = 0;
	int i;

	temp = nm_client_get_devices (applet->nm_client);
	for (i = 0; temp && (i < temp->len); i++)
		devices = g_slist_append (devices, g_ptr_array_index (temp, i));
	if (devices)
		devices = g_slist_sort (devices, sort_devices);

	for (iter = devices; iter; iter = iter->next) {
		NMDevice *device = NM_DEVICE (iter->data);

		/* Ignore unsupported devices */
		if (!(nm_device_get_capabilities (device) & NM_DEVICE_CAP_NM_SUPPORTED))
			continue;

		if (NM_IS_DEVICE_WIFI (device)) {
			n_wifi_devices++;
			if (   nm_client_wireless_get_enabled (applet->nm_client)
			    && (nm_device_get_state (device) >= NM_DEVICE_STATE_DISCONNECTED))
				n_usable_wifi_devices++;
		} else if (NM_IS_DEVICE_ETHERNET (device))
			n_wired_devices++;
		else if (NM_IS_CDMA_DEVICE (device))
			n_cdma_devices++;
		else if (NM_IS_GSM_DEVICE (device))
			n_gsm_devices++;
	}

	if (!n_wired_devices && !n_wifi_devices && !n_cdma_devices && !n_gsm_devices) {
		nma_menu_add_text_item (menu, _("No network devices available"));
		goto out;
	}

	/* Add all devices in our device list to the menu */
	for (iter = devices; iter; iter = iter->next) {
		NMDevice *device = NM_DEVICE (iter->data);
		gint n_devices = 0;
		NMADeviceClass *dclass;
		NMConnection *active;

		/* Ignore unsupported devices */
		if (!(nm_device_get_capabilities (device) & NM_DEVICE_CAP_NM_SUPPORTED))
			continue;

		if (NM_IS_DEVICE_WIFI (device))
			n_devices = n_wifi_devices;
		else if (NM_IS_DEVICE_ETHERNET (device))
			n_devices = n_wired_devices;
		else if (NM_IS_CDMA_DEVICE (device))
			n_devices = n_cdma_devices;
		else if (NM_IS_GSM_DEVICE (device))
			n_devices = n_gsm_devices;

		active = applet_find_active_connection_for_device (device, applet, NULL);

		dclass = get_device_class (device, applet);
		if (dclass)
			dclass->add_menu_item (device, n_devices, active, menu, applet);
	}

 out:
	g_slist_free (devices);

	/* Return # of usable wifi devices here for correct enable/disable state
	 * of things like Enable Wireless, "Connect to other..." and such.
	 */
	return n_usable_wifi_devices;
}

static int
sort_vpn_connections (gconstpointer a, gconstpointer b)
{
	return strcmp (get_connection_id (NM_CONNECTION (a)), get_connection_id (NM_CONNECTION (b)));
}

static GSList *
get_vpn_connections (NMApplet *applet)
{
	GSList *all_connections;
	GSList *iter;
	GSList *list = NULL;

	all_connections = applet_get_all_connections (applet);

	for (iter = all_connections; iter; iter = iter->next) {
		NMConnection *connection = NM_CONNECTION (iter->data);
		NMSettingConnection *s_con;

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		if (strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME))
			/* Not a VPN connection */
			continue;

		if (!nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN)) {
			const char *name = NM_SETTING (s_con)->name;
			g_warning ("%s: VPN connection '%s' didn't have requires vpn setting.", __func__, name);
			continue;
		}

		list = g_slist_prepend (list, connection);
	}

	g_slist_free (all_connections);

	return g_slist_sort (list, sort_vpn_connections);
}

static void
nma_menu_add_vpn_submenu (GtkWidget *menu, NMApplet *applet)
{
	GtkMenu *vpn_menu;
	GtkMenuItem *item;
	GSList *list, *iter;
	int num_vpn_active = 0;

	nma_menu_add_separator_item (menu);

	vpn_menu = GTK_MENU (gtk_menu_new ());

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_VPN Connections")));
	gtk_menu_item_set_submenu (item, GTK_WIDGET (vpn_menu));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), GTK_WIDGET (item));

	list = get_vpn_connections (applet);
	for (iter = list; iter; iter = g_slist_next (iter)) {
		NMConnection *connection = NM_CONNECTION (iter->data);

		if (applet_get_active_for_connection (applet, connection))
			num_vpn_active++;
	}

	for (iter = list; iter; iter = g_slist_next (iter)) {
		NMConnection *connection = NM_CONNECTION (iter->data);
		NMActiveConnection *active;
		const char *name;

		name = get_connection_id (connection);

		item = GTK_MENU_ITEM (gtk_check_menu_item_new_with_label (name));
		gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);

		/* If no VPN connections are active, draw all menu items enabled. If
		 * >= 1 VPN connections are active, only the active VPN menu item is
		 * drawn enabled.
		 */
		active = applet_get_active_for_connection (applet, connection);

		if (nm_client_get_state (applet->nm_client) != NM_STATE_CONNECTED)
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
		else if ((num_vpn_active == 0) || active)
			gtk_widget_set_sensitive (GTK_WIDGET (item), TRUE);
		else
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

		if (active)
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

		g_object_set_data_full (G_OBJECT (item), "connection", 
						    g_object_ref (connection),
						    (GDestroyNotify) g_object_unref);

		g_signal_connect (item, "activate", G_CALLBACK (nma_menu_vpn_item_clicked), applet);
		gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));
	}

	/* Draw a seperator, but only if we have VPN connections above it */
	if (list)
		nma_menu_add_separator_item (GTK_WIDGET (vpn_menu));

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Configure VPN...")));
	g_signal_connect (item, "activate", G_CALLBACK (nma_menu_configure_vpn_item_activate), applet);
	gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Disconnect VPN...")));
	g_signal_connect (item, "activate", G_CALLBACK (nma_menu_disconnect_vpn_item_activate), applet);
	gtk_menu_shell_append (GTK_MENU_SHELL (vpn_menu), GTK_WIDGET (item));
	if (num_vpn_active == 0)
		gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

	g_slist_free (list);
}


static void
nma_set_wireless_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_wireless_set_enabled (applet->nm_client, state);
}


static void
nma_set_networking_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_sleep (applet->nm_client, !state);
}

/*
 * nma_menu_show_cb
 *
 * Pop up the wireless networks menu
 *
 */
static void nma_menu_show_cb (GtkWidget *menu, NMApplet *applet)
{
	guint32 n_wireless;

	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);

	gtk_status_icon_set_tooltip (applet->status_icon, NULL);

	if (!nm_client_get_manager_running (applet->nm_client)) {
		nma_menu_add_text_item (menu, _("NetworkManager is not running..."));
		return;
	}

	if (nm_client_get_state (applet->nm_client) == NM_STATE_ASLEEP) {
		nma_menu_add_text_item (menu, _("Networking disabled"));
		return;
	}

	n_wireless = nma_menu_add_devices (menu, applet);

	nma_menu_add_vpn_submenu (menu, applet);

	if (n_wireless > 0 && nm_client_wireless_get_enabled (applet->nm_client)) {
		/* Add the "Other wireless network..." entry */
		nma_menu_add_separator_item (menu);
		nma_menu_add_other_network_item (menu, applet);
		nma_menu_add_create_network_item (menu, applet);
	}

	gtk_widget_show_all (menu);

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

static gboolean nma_menu_clear (NMApplet *applet);

static void
nma_menu_deactivate_cb (GtkWidget *widget, NMApplet *applet)
{
	/* Must punt the destroy to a low-priority idle to ensure that
	 * the menu items don't get destroyed before any 'activate' signal
	 * fires for an item.
	 */
	g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc) nma_menu_clear, applet, NULL);
}

/*
 * nma_menu_create
 *
 * Create the applet's dropdown menu
 *
 */
static GtkWidget *
nma_menu_create (NMApplet *applet)
{
	GtkWidget	*menu;

	g_return_val_if_fail (applet != NULL, NULL);

	menu = gtk_menu_new ();
	gtk_container_set_border_width (GTK_CONTAINER (menu), 0);
	g_signal_connect (menu, "show", G_CALLBACK (nma_menu_show_cb), applet);
	g_signal_connect (menu, "deactivate", G_CALLBACK (nma_menu_deactivate_cb), applet);
	return menu;
}

/*
 * nma_menu_clear
 *
 * Destroy the menu and each of its items data tags
 *
 */
static gboolean nma_menu_clear (NMApplet *applet)
{
	g_return_val_if_fail (applet != NULL, FALSE);

	if (applet->menu)
		gtk_widget_destroy (applet->menu);
	applet->menu = nma_menu_create (applet);
	return FALSE;
}


/*
 * nma_context_menu_update
 *
 */
static void
nma_context_menu_update (NMApplet *applet)
{
	NMState state;
	gboolean have_wireless = FALSE;
	gboolean wireless_hw_enabled;

	state = nm_client_get_state (applet->nm_client);

	gtk_widget_set_sensitive (applet->info_menu_item, state == NM_STATE_CONNECTED);

	/* Update checkboxes, and block 'toggled' signal when updating so that the
	 * callback doesn't get triggered.
	 */

	/* Enabled Networking */
	g_signal_handler_block (G_OBJECT (applet->networking_enabled_item),
	                        applet->networking_enabled_toggled_id);
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->networking_enabled_item),
	                                state != NM_STATE_ASLEEP);
	g_signal_handler_unblock (G_OBJECT (applet->networking_enabled_item),
	                          applet->networking_enabled_toggled_id);

	/* Enabled Wireless */
	g_signal_handler_block (G_OBJECT (applet->wifi_enabled_item),
	                        applet->wifi_enabled_toggled_id);
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->wifi_enabled_item),
	                                nm_client_wireless_get_enabled (applet->nm_client));
	g_signal_handler_unblock (G_OBJECT (applet->wifi_enabled_item),
	                          applet->wifi_enabled_toggled_id);

	wireless_hw_enabled = nm_client_wireless_hardware_get_enabled (applet->nm_client);
	gtk_widget_set_sensitive (GTK_WIDGET (applet->wifi_enabled_item),
	                          wireless_hw_enabled);

	/* Don't show wifi-specific stuff if wireless is off */
	if (state != NM_STATE_ASLEEP) {
		const GPtrArray *devices;
		int i;

		devices = nm_client_get_devices (applet->nm_client);
		for (i = 0; devices && (i < devices->len); i++) {
			if (NM_IS_DEVICE_WIFI (g_ptr_array_index (devices, i))) {
				have_wireless = TRUE;
				break;
			}
		}
	}

	if (have_wireless)
		gtk_widget_show_all (applet->wifi_enabled_item);
	else
		gtk_widget_hide (applet->wifi_enabled_item);
}

static void
ce_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	/* We are in the child process at this point */
	pid_t pid = getpid ();
	setpgid (pid, pid);
}

static void
nma_edit_connections_cb (GtkMenuItem *mi, NMApplet *applet)
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
 * nma_context_menu_create
 *
 * Generate the contextual popup menu.
 *
 */
static GtkWidget *nma_context_menu_create (NMApplet *applet)
{
	GtkMenuShell *menu;
	GtkWidget *menu_item;
	GtkWidget *image;
	guint id;

	g_return_val_if_fail (applet != NULL, NULL);

	menu = GTK_MENU_SHELL (gtk_menu_new ());

	/* 'Enable Networking' item */
	applet->networking_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Networking"));
	id = g_signal_connect (applet->networking_enabled_item,
	                       "toggled",
	                       G_CALLBACK (nma_set_networking_enabled_cb),
	                       applet);
	applet->networking_enabled_toggled_id = id;
	gtk_menu_shell_append (menu, applet->networking_enabled_item);

	/* 'Enable Wireless' item */
	applet->wifi_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Wireless"));
	id = g_signal_connect (applet->wifi_enabled_item,
	                       "toggled",
	                       G_CALLBACK (nma_set_wireless_enabled_cb),
	                       applet);
	applet->wifi_enabled_toggled_id = id;
	gtk_menu_shell_append (menu, applet->wifi_enabled_item);

	nma_menu_add_separator_item (GTK_WIDGET (menu));

	/* 'Connection Information' item */
	applet->info_menu_item = gtk_image_menu_item_new_with_mnemonic (_("Connection _Information"));
	g_signal_connect_swapped (applet->info_menu_item,
	                          "activate",
	                          G_CALLBACK (applet_connection_info_cb),
	                          applet);
	image = gtk_image_new_from_stock (GTK_STOCK_INFO, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->info_menu_item), image);
	gtk_menu_shell_append (menu, applet->info_menu_item);

	/* 'Edit Connections...' item */
	applet->connections_menu_item = gtk_image_menu_item_new_with_mnemonic (_("Edit Connections..."));
	g_signal_connect (applet->connections_menu_item,
				   "activate",
				   G_CALLBACK (nma_edit_connections_cb),
				   applet);
	image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->connections_menu_item), image);
	gtk_menu_shell_append (menu, applet->connections_menu_item);

	/* Separator */
	nma_menu_add_separator_item (GTK_WIDGET (menu));

#if 0	/* FIXME: Implement the help callback, nma_help_cb()! */
	/* Help item */
	menu_item = gtk_image_menu_item_new_with_mnemonic (_("_Help"));
	g_signal_connect (menu_item, "activate", G_CALLBACK (nma_help_cb), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_HELP, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (menu, menu_item);
	gtk_widget_set_sensitive (menu_item, FALSE);
#endif

	/* About item */
	menu_item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (applet_about_dialog_show), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (menu, menu_item);

	gtk_widget_show_all (GTK_WIDGET (menu));

	return GTK_WIDGET (menu);
}


/*****************************************************************************/

static void
foo_set_icon (NMApplet *applet, GdkPixbuf *pixbuf, guint32 layer)
{
	int i;

	if (layer > ICON_LAYER_MAX) {
		g_warning ("Tried to icon to invalid layer %d", layer);
		return;
	}

	/* Ignore setting of the same icon as is already displayed */
	if (applet->icon_layers[layer] == pixbuf)
		return;

	if (applet->icon_layers[layer]) {
		g_object_unref (applet->icon_layers[layer]);
		applet->icon_layers[layer] = NULL;
	}

	if (pixbuf)
		applet->icon_layers[layer] = g_object_ref (pixbuf);

	if (!applet->icon_layers[0]) {
		pixbuf = g_object_ref (applet->no_connection_icon);
	} else {
		pixbuf = gdk_pixbuf_copy (applet->icon_layers[0]);

		for (i = ICON_LAYER_LINK + 1; i <= ICON_LAYER_MAX; i++) {
			GdkPixbuf *top = applet->icon_layers[i];

			if (!top)
				continue;

			gdk_pixbuf_composite (top, pixbuf, 0, 0, gdk_pixbuf_get_width (top),
							  gdk_pixbuf_get_height (top),
							  0, 0, 1.0, 1.0,
							  GDK_INTERP_NEAREST, 255);
		}
	}

	gtk_status_icon_set_from_pixbuf (applet->status_icon, pixbuf);
	g_object_unref (pixbuf);
}


NMAGConfConnection *
applet_get_exported_connection_for_device (NMDevice *device, NMApplet *applet)
{
	const GPtrArray *active_connections;
	int i;

	active_connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; active_connections && (i < active_connections->len); i++) {
		NMActiveConnection *active;
		NMAGConfConnection *gconf_connection;
		const char *service_name;
		const char *connection_path;
		const GPtrArray *devices;

		active = g_ptr_array_index (active_connections, i);
		devices = nm_active_connection_get_devices (active);
		service_name = nm_active_connection_get_service_name (active);
		connection_path = nm_active_connection_get_connection (active);

		if (strcmp (service_name, NM_DBUS_SERVICE_USER_SETTINGS) != 0)
			continue;

		if (!nm_g_ptr_array_contains (devices, device))
			continue;

		gconf_connection = nma_gconf_settings_get_by_dbus_path (applet->gconf_settings, connection_path);
		if (!gconf_connection || !nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (gconf_connection)))
			continue;

		return gconf_connection;
	}
	return NULL;
}

static void
applet_common_device_state_changed (NMDevice *device,
                                    NMDeviceState state,
                                    NMApplet *applet)
{
	NMConnection *connection;
	NMActiveConnection *active = NULL;

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
	case NM_DEVICE_STATE_NEED_AUTH:
	case NM_DEVICE_STATE_IP_CONFIG:
		start_animation_timeout (applet);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		/* If the device activation was successful, update the corresponding
		 * connection object with a current timestamp.
		 */
		connection = applet_find_active_connection_for_device (device, applet, &active);
		if (connection && active)
			update_connection_timestamp (active, connection, applet);
		/* Fall through */
	default:
		clear_animation_timeout (applet);
		break;
	}
}

static void
foo_device_state_changed_cb (NMDevice *device, GParamSpec *pspec, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMADeviceClass *dclass;
	NMDeviceState state;

	dclass = get_device_class (device, applet);
	g_assert (dclass);

	state = nm_device_get_state (device);
	dclass->device_state_changed (device, state, applet);
	applet_common_device_state_changed (device, state, applet);

	applet_schedule_update_icon (applet);
}

static void
foo_device_added_cb (NMClient *client, NMDevice *device, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMADeviceClass *dclass;

	dclass = get_device_class (device, applet);
	g_return_if_fail (dclass != NULL);

	if (dclass->device_added)
		dclass->device_added (device, applet);

	g_signal_connect (device, "notify::state",
				   G_CALLBACK (foo_device_state_changed_cb),
				   user_data);

	foo_device_state_changed_cb	(device, NULL, applet);
}

static void
foo_client_state_changed_cb (NMClient *client, GParamSpec *pspec, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	switch (nm_client_get_state (client)) {
	case NM_STATE_DISCONNECTED:
		applet_do_notify (applet, NOTIFY_URGENCY_NORMAL, _("Disconnected"),
						  _("The network connection has been disconnected."),
						  "nm-no-connection", NULL, NULL, NULL, NULL);
		/* Fall through */
	default:
		break;
	}

	applet_schedule_update_icon (applet);
}

static void
foo_manager_running_cb (NMClient *client,
                        GParamSpec *pspec,
                        gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	if (nm_client_get_manager_running (client)) {
		g_message ("NM appeared");
	} else {
		g_message ("NM disappeared");
		clear_animation_timeout (applet);
	}

	applet_schedule_update_icon (applet);
}

#define VPN_STATE_ID_TAG "vpn-state-id"

static void
foo_active_connections_changed_cb (NMClient *client,
                                   GParamSpec *pspec,
                                   gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	const GPtrArray *active_list;
	int i;

	/* Track the state of new VPN connections */
	active_list = nm_client_get_active_connections (client);
	for (i = 0; active_list && (i < active_list->len); i++) {
		NMActiveConnection *candidate = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_list, i));
		guint id;

		if (   !NM_IS_VPN_CONNECTION (candidate)
		    || g_object_get_data (G_OBJECT (candidate), VPN_STATE_ID_TAG))
			continue;

		id = g_signal_connect (G_OBJECT (candidate), "vpn-state-changed",
		                       G_CALLBACK (vpn_connection_state_changed), applet);
		g_object_set_data (G_OBJECT (candidate), VPN_STATE_ID_TAG, GUINT_TO_POINTER (id));
	}

	applet_schedule_update_icon (applet);
}

static gboolean
foo_set_initial_state (gpointer data)
{
	NMApplet *applet = NM_APPLET (data);
	const GPtrArray *devices;
	int i;

	devices = nm_client_get_devices (applet->nm_client);
	for (i = 0; devices && (i < devices->len); i++)
		foo_device_added_cb (applet->nm_client, NM_DEVICE (g_ptr_array_index (devices, i)), applet);

	foo_active_connections_changed_cb (applet->nm_client, NULL, applet);

	applet_schedule_update_icon (applet);

	return FALSE;
}

static void
foo_client_setup (NMApplet *applet)
{
	applet->nm_client = nm_client_new ();
	if (!applet->nm_client)
		return;

	g_signal_connect (applet->nm_client, "notify::state",
	                  G_CALLBACK (foo_client_state_changed_cb),
	                  applet);
	g_signal_connect (applet->nm_client, "notify::active-connections",
	                  G_CALLBACK (foo_active_connections_changed_cb),
	                  applet);
	g_signal_connect (applet->nm_client, "device-added",
	                  G_CALLBACK (foo_device_added_cb),
	                  applet);
	g_signal_connect (applet->nm_client, "notify::manager-running",
	                  G_CALLBACK (foo_manager_running_cb),
	                  applet);

	if (nm_client_get_manager_running (applet->nm_client))
		g_idle_add (foo_set_initial_state, applet);
}

static GdkPixbuf *
applet_common_get_device_icon (NMDeviceState state, NMApplet *applet)
{
	GdkPixbuf *pixbuf = NULL;
	int stage = -1;

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		stage = 0;
		break;
	case NM_DEVICE_STATE_CONFIG:
		stage = 1;
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		stage = 2;
		break;
	default:
		break;
	}

	if (stage >= 0) {
		pixbuf = applet->network_connecting_icons[stage][applet->animation_step];
		applet->animation_step++;
		if (applet->animation_step >= NUM_CONNECTING_FRAMES)
			applet->animation_step = 0;
	}

	return pixbuf;
}

static GdkPixbuf *
applet_get_device_icon_for_state (NMApplet *applet, char **tip)
{
	NMActiveConnection *active;
	NMDevice *device = NULL;
	GdkPixbuf *pixbuf = NULL;
	NMDeviceState state = NM_DEVICE_STATE_UNKNOWN;
	NMADeviceClass *dclass;

	// FIXME: handle multiple device states here

	/* First show the best activating device's state */
	active = applet_get_best_activating_connection (applet, &device);
	if (!active || !device) {
		/* If there aren't any activating devices, then show the state of
		 * the default active connection instead.
		 */
		active = applet_get_default_active_connection (applet, &device);
		if (!active || !device)
			goto out;
	}

	state = nm_device_get_state (device);

	dclass = get_device_class (device, applet);
	if (dclass)
		pixbuf = dclass->get_icon (device, state, tip, applet);

out:
	if (!pixbuf)
		pixbuf = applet_common_get_device_icon (state, applet);
	return pixbuf;
}

static gboolean
applet_update_icon (gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GdkPixbuf *pixbuf = NULL;
	NMState state;
	char *tip = NULL;
	NMVPNConnectionState vpn_state = NM_VPN_SERVICE_STATE_UNKNOWN;
	gboolean nm_running;

	applet->update_icon_id = 0;

	nm_running = nm_client_get_manager_running (applet->nm_client);
	gtk_status_icon_set_visible (applet->status_icon, nm_running);

	/* Handle device state first */

	state = nm_client_get_state (applet->nm_client);
	if (!nm_running)
		state = NM_STATE_UNKNOWN;

	switch (state) {
	case NM_STATE_UNKNOWN:
	case NM_STATE_ASLEEP:
		pixbuf = applet->no_connection_icon;
		tip = g_strdup (_("Networking disabled"));
		break;
	case NM_STATE_DISCONNECTED:
		pixbuf = applet->no_connection_icon;
		tip = g_strdup (_("No network connection"));
		break;
	default:
		pixbuf = applet_get_device_icon_for_state (applet, &tip);
		break;
	}

	foo_set_icon (applet, pixbuf, ICON_LAYER_LINK);
	if (tip) {
		gtk_status_icon_set_tooltip (applet->status_icon, tip);
		g_free (tip);
	}

	/* VPN state next */
	pixbuf = NULL;
	if (applet_get_first_active_vpn_connection (applet, &vpn_state)) {
		switch (vpn_state) {
		case NM_VPN_CONNECTION_STATE_ACTIVATED:
			pixbuf = applet->vpn_lock_icon;
			break;
		case NM_VPN_CONNECTION_STATE_PREPARE:
		case NM_VPN_CONNECTION_STATE_NEED_AUTH:
		case NM_VPN_CONNECTION_STATE_CONNECT:
		case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
			pixbuf = applet->vpn_connecting_icons[applet->animation_step];
			applet->animation_step++;
			if (applet->animation_step >= NUM_VPN_CONNECTING_FRAMES)
				applet->animation_step = 0;
			break;
		default:
			break;
		}
	}
	foo_set_icon (applet, pixbuf, ICON_LAYER_VPN);

	return FALSE;
}

void
applet_schedule_update_icon (NMApplet *applet)
{
	if (!applet->update_icon_id)
		applet->update_icon_id = g_idle_add (applet_update_icon, applet);
}

static NMDevice *
find_active_device (NMAGConfConnection *exported,
                    NMApplet *applet,
                    const char **out_specific_object)
{
	const GPtrArray *active_connections;
	int i;

	g_return_val_if_fail (exported != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	/* Look through the active connection list trying to find the D-Bus
	 * object path of applet_connection.
	 */
	active_connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; active_connections && (i < active_connections->len); i++) {
		NMActiveConnection *active;
		NMConnection *connection;
		const char *service_name;
		const char *connection_path;
		const char *specific_object;
		const GPtrArray *devices;

		active = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_connections, i));
		service_name = nm_active_connection_get_service_name (active);
		if (strcmp (service_name, NM_DBUS_SERVICE_USER_SETTINGS))
			continue;

		connection_path = nm_active_connection_get_connection (active);
		specific_object = nm_active_connection_get_specific_object (active);

		connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
		if (!strcmp (connection_path, nm_connection_get_path (connection))) {
			devices = nm_active_connection_get_devices (active);
			*out_specific_object = specific_object;
			return devices ? NM_DEVICE (g_ptr_array_index (devices, 0)) : NULL;
		}
	}

	return NULL;
}

static void
applet_settings_new_secrets_requested_cb (NMAGConfSettings *settings,
                                          NMAGConfConnection *exported,
                                          const char *setting_name,
                                          const char **hints,
                                          gboolean ask_user,
                                          DBusGMethodInvocation *context,
                                          gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMDevice *device;
	NMADeviceClass *dclass;
	GError *error = NULL;
	const char *specific_object = NULL;

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (exported));
	g_return_if_fail (connection != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_return_if_fail (s_con != NULL);
	g_return_if_fail (s_con->type != NULL);

	/* VPN secrets get handled a bit differently */
	if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME)) {
		nma_vpn_request_password (NM_EXPORTED_CONNECTION (exported), ask_user, context);
		return;
	}

	/* Find the active device for this connection */
	device = find_active_device (exported, applet, &specific_object);
	if (!device) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't find details for connection",
		             __FILE__, __LINE__, __func__);
		goto error;
	}

	dclass = get_device_class (device, applet);
	if (!dclass) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): device type unknown",
		             __FILE__, __LINE__, __func__);
		goto error;
	}

	if (!dclass->get_secrets) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): no secrets found",
		             __FILE__, __LINE__, __func__);
		goto error;
	}

	/* Let the device class handle secrets */
	if (!dclass->get_secrets (device, connection, specific_object, setting_name,
	                          hints, context, applet, &error))
		goto error;

	return;

error:
	g_warning ("%s", error->message);
	dbus_g_method_return_error (context, error);
	g_error_free (error);
}

static gboolean
periodic_update_active_connection_timestamps (gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	const GPtrArray *connections;
	int i;

	if (!applet->nm_client || !nm_client_get_manager_running (applet->nm_client))
		return TRUE;

	connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; connections && (i < connections->len); i++) {
		NMActiveConnection *active = NM_ACTIVE_CONNECTION (g_ptr_array_index (connections, i));
		const char *path;
		NMAGConfConnection *gconf_connection;
		NMConnection *connection;
		const GPtrArray *devices;
		int k;

		if (nm_active_connection_get_scope (active) == NM_CONNECTION_SCOPE_SYSTEM)
			continue;

		path = nm_active_connection_get_connection (active);
		gconf_connection = nma_gconf_settings_get_by_dbus_path (applet->gconf_settings, path);
		if (!gconf_connection)
			continue;

		devices = nm_active_connection_get_devices (active);
		if (!devices || !devices->len)
			continue;

		/* Check if a device owned by the active connection is completely
		 * activated before updating timestamp.
		 */
		for (k = 0; devices && (k < devices->len); k++) {
			NMDevice *device = NM_DEVICE (g_ptr_array_index (devices, k));

			if (nm_device_get_state (device) == NM_DEVICE_STATE_ACTIVATED) {
				NMSettingConnection *s_con;

				connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (gconf_connection));
				s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
				g_assert (s_con);

				s_con->timestamp = (guint64) time (NULL);
				nma_gconf_connection_save (gconf_connection);
				break;
			}
		}
	}

	return TRUE;
}

/*****************************************************************************/

#define CLEAR_ICON(x) \
	if (x) { \
		g_object_unref (x); \
		x = NULL; \
	}

static void nma_icons_free (NMApplet *applet)
{
	int i, j;

	if (!applet->icons_loaded)
		return;

	for (i = 0; i <= ICON_LAYER_MAX; i++)
		CLEAR_ICON(applet->icon_layers[i]);

	CLEAR_ICON(applet->no_connection_icon);
	CLEAR_ICON(applet->wired_icon);
	CLEAR_ICON(applet->adhoc_icon);
	CLEAR_ICON(applet->wwan_icon);
	CLEAR_ICON(applet->vpn_lock_icon);
	CLEAR_ICON(applet->wireless_00_icon);
	CLEAR_ICON(applet->wireless_25_icon);
	CLEAR_ICON(applet->wireless_50_icon);
	CLEAR_ICON(applet->wireless_75_icon);
	CLEAR_ICON(applet->wireless_100_icon);

	for (i = 0; i < NUM_CONNECTING_STAGES; i++) {
		for (j = 0; j < NUM_CONNECTING_FRAMES; j++)
			CLEAR_ICON(applet->network_connecting_icons[i][j]);
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++)
		CLEAR_ICON(applet->vpn_connecting_icons[i]);

	for (i = 0; i <= ICON_LAYER_MAX; i++)
		CLEAR_ICON(applet->icon_layers[i]);

	applet->icons_loaded = FALSE;
}

#define ICON_LOAD(x, y)	\
	{ \
		x = gtk_icon_theme_load_icon (applet->icon_theme, y, applet->size, 0, &err); \
		if (x == NULL) { \
			g_warning ("Icon %s missing: %s", y, err->message); \
			g_error_free (err); \
			goto out; \
		} \
	}

static gboolean
nma_icons_load (NMApplet *applet)
{
	int i, j;
	GError *err = NULL;

	g_return_val_if_fail (!applet->icons_loaded, FALSE);

	if (applet->size < 0)
		return FALSE;

	ICON_LOAD(applet->no_connection_icon, "nm-no-connection");
	ICON_LOAD(applet->wired_icon, "nm-device-wired");
	ICON_LOAD(applet->adhoc_icon, "nm-adhoc");
	ICON_LOAD(applet->wwan_icon, "nm-device-wwan");
	ICON_LOAD(applet->vpn_lock_icon, "nm-vpn-active-lock");

	ICON_LOAD(applet->wireless_00_icon, "nm-signal-00");
	ICON_LOAD(applet->wireless_25_icon, "nm-signal-25");
	ICON_LOAD(applet->wireless_50_icon, "nm-signal-50");
	ICON_LOAD(applet->wireless_75_icon, "nm-signal-75");
	ICON_LOAD(applet->wireless_100_icon, "nm-signal-100");

	for (i = 0; i < NUM_CONNECTING_STAGES; i++) {
		for (j = 0; j < NUM_CONNECTING_FRAMES; j++) {
			char *name;

			name = g_strdup_printf ("nm-stage%02d-connecting%02d", i+1, j+1);
			ICON_LOAD(applet->network_connecting_icons[i][j], name);
			g_free (name);
		}
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++) {
		char *name;

		name = g_strdup_printf ("nm-vpn-connecting%02d", i+1);
		ICON_LOAD(applet->vpn_connecting_icons[i], name);
		g_free (name);
	}

	applet->icons_loaded = TRUE;

out:
	if (!applet->icons_loaded) {
		applet_warning_dialog_show (_("The NetworkManager applet could not find some required resources.  It cannot continue.\n"));
		nma_icons_free (applet);
	}

	return applet->icons_loaded;
}

static void nma_icon_theme_changed (GtkIconTheme *icon_theme, NMApplet *applet)
{
	nma_icons_free (applet);
	nma_icons_load (applet);
}

static void nma_icons_init (NMApplet *applet)
{
	GdkScreen *screen;
	gboolean path_appended;

	if (applet->icon_theme) {
		g_signal_handlers_disconnect_by_func (applet->icon_theme,
						      G_CALLBACK (nma_icon_theme_changed),
						      applet);
		g_object_unref (G_OBJECT (applet->icon_theme));
	}

#if GTK_CHECK_VERSION(2, 11, 0)
	screen = gtk_status_icon_get_screen (applet->status_icon);
#else
	screen = gdk_screen_get_default ();
#endif /* gtk 2.11.0 */
	
	applet->icon_theme = gtk_icon_theme_get_for_screen (screen);

	/* If not done yet, append our search path */
	path_appended = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (applet->icon_theme),
					 		    "NMAIconPathAppended"));
	if (path_appended == FALSE) {
		gtk_icon_theme_append_search_path (applet->icon_theme, ICONDIR);
		g_object_set_data (G_OBJECT (applet->icon_theme),
				   "NMAIconPathAppended",
				   GINT_TO_POINTER (TRUE));
	}

	g_signal_connect (applet->icon_theme, "changed", G_CALLBACK (nma_icon_theme_changed), applet);
}

static void
status_icon_screen_changed_cb (GtkStatusIcon *icon,
                               GParamSpec *pspec,
                               NMApplet *applet)
{
	nma_icons_init (applet);
	nma_icon_theme_changed (NULL, applet);
}

static gboolean
status_icon_size_changed_cb (GtkStatusIcon *icon,
                             gint size,
                             NMApplet *applet)
{
	applet->size = size;

	nma_icons_free (applet);
	nma_icons_load (applet);

	applet_schedule_update_icon (applet);

	return TRUE;
}

static void
status_icon_activate_cb (GtkStatusIcon *icon, NMApplet *applet)
{
	/* Have clicking on the applet act also as acknowledgement
	 * of the notification. 
	 */
	applet_clear_notify (applet);

	nma_menu_clear (applet);
	gtk_menu_popup (GTK_MENU (applet->menu), NULL, NULL,
			gtk_status_icon_position_menu, icon,
			1, gtk_get_current_event_time ());
}

static void
status_icon_popup_menu_cb (GtkStatusIcon *icon,
                           guint button,
                           guint32 activate_time,
                           NMApplet *applet)
{
	/* Have clicking on the applet act also as acknowledgement
	 * of the notification. 
	 */
	applet_clear_notify (applet);

	nma_context_menu_update (applet);
	gtk_menu_popup (GTK_MENU (applet->context_menu), NULL, NULL,
			gtk_status_icon_position_menu, icon,
			button, activate_time);
}

static gboolean
setup_widgets (NMApplet *applet)
{
	g_return_val_if_fail (NM_IS_APPLET (applet), FALSE);

	applet->status_icon = gtk_status_icon_new ();
	if (!applet->status_icon)
		return FALSE;

	g_signal_connect (applet->status_icon, "notify::screen",
			  G_CALLBACK (status_icon_screen_changed_cb), applet);
	g_signal_connect (applet->status_icon, "size-changed",
			  G_CALLBACK (status_icon_size_changed_cb), applet);
	g_signal_connect (applet->status_icon, "activate",
			  G_CALLBACK (status_icon_activate_cb), applet);
	g_signal_connect (applet->status_icon, "popup-menu",
			  G_CALLBACK (status_icon_popup_menu_cb), applet);

	applet->menu = nma_menu_create (applet);
	if (!applet->menu)
		return FALSE;

	applet->context_menu = nma_context_menu_create (applet);
	if (!applet->context_menu)
		return FALSE;
	applet->encryption_size_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);
	if (!applet->encryption_size_group)
		return FALSE;

	return TRUE;
}

static GObject *
constructor (GType type,
             guint n_props,
             GObjectConstructParam *construct_props)
{
	NMApplet *applet;
	AppletDBusManager * dbus_mgr;

	applet = NM_APPLET (G_OBJECT_CLASS (nma_parent_class)->constructor (type, n_props, construct_props));

	g_set_application_name (_("NetworkManager Applet"));
	gtk_window_set_default_icon_name (GTK_STOCK_NETWORK);

	applet->glade_file = g_build_filename (GLADEDIR, "applet.glade", NULL);
	if (!applet->glade_file || !g_file_test (applet->glade_file, G_FILE_TEST_IS_REGULAR)) {
		applet_warning_dialog_show (_("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		goto error;
	}

	applet->info_dialog_xml = glade_xml_new (applet->glade_file, "info_dialog", NULL);
	if (!applet->info_dialog_xml)
        goto error;

	applet->gconf_client = gconf_client_get_default ();
	if (!applet->gconf_client)
	    goto error;

	/* Load pixmaps and create applet widgets */
	if (!setup_widgets (applet))
	    goto error;
	nma_icons_init (applet);

	if (!notify_is_initted ())
		notify_init ("NetworkManager");

	dbus_mgr = applet_dbus_manager_get ();
	if (dbus_mgr == NULL) {
		nm_warning ("Couldn't initialize the D-Bus manager.");
		g_object_unref (applet);
		return NULL;
	}

	applet->dbus_settings = (NMDBusSettings *) nm_dbus_settings_system_new (applet_dbus_manager_get_connection (dbus_mgr));

	applet->gconf_settings = nma_gconf_settings_new ();
	g_signal_connect (applet->gconf_settings, "new-secrets-requested",
	                  G_CALLBACK (applet_settings_new_secrets_requested_cb),
	                  applet);

	dbus_g_connection_register_g_object (applet_dbus_manager_get_connection (dbus_mgr),
										 NM_DBUS_PATH_SETTINGS,
										 G_OBJECT (applet->gconf_settings));

    /* Start our DBus service */
    if (!applet_dbus_manager_start_service (dbus_mgr)) {
		g_object_unref (applet);
		return NULL;
    }

	/* Initialize device classes */
	applet->wired_class = applet_device_wired_get_class (applet);
	g_assert (applet->wired_class);

	applet->wifi_class = applet_device_wifi_get_class (applet);
	g_assert (applet->wifi_class);

	applet->gsm_class = applet_device_gsm_get_class (applet);
	g_assert (applet->gsm_class);

	applet->cdma_class = applet_device_cdma_get_class (applet);
	g_assert (applet->cdma_class);

	foo_client_setup (applet);

	/* timeout to update connection timestamps every 5 minutes */
	applet->update_timestamps_id = g_timeout_add (300000,
			(GSourceFunc) periodic_update_active_connection_timestamps, applet);

	return G_OBJECT (applet);

error:
	g_object_unref (applet);
	return NULL;
}

static void finalize (GObject *object)
{
	NMApplet *applet = NM_APPLET (object);

	if (applet->update_timestamps_id)
		g_source_remove (applet->update_timestamps_id);

	g_slice_free (NMADeviceClass, applet->wired_class);
	g_slice_free (NMADeviceClass, applet->wifi_class);
	g_slice_free (NMADeviceClass, applet->gsm_class);

	if (applet->update_icon_id)
		g_source_remove (applet->update_icon_id);

	nma_menu_clear (applet);
	nma_icons_free (applet);

	if (applet->notification) {
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}

	g_free (applet->glade_file);
	if (applet->info_dialog_xml)
		g_object_unref (applet->info_dialog_xml);

	g_object_unref (applet->gconf_settings);
	g_object_unref (applet->dbus_settings);

	g_object_unref (applet->gconf_client);

	if (applet->status_icon)
		g_object_unref (applet->status_icon);

	g_object_unref (applet->nm_client);

	G_OBJECT_CLASS (nma_parent_class)->finalize (object);
}

static void nma_init (NMApplet *applet)
{
	applet->animation_id = 0;
	applet->animation_step = 0;
	applet->icon_theme = NULL;
	applet->notification = NULL;
	applet->size = -1;
}

static void nma_class_init (NMAppletClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->constructor = constructor;
	gobject_class->finalize = finalize;
}

NMApplet *
nm_applet_new ()
{
	return g_object_new (NM_TYPE_APPLET, NULL);
}

