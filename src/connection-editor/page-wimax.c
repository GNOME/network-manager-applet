/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * Copyright 2012 Red Hat, Inc.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-wimax.h>
#include <nm-device-wimax.h>
#include <nm-utils.h>

#include "page-wimax.h"

G_DEFINE_TYPE (CEPageWimax, ce_page_wimax, CE_TYPE_PAGE)

#define CE_PAGE_WIMAX_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_WIMAX, CEPageWimaxPrivate))

typedef struct {
	NMSettingWimax *setting;

	GtkEntry *name;
#if GTK_CHECK_VERSION (2,24,0)
	GtkComboBoxText *device_mac;  /* Permanent MAC of the device */
#else
	GtkComboBoxEntry *device_mac;
#endif

	gboolean disposed;
} CEPageWimaxPrivate;

static void
wimax_private_init (CEPageWimax *self)
{
	CEPageWimaxPrivate *priv = CE_PAGE_WIMAX_GET_PRIVATE (self);
	GtkBuilder *builder;
	GtkWidget *align;
	GtkLabel *label;

	builder = CE_PAGE (self)->builder;

	priv->name = GTK_ENTRY (gtk_builder_get_object (builder, "wimax_name"));

#if GTK_CHECK_VERSION(2,24,0)
	priv->device_mac = GTK_COMBO_BOX_TEXT (gtk_combo_box_text_new_with_entry ());
	gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (priv->device_mac), 0);
#else
	priv->device_mac = GTK_COMBO_BOX_ENTRY (gtk_combo_box_entry_new_text ());
	gtk_combo_box_entry_set_text_column (GTK_COMBO_BOX_ENTRY (priv->device_mac), 0);
#endif
	gtk_widget_set_tooltip_text (GTK_WIDGET (priv->device_mac),
	                             _("This option locks this connection to the network device specified by its permanent MAC address entered here.  Example: 00:11:22:33:44:55"));

	align = GTK_WIDGET (gtk_builder_get_object (builder, "wimax_device_mac_alignment"));
	gtk_container_add (GTK_CONTAINER (align), GTK_WIDGET (priv->device_mac));
	gtk_widget_show_all (GTK_WIDGET (priv->device_mac));

	/* Set mnemonic widget for device MAC label */
	label = GTK_LABEL (GTK_WIDGET (gtk_builder_get_object (builder, "wimax_device_mac_label")));
	gtk_label_set_mnemonic_widget (label, GTK_WIDGET (priv->device_mac));
}

static void
populate_ui (CEPageWimax *self)
{
	CEPageWimaxPrivate *priv = CE_PAGE_WIMAX_GET_PRIVATE (self);
	NMSettingWimax *setting = priv->setting;
	char **mac_list, **iter;
	const GByteArray *s_mac;
	char *s_mac_str;
	char *active_mac = NULL;
	GtkWidget *entry;

	gtk_entry_set_text (priv->name, nm_setting_wimax_get_network_name (setting));
	g_signal_connect_swapped (priv->name, "changed", G_CALLBACK (ce_page_changed), self);

	/* Device MAC address */
	mac_list = ce_page_get_mac_list (CE_PAGE (self));
	s_mac = nm_setting_wimax_get_mac_address (setting);
	s_mac_str = s_mac ? g_strdup_printf ("%02X:%02X:%02X:%02X:%02X:%02X",
	                                     s_mac->data[0], s_mac->data[1], s_mac->data[2],
	                                     s_mac->data[3], s_mac->data[4], s_mac->data[5]):
	                    NULL;

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
	g_strfreev (mac_list);
	g_signal_connect_swapped (priv->device_mac, "changed", G_CALLBACK (ce_page_changed), self);
}

static void
finish_setup (CEPageWimax *self, gpointer unused, GError *error, gpointer user_data)
{
	if (error)
		return;

	populate_ui (self);
}

CEPage *
ce_page_wimax_new (NMConnection *connection,
                   GtkWindow *parent_window,
                   NMClient *client,
                   const char **out_secrets_setting_name,
                   GError **error)
{
	CEPageWimax *self;
	CEPageWimaxPrivate *priv;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	self = CE_PAGE_WIMAX (ce_page_new (CE_TYPE_PAGE_WIMAX,
	                                   connection,
	                                   parent_window,
	                                   client,
	                                   UIDIR "/ce-page-wimax.ui",
	                                   "WimaxPage",
	                                   _("WiMAX")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC,
		                     _("Could not load WiMAX user interface."));
		return NULL;
	}

	wimax_private_init (self);
	priv = CE_PAGE_WIMAX_GET_PRIVATE (self);

	priv->setting = nm_connection_get_setting_wimax (connection);
	if (!priv->setting) {
		priv->setting = NM_SETTING_WIMAX (nm_setting_wimax_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageWimax *self)
{
	CEPageWimaxPrivate *priv = CE_PAGE_WIMAX_GET_PRIVATE (self);
	const char *name;
	GByteArray *device_mac = NULL;
	GtkWidget *entry;

	name = gtk_entry_get_text (priv->name);

	entry = gtk_bin_get_child (GTK_BIN (priv->device_mac));
	if (entry)
		device_mac = ce_page_entry_to_mac (GTK_ENTRY (entry), NULL);

	g_object_set (priv->setting,
	              NM_SETTING_WIMAX_NETWORK_NAME, name,
	              NM_SETTING_WIMAX_MAC_ADDRESS, device_mac,
	              NULL);

	if (device_mac)
		g_byte_array_free (device_mac, TRUE);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageWimax *self = CE_PAGE_WIMAX (page);
	CEPageWimaxPrivate *priv = CE_PAGE_WIMAX_GET_PRIVATE (self);
	const char *name;
	gboolean invalid = FALSE;
	GByteArray *ignore;
	GtkWidget *entry;

	name = gtk_entry_get_text (priv->name);
	if (!*name)
		return FALSE;

	entry = gtk_bin_get_child (GTK_BIN (priv->device_mac));
	if (entry) {
		ignore = ce_page_entry_to_mac (GTK_ENTRY (entry), &invalid);
		if (invalid)
			return FALSE;
		if (ignore)
			g_byte_array_free (ignore, TRUE);
	}

	ui_to_setting (self);
	return TRUE;
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

		if (!NM_IS_DEVICE_WIMAX (dev))
			continue;

		mac = nm_device_wimax_get_hw_address (NM_DEVICE_WIMAX (dev));
		iface = nm_device_get_iface (NM_DEVICE (dev));
		g_string_append_printf (mac_str, "%s (%s),", mac, iface);
	}
	g_string_truncate (mac_str, mac_str->len-1);

	mac_list = g_strsplit (mac_str->str, ",", 0);
	g_string_free (mac_str, TRUE);

	return mac_list;
}

static void
ce_page_wimax_init (CEPageWimax *self)
{
}

static void
ce_page_wimax_class_init (CEPageWimaxClass *wimax_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (wimax_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (wimax_class);

	g_type_class_add_private (object_class, sizeof (CEPageWimaxPrivate));

	/* virtual methods */
	parent_class->validate = validate;
	parent_class->get_mac_list = get_mac_list;
}


void
wimax_connection_new (GtkWindow *parent,
                      const char *detail,
                      NMRemoteSettings *settings,
                      PageNewConnectionResultFunc result_func,
                      gpointer user_data)
{
	NMConnection *connection;
	NMSetting *s_wimax;

	connection = ce_page_new_connection (_("WiMAX connection %d"),
	                                     NM_SETTING_WIMAX_SETTING_NAME,
	                                     TRUE,
	                                     settings,
	                                     user_data);
	s_wimax = nm_setting_wimax_new ();
	nm_connection_add_setting (connection, s_wimax);

	(*result_func) (connection, FALSE, NULL, user_data);
}


