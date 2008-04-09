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

	/* DNS servers */
	GtkEntry *dns_new;
	GtkButton *dns_add;
	GtkButton *dns_remove;
	GtkTreeView *dns_list;

	/* Search domains */
	GtkEntry *search_new;
	GtkButton *search_add;
	GtkButton *search_remove;
	GtkTreeView *search_list;

	gboolean disposed;
} CEPageIP4Private;

#define IP4_METHOD_DHCP   0
#define IP4_METHOD_AUTOIP 1
#define IP4_METHOD_MANUAL 2

static void
ip4_private_init (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GladeXML *xml;

	xml = CE_PAGE (self)->xml;

	priv->method = GTK_COMBO_BOX (glade_xml_get_widget (xml, "ip4_method"));

	priv->dns_new = GTK_ENTRY (glade_xml_get_widget (xml, "ip4_new_dns_server"));
	priv->dns_add = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_add_dns_server"));
	priv->dns_remove = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_delete_dns_server"));
	priv->dns_list = GTK_TREE_VIEW (glade_xml_get_widget (xml, "ip4_dns_servers"));

	priv->search_new = GTK_ENTRY (glade_xml_get_widget (xml, "ip4_new_search_domain"));
	priv->search_add = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_add_search_domain"));
	priv->search_remove = GTK_BUTTON (glade_xml_get_widget (xml, "ip4_delete_search_domain"));
	priv->search_list = GTK_TREE_VIEW (glade_xml_get_widget (xml, "ip4_search_domains"));
}

static void
method_changed (GtkComboBox *combo, gpointer user_data)
{
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

	/* Method */
	if (setting->method) {
		if (!strcmp (setting->method, NM_SETTING_IP4_CONFIG_METHOD_AUTOIP))
			method = IP4_METHOD_AUTOIP;
		else if (!strcmp (setting->method, NM_SETTING_IP4_CONFIG_METHOD_MANUAL))
			method = IP4_METHOD_MANUAL;
	}
	gtk_combo_box_set_active (priv->method, method);
	g_signal_connect (priv->method, "changed", G_CALLBACK (method_changed), self);

	/* DNS servers */
	store = gtk_list_store_new (1, G_TYPE_STRING);
	if (setting->dns) {
		int i;

		for (i = 0; i < setting->dns->len; i++) {
			struct in_addr tmp_addr;
			gchar *ip_string;

			tmp_addr.s_addr = g_array_index (setting->dns, guint, i);
			ip_string = inet_ntoa (tmp_addr);

			gtk_list_store_append (store, &model_iter);
			gtk_list_store_set (store, &model_iter, 0, g_strdup (ip_string), -1);
		}
	}

	gtk_tree_view_set_model (priv->dns_list, GTK_TREE_MODEL (store));
	g_signal_connect (store, "row-inserted", G_CALLBACK (row_added), self);
	g_signal_connect (store, "row-deleted", G_CALLBACK (row_removed), self);
	g_object_unref (store);

	/* Search domains */
	store = gtk_list_store_new (1, G_TYPE_STRING);
	for (iter = setting->dns_search; iter; iter = iter->next) {
		gtk_list_store_append (store, &model_iter);
		gtk_list_store_set (store, &model_iter, 0, iter->data, -1);
	}

	gtk_tree_view_set_model (priv->search_list, GTK_TREE_MODEL (store));
	g_signal_connect (store, "row-inserted", G_CALLBACK (row_added), self);
	g_signal_connect (store, "row-deleted", G_CALLBACK (row_removed), self);
	g_object_unref (store);
}

static void
dns_new_changed (GtkEditable *entry, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (user_data);
	const char *text;
	struct in_addr tmp_addr;

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_add),
							  (text && strlen (text) > 0 && inet_aton (text, &tmp_addr) != 0));
}

static void
dns_add_clicked (GtkButton *button, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (user_data);
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE (gtk_tree_view_get_model (priv->dns_list));
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, g_strdup (gtk_entry_get_text (priv->dns_new)), -1);
	gtk_entry_set_text (priv->dns_new, "");

	ce_page_changed (CE_PAGE (user_data));
}

static void
search_new_changed (GtkEditable *entry, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (user_data);
	const char *text;

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	gtk_widget_set_sensitive (GTK_WIDGET (priv->search_add), (text && strlen (text) > 0));
}

static void
search_add_clicked (GtkButton *button, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (user_data);
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE (gtk_tree_view_get_model (priv->search_list));
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, g_strdup (gtk_entry_get_text (priv->search_new)), -1);
	gtk_entry_set_text (priv->search_new, "");

	ce_page_changed (CE_PAGE (user_data));
}

static void
remove_clicked (GtkButton *button, gpointer user_data)
{
	GtkTreeView *treeview = GTK_TREE_VIEW (user_data);
	GtkTreeSelection *selection;
	GList *selected_rows;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;

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

CEPageIP4 *
ce_page_ip4_new (NMConnection *connection)
{
	CEPageIP4 *self;
	CEPageIP4Private *priv;
	CEPage *parent;
	NMSettingIP4Config *s_ip4;
	GtkTreeSelection *selection;

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

	s_ip4 = (NMSettingIP4Config *) nm_connection_get_setting (connection, NM_TYPE_SETTING_IP4_CONFIG);
	if (s_ip4) {
		/* Duplicate it */
		/* FIXME: Implement nm_setting_dup () in nm-setting.[ch] maybe? */
		GHashTable *hash;

		hash = nm_setting_to_hash (NM_SETTING (s_ip4));
		priv->setting = NM_SETTING_IP4_CONFIG (nm_setting_from_hash (NM_TYPE_SETTING_IP4_CONFIG, hash));
		g_hash_table_destroy (hash);
	} else
		priv->setting = NM_SETTING_IP4_CONFIG (nm_setting_ip4_config_new ());

	populate_ui (self);

	gtk_tree_view_insert_column_with_attributes (priv->dns_list,
	                                             -1, "", gtk_cell_renderer_text_new (),
	                                             "text", 0,
	                                             NULL);

	gtk_tree_view_insert_column_with_attributes (priv->search_list,
	                                             -1, "", gtk_cell_renderer_text_new (),
	                                             "text", 0,
	                                             NULL);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_add), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_remove), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->search_add), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->search_remove), FALSE);

	g_signal_connect (priv->dns_new, "changed", G_CALLBACK (dns_new_changed), self);
	g_signal_connect (priv->dns_add, "clicked", G_CALLBACK (dns_add_clicked), self);
	g_signal_connect (priv->dns_remove, "clicked", G_CALLBACK (remove_clicked), priv->dns_list);
	selection = gtk_tree_view_get_selection (priv->dns_list);
	g_signal_connect (selection, "changed", G_CALLBACK (list_selection_changed), priv->dns_remove);

	g_signal_connect (priv->search_new, "changed", G_CALLBACK (search_new_changed), self);
	g_signal_connect (priv->search_add, "clicked", G_CALLBACK (search_add_clicked), self);
	g_signal_connect (priv->search_remove, "clicked", G_CALLBACK (remove_clicked), priv->search_list);
	selection = gtk_tree_view_get_selection (priv->search_list);
	g_signal_connect (selection, "changed", G_CALLBACK (list_selection_changed), priv->search_remove);

	return self;
}

static void
ui_to_setting (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeModel *model;
	GtkTreeIter iter;
	const char *method;
	GArray *dns_servers;
	GSList *search_domains = NULL;
	gboolean valid;
	gchar *str;

	/* Method */
	switch (gtk_combo_box_get_active (priv->method)) {
	case IP4_METHOD_AUTOIP:
		method = NM_SETTING_IP4_CONFIG_METHOD_AUTOIP;
		break;
	case IP4_METHOD_MANUAL:
		method = NM_SETTING_IP4_CONFIG_METHOD_MANUAL;
		break;
	default:
		method = NM_SETTING_IP4_CONFIG_METHOD_DHCP;
		break;
	}

	/* DNS servers */
	dns_servers = g_array_new (FALSE, FALSE, sizeof (guint));
	model = gtk_tree_view_get_model (priv->dns_list);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	while (valid) {
		struct in_addr tmp_addr;

		str = NULL;
		gtk_tree_model_get (model, &iter, 0, &str, -1);

		if (str && inet_aton (str, &tmp_addr))
			g_array_append_val (dns_servers, tmp_addr.s_addr);

		g_free (str);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* Search domains */
	model = gtk_tree_view_get_model (priv->search_list);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	while (valid) {
		str = NULL;
		gtk_tree_model_get (model, &iter, 0, &str, -1);
		search_domains = g_slist_prepend (search_domains, str);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	search_domains = g_slist_reverse (search_domains);

	/* Update setting */
	g_object_set (priv->setting,
				  NM_SETTING_IP4_CONFIG_METHOD, method,
				  NM_SETTING_IP4_CONFIG_DNS, dns_servers,
				  NM_SETTING_IP4_CONFIG_DNS_SEARCH, search_domains,
				  NULL);

	g_array_free (dns_servers, TRUE);
	g_slist_foreach (search_domains, (GFunc) g_free, NULL);
	g_slist_free (search_domains);
}

static gboolean
validate (CEPage *page)
{
	CEPageIP4 *self = CE_PAGE_IP4 (page);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL);
}

static void
update_connection (CEPage *page, NMConnection *connection)
{
	CEPageIP4 *self = CE_PAGE_IP4 (page);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (page);

	ui_to_setting (self);
	g_object_ref (priv->setting); /* Add setting steals the reference. */
	nm_connection_add_setting (connection, NM_SETTING (priv->setting));
}

static void
ce_page_ip4_init (CEPageIP4 *self)
{
}

static void
dispose (GObject *object)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (object);

	if (priv->disposed)
		return;

	priv->disposed = TRUE;
	g_object_unref (priv->setting);

	G_OBJECT_CLASS (ce_page_ip4_parent_class)->dispose (object);
}

static void
ce_page_ip4_class_init (CEPageIP4Class *ip4_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (ip4_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (ip4_class);

	g_type_class_add_private (object_class, sizeof (CEPageIP4Private));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
	parent_class->update_connection = update_connection;
}
