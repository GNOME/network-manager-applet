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
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus.h>
#include <gconf/gconf-client.h>
#include <gnome-keyring.h>
#include <iwlib.h>

#include "applet.h"
#include "nm-gconf-wso.h"
#include "nm-gconf-wso-private.h"
#include "nm-gconf-wso-wep.h"
#include "nm-gconf-wso-wpa-eap.h"
#include "nm-gconf-wso-wpa-psk.h"
#include "nm-gconf-wso-leap.h"
#include "gconf-helpers.h"
#include "wireless-security-option.h"
#include "nm-utils.h"


#define NM_GCONF_WSO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_GCONF_WSO, NMGConfWSOPrivate))

struct _NMGConfWSOPrivate
{
	int		we_cipher;
	char *	key;

	gboolean	dispose_has_run;
};

static NMGConfWSO *
nm_gconf_wso_new (int we_cipher)
{
	NMGConfWSO * security;

	security = g_object_new (NM_TYPE_GCONF_WSO, NULL);
	security->priv->we_cipher = we_cipher;
	security->priv->key = NULL;
	return security;
}


NMGConfWSO *
nm_gconf_wso_new_deserialize_dbus (DBusMessageIter *iter)
{
	NMGConfWSO * security = NULL;
	int we_cipher;

	g_return_val_if_fail (iter != NULL, NULL);
	/* We require the WE cipher (an INT32) first */
	g_return_val_if_fail (dbus_message_iter_get_arg_type (iter) == DBUS_TYPE_INT32, NULL);

	/* Get and validate WE cipher */
	dbus_message_iter_get_basic (iter, &we_cipher);

	if (we_cipher == IW_AUTH_CIPHER_NONE)
		security = nm_gconf_wso_new (we_cipher);
	else
	{
		/* Advance to start of cipher-dependent options */
		if (!dbus_message_iter_next (iter))
			goto out;

		switch (we_cipher)
		{
			case IW_AUTH_CIPHER_WEP40:
			case IW_AUTH_CIPHER_WEP104:
				security = NM_GCONF_WSO (nm_gconf_wso_wep_new_deserialize_dbus (iter, we_cipher));
				break;

			case NM_AUTH_TYPE_WPA_PSK_AUTO:
			case IW_AUTH_CIPHER_TKIP:
			case IW_AUTH_CIPHER_CCMP:
				security = NM_GCONF_WSO (nm_gconf_wso_wpa_psk_new_deserialize_dbus (iter, we_cipher));
				break;

			case NM_AUTH_TYPE_WPA_EAP:
				security = NM_GCONF_WSO (nm_gconf_wso_wpa_eap_new_deserialize_dbus (iter, we_cipher));
				break;

			case NM_AUTH_TYPE_LEAP:
				security = NM_GCONF_WSO (nm_gconf_wso_leap_new_deserialize_dbus (iter, we_cipher));
				break;

			default:
				break;
		}
	}

out:
	return security;
}

NMGConfWSO *
nm_gconf_wso_new_deserialize_gconf (GConfClient *client,
							 NMNetworkType type,
                                    const char *network)
{
	NMGConfWSO * security = NULL;
	int we_cipher;
	const char *gconf_prefix;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (network != NULL, NULL);

	if (type == NETWORK_TYPE_ALLOWED)
		gconf_prefix = GCONF_PATH_WIRELESS_NETWORKS;
	else if (type == NETWORK_TYPE_WIRED)
		gconf_prefix = GCONF_PATH_WIRED_NETWORKS;
	else {
		nm_warning ("Invalid network type");
		return FALSE;
	}

	if (!nm_gconf_get_int_helper (client,
							gconf_prefix,
							"we_cipher",
							network,
							&we_cipher))
		goto out;

	if (we_cipher == IW_AUTH_CIPHER_NONE)
		security = nm_gconf_wso_new (we_cipher);
	else
	{
		switch (we_cipher)
		{
			case IW_AUTH_CIPHER_WEP40:
			case IW_AUTH_CIPHER_WEP104:
				security = NM_GCONF_WSO (nm_gconf_wso_wep_new_deserialize_gconf (client, type, network, we_cipher));
				break;

			case NM_AUTH_TYPE_WPA_PSK_AUTO:
			case IW_AUTH_CIPHER_TKIP:
			case IW_AUTH_CIPHER_CCMP:
				security = NM_GCONF_WSO (nm_gconf_wso_wpa_psk_new_deserialize_gconf (client, type, network, we_cipher));
				break;

			case NM_AUTH_TYPE_WPA_EAP:
				security = NM_GCONF_WSO (nm_gconf_wso_wpa_eap_new_deserialize_gconf (client, type, network, we_cipher));
				break;

			case NM_AUTH_TYPE_LEAP:
				security = NM_GCONF_WSO (nm_gconf_wso_leap_new_deserialize_gconf (client, type, network, we_cipher));
				break;

			default:
				break;
		}
	}

out:
	return security;
}

/* HACK: to convert the WirelessSecurityOption -> NMGConfWSO,
 * we serialize the WSO to a dbus message then deserialize
 * it into an NMGConfWSO.
 */
NMGConfWSO *
nm_gconf_wso_new_from_wso (WirelessSecurityOption *opt,
                           const char *ssid)
{
	DBusMessage *		message;
	DBusMessageIter	iter;
	NMGConfWSO *		gconf_wso = NULL;

	g_return_val_if_fail (opt != NULL, NULL);
	g_return_val_if_fail (ssid != NULL, NULL);

	message = dbus_message_new_method_call (NMI_DBUS_SERVICE, NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "foobar");
	if (!wso_append_dbus_params (opt, ssid, message))
		goto out;

	dbus_message_iter_init (message, &iter);
	gconf_wso = nm_gconf_wso_new_deserialize_dbus (&iter);

out:
	dbus_message_unref (message);
	return gconf_wso;
}

/* HACK: to convert the NMGConfWSO -> WirelessSecurityOption,
 * we serialize the NMGConfWSO to a dbus message and then let
 * the WSO to deserialize it.
 */
gboolean
nm_gconf_wso_populate_wso (NMGConfWSO *self, WirelessSecurityOption *opt)
{
	DBusMessage *message;
	DBusMessageIter iter;
	gboolean success = FALSE;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (opt != NULL, FALSE);

	message = dbus_message_new_method_call (NMI_DBUS_SERVICE, NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "foobar");
	dbus_message_iter_init_append (message, &iter);

	if (nm_gconf_wso_serialize_dbus (self, &iter)) {
		dbus_message_iter_init (message, &iter);

		success = wso_populate_from_dbus_params (opt, &iter);
	}

	dbus_message_unref (message);

	return success;
}

void
nm_gconf_wso_set_we_cipher (NMGConfWSO *self,
                            int we_cipher)
{
	g_return_if_fail (self != NULL);

	/* Ensure the cipher is valid */
	g_return_if_fail (
		   (we_cipher == NM_AUTH_TYPE_WPA_PSK_AUTO)
		|| (we_cipher == NM_AUTH_TYPE_WPA_EAP)
		|| (we_cipher == NM_AUTH_TYPE_LEAP)
		|| (we_cipher == IW_AUTH_CIPHER_NONE)
		|| (we_cipher == IW_AUTH_CIPHER_WEP40)
		|| (we_cipher == IW_AUTH_CIPHER_WEP104)
		|| (we_cipher == IW_AUTH_CIPHER_TKIP)
		|| (we_cipher == IW_AUTH_CIPHER_CCMP));

	self->priv->we_cipher = we_cipher;
}

void
nm_gconf_wso_set_key (NMGConfWSO *self,
                      const char *key,
                      int key_len)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (key_len > 0);

	if (self->priv->key)
		g_free (self->priv->key);
	self->priv->key = g_malloc0 (key_len + 1);
	memcpy (self->priv->key, key, key_len);
}

static gboolean 
real_serialize_dbus (NMGConfWSO *self,
                     DBusMessageIter *iter)
{
	/* Nothing to do */
	return TRUE;
}

static int 
real_serialize_gconf (NMGConfWSO *self,
                      GConfClient *client,
				  NMNetworkType type,
                      const char *network)
{
	/* Nothing to do */
	return TRUE;
}

int
nm_gconf_wso_get_we_cipher (NMGConfWSO *self)
{
	g_return_val_if_fail (self != NULL, -1);

	return self->priv->we_cipher;
}

const char *
nm_gconf_wso_get_key (NMGConfWSO *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return self->priv->key;
}

gboolean
nm_gconf_wso_serialize_dbus (NMGConfWSO *self,
                             DBusMessageIter *iter)
{
	dbus_int32_t	dbus_we_cipher;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (iter != NULL, FALSE);

	if (self->priv->dispose_has_run)
		return FALSE;

	/* First arg: WE cipher (INT32) */
	dbus_we_cipher = (dbus_int32_t) self->priv->we_cipher;
	dbus_message_iter_append_basic (iter, DBUS_TYPE_INT32, &dbus_we_cipher);

	return NM_GCONF_WSO_GET_CLASS (self)->serialize_dbus_func (self, iter);
}

gboolean
nm_gconf_wso_serialize_gconf (NMGConfWSO *self,
                              GConfClient *client,
						NMNetworkType type,
                              const char *network)
{
	char *		key;
	const char *gconf_prefix;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (network != NULL, FALSE);

	if (self->priv->dispose_has_run)
		return FALSE;

	if (type == NETWORK_TYPE_ALLOWED)
		gconf_prefix = GCONF_PATH_WIRELESS_NETWORKS;
	else if (type == NETWORK_TYPE_WIRED)
		gconf_prefix = GCONF_PATH_WIRED_NETWORKS;
	else {
		nm_warning ("Invalid network type");
		return FALSE;
	}

	key = g_strdup_printf ("%s/%s/we_cipher", gconf_prefix, network);
	gconf_client_set_int (client, key, self->priv->we_cipher, NULL);
	g_free (key);

	/* Encryption key doesn't get serialized since its stored in the keyring */

	return NM_GCONF_WSO_GET_CLASS (self)->serialize_gconf_func (self, client, type, network);
}

gboolean
nm_gconf_wso_read_secrets (NMGConfWSO *self,
                           const char *ssid)
{
	return NM_GCONF_WSO_GET_CLASS (self)->read_secrets_func (self, ssid);
}

static gboolean
real_read_secrets (NMGConfWSO *self,
                   const char *ssid)
{
	GList *found_list = NULL;
	GnomeKeyringResult ret;
	GnomeKeyringFound *found;

	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      "essid",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      ssid,
	                                      NULL);
	if ((ret != GNOME_KEYRING_RESULT_OK) || (g_list_length (found_list) == 0)) {
		nm_info ("No keyring secrets found for network %s", ssid);
		return FALSE;
	}

	found = (GnomeKeyringFound *) found_list->data;
	nm_gconf_wso_set_key (self, found->secret, strlen (found->secret));
	gnome_keyring_found_list_free (found_list);

	return TRUE;
}

void
nm_gconf_wso_write_secrets (NMGConfWSO *self,
                            const char *ssid)
{
	NM_GCONF_WSO_GET_CLASS (self)->write_secrets_func (self, ssid);
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
real_write_secrets (NMGConfWSO *self,
                    const char *ssid)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringAttribute attr;		
	char *display_name;

	if (nm_gconf_wso_get_we_cipher (self) == IW_AUTH_CIPHER_NONE)
		return;

	display_name = g_strdup_printf (_("Passphrase for wireless network %s"), ssid);

	attributes = gnome_keyring_attribute_list_new ();
	attr.name = g_strdup ("essid");
	attr.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attr.value.string = g_strdup (ssid);
	g_array_append_val (attributes, attr);

	gnome_keyring_item_create (NULL,
						  GNOME_KEYRING_ITEM_GENERIC_SECRET,
						  display_name,
						  attributes,
						  nm_gconf_wso_get_key (self),
						  TRUE,
						  real_write_secrets_cb,
						  g_strdup (ssid),
						  g_free);

	g_free (display_name);
	gnome_keyring_attribute_list_free (attributes);
}

static void
nm_gconf_wso_init (NMGConfWSO * self)
{
	self->priv = NM_GCONF_WSO_GET_PRIVATE (self);
	self->priv->dispose_has_run = FALSE;
	self->priv->we_cipher = IW_AUTH_CIPHER_NONE;
	self->priv->key = NULL;
}

static void
nm_gconf_wso_dispose (GObject *object)
{
	NMGConfWSO *		self = (NMGConfWSO *) object;
	NMGConfWSOClass *	klass;
	GObjectClass *		parent_class;  

	if (self->priv->dispose_has_run)
		/* If dispose did already run, return. */
		return;

	/* Make sure dispose does not run twice. */
	self->priv->dispose_has_run = TRUE;

	/* 
	 * In dispose, you are supposed to free all types referenced from this
	 * object which might themselves hold a reference to self. Generally,
	 * the most simple solution is to unref all members on which you own a 
	 * reference.
	 */

	/* Chain up to the parent class */
	klass = NM_GCONF_WSO_CLASS (g_type_class_peek (NM_TYPE_GCONF_WSO));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	parent_class->dispose (object);
}

static void
nm_gconf_wso_finalize (GObject *object)
{
	NMGConfWSO *		self = (NMGConfWSO *) object;
	NMGConfWSOClass *	klass;
	GObjectClass *		parent_class;  

	/* Complete object destruction */
	g_free (self->priv->key);

	/* Chain up to the parent class */
	klass = NM_GCONF_WSO_CLASS (g_type_class_peek (NM_TYPE_GCONF_WSO));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	parent_class->finalize (object);
}


static void
nm_gconf_wso_class_init (NMGConfWSOClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = nm_gconf_wso_dispose;
	object_class->finalize = nm_gconf_wso_finalize;

	klass->serialize_dbus_func = real_serialize_dbus;
	klass->serialize_gconf_func = real_serialize_gconf;
	klass->read_secrets_func = real_read_secrets;
	klass->write_secrets_func = real_write_secrets;

	g_type_class_add_private (object_class, sizeof (NMGConfWSOPrivate));
}

GType
nm_gconf_wso_get_type (void)
{
	static GType type = 0;
	if (type == 0) {
		static const GTypeInfo info = {
			sizeof (NMGConfWSOClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) nm_gconf_wso_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (NMGConfWSO),
			0,		/* n_preallocs */
			(GInstanceInitFunc) nm_gconf_wso_init,
			NULL		/* value_table */
		};
		type = g_type_register_static (G_TYPE_OBJECT,
								"NMGConfWSO",
								&info, 0);
	}
	return type;
}
