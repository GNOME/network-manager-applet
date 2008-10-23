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
#include <glib/gi18n.h>

#ifdef NO_POLKIT_GNOME
#include "polkit-06-helpers.h"
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

G_DEFINE_TYPE (NMConnectionEditor, nm_connection_editor, G_TYPE_OBJECT)

enum {
	EDITOR_DONE,
	EDITOR_LAST_SIGNAL
};

static guint editor_signals[EDITOR_LAST_SIGNAL] = { 0 };

static void nm_connection_editor_set_connection (NMConnectionEditor *editor,
                                                 NMConnection *connection);

static void
nm_connection_editor_update_title (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;

	g_return_if_fail (editor != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	if (s_con->id && strlen (s_con->id)) {
		char *title = g_strdup_printf (_("Editing %s"), s_con->id);
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

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (editor->system_button)))
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
	gtk_widget_set_sensitive (editor->ok_button, valid);
}

static void
nm_connection_editor_init (NMConnectionEditor *editor)
{
	GtkWidget *dialog;
	PolKitAction *pk_action;

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
	editor->ok_button = glade_xml_get_widget (editor->xml, "ok_button");
	editor->cancel_button = glade_xml_get_widget (editor->xml, "cancel_button");

	gtk_window_set_default_icon_name ("preferences-system-network");

	editor->pages = NULL;

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.network-manager-settings.system.modify");
	editor->system_action = polkit_gnome_toggle_action_new_default ("system", pk_action,
	                                                                _("Available to everyone..."),
	                                                                _("Available to everyone"));
	polkit_action_unref (pk_action);

	editor->system_button = glade_xml_get_widget (editor->xml, "system_checkbutton");
	gtk_action_connect_proxy (GTK_ACTION (editor->system_action), editor->system_button);
	g_signal_connect_swapped (editor->system_button, "toggled", G_CALLBACK (connection_editor_validate), editor);
}

static void
dispose (GObject *object)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (object);

	gtk_widget_hide (GTK_WIDGET (editor->window));

	g_slist_foreach (editor->pages, (GFunc) g_object_unref, NULL);
	g_slist_free (editor->pages);
	editor->pages = NULL;

	if (editor->connection)
		g_object_unref (editor->connection);

	gtk_widget_destroy (editor->window);
	g_object_unref (editor->xml);

	g_object_unref (editor->system_action);

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
					  g_cclosure_marshal_VOID__INT,
					  G_TYPE_NONE, 1, G_TYPE_INT);
}

NMConnectionEditor *
nm_connection_editor_new (NMConnection *connection)
{
	NMConnectionEditor *editor;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	editor = g_object_new (NM_TYPE_CONNECTION_EDITOR, NULL);
	nm_connection_editor_set_connection (editor, connection);

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
		gtk_entry_set_text (GTK_ENTRY (name), s_con->id);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoconnect), s_con->autoconnect);
	} else {
		gtk_entry_set_text (GTK_ENTRY (name), NULL);
	}

	g_signal_connect_swapped (name, "changed", G_CALLBACK (connection_editor_validate), editor);
	g_signal_connect_swapped (autoconnect, "toggled", G_CALLBACK (connection_editor_validate), editor);
}

static void
page_changed (CEPage *page, gpointer user_data)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (user_data);

	connection_editor_validate (editor);
}

static void
add_page (NMConnectionEditor *editor, CEPage *page)
{
	GtkWidget *widget;
	GtkWidget *notebook;
	GtkWidget *label;

	g_return_if_fail (editor != NULL);
	g_return_if_fail (page != NULL);

	notebook = glade_xml_get_widget (editor->xml, "notebook");
	label = gtk_label_new (ce_page_get_title (page));
	widget = ce_page_get_page (page);
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), widget, label);

	g_signal_connect (page, "changed",
				   G_CALLBACK (page_changed),
				   editor);

	editor->pages = g_slist_append (editor->pages, page);
}

static void
nm_connection_editor_set_connection (NMConnectionEditor *editor, NMConnection *connection)
{
	NMSettingConnection *s_con;

	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));
	g_return_if_fail (NM_IS_CONNECTION (connection));

	/* clean previous connection */
	if (editor->connection)
		g_object_unref (editor->connection);

	editor->connection = g_object_ref (connection);
	if (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_SYSTEM) {
		gtk_action_block_activate_from (GTK_ACTION (editor->system_action), editor->system_button);
		g_signal_handlers_block_by_func (editor->system_button, connection_editor_validate, editor);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->system_button), TRUE);

		g_signal_handlers_unblock_by_func (editor->system_button, connection_editor_validate, editor);
		gtk_action_unblock_activate_from (GTK_ACTION (editor->system_action), editor->system_button);
	}
	nm_connection_editor_update_title (editor);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	if (!strcmp (s_con->type, NM_SETTING_WIRED_SETTING_NAME)) {
		add_page (editor, CE_PAGE (ce_page_wired_new (editor->connection)));
		add_page (editor, CE_PAGE (ce_page_wired_security_new (editor->connection)));
		add_page (editor, CE_PAGE (ce_page_ip4_new (editor->connection)));
	} else if (!strcmp (s_con->type, NM_SETTING_WIRELESS_SETTING_NAME)) {
		add_page (editor, CE_PAGE (ce_page_wireless_new (editor->connection)));
		add_page (editor, CE_PAGE (ce_page_wireless_security_new (editor->connection)));
		add_page (editor, CE_PAGE (ce_page_ip4_new (editor->connection)));
	} else if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME)) {
		add_page (editor, CE_PAGE (ce_page_vpn_new (editor->connection)));
		add_page (editor, CE_PAGE (ce_page_ip4_new (editor->connection)));
	} else if (!strcmp (s_con->type, NM_SETTING_PPPOE_SETTING_NAME)) {
		add_page (editor, CE_PAGE (ce_page_dsl_new (editor->connection)));
		add_page (editor, CE_PAGE (ce_page_wired_new (editor->connection)));
		add_page (editor, CE_PAGE (ce_page_ppp_new (editor->connection)));
	} else if (!strcmp (s_con->type, NM_SETTING_GSM_SETTING_NAME) || 
			 !strcmp (s_con->type, NM_SETTING_CDMA_SETTING_NAME)) {
		add_page (editor, CE_PAGE (ce_page_mobile_new (editor->connection)));
		add_page (editor, CE_PAGE (ce_page_ppp_new (editor->connection)));
	} else {
		g_warning ("Unhandled setting type '%s'", s_con->type);
	}

	/* set the UI */
	populate_connection_ui (editor);

	connection_editor_validate (editor);
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
	g_signal_emit (self, editor_signals[EDITOR_DONE], 0, GTK_RESPONSE_CANCEL);
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
	g_signal_emit (self, editor_signals[EDITOR_DONE], 0, GTK_RESPONSE_OK);
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

