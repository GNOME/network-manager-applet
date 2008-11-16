/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Dan Williams <dcbw@redhat.com>
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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <glade/glade.h>
#include <glib/gi18n.h>

#include <nm-utils.h>

#include "ip4-routes-dialog.h"

#define COL_ADDRESS 0
#define COL_PREFIX  1
#define COL_NEXT_HOP 2
#define COL_METRIC  3
#define COL_LAST COL_METRIC

static void
route_add_clicked (GtkButton *button, gpointer user_data)
{
	GladeXML *xml = GLADE_XML (user_data);
	GtkWidget *widget;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkTreePath *path;
	GList *cells;

	widget = glade_xml_get_widget (xml, "ip4_routes");
	store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (widget)));
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, COL_ADDRESS, "", -1);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	gtk_tree_selection_select_iter (selection, &iter);

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), COL_ADDRESS);

	/* FIXME: using cells->data is pretty fragile but GTK apparently doesn't
	 * have a way to get a cell renderer from a column based on path or iter
	 * or whatever.
	 */
	cells = gtk_tree_view_column_get_cell_renderers (column);
	gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (widget), path, column, cells->data, TRUE);

	g_list_free (cells);
	gtk_tree_path_free (path);
}

static void
route_delete_clicked (GtkButton *button, gpointer user_data)
{
	GtkTreeView *treeview = GTK_TREE_VIEW (user_data);
	GtkTreeSelection *selection;
	GList *selected_rows;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int num_rows;

	selection = gtk_tree_view_get_selection (treeview);
	if (gtk_tree_selection_count_selected_rows (selection) != 1)
		return;

	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
	if (!selected_rows)
		return;

	if (gtk_tree_model_get_iter (model, &iter, (GtkTreePath *) selected_rows->data))
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);

	num_rows = gtk_tree_model_iter_n_children (model, NULL);
	if (num_rows && gtk_tree_model_iter_nth_child (model, &iter, NULL, num_rows - 1)) {
		selection = gtk_tree_view_get_selection (treeview);
		gtk_tree_selection_select_iter (selection, &iter);
	}
}

static void
list_selection_changed (GtkTreeSelection *selection, gpointer user_data)
{
	GtkWidget *button = GTK_WIDGET (user_data);
	GtkTreeIter iter;
	GtkTreeModel *model = NULL;

	if (gtk_tree_selection_get_selected (selection, &model, &iter))
		gtk_widget_set_sensitive (button, TRUE);
	else
		gtk_widget_set_sensitive (button, FALSE);
}

static void
cell_edited (GtkCellRendererText *cell,
             const gchar *path_string,
             const gchar *new_text,
             gpointer user_data)
{
	GladeXML *xml = GLADE_XML (user_data);
	GtkWidget *widget, *dialog;
	GtkListStore *store;
	GtkTreePath *path;
	GtkTreeIter iter;
	guint32 column;
	GtkTreeViewColumn *next_col;
	GtkCellRenderer *next_cell;

	widget = glade_xml_get_widget (xml, "ip4_routes");
	store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (widget)));
	path = gtk_tree_path_new_from_string (path_string);
	column = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (cell), "column"));

	gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
	gtk_list_store_set (store, &iter, column, new_text, -1);

	/* Move focus to the next column */
	column = (column >= COL_LAST) ? 0 : column + 1;
	next_col = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), column);
	dialog = glade_xml_get_widget (xml, "ip4_routes_dialog");
	next_cell = g_slist_nth_data (g_object_get_data (G_OBJECT (dialog), "renderers"), column);

	gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (widget), path, next_col, next_cell, TRUE);
	gtk_widget_grab_focus (widget);

	gtk_tree_path_free (path);
}

static void
ip_address_filter_cb (GtkEntry *   entry,
                      const gchar *text,
                      gint         length,
                      gint *       position,
                      gpointer     data)
{
	GtkEditable *editable = GTK_EDITABLE (entry);
	int i, count = 0;
	gchar *result = g_new (gchar, length);

	for (i = 0; i < length; i++) {
		if ((text[i] >= '0' && text[i] <= '9') || (text[i] == '.'))
			result[count++] = text[i];
	}

	if (count > 0) {
		g_signal_handlers_block_by_func (G_OBJECT (editable),
		                                 G_CALLBACK (ip_address_filter_cb),
		                                 data);
		gtk_editable_insert_text (editable, result, count, position);
		g_signal_handlers_unblock_by_func (G_OBJECT (editable),
		                                   G_CALLBACK (ip_address_filter_cb),
		                                   data);
	}

	g_signal_stop_emission_by_name (G_OBJECT (editable), "insert-text");
	g_free (result);
}

static void
cell_editing_started (GtkCellRenderer *cell,
                      GtkCellEditable *editable,
                      const gchar     *path,
                      gpointer         data)
{
	if (!GTK_IS_ENTRY (editable)) {
		g_warning ("%s: Unexpected cell editable type.", __func__);
		return;
	}

	/* Set up the entry filter */
	g_signal_connect (G_OBJECT (editable), "insert-text",
	                  (GCallback) ip_address_filter_cb,
	                  data);
}

GtkWidget *
ip4_routes_dialog_new (NMSettingIP4Config *s_ip4, gboolean automatic)
{
	GladeXML *xml;
	GtkWidget *dialog, *widget;
	GtkListStore *store;
	GtkTreeIter model_iter;
	GtkTreeSelection *selection;
	gint offset;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	int i;
	GSList *renderers = NULL;

	xml = glade_xml_new (GLADEDIR "/ce-page-ip4.glade", "ip4_routes_dialog", NULL);
	if (!xml) {
		g_warning ("%s: Couldn't load ip4 page glade file.", __func__);
		return NULL;
	}

	dialog = glade_xml_get_widget (xml, "ip4_routes_dialog");
	if (!dialog) {
		g_warning ("%s: Couldn't load ip4 routes dialog from glade file.", __func__);
		g_object_unref (xml);
		return NULL;
	}

	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	g_object_set_data_full (G_OBJECT (dialog), "glade-xml",
	                        xml, (GDestroyNotify) g_object_unref);

	store = gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* Add existing routes */
	for (i = 0; i < nm_setting_ip4_config_get_num_routes (s_ip4); i++) {
		NMIP4Route *route = nm_setting_ip4_config_get_route (s_ip4, i);
		struct in_addr tmp_addr;
		char ip_string[INET_ADDRSTRLEN];
		char *tmp;

		if (!route) {
			g_warning ("%s: empty IP4 route structure!", __func__);
			continue;
		}

		gtk_list_store_append (store, &model_iter);

		tmp_addr.s_addr = nm_ip4_route_get_dest (route);;
		if (inet_ntop (AF_INET, &tmp_addr, &ip_string[0], sizeof (ip_string)))
			gtk_list_store_set (store, &model_iter, COL_ADDRESS, ip_string, -1);

		tmp_addr.s_addr = nm_utils_ip4_prefix_to_netmask (nm_ip4_route_get_prefix (route));
		if (inet_ntop (AF_INET, &tmp_addr, &ip_string[0], sizeof (ip_string)))
			gtk_list_store_set (store, &model_iter, COL_PREFIX, ip_string, -1);

		tmp_addr.s_addr = nm_ip4_route_get_next_hop (route);
		if (tmp_addr.s_addr && inet_ntop (AF_INET, &tmp_addr, &ip_string[0], sizeof (ip_string)))
			gtk_list_store_set (store, &model_iter, COL_NEXT_HOP, ip_string, -1);

		if (nm_ip4_route_get_metric (route)) {
			tmp = g_strdup_printf ("%d", nm_ip4_route_get_metric (route));
			gtk_list_store_set (store, &model_iter, COL_METRIC, tmp, -1);
			g_free (tmp);
		}
	}

	widget = glade_xml_get_widget (xml, "ip4_routes");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget), GTK_TREE_MODEL (store));
	g_object_unref (store);

	/* IP Address column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), xml);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_ADDRESS));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), store);
	renderers = g_slist_append (renderers, renderer);

	offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (widget),
	                                                      -1, _("Address"), renderer,
	                                                      "text", COL_ADDRESS,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	/* Prefix column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), xml);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_PREFIX));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), store);
	renderers = g_slist_append (renderers, renderer);

	offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (widget),
	                                                      -1, _("Netmask"), renderer,
	                                                      "text", COL_PREFIX,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	/* Gateway column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), xml);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_NEXT_HOP));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), store);
	renderers = g_slist_append (renderers, renderer);

	offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (widget),
	                                                      -1, _("Gateway"), renderer,
	                                                      "text", COL_NEXT_HOP,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	/* Metric column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), xml);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_METRIC));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), store);
	renderers = g_slist_append (renderers, renderer);

	offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (widget),
	                                                      -1, _("Metric"), renderer,
	                                                      "text", COL_METRIC,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	g_object_set_data_full (G_OBJECT (dialog), "renderers", renderers, (GDestroyNotify) g_slist_free);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
	                  G_CALLBACK (list_selection_changed),
	                  glade_xml_get_widget (xml, "ip4_route_delete_button"));

	widget = glade_xml_get_widget (xml, "ip4_route_add_button");
	gtk_widget_set_sensitive (widget, TRUE);
	g_signal_connect (widget, "clicked", G_CALLBACK (route_add_clicked), xml);

	widget = glade_xml_get_widget (xml, "ip4_route_delete_button");
	gtk_widget_set_sensitive (widget, FALSE);
	g_signal_connect (widget, "clicked",
	                  G_CALLBACK (route_delete_clicked),
	                  glade_xml_get_widget (xml, "ip4_routes"));

	widget = glade_xml_get_widget (xml, "ip4_ignore_auto_routes");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
	                              nm_setting_ip4_config_get_ignore_auto_routes (s_ip4));
	gtk_widget_set_sensitive (widget, automatic);

	return dialog;
}

static gboolean
get_one_int (GtkTreeModel *model,
             GtkTreeIter *iter,
             int column,
             guint32 max_value,
             gboolean fail_if_missing,
             guint32 *out)
{
	char *item = NULL;
	gboolean success = FALSE;
	long int tmp_int;

	gtk_tree_model_get (model, iter, column, &item, -1);
	if ((!item || !strlen (item)) && !fail_if_missing) {
		g_free (item);
		return TRUE;
	}

	errno = 0;
	tmp_int = strtol (item, NULL, 10);
	if (errno || tmp_int < 0 || tmp_int > max_value)
		goto out;

	*out = (guint32) tmp_int;
	success = TRUE;

out:
	g_free (item);
	return success;
}

static gboolean
get_one_prefix (GtkTreeModel *model,
                GtkTreeIter *iter,
                int column,
                gboolean fail_if_missing,
                guint32 *out)
{
	char *item = NULL;
	struct in_addr tmp_addr = { 0 };
	gboolean success = FALSE;
	glong tmp_prefix;

	gtk_tree_model_get (model, iter, column, &item, -1);
	if ((!item || !strlen (item)) && !fail_if_missing) {
		g_free (item);
		return TRUE;
	}

	errno = 0;

	/* Is it a prefix? */
	if (!strchr (item, '.')) {
		tmp_prefix = strtol (item, NULL, 10);
		if (!errno && tmp_prefix >= 0 && tmp_prefix <= 32) {
			*out = tmp_prefix;
			success = TRUE;
			goto out;
		}
	}

	/* Is it a netmask? */
	if (inet_pton (AF_INET, item, &tmp_addr) > 0) {
		*out = nm_utils_ip4_netmask_to_prefix (tmp_addr.s_addr);
		success = TRUE;
	}

out:
	g_free (item);
	return success;
}

static gboolean
get_one_addr (GtkTreeModel *model,
              GtkTreeIter *iter,
              int column,
              gboolean fail_if_missing,
              guint32 *out)
{
	char *item = NULL;
	struct in_addr tmp_addr = { 0 };
	gboolean success = FALSE;

	gtk_tree_model_get (model, iter, column, &item, -1);
	if ((!item || !strlen (item)) && !fail_if_missing) {
		g_free (item);
		return TRUE;
	}

	if (inet_pton (AF_INET, item, &tmp_addr) > 0) {
		*out = tmp_addr.s_addr;
		success = TRUE;
	}

	g_free (item);
	return success;
}

void
ip4_routes_dialog_update_setting (GtkWidget *dialog, NMSettingIP4Config *s_ip4)
{
	GladeXML *xml;
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter tree_iter;
	gboolean iter_valid;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (s_ip4 != NULL);

	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_return_if_fail (xml != NULL);
	g_return_if_fail (GLADE_IS_XML (xml));

	widget = glade_xml_get_widget (xml, "ip4_routes");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
	iter_valid = gtk_tree_model_get_iter_first (model, &tree_iter);

	nm_setting_ip4_config_clear_routes (s_ip4);

	while (iter_valid) {
		guint32 addr = 0, prefix = 0, next_hop = 0, metric = 0;
		NMIP4Route *route;

		/* Address */
		if (!get_one_addr (model, &tree_iter, COL_ADDRESS, TRUE, &addr)) {
			g_warning ("%s: IPv4 address missing or invalid!", __func__);
			goto next;
		}

		/* Prefix */
		if (!get_one_prefix (model, &tree_iter, COL_PREFIX, TRUE, &prefix)) {
			g_warning ("%s: IPv4 prefix/netmask missing or invalid!", __func__);
			goto next;
		}

		/* Next hop (optional) */
		if (!get_one_addr (model, &tree_iter, COL_NEXT_HOP, FALSE, &next_hop)) {
			g_warning ("%s: IPv4 next hop invalid!", __func__);
			goto next;
		}

		/* Metric (optional) */
		if (!get_one_int (model, &tree_iter, COL_METRIC, G_MAXUINT32, FALSE, &metric)) {
			g_warning ("%s: IPv4 metric invalid!", __func__);
			goto next;
		}

		route = nm_ip4_route_new ();
		nm_ip4_route_set_dest (route, addr);
		nm_ip4_route_set_prefix (route, prefix);
		nm_ip4_route_set_next_hop (route, next_hop);
		nm_ip4_route_set_metric (route, metric);
		nm_setting_ip4_config_add_route (s_ip4, route);
		nm_ip4_route_unref (route);

	next:
		iter_valid = gtk_tree_model_iter_next (model, &tree_iter);
	}

	widget = glade_xml_get_widget (xml, "ip4_ignore_auto_routes");
	g_object_set (s_ip4, NM_SETTING_IP4_CONFIG_IGNORE_AUTO_ROUTES,
	              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)),
	              NULL);
}

