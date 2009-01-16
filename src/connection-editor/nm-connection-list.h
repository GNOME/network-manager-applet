/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <nm-dbus-settings-system.h>
#include "nma-gconf-settings.h"

#define NM_TYPE_CONNECTION_LIST    (nm_connection_list_get_type ())
#define NM_IS_CONNECTION_LIST(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_CONNECTION_LIST))
#define NM_CONNECTION_LIST(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_CONNECTION_LIST, NMConnectionList))

typedef struct {
	GObject parent;

	/* private data */
	GHashTable *editors;
	GHashTable *treeviews;

	GConfClient *client;
	NMAGConfSettings *gconf_settings;
	NMDBusSettingsSystem *system_settings;

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

	/* Signals */
	void (*done)  (NMConnectionList *list, gint result);
} NMConnectionListClass;

GType             nm_connection_list_get_type (void);
NMConnectionList *nm_connection_list_new (const char *def_type);

void              nm_connection_list_run (NMConnectionList *list);

void              nm_connection_list_set_type (NMConnectionList *list, const char *type);

void              nm_connection_list_present (NMConnectionList *list);

#endif
