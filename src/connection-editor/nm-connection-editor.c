/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
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
 * (C) Copyright 2004-2005 Red Hat, Inc.
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
dialog_response_cb (GtkDialog *dialog, guint response, gpointer user_data)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
nm_connection_editor_update_title (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;

	g_return_if_fail (editor != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	if (s_con->id && strlen (s_con->id)) {
		char *title = g_strdup_printf (_("Editing %s"), s_con->id);
		gtk_window_set_title (GTK_WINDOW (editor->dialog), title);
		g_free (title);
	} else
		gtk_window_set_title (GTK_WINDOW (editor->dialog), _("Editing un-named connection"));
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

	widget = glade_xml_get_widget (editor->xml, "connection_system");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
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

	editor->xml = glade_xml_new (GLADEDIR "/nm-connection-editor.glade", NULL, NULL);
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

	editor->dialog = glade_xml_get_widget (editor->xml, "NMConnectionEditor");
	g_signal_connect (G_OBJECT (editor->dialog), "response", G_CALLBACK (dialog_response_cb), editor);

	editor->ok_button = glade_xml_get_widget (editor->xml, "ok_button");

	editor->pages = NULL;
}

static void
dispose (GObject *object)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (object);

	gtk_widget_hide (GTK_WIDGET (editor->dialog));

	g_slist_foreach (editor->pages, (GFunc) g_object_unref, NULL);
	g_slist_free (editor->pages);
	editor->pages = NULL;

	if (editor->connection)
		g_object_unref (editor->connection);

	gtk_widget_destroy (editor->dialog);
	g_object_unref (editor->xml);

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
	GtkWidget *system;

	name = glade_xml_get_widget (editor->xml, "connection_name");
	autoconnect = glade_xml_get_widget (editor->xml, "connection_autoconnect");
	system = glade_xml_get_widget (editor->xml, "connection_system");

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (editor->connection, NM_TYPE_SETTING_CONNECTION));
	if (s_con) {
		gtk_entry_set_text (GTK_ENTRY (name), s_con->id);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoconnect), s_con->autoconnect);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (system), nm_connection_get_scope (editor->connection) == NM_CONNECTION_SCOPE_SYSTEM);
	} else {
		gtk_entry_set_text (GTK_ENTRY (name), NULL);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoconnect), FALSE);
	}

	g_signal_connect_swapped (name, "changed",
						 G_CALLBACK (connection_editor_validate), editor);
	g_signal_connect_swapped (autoconnect, "toggled",
						 G_CALLBACK (connection_editor_validate), editor);
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

	gtk_window_present (GTK_WINDOW (editor->dialog));
}

static void
editor_response_cb (GtkDialog *dialog, gint response, gpointer user_data)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (user_data);

	g_signal_emit (editor, editor_signals[EDITOR_DONE], 0, response);
}

static void
editor_close_cb (GtkDialog *dialog, gpointer user_data)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_CLOSE);
}

void
nm_connection_editor_run (NMConnectionEditor *editor)
{
	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));

	g_signal_connect (G_OBJECT (editor->dialog), "response",
	                  G_CALLBACK (editor_response_cb), editor);
	g_signal_connect (G_OBJECT (editor->dialog), "close",
	                  G_CALLBACK (editor_close_cb), editor);

	nm_connection_editor_present (editor);
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

