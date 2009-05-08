/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * (C) Copyright 2007 Red Hat, Inc.
 */

#ifndef __APPLET_DBUS_MANAGER_H__
#define __APPLET_DBUS_MANAGER_H__

#include "config.h"
#include <glib-object.h>
#include <dbus/dbus.h>

G_BEGIN_DECLS

typedef gboolean (* AppletDBusSignalHandlerFunc) (DBusConnection * connection,
                                                  DBusMessage *    message,
                                                  gpointer         user_data);

#define APPLET_TYPE_DBUS_MANAGER         (applet_dbus_manager_get_type ())
#define APPLET_DBUS_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), APPLET_TYPE_DBUS_MANAGER, AppletDBusManager))
#define APPLET_DBUS_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), APPLET_TYPE_DBUS_MANAGER, AppletDBusManagerClass))
#define APPLET_IS_DBUS_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), APPLET_TYPE_DBUS_MANAGER))
#define APPLET_IS_DBUS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), APPLET_TYPE_DBUS_MANAGER))
#define APPLET_DBUS_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), APPLET_TYPE_DBUS_MANAGER, AppletDBusManagerClass)) 

typedef struct {
	GObject parent;
} AppletDBusManager;

typedef struct {
	GObjectClass parent;

	/* Signals */
	void (*connection_changed) (AppletDBusManager *mgr,
	                            DBusConnection *connection);

	void (*name_owner_changed) (AppletDBusManager *mgr,
	                            DBusConnection *connection,
	                            const char *name,
	                            const char *old_owner,
	                            const char *new_owner);

	void (*exit_now)           (AppletDBusManager *mgr);
} AppletDBusManagerClass;

GType applet_dbus_manager_get_type (void);

AppletDBusManager *applet_dbus_manager_get (void);

char *applet_dbus_manager_get_name_owner (AppletDBusManager *self,
                                          const char *name);

gboolean applet_dbus_manager_start_service (AppletDBusManager *self);

gboolean applet_dbus_manager_name_has_owner (AppletDBusManager *self,
                                             const char *name);

DBusConnection *applet_dbus_manager_get_dbus_connection (AppletDBusManager *self);
DBusGConnection *applet_dbus_manager_get_connection (AppletDBusManager *self);

G_END_DECLS

#endif /* __APPLET_DBUS_MANAGER_H__ */

