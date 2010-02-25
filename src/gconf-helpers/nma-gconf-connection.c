/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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

#include <string.h>
#include <unistd.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gnome-keyring.h>

#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>
#include <nm-setting-8021x.h>

#include "nma-gconf-connection.h"
#include "gconf-helpers.h"
#include "nm-utils.h"
#include "utils.h"
#include "nma-marshal.h"
#include "nm-settings-interface.h"

static NMSettingsConnectionInterface *parent_settings_connection_iface;

static void settings_connection_interface_init (NMSettingsConnectionInterface *class);

G_DEFINE_TYPE_EXTENDED (NMAGConfConnection, nma_gconf_connection, NM_TYPE_EXPORTED_CONNECTION, 0,
                        G_IMPLEMENT_INTERFACE (NM_TYPE_SETTINGS_CONNECTION_INTERFACE,
                                               settings_connection_interface_init))

#define NMA_GCONF_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NMA_TYPE_GCONF_CONNECTION, NMAGConfConnectionPrivate))

typedef struct {
	GConfClient *client;
	char *dir;

	gboolean disposed;
} NMAGConfConnectionPrivate;

enum {
	PROP_0,
	PROP_CLIENT,
	PROP_DIR,

	LAST_PROP
};

enum {
	NEW_SECRETS_REQUESTED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

NMAGConfConnection *
nma_gconf_connection_new (GConfClient *client, const char *conf_dir)
{
	NMConnection *connection;
	NMAGConfConnection *gconf_connection;

	g_return_val_if_fail (GCONF_IS_CLIENT (client), NULL);
	g_return_val_if_fail (conf_dir != NULL, NULL);

	/* retrieve GConf data */
	connection = nm_gconf_read_connection (client, conf_dir);
	if (connection) {
		gconf_connection = nma_gconf_connection_new_from_connection (client, conf_dir, connection);
		g_object_unref (connection);
	} else {
		nm_warning ("No connection read from GConf at %s.", conf_dir);
		gconf_connection = NULL;
	}
	
	return gconf_connection;
}

NMAGConfConnection *
nma_gconf_connection_new_from_connection (GConfClient *client,
                                          const char *conf_dir,
                                          NMConnection *connection)
{
	GObject *object;
	NMAGConfConnection *self;
	GError *error = NULL;
	gboolean success;
	GHashTable *settings;

	g_return_val_if_fail (GCONF_IS_CLIENT (client), NULL);
	g_return_val_if_fail (conf_dir != NULL, NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	/* Ensure the connection is valid first */
	success = nm_connection_verify (connection, &error);
	if (!success) {
		g_warning ("Invalid connection %s: '%s' / '%s' invalid: %d",
		           conf_dir,
		           g_type_name (nm_connection_lookup_setting_type_by_quark (error->domain)),
		           (error && error->message) ? error->message : "(unknown)",
		           error ? error->code : -1);
		g_clear_error (&error);
		return NULL;
	}

	object = g_object_new (NMA_TYPE_GCONF_CONNECTION,
	                       NMA_GCONF_CONNECTION_CLIENT, client,
	                       NMA_GCONF_CONNECTION_DIR, conf_dir,
	                       NM_CONNECTION_SCOPE, NM_CONNECTION_SCOPE_USER,
	                       NULL);
	if (!object)
		return NULL;

	self = NMA_GCONF_CONNECTION (object);

	/* Fill certs so that the nm_connection_replace_settings verification works */
	settings = nm_connection_to_hash (connection);
	success = nm_connection_replace_settings (NM_CONNECTION (self), settings, NULL);
	g_hash_table_destroy (settings);

	/* Already verified the settings above, they had better be OK */
	g_assert (success);

	return self;
}

const char *
nma_gconf_connection_get_gconf_path (NMAGConfConnection *self)
{
	g_return_val_if_fail (NMA_IS_GCONF_CONNECTION (self), NULL);

	return NMA_GCONF_CONNECTION_GET_PRIVATE (self)->dir;
}

static void
add_vpn_user_name (NMConnection *connection)
{
	NMSettingVPN *s_vpn;
	const char *user_name;

	/* Insert the default VPN username when NM gets the connection; it doesn't
	 * get stored in GConf since it's always available and could change at any
	 * time, so it's inserted on-the-fly.
	 */
	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	if (s_vpn) {
		user_name = g_get_user_name ();
		g_assert (g_utf8_validate (user_name, -1, NULL));
		g_object_set (s_vpn, NM_SETTING_VPN_USER_NAME, user_name, NULL);
	}
}

gboolean
nma_gconf_connection_gconf_changed (NMAGConfConnection *self)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (self);
	NMConnection *new;
	GHashTable *new_settings;
	GError *error = NULL;
	gboolean success;

	new = nm_gconf_read_connection (priv->client, priv->dir);
	if (!new) {
		g_warning ("No connection read from GConf at %s.", priv->dir);
		goto invalid;
	}

	success = nm_connection_verify (new, &error);
	if (!success) {
		g_warning ("%s: Invalid connection %s: '%s' / '%s' invalid: %d",
		           __func__, priv->dir,
		           g_type_name (nm_connection_lookup_setting_type_by_quark (error->domain)),
		           error->message, error->code);
		g_object_unref (new);
		goto invalid;
	}

	/* Ignore the GConf update if nothing changed */
	if (nm_connection_compare (NM_CONNECTION (self), new, NM_SETTING_COMPARE_FLAG_EXACT)) {
		g_object_unref (new);
		return TRUE;
	}

	new_settings = nm_connection_to_hash (new);
	success = nm_connection_replace_settings (NM_CONNECTION (self), new_settings, &error);
	g_hash_table_destroy (new_settings);
	g_object_unref (new);

	if (!success) {
		g_warning ("%s: '%s' / '%s' invalid: %d",
		           __func__,
		           error ? g_type_name (nm_connection_lookup_setting_type_by_quark (error->domain)) : "(none)",
		           (error && error->message) ? error->message : "(none)",
		           error ? error->code : -1);
		goto invalid;
	}

	add_vpn_user_name (NM_CONNECTION (self));

	nm_settings_connection_interface_emit_updated (NM_SETTINGS_CONNECTION_INTERFACE (self));
	return TRUE;

invalid:
	g_clear_error (&error);
	g_signal_emit_by_name (self, NM_SETTINGS_CONNECTION_INTERFACE_REMOVED);
	return FALSE;
}

/******************************************************/

static GValue *
string_to_gvalue (const char *str)
{
	GValue *val;

	val = g_slice_new0 (GValue);
	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, str);

	return val;
}

#define FILE_TAG "file://"

static GValue *
path_to_gvalue (const char *path)
{
	GValue *val;
	GByteArray *array;

	array = g_byte_array_sized_new (strlen (FILE_TAG) + strlen (path) + 1);
	g_byte_array_append (array, (guint8 *) FILE_TAG, strlen (FILE_TAG));
	g_byte_array_append (array, (guint8 *) path, strlen (path) + 1);  /* +1 for the trailing NULL */

	val = g_slice_new0 (GValue);
	g_value_init (val, DBUS_TYPE_G_UCHAR_ARRAY);
	g_value_take_boxed (val, array);

	return val;
}

static void
destroy_gvalue (gpointer data)
{
	GValue *value = (GValue *) data;

	g_value_unset (value);
	g_slice_free (GValue, value);
}

static GHashTable *
nma_gconf_connection_get_keyring_items (NMAGConfConnection *self,
                                        const char *setting_name,
                                        GError **error)
{
	NMAGConfConnectionPrivate *priv;
	NMSettingConnection *s_con;
	GHashTable *secrets;
	GList *found_list = NULL;
	GnomeKeyringResult ret;
	GList *iter;
	const char *connection_name;
	char *path = NULL;

	g_return_val_if_fail (self != NULL, NULL);
	g_return_val_if_fail (setting_name != NULL, NULL);
	g_return_val_if_fail (error != NULL, NULL);
	g_return_val_if_fail (*error == NULL, NULL);

	priv = NMA_GCONF_CONNECTION_GET_PRIVATE (self);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (NM_CONNECTION (self), NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	connection_name = nm_setting_connection_get_id (s_con);
	g_assert (connection_name);

	pre_keyring_callback ();

	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      KEYRING_UUID_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
										  nm_setting_connection_get_uuid (s_con),
	                                      KEYRING_SN_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      setting_name,
	                                      NULL);
	if ((ret != GNOME_KEYRING_RESULT_OK) || (g_list_length (found_list) == 0))
		return NULL;

	secrets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, destroy_gvalue);

	for (iter = found_list; iter != NULL; iter = g_list_next (iter)) {
		GnomeKeyringFound *found = (GnomeKeyringFound *) iter->data;
		int i;
		const char *key_name = NULL;

		for (i = 0; i < found->attributes->len; i++) {
			GnomeKeyringAttribute *attr;

			attr = &(gnome_keyring_attribute_list_index (found->attributes, i));
			if (   (strcmp (attr->name, KEYRING_SK_TAG) == 0)
			    && (attr->type == GNOME_KEYRING_ATTRIBUTE_TYPE_STRING)) {
				key_name = attr->value.string;
				break;
			}
		}

		if (key_name == NULL) {
			g_set_error (error,
			             NM_SETTINGS_INTERFACE_ERROR,
			             NM_SETTINGS_INTERFACE_ERROR_SECRETS_UNAVAILABLE,
			             "%s.%d - Internal error; keyring item '%s/%s' didn't "
			             "have a 'setting-key' attribute.",
			             __FILE__, __LINE__, connection_name, setting_name);
			break;
		}

		g_hash_table_insert (secrets,
		                     g_strdup (key_name),
		                     string_to_gvalue (found->secret));
	}

	/* The phase1 and phase2 private key are still marked as 'secret' for
	 * backwards compat, but since they don't get stored in the keyring since
	 * they aren't really secret (because we now use paths everywhere and not
	 * the decrypted private key like 0.7.x).  So we need to grab them out of
	 * GConf and add them to the returned secret hash.
	 */
	/* Private key path */
	path = NULL;
	if (nm_gconf_get_string_helper (priv->client,
	                                priv->dir,
	                                NM_SETTING_802_1X_PRIVATE_KEY,
	                                NM_SETTING_802_1X_SETTING_NAME,
	                                &path)) {
		g_hash_table_insert (secrets,
		                     g_strdup (NM_SETTING_802_1X_PRIVATE_KEY),
		                     path_to_gvalue (path));
		g_free (path);
	}

	/* Phase2 private key path */
	path = NULL;
	if (nm_gconf_get_string_helper (priv->client,
	                                priv->dir,
	                                NM_SETTING_802_1X_PHASE2_PRIVATE_KEY,
	                                NM_SETTING_802_1X_SETTING_NAME,
	                                &path)) {
		g_hash_table_insert (secrets,
		                     g_strdup (NM_SETTING_802_1X_PHASE2_PRIVATE_KEY),
		                     path_to_gvalue (path));
		g_free (path);
	}

	if (*error) {
		nm_warning ("%s: error reading secrets: (%d) %s", __func__,
		            (*error)->code, (*error)->message);
		g_hash_table_destroy (secrets);
		secrets = NULL;
	}

	gnome_keyring_found_list_free (found_list);
	return secrets;
}

static void
delete_done (GnomeKeyringResult result, gpointer user_data)
{
}

static void
clear_keyring_items (NMAGConfConnection *self)
{
	NMSettingConnection *s_con;
	const char *uuid;
	GList *found_list = NULL;
	GnomeKeyringResult ret;
	GList *iter;

	g_return_if_fail (self != NULL);

	s_con = (NMSettingConnection *) nm_connection_get_setting (NM_CONNECTION (self), NM_TYPE_SETTING_CONNECTION);
	g_return_if_fail (s_con != NULL);

	uuid = nm_setting_connection_get_uuid (s_con);
	g_return_if_fail (uuid != NULL);

	pre_keyring_callback ();

	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      KEYRING_UUID_TAG,
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      uuid,
	                                      NULL);
	if (ret == GNOME_KEYRING_RESULT_OK) {
		for (iter = found_list; iter != NULL; iter = g_list_next (iter)) {
			GnomeKeyringFound *found = (GnomeKeyringFound *) iter->data;

			gnome_keyring_item_delete (found->keyring,
			                           found->item_id,
			                           delete_done,
			                           NULL,
			                           NULL);
		}
		gnome_keyring_found_list_free (found_list);
	}
}

void
nma_gconf_connection_update (NMAGConfConnection *self,
                             gboolean ignore_secrets)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (self);

	nm_gconf_write_connection (NM_CONNECTION (self),
	                           priv->client,
	                           priv->dir,
	                           ignore_secrets);
	gconf_client_notify (priv->client, priv->dir);
	gconf_client_suggest_sync (priv->client, NULL);
}

/******************************************************/

static gboolean
update (NMSettingsConnectionInterface *connection,
	    NMSettingsConnectionInterfaceUpdateFunc callback,
	    gpointer user_data)
{
	/* Always update secrets since it's assumed that secrets are included in
	 * the new connection data.
	 */
	nma_gconf_connection_update (NMA_GCONF_CONNECTION (connection), FALSE);

	return parent_settings_connection_iface->update (connection, callback, user_data);
}

static gboolean 
do_delete (NMSettingsConnectionInterface *connection,
	       NMSettingsConnectionInterfaceDeleteFunc callback,
	       gpointer user_data)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (connection);
	gboolean success;
	GError *error = NULL;

	/* Clean up keyring keys */
	clear_keyring_items (NMA_GCONF_CONNECTION (connection));

	success = gconf_client_recursive_unset (priv->client, priv->dir, 0, &error);
	if (!success) {
		callback (connection, error, user_data);
		g_error_free (error);
		return FALSE;
	}
	gconf_client_suggest_sync (priv->client, NULL);

	return parent_settings_connection_iface->delete (connection, callback, user_data);
}

static gboolean
is_otp_always_ask (NMConnection *connection)
{
	NMSetting8021x *s_8021x;
	NMSettingConnection *s_con;
	const char *uuid, *eap_method, *phase2;

	s_8021x = (NMSetting8021x *) nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
	if (s_8021x) {
		gboolean can_always_ask = FALSE;

		/* Check if PEAP or TTLS is used */
		eap_method = nm_setting_802_1x_get_eap_method (s_8021x, 0);
		s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
		if (!strcmp (eap_method, "peap"))
			can_always_ask = TRUE;
		else if (!strcmp (eap_method, "ttls")) {
			/* Now make sure the phase2 method isn't TLS */
			phase2 = nm_setting_802_1x_get_phase2_auth (s_8021x);
			if (phase2 && strcmp (phase2, "tls"))
				can_always_ask = TRUE;
			else {
				phase2 = nm_setting_802_1x_get_phase2_autheap (s_8021x);
				if (phase2 && strcmp (phase2, "tls"))
					can_always_ask = TRUE;
			}
		}

		if (can_always_ask) {
			uuid = nm_setting_connection_get_uuid (s_con);
			if (nm_gconf_get_8021x_password_always_ask (uuid))
				return TRUE;
		}
	}
	return FALSE;
}

static gboolean
internal_get_secrets (NMSettingsConnectionInterface *connection,
                      const char *setting_name,
                      const char **hints,
                      gboolean request_new,
                      gboolean local,
                      NMANewSecretsRequestedFunc callback,
                      gpointer callback_data,
                      GError **error)
{
	NMAGConfConnection *self = NMA_GCONF_CONNECTION (connection);
	GHashTable *settings = NULL;
	GHashTable *secrets = NULL;
	NMSettingConnection *s_con;
	NMSetting *setting;
	const char *connection_id;
	const char *connection_type;

	setting = nm_connection_get_setting_by_name (NM_CONNECTION (self), setting_name);
	if (!setting) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INVALID_CONNECTION,
		             "%s.%d - Connection didn't have requested setting '%s'.",
		             __FILE__, __LINE__, setting_name);
		return FALSE;
	}

	s_con = (NMSettingConnection *) nm_connection_get_setting (NM_CONNECTION (self), NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);
	connection_id = nm_setting_connection_get_id (s_con);
	connection_type = nm_setting_connection_get_connection_type (s_con);

	if (!s_con || !connection_id || !strlen (connection_id) || !connection_type) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INVALID_CONNECTION,
		             "%s.%d - Connection didn't have required '"
		             NM_SETTING_CONNECTION_SETTING_NAME
		             "' setting , or the connection name was invalid.",
		             __FILE__, __LINE__);
		return FALSE;
	}

	/* Only try to get new secrets for D-Bus requests */
	if (local) {
		secrets = nma_gconf_connection_get_keyring_items (self, setting_name, error);
		if (!secrets && error && *error)
			return FALSE;
	} else {
		/* VPN passwords are handled by the VPN plugin's auth dialog */
		if (!strcmp (connection_type, NM_SETTING_VPN_SETTING_NAME))
			goto get_secrets;

		if (request_new) {
			nm_info ("New secrets for %s/%s requested; ask the user",
			         connection_id, setting_name);
			nm_connection_clear_secrets (NM_CONNECTION (self));
			goto get_secrets;
		}

		/* OTP connections marked as always ask */
		if (is_otp_always_ask (NM_CONNECTION (self)))
			goto get_secrets;

		secrets = nma_gconf_connection_get_keyring_items (self, setting_name, error);
		if (!secrets) {
			if (error && *error)
				return FALSE;

			nm_info ("No keyring secrets found for %s/%s; asking user.",
			         connection_id, setting_name);
			goto get_secrets;
		}

		if (g_hash_table_size (secrets) == 0) {
			g_hash_table_destroy (secrets);
			nm_warning ("%s.%d - Secrets were found for setting '%s' but none"
					  " were valid.", __FILE__, __LINE__, setting_name);
			goto get_secrets;
		}

		/* If there were hints, and none of the hints were returned by the keyring,
		 * get some new secrets.
		 */
		if (hints && g_strv_length ((char **) hints)) {
			GHashTableIter iter;
			gpointer key, value;
			gboolean found = FALSE;

			g_hash_table_iter_init (&iter, secrets);
			while (g_hash_table_iter_next (&iter, &key, &value) && !found) {
				const char **hint = hints;

				while (!found && *hint) {
					if (!strcmp (*hint, (const char *) key) && value && G_IS_VALUE (value)) {
						found = TRUE;
						break;
					}
					hint++;
				}
			}

			if (!found) {
				g_hash_table_destroy (secrets);
				goto get_secrets;
			}
		}
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                  g_free,
	                                  (GDestroyNotify) g_hash_table_destroy);
	if (secrets)
		g_hash_table_insert (settings, g_strdup (setting_name), secrets);
	callback (NM_SETTINGS_CONNECTION_INTERFACE (self), settings, NULL, callback_data);
	g_hash_table_destroy (settings);
	return TRUE;

get_secrets:
	g_signal_emit (self,
	               signals[NEW_SECRETS_REQUESTED],
	               0,
	               setting_name,
	               hints,
	               request_new,
	               callback,
	               callback_data);
	return TRUE;
}

typedef struct {
	NMSettingsConnectionInterfaceGetSecretsFunc callback;
	gpointer callback_data;
} GetSecretsInfo;

static void
get_secrets_cb (NMSettingsConnectionInterface *connection,
                GHashTable *settings,
                GError *error,
                gpointer user_data)
{
	GetSecretsInfo *info = user_data;

	info->callback (NM_SETTINGS_CONNECTION_INTERFACE (connection), settings, error, info->callback_data);
	g_free (info);
}

static gboolean
get_secrets (NMSettingsConnectionInterface *connection,
	         const char *setting_name,
             const char **hints,
             gboolean request_new,
             NMSettingsConnectionInterfaceGetSecretsFunc callback,
             gpointer user_data)
{
	GetSecretsInfo *info;
	GError *error = NULL;

	info = g_malloc0 (sizeof (GetSecretsInfo));
	info->callback = callback;
	info->callback_data = user_data;

	if (!internal_get_secrets (connection,
	                           setting_name,
	                           hints,
	                           request_new,
	                           TRUE,
	                           get_secrets_cb,
	                           info,
	                           &error)) {
		callback (NM_SETTINGS_CONNECTION_INTERFACE (connection), NULL, error, user_data);
		g_error_free (error);
		g_free (info);
		return FALSE;
	}

	return TRUE;
}

/******************************************************/

static gboolean
is_dbus_request_authorized (DBusGMethodInvocation *context,
                            gboolean allow_user,
                            GError **error)
{
	DBusGConnection *bus = NULL;
	DBusConnection *connection = NULL;
	char *sender = NULL;
	gulong sender_uid = G_MAXULONG;
	DBusError dbus_error;
	gboolean success = FALSE;

	sender = dbus_g_method_get_sender (context);
	if (!sender) {
		g_set_error (error, NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s", "Could not determine D-Bus requestor");
		goto out;
	}

	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	if (!bus) {
		g_set_error (error, NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s", "Could not get the system bus");
		goto out;
	}
	connection = dbus_g_connection_get_connection (bus);
	if (!connection) {
		g_set_error (error, NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s", "Could not get the D-Bus system bus");
		goto out;
	}

	dbus_error_init (&dbus_error);
	sender_uid = dbus_bus_get_unix_user (connection, sender, &dbus_error);
	if (dbus_error_is_set (&dbus_error)) {
		dbus_error_free (&dbus_error);
		g_set_error (error, NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_PERMISSION_DENIED,
		             "%s", "Could not determine the Unix user ID of the requestor");
		goto out;
	}

	/* And finally, the actual UID check */
	if (   (allow_user && (sender_uid == geteuid()))
	    || (sender_uid == 0))
		success = TRUE;
	else {
		g_set_error (error, NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_PERMISSION_DENIED,
		             "%s", "Requestor UID does not match the UID of the user settings service");
	}

out:
	if (bus)
		dbus_g_connection_unref (bus);
	g_free (sender);
	return success;
}

static void
con_update_cb (NMSettingsConnectionInterface *connection,
               GError *error,
               gpointer user_data)
{
	DBusGMethodInvocation *context = user_data;

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context);
}

static void
dbus_update (NMExportedConnection *exported,
             GHashTable *new_settings,
             DBusGMethodInvocation *context)
{
	NMAGConfConnection *self = NMA_GCONF_CONNECTION (exported);
	NMConnection *new;
	gboolean success = FALSE;
	GError *error = NULL;

	/* Restrict Update to execution by the current user and root for DBus invocation */
	if (!is_dbus_request_authorized (context, TRUE, &error)) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	new = nm_connection_new_from_hash (new_settings, &error);
	if (!new) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}
	g_object_unref (new);
	
	success = nm_connection_replace_settings (NM_CONNECTION (self), new_settings, NULL);
	/* Settings better be valid; we verified them above */
	g_assert (success);

	nm_settings_connection_interface_update (NM_SETTINGS_CONNECTION_INTERFACE (self),
	                                         con_update_cb,
	                                         context);
}

static void
con_delete_cb (NMSettingsConnectionInterface *connection,
               GError *error,
               gpointer user_data)
{
	DBusGMethodInvocation *context = user_data;

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context);
}

static void
dbus_delete (NMExportedConnection *exported,
             DBusGMethodInvocation *context)
{
	NMAGConfConnection *self = NMA_GCONF_CONNECTION (exported);
	GError *error = NULL;

	/* Restrict Update to execution by the current user and root for DBus invocation */
	if (!is_dbus_request_authorized (context, TRUE, &error)) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	nm_settings_connection_interface_delete (NM_SETTINGS_CONNECTION_INTERFACE (self),
	                                         con_delete_cb,
	                                         context);
}

static void
dbus_get_secrets_cb (NMSettingsConnectionInterface *connection,
                     GHashTable *settings,
                     GError *error,
                     gpointer user_data)
{
	DBusGMethodInvocation *context = user_data;

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context, settings);
}

static void
dbus_get_secrets (NMExportedConnection *connection,
                  const gchar *setting_name,
                  const gchar **hints,
                  gboolean request_new,
                  DBusGMethodInvocation *context)
{
	GError *error = NULL;

	/* Restrict GetSecrets to execution by root for DBus invocation */
	if (!is_dbus_request_authorized (context, FALSE, &error)) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	if (!internal_get_secrets (NM_SETTINGS_CONNECTION_INTERFACE (connection),
	                           setting_name,
	                           hints,
	                           request_new,
	                           FALSE,
	                           dbus_get_secrets_cb,
	                           context,
	                           &error)) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}
}

static GHashTable *
dbus_get_settings (NMExportedConnection *connection, GError **error)
{
	add_vpn_user_name (NM_CONNECTION (connection));

	return NM_EXPORTED_CONNECTION_CLASS (nma_gconf_connection_parent_class)->get_settings (connection, error);
}

/************************************************************/

static void
settings_connection_interface_init (NMSettingsConnectionInterface *iface)
{
	parent_settings_connection_iface = g_type_interface_peek_parent (iface);

	iface->update = update;
	iface->delete = do_delete;
	iface->get_secrets = get_secrets;
}

static void
nma_gconf_connection_init (NMAGConfConnection *connection)
{
}

static GObject *
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
	GObject *object;
	NMAGConfConnectionPrivate *priv;

	object = G_OBJECT_CLASS (nma_gconf_connection_parent_class)->constructor (type, n_construct_params, construct_params);

	if (!object)
		return NULL;

	priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	if (!priv->client) {
		nm_warning ("GConfClient not provided.");
		g_object_unref (object);
		return NULL;
	}

	if (!priv->dir) {
		nm_warning ("GConf directory not provided.");
		g_object_unref (object);
		return NULL;
	}

	return object;
}

static void
dispose (GObject *object)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	if (priv->disposed)
		return;
	priv->disposed = TRUE;

	g_object_unref (priv->client);

	G_OBJECT_CLASS (nma_gconf_connection_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	g_free (priv->dir);

	G_OBJECT_CLASS (nma_gconf_connection_parent_class)->finalize (object);
}

static void
set_property (GObject *object, guint prop_id,
		    const GValue *value, GParamSpec *pspec)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_CLIENT:
		/* Construct only */
		priv->client = GCONF_CLIENT (g_value_dup_object (value));
		break;
	case PROP_DIR:
		/* Construct only */
		priv->dir = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
		    GValue *value, GParamSpec *pspec)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_CLIENT:
		g_value_set_object (value, priv->client);
		break;
	case PROP_DIR:
		g_value_set_string (value, priv->dir);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nma_gconf_connection_class_init (NMAGConfConnectionClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	NMExportedConnectionClass *ec_class = NM_EXPORTED_CONNECTION_CLASS (class);

	g_type_class_add_private (class, sizeof (NMAGConfConnectionPrivate));

	/* Virtual methods */
	object_class->constructor  = constructor;
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->dispose      = dispose;
	object_class->finalize     = finalize;

	ec_class->update = dbus_update;
	ec_class->delete = dbus_delete;
	ec_class->get_secrets = dbus_get_secrets;
	ec_class->get_settings = dbus_get_settings;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_CLIENT,
		 g_param_spec_object (NMA_GCONF_CONNECTION_CLIENT,
						  "GConfClient",
						  "GConfClient",
						  GCONF_TYPE_CLIENT,
						  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_DIR,
		 g_param_spec_string (NMA_GCONF_CONNECTION_DIR,
						  "GConf directory",
						  "GConf directory",
						  NULL,
						  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/* Signals */
	signals[NEW_SECRETS_REQUESTED] =
		g_signal_new ("new-secrets-requested",
				    G_OBJECT_CLASS_TYPE (object_class),
				    G_SIGNAL_RUN_FIRST,
				    G_STRUCT_OFFSET (NMAGConfConnectionClass, new_secrets_requested),
				    NULL, NULL,
				    nma_marshal_VOID__STRING_POINTER_BOOLEAN_POINTER_POINTER,
				    G_TYPE_NONE, 5,
				    G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN, G_TYPE_POINTER, G_TYPE_POINTER);
}
