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
 * Copyright (C) 2004 - 2008 Red Hat, Inc.
 * Copyright (C) 2005 - 2008 Novell, Inc.
 */

#ifndef APPLET_H
#define APPLET_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <gconf/gconf-client.h>
#include <glade/glade.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <net/ethernet.h>

#include <libnotify/notify.h>

#include <nm-connection.h>
#include <nm-client.h>
#include <nm-access-point.h>
#include <nm-device.h>
#include <NetworkManager.h>
#include <nm-active-connection.h>
#include <nm-dbus-settings.h>

#include "applet-dbus-manager.h"
#include "nma-gconf-settings.h"

#define NM_TYPE_APPLET			(nma_get_type())
#define NM_APPLET(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), NM_TYPE_APPLET, NMApplet))
#define NM_APPLET_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), NM_TYPE_APPLET, NMAppletClass))
#define NM_IS_APPLET(object)		(G_TYPE_CHECK_INSTANCE_TYPE((object), NM_TYPE_APPLET))
#define NM_IS_APPLET_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), NM_TYPE_APPLET))
#define NM_APPLET_GET_CLASS(object)(G_TYPE_INSTANCE_GET_CLASS((object), NM_TYPE_APPLET, NMAppletClass))

typedef struct
{
	GObjectClass	parent_class;
} NMAppletClass; 

#define APPLET_PREFS_PATH "/apps/nm-applet"
#define PREF_DISABLE_CONNECTED_NOTIFICATIONS	APPLET_PREFS_PATH "/disable-connected-notifications"
#define PREF_DISABLE_DISCONNECTED_NOTIFICATIONS	APPLET_PREFS_PATH "/disable-disconnected-notifications"

#define ICON_LAYER_LINK 0
#define ICON_LAYER_VPN 1
#define ICON_LAYER_MAX ICON_LAYER_VPN

typedef struct NMADeviceClass NMADeviceClass;

/*
 * Applet instance data
 *
 */
typedef struct
{
	GObject parent_instance;

	GMainLoop *loop;
	NMClient *nm_client;

	NMDBusSettings *dbus_settings;
	NMAGConfSettings *gconf_settings;

	GConfClient *	gconf_client;
	char	*		glade_file;

	guint update_timestamps_id;

	/* Device classes */
	NMADeviceClass *wired_class;
	NMADeviceClass *wifi_class;
	NMADeviceClass *gsm_class;
	NMADeviceClass *cdma_class;

	/* Data model elements */
	guint			update_icon_id;
	gboolean		icons_loaded;

	GtkIconTheme *	icon_theme;
	GdkPixbuf *		no_connection_icon;
	GdkPixbuf *		wired_icon;
	GdkPixbuf *		adhoc_icon;
	GdkPixbuf *		wwan_icon;
	GdkPixbuf *		wireless_00_icon;
	GdkPixbuf *		wireless_25_icon;
	GdkPixbuf *		wireless_50_icon;
	GdkPixbuf *		wireless_75_icon;
	GdkPixbuf *		wireless_100_icon;
#define NUM_CONNECTING_STAGES 3
#define NUM_CONNECTING_FRAMES 11
	GdkPixbuf *		network_connecting_icons[NUM_CONNECTING_STAGES][NUM_CONNECTING_FRAMES];
#define NUM_VPN_CONNECTING_FRAMES 14
	GdkPixbuf *		vpn_connecting_icons[NUM_VPN_CONNECTING_FRAMES];
	GdkPixbuf *		vpn_lock_icon;

	/* Active status icon pixbufs */
	GdkPixbuf *		icon_layers[ICON_LAYER_MAX + 1];

	/* Animation stuff */
	int				animation_step;
	guint			animation_id;

	/* Direct UI elements */
	GtkStatusIcon *	status_icon;
	int				size;

	GtkWidget *		menu;
	GtkSizeGroup *	encryption_size_group;
	char *          tip;

	GtkWidget *		context_menu;
	GtkWidget *		networking_enabled_item;
	guint           networking_enabled_toggled_id;
	GtkWidget *		wifi_enabled_item;
	guint           wifi_enabled_toggled_id;
	GtkWidget *		info_menu_item;
	GtkWidget *		connections_menu_item;

	GladeXML *		info_dialog_xml;
	NotifyNotification*	notification;
} NMApplet;


struct NMADeviceClass {
	NMConnection * (*new_auto_connection)  (NMDevice *device,
	                                        NMApplet *applet,
	                                        gpointer user_data);

	void           (*add_menu_item)        (NMDevice *device,
	                                        guint32 num_devices,
	                                        NMConnection *active,
	                                        GtkWidget *menu,
	                                        NMApplet *applet);

	void           (*device_added)         (NMDevice *device, NMApplet *applet);

	void           (*device_state_changed) (NMDevice *device,
	                                        NMDeviceState new_state,
	                                        NMDeviceState old_state,
	                                        NMDeviceStateReason reason,
	                                        NMApplet *applet);

	GdkPixbuf *    (*get_icon)             (NMDevice *device,
	                                        NMDeviceState state,
	                                        NMConnection *connection,
	                                        char **tip,
	                                        NMApplet *applet);

	void           (*get_more_info)        (NMDevice *device,
	                                        NMConnection *connection,
	                                        NMApplet *applet,
	                                        gpointer user_data);

	gboolean       (*get_secrets)          (NMDevice *device,
	                                        NMConnection *connection,
	                                        NMActiveConnection *active_connection,
	                                        const char *setting_name,
	                                        const char **hints,
	                                        DBusGMethodInvocation *context,
	                                        NMApplet *applet,
	                                        GError **error);
};

GType nma_get_type (void);

NMApplet *nm_applet_new (GMainLoop *loop);

void applet_schedule_update_icon (NMApplet *applet);

NMSettings *applet_get_settings (NMApplet *applet);

GSList *applet_get_all_connections (NMApplet *applet);

GtkWidget *nma_menu_device_check_unusable (NMDevice *device,
                                           const char *unavailable_msg);

void applet_menu_item_activate_helper (NMDevice *device,
                                       NMConnection *connection,
                                       const char *specific_object,
                                       NMApplet *applet,
                                       gpointer user_data);

NMAGConfConnection *applet_get_exported_connection_for_device (NMDevice *device, NMApplet *applet);

void applet_do_notify (NMApplet *applet,
                       NotifyUrgency urgency,
                       const char *summary,
                       const char *message,
                       const char *icon,
                       const char *action1,
                       const char *action1_label,
                       NotifyActionCallback action1_cb,
                       gpointer action1_user_data);

void applet_do_notify_with_pref (NMApplet *applet,
                                 const char *summary,
                                 const char *message,
                                 const char *icon,
                                 const char *pref);

NMConnection * applet_find_active_connection_for_device (NMDevice *device,
                                                         NMApplet *applet,
                                                         NMActiveConnection **out_active);

#endif
