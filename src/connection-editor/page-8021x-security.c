// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * Copyright 2008 - 2014 Red Hat, Inc.
 */

#include "nm-default.h"

#include <string.h>

#include "page-ethernet.h"
#include "page-8021x-security.h"
#include "nm-connection-editor.h"
#include "nma-ws.h"

G_DEFINE_TYPE (CEPage8021xSecurity, ce_page_8021x_security, CE_TYPE_PAGE)

#define CE_PAGE_8021X_SECURITY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_8021X_SECURITY, CEPage8021xSecurityPrivate))

typedef struct {
	GtkToggleButton *enabled;
	NMAWs *security;
	GtkSizeGroup *group;

	gboolean initial_have_8021x;
} CEPage8021xSecurityPrivate;

static void
stuff_changed (NMAWs *ws, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
enable_toggled (GtkToggleButton *button, gpointer user_data)
{
	CEPage8021xSecurityPrivate *priv = CE_PAGE_8021X_SECURITY_GET_PRIVATE (user_data);
	gboolean active = gtk_toggle_button_get_active (priv->enabled);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->security), active);
	nm_connection_editor_inter_page_set_value (CE_PAGE (user_data)->editor,
	                                           INTER_PAGE_CHANGE_802_1X_ENABLE,
	                                           GINT_TO_POINTER (active));
	ce_page_changed (CE_PAGE (user_data));
}

static void
finish_setup (CEPage8021xSecurity *self, gpointer user_data)
{
	CEPage *parent = CE_PAGE (self);
	CEPage8021xSecurityPrivate *priv = CE_PAGE_8021X_SECURITY_GET_PRIVATE (self);
	GtkWidget *parent_container;
	NMAWs8021x *ws;

	ws = nma_ws_802_1x_new (parent->connection, TRUE, FALSE);
	if (G_IS_INITIALLY_UNOWNED (ws))
		g_object_ref_sink (ws);
	priv->security = NMA_WS (ws);
	nma_ws_add_to_size_group (priv->security, priv->group);

	g_signal_connect (priv->security, "ws-changed", G_CALLBACK (stuff_changed), self);
	parent_container = gtk_widget_get_parent (GTK_WIDGET (priv->security));
	if (parent_container)
		gtk_container_remove (GTK_CONTAINER (parent_container), GTK_WIDGET (priv->security));

	gtk_widget_set_visible (GTK_WIDGET (priv->enabled), TRUE);
	gtk_toggle_button_set_active (priv->enabled, priv->initial_have_8021x);
	g_signal_connect (priv->enabled, "toggled", G_CALLBACK (enable_toggled), self);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->security), priv->initial_have_8021x);

	gtk_box_pack_start (GTK_BOX (parent->page), GTK_WIDGET (priv->enabled), FALSE, TRUE, 12);
	gtk_box_pack_start (GTK_BOX (parent->page), GTK_WIDGET (priv->security), TRUE, TRUE, 0);
	gtk_widget_show (parent->page);
}

CEPage *
ce_page_8021x_security_new (NMConnectionEditor *editor,
                            NMConnection *connection,
                            GtkWindow *parent_window,
                            NMClient *client,
                            const char **out_secrets_setting_name,
                            GError **error)
{
	CEPage8021xSecurity *self;
	CEPage8021xSecurityPrivate *priv;
	CEPage *parent;

	self = CE_PAGE_8021X_SECURITY (ce_page_new (CE_TYPE_PAGE_8021X_SECURITY,
	                                            editor,
	                                            connection,
	                                            parent_window,
	                                            client,
	                                            NULL,
	                                            NULL,
	                                            _("802.1X Security")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Could not load 802.1X Security user interface."));
		return NULL;
	}

	parent = CE_PAGE (self);
	priv = CE_PAGE_8021X_SECURITY_GET_PRIVATE (self);

	parent->page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	g_object_ref_sink (G_OBJECT (parent->page));
	gtk_container_set_border_width (GTK_CONTAINER (parent->page), 6);

	if (nm_connection_get_setting_802_1x (connection))
		priv->initial_have_8021x = TRUE;

	priv->enabled = GTK_TOGGLE_BUTTON (gtk_check_button_new_with_mnemonic (_("Use 802.1_X security for this connection")));

	priv->group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	g_signal_connect (self, CE_PAGE_INITIALIZED, G_CALLBACK (finish_setup), NULL);

	if (priv->initial_have_8021x)
		*out_secrets_setting_name = NM_SETTING_802_1X_SETTING_NAME;

	return CE_PAGE (self);
}

static void
clear_widget_errors (GtkWidget *widget,
                     gpointer   user_data)
{
	if (GTK_IS_CONTAINER (widget)) {
		gtk_container_forall (GTK_CONTAINER (widget),
		                      clear_widget_errors,
		                      NULL);
	} else {
		widget_unset_error (widget);
	}
}

static gboolean
ce_page_validate_v (CEPage *page, NMConnection *connection, GError **error)
{
	CEPage8021xSecurityPrivate *priv = CE_PAGE_8021X_SECURITY_GET_PRIVATE (page);
	gboolean valid = TRUE;

	if (gtk_toggle_button_get_active (priv->enabled)) {
		valid = nma_ws_validate (priv->security, error);
		if (valid) {
			nma_ws_fill_connection (priv->security, connection);
			nm_connection_remove_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY);
		}
	} else {
		gtk_container_forall (GTK_CONTAINER (priv->security),
		                      clear_widget_errors,
		                      NULL);
		nm_connection_remove_setting (connection, NM_TYPE_SETTING_802_1X);
		valid = TRUE;
	}

	return valid;
}

static gboolean
inter_page_change (CEPage *page)
{
	CEPage8021xSecurityPrivate *priv = CE_PAGE_8021X_SECURITY_GET_PRIVATE (page);
	gpointer macsec_mode;

	if (nm_connection_editor_inter_page_get_value (page->editor,
	                                               INTER_PAGE_CHANGE_MACSEC_MODE,
	                                               &macsec_mode)) {
		gtk_toggle_button_set_active (priv->enabled,
		                              GPOINTER_TO_INT (macsec_mode) == NM_SETTING_MACSEC_MODE_EAP);
		enable_toggled (priv->enabled, page);
	}

	return TRUE;
}

static void
ce_page_8021x_security_init (CEPage8021xSecurity *self)
{
}

static void
dispose (GObject *object)
{
	CEPage *parent = CE_PAGE (object);
	CEPage8021xSecurityPrivate *priv = CE_PAGE_8021X_SECURITY_GET_PRIVATE (object);

	g_clear_object (&priv->group);

	gtk_container_remove (GTK_CONTAINER (parent->page), GTK_WIDGET (priv->security));
	g_clear_object (&priv->security);

	G_OBJECT_CLASS (ce_page_8021x_security_parent_class)->dispose (object);
}

static void
ce_page_8021x_security_class_init (CEPage8021xSecurityClass *security_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (security_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (security_class);

	g_type_class_add_private (object_class, sizeof (CEPage8021xSecurityPrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->ce_page_validate_v = ce_page_validate_v;
	parent_class->inter_page_change = inter_page_change;
}
