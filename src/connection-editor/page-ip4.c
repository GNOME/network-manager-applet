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

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-setting-pppoe.h>
#include <nm-setting-vpn.h>
#include <nm-utils.h>

#include "page-ip4.h"
#include "ip4-routes-dialog.h"

G_DEFINE_TYPE (CEPageIP4, ce_page_ip4, CE_TYPE_PAGE)

#define CE_PAGE_IP4_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_IP4, CEPageIP4Private))

typedef struct {
	NMSettingIP4Config *setting;
	char *connection_id;

	GtkComboBox *method;
	GtkListStore *method_store;

	/* Addresses */
	GtkWidget *addr_label;
	GtkButton *addr_add;
	GtkButton *addr_delete;
	GtkTreeView *addr_list;

	/* DNS servers */
	GtkWidget *dns_servers_label;
	GtkEntry *dns_servers;

	/* Search domains */
	GtkWidget *dns_searches_label;
	GtkEntry *dns_searches;

	/* DHCP stuff */
	GtkWidget *dhcp_client_id_label;
	GtkEntry *dhcp_client_id;

	GtkButton *routes_button;

	GtkWindowGroup *window_group;
	gboolean window_added;
} CEPageIP4Private;

#define METHOD_COL_NAME 0
#define METHOD_COL_NUM  1

#define IP4_METHOD_AUTO            0
#define IP4_METHOD_AUTO_ADDRESSES  1
#define IP4_METHOD_MANUAL          2
#define IP4_METHOD_LINK_LOCAL      3
#define IP4_METHOD_SHARED          4

#define COL_ADDRESS 0
#define COL_PREFIX 1
#define COL_GATEWAY 2

static void
ip4_private_init (CEPageIP4 *self, NMConnection *connection)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GladeXML *xml;
	GtkTreeIter iter;
	NMSettingConnection *s_con;
	const char *connection_type;
	char *str_auto = NULL, *str_auto_only = NULL;
	gboolean is_vpn = FALSE;

	xml = CE_PAGE (self)->xml;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);
	connection_type = nm_setting_connection_get_connection_type (s_con);
	g_assert (connection_type);

	if (!strcmp (connection_type, NM_SETTING_VPN_SETTING_NAME)) {
		str_auto = _("Automatic (VPN)");
		str_auto_only = _("Automatic (VPN) addresses only");
		is_vpn = TRUE;
	} else if (   !strcmp (connection_type, NM_SETTING_GSM_SETTING_NAME)
	           || !strcmp (connection_type, NM_SETTING_CDMA_SETTING_NAME)) {
		str_auto = _("Automatic (PPP)");
		str_auto_only = _("Automatic (PPP) addresses only");
	} else if (!strcmp (connection_type, NM_SETTING_PPPOE_SETTING_NAME)) {
		str_auto = _("Automatic (PPPoE)");
		str_auto_only = _("Automatic (PPPoE) addresses only");
	} else {
		str_auto = _("Automatic (DHCP)");
		str_auto_only = _("Automatic (DHCP) addresses only");
	}

	priv->method = GTK_COMBO_BOX (glade_xml_get_widget (xml, "ip4_method"));

	priv->method_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_UINT);

	gtk_list_store_append (priv->method_store, &iter);
	gtk_list_store_set (priv->method_store, &iter,
	                    METHOD_COL_NAME, str_auto,
	                    METHOD_COL_NUM, IP4_METHOD_AUTO,
	                    -1);

	gtk_list_store_append (priv->method_store, &iter);
	gtk_list_store_set (priv->method_store, &iter,
	                    METHOD_COL_NAME, str_auto_only,
	                    METHOD_COL_NUM, IP4_METHOD_AUTO_ADDRESSES,
	                    -1);

	gtk_list_store_append (priv->method_store, &iter);
	gtk_list_store_set (priv->method_store, &iter,
	                    METHOD_COL_NAME, _("Manual"),
	                    METHOD_COL_NUM, IP4_METHOD_MANUAL,
	                    -1);

	if (!is_vpn) {
		/* Link-local is pointless for VPNs */
		gtk_list_store_append (priv->method_store, &iter);
		gtk_list_store_set (priv->method_store, &iter,
		                    METHOD_COL_NAME, _("Link-Local Only"),
		                    METHOD_COL_NUM, IP4_METHOD_LINK_LOCAL,
		                    -1);

		gtk_list_store_append (priv->method_store, &iter);
		gtk_list_store_set (priv->method_store, &iter,
		                    METHOD_COL_NAME, _("Shared to other computers"),
		                    METHOD_COL_NUM, IP4_METHOD_SHARED,
		                    -1);
	}

	gtk_combo_box_set_model (priv->method, GTK_TREE_MODEL (priv->method_store));

	priv->addr_label = glade_xml_get_widget (xml, "ip4_addr_label");
	priv->addr_add = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_addr_add_button"));
	priv->addr_delete = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_addr_delete_button"));
	priv->addr_list = GTK_TREE_VIEW (glade_xml_get_widget (xml, "ip4_addresses"));

	priv->dns_servers_label = glade_xml_get_widget (xml, "ip4_dns_servers_label");
	priv->dns_servers = GTK_ENTRY (glade_xml_get_widget (xml, "ip4_dns_servers_entry"));

	priv->dns_searches_label = glade_xml_get_widget (xml, "ip4_dns_searches_label");
	priv->dns_searches = GTK_ENTRY (glade_xml_get_widget (xml, "ip4_dns_searches_entry"));

	priv->dhcp_client_id_label = glade_xml_get_widget (xml, "ip4_dhcp_client_id_label");
	priv->dhcp_client_id = GTK_ENTRY (glade_xml_get_widget (xml, "ip4_dhcp_client_id_entry"));

	priv->routes_button = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_routes_button"));
}

static void
method_changed (GtkComboBox *combo, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (user_data);
	guint32 method = IP4_METHOD_AUTO;
	gboolean addr_enabled = FALSE;
	gboolean dns_enabled = FALSE;
	gboolean dhcp_enabled = FALSE;
	gboolean routes_enabled = FALSE;
	GtkTreeIter iter;

	if (gtk_combo_box_get_active_iter (priv->method, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (priv->method_store), &iter,
		                    METHOD_COL_NUM, &method, -1);
	}

	switch (method) {
	case IP4_METHOD_AUTO:
		dhcp_enabled = routes_enabled = TRUE;
		break;
	case IP4_METHOD_AUTO_ADDRESSES:
		addr_enabled = FALSE;
		dns_enabled = dhcp_enabled = routes_enabled = TRUE;
		break;
	case IP4_METHOD_MANUAL:
		addr_enabled = dns_enabled = routes_enabled = TRUE;
		break;
	default:
		break;
	}

	gtk_widget_set_sensitive (priv->addr_label, addr_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_add), addr_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_delete), addr_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_list), addr_enabled);
	if (!addr_enabled) {
		GtkListStore *store;

		store = GTK_LIST_STORE (gtk_tree_view_get_model (priv->addr_list));
		gtk_list_store_clear (store);
	}

	gtk_widget_set_sensitive (priv->dns_servers_label, dns_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_servers), dns_enabled);
	if (!dns_enabled)
		gtk_entry_set_text (priv->dns_servers, "");

	gtk_widget_set_sensitive (priv->dns_searches_label, dns_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_searches), dns_enabled);
	if (!dns_enabled)
		gtk_entry_set_text (priv->dns_searches, "");

	gtk_widget_set_sensitive (priv->dhcp_client_id_label, dhcp_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->dhcp_client_id), dhcp_enabled);
	if (!dhcp_enabled)
		gtk_entry_set_text (priv->dhcp_client_id, "");

	gtk_widget_set_sensitive (GTK_WIDGET (priv->routes_button), routes_enabled);

	ce_page_changed (CE_PAGE (user_data));
}

typedef struct {
	int method;
	GtkComboBox *combo;
} SetMethodInfo;

static gboolean
set_method (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data)
{
	SetMethodInfo *info = (SetMethodInfo *) user_data;
	int method = 0;

	gtk_tree_model_get (model, iter, METHOD_COL_NUM, &method, -1);
	if (method == info->method) {
		gtk_combo_box_set_active_iter (info->combo, iter);
		return TRUE;
	}
	return FALSE;
}

static void
populate_ui (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	NMSettingIP4Config *setting = priv->setting;
	GtkListStore *store;
	GtkTreeIter model_iter;
	GSList *iter;
	int method = IP4_METHOD_AUTO;
	GString *string = NULL;
	SetMethodInfo info;

	/* Method */
	gtk_combo_box_set_active (priv->method, 0);
	if (setting->method) {
		if (!strcmp (setting->method, NM_SETTING_IP4_CONFIG_METHOD_LINK_LOCAL))
			method = IP4_METHOD_LINK_LOCAL;
		else if (!strcmp (setting->method, NM_SETTING_IP4_CONFIG_METHOD_MANUAL))
			method = IP4_METHOD_MANUAL;
		else if (!strcmp (setting->method, NM_SETTING_IP4_CONFIG_METHOD_SHARED))
			method = IP4_METHOD_SHARED;
	}

	if (method == IP4_METHOD_AUTO && setting->ignore_auto_dns)
		method = IP4_METHOD_AUTO_ADDRESSES;

	info.method = method;
	info.combo = priv->method;
	gtk_tree_model_foreach (GTK_TREE_MODEL (priv->method_store), set_method, &info);

	/* Addresses */
	store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	for (iter = setting->addresses; iter; iter = g_slist_next (iter)) {
		NMSettingIP4Address *addr = (NMSettingIP4Address *) iter->data;
		struct in_addr tmp_addr;
		gchar *ip_string;

		if (!addr) {
			g_warning ("%s: empty IP4 Address structure!", __func__);
			continue;
		}

		gtk_list_store_append (store, &model_iter);

		tmp_addr.s_addr = addr->address;
		ip_string = inet_ntoa (tmp_addr);
		gtk_list_store_set (store, &model_iter, COL_ADDRESS, g_strdup (ip_string), -1);

		gtk_list_store_set (store, &model_iter, COL_PREFIX, g_strdup_printf ("%d", addr->prefix), -1);

		tmp_addr.s_addr = addr->gateway;
		ip_string = inet_ntoa (tmp_addr);
		gtk_list_store_set (store, &model_iter, COL_GATEWAY, g_strdup (ip_string), -1);
	}

	gtk_tree_view_set_model (priv->addr_list, GTK_TREE_MODEL (store));
	g_signal_connect_swapped (store, "row-inserted", G_CALLBACK (ce_page_changed), self);
	g_signal_connect_swapped (store, "row-deleted", G_CALLBACK (ce_page_changed), self);
	g_object_unref (store);

	/* DNS servers */
	if (setting->dns) {
		int i;

		string = g_string_new ("");
		for (i = 0; i < setting->dns->len; i++) {
			struct in_addr tmp_addr;
			char *ip_string;

			tmp_addr.s_addr = g_array_index (setting->dns, guint32, i);
			if (!tmp_addr.s_addr)
				continue;

			ip_string = inet_ntoa (tmp_addr);
			if (string->len)
				g_string_append (string, ", ");
			g_string_append (string, ip_string);
		}

		gtk_entry_set_text (priv->dns_servers, string->str);
		g_string_free (string, TRUE);
	}

	/* DNS searches */
	string = g_string_new ("");
	for (iter = setting->dns_search; iter; iter = g_slist_next (iter)) {
		if (string->len)
			g_string_append (string, ", ");
		g_string_append (string, g_strdup (iter->data));
	}
	gtk_entry_set_text (priv->dns_searches, string->str);
	g_string_free (string, TRUE);

	if ((method == IP4_METHOD_AUTO) || (method = IP4_METHOD_AUTO_ADDRESSES)) {
		if (setting->dhcp_client_id)
			gtk_entry_set_text (priv->dhcp_client_id, setting->dhcp_client_id);
	}
}

static void
addr_add_clicked (GtkButton *button, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (user_data);
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkTreePath *path;
	GList *cells;

	store = GTK_LIST_STORE (gtk_tree_view_get_model (priv->addr_list));
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, g_strdup (""), -1);

	selection = gtk_tree_view_get_selection (priv->addr_list);
	gtk_tree_selection_select_iter (selection, &iter);

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
	column = gtk_tree_view_get_column (priv->addr_list, COL_ADDRESS);

	/* FIXME: using cells->data is pretty fragile but GTK apparently doesn't
	 * have a way to get a cell renderer from a column based on path or iter
	 * or whatever.
	 */
	cells = gtk_tree_view_column_get_cell_renderers (column);
	gtk_tree_view_set_cursor_on_cell (priv->addr_list, path, column, cells->data, TRUE);

	g_list_free (cells);
	gtk_tree_path_free (path);
}

static void
addr_delete_clicked (GtkButton *button, gpointer user_data)
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
	CEPageIP4 *self = CE_PAGE_IP4 (user_data);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkListStore *store = GTK_LIST_STORE (gtk_tree_view_get_model (priv->addr_list));
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter iter;
	guint32 column = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (cell), "column"));

	gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
	gtk_list_store_set (store, &iter, column, new_text, -1);
	gtk_tree_path_free (path);

	ce_page_changed (CE_PAGE (self));
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

static void
routes_dialog_close_cb (GtkWidget *dialog, gpointer user_data)
{
	gtk_widget_hide (dialog);
	/* gtk_widget_destroy() will remove the window from the window group */
	gtk_widget_destroy (dialog);
}

static void
routes_dialog_response_cb (GtkWidget *dialog, gint response, gpointer user_data)
{
	CEPageIP4 *self = CE_PAGE_IP4 (user_data);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	if (response == GTK_RESPONSE_OK)
		ip4_routes_dialog_update_setting (dialog, priv->setting);

	routes_dialog_close_cb (dialog, NULL);
}

static void
routes_button_clicked_cb (GtkWidget *button, gpointer user_data)
{
	CEPageIP4 *self = CE_PAGE_IP4 (user_data);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkWidget *dialog, *toplevel;
	gboolean automatic = FALSE;

	toplevel = gtk_widget_get_toplevel (CE_PAGE (self)->page);
	g_return_if_fail (GTK_WIDGET_TOPLEVEL (toplevel));

	if (!priv->setting->method || !strcmp (priv->setting->method, NM_SETTING_IP4_CONFIG_METHOD_AUTO))
		automatic = TRUE;
	
	dialog = ip4_routes_dialog_new (priv->setting->routes, automatic, priv->setting->ignore_auto_routes);
	if (!dialog) {
		g_warning ("%s: failed to create the routes dialog!", __func__);
		return;
	}

	gtk_window_group_add_window (priv->window_group, GTK_WINDOW (dialog));
	if (!priv->window_added) {
		gtk_window_group_add_window (priv->window_group, GTK_WINDOW (toplevel));
		priv->window_added = TRUE;
	}

	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (toplevel));
	gtk_window_set_title (GTK_WINDOW (dialog), g_strdup_printf (_("Editing IPv4 routes for %s"), priv->connection_id));

	g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK (routes_dialog_response_cb), self);
	g_signal_connect (G_OBJECT (dialog), "close", G_CALLBACK (routes_dialog_close_cb), self);

	gtk_widget_show_all (dialog);
}

CEPageIP4 *
ce_page_ip4_new (NMConnection *connection)
{
	CEPageIP4 *self;
	CEPageIP4Private *priv;
	CEPage *parent;
	NMSettingConnection *s_con;
	GtkTreeSelection *selection;
	gint offset;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkListStore *store;

	self = CE_PAGE_IP4 (g_object_new (CE_TYPE_PAGE_IP4, NULL));
	parent = CE_PAGE (self);

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-ip4.glade", "IP4Page", NULL);
	if (!parent->xml) {
		g_warning ("%s: Couldn't load wired page glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "IP4Page");
	if (!parent->page) {
		g_warning ("%s: Couldn't load wired page from glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("IPv4 Settings"));

	ip4_private_init (self, connection);
	priv = CE_PAGE_IP4_GET_PRIVATE (self);

	priv->window_group = gtk_window_group_new ();

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);
	priv->connection_id = g_strdup (nm_setting_connection_get_id (s_con));

	priv->setting = (NMSettingIP4Config *) nm_connection_get_setting (connection, NM_TYPE_SETTING_IP4_CONFIG);
	if (!priv->setting) {
		priv->setting = NM_SETTING_IP4_CONFIG (nm_setting_ip4_config_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	populate_ui (self);

	/* Address column */
	store = GTK_LIST_STORE (gtk_tree_view_get_model (priv->addr_list));

	/* IP Address column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), self);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_ADDRESS));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), store);

	offset = gtk_tree_view_insert_column_with_attributes (priv->addr_list,
	                                                      -1, _("Address"), renderer,
	                                                      "text", COL_ADDRESS,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->addr_list), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	/* Prefix column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), self);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_PREFIX));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), store);

	offset = gtk_tree_view_insert_column_with_attributes (priv->addr_list,
	                                                      -1, _("Netmask"), renderer,
	                                                      "text", COL_PREFIX,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->addr_list), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	/* Gateway column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), self);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_GATEWAY));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), store);

	offset = gtk_tree_view_insert_column_with_attributes (priv->addr_list,
	                                                      -1, _("Gateway"), renderer,
	                                                      "text", COL_GATEWAY,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->addr_list), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_add), TRUE);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_delete), FALSE);

	g_signal_connect (priv->addr_add, "clicked", G_CALLBACK (addr_add_clicked), self);
	g_signal_connect (priv->addr_delete, "clicked", G_CALLBACK (addr_delete_clicked), priv->addr_list);
	selection = gtk_tree_view_get_selection (priv->addr_list);
	g_signal_connect (selection, "changed", G_CALLBACK (list_selection_changed), priv->addr_delete);

	g_signal_connect_swapped (priv->dns_servers, "changed", G_CALLBACK (ce_page_changed), self);
	g_signal_connect_swapped (priv->dns_searches, "changed", G_CALLBACK (ce_page_changed), self);

	method_changed (priv->method, self);
	g_signal_connect (priv->method, "changed", G_CALLBACK (method_changed), self);

	g_signal_connect_swapped (priv->dhcp_client_id, "changed", G_CALLBACK (ce_page_changed), self);

	g_signal_connect (priv->routes_button, "clicked", G_CALLBACK (routes_button_clicked_cb), self);

	return self;
}

static void
free_one_addr (gpointer data)
{
	g_array_free ((GArray *) data, TRUE);
}

static gboolean
parse_netmask (const char *str, guint32 *prefix)
{
	struct in_addr tmp_addr;
	glong tmp_prefix;

	errno = 0;

	/* Is it a prefix? */
	tmp_prefix = strtol (str, NULL, 10);
	if (!errno && tmp_prefix >= 0 && tmp_prefix <= 32) {
		*prefix = tmp_prefix;
		return TRUE;
	}

	/* Is it a netmask? */
	if (inet_aton (str, &tmp_addr)) {
		*prefix = nm_utils_ip4_netmask_to_prefix (tmp_addr.s_addr);
		return TRUE;
	}

	return FALSE;
}

static gboolean
ui_to_setting (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeModel *model;
	GtkTreeIter tree_iter;
	int int_method = IP4_METHOD_AUTO;
	const char *method;
	GArray *dns_servers = NULL;
	GSList *search_domains = NULL;
	GPtrArray *addresses = NULL;
	gboolean valid = FALSE, iter_valid;
	const char *text;
	gboolean ignore_auto_dns = FALSE;
	const char *dhcp_client_id = NULL;
	char **items = NULL, **iter;

	/* Method */
	if (gtk_combo_box_get_active_iter (priv->method, &tree_iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (priv->method_store), &tree_iter,
		                    METHOD_COL_NUM, &int_method, -1);
	}

	switch (int_method) {
	case IP4_METHOD_LINK_LOCAL:
		method = NM_SETTING_IP4_CONFIG_METHOD_LINK_LOCAL;
		break;
	case IP4_METHOD_MANUAL:
		method = NM_SETTING_IP4_CONFIG_METHOD_MANUAL;
		break;
	case IP4_METHOD_SHARED:
		method = NM_SETTING_IP4_CONFIG_METHOD_SHARED;
		break;
	case IP4_METHOD_AUTO_ADDRESSES:
		ignore_auto_dns = TRUE;
		/* fall through */
	default:
		method = NM_SETTING_IP4_CONFIG_METHOD_AUTO;
		break;
	}

	/* IP addresses */
	model = gtk_tree_view_get_model (priv->addr_list);
	iter_valid = gtk_tree_model_get_iter_first (model, &tree_iter);

	addresses = g_ptr_array_sized_new (1);
	while (iter_valid) {
		char *item = NULL;
		struct in_addr tmp_addr, tmp_gateway = { 0 };
		GArray *addr;
		guint32 empty_val = 0, prefix;

		gtk_tree_model_get (model, &tree_iter, COL_ADDRESS, &item, -1);
		if (!item || !inet_aton (item, &tmp_addr)) {
			g_warning ("%s: IPv4 address '%s' missing or invalid!",
			           __func__, item ? item : "<none>");
			g_free (item);
			goto out;
		}
		g_free (item);

		gtk_tree_model_get (model, &tree_iter, COL_PREFIX, &item, -1);
		if (!item) {
			g_warning ("%s: IPv4 prefix '%s' missing!",
			           __func__, item ? item : "<none>");
			goto out;
		}

		if (!parse_netmask (item, &prefix)) {
			g_warning ("%s: IPv4 prefix '%s' invalid!",
			           __func__, item ? item : "<none>");
			g_free (item);
			goto out;
		}
		g_free (item);

		/* Gateway is optional... */
		gtk_tree_model_get (model, &tree_iter, COL_GATEWAY, &item, -1);
		if (item && !inet_aton (item, &tmp_gateway)) {
			g_warning ("%s: IPv4 gateway '%s' invalid!",
			           __func__, item ? item : "<none>");
			g_free (item);
			goto out;
		}
		g_free (item);

		addr = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 3);
		g_array_append_val (addr, tmp_addr.s_addr);
		g_array_append_val (addr, prefix);
		if (tmp_gateway.s_addr)
			g_array_append_val (addr, tmp_gateway.s_addr);
		else
			g_array_append_val (addr, empty_val);
		g_ptr_array_add (addresses, addr);

		iter_valid = gtk_tree_model_iter_next (model, &tree_iter);
	}

	/* Don't pass empty array to the setting */
	if (!addresses->len) {
		g_ptr_array_free (addresses, TRUE);
		addresses = NULL;
	}

	/* DNS servers */
	dns_servers = g_array_new (FALSE, FALSE, sizeof (guint));

	text = gtk_entry_get_text (GTK_ENTRY (priv->dns_servers));
	if (text && strlen (text)) {
		items = g_strsplit_set (text, ", ;:", 0);
		for (iter = items; *iter; iter++) {
			struct in_addr tmp_addr;
			char *stripped = g_strstrip (*iter);

			if (!strlen (stripped))
				continue;

			if (inet_pton (AF_INET, stripped, &tmp_addr))
				g_array_append_val (dns_servers, tmp_addr.s_addr);
			else {
				g_strfreev (items);
				goto out;
			}
		}
		g_strfreev (items);
	}

	/* Search domains */
	text = gtk_entry_get_text (GTK_ENTRY (priv->dns_searches));
	if (text && strlen (text)) {
		items = g_strsplit_set (text, ", ;:", 0);
		for (iter = items; *iter; iter++) {
			char *stripped = g_strstrip (*iter);

			if (strlen (stripped))
				search_domains = g_slist_prepend (search_domains, g_strdup (stripped));
		}

		if (items)
			g_strfreev (items);
	}

	search_domains = g_slist_reverse (search_domains);

	/* DHCP client ID */
	if (!strcmp (method, NM_SETTING_IP4_CONFIG_METHOD_AUTO)) {
		dhcp_client_id = gtk_entry_get_text (priv->dhcp_client_id);
		if (dhcp_client_id && !strlen (dhcp_client_id))
			dhcp_client_id = NULL;
	}

	/* Update setting */
	g_object_set (priv->setting,
				  NM_SETTING_IP4_CONFIG_METHOD, method,
				  NM_SETTING_IP4_CONFIG_ADDRESSES, addresses,
				  NM_SETTING_IP4_CONFIG_DNS, dns_servers,
				  NM_SETTING_IP4_CONFIG_DNS_SEARCH, search_domains,
				  NM_SETTING_IP4_CONFIG_IGNORE_AUTO_DNS, ignore_auto_dns,
				  NM_SETTING_IP4_CONFIG_DHCP_CLIENT_ID, dhcp_client_id,
				  NULL);
	valid = TRUE;

out:
	if (addresses) {
		g_ptr_array_foreach (addresses, (GFunc) free_one_addr, NULL);
		g_ptr_array_free (addresses, TRUE);
	}

	if (dns_servers)
		g_array_free (dns_servers, TRUE);

	g_slist_foreach (search_domains, (GFunc) g_free, NULL);
	g_slist_free (search_domains);

	return valid;
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageIP4 *self = CE_PAGE_IP4 (page);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	if (!ui_to_setting (self))
		return FALSE;
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static void
ce_page_ip4_init (CEPageIP4 *self)
{
}

static void
dispose (GObject *object)
{
	CEPageIP4 *self = CE_PAGE_IP4 (object);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	if (priv->window_group)
		g_object_unref (priv->window_group);

	g_free (priv->connection_id);

	G_OBJECT_CLASS (ce_page_ip4_parent_class)->dispose (object);
}

static void
ce_page_ip4_class_init (CEPageIP4Class *ip4_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (ip4_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (ip4_class);

	g_type_class_add_private (object_class, sizeof (CEPageIP4Private));

	/* virtual methods */
	parent_class->validate = validate;
	object_class->dispose = dispose;
}
