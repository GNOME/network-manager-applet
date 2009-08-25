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

#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-ip6-config.h>
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
#include "page-ip6.h"
#include "page-dsl.h"
#include "page-mobile.h"
#include "page-ppp.h"
#include "page-vpn.h"
#include "ce-polkit-button.h"

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
update_sensitivity (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	gboolean actionable = FALSE, authorized = FALSE, sensitive = FALSE;
	GtkWidget *widget;
	GSList *iter;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));

	/* Can't modify read-only connections; can't modify anything before the
	 * editor is initialized either.
	 */
	if (!nm_setting_connection_get_read_only (s_con) && editor->initialized) {
		if (editor->system_settings_can_modify) {
			actionable = ce_polkit_button_get_actionable (CE_POLKIT_BUTTON (editor->ok_button));
			authorized = actionable && ce_polkit_button_get_authorized (CE_POLKIT_BUTTON (editor->ok_button));
		}

		if (editor->orig_scope == NM_CONNECTION_SCOPE_SYSTEM) {
			/* If the user cannot ever be authorized to change system connections, and
			 * the connection is a system connection, we desensitize the entire dialog.
			 */
			sensitive = actionable;
		} else {
			/* Otherwise, if the connection is originally a user-connection,
			 * then everything is sensitive except possible the system checkbutton,
			 * which will be insensitive if the user has no possibility of
			 * authorizing.
			 */
			sensitive = TRUE;
		}
	}

	gtk_widget_set_sensitive (GTK_WIDGET (editor->system_checkbutton), authorized);

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

	for (iter = editor->pages; iter; iter = g_slist_next (iter)) {
		widget = ce_page_get_page (CE_PAGE (iter->data));
		gtk_widget_set_sensitive (widget, sensitive);
	}
}

static void
connection_editor_validate (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	gboolean valid = FALSE;
	GSList *iter;

	if (!editor->initialized)
		goto done;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);
	if (nm_setting_connection_get_read_only (s_con))
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
	ce_polkit_button_set_master_sensitive (CE_POLKIT_BUTTON (editor->ok_button), valid);
	update_sensitivity (editor);
}

static void
ok_button_actionable_cb (GtkWidget *button,
                         gboolean actionable,
                         NMConnectionEditor *editor)
{
	connection_editor_validate (editor);
}

static void
can_modify_changed_cb (NMRemoteSettingsSystem *settings,
                       GParamSpec *pspec,
                       NMConnectionEditor *editor)
{
	connection_editor_validate (editor);
}

static void
system_checkbutton_toggled_cb (GtkWidget *widget, NMConnectionEditor *editor)
{
	/* Whether the Apply button uses system settings permissions as part of
	 * it's sensitivity determination depends on whether the system checkbutton
	 * is toggled or not.
	 */
	ce_polkit_button_set_use_polkit (CE_POLKIT_BUTTON (editor->ok_button),
	                                 gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));

	connection_editor_validate (editor);
}

static void
nm_connection_editor_init (NMConnectionEditor *editor)
{
	GtkWidget *dialog;

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
                          NMRemoteSettingsSystem *settings,
                          GError **error)
{
	NMConnectionEditor *editor;
	GtkWidget *hbox;
	gboolean sensitive = TRUE, use_polkit = FALSE;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	editor = g_object_new (NM_TYPE_CONNECTION_EDITOR, NULL);
	if (!editor) {
		g_set_error (error, 0, 0, "%s", _("Error creating connection editor dialog."));
		return NULL;
	}

	g_object_get (settings,
	              NM_SETTINGS_SYSTEM_INTERFACE_CAN_MODIFY,
	              &editor->system_settings_can_modify,
	              NULL);
	g_signal_connect (settings,
	                  "notify::" NM_SETTINGS_SYSTEM_INTERFACE_CAN_MODIFY,
	                  G_CALLBACK (can_modify_changed_cb),
	                  editor);

	/* If this is a system connection that we can't ever modify,
	 * set the editor's Apply button always insensitive.
	 */
	if (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_SYSTEM) {
		sensitive = editor->system_settings_can_modify;
		use_polkit = TRUE;
	}

	editor->ok_button = ce_polkit_button_new (_("Apply"),
	                                          _("Save this connection for all users of this machine."),
	                                          _("Apply..."),
	                                          _("Authenticate to save this connection for all users of this machine."),
	                                          GTK_STOCK_APPLY,
	                                          settings,
	                                          NM_SETTINGS_SYSTEM_PERMISSION_CONNECTION_MODIFY);
	ce_polkit_button_set_use_polkit (CE_POLKIT_BUTTON (editor->ok_button), use_polkit);

	g_signal_connect (editor->ok_button, "actionable",
	                  G_CALLBACK (ok_button_actionable_cb), editor);
	g_signal_connect (editor->ok_button, "authorized",
	                  G_CALLBACK (ok_button_actionable_cb), editor);
	hbox = glade_xml_get_widget (editor->xml, "action_area_hbox");
	gtk_box_pack_end (GTK_BOX (hbox), editor->ok_button, TRUE, TRUE, 0);
	gtk_widget_show_all (editor->ok_button);

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

	g_signal_connect (editor->system_checkbutton, "toggled", G_CALLBACK (system_checkbutton_toggled_cb), editor);

	if (editor->orig_scope == NM_CONNECTION_SCOPE_SYSTEM)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->system_checkbutton), TRUE);

	connection_editor_validate (editor);
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
			update_sensitivity (editor);
			return;
		}
	}

	editor->initialized = TRUE;

	populate_connection_ui (editor);

	/* Validate the connection from an idle handler to ensure that stuff like
	 * GtkFileChoosers have had a chance to asynchronously find their files.
	 */
	g_idle_add (idle_validate, editor);
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

	recheck_initialization (editor);
}

static gboolean
add_page (NMConnectionEditor *editor,
          CEPageNewFunc func,
          NMConnection *connection,
          GError **error)
{
	CEPage *page;
	GtkWidget *widget;
	GtkWidget *notebook;
	GtkWidget *label;

	g_return_val_if_fail (editor != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);
	g_return_val_if_fail (connection != NULL, FALSE);

	page = (*func) (connection, GTK_WINDOW (editor->window), error);
	if (!page)
		return FALSE;

	notebook = glade_xml_get_widget (editor->xml, "notebook");
	label = gtk_label_new (ce_page_get_title (page));
	widget = ce_page_get_page (page);
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), widget, label);

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
		if (!add_page (editor, ce_page_ip6_new, connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_WIRELESS_SETTING_NAME)) {
		if (!add_page (editor, ce_page_wireless_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_wireless_security_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip6_new, connection, error))
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

