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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2004-2005 Red Hat, Inc.
 */

#include <string.h>

#include <gtk/gtkbutton.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gconf/gconf-client.h>

#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-connection.h>
#include <nm-setting.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-setting-vpn.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>

#include "nm-connection-editor.h"
#include "nm-connection-list.h"
#include "gconf-helpers.h"

G_DEFINE_TYPE (NMConnectionList, nm_connection_list, G_TYPE_OBJECT)

#define CE_GCONF_PATH_TAG "ce-gconf-path"

#define COL_ID 			0
#define COL_ICON		1
#define COL_CONNECTION	2

static NMConnection *
get_connection_for_selection (NMConnectionList *list,
                              GtkTreeModel **model,
                              GtkTreeIter *iter)
{
	GtkTreeSelection *selection;
	GList *selected_rows;
	NMConnection *connection = NULL;

	g_return_val_if_fail (list != NULL, NULL);
	g_return_val_if_fail (model != NULL, NULL);
	g_return_val_if_fail (*model == NULL, NULL);
	g_return_val_if_fail (iter != NULL, NULL);

	/* get selected row from the tree view */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list->connection_list));
	if (gtk_tree_selection_count_selected_rows (selection) != 1)
		return NULL;

	selected_rows = gtk_tree_selection_get_selected_rows (selection, model);
	if (!selected_rows)
		return NULL;
	
	if (gtk_tree_model_get_iter (*model, iter, (GtkTreePath *) selected_rows->data))
		gtk_tree_model_get (*model, iter, COL_CONNECTION, &connection, -1);

	/* free memory */
	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);

	return connection;
}

static void
add_connection_cb (GtkButton *button, gpointer user_data)
{
	NMConnectionEditor *editor;
	NMConnection *connection = nm_connection_new ();

	editor = nm_connection_editor_new (connection);
	nm_connection_editor_run_and_close (editor);

	g_object_unref (editor);
	g_object_unref (connection);
}

static void
edit_connection_cb (GtkButton *button, gpointer user_data)
{
	NMConnectionEditor *editor;
	NMConnection *connection;
	GtkTreeModel *ignore1 = NULL;
	GtkTreeIter ignore2;

	connection = get_connection_for_selection (NM_CONNECTION_LIST (user_data),
	                                           &ignore1,
	                                           &ignore2);
	g_return_if_fail (connection != NULL);

	editor = nm_connection_editor_new (connection);
	nm_connection_editor_run_and_close (editor);
	g_object_unref (editor);
}

static void
delete_connection_cb (GtkButton *button, gpointer user_data)
{
	NMConnectionList *list = NM_CONNECTION_LIST (user_data);
	NMSettingConnection *s_con;
	NMConnection *connection;
	GtkWidget *dialog;
	gint result;
	const char *dir;
	GError *error = NULL;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;

	connection = get_connection_for_selection (NM_CONNECTION_LIST (user_data),
	                                           &model, &iter);
	g_return_if_fail (connection != NULL);
	g_return_if_fail (model != NULL);

	dir = g_object_get_data (G_OBJECT (connection), CE_GCONF_PATH_TAG);
	g_return_if_fail (dir != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con || !s_con->id)
		return;

	dialog = gtk_message_dialog_new (GTK_WINDOW (list->dialog),
	                                 GTK_DIALOG_DESTROY_WITH_PARENT,
	                                 GTK_MESSAGE_QUESTION,
	                                 GTK_BUTTONS_NONE,
	                                 _("Are you sure you wish to delete the connection %s?"),
	                                 s_con->id);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
	                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                        GTK_STOCK_DELETE, GTK_RESPONSE_YES,
	                        NULL);

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	if (result != GTK_RESPONSE_YES)
		return;

	if (!gconf_client_recursive_unset (list->client, dir, 0, &error)) {
		g_warning ("%s: Failed to completely remove connection '%s': (%d) %s",
		           __func__, s_con->id, error->code, error->message);
		g_error_free (error);
	}

	gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

	if (!g_hash_table_remove (list->connections, dir))
		g_warning ("%s: couldn't remove connection from hash table.", __func__);
}

static void
dialog_response_cb (GtkDialog *dialog, guint response, gpointer user_data)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
hash_add_connection_to_list (gpointer key, gpointer value, gpointer user_data)
{
	NMConnectionList *list = NM_CONNECTION_LIST (user_data);
	NMConnection *connection = (NMConnection *) value;
	NMSettingConnection *s_con;
	GtkTreeIter iter;
	GdkPixbuf *pixbuf = list->unknown_icon;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con)
		return;

	if (!strcmp (s_con->type, NM_SETTING_WIRED_SETTING_NAME))
		pixbuf = list->wired_icon;
	else if (!strcmp (s_con->type, NM_SETTING_WIRELESS_SETTING_NAME))
		pixbuf = list->wireless_icon;
	else if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME))
		pixbuf = list->vpn_icon;
	else if (!strcmp (s_con->type, NM_SETTING_GSM_SETTING_NAME))
		pixbuf = list->wwan_icon;
	else if (!strcmp (s_con->type, NM_SETTING_CDMA_SETTING_NAME))
		pixbuf = list->wwan_icon;

	gtk_list_store_append (list->model, &iter);
	gtk_list_store_set (list->model, &iter,
	                    COL_ID, s_con->id,
	                    COL_ICON, pixbuf,
	                    COL_CONNECTION, connection,
	                    -1);
}

static void
load_connections (NMConnectionList *list)
{
	GSList *conf_list;

	g_return_if_fail (NM_IS_CONNECTION_LIST (list));

	conf_list = nm_gconf_get_all_connections (list->client);
	if (!conf_list) {
		g_warning ("No connections defined");
		return;
	}

	while (conf_list != NULL) {
		NMConnection *connection;
		gchar *dir = (gchar *) conf_list->data;

		connection = nm_gconf_read_connection (list->client, dir);
		if (connection) {
			g_object_set_data_full (G_OBJECT (connection),
							    CE_GCONF_PATH_TAG, 
							    g_strdup (dir),
							    (GDestroyNotify) g_free);

			g_hash_table_insert (list->connections,
			                     g_strdup (dir),
			                     connection);
		}

		conf_list = g_slist_remove (conf_list, dir);
		g_free (dir);
	}
	g_slist_free (conf_list);
}

static void
list_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	NMConnectionList *list = NM_CONNECTION_LIST (user_data);
	GtkTreeIter iter;
	GtkTreeModel *model;

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_widget_set_sensitive (GTK_WIDGET (list->edit_button), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (list->delete_button), TRUE);
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (list->edit_button), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (list->delete_button), FALSE);
	}
}

#define ICON_LOAD(x, y)	\
	{ \
		x = gtk_icon_theme_load_icon (list->icon_theme, y, 16, 0, &error); \
		if (x == NULL) { \
			g_warning ("Icon %s missing: %s", y, error->message); \
			g_error_free (error); \
			return; \
		} \
	}

static void
nm_connection_list_init (NMConnectionList *list)
{
	GtkTreeSelection *select;
	GError *error = NULL;

	/* load GUI */
	list->gui = glade_xml_new (GLADEDIR "/nm-connection-editor.glade", "NMConnectionList", NULL);
	if (!list->gui) {
		g_warning ("Could not load Glade file for connection list");
		return;
	}

	list->icon_theme = gtk_icon_theme_get_for_screen (gdk_screen_get_default ());

	/* Load icons */
	ICON_LOAD(list->wired_icon, "nm-device-wired");
	ICON_LOAD(list->wireless_icon, "nm-device-wireless");
	ICON_LOAD(list->wwan_icon, "nm-device-wwan");
	ICON_LOAD(list->vpn_icon, "lock");
	ICON_LOAD(list->unknown_icon, "nm-no-connection");

	list->client = gconf_client_get_default ();

	/* read connections */
	list->connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	load_connections (list);

	list->dialog = glade_xml_get_widget (list->gui, "NMConnectionList");
	g_signal_connect (G_OBJECT (list->dialog), "response", G_CALLBACK (dialog_response_cb), list);

	list->connection_list = glade_xml_get_widget (list->gui, "connection_list");

	list->model = gtk_list_store_new (3, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_OBJECT);
	g_hash_table_foreach (list->connections,
	                      (GHFunc) hash_add_connection_to_list,
	                      list);
	gtk_tree_view_set_model (GTK_TREE_VIEW (list->connection_list), GTK_TREE_MODEL (list->model));

	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (list->connection_list),
	                                             -1, "Name", gtk_cell_renderer_text_new (),
	                                             "text", COL_ID,
	                                             NULL);

	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (list->connection_list),
	                                             -1, "Type", gtk_cell_renderer_pixbuf_new (),
	                                             "pixbuf", COL_ICON,
	                                             NULL);

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (list->connection_list));
	gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
	g_signal_connect (G_OBJECT (select),
	                  "changed",
	                  G_CALLBACK (list_selection_changed_cb),
	                  list);

	/* buttons */
	list->add_button = glade_xml_get_widget (list->gui, "add_connection_button");
	g_signal_connect (G_OBJECT (list->add_button), "clicked", G_CALLBACK (add_connection_cb), list);

	list->edit_button = glade_xml_get_widget (list->gui, "edit_connection_button");
	g_signal_connect (G_OBJECT (list->edit_button), "clicked", G_CALLBACK (edit_connection_cb), list);

	list->delete_button = glade_xml_get_widget (list->gui, "delete_connection_button");
	g_signal_connect (G_OBJECT (list->delete_button), "clicked", G_CALLBACK (delete_connection_cb), list);
}

static void
nm_connection_list_finalize (GObject *object)
{
	NMConnectionList *list = NM_CONNECTION_LIST (object);

	g_object_unref (list->wired_icon);
	g_object_unref (list->wireless_icon);
	g_object_unref (list->wwan_icon);
	g_object_unref (list->vpn_icon);
	g_object_unref (list->unknown_icon);

	gtk_widget_destroy (list->dialog);
	g_object_unref (list->gui);
	g_hash_table_destroy (list->connections);
	g_object_unref (list->client);

	G_OBJECT_CLASS (nm_connection_list_parent_class)->finalize (object);
}

static void
nm_connection_list_class_init (NMConnectionListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/* virtual methods */
	object_class->finalize = nm_connection_list_finalize;
}

NMConnectionList *
nm_connection_list_new (void)
{
	NMConnectionList *list;

	list = g_object_new (NM_TYPE_CONNECTION_LIST, NULL);

	return list;
}

void
nm_connection_list_show (NMConnectionList *list)
{
	g_return_if_fail (NM_IS_CONNECTION_LIST (list));

	gtk_widget_show (GTK_WIDGET (list->dialog));
}

gint
nm_connection_list_run_and_close (NMConnectionList *list)
{
	gint result;

	g_return_val_if_fail (NM_IS_CONNECTION_LIST (list), GTK_RESPONSE_CANCEL);

	result = gtk_dialog_run (GTK_DIALOG (list->dialog));
	gtk_widget_hide (GTK_WIDGET (list->dialog));

	return result;
}
