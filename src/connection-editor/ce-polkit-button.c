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
 * (C) Copyright 2009 Red Hat, Inc.
 */

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "ce-polkit-button.h"

G_DEFINE_TYPE (CEPolkitButton, ce_polkit_button, GTK_TYPE_BUTTON)

#define CE_POLKIT_BUTTON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_POLKIT_BUTTON, CEPolkitButtonPrivate))

typedef struct {
	gboolean disposed;

	char *label;
	char *tooltip;
	char *auth_label;
	char *auth_tooltip;
	gboolean master_sensitive;

	GtkWidget *stock;
	GtkWidget *auth;

	NMRemoteSettingsSystem *settings;
	NMSettingsSystemPermissions permission;
	gboolean use_polkit;
	GSList *perm_calls;
	/* authorized = TRUE if either explicitly authorized or if the action
	 * could be performed if the user successfully authenticated to gain the
	 * authorization.
	 */
	gboolean authorized;

	guint check_id;
} CEPolkitButtonPrivate;

enum {
	ACTIONABLE,
	AUTHORIZED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
update_button (CEPolkitButton *self, gboolean actionable)
{
	CEPolkitButtonPrivate *priv = CE_POLKIT_BUTTON_GET_PRIVATE (self);

	gtk_widget_set_sensitive (GTK_WIDGET (self), actionable);

	if (priv->use_polkit && priv->authorized) {
		gtk_button_set_label (GTK_BUTTON (self), priv->auth_label);
		gtk_widget_set_tooltip_text (GTK_WIDGET (self), priv->auth_tooltip);
		gtk_button_set_image (GTK_BUTTON (self), priv->auth);
	} else {
		gtk_button_set_label (GTK_BUTTON (self), priv->label);
		gtk_widget_set_tooltip_text (GTK_WIDGET (self), priv->tooltip);
		gtk_button_set_image (GTK_BUTTON (self), priv->stock);
	}
}

static void
update_and_emit (CEPolkitButton *self, gboolean old_actionable)
{
	gboolean new_actionable;

	new_actionable = ce_polkit_button_get_actionable (self);
	update_button (self, new_actionable);
	if (new_actionable != old_actionable)
		g_signal_emit (self, signals[ACTIONABLE], 0, new_actionable);
}

void
ce_polkit_button_set_use_polkit (CEPolkitButton *self, gboolean use_polkit)
{
	gboolean old_actionable;

	g_return_if_fail (self != NULL);
	g_return_if_fail (CE_IS_POLKIT_BUTTON (self));

	old_actionable = ce_polkit_button_get_actionable (self);
	CE_POLKIT_BUTTON_GET_PRIVATE (self)->use_polkit = use_polkit;
	update_and_emit (self, old_actionable);
}

void
ce_polkit_button_set_master_sensitive (CEPolkitButton *self, gboolean sensitive)
{
	gboolean old_actionable;

	g_return_if_fail (self != NULL);
	g_return_if_fail (CE_IS_POLKIT_BUTTON (self));

	old_actionable = ce_polkit_button_get_actionable (self);
	CE_POLKIT_BUTTON_GET_PRIVATE (self)->master_sensitive = sensitive;
	update_and_emit (self, old_actionable);
}

gboolean
ce_polkit_button_get_actionable (CEPolkitButton *self)
{
	CEPolkitButtonPrivate *priv;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (CE_IS_POLKIT_BUTTON (self), FALSE);

	priv = CE_POLKIT_BUTTON_GET_PRIVATE (self);

	if (!priv->master_sensitive)
		return FALSE;

	/* If polkit is in-use, the button is only actionable if the operation is
	 * authorized or able to be authorized via user authentication.  If polkit
	 * isn't in-use, the button will always be actionable unless insensitive.
	 */
	return priv->use_polkit ? priv->authorized : TRUE;
}

gboolean
ce_polkit_button_get_authorized (CEPolkitButton *self)
{
	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (CE_IS_POLKIT_BUTTON (self), FALSE);

	return CE_POLKIT_BUTTON_GET_PRIVATE (self)->authorized;
}

typedef struct {
	CEPolkitButton *self;
	gboolean disposed;
} PermInfo;

static void
get_permissions_cb (NMSettingsSystemInterface *settings,
                    NMSettingsSystemPermissions permissions,
                    GError *error,
                    gpointer user_data)
{
	PermInfo *info = user_data;
	CEPolkitButton *self = info->self;
	CEPolkitButtonPrivate *priv;
	gboolean old_actionable, old_authorized;

	/* Response might come when button is already disposed */
	if (info->disposed)
		goto out;

	priv = CE_POLKIT_BUTTON_GET_PRIVATE (info->self);

	priv->perm_calls = g_slist_remove (priv->perm_calls, info);

	old_actionable = ce_polkit_button_get_actionable (self);
	old_authorized = priv->authorized;

	priv->authorized = (permissions & priv->permission);
	if (priv->use_polkit)
		update_and_emit (self, old_actionable);

	if (priv->authorized != old_authorized)
		g_signal_emit (self, signals[AUTHORIZED], 0, priv->authorized);

out:
	g_free (info);
}

static void
check_permissions_cb (NMRemoteSettingsSystem *settings, CEPolkitButton *self)
{
	PermInfo *info;
	CEPolkitButtonPrivate *priv;

	info = g_malloc0 (sizeof (PermInfo));
	info->self = self;

	priv = CE_POLKIT_BUTTON_GET_PRIVATE (info->self);
	priv->perm_calls = g_slist_append (priv->perm_calls, info);

	/* recheck permissions */
	nm_settings_system_interface_get_permissions (NM_SETTINGS_SYSTEM_INTERFACE (settings),
	                                              get_permissions_cb,
	                                              info);
}

GtkWidget *
ce_polkit_button_new (const char *label,
                      const char *tooltip,
                      const char *auth_label,
                      const char *auth_tooltip,
                      const char *stock_icon,
                      NMRemoteSettingsSystem *settings,
                      NMSettingsSystemPermissions permission)
{
	GObject *object;
	CEPolkitButtonPrivate *priv;

	object = g_object_new (CE_TYPE_POLKIT_BUTTON, NULL);
	if (!object)
		return NULL;

	priv = CE_POLKIT_BUTTON_GET_PRIVATE (object);

	priv->label = g_strdup (label);
	priv->tooltip = g_strdup (tooltip);
	priv->auth_label = g_strdup (auth_label);
	priv->auth_tooltip = g_strdup (auth_tooltip);
	priv->permission = permission;
	priv->use_polkit = FALSE;

	priv->settings = g_object_ref (settings);
	priv->check_id = g_signal_connect (settings,
	                                   NM_SETTINGS_SYSTEM_INTERFACE_CHECK_PERMISSIONS,
	                                   G_CALLBACK (check_permissions_cb),
	                                   object);

	priv->stock = gtk_image_new_from_stock (stock_icon, GTK_ICON_SIZE_BUTTON);
	g_object_ref_sink (priv->stock);
	priv->auth = gtk_image_new_from_stock (GTK_STOCK_DIALOG_AUTHENTICATION, GTK_ICON_SIZE_BUTTON);
	g_object_ref_sink (priv->auth);

	update_button (CE_POLKIT_BUTTON (object),
	               ce_polkit_button_get_actionable (CE_POLKIT_BUTTON (object)));

	check_permissions_cb (settings, CE_POLKIT_BUTTON (object));

	return GTK_WIDGET (object);
}

static void
dispose (GObject *object)
{
	CEPolkitButtonPrivate *priv = CE_POLKIT_BUTTON_GET_PRIVATE (object);
	GSList *iter;

	if (priv->disposed == FALSE) {
		priv->disposed = TRUE;

		/* Mark any ongoing permissions calls as disposed */
		for (iter = priv->perm_calls; iter; iter = g_slist_next (iter))
			((PermInfo *) iter->data)->disposed = TRUE;

		if (priv->check_id)
			g_signal_handler_disconnect (priv->settings, priv->check_id);

		g_object_unref (priv->settings);
		g_object_unref (priv->auth);
		g_object_unref (priv->stock);
	}

	G_OBJECT_CLASS (ce_polkit_button_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	CEPolkitButtonPrivate *priv = CE_POLKIT_BUTTON_GET_PRIVATE (object);

	g_free (priv->label);
	g_free (priv->auth_label);
	g_free (priv->tooltip);
	g_free (priv->auth_tooltip);
	g_slist_free (priv->perm_calls);

	G_OBJECT_CLASS (ce_polkit_button_parent_class)->finalize (object);
}

static void
ce_polkit_button_init (CEPolkitButton *self)
{
}

static void
ce_polkit_button_class_init (CEPolkitButtonClass *pb_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (pb_class);

	g_type_class_add_private (object_class, sizeof (CEPolkitButtonPrivate));

	object_class->dispose = dispose;
	object_class->finalize = finalize;

	signals[ACTIONABLE] = g_signal_new ("actionable",
	                                    G_OBJECT_CLASS_TYPE (object_class),
	                                    G_SIGNAL_RUN_FIRST,
	                                    G_STRUCT_OFFSET (CEPolkitButtonClass, actionable),
	                                    NULL, NULL,
	                                    g_cclosure_marshal_VOID__BOOLEAN,
	                                    G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	signals[AUTHORIZED] = g_signal_new ("authorized",
	                                    G_OBJECT_CLASS_TYPE (object_class),
	                                    G_SIGNAL_RUN_FIRST,
	                                    G_STRUCT_OFFSET (CEPolkitButtonClass, authorized),
	                                    NULL, NULL,
	                                    g_cclosure_marshal_VOID__BOOLEAN,
	                                    G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

