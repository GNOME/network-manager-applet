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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>
#include <nm-setting-vpn-properties.h>

#define NM_VPN_API_SUBJECT_TO_CHANGE
#include <nm-vpn-plugin-ui-interface.h>

#include "page-vpn.h"
#include "nm-connection-editor.h"
#include "vpn-helpers.h"

G_DEFINE_TYPE (CEPageVpn, ce_page_vpn, CE_TYPE_PAGE)

#define CE_PAGE_VPN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_VPN, CEPageVpnPrivate))

typedef struct {
	NMSettingVPN *setting;

	NMVpnPluginUiWidgetInterface *ui;

	gboolean disposed;
} CEPageVpnPrivate;

static void
vpn_plugin_changed_cb (NMVpnPluginUiInterface *plugin, CEPageVpn *self)
{
	ce_page_changed (CE_PAGE (self));
}

CEPageVpn *
ce_page_vpn_new (NMConnection *connection)
{
	CEPageVpn *self;
	CEPageVpnPrivate *priv;
	CEPage *parent;
	GError *error = NULL;
	NMVpnPluginUiInterface *plugin;

	self = CE_PAGE_VPN (g_object_new (CE_TYPE_PAGE_VPN, NULL));
	parent = CE_PAGE (self);
	priv = CE_PAGE_VPN_GET_PRIVATE (self);

	parent->title = g_strdup (_("VPN"));

	priv->setting = (NMSettingVPN *) nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN);
	g_assert (priv->setting);
	g_assert (priv->setting->service_type);

	plugin = vpn_get_plugin_by_service (priv->setting->service_type);
	if (!plugin) {
		g_warning ("%s: couldn't find VPN plugin for service '%s'!",
		           __func__, priv->setting->service_type);
		g_object_unref (self);
		return NULL;
	}

	priv->ui = nm_vpn_plugin_ui_interface_ui_factory (plugin, connection, &error);
	if (!priv->ui) {
		g_warning ("%s: couldn't create VPN UI for service '%s': %s",
		           __func__, priv->setting->service_type, error->message);
		g_error_free (error);
		g_object_unref (self);
		return NULL;
	}
	g_signal_connect (priv->ui, "changed", G_CALLBACK (vpn_plugin_changed_cb), self);

	parent->page = GTK_WIDGET (nm_vpn_plugin_ui_widget_interface_get_widget (priv->ui));
	if (!parent->page) {
		g_warning ("%s: Couldn't load vpn page from the plugin.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);
	gtk_widget_show_all (parent->page);

	return self;
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageVpn *self = CE_PAGE_VPN (page);
	CEPageVpnPrivate *priv = CE_PAGE_VPN_GET_PRIVATE (self);

	return nm_vpn_plugin_ui_widget_interface_update_connection (priv->ui, connection, error);
}

static void
ce_page_vpn_init (CEPageVpn *self)
{
}

static void
dispose (GObject *object)
{
	CEPageVpnPrivate *priv = CE_PAGE_VPN_GET_PRIVATE (object);

	if (priv->disposed)
		return;

	priv->disposed = TRUE;

	if (priv->ui)
		g_object_unref (priv->ui);

	G_OBJECT_CLASS (ce_page_vpn_parent_class)->dispose (object);
}

static void
ce_page_vpn_class_init (CEPageVpnClass *vpn_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (vpn_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (vpn_class);

	g_type_class_add_private (object_class, sizeof (CEPageVpnPrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
}
