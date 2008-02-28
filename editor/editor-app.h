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

#include <iwlib.h>

// Security Options for Combo Box
#define SEC_OPTION_NONE				0
#define SEC_OPTION_WEP_PASSPHRASE	1
#define SEC_OPTION_WEP_HEX			2
#define SEC_OPTION_WEP_ASCII		3
#define SEC_OPTION_WPA_PERSONAL		4
#define SEC_OPTION_WPA2_PERSONAL	5
#define SEC_OPTION_WPA_ENTERPRISE	6
#define SEC_OPTION_WPA2_ENTERPRISE	7
#define SEC_OPTION_LEAP				8


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

	guint       sec_option;
} WE_DATA;




#endif // EDITOR_H
