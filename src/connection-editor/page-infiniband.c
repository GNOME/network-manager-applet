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
#include <nm-setting-infiniband.h>
#include <nm-device-infiniband.h>
#include <nm-utils.h>

#include <net/if_arp.h>
#include <linux/if_infiniband.h>

#include "page-infiniband.h"

G_DEFINE_TYPE (CEPageInfiniband, ce_page_infiniband, CE_TYPE_PAGE)

#define CE_PAGE_INFINIBAND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_INFINIBAND, CEPageInfinibandPrivate))

typedef struct {
	NMSettingInfiniband *setting;

#if GTK_CHECK_VERSION(2,24,0)
	GtkComboBoxText *device_mac;  /* Permanent MAC of the device */
#else
	GtkComboBoxEntry *device_mac;
#endif

	GtkComboBox *transport_mode;
	GtkSpinButton *mtu;
} CEPageInfinibandPrivate;

#define TRANSPORT_MODE_DATAGRAM  0
#define TRANSPORT_MODE_CONNECTED 1

static void
infiniband_private_init (CEPageInfiniband *self)
{
	CEPageInfinibandPrivate *priv = CE_PAGE_INFINIBAND_GET_PRIVATE (self);
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

	align = GTK_WIDGET (gtk_builder_get_object (builder, "infiniband_device_mac_alignment"));
	gtk_container_add (GTK_CONTAINER (align), GTK_WIDGET (priv->device_mac));
	gtk_widget_show_all (GTK_WIDGET (priv->device_mac));

	/* Set mnemonic widget for device MAC label */
	label = GTK_LABEL (GTK_WIDGET (gtk_builder_get_object (builder, "infiniband_device_mac_label")));
	gtk_label_set_mnemonic_widget (label, GTK_WIDGET (priv->device_mac));

	priv->transport_mode = GTK_COMBO_BOX (gtk_builder_get_object (builder, "infiniband_mode"));
	priv->mtu = GTK_SPIN_BUTTON (GTK_WIDGET (gtk_builder_get_object (builder, "infiniband_mtu")));
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
populate_ui (CEPageInfiniband *self)
{
	CEPageInfinibandPrivate *priv = CE_PAGE_INFINIBAND_GET_PRIVATE (self);
	NMSettingInfiniband *setting = priv->setting;
	const char *mode;
	int mode_idx = TRANSPORT_MODE_DATAGRAM;
	int mtu_def;
	char **mac_list, **iter;
	const GByteArray *s_mac;
	char *s_mac_str;
	char *active_mac = NULL;
	GtkWidget *entry;

	/* Port */
	mode = nm_setting_infiniband_get_transport_mode (setting);
	if (mode) {
		if (!strcmp (mode, "datagram"))
			mode_idx = TRANSPORT_MODE_DATAGRAM;
		else if (!strcmp (mode, "connected"))
			mode_idx = TRANSPORT_MODE_CONNECTED;
	}
	gtk_combo_box_set_active (priv->transport_mode, mode_idx);

	/* Device MAC address */
	mac_list = ce_page_get_mac_list (CE_PAGE (self));
	s_mac = nm_setting_infiniband_get_mac_address (setting);
	s_mac_str = s_mac ? nm_utils_hwaddr_ntoa (s_mac->data, ARPHRD_INFINIBAND):
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
	g_signal_connect (priv->device_mac, "changed", G_CALLBACK (stuff_changed), self);

	/* MTU */
	mtu_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_INFINIBAND_MTU);
	g_signal_connect (priv->mtu, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (mtu_def));

	gtk_spin_button_set_value (priv->mtu, (gdouble) nm_setting_infiniband_get_mtu (setting));
}

static void
finish_setup (CEPageInfiniband *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPageInfinibandPrivate *priv = CE_PAGE_INFINIBAND_GET_PRIVATE (self);

	if (error)
		return;

	populate_ui (self);

	g_signal_connect (priv->transport_mode, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->mtu, "value-changed", G_CALLBACK (stuff_changed), self);
}

CEPage *
ce_page_infiniband_new (NMConnection *connection,
                        GtkWindow *parent_window,
                        NMClient *client,
                        const char **out_secrets_setting_name,
                        GError **error)
{
	CEPageInfiniband *self;
	CEPageInfinibandPrivate *priv;

	self = CE_PAGE_INFINIBAND (ce_page_new (CE_TYPE_PAGE_INFINIBAND,
	                                        connection,
	                                        parent_window,
	                                        client,
	                                        UIDIR "/ce-page-infiniband.ui",
	                                        "InfinibandPage",
	                                        _("InfiniBand")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC,
		                     _("Could not load InfiniBand user interface."));
		return NULL;
	}

	infiniband_private_init (self);
	priv = CE_PAGE_INFINIBAND_GET_PRIVATE (self);

	priv->setting = nm_connection_get_setting_infiniband (connection);
	if (!priv->setting) {
		priv->setting = NM_SETTING_INFINIBAND (nm_setting_infiniband_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageInfiniband *self)
{
	CEPageInfinibandPrivate *priv = CE_PAGE_INFINIBAND_GET_PRIVATE (self);
	const char *mode;
	GByteArray *device_mac = NULL;
	GtkWidget *entry;

	/* Transport mode */
	if (gtk_combo_box_get_active (priv->transport_mode) == TRANSPORT_MODE_CONNECTED)
		mode = "connected";
	else
		mode = "datagram";

	entry = gtk_bin_get_child (GTK_BIN (priv->device_mac));
	if (entry)
		device_mac = nm_utils_hwaddr_atoba (gtk_entry_get_text (GTK_ENTRY (entry)),
		                                    ARPHRD_INFINIBAND);

	g_object_set (priv->setting,
	              NM_SETTING_INFINIBAND_MAC_ADDRESS, device_mac,
	              NM_SETTING_INFINIBAND_MTU, (guint32) gtk_spin_button_get_value_as_int (priv->mtu),
	              NM_SETTING_INFINIBAND_TRANSPORT_MODE, mode,
	              NULL);

	if (device_mac)
		g_byte_array_free (device_mac, TRUE);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageInfiniband *self = CE_PAGE_INFINIBAND (page);
	CEPageInfinibandPrivate *priv = CE_PAGE_INFINIBAND_GET_PRIVATE (self);
	GtkWidget *entry;
	char buf[INFINIBAND_ALEN];
	const char *hwaddr;

	entry = gtk_bin_get_child (GTK_BIN (priv->device_mac));
	hwaddr = gtk_entry_get_text (GTK_ENTRY (entry));
	if (hwaddr && *hwaddr && !nm_utils_hwaddr_aton (hwaddr, ARPHRD_INFINIBAND, buf))
		return FALSE;

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

		if (!NM_IS_DEVICE_INFINIBAND (dev))
			continue;

		mac = nm_device_infiniband_get_hw_address (NM_DEVICE_INFINIBAND (dev));
		iface = nm_device_get_iface (NM_DEVICE (dev));
		g_string_append_printf (mac_str, "%s (%s),", mac, iface);
	}
	g_string_truncate (mac_str, mac_str->len-1);

	mac_list = g_strsplit (mac_str->str, ",", 0);
	g_string_free (mac_str, TRUE);

	return mac_list;
}

static void
ce_page_infiniband_init (CEPageInfiniband *self)
{
}

static void
ce_page_infiniband_class_init (CEPageInfinibandClass *infiniband_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (infiniband_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (infiniband_class);

	g_type_class_add_private (object_class, sizeof (CEPageInfinibandPrivate));

	/* virtual methods */
	parent_class->validate = validate;
	parent_class->get_mac_list = get_mac_list;
}


void
infiniband_connection_new (GtkWindow *parent,
                           const char *detail,
                           NMRemoteSettings *settings,
                           PageNewConnectionResultFunc result_func,
                           gpointer user_data)
{
	NMConnection *connection;

	connection = ce_page_new_connection (_("InfiniBand connection %d"),
	                                     NM_SETTING_INFINIBAND_SETTING_NAME,
	                                     TRUE,
	                                     settings,
	                                     user_data);
	nm_connection_add_setting (connection, nm_setting_infiniband_new ());

	(*result_func) (connection, FALSE, NULL, user_data);
}

