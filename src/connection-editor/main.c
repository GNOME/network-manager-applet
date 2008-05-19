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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <dbus/dbus-glib.h>

#include "nm-connection-list.h"

static GMainLoop *loop = NULL;

static void
signal_handler (int signo)
{
	if (signo == SIGINT || signo == SIGTERM) {
		g_message ("Caught signal %d, shutting down...", signo);
		g_main_loop_quit (loop);
	}
}

static void
setup_signals (void)
{
	struct sigaction action;
	sigset_t mask;

	sigemptyset (&mask);
	action.sa_handler = signal_handler;
	action.sa_mask = mask;
	action.sa_flags = 0;
	sigaction (SIGTERM,  &action, NULL);
	sigaction (SIGINT,  &action, NULL);
}

static void
list_done_cb (NMConnectionList *list, gint response, gpointer user_data)
{
	g_main_loop_quit (loop);
}

int
main (int argc, char *argv[])
{
	NMConnectionList *list;
	DBusGConnection *ignore;

	bindtextdomain (GETTEXT_PACKAGE, NMALOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	gtk_init (&argc, &argv);
	textdomain (GETTEXT_PACKAGE);

	/* parse arguments: an idea is to use gconf://$setting_name / system://$setting_name to
	   allow this program to work with both GConf and system-wide settings */

	/* Hack to init the dbus-glib type system */
	ignore = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	dbus_g_connection_unref (ignore);

	loop = g_main_loop_new (NULL, FALSE);

	list = nm_connection_list_new ();
	if (!list) {
		g_warning ("Failed to initialize the UI, exiting...");
		return 1;
	}

	g_signal_connect (G_OBJECT (list), "done", G_CALLBACK (list_done_cb), NULL);
	nm_connection_list_run (list);

	setup_signals ();
	g_main_loop_run (loop);

	g_object_unref (list);

	return 0;
}
