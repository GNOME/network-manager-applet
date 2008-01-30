/* NetworkManager Wireless Editor -- Edits wireless access points 
 *
 * Calvin Gaisford <cgaisford@novell.com>
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
 * (C) Copyright 2006 Novell, Inc.
 */

#ifndef EDITOR_H
#define EDITOR_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <net/ethernet.h>


// FIX THIS... these should be included from the proper files!
/* IW_AUTH_WPA_VERSION values (bit field) */
#define IW_AUTH_WPA_VERSION_DISABLED    0x00000001
#define IW_AUTH_WPA_VERSION_WPA     0x00000002
#define IW_AUTH_WPA_VERSION_WPA2    0x00000004

/* IW_AUTH_PAIRWISE_CIPHER and IW_AUTH_GROUP_CIPHER values (bit field) */
#define IW_AUTH_CIPHER_NONE 0x00000001
#define IW_AUTH_CIPHER_WEP40    0x00000002
#define IW_AUTH_CIPHER_TKIP 0x00000004
#define IW_AUTH_CIPHER_CCMP 0x00000008
#define IW_AUTH_CIPHER_WEP104   0x00000010

/* IW_AUTH_80211_AUTH_ALG values (bit field) */
#define IW_AUTH_ALG_OPEN_SYSTEM	0x00000001
#define IW_AUTH_ALG_SHARED_KEY	0x00000002
#define IW_AUTH_ALG_LEAP	0x00000004


typedef struct _wireless_editor_data
{
	gchar		*glade_file;
	GladeXML	*editor_xml;
	GladeXML	*sub_xml;
	GConfClient	*gconf_client;
	gchar		*cur_gconf_dir;
	GtkWidget	*window;
	GtkWidget	*treeview;
	GtkWidget	*remove_button;
	GtkWidget	*essid_entry;
	GtkWidget	*stamp_editor;
	GtkWidget	*security_combo;
	gchar		*essid_value;
	gulong		essid_shid;
	gulong		essid_focus_shid;
	gulong		stamp_date_shid;
	gulong		stamp_time_shid;
	gulong		combo_shid;
} WE_DATA;




#endif // EDITOR_H
