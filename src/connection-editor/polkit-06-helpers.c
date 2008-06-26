/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <glib.h>
#include <dbus/dbus-glib.h>

#include "polkit-06-helpers.h"

typedef struct {
        PolKitAction *action;
        PolKitGnomeAuthCB callback;
        gpointer user_data;
} DialogResponseInfo;

static void
pk_auth_dialog_response_cb (DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
	GError *error = NULL;
	DialogResponseInfo *info = (DialogResponseInfo *) user_data;
	gboolean gained_privilege = FALSE;

	if (!dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_BOOLEAN, &gained_privilege, G_TYPE_INVALID))
		gained_privilege = FALSE;

	/* perform the callback */
	info->callback (info->action, gained_privilege, error, info->user_data);

	g_object_unref (proxy);
	polkit_action_unref (info->action);
}

gboolean
polkit_gnome_auth_obtain (PolKitAction *pk_action,
                          guint xid,
                          pid_t pid,
                          PolKitGnomeAuthCB callback,
                          gpointer user_data,
                          GError **error)
{
	char *polkit_action_id;
	DialogResponseInfo *info;
	DBusGConnection *session_bus;
	DBusGProxy *polkit_gnome_proxy;
	gboolean ret = TRUE;

	if ((session_bus = dbus_g_bus_get (DBUS_BUS_SESSION, error)) == NULL)
		return FALSE;

	/* TODO: this can fail.. */
	polkit_action_get_action_id (pk_action, &polkit_action_id);

	polkit_gnome_proxy = dbus_g_proxy_new_for_name (session_bus,
	                                                "org.freedesktop.PolicyKit.AuthenticationAgent", /* bus name */
	                                                "/",                                             /* object */
	                                                "org.freedesktop.PolicyKit.AuthenticationAgent");/* interface */

	info = g_malloc0 (sizeof (DialogResponseInfo));
	info->action = polkit_action_ref (pk_action);
	info->callback = callback;
	info->user_data = user_data;	

	/* now use PolicyKit-gnome to bring up an auth dialog */
	if (!dbus_g_proxy_begin_call_with_timeout (polkit_gnome_proxy,
	                                           "ShowDialog",
	                                           pk_auth_dialog_response_cb,
	                                           info,
	                                           g_free,
	                                           INT_MAX,
	                                           /* parameters: */
	                                           G_TYPE_STRING, polkit_action_id,  /* action_id */
	                                           G_TYPE_UINT, xid, /* X11 window ID */
	                                           G_TYPE_INVALID)) {
		ret = FALSE;
	}

	dbus_g_connection_unref (session_bus);
	return ret;
}


