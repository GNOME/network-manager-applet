/* NetworkManager bssid editor dialog -- Edits bssids on wireless access points
 *
 * Calvin Gaisford <cgaisford@novell.com>
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
 * (C) Copyright 2006 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomeui/libgnomeui.h>

#if !GTK_CHECK_VERSION(2,6,0)
#include <gnome.h>
#endif

#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <NetworkManager.h>

#include "bssid-editor-dialog.h"

#define BSSID_DIALOG		"bssids_dialog"
#define BSSID_EDITOR		"bssid_editor"
#define BSSID_TREEVIEW		"bssids_treeview"
#define BSSID_ADD			"add_button"
#define BSSID_REMOVE		"remove_button"
#define BSSID_EDIT			"edit_button"
#define BSSID_EDITOR_DIALOG "bssid_editor"
#define BSSID_EDITOR_ENTRY	"bssid_entry"

typedef struct _bssid_editor_data
{
	char		*glade_file;
	GladeXML	*glade_xml;
	GtkWidget	*dialog;
	GtkWidget	*treeview;
	GtkWidget	*add_button;
	GtkWidget	*remove_button;
	GtkWidget	*edit_button;
} BED_DATA; //bssid_editor_dialog

static void
edit_button_clicked_cb (GtkButton *button, gpointer data)
{
	GtkWidget			*dialog;
	GtkWidget			*bssid_entry;
	BED_DATA			*bed_data;
	GladeXML			*glade_xml;
	GtkTreeSelection	*selection;
	GtkTreeIter			iter;
	GtkTreeIter			childIter;
	GtkTreeModel		*model;
	GtkListStore		*store;
	gchar				*bssid_value = NULL;

	bed_data = data;

	selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (bed_data->treeview));

	if(!gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

	gtk_tree_model_get (model, &iter, 0, &bssid_value, -1);
	if(bssid_value == NULL)
		return;

	glade_xml = glade_xml_new (bed_data->glade_file, BSSID_EDITOR_DIALOG, NULL);

	dialog = glade_xml_get_widget (glade_xml, BSSID_EDITOR_DIALOG);

	bssid_entry = glade_xml_get_widget( glade_xml, 
			BSSID_EDITOR_ENTRY);

	gtk_window_set_transient_for(GTK_WINDOW(dialog), 
			GTK_WINDOW(bed_data->dialog));

	gtk_entry_set_text(GTK_ENTRY(bssid_entry), bssid_value);

	gint result = gtk_dialog_run (GTK_DIALOG(dialog));

	if(result == GTK_RESPONSE_OK)
	{
		gchar *new_bssid;

		new_bssid = g_strdup(gtk_entry_get_text(GTK_ENTRY(bssid_entry)));

		store = GTK_LIST_STORE(gtk_tree_model_filter_get_model(
					GTK_TREE_MODEL_FILTER(model)));

		gtk_tree_model_filter_convert_iter_to_child_iter(
				GTK_TREE_MODEL_FILTER(model),
				&childIter,
				&iter);

		gtk_list_store_set ( store, &childIter, 0, new_bssid, -1);
	}

	gtk_widget_destroy(dialog);
	g_object_unref (glade_xml);
}

static void
remove_button_clicked_cb(GtkButton *button, gpointer data)
{
	GtkTreeSelection	*selection;
	GtkTreeIter			iter;
	GtkTreeIter			childIter;
	GtkTreeModel		*model;
	BED_DATA			*bed_data;
	GtkListStore		*store;

	bed_data = data;

	selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (bed_data->treeview));

	if(!gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

	store = GTK_LIST_STORE(gtk_tree_model_filter_get_model(
				GTK_TREE_MODEL_FILTER(model)));

	gtk_tree_model_filter_convert_iter_to_child_iter(
			GTK_TREE_MODEL_FILTER(model),
			&childIter,
			&iter);

	gtk_list_store_remove ( store, &childIter);
}

static void
add_button_clicked_cb(GtkButton *button, gpointer data)
{
	GtkWidget			*dialog;
	GtkWidget			*bssid_entry;
	BED_DATA			*bed_data;
	GladeXML			*glade_xml;

	bed_data = data;

	glade_xml = glade_xml_new (bed_data->glade_file, BSSID_EDITOR_DIALOG, NULL);

	dialog = glade_xml_get_widget (glade_xml, BSSID_EDITOR_DIALOG);

	bssid_entry = glade_xml_get_widget( glade_xml, 
			BSSID_EDITOR_ENTRY);

	gtk_window_set_transient_for(GTK_WINDOW(dialog), 
			GTK_WINDOW(bed_data->dialog));

	gint result = gtk_dialog_run (GTK_DIALOG(dialog));

	if(result == GTK_RESPONSE_OK)
	{
		GtkTreeSelection	*selection;
		GtkTreeIter			iter;
		GtkTreeIter			childIter;
		GtkTreeModel		*model;
		GtkListStore		*store;
		gchar *new_bssid;

		new_bssid = g_strdup(gtk_entry_get_text(GTK_ENTRY(bssid_entry)));

		selection = gtk_tree_view_get_selection (
				GTK_TREE_VIEW (bed_data->treeview));

		gtk_tree_selection_get_selected (selection, &model, &iter);

		store = GTK_LIST_STORE(gtk_tree_model_filter_get_model(
					GTK_TREE_MODEL_FILTER(model)));

		gtk_list_store_append (store, &childIter);
		gtk_list_store_set (store, &childIter, 
				0, new_bssid, 
				-1);

		gtk_tree_model_filter_convert_child_iter_to_iter(
				GTK_TREE_MODEL_FILTER(model),
				&iter,
				&childIter);

		gtk_tree_selection_select_iter(selection, &iter);
	}

	gtk_widget_destroy(dialog);
	g_object_unref (glade_xml);

}

static void
selection_changed_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeIter			iter;
	GtkTreeModel		*model;
	BED_DATA			*bed_data;
	gboolean			enable;

	bed_data = data;

	enable = gtk_tree_selection_get_selected (selection, &model, &iter);

	gtk_widget_set_sensitive(bed_data->remove_button, enable);
	gtk_widget_set_sensitive(bed_data->edit_button, enable);
}

gboolean
run_bssid_editor (const char *glade_file, GtkWidget *parent, const char *gconf_dir)
{
	GtkTreeSelection	*select;
	GConfClient			*gconf_client;
	GSList				*bssids;
	GtkListStore		*store;
	GtkTreeViewColumn	*column;
	GtkCellRenderer		*renderer;
	GtkTreeModel		*filter;
	GError				*err = NULL;
	BED_DATA			*bed_data;
	gboolean			rc = FALSE;

	bed_data = g_new0 (BED_DATA, 1);
	bed_data->glade_file = g_strdup (glade_file);

	// load the glade file
	bed_data->glade_xml = glade_xml_new (glade_file, BSSID_DIALOG, NULL);

	bed_data->dialog = glade_xml_get_widget (bed_data->glade_xml, BSSID_DIALOG);

	bed_data->treeview = glade_xml_get_widget( bed_data->glade_xml, 
			BSSID_TREEVIEW);
	bed_data->add_button = glade_xml_get_widget( bed_data->glade_xml, 
			BSSID_ADD);
	bed_data->remove_button = glade_xml_get_widget( bed_data->glade_xml, 
			BSSID_REMOVE);
	bed_data->edit_button = glade_xml_get_widget( bed_data->glade_xml, 
			BSSID_EDIT);

	select = gtk_tree_view_get_selection(GTK_TREE_VIEW (bed_data->treeview));
	gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

	g_signal_connect (G_OBJECT (select), "changed",
			G_CALLBACK (selection_changed_cb), bed_data);
	g_signal_connect (G_OBJECT (bed_data->add_button), "clicked",
			G_CALLBACK (add_button_clicked_cb), bed_data);
	g_signal_connect (G_OBJECT (bed_data->remove_button), "clicked",
			G_CALLBACK (remove_button_clicked_cb), bed_data);
	g_signal_connect (G_OBJECT (bed_data->edit_button), "clicked",
			G_CALLBACK (edit_button_clicked_cb), bed_data);

	store = gtk_list_store_new (1, G_TYPE_STRING);

	gconf_client = gconf_client_get_default();

	// Get the bssids
	bssids = gconf_client_get_list(gconf_client, gconf_dir, 
			GCONF_VALUE_STRING, &err);
	if(err == 0)
	{
		gchar	*bssid = NULL; 

		while(bssids)
		{
			GtkTreeIter			iter;

			bssid = g_strdup(bssids->data);

			gtk_list_store_append (store, &iter);

			gtk_list_store_set (store, &iter, 0, bssid, -1);

			g_free(bssids->data);

			bssids = g_slist_delete_link (bssids, bssids);
		}
	}

	filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (store), NULL);

	gtk_tree_view_set_model (GTK_TREE_VIEW (bed_data->treeview), filter);

	g_object_unref (store);
	g_object_unref (filter);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
			"markup", 0, NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (bed_data->treeview), column);
	gtk_tree_view_column_set_sort_column_id (column, 0);


	gtk_window_set_transient_for(GTK_WINDOW(bed_data->dialog), 
			GTK_WINDOW(parent));

	gint result = gtk_dialog_run (GTK_DIALOG (bed_data->dialog));

	if(result == GTK_RESPONSE_OK)
	{
		GtkTreeIter			iter;
		GtkTreeModel		*model;
		gboolean			valid;
		gchar				*new_bssid;
		GSList				*bssid_list;

		model = gtk_tree_view_get_model(GTK_TREE_VIEW(bed_data->treeview));

		valid = gtk_tree_model_get_iter_first(model, &iter);

		bssid_list = NULL;

		while(valid)
		{
			gtk_tree_model_get(model, &iter, 0, &new_bssid, -1);

			bssid_list = g_slist_append(bssid_list, new_bssid);

			valid = gtk_tree_model_iter_next(model, &iter);
		}

		err = NULL;

		if(bssid_list)
		{
			gconf_client_set_list(gconf_client, gconf_dir, 
					GCONF_VALUE_STRING, bssid_list, NULL);
		}
		else
		{
			gconf_client_unset(gconf_client, gconf_dir, NULL);
		}

		gconf_client_suggest_sync(gconf_client, NULL);
		rc = TRUE;
	}

	gtk_widget_destroy(bed_data->dialog);
	g_object_unref (bed_data->glade_xml);
	g_free (bed_data->glade_file);

	g_free (bed_data);

	g_object_unref(gconf_client);

	return rc;
}
