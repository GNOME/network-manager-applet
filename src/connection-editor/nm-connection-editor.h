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

#ifndef NM_CONNECTION_EDITOR_H
#define NM_CONNECTION_EDITOR_H

#include <glib-object.h>
#include <nm-connection.h>
#include <glade/glade-xml.h>

#define NM_TYPE_CONNECTION_EDITOR    (nm_connection_editor_get_type ())
#define NM_IS_CONNECTION_EDITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_CONNECTION_EDITOR))
#define NM_CONNECTION_EDITOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_CONNECTION_EDITOR, NMConnectionEditor))

typedef enum {
	NM_CONNECTION_EDITOR_PAGE_DEFAULT = 0,
	NM_CONNECTION_EDITOR_PAGE_ETHERNET = 1 << 0,
	NM_CONNECTION_EDITOR_PAGE_WIRELESS = 1 << 1,
	NM_CONNECTION_EDITOR_PAGE_WIRELESS_SECURITY = 1 << 2
} NMConnectionEditorPage;

typedef struct {
	GObject parent;

	/* private data */
	NMConnection *connection;
	NMConnectionEditorPage pages;

	GladeXML *gui;
	GtkWidget *connection_name;
	GtkWidget *connection_autoconnect;
	GtkWidget *ethernet_port;
	GtkWidget *ethernet_speed;
	GtkWidget *ethernet_duplex;
	GtkWidget *ethernet_autonegotiate;
	GtkWidget *ethernet_mtu;
	GtkWidget *wireless_mode;
	GtkWidget *wireless_band;
	GtkWidget *wireless_channel;
	GtkWidget *wireless_rate;
	GtkWidget *wireless_tx_power;
	GtkWidget *wireless_mtu;
} NMConnectionEditor;

typedef struct {
	GObjectClass parent_class;
} NMConnectionEditorClass;

GType               nm_connection_editor_get_type (void);
NMConnectionEditor *nm_connection_editor_new (NMConnection *connection, NMConnectionEditorPage pages);

NMConnection       *nm_connection_editor_get_connection (NMConnectionEditor *editor);
void                nm_connection_editor_set_connection (NMConnectionEditor *editor, NMConnection *connection);

#endif
