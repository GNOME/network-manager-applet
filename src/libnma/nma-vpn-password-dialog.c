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
	GtkLabel *message_label;
	GtkLabel *password_label;
	GtkLabel *password_label_secondary;
	GtkLabel *password_label_tertiary;
	GtkEntry *password_entry;
	GtkEntry *password_entry_secondary;
	GtkEntry *password_entry_tertiary;
	char *password_key;
	char *password_key_secondary;
	char *password_key_tertiary;
	GtkCheckButton *show_passwords_checkbox;
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

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	gtk_entry_set_visibility (priv->password_entry, visible);
	gtk_entry_set_visibility (priv->password_entry_secondary, visible);
	gtk_entry_set_visibility (priv->password_entry_tertiary, visible);
}

static void
dispose (GObject *object)
{
	NMAVpnPasswordDialogPrivate *priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (object);

	g_clear_pointer (&priv->password_key, g_free);
	g_clear_pointer (&priv->password_key_secondary, g_free);
	g_clear_pointer (&priv->password_key_tertiary, g_free);

	G_OBJECT_CLASS (nma_vpn_password_dialog_parent_class)->dispose (object);
}

static void
nma_vpn_password_dialog_class_init (NMAVpnPasswordDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	g_type_ensure (NM_TYPE_DEVICE);

	object_class->dispose = dispose;

	gtk_widget_class_set_template_from_resource (widget_class,
	                                             "/org/freedesktop/network-manager-applet/nma-vpn-password-dialog.ui");

	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, message_label);
	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, password_label);
	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, password_label_secondary);
	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, password_label_tertiary);
	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, password_entry);
	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, password_entry_secondary);
	gtk_widget_class_bind_template_child_private (widget_class, NMAVpnPasswordDialog, password_entry_tertiary);
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

	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry)))
		gtk_widget_grab_focus (GTK_WIDGET (priv->password_entry));
	else if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry_secondary)))
		gtk_widget_grab_focus (GTK_WIDGET (priv->password_entry_secondary));
	else if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry_tertiary)))
		gtk_widget_grab_focus (GTK_WIDGET (priv->password_entry_tertiary));
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
		gtk_label_set_text (priv->message_label, message);
		gtk_widget_show (GTK_WIDGET (priv->message_label));
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

#define UI_KEYFILE_GROUP "VPN Plugin UI"

static void
keyfile_add_entry_info (GKeyFile *keyfile,
                        const char *key,
                        const char *value,
                        const char *label,
                        gboolean is_secret,
                        gboolean should_ask)
{
	g_key_file_set_string (keyfile, key, "Value", value);
	g_key_file_set_string (keyfile, key, "Label", label);
	g_key_file_set_boolean (keyfile, key, "IsSecret", is_secret);
	g_key_file_set_boolean (keyfile, key, "ShouldAsk", should_ask);
}

static void
print_external_mode (NMAVpnPasswordDialog *dialog,
                     gboolean should_ask)
{
	NMAVpnPasswordDialogPrivate *priv;
	GKeyFile *keyfile;
	char *data;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	keyfile = g_key_file_new ();

	g_key_file_set_integer (keyfile, UI_KEYFILE_GROUP, "Version", 2);
	g_key_file_set_string (keyfile, UI_KEYFILE_GROUP, "Description", gtk_label_get_text (priv->message_label));
	g_key_file_set_string (keyfile, UI_KEYFILE_GROUP, "Title", gtk_window_get_title (GTK_WINDOW (dialog)));

	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry))) {
		g_return_if_fail (priv->password_key);
		keyfile_add_entry_info (keyfile,
		                        priv->password_key,
		                        gtk_entry_get_text (priv->password_entry),
		                        gtk_label_get_text (priv->password_label),
		                        TRUE,
		                        should_ask);
	}

	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry_secondary))) {
		g_return_if_fail (priv->password_key_secondary);
		keyfile_add_entry_info (keyfile,
		                        priv->password_key_secondary,
		                        gtk_entry_get_text (priv->password_entry_secondary),
		                        gtk_label_get_text (priv->password_label_secondary),
		                        TRUE,
		                        should_ask);
	}

	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry_tertiary))) {
		g_return_if_fail (priv->password_key_tertiary);
		keyfile_add_entry_info (keyfile,
		                        priv->password_key_tertiary,
		                        gtk_entry_get_text (priv->password_entry_tertiary),
		                        gtk_label_get_text (priv->password_label_tertiary),
		                        TRUE,
		                        should_ask);
	}

	data = g_key_file_to_data (keyfile, NULL, NULL);
	fputs (data, stdout);

	g_key_file_unref (keyfile);
	g_free (data);
}

static void
print_secrets (NMAVpnPasswordDialog *dialog)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry))) {
		g_return_if_fail (priv->password_key);
		g_print ("%s\n", priv->password_key);
		g_print ("%s\n", gtk_entry_get_text (priv->password_entry));
	}

	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry_secondary))) {
		g_return_if_fail (priv->password_key_secondary);
		g_print ("%s\n", priv->password_key_secondary);
		g_print ("%s\n", gtk_entry_get_text (priv->password_entry_secondary));
	}

	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry_tertiary))) {
		g_return_if_fail (priv->password_key_tertiary);
		g_print ("%s\n", priv->password_key_tertiary);
		g_print ("%s\n", gtk_entry_get_text (priv->password_entry_tertiary));
	}

	g_print ("\n\n");
}

gboolean
nma_vpn_password_dialog_run_and_print (NMAVpnPasswordDialog *dialog,
                                       gboolean external_ui_mode,
                                       gboolean should_ask)
{
	gboolean success = TRUE;

	if (external_ui_mode) {
		print_external_mode (dialog, should_ask);
	} else {
		if (should_ask)
			success = nma_vpn_password_dialog_run_and_block (dialog);
		if (success)
			print_secrets (dialog);
	}

	return success;
}

void
nma_vpn_password_dialog_set_password (NMAVpnPasswordDialog	*dialog,
                                      const char *password)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gtk_entry_set_text (priv->password_entry, password ? password : "");
}

void
nma_vpn_password_dialog_set_password_secondary (NMAVpnPasswordDialog *dialog,
                                                const char *password_secondary)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gtk_entry_set_text (priv->password_entry_secondary,
	                    password_secondary ? password_secondary : "");
}

void
nma_vpn_password_dialog_set_password_ternary (NMAVpnPasswordDialog *dialog,
                                              const char *password_tertiary)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gtk_entry_set_text (priv->password_entry_tertiary,
	                    password_tertiary ? password_tertiary : "");
}

void
nma_vpn_password_dialog_set_show_password (NMAVpnPasswordDialog *dialog, gboolean show)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gtk_widget_set_visible (GTK_WIDGET (priv->password_label), show);
	gtk_widget_set_visible (GTK_WIDGET (priv->password_entry), show);
}

void
nma_vpn_password_dialog_set_show_password_secondary (NMAVpnPasswordDialog *dialog,
                                                     gboolean show)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gtk_widget_set_visible (GTK_WIDGET (priv->password_label_secondary), show);
	gtk_widget_set_visible (GTK_WIDGET (priv->password_entry_secondary), show);
}

void
nma_vpn_password_dialog_set_show_password_ternary (NMAVpnPasswordDialog *dialog,
                                                   gboolean show)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	gtk_widget_set_visible (GTK_WIDGET (priv->password_label_tertiary), show);
	gtk_widget_set_visible (GTK_WIDGET (priv->password_entry_tertiary), show);
}

void
nma_vpn_password_dialog_focus_password (NMAVpnPasswordDialog *dialog)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry)))
		gtk_widget_grab_focus (GTK_WIDGET (priv->password_entry));
}

void
nma_vpn_password_dialog_focus_password_secondary (NMAVpnPasswordDialog *dialog)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry_secondary)))
		gtk_widget_grab_focus (GTK_WIDGET (priv->password_entry_secondary));
}

void
nma_vpn_password_dialog_focus_password_ternary (NMAVpnPasswordDialog *dialog)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	if (gtk_widget_get_visible (GTK_WIDGET (priv->password_entry_tertiary)))
		gtk_widget_grab_focus (GTK_WIDGET (priv->password_entry_tertiary));
}

const char *
nma_vpn_password_dialog_get_password (NMAVpnPasswordDialog *dialog)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_val_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog), NULL);

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	return gtk_entry_get_text (priv->password_entry);
}

const char *
nma_vpn_password_dialog_get_password_secondary (NMAVpnPasswordDialog *dialog)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_val_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog), NULL);

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	return gtk_entry_get_text (priv->password_entry_secondary);
}

const char *
nma_vpn_password_dialog_get_password_ternary (NMAVpnPasswordDialog *dialog)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_val_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog), NULL);

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);
	return gtk_entry_get_text (priv->password_entry_tertiary);
}

void nma_vpn_password_dialog_set_password_label (NMAVpnPasswordDialog *dialog,
                                                 const char *label)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	gtk_label_set_text_with_mnemonic (priv->password_label, label);
}

void nma_vpn_password_dialog_set_password_secondary_label (NMAVpnPasswordDialog *dialog,
                                                           const char *label)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	gtk_label_set_text_with_mnemonic (priv->password_label_secondary, label);
}

void
nma_vpn_password_dialog_set_password_ternary_label (NMAVpnPasswordDialog *dialog,
                                                    const char *label)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	gtk_label_set_text_with_mnemonic (priv->password_label_tertiary, label);
}

void
nma_vpn_password_dialog_set_password_key (NMAVpnPasswordDialog *dialog,
                                          const char *key)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	g_clear_pointer (&priv->password_key, g_free);
	priv->password_key = g_strdup (key);
}

void
nma_vpn_password_dialog_set_password_secondary_key (NMAVpnPasswordDialog *dialog,
                                                    const char *key)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	g_clear_pointer (&priv->password_key_secondary, g_free);
	priv->password_key_secondary = g_strdup (key);
}

void
nma_vpn_password_dialog_set_password_tertiary_key (NMAVpnPasswordDialog *dialog,
                                                   const char *key)
{
	NMAVpnPasswordDialogPrivate *priv;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (NMA_VPN_IS_PASSWORD_DIALOG (dialog));

	priv = NMA_VPN_PASSWORD_DIALOG_GET_PRIVATE (dialog);

	g_clear_pointer (&priv->password_key_tertiary, g_free);
	priv->password_key_tertiary = g_strdup (key);
}
