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
#include <nm-setting-pppoe.h>

#include "nm-connection-editor.h"
#include "nm-connection-list.h"
#include "gconf-helpers.h"

G_DEFINE_TYPE (NMConnectionList, nm_connection_list, G_TYPE_OBJECT)

enum {
	LIST_DONE,
	LIST_LAST_SIGNAL
};

static guint list_signals[LIST_LAST_SIGNAL] = { 0 };

#define CE_GCONF_PATH_TAG "ce-gconf-path"
#define CONNECTION_LIST_TAG "nm-connection-list"

#define COL_ID 			0
#define COL_LAST_USED	1
#define COL_TIMESTAMP	2
#define COL_CONNECTION	3

static gboolean
get_iter_for_connection (GtkWidget *clist,
                         NMConnection *connection,
                         GtkTreeModel **model,
                         GtkTreeIter *iter)
{
	GtkTreeModel *sort_model;
	GtkTreeModel *temp_model;
	GtkTreeIter temp_iter;
	gboolean found = FALSE;

	g_return_val_if_fail (GTK_IS_TREE_VIEW (clist), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);
	g_return_val_if_fail (model != NULL, FALSE);
	g_return_val_if_fail (*model == NULL, FALSE);
	g_return_val_if_fail (iter != NULL, FALSE);

	sort_model = gtk_tree_view_get_model (GTK_TREE_VIEW (clist));
	g_return_val_if_fail (sort_model != NULL, FALSE);

	temp_model = gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (sort_model));
	g_return_val_if_fail (temp_model != NULL, FALSE);

	if (!gtk_tree_model_get_iter_first (temp_model, &temp_iter))
		return FALSE;

	do {
		NMConnection *candidate = NULL;

		gtk_tree_model_get (temp_model, &temp_iter, COL_CONNECTION, &candidate, -1);
		if (candidate && (candidate == connection)) {
			*iter = temp_iter;
			*model = temp_model;
			found = TRUE;
			break;
		}
	} while (gtk_tree_model_iter_next (temp_model, &temp_iter));

	return found;
}

static NMConnection *
get_connection_for_selection (GtkWidget *clist,
                              GtkTreeModel **model,
                              GtkTreeIter *iter)
{
	GtkTreeSelection *selection;
	GList *selected_rows;
	NMConnection *connection = NULL;

	g_return_val_if_fail (clist != NULL, NULL);
	g_return_val_if_fail (GTK_IS_TREE_VIEW (clist), NULL);
	g_return_val_if_fail (model != NULL, NULL);
	g_return_val_if_fail (*model == NULL, NULL);
	g_return_val_if_fail (iter != NULL, NULL);

	/* get selected row from the tree view */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (clist));
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

static char *
format_last_used (guint64 timestamp)
{
	GTimeVal now_tv;
	GDate *now, *last;
	char *last_used = NULL;

	if (!timestamp)
		return g_strdup (_("never"));

	g_get_current_time (&now_tv);
	now = g_date_new ();
	g_date_set_time_val (now, &now_tv);

	last = g_date_new ();
	g_date_set_time_t (last, (time_t) timestamp);

	/* timestamp is now or in the future */
	if (now_tv.tv_sec <= timestamp) {
		last_used = g_strdup (_("now"));
		goto out;
	}

	if (g_date_compare (now, last) <= 0) {
		guint minutes, hours;

		/* Same day */

		minutes = (now_tv.tv_sec - timestamp) / 60;
		if (minutes == 0) {
			last_used = g_strdup (_("now"));
			goto out;
		}

		hours = (now_tv.tv_sec - timestamp) / 3600;
		if (hours == 0) {
			/* less than an hour ago */
			last_used = g_strdup_printf (ngettext ("%d minute ago", "%d minutes ago", minutes), minutes);
			goto out;
		}

		last_used = g_strdup_printf (ngettext ("%d hour ago", "%d hours ago", hours), hours);
	} else {
		guint days, months, years;

		days = g_date_get_julian (now) - g_date_get_julian (last);
		if (days == 0) {
			last_used = g_strdup ("today");
			goto out;
		}

		months = days / 30;
		if (months == 0) {
			last_used = g_strdup_printf (ngettext ("%d day ago", "%d days ago", days), days);
			goto out;
		}

		years = days / 365;
		if (years == 0) {
			last_used = g_strdup_printf (ngettext ("%d month ago", "%d months ago", months), months);
			goto out;
		}

		last_used = g_strdup_printf (ngettext ("%d year ago", "%d years ago", years), years);
	}

out:
	g_date_free (now);
	g_date_free (last);
	return last_used;
}

typedef struct {
	NMConnectionList *list;
	GtkWidget *clist;
} EditorDoneInfo;

static void
add_done_cb (NMConnectionEditor *editor, gint response, gpointer user_data)
{
	EditorDoneInfo *info = (EditorDoneInfo *) user_data;
	NMConnection *connection;

	connection = nm_connection_editor_get_connection (editor);

	if (response == GTK_RESPONSE_OK) {
		NMSettingConnection *s_con;
		const char *path;
		char *last_used;
		GtkTreeModel *sort_model;
		GtkListStore *store;
		GtkTreeIter iter;

		path = nm_connection_editor_get_gconf_path (editor);

		g_object_set_data_full (G_OBJECT (connection),
						    CE_GCONF_PATH_TAG, 
						    g_strdup (path),
						    (GDestroyNotify) g_free);

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		g_assert (s_con);

		sort_model = gtk_tree_view_get_model (GTK_TREE_VIEW (info->clist));
		store = GTK_LIST_STORE (gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (sort_model)));
		gtk_list_store_insert (store, &iter, G_MAXINT);

		last_used = format_last_used (s_con->timestamp);
		gtk_list_store_set (store, &iter,
		                    COL_ID, s_con->id,
		                    COL_LAST_USED, last_used,
		                    COL_TIMESTAMP, s_con->timestamp,
		                    COL_CONNECTION, connection,
		                    -1);
		g_free (last_used);
	}

	g_hash_table_remove (info->list->editors, connection);
	g_free (info);
}

static void
add_connection_cb (GtkButton *button, gpointer user_data)
{
	GtkWidget *clist = GTK_WIDGET (user_data);
	NMConnectionList *list;
	NMConnectionEditor *editor;
	NMConnection *connection;
	EditorDoneInfo *info;

	list = NM_CONNECTION_LIST (g_object_get_data (G_OBJECT (button), CONNECTION_LIST_TAG));
	g_assert (list);

	info = g_malloc0 (sizeof (EditorDoneInfo));
	g_assert (info);
	info->list = list;
	info->clist = clist;

	connection = nm_connection_new ();
	editor = nm_connection_editor_new (connection, NULL, NULL);
	g_signal_connect (G_OBJECT (editor), "done", G_CALLBACK (add_done_cb), info);
	g_hash_table_insert (list->editors, connection, editor);

	nm_connection_editor_run (editor);
}

static void
edit_done_cb (NMConnectionEditor *editor, gint response, gpointer user_data)
{
	EditorDoneInfo *info = (EditorDoneInfo *) user_data;
	NMConnection *connection;
	NMSettingConnection *s_con;

	connection = nm_connection_editor_get_connection (editor);

	if (response == GTK_RESPONSE_OK) {
		GtkTreeModel *model = NULL;
		GtkTreeIter iter;

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		g_assert (s_con);

		if (get_iter_for_connection (info->clist, connection, &model, &iter) && model)
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, COL_ID, s_con->id, -1);
	}

	g_hash_table_remove (info->list->editors, connection);
	g_free (info);
}

static void
do_edit (NMConnectionList *list, NMConnection *connection, GtkWidget *clist)
{
	NMConnectionEditor *editor;
	const char *gconf_path;
	EditorDoneInfo *info;

	/* Don't allow two editors for the same connection */
	editor = NM_CONNECTION_EDITOR (g_hash_table_lookup (list->editors, connection));
	if (editor) {
		nm_connection_editor_present (editor);
		return;
	}

	gconf_path = g_object_get_data (G_OBJECT (connection), CE_GCONF_PATH_TAG);
	g_assert (gconf_path);

	info = g_malloc0 (sizeof (EditorDoneInfo));
	g_assert (info);
	info->list = list;
	info->clist = clist;

	editor = nm_connection_editor_new (connection, gconf_path, list->client);
	g_signal_connect (G_OBJECT (editor), "done", G_CALLBACK (edit_done_cb), info);
	g_hash_table_insert (list->editors, connection, editor);

	nm_connection_editor_run (editor);
}

static void
edit_connection_cb (GtkButton *button, gpointer user_data)
{
	NMConnectionList *list;
	GtkWidget *clist = GTK_WIDGET (user_data);
	NMConnection *connection;
	GtkTreeModel *ignore1 = NULL;
	GtkTreeIter ignore2;

	connection = get_connection_for_selection (clist, &ignore1, &ignore2);
	g_return_if_fail (connection != NULL);

	list = NM_CONNECTION_LIST (g_object_get_data (G_OBJECT (button), CONNECTION_LIST_TAG));
	g_assert (list);

	do_edit (list, connection, clist);
}

static void
delete_connection_cb (GtkButton *button, gpointer user_data)
{
	GtkWidget *clist = GTK_WIDGET (user_data);
	NMConnectionList *list;
	NMConnectionEditor *editor;
	NMSettingConnection *s_con;
	NMConnection *connection;
	GtkWidget *dialog;
	gint result;
	const char *dir;
	GError *error = NULL;
	GtkTreeModel *model = NULL, *sorted_model;
	GtkTreeIter iter, sorted_iter;

	connection = get_connection_for_selection (clist, &model, &iter);
	g_return_if_fail (connection != NULL);
	g_return_if_fail (model != NULL);

	dir = g_object_get_data (G_OBJECT (connection), CE_GCONF_PATH_TAG);
	g_return_if_fail (dir != NULL);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con || !s_con->id)
		return;

	list = NM_CONNECTION_LIST (g_object_get_data (G_OBJECT (button), CONNECTION_LIST_TAG));
	g_return_if_fail (list != NULL);

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

	/* Close any open editor windows for this connection */
	editor = g_hash_table_lookup (list->editors, connection);
	if (editor)
		g_hash_table_remove (list->editors, connection);

	if (!gconf_client_recursive_unset (list->client, dir, 0, &error)) {
		g_warning ("%s: Failed to completely remove connection '%s': (%d) %s",
		           __func__, s_con->id, error->code, error->message);
		g_error_free (error);
	}

	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (model),
	                                                &sorted_iter,
	                                                &iter);
	sorted_model = gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (model));
	gtk_list_store_remove (GTK_LIST_STORE (sorted_model), &sorted_iter);

	if (!g_hash_table_remove (list->connections, dir))
		g_warning ("%s: couldn't remove connection from hash table.", __func__);
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
			                        NMA_CONNECTION_ID_TAG, g_path_get_basename (dir),
			                        (GDestroyNotify) g_free);

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
	GtkWidget *button = GTK_WIDGET (user_data);
	GtkTreeIter iter;
	GtkTreeModel *model;

	if (gtk_tree_selection_get_selected (selection, &model, &iter))
		gtk_widget_set_sensitive (GTK_WIDGET (button), TRUE);
	else
		gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
}

static void
connection_double_clicked_cb (GtkTreeView *tree_view,
                              GtkTreePath *path,
                              GtkTreeViewColumn *column,
                              gpointer user_data)
{
	NMConnectionList *list = NM_CONNECTION_LIST (user_data);
	GtkTreeModel *model;
	GtkTreeIter iter;
	NMConnection *connection;
	gboolean success;

	model = gtk_tree_view_get_model (tree_view);
	g_return_if_fail (model != NULL);

	success = gtk_tree_model_get_iter (model, &iter, path);
	g_return_if_fail (success);
	gtk_tree_model_get (model, &iter, COL_CONNECTION, &connection, -1);
	g_return_if_fail (connection != NULL);

	do_edit (list, connection, GTK_WIDGET (tree_view));
}

typedef struct {
	GSList *types;
	GtkListStore *model;
} NewListInfo;

static void
hash_add_connection_to_list (gpointer key, gpointer value, gpointer user_data)
{
	NewListInfo *info = (NewListInfo *) user_data;
	NMConnection *connection = (NMConnection *) value;
	NMSettingConnection *s_con;
	GtkTreeIter model_iter;
	gboolean found = FALSE;
	GSList *iter;
	char *last_used;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con || !s_con->type)
		return;

	/* Filter on requested types */
	for (iter = info->types; iter; iter = g_slist_next (iter)) {
		if (!strcmp (s_con->type, (const char *) iter->data)) {
			found = TRUE;
			break;
		}
	}

	if (!found)
		return;

	last_used = format_last_used (s_con->timestamp);

	gtk_list_store_append (info->model, &model_iter);
	gtk_list_store_set (info->model, &model_iter,
	                    COL_ID, s_con->id,
	                    COL_LAST_USED, last_used,
	                    COL_TIMESTAMP, s_con->timestamp,
	                    COL_CONNECTION, connection,
	                    -1);
	g_free (last_used);
}

static GtkWidget *
new_connection_list (NMConnectionList *list,
                     GSList *types,
                     const char *prefix,
                     GdkPixbuf *pixbuf,
                     const char *label_text)
{
	GtkWidget *notebook;
	GtkWidget *clist;
	GtkListStore *model;
	GtkTreeModel *sort_model;
	char *name;
	GtkWidget *image, *hbox, *button, *child;
	GtkTreeSelection *select;
	NewListInfo info;
	GtkCellRenderer *renderer;
	GValue val = { 0, };

	name = g_strdup_printf ("%s_child", prefix);
	child = glade_xml_get_widget (list->gui, name);
	g_free (name);

	/* Tab label with icon */
	hbox = gtk_hbox_new (FALSE, 6);
	if (pixbuf) {
		image = gtk_image_new_from_pixbuf (pixbuf);
		gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
	}
	gtk_box_pack_start (GTK_BOX (hbox), gtk_label_new (label_text), FALSE, FALSE, 0);
	gtk_widget_show_all (hbox);

	notebook = glade_xml_get_widget (list->gui, "list_notebook");
	gtk_notebook_set_tab_label (GTK_NOTEBOOK (notebook), child, hbox);

	name = g_strdup_printf ("%s_list", prefix);
	clist = glade_xml_get_widget (list->gui, name);
	g_free (name);

	model = gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_OBJECT);

	info.types = types;
	info.model = model;
	g_hash_table_foreach (list->connections,
	                      (GHFunc) hash_add_connection_to_list,
	                      &info);

	sort_model = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (model));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sort_model),
	                                      COL_TIMESTAMP, GTK_SORT_DESCENDING);
	gtk_tree_view_set_model (GTK_TREE_VIEW (clist), GTK_TREE_MODEL (sort_model));

	g_signal_connect (G_OBJECT (clist),
	                  "row-activated", G_CALLBACK (connection_double_clicked_cb),
	                  list);

	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (clist),
	                                             -1, "Name", gtk_cell_renderer_text_new (),
	                                             "text", COL_ID,
	                                             NULL);
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (clist), 0), TRUE);

	renderer = gtk_cell_renderer_text_new ();
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, "SlateGray");
	g_object_set_property (G_OBJECT (renderer), "foreground", &val);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (clist),
	                                             -1, "Last Used", renderer,
	                                             "text", COL_LAST_USED,
	                                             NULL);

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (clist));
	gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

	name = g_strdup_printf ("%s_add", prefix);
	button = glade_xml_get_widget (list->gui, name);
	g_object_set_data (G_OBJECT (button), CONNECTION_LIST_TAG, list);
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (add_connection_cb), clist);
	g_free (name);

	name = g_strdup_printf ("%s_edit", prefix);
	button = glade_xml_get_widget (list->gui, name);
	g_object_set_data (G_OBJECT (button), CONNECTION_LIST_TAG, list);
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (edit_connection_cb), clist);
	g_signal_connect (G_OBJECT (select),
	                  "changed", G_CALLBACK (list_selection_changed_cb),
	                  button);
	g_free (name);

	name = g_strdup_printf ("%s_delete", prefix);
	button = glade_xml_get_widget (list->gui, name);
	g_object_set_data (G_OBJECT (button), CONNECTION_LIST_TAG, list);
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (delete_connection_cb), clist);
	g_signal_connect (G_OBJECT (select),
	                  "changed", G_CALLBACK (list_selection_changed_cb),
	                  button);
	g_free (name);

	return clist;
}

static gboolean
init_connection_lists (NMConnectionList *list)
{
	GSList *types = NULL;
	GtkWidget *clist;

	types = g_slist_append (NULL, NM_SETTING_WIRED_SETTING_NAME);
	clist = new_connection_list (list, types, "wired", list->wired_icon, _("Wired"));
	g_slist_free (types);

	types = g_slist_append (NULL, NM_SETTING_WIRELESS_SETTING_NAME);
	clist = new_connection_list (list, types, "wireless", list->wireless_icon, _("Wireless"));
	g_slist_free (types);

	types = g_slist_append (NULL, NM_SETTING_GSM_SETTING_NAME);
	types = g_slist_append (types, NM_SETTING_CDMA_SETTING_NAME);
	clist = new_connection_list (list, types, "wwan", list->wwan_icon, _("Mobile Broadband"));
	g_slist_free (types);

	types = g_slist_append (NULL, NM_SETTING_VPN_SETTING_NAME);
	clist = new_connection_list (list, types, "vpn", list->vpn_icon, _("VPN"));
	g_slist_free (types);

	types = g_slist_append (NULL, NM_SETTING_PPPOE_SETTING_NAME);
	clist = new_connection_list (list, types, "dsl", list->wired_icon, _("DSL"));
	g_slist_free (types);

	return TRUE;
}

static void
dialog_response_cb (GtkDialog *dialog, guint response, gpointer user_data)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
nm_connection_list_init (NMConnectionList *list)
{
}

static void
dispose (GObject *object)
{
	NMConnectionList *list = NM_CONNECTION_LIST (object);

	if (list->dialog)
		gtk_widget_hide (list->dialog);

	if (list->editors)
		g_hash_table_destroy (list->editors);

	if (list->wired_icon)
		g_object_unref (list->wired_icon);
	if (list->wireless_icon)
		g_object_unref (list->wireless_icon);
	if (list->wwan_icon)
		g_object_unref (list->wwan_icon);
	if (list->vpn_icon)
		g_object_unref (list->vpn_icon);
	if (list->unknown_icon)
		g_object_unref (list->unknown_icon);

	if (list->dialog)
		gtk_widget_destroy (list->dialog);
	if (list->gui)
		g_object_unref (list->gui);
	if (list->connections)
		g_hash_table_destroy (list->connections);
	if (list->client)
		g_object_unref (list->client);

	G_OBJECT_CLASS (nm_connection_list_parent_class)->dispose (object);
}

static void
nm_connection_list_class_init (NMConnectionListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/* virtual methods */
	object_class->dispose = dispose;

	/* Signals */
	list_signals[LIST_DONE] =
		g_signal_new ("done",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMConnectionListClass, done),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__INT,
					  G_TYPE_NONE, 1, G_TYPE_INT);
}

#define ICON_LOAD(x, y)	\
	{ \
		x = gtk_icon_theme_load_icon (list->icon_theme, y, 16, 0, &error); \
		if (x == NULL) { \
			g_warning ("Icon %s missing: %s", y, error->message); \
			g_error_free (error); \
			goto error; \
		} \
	}

NMConnectionList *
nm_connection_list_new (void)
{
	NMConnectionList *list;
	GError *error = NULL;

	list = g_object_new (NM_TYPE_CONNECTION_LIST, NULL);
	if (!list)
		return NULL;

	/* load GUI */
	list->gui = glade_xml_new (GLADEDIR "/nm-connection-editor.glade", "NMConnectionList", NULL);
	if (!list->gui) {
		g_warning ("Could not load Glade file for connection list");
		goto error;
	}

	list->icon_theme = gtk_icon_theme_get_for_screen (gdk_screen_get_default ());

	/* Load icons */
	ICON_LOAD(list->wired_icon, "nm-device-wired");
	ICON_LOAD(list->wireless_icon, "nm-device-wireless");
	ICON_LOAD(list->wwan_icon, "nm-device-wwan");
	ICON_LOAD(list->vpn_icon, "nm-vpn-standalone-lock");
	ICON_LOAD(list->unknown_icon, "nm-no-connection");

	list->client = gconf_client_get_default ();
	if (!list->client)
		goto error;

	/* read connections */
	list->connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	load_connections (list);
	init_connection_lists (list);

	list->editors = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, g_object_unref);

	list->dialog = glade_xml_get_widget (list->gui, "NMConnectionList");
	if (!list->dialog)
		goto error;
	g_signal_connect (G_OBJECT (list->dialog), "response", G_CALLBACK (dialog_response_cb), list);

	return list;

error:
	g_object_unref (list);
	return NULL;
}

static void
nm_connection_list_present (NMConnectionList *list)
{
	g_return_if_fail (NM_IS_CONNECTION_LIST (list));

	gtk_window_present (GTK_WINDOW (list->dialog));
}

static void
list_response_cb (GtkDialog *dialog, gint response, gpointer user_data)
{
	g_signal_emit (NM_CONNECTION_LIST (user_data), list_signals[LIST_DONE], 0, response);
}

static void
list_close_cb (GtkDialog *dialog, gpointer user_data)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_CLOSE);
}

void
nm_connection_list_run (NMConnectionList *list)
{
	g_return_if_fail (NM_IS_CONNECTION_LIST (list));

	g_signal_connect (G_OBJECT (list->dialog), "response",
	                  G_CALLBACK (list_response_cb), list);
	g_signal_connect (G_OBJECT (list->dialog), "close",
	                  G_CALLBACK (list_close_cb), list);

	nm_connection_list_present (list);
}

