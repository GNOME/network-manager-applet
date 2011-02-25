/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 *  NetworkManager Applet
 *
 *  Copyright (C) 2009  Bastien Nocera <hadess@hadess.net>
 *  Copyright (C) 2009 - 2010  Dan Williams <dcbw@redhat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <net/ethernet.h>
#include <netinet/ether.h>
#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>
#include <bluetooth-plugin.h>
#include <bluetooth-client.h>
#include <nm-setting-connection.h>
#include <nm-setting-bluetooth.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-cdma.h>
#include <nm-setting-gsm.h>
#include <nm-setting-serial.h>
#include <nm-setting-ppp.h>
#include <nm-utils.h>
#include <nm-remote-settings.h>
#include <nm-remote-connection.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include "nma-marshal.h"
#include "mobile-wizard.h"

#define DBUS_TYPE_G_MAP_OF_VARIANT (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))

#define BLUEZ_SERVICE           "org.bluez"
#define BLUEZ_MANAGER_PATH      "/"
#define BLUEZ_MANAGER_INTERFACE "org.bluez.Manager"
#define BLUEZ_ADAPTER_INTERFACE "org.bluez.Adapter"
#define BLUEZ_DEVICE_INTERFACE  "org.bluez.Device"
#define BLUEZ_SERIAL_INTERFACE  "org.bluez.Serial"
#define BLUEZ_NETWORK_INTERFACE "org.bluez.Network"

#define MM_SERVICE         "org.freedesktop.ModemManager"
#define MM_PATH            "/org/freedesktop/ModemManager"
#define MM_INTERFACE       "org.freedesktop.ModemManager"
#define MM_MODEM_INTERFACE "org.freedesktop.ModemManager.Modem"

typedef struct {
	NMRemoteSettings *settings;
	char *bdaddr;
	BluetoothClient *btclient;
	GtkTreeModel *btmodel;

	gboolean pan;
	GtkWidget *pan_button;
	guint pan_toggled_id;
	NMRemoteConnection *pan_connection;

	gboolean dun;
	GtkWidget *dun_button;
	guint dun_toggled_id;
	NMRemoteConnection *dun_connection;

	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *spinner;

	/* DUN stuff */
	DBusGConnection *bus;
	DBusGProxy *dun_proxy;

	DBusGProxy *mm_proxy;
	GSList *modem_proxies;

	char *rfcomm_iface;
	guint dun_timeout_id;

	MobileWizard *wizard;
	GtkWindowGroup *window_group;
} PluginInfo;

static void
get_capabilities (const char *bdaddr,
                  const char **uuids,
                  gboolean *pan,
                  gboolean *dun)
{
	guint i;

	g_return_if_fail (bdaddr != NULL);
	g_return_if_fail (uuids != NULL);
	g_return_if_fail (pan != NULL);
	g_return_if_fail (*pan == FALSE);
	g_return_if_fail (dun != NULL);
	g_return_if_fail (*dun == FALSE);

	for (i = 0; uuids && uuids[i] != NULL; i++) {
		g_message ("has_config_widget %s %s", bdaddr, uuids[i]);
		if (g_str_equal (uuids[i], "NAP"))
			*pan = TRUE;
		if (g_str_equal (uuids[i], "DialupNetworking"))
			*dun = TRUE;
	}
}

static gboolean
has_config_widget (const char *bdaddr, const char **uuids)
{
	gboolean pan = FALSE, dun = FALSE;

	get_capabilities (bdaddr, uuids, &pan, &dun);
	return pan || dun;
}

static GByteArray *
get_array_from_bdaddr (const char *str)
{
	struct ether_addr *addr;
	GByteArray *array;

	addr = ether_aton (str);
	if (addr) {
		array = g_byte_array_sized_new (ETH_ALEN);
		g_byte_array_append (array, (const guint8 *) addr->ether_addr_octet, ETH_ALEN);
		return array;
	}

	return NULL;
}

static gboolean
get_device_iter (GtkTreeModel *model, const char *bdaddr, GtkTreeIter *out_iter)
{
	GtkTreeIter iter;
	gboolean valid, child_valid;

	g_return_val_if_fail (model != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_TREE_MODEL (model), FALSE);
	g_return_val_if_fail (bdaddr != NULL, FALSE);
	g_return_val_if_fail (out_iter != NULL, FALSE);

	/* Loop over adapters */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		/* Loop over devices */
		if (gtk_tree_model_iter_n_children (model, &iter)) {
			child_valid = gtk_tree_model_iter_children (model, out_iter, &iter);
			while (child_valid) {
				char *addr = NULL;
				gboolean good;

				gtk_tree_model_get (model, out_iter, BLUETOOTH_COLUMN_ADDRESS, &addr, -1);
				good = (addr && !strcasecmp (addr, bdaddr));
				g_free (addr);

				if (good)
					return TRUE;  /* found */

				child_valid = gtk_tree_model_iter_next (model, out_iter);
			}
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}

	return FALSE;
}

/*******************************************************************/

static void
pan_cleanup (PluginInfo *info, const char *message, gboolean uncheck)
{
	if (info->spinner) {
		gtk_spinner_stop (GTK_SPINNER (info->spinner));
		gtk_widget_hide (info->spinner);
	}

	gtk_label_set_text (GTK_LABEL (info->label), message);
	gtk_widget_set_sensitive (info->pan_button, TRUE);

	if (uncheck) {
		g_signal_handler_block (info->pan_button, info->pan_toggled_id);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (info->pan_button), FALSE);
		g_signal_handler_unblock (info->pan_button, info->pan_toggled_id);
	}
}

static void
pan_add_cb (NMRemoteSettings *settings,
            NMRemoteConnection *connection,
            GError *error,
            gpointer user_data)
{
	PluginInfo *info = user_data;
	char *message;

	if (error) {
		message = g_strdup_printf (_("Failed to create PAN connection: %s"), error->message);
		pan_cleanup (info, message, TRUE);
		g_free (message);
	} else {
		info->pan_connection = connection;
		pan_cleanup (info, _("Your phone is now ready to use!"), FALSE);
	}
}

static void
add_pan_connection (PluginInfo *info)
{
	NMConnection *connection;
	NMSetting *setting, *bt_setting, *ip_setting;
	GByteArray *mac;
	char *id, *uuid, *alias = NULL;
	GtkTreeIter iter;

	mac = get_array_from_bdaddr (info->bdaddr);
	g_assert (mac);

	if (get_device_iter (info->btmodel, info->bdaddr, &iter))
		gtk_tree_model_get (info->btmodel, &iter, BLUETOOTH_COLUMN_ALIAS, &alias, -1);

	/* The connection */
	connection = nm_connection_new ();

	/* The connection settings */
	setting = nm_setting_connection_new ();
	id = g_strdup_printf (_("%s Network"), alias ? alias : info->bdaddr);
	uuid = nm_utils_uuid_generate ();
	g_object_set (G_OBJECT (setting),
	              NM_SETTING_CONNECTION_ID, id,
	              NM_SETTING_CONNECTION_UUID, uuid,
	              NM_SETTING_CONNECTION_TYPE, NM_SETTING_BLUETOOTH_SETTING_NAME,
	              NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
	              NULL);
	g_free (id);
	g_free (uuid);
	nm_connection_add_setting (connection, setting);

	/* The Bluetooth settings */
	bt_setting = nm_setting_bluetooth_new ();
	g_object_set (G_OBJECT (bt_setting),
	              NM_SETTING_BLUETOOTH_BDADDR, mac,
	              NM_SETTING_BLUETOOTH_TYPE, NM_SETTING_BLUETOOTH_TYPE_PANU,
	              NULL);
	nm_connection_add_setting (connection, bt_setting);

	/* The IPv4 settings */
	ip_setting = nm_setting_ip4_config_new ();
	g_object_set (G_OBJECT (ip_setting),
	              NM_SETTING_IP4_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO,
	              NULL);
	nm_connection_add_setting (connection, ip_setting);

	/* Add the connection to the settings service */
	nm_remote_settings_add_connection (info->settings,
	                                   connection,
	                                   pan_add_cb,
	                                   info);

	g_byte_array_free (mac, TRUE);
	g_free (alias);
}

static void
pan_start (PluginInfo *info)
{
	/* Start the spinner */
	if (!info->spinner) {
		info->spinner = gtk_spinner_new ();
		gtk_box_pack_start (GTK_BOX (info->hbox), info->spinner, FALSE, FALSE, 6);
	}
	gtk_spinner_start (GTK_SPINNER (info->spinner));
	gtk_widget_show_all (info->hbox);

	gtk_widget_set_sensitive (info->pan_button, FALSE);

	add_pan_connection (info);
}

/*******************************************************************/

static void dun_property_changed (DBusGProxy *proxy,
                                  const char *property,
                                  GValue *value,
                                  gpointer user_data);

static void
dun_cleanup (PluginInfo *info, const char *message, gboolean uncheck)
{
	GSList *iter;

	for (iter = info->modem_proxies; iter; iter = g_slist_next (iter))
		g_object_unref (DBUS_G_PROXY (iter->data));
	g_slist_free (info->modem_proxies);
	info->modem_proxies = NULL;

	if (info->dun_proxy) {
		if (info->rfcomm_iface) {
			dbus_g_proxy_call_no_reply (info->dun_proxy, "Disconnect",
			                            G_TYPE_STRING, info->rfcomm_iface,
			                            G_TYPE_INVALID);
		}

		dbus_g_proxy_disconnect_signal (info->dun_proxy, "PropertyChanged",
		                                G_CALLBACK (dun_property_changed), info);

		g_object_unref (info->dun_proxy);
		info->dun_proxy = NULL;
	}

	g_free (info->rfcomm_iface);
	info->rfcomm_iface = NULL;

	if (info->bus) {
		dbus_g_connection_unref (info->bus);
		info->bus = NULL;
	}

	if (info->dun_timeout_id) {
		g_source_remove (info->dun_timeout_id);
		info->dun_timeout_id = 0;
	}

	if (info->window_group) {
		g_object_unref (info->window_group);
		info->window_group = NULL;
	}

	if (info->wizard) {
		mobile_wizard_destroy (info->wizard);
		info->wizard = NULL;
	}

	if (info->spinner) {
		gtk_spinner_stop (GTK_SPINNER (info->spinner));
		gtk_widget_hide (info->spinner);
	}
	gtk_label_set_text (GTK_LABEL (info->label), message);
	gtk_widget_set_sensitive (info->dun_button, TRUE);

	if (uncheck) {
		g_signal_handler_block (info->dun_button, info->dun_toggled_id);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (info->dun_button), FALSE);
		g_signal_handler_unblock (info->dun_button, info->dun_toggled_id);
	}
}

static void
dun_error (PluginInfo *info, const char *func, GError *error, const char *fallback)
{
	char *message;

	message = g_strdup_printf (_("Error: %s"), (error && error->message) ? error->message : fallback);
	g_warning ("%s: %s", func, message);
	dun_cleanup (info, message, TRUE);
	g_free (message);
}

static NMConnection *
dun_new_cdma (MobileWizardAccessMethod *method)
{
	NMConnection *connection;
	NMSetting *setting;
	char *uuid, *id;

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
	              NM_SETTING_CONNECTION_TYPE, NM_SETTING_BLUETOOTH_SETTING_NAME,
	              NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
	              NM_SETTING_CONNECTION_UUID, uuid,
	              NULL);
	g_free (uuid);
	g_free (id);
	nm_connection_add_setting (connection, setting);

	return connection;
}

static NMConnection *
dun_new_gsm (MobileWizardAccessMethod *method)
{
	NMConnection *connection;
	NMSetting *setting;
	char *uuid, *id;

	connection = nm_connection_new ();

	setting = nm_setting_gsm_new ();
	g_object_set (setting,
	              NM_SETTING_GSM_NUMBER, "*99#",
	              NM_SETTING_GSM_USERNAME, method->username,
	              NM_SETTING_GSM_PASSWORD, method->password,
	              NM_SETTING_GSM_APN, method->gsm_apn,
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
	              NM_SETTING_CONNECTION_TYPE, NM_SETTING_BLUETOOTH_SETTING_NAME,
	              NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
	              NM_SETTING_CONNECTION_UUID, uuid,
	              NULL);
	g_free (uuid);
	g_free (id);
	nm_connection_add_setting (connection, setting);

	return connection;
}

static void
dun_add_cb (NMRemoteSettings *settings,
            NMRemoteConnection *connection,
            GError *error,
            gpointer user_data)
{
	PluginInfo *info = user_data;
	char *message;

	if (error) {
		message = g_strdup_printf (_("Failed to create DUN connection: %s"), error->message);
		dun_cleanup (info, message, TRUE);
		g_free (message);
	} else {
		info->dun_connection = connection;
		dun_cleanup (info, _("Your phone is now ready to use!"), FALSE);
	}
}

static void
wizard_done_cb (MobileWizard *self,
                gboolean canceled,
                MobileWizardAccessMethod *method,
                gpointer user_data)
{
	PluginInfo *info = user_data;
	NMConnection *connection = NULL;
	GByteArray *mac;
	NMSetting *s_bt;

	g_message ("%s: mobile wizard done", __func__);

	if (canceled || !method) {
		dun_error (info, __func__, NULL, _("Mobile wizard was canceled"));
		return;
	}

	if (method->devtype == NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)
		connection = dun_new_cdma (method);
	else if (method->devtype == NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS)
		connection = dun_new_gsm (method);
	else {
		dun_error (info, __func__, NULL, _("Unknown phone device type (not GSM or CDMA)"));
		return;
	}

	mobile_wizard_destroy (info->wizard);
	info->wizard = NULL;

	g_assert (connection);

	/* The Bluetooth settings */
	mac = get_array_from_bdaddr (info->bdaddr);
	g_assert (mac);
	s_bt = nm_setting_bluetooth_new ();
	g_object_set (G_OBJECT (s_bt),
	              NM_SETTING_BLUETOOTH_BDADDR, mac,
	              NM_SETTING_BLUETOOTH_TYPE, NM_SETTING_BLUETOOTH_TYPE_DUN,
	              NULL);
	g_byte_array_free (mac, TRUE);
	nm_connection_add_setting (connection, s_bt);

	g_message ("%s: adding new setting", __func__);

	/* Add the connection to the settings service */
	nm_remote_settings_add_connection (info->settings,
	                                   connection,
	                                   dun_add_cb,
	                                   info);

	g_message ("%s: waiting for add connection result...", __func__);
}

static void
modem_get_all_cb (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	PluginInfo *info = user_data;
	const char *path;
	GHashTable *properties = NULL;
	GError *error = NULL;
	GValue *value;
	NMDeviceType devtype = NM_DEVICE_TYPE_UNKNOWN;

	path = dbus_g_proxy_get_path (proxy);
	g_message ("%s: (%s) processing GetAll reply", __func__, path);

	if (!dbus_g_proxy_end_call (proxy, call, &error,
	                            DBUS_TYPE_G_MAP_OF_VARIANT, &properties,
	                            G_TYPE_INVALID)) {
		g_warning ("%s: (%s) Error getting modem properties: (%d) %s",
		           __func__,
		           path,
		           error ? error->code : -1,
		           (error && error->message) ? error->message : "(unknown)");
		g_error_free (error);
		goto out;
	}

	/* check whether this is the device we care about */
	value = g_hash_table_lookup (properties, "Device");
	if (value && G_VALUE_HOLDS_STRING (value) && g_value_get_string (value)) {
		char *iface_basename = g_path_get_basename (info->rfcomm_iface);
		const char *modem_iface = g_value_get_string (value);

		if (!strcmp (iface_basename, modem_iface)) {
			/* yay, found it! */

			value = g_hash_table_lookup (properties, "Type");
			if (value && G_VALUE_HOLDS_UINT (value)) {
				switch (g_value_get_uint (value)) {
				case 1:
					devtype = NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS;
					break;
				case 2:
					devtype = NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO;
					break;
				default:
					g_message ("%s: (%s) unknown modem type", __func__, path);
					break;
				}
			}
		} else {
			g_message ("%s: (%s) (%s) not the modem we're looking for (%s)",
			           __func__, path, modem_iface, iface_basename);
		}

		g_free (iface_basename);
	} else
		g_message ("%s: (%s) modem had no 'Device' property", __func__, path);

	g_hash_table_unref (properties);

	if (devtype != NM_DEVICE_TYPE_UNKNOWN) {
		GtkWidget *parent;

		if (info->wizard) {
			g_message ("%s: (%s) oops! not starting Wizard as one is already in progress", __func__, path);
			goto out;
		}

		g_message ("%s: (%s) starting the mobile wizard", __func__, path);

		g_source_remove (info->dun_timeout_id);
		info->dun_timeout_id = 0;

		parent = gtk_widget_get_toplevel (info->hbox);
		if (gtk_widget_is_toplevel (parent)) {
			info->window_group = gtk_window_group_new ();
			gtk_window_group_add_window (info->window_group, GTK_WINDOW (parent));
		} else {
			parent = NULL;
			info->window_group = NULL;
		}

		/* Start the mobile wizard */
		info->wizard = mobile_wizard_new (parent ? GTK_WINDOW (parent) : NULL,
		                                  info->window_group,
		                                  devtype,
		                                  FALSE,
		                                  wizard_done_cb,
		                                  info);
		mobile_wizard_present (info->wizard);
	}

out:
	g_message ("%s: finished", __func__);
}

static void
modem_added (DBusGProxy *proxy, const char *path, gpointer user_data)
{
	PluginInfo *info = user_data;
	DBusGProxy *props_proxy;

	g_return_if_fail (path != NULL);

	g_message ("%s: (%s) modem found", __func__, path);

	/* Create a proxy for the modem and get its properties */
	props_proxy = dbus_g_proxy_new_for_name (info->bus,
	                                         MM_SERVICE,
	                                         path,
	                                         "org.freedesktop.DBus.Properties");
	g_assert (proxy);
	info->modem_proxies = g_slist_append (info->modem_proxies, props_proxy);

	g_message ("%s: (%s) calling GetAll...", __func__, path);

	dbus_g_proxy_begin_call (props_proxy, "GetAll",
	                         modem_get_all_cb,
	                         info,
	                         NULL,
	                         G_TYPE_STRING, MM_MODEM_INTERFACE,
	                         G_TYPE_INVALID);
}

static void
modem_removed (DBusGProxy *proxy, const char *path, gpointer user_data)
{
	PluginInfo *info = user_data;
	GSList *iter;
	DBusGProxy *found = NULL;

	g_return_if_fail (path != NULL);

	g_message ("%s: (%s) modem removed", __func__, path);

	/* Clean up if a modem gets removed */

	for (iter = info->modem_proxies; iter; iter = g_slist_next (iter)) {
		if (!strcmp (path, dbus_g_proxy_get_path (DBUS_G_PROXY (iter->data)))) {
			found = iter->data;
			break;
		}
	}

	if (found) {
		info->modem_proxies = g_slist_remove (info->modem_proxies, found);
		g_object_unref (found);
	}
}

static void
dun_connect_cb (DBusGProxy *proxy,
                DBusGProxyCall *call,
                void *user_data)
{
	PluginInfo *info = user_data;
	GError *error = NULL;
	char *device;

	g_message ("%s: processing Connect reply", __func__);

	if (!dbus_g_proxy_end_call (proxy, call, &error,
	                            G_TYPE_STRING, &device,
	                            G_TYPE_INVALID)) {
		dun_error (info, __func__, error, _("failed to connect to the phone."));
		g_clear_error (&error);
		goto out;
	}

	if (!device || !strlen (device)) {
		dun_error (info, __func__, NULL, _("failed to connect to the phone."));
		g_free (device);
		goto out;
	}

	info->rfcomm_iface = device;
	g_message ("%s: new rfcomm interface '%s'", __func__, device);

out:
	g_message ("%s: finished", __func__);
}

static void
dun_property_changed (DBusGProxy *proxy,
                      const char *property,
                      GValue *value,
                      gpointer user_data)
{
	PluginInfo *info = user_data;
	gboolean connected;

	if (strcmp (property, "Connected"))
		return;

	connected = g_value_get_boolean (value);

	g_message ("%s: device property Connected changed to %s",
	           __func__,
	           connected ? "TRUE" : "FALSE");

	if (connected) {
		/* Wait for MM here ? */
	} else
		dun_error (info, __func__, NULL, _("unexpectedly disconnected from the phone."));
}

static gboolean
dun_timeout_cb (gpointer user_data)
{
	PluginInfo *info = user_data;

	info->dun_timeout_id = 0;
	dun_error (info, __func__, NULL, _("timed out detecting phone details."));
	return FALSE;
}

static void
dun_start (PluginInfo *info)
{
	GError *error = NULL;
	GtkTreeIter iter;

	g_message ("%s: starting DUN device discovery...", __func__);

	gtk_label_set_text (GTK_LABEL (info->label), _("Detecting phone configuration..."));

	/* Start the spinner */
	if (!info->spinner) {
		info->spinner = gtk_spinner_new ();
		gtk_box_pack_start (GTK_BOX (info->hbox), info->spinner, FALSE, FALSE, 6);
	}
	gtk_spinner_start (GTK_SPINNER (info->spinner));
	gtk_widget_show_all (info->hbox);

	gtk_widget_set_sensitive (info->dun_button, FALSE);

	/* ModemManager stuff */
	info->mm_proxy = dbus_g_proxy_new_for_name (info->bus,
	                                            MM_SERVICE,
	                                            MM_PATH,
	                                            MM_INTERFACE);
	g_assert (info->mm_proxy);

	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOXED,
	                                   G_TYPE_NONE,
	                                   G_TYPE_BOXED,
	                                   G_TYPE_INVALID);
	dbus_g_proxy_add_signal (info->mm_proxy, "DeviceAdded",
	                         DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (info->mm_proxy, "DeviceAdded",
								 G_CALLBACK (modem_added), info,
								 NULL);

	dbus_g_proxy_add_signal (info->mm_proxy, "DeviceRemoved",
	                         DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (info->mm_proxy, "DeviceRemoved",
								 G_CALLBACK (modem_removed), info,
								 NULL);

	/* Get the device we're looking for */
	info->dun_proxy = NULL;
	if (get_device_iter (info->btmodel, info->bdaddr, &iter))
		gtk_tree_model_get (info->btmodel, &iter, BLUETOOTH_COLUMN_PROXY, &info->dun_proxy, -1);

	if (info->dun_proxy) {
		info->dun_timeout_id = g_timeout_add_seconds (45, dun_timeout_cb, info);

		dbus_g_proxy_set_interface (info->dun_proxy, BLUEZ_SERIAL_INTERFACE);

		g_message ("%s: calling Connect...", __func__);

		/* Watch for BT device property changes */
		dbus_g_object_register_marshaller (nma_marshal_VOID__STRING_BOXED,
		                                   G_TYPE_NONE,
		                                   G_TYPE_STRING, G_TYPE_VALUE,
		                                   G_TYPE_INVALID);
		dbus_g_proxy_add_signal (info->dun_proxy, "PropertyChanged",
		                         G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_INVALID);
		dbus_g_proxy_connect_signal (info->dun_proxy, "PropertyChanged",
		                             G_CALLBACK (dun_property_changed), info, NULL);

		/* Request a connection to the device and get the port */
		dbus_g_proxy_begin_call_with_timeout (info->dun_proxy, "Connect",
		                                      dun_connect_cb,
		                                      info,
		                                      NULL,
		                                      20000,
		                                      G_TYPE_STRING, "dun",
		                                      G_TYPE_INVALID);
	} else
		dun_error (info, __func__, error, _("could not find the Bluetooth device."));

	g_message ("%s: finished", __func__);
}

/*******************************************************************/

static void
delete_cb (NMRemoteConnection *connection, GError *error, gpointer user_data)
{
	if (error) {
		g_warning ("Error deleting connection: (%d) %s",
		           error ? error->code : -1,
		           error && error->message ? error->message : "(unknown)");
	}
}

static void
pan_button_toggled (GtkToggleButton *button, gpointer user_data)
{
	PluginInfo *info = user_data;

	if (gtk_toggle_button_get_active (button) == FALSE) {
		nm_remote_connection_delete (info->pan_connection, delete_cb, NULL);
		info->pan_connection = NULL;
	} else
		pan_start (info);
}

static void
dun_button_toggled (GtkToggleButton *button, gpointer user_data)
{
	PluginInfo *info = user_data;

	if (gtk_toggle_button_get_active (button) == FALSE) {
		nm_remote_connection_delete (info->dun_connection, delete_cb, NULL);
		info->dun_connection = NULL;
	} else
		dun_start (info);
}

static gboolean
match_connection (NMConnection *connection, GByteArray *bdaddr, gboolean pan)
{
	NMSetting *setting;
	const char *type;
	const GByteArray *tmp;

	setting = nm_connection_get_setting_by_name (connection, NM_SETTING_BLUETOOTH_SETTING_NAME);
	if (setting == NULL)
		return FALSE;

	type = nm_setting_bluetooth_get_connection_type (NM_SETTING_BLUETOOTH (setting));
	if (pan) {
		if (g_strcmp0 (type, NM_SETTING_BLUETOOTH_TYPE_PANU) != 0)
			return FALSE;
	} else {
		if (g_strcmp0 (type, NM_SETTING_BLUETOOTH_TYPE_DUN) != 0)
			return FALSE;
	}

	tmp = nm_setting_bluetooth_get_bdaddr (NM_SETTING_BLUETOOTH (setting));
	if (tmp == NULL || memcmp (tmp->data, bdaddr->data, tmp->len) != 0)
		return FALSE;

	return TRUE;
}

static NMRemoteConnection *
get_connection_for_bdaddr (NMRemoteSettings *settings,
                           const char *bdaddr,
                           gboolean pan)
{
	NMRemoteConnection *found = NULL;
	GSList *list, *iter;
	GByteArray *array;

	array = get_array_from_bdaddr (bdaddr);
	if (array) {
		list = nm_remote_settings_list_connections (settings);
		for (iter = list; iter != NULL; iter = g_slist_next (iter)) {
			if (match_connection (NM_CONNECTION (iter->data), array, pan)) {
				found = iter->data;
				break;
			}
		}
		g_slist_free (list);
		g_byte_array_free (array, TRUE);
	}
	return found;
}

static void
plugin_info_destroy (gpointer data)
{
	PluginInfo *info = data;

	g_free (info->bdaddr);
	g_free (info->rfcomm_iface);
	if (info->pan_connection)
		g_object_unref (info->pan_connection);
	if (info->dun_connection)
		g_object_unref (info->dun_connection);
	if (info->spinner)
		gtk_spinner_stop (GTK_SPINNER (info->spinner));
	g_object_unref (info->settings);
	g_object_unref (info->btmodel);
	g_object_unref (info->btclient);
	memset (info, 0, sizeof (PluginInfo));
	g_free (info);
}

static void
default_adapter_powered_changed (GObject *object,
                                 GParamSpec *pspec,
                                 gpointer user_data)
{
	PluginInfo *info = user_data;
	gboolean powered = TRUE;

	g_object_get (G_OBJECT (info->btclient), "default-adapter-powered", &powered, NULL);
	g_message ("Default Bluetooth adapter is %s", powered ? "powered" : "switched off");

	/* If the default adapter isn't powered we can't inspect the device
	 * and create a connection for it.
	 */
	if (powered) {
		gtk_label_set_text (GTK_LABEL (info->label), NULL); 
		if (info->dun)
			gtk_widget_set_sensitive (info->dun_button, TRUE);
	} else {
		/* powered only matters for DUN */
		if (info->dun) {
			dun_cleanup (info, _("The default Bluetooth adapter must be enabled before setting up a Dial-Up-Networking connection."), TRUE);
			/* Can't toggle the DUN button unless the adapter is powered */
			gtk_widget_set_sensitive (info->dun_button, FALSE);
		}
	}
}

static void
default_adapter_changed (GObject *gobject,
                         GParamSpec *pspec,
                         gpointer user_data)
{
	PluginInfo *info = user_data;
	char *adapter;

	g_object_get (G_OBJECT (gobject), "default-adapter", &adapter, NULL);
	g_message ("Default Bluetooth adapter changed: %s", adapter ? adapter : "(none)");
	g_free (adapter);

	default_adapter_powered_changed (G_OBJECT (info->btclient), NULL, info);
}

static gboolean
nm_is_running (void)
{
	DBusGConnection *bus;
	DBusGProxy *proxy = NULL;
	GError *error = NULL;
	gboolean running = FALSE;

	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error || !bus) {
		g_message (_("Bluetooth configuration not possible (failed to connect to D-Bus: %s)."),
		           (error && error->message) ? error->message : "unknown");
		goto out;
	}

	proxy = dbus_g_proxy_new_for_name (bus,
	                                   "org.freedesktop.DBus",
	                                   "/org/freedesktop/DBus",
	                                   "org.freedesktop.DBus");
	if (!proxy) {
		g_message (_("Bluetooth configuration not possible (failed to create D-Bus proxy)."));
		goto out;
	}

	if (!dbus_g_proxy_call (proxy, "NameHasOwner", &error,
	                        G_TYPE_STRING, NM_DBUS_SERVICE,
	                        G_TYPE_INVALID,
	                        G_TYPE_BOOLEAN, &running,
	                        G_TYPE_INVALID)) {
		g_message (_("Bluetooth configuration not possible (error finding NetworkManager: %s)."),
		           error && error->message ? error->message : "unknown");
	}

out:
	g_clear_error (&error);
	if (proxy)
		g_object_unref (proxy);
	if (bus)
		dbus_g_connection_unref (bus);
	return running;
}

static GtkWidget *
get_config_widgets (const char *bdaddr, const char **uuids)
{
	PluginInfo *info;
	GtkWidget *vbox, *hbox;
	gboolean pan = FALSE, dun = FALSE;
	DBusGConnection *bus;
	GError *error = NULL;

	/* Don't allow configuration if NM isn't running; it just confuses people
	 * if they see the checkboxes but the configuration doesn't seem to have
	 * any visible effect since they aren't running NM/nm-applet.
	 */
	if (!nm_is_running ())
		return NULL;

	get_capabilities (bdaddr, uuids, &pan, &dun);
	if (!pan && !dun)
		return NULL;

	/* Set up dbus */
	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error || !bus) {
		g_warning ("%s: failed to get a connection to D-Bus! %s", __func__,
		           error ? error->message : "(unknown)");
		g_clear_error (&error);
		return NULL;
	}

	info = g_malloc0 (sizeof (PluginInfo));
	info->bus = bus;
	info->settings = nm_remote_settings_new (bus);
	info->bdaddr = g_strdup (bdaddr);
	info->pan = pan;
	info->dun = dun;
	
	/* BluetoothClient setup */
	info->btclient = bluetooth_client_new ();
	info->btmodel = bluetooth_client_get_model (info->btclient);
	g_signal_connect (G_OBJECT (info->btclient), "notify::default-adapter",
			  G_CALLBACK (default_adapter_changed), info);
	g_signal_connect (G_OBJECT (info->btclient), "notify::default-adapter-powered",
			  G_CALLBACK (default_adapter_powered_changed), info);

	/* UI setup */
	vbox = gtk_vbox_new (FALSE, 6);
	g_object_set_data_full (G_OBJECT (vbox), "info", info, plugin_info_destroy);

	if (pan) {
		info->pan_connection = get_connection_for_bdaddr (info->settings, bdaddr, TRUE);
		info->pan_button = gtk_check_button_new_with_label (_("Use your mobile phone as a network device (PAN/NAP)"));
		if (info->pan_connection)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (info->pan_button), TRUE);
		info->pan_toggled_id = g_signal_connect (G_OBJECT (info->pan_button), "toggled", G_CALLBACK (pan_button_toggled), info);
		gtk_box_pack_start (GTK_BOX (vbox), info->pan_button, FALSE, TRUE, 6);
	}

	if (dun) {
		info->dun_connection = get_connection_for_bdaddr (info->settings, bdaddr, FALSE);
		info->dun_button = gtk_check_button_new_with_label (_("Access the Internet using your mobile phone (DUN)"));
		if (info->dun_connection)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (info->dun_button), TRUE);
		info->dun_toggled_id = g_signal_connect (G_OBJECT (info->dun_button), "toggled", G_CALLBACK (dun_button_toggled), info);
		gtk_box_pack_start (GTK_BOX (vbox), info->dun_button, FALSE, TRUE, 6);
	}

	hbox = gtk_hbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 6);

	/* Spinner's hbox */
	info->hbox = gtk_hbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (hbox), info->hbox, FALSE, FALSE, 0);

	info->label = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hbox), info->label, FALSE, TRUE, 6);

	default_adapter_powered_changed (G_OBJECT (info->btclient), NULL, info);

	return vbox;
}

typedef struct {
	NMRemoteSettings *settings;
	GByteArray *bdaddr;
	char *str_bdaddr;
	guint timeout_id;
} RemoveInfo;

static void
remove_cleanup (RemoveInfo *info)
{
	g_object_unref (info->settings);
	g_byte_array_free (info->bdaddr, TRUE);
	g_free (info->str_bdaddr);
	memset (info, 0, sizeof (RemoveInfo));
	g_free (info);
}

static GSList *
list_connections_for_bdaddr (NMRemoteSettings *settings, GByteArray *bdaddr, gboolean pan)
{
	GSList *list, *iter, *ret = NULL;

	list = nm_remote_settings_list_connections (settings);
	for (iter = list; iter != NULL; iter = g_slist_next (iter)) {
		if (match_connection (NM_CONNECTION (iter->data), bdaddr, pan))
			ret = g_slist_prepend (ret, iter->data);
	}
	g_slist_free (list);
	return ret;
}

static void
remove_connections_read (NMRemoteSettings *settings, gpointer user_data)
{
	RemoveInfo *info = user_data;
	GSList *list, *iter;

	g_source_remove (info->timeout_id);

	g_message ("Removing Bluetooth connections for %s", info->str_bdaddr);

	/* First PAN */
	list = list_connections_for_bdaddr (info->settings, info->bdaddr, TRUE);
	for (iter = list; iter; iter = g_slist_next (iter))
			nm_remote_connection_delete (NM_REMOTE_CONNECTION (iter->data), delete_cb, NULL);
	g_slist_free (list);

	/* Now DUN */
	list = list_connections_for_bdaddr (info->settings, info->bdaddr, FALSE);
	for (iter = list; iter; iter = g_slist_next (iter))
			nm_remote_connection_delete (NM_REMOTE_CONNECTION (iter->data), delete_cb, NULL);
	g_slist_free (list);

	remove_cleanup (info);
}

static gboolean
remove_timeout (gpointer user_data)
{
	RemoveInfo *info = user_data;

	g_message ("Timed out removing Bluetooth connections for %s", info->str_bdaddr);
	remove_cleanup (info);
	return FALSE;
}

static void
device_removed (const char *bdaddr)
{
	GError *error = NULL;
	DBusGConnection *bus;
	RemoveInfo *info;
	GByteArray *array;

	g_message ("Device '%s' was removed; deleting connections", bdaddr);

	/* Remove any connections associated with the deleted device */

	array = get_array_from_bdaddr (bdaddr);
	if (!array) {
		g_warning ("Failed to convert Bluetooth address '%s'", bdaddr);
		return;
	}

	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error || !bus) {
		g_warning ("%s: failed to get a connection to D-Bus! %s", __func__,
		           error ? error->message : "(unknown)");
		g_clear_error (&error);
		g_byte_array_free (array, TRUE);
		return;
	}

	info = g_malloc0 (sizeof (RemoveInfo));
	info->settings = nm_remote_settings_new (bus);
	info->bdaddr = array;
	info->str_bdaddr = g_strdup (bdaddr);
	info->timeout_id = g_timeout_add_seconds (15, remove_timeout, info);

	g_signal_connect (info->settings,
	                  NM_REMOTE_SETTINGS_CONNECTIONS_READ,
	                  G_CALLBACK (remove_connections_read),
	                  info);

	dbus_g_connection_unref (bus);
}

static GbtPluginInfo plugin_info = {
	"network-manager-applet",
	has_config_widget,
	get_config_widgets,
	device_removed
};

GBT_INIT_PLUGIN(plugin_info)

