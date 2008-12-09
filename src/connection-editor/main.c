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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2004-2005 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <dbus/dbus.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <glib/gtypes.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <nm-setting-wired.h>
#include "nm-connection-list.h"

static GMainLoop *loop = NULL;

#define ARG_TYPE "type"

#define DBUS_TYPE_G_MAP_OF_VARIANT          (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))

#define NM_CE_DBUS_SERVICE_NAME "org.freedesktop.NetworkManager.Gnome.ConnectionEditor"

#define NM_TYPE_CE_SERVICE            (nm_ce_service_get_type ())
#define NM_CE_SERVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_CE_SERVICE, NMCEService))
#define NM_CE_SERVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_CE_SERVICE, NMCEServiceClass))
#define NM_IS_CE_SERVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_CE_SERVICE))
#define NM_IS_CE_SERVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_CE_SERVICE))
#define NM_CE_SERVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_CE_SERVICE, NMCEServiceClass))

typedef struct {
	GObject parent;
	NMConnectionList *list;
} NMCEService;

typedef struct {
	GObjectClass parent;
} NMCEServiceClass;

GType nm_ce_service_get_type (void);

G_DEFINE_TYPE (NMCEService, nm_ce_service, G_TYPE_OBJECT)

static gboolean impl_start (NMCEService *self, GHashTable *args, GError **error);

#include "nm-connection-editor-service-glue.h"

static NMCEService *
nm_ce_service_new (DBusGConnection *bus, DBusGProxy *proxy, NMConnectionList *list)
{
	GObject *object;
	DBusConnection *connection;
	GError *err = NULL;
	guint32 result;

	g_return_val_if_fail (bus != NULL, NULL);
	g_return_val_if_fail (proxy != NULL, NULL);

	object = g_object_new (NM_TYPE_CE_SERVICE, NULL);
	if (!object)
		return NULL;

	NM_CE_SERVICE (object)->list = list;

	dbus_connection_set_change_sigpipe (TRUE);
	connection = dbus_g_connection_get_connection (bus);
	dbus_connection_set_exit_on_disconnect (connection, FALSE);

	/* Register our single-instance service.  Don't care if it fails. */
	if (!dbus_g_proxy_call (proxy, "RequestName", &err,
					    G_TYPE_STRING, NM_CE_DBUS_SERVICE_NAME,
					    G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
					    G_TYPE_INVALID,
					    G_TYPE_UINT, &result,
					    G_TYPE_INVALID)) {
		g_warning ("Could not acquire the connection editor service.\n"
		            "  Message: '%s'", err->message);
		g_error_free (err);
	} else {
		if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
			g_warning ("Could not acquire the connection editor service as it is already taken.");
		else {
			/* success */
			dbus_g_connection_register_g_object (bus, "/", object);
		}
	}

	return (NMCEService *) object;
}

static void
nm_ce_service_init (NMCEService *self)
{
}

static void
nm_ce_service_class_init (NMCEServiceClass *config_class)
{
	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (config_class),
									 &dbus_glib_nm_connection_editor_service_object_info);
}

static gboolean
impl_start (NMCEService *self, GHashTable *table, GError **error)
{
	GValue *value;

	value = g_hash_table_lookup (table, ARG_TYPE);
	if (value && G_VALUE_HOLDS_STRING (value))
		nm_connection_list_set_type (self->list, g_value_get_string (value));
	nm_connection_list_present (self->list);

	return TRUE;
}


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

static gboolean
try_existing_instance (DBusGConnection *bus, DBusGProxy *proxy, const char *type)
{
	gboolean has_owner = FALSE;
	DBusGProxy *instance;
	GHashTable *args;
	GValue type_value = { 0, };
	gboolean success = FALSE;

	if (!dbus_g_proxy_call (proxy, "NameHasOwner", NULL,
	                        G_TYPE_STRING, NM_CE_DBUS_SERVICE_NAME, G_TYPE_INVALID,
	                        G_TYPE_BOOLEAN, &has_owner, G_TYPE_INVALID))
		return FALSE;

	if (!has_owner)
		return FALSE;

	/* Send arguments to existing process */
	instance = dbus_g_proxy_new_for_name (bus,
	                                      NM_CE_DBUS_SERVICE_NAME,
	                                      "/",
	                                      NM_CE_DBUS_SERVICE_NAME);
	if (!instance)
		return FALSE;

	args = g_hash_table_new (g_str_hash, g_str_equal);
	if (type) {
		g_value_init (&type_value, G_TYPE_STRING);
		g_value_set_static_string (&type_value, type);
		g_hash_table_insert (args, "type", &type_value);
	}

	if (dbus_g_proxy_call (instance, "Start", NULL,
	                       DBUS_TYPE_G_MAP_OF_VARIANT, args, G_TYPE_INVALID,
	                       G_TYPE_INVALID))
		success = TRUE;

	g_hash_table_destroy (args);
	g_object_unref (instance);
	return success;
}

int
main (int argc, char *argv[])
{
	GOptionContext *opt_ctx;
	GError *error = NULL;
	NMConnectionList *list;
	DBusGConnection *bus;
	char *type = NULL;
	NMCEService *service = NULL;
	DBusGProxy *proxy = NULL;

	GOptionEntry entries[] = {
		{ ARG_TYPE, 0, 0, G_OPTION_ARG_STRING, &type, "Type of connection to show at launch", NM_SETTING_WIRED_SETTING_NAME },
		{ NULL }
	};

	bindtextdomain (GETTEXT_PACKAGE, NMALOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	gtk_init (&argc, &argv);
	textdomain (GETTEXT_PACKAGE);

	/* parse arguments: an idea is to use gconf://$setting_name / system://$setting_name to
	   allow this program to work with both GConf and system-wide settings */

	opt_ctx = g_option_context_new (NULL);
	g_option_context_set_summary (opt_ctx, "Allows users to view and edit network connection settings");
	g_option_context_add_main_entries (opt_ctx, entries, NULL);

	if (!g_option_context_parse (opt_ctx, &argc, &argv, &error)) {
		g_warning ("%s\n", error->message);
		g_error_free (error);
		return 1;
	}

	g_option_context_free (opt_ctx);

	/* Inits the dbus-glib type system too */
	bus = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	proxy = dbus_g_proxy_new_for_name (bus,
									 "org.freedesktop.DBus",
									 "/org/freedesktop/DBus",
									 "org.freedesktop.DBus");

	/* Check for an existing instance on the bus */
	if (proxy) {
		if (try_existing_instance (bus, proxy, type))
			goto exit;
	}

	loop = g_main_loop_new (NULL, FALSE);

	list = nm_connection_list_new (type);
	if (!list) {
		g_warning ("Failed to initialize the UI, exiting...");
		return 1;
	}

	/* Create our single-instance-app service if we can */
	if (proxy)
		service = nm_ce_service_new (bus, proxy, list);

	g_signal_connect_swapped (G_OBJECT (list), "done", G_CALLBACK (g_main_loop_quit), loop);
	nm_connection_list_run (list);

	setup_signals ();
	g_main_loop_run (loop);

	g_object_unref (list);
	if (service)
		g_object_unref (service);

exit:
	if (proxy)
		g_object_unref (proxy);
	dbus_g_connection_unref (bus);
	return 0;
}

