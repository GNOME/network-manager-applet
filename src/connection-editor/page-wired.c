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
 * (C) Copyright 2008 - 2012 Red Hat, Inc.
 */

#include "config.h"

#include <string.h>
#include <net/ethernet.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-wired.h>
#include <nm-device-ethernet.h>
#include <nm-utils.h>
#include <net/if_arp.h> /* for ARPHRD_ETHER for MAC utilies */

#include "page-wired.h"

G_DEFINE_TYPE (CEPageWired, ce_page_wired, CE_TYPE_PAGE)

#define CE_PAGE_WIRED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_WIRED, CEPageWiredPrivate))

typedef struct {
	NMSettingWired *setting;

#if GTK_CHECK_VERSION(2,24,0)
	GtkComboBoxText *device_mac;  /* Permanent MAC of the device */
#else
	GtkComboBoxEntry *device_mac;
#endif
	GtkEntry *cloned_mac;         /* Cloned MAC - used for MAC spoofing */
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
	GtkWidget *align;
	GtkLabel *label;

	builder = CE_PAGE (self)->builder;

#if GTK_CHECK_VERSION(2,24,0)
	priv->device_mac = GTK_COMBO_BOX_TEXT (gtk_combo_box_text_new_with_entry ());
	gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (priv->device_mac), 0);
#else
	priv->device_mac = GTK_COMBO_BOX_ENTRY (gtk_combo_box_entry_new_text ());
	gtk_combo_box_entry_set_text_column (GTK_COMBO_BOX_ENTRY (priv->device_mac), 0);
#endif
	gtk_widget_set_tooltip_text (GTK_WIDGET (priv->device_mac),
	                             _("This option locks this connection to the network device specified by its permanent MAC address entered here.  Example: 00:11:22:33:44:55"));

	align = GTK_WIDGET (gtk_builder_get_object (builder, "wired_device_mac_alignment"));
	gtk_container_add (GTK_CONTAINER (align), GTK_WIDGET (priv->device_mac));
	gtk_widget_show_all (GTK_WIDGET (priv->device_mac));

	/* Set mnemonic widget for device MAC label */
	label = GTK_LABEL (GTK_WIDGET (gtk_builder_get_object (builder, "wired_device_mac_label")));
	gtk_label_set_mnemonic_widget (label, GTK_WIDGET (priv->device_mac));

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
	char **mac_list, **iter;
	const GByteArray *s_mac;
	char *s_mac_str;
	char *active_mac = NULL;
	GtkWidget *entry;

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
	mac_list = ce_page_get_mac_list (CE_PAGE (self));
	s_mac = nm_setting_wired_get_mac_address (setting);
	s_mac_str = s_mac ? nm_utils_hwaddr_ntoa (s_mac->data, ARPHRD_ETHER) : NULL;
	for (iter = mac_list; iter && *iter; iter++) {
#if GTK_CHECK_VERSION (2,24,0)
		gtk_combo_box_text_append_text (priv->device_mac, *iter);
#else
		gtk_combo_box_append_text (GTK_COMBO_BOX (priv->device_mac), *iter);
#endif
		if (s_mac_str && g_ascii_strncasecmp (*iter, s_mac_str, 17) == 0)
			active_mac = *iter;
	}

	if (s_mac_str) {
		if (!active_mac) {
#if GTK_CHECK_VERSION (2,24,0)
			gtk_combo_box_text_prepend_text (priv->device_mac, s_mac_str);
#else
			gtk_combo_box_prepend_text (GTK_COMBO_BOX (priv->device_mac), s_mac_str);
#endif
		}

		entry = gtk_bin_get_child (GTK_BIN (priv->device_mac));
		if (entry)
			gtk_entry_set_text (GTK_ENTRY (entry), active_mac ? active_mac : s_mac_str);
	}
	g_free (s_mac_str);
	g_strfreev (mac_list);
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
                   NMClient *client,
                   const char **out_secrets_setting_name,
                   GError **error)
{
	CEPageWired *self;
	CEPageWiredPrivate *priv;

	self = CE_PAGE_WIRED (ce_page_new (CE_TYPE_PAGE_WIRED,
	                                   connection,
	                                   parent_window,
	                                   client,
	                                   UIDIR "/ce-page-wired.ui",
	                                   "WiredPage",
	                                   _("Wired")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Could not load wired user interface."));
		return NULL;
	}

	wired_private_init (self);
	priv = CE_PAGE_WIRED_GET_PRIVATE (self);

	priv->setting = nm_connection_get_setting_wired (connection);
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
	GtkWidget *entry;

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

	entry = gtk_bin_get_child (GTK_BIN (priv->device_mac));
	if (entry)
		device_mac = ce_page_entry_to_mac (GTK_ENTRY (entry), NULL);
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
	GtkWidget *entry;

	entry = gtk_bin_get_child (GTK_BIN (priv->device_mac));
	if (entry) {
		ignore = ce_page_entry_to_mac (GTK_ENTRY (entry), &invalid);
		if (invalid)
			return FALSE;
		if (ignore)
			g_byte_array_free (ignore, TRUE);
	}

	ignore = ce_page_entry_to_mac (priv->cloned_mac, &invalid);
	if (invalid)
		return FALSE;
	if (ignore)
		g_byte_array_free (ignore, TRUE);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static char **
get_mac_list (CEPage *page)
{
	const GPtrArray *devices;
	GString *mac_str;
	char **mac_list;
	int i;

	if (!page->client)
		return NULL;

	mac_str = g_string_new (NULL);
	devices = nm_client_get_devices (page->client);
	for (i = 0; devices && (i < devices->len); i++) {
		const char *mac, *iface;
		NMDevice *dev = g_ptr_array_index (devices, i);

		if (!NM_IS_DEVICE_ETHERNET (dev))
			continue;

		mac = nm_device_ethernet_get_permanent_hw_address (NM_DEVICE_ETHERNET (dev));
		iface = nm_device_get_iface (NM_DEVICE (dev));
		g_string_append_printf (mac_str, "%s (%s),", mac, iface);
	}
	g_string_truncate (mac_str, mac_str->len-1);

	mac_list = g_strsplit (mac_str->str, ",", 0);
	g_string_free (mac_str, TRUE);

	return mac_list;
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
	parent_class->get_mac_list = get_mac_list;
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

