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

#include <nm-connection.h>
#include <nm-setting.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkcellrenderertext.h>
#include <gconf/gconf-client.h>
#include "nm-connection-editor.h"
#include "nm-connection-list.h"
#include "gconf-helpers.h"

G_DEFINE_TYPE (NMConnectionList, nm_connection_list, G_TYPE_OBJECT)

static void
add_connection_cb (GtkButton *button, gpointer user_data)
{
	NMConnectionEditor *editor;
	NMConnection *connection = nm_connection_new ();

	editor = nm_connection_editor_new (connection, NM_CONNECTION_EDITOR_PAGE_DEFAULT);
	nm_connection_editor_run_and_close (editor);

	g_object_unref (editor);
	g_object_unref (connection);
}

static void
edit_connection_cb (GtkButton *button, gpointer user_data)
{
	NMConnectionEditor *editor;
	NMConnection *connection;
	GtkTreeSelection *selection;
	GList *selected_rows;
	GtkTreeModel *model = NULL;
	NMConnectionList *list = NM_CONNECTION_LIST (user_data);
	GtkTreeIter iter;

	/* get selected row from the tree view */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list->connection_list));
	if (gtk_tree_selection_count_selected_rows (selection) != 1)
		return;

	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
	if (!selected_rows)
		return;
	
	if (!gtk_tree_model_get_iter (model, &iter, (GtkTreePath *) selected_rows->data))
		goto out;

	gtk_tree_model_get (model, &iter, 1, &connection, -1);
	editor = nm_connection_editor_new (connection, NM_CONNECTION_EDITOR_PAGE_DEFAULT);
	nm_connection_editor_run_and_close (editor);
	g_object_unref (editor);

out:
	/* free memory */
	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);
}

static void
delete_connection_cb (GtkButton *button, gpointer user_data)
{
}

static void
dialog_response_cb (GtkDialog *dialog, guint response, gpointer user_data)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
hash_add_connection_to_list (gpointer key, gpointer value, gpointer user_data)
{
	NMSettingConnection *s_connection;
	GtkTreeIter iter;
	NMConnection *connection = (NMConnection *) value;
	GtkListStore *model = GTK_LIST_STORE (user_data);

	s_connection = (NMSettingConnection *) nm_connection_get_setting (connection, "connection");
	if (!s_connection)
		return;

	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter,
	                    0, s_connection->name,
	                    1, connection,
	                    -1);
}

static void
load_connections (NMConnectionList *list)
{
	GSList *conf_list;

	g_return_if_fail (NM_IS_CONNECTION_LIST (list));

	conf_list = gconf_client_all_dirs (list->client, GCONF_PATH_CONNECTIONS, NULL);
	if (!conf_list) {
		g_warning ("No connections defined");
		return;
	}

	while (conf_list != NULL) {
		NMConnection *connection;
		gchar *dir = (gchar *) conf_list->data;

		connection = nm_gconf_read_connection (list->client, dir);
		if (connection) {
			NMSettingConnection *s_con;

			g_object_set_data_full (G_OBJECT (connection),
							    "gconf-path", 
							    g_strdup (dir),
							    (GDestroyNotify) g_free);

			s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_SETTING_CONNECTION);
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

static void
nm_connection_list_init (NMConnectionList *list)
{
	GtkListStore *model;
	GtkTreeSelection *select;

	list->client = gconf_client_get_default ();

	/* read connections */
	list->connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	load_connections (list);

	/* load GUI */
	list->gui = glade_xml_new (GLADEDIR "/nm-connection-editor.glade", "NMConnectionList", NULL);
	if (!list->gui) {
		g_warning ("Could not load Glade file for connection list");
		return;
	}

	list->dialog = glade_xml_get_widget (list->gui, "NMConnectionList");
	g_signal_connect (G_OBJECT (list->dialog), "response", G_CALLBACK (dialog_response_cb), list);

	list->connection_list = glade_xml_get_widget (list->gui, "connection_list");

	model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_STRING);
	g_hash_table_foreach (list->connections,
	                      (GHFunc) hash_add_connection_to_list,
	                      model);
	gtk_tree_view_set_model (GTK_TREE_VIEW (list->connection_list), GTK_TREE_MODEL (model));
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (list->connection_list),
	                                             -1, "Name", gtk_cell_renderer_text_new (),
	                                             "text", 0,
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

	gtk_widget_show (GTK_WIDGET (list));
}

gint
nm_connection_list_run_and_close (NMConnectionList *list)
{
	gint result;

	g_return_val_if_fail (NM_IS_CONNECTION_LIST (list), GTK_RESPONSE_CANCEL);

	result = gtk_dialog_run (GTK_DIALOG (list->dialog));
	gtk_widget_hide (list->dialog);

	return result;
}
