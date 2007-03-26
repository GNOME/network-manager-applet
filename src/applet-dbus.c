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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2004 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "applet.h"
#include "applet-dbus.h"
#include "applet-dbus-info.h"
#include "passphrase-dialog.h"
#include "nm-utils.h"

#define	DBUS_NO_SERVICE_ERROR			"org.freedesktop.DBus.Error.ServiceDoesNotExist"

void
nma_dbus_vpn_set_last_attempt_status (NMApplet *applet, const char *vpn_name, gboolean last_attempt_success)
{
	char *gconf_key;
	char *escaped_name;
	NMVPNConnection *vpn;

	vpn = nm_client_get_vpn_connection_by_name (applet->nm_client, vpn_name);
	if (vpn) {
		escaped_name = gconf_escape_key (vpn_name, strlen (vpn_name));

		gconf_key = g_strdup_printf ("%s/%s/last_attempt_success", GCONF_PATH_VPN_CONNECTIONS, escaped_name);
		gconf_client_set_bool (applet->gconf_client, gconf_key, last_attempt_success, NULL);

		g_free (gconf_key);
		g_free (escaped_name);
	}
}

/*
 * nma_dbus_filter
 *
 */
static DBusHandlerResult nma_dbus_filter (DBusConnection *connection, DBusMessage *message, void *user_data)
{
	NMApplet *	applet = (NMApplet *)user_data;
	gboolean		handled = TRUE;
	const char *	object_path;
	const char *	member;
	const char *	interface;

	g_return_val_if_fail (applet != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
	g_return_val_if_fail (connection != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
	g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

	if (!(object_path = dbus_message_get_path (message)))
		return FALSE;
	if (!(member = dbus_message_get_member (message)))
		return FALSE;
	if (!(interface = dbus_message_get_interface (message)))
		return FALSE;

	/* nm_info ("signal(): got signal op='%s' member='%s' interface='%s'", object_path, member, interface); */

	if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected"))
	{
		dbus_connection_unref (applet->connection);
		applet->connection = NULL;
		nma_set_running (applet, FALSE);
		if (!applet->connection_timeout_id)
			nma_start_dbus_connection_watch (applet);
	}
	else if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged"))
	{
		char 	*service;
		char		*old_owner;
		char		*new_owner;

		if (dbus_message_get_args (message, NULL,
								DBUS_TYPE_STRING, &service,
								DBUS_TYPE_STRING, &old_owner,
								DBUS_TYPE_STRING, &new_owner,
								DBUS_TYPE_INVALID))
		{
			if (strcmp (service, NM_DBUS_SERVICE) == 0)
			{
				gboolean old_owner_good = (old_owner && (strlen (old_owner) > 0));
				gboolean new_owner_good = (new_owner && (strlen (new_owner) > 0));

				if (!old_owner_good && new_owner_good && !applet->nm_running)
				{
					/* NetworkManager started up */
					nma_set_running (applet, TRUE);
				}
				else if (old_owner_good && !new_owner_good)
				{
					nma_set_running (applet, FALSE);
					nmi_passphrase_dialog_destroy (applet);
				}
			}
		}
	}
	else if (    dbus_message_is_signal (message, NM_DBUS_INTERFACE_VPN, NM_DBUS_VPN_SIGNAL_LOGIN_FAILED)
			|| dbus_message_is_signal (message, NM_DBUS_INTERFACE_VPN, NM_DBUS_VPN_SIGNAL_LAUNCH_FAILED)
			|| dbus_message_is_signal (message, NM_DBUS_INTERFACE_VPN, NM_DBUS_VPN_SIGNAL_CONNECT_FAILED)
			|| dbus_message_is_signal (message, NM_DBUS_INTERFACE_VPN, NM_DBUS_VPN_SIGNAL_VPN_CONFIG_BAD)
			|| dbus_message_is_signal (message, NM_DBUS_INTERFACE_VPN, NM_DBUS_VPN_SIGNAL_IP_CONFIG_BAD))
	{
		char *vpn_name;
		char *error_msg;

		if (dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &vpn_name, DBUS_TYPE_STRING, &error_msg, DBUS_TYPE_INVALID)) {
			nma_show_vpn_failure_alert (applet, member, vpn_name, error_msg);
			/* clear the 'last_attempt_success' key in gconf so we prompt for password next time */
			nma_dbus_vpn_set_last_attempt_status (applet, vpn_name, FALSE);
		}
	}
	else if (dbus_message_is_signal (message, NM_DBUS_INTERFACE_VPN, NM_DBUS_VPN_SIGNAL_LOGIN_BANNER))
	{
		char *vpn_name;
		char *banner;

		if (dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &vpn_name, DBUS_TYPE_STRING, &banner, DBUS_TYPE_INVALID))
		{
			char *stripped = g_strstrip (g_strdup (banner));

			nma_show_vpn_login_banner (applet, vpn_name, stripped);
			g_free (stripped);

			/* set the 'last_attempt_success' key in gconf so we DON'T prompt for password next time */
			nma_dbus_vpn_set_last_attempt_status (applet, vpn_name, TRUE);
		}
	}
	else
		handled = FALSE;

	return (handled ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}


/*
 * nma_dbus_init
 *
 * Initialize a connection to NetworkManager if we can get one
 *
 */
static DBusConnection * nma_dbus_init (NMApplet *applet)
{
	DBusConnection	*		connection = NULL;
	DBusError		 		error;
	DBusObjectPathVTable	vtable = { NULL, &nmi_dbus_info_message_handler, NULL, NULL, NULL, NULL };
	int					acquisition;
	dbus_bool_t			success = FALSE;

	g_return_val_if_fail (applet != NULL, NULL);

	dbus_error_init (&error);
	connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (dbus_error_is_set (&error)) {
		nm_warning ("%s raised:\n %s\n\n", error.name, error.message);
		goto error;
	}

	dbus_error_init (&error);
	acquisition = dbus_bus_request_name (connection,
	                                     NMI_DBUS_SERVICE,
	                                     DBUS_NAME_FLAG_REPLACE_EXISTING,
	                                     &error);
	if (dbus_error_is_set (&error)) {
		nm_warning ("could not acquire its service.  dbus_bus_acquire_service()"
		            " says: '%s'",
		            error.message);
		goto error;
	}
	if (acquisition == DBUS_REQUEST_NAME_REPLY_EXISTS)
		goto error;

	success = dbus_connection_register_object_path (connection,
	                                                NMI_DBUS_PATH,
	                                                &vtable,
	                                                applet);
	if (!success) {
		nm_warning ("could not register a messgae handler for the"
		            " NetworkManagerInfo service.  Not enough memory?");
		goto error;
	}

	success = dbus_connection_add_filter (connection, nma_dbus_filter, applet, NULL);
	if (!success)
		goto error;

	dbus_connection_set_exit_on_disconnect (connection, FALSE);
	dbus_connection_setup_with_g_main (connection, NULL);

	dbus_error_init (&error);
	dbus_bus_add_match(connection,
				"type='signal',"
				"interface='" DBUS_INTERFACE_DBUS "',"
				"sender='" DBUS_SERVICE_DBUS "'",
				&error);
	if (dbus_error_is_set (&error)) {
		nm_warning ("Could not register signal handlers.  '%s'",
		            error.message);
		goto error;
	}

	dbus_error_init (&error);
	dbus_bus_add_match(connection,
				"type='signal',"
				"interface='" NM_DBUS_INTERFACE "',"
				"path='" NM_DBUS_PATH "',"
				"sender='" NM_DBUS_SERVICE "'",
				&error);
	if (dbus_error_is_set (&error)) {
		nm_warning ("Could not register signal handlers.  '%s'",
		            error.message);
		goto error;
	}

	dbus_error_init (&error);
	dbus_bus_add_match(connection,
				"type='signal',"
				"interface='" NM_DBUS_INTERFACE_VPN "',"
				"path='" NM_DBUS_PATH_VPN "',"
				"sender='" NM_DBUS_SERVICE "'",
				&error);
	if (dbus_error_is_set (&error)) {
		nm_warning ("Could not register signal handlers.  '%s'",
		            error.message);
		goto error;
	}

	return connection;

error:
	if (dbus_error_is_set (&error))
		dbus_error_free (&error);
	if (connection)
		dbus_connection_unref (connection);
	return NULL;
}


/*
 * nma_dbus_connection_watcher
 *
 * Try to reconnect if we ever get disconnected from the bus
 *
 */
static gboolean
nma_dbus_connection_watcher (gpointer user_data)
{
	NMApplet * applet = (NMApplet *)user_data;

	g_return_val_if_fail (applet != NULL, TRUE);

	nma_dbus_init_helper (applet);
	if (applet->connection) {
		applet->connection_timeout_id = 0;
		return FALSE;  /* Remove timeout */
	}

	return TRUE;
}


void
nma_start_dbus_connection_watch (NMApplet *applet)
{
	if (applet->connection_timeout_id)
		g_source_remove (applet->connection_timeout_id);

	applet->connection_timeout_id = g_timeout_add (5000,
	                                               (GSourceFunc) nma_dbus_connection_watcher,
	                                               applet);
}


/*
 * nma_dbus_init_helper
 *
 * Set up the applet's NMI dbus methods and dbus connection
 *
 */
void
nma_dbus_init_helper (NMApplet *applet)
{
	g_return_if_fail (applet != NULL);

	applet->connection = nma_dbus_init (applet);
	if (applet->connection) {
		if (applet->connection_timeout_id) {
			g_source_remove (applet->connection_timeout_id);
			applet->connection_timeout_id = 0;
		}

		if (nm_client_manager_is_running (applet->nm_client)) {
			nma_set_running (applet, TRUE);
		}
	}
}
