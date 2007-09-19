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

#include "nm-connection-editor.h"

int
main (int argc, char *argv[])
{
	NMConnectionEditor *editor;
	NMConnection *connection = NULL;

	gtk_init (&argc, &argv);

	/* parse arguments: an idea is to use gconf://$setting_name / system://$setting_name to
	   allow this program to work with both GConf and system-wide settings */

	editor = nm_connection_editor_new (connection, NM_CONNECTION_EDITOR_PAGE_DEFAULT);
	nm_connection_editor_show (editor);

	gtk_main ();

	/* save connection to GConf/system */

	return 0;
}
