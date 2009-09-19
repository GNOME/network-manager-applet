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

#ifndef NM_CONNECTION_EDITOR_H
#define NM_CONNECTION_EDITOR_H

#include "config.h"

#include <glib-object.h>
#include <glade/glade-xml.h>

#include "nm-remote-settings-system.h"

#define NM_TYPE_CONNECTION_EDITOR    (nm_connection_editor_get_type ())
#define NM_IS_CONNECTION_EDITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_CONNECTION_EDITOR))
#define NM_CONNECTION_EDITOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_CONNECTION_EDITOR, NMConnectionEditor))

typedef struct {
	GObject parent;

	/* private data */
	NMConnection *connection;
	NMConnection *orig_connection;
	gboolean initialized;

	NMConnectionScope orig_scope;

	GtkWidget *system_checkbutton;
	gboolean system_settings_can_modify;

	GSList *pages;
	GladeXML *xml;
	GtkWidget *window;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
} NMConnectionEditor;

typedef struct {
	GObjectClass parent_class;

	/* Signals */
	void (*done)  (NMConnectionEditor *editor, gint result);
} NMConnectionEditorClass;

GType               nm_connection_editor_get_type (void);
NMConnectionEditor *nm_connection_editor_new (NMConnection *connection,
                                              NMRemoteSettingsSystem *settings,
                                              GError **error);

void                nm_connection_editor_present (NMConnectionEditor *editor);
void                nm_connection_editor_run (NMConnectionEditor *editor);
void                nm_connection_editor_save_vpn_secrets (NMConnectionEditor *editor);
NMConnection *      nm_connection_editor_get_connection (NMConnectionEditor *editor);
gboolean            nm_connection_editor_update_connection (NMConnectionEditor *editor, GError **error);
GtkWindow *         nm_connection_editor_get_window (NMConnectionEditor *editor);

#endif
