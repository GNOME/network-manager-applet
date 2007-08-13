/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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

#ifndef APPLET_DBUS_SETTINGS_H
#define APPLET_DBUS_SETTINGS_H

#include <gconf/gconf-client.h>
#include <nm-connection.h>
#include <nm-settings.h>

#define APPLET_TYPE_DBUS_SETTINGS    (applet_dbus_settings_get_type ())
#define APPLET_IS_DBUS_SETTINGS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), APPLET_TYPE_DBUS_SETTINGS))

typedef struct {
	NMSettings parent;

	/* private data */
	GConfClient *conf_client;
	GSList *connections;
} AppletDbusSettings;

typedef struct {
	NMSettingsClass parent_class;
} AppletDbusSettingsClass;

GType       applet_dbus_settings_get_type (void);
NMSettings *applet_dbus_settings_new (void);

#define APPLET_TYPE_DBUS_CONNECTION_SETTINGS    (applet_dbus_connection_settings_get_type ())
#define APPLET_IS_DBUS_CONNECTION_SETTINGS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), APPLET_TYPE_DBUS_CONNECTION_SETTINGS))

typedef struct {
	NMConnectionSettings parent;

	/* private data */
	GConfClient *conf_client;
	gchar *conf_dir;
	guint conf_notify_id;
	gchar *id;
	NMConnection *settings;
} AppletDbusConnectionSettings;

typedef struct {
	NMConnectionSettingsClass parent_class;
} AppletDbusConnectionSettingsClass;

GType                 applet_dbus_connection_settings_get_type (void);
NMConnectionSettings *applet_dbus_connection_settings_new (GConfClient *conf_client, const gchar *conf_dir);

#endif
