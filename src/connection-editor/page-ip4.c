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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>

#include "page-ip4.h"

G_DEFINE_TYPE (CEPageIP4, ce_page_ip4, CE_TYPE_PAGE)

#define CE_PAGE_IP4_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_IP4, CEPageIP4Private))

typedef struct {
	NMSettingIP4Config *setting;

	GtkComboBox *method;

	/* Addresses */
	GtkButton *addr_add;
	GtkButton *addr_delete;
	GtkTreeView *addr_list;

	/* DNS servers */
	GtkEntry *dns_servers;

	/* Search domains */
	GtkEntry *dns_searches;

	gboolean disposed;
} CEPageIP4Private;

#define IP4_METHOD_DHCP            0
#define IP4_METHOD_AUTOIP          1
#define IP4_METHOD_MANUAL          2
#define IP4_METHOD_DHCP_MANUAL_DNS 3
#define IP4_METHOD_SHARED          4

#define COL_ADDRESS 0
#define COL_NETMASK 1
#define COL_GATEWAY 2

static void
ip4_private_init (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GladeXML *xml;

	xml = CE_PAGE (self)->xml;

	priv->method = GTK_COMBO_BOX (glade_xml_get_widget (xml, "ip4_method"));

	priv->addr_add = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_addr_add_button"));
	priv->addr_delete = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_addr_delete_button"));
	priv->addr_list = GTK_TREE_VIEW (glade_xml_get_widget (xml, "ip4_addresses"));

	priv->dns_servers = GTK_ENTRY (glade_xml_get_widget (xml, "ip4_dns_servers_entry"));
	priv->dns_searches = GTK_ENTRY (glade_xml_get_widget (xml, "ip4_dns_searches_entry"));
}

static void
method_changed (GtkComboBox *combo, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (user_data);
	gboolean is_shared;

	is_shared = (gtk_combo_box_get_active (priv->method) == IP4_METHOD_SHARED);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_add), !is_shared);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_delete), !is_shared);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_list), !is_shared);
	if (is_shared) {
		GtkListStore *store;

		store = GTK_LIST_STORE (gtk_tree_view_get_model (priv->addr_list));
		gtk_list_store_clear (store);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_servers), !is_shared);
	if (is_shared)
		gtk_entry_set_text (priv->dns_servers, "");

	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_searches), !is_shared);
	if (is_shared)
		gtk_entry_set_text (priv->dns_searches, "");

	ce_page_changed (CE_PAGE (user_data));
}

static void
row_added (GtkTreeModel *tree_model,
		   GtkTreePath *path,
		   GtkTreeIter *iter,
		   gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
row_removed (GtkTreeModel *tree_model,
			 GtkTreePath *path,
			 gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
populate_ui (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	NMSettingIP4Config *setting = priv->setting;
	GtkListStore *store;
	GtkTreeIter model_iter;
	GSList *iter;
	int method = IP4_METHOD_DHCP;
	GString *string = NULL;

	/* Method */
	if (setting->method) {
		if (!strcmp (setting->method, NM_SETTING_IP4_CONFIG_METHOD_AUTOIP))
			method = IP4_METHOD_AUTOIP;
		else if (!strcmp (setting->method, NM_SETTING_IP4_CONFIG_METHOD_MANUAL))
			method = IP4_METHOD_MANUAL;
		else if (!strcmp (setting->method, NM_SETTING_IP4_CONFIG_METHOD_SHARED))
			method = IP4_METHOD_SHARED;
	}

	if (method == IP4_METHOD_DHCP && setting->ignore_dhcp_dns)
		method = IP4_METHOD_DHCP_MANUAL_DNS;

	gtk_combo_box_set_active (priv->method, method);

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

		tmp_addr.s_addr = addr->netmask;
		ip_string = inet_ntoa (tmp_addr);
		gtk_list_store_set (store, &model_iter, COL_NETMASK, g_strdup (ip_string), -1);

		tmp_addr.s_addr = addr->gateway;
		ip_string = inet_ntoa (tmp_addr);
		gtk_list_store_set (store, &model_iter, COL_GATEWAY, g_strdup (ip_string), -1);
	}

	gtk_tree_view_set_model (priv->addr_list, GTK_TREE_MODEL (store));
	g_signal_connect (store, "row-inserted", G_CALLBACK (row_added), self);
	g_signal_connect (store, "row-deleted", G_CALLBACK (row_removed), self);
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
}

static void
dns_servers_changed (GtkEditable *entry, gpointer user_data)
{
	const char *text;
	char **ips = NULL, **iter;
	gboolean valid = TRUE;

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (!text || !strlen (text))
		goto out;

	ips = g_strsplit (text, ",", 0);
	for (iter = ips; *iter; iter++) {
		struct in_addr tmp_addr;
		
		if (inet_aton (g_strstrip (*iter), &tmp_addr) == 0) {
			valid = FALSE;
			break;
		}
	}

	if (ips)
		g_strfreev (ips);

out:
	/* FIXME: do something with 'valid' */
	return;
}

static void
dns_searches_changed (GtkEditable *entry, gpointer user_data)
{
	const char *text;
	char **searches = NULL, **iter;
	gboolean valid = TRUE;

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (!text || !strlen (text))
		goto out;

	searches = g_strsplit (text, ",", 0);
	for (iter = searches; *iter; iter++) {
		/* Need at least one . in the search domain */
		if (!strchr (g_strstrip (*iter), '.')) {
			valid = FALSE;
			break;
		}
	}

	if (searches)
		g_strfreev (searches);

out:
	/* FIXME: do something with 'valid' */
	return;
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

CEPageIP4 *
ce_page_ip4_new (NMConnection *connection)
{
	CEPageIP4 *self;
	CEPageIP4Private *priv;
	CEPage *parent;
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

	ip4_private_init (self);
	priv = CE_PAGE_IP4_GET_PRIVATE (self);

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

	/* Netmask column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), self);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_NETMASK));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), store);

	offset = gtk_tree_view_insert_column_with_attributes (priv->addr_list,
	                                                      -1, _("Netmask"), renderer,
	                                                      "text", COL_NETMASK,
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

	g_signal_connect (priv->dns_servers, "changed", G_CALLBACK (dns_servers_changed), self);
	g_signal_connect (priv->dns_searches, "changed", G_CALLBACK (dns_searches_changed), self);

	method_changed (priv->method, self);
	g_signal_connect (priv->method, "changed", G_CALLBACK (method_changed), self);

	return self;
}

static void
free_one_addr (gpointer data)
{
	g_array_free ((GArray *) data, TRUE);
}

static void
ui_to_setting (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeModel *model;
	GtkTreeIter tree_iter;
	const char *method;
	GArray *dns_servers;
	GSList *search_domains = NULL;
	GPtrArray *addresses = NULL;
	gboolean valid;
	const char *text;
	char **items = NULL, **iter;
	gboolean ignore_dhcp_dns = FALSE;

	/* Method */
	switch (gtk_combo_box_get_active (priv->method)) {
	case IP4_METHOD_AUTOIP:
		method = NM_SETTING_IP4_CONFIG_METHOD_AUTOIP;
		break;
	case IP4_METHOD_MANUAL:
		method = NM_SETTING_IP4_CONFIG_METHOD_MANUAL;
		break;
	case IP4_METHOD_SHARED:
		method = NM_SETTING_IP4_CONFIG_METHOD_SHARED;
		break;
	case IP4_METHOD_DHCP_MANUAL_DNS:
		ignore_dhcp_dns = TRUE;
		/* fall through */
	default:
		method = NM_SETTING_IP4_CONFIG_METHOD_DHCP;
		break;
	}

	/* IP addresses */
	model = gtk_tree_view_get_model (priv->addr_list);
	valid = gtk_tree_model_get_iter_first (model, &tree_iter);

	addresses = g_ptr_array_sized_new (1);
	while (valid) {
		char *str_address = NULL;
		char *str_netmask = NULL;
		char *str_gateway = NULL;
		struct in_addr tmp_addr, tmp_netmask, tmp_gateway = { 0 };
		GArray *addr;
		guint32 empty_val = 0;
		
		gtk_tree_model_get (model, &tree_iter, COL_ADDRESS, &str_address, -1);
		gtk_tree_model_get (model, &tree_iter, COL_NETMASK, &str_netmask, -1);
		gtk_tree_model_get (model, &tree_iter, COL_GATEWAY, &str_gateway, -1);

		if (!str_address || !inet_aton (str_address, &tmp_addr)) {
			g_warning ("%s: IPv4 address '%s' missing or invalid!",
			           __func__, str_address ? str_address : "<none>");
			goto next;
		}

		if (!str_netmask || !inet_aton (str_netmask, &tmp_netmask)) {
			g_warning ("%s: IPv4 netmask '%s' missing or invalid!",
			           __func__, str_netmask ? str_netmask : "<none>");
			goto next;
		}

		/* Gateway is optional... */
		if (str_gateway && !inet_aton (str_gateway, &tmp_gateway)) {
			g_warning ("%s: IPv4 gateway '%s' missing or invalid!",
			           __func__, str_gateway ? str_gateway : "<none>");
			goto next;
		}

		addr = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 3);
		g_array_append_val (addr, tmp_addr.s_addr);
		g_array_append_val (addr, tmp_netmask.s_addr);
		if (tmp_gateway.s_addr)
			g_array_append_val (addr, tmp_gateway.s_addr);
		else
			g_array_append_val (addr, empty_val);
		g_ptr_array_add (addresses, addr);

next:
		valid = gtk_tree_model_iter_next (model, &tree_iter);
	}

	if (!addresses->len) {
		g_ptr_array_free (addresses, TRUE);
		addresses = NULL;
	}

	/* DNS servers */
	dns_servers = g_array_new (FALSE, FALSE, sizeof (guint));

	text = gtk_entry_get_text (GTK_ENTRY (priv->dns_servers));
	if (text && strlen (text)) {
		items = g_strsplit (text, ",", 0);
		for (iter = items; *iter; iter++) {
			struct in_addr tmp_addr;

			if (inet_aton (g_strstrip (*iter), &tmp_addr))
				g_array_append_val (dns_servers, tmp_addr.s_addr);
		}

		if (items)
			g_strfreev (items);
	}

	/* Search domains */
	text = gtk_entry_get_text (GTK_ENTRY (priv->dns_searches));
	if (text && strlen (text)) {
		items = g_strsplit (text, ",", 0);
		for (iter = items; *iter; iter++)
			search_domains = g_slist_prepend (search_domains, g_strdup (g_strstrip (*iter)));

		if (items)
			g_strfreev (items);
	}

	search_domains = g_slist_reverse (search_domains);

	/* Update setting */
	g_object_set (priv->setting,
				  NM_SETTING_IP4_CONFIG_METHOD, method,
				  NM_SETTING_IP4_CONFIG_ADDRESSES, addresses,
				  NM_SETTING_IP4_CONFIG_DNS, dns_servers,
				  NM_SETTING_IP4_CONFIG_DNS_SEARCH, search_domains,
				  NM_SETTING_IP4_CONFIG_IGNORE_DHCP_DNS, ignore_dhcp_dns,
				  NULL);

	if (addresses) {
		g_ptr_array_foreach (addresses, (GFunc) free_one_addr, NULL);
		g_ptr_array_free (addresses, TRUE);
	}

	g_array_free (dns_servers, TRUE);
	g_slist_foreach (search_domains, (GFunc) g_free, NULL);
	g_slist_free (search_domains);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageIP4 *self = CE_PAGE_IP4 (page);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static void
ce_page_ip4_init (CEPageIP4 *self)
{
}

static void
ce_page_ip4_class_init (CEPageIP4Class *ip4_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (ip4_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (ip4_class);

	g_type_class_add_private (object_class, sizeof (CEPageIP4Private));

	/* virtual methods */
	parent_class->validate = validate;
}
