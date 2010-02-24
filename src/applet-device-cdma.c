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
 * (C) Copyright 2008 - 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <nm-device.h>
#include <nm-setting-connection.h>
#include <nm-setting-cdma.h>
#include <nm-setting-serial.h>
#include <nm-setting-ppp.h>
#include <nm-cdma-device.h>
#include <nm-utils.h>

#include "applet.h"
#include "applet-device-cdma.h"
#include "utils.h"
#include "mobile-wizard.h"
#include "applet-dialogs.h"

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMConnection *connection;
} CdmaMenuItemInfo;

static void
cdma_menu_item_info_destroy (gpointer data)
{
	g_slice_free (CdmaMenuItemInfo, data);
}

typedef struct {
	AppletNewAutoConnectionCallback callback;
	gpointer callback_data;
} AutoCdmaWizardInfo;

static void
mobile_wizard_done (MobileWizard *wizard,
                    gboolean canceled,
                    MobileWizardAccessMethod *method,
                    gpointer user_data)
{
	AutoCdmaWizardInfo *info = user_data;
	NMConnection *connection = NULL;

	if (!canceled && method) {
		NMSetting *setting;
		char *uuid, *id;

		if (method->devtype != NM_DEVICE_TYPE_CDMA) {
			g_warning ("Unexpected device type (not CDMA).");
			canceled = TRUE;
			goto done;
		}

		connection = nm_connection_new ();

		setting = nm_setting_cdma_new ();
		g_object_set (setting,
		              NM_SETTING_CDMA_NUMBER, "#777",
		              NM_SETTING_CDMA_USERNAME, method->username,
		              NM_SETTING_CDMA_PASSWORD, method->password,
		              NULL);
		nm_connection_add_setting (connection, setting);

		/* Serial setting */
		setting = nm_setting_serial_new ();
		g_object_set (setting,
		              NM_SETTING_SERIAL_BAUD, 115200,
		              NM_SETTING_SERIAL_BITS, 8,
		              NM_SETTING_SERIAL_PARITY, 'n',
		              NM_SETTING_SERIAL_STOPBITS, 1,
		              NULL);
		nm_connection_add_setting (connection, setting);

		nm_connection_add_setting (connection, nm_setting_ppp_new ());

		setting = nm_setting_connection_new ();
		if (method->plan_name)
			id = g_strdup_printf ("%s %s", method->provider_name, method->plan_name);
		else
			id = g_strdup_printf ("%s connection", method->provider_name);
		uuid = nm_utils_uuid_generate ();
		g_object_set (setting,
		              NM_SETTING_CONNECTION_ID, id,
		              NM_SETTING_CONNECTION_TYPE, NM_SETTING_CDMA_SETTING_NAME,
		              NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
		              NM_SETTING_CONNECTION_UUID, uuid,
		              NULL);
		g_free (uuid);
		g_free (id);
		nm_connection_add_setting (connection, setting);
	}

done:
	(*(info->callback)) (connection, TRUE, canceled, info->callback_data);

	if (wizard)
		mobile_wizard_destroy (wizard);
	g_free (info);
}

static gboolean
cdma_new_auto_connection (NMDevice *device,
                          gpointer dclass_data,
                          AppletNewAutoConnectionCallback callback,
                          gpointer callback_data)
{
	MobileWizard *wizard;
	AutoCdmaWizardInfo *info;
	MobileWizardAccessMethod *method;

	info = g_malloc0 (sizeof (AutoCdmaWizardInfo));
	info->callback = callback;
	info->callback_data = callback_data;

	wizard = mobile_wizard_new (NULL, NULL, NM_DEVICE_TYPE_CDMA, FALSE,
	                            mobile_wizard_done, info);
	if (wizard) {
		mobile_wizard_present (wizard);
		return TRUE;
	}

	/* Fall back to something */
	method = g_malloc0 (sizeof (MobileWizardAccessMethod));
	method->devtype = NM_DEVICE_TYPE_CDMA;
	method->provider_name = _("CDMA");
	mobile_wizard_done (NULL, FALSE, method, info);
	g_free (method);

	return TRUE;
}

static void
cdma_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	CdmaMenuItemInfo *info = (CdmaMenuItemInfo *) user_data;

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
	CdmaMenuItemInfo *info;

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

		info = g_slice_new0 (CdmaMenuItemInfo);
		info->applet = applet;
		info->device = g_object_ref (G_OBJECT (device));
		info->connection = g_object_ref (connection);

		g_signal_connect_data (item, "activate",
		                       G_CALLBACK (cdma_menu_item_activate),
		                       info,
		                       (GClosureNotify) cdma_menu_item_info_destroy, 0);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}
}

static void
add_default_connection_item (NMDevice *device,
                             GtkWidget *menu,
                             NMApplet *applet)
{
	CdmaMenuItemInfo *info;
	GtkWidget *item;
	
	item = gtk_check_menu_item_new_with_label (_("New Mobile Broadband (CDMA) connection..."));

	info = g_slice_new0 (CdmaMenuItemInfo);
	info->applet = applet;
	info->device = g_object_ref (G_OBJECT (device));

	g_signal_connect_data (item, "activate",
	                       G_CALLBACK (cdma_menu_item_activate),
	                       info,
	                       (GClosureNotify) cdma_menu_item_info_destroy, 0);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

static void
cdma_add_menu_item (NMDevice *device,
                    guint32 n_devices,
                    NMConnection *active,
                    GtkWidget *menu,
                    NMApplet *applet)
{
	char *text;
	GtkWidget *item;
	GSList *connections, *all;
	GtkWidget *label;
	char *bold_text;

	all = applet_get_all_connections (applet);
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	if (n_devices > 1) {
		char *desc;

		desc = (char *) utils_get_device_description (device);
		if (!desc)
			desc = (char *) nm_device_get_iface (device);
		g_assert (desc);

		text = g_strdup_printf (_("Mobile Broadband (%s)"), desc);
	} else {
		text = g_strdup (_("Mobile Broadband"));
	}

	item = applet_menu_item_create_device_item_helper (device, applet, text);
	g_free (text);

	label = gtk_bin_get_child (GTK_BIN (item));
	bold_text = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>",
	                                     gtk_label_get_text (GTK_LABEL (label)));
	gtk_label_set_markup (GTK_LABEL (label), bold_text);
	g_free (bold_text);

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
		if ((!active && g_slist_length (connections)) || (active && g_slist_length (connections) > 1))
			applet_menu_item_add_complex_separator_helper (menu, applet, _("Available"), -1);

		if (g_slist_length (connections))
			add_connection_items (device, connections, active, ADD_INACTIVE, menu, applet);
		else
			add_default_connection_item (device, menu, applet);
	}

	g_slist_free (connections);
}

static void
cdma_device_state_changed (NMDevice *device,
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
		                            str ? str : _("You are now connected to the CDMA network."),
		                            "nm-device-wwan",
		                            PREF_DISABLE_CONNECTED_NOTIFICATIONS);
		g_free (str);
	}
}

static GdkPixbuf *
cdma_get_icon (NMDevice *device,
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
		pixbuf = nma_icon_check_and_load ("nm-device-wwan", &applet->wwan_icon, applet);
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
} NMCdmaInfo;

static void
destroy_cdma_dialog (gpointer user_data, GObject *finalized)
{
	NMCdmaInfo *info = user_data;

	gtk_widget_hide (info->dialog);
	gtk_widget_destroy (info->dialog);

	g_object_unref (info->connection);
	g_free (info->secret_name);
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
get_cdma_secrets_cb (GtkDialog *dialog,
                     gint response,
                     gpointer user_data)
{
	NMCdmaInfo *info = (NMCdmaInfo *) user_data;
	NMSettingCdma *setting;
	GHashTable *settings_hash;
	GHashTable *secrets;
	GError *err = NULL;

	/* Got a user response, clear the NMActiveConnection destroy handler for
	 * this dialog since this function will now take over dialog destruction.
	 */
	g_object_weak_unref (G_OBJECT (info->active_connection), destroy_cdma_dialog, info);

	if (response != GTK_RESPONSE_OK) {
		g_set_error (&err,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): canceled",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	setting = NM_SETTING_CDMA (nm_connection_get_setting (NM_CONNECTION (info->connection), NM_TYPE_SETTING_CDMA));

	if (!strcmp (info->secret_name, NM_SETTING_CDMA_PASSWORD)) {
		g_object_set (setting, 
			      NM_SETTING_CDMA_PASSWORD, gtk_entry_get_text (info->secret_entry),
			      NULL);
	}

	secrets = nm_setting_to_hash (NM_SETTING (setting));
	if (!secrets) {
		g_set_error (&err,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): failed to hash setting '%s'.",
		             __FILE__, __LINE__, __func__, nm_setting_get_name (NM_SETTING (setting)));
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
	destroy_cdma_dialog (info, NULL);
}

static gboolean
cdma_get_secrets (NMDevice *device,
                  NMSettingsConnectionInterface *connection,
                  NMActiveConnection *active_connection,
                  const char *setting_name,
                  const char **hints,
                  NMANewSecretsRequestedFunc callback,
                  gpointer callback_data,
                  NMApplet *applet,
                  GError **error)
{
	NMCdmaInfo *info;
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

	if (!strcmp (hints[0], NM_SETTING_CDMA_PASSWORD))
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

	info = g_new (NMCdmaInfo, 1);
	info->callback = callback;
	info->callback_data = callback_data;
	info->applet = applet;
	info->active_connection = active_connection;
	info->connection = g_object_ref (connection);
	info->secret_name = g_strdup (hints[0]);
	info->dialog = widget;
	info->secret_entry = secret_entry;

	g_signal_connect (widget, "response", G_CALLBACK (get_cdma_secrets_cb), info);

	/* Attach a destroy notifier to the NMActiveConnection so we can destroy
	 * the dialog when the active connection goes away.
	 */
	g_object_weak_ref (G_OBJECT (active_connection), destroy_cdma_dialog, info);

	gtk_window_set_position (GTK_WINDOW (widget), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (GTK_WIDGET (widget));
	gtk_window_present (GTK_WINDOW (widget));

	return TRUE;
}

NMADeviceClass *
applet_device_cdma_get_class (NMApplet *applet)
{
	NMADeviceClass *dclass;

	dclass = g_slice_new0 (NMADeviceClass);
	if (!dclass)
		return NULL;

	dclass->new_auto_connection = cdma_new_auto_connection;
	dclass->add_menu_item = cdma_add_menu_item;
	dclass->device_state_changed = cdma_device_state_changed;
	dclass->get_icon = cdma_get_icon;
	dclass->get_secrets = cdma_get_secrets;

	return dclass;
}

