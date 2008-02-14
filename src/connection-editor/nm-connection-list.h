/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
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
 * (C) Copyright 2004-2005 Red Hat, Inc.
 */

#ifndef NM_CONNECTION_LIST_H
#define NM_CONNECTION_LIST_H

#include <glib-object.h>
#include <glade/glade-xml.h>
#include <gconf/gconf-client.h>
#include <gdk/gdkpixbuf.h>
#include <gtk/gtk.h>
#include <gtk/gtkicontheme.h>

#define NM_TYPE_CONNECTION_LIST    (nm_connection_list_get_type ())
#define NM_IS_CONNECTION_LIST(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_CONNECTION_LIST))
#define NM_CONNECTION_LIST(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_CONNECTION_LIST, NMConnectionList))

typedef struct {
	GObject parent;

	/* private data */
	GHashTable *connections;

	GConfClient *client;

	GladeXML *gui;
	GtkWidget *dialog;

	GdkPixbuf *wired_icon;
	GdkPixbuf *wireless_icon;
	GdkPixbuf *wwan_icon;
	GdkPixbuf *vpn_icon;
	GdkPixbuf *unknown_icon;
	GtkIconTheme *icon_theme;
} NMConnectionList;

typedef struct {
	GObjectClass parent_class;
} NMConnectionListClass;

GType             nm_connection_list_get_type (void);
NMConnectionList *nm_connection_list_new (void);

void              nm_connection_list_show (NMConnectionList *list);
gint              nm_connection_list_run_and_close (NMConnectionList *list);

#endif
