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

#include <gtk/gtk.h>
#include <dbus/dbus-glib.h>

#include "nm-connection-list.h"

int
main (int argc, char *argv[])
{
	NMConnectionList *list;
	DBusGConnection *ignore;

	gtk_init (&argc, &argv);

	/* parse arguments: an idea is to use gconf://$setting_name / system://$setting_name to
	   allow this program to work with both GConf and system-wide settings */

	/* Hack to init the dbus-glib type system */
	ignore = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	dbus_g_connection_unref (ignore);

	list = nm_connection_list_new ();
	nm_connection_list_run_and_close (list);
	g_object_unref (list);

	return 0;
}
