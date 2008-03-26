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

#define APPLET_TYPE_EXPORTED_CONNECTION    (applet_exported_connection_get_type ())
#define APPLET_IS_EXPORTED_CONNECTION(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), APPLET_TYPE_EXPORTED_CONNECTION))
#define APPLET_EXPORTED_CONNECTION(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), APPLET_TYPE_EXPORTED_CONNECTION, AppletExportedConnection))

typedef struct {
	NMExportedConnection parent;

	/* private data */
	GConfClient *conf_client;
	gchar *conf_dir;
	gchar *id;
} AppletExportedConnection;

typedef struct {
	NMExportedConnectionClass parent_class;

	/* Signals */
	void (*new_secrets_requested)  (AppletExportedConnection *exported,
	                                const char *setting_name,
	                                const char **hints,
	                                gboolean ask_user,
	                                DBusGMethodInvocation *context);
} AppletExportedConnectionClass;

GType                     applet_exported_connection_get_type (void);
AppletExportedConnection *applet_exported_connection_new (GConfClient *conf_client, const gchar *conf_dir);

void applet_exported_connection_save (AppletExportedConnection *exported);


#define APPLET_TYPE_DBUS_SETTINGS    (applet_dbus_settings_get_type ())
#define APPLET_IS_DBUS_SETTINGS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), APPLET_TYPE_DBUS_SETTINGS))
#define APPLET_DBUS_SETTINGS(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), APPLET_TYPE_DBUS_SETTINGS, AppletDbusSettings))

typedef struct {
	NMSettings parent;

	/* private data */
	GConfClient *conf_client;
	guint conf_notify_id;
	GSList *connections;

	/* List of NMConnection objects */
	GHashTable *system_connections;
	DBusGProxy *system_proxy;
} AppletDbusSettings;

typedef struct {
	NMSettingsClass parent_class;

	/* Signals */
	void (*new_secrets_requested)  (AppletDbusSettings *settings,
	                                AppletExportedConnection *exported,
	                                const char *setting_name,
	                                const char **hints,
	                                gboolean ask_user,
	                                DBusGMethodInvocation *context);

	void (*system_settings_changed)(AppletDbusSettings *settings);
} AppletDbusSettingsClass;

GType       applet_dbus_settings_get_type (void);
AppletDbusSettings *applet_dbus_settings_new (void);

AppletExportedConnection * applet_dbus_settings_user_add_connection (AppletDbusSettings *settings,
                                                                     NMConnection *connection);

AppletExportedConnection * applet_dbus_settings_user_get_by_dbus_path (AppletDbusSettings *settings,
                                                                           const char *path);

AppletExportedConnection * applet_dbus_settings_user_get_by_connection (AppletDbusSettings *settings,
                                                                        NMConnection *connection);

const char * applet_dbus_settings_system_get_dbus_path (AppletDbusSettings *settings,
                                                        NMConnection *connection);

NMConnection * applet_dbus_settings_system_get_by_dbus_path (AppletDbusSettings *settings,
                                                             const char *path);

/* Returns a list of NMConnectionSettings objects */
GSList * applet_dbus_settings_list_connections (AppletDbusSettings *settings);

/* Returns a list of both user and system NMConnection objects */
GSList * applet_dbus_settings_get_all_connections (AppletDbusSettings *settings);

#endif
