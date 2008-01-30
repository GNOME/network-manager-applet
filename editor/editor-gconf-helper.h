/* Wireless Security Editor gconf helper functions
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


#ifndef EDITOR_GCONF_HELPER_H
#define EDITOR_GCONF_HELPER_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnome/libgnome.h>

#if !GTK_CHECK_VERSION(2,6,0)
#include <gnome.h>
#endif

#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <NetworkManager.h>

#include "editor-app.h"

void eh_gconf_client_set_string(WE_DATA *we_data, gchar *subkey, gchar *value);
gchar *eh_gconf_client_get_string(WE_DATA *we_data, gchar *subkey);
void eh_gconf_client_set_int(WE_DATA *we_data, gchar *subkey, gint value);
gint eh_gconf_client_get_int(WE_DATA *we_data, gchar *subkey);
void eh_gconf_client_unset(WE_DATA *we_data, gchar *subkey);

#endif // EDITOR_GCONF_HELPER_H






