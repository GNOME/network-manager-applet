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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>

#define NM_VPN_API_SUBJECT_TO_CHANGE
#include <nm-vpn-plugin-ui-interface.h>

#include "page-vpn.h"
#include "nm-connection-editor.h"
#include "vpn-helpers.h"

G_DEFINE_TYPE (CEPageVpn, ce_page_vpn, CE_TYPE_PAGE)

#define CE_PAGE_VPN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_VPN, CEPageVpnPrivate))

typedef struct {
	NMSettingVPN *setting;

	char *service_type;

	NMVpnPluginUiInterface *plugin;
	NMVpnPluginUiWidgetInterface *ui;

	gboolean disposed;
} CEPageVpnPrivate;

static void
vpn_plugin_changed_cb (NMVpnPluginUiInterface *plugin, CEPageVpn *self)
{
	ce_page_changed (CE_PAGE (self));
}

static void
finish_setup (CEPageVpn *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPage *parent = CE_PAGE (self);
	CEPageVpnPrivate *priv = CE_PAGE_VPN_GET_PRIVATE (self);
	GError *vpn_error = NULL;

	if (error)
		return;

	g_return_if_fail (priv->plugin != NULL);

	priv->ui = nm_vpn_plugin_ui_interface_ui_factory (priv->plugin, parent->connection, &vpn_error);
	if (!priv->ui) {
		g_warning ("Could not load VPN user interface for service '%s': %s.",
		           priv->service_type,
		           (vpn_error && vpn_error->message) ? vpn_error->message : "(unknown)");
		g_error_free (vpn_error);
		return;
	}
	g_signal_connect (priv->ui, "changed", G_CALLBACK (vpn_plugin_changed_cb), self);

	parent->page = GTK_WIDGET (nm_vpn_plugin_ui_widget_interface_get_widget (priv->ui));
	if (!parent->page) {
		g_warning ("Could not load VPN user interface for service '%s'.", priv->service_type);
		return;
	}
	g_object_ref_sink (parent->page);
	gtk_widget_show_all (parent->page);
}

CEPage *
ce_page_vpn_new (NMConnection *connection,
                 GtkWindow *parent_window,
                 const char **out_secrets_setting_name,
                 GError **error)
{
	CEPageVpn *self;
	CEPageVpnPrivate *priv;
	const char *service_type;

	self = CE_PAGE_VPN (ce_page_new (CE_TYPE_PAGE_VPN,
	                                 connection,
	                                 parent_window,
	                                 NULL,
	                                 NULL,
	                                 _("VPN")));
	if (!self) {
		g_set_error_literal (error, 0, 0, _("Could not load VPN user interface."));
		return NULL;
	}

	priv = CE_PAGE_VPN_GET_PRIVATE (self);

	priv->setting = (NMSettingVPN *) nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN);
	g_assert (priv->setting);

	service_type = nm_setting_vpn_get_service_type (priv->setting);
	g_assert (service_type);
	priv->service_type = g_strdup (service_type);

	priv->plugin = vpn_get_plugin_by_service (service_type);
	if (!priv->plugin) {
		g_set_error (error, 0, 0, _("Could not find VPN plugin service for '%s'."), service_type);
		g_object_unref (self);
		return NULL;
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	*out_secrets_setting_name = NM_SETTING_VPN_SETTING_NAME;

	return CE_PAGE (self);
}

gboolean
ce_page_vpn_save_secrets (CEPage *page, NMConnection *connection)
{
	CEPageVpn *self = CE_PAGE_VPN (page);
	CEPageVpnPrivate *priv = CE_PAGE_VPN_GET_PRIVATE (self);
	GError *error = NULL;
	gboolean success = FALSE;

	success = nm_vpn_plugin_ui_widget_interface_save_secrets (priv->ui, connection, &error);
	if (!success) {
		g_warning ("%s: couldn't save VPN secrets: (%d) %s", __func__,
		           error ? error->code : -1, error ? error->message : "unknown");
		if (error)
			g_error_free (error);
	}

	return success;
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

	g_free (priv->service_type);

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


void
vpn_connection_new (GtkWindow *parent,
                    PageNewConnectionResultFunc result_func,
                    PageGetConnectionsFunc get_connections_func,
                    gpointer user_data)
{
	char *service = NULL;
	NMConnection *connection;
	NMSetting *s_vpn;

	service = vpn_ask_connection_type (parent);
	if (!service) {
		(*result_func) (NULL, TRUE, NULL, user_data);
		return;
	}

	connection = ce_page_new_connection (_("VPN connection %d"),
	                                     NM_SETTING_VPN_SETTING_NAME,
	                                     FALSE,
	                                     get_connections_func,
	                                     user_data);
	s_vpn = nm_setting_vpn_new ();
	g_object_set (s_vpn, NM_SETTING_VPN_SERVICE_TYPE, service, NULL);
	g_free (service);
	nm_connection_add_setting (connection, s_vpn);

	(*result_func) (connection, FALSE, NULL, user_data);
}


