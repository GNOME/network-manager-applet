/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
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
 * Copyright (C) 2011 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <string.h>
#include <gnome-keyring.h>
#include <dbus/dbus-glib.h>
#include <nm-setting-connection.h>
#include <nm-setting-8021x.h>
#include <nm-setting-vpn.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>
#include <nm-setting-wired.h>
#include <nm-setting-pppoe.h>

#include "applet-agent.h"
#include "utils.h"
#include "nma-marshal.h"

G_DEFINE_TYPE (AppletAgent, applet_agent, NM_TYPE_SECRET_AGENT);

#define APPLET_AGENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), APPLET_TYPE_AGENT, AppletAgentPrivate))

typedef struct {
	GHashTable *requests;

	gboolean disposed;
} AppletAgentPrivate;

enum {
	GET_SECRETS,
	CANCEL_SECRETS,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };


/*******************************************************/

#define DBUS_TYPE_G_MAP_OF_STRING (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_STRING))

typedef struct {
	guint id;

	NMSecretAgent *agent;
	NMConnection *connection;
	char *path;
	char *setting_name;
	char **hints;
	guint32 flags;
	NMSecretAgentGetSecretsFunc get_callback;
	NMSecretAgentSaveSecretsFunc save_callback;
	NMSecretAgentDeleteSecretsFunc delete_callback;
	gpointer callback_data;

	GSList *keyring_calls;
	gboolean canceled;
} Request;

static Request *
request_new (NMSecretAgent *agent,
             NMConnection *connection,
             const char *connection_path,
             const char *setting_name,
             const char **hints,
             guint32 flags,
             NMSecretAgentGetSecretsFunc get_callback,
             NMSecretAgentSaveSecretsFunc save_callback,
             NMSecretAgentDeleteSecretsFunc delete_callback,
             gpointer callback_data)
{
	static guint32 counter = 1;
	Request *r;

	r = g_slice_new0 (Request);
	r->id = counter++;
	r->agent = agent;
	r->connection = g_object_ref (connection);
	r->path = g_strdup (connection_path);
	r->setting_name = g_strdup (setting_name);
	if (hints)
		r->hints = g_strdupv ((gchar **) hints);
	r->flags = flags;
	r->get_callback = get_callback;
	r->save_callback = save_callback;
	r->delete_callback = delete_callback;
	r->callback_data = callback_data;
	return r;
}

static void
request_free (Request *r)
{
	if (r->canceled == FALSE)
		g_hash_table_remove (APPLET_AGENT_GET_PRIVATE (r->agent)->requests, GUINT_TO_POINTER (r->id));

	/* By the time the request is freed, all keyring calls should be completed */
	g_warn_if_fail (r->keyring_calls == NULL);

	g_object_unref (r->connection);
	g_free (r->path);
	g_free (r->setting_name);
	g_strfreev (r->hints);
	memset (r, 0, sizeof (*r));
	g_slice_free (Request, r);
}


/*************************************************************/

/* The keyring doesn't pass the call ID to the callback for the
 * operation which the call ID represents, so we have to track
 * it with a small structure.  Ugh.
 */

typedef struct {
	Request *r;
	gpointer keyring_id;
} KeyringCall;

static inline KeyringCall *
keyring_call_new (Request *r)
{
	KeyringCall *call;

	call = g_malloc0 (sizeof (KeyringCall));
	call->r = r;
	return call;
}

static inline void
keyring_call_free (gpointer data)
{
	KeyringCall *call = data;

	memset (call, 0, sizeof (*call));
	g_free (call);
}

/*******************************************************/

static void
get_secrets_cb (AppletAgent *self,
                GHashTable *secrets,
                GError *error,
                gpointer user_data)
{
	Request *r = user_data;

	if (r->canceled == FALSE)
		r->get_callback (NM_SECRET_AGENT (r->agent), r->connection, error ? NULL : secrets, error, r->callback_data);
	request_free (r);
}

static void
ask_for_secrets (Request *r)
{
	/* Ask the applet to get some secrets for us */
	g_signal_emit (r->agent,
	               signals[GET_SECRETS],
	               0,
	               GUINT_TO_POINTER (r->id),
	               r->connection,
	               r->setting_name,
	               r->hints,
	               r->flags,
	               get_secrets_cb,
	               r);
}

static void
check_always_ask_cb (NMSetting *setting,
                     const char *key,
                     const GValue *value,
                     GParamFlags flags,
                     gpointer user_data)
{
	gboolean *always_ask = user_data;
	NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;

	if (flags & NM_SETTING_PARAM_SECRET) {
		if (nm_setting_get_secret_flags (setting, key, &secret_flags, NULL)) {
			if (secret_flags & NM_SETTING_SECRET_FLAG_NOT_SAVED)
				*always_ask = TRUE;
		}
	}
}

static gboolean
has_always_ask (NMSetting *setting)
{
	gboolean always_ask = FALSE;

	nm_setting_enumerate_values (setting, check_always_ask_cb, &always_ask);
	return always_ask;
}

static gboolean
is_connection_always_ask (NMConnection *connection)
{
	NMSettingConnection *s_con;
	const char *ctype;
	NMSetting *setting;

	/* For the given connection type, check if the secrets for that connection
	 * are always-ask or not.
	 */
	s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);
	ctype = nm_setting_connection_get_connection_type (s_con);

	setting = nm_connection_get_setting_by_name (connection, ctype);
	g_return_val_if_fail (setting != NULL, FALSE);

	if (has_always_ask (setting))
		return TRUE;

	/* Try type-specific settings too; be a bit paranoid and only consider
	 * secrets from settings relevant to the connection type.
	 */
	if (NM_IS_SETTING_WIRELESS (setting)) {
		setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY);
		if (setting && has_always_ask (setting))
			return TRUE;
		setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
		if (setting && has_always_ask (setting))
			return TRUE;
	} else if (NM_IS_SETTING_WIRED (setting)) {
		setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_PPPOE);
		if (setting && has_always_ask (setting))
			return TRUE;
		setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
		if (setting && has_always_ask (setting))
			return TRUE;
	}

	return FALSE;
}

static GValue *
string_to_gvalue (const char *str)
{
	GValue *val;

	val = g_slice_new0 (GValue);
	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, str);
	return val;
}

static void
destroy_gvalue (gpointer data)
{
	g_value_unset ((GValue *) data);
	g_slice_free (GValue, data);
}

static void
keyring_find_secrets_cb (GnomeKeyringResult result,
                         GList *list,
                         gpointer user_data)
{
	KeyringCall *call = user_data;
	Request *r = call->r;
	GError *error = NULL;
	NMSettingConnection *s_con;
	const char *connection_id = NULL;
	GHashTable *secrets = NULL, *settings = NULL;
	GList *iter;
	gboolean hint_found = FALSE, ask = FALSE;

	r->keyring_calls = g_slist_remove (r->keyring_calls, call);
	if (r->canceled) {
		/* Callback already called by NM or dispose */
		request_free (r);
		return;
	}

	s_con = (NMSettingConnection *) nm_connection_get_setting (r->connection, NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);
	connection_id = nm_setting_connection_get_id (s_con);

	if (result == GNOME_KEYRING_RESULT_CANCELLED) {
		error = g_error_new_literal (NM_SECRET_AGENT_ERROR,
		                             NM_SECRET_AGENT_ERROR_USER_CANCELED,
		                             "The secrets request was canceled by the user");
		goto done;
	} else if (   result != GNOME_KEYRING_RESULT_OK
	           && result != GNOME_KEYRING_RESULT_NO_MATCH) {
		error = g_error_new (NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "%s.%d - failed to read secrets from keyring (result %d)",
		                     __FILE__, __LINE__, result);
		goto done;
	}

	/* Only ask if we're allowed to, ie if flags != NM_SECRET_AGENT_GET_SECRETS_FLAG_NONE */
	if (r->flags && g_list_length (list) == 0) {
		g_message ("No keyring secrets found for %s/%s; asking user.", connection_id, r->setting_name);
		ask_for_secrets (r);
		return;
	}

	secrets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, destroy_gvalue);

	/* Extract the secrets from the list of matching keyring items */
	for (iter = list; iter != NULL; iter = g_list_next (iter)) {
		GnomeKeyringFound *found = iter->data;
		GnomeKeyringAttribute *attr;
		const char *key_name = NULL;
		int i;

		for (i = 0; i < found->attributes->len; i++) {
			attr = &(gnome_keyring_attribute_list_index (found->attributes, i));
			if (   (strcmp (attr->name, KEYRING_SK_TAG) == 0)
			    && (attr->type == GNOME_KEYRING_ATTRIBUTE_TYPE_STRING)) {

				key_name = attr->value.string;
				g_hash_table_insert (secrets, g_strdup (key_name), string_to_gvalue (found->secret));

				/* See if this property matches a given hint */
				if (r->hints && r->hints[0]) {
					if (!g_strcmp0 (r->hints[0], key_name) || !g_strcmp0 (r->hints[1], key_name))
						hint_found = TRUE;
				}
				break;
			}
		}
	}

	/* If there were hints, and none of the hints were returned by the keyring,
	 * get some new secrets.
	 */
	if (r->flags) {
		if (r->hints && r->hints[0] && !hint_found)
			ask = TRUE;
		else if (r->flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_REQUEST_NEW) {
			g_message ("New secrets for %s/%s requested; ask the user", connection_id, r->setting_name);
			ask = TRUE;
		} else if (   (r->flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION)
			       && is_connection_always_ask (r->connection))
			ask = TRUE;
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_hash_table_destroy);
	g_hash_table_insert (settings, g_strdup (r->setting_name), secrets);

done:
	if (ask) {
		GHashTableIter hash_iter;
		const char *setting_name;
		GHashTable *setting_hash;

		/* Stuff all the found secrets into the connection for the UI to use */
		g_hash_table_iter_init (&hash_iter, settings);
		while (g_hash_table_iter_next (&hash_iter,
		                               (gpointer *) &setting_name,
		                               (gpointer *) &setting_hash)) {
			nm_connection_update_secrets (r->connection,
				                          setting_name,
				                          setting_hash,
				                          NULL);
		}

		ask_for_secrets (r);
	} else {
		/* Otherwise send the secrets back to NetworkManager */
		r->get_callback (NM_SECRET_AGENT (r->agent), r->connection, error ? NULL : settings, error, r->callback_data);
		request_free (r);
	}

	if (settings)
		g_hash_table_destroy (settings);
	g_clear_error (&error);
}

static void
get_secrets (NMSecretAgent *agent,
             NMConnection *connection,
             const char *connection_path,
             const char *setting_name,
             const char **hints,
             guint32 flags,
             NMSecretAgentGetSecretsFunc callback,
             gpointer callback_data)
{
	AppletAgentPrivate *priv = APPLET_AGENT_GET_PRIVATE (agent);
	Request *r;
	GError *error = NULL;
	NMSettingConnection *s_con;
	NMSetting *setting;
	const char *id;
	const char *ctype;
	KeyringCall *call;

	setting = nm_connection_get_setting_by_name (connection, setting_name);
	if (!setting) {
		error = g_error_new (NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INVALID_CONNECTION,
		                     "%s.%d - Connection didn't have requested setting '%s'.",
		                     __FILE__, __LINE__, setting_name);
		callback (agent, connection, NULL, error, callback_data);
		g_error_free (error);
		return;
	}

	s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);
	id = nm_setting_connection_get_id (s_con);
	ctype = nm_setting_connection_get_connection_type (s_con);

	if (!s_con || !id || !strlen (id) || !ctype) {
		error = g_error_new (NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INVALID_CONNECTION,
		                     "%s.%d - Connection didn't have required '"
		                     NM_SETTING_CONNECTION_SETTING_NAME
		                     "' setting , or the connection name was invalid.",
		                     __FILE__, __LINE__);
		callback (agent, connection, NULL, error, callback_data);
		g_error_free (error);
		return;
	}

	/* Track the secrets request */
	r = request_new (agent, connection, connection_path, setting_name, hints, flags, callback, NULL, NULL, callback_data);
	g_hash_table_insert (priv->requests, GUINT_TO_POINTER (r->id), r);

	/* VPN passwords are handled by the VPN plugin's auth dialog */
	if (!strcmp (ctype, NM_SETTING_VPN_SETTING_NAME)) {
		ask_for_secrets (r);
		return;
	}

	/* For everything else we scrape the keyring for secrets first, and ask
	 * later if required.
	 */
	call = keyring_call_new (r);
	call->keyring_id = gnome_keyring_find_itemsv (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                              keyring_find_secrets_cb,
	                                              call,
	                                              keyring_call_free,
	                                              KEYRING_UUID_TAG,
	                                              GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                              nm_setting_connection_get_uuid (s_con),
	                                              KEYRING_SN_TAG,
	                                              GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                              setting_name,
	                                              NULL);
	r->keyring_calls = g_slist_append (r->keyring_calls, call);
}

/*******************************************************/

static void
cancel_get_secrets (NMSecretAgent *agent,
                    const char *connection_path,
                    const char *setting_name)
{
	AppletAgentPrivate *priv = APPLET_AGENT_GET_PRIVATE (agent);
	GHashTableIter iter;
	Request *r;
	GError *error;

	error = g_error_new_literal (NM_SECRET_AGENT_ERROR,
	                             NM_SECRET_AGENT_ERROR_AGENT_CANCELED,
	                             "Canceled by NetworkManager");

	g_hash_table_iter_init (&iter, priv->requests);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer) &r)) {
		/* Only care about GetSecrets requests here */
		if (r->get_callback == NULL)
			continue;

		/* Cancel any matching GetSecrets call */
		if (   g_strcmp0 (r->path, connection_path) == 0
		    && g_strcmp0 (r->setting_name, setting_name) == 0) {
			GSList *kiter;

			r->canceled = TRUE;

			/* cancel outstanding keyring operations */
			for (kiter = r->keyring_calls; kiter; kiter = g_slist_next (kiter)) {
				KeyringCall *call = kiter->data;

				gnome_keyring_cancel_request (call->keyring_id);
			}

			r->get_callback (NM_SECRET_AGENT (r->agent), r->connection, NULL, error, r->callback_data);
			g_hash_table_remove (priv->requests, GUINT_TO_POINTER (r->id));
			g_signal_emit (r->agent, signals[CANCEL_SECRETS], 0, GUINT_TO_POINTER (r->id));
		}
	}

	g_error_free (error);
}

/*******************************************************/

static void
save_request_try_complete (Request *r, KeyringCall *call)
{
	/* Only call the SaveSecrets callback and free the request when all the
	 * secrets have been saved to the keyring.
	 */
	if (call)
		r->keyring_calls = g_slist_remove (r->keyring_calls, call);

	if (g_slist_length (r->keyring_calls) == 0) {
		if (r->canceled == FALSE)
			r->save_callback (NM_SECRET_AGENT (r->agent), r->connection, NULL, r->callback_data);
		request_free (r);
	}
}

static void
save_secret_cb (GnomeKeyringResult result, guint val, gpointer user_data)
{
	KeyringCall *call = user_data;

	save_request_try_complete (call->r, call);
}

static void
save_one_secret (Request *r,
                 NMSetting *setting,
                 const char *key,
                 const char *secret,
                 const char *display_name)
{
	GnomeKeyringAttributeList *attrs;
	KeyringCall *call;
	char *alt_display_name = NULL;
	const char *setting_name;
	NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;

	/* Don't system-owned or always-ask secrets */
	if (!nm_setting_get_secret_flags (setting, key, &secret_flags, NULL))
		return;
	if (secret_flags != NM_SETTING_SECRET_FLAG_AGENT_OWNED)
		return;

	setting_name = nm_setting_get_name (setting);
	g_assert (setting_name);

	attrs = utils_create_keyring_add_attr_list (r->connection, NULL, NULL,
	                                            setting_name,
	                                            key,
	                                            display_name ? NULL : &alt_display_name);
	g_assert (attrs);
	call = keyring_call_new (r);
	call->keyring_id = gnome_keyring_item_create (NULL,
	                                              GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                              display_name ? display_name : alt_display_name,
	                                              attrs,
	                                              secret,
	                                              TRUE,
	                                              save_secret_cb,
	                                              call,
	                                              keyring_call_free);
	r->keyring_calls = g_slist_append (r->keyring_calls, call);

	gnome_keyring_attribute_list_free (attrs);
	g_free (alt_display_name);
}

static void
vpn_secret_iter_cb (const char *key, const char *secret, gpointer user_data)
{
	Request *r = user_data;
	NMSetting *setting;
	const char *service_name, *id;
	char *display_name;

	if (secret && strlen (secret)) {
		setting = nm_connection_get_setting (r->connection, NM_TYPE_SETTING_VPN);
		g_assert (setting);
		service_name = nm_setting_vpn_get_service_type (NM_SETTING_VPN (setting));
		g_assert (service_name);
		id = nm_connection_get_id (r->connection);
		g_assert (id);

		display_name = g_strdup_printf ("VPN %s secret for %s/%s/" NM_SETTING_VPN_SETTING_NAME,
		                                key,
		                                id,
		                                service_name);
		save_one_secret (r, setting, key, secret, display_name);
		g_free (display_name);
	}
}

static void
write_one_secret_to_keyring (NMSetting *setting,
                             const char *key,
                             const GValue *value,
                             GParamFlags flags,
                             gpointer user_data)
{
	Request *r = user_data;
	GType type = G_VALUE_TYPE (value);
	const char *secret;

	/* Non-secrets obviously don't get saved in the keyring */
	if (!(flags & NM_SETTING_PARAM_SECRET))
		return;

	if (NM_IS_SETTING_VPN (setting) && (g_strcmp0 (key, NM_SETTING_VPN_SECRETS) == 0)) {
		g_return_if_fail (type == DBUS_TYPE_G_MAP_OF_STRING);

		/* Process VPN secrets specially since it's a hash of secrets, not just one */
		nm_setting_vpn_foreach_secret (NM_SETTING_VPN (setting),
		                               vpn_secret_iter_cb,
		                               r);
	} else {
		g_return_if_fail (type == G_TYPE_STRING);
		secret = g_value_get_string (value);
		if (secret && strlen (secret))
			save_one_secret (r, setting, key, secret, NULL);
	}
}

static void
save_delete_cb (NMSecretAgent *agent,
                NMConnection *connection,
                GError *error,
                gpointer user_data)
{
	Request *r = user_data;

	/* Ignore errors; now save all new secrets */
	nm_connection_for_each_setting_value (connection, write_one_secret_to_keyring, r);

	/* If no secrets actually got saved there may be nothing to do so
	 * try to complete the request here.  If there were secrets to save the
	 * request will get completed when those keyring calls return.
	 */
	save_request_try_complete (r, NULL);
}

static void
save_secrets (NMSecretAgent *agent,
              NMConnection *connection,
              const char *connection_path,
              NMSecretAgentSaveSecretsFunc callback,
              gpointer callback_data)
{
	AppletAgentPrivate *priv = APPLET_AGENT_GET_PRIVATE (agent);
	Request *r;

	r = request_new (agent, connection, connection_path, NULL, NULL, FALSE, NULL, callback, NULL, callback_data);
	g_hash_table_insert (priv->requests, GUINT_TO_POINTER (r->id), r);

	/* First delete any existing items in the keyring */
	nm_secret_agent_delete_secrets (agent, connection, save_delete_cb, r);
}

/*******************************************************/

static void
keyring_delete_cb (GnomeKeyringResult result, gpointer user_data)
{
	/* Ignored */
}

static void
delete_find_items_cb (GnomeKeyringResult result, GList *list, gpointer user_data)
{
	KeyringCall *call = user_data;
	Request *r = call->r;
	GList *iter;
	GError *error = NULL;

	r->keyring_calls = g_slist_remove (r->keyring_calls, call);
	if (r->canceled) {
		/* Callback already called by NM or dispose */
		request_free (r);
		return;
	}

	if ((result == GNOME_KEYRING_RESULT_OK) || (result == GNOME_KEYRING_RESULT_NO_MATCH)) {
		for (iter = list; iter != NULL; iter = g_list_next (iter)) {
			GnomeKeyringFound *found = (GnomeKeyringFound *) iter->data;

			gnome_keyring_item_delete (found->keyring, found->item_id, keyring_delete_cb, NULL, NULL);
		}
	} else {
		error = g_error_new (NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		                     "The request could not be completed.  Keyring result: %d",
		                     result);
	}

	r->delete_callback (r->agent, r->connection, error, r->callback_data);
	request_free (r);
}

static void
delete_secrets (NMSecretAgent *agent,
                NMConnection *connection,
                const char *connection_path,
                NMSecretAgentDeleteSecretsFunc callback,
                gpointer callback_data)
{
	AppletAgentPrivate *priv = APPLET_AGENT_GET_PRIVATE (agent);
	Request *r;
	NMSettingConnection *s_con;
	const char *uuid;
	KeyringCall *call;

	r = request_new (agent, connection, connection_path, NULL, NULL, FALSE, NULL, NULL, callback, callback_data);
	g_hash_table_insert (priv->requests, GUINT_TO_POINTER (r->id), r);

	s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);
	uuid = nm_setting_connection_get_uuid (s_con);
	g_assert (uuid);

	call = keyring_call_new (r);
	call->keyring_id = gnome_keyring_find_itemsv (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                              delete_find_items_cb,
	                                              call,
	                                              keyring_call_free,
	                                              KEYRING_UUID_TAG,
	                                              GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                              uuid,
	                                              NULL);
	r->keyring_calls = g_slist_append (r->keyring_calls, call);
}

/*******************************************************/

AppletAgent *
applet_agent_new (void)
{
	return (AppletAgent *) g_object_new (APPLET_TYPE_AGENT,
	                                     NM_SECRET_AGENT_IDENTIFIER, "org.freedesktop.nm-applet",
	                                     NULL);
}

static void
agent_registration_result_cb (NMSecretAgent *agent, GError *error, gpointer user_data)
{
	if (error)
		g_warning ("Failed to register as an agent: (%d) %s", error->code, error->message);
}

static void
applet_agent_init (AppletAgent *self)
{
	AppletAgentPrivate *priv = APPLET_AGENT_GET_PRIVATE (self);

	priv->requests = g_hash_table_new (g_direct_hash, g_direct_equal);

	g_signal_connect (self, NM_SECRET_AGENT_REGISTRATION_RESULT,
	                  G_CALLBACK (agent_registration_result_cb), NULL);
}

static void
dispose (GObject *object)
{
	AppletAgent *self = APPLET_AGENT (object);
	AppletAgentPrivate *priv = APPLET_AGENT_GET_PRIVATE (self);

	if (!priv->disposed) {
		GHashTableIter iter;
		Request *r;
		GSList *kiter;

		/* Mark any outstanding requests as canceled */
		g_hash_table_iter_init (&iter, priv->requests);
		while (g_hash_table_iter_next (&iter, NULL, (gpointer) &r)) {
			r->canceled = TRUE;

			/* cancel the request's outstanding keyring operations */
			for (kiter = r->keyring_calls; kiter; kiter = g_slist_next (kiter)) {
				KeyringCall *call = kiter->data;

				gnome_keyring_cancel_request (call->keyring_id);
			}
		}

		g_hash_table_destroy (priv->requests);
		priv->disposed = TRUE;
	}

	G_OBJECT_CLASS (applet_agent_parent_class)->dispose (object);
}

static void
applet_agent_class_init (AppletAgentClass *agent_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (agent_class);
	NMSecretAgentClass *parent_class = NM_SECRET_AGENT_CLASS (agent_class);

	g_type_class_add_private (agent_class, sizeof (AppletAgentPrivate));

	/* virtual methods */
	object_class->dispose = dispose;
	parent_class->get_secrets = get_secrets;
	parent_class->cancel_get_secrets = cancel_get_secrets;
	parent_class->save_secrets = save_secrets;
	parent_class->delete_secrets = delete_secrets;

	/* Signals */
	signals[GET_SECRETS] =
		g_signal_new (APPLET_AGENT_GET_SECRETS,
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AppletAgentClass, get_secrets),
		              NULL, NULL,
		              nma_marshal_VOID__POINTER_POINTER_STRING_POINTER_UINT_POINTER_POINTER,
		              G_TYPE_NONE, 7,
		              G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER);

	signals[CANCEL_SECRETS] =
		g_signal_new (APPLET_AGENT_CANCEL_SECRETS,
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AppletAgentClass, cancel_secrets),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__POINTER,
		              G_TYPE_NONE, 1, G_TYPE_POINTER);
}

