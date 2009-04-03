/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * (C) Copyright 2008 Novell, Inc.
 * (C) Copyright 2008 - 2009 Red Hat, Inc.
 */

#include <unistd.h>
#include <sys/types.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <glib/gi18n.h>
#include <gdk/gdkx.h>

#include "polkit-helpers.h"

gboolean
pk_helper_is_permission_denied_error (GError *error)
{
	return    dbus_g_error_has_name (error, "org.freedesktop.NetworkManagerSettings.Connection.NotPrivileged")
	       || dbus_g_error_has_name (error, "org.freedesktop.NetworkManagerSettings.System.NotPrivileged");
}

gboolean
pk_helper_obtain_auth (GError *pk_error,
                       GtkWindow *parent,
                       PolKitGnomeAuthCB callback,
                       gpointer user_data,
                       GError **error)
{
	PolKitAction *pk_action;
	char **tokens;
	gboolean success = FALSE;
	guint xid = 0;

	tokens = g_strsplit (pk_error->message, " ", 2);
	if (g_strv_length (tokens) != 2) {
		g_set_error (error, 0, 0, "%s", _("PolicyKit authorization was malformed."));
		goto out;
	}

        pk_action = polkit_action_new ();
	if (!pk_action) {
		g_set_error (error, 0, 0, "%s", _("PolicyKit authorization could not be created."));
		goto out;
	}

        if (!polkit_action_set_action_id (pk_action, tokens[0])) {
                polkit_action_unref (pk_action);
                pk_action = NULL;
		g_set_error (error, 0, 0, "%s", _("PolicyKit authorization could not be created; invalid action ID."));
		goto out;
        }

	if (parent)
		xid = gdk_x11_drawable_get_xid (GDK_DRAWABLE (GTK_WIDGET (parent)->window));
	success = polkit_gnome_auth_obtain (pk_action, xid, getpid (), callback, user_data, error);
	polkit_action_unref (pk_action);

out:
	g_strfreev (tokens);
	return success;
}

