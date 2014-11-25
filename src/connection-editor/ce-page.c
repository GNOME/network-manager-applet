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
 * Copyright 2008 - 2014 Red Hat, Inc.
 */

#include <config.h>

#include <net/ethernet.h>
#include <netinet/ether.h>
#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>

#include "ce-page.h"

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

static gboolean
spin_output_with_default_string (GtkSpinButton *spin,
                                 int defvalue,
                                 const char *defstring)
{
	int val;
	gchar *buf = NULL;

	val = gtk_spin_button_get_value_as_int (spin);
	if (val == defvalue)
		buf = g_strdup (defstring);
	else
		buf = g_strdup_printf ("%d", val);

	if (strcmp (buf, gtk_entry_get_text (GTK_ENTRY (spin))))
		gtk_entry_set_text (GTK_ENTRY (spin), buf);

	g_free (buf);
	return TRUE;
}

gboolean
ce_spin_output_with_automatic (GtkSpinButton *spin, gpointer user_data)
{
	return spin_output_with_default_string (spin,
	                                        GPOINTER_TO_INT (user_data),
	                                        _("automatic"));
}

gboolean
ce_spin_output_with_default (GtkSpinButton *spin, gpointer user_data)
{
	return spin_output_with_default_string (spin,
	                                        GPOINTER_TO_INT (user_data),
	                                        _("default"));
}

int
ce_get_property_default (NMSetting *setting, const char *property_name)
{
	GParamSpec *spec;
	GValue value = { 0, };

	g_return_val_if_fail (NM_IS_SETTING (setting), -1);

	spec = g_object_class_find_property (G_OBJECT_GET_CLASS (setting), property_name);
	g_return_val_if_fail (spec != NULL, -1);

	g_value_init (&value, spec->value_type);
	g_param_value_set_default (spec, &value);

	if (G_VALUE_HOLDS_CHAR (&value))
		return (int) g_value_get_schar (&value);
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

char **
ce_page_get_mac_list (CEPage *self, GType device_type, const char *mac_property)
{
	const GPtrArray *devices;
	GPtrArray *macs;
	int i;

	g_return_val_if_fail (CE_IS_PAGE (self), NULL);

	if (!self->client)
		return NULL;

	macs = g_ptr_array_new ();
	devices = nm_client_get_devices (self->client);
	for (i = 0; i < devices->len; i++) {
		NMDevice *dev = g_ptr_array_index (devices, i);
		const char *iface;
		char *mac, *item;

		if (!G_TYPE_CHECK_INSTANCE_TYPE (dev, device_type))
			continue;

		g_object_get (G_OBJECT (dev), mac_property, &mac, NULL);
		iface = nm_device_get_iface (NM_DEVICE (dev));
		item = g_strdup_printf ("%s (%s)", mac, iface);
		g_free (mac);
		g_ptr_array_add (macs, item);
	}

	g_ptr_array_add (macs, NULL);
	return (char **)g_ptr_array_free (macs, FALSE);
}

void
ce_page_setup_mac_combo (CEPage *self, GtkComboBox *combo,
                         const char *current_mac, char **mac_list)
{
	char **iter, *active_mac = NULL;
	int i, active_idx = -1;
	int current_mac_len;
	GtkWidget *entry;

	if (current_mac)
		current_mac_len = strlen (current_mac);
	else
		current_mac_len = -1;

	for (iter = mac_list, i = 0; iter && *iter; iter++, i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), *iter);
		if (   current_mac
		    && g_ascii_strncasecmp (*iter, current_mac, current_mac_len) == 0
		    && ((*iter)[current_mac_len] == '\0' || (*iter)[current_mac_len] == ' ')) {
			active_mac = *iter;
			active_idx = i;
		}
	}

	if (current_mac) {
		/* set active item */
		gtk_combo_box_set_active (combo, active_idx);
		
		if (!active_mac)
			gtk_combo_box_text_prepend_text (GTK_COMBO_BOX_TEXT (combo), current_mac);

		entry = gtk_bin_get_child (GTK_BIN (combo));
		if (entry)
			gtk_entry_set_text (GTK_ENTRY (entry), active_mac ? active_mac : current_mac);
	}
}

char *
ce_page_entry_to_mac (GtkEntry *entry, int type, gboolean *invalid)
{
	const char *sp, *temp;
	char *mac;

	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (GTK_IS_ENTRY (entry), NULL);

	if (invalid)
		g_return_val_if_fail (*invalid == FALSE, NULL);

	temp = gtk_entry_get_text (entry);
	if (!temp || !*temp)
		return NULL;

	sp = strchr (temp, ' ');
	if (sp)
		mac = g_strndup (temp, sp - temp);
	else
		mac = g_strdup (temp);

	if (!nm_utils_hwaddr_valid (mac, nm_utils_hwaddr_len (type))) {
		g_free (mac);
		if (invalid)
			*invalid = TRUE;
		return NULL;
	}
	return mac;
}

char *
ce_page_get_next_available_name (const GPtrArray *connections, const char *format)
{
	GSList *names = NULL, *iter;
	char *cname = NULL;
	int i = 0;

	for (i = 0; i < connections->len; i++) {
		const char *id;

		id = nm_connection_get_id (connections->pdata[i]);
		g_assert (id);
		names = g_slist_append (names, (gpointer) id);
	}

	/* Find the next available unique connection name */
	for (i = 1; !cname && i < 10000; i++) {
		char *temp;
		gboolean found = FALSE;

		temp = g_strdup_printf (format, i);
		for (iter = names; iter; iter = g_slist_next (iter)) {
			if (!strcmp (iter->data, temp)) {
				found = TRUE;
				break;
			}
		}
		if (!found)
			cname = temp;
		else
			g_free (temp);
	}

	g_slist_free (names);
	return cname;
}

static void
emit_initialized (CEPage *self, GError *error)
{
	self->initialized = TRUE;
	g_signal_emit (self, signals[INITIALIZED], 0, error);
}

void
ce_page_complete_init (CEPage *self,
                       const char *setting_name,
                       GVariant *secrets,
                       GError *error)
{
	GError *update_error = NULL;
	GVariant *setting_dict;
	char *dbus_err;
	gboolean ignore_error = FALSE;

	g_return_if_fail (self != NULL);
	g_return_if_fail (CE_IS_PAGE (self));

	if (error) {
		dbus_err = g_dbus_error_get_remote_error (error);
		ignore_error =    !g_strcmp0 (dbus_err, "org.freedesktop.NetworkManager.Settings.InvalidSetting")
		               || !g_strcmp0 (dbus_err, "org.freedesktop.NetworkManager.Settings.Connection.SettingNotFound")
		               || !g_strcmp0 (dbus_err, "org.freedesktop.NetworkManager.AgentManager.NoSecrets");
		g_free (dbus_err);
	}

	/* Ignore missing settings errors */
	if (error && !ignore_error) {
		emit_initialized (self, error);
		return;
	} else if (!setting_name || !secrets || g_variant_n_children (secrets) == 0) {
		/* Success, no secrets */
		emit_initialized (self, NULL);
		return;
	}

	g_assert (setting_name);
	g_assert (secrets);

	setting_dict = g_variant_lookup_value (secrets, setting_name, NM_VARIANT_TYPE_SETTING);
	if (!setting_dict) {
		/* Success, no secrets */
		emit_initialized (self, NULL);
		return;
	}
	g_variant_unref (setting_dict);

	/* Update the connection with the new secrets */
	if (nm_connection_update_secrets (self->connection,
	                                  setting_name,
	                                  secrets,
	                                  &update_error)) {
		/* Success */
		emit_initialized (self, NULL);
		return;
	}

	if (!update_error) {
		g_set_error_literal (&update_error, NMA_ERROR, NMA_ERROR_GENERIC,
		                     _("Failed to update connection secrets due to an unknown error."));
	}

	emit_initialized (self, update_error);
	g_clear_error (&update_error);
}

static void
ce_page_init (CEPage *self)
{
	self->builder = gtk_builder_new ();
}

static void
dispose (GObject *object)
{
	CEPage *self = CE_PAGE (object);

	g_clear_object (&self->page);
	g_clear_object (&self->builder);
	g_clear_object (&self->connection);

	G_OBJECT_CLASS (ce_page_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	CEPage *self = CE_PAGE (object);

	g_free (self->title);

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
	                      NULL, NULL, NULL,
	                      G_TYPE_NONE, 0);

	signals[INITIALIZED] = 
		g_signal_new ("initialized",
	                      G_OBJECT_CLASS_TYPE (object_class),
	                      G_SIGNAL_RUN_FIRST,
	                      G_STRUCT_OFFSET (CEPageClass, initialized),
	                      NULL, NULL, NULL,
	                      G_TYPE_NONE, 1, G_TYPE_POINTER);
}


NMConnection *
ce_page_new_connection (const char *format,
                        const char *ctype,
                        gboolean autoconnect,
                        NMClient *client,
                        gpointer user_data)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	char *uuid, *id;
	const GPtrArray *connections;

	connection = nm_simple_connection_new ();

	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	nm_connection_add_setting (connection, NM_SETTING (s_con));

	uuid = nm_utils_uuid_generate ();

	connections = nm_client_get_connections (client);
	id = ce_page_get_next_available_name (connections, format);

	g_object_set (s_con,
	              NM_SETTING_CONNECTION_UUID, uuid,
	              NM_SETTING_CONNECTION_ID, id,
	              NM_SETTING_CONNECTION_TYPE, ctype,
	              NM_SETTING_CONNECTION_AUTOCONNECT, autoconnect,
	              NULL);

	g_free (uuid);
	g_free (id);

	return connection;
}

CEPage *
ce_page_new (GType page_type,
             NMConnection *connection,
             GtkWindow *parent_window,
             NMClient *client,
             const char *ui_file,
             const char *widget_name,
             const char *title)
{
	CEPage *self;
	GError *error = NULL;

	g_return_val_if_fail (title != NULL, NULL);
	if (ui_file)
		g_return_val_if_fail (widget_name != NULL, NULL);

	self = CE_PAGE (g_object_new (page_type,
	                              CE_PAGE_CONNECTION, connection,
	                              CE_PAGE_PARENT_WINDOW, parent_window,
	                              NULL));
	self->title = g_strdup (title);
	self->client = client;

	if (ui_file) {
		if (!gtk_builder_add_from_file (self->builder, ui_file, &error)) {
			g_warning ("Couldn't load builder file: %s", error->message);
			g_error_free (error);
			g_object_unref (self);
			return NULL;
		}

		self->page = GTK_WIDGET (gtk_builder_get_object (self->builder, widget_name));
		if (!self->page) {
			g_warning ("Couldn't load page widget '%s' from %s", widget_name, ui_file);
			g_object_unref (self);
			return NULL;
		}
		g_object_ref_sink (self->page);
	}
	return self;
}

