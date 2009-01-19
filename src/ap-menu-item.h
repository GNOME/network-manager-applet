/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* ap-menu-item.h - Class to represent a Wifi access point 
 *
 * Jonathan Blandford <jrb@redhat.com>
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
 * Copyright (C) 2005 - 2008 Red Hat, Inc.
 */

#ifndef __AP_MENU_ITEM_H__
#define __AP_MENU_ITEM_H__

#include <gtk/gtk.h>
#include "applet.h"
#include "nm-access-point.h"

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
	GSList *    dupes;
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
                                            GdkPixbuf * adhoc_icon,
                                            guint32 dev_caps);

gboolean   nm_network_menu_item_find_dupe (NMNetworkMenuItem *item,
                                           NMAccessPoint *ap);

void       nm_network_menu_item_add_dupe (NMNetworkMenuItem *item,
                                          NMAccessPoint *ap);

#endif /* __AP_MENU_ITEM_H__ */
