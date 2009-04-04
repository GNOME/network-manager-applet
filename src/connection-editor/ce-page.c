/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * (C) Copyright 2008 - 2009 Red Hat, Inc.
 */

#include <net/ethernet.h>
#include <netinet/ether.h>
#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>

#include "ce-page.h"
#include "nma-marshal.h"
#include "utils.h"
#include "polkit-helpers.h"

#define DBUS_TYPE_G_ARRAY_OF_STRING         (dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING))
#define DBUS_TYPE_G_MAP_OF_VARIANT          (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))
#define DBUS_TYPE_G_MAP_OF_MAP_OF_VARIANT   (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, DBUS_TYPE_G_MAP_OF_VARIANT))

static gboolean internal_request_secrets (CEPage *self, GError **error);

G_DEFINE_ABSTRACT_TYPE (CEPage, ce_page, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_CONNECTION,
	PROP_INITIALIZED,
	PROP_PARENT_WINDOW,

	LAST_PROP
};

enum {
	CHANGED,
	INITIALIZED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

gint
ce_spin_output_with_default (GtkSpinButton *spin, gpointer user_data)
{
	int defvalue = GPOINTER_TO_INT (user_data);
	int val;
	gchar *buf = NULL;

	val = gtk_spin_button_get_value_as_int (spin);
	if (val == defvalue)
		buf = g_strdup (_("automatic"));
	else
		buf = g_strdup_printf ("%d", val);

	if (strcmp (buf, gtk_entry_get_text (GTK_ENTRY (spin))))
		gtk_entry_set_text (GTK_ENTRY (spin), buf);

	g_free (buf);
	return TRUE;
}

int
ce_get_property_default (NMSetting *setting, const char *property_name)
{
	GParamSpec *spec;
	GValue value = { 0, };

	spec = g_object_class_find_property (G_OBJECT_GET_CLASS (setting), property_name);
	g_return_val_if_fail (spec != NULL, -1);

	g_value_init (&value, spec->value_type);
	g_param_value_set_default (spec, &value);

	if (G_VALUE_HOLDS_CHAR (&value))
		return (int) g_value_get_char (&value);
	else if (G_VALUE_HOLDS_INT (&value))
		return g_value_get_int (&value);
	else if (G_VALUE_HOLDS_INT64 (&value))
		return (int) g_value_get_int64 (&value);
	else if (G_VALUE_HOLDS_LONG (&value))
		return (int) g_value_get_long (&value);
	else if (G_VALUE_HOLDS_UINT (&value))
		return (int) g_value_get_uint (&value);
	else if (G_VALUE_HOLDS_UINT64 (&value))
		return (int) g_value_get_uint64 (&value);
	else if (G_VALUE_HOLDS_ULONG (&value))
		return (int) g_value_get_ulong (&value);
	else if (G_VALUE_HOLDS_UCHAR (&value))
		return (int) g_value_get_uchar (&value);
	g_return_val_if_fail (FALSE, 0);
	return 0;
}

gboolean
ce_page_validate (CEPage *self, NMConnection *connection, GError **error)
{
	g_return_val_if_fail (CE_IS_PAGE (self), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	if (CE_PAGE_GET_CLASS (self)->validate)
		return CE_PAGE_GET_CLASS (self)->validate (self, connection, error);

	return TRUE;
}

void
ce_page_mac_to_entry (const GByteArray *mac, GtkEntry *entry)
{
	struct ether_addr addr;
	char *str_addr;

	g_return_if_fail (entry != NULL);
	g_return_if_fail (GTK_IS_ENTRY (entry));

	if (!mac || !mac->len)
		return;

	memcpy (addr.ether_addr_octet, mac->data, ETH_ALEN);
	str_addr = utils_ether_ntop (&addr);
	gtk_entry_set_text (entry, str_addr);
	g_free (str_addr);
}

GByteArray *
ce_page_entry_to_mac (GtkEntry *entry, gboolean *invalid)
{
	struct ether_addr *ether;
	const char *temp;
	GByteArray *mac;

	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (GTK_IS_ENTRY (entry), NULL);

	if (invalid)
		g_return_val_if_fail (*invalid == FALSE, NULL);

	temp = gtk_entry_get_text (entry);
	if (!temp || !strlen (temp))
		return NULL;

	ether = ether_aton (temp);
	if (!ether || !utils_mac_valid (ether)) {
		if (invalid)
			*invalid = TRUE;
		return NULL;
	}

	mac = g_byte_array_sized_new (ETH_ALEN);
	g_byte_array_append (mac, (const guint8 *) ether->ether_addr_octet, ETH_ALEN);
	return mac;
}

static void
emit_initialized (CEPage *self, GError *error)
{
	self->initialized = TRUE;
	g_signal_emit (self, signals[INITIALIZED], 0, NULL, error);
}

static void
try_secrets_again (PolKitAction *action,
                   gboolean gained_privilege,
                   GError *error,
                   gpointer user_data)
{
	CEPage *self = user_data;
	GError *real_error = NULL;

	if (error) {
		emit_initialized (self, error);
		return;
	}

	if (gained_privilege) {
		/* Yay! Got privilege, try again */
		internal_request_secrets (self, &real_error);
	} else if (!error) {
		/* Sometimes PK screws up and won't return an error even if
		 * the operation failed.
		 */
		g_set_error (&real_error, 0, 0, "%s",
		             _("Insufficient privileges or unknown error retrieving system connection secrets."));
	}

	if (real_error)
		emit_initialized (self, real_error);
	g_clear_error (&real_error);
}

static void
get_secrets_cb (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	CEPage *self = user_data;
	GError *pk_error = NULL;
	GError *error = NULL;
	GHashTable *settings = NULL, *setting_hash;
	gboolean do_signal = TRUE;

	if (!dbus_g_proxy_end_call (proxy, call, &pk_error,
	                            DBUS_TYPE_G_MAP_OF_MAP_OF_VARIANT, &settings,
	                            G_TYPE_INVALID)) {
		if (pk_helper_is_permission_denied_error (pk_error)) {
			/* If permission was denied, try to authenticate */
			if (pk_helper_obtain_auth (pk_error, self->parent_window, try_secrets_again, self, &error))
				do_signal = FALSE; /* 'secrets' signal will happen after auth result */
		}
	} else {
		/* Update the connection with the new secrets */
		setting_hash = g_hash_table_lookup (settings, self->setting_name);
		if (setting_hash) {
			if (!nm_connection_update_secrets (self->connection,
			                                   self->setting_name,
			                                   setting_hash,
			                                   &error)) {
				if (!error) {
					g_set_error (&error, 0, 0, "%s",
					             _("Failed to update connection secrets due to an unknown error."));
				}
			}
		}
		g_hash_table_destroy (settings);
	}

	if (do_signal)
		emit_initialized (self, error);
	g_clear_error (&error);
}

static gboolean
internal_request_secrets (CEPage *self, GError **error)
{
	DBusGProxyCall *call;
	GPtrArray *hints = NULL;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (self->proxy != NULL, FALSE);
	g_return_val_if_fail (self->setting_name != NULL, FALSE);

	hints = g_ptr_array_new ();
	call = dbus_g_proxy_begin_call_with_timeout (self->proxy, "GetSecrets",
	                                             get_secrets_cb, self, NULL,
	                                             10000,
	                                             G_TYPE_STRING, self->setting_name,
	                                             DBUS_TYPE_G_ARRAY_OF_STRING, hints,
	                                             G_TYPE_BOOLEAN, FALSE,
	                                             G_TYPE_INVALID);
	g_ptr_array_free (hints, TRUE);

	if (!call) {
		g_set_error (error, 0, 0, "%s", _("Could not request secrets from the system settings service."));
		return FALSE;
	}

	return TRUE;
}

gboolean
ce_page_initialize (CEPage *self,
                    const char *setting_name,
                    GError **error)
{
	DBusGConnection *g_connection;
	gboolean success = FALSE;
	NMConnectionScope scope;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (self->connection != NULL, FALSE);

	/* Don't need to request secrets from user connections or from
	 * settings which are known to not require secrets.
	 */
	scope = nm_connection_get_scope (self->connection);
	if (!setting_name || (scope != NM_CONNECTION_SCOPE_SYSTEM)) {
		emit_initialized (self, NULL);
		return TRUE;
	}

	g_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, error);
	if (!g_connection) {
		g_set_error (error, 0, 0, "%s", _("Could not connect to D-Bus to request connection secrets."));
		return FALSE;
	}

	if (self->setting_name)
		g_free (self->setting_name);
	self->setting_name = g_strdup (setting_name);

	self->proxy = dbus_g_proxy_new_for_name (g_connection,
	                                         NM_DBUS_SERVICE_SYSTEM_SETTINGS,
	                                         nm_connection_get_path (self->connection),
	                                         NM_DBUS_IFACE_SETTINGS_CONNECTION_SECRETS);
	if (!self->proxy) {
		g_set_error (error, 0, 0, "%s", _("Could not create D-Bus proxy for connection secrets."));
		goto out;
	}

	success = internal_request_secrets (self, error);

out:
	dbus_g_connection_unref (g_connection);
	return success;
}

static void
ce_page_init (CEPage *self)
{
}

static void
dispose (GObject *object)
{
	CEPage *self = CE_PAGE (object);

	if (self->disposed)
		return;

	self->disposed = TRUE;

	if (self->page)
		g_object_unref (self->page);

	if (self->xml)
		g_object_unref (self->xml);

	if (self->proxy)
		g_object_unref (self->proxy);

	if (self->connection)
		g_object_unref (self->connection);

	G_OBJECT_CLASS (ce_page_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	CEPage *self = CE_PAGE (object);

	g_free (self->title);
	g_free (self->setting_name);

	G_OBJECT_CLASS (ce_page_parent_class)->finalize (object);
}

GtkWidget *
ce_page_get_page (CEPage *self)
{
	g_return_val_if_fail (CE_IS_PAGE (self), NULL);

	return self->page;
}

const char *
ce_page_get_title (CEPage *self)
{
	g_return_val_if_fail (CE_IS_PAGE (self), NULL);

	return self->title;
}

gboolean
ce_page_get_initialized (CEPage *self)
{
	g_return_val_if_fail (CE_IS_PAGE (self), FALSE);

	return self->initialized;
}

void
ce_page_changed (CEPage *self)
{
	g_return_if_fail (CE_IS_PAGE (self));

	g_signal_emit (self, signals[CHANGED], 0);
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	CEPage *self = CE_PAGE (object);

	switch (prop_id) {
	case PROP_CONNECTION:
		g_value_set_object (value, self->connection);
		break;
	case PROP_INITIALIZED:
		g_value_set_boolean (value, self->initialized);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	CEPage *self = CE_PAGE (object);

	switch (prop_id) {
	case PROP_CONNECTION:
		if (self->connection)
			g_object_unref (self->connection);
		self->connection = g_value_dup_object (value);
		break;
	case PROP_PARENT_WINDOW:
		self->parent_window = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
ce_page_class_init (CEPageClass *page_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (page_class);

	/* virtual methods */
	object_class->dispose      = dispose;
	object_class->finalize     = finalize;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_CONNECTION,
		 g_param_spec_object (CE_PAGE_CONNECTION,
		                      "Connection",
		                      "Connection",
		                      NM_TYPE_CONNECTION,
		                      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_INITIALIZED,
		 g_param_spec_boolean (CE_PAGE_INITIALIZED,
		                       "Initialized",
		                       "Initialized",
		                       FALSE,
		                       G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_PARENT_WINDOW,
		 g_param_spec_pointer (CE_PAGE_PARENT_WINDOW,
		                       "Parent window",
		                       "Parent window",
		                       G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	/* Signals */
	signals[CHANGED] = 
		g_signal_new ("changed",
	                      G_OBJECT_CLASS_TYPE (object_class),
	                      G_SIGNAL_RUN_FIRST,
	                      G_STRUCT_OFFSET (CEPageClass, changed),
	                      NULL, NULL,
	                      g_cclosure_marshal_VOID__VOID,
	                      G_TYPE_NONE, 0);

	signals[INITIALIZED] = 
		g_signal_new ("initialized",
	                      G_OBJECT_CLASS_TYPE (object_class),
	                      G_SIGNAL_RUN_FIRST,
	                      G_STRUCT_OFFSET (CEPageClass, initialized),
	                      NULL, NULL,
	                      nma_marshal_VOID__POINTER_POINTER,
	                      G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
}
