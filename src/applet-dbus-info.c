/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
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
 * (C) Copyright 2004-2005 Red Hat, Inc.
 */


#include <string.h>
#include <stdio.h>
#include <time.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gnome-keyring.h>
#include <iwlib.h>

#include "NetworkManager.h"
#include "applet.h"
#include "applet-dbus.h"
#include "applet-dbus-info.h"
#include "nm-utils.h"
#include "gconf-helpers.h"
#include "dbus-method-dispatcher.h"
#include "dbus-helpers.h"


static DBusMessage * new_invalid_args_error (DBusMessage *message, const char *func)
{
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (func != NULL, NULL);

	return nmu_create_dbus_error_message (message,
								NMI_DBUS_SERVICE,
								"InvalidArguments",
								"%s called with invalid arguments.",
								func);
}

/*
 * nmi_dbus_signal_update_vpn_connection
 *
 * Signal NetworkManager that it needs to update info associated with a particular
 * VPN connection.
 *
 */
void nmi_dbus_signal_update_vpn_connection (DBusConnection *connection, const char *name)
{
	DBusMessage		*message;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (name != NULL);

	message = dbus_message_new_signal (NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "VPNConnectionUpdate");
	dbus_message_append_args (message, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
	if (!dbus_connection_send (connection, message, NULL))
		nm_warning ("Could not raise the 'VPNConnectionUpdate' signal!");

	dbus_message_unref (message);
}


/*
 * nmi_dbus_get_vpn_connections
 *
 * Grab a list of VPN connections from GConf and return it in the form
 * of a string array in a dbus message.
 *
 */
static DBusMessage *
nmi_dbus_get_vpn_connections (DBusConnection *connection,
                              DBusMessage *message,
                              void *user_data)
{
	NMApplet *	applet = (NMApplet *) user_data;
	GSList *			dir_list = NULL;
	GSList *			elt = NULL;
	DBusMessage *		reply = NULL;
	DBusMessageIter	iter;
	DBusMessageIter	iter_array;
	gboolean			value_added = FALSE;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	/* List all VPN connections that gconf knows about */
	if (!(dir_list = gconf_client_all_dirs (applet->gconf_client, GCONF_PATH_VPN_CONNECTIONS, NULL)))
	{
		reply = nmu_create_dbus_error_message (message, NMI_DBUS_SERVICE, "NoVPNConnections",
							"There are no VPN connections stored.");
		goto out;
	}

	reply = dbus_message_new_method_return (message);
	dbus_message_iter_init_append (reply, &iter);

	/* Append the essid of every allowed or ignored access point we know of 
	 * to a string array in the dbus message.
	 */
	dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &iter_array);
	for (elt = dir_list; elt; elt = g_slist_next (elt))
	{
		char			key[100];
		GConfValue *	value;
		char *		dir = (char *) (elt->data);

		g_snprintf (&key[0], 99, "%s/name", dir);
		if ((value = gconf_client_get (applet->gconf_client, key, NULL)))
		{
			if (value->type == GCONF_VALUE_STRING)
			{
				const gchar *essid = gconf_value_get_string (value);
				dbus_message_iter_append_basic (&iter_array, DBUS_TYPE_STRING, &essid);
				value_added = TRUE;
			}
			gconf_value_free (value);
		}
		g_free (dir);
	}
	g_slist_free (dir_list);
	dbus_message_iter_close_container (&iter, &iter_array);

	if (!value_added)
	{
		dbus_message_unref (reply);
		reply = nmu_create_dbus_error_message (message, NMI_DBUS_SERVICE, "NoVPNConnections",
						"There are no VPN connections stored.");
	}

out:
	return reply;
}


/*
 * nmi_dbus_get_vpn_connection_properties
 *
 * Returns the properties of a specific VPN connection from gconf
 *
 */
static DBusMessage *
nmi_dbus_get_vpn_connection_properties (DBusConnection *connection,
                                        DBusMessage *message,
                                        void *user_data)
{
	NMApplet *	applet = (NMApplet *) user_data;
	DBusMessage *	reply = NULL;
	char *		vpn_connection = NULL;
	char *		escaped_name = NULL;
	char *		name = NULL;
	char *		service_name = NULL;
	const char *	user_name = NULL;
	DBusMessageIter iter;
	GConfClient *	client;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	client = applet->gconf_client;

	if (    !dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &vpn_connection, DBUS_TYPE_INVALID)
		|| (strlen (vpn_connection) <= 0))
	{
		return new_invalid_args_error (message, __func__);
	}

	escaped_name = gconf_escape_key (vpn_connection, strlen (vpn_connection));

	/* User-visible name of connection */
	if (!nm_gconf_get_string_helper (client, GCONF_PATH_VPN_CONNECTIONS, "name", escaped_name, &name) || !name)
	{
		nm_warning ("%s:%d - couldn't get 'name' item from GConf.", __FILE__, __LINE__);
		goto out;
	}

	/* Service name of connection */
	if (!nm_gconf_get_string_helper (client, GCONF_PATH_VPN_CONNECTIONS, "service_name", escaped_name, &service_name) || !service_name)
	{
		nm_warning ("%s:%d - couldn't get 'service_name' item from GConf.", __FILE__, __LINE__);
		goto out;
	}

	/* User name of connection - use the logged in user */
	user_name = g_get_user_name ();

	reply = dbus_message_new_method_return (message);
	dbus_message_iter_init_append (reply, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &name);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &service_name);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &user_name);

out:
	g_free (service_name);
	g_free (name);
	g_free (escaped_name);

	return reply;
}


/*
 * nmi_dbus_get_vpn_connection_vpn_data
 *
 * Returns vpn-daemon specific properties for a particular VPN connection.
 *
 */
static DBusMessage *
nmi_dbus_get_vpn_connection_vpn_data (DBusConnection *connection,
                                      DBusMessage *message,
                                      void *user_data)
{
	NMApplet *	applet = (NMApplet *) user_data;
	DBusMessage *	reply = NULL;
	gchar *		gconf_key = NULL;
	char *		name = NULL;
	GConfValue *	vpn_data_value = NULL;
	GConfValue *	value = NULL;
	char *		escaped_name;
	DBusMessageIter iter, array_iter;
	GSList *		elt;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	if (!dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID) || (strlen (name) <= 0))
		return new_invalid_args_error (message, __func__);

	escaped_name = gconf_escape_key (name, strlen (name));

	/* User-visible name of connection */
	gconf_key = g_strdup_printf ("%s/%s/name", GCONF_PATH_VPN_CONNECTIONS, escaped_name);
	if (!(value = gconf_client_get (applet->gconf_client, gconf_key, NULL)))
	{
		reply = nmu_create_dbus_error_message (message, "BadVPNConnectionData",
						"NetworkManagerInfo::getVPNConnectionVPNData could not access the name for connection '%s'", name);
		return reply;
	}
	gconf_value_free (value);
	g_free (gconf_key);

	/* Grab vpn-daemon specific data */
	gconf_key = g_strdup_printf ("%s/%s/vpn_data", GCONF_PATH_VPN_CONNECTIONS, escaped_name);
	if (!(vpn_data_value = gconf_client_get (applet->gconf_client, gconf_key, NULL))
		|| !(vpn_data_value->type == GCONF_VALUE_LIST)
		|| !(gconf_value_get_list_type (vpn_data_value) == GCONF_VALUE_STRING))
	{
		reply = nmu_create_dbus_error_message (message, "BadVPNConnectionData",
						"NetworkManagerInfo::getVPNConnectionVPNData could not access the VPN data for connection '%s'", name);
		if (vpn_data_value)
			gconf_value_free (vpn_data_value);
		return reply;
	}
	g_free (gconf_key);

	reply = dbus_message_new_method_return (message);
	dbus_message_iter_init_append (reply, &iter);
	dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array_iter);

	for (elt = gconf_value_get_list (vpn_data_value); elt; elt = g_slist_next (elt))
	{
		const char *string = gconf_value_get_string ((GConfValue *)elt->data);
		if (string)
			dbus_message_iter_append_basic (&array_iter, DBUS_TYPE_STRING, &string);
	}

	dbus_message_iter_close_container (&iter, &array_iter);

	gconf_value_free (vpn_data_value);
	g_free (escaped_name);

	return reply;
}

/*
 * nmi_dbus_get_vpn_connection_routes
 *
 * Returns routes for a particular VPN connection.
 *
 */
static DBusMessage *
nmi_dbus_get_vpn_connection_routes (DBusConnection *connection,
                                    DBusMessage *message,
                                    void *user_data)
{
	NMApplet *	applet = (NMApplet *) user_data;
	DBusMessage *	reply = NULL;
	gchar *		gconf_key = NULL;
	char *		name = NULL;
	GConfValue *	routes_value = NULL;
	GConfValue *	value = NULL;
	char *		escaped_name;
	DBusMessageIter iter, array_iter;
	GSList *		elt;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	if (!dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID) || (strlen (name) <= 0))
		return new_invalid_args_error (message, __func__);

	escaped_name = gconf_escape_key (name, strlen (name));

	/* User-visible name of connection */
	gconf_key = g_strdup_printf ("%s/%s/name", GCONF_PATH_VPN_CONNECTIONS, escaped_name);
	if (!(value = gconf_client_get (applet->gconf_client, gconf_key, NULL)))
	{
		reply = nmu_create_dbus_error_message (message, "BadVPNConnectionData",
						"NetworkManagerInfo::getVPNConnectionRoutes could not access the name for connection '%s'", name);
		return reply;
	}
	gconf_value_free (value);
	g_free (gconf_key);

	/* Grab vpn-daemon specific data */
	gconf_key = g_strdup_printf ("%s/%s/routes", GCONF_PATH_VPN_CONNECTIONS, escaped_name);
	if (!(routes_value = gconf_client_get (applet->gconf_client, gconf_key, NULL))
		|| !(routes_value->type == GCONF_VALUE_LIST)
		|| !(gconf_value_get_list_type (routes_value) == GCONF_VALUE_STRING))
	{
		reply = nmu_create_dbus_error_message (message, "BadVPNConnectionData",
						"NetworkManagerInfo::getVPNConnectionRoutes could not access the routes for connection '%s'", name);
		if (routes_value)
			gconf_value_free (routes_value);
		return reply;
	}
	g_free (gconf_key);

	reply = dbus_message_new_method_return (message);
	dbus_message_iter_init_append (reply, &iter);
	dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array_iter);

	for (elt = gconf_value_get_list (routes_value); elt; elt = g_slist_next (elt))
	{
		const char *string = gconf_value_get_string ((GConfValue *)elt->data);
		if (string)
			dbus_message_iter_append_basic (&array_iter, DBUS_TYPE_STRING, &string);
	}

	dbus_message_iter_close_container (&iter, &array_iter);

	gconf_value_free (routes_value);
	g_free (escaped_name);

	return reply;
}


/*
 * nmi_dbus_info_message_handler
 *
 * Respond to requests against the NetworkManagerInfo object
 *
 */
DBusHandlerResult nmi_dbus_info_message_handler (DBusConnection *connection, DBusMessage *message, void *user_data)
{
	NMApplet *	applet = (NMApplet *)user_data;
	DBusMessage *	reply = NULL;
	gboolean		handled;

	g_return_val_if_fail (applet != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

	handled = dbus_method_dispatcher_dispatch (applet->nmi_methods,
                                                connection,
                                                message,
                                                &reply,
                                                applet);

	if (reply)
	{
		dbus_connection_send (connection, reply, NULL);
		dbus_message_unref (reply);
	}

	return (handled ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}

void nmi_dbus_signal_user_interface_activated (DBusConnection *connection)
{
	DBusMessage		*message;

	g_return_if_fail (connection != NULL);

	message = dbus_message_new_signal (NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "UserInterfaceActivated");
	if (!message)
	{
		nm_warning ("Not enough memory for new dbus message!");
		return;
	}

	if (!dbus_connection_send (connection, message, NULL))
		nm_warning ("Could not raise the 'UserInterfaceActivated' signal!");

	dbus_message_unref (message);
}

/*
 * nmi_dbus_nmi_methods_setup
 *
 * Register handlers for dbus methods on the org.freedesktop.NetworkManagerInfo object.
 *
 */
DBusMethodDispatcher *nmi_dbus_nmi_methods_setup (void)
{
	DBusMethodDispatcher *	dispatcher = dbus_method_dispatcher_new (NULL);

	dbus_method_dispatcher_register_method (dispatcher, "getVPNConnections",         nmi_dbus_get_vpn_connections);
	dbus_method_dispatcher_register_method (dispatcher, "getVPNConnectionProperties",nmi_dbus_get_vpn_connection_properties);
	dbus_method_dispatcher_register_method (dispatcher, "getVPNConnectionVPNData",   nmi_dbus_get_vpn_connection_vpn_data);
	dbus_method_dispatcher_register_method (dispatcher, "getVPNConnectionRoutes",    nmi_dbus_get_vpn_connection_routes);

	return dispatcher;
}
