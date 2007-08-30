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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2004 Red Hat, Inc.
 */

#ifndef APPLET_H
#define APPLET_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <net/ethernet.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#ifndef HAVE_STATUS_ICON
#include "eggtrayicon.h"
#endif

#include <nm-client.h>
#include <nm-access-point.h>

#include <nm-device.h>
#include <dbus-method-dispatcher.h>

#ifdef ENABLE_NOTIFY
#include <libnotify/notify.h>
#endif

#include "applet-dbus-manager.h"
#include "applet-dbus-settings.h"

/*
 * D-Bus service stuff
 */
#define APPLET_DBUS_SERVICE_USER_SETTINGS "org.freedesktop.NetworkManagerUserSettings"
#define APPLET_DBUS_PATH_USER_SETTINGS "/org/freedesktop/NetworkManagerUserSettings"
#define APPLET_DBUS_IFACE_USER_SETTINGS "org.freedesktop.NetworkManagerUserSettings"


/*
 * Preference locations
 */
#define GCONF_PATH_CONNECTIONS          "/system/networking/connections"
#define GCONF_PATH_WIRELESS_NETWORKS	"/system/networking/wireless/networks"
#define GCONF_PATH_WIRELESS			"/system/networking/wireless"
#define GCONF_PATH_VPN_CONNECTIONS		"/system/networking/vpn_connections"
#define GCONF_PATH_PREFS				"/apps/NetworkManagerApplet"


typedef struct VPNConnection VPNConnection;


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


#define ICON_LAYER_LINK 0
#define ICON_LAYER_VPN 1
#define ICON_LAYER_MAX ICON_LAYER_VPN

/*
 * Applet instance data
 *
 */
typedef struct
{
	GObject                 parent_instance;

	NMClient *nm_client;
	NMAccessPoint *current_ap;
	gulong wireless_strength_monitor;

	NMSettings * settings;

	GConfClient *		gconf_client;
	guint		 	gconf_prefs_notify_id;
	guint		 	gconf_vpn_notify_id;
	char	*			glade_file;

	/* Data model elements */
	gboolean			is_adhoc;
	gboolean			icons_loaded;

	GtkIconTheme *          icon_theme;
	GdkPixbuf *		no_connection_icon;
	GdkPixbuf *		wired_icon;
	GdkPixbuf *		adhoc_icon;
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
#ifdef HAVE_STATUS_ICON
	GtkStatusIcon *		status_icon;
	int			size;
#else
	EggTrayIcon *		tray_icon;
	GtkWidget *		pixmap;
	GtkWidget *		event_box;
	GtkTooltips *		tooltips;
#endif /* HAVE_STATUS_ICON */

	GtkWidget *		top_menu_item;
	GtkWidget *		dropdown_menu;
	GtkSizeGroup *		encryption_size_group;

	GtkWidget *		context_menu;
	GtkWidget *		enable_networking_item;
	GtkWidget *		stop_wireless_item;
	GtkWidget *		info_menu_item;

	GtkWidget *		passphrase_dialog;
	GladeXML *		info_dialog_xml;
#ifdef ENABLE_NOTIFY
	NotifyNotification*	notification;
#endif
} NMApplet;

GType nma_get_type (void);

NMApplet * nm_applet_new (void);

void				nma_schedule_warning_dialog			(NMApplet *applet, const char *msg);
void				nma_show_vpn_failure_alert			(NMApplet *applet, const char *member, const char *vpn_name, const char *error_msg);
void				nma_show_vpn_login_banner			(NMApplet *applet, const char *vpn_name, const char *banner);

const char * nma_escape_ssid (const char * ssid, guint32 len);

static inline gboolean
nma_same_ssid (const GByteArray * ssid1, const GByteArray * ssid2)
{
	if (ssid1 == ssid2)
		return TRUE;
	if ((ssid1 && !ssid2) || (!ssid1 && ssid2))
		return FALSE;
	if (ssid1->len != ssid2->len)
		return FALSE;

	return memcmp (ssid1->data, ssid2->data, ssid1->len) == 0 ? TRUE : FALSE;
}

#endif
