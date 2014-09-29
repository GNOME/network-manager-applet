/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Lubomir Rintel <lkundrak@v3.sk>
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
 * Copyright 2014 Red Hat, Inc.
 */

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-bluetooth.h>

#include "page-bluetooth.h"
#include "nm-connection-editor.h"

G_DEFINE_TYPE (CEPageBluetooth, ce_page_bluetooth, CE_TYPE_PAGE)

#define CE_PAGE_BLUETOOTH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_BLUETOOTH, CEPageBluetoothPrivate))

typedef struct {
	NMSettingBluetooth *setting;

	GtkEntry *bdaddr;

	gboolean disposed;
} CEPageBluetoothPrivate;

static void
bluetooth_private_init (CEPageBluetooth *self)
{
	CEPageBluetoothPrivate *priv = CE_PAGE_BLUETOOTH_GET_PRIVATE (self);
	GtkBuilder *builder;

	builder = CE_PAGE (self)->builder;

	priv->bdaddr = GTK_ENTRY (gtk_builder_get_object (builder, "bluetooth_bdaddr"));

}

static void
populate_ui (CEPageBluetooth *self, NMConnection *connection)
{
	CEPageBluetoothPrivate *priv = CE_PAGE_BLUETOOTH_GET_PRIVATE (self);
	NMSettingBluetooth *setting = priv->setting;

	ce_page_mac_to_entry (nm_setting_bluetooth_get_bdaddr (setting),
	                      ARPHRD_ETHER, priv->bdaddr);
}

static void
stuff_changed (GtkEditable *editable, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
finish_setup (CEPageBluetooth *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPage *parent = CE_PAGE (self);
	CEPageBluetoothPrivate *priv = CE_PAGE_BLUETOOTH_GET_PRIVATE (self);

	if (error)
		return;

	populate_ui (self, parent->connection);

	g_signal_connect (priv->bdaddr, "changed", G_CALLBACK (stuff_changed), self);
}

CEPage *
ce_page_bluetooth_new (NMConnection *connection,
                       GtkWindow *parent_window,
                       NMClient *client,
                       NMRemoteSettings *settings,
                       const char **out_secrets_setting_name,
                       GError **error)
{
	CEPageBluetooth *self;
	CEPageBluetoothPrivate *priv;

	self = CE_PAGE_BLUETOOTH (ce_page_new (CE_TYPE_PAGE_BLUETOOTH,
	                          connection,
	                          parent_window,
	                          client,
	                          settings,
	                          UIDIR "/ce-page-bluetooth.ui",
	                          "BluetoothPage",
	                          _("Bluetooth")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Could not load Bluetooth user interface."));
		return NULL;
	}

	bluetooth_private_init (self);
	priv = CE_PAGE_BLUETOOTH_GET_PRIVATE (self);

	priv->setting = nm_connection_get_setting_bluetooth (connection);
	if (!priv->setting) {
		priv->setting = NM_SETTING_BLUETOOTH (nm_setting_bluetooth_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	*out_secrets_setting_name = NM_SETTING_BLUETOOTH_SETTING_NAME;

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageBluetooth *self)
{
	CEPageBluetoothPrivate *priv = CE_PAGE_BLUETOOTH_GET_PRIVATE (self);
	GByteArray *bdaddr;

	bdaddr = ce_page_entry_to_mac (priv->bdaddr, ARPHRD_ETHER, NULL);
	g_object_set (priv->setting,
	              NM_SETTING_BLUETOOTH_BDADDR, bdaddr,
	              NULL);
	if (bdaddr)
		g_byte_array_free (bdaddr, TRUE);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageBluetooth *self = CE_PAGE_BLUETOOTH (page);
	CEPageBluetoothPrivate *priv = CE_PAGE_BLUETOOTH_GET_PRIVATE (self);
	GByteArray *bdaddr;
	gboolean invalid;

	bdaddr = ce_page_entry_to_mac (priv->bdaddr, ARPHRD_ETHER, &invalid);
	if (invalid)
		return FALSE;
	if (bdaddr)
		g_byte_array_free (bdaddr, TRUE);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static void
ce_page_bluetooth_init (CEPageBluetooth *self)
{
}

static void
ce_page_bluetooth_class_init (CEPageBluetoothClass *bluetooth_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (bluetooth_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (bluetooth_class);

	g_type_class_add_private (object_class, sizeof (CEPageBluetoothPrivate));

	/* virtual methods */
	parent_class->validate = validate;
}
