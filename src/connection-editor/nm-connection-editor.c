/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
 * Dan Williams <dcbw@redhat.com>
 * Tambet Ingo <tambet@gmail.com>
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
 * (C) Copyright 2007 - 2008 Red Hat, Inc.
 * (C) Copyright 2007 - 2008 Novell, Inc.
 */

#include "config.h"

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtklabel.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>

#ifdef NO_POLKIT_GNOME
#include "polkit-gnome.h"
#else
#include <polkit-gnome/polkit-gnome.h>
#endif

#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-wired.h>
#include <nm-setting-8021x.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>
#include <nm-setting-vpn.h>
#include <nm-setting-pppoe.h>
#include <nm-setting-ppp.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-utils.h>

#include "nm-connection-editor.h"
#include "gconf-helpers.h"
#include "nma-marshal.h"

#include "ce-page.h"
#include "page-wired.h"
#include "page-wired-security.h"
#include "page-wireless.h"
#include "page-wireless-security.h"
#include "page-ip4.h"
#include "page-dsl.h"
#include "page-mobile.h"
#include "page-ppp.h"
#include "page-vpn.h"
#include "polkit-helpers.h"

G_DEFINE_TYPE (NMConnectionEditor, nm_connection_editor, G_TYPE_OBJECT)

enum {
	EDITOR_DONE,
	EDITOR_LAST_SIGNAL
};

static guint editor_signals[EDITOR_LAST_SIGNAL] = { 0 };

static gboolean nm_connection_editor_set_connection (NMConnectionEditor *editor,
                                                     NMConnection *connection,
                                                     GError **error);

static void
nm_connection_editor_update_title (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	const char *id;

	g_return_if_fail (editor != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	id = nm_setting_connection_get_id (s_con);
	if (id && strlen (id)) {
		char *title = g_strdup_printf (_("Editing %s"), id);
		gtk_window_set_title (GTK_WINDOW (editor->window), title);
		g_free (title);
	} else
		gtk_window_set_title (GTK_WINDOW (editor->window), _("Editing un-named connection"));
}

static gboolean
ui_to_setting (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	GtkWidget *widget;
	const char *name;
	gboolean autoconnect = FALSE;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	widget = glade_xml_get_widget (editor->xml, "connection_name");
	name = gtk_entry_get_text (GTK_ENTRY (widget));

	g_object_set (G_OBJECT (s_con), NM_SETTING_CONNECTION_ID, name, NULL);
	nm_connection_editor_update_title (editor);

	if (!name || !strlen (name))
		return FALSE;

	widget = glade_xml_get_widget (editor->xml, "connection_autoconnect");
	autoconnect = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	g_object_set (G_OBJECT (s_con), NM_SETTING_CONNECTION_AUTOCONNECT, autoconnect, NULL);

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (editor->system_checkbutton)))
		nm_connection_set_scope (editor->connection, NM_CONNECTION_SCOPE_SYSTEM);
	else
		nm_connection_set_scope (editor->connection, NM_CONNECTION_SCOPE_USER);

	return TRUE;
}

static void
connection_editor_validate (NMConnectionEditor *editor)
{
	gboolean valid = FALSE;
	GSList *iter;

	if (!editor->initialized)
		goto done;

	if (!ui_to_setting (editor))
		goto done;

	for (iter = editor->pages; iter; iter = g_slist_next (iter)) {
		GError *error = NULL;

		if (!ce_page_validate (CE_PAGE (iter->data), editor->connection, &error)) {
			/* FIXME: use the error to indicate which UI widgets are invalid */
			if (error) {
				g_warning ("Invalid setting %s: %s", CE_PAGE (iter->data)->title, error->message);
				g_error_free (error);
			} else
				g_warning ("Invalid setting %s", CE_PAGE (iter->data)->title);

			goto done;
		}
	}
	valid = TRUE;

done:
	g_object_set (editor->system_gnome_action, "master-sensitive", valid, NULL);
}

static void
system_checkbutton_toggled_cb (GtkWidget *widget, NMConnectionEditor *editor)
{
	gboolean req_privs = FALSE;
	NMSettingConnection *s_con;

	/* If the connection was originally a system connection, obviously
	 * privileges are required to change it.  If it was originally a user
	 * connection, but the user requests that it be changed to a system
	 * connection, privileges are also required.
	 */

	if (editor->orig_scope == NM_CONNECTION_SCOPE_USER) {
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
			req_privs = TRUE;
	} else
		req_privs = TRUE;

	if (req_privs)
		g_object_set (editor->system_gnome_action, "polkit-action", editor->system_action, NULL);
	else
		g_object_set (editor->system_gnome_action, "polkit-action", NULL, NULL);

	/* Can't ever modify read-only connections */
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	if (nm_setting_connection_get_read_only (s_con))
		gtk_widget_set_sensitive (editor->ok_button, FALSE);

	if (editor->initialized)
		connection_editor_validate (editor);
}

static void
set_editor_sensitivity (NMConnectionEditor *editor, gboolean sensitive)
{
	GtkWidget *widget;
	GSList *iter;

	/* Cancel button is always sensitive */
	gtk_widget_set_sensitive (GTK_WIDGET (editor->cancel_button), TRUE);

	widget = glade_xml_get_widget (editor->xml, "connection_name_label");
	gtk_widget_set_sensitive (widget, sensitive);

	widget = glade_xml_get_widget (editor->xml, "connection_name");
	gtk_widget_set_sensitive (widget, sensitive);

	widget = glade_xml_get_widget (editor->xml, "connection_autoconnect");
	gtk_widget_set_sensitive (widget, sensitive);

	widget = glade_xml_get_widget (editor->xml, "connection_name");
	gtk_widget_set_sensitive (widget, sensitive);

	if (editor->system_settings_can_modify)
		gtk_widget_set_sensitive (GTK_WIDGET (editor->system_checkbutton), sensitive);

	for (iter = editor->pages; iter; iter = g_slist_next (iter)) {
		widget = ce_page_get_page (CE_PAGE (iter->data));
		gtk_widget_set_sensitive (widget, sensitive);
	}
}

static void
update_sensitivity (NMConnectionEditor *editor, PolKitResult pk_result)
{
	NMSettingConnection *s_con;
	gboolean denied = FALSE;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	/* Can't ever modify read-only connections */
	if (nm_setting_connection_get_read_only (s_con)) {
		set_editor_sensitivity (editor, FALSE);
		return;
	}

	if (pk_result == POLKIT_RESULT_UNKNOWN)
		pk_result = polkit_gnome_action_get_polkit_result (editor->system_gnome_action);

	if (pk_result == POLKIT_RESULT_NO || pk_result == POLKIT_RESULT_UNKNOWN)
		denied = TRUE;

	switch (editor->orig_scope) {
	case NM_CONNECTION_SCOPE_SYSTEM:
		/* If the user cannot ever be authorized to change system connections, and
		 * the connection is a system connection, we desensitize the entire dialog.
		 */
		set_editor_sensitivity (editor, !denied);
		break;
	default:
		/* If the user cannot ever be authorized to change system connections, and
		 * the connection is a user connection, we desensitize system_checkbutton.
		 */
		set_editor_sensitivity (editor, TRUE);
		if (denied)
			gtk_widget_set_sensitive (GTK_WIDGET (editor->system_checkbutton), FALSE);
		break;
	}
}

static void
system_pk_result_changed_cb (PolKitGnomeAction *gnome_action,
                             PolKitResult result,
                             NMConnectionEditor *editor)
{
	update_sensitivity (editor, result);
}

static void
nm_connection_editor_init (NMConnectionEditor *editor)
{
	GtkWidget *dialog, *hbox;
	const char *auth_label, *auth_tooltip;
	const char *label, *tooltip;

	/* Yes, we mean applet.glade, not nm-connection-editor.glade. The wireless security bits
	   are taken from applet.glade. */
	if (!g_file_test (GLADEDIR "/applet.glade", G_FILE_TEST_EXISTS)) {
		dialog = gtk_message_dialog_new (NULL, 0,
		                                 GTK_MESSAGE_ERROR,
		                                 GTK_BUTTONS_OK,
		                                 "%s",
		                                 _("The connection editor could not find some required resources (the NetworkManager applet glade file was not found)."));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		gtk_main_quit ();
		return;
	}

	editor->xml = glade_xml_new (GLADEDIR "/nm-connection-editor.glade", "nm-connection-editor", NULL);
	if (!editor->xml) {
		dialog = gtk_message_dialog_new (NULL, 0,
		                                 GTK_MESSAGE_ERROR,
		                                 GTK_BUTTONS_OK,
		                                 "%s",
		                                 _("The connection editor could not find some required resources (the glade file was not found)."));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		gtk_main_quit ();
		return;
	}

	editor->window = glade_xml_get_widget (editor->xml, "nm-connection-editor");
	editor->cancel_button = glade_xml_get_widget (editor->xml, "cancel_button");
	editor->system_checkbutton = glade_xml_get_widget (editor->xml, "system_checkbutton");

	editor->system_action = polkit_action_new ();
	polkit_action_set_action_id (editor->system_action, "org.freedesktop.network-manager-settings.system.modify");

	editor->system_gnome_action = polkit_gnome_action_new ("system");

	auth_label = _("Apply...");
	auth_tooltip = _("Authenticate to save this connection for all users of this machine.");
	label = _("Apply");
	tooltip = _("Save this connection for all users of this machine.");
	g_object_set (editor->system_gnome_action,
	              "polkit-action", NULL,

	              "self-blocked-visible",       TRUE,
	              "self-blocked-sensitive",     FALSE,
	              "self-blocked-short-label",   label,
	              "self-blocked-label",         label,
	              "self-blocked-tooltip",       tooltip,
	              "self-blocked-icon-name",     GTK_STOCK_APPLY,

	              "no-visible",       TRUE,
	              "no-sensitive",     FALSE,
	              "no-short-label",   label,
	              "no-label",         label,
	              "no-tooltip",       tooltip,
	              "no-icon-name",     GTK_STOCK_APPLY,

	              "auth-visible",     TRUE,
	              "auth-sensitive",   TRUE,
	              "auth-short-label", auth_label,
	              "auth-label",       auth_label,
	              "auth-tooltip",     auth_tooltip,
	              "auth-icon-name",   GTK_STOCK_DIALOG_AUTHENTICATION,

	              "yes-visible",      TRUE,
	              "yes-sensitive",    TRUE,
	              "yes-short-label",  label,
	              "yes-label",        label,
	              "yes-tooltip",      tooltip,
	              "yes-icon-name",    GTK_STOCK_APPLY,

	              "master-visible",   TRUE,
	              "master-sensitive", TRUE,
	              NULL);
	g_signal_connect (editor->system_gnome_action, "polkit-result-changed",
	                  G_CALLBACK (system_pk_result_changed_cb), editor);


	editor->ok_button = polkit_gnome_action_create_button (editor->system_gnome_action);
	hbox = glade_xml_get_widget (editor->xml, "action_area_hbox");
	gtk_box_pack_end (GTK_BOX (hbox), editor->ok_button, TRUE, TRUE, 0);
}

static void
dispose (GObject *object)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (object);

	g_slist_foreach (editor->pages, (GFunc) g_object_unref, NULL);
	g_slist_free (editor->pages);
	editor->pages = NULL;

	if (editor->connection) {
                g_object_unref (editor->connection);
                editor->connection = NULL;
        }
        if (editor->window) {
                gtk_widget_destroy (editor->window);
                editor->window = NULL;
        }
        if (editor->xml) {
                g_object_unref (editor->xml);
                editor->xml = NULL;
        }

        if (editor->system_action) {
                polkit_action_unref (editor->system_action);
                editor->system_action = NULL;
        }
        if (editor->system_gnome_action) {
                g_object_unref (editor->system_gnome_action);
                editor->system_gnome_action = NULL;
        }

	G_OBJECT_CLASS (nm_connection_editor_parent_class)->dispose (object);
}

static void
nm_connection_editor_class_init (NMConnectionEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/* virtual methods */
	object_class->dispose = dispose;

	/* Signals */
	editor_signals[EDITOR_DONE] =
		g_signal_new ("done",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (NMConnectionEditorClass, done),
		              NULL, NULL,
		              nma_marshal_VOID__INT_POINTER,
		              G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
}

NMConnectionEditor *
nm_connection_editor_new (NMConnection *connection,
                          gboolean system_settings_can_modify,
                          GError **error)
{
	NMConnectionEditor *editor;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	editor = g_object_new (NM_TYPE_CONNECTION_EDITOR, NULL);
	if (!editor) {
		g_set_error (error, 0, 0, "%s", _("Error creating connection editor dialog."));
		return NULL;
	}

	editor->system_settings_can_modify = system_settings_can_modify;
	if (!nm_connection_editor_set_connection (editor, connection, error)) {
		g_object_unref (editor);
		return NULL;
	}

	return editor;
}

NMConnection *
nm_connection_editor_get_connection (NMConnectionEditor *editor)
{
	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), NULL);

	return editor->connection;
}

static void
populate_connection_ui (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	GtkWidget *name;
	GtkWidget *autoconnect;

	name = glade_xml_get_widget (editor->xml, "connection_name");
	autoconnect = glade_xml_get_widget (editor->xml, "connection_autoconnect");

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	if (s_con) {
		const char *id = nm_setting_connection_get_id (s_con);

		gtk_entry_set_text (GTK_ENTRY (name), id);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoconnect),
		                              nm_setting_connection_get_autoconnect (s_con));
	} else {
		gtk_entry_set_text (GTK_ENTRY (name), NULL);
	}

	g_signal_connect_swapped (name, "changed", G_CALLBACK (connection_editor_validate), editor);
	g_signal_connect_swapped (autoconnect, "toggled", G_CALLBACK (connection_editor_validate), editor);

	if (!editor->system_settings_can_modify)
		gtk_widget_set_sensitive (editor->system_checkbutton, FALSE);
	g_signal_connect (editor->system_checkbutton, "toggled", G_CALLBACK (system_checkbutton_toggled_cb), editor);

	if (editor->orig_scope == NM_CONNECTION_SCOPE_SYSTEM)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->system_checkbutton), TRUE);

	update_sensitivity (editor, POLKIT_RESULT_UNKNOWN);
}

static void
page_changed (CEPage *page, gpointer user_data)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (user_data);

	connection_editor_validate (editor);
}

static gboolean
idle_validate (gpointer user_data)
{
	connection_editor_validate (NM_CONNECTION_EDITOR (user_data));
	return FALSE;
}

static void
recheck_initialization (NMConnectionEditor *editor)
{
	GSList *iter;

	/* Check if all pages are initialized; if not, desensitize the editor */
	for (iter = editor->pages; iter; iter = g_slist_next (iter)) {
		if (!ce_page_get_initialized (CE_PAGE (iter->data))) {
			set_editor_sensitivity (editor, FALSE);
			return;
		}
	}

	populate_connection_ui (editor);
	update_sensitivity (editor, POLKIT_RESULT_UNKNOWN);

	editor->initialized = TRUE;

	/* Validate the connection from an idle handler to ensure that stuff like
	 * GtkFileChoosers have had a chance to asynchronously find their files.
	 */
	g_idle_add (idle_validate, editor);
}

static void
really_add_page (NMConnectionEditor *editor, CEPage *page)
{
	GtkWidget *widget;
	GtkWidget *notebook;
	GtkWidget *label;

	notebook = glade_xml_get_widget (editor->xml, "notebook");
	label = gtk_label_new (ce_page_get_title (page));
	widget = ce_page_get_page (page);
	if (widget) {
		gtk_notebook_append_page (GTK_NOTEBOOK (notebook), widget, label);
		g_object_set_data (G_OBJECT (page), "widget-added", GUINT_TO_POINTER (1));
	}
}

static void
page_initialized (CEPage *page, gpointer unused, GError *error, gpointer user_data)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (user_data);

	if (error) {
		gtk_widget_hide (editor->window);
		g_signal_emit (editor, editor_signals[EDITOR_DONE], 0, GTK_RESPONSE_NONE, error);
		return;
	}

	/* If the page didn't get added the first time (because it didn't have secrets
	 * or the widget couldn't be built until the secrets were requested) then do
	 * that here.  This mainly happens for VPN system connections where the 
	 * widget is constructed after the secrets call returns, which could be
	 * a long time after ce_page_vpn_new() is called.
	 */

	if (!g_object_get_data (G_OBJECT (page), "widget-added"))
		really_add_page (editor, page);

	recheck_initialization (editor);
}

static gboolean
add_page (NMConnectionEditor *editor,
          CEPageNewFunc func,
          NMConnection *connection,
          GError **error)
{
	CEPage *page;

	g_return_val_if_fail (editor != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);
	g_return_val_if_fail (connection != NULL, FALSE);

	page = (*func) (connection, GTK_WINDOW (editor->window), error);
	if (!page)
		return FALSE;

	really_add_page (editor, page);

	editor->pages = g_slist_append (editor->pages, page);

	g_signal_connect (page, "changed", G_CALLBACK (page_changed), editor);
	g_signal_connect (page, "initialized", G_CALLBACK (page_initialized), editor);

	return TRUE;
}

static gboolean
nm_connection_editor_set_connection (NMConnectionEditor *editor,
                                     NMConnection *connection,
                                     GError **error)
{
	NMSettingConnection *s_con;
	const char *connection_type;
	gboolean success = FALSE;

	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	/* clean previous connection */
	if (editor->connection)
		g_object_unref (editor->connection);

	editor->connection = g_object_ref (connection);
	editor->orig_scope = nm_connection_get_scope (connection);
	nm_connection_editor_update_title (editor);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	connection_type = nm_setting_connection_get_connection_type (s_con);
	if (!strcmp (connection_type, NM_SETTING_WIRED_SETTING_NAME)) {
		if (!add_page (editor, ce_page_wired_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_wired_security_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_WIRELESS_SETTING_NAME)) {
		if (!add_page (editor, ce_page_wireless_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_wireless_security_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_VPN_SETTING_NAME)) {
		if (!add_page (editor, ce_page_vpn_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_PPPOE_SETTING_NAME)) {
		if (!add_page (editor, ce_page_dsl_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_wired_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ppp_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_GSM_SETTING_NAME) || 
	           !strcmp (connection_type, NM_SETTING_CDMA_SETTING_NAME)) {
		if (!add_page (editor, ce_page_mobile_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ppp_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, connection, error))
			goto out;
	} else {
		g_warning ("Unhandled setting type '%s'", connection_type);
	}

	/* set the UI */
	recheck_initialization (editor);
	success = TRUE;

out:
	return success;
}

void
nm_connection_editor_present (NMConnectionEditor *editor)
{
	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));

	gtk_window_present (GTK_WINDOW (editor->window));
}

static void
cancel_button_clicked_cb (GtkWidget *widget, gpointer user_data)
{
	NMConnectionEditor *self = NM_CONNECTION_EDITOR (user_data);

	gtk_widget_hide (widget);
	g_signal_emit (self, editor_signals[EDITOR_DONE], 0, GTK_RESPONSE_CANCEL, NULL);
}

static void
editor_closed_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	cancel_button_clicked_cb (widget, user_data);
}

static void
ok_button_clicked_cb (GtkWidget *widget, gpointer user_data)
{
	NMConnectionEditor *self = NM_CONNECTION_EDITOR (user_data);

	gtk_widget_hide (widget);
	g_signal_emit (self, editor_signals[EDITOR_DONE], 0, GTK_RESPONSE_OK, NULL);
}

void
nm_connection_editor_run (NMConnectionEditor *self)
{
	g_return_if_fail (NM_IS_CONNECTION_EDITOR (self));

	g_signal_connect (G_OBJECT (self->window), "delete-event",
	                  G_CALLBACK (editor_closed_cb), self);

	g_signal_connect (G_OBJECT (self->ok_button), "clicked",
	                  G_CALLBACK (ok_button_clicked_cb), self);
	g_signal_connect (G_OBJECT (self->cancel_button), "clicked",
	                  G_CALLBACK (cancel_button_clicked_cb), self);

	nm_connection_editor_present (self);
}

void
nm_connection_editor_save_vpn_secrets (NMConnectionEditor *editor)
{
	GSList *iter;

	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));
	g_return_if_fail (nm_connection_get_scope (editor->connection) == NM_CONNECTION_SCOPE_USER);

	for (iter = editor->pages; iter; iter = g_slist_next (iter)) {
		CEPage *page = CE_PAGE (iter->data);

		if (CE_IS_PAGE_VPN (page)) {
			ce_page_vpn_save_secrets (page, editor->connection);
			break;
		}
	}
}

GtkWindow *
nm_connection_editor_get_window (NMConnectionEditor *editor)
{
	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), NULL);

	return GTK_WINDOW (editor->window);
}

