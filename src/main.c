/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
 *
 * Dan Williams <dcbw@redhat.com>
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
 * This applet used the GNOME Wireless Applet as a skeleton to build from.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib-unix.h>

#include "applet.h"

static GMainLoop *loop = NULL;
gboolean shell_debug = FALSE;
gboolean with_agent = TRUE;

static void
usage (const char *progname)
{
	char *foo;

	foo = g_path_get_basename (progname);
	fprintf (stdout, "%s %s\n\n%s\n%s\n\n",
	                 _("Usage:"),
	                 foo,
	                 _("This program is a component of NetworkManager (https://wiki.gnome.org/Projects/NetworkManager/)."),
	                 _("It is not intended for command-line interaction but instead runs in the GNOME desktop environment."));
	g_free (foo);
}

static gboolean
do_quit (gpointer user_data)
{
	g_main_loop_quit ((GMainLoop *) user_data);
	return G_SOURCE_REMOVE;
}

int main (int argc, char *argv[])
{
	NMApplet *applet;
	guint32 i;

	for (i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--help")) {
			usage (argv[0]);
			exit (0);
		}
		if (!strcmp (argv[i], "--shell-debug"))
			shell_debug = TRUE;
		if (!strcmp (argv[i], "--no-agent"))
			with_agent = FALSE;
	}

	bindtextdomain (GETTEXT_PACKAGE, NMALOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	gtk_init (&argc, &argv);
	textdomain (GETTEXT_PACKAGE);

	loop = g_main_loop_new (NULL, FALSE);

	applet = nm_applet_new ();
	if (applet == NULL)
		exit (1);

	g_unix_signal_add (SIGINT, do_quit, loop);
	g_unix_signal_add (SIGTERM, do_quit, loop);
	g_main_loop_run (loop);

	g_object_unref (G_OBJECT (applet));

	exit (0);
}

