/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * Copyright (C) 2004 - 2012 Red Hat, Inc.
 * Copyright (C) 2005 - 2008 Novell, Inc.
 *
 * This applet used the GNOME Wireless Applet as a skeleton to build from.
 *
 * GNOME Wireless Applet Authors:
 *		Eskil Heyn Olsen <eskil@eskil.dk>
 *		Bastien Nocera <hadess@hadess.net> (Gnome2 port)
 *
 * (C) Copyright 2001, 2002 Free Software Foundation
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <time.h>
#include <string.h>
#include <strings.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>

#include <gio/gio.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <NetworkManagerVPN.h>
#include <nm-device-ethernet.h>
#include <nm-device-wifi.h>
#include <nm-device-modem.h>
#include <nm-device-bt.h>
#include <nm-device-wimax.h>
#include <nm-utils.h>
#include <nm-connection.h>
#include <nm-vpn-connection.h>
#include <nm-setting-connection.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-setting-pppoe.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-setting-bluetooth.h>
#include <nm-setting-vpn.h>
#include <nm-active-connection.h>
#include <nm-secret-agent.h>

#include <libnotify/notify.h>

#include "applet.h"
#include "applet-device-wired.h"
#include "applet-device-wifi.h"
#include "applet-device-gsm.h"
#include "applet-device-cdma.h"
#include "applet-device-bt.h"
#include "applet-device-wimax.h"
#include "applet-dialogs.h"
#include "nm-wireless-dialog.h"
#include "applet-vpn-request.h"
#include "utils.h"
#include "shell-watcher.h"

#define NOTIFY_CAPS_ACTIONS_KEY "actions"

extern gboolean shell_debug;

G_DEFINE_TYPE(NMApplet, nma, G_TYPE_OBJECT)

/********************************************************************/
/* Temporary dbus interface stuff */

static gboolean
impl_dbus_connect_to_hidden_network (NMApplet *applet, GError **error)
{
	if (!applet_wifi_connect_to_hidden_network (applet)) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "Failed to create wireless dialog");
		return FALSE;
	}

	return TRUE;
}

static gboolean
impl_dbus_create_wifi_network (NMApplet *applet, GError **error)
{
	if (!applet_wifi_can_create_wifi_network (applet)) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_NOT_AUTHORIZED,
		                     "Creation of wifi networks has been disabled by system policy.");
		return FALSE;
	}

	if (!applet_wifi_create_wifi_network (applet)) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "Failed to create wireless dialog");
		return FALSE;
	}

	return TRUE;
}

static gboolean
impl_dbus_connect_to_8021x_network (NMApplet *applet,
                                    const char *device_path,
                                    const char *ap_path,
                                    GError **error)
{
	NMDevice *device;
	NMAccessPoint *ap;

	device = nm_client_get_device_by_path (applet->nm_client, device_path);
	if (!device || NM_IS_DEVICE_WIFI (device) == FALSE) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "The device could not be found.");
		return FALSE;
	}

	ap = nm_device_wifi_get_access_point_by_path (NM_DEVICE_WIFI (device), ap_path);
	if (!ap) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "The access point could not be found.");
		return FALSE;
	}

	/* FIXME: this doesn't account for Dynamic WEP */
	if (   !(nm_access_point_get_wpa_flags (ap) & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
	    && !(nm_access_point_get_rsn_flags (ap) & NM_802_11_AP_SEC_KEY_MGMT_802_1X)) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "The access point had no 802.1x capabilities");
		return FALSE;
	}

	if (!applet_wifi_connect_to_8021x_network (applet, device, ap)) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "Failed to create wireless dialog");
		return FALSE;
	}

	return TRUE;
}

static gboolean
impl_dbus_connect_to_3g_network (NMApplet *applet,
                                 const char *device_path,
                                 GError **error)
{
	NMDevice *device;
	NMDeviceModemCapabilities caps;

	device = nm_client_get_device_by_path (applet->nm_client, device_path);
	if (!device || NM_IS_DEVICE_MODEM (device) == FALSE) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "The device could not be found.");
		return FALSE;
	}

	caps = nm_device_modem_get_current_capabilities (NM_DEVICE_MODEM (device));
	if (caps & NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS) {
		applet_gsm_connect_network (applet, device);
	} else if (caps & NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO) {
		applet_cdma_connect_network (applet, device);
	} else {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "The device had no GSM or CDMA capabilities.");
		return FALSE;
	}

	return TRUE;
}

#include "applet-dbus-bindings.h"

/********************************************************************/

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
			if (NM_IS_DEVICE_ETHERNET (candidate_dev)) {
				best_dev = candidate_dev;
				best = candidate;
			}
		} else if (NM_IS_DEVICE_MODEM (best_dev)) {
			NMDeviceModemCapabilities best_caps;
			NMDeviceModemCapabilities candidate_caps = NM_DEVICE_MODEM_CAPABILITY_NONE;

			best_caps = nm_device_modem_get_current_capabilities (NM_DEVICE_MODEM (best_dev));
			if (NM_IS_DEVICE_MODEM (candidate_dev))
				candidate_caps = nm_device_modem_get_current_capabilities (NM_DEVICE_MODEM (candidate_dev));

			if (best_caps & NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO) {
				if (   NM_IS_DEVICE_ETHERNET (candidate_dev)
				    || NM_IS_DEVICE_WIFI (candidate_dev)) {
					best_dev = candidate_dev;
					best = candidate;
				}
			} else if (best_caps & NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS) {
				if (   NM_IS_DEVICE_ETHERNET (candidate_dev)
					|| NM_IS_DEVICE_WIFI (candidate_dev)
					|| (candidate_caps & NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)) {
					best_dev = candidate_dev;
					best = candidate;
				}
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

NMRemoteSettings *
applet_get_settings (NMApplet *applet)
{
	return applet->settings;
}

GSList *
applet_get_all_connections (NMApplet *applet)
{
	return nm_remote_settings_list_connections (applet->settings);
}

static NMConnection *
applet_get_connection_for_active (NMApplet *applet, NMActiveConnection *active)
{
	GSList *list, *iter;
	NMConnection *connection = NULL;
	const char *path;

	path = nm_active_connection_get_connection (active);
	g_return_val_if_fail (path != NULL, NULL);

	list = applet_get_all_connections (applet);
	for (iter = list; iter; iter = g_slist_next (iter)) {
		NMConnection *candidate = NM_CONNECTION (iter->data);

		if (!strcmp (nm_connection_get_path (candidate), path)) {
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

	cpath = nm_connection_get_path (connection);
	g_return_val_if_fail (cpath != NULL, NULL);

	active_list = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; active_list && (i < active_list->len); i++) {
		NMActiveConnection *active = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_list, i));
		const char *active_cpath = nm_active_connection_get_connection (active);

		if (active_cpath && !strcmp (active_cpath, cpath))
			return active;
	}
	return NULL;
}

NMDevice *
applet_get_device_for_connection (NMApplet *applet, NMConnection *connection)
{
	const GPtrArray *active_list;
	const char *cpath;
	int i;

	cpath = nm_connection_get_path (connection);
	g_return_val_if_fail (cpath != NULL, NULL);

	active_list = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; active_list && (i < active_list->len); i++) {
		NMActiveConnection *active = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_list, i));

		if (!g_strcmp0 (nm_active_connection_get_connection (active), cpath))
			return g_ptr_array_index (nm_active_connection_get_devices (active), 0);
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
	else if (NM_IS_DEVICE_MODEM (device)) {
		NMDeviceModemCapabilities caps;

		caps = nm_device_modem_get_current_capabilities (NM_DEVICE_MODEM (device));
		if (caps & NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS)
			return applet->gsm_class;
		else if (caps & NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)
			return applet->cdma_class;
		else
			g_message ("%s: unhandled modem capabilities 0x%X", __func__, caps);
	} else if (NM_IS_DEVICE_BT (device))
		return applet->bt_class;
	else if (NM_IS_DEVICE_WIMAX (device))
		return applet->wimax_class;
	else
		g_message ("%s: Unknown device type '%s'", __func__, G_OBJECT_TYPE_NAME (device));
	return NULL;
}

static inline NMADeviceClass *
get_device_class_from_connection (NMConnection *connection, NMApplet *applet)
{
	NMSettingConnection *s_con;
	const char *ctype;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	s_con = nm_connection_get_setting_connection (connection);
	g_return_val_if_fail (s_con != NULL, NULL);

	ctype = nm_setting_connection_get_connection_type (s_con);
	g_return_val_if_fail (ctype != NULL, NULL);

	if (!strcmp (ctype, NM_SETTING_WIRED_SETTING_NAME) || !strcmp (ctype, NM_SETTING_PPPOE_SETTING_NAME))
		return applet->wired_class;
	else if (!strcmp (ctype, NM_SETTING_WIRELESS_SETTING_NAME))
		return applet->wifi_class;
	else if (!strcmp (ctype, NM_SETTING_GSM_SETTING_NAME))
		return applet->gsm_class;
	else if (!strcmp (ctype, NM_SETTING_CDMA_SETTING_NAME))
		return applet->cdma_class;
	else if (!strcmp (ctype, NM_SETTING_BLUETOOTH_SETTING_NAME))
		return applet->bt_class;
	else
		g_warning ("%s: unhandled connection type '%s'", __func__, ctype);
	return NULL;
}

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	char *specific_object;
	NMConnection *connection;
} AppletItemActivateInfo;

static void
applet_item_activate_info_destroy (AppletItemActivateInfo *info)
{
	g_return_if_fail (info != NULL);

	if (info->device)
		g_object_unref (info->device);
	g_free (info->specific_object);
	if (info->connection)
		g_object_unref (info->connection);
	memset (info, 0, sizeof (AppletItemActivateInfo));
	g_free (info);
}

static void
add_and_activate_cb (NMClient *client,
                     NMActiveConnection *active,
                     const char *connection_path,
                     GError *error,
                     gpointer user_data)
{
	if (error) {
		const char *text = _("Failed to add/activate connection");
		char *err_text = g_strdup_printf ("(%d) %s", error->code,
		                                  error->message ? error->message : _("Unknown error"));

		g_warning ("%s: %s", text, err_text);
		utils_show_error_dialog (_("Connection failure"), text, err_text, FALSE, NULL);
		g_free (err_text);
	}

	applet_schedule_update_icon (NM_APPLET (user_data));
}

static void
applet_menu_item_activate_helper_new_connection (NMConnection *connection,
                                                 gboolean auto_created,
                                                 gboolean canceled,
                                                 gpointer user_data)
{
	AppletItemActivateInfo *info = user_data;

	if (canceled) {
		applet_item_activate_info_destroy (info);
		return;
	}

	g_return_if_fail (connection != NULL);

	/* Ask NM to add the new connection and activate it; NM will fill in the
	 * missing details based on the specific object and the device.
	 */
	nm_client_add_and_activate_connection (info->applet->nm_client,
	                                       connection,
	                                       info->device,
	                                       info->specific_object,
	                                       add_and_activate_cb,
	                                       info->applet);

	applet_item_activate_info_destroy (info);
}

static void
disconnect_cb (NMDevice *device, GError *error, gpointer user_data)
{
	if (error) {
		const char *text = _("Device disconnect failed");
		char *err_text = g_strdup_printf ("(%d) %s", error->code,
		                                  error->message ? error->message : _("Unknown error"));

		g_warning ("%s: %s: %s", __func__, text, err_text);
		utils_show_error_dialog (_("Disconnect failure"), text, err_text, FALSE, NULL);
		g_free (err_text);
	}
}

void
applet_menu_item_disconnect_helper (NMDevice *device,
                                    NMApplet *applet)
{
	g_return_if_fail (NM_IS_DEVICE (device));

	nm_device_disconnect (device, disconnect_cb, NULL);
}

static void
activate_connection_cb (NMClient *client,
                        NMActiveConnection *active,
                        GError *error,
                        gpointer user_data)
{
	if (error) {
		const char *text = _("Connection activation failed");
		char *err_text = g_strdup_printf ("(%d) %s", error->code,
		                                  error->message ? error->message : _("Unknown error"));

		g_warning ("%s: %s", text, err_text);
		utils_show_error_dialog (_("Connection failure"), text, err_text, FALSE, NULL);
		g_free (err_text);
	}

	applet_schedule_update_icon (NM_APPLET (user_data));
}

void
applet_menu_item_activate_helper (NMDevice *device,
                                  NMConnection *connection,
                                  const char *specific_object,
                                  NMApplet *applet,
                                  gpointer dclass_data)
{
	AppletItemActivateInfo *info;
	NMADeviceClass *dclass;

	g_return_if_fail (NM_IS_DEVICE (device));

	if (connection) {
		/* If the menu item had an associated connection already, just tell
		 * NM to activate that connection.
		 */
		nm_client_activate_connection (applet->nm_client,
			                           connection,
			                           device,
			                           specific_object,
			                           activate_connection_cb,
			                           applet);
		return;
	}

	/* If no connection was given, ask the device class to create a new
	 * default connection for this device type.  This could be a wizard,
	 * and thus take a while.
	 */

	info = g_malloc0 (sizeof (AppletItemActivateInfo));
	info->applet = applet;
	info->specific_object = g_strdup (specific_object);
	info->device = g_object_ref (device);

	dclass = get_device_class (device, applet);
	g_assert (dclass);
	if (!dclass->new_auto_connection (device, dclass_data,
	                                  applet_menu_item_activate_helper_new_connection,
	                                  info)) {
		g_warning ("Couldn't create default connection.");
		applet_item_activate_info_destroy (info);
	}
}

void
applet_menu_item_add_complex_separator_helper (GtkWidget *menu,
                                               NMApplet *applet,
                                               const gchar* label,
                                               int pos)
{
	GtkWidget *menu_item = gtk_image_menu_item_new ();
#if GTK_CHECK_VERSION(3,1,6)
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
#else
	GtkWidget *box = gtk_hbox_new (FALSE, 0);
#endif
	GtkWidget *xlabel = NULL;

	if (label) {
		xlabel = gtk_label_new (NULL);
		gtk_label_set_markup (GTK_LABEL (xlabel), label);

#if GTK_CHECK_VERSION(3,1,6)
		gtk_box_pack_start (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), TRUE, TRUE, 0);
#else
		gtk_box_pack_start (GTK_BOX (box), gtk_hseparator_new (), TRUE, TRUE, 0);
#endif
		gtk_box_pack_start (GTK_BOX (box), xlabel, FALSE, FALSE, 2);
	}

#if GTK_CHECK_VERSION(3,1,6)
	gtk_box_pack_start (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), TRUE, TRUE, 0);
#else
	gtk_box_pack_start (GTK_BOX (box), gtk_hseparator_new (), TRUE, TRUE, 0);
#endif

	g_object_set (G_OBJECT (menu_item),
	              "child", box,
	              "sensitive", FALSE,
	              NULL);
	if (pos < 0)
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	else
		gtk_menu_shell_insert (GTK_MENU_SHELL (menu), menu_item, pos);
	return;
}

GtkWidget *
applet_new_menu_item_helper (NMConnection *connection,
                             NMConnection *active,
                             gboolean add_active)
{
	GtkWidget *item;
	NMSettingConnection *s_con;
	char *markup;
	GtkWidget *label;

	s_con = nm_connection_get_setting_connection (connection);
	item = gtk_image_menu_item_new_with_label ("");
	if (add_active && (active == connection)) {
		/* Pure evil */
		label = gtk_bin_get_child (GTK_BIN (item));
		gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
		markup = g_markup_printf_escaped ("<b>%s</b>", nm_setting_connection_get_id (s_con));
		gtk_label_set_markup (GTK_LABEL (label), markup);
		g_free (markup);
	} else
		gtk_menu_item_set_label (GTK_MENU_ITEM (item), nm_setting_connection_get_id (s_con));

	gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
	return item;
}

#define TITLE_TEXT_R ((double) 0x5e / 255.0 )
#define TITLE_TEXT_G ((double) 0x5e / 255.0 )
#define TITLE_TEXT_B ((double) 0x5e / 255.0 )

static void
menu_item_draw_generic (GtkWidget *widget, cairo_t *cr)
{
	GtkWidget *label;
	PangoFontDescription *desc;
	PangoLayout *layout;
	int width = 0, height = 0, owidth, oheight;
	gdouble extraheight = 0, extrawidth = 0;
	const char *text;
	gdouble xpadding = 10.0;
	gdouble ypadding = 5.0;
	gdouble postpadding = 0.0;

	label = gtk_bin_get_child (GTK_BIN (widget));
	text = gtk_label_get_text (GTK_LABEL (label));

	layout = pango_cairo_create_layout (cr);
#if GTK_CHECK_VERSION(2,20,0) && !GTK_CHECK_VERSION(2,91,6)
        {
                GtkStyle *style;
                style = gtk_widget_get_style (widget);
                desc = pango_font_description_copy (style->font_desc);
        }
#else
        {
                GtkStyleContext *style;
                style = gtk_widget_get_style_context (widget);
                gtk_style_context_get (style, gtk_style_context_get_state (style),
                                       "font", &desc,
                                       NULL);
        }
#endif
	pango_font_description_set_variant (desc, PANGO_VARIANT_SMALL_CAPS);
	pango_font_description_set_weight (desc, PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description (layout, desc);
	pango_layout_set_text (layout, text, -1);
	pango_cairo_update_layout (cr, layout);
	pango_layout_get_size (layout, &owidth, &oheight);
	width = owidth / PANGO_SCALE;
	height += oheight / PANGO_SCALE;

	cairo_save (cr);

	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.0);
	cairo_rectangle (cr, 0, 0,
	                 (double) (width + 2 * xpadding),
	                 (double) (height + ypadding + postpadding));
	cairo_fill (cr);

	/* now the in-padding content */
	cairo_translate (cr, xpadding , ypadding);
	cairo_set_source_rgb (cr, TITLE_TEXT_R, TITLE_TEXT_G, TITLE_TEXT_B);
	cairo_move_to (cr, extrawidth, extraheight);
	pango_cairo_show_layout (cr, layout);

	cairo_restore(cr);

	pango_font_description_free (desc);
	g_object_unref (layout);

	gtk_widget_set_size_request (widget, width + 2 * xpadding, height + ypadding + postpadding);
}

#if GTK_CHECK_VERSION(2,90,7)
static gboolean
menu_title_item_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	menu_item_draw_generic (widget, cr);
	return TRUE;
}
#else
static gboolean
menu_title_item_expose (GtkWidget *widget, GdkEventExpose *event)
{
	GtkAllocation allocation;
	cairo_t *cr;

	cr = gdk_cairo_create (gtk_widget_get_window (widget));

	/* The drawing area we get is the whole menu; clip the drawing to the
	 * event area, which should just be our menu item.
	 */
	cairo_rectangle (cr,
	                 event->area.x, event->area.y,
	                 event->area.width, event->area.height);
	cairo_clip (cr);

	/* We also need to reposition the cairo context so that (0, 0) is the
	 * top-left of where we're supposed to start drawing.
	 */
	gtk_widget_get_allocation (widget, &allocation);
	cairo_translate (cr, allocation.x, allocation.y);

	menu_item_draw_generic (widget, cr);

	cairo_destroy (cr);
	return TRUE;
}
#endif

GtkWidget *
applet_menu_item_create_device_item_helper (NMDevice *device,
                                            NMApplet *applet,
                                            const gchar *text)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_mnemonic (text);
	gtk_widget_set_sensitive (item, FALSE);
#if GTK_CHECK_VERSION(2,90,7)
	g_signal_connect (item, "draw", G_CALLBACK (menu_title_item_draw), NULL);
#else
	g_signal_connect (item, "expose-event", G_CALLBACK (menu_title_item_expose), NULL);
#endif
	return item;
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

static gboolean
applet_notify_server_has_actions (void)
{
	static gboolean has_actions = FALSE;
	static gboolean initialized = FALSE;
	GList *server_caps, *iter;

	if (initialized)
		return has_actions;
	initialized = TRUE;

	server_caps = notify_get_server_caps();
	for (iter = server_caps; iter; iter = g_list_next (iter)) {
		if (!strcmp ((const char *) iter->data, NOTIFY_CAPS_ACTIONS_KEY)) {
			has_actions = TRUE;
			break;
		}
	}
	g_list_foreach (server_caps, (GFunc) g_free, NULL);
	g_list_free (server_caps);

	return has_actions;
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
	char *escaped;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (summary != NULL);
	g_return_if_fail (message != NULL);

	if (!gtk_status_icon_is_embedded (applet->status_icon))
		return;

	applet_clear_notify (applet);

	escaped = utils_escape_notify_message (message);
	notify = notify_notification_new (summary,
	                                  escaped,
	                                  icon ? icon : GTK_STOCK_NETWORK
#if HAVE_LIBNOTIFY_07
	                                  );
#else
	                                  , NULL);
#endif
	g_free (escaped);
	applet->notification = notify;

#if HAVE_LIBNOTIFY_07
	notify_notification_set_hint (notify, "transient", g_variant_new_boolean (TRUE));
#else
	notify_notification_attach_to_status_icon (notify, applet->status_icon);
#endif
	notify_notification_set_urgency (notify, urgency);
	notify_notification_set_timeout (notify, NOTIFY_EXPIRES_DEFAULT);

	if (applet_notify_server_has_actions () && action1) {
		notify_notification_add_action (notify, action1, action1_label,
		                                action1_cb, action1_user_data, NULL);
	}

	if (!notify_notification_show (notify, &error)) {
		g_warning ("Failed to show notification: %s",
		           error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}
}

static void
notify_dont_show_cb (NotifyNotification *notify,
                     gchar *id,
                     gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	if (!id)
		return;

	if (   strcmp (id, PREF_DISABLE_CONNECTED_NOTIFICATIONS)
	    && strcmp (id, PREF_DISABLE_DISCONNECTED_NOTIFICATIONS)
	    && strcmp (id, PREF_DISABLE_VPN_NOTIFICATIONS))
		return;

	g_settings_set_boolean (applet->gsettings, id, TRUE);
}

void applet_do_notify_with_pref (NMApplet *applet,
                                 const char *summary,
                                 const char *message,
                                 const char *icon,
                                 const char *pref)
{
	if (g_settings_get_boolean (applet->gsettings, pref))
		return;
	
	applet_do_notify (applet, NOTIFY_URGENCY_LOW, summary, message, icon, pref,
	                  _("Don't show this message again"),
	                  notify_dont_show_cb,
	                  applet);
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

static gboolean
applet_is_any_device_activating (NMApplet *applet)
{
	const GPtrArray *devices;
	int i;

	/* Check for activating devices */
	devices = nm_client_get_devices (applet->nm_client);
	for (i = 0; devices && (i < devices->len); i++) {
		NMDevice *candidate = NM_DEVICE (g_ptr_array_index (devices, i));
		NMDeviceState state;

		state = nm_device_get_state (candidate);
		if (state > NM_DEVICE_STATE_DISCONNECTED && state < NM_DEVICE_STATE_ACTIVATED)
			return TRUE;
	}
	return FALSE;
}

static gboolean
applet_is_any_vpn_activating (NMApplet *applet)
{
	const GPtrArray *connections;
	int i;

	connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; connections && (i < connections->len); i++) {
		NMActiveConnection *candidate = NM_ACTIVE_CONNECTION (g_ptr_array_index (connections, i));
		NMVPNConnectionState vpn_state;

		if (NM_IS_VPN_CONNECTION (candidate)) {
			vpn_state = nm_vpn_connection_get_vpn_state (NM_VPN_CONNECTION (candidate));
			if (   vpn_state == NM_VPN_CONNECTION_STATE_PREPARE
			    || vpn_state == NM_VPN_CONNECTION_STATE_NEED_AUTH
			    || vpn_state == NM_VPN_CONNECTION_STATE_CONNECT
			    || vpn_state == NM_VPN_CONNECTION_STATE_IP_CONFIG_GET) {
				return TRUE;
			}
		}
	}
	return FALSE;
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
	s_con = nm_connection_get_setting_connection (connection);

	switch (reason) {
	case NM_VPN_CONNECTION_STATE_REASON_DEVICE_DISCONNECTED:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the network connection was interrupted."),
								nm_setting_connection_get_id (s_con));
	case NM_VPN_CONNECTION_STATE_REASON_SERVICE_STOPPED:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service stopped unexpectedly."),
								nm_setting_connection_get_id (s_con));
	case NM_VPN_CONNECTION_STATE_REASON_IP_CONFIG_INVALID:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service returned invalid configuration."),
								nm_setting_connection_get_id (s_con));
	case NM_VPN_CONNECTION_STATE_REASON_CONNECT_TIMEOUT:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the connection attempt timed out."),
								nm_setting_connection_get_id (s_con));
	case NM_VPN_CONNECTION_STATE_REASON_SERVICE_START_TIMEOUT:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service did not start in time."),
								nm_setting_connection_get_id (s_con));
	case NM_VPN_CONNECTION_STATE_REASON_SERVICE_START_FAILED:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because the VPN service failed to start."),
								nm_setting_connection_get_id (s_con));
	case NM_VPN_CONNECTION_STATE_REASON_NO_SECRETS:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because there were no valid VPN secrets."),
								nm_setting_connection_get_id (s_con));
	case NM_VPN_CONNECTION_STATE_REASON_LOGIN_FAILED:
		return g_strdup_printf (_("\nThe VPN connection '%s' failed because of invalid VPN secrets."),
								nm_setting_connection_get_id (s_con));

	default:
		break;
	}

	return g_strdup_printf (_("\nThe VPN connection '%s' failed."), nm_setting_connection_get_id (s_con));
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
	s_con = nm_connection_get_setting_connection (connection);

	switch (reason) {
	case NM_VPN_CONNECTION_STATE_REASON_DEVICE_DISCONNECTED:
		return g_strdup_printf (_("\nThe VPN connection '%s' disconnected because the network connection was interrupted."),
								nm_setting_connection_get_id (s_con));
	case NM_VPN_CONNECTION_STATE_REASON_SERVICE_STOPPED:
		return g_strdup_printf (_("\nThe VPN connection '%s' disconnected because the VPN service stopped."),
								nm_setting_connection_get_id (s_con));
	default:
		break;
	}

	return g_strdup_printf (_("\nThe VPN connection '%s' disconnected."), nm_setting_connection_get_id (s_con));
}

static void
vpn_connection_state_changed (NMVPNConnection *vpn,
                              NMVPNConnectionState state,
                              NMVPNConnectionStateReason reason,
                              gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	const char *banner;
	char *title = NULL, *msg;
	gboolean device_activating, vpn_activating;

	device_activating = applet_is_any_device_activating (applet);
	vpn_activating = applet_is_any_vpn_activating (applet);

	switch (state) {
	case NM_VPN_CONNECTION_STATE_PREPARE:
	case NM_VPN_CONNECTION_STATE_NEED_AUTH:
	case NM_VPN_CONNECTION_STATE_CONNECT:
	case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
		/* Be sure to turn animation timeout on here since the dbus signals
		 * for new active connections might not have come through yet.
		 */
		vpn_activating = TRUE;
		break;
	case NM_VPN_CONNECTION_STATE_ACTIVATED:
		banner = nm_vpn_connection_get_banner (vpn);
		if (banner && strlen (banner))
			msg = g_strdup_printf (_("VPN connection has been successfully established.\n\n%s\n"), banner);
		else
			msg = g_strdup (_("VPN connection has been successfully established.\n"));

		title = _("VPN Login Message");
		applet_do_notify_with_pref (applet, title, msg, "gnome-lockscreen",
		                            PREF_DISABLE_VPN_NOTIFICATIONS);
		g_free (msg);
		break;
	case NM_VPN_CONNECTION_STATE_FAILED:
		title = _("VPN Connection Failed");
		msg = make_vpn_failure_message (vpn, reason, applet);
		applet_do_notify_with_pref (applet, title, msg, "gnome-lockscreen",
		                            PREF_DISABLE_VPN_NOTIFICATIONS);
		g_free (msg);
		break;
	case NM_VPN_CONNECTION_STATE_DISCONNECTED:
		if (reason != NM_VPN_CONNECTION_STATE_REASON_USER_DISCONNECTED) {
			title = _("VPN Connection Failed");
			msg = make_vpn_disconnection_message (vpn, reason, applet);
			applet_do_notify_with_pref (applet, title, msg, "gnome-lockscreen",
			                            PREF_DISABLE_VPN_NOTIFICATIONS);
			g_free (msg);
		}
		break;
	default:
		break;
	}

	if (device_activating || vpn_activating)
		start_animation_timeout (applet);
	else
		clear_animation_timeout (applet);

	applet_schedule_update_icon (applet);
}

static const char *
get_connection_id (NMConnection *connection)
{
	NMSettingConnection *s_con;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	s_con = nm_connection_get_setting_connection (connection);
	g_return_val_if_fail (s_con != NULL, NULL);

	return nm_setting_connection_get_id (s_con);
}

typedef struct {
	NMApplet *applet;
	char *vpn_name;
} VPNActivateInfo;

static void
activate_vpn_cb (NMClient *client,
                 NMActiveConnection *active,
                 GError *error,
                 gpointer user_data)
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

		applet_do_notify_with_pref (info->applet, title, msg, "gnome-lockscreen",
		                            PREF_DISABLE_VPN_NOTIFICATIONS);
		g_free (msg);

		g_warning ("VPN Connection activation failed: (%s) %s", name, error->message);
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

	s_con = nm_connection_get_setting_connection (connection);
	info = g_malloc0 (sizeof (VPNActivateInfo));
	info->applet = applet;
	info->vpn_name = g_strdup (nm_setting_connection_get_id (s_con));

	/* Connection inactive, activate */
	nm_client_activate_connection (applet->nm_client,
	                               connection,
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
	const char *argv[] = { BINDIR "/nm-connection-editor", "--show", "--type", NM_SETTING_VPN_SETTING_NAME, NULL};

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

		s_con = nm_connection_get_setting_connection (connection);
		g_assert (s_con);

		if (!strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_VPN_SETTING_NAME)) {
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
 * Signal function called when user clicks "Disconnect VPN"
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
	GType aa_type = G_OBJECT_TYPE (G_OBJECT (aa));
	GType bb_type = G_OBJECT_TYPE (G_OBJECT (bb));

	if (aa_type == bb_type) {
		char *aa_desc = NULL;
		char *bb_desc = NULL;

		aa_desc = (char *) utils_get_device_description (aa);
		if (!aa_desc)
			aa_desc = (char *) nm_device_get_iface (aa);

		bb_desc = (char *) utils_get_device_description (bb);
		if (!bb_desc)
			bb_desc = (char *) nm_device_get_iface (bb);

		return g_strcmp0 (aa_desc, bb_desc);
	}

	/* Ethernet always first */
	if (aa_type == NM_TYPE_DEVICE_ETHERNET)
		return -1;
	if (bb_type == NM_TYPE_DEVICE_ETHERNET)
		return 1;

	/* Modems next */
	if (aa_type == NM_TYPE_DEVICE_MODEM)
		return -1;
	if (bb_type == NM_TYPE_DEVICE_MODEM)
		return 1;

	/* Bluetooth next */
	if (aa_type == NM_TYPE_DEVICE_BT)
		return -1;
	if (bb_type == NM_TYPE_DEVICE_BT)
		return 1;

	/* WiMAX next */
	if (aa_type == NM_TYPE_DEVICE_WIMAX)
		return -1;
	if (bb_type == NM_TYPE_DEVICE_WIMAX)
		return 1;

	/* WiFi last because it has many menu items */
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
		NMRemoteConnection *tmp;
		NMActiveConnection *active;
		const char *connection_path;
		const GPtrArray *devices;

		active = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_connections, i));
		devices = nm_active_connection_get_devices (active);
		connection_path = nm_active_connection_get_connection (active);

		if (!devices || !connection_path)
			continue;

		if (!nm_g_ptr_array_contains (devices, device))
			continue;

		tmp = nm_remote_settings_get_connection_by_path (applet->settings, connection_path);
		if (tmp) {
			connection = NM_CONNECTION (tmp);
			if (out_active)
				*out_active = active;
			break;
		}
	}

	return connection;
}

gboolean
nma_menu_device_check_unusable (NMDevice *device)
{
	switch (nm_device_get_state (device)) {
	case NM_DEVICE_STATE_UNKNOWN:
	case NM_DEVICE_STATE_UNAVAILABLE:
	case NM_DEVICE_STATE_UNMANAGED:
		return TRUE;
	default:
		break;
	}
	return FALSE;
}


struct AppletDeviceMenuInfo {
	NMDevice *device;
	NMApplet *applet;
};

static void
applet_device_info_destroy (struct AppletDeviceMenuInfo *info)
{
	g_return_if_fail (info != NULL);

	if (info->device)
		g_object_unref (info->device);
	memset (info, 0, sizeof (struct AppletDeviceMenuInfo));
	g_free (info);
}

static void
applet_device_disconnect_db (GtkMenuItem *item, gpointer user_data)
{
	struct AppletDeviceMenuInfo *info = user_data;

	applet_menu_item_disconnect_helper (info->device,
	                                    info->applet);
}

GtkWidget *
nma_menu_device_get_menu_item (NMDevice *device,
                               NMApplet *applet,
                               const char *unavailable_msg)
{
	GtkWidget *item = NULL;
	gboolean managed = TRUE;

	if (!unavailable_msg) {
		if (nm_device_get_firmware_missing (device))
			unavailable_msg = _("device not ready (firmware missing)");
		else
			unavailable_msg = _("device not ready");
	}

	switch (nm_device_get_state (device)) {
	case NM_DEVICE_STATE_UNKNOWN:
	case NM_DEVICE_STATE_UNAVAILABLE:
		item = gtk_menu_item_new_with_label (unavailable_msg);
		gtk_widget_set_sensitive (item, FALSE);
		break;
	case NM_DEVICE_STATE_DISCONNECTED:
		unavailable_msg = _("disconnected");
		item = gtk_menu_item_new_with_label (unavailable_msg);
		gtk_widget_set_sensitive (item, FALSE);
		break;
	case NM_DEVICE_STATE_UNMANAGED:
		managed = FALSE;
		break;
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
	case NM_DEVICE_STATE_NEED_AUTH:
	case NM_DEVICE_STATE_IP_CONFIG:
	case NM_DEVICE_STATE_ACTIVATED:
	{
		struct AppletDeviceMenuInfo *info = g_new0 (struct AppletDeviceMenuInfo, 1);
		info->device = g_object_ref (device);
		info->applet = applet;
		item = gtk_menu_item_new_with_label (_("Disconnect"));
		g_signal_connect_data (item, "activate",
		                       G_CALLBACK (applet_device_disconnect_db),
		                       info,
		                       (GClosureNotify) applet_device_info_destroy, 0);
		gtk_widget_set_sensitive (item, TRUE);
		break;
	}
	default:
		managed = nm_device_get_managed (device);
		break;
	}

	if (!managed) {
		item = gtk_menu_item_new_with_label (_("device not managed"));
		gtk_widget_set_sensitive (item, FALSE);
	}

	return item;
}

static guint32
nma_menu_add_devices (GtkWidget *menu, NMApplet *applet)
{
	const GPtrArray *temp = NULL;
	GSList *devices = NULL, *iter = NULL;
	gint n_wifi_devices = 0;
	gint n_usable_wifi_devices = 0;
	gint n_wired_devices = 0;
	gint n_mb_devices = 0;
	gint n_bt_devices = 0;
	int i;

	temp = nm_client_get_devices (applet->nm_client);
	for (i = 0; temp && (i < temp->len); i++)
		devices = g_slist_insert_sorted (devices, g_ptr_array_index (temp, i), sort_devices);

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
		else if (NM_IS_DEVICE_MODEM (device))
			n_mb_devices++;
		else if (NM_IS_DEVICE_BT (device))
			n_bt_devices++;
	}

	if (!n_wired_devices && !n_wifi_devices && !n_mb_devices && !n_bt_devices) {
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
		else if (NM_IS_DEVICE_MODEM (device))
			n_devices = n_mb_devices;

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

		s_con = nm_connection_get_setting_connection (connection);
		if (strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_VPN_SETTING_NAME))
			/* Not a VPN connection */
			continue;

		if (!nm_connection_get_setting_vpn (connection)) {
			g_warning ("%s: VPN connection '%s' didn't have required vpn setting.", __func__,
			           nm_setting_connection_get_id (s_con));
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
		GtkWidget *image;
		NMState state;

		name = get_connection_id (connection);

		item = GTK_MENU_ITEM (gtk_image_menu_item_new_with_label (name));
		gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);

		/* If no VPN connections are active, draw all menu items enabled. If
		 * >= 1 VPN connections are active, only the active VPN menu item is
		 * drawn enabled.
		 */
		active = applet_get_active_for_connection (applet, connection);

		state = nm_client_get_state (applet->nm_client);
		if (   state != NM_STATE_CONNECTED_LOCAL
		    && state != NM_STATE_CONNECTED_SITE
		    && state != NM_STATE_CONNECTED_GLOBAL)
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
		else if ((num_vpn_active == 0) || active)
			gtk_widget_set_sensitive (GTK_WIDGET (item), TRUE);
		else
			gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);

		if (active) {
			image = gtk_image_new_from_stock (GTK_STOCK_CONNECT, GTK_ICON_SIZE_MENU);
			gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		}

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

	item = GTK_MENU_ITEM (gtk_menu_item_new_with_mnemonic (_("_Disconnect VPN")));
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
nma_set_wwan_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_wwan_set_enabled (applet->nm_client, state);
}

static void
nma_set_wimax_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_wimax_set_enabled (applet->nm_client, state);
}

static void
nma_set_networking_enabled_cb (GtkWidget *widget, NMApplet *applet)
{
	gboolean state;

	g_return_if_fail (applet != NULL);

	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	nm_client_networking_set_enabled (applet->nm_client, state);
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
	                        PREF_SUPPRESS_WIRELESS_NETWORKS_AVAILABLE,
	                        !state);
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

	gtk_status_icon_set_tooltip_text (applet->status_icon, NULL);

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
		/* Add the "Hidden wireless network..." entry */
		nma_menu_add_separator_item (menu);
		nma_menu_add_hidden_network_item (menu, applet);
		nma_menu_add_create_network_item (menu, applet);
	}

	gtk_widget_show_all (menu);

//	nmi_dbus_signal_user_interface_activated (applet->connection);
}

static gboolean
destroy_old_menu (gpointer user_data)
{
	g_object_unref (user_data);
	return FALSE;
}

static void
nma_menu_deactivate_cb (GtkWidget *widget, NMApplet *applet)
{
	/* Must punt the destroy to a low-priority idle to ensure that
	 * the menu items don't get destroyed before any 'activate' signal
	 * fires for an item.
	 */
	g_signal_handlers_disconnect_by_func (applet->menu, G_CALLBACK (nma_menu_deactivate_cb), applet);
	g_idle_add_full (G_PRIORITY_LOW, destroy_old_menu, applet->menu, NULL);
	applet->menu = NULL;

	/* Re-set the tooltip */
	gtk_status_icon_set_tooltip_text (applet->status_icon, applet->tip);
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
static void
nma_context_menu_update (NMApplet *applet)
{
	NMState state;
	gboolean net_enabled = TRUE;
	gboolean have_wireless = FALSE;
	gboolean have_wwan = FALSE;
	gboolean have_wimax = FALSE;
	gboolean wireless_hw_enabled;
	gboolean wwan_hw_enabled;
	gboolean wimax_hw_enabled;
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

	/* Enabled Wireless */
	g_signal_handler_block (G_OBJECT (applet->wifi_enabled_item),
	                        applet->wifi_enabled_toggled_id);
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->wifi_enabled_item),
	                                nm_client_wireless_get_enabled (applet->nm_client));
	g_signal_handler_unblock (G_OBJECT (applet->wifi_enabled_item),
	                          applet->wifi_enabled_toggled_id);

	wireless_hw_enabled = nm_client_wireless_hardware_get_enabled (applet->nm_client);
	gtk_widget_set_sensitive (GTK_WIDGET (applet->wifi_enabled_item),
	                          wireless_hw_enabled && is_permission_yes (applet, NM_CLIENT_PERMISSION_ENABLE_DISABLE_WIFI));

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

	/* Enable WiMAX */
	g_signal_handler_block (G_OBJECT (applet->wimax_enabled_item),
	                        applet->wimax_enabled_toggled_id);
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->wimax_enabled_item),
	                                nm_client_wimax_get_enabled (applet->nm_client));
	g_signal_handler_unblock (G_OBJECT (applet->wimax_enabled_item),
	                          applet->wimax_enabled_toggled_id);

	wimax_hw_enabled = nm_client_wimax_hardware_get_enabled (applet->nm_client);
	gtk_widget_set_sensitive (GTK_WIDGET (applet->wimax_enabled_item),
	                          wimax_hw_enabled && is_permission_yes (applet, NM_CLIENT_PERMISSION_ENABLE_DISABLE_WIMAX));

	/* Enabled notifications */
	g_signal_handler_block (G_OBJECT (applet->notifications_enabled_item),
	                        applet->notifications_enabled_toggled_id);
	if (   g_settings_get_boolean (applet->gsettings, PREF_DISABLE_CONNECTED_NOTIFICATIONS)
	    && g_settings_get_boolean (applet->gsettings, PREF_DISABLE_DISCONNECTED_NOTIFICATIONS)
	    && g_settings_get_boolean (applet->gsettings, PREF_DISABLE_VPN_NOTIFICATIONS)
	    && g_settings_get_boolean (applet->gsettings, PREF_SUPPRESS_WIRELESS_NETWORKS_AVAILABLE))
		notifications_enabled = FALSE;
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (applet->notifications_enabled_item), notifications_enabled);
	g_signal_handler_unblock (G_OBJECT (applet->notifications_enabled_item),
	                          applet->notifications_enabled_toggled_id);

	/* Don't show wifi-specific stuff if wireless is off */
	if (state != NM_STATE_ASLEEP) {
		const GPtrArray *devices;
		int i;

		devices = nm_client_get_devices (applet->nm_client);
		for (i = 0; devices && (i < devices->len); i++) {
			NMDevice *candidate = g_ptr_array_index (devices, i);

			if (NM_IS_DEVICE_WIFI (candidate))
				have_wireless = TRUE;
			else if (NM_IS_DEVICE_MODEM (candidate))
				have_wwan = TRUE;
			else if (NM_IS_DEVICE_WIMAX (candidate))
				have_wimax = TRUE;
		}
	}

	if (have_wireless)
		gtk_widget_show_all (applet->wifi_enabled_item);
	else
		gtk_widget_hide (applet->wifi_enabled_item);

	if (have_wwan)
		gtk_widget_show_all (applet->wwan_enabled_item);
	else
		gtk_widget_hide (applet->wwan_enabled_item);

	if (have_wimax)
		gtk_widget_show_all (applet->wimax_enabled_item);
	else
		gtk_widget_hide (applet->wimax_enabled_item);
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

	/* 'Enable Mobile Broadband' item */
	applet->wwan_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable _Mobile Broadband"));
	id = g_signal_connect (applet->wwan_enabled_item,
	                       "toggled",
	                       G_CALLBACK (nma_set_wwan_enabled_cb),
	                       applet);
	applet->wwan_enabled_toggled_id = id;
	gtk_menu_shell_append (menu, applet->wwan_enabled_item);

	/* 'Enable WiMAX Mobile Broadband' item */
	applet->wimax_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable WiMA_X Mobile Broadband"));
	id = g_signal_connect (applet->wimax_enabled_item,
	                       "toggled",
	                       G_CALLBACK (nma_set_wimax_enabled_cb),
	                       applet);
	applet->wimax_enabled_toggled_id = id;
	gtk_menu_shell_append (menu, applet->wimax_enabled_item);

	nma_menu_add_separator_item (GTK_WIDGET (menu));

	/* Toggle notifications item */
	applet->notifications_enabled_item = gtk_check_menu_item_new_with_mnemonic (_("Enable N_otifications"));
	id = g_signal_connect (applet->notifications_enabled_item,
	                       "toggled",
	                       G_CALLBACK (nma_set_notifications_enabled_cb),
	                       applet);
	applet->notifications_enabled_toggled_id = id;
	gtk_menu_shell_append (menu, applet->notifications_enabled_item);

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
		nma_icon_check_and_load ("nm-no-connection", &applet->no_connection_icon, applet);
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


NMRemoteConnection *
applet_get_exported_connection_for_device (NMDevice *device, NMApplet *applet)
{
	const GPtrArray *active_connections;
	int i;

	active_connections = nm_client_get_active_connections (applet->nm_client);
	for (i = 0; active_connections && (i < active_connections->len); i++) {
		NMActiveConnection *active;
		NMRemoteConnection *connection;
		const char *connection_path;
		const GPtrArray *devices;

		active = g_ptr_array_index (active_connections, i);
		if (!active)
			continue;

		devices = nm_active_connection_get_devices (active);
		connection_path = nm_active_connection_get_connection (active);
		if (!devices || !connection_path)
			continue;

		if (!nm_g_ptr_array_contains (devices, device))
			continue;

		connection = nm_remote_settings_get_connection_by_path (applet->settings, connection_path);
		if (connection)
			return connection;
	}
	return NULL;
}

static void
applet_common_device_state_changed (NMDevice *device,
                                    NMDeviceState new_state,
                                    NMDeviceState old_state,
                                    NMDeviceStateReason reason,
                                    NMApplet *applet)
{
	gboolean device_activating = FALSE, vpn_activating = FALSE;

	device_activating = applet_is_any_device_activating (applet);
	vpn_activating = applet_is_any_vpn_activating (applet);

	switch (new_state) {
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
	case NM_DEVICE_STATE_NEED_AUTH:
	case NM_DEVICE_STATE_IP_CONFIG:
		/* Be sure to turn animation timeout on here since the dbus signals
		 * for new active connections or devices might not have come through yet.
		 */
		device_activating = TRUE;
		break;
	case NM_DEVICE_STATE_ACTIVATED:
	default:
		break;
	}

	/* If there's an activating device but we're not animating, start animation.
	 * If we're animating, but there's no activating device or VPN, stop animating.
	 */
	if (device_activating || vpn_activating)
		start_animation_timeout (applet);
	else
		clear_animation_timeout (applet);
}

static void
foo_device_state_changed_cb (NMDevice *device,
                             NMDeviceState new_state,
                             NMDeviceState old_state,
                             NMDeviceStateReason reason,
                             gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMADeviceClass *dclass;

	dclass = get_device_class (device, applet);
	g_assert (dclass);

	dclass->device_state_changed (device, new_state, old_state, reason, applet);
	applet_common_device_state_changed (device, new_state, old_state, reason, applet);

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

	g_signal_connect (device, "state-changed",
				   G_CALLBACK (foo_device_state_changed_cb),
				   user_data);

	foo_device_state_changed_cb	(device,
	                             nm_device_get_state (device),
	                             NM_DEVICE_STATE_UNKNOWN,
	                             NM_DEVICE_STATE_REASON_NONE,
	                             applet);
}

static void
foo_client_state_changed_cb (NMClient *client, GParamSpec *pspec, gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	switch (nm_client_get_state (client)) {
	case NM_STATE_DISCONNECTED:
		applet_do_notify_with_pref (applet, _("Disconnected"),
		                            _("The network connection has been disconnected."),
		                            "nm-no-connection",
		                            PREF_DISABLE_DISCONNECTED_NOTIFICATIONS);
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

static void
foo_manager_permission_changed (NMClient *client,
                                NMClientPermission permission,
                                NMClientPermissionResult result,
                                gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	if (permission <= NM_CLIENT_PERMISSION_LAST)
		applet->permissions[permission] = result;
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
	NMClientPermission perm;

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

	g_signal_connect (applet->nm_client, "permission-changed",
	                  G_CALLBACK (foo_manager_permission_changed),
	                  applet);

	/* Initialize permissions - the initial 'permission-changed' signal is emitted from NMClient constructor, and thus not caught */
	for (perm = NM_CLIENT_PERMISSION_NONE + 1; perm <= NM_CLIENT_PERMISSION_LAST; perm++) {
		applet->permissions[perm] = nm_client_get_permission_result (applet->nm_client, perm);
	}

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
	case NM_DEVICE_STATE_NEED_AUTH:
		stage = 1;
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		stage = 2;
		break;
	default:
		break;
	}

	if (stage >= 0) {
		int i, j;

		for (i = 0; i < NUM_CONNECTING_STAGES; i++) {
			for (j = 0; j < NUM_CONNECTING_FRAMES; j++) {
				char *name;

				name = g_strdup_printf ("nm-stage%02d-connecting%02d", i+1, j+1);
				nma_icon_check_and_load (name, &applet->network_connecting_icons[i][j], applet);
				g_free (name);
			}
		}

		pixbuf = applet->network_connecting_icons[stage][applet->animation_step];
		applet->animation_step++;
		if (applet->animation_step >= NUM_CONNECTING_FRAMES)
			applet->animation_step = 0;
	}

	return pixbuf;
}

static char *
get_tip_for_device_state (NMDevice *device,
                          NMDeviceState state,
                          NMConnection *connection)
{
	NMSettingConnection *s_con;
	char *tip = NULL;
	const char *id = NULL;

	id = nm_device_get_iface (device);
	if (connection) {
		s_con = nm_connection_get_setting_connection (connection);
		id = nm_setting_connection_get_id (s_con);
	}

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
		tip = g_strdup_printf (_("Preparing network connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_NEED_AUTH:
		tip = g_strdup_printf (_("User authentication required for network connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		tip = g_strdup_printf (_("Requesting a network address for '%s'..."), id);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		tip = g_strdup_printf (_("Network connection '%s' active"), id);
		break;
	default:
		break;
	}

	return tip;
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
	if (dclass) {
		NMConnection *connection;

		connection = applet_find_active_connection_for_device (device, applet, NULL);
		/* device class returns a referenced pixbuf */
		pixbuf = dclass->get_icon (device, state, connection, tip, applet);
		if (!*tip)
			*tip = get_tip_for_device_state (device, state, connection);
	}

out:
	if (!pixbuf) {
		pixbuf = applet_common_get_device_icon (state, applet);
		/* reference the pixbuf to match the device class' get_icon() function behavior */
		if (pixbuf)
			g_object_ref (pixbuf);
	}
	return pixbuf;
}

static char *
get_tip_for_vpn (NMActiveConnection *active, NMVPNConnectionState state, NMApplet *applet)
{
	char *tip = NULL;
	const char *path, *id = NULL;
	GSList *iter, *list;

	path = nm_active_connection_get_connection (active);
	g_return_val_if_fail (path != NULL, NULL);

	list = applet_get_all_connections (applet);
	for (iter = list; iter; iter = g_slist_next (iter)) {
		NMConnection *candidate = NM_CONNECTION (iter->data);
		NMSettingConnection *s_con;

		if (!strcmp (nm_connection_get_path (candidate), path)) {
			s_con = nm_connection_get_setting_connection (candidate);
			id = nm_setting_connection_get_id (s_con);
			break;
		}
	}
	g_slist_free (list);

	if (!id)
		return NULL;

	switch (state) {
	case NM_VPN_CONNECTION_STATE_CONNECT:
	case NM_VPN_CONNECTION_STATE_PREPARE:
		tip = g_strdup_printf (_("Starting VPN connection '%s'..."), id);
		break;
	case NM_VPN_CONNECTION_STATE_NEED_AUTH:
		tip = g_strdup_printf (_("User authentication required for VPN connection '%s'..."), id);
		break;
	case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
		tip = g_strdup_printf (_("Requesting a VPN address for '%s'..."), id);
		break;
	case NM_VPN_CONNECTION_STATE_ACTIVATED:
		tip = g_strdup_printf (_("VPN connection '%s' active"), id);
		break;
	default:
		break;
	}

	return tip;
}

static gboolean
applet_update_icon (gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GdkPixbuf *pixbuf = NULL;
	NMState state;
	char *dev_tip = NULL, *vpn_tip = NULL;
	NMVPNConnectionState vpn_state = NM_VPN_SERVICE_STATE_UNKNOWN;
	gboolean nm_running;
	NMActiveConnection *active_vpn = NULL;

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
		pixbuf = nma_icon_check_and_load ("nm-no-connection", &applet->no_connection_icon, applet);
		g_object_ref (pixbuf);
		dev_tip = g_strdup (_("Networking disabled"));
		break;
	case NM_STATE_DISCONNECTED:
		pixbuf = nma_icon_check_and_load ("nm-no-connection", &applet->no_connection_icon, applet);
		g_object_ref (pixbuf);
		dev_tip = g_strdup (_("No network connection"));
		break;
	default:
		pixbuf = applet_get_device_icon_for_state (applet, &dev_tip);
		break;
	}

	foo_set_icon (applet, pixbuf, ICON_LAYER_LINK);
	if (pixbuf)
		g_object_unref (pixbuf);

	/* VPN state next */
	pixbuf = NULL;
	active_vpn = applet_get_first_active_vpn_connection (applet, &vpn_state);
	if (active_vpn) {
		int i;

		switch (vpn_state) {
		case NM_VPN_CONNECTION_STATE_ACTIVATED:
			pixbuf = nma_icon_check_and_load ("nm-vpn-active-lock", &applet->vpn_lock_icon, applet);
			break;
		case NM_VPN_CONNECTION_STATE_PREPARE:
		case NM_VPN_CONNECTION_STATE_NEED_AUTH:
		case NM_VPN_CONNECTION_STATE_CONNECT:
		case NM_VPN_CONNECTION_STATE_IP_CONFIG_GET:
			for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++) {
				char *name;

				name = g_strdup_printf ("nm-vpn-connecting%02d", i+1);
				nma_icon_check_and_load (name, &applet->vpn_connecting_icons[i], applet);
				g_free (name);
			}

			pixbuf = applet->vpn_connecting_icons[applet->animation_step];
			applet->animation_step++;
			if (applet->animation_step >= NUM_VPN_CONNECTING_FRAMES)
				applet->animation_step = 0;
			break;
		default:
			break;
		}

		vpn_tip = get_tip_for_vpn (active_vpn, vpn_state, applet);
	}
	foo_set_icon (applet, pixbuf, ICON_LAYER_VPN);

	g_free (applet->tip);
	applet->tip = NULL;

	if (dev_tip || vpn_tip) {
		GString *tip;

		tip = g_string_new (dev_tip);

		if (vpn_tip)
			g_string_append_printf (tip, "%s%s", tip->len ? "\n" : "", vpn_tip);

		if (tip->len)
			applet->tip = tip->str;

		g_free (vpn_tip);
		g_free (dev_tip);
		g_string_free (tip, FALSE);
	}

	gtk_status_icon_set_tooltip_text (applet->status_icon, applet->tip);

	return FALSE;
}

void
applet_schedule_update_icon (NMApplet *applet)
{
	if (!applet->update_icon_id)
		applet->update_icon_id = g_idle_add (applet_update_icon, applet);
}

/*****************************************************************************/

static SecretsRequest *
applet_secrets_request_new (size_t totsize,
                            NMConnection *connection,
                            gpointer request_id,
                            const char *setting_name,
                            const char **hints,
                            guint32 flags,
                            AppletAgentSecretsCallback callback,
                            gpointer callback_data,
                            NMApplet *applet)
{
	SecretsRequest *req;

	g_return_val_if_fail (totsize >= sizeof (SecretsRequest), NULL);
	g_return_val_if_fail (connection != NULL, NULL);

	req = g_malloc0 (totsize);
	req->totsize = totsize;
	req->connection = g_object_ref (connection);
	req->reqid = request_id;
	req->setting_name = g_strdup (setting_name);
	req->hints = g_strdupv ((char **) hints);
	req->flags = flags;
	req->callback = callback;
	req->callback_data = callback_data;
	req->applet = applet;
	return req;
}

void
applet_secrets_request_set_free_func (SecretsRequest *req,
                                      SecretsRequestFreeFunc free_func)
{
	req->free_func = free_func;
}

void
applet_secrets_request_complete (SecretsRequest *req,
                                 GHashTable *settings,
                                 GError *error)
{
	req->callback (req->applet->agent, error ? NULL : settings, error, req->callback_data);
}

void
applet_secrets_request_complete_setting (SecretsRequest *req,
                                         const char *setting_name,
                                         GError *error)
{
	NMSetting *setting;
	GHashTable *settings = NULL, *secrets;

	if (setting_name && !error) {
		setting = nm_connection_get_setting_by_name (req->connection, setting_name);
		if (setting) {
			secrets = nm_setting_to_hash (NM_SETTING (setting), NM_SETTING_HASH_FLAG_ALL);
			if (secrets) {
				/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
				 * will contain all the individual settings hashes.
				 */
				settings = g_hash_table_new_full (g_str_hash,
				                                  g_str_equal,
				                                  g_free,
				                                  (GDestroyNotify) g_hash_table_destroy);
				g_hash_table_insert (settings, g_strdup (setting_name), secrets);
			} else {
				g_set_error (&error,
						     NM_SECRET_AGENT_ERROR,
						     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
						     "%s.%d (%s): failed to hash setting '%s'.",
						     __FILE__, __LINE__, __func__, setting_name);
			}
		} else {
			g_set_error (&error,
				         NM_SECRET_AGENT_ERROR,
				         NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
				         "%s.%d (%s): unhandled setting '%s'",
				         __FILE__, __LINE__, __func__, setting_name);
		}
	}

	req->callback (req->applet->agent, settings, error, req->callback_data);
}

void
applet_secrets_request_free (SecretsRequest *req)
{
	g_return_if_fail (req != NULL);

	if (req->free_func)
		req->free_func (req);

	req->applet->secrets_reqs = g_slist_remove (req->applet->secrets_reqs, req);

	g_object_unref (req->connection);
	g_free (req->setting_name);
	g_strfreev (req->hints);
	memset (req, 0, req->totsize);
	g_free (req);
}

static void
get_existing_secrets_cb (NMSecretAgent *agent,
                         NMConnection *connection,
                         GHashTable *secrets,
                         GError *secrets_error,
                         gpointer user_data)
{
	SecretsRequest *req = user_data;
	NMADeviceClass *dclass;
	GError *error = NULL;

	/* Merge existing secrets into connection; ignore errors */
	nm_connection_update_secrets (connection, req->setting_name, secrets, NULL);

	dclass = get_device_class_from_connection (connection, req->applet);
	g_assert (dclass);

	/* Let the device class handle secrets */
	if (!dclass->get_secrets (req, &error)) {
		g_warning ("%s:%d - %s", __func__, __LINE__, error ? error->message : "(unknown)");
		applet_secrets_request_complete (req, NULL, error);
		applet_secrets_request_free (req);
		g_error_free (error);
	}
	/* Otherwise success; wait for the secrets callback */
}

static void
applet_agent_get_secrets_cb (AppletAgent *agent,
                             gpointer request_id,
                             NMConnection *connection,
                             const char *setting_name,
                             const char **hints,
                             guint32 flags,
                             AppletAgentSecretsCallback callback,
                             gpointer callback_data,
                             gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	NMSettingConnection *s_con;
	NMADeviceClass *dclass;
	GError *error = NULL;
	SecretsRequest *req = NULL;

	s_con = nm_connection_get_setting_connection (connection);
	g_return_if_fail (s_con != NULL);

	/* VPN secrets get handled a bit differently */
	if (!strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_VPN_SETTING_NAME)) {
		req = applet_secrets_request_new (applet_vpn_request_get_secrets_size (),
		                                  connection,
		                                  request_id,
		                                  setting_name,
		                                  hints,
		                                  flags,
		                                  callback,
		                                  callback_data,
		                                  applet);
		if (!applet_vpn_request_get_secrets (req, &error))
			goto error;

		applet->secrets_reqs = g_slist_prepend (applet->secrets_reqs, req);
		return;
	}

	dclass = get_device_class_from_connection (connection, applet);
	if (!dclass) {
		error = g_error_new (NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "%s.%d (%s): device type unknown",
		                     __FILE__, __LINE__, __func__);
		goto error;
	}

	if (!dclass->get_secrets) {
		error = g_error_new (NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_NO_SECRETS,
		                     "%s.%d (%s): no secrets found",
		                     __FILE__, __LINE__, __func__);
		goto error;
	}

	g_assert (dclass->secrets_request_size);
	req = applet_secrets_request_new (dclass->secrets_request_size,
	                                  connection,
	                                  request_id,
	                                  setting_name,
	                                  hints,
	                                  flags,
	                                  callback,
	                                  callback_data,
	                                  applet);
	applet->secrets_reqs = g_slist_prepend (applet->secrets_reqs, req);

	/* Get existing secrets, if any */
	nm_secret_agent_get_secrets (NM_SECRET_AGENT (applet->agent),
			                     connection,
			                     setting_name,
			                     hints,
			                     NM_SECRET_AGENT_GET_SECRETS_FLAG_NONE,
			                     get_existing_secrets_cb,
			                     req);
	return;

error:
	g_warning ("%s", error->message);
	callback (agent, NULL, error, callback_data);
	g_error_free (error);

	if (req)
		applet_secrets_request_free (req);
}

static void
applet_agent_cancel_secrets_cb (AppletAgent *agent,
                                gpointer request_id,
                                gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);
	GSList *iter;

	for (iter = applet->secrets_reqs; iter; iter = g_slist_next (iter)) {
		SecretsRequest *req = iter->data;

		if (req->reqid == request_id) {
			/* cancel and free this password request */
			applet_secrets_request_free (req);
		}
	}
}

static void
applet_agent_registered_cb (AppletAgent *agent,
                            GParamSpec *pspec,
                            gpointer user_data)
{
	NMApplet *applet = NM_APPLET (user_data);

	/* If the shell is running and the agent just got registered, unregister it */
	if (   (nm_shell_watcher_version_at_least (applet->shell_watcher, 3, 4))
	    && nm_secret_agent_get_registered (NM_SECRET_AGENT (agent))) {
		g_message ("Stopping registered applet secret agent because GNOME Shell is running");
		nm_secret_agent_unregister (NM_SECRET_AGENT (agent));
	}
}

/*****************************************************************************/

static void
nma_clear_icon (GdkPixbuf **icon, NMApplet *applet)
{
	g_return_if_fail (icon != NULL);
	g_return_if_fail (applet != NULL);

	if (*icon && (*icon != applet->fallback_icon)) {
		g_object_unref (*icon);
		*icon = NULL;
	}
}

static void nma_icons_free (NMApplet *applet)
{
	int i, j;

	for (i = 0; i <= ICON_LAYER_MAX; i++)
		nma_clear_icon (&applet->icon_layers[i], applet);

	nma_clear_icon (&applet->no_connection_icon, applet);
	nma_clear_icon (&applet->wired_icon, applet);
	nma_clear_icon (&applet->adhoc_icon, applet);
	nma_clear_icon (&applet->wwan_icon, applet);
	nma_clear_icon (&applet->wwan_tower_icon, applet);
	nma_clear_icon (&applet->vpn_lock_icon, applet);
	nma_clear_icon (&applet->wireless_00_icon, applet);
	nma_clear_icon (&applet->wireless_25_icon, applet);
	nma_clear_icon (&applet->wireless_50_icon, applet);
	nma_clear_icon (&applet->wireless_75_icon, applet);
	nma_clear_icon (&applet->wireless_100_icon, applet);
	nma_clear_icon (&applet->secure_lock_icon, applet);

	nma_clear_icon (&applet->mb_tech_1x_icon, applet);
	nma_clear_icon (&applet->mb_tech_evdo_icon, applet);
	nma_clear_icon (&applet->mb_tech_gprs_icon, applet);
	nma_clear_icon (&applet->mb_tech_edge_icon, applet);
	nma_clear_icon (&applet->mb_tech_umts_icon, applet);
	nma_clear_icon (&applet->mb_tech_hspa_icon, applet);
	nma_clear_icon (&applet->mb_roaming_icon, applet);
	nma_clear_icon (&applet->mb_tech_3g_icon, applet);

	for (i = 0; i < NUM_CONNECTING_STAGES; i++) {
		for (j = 0; j < NUM_CONNECTING_FRAMES; j++)
			nma_clear_icon (&applet->network_connecting_icons[i][j], applet);
	}

	for (i = 0; i < NUM_VPN_CONNECTING_FRAMES; i++)
		nma_clear_icon (&applet->vpn_connecting_icons[i], applet);

	for (i = 0; i <= ICON_LAYER_MAX; i++)
		nma_clear_icon (&applet->icon_layers[i], applet);
}

GdkPixbuf *
nma_icon_check_and_load (const char *name, GdkPixbuf **icon, NMApplet *applet)
{
	GError *error = NULL;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (icon != NULL, NULL);
	g_return_val_if_fail (applet != NULL, NULL);

	/* icon already loaded successfully */
	if (*icon && (*icon != applet->fallback_icon))
		return *icon;

	/* Try to load the icon; if the load fails, log the problem, and set
	 * the icon to the fallback icon if requested.
	 */
	*icon = gtk_icon_theme_load_icon (applet->icon_theme, name, applet->icon_size, 0, &error);
	if (!*icon) {
		g_warning ("Icon %s missing: (%d) %s",
		           name,
		           error ? error->code : -1,
			       (error && error->message) ? error->message : "(unknown)");
		g_clear_error (&error);

		*icon = applet->fallback_icon;
	}
	return *icon;
}

#include "fallback-icon.h"

static gboolean
nma_icons_reload (NMApplet *applet)
{
	GError *error = NULL;
	GdkPixbufLoader *loader;

	g_return_val_if_fail (applet->icon_size > 0, FALSE);

	nma_icons_free (applet);

	loader = gdk_pixbuf_loader_new_with_type ("png", &error);
	if (!loader)
		goto error;

	if (!gdk_pixbuf_loader_write (loader,
	                              fallback_icon_data,
	                              sizeof (fallback_icon_data),
	                              &error))
		goto error;

	if (!gdk_pixbuf_loader_close (loader, &error))
		goto error;

	applet->fallback_icon = gdk_pixbuf_loader_get_pixbuf (loader);
	g_object_ref (applet->fallback_icon);
	g_assert (applet->fallback_icon);
	g_object_unref (loader);

	return TRUE;

error:
	g_warning ("Could not load fallback icon: (%d) %s",
	           error ? error->code : -1,
		       (error && error->message) ? error->message : "(unknown)");
	g_clear_error (&error);
	/* Die if we can't get a fallback icon */
	g_assert (FALSE);
	return FALSE;
}

static void nma_icon_theme_changed (GtkIconTheme *icon_theme, NMApplet *applet)
{
	nma_icons_reload (applet);
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

	screen = gtk_status_icon_get_screen (applet->status_icon);
	g_assert (screen);
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
	if (getenv ("NMA_SIZE_DEBUG")) {
		g_message ("%s(): status icon size now %d", __func__, size);
	}

	/* icon_size may be 0 if for example the panel hasn't given us any space
	 * yet.  We'll get resized later, but for now just load the 16x16 icons.
	 */
	applet->icon_size = MAX (16, size);

	nma_icons_reload (applet);

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

	/* Kill any old menu */
	if (applet->menu)
		g_object_unref (applet->menu);

	/* And make a fresh new one */
	applet->menu = gtk_menu_new ();
	/* Sink the ref so we can explicitly destroy the menu later */
	g_object_ref_sink (G_OBJECT (applet->menu));

	gtk_container_set_border_width (GTK_CONTAINER (applet->menu), 0);
	g_signal_connect (applet->menu, "show", G_CALLBACK (nma_menu_show_cb), applet);
	g_signal_connect (applet->menu, "deactivate", G_CALLBACK (nma_menu_deactivate_cb), applet);

	/* Display the new menu */
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
	if (shell_debug)
		gtk_status_icon_set_name (applet->status_icon, "adsfasdfasdfadfasdf");

	g_signal_connect (applet->status_icon, "notify::screen",
			  G_CALLBACK (status_icon_screen_changed_cb), applet);
	g_signal_connect (applet->status_icon, "size-changed",
			  G_CALLBACK (status_icon_size_changed_cb), applet);
	g_signal_connect (applet->status_icon, "activate",
			  G_CALLBACK (status_icon_activate_cb), applet);
	g_signal_connect (applet->status_icon, "popup-menu",
			  G_CALLBACK (status_icon_popup_menu_cb), applet);

	applet->context_menu = nma_context_menu_create (applet);
	if (!applet->context_menu)
		return FALSE;

	return TRUE;
}

static void
applet_embedded_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
	gboolean embedded = gtk_status_icon_is_embedded (GTK_STATUS_ICON (object));

	g_message ("applet now %s the notification area",
	           embedded ? "embedded in" : "removed from");
}

#if GLIB_CHECK_VERSION(2,26,0)
static gboolean
delayed_start_agent (gpointer user_data)
{
	NMApplet *applet = user_data;

	applet->agent_start_id = 0;

	g_assert (applet->agent);

	/* If the agent is already running, there's nothing to do. */
	if (nm_secret_agent_get_registered (NM_SECRET_AGENT (applet->agent)) == TRUE)
		return FALSE;

	if (nm_secret_agent_register (NM_SECRET_AGENT (applet->agent)))
		g_message ("Starting applet secret agent because GNOME Shell disappeared");
	else
		g_warning ("Failed to start applet secret agent!");
	return FALSE;
}

static void
shell_version_changed_cb (NMShellWatcher *watcher, GParamSpec *pspec, gpointer user_data)
{
	NMApplet *applet = user_data;

	if (applet->agent_start_id) {
		g_source_remove (applet->agent_start_id);
		applet->agent_start_id = 0;
	}

	if (!applet->agent)
		return;

	if (nm_shell_watcher_version_at_least (watcher, 3, 4)) {
		/* GNOME Shell handles all secrets requests */
		if (nm_secret_agent_get_registered (NM_SECRET_AGENT (applet->agent))) {
			g_message ("Stopping applet secret agent because GNOME Shell appeared");
			nm_secret_agent_unregister (NM_SECRET_AGENT (applet->agent));
		}
	} else if (nm_shell_watcher_version_at_least (watcher, 3, 2)) {
		/* GNOME Shell handles everything except VPN secrets requests */
		if (nm_secret_agent_get_registered (NM_SECRET_AGENT (applet->agent)))
			g_message ("Applet secret agent handling only VPN secrets because GNOME Shell appeared");
		applet_agent_handle_vpn_only (applet->agent, TRUE);
	} else {
		/* If the shell quit and our agent wasn't already registered, do it
		 * now on a delay (just in case the shell is restarting).
		 */
		if (!nm_secret_agent_get_registered (NM_SECRET_AGENT (applet->agent)))
			applet->agent_start_id = g_timeout_add_seconds (4, delayed_start_agent, applet);
		applet_agent_handle_vpn_only (applet->agent, FALSE);
	}
}
#endif

static gboolean
dbus_setup (NMApplet *applet, GError **error)
{
	DBusConnection *connection;
	DBusGProxy *proxy;
	guint result;
	gboolean success;

	applet->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, error);
	if (!applet->bus)
		return FALSE;

	connection = dbus_g_connection_get_connection (applet->bus);
	dbus_connection_set_exit_on_disconnect (connection, FALSE);

	applet->session_bus = dbus_g_bus_get (DBUS_BUS_SESSION, error);
	if (!applet->session_bus)
		return FALSE;

	dbus_g_connection_register_g_object (applet->session_bus,
	                                     "/org/gnome/network_manager_applet",
	                                     G_OBJECT (applet));

	proxy = dbus_g_proxy_new_for_name (applet->session_bus,
	                                   DBUS_SERVICE_DBUS,
	                                   DBUS_PATH_DBUS,
	                                   DBUS_INTERFACE_DBUS);
	success = dbus_g_proxy_call (proxy, "RequestName", error,
	                             G_TYPE_STRING, "org.gnome.network_manager_applet",
	                             G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
	                             G_TYPE_INVALID,
	                             G_TYPE_UINT, &result,
	                             G_TYPE_INVALID);
	g_object_unref (proxy);

	return success;
}

static GObject *
constructor (GType type,
             guint n_props,
             GObjectConstructParam *construct_props)
{
	NMApplet *applet;
	GError* error = NULL;

	applet = NM_APPLET (G_OBJECT_CLASS (nma_parent_class)->constructor (type, n_props, construct_props));

	g_set_application_name (_("NetworkManager Applet"));
	gtk_window_set_default_icon_name (GTK_STOCK_NETWORK);

	applet->info_dialog_ui = gtk_builder_new ();

	if (!gtk_builder_add_from_file (applet->info_dialog_ui, UIDIR "/info.ui", &error)) {
		g_warning ("Couldn't load info dialog ui file: %s", error->message);
		g_error_free (error);
		goto error;
	}

	applet->gsettings = g_settings_new (APPLET_PREFS_SCHEMA);

	/* Load pixmaps and create applet widgets */
	if (!setup_widgets (applet))
		goto error;
	nma_icons_init (applet);

	if (!notify_is_initted ())
		notify_init ("NetworkManager");

	if (!dbus_setup (applet, &error)) {
		g_warning ("Failed to initialize D-Bus: %s", error->message);
		g_error_free (error);
		goto error;
	}
	applet->settings = nm_remote_settings_new (applet->bus);

#ifdef BUILD_MIGRATION_TOOL
	{
		char *argv[2] = { LIBEXECDIR "/nm-applet-migration-tool", NULL };
		int status;

		/* Move user connections to the system */
		if (!g_spawn_sync (NULL, argv, NULL, 0, NULL, NULL,
						   NULL, NULL, &status, &error)) {
			g_warning ("Could not run nm-applet-migration-tool: %s",
					   error->message);
			g_error_free (error);
		} else if (!WIFEXITED (status) || WEXITSTATUS (status) != 0) {
			g_warning ("nm-applet-migration-tool exited with error");
		}
	}
#endif

	applet->agent = applet_agent_new ();
	g_assert (applet->agent);
	g_signal_connect (applet->agent, APPLET_AGENT_GET_SECRETS,
	                  G_CALLBACK (applet_agent_get_secrets_cb), applet);
	g_signal_connect (applet->agent, APPLET_AGENT_CANCEL_SECRETS,
	                  G_CALLBACK (applet_agent_cancel_secrets_cb), applet);
	g_signal_connect (applet->agent, "notify::" NM_SECRET_AGENT_REGISTERED,
	                  G_CALLBACK (applet_agent_registered_cb), applet);

	/* Initialize device classes */
	applet->wired_class = applet_device_wired_get_class (applet);
	g_assert (applet->wired_class);

	applet->wifi_class = applet_device_wifi_get_class (applet);
	g_assert (applet->wifi_class);

	applet->gsm_class = applet_device_gsm_get_class (applet);
	g_assert (applet->gsm_class);

	applet->cdma_class = applet_device_cdma_get_class (applet);
	g_assert (applet->cdma_class);

	applet->bt_class = applet_device_bt_get_class (applet);
	g_assert (applet->bt_class);

	applet->wimax_class = applet_device_wimax_get_class (applet);
	g_assert (applet->wimax_class);

	foo_client_setup (applet);

	/* Track embedding to help debug issues where user has removed the
	 * notification area applet from the panel, and thus nm-applet too.
	 */
	g_signal_connect (applet->status_icon, "notify::embedded",
	                  G_CALLBACK (applet_embedded_cb), NULL);
	applet_embedded_cb (G_OBJECT (applet->status_icon), NULL, NULL);

#if GLIB_CHECK_VERSION(2,26,0)
	/* Watch GNOME Shell so we can unregister our applet agent if it appears */
	applet->shell_watcher = nm_shell_watcher_new ();
	g_signal_connect (applet->shell_watcher,
	                  "notify::shell-version",
	                  G_CALLBACK (shell_version_changed_cb),
	                  applet);
#endif

	return G_OBJECT (applet);

error:
	g_object_unref (applet);
	return NULL;
}

static void finalize (GObject *object)
{
	NMApplet *applet = NM_APPLET (object);

	g_slice_free (NMADeviceClass, applet->wired_class);
	g_slice_free (NMADeviceClass, applet->wifi_class);
	g_slice_free (NMADeviceClass, applet->gsm_class);
	g_slice_free (NMADeviceClass, applet->cdma_class);
	g_slice_free (NMADeviceClass, applet->bt_class);
	g_slice_free (NMADeviceClass, applet->wimax_class);

	if (applet->update_icon_id)
		g_source_remove (applet->update_icon_id);

	if (applet->menu)
		g_object_unref (applet->menu);
	nma_icons_free (applet);

	g_free (applet->tip);

	while (g_slist_length (applet->secrets_reqs))
		applet_secrets_request_free ((SecretsRequest *) applet->secrets_reqs->data);

	if (applet->notification) {
		notify_notification_close (applet->notification, NULL);
		g_object_unref (applet->notification);
	}

	if (applet->info_dialog_ui)
		g_object_unref (applet->info_dialog_ui);

	if (applet->gsettings)
		g_object_unref (applet->gsettings);

	if (applet->status_icon)
		g_object_unref (applet->status_icon);

	if (applet->nm_client)
		g_object_unref (applet->nm_client);

	if (applet->fallback_icon)
		g_object_unref (applet->fallback_icon);

	if (applet->agent)
		g_object_unref (applet->agent);

	if (applet->settings)
		g_object_unref (applet->settings);

	if (applet->bus)
		dbus_g_connection_unref (applet->bus);

	if (applet->session_bus)
		dbus_g_connection_unref (applet->session_bus);

#if GLIB_CHECK_VERSION(2,26,0)
	if (applet->shell_watcher)
		g_object_unref (applet->shell_watcher);
#endif
	if (applet->agent_start_id)
		g_source_remove (applet->agent_start_id);

	G_OBJECT_CLASS (nma_parent_class)->finalize (object);
}

static void nma_init (NMApplet *applet)
{
	applet->animation_id = 0;
	applet->animation_step = 0;
	applet->icon_theme = NULL;
	applet->notification = NULL;
	applet->icon_size = 16;
}

enum {
	PROP_0,
	PROP_LOOP,
	LAST_PROP
};

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMApplet *applet = NM_APPLET (object);

	switch (prop_id) {
	case PROP_LOOP:
		applet->loop = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void nma_class_init (NMAppletClass *klass)
{
	GObjectClass *oclass = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	oclass->set_property = set_property;
	oclass->constructor = constructor;
	oclass->finalize = finalize;

	pspec = g_param_spec_pointer ("loop", "Loop", "Applet mainloop", G_PARAM_CONSTRUCT | G_PARAM_WRITABLE);
	g_object_class_install_property (oclass, PROP_LOOP, pspec);

	dbus_g_object_type_install_info (NM_TYPE_APPLET, &dbus_glib_nma_object_info);
}

NMApplet *
nm_applet_new (GMainLoop *loop)
{
	return g_object_new (NM_TYPE_APPLET, "loop", loop, NULL);
}

