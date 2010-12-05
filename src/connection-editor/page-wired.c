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
 * (C) Copyright 2008 - 2010 Red Hat, Inc.
 */

#include <string.h>
#include <net/ethernet.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-wired.h>

#include "page-wired.h"

G_DEFINE_TYPE (CEPageWired, ce_page_wired, CE_TYPE_PAGE)

#define CE_PAGE_WIRED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_WIRED, CEPageWiredPrivate))

typedef struct {
	NMSettingWired *setting;

	GtkEntry *device_mac;  /* Permanent MAC of the device */
	GtkEntry *cloned_mac;  /* Cloned MAC - used for MAC spoofing */
	GtkComboBox *port;
	GtkComboBox *speed;
	GtkToggleButton *duplex;
	GtkToggleButton *autonegotiate;
	GtkSpinButton *mtu;

	gboolean disposed;
} CEPageWiredPrivate;

#define PORT_DEFAULT  0
#define PORT_TP       1
#define PORT_AUI      2
#define PORT_BNC      3
#define PORT_MII      4

#define SPEED_DEFAULT 0
#define SPEED_10      1
#define SPEED_100     2
#define SPEED_1000    3
#define SPEED_10000   4

static void
wired_private_init (CEPageWired *self)
{
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);
	GtkBuilder *builder;

	builder = CE_PAGE (self)->builder;

	priv->device_mac = GTK_ENTRY (GTK_WIDGET (gtk_builder_get_object (builder, "wired_device_mac")));
	priv->cloned_mac = GTK_ENTRY (GTK_WIDGET (gtk_builder_get_object (builder, "wired_cloned_mac")));
	priv->port = GTK_COMBO_BOX (GTK_WIDGET (gtk_builder_get_object (builder, "wired_port")));
	priv->speed = GTK_COMBO_BOX (GTK_WIDGET (gtk_builder_get_object (builder, "wired_speed")));
	priv->duplex = GTK_TOGGLE_BUTTON (GTK_WIDGET (gtk_builder_get_object (builder, "wired_duplex")));
	priv->autonegotiate = GTK_TOGGLE_BUTTON (GTK_WIDGET (gtk_builder_get_object (builder, "wired_autonegotiate")));
	priv->mtu = GTK_SPIN_BUTTON (GTK_WIDGET (gtk_builder_get_object (builder, "wired_mtu")));
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
populate_ui (CEPageWired *self)
{
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);
	NMSettingWired *setting = priv->setting;
	const char *port;
	const char *duplex;
	int port_idx = PORT_DEFAULT;
	int speed_idx;
	int mtu_def;

	/* Port */
	port = nm_setting_wired_get_port (setting);
	if (port) {
		if (!strcmp (port, "tp"))
			port_idx = PORT_TP;
		else if (!strcmp (port, "aui"))
			port_idx = PORT_AUI;
		else if (!strcmp (port, "bnc"))
			port_idx = PORT_BNC;
		else if (!strcmp (port, "mii"))
			port_idx = PORT_MII;
	}
	gtk_combo_box_set_active (priv->port, port_idx);

	/* Speed */
	switch (nm_setting_wired_get_speed (setting)) {
	case 10:
		speed_idx = SPEED_10;
		break;
	case 100:
		speed_idx = SPEED_100;
		break;
	case 1000:
		speed_idx = SPEED_1000;
		break;
	case 10000:
		speed_idx = SPEED_10000;
		break;
	default:
		speed_idx = SPEED_DEFAULT;
		break;
	}
	gtk_combo_box_set_active (priv->speed, speed_idx);

	/* Duplex */
	duplex = nm_setting_wired_get_duplex (setting);
	if (duplex && !strcmp (duplex, "half"))
		gtk_toggle_button_set_active (priv->duplex, FALSE);
	else
		gtk_toggle_button_set_active (priv->duplex, TRUE);

	/* Autonegotiate */
	gtk_toggle_button_set_active (priv->autonegotiate, 
	                              nm_setting_wired_get_auto_negotiate (setting));

	/* Device MAC address */
	ce_page_mac_to_entry (nm_setting_wired_get_mac_address (setting), priv->device_mac);
	g_signal_connect (priv->device_mac, "changed", G_CALLBACK (stuff_changed), self);

	/* Cloned MAC address */
	ce_page_mac_to_entry (nm_setting_wired_get_cloned_mac_address (setting), priv->cloned_mac);
	g_signal_connect (priv->cloned_mac, "changed", G_CALLBACK (stuff_changed), self);

	/* MTU */
	mtu_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_WIRED_MTU);
	g_signal_connect (priv->mtu, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (mtu_def));

	gtk_spin_button_set_value (priv->mtu, (gdouble) nm_setting_wired_get_mtu (setting));
}

static void
finish_setup (CEPageWired *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPage *parent = CE_PAGE (self);
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);
	GtkWidget *widget;

	if (error)
		return;

	populate_ui (self);

	g_signal_connect (priv->port, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->speed, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->duplex, "toggled", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->autonegotiate, "toggled", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->mtu, "value-changed", G_CALLBACK (stuff_changed), self);

	/* Hide widgets we don't yet support */
	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wired_port_label"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wired_port"));
	gtk_widget_hide (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wired_speed_label"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wired_speed"));
	gtk_widget_hide (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wired_duplex"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (parent->builder, "wired_autonegotiate"));
	gtk_widget_hide (widget);
}

CEPage *
ce_page_wired_new (NMConnection *connection,
                   GtkWindow *parent_window,
                   const char **out_secrets_setting_name,
                   GError **error)
{
	CEPageWired *self;
	CEPageWiredPrivate *priv;

	self = CE_PAGE_WIRED (ce_page_new (CE_TYPE_PAGE_WIRED,
	                                   connection,
	                                   parent_window,
	                                   UIDIR "/ce-page-wired.ui",
	                                   "WiredPage",
	                                   _("Wired")));
	if (!self) {
		g_set_error_literal (error, 0, 0, _("Could not load wired user interface."));
		return NULL;
	}

	wired_private_init (self);
	priv = CE_PAGE_WIRED_GET_PRIVATE (self);

	priv->setting = (NMSettingWired *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRED);
	if (!priv->setting) {
		priv->setting = NM_SETTING_WIRED (nm_setting_wired_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageWired *self)
{
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);
	const char *port;
	guint32 speed;
	GByteArray *device_mac = NULL;
	GByteArray *cloned_mac = NULL;

	/* Port */
	switch (gtk_combo_box_get_active (priv->port)) {
	case PORT_TP:
		port = "tp";
		break;
	case PORT_AUI:
		port = "aui";
		break;
	case PORT_BNC:
		port = "bnc";
		break;
	case PORT_MII:
		port = "mii";
		break;
	default:
		port = NULL;
		break;
	}

	/* Speed */
	switch (gtk_combo_box_get_active (priv->speed)) {
	case SPEED_10:
		speed = 10;
		break;
	case SPEED_100:
		speed = 100;
		break;
	case SPEED_1000:
		speed = 1000;
		break;
	case SPEED_10000:
		speed = 10000;
		break;
	default:
		speed = 0;
		break;
	}

	device_mac = ce_page_entry_to_mac (priv->device_mac, NULL);
	cloned_mac = ce_page_entry_to_mac (priv->cloned_mac, NULL);

	g_object_set (priv->setting,
				  NM_SETTING_WIRED_MAC_ADDRESS, device_mac,
				  NM_SETTING_WIRED_CLONED_MAC_ADDRESS, cloned_mac,
				  NM_SETTING_WIRED_PORT, port,
				  NM_SETTING_WIRED_SPEED, speed,
				  NM_SETTING_WIRED_DUPLEX, gtk_toggle_button_get_active (priv->duplex) ? "full" : "half",
				  NM_SETTING_WIRED_AUTO_NEGOTIATE, gtk_toggle_button_get_active (priv->autonegotiate),
				  NM_SETTING_WIRED_MTU, (guint32) gtk_spin_button_get_value_as_int (priv->mtu),
				  NULL);

	if (device_mac)
		g_byte_array_free (device_mac, TRUE);
	if (cloned_mac)
		g_byte_array_free (cloned_mac, TRUE);

}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageWired *self = CE_PAGE_WIRED (page);
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);
	gboolean invalid = FALSE;
	GByteArray *ignore;

	ignore = ce_page_entry_to_mac (priv->device_mac, &invalid);
	if (invalid)
		return FALSE;
	if (ignore)
		g_byte_array_free (ignore, TRUE);

	ignore = ce_page_entry_to_mac (priv->cloned_mac, &invalid);
	if (invalid)
		return FALSE;
	if (ignore)
		g_byte_array_free (ignore, TRUE);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static void
ce_page_wired_init (CEPageWired *self)
{
}

static void
ce_page_wired_class_init (CEPageWiredClass *wired_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (wired_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (wired_class);

	g_type_class_add_private (object_class, sizeof (CEPageWiredPrivate));

	/* virtual methods */
	parent_class->validate = validate;
}


void
wired_connection_new (GtkWindow *parent,
                      PageNewConnectionResultFunc result_func,
                      PageGetConnectionsFunc get_connections_func,
                      gpointer user_data)
{
	NMConnection *connection;

	connection = ce_page_new_connection (_("Wired connection %d"),
	                                     NM_SETTING_WIRED_SETTING_NAME,
	                                     TRUE,
	                                     get_connections_func,
	                                     user_data);
	nm_connection_add_setting (connection, nm_setting_wired_new ());

	(*result_func) (connection, FALSE, NULL, user_data);
}

