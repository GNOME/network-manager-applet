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
#include <iwlib.h>

#include "NetworkManager.h"
#include "applet.h"
#include "applet-dbus.h"
#include "applet-dbus-info.h"
#include "passphrase-dialog.h"
#include "nm-wired-dialog.h"
#include "nm-utils.h"
#include "nm-gconf-wso.h"
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
 * nmi_network_type_valid
 *
 * Helper to validate network types NMI can deal with
 *
 */
static inline gboolean nmi_network_type_valid (NMNetworkType type)
{
	return ((type == NETWORK_TYPE_ALLOWED));
}


/*
 * nmi_dbus_get_key_for_network
 *
 * Throw up the user key dialog
 *
 */
static DBusMessage *
nmi_dbus_get_key_for_network (DBusConnection *connection,
                              DBusMessage *message,
                              void *user_data)
{
	NMApplet *		applet = (NMApplet *) user_data;
	char *			dev_path = NULL;
	char *			net_path = NULL;
	char *			essid = NULL;
	int				attempt = 0;
	gboolean			new_key = FALSE;
	NetworkDevice *	dev = NULL;
	WirelessNetwork *	net = NULL;
	char *			temp = NULL;
	char *			escaped_network;
	NMNetworkType type = NETWORK_TYPE_ALLOWED;
	const char *gconf_prefix = GCONF_PATH_WIRELESS_NETWORKS;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	if (!dbus_message_get_args (message, NULL,
	                           DBUS_TYPE_OBJECT_PATH, &dev_path,
	                           DBUS_TYPE_OBJECT_PATH, &net_path,
	                           DBUS_TYPE_STRING, &essid,
	                           DBUS_TYPE_INT32, &attempt,
	                           DBUS_TYPE_BOOLEAN, &new_key,
	                           DBUS_TYPE_INVALID))
		return NULL;

	if (!(dev = nma_get_device_for_nm_path (applet->device_list, dev_path)))
		return NULL;

	if (network_device_is_wired (dev)) {
		type = NETWORK_TYPE_WIRED;
		gconf_prefix = GCONF_PATH_WIRED_NETWORKS;
	}

	/* If we don't have a record of the network yet in GConf, ask for
	 * a new key no matter what NM says.
	 */
	escaped_network = gconf_escape_key (essid, strlen (essid));
	if (!nm_gconf_get_string_helper (applet->gconf_client,
                                      gconf_prefix,
                                      "essid",
                                      escaped_network, &temp)
         || !temp)
		new_key = TRUE;

	g_free (temp);

	/* It's not a new key, so try to get the key from the keyring. */
	if (!new_key)
	{
		NMGConfWSO *gconf_wso;

		/* If the menu happens to be showing when we pop up the
		 * keyring dialog, we get an X server deadlock.  So deactivate
		 * the menu here.
		 */
		if (applet->dropdown_menu && GTK_WIDGET_VISIBLE (GTK_WIDGET (applet->dropdown_menu)))
			gtk_menu_shell_deactivate (GTK_MENU_SHELL (applet->dropdown_menu));

		gconf_wso = nm_gconf_wso_new_deserialize_gconf (applet->gconf_client, type, escaped_network);
		if (!gconf_wso) {
			new_key = TRUE;
			goto new_key;
		}

		/* Grab secrets from the keyring, if any */
		if (!nm_gconf_wso_read_secrets (gconf_wso, essid)) {
			new_key = TRUE;
			goto new_key;
		}

		nmi_dbus_return_user_key (applet->connection, message, gconf_wso);
	}

new_key:
	if (new_key) {
		if (network_device_is_wireless (dev)) {
			/* We only ask the user for a new key when we know about the network from NM,
			 * since throwing up a dialog with a random essid from somewhere is a security issue.
			 */
			if ((net = network_device_get_wireless_network_by_nm_path (dev, net_path)))
				nmi_passphrase_dialog_new (applet, dev, net, message);

		} else if (network_device_is_wired (dev)) {
			nma_wired_dialog_ask_password (applet, escaped_network, message);
		} else {
			nm_warning ("Unhandled device type ('%s')", G_OBJECT_TYPE_NAME (dev));
		}
	}

	g_free (escaped_network);
	return NULL;
}


/*
 * nmi_dbus_dbus_return_user_key
 *
 * Alert NetworkManager of the new user key
 *
 */
void
nmi_dbus_return_user_key (DBusConnection *connection,
                          DBusMessage *message,
                          NMGConfWSO *gconf_wso)
{
	DBusMessage *		reply;
	DBusMessageIter	iter;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (message != NULL);
	g_return_if_fail (gconf_wso != NULL);

	reply = dbus_message_new_method_return (message);
	dbus_message_iter_init_append (reply, &iter);
	if (nm_gconf_wso_serialize_dbus (gconf_wso, &iter))
		dbus_connection_send (connection, reply, NULL);
	else
		nm_warning ("couldn't serialize gconf_wso");
	dbus_message_unref (reply);
}


static DBusMessage *
nmi_dbus_cancel_get_key_for_network (DBusConnection *connection,
                                     DBusMessage *message,
                                     void *user_data)
{
	NMApplet *	applet = (NMApplet *) user_data;

	g_return_val_if_fail (applet != NULL, NULL);

	if (applet->passphrase_dialog)
		gtk_widget_destroy (applet->passphrase_dialog);

	return NULL;
}


/*
 * nmi_dbus_signal_update_network
 *
 * Signal NetworkManager that it needs to update info associated with a particular
 * allowed/ignored network.
 *
 */
void nmi_dbus_signal_update_network (DBusConnection *connection, const char *network, NMNetworkType type)
{
	DBusMessage		*message;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (network != NULL);

	if (type != NETWORK_TYPE_ALLOWED)
		return;

	message = dbus_message_new_signal (NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "WirelessNetworkUpdate");
	dbus_message_append_args (message, DBUS_TYPE_STRING, &network, DBUS_TYPE_INVALID);
	if (!dbus_connection_send (connection, message, NULL))
		nm_warning ("Could not raise the 'WirelessNetworkUpdate' signal!");

	dbus_message_unref (message);
}


/*
 * nmi_dbus_get_networks
 *
 * Grab a list of access points from GConf and return it in the form
 * of a string array in a dbus message.
 *
 */
static DBusMessage *
nmi_dbus_get_networks (DBusConnection *connection,
                       DBusMessage *message,
                       void *user_data)
{
	const char * 		NO_NET_ERROR = "NoNetworks";
	const char * 		NO_NET_ERROR_MSG = "There are no wireless networks stored.";
	NMApplet *		applet = (NMApplet *) user_data;
	GSList *			dir_list = NULL;
	GSList *			elt;
	DBusMessage *		reply = NULL;
	DBusMessageIter	iter;
	DBusMessageIter	iter_array;
	NMNetworkType		type;
	gboolean			value_added = FALSE;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	if (	   !dbus_message_get_args (message, NULL, DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID)
		|| !nmi_network_type_valid (type))
		return new_invalid_args_error (message, __func__);

	/* List all allowed access points that gconf knows about */
	if (!(dir_list = gconf_client_all_dirs (applet->gconf_client, GCONF_PATH_WIRELESS_NETWORKS, NULL)))
		return nmu_create_dbus_error_message (message, NMI_DBUS_SERVICE, NO_NET_ERROR, NO_NET_ERROR_MSG);

	reply = dbus_message_new_method_return (message);
	dbus_message_iter_init_append (reply, &iter);
	dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &iter_array);

	/* Append the essid of every allowed or ignored access point we know of 
	 * to a string array in the dbus message.
	 */
	for (elt = dir_list; elt; elt = g_slist_next (elt))
	{
		char			key[100];
		GConfValue *	value;
		char *		dir = (char *) (elt->data);

		g_snprintf (&key[0], 99, "%s/essid", dir);
		if ((value = gconf_client_get (applet->gconf_client, key, NULL)))
		{
			if (value->type == GCONF_VALUE_STRING)
			{
				const char *essid = gconf_value_get_string (value);
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
		reply = nmu_create_dbus_error_message (message, NMI_DBUS_SERVICE, NO_NET_ERROR, NO_NET_ERROR_MSG);
	}

	return reply;
}


static void addr_list_append_helper (GConfValue *value, DBusMessageIter *iter)
{
	const char *string = gconf_value_get_string (value);
	dbus_message_iter_append_basic (iter, DBUS_TYPE_STRING, &string);
}

/*
 * nmi_dbus_get_network_properties
 *
 * Returns the properties of a specific wireless network from gconf
 *
 */
static DBusMessage *
nmi_dbus_get_network_properties (DBusConnection *connection,
                                 DBusMessage *message,
                                 void *user_data)
{
	NMApplet *		applet = (NMApplet *) user_data;
	DBusMessage *		reply = NULL;
	gchar *			gconf_key = NULL;
	char *			network = NULL;
	GConfValue *		bssids_value = NULL;
	NMNetworkType		type;
	char *			escaped_network = NULL;
	char *			essid = NULL;
	gint				timestamp = -1;
	gboolean			trusted = FALSE;
	DBusMessageIter 	iter, array_iter;
	GConfClient *		client;
	NMGConfWSO *		gconf_wso;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	client = applet->gconf_client;

	if (!dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &network, DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID))
	{
		nm_warning ("%s:%d - message arguments were invalid.", __FILE__, __LINE__);
		goto out;
	}

	if (!nmi_network_type_valid (type) || (strlen (network) <= 0))
	{
		nm_warning ("%s:%d - network or network type was invalid.", __FILE__, __LINE__);
		goto out;
	}

	if (!(escaped_network = gconf_escape_key (network, strlen (network))))
	{
		nm_warning ("%s:%d - couldn't unescape network name.", __FILE__, __LINE__);
		goto out;
	}

	/* ESSID */
	if (!nm_gconf_get_string_helper (client, GCONF_PATH_WIRELESS_NETWORKS, "essid", escaped_network, &essid) || !essid)
	{
		nm_warning ("%s:%d - couldn't get 'essid' item from GConf for '%s'.",
				__FILE__, __LINE__, network);
		goto out;
	}

	/* Timestamp.  If the timestamp is not set, return zero. */
	if (!nm_gconf_get_int_helper (client, GCONF_PATH_WIRELESS_NETWORKS, "timestamp", escaped_network, &timestamp) || (timestamp < 0))
		timestamp = 0;

	/* Trusted status */
	if (!nm_gconf_get_bool_helper (client, GCONF_PATH_WIRELESS_NETWORKS, "trusted", escaped_network, &trusted))
		trusted = FALSE;

	/* Grab the list of stored access point BSSIDs */
	gconf_key = g_strdup_printf ("%s/%s/bssids", GCONF_PATH_WIRELESS_NETWORKS, escaped_network);
	bssids_value = gconf_client_get (client, gconf_key, NULL);
	g_free (gconf_key);
	if (bssids_value && ((bssids_value->type != GCONF_VALUE_LIST) || (gconf_value_get_list_type (bssids_value) != GCONF_VALUE_STRING)))
	{
		nm_warning ("%s:%d - addresses value existed in GConf, but was not a string list for '%s'.",
				__FILE__, __LINE__, essid);
		goto out;
	}

	/* Get the network's security information from GConf */
	if (!(gconf_wso = nm_gconf_wso_new_deserialize_gconf (client, type, escaped_network)))
	{
		nm_warning ("%s:%d - couldn't retrieve security information from "
				"GConf for '%s'.", __FILE__, __LINE__, essid);
		goto out;
	}

	/* Build reply */
	reply = dbus_message_new_method_return (message);
	dbus_message_iter_init_append (reply, &iter);

	/* First arg: ESSID (STRING) */
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &essid);

	/* Second arg: Timestamp (INT32) */
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT32, &timestamp);

	/* Third arg: Trusted (BOOLEAN) */
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &trusted);

	/* Fourth arg: List of AP BSSIDs (ARRAY, STRING) */
	dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array_iter);
	if (bssids_value && (g_slist_length (gconf_value_get_list (bssids_value)) > 0))
		g_slist_foreach (gconf_value_get_list (bssids_value), (GFunc) addr_list_append_helper, &array_iter);
	else
	{
		const char *fake = "";
		dbus_message_iter_append_basic (&array_iter, DBUS_TYPE_STRING, &fake);
	}
	dbus_message_iter_close_container (&iter, &array_iter);

	/* Serialize the security info into the message */
	nm_gconf_wso_serialize_dbus (gconf_wso, &iter);
	g_object_unref (G_OBJECT (gconf_wso));

out:
	if (bssids_value)
		gconf_value_free (bssids_value);

	g_free (essid);
	g_free (escaped_network);

	if (!reply)
		reply = new_invalid_args_error (message, __func__);

	return reply;
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
 * nmi_save_network_info
 *
 * Save information about a wireless network in gconf and the gnome keyring.
 *
 */
static void
nmi_save_network_info (NMApplet *applet,
                       const char *essid,
                       gboolean automatic,
                       const char *bssid,
                       NMGConfWSO * gconf_wso)
{
	char *					key;
	GConfEntry *				gconf_entry;
	char *					escaped_network;
	const char *gconf_prefix = GCONF_PATH_WIRELESS_NETWORKS;
	NMNetworkType type = NETWORK_TYPE_ALLOWED;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (essid != NULL);
	g_return_if_fail (gconf_wso != NULL);

	/* Crappy hack */
	if (!strncmp (bssid, "WIRED", 5)) {
		type = NETWORK_TYPE_WIRED;
		gconf_prefix = GCONF_PATH_WIRED_NETWORKS;
	}

	escaped_network = gconf_escape_key (essid, strlen (essid));
	key = g_strdup_printf ("%s/%s", gconf_prefix, escaped_network);
	gconf_entry = gconf_client_get_entry (applet->gconf_client, key, NULL, TRUE, NULL);
	g_free (key);
	if (!gconf_entry)
	{
		nm_warning ("Failed to create or obtain GConf entry for '%s'.", essid);
		goto out;
	}
	gconf_entry_unref (gconf_entry);

	key = g_strdup_printf ("%s/%s/essid", gconf_prefix, escaped_network);
	gconf_client_set_string (applet->gconf_client, key, essid, NULL);
	g_free (key);

	/* We only update the timestamp if the user requested a particular network, not if
	 * NetworkManager decided to switch access points by itself.
	 */
	if (!automatic)
	{
		key = g_strdup_printf ("%s/%s/timestamp", gconf_prefix, escaped_network);
		gconf_client_set_int (applet->gconf_client, key, time (NULL), NULL);
		g_free (key);
	}

	if (bssid && (strlen (bssid) >= 11))
	{
		GConfValue *	value;
		GSList *		new_bssid_list = NULL;
		gboolean		found = FALSE;

		/* Get current list of access point BSSIDs for this AP from GConf */
		key = g_strdup_printf ("%s/%s/bssids", gconf_prefix, escaped_network);
		if ((value = gconf_client_get (applet->gconf_client, key, NULL)))
		{
			if ((value->type == GCONF_VALUE_LIST) && (gconf_value_get_list_type (value) == GCONF_VALUE_STRING))
			{
				GSList *	elt;

				new_bssid_list = gconf_client_get_list (applet->gconf_client, key, GCONF_VALUE_STRING, NULL);

				/* Ensure that the MAC isn't already in the list */
				for (elt = new_bssid_list; elt; elt = g_slist_next (elt))
				{
					if (elt->data && !strcmp (bssid, elt->data))
					{
						found = TRUE;
						break;
					}
				}
			}
			gconf_value_free (value);
		}

		/* Add the new MAC address to the end of the list */
		if (!found)
		{
			new_bssid_list = g_slist_append (new_bssid_list, g_strdup (bssid));
			gconf_client_set_list (applet->gconf_client, key, GCONF_VALUE_STRING, new_bssid_list, NULL);
		}
		g_free (key);

		/* Free the list, since gconf_client_set_list deep-copies it */
		g_slist_foreach (new_bssid_list, (GFunc) g_free, NULL);
		g_slist_free (new_bssid_list);
	}

	/* Stuff the security information into GConf */
	if (!nm_gconf_wso_serialize_gconf (gconf_wso, applet->gconf_client, type, escaped_network))
	{
		nm_warning ("%s:%d - Couldn't serialize security info for '%s'.",
				__FILE__, __LINE__, essid);
	}

	/* Stuff the encryption key into the keyring */
	nm_gconf_wso_write_secrets (gconf_wso, essid);

out:
	g_free (escaped_network);
}



/*
 * nmi_dbus_update_network_info
 *
 * Update a network's authentication method and encryption key in gconf & the keyring
 *
 */
static DBusMessage *
nmi_dbus_update_network_info (DBusConnection *connection,
                              DBusMessage *message,
                              void *user_data)
{
	NMApplet *		applet = (NMApplet *) user_data;
	char *			essid = NULL;
	gboolean			automatic;
	NMGConfWSO *		gconf_wso = NULL;
	DBusMessageIter	iter;
	char *			bssid;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_message_iter_init (message, &iter);

	/* First argument: ESSID (STRING) */
	if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_STRING)
	{
		nm_warning ("%s:%d - message format was invalid.", __FILE__, __LINE__);
		goto out;
	}
	dbus_message_iter_get_basic (&iter, &essid);
	if (strlen (essid) <= 0)
	{
		nm_warning ("%s:%d - message argument 'essid' was invalid.", __FILE__, __LINE__);
		goto out;
	}

	/* Second argument: Automatic (BOOLEAN) */
	if (!dbus_message_iter_next (&iter) || (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_BOOLEAN))
	{
		nm_warning ("%s:%d - message argument 'automatic' was invalid.", __FILE__, __LINE__);
		goto out;
	}
	dbus_message_iter_get_basic (&iter, &automatic);

	/* Third argument: Access point's BSSID */
	if (!dbus_message_iter_next (&iter) || (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_STRING))
	{
		nm_warning ("%s:%d - message argument 'bssid' was invalid.", __FILE__, __LINE__);
		goto out;
	}
	dbus_message_iter_get_basic (&iter, &bssid);

	/* Deserialize the sercurity option out of the message */
	if (!dbus_message_iter_next (&iter))
		goto out;

	if (!(gconf_wso = nm_gconf_wso_new_deserialize_dbus (&iter)))
	{
		nm_warning ("%s:%d - couldn't get security information from the message.", __FILE__, __LINE__);
		goto out;
	}

	nmi_save_network_info (applet, essid, automatic, bssid, gconf_wso);
	g_object_unref (G_OBJECT (gconf_wso));

out:
	return NULL;
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

	dbus_method_dispatcher_register_method (dispatcher, "getKeyForNetwork",          nmi_dbus_get_key_for_network);
	dbus_method_dispatcher_register_method (dispatcher, "cancelGetKeyForNetwork",    nmi_dbus_cancel_get_key_for_network);
	dbus_method_dispatcher_register_method (dispatcher, "getNetworks",               nmi_dbus_get_networks);
	dbus_method_dispatcher_register_method (dispatcher, "getNetworkProperties",      nmi_dbus_get_network_properties);
	dbus_method_dispatcher_register_method (dispatcher, "updateNetworkInfo",         nmi_dbus_update_network_info);
	dbus_method_dispatcher_register_method (dispatcher, "getVPNConnections",         nmi_dbus_get_vpn_connections);
	dbus_method_dispatcher_register_method (dispatcher, "getVPNConnectionProperties",nmi_dbus_get_vpn_connection_properties);
	dbus_method_dispatcher_register_method (dispatcher, "getVPNConnectionVPNData",   nmi_dbus_get_vpn_connection_vpn_data);
	dbus_method_dispatcher_register_method (dispatcher, "getVPNConnectionRoutes",    nmi_dbus_get_vpn_connection_routes);

	return dispatcher;
}
