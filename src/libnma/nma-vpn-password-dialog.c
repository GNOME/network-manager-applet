/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* nma-vpn-password-dialog.c - A password prompting dialog widget.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the ree Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 1999, 2000 Eazel, Inc.
 * Copyright (C) 2011 - 2018 Red Hat, Inc.
 *
 * Authors: Ramiro Estrugo <ramiro@eazel.com>
 *          Dan Williams <dcbw@redhat.com>
 *          Lubomir Rintel <lkundrak@v3.sk>
 */

#include "nm-default.h"

#include "nma-vpn-password-dialog.h"

typedef struct {
	GtkWidget *message_label;
	GtkWidget *field_label[NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS];
	GtkWidget *field_entry[NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS];
	gboolean   is_password[NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS];
	GtkWidget *show_passwords_checkbox;
} NMAVpnPasswordDialogPrivate;

G_DEFINE_TYPE_WITH_CODE (NMAVpnPasswordDialog, nma_vpn_password_dialog, GTK_TYPE_DIALOG,
                         G_ADD_PRIVATE (NMAVpnPasswordDialog))


#define NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                                NMA_VPN_TYPE_PASSWORD_DIALOG, \
                                                NMAVpnPasswordDialogPrivate))

/* NMAVpnPasswordDialogClass methods */
static void nma_vpn_password_dialog_class_init (NMAVpnPasswordDialogClass *password_dialog_class);
static void nma_vpn_password_dialog_init       (NMAVpnPasswordDialog      *password_dialog);

/* GtkDialog callbacks */
static void dialog_show_callback (GtkWidget *widget, gpointer callback_data);
static void dialog_close_callback (GtkWidget *widget, gpointer callback_data);

static void
show_passwords_toggled_cb (GtkWidget *widget, gpointer user_data)
{
	NMAVpnPasswordDialog *dialog = NMA_VPN_PASSWORD_DIALOG (user_data);
	NMAVpnPasswordDialogPrivate *priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gboolean visible;
	guint i;

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	for (i = 0; i < NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS; i++) {
		if (priv->is_password[i])
			gtk_entry_set_visibility (GTK_ENTRY (priv->field_entry[i]), visible);
	}
}

static void
nma_vpn_password_dialog_class_init (NMAVpnPasswordDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	char name[256];
	guint i;

	g_type_ensure (NM_TYPE_DEVICE);
	gtk_widget_class_set_template_from_resource (widget_class,
	                                             "/org/freedesktop/network-manager-applet/nma-vpn-password-dialog.ui");

	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, message_label);
	for (i = 0; i < NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS; i++) {
		nm_sprintf_buf (name, "field_label_%u", i);
		gtk_widget_class_bind_template_child_full (widget_class,
		                                           name,
		                                           FALSE,
		                                           G_PRIVATE_OFFSET (NMAVpnPasswordDialog, field_label[i]));
		nm_sprintf_buf (name, "field_entry_%u", i);
		gtk_widget_class_bind_template_child_full (widget_class,
		                                           name,
		                                           FALSE,
		                                           G_PRIVATE_OFFSET (NMAVpnPasswordDialog, field_entry[i]));
	}

	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, show_passwords_checkbox);

	gtk_widget_class_bind_template_callback (widget_class, dialog_close_callback);
	gtk_widget_class_bind_template_callback (widget_class, dialog_show_callback);
	gtk_widget_class_bind_template_callback (widget_class, gtk_window_activate_default);
	gtk_widget_class_bind_template_callback (widget_class, show_passwords_toggled_cb);
}

static void
nma_vpn_password_dialog_init (NMAVpnPasswordDialog *dialog)
{
	gtk_widget_init_template (GTK_WIDGET (dialog));
}

/* GtkDialog callbacks */
static void
dialog_show_callback (GtkWidget *widget, gpointer callback_data)
{
	NMAVpnPasswordDialog *dialog = NMA_VPN_PASSWORD_DIALOG (callback_data);
	NMAVpnPasswordDialogPrivate *priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	guint i;

	for (i = 0; i < NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS; i++) {
		if (gtk_widget_get_visible (priv->field_entry[i])) {
			gtk_widget_grab_focus (priv->field_entry[i]);
			break;
		}
	}
}

static void
dialog_close_callback (GtkWidget *widget, gpointer callback_data)
{
	gtk_widget_hide (widget);
}

/* Public NMAVpnPasswordDialog methods */
GtkWidget *
nma_vpn_password_dialog_new (const char *title,
                             const char *message,
                             const char *password)
{
	GtkWidget *dialog;
	NMAVpnPasswordDialogPrivate *priv;

	dialog = gtk_widget_new (NMA_VPN_TYPE_PASSWORD_DIALOG, "title", title, NULL);
	if (!dialog)
		return NULL;
	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	if (message) {
		gtk_label_set_text (GTK_LABEL (priv->message_label), message);
		gtk_widget_show (priv->message_label);
	}

	nma_vpn_password_dialog_set_password (NMA_VPN_PASSWORD_DIALOG (dialog), password);
	
	return GTK_WIDGET (dialog);
}

gboolean
nma_vpn_password_dialog_run_and_block (NMAVpnPasswordDialog *dialog)
{
	gint button_clicked;

	g_return_val_if_fail (dialog != NULL, FALSE);
	g_return_val_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog), FALSE);

	button_clicked = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_hide (GTK_WIDGET (dialog));

	return button_clicked == GTK_RESPONSE_OK;
}

void
nma_vpn_password_dialog_field_set_show (NMAVpnPasswordDialog *dialog,
                                        guint i,
                                        gboolean show,
                                        gboolean is_password)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));
	g_return_if_fail (i < NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS);

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	priv->is_password[i] = is_password;
	gtk_widget_set_visible (priv->field_label[i], show);
	gtk_widget_set_visible (priv->field_entry[i], show);
	gtk_entry_set_visibility (GTK_ENTRY (priv->field_entry[i]), !is_password);
}

void
nma_vpn_password_dialog_field_focus (NMAVpnPasswordDialog *dialog,
                                     guint i)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));
	g_return_if_fail (i < NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS);

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	if (gtk_widget_get_visible (priv->field_entry[i]))
		gtk_widget_grab_focus (priv->field_entry[i]);

}

void
nma_vpn_password_dialog_field_set_text (NMAVpnPasswordDialog *dialog,
                                        guint i,
                                        const char *text)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));
	g_return_if_fail (i < NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS);

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gtk_entry_set_text (GTK_ENTRY (priv->field_entry[i]), text ?: "");
}

void
nma_vpn_password_dialog_field_set_label (NMAVpnPasswordDialog *dialog,
                                         guint i,
                                         const char *label)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));
	g_return_if_fail (i < NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS);

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->field_label[i]), label);

}

const char *
nma_vpn_password_dialog_field_get_text (NMAVpnPasswordDialog *dialog, guint i)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_val_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog), NULL);
	g_return_val_if_fail (i < NMA_VPN_PASSWORD_DIALOG_NUM_FIELDS, NULL);

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	return gtk_entry_get_text (GTK_ENTRY (priv->field_entry[i]));
}

void
nma_vpn_password_dialog_set_password (NMAVpnPasswordDialog	*dialog,
                                      const char *password)
{

	nma_vpn_password_dialog_field_set_text (dialog, 0, password);
}

void
nma_vpn_password_dialog_set_password_secondary (NMAVpnPasswordDialog *dialog,
                                                const char *password_secondary)
{
	nma_vpn_password_dialog_field_set_text (dialog, 1, password_secondary);
}

void
nma_vpn_password_dialog_set_password_ternary (NMAVpnPasswordDialog *dialog,
                                              const char *password_tertiary)
{
	nma_vpn_password_dialog_field_set_text (dialog, 2, password_tertiary);
}

void
nma_vpn_password_dialog_set_show_password (NMAVpnPasswordDialog *dialog, gboolean show)
{
	nma_vpn_password_dialog_field_set_show (dialog, 0, show, TRUE);
}

void
nma_vpn_password_dialog_set_show_password_secondary (NMAVpnPasswordDialog *dialog,
                                                     gboolean show)
{
	nma_vpn_password_dialog_field_set_show (dialog, 1, show, TRUE);
}

void
nma_vpn_password_dialog_set_show_password_ternary (NMAVpnPasswordDialog *dialog,
                                                   gboolean show)
{
	nma_vpn_password_dialog_field_set_show (dialog, 2, show, TRUE);
}

void
nma_vpn_password_dialog_focus_password (NMAVpnPasswordDialog *dialog)
{
	nma_vpn_password_dialog_field_focus (dialog, 0);
}

void
nma_vpn_password_dialog_focus_password_secondary (NMAVpnPasswordDialog *dialog)
{
	nma_vpn_password_dialog_field_focus (dialog, 1);
}

void
nma_vpn_password_dialog_focus_password_ternary (NMAVpnPasswordDialog *dialog)
{
	nma_vpn_password_dialog_field_focus (dialog, 2);
}

const char *
nma_vpn_password_dialog_get_password (NMAVpnPasswordDialog *dialog)
{
	return nma_vpn_password_dialog_field_get_text (dialog, 0);
}

const char *
nma_vpn_password_dialog_get_password_secondary (NMAVpnPasswordDialog *dialog)
{
	return nma_vpn_password_dialog_field_get_text (dialog, 1);
}

const char *
nma_vpn_password_dialog_get_password_ternary (NMAVpnPasswordDialog *dialog)
{
	return nma_vpn_password_dialog_field_get_text (dialog, 2);
}

void nma_vpn_password_dialog_set_password_label (NMAVpnPasswordDialog *dialog,
                                                 const char *label)
{
	nma_vpn_password_dialog_field_set_label (dialog, 0, label);
}

void nma_vpn_password_dialog_set_password_secondary_label (NMAVpnPasswordDialog *dialog,
                                                           const char *label)
{
	nma_vpn_password_dialog_field_set_label (dialog, 1, label);
}

void
nma_vpn_password_dialog_set_password_ternary_label (NMAVpnPasswordDialog *dialog,
                                                    const char *label)
{
	nma_vpn_password_dialog_field_set_label (dialog, 2, label);
}
