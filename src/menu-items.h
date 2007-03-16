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
#include "applet.h"

#include <nm-device-802-3-ethernet.h>
#include <nm-device-802-11-wireless.h>

typedef struct NMNetworkMenuItem NMNetworkMenuItem;

GtkMenuItem *wired_menu_item_new (NMDevice8023Ethernet *device,
								  gint n_devices);

GtkMenuItem *wireless_menu_item_new (NMDevice80211Wireless *device,
									 gint n_devices);

NMNetworkMenuItem	*network_menu_item_new (GtkSizeGroup *encryption_size_group);
GtkCheckMenuItem	*network_menu_item_get_check_item (NMNetworkMenuItem *item);
void				 network_menu_item_update (NMApplet *applet,
											   NMNetworkMenuItem *item,
											   NMAccessPoint *ap,
											   gboolean is_encrypted);

/* Helper function; escapes an essid for human readable display. */
char      		*nm_menu_network_escape_essid_for_display (const char *essid);


#endif /* MENU_INFO_H */
