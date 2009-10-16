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

#include "config.h"
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <string.h>
#include "nm-utils.h"
#include "applet.h"
#include "nma-marshal.h"
#include "applet-dbus-manager.h"

enum {
	CONNECTION_CHANGED = 0,
	NAME_OWNER_CHANGED,
	EXIT_NOW,
	NUM_SIGNALS
};
static guint signals[NUM_SIGNALS];


G_DEFINE_TYPE(AppletDBusManager, applet_dbus_manager, G_TYPE_OBJECT)

#define APPLET_DBUS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                            APPLET_TYPE_DBUS_MANAGER, \
                                            AppletDBusManagerPrivate))

typedef struct {
	/* system bus */
	DBusConnection * connection;
	DBusGConnection *g_connection;
	DBusGProxy *     proxy;
	gboolean         started;

	/* session bus */
	DBusGConnection *session_g_connection;
	DBusGProxy *     session_proxy;

	gulong recon_id;
} AppletDBusManagerPrivate;


static gboolean system_bus_init (AppletDBusManager *self);
static void system_bus_cleanup (AppletDBusManager *self);

static void session_bus_cleanup (AppletDBusManager *self);

static void start_reconnection_timeout (AppletDBusManager *self);


/*
 * The applet claims two services:
 *   a) system bus service - to provide settings to NetworkManager and other
 *      programs that want information about network connections
 *   b) session bus service - to enforce single-applet-per-session; this service
 *      *must* be claimed, otherwise the applet quits becuase there is already
 *      an applet running in the session.
 */


DBusConnection *
applet_dbus_manager_get_dbus_connection (AppletDBusManager *self)
{
	AppletDBusManagerPrivate *priv;

	g_return_val_if_fail (APPLET_IS_DBUS_MANAGER (self), NULL);

	priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);
	return priv->g_connection ? dbus_g_connection_get_connection (priv->g_connection) : NULL;
}

DBusGConnection *
applet_dbus_manager_get_connection (AppletDBusManager *self)
{
	g_return_val_if_fail (APPLET_IS_DBUS_MANAGER (self), NULL);

	return APPLET_DBUS_MANAGER_GET_PRIVATE (self)->g_connection;
}

static DBusGConnection *
bus_init (DBusBusType bus_type,
          DBusGProxy **proxy,
          GCallback destroy_cb,
          gpointer user_data,
          const char *detail)
{
	DBusGConnection *gc;
	DBusConnection *connection;
	GError *error = NULL;

	dbus_connection_set_change_sigpipe (TRUE);

	gc = dbus_g_bus_get (bus_type, &error);
	if (!gc) {
		nm_warning ("Could not get the %s bus.  Make sure the message "
		            "bus daemon is running!  Message: %s",
		            detail, error->message);
		g_error_free (error);
		return FALSE;
	}

	connection = dbus_g_connection_get_connection (gc);
	dbus_connection_set_exit_on_disconnect (connection, FALSE);

	*proxy = dbus_g_proxy_new_for_name (gc,
	                                    "org.freedesktop.DBus",
	                                    "/org/freedesktop/DBus",
	                                    "org.freedesktop.DBus");
	if (!*proxy) {
		nm_warning ("Could not get the DBus object!");
		dbus_g_connection_unref (gc);
		gc = NULL;
	} else
		g_signal_connect (*proxy, "destroy", destroy_cb, user_data);

	return gc;
}

static gboolean
request_name (DBusGProxy *proxy, int flags, const char *detail)
{
	int request_name_result;
	GError *error = NULL;

	if (!dbus_g_proxy_call (proxy, "RequestName", &error,
	                        G_TYPE_STRING, NM_DBUS_SERVICE_USER_SETTINGS,
	                        G_TYPE_UINT, flags,
	                        G_TYPE_INVALID,
	                        G_TYPE_UINT, &request_name_result,
	                        G_TYPE_INVALID)) {
		nm_warning ("Could not acquire the %s service.\n"
		            "  Error: (%d) %s",
		            detail,
		            error ? error->code : -1,
		            error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
		return FALSE;
	}

	if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		nm_warning ("Could not acquire the %s service as it is already "
		            "taken.  Return: %d",
		            detail, request_name_result);
		return FALSE;
	}

	return TRUE;
}

static void
system_bus_destroy_cb (DBusGProxy *proxy, gpointer user_data)
{
	AppletDBusManager *self = APPLET_DBUS_MANAGER (user_data);

	/* Clean up existing connection */
	nm_info ("disconnected by the system bus.");
	system_bus_cleanup (self);

	g_signal_emit (G_OBJECT (self), 
	               signals[CONNECTION_CHANGED],
	               0, NULL);

	start_reconnection_timeout (self);
}

static void
system_bus_cleanup (AppletDBusManager *self)
{
	AppletDBusManagerPrivate *priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);

	if (priv->g_connection) {
		dbus_g_connection_unref (priv->g_connection);
		priv->g_connection = NULL;
		priv->connection = NULL;
	}

	if (priv->proxy) {
		g_signal_handlers_disconnect_by_func (priv->proxy, system_bus_destroy_cb, self);
		g_object_unref (priv->proxy);
		priv->proxy = NULL;
	}

	priv->started = FALSE;
}

static void
session_bus_destroy_cb (DBusGProxy *proxy, gpointer user_data)
{
	AppletDBusManager *self = APPLET_DBUS_MANAGER (user_data);

	nm_info ("disconnected by the session bus.");
	session_bus_cleanup (self);
	start_reconnection_timeout (self);
}

static void
session_bus_cleanup (AppletDBusManager *self)
{
	AppletDBusManagerPrivate *priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);

	if (priv->session_g_connection) {
		dbus_g_connection_unref (priv->session_g_connection);
		priv->session_g_connection = NULL;
	}

	if (priv->session_proxy) {
		g_signal_handlers_disconnect_by_func (priv->session_proxy, session_bus_destroy_cb, self);
		g_object_unref (priv->session_proxy);
		priv->session_proxy = NULL;
	}
}

static gboolean
start_session_bus_service (AppletDBusManager *self)
{
	return request_name (APPLET_DBUS_MANAGER_GET_PRIVATE (self)->session_proxy,
		             DBUS_NAME_FLAG_DO_NOT_QUEUE,
		             "session");
}

static gboolean
session_bus_init (AppletDBusManager *self)
{
	AppletDBusManagerPrivate *priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);

	if (priv->session_g_connection) {
		nm_warning ("session connection already exists");
		return FALSE;
	}

	priv->session_g_connection = bus_init (DBUS_BUS_SESSION, &(priv->session_proxy),
	                                       G_CALLBACK (session_bus_destroy_cb), self,
	                                       "session");
	if (!priv->session_g_connection) {
		session_bus_cleanup (self);
		return FALSE;
	}

	return TRUE;
}

static gboolean
reconnect_cb (gpointer user_data)
{
	AppletDBusManager *self = APPLET_DBUS_MANAGER (user_data);
	AppletDBusManagerPrivate *priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);
	guint32 i = 0;

	g_assert (self != NULL);

	/* System bus connection */
	if (priv->g_connection)
		i++;
	else {
		if (system_bus_init (self)) {
			if (applet_dbus_manager_start_service (self)) {
				nm_info ("reconnected to the system bus.");
				g_signal_emit (self, signals[CONNECTION_CHANGED],
				               0, priv->connection);
				i++;
			} else
				system_bus_cleanup (self);
		}
	}

	if (priv->session_g_connection)
		i++;
	else {
		if (session_bus_init (self)) {
			nm_info ("reconnected to the session bus.");
			if (start_session_bus_service (self))
				i++;
			else {
				session_bus_cleanup (self);
				g_signal_emit (self, signals[EXIT_NOW], 0);
			}
		}
	}

	/* Keep the reconnect timeout active if at least one of the system or
	 * session bus connections still needs to be reconnected.
	 */
	return i < 2 ? FALSE : TRUE;
}

static void
start_reconnection_timeout (AppletDBusManager *self)
{
	AppletDBusManagerPrivate *priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);

	/* Schedule timeout for reconnection attempts */
	if (!priv->recon_id)
		priv->recon_id = g_timeout_add_seconds (3, reconnect_cb, self);
}

char *
applet_dbus_manager_get_name_owner (AppletDBusManager *self,
                                    const char *name)
{
	char *owner = NULL;
	GError *err = NULL;

	g_return_val_if_fail (APPLET_IS_DBUS_MANAGER (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	if (!dbus_g_proxy_call (APPLET_DBUS_MANAGER_GET_PRIVATE (self)->proxy,
	                        "GetNameOwner", &err,
	                        G_TYPE_STRING, name,
	                        G_TYPE_INVALID,
	                        G_TYPE_STRING, &owner,
	                        G_TYPE_INVALID)) {
		nm_warning ("Error on GetNameOwner DBUS call: %s", err->message);
		g_error_free (err);
	}

	return owner;
}

gboolean
applet_dbus_manager_name_has_owner (AppletDBusManager *self,
                                    const char *name)
{
	gboolean has_owner = FALSE;
	GError *err = NULL;

	g_return_val_if_fail (APPLET_IS_DBUS_MANAGER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (!dbus_g_proxy_call (APPLET_DBUS_MANAGER_GET_PRIVATE (self)->proxy,
	                        "NameHasOwner", &err,
	                        G_TYPE_STRING, name,
	                        G_TYPE_INVALID,
	                        G_TYPE_BOOLEAN, &has_owner,
	                        G_TYPE_INVALID)) {
		nm_warning ("Error on NameHasOwner DBUS call: %s", err->message);
		g_error_free (err);
	}

	return has_owner;
}

static void
proxy_name_owner_changed (DBusGProxy *proxy,
                          const char *name,
                          const char *old_owner,
                          const char *new_owner,
                          gpointer user_data)
{
	AppletDBusManager *self = APPLET_DBUS_MANAGER (user_data);

	g_signal_emit (self, 
	               signals[NAME_OWNER_CHANGED],
	               0,
	               name, old_owner, new_owner);
}

static gboolean
system_bus_init (AppletDBusManager *self)
{
	AppletDBusManagerPrivate *priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);

	if (priv->connection) {
		nm_warning ("DBus Manager already has a valid connection.");
		return FALSE;
	}

	priv->g_connection = bus_init (DBUS_BUS_SYSTEM, &(priv->proxy),
	                               G_CALLBACK (system_bus_destroy_cb), self,
	                               "system");
	if (!priv->g_connection) {
		system_bus_cleanup (self);
		return FALSE;
	}

	dbus_g_proxy_add_signal (priv->proxy, "NameOwnerChanged",
	                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
	                         G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (priv->proxy,
	                             "NameOwnerChanged",
	                             G_CALLBACK (proxy_name_owner_changed),
	                             self, NULL);
	return TRUE;
}

/* Register our service on the bus; shouldn't be called until
 * all necessary message handlers have been registered, because
 * when we register on the bus, clients may start to call.
 */
gboolean
applet_dbus_manager_start_service (AppletDBusManager *self)
{
	AppletDBusManagerPrivate *priv;

	g_return_val_if_fail (APPLET_IS_DBUS_MANAGER (self), FALSE);

	priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);

	if (!priv->started) {
		priv->started = request_name (priv->proxy,
		                              DBUS_NAME_FLAG_DO_NOT_QUEUE,
		                              "NetworkManagerUserSettings");
		if (!priv->started)
			system_bus_cleanup (self);
	}

	return priv->started;
}

AppletDBusManager *
applet_dbus_manager_get (void)
{
	static AppletDBusManager *singleton = NULL;

	if (!singleton) {
		singleton = APPLET_DBUS_MANAGER (g_object_new (APPLET_TYPE_DBUS_MANAGER, NULL));

		/* If there's another applet on the session bus, have to quit */
		if (session_bus_init (singleton)) {
			if (!start_session_bus_service (singleton)) {
				g_object_unref (singleton);
				singleton = NULL;
				return NULL;
			}
		} else
			start_reconnection_timeout (singleton);

		if (!system_bus_init (singleton))
			start_reconnection_timeout (singleton);
	} else {
		g_object_ref (singleton);
	}

	g_assert (singleton);
	return singleton;
}

static void
applet_dbus_manager_init (AppletDBusManager *self)
{
}

static void
applet_dbus_manager_finalize (GObject *object)
{
	AppletDBusManager *self = APPLET_DBUS_MANAGER (object);
	AppletDBusManagerPrivate *priv = APPLET_DBUS_MANAGER_GET_PRIVATE (self);

	system_bus_cleanup (self);
	session_bus_cleanup (self);

	if (priv->recon_id)
		g_source_remove (priv->recon_id);

	G_OBJECT_CLASS (applet_dbus_manager_parent_class)->finalize (object);
}

static void
applet_dbus_manager_class_init (AppletDBusManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = applet_dbus_manager_finalize;

	signals[CONNECTION_CHANGED] =
		g_signal_new ("dbus-connection-changed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (AppletDBusManagerClass, connection_changed),
		              NULL, NULL, nma_marshal_VOID__POINTER,
		              G_TYPE_NONE, 1, G_TYPE_POINTER);

	signals[NAME_OWNER_CHANGED] =
		g_signal_new ("name-owner-changed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (AppletDBusManagerClass, name_owner_changed),
		              NULL, NULL, nma_marshal_VOID__STRING_STRING_STRING,
		              G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	signals[EXIT_NOW] =
		g_signal_new ("exit-now",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (AppletDBusManagerClass, exit_now),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (AppletDBusManagerPrivate));
}

