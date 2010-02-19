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
 * (C) Copyright 2008 Red Hat, Inc.
 * (C) Copyright 2008 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <nm-device.h>
#include <nm-setting-connection.h>
#include <nm-setting-bluetooth.h>
#include <nm-setting-cdma.h>
#include <nm-setting-gsm.h>
#include <nm-device-bt.h>
#include <nm-utils.h>

#include "applet.h"
#include "applet-device-bt.h"
#include "wired-dialog.h"
#include "utils.h"
#include "gconf-helpers.h"
#include "applet-dialogs.h"

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMConnection *connection;
} BtMenuItemInfo;

static void
bt_menu_item_info_destroy (gpointer data)
{
	BtMenuItemInfo *info = data;

	g_object_unref (G_OBJECT (info->device));
	if (info->connection)
		g_object_unref (G_OBJECT (info->connection));

	g_slice_free (BtMenuItemInfo, data);
}

static gboolean
bt_new_auto_connection (NMDevice *device,
                        gpointer dclass_data,
                        AppletNewAutoConnectionCallback callback,
                        gpointer callback_data)
{

	// FIXME: call gnome-bluetooth setup wizard
	return FALSE;
}

static void
bt_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	BtMenuItemInfo *info = user_data;

	applet_menu_item_activate_helper (info->device,
	                                  info->connection,
	                                  "/",
	                                  info->applet,
	                                  user_data);
}


typedef enum {
	ADD_ACTIVE = 1,
	ADD_INACTIVE = 2,
} AddActiveInactiveEnum;

static void
add_connection_items (NMDevice *device,
                      GSList *connections,
                      NMConnection *active,
                      AddActiveInactiveEnum flag,
                      GtkWidget *menu,
                      NMApplet *applet)
{
	GSList *iter;
	BtMenuItemInfo *info;

	for (iter = connections; iter; iter = g_slist_next (iter)) {
		NMConnection *connection = NM_CONNECTION (iter->data);
		GtkWidget *item;

		if (active == connection) {
			if ((flag & ADD_ACTIVE) == 0)
				continue;
		} else {
			if ((flag & ADD_INACTIVE) == 0)
				continue;
		}

		item = applet_new_menu_item_helper (connection, active, (flag & ADD_ACTIVE));

		info = g_slice_new0 (BtMenuItemInfo);
		info->applet = applet;
		info->device = g_object_ref (G_OBJECT (device));
		info->connection = g_object_ref (connection);

		g_signal_connect_data (item, "activate",
		                       G_CALLBACK (bt_menu_item_activate),
		                       info,
		                       (GClosureNotify) bt_menu_item_info_destroy, 0);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}
}

static void
bt_add_menu_item (NMDevice *device,
                  guint32 n_devices,
                  NMConnection *active,
                  GtkWidget *menu,
                  NMApplet *applet)
{
	const char *text;
	GtkWidget *item;
	GSList *connections, *all;

	all = applet_get_all_connections (applet);
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	text = nm_device_bt_get_name (NM_DEVICE_BT (device));
	if (!text) {
		text = utils_get_device_description (device);
		if (!text)
			text = nm_device_get_iface (device);
		g_assert (text);
	}

	item = applet_menu_item_create_device_item_helper (device, applet, text);

	gtk_widget_set_sensitive (item, FALSE);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	if (g_slist_length (connections))
		add_connection_items (device, connections, active, ADD_ACTIVE, menu, applet);

	/* Notify user of unmanaged or unavailable device */
	item = nma_menu_device_get_menu_item (device, applet, NULL);
	if (item) {
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	if (!nma_menu_device_check_unusable (device)) {
		/* Add menu items for existing bluetooth connections for this device */
		if (g_slist_length (connections)) {
			applet_menu_item_add_complex_separator_helper (menu, applet, _("Available"), -1);
			add_connection_items (device, connections, active, ADD_INACTIVE, menu, applet);
		}
	}

	g_slist_free (connections);
}

static void
bt_device_state_changed (NMDevice *device,
                         NMDeviceState new_state,
                         NMDeviceState old_state,
                         NMDeviceStateReason reason,
                         NMApplet *applet)
{
	if (new_state == NM_DEVICE_STATE_ACTIVATED) {
		NMConnection *connection;
		NMSettingConnection *s_con = NULL;
		char *str = NULL;

		connection = applet_find_active_connection_for_device (device, applet, NULL);
		if (connection) {
			const char *id;
			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			id = s_con ? nm_setting_connection_get_id (s_con) : NULL;
			if (id)
				str = g_strdup_printf (_("You are now connected to '%s'."), id);
		}

		applet_do_notify_with_pref (applet,
		                            _("Connection Established"),
		                            str ? str : _("You are now connected to the mobile broadband network."),
		                            "nm-device-wwan",
		                            PREF_DISABLE_CONNECTED_NOTIFICATIONS);
		g_free (str);
	}
}

static GdkPixbuf *
bt_get_icon (NMDevice *device,
             NMDeviceState state,
             NMConnection *connection,
             char **tip,
             NMApplet *applet)
{
	NMSettingConnection *s_con;
	GdkPixbuf *pixbuf = NULL;
	const char *id;

	id = nm_device_get_iface (NM_DEVICE (device));
	if (connection) {
		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		id = nm_setting_connection_get_id (s_con);
	}

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		*tip = g_strdup_printf (_("Preparing mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_CONFIG:
		*tip = g_strdup_printf (_("Configuring mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_NEED_AUTH:
		*tip = g_strdup_printf (_("User authentication required for mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		*tip = g_strdup_printf (_("Requesting a network address for '%s'..."), id);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		pixbuf = applet->wwan_icon;
		*tip = g_strdup_printf (_("Mobile broadband connection '%s' active"), id);
		break;
	default:
		break;
	}

	return pixbuf;
}

typedef struct {
	NMANewSecretsRequestedFunc callback;
	gpointer callback_data;
	NMApplet *applet;
	NMSettingsConnectionInterface *connection;
	NMActiveConnection *active_connection;
	GtkWidget *dialog;
	GtkEntry *secret_entry;
	char *secret_name;
	char *setting_name;
} NMBtSecretsInfo;

static void
destroy_secrets_dialog (gpointer user_data, GObject *finalized)
{
	NMBtSecretsInfo *info = user_data;

	gtk_widget_hide (info->dialog);
	gtk_widget_destroy (info->dialog);

	g_object_unref (info->connection);
	g_free (info->secret_name);
	g_free (info->setting_name);
	g_free (info);
}

static void
update_cb (NMSettingsConnectionInterface *connection,
           GError *error,
           gpointer user_data)
{
	if (error) {
		g_warning ("%s: failed to update connection: (%d) %s",
		           __func__, error->code, error->message);
	}
}

static void
get_bt_secrets_cb (GtkDialog *dialog,
                   gint response,
                   gpointer user_data)
{
	NMBtSecretsInfo *info = user_data;
	NMSetting *setting;
	GHashTable *settings_hash;
	GHashTable *secrets;
	GError *err = NULL;

	/* Got a user response, clear the NMActiveConnection destroy handler for
	 * this dialog since this function will now take over dialog destruction.
	 */
	g_object_weak_unref (G_OBJECT (info->active_connection), destroy_secrets_dialog, info);

	if (response != GTK_RESPONSE_OK) {
		g_set_error (&err,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): canceled",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	setting = nm_connection_get_setting_by_name (NM_CONNECTION (info->connection), info->setting_name);
	if (!setting) {
		g_set_error (&err,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): unhandled setting '%s'",
		             __FILE__, __LINE__, __func__, info->setting_name);
		goto done;
	}

	/* Normally we'd want to get all the settings's secrets and return those
	 * to NM too (since NM wants them), but since the only other secrets for 3G
	 * connections are PINs, and since the phone obviously has to be unlocked
	 * to even make the Bluetooth connection, we can skip doing that here for
	 * Bluetooth devices.
	 */

	/* Update the password */
	g_object_set (G_OBJECT (setting),
	              info->secret_name, gtk_entry_get_text (info->secret_entry),
	              NULL);

	secrets = nm_setting_to_hash (NM_SETTING (setting));
	if (!secrets) {
		g_set_error (&err,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): failed to hash setting '%s'.",
		             __FILE__, __LINE__, __func__,
		             nm_setting_get_name (NM_SETTING (setting)));
		goto done;
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                       g_free, (GDestroyNotify) g_hash_table_destroy);

	g_hash_table_insert (settings_hash, g_strdup (nm_setting_get_name (NM_SETTING (setting))), secrets);
	info->callback (info->connection, settings_hash, NULL, info->callback_data);
	g_hash_table_destroy (settings_hash);

	/* Save the connection back to GConf _after_ hashing it, because
	 * saving to GConf might trigger the GConf change notifiers, resulting
	 * in the connection being read back in from GConf which clears secrets.
	 */
	if (NMA_IS_GCONF_CONNECTION (info->connection))
		nm_settings_connection_interface_update (info->connection, update_cb, NULL);

 done:
	if (err) {
		g_warning ("%s", err->message);
		info->callback (info->connection, NULL, err, info->callback_data);
		g_error_free (err);
	}

	nm_connection_clear_secrets (NM_CONNECTION (info->connection));
	destroy_secrets_dialog (info, NULL);
}

static gboolean
bt_get_secrets (NMDevice *device,
                NMSettingsConnectionInterface *connection,
                NMActiveConnection *active_connection,
                const char *setting_name,
                const char **hints,
                NMANewSecretsRequestedFunc callback,
                gpointer callback_data,
                NMApplet *applet,
                GError **error)
{
	NMBtSecretsInfo *info;
	GtkWidget *widget;
	GtkEntry *secret_entry = NULL;

	if (!hints || !g_strv_length ((char **) hints)) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): missing secrets hints.",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	if (   (!strcmp (setting_name, NM_SETTING_CDMA_SETTING_NAME) && !strcmp (hints[0], NM_SETTING_CDMA_PASSWORD))
	    || (!strcmp (setting_name, NM_SETTING_GSM_SETTING_NAME) && !strcmp (hints[0], NM_SETTING_GSM_PASSWORD)))
		widget = applet_mobile_password_dialog_new (device, NM_CONNECTION (connection), &secret_entry);
	else {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): unknown secrets hint '%s'.",
		             __FILE__, __LINE__, __func__, hints[0]);
		return FALSE;
	}

	if (!widget || !secret_entry) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): error asking for CDMA secrets.",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	info = g_malloc0 (sizeof (NMBtSecretsInfo));
	info->callback = callback;
	info->callback_data = callback_data;
	info->applet = applet;
	info->active_connection = active_connection;
	info->connection = g_object_ref (connection);
	info->secret_name = g_strdup (hints[0]);
	info->setting_name = g_strdup (setting_name);
	info->dialog = widget;
	info->secret_entry = secret_entry;

	g_signal_connect (widget, "response", G_CALLBACK (get_bt_secrets_cb), info);

	/* Attach a destroy notifier to the NMActiveConnection so we can destroy
	 * the dialog when the active connection goes away.
	 */
	g_object_weak_ref (G_OBJECT (active_connection), destroy_secrets_dialog, info);

	gtk_window_set_position (GTK_WINDOW (widget), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (GTK_WIDGET (widget));
	gtk_window_present (GTK_WINDOW (widget));

	return TRUE;
}

NMADeviceClass *
applet_device_bt_get_class (NMApplet *applet)
{
	NMADeviceClass *dclass;

	dclass = g_slice_new0 (NMADeviceClass);
	if (!dclass)
		return NULL;

	dclass->new_auto_connection = bt_new_auto_connection;
	dclass->add_menu_item = bt_add_menu_item;
	dclass->device_state_changed = bt_device_state_changed;
	dclass->get_icon = bt_get_icon;
	dclass->get_secrets = bt_get_secrets;

	return dclass;
}
