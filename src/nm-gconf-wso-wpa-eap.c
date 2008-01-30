/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

/* NetworkManager -- Network link manager
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
 * (C) Copyright 2005 Red Hat, Inc.
 * (C) Copyright 2006 Novell, Inc.
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus.h>
#include <iwlib.h>
#include <gnome-keyring.h>

#include "applet.h"
#include "nm-gconf-wso.h"
#include "nm-gconf-wso-wpa-eap.h"
#include "nm-gconf-wso-private.h"
#include "dbus-helpers.h"
#include "gconf-helpers.h"
#include "nm-utils.h"

#define WPA_EAP_PREFIX "wpa_eap_"

#define NM_GCONF_WSO_WPA_EAP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_GCONF_WSO_WPA_EAP, NMGConfWSOWPA_EAPPrivate))

struct _NMGConfWSOWPA_EAPPrivate
{
	int		eap_method;
	int		key_type;
	int		phase2_type;
	int		wpa_version;
	int		key_mgmt;
	char *	identity;
	char *	anon_identity;
	char *	private_key_file;
	char *	private_key_passwd;
	char *	client_cert_file;
	char *	ca_cert_file;
};


NMGConfWSOWPA_EAP *
nm_gconf_wso_wpa_eap_new_deserialize_dbus (DBusMessageIter *iter, int we_cipher)
{
	NMGConfWSOWPA_EAP *	security = NULL;
	char *			identity = NULL;
	char *			passwd = NULL;
	char *			anon_identity = NULL;
	char *			private_key_passwd = NULL;
	char *			private_key_file = NULL;
	char *			client_cert_file = NULL;
	char *			ca_cert_file = NULL;
	int				wpa_version;
	int				eap_method;
	int				key_type;

	g_return_val_if_fail (we_cipher == NM_AUTH_TYPE_WPA_EAP, NULL);
	g_return_val_if_fail (iter != NULL, NULL);

	if (!nmu_security_deserialize_wpa_eap (iter, &eap_method, &key_type, &identity, &passwd,
								    &anon_identity, &private_key_passwd, &private_key_file,
								    &client_cert_file, &ca_cert_file, &wpa_version))
		goto out;

	/* Success, build up our security object */
	security = g_object_new (NM_TYPE_GCONF_WSO_WPA_EAP, NULL);
	nm_gconf_wso_set_we_cipher (NM_GCONF_WSO (security), we_cipher);
	nm_gconf_wso_set_key (NM_GCONF_WSO (security), passwd, strlen (passwd));
	security->priv->wpa_version = wpa_version;
	security->priv->key_type = key_type;
	security->priv->eap_method = NM_EAP_TO_EAP_METHOD (eap_method);
	security->priv->phase2_type = NM_EAP_TO_PHASE2_METHOD (eap_method);
	security->priv->key_mgmt = IW_AUTH_KEY_MGMT_802_1X;
	security->priv->identity = g_strdup (identity);
	if (private_key_passwd && strlen (private_key_passwd) > 0)
		security->priv->private_key_passwd = g_strdup (private_key_passwd);
	security->priv->anon_identity = g_strdup (anon_identity);
	security->priv->private_key_file = g_strdup (private_key_file);
	security->priv->client_cert_file = g_strdup (client_cert_file);
	security->priv->ca_cert_file = g_strdup (ca_cert_file);

out:
	return security;
}


NMGConfWSOWPA_EAP *
nm_gconf_wso_wpa_eap_new_deserialize_gconf (GConfClient *client, NMNetworkType type, const char *network, int we_cipher)
{
	NMGConfWSOWPA_EAP *	security = NULL;
	const char *gconf_prefix;
	char *			identity = NULL;
	char *			anon_identity = NULL;
	char *			private_key_file = NULL;
	char *			client_cert_file = NULL;
	char *			ca_cert_file = NULL;
	int				wpa_version = 0;
	int				eap_method = 0;
	int				key_type = 0;
	int				phase2_type = 0;
	int				key_mgmt = 0;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (network != NULL, NULL);
	g_return_val_if_fail ((we_cipher == NM_AUTH_TYPE_WPA_EAP), NULL);

	if (type == NETWORK_TYPE_ALLOWED)
		gconf_prefix = GCONF_PATH_WIRELESS_NETWORKS;
	else if (type == NETWORK_TYPE_WIRED)
		gconf_prefix = GCONF_PATH_WIRED_NETWORKS;
	else {
		nm_warning ("Invalid network type");
		return NULL;
	}

	nm_gconf_get_int_helper (client,
						gconf_prefix,
						WPA_EAP_PREFIX"eap_method",
						network,
						&eap_method);

	nm_gconf_get_int_helper (client,
						gconf_prefix,
						WPA_EAP_PREFIX"key_type",
						network,
						&key_type);

	nm_gconf_get_int_helper (client,
						gconf_prefix,
						WPA_EAP_PREFIX"phase2_type",
						network,
						&phase2_type);

	nm_gconf_get_int_helper (client,
						gconf_prefix,
						WPA_EAP_PREFIX"wpa_version",
						network,
						&wpa_version);

	nm_gconf_get_int_helper (client,
						gconf_prefix,
						WPA_EAP_PREFIX"key_mgt",
						network,
						&key_mgmt);

	nm_gconf_get_string_helper (client,
						   gconf_prefix,
						   WPA_EAP_PREFIX"identity",
						   network,
						   &identity);

	nm_gconf_get_string_helper (client,
						   gconf_prefix,
						   WPA_EAP_PREFIX"anon_identity",
						   network,
						   &anon_identity);

	nm_gconf_get_string_helper (client,
						   gconf_prefix,
						   WPA_EAP_PREFIX"private_key_file",
						   network,
						   &private_key_file);

	nm_gconf_get_string_helper (client,
						   gconf_prefix,
						   WPA_EAP_PREFIX"client_cert_file",
						   network,
						   &client_cert_file);

	nm_gconf_get_string_helper (client,
						   gconf_prefix,
						   WPA_EAP_PREFIX"ca_cert_file",
						   network,
						   &ca_cert_file);

	/* Success, build up our security object */
	security = g_object_new (NM_TYPE_GCONF_WSO_WPA_EAP, NULL);
	nm_gconf_wso_set_we_cipher (NM_GCONF_WSO (security), we_cipher);
	security->priv->wpa_version = wpa_version;
	security->priv->eap_method = eap_method;
	security->priv->key_type = key_type;
	security->priv->phase2_type = phase2_type;
	security->priv->key_mgmt = IW_AUTH_KEY_MGMT_802_1X;
	security->priv->identity = g_strdup (identity);
	security->priv->anon_identity = g_strdup (anon_identity);
	security->priv->private_key_file = g_strdup (private_key_file);
	security->priv->client_cert_file = g_strdup (client_cert_file);
	security->priv->ca_cert_file = g_strdup (ca_cert_file);

	g_free (identity);
	g_free (anon_identity);
	g_free (private_key_file);
	g_free (client_cert_file);
	g_free (ca_cert_file);

	return security;
}


static gboolean 
real_serialize_dbus (NMGConfWSO *instance, DBusMessageIter *iter)
{
	NMGConfWSOWPA_EAP * self = NM_GCONF_WSO_WPA_EAP (instance);

	if (!nmu_security_serialize_wpa_eap (iter,
			self->priv->eap_method | self->priv->phase2_type,
			self->priv->key_type,
			self->priv->identity ? : "",
			nm_gconf_wso_get_key (instance) ? : "",
			self->priv->anon_identity ? : "",
			self->priv->private_key_passwd ? : "",
			self->priv->private_key_file ? : "",
			self->priv->client_cert_file ? : "",
			self->priv->ca_cert_file ? : "",
			self->priv->wpa_version))
		return FALSE;
	return TRUE;
}

static gboolean 
real_serialize_gconf (NMGConfWSO *instance, GConfClient *client, NMNetworkType type, const char *network)
{
	NMGConfWSOWPA_EAP *	self = NM_GCONF_WSO_WPA_EAP (instance);
	const char *gconf_prefix;
	char *			key;

	if (type == NETWORK_TYPE_ALLOWED)
		gconf_prefix = GCONF_PATH_WIRELESS_NETWORKS;
	else if (type == NETWORK_TYPE_WIRED)
		gconf_prefix = GCONF_PATH_WIRED_NETWORKS;
	else {
		nm_warning ("Invalid network type");
		return FALSE;
	}

	key = g_strdup_printf ("%s/%s/%seap_method", gconf_prefix, network, WPA_EAP_PREFIX);
	gconf_client_set_int (client, key, self->priv->eap_method, NULL);
	g_free (key);

	key = g_strdup_printf ("%s/%s/%skey_type", gconf_prefix, network, WPA_EAP_PREFIX);
	gconf_client_set_int (client, key, self->priv->key_type, NULL);
	g_free (key);

	key = g_strdup_printf ("%s/%s/%sphase2_type", gconf_prefix, network, WPA_EAP_PREFIX);
	gconf_client_set_int (client, key, self->priv->phase2_type, NULL);
	g_free (key);

	key = g_strdup_printf ("%s/%s/%swpa_version", gconf_prefix, network, WPA_EAP_PREFIX);
	gconf_client_set_int (client, key, self->priv->wpa_version, NULL);
	g_free (key);

	key = g_strdup_printf ("%s/%s/%skey_mgt", gconf_prefix, network, WPA_EAP_PREFIX);
	gconf_client_set_int (client, key, self->priv->key_mgmt, NULL);
	g_free (key);

	if (self->priv->identity && strlen (self->priv->identity) > 0)
	{
		key = g_strdup_printf ("%s/%s/%sidentity", gconf_prefix, network, WPA_EAP_PREFIX);
		gconf_client_set_string (client, key, self->priv->identity, NULL);
		g_free (key);
	}

	if (self->priv->anon_identity && strlen (self->priv->anon_identity) > 0)
	{
		key = g_strdup_printf ("%s/%s/%sanon_identity", gconf_prefix, network, WPA_EAP_PREFIX);
		gconf_client_set_string (client, key, self->priv->anon_identity, NULL);
		g_free (key);
	}

	if (self->priv->private_key_file && strlen (self->priv->private_key_file) > 0)
	{
		key = g_strdup_printf ("%s/%s/%sprivate_key_file", gconf_prefix, network, WPA_EAP_PREFIX);
		gconf_client_set_string (client, key, self->priv->private_key_file, NULL);
		g_free (key);
	}

	if (self->priv->client_cert_file && strlen (self->priv->client_cert_file) > 0)
	{
		key = g_strdup_printf ("%s/%s/%sclient_cert_file", gconf_prefix, network, WPA_EAP_PREFIX);
		gconf_client_set_string (client, key, self->priv->client_cert_file, NULL);
		g_free (key);
	}

	if (self->priv->ca_cert_file && strlen (self->priv->ca_cert_file) > 0)
	{
		key = g_strdup_printf ("%s/%s/%sca_cert_file", gconf_prefix, network, WPA_EAP_PREFIX);
		gconf_client_set_string (client, key, self->priv->ca_cert_file, NULL);
		g_free (key);
	}

	return TRUE;
}

static gboolean
real_read_secrets (NMGConfWSO *instance,
                   const char *ssid)
{
	NMGConfWSOWPA_EAP *self = NM_GCONF_WSO_WPA_EAP (instance);
	GList *found_list = NULL;
	GnomeKeyringResult ret;
	GnomeKeyringFound *found;

	NM_GCONF_WSO_CLASS (g_type_class_peek (NM_TYPE_GCONF_WSO))->read_secrets_func (instance, ssid);

	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      "private-key-passwd",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      ssid,
	                                      NULL);
	if ((ret != GNOME_KEYRING_RESULT_OK) || (g_list_length (found_list) == 0)) {
		nm_info ("No keyring secrets found for network %s", ssid);
		return FALSE;
	}

	found = (GnomeKeyringFound *) found_list->data;
	self->priv->private_key_passwd = g_strdup (found->secret);
	gnome_keyring_found_list_free (found_list);

	return TRUE;
}

static void
real_write_secrets_cb (GnomeKeyringResult result,
				   guint32 val,
				   gpointer user_data)
{
	if (result != GNOME_KEYRING_RESULT_OK)
		nm_warning ("Error saving secret for wireless network '%s' in keyring: %d", (char *) user_data, result);
}

static void
real_write_secrets (NMGConfWSO *instance,
                    const char *ssid)
{
	NMGConfWSOWPA_EAP *self = NM_GCONF_WSO_WPA_EAP (instance);
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringAttribute attr;		
	char *display_name;

	NM_GCONF_WSO_CLASS (g_type_class_peek (NM_TYPE_GCONF_WSO))->write_secrets_func (instance, ssid);

	if (!self->priv->private_key_passwd)
		return;

	display_name = g_strdup_printf (_("Private key password for wireless network %s"), ssid);

	attributes = gnome_keyring_attribute_list_new ();
	attr.name = g_strdup ("private-key-passwd");
	attr.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attr.value.string = g_strdup (ssid);
	g_array_append_val (attributes, attr);

	gnome_keyring_item_create (NULL,
						  GNOME_KEYRING_ITEM_GENERIC_SECRET,
						  display_name,
						  attributes,
						  self->priv->private_key_passwd,
						  TRUE,
						  real_write_secrets_cb,
						  g_strdup (ssid),
						  g_free);

	g_free (display_name);
	gnome_keyring_attribute_list_free (attributes);
}

static void
nm_gconf_wso_wpa_eap_init (NMGConfWSOWPA_EAP *self)
{
	self->priv = NM_GCONF_WSO_WPA_EAP_GET_PRIVATE (self);
	self->priv->wpa_version = IW_AUTH_WPA_VERSION_WPA;
	self->priv->key_mgmt = IW_AUTH_KEY_MGMT_802_1X;
}


static void
nm_gconf_wso_wpa_eap_class_init (NMGConfWSOWPA_EAPClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMGConfWSOClass *par_class = NM_GCONF_WSO_CLASS (klass);

	par_class->serialize_dbus_func = real_serialize_dbus;
	par_class->serialize_gconf_func = real_serialize_gconf;
	par_class->read_secrets_func = real_read_secrets;
	par_class->write_secrets_func = real_write_secrets;

	g_type_class_add_private (object_class, sizeof (NMGConfWSOWPA_EAPPrivate));
}


GType
nm_gconf_wso_wpa_eap_get_type (void)
{
	static GType type = 0;
	if (type == 0) {
		static const GTypeInfo info = {
			sizeof (NMGConfWSOWPA_EAPClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) nm_gconf_wso_wpa_eap_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (NMGConfWSOWPA_EAP),
			0,		/* n_preallocs */
			(GInstanceInitFunc) nm_gconf_wso_wpa_eap_init,
			NULL		/* value_table */
		};
		type = g_type_register_static (NM_TYPE_GCONF_WSO,
								 "NMGConfWSOWPA_EAP",
								 &info, 0);
	}
	return type;
}
