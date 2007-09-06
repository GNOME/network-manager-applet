/* menu-info.h: Simple menu items for the Applet to use
 *
 * Jonathan Blandford <jrb@redhat.com>
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

#ifndef MENU_ITEMS_H
#define MENU_ITEMS_H

#include <gtk/gtk.h>
#include <gtk/gtkcheckmenuitem.h>
#include "applet.h"
#include "nm-access-point.h"

#include <nm-device-802-3-ethernet.h>
#include <nm-device-802-11-wireless.h>

GtkMenuItem *wired_menu_item_new (NMDevice8023Ethernet *device,
								  gint n_devices);

GtkMenuItem *wireless_menu_item_new (NMDevice80211Wireless *device,
									 gint n_devices);


#define NM_TYPE_NETWORK_MENU_ITEM            (nm_network_menu_item_get_type ())
#define NM_NETWORK_MENU_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_NETWORK_MENU_ITEM, NMNetworkMenuItem))
#define NM_NETWORK_MENU_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_NETWORK_MENU_ITEM, NMNetworkMenuItemClass))
#define NM_IS_NETWORK_MENU_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_NETWORK_MENU_ITEM))
#define NM_IS_NETWORK_MENU_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_NETWORK_MENU_ITEM))
#define NM_NETWORK_MENU_ITEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_NETWORK_MENU_ITEM, NMNetworkMenuItemClass))


typedef struct _NMNetworkMenuItem	    NMNetworkMenuItem;
typedef struct _NMNetworkMenuItemClass  NMNetworkMenuItemClass;

struct _NMNetworkMenuItem
{
	GtkCheckMenuItem check_item;

	/*< private >*/
	GtkWidget * ssid;
	GtkWidget * strength;
	guint32     int_strength;
	GtkWidget * detail;
	GtkWidget * hbox;
	guchar *    hash;
	guint32     hash_len;
	gboolean    destroyed;
};

struct _NMNetworkMenuItemClass
{
	GtkCheckMenuItemClass parent_class;
};


GType	   nm_network_menu_item_get_type (void) G_GNUC_CONST;
GtkWidget* nm_network_menu_item_new (GtkSizeGroup * size_group,
                                     guchar * hash,
                                     guint32 hash_len);
void       nm_network_menu_item_set_ssid (NMNetworkMenuItem * item,
                                          GByteArray * ssid);
guint32    nm_network_menu_item_get_strength (NMNetworkMenuItem * item);
void       nm_network_menu_item_set_strength (NMNetworkMenuItem * item,
                                              guint32 strength);
const guchar * nm_network_menu_item_get_hash (NMNetworkMenuItem * item,
                                              guint32 * length);
void       nm_network_menu_item_set_detail (NMNetworkMenuItem * item,
                                            NMAccessPoint * ap,
                                            GdkPixbuf * adhoc_icon);

/* Helper function; escapes an essid for human readable display. */
char *     nm_menu_network_escape_essid_for_display (const char *essid);


#endif /* MENU_INFO_H */
