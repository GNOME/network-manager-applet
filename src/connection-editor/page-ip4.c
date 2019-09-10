// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * Copyright 2008 - 2014 Red Hat, Inc.
 */

#include "nm-default.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <gdk/gdkkeysyms.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "page-ip4.h"
#include "ip4-routes-dialog.h"
#include "connection-helpers.h"
#include "ce-utils.h"

G_DEFINE_TYPE (CEPageIP4, ce_page_ip4, CE_TYPE_PAGE)

#define CE_PAGE_IP4_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_IP4, CEPageIP4Private))

#define COL_ADDRESS 0
#define COL_PREFIX 1
#define COL_GATEWAY 2
#define COL_LAST COL_GATEWAY

typedef struct {
	NMSettingIPConfig *setting;
	char *connection_id;
	GType connection_type;

	GtkComboBox *method;
	GtkListStore *method_store;
	int normal_method_idx;
	int hotspot_method_idx;

	/* Addresses */
	GtkWidget *addr_label;
	GtkButton *addr_add;
	GtkButton *addr_delete;
	GtkTreeView *addr_list;
	GtkCellRenderer *addr_cells[COL_LAST + 1];
	GtkTreeModel *addr_saved;
	guint32 addr_method;

	/* DNS servers */
	GtkWidget *dns_servers_label;
	GtkEntry *dns_servers;

	/* Search domains */
	GtkWidget *dns_searches_label;
	GtkEntry *dns_searches;

	/* DHCP stuff */
	GtkWidget *dhcp_client_id_label;
	GtkEntry *dhcp_client_id;

	/* Routes */
	GtkButton *routes_button;

	/* IPv4 required */
	GtkCheckButton *ip4_required;

	GtkWindowGroup *window_group;
	gboolean window_added;

	/* Cached tree view entry for editing-canceled */
	/* Used also for saving old value when switching between cells via mouse
	 * clicks - GTK3 produces neither editing-canceled nor editing-done for
	 * that :( */
	char *last_edited; /* cell text */
	char *last_path;   /* row in treeview */
	int last_column;   /* column in treeview */
} CEPageIP4Private;

#define METHOD_COL_NAME 0
#define METHOD_COL_NUM  1
#define METHOD_COL_ENABLED 2

#define IP4_METHOD_AUTO            0
#define IP4_METHOD_AUTO_ADDRESSES  1
#define IP4_METHOD_MANUAL          2
#define IP4_METHOD_LINK_LOCAL      3
#define IP4_METHOD_SHARED          4
#define IP4_METHOD_DISABLED        5

static void
ip4_private_init (CEPageIP4 *self, NMConnection *connection)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkBuilder *builder;
	GtkTreeIter iter;
	NMSettingConnection *s_con;
	const char *connection_type;
	char *str_auto = NULL, *str_auto_only = NULL;
	gs_free_list GList *cells = NULL;

	builder = CE_PAGE (self)->builder;

	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con);
	connection_type = nm_setting_connection_get_connection_type (s_con);
	g_assert (connection_type);

	priv->connection_type = nm_setting_lookup_type (connection_type);

	if (priv->connection_type == NM_TYPE_SETTING_VPN) {
		str_auto = _("Automatic (VPN)");
		str_auto_only = _("Automatic (VPN) addresses only");
	} else if (   priv->connection_type == NM_TYPE_SETTING_GSM
	           || priv->connection_type == NM_TYPE_SETTING_CDMA) {
		str_auto = _("Automatic");
		str_auto_only = _("Automatic, addresses only");
	} else if (priv->connection_type == NM_TYPE_SETTING_PPPOE) {
		str_auto = _("Automatic (PPPoE)");
		str_auto_only = _("Automatic (PPPoE) addresses only");
	} else {
		str_auto = _("Automatic (DHCP)");
		str_auto_only = _("Automatic (DHCP) addresses only");
	}

	priv->method = GTK_COMBO_BOX (gtk_builder_get_object (builder, "ip4_method"));
	cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (priv->method));
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (priv->method), cells->data,
	                               "sensitive", METHOD_COL_ENABLED);

	priv->method_store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_BOOLEAN);

	gtk_list_store_append (priv->method_store, &iter);
	gtk_list_store_set (priv->method_store, &iter,
	                    METHOD_COL_NAME, str_auto,
	                    METHOD_COL_NUM, IP4_METHOD_AUTO,
	                    METHOD_COL_ENABLED, TRUE,
	                    -1);

	gtk_list_store_append (priv->method_store, &iter);
	gtk_list_store_set (priv->method_store, &iter,
	                    METHOD_COL_NAME, str_auto_only,
	                    METHOD_COL_NUM, IP4_METHOD_AUTO_ADDRESSES,
	                    METHOD_COL_ENABLED, TRUE,
	                    -1);

	/* Manual is pointless for Mobile Broadband */
	if (   priv->connection_type != NM_TYPE_SETTING_GSM
	    && priv->connection_type != NM_TYPE_SETTING_CDMA
	    && priv->connection_type != NM_TYPE_SETTING_VPN) {
		gtk_list_store_append (priv->method_store, &iter);
		gtk_list_store_set (priv->method_store, &iter,
		                    METHOD_COL_NAME, _("Manual"),
		                    METHOD_COL_NUM, IP4_METHOD_MANUAL,
		                    METHOD_COL_ENABLED, TRUE,
		                    -1);
	}

	/* Link-local is pointless for VPNs, Mobile Broadband, and PPPoE */
	if (   priv->connection_type != NM_TYPE_SETTING_VPN
	    && priv->connection_type != NM_TYPE_SETTING_PPPOE
	    && priv->connection_type != NM_TYPE_SETTING_GSM
	    && priv->connection_type != NM_TYPE_SETTING_CDMA) {
		gtk_list_store_append (priv->method_store, &iter);
		gtk_list_store_set (priv->method_store, &iter,
		                    METHOD_COL_NAME, _("Link-Local Only"),
		                    METHOD_COL_NUM, IP4_METHOD_LINK_LOCAL,
		                    METHOD_COL_ENABLED, TRUE,
		                    -1);

		gtk_list_store_append (priv->method_store, &iter);
		gtk_list_store_set (priv->method_store, &iter,
		                    METHOD_COL_NAME, _("Shared to other computers"),
		                    METHOD_COL_NUM, IP4_METHOD_SHARED,
		                    METHOD_COL_ENABLED, TRUE,
		                    -1);
	}

	/* Disabled is only supported for types that also support IPv6 */
	if (connection_supports_ip6 (connection)) {
		gtk_list_store_append (priv->method_store, &iter);
		gtk_list_store_set (priv->method_store, &iter,
		                    METHOD_COL_NAME, _("Disabled"),
		                    METHOD_COL_NUM, IP4_METHOD_DISABLED,
		                    METHOD_COL_ENABLED, TRUE,
		                    -1);
	}
	gtk_combo_box_set_model (priv->method, GTK_TREE_MODEL (priv->method_store));

	priv->addr_label = GTK_WIDGET (gtk_builder_get_object (builder, "ip4_addr_label"));
	priv->addr_add = GTK_BUTTON (gtk_builder_get_object (builder, "ip4_addr_add_button"));
	priv->addr_delete = GTK_BUTTON (gtk_builder_get_object (builder, "ip4_addr_delete_button"));
	priv->addr_list = GTK_TREE_VIEW (gtk_builder_get_object (builder, "ip4_addresses"));

	priv->dns_servers_label = GTK_WIDGET (gtk_builder_get_object (builder, "ip4_dns_servers_label"));
	priv->dns_servers = GTK_ENTRY (gtk_builder_get_object (builder, "ip4_dns_servers_entry"));

	priv->dns_searches_label = GTK_WIDGET (gtk_builder_get_object (builder, "ip4_dns_searches_label"));
	priv->dns_searches = GTK_ENTRY (gtk_builder_get_object (builder, "ip4_dns_searches_entry"));

	priv->dhcp_client_id_label = GTK_WIDGET (gtk_builder_get_object (builder, "ip4_dhcp_client_id_label"));
	priv->dhcp_client_id = GTK_ENTRY (gtk_builder_get_object (builder, "ip4_dhcp_client_id_entry"));

	/* Hide DHCP stuff if it'll never be used for a particular method */
	if (   priv->connection_type == NM_TYPE_SETTING_VPN
	    || priv->connection_type == NM_TYPE_SETTING_GSM
	    || priv->connection_type == NM_TYPE_SETTING_CDMA
	    || priv->connection_type == NM_TYPE_SETTING_PPPOE) {
		gtk_widget_hide (GTK_WIDGET (priv->dhcp_client_id_label));
		gtk_widget_hide (GTK_WIDGET (priv->dhcp_client_id));
	}

	priv->ip4_required = GTK_CHECK_BUTTON (gtk_builder_get_object (builder, "ip4_required_checkbutton"));
	/* Hide IP4-require button if it'll never be used for a particular method */
	if (   priv->connection_type == NM_TYPE_SETTING_VPN
	    || priv->connection_type == NM_TYPE_SETTING_GSM
	    || priv->connection_type == NM_TYPE_SETTING_CDMA
	    || priv->connection_type == NM_TYPE_SETTING_PPPOE)
		gtk_widget_hide (GTK_WIDGET (priv->ip4_required));

	priv->routes_button = GTK_BUTTON (gtk_builder_get_object (builder, "ip4_routes_button"));
}

static void
address_list_changed (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeModel *model;
	int num_rows;

	if (priv->addr_method != IP4_METHOD_SHARED)
		return;

	/* Only one address is allowed in shared mode */
	model = gtk_tree_view_get_model (priv->addr_list);
	num_rows = gtk_tree_model_iter_n_children (model, NULL);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_add), num_rows == 0);
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
	gboolean ip4_required_enabled = TRUE;
	gboolean method_auto = FALSE;
	GtkTreeIter iter;
	GtkListStore *store;
	const char *tooltip = NULL, *label = NULL;

	if (gtk_combo_box_get_active_iter (priv->method, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (priv->method_store), &iter,
		                    METHOD_COL_NUM, &method, -1);
	}

	switch (method) {
	case IP4_METHOD_AUTO:
		addr_enabled = TRUE;
		dhcp_enabled = routes_enabled = TRUE;
		dns_enabled = TRUE;
		method_auto = TRUE;
		tooltip = CE_TOOLTIP_ADDR_AUTO;
		label = CE_LABEL_ADDR_AUTO;
		break;
	case IP4_METHOD_AUTO_ADDRESSES:
		addr_enabled = TRUE;
		dns_enabled = dhcp_enabled = routes_enabled = TRUE;
		tooltip = CE_TOOLTIP_ADDR_AUTO;
		label = CE_LABEL_ADDR_AUTO;
		break;
	case IP4_METHOD_MANUAL:
		addr_enabled = dns_enabled = routes_enabled = TRUE;
		tooltip = CE_TOOLTIP_ADDR_MANUAL;
		label = CE_LABEL_ADDR_MANUAL;
		break;
	case IP4_METHOD_SHARED:
		addr_enabled = TRUE;
		tooltip = CE_TOOLTIP_ADDR_SHARED;
		label = CE_LABEL_ADDR_SHARED;
		break;
	case IP4_METHOD_DISABLED:
		addr_enabled = dns_enabled = dhcp_enabled = routes_enabled = ip4_required_enabled = FALSE;
		break;
	default:
		break;
	}

	priv->addr_method = method;
	gtk_widget_set_tooltip_text (GTK_WIDGET (priv->addr_list), tooltip);
	gtk_label_set_text (GTK_LABEL (priv->addr_label), label);

	/* Disable DHCP stuff for VPNs (though in the future we should support
	 * DHCP over tap interfaces for OpenVPN and vpnc).
	 */
	if (   priv->connection_type == NM_TYPE_SETTING_VPN
	    || priv->connection_type == NM_TYPE_SETTING_GSM
	    || priv->connection_type == NM_TYPE_SETTING_CDMA
	    || priv->connection_type == NM_TYPE_SETTING_PPPOE)
		dhcp_enabled = FALSE;

	gtk_widget_set_sensitive (priv->addr_label, addr_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_add), addr_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_delete), addr_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_list), addr_enabled);

	if (addr_enabled) {
		if (priv->addr_saved) {
			/* Restore old entries */
			gtk_tree_view_set_model (priv->addr_list, priv->addr_saved);
			g_clear_object (&priv->addr_saved);
		}
	} else {
		if (!priv->addr_saved) {
			/* Save current entries, set empty list */
			priv->addr_saved = g_object_ref (gtk_tree_view_get_model (priv->addr_list));
			store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
			gtk_tree_view_set_model (priv->addr_list, GTK_TREE_MODEL (store));
			g_object_unref (store);
		}
	}

	if (method == IP4_METHOD_SHARED) {
		GtkTreeModel *model;
		gboolean iter_valid;
		int i;

		/* Remove all rows except first */
		model = gtk_tree_view_get_model (priv->addr_list);
		iter_valid = gtk_tree_model_get_iter_first (model, &iter);
		for (i = 0; iter_valid; i++) {
			if (i > 0)
				iter_valid = gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
			else
				iter_valid = gtk_tree_model_iter_next (model, &iter);
		}
	}

	address_list_changed (user_data);

	gtk_widget_set_sensitive (priv->dns_servers_label, dns_enabled);
	if (method_auto)
		gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->dns_servers_label), _("Additional DNS ser_vers"));
	else
		gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->dns_servers_label), _("DNS ser_vers"));
	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_servers), dns_enabled);
	if (!dns_enabled)
		gtk_entry_set_text (priv->dns_servers, "");

	gtk_widget_set_sensitive (priv->dns_searches_label, dns_enabled);
	if (method_auto)
		gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->dns_searches_label), _("Additional s_earch domains"));
	else
		gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->dns_searches_label), _("S_earch domains"));
	gtk_widget_set_sensitive (GTK_WIDGET (priv->dns_searches), dns_enabled);
	if (!dns_enabled)
		gtk_entry_set_text (priv->dns_searches, "");

	gtk_widget_set_sensitive (priv->dhcp_client_id_label, dhcp_enabled);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->dhcp_client_id), dhcp_enabled);
	if (!dhcp_enabled)
		gtk_entry_set_text (priv->dhcp_client_id, "");

	gtk_widget_set_sensitive (GTK_WIDGET (priv->ip4_required), ip4_required_enabled);

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
	NMSettingIPConfig *setting = priv->setting;
	GtkListStore *store;
	GtkTreeIter model_iter;
	int method = IP4_METHOD_AUTO;
	GString *string = NULL;
	SetMethodInfo info;
	const char *str_method;
	int i;

	/* Method */
	gtk_combo_box_set_active (priv->method, 0);
	str_method = nm_setting_ip_config_get_method (setting);
	if (str_method) {
		if (!strcmp (str_method, NM_SETTING_IP4_CONFIG_METHOD_LINK_LOCAL))
			method = IP4_METHOD_LINK_LOCAL;
		else if (!strcmp (str_method, NM_SETTING_IP4_CONFIG_METHOD_MANUAL))
			method = IP4_METHOD_MANUAL;
		else if (!strcmp (str_method, NM_SETTING_IP4_CONFIG_METHOD_SHARED))
			method = IP4_METHOD_SHARED;
		else if (!strcmp (str_method, NM_SETTING_IP4_CONFIG_METHOD_DISABLED))
			method = IP4_METHOD_DISABLED;
	}

	if (method == IP4_METHOD_AUTO && nm_setting_ip_config_get_ignore_auto_dns (setting))
		method = IP4_METHOD_AUTO_ADDRESSES;

	info.method = method;
	info.combo = priv->method;
	gtk_tree_model_foreach (GTK_TREE_MODEL (priv->method_store), set_method, &info);

	/* Addresses */
	store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	for (i = 0; i < nm_setting_ip_config_get_num_addresses (setting); i++) {
		NMIPAddress *addr = nm_setting_ip_config_get_address (setting, i);
		char buf[32];

		if (!addr) {
			g_warning ("%s: empty IP4 Address structure!", __func__);
			continue;
		}

		snprintf (buf, sizeof (buf), "%u", nm_ip_address_get_prefix (addr));

		gtk_list_store_append (store, &model_iter);
		gtk_list_store_set (store, &model_iter,
		                    COL_ADDRESS, nm_ip_address_get_address (addr),
		                    COL_PREFIX, buf,
		                    /* FIXME */
		                    COL_GATEWAY, i == 0 ? nm_setting_ip_config_get_gateway (setting) : NULL,
		                    -1);
	}

	gtk_tree_view_set_model (priv->addr_list, GTK_TREE_MODEL (store));
	g_signal_connect_swapped (store, "row-inserted", G_CALLBACK (ce_page_changed), self);
	g_signal_connect_swapped (store, "row-deleted", G_CALLBACK (ce_page_changed), self);
	g_object_unref (store);

	/* DNS servers */
	string = g_string_new ("");
	for (i = 0; i < nm_setting_ip_config_get_num_dns (setting); i++) {
		const char *dns;

		dns = nm_setting_ip_config_get_dns (setting, i);
		if (!dns)
			continue;

		if (string->len)
			g_string_append (string, ", ");
		g_string_append (string, dns);
	}
	gtk_entry_set_text (priv->dns_servers, string->str);
	g_string_free (string, TRUE);

	/* DNS searches */
	string = g_string_new ("");
	for (i = 0; i < nm_setting_ip_config_get_num_dns_searches (setting); i++) {
		if (string->len)
			g_string_append (string, ", ");
		g_string_append (string, nm_setting_ip_config_get_dns_search (setting, i));
	}
	gtk_entry_set_text (priv->dns_searches, string->str);
	g_string_free (string, TRUE);

	if ((method == IP4_METHOD_AUTO) || (method == IP4_METHOD_AUTO_ADDRESSES)) {
		if (nm_setting_ip4_config_get_dhcp_client_id (NM_SETTING_IP4_CONFIG (setting))) {
			gtk_entry_set_text (priv->dhcp_client_id,
			                    nm_setting_ip4_config_get_dhcp_client_id (NM_SETTING_IP4_CONFIG (setting)));
		}
	}

	/* IPv4 required */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->ip4_required),
	                              !nm_setting_ip_config_get_may_fail (setting));
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
	gtk_list_store_set (store, &iter, COL_ADDRESS, "", -1);

	selection = gtk_tree_view_get_selection (priv->addr_list);
	gtk_tree_selection_select_iter (selection, &iter);

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
	column = gtk_tree_view_get_column (priv->addr_list, COL_ADDRESS);

	/* FIXME: using cells->data is pretty fragile but GTK apparently doesn't
	 * have a way to get a cell renderer from a column based on path or iter
	 * or whatever.
	 */
	cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
	gtk_tree_view_set_cursor_on_cell (priv->addr_list, path, column, cells->data, TRUE);

	g_list_free (cells);
	gtk_tree_path_free (path);
	address_list_changed (user_data);
}

static void
addr_delete_clicked (GtkButton *button, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (user_data);
	GtkTreeView *treeview = priv->addr_list;
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

	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

	num_rows = gtk_tree_model_iter_n_children (model, NULL);
	if (num_rows && gtk_tree_model_iter_nth_child (model, &iter, NULL, num_rows - 1)) {
		selection = gtk_tree_view_get_selection (treeview);
		gtk_tree_selection_select_iter (selection, &iter);
	}

	address_list_changed (user_data);
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
cell_editing_canceled (GtkCellRenderer *renderer, gpointer user_data)
{
	CEPageIP4 *self;
	CEPageIP4Private *priv;
	GtkTreeModel *model = NULL;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	guint32 column;

	/* user_data disposed? */
	if (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (renderer), "ce-page-not-valid")))
		return;

	self = CE_PAGE_IP4 (user_data);
	priv = CE_PAGE_IP4_GET_PRIVATE (self);

	if (priv->last_edited) {
		selection = gtk_tree_view_get_selection (priv->addr_list);
		if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
			column = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (renderer), "column"));
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, column, priv->last_edited, -1);
		}

		g_free (priv->last_edited);
		priv->last_edited = NULL;

		ce_page_changed (CE_PAGE (self));
	}

	g_free (priv->last_path);
	priv->last_path = NULL;
	priv->last_column = -1;
}

#define DO_NOT_CYCLE_TAG "do-not-cycle"
#define DIRECTION_TAG    "direction"

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
	guint32 column;
	GtkTreeViewColumn *next_col;
	GtkCellRenderer *next_cell;
	gboolean can_cycle;
	int direction, tmp;

	/* Free auxiliary stuff */
	g_free (priv->last_edited);
	priv->last_edited = NULL;
	g_free (priv->last_path);
	priv->last_path = NULL;
	priv->last_column = -1;

	column = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (cell), "column"));
	gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
	gtk_list_store_set (store, &iter, column, new_text, -1);

	/* Try to autodetect the prefix from the given address if we can */
	if (column == COL_ADDRESS && new_text && strlen (new_text)) {
		char *prefix = NULL;
		const char *guess_prefix = NULL;

		gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (store), &iter, path_string);
		gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, COL_PREFIX, &prefix, -1);
		if (!prefix || !strlen (prefix)) {
			if (!strncmp ("10.", new_text, 3))
				guess_prefix = "8";
			else if (!strncmp ("172.16.", new_text, 7))
				guess_prefix = "16";
			else if (!strncmp ("192.168.", new_text, 8))
				guess_prefix = "24";

			if (guess_prefix)
				gtk_list_store_set (store, &iter, COL_PREFIX, guess_prefix, -1);
		}
		g_free (prefix);
	}

	/* Move focus to the next/previous column */
	can_cycle = g_object_get_data (G_OBJECT (cell), DO_NOT_CYCLE_TAG) == NULL;
	direction = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cell), DIRECTION_TAG));
	g_object_set_data (G_OBJECT (cell), DIRECTION_TAG, NULL);
	g_object_set_data (G_OBJECT (cell), DO_NOT_CYCLE_TAG, NULL);
	if (direction == 0)  /* Move forward by default */
		direction = 1;

	tmp = column + direction;
	if (can_cycle)
		column = tmp < 0 ? COL_LAST : tmp > COL_LAST ? 0 : tmp;
	else
		column = tmp;
	next_col = gtk_tree_view_get_column (priv->addr_list, column);
	next_cell = column <= COL_LAST ? priv->addr_cells[column] : NULL;
	gtk_tree_view_set_cursor_on_cell (priv->addr_list, path, next_col, next_cell, TRUE);

	gtk_tree_path_free (path);
	ce_page_changed (CE_PAGE (self));
}

static void
ip_address_filter_cb (GtkEditable *editable,
                      gchar *text,
                      gint length,
                      gint *position,
                      gpointer user_data)
{
	CEPageIP4 *self = CE_PAGE_IP4 (user_data);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	gboolean changed;

	changed = utils_filter_editable_on_insert_text (editable,
	                                                text, length, position, user_data,
	                                                utils_char_is_ascii_ip4_address,
	                                                ip_address_filter_cb);

	if (changed) {
		g_free (priv->last_edited);
		priv->last_edited = gtk_editable_get_chars (editable, 0, -1);
	}
}

static gboolean
_char_is_ascii_dns_servers (char character)
{
	return utils_char_is_ascii_ip4_address (character) ||
	       character == ' ' ||
	       character == ',' ||
	       character == ':' ||
	       character == ';';
}

static void
dns_servers_filter_cb (GtkEditable *editable,
                       gchar *text,
                       gint length,
                       gint *position,
                       gpointer user_data)
{
	utils_filter_editable_on_insert_text (editable,
	                                      text, length, position, user_data,
	                                      _char_is_ascii_dns_servers,
	                                      dns_servers_filter_cb);
}

static void
delete_text_cb (GtkEditable *editable,
                    gint start_pos,
                    gint end_pos,
                    gpointer user_data)
{
	CEPageIP4 *self = CE_PAGE_IP4 (user_data);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	/* Keep last_edited up-to-date */
	g_free (priv->last_edited);
	priv->last_edited = gtk_editable_get_chars (editable, 0, -1);
}

static gboolean
parse_netmask (const char *str, guint32 *prefix)
{
	struct in_addr tmp_addr;
	glong tmp_prefix;

	if (!str || !*str)
		return FALSE;

	errno = 0;

	/* Is it a prefix? */
	if (!strchr (str, '.')) {
		tmp_prefix = strtol (str, NULL, 10);
		if (!errno && tmp_prefix > 0 && tmp_prefix <= 32) {
			*prefix = tmp_prefix;
			return TRUE;
		}
	}

	/* Is it a netmask? */
	if (inet_pton (AF_INET, str, &tmp_addr) > 0) {
		*prefix = nm_utils_ip4_netmask_to_prefix (tmp_addr.s_addr);
		return TRUE;
	}

	return FALSE;
}

static gboolean
is_address_unspecified (const char *str)
{
	struct in_addr addr;

	if (!str)
		return FALSE;

	return (   inet_pton (AF_INET, str, &addr) == 1
	        && addr.s_addr == INADDR_ANY);
}

static gboolean
gateway_matches_address (const char *gw_str, const char *addr_str, guint32 prefix)
{
	struct in_addr gw, addr;
	guint32 netmask;

	if (!gw_str || inet_pton (AF_INET, gw_str, &gw) != 1)
		return FALSE;
	if (!addr_str || inet_pton (AF_INET, addr_str, &addr) != 1)
		return FALSE;

	netmask = nm_utils_ip4_prefix_to_netmask (prefix);
	return ((addr.s_addr & netmask) == (gw.s_addr & netmask));
}

static gboolean
possibly_wrong_gateway (GtkTreeModel *model, GtkTreeIter *iter, const char *gw_str)
{
	gs_free char *addr_str = NULL, *prefix_str = NULL;
	gboolean addr_valid;
	guint32 prefix;

	gtk_tree_model_get (model, iter, COL_ADDRESS, &addr_str, -1);
	gtk_tree_model_get (model, iter, COL_PREFIX, &prefix_str, -1);
	addr_valid =   addr_str && *addr_str && nm_utils_ipaddr_valid (AF_INET, addr_str) && !is_address_unspecified (addr_str)
	            && parse_netmask (prefix_str, &prefix);

	if (addr_valid && !gateway_matches_address (gw_str, addr_str, prefix))
		return TRUE;
	else
		return FALSE;
}

typedef struct {
	GtkTreeModel *model;
	GtkTreeIter iter;
	guint column;
} AddressLineInfo;

static gboolean
cell_changed_cb (GtkEditable *editable,
                 gpointer user_data)
{
	AddressLineInfo *info = (AddressLineInfo *) user_data;
	char *cell_text;
	GdkRGBA rgba;
	gboolean value_valid = FALSE;
	const char *colorname = NULL;

	cell_text = gtk_editable_get_chars (editable, 0, -1);

	/* The COL_PREFIX can contain IP address or prefix */
	if (info->column == COL_PREFIX) {
		guint32 tmp_prefix;

		value_valid = parse_netmask (cell_text, &tmp_prefix);
	} else {
		struct in_addr tmp_addr;

		if (inet_pton (AF_INET, cell_text, &tmp_addr) > 0)
			value_valid = TRUE;

		 /* 0.0.0.0 is not accepted for address */
		if (info->column == COL_ADDRESS && tmp_addr.s_addr == 0)
			value_valid = FALSE;
		/* Consider empty gateway as valid */
		if (!*cell_text && info->column == COL_GATEWAY)
			value_valid = TRUE;
	}

	/* Change cell's background color while editing */
	colorname = value_valid ? "lightgreen" : "red";

	/* Check gateway against address and prefix */
	if (   info->column == COL_GATEWAY
	    && value_valid
	    && possibly_wrong_gateway (info->model, &info->iter, cell_text))
		colorname = "yellow";

	gdk_rgba_parse (&rgba, colorname);
	utils_override_bg_color (GTK_WIDGET (editable), &rgba);

	g_free (cell_text);
	return FALSE;
}

static gboolean
key_pressed_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	GdkModifierType modifiers;
	GtkCellRenderer *cell = (GtkCellRenderer *) user_data;

	modifiers = event->state & gtk_accelerator_get_default_mod_mask ();

	/*
	 * Change some keys so that they work properly:
	 * We want:
	 *   - Tab should behave the same way as Enter (cycling on cells),
	 *   - Shift-Tab should move in backwards direction.
	 *   - Down arrow moves as Enter, but we have to handle Down arrow on
	 *     key pad.
	 *   - Up arrow should move backwards and we also have to handle Up arrow
	 *     on key pad.
	 *   - Enter should end editing when pressed on last column.
	 *
	 * Note: gtk_cell_editable_editing_done (GTK_CELL_EDITABLE (widget)) cannot be called
	 * in this function, because it would crash with XIM input (GTK_IM_MODULE=xim), see
	 * https://bugzilla.redhat.com/show_bug.cgi?id=747368
	 */

	if (event->keyval == GDK_KEY_Tab && modifiers == 0) {
		/* Tab */
		g_object_set_data (G_OBJECT (cell), DIRECTION_TAG, GINT_TO_POINTER (1));
		utils_fake_return_key (event);
	} else if (event->keyval == GDK_KEY_ISO_Left_Tab && modifiers == GDK_SHIFT_MASK) {
		/* Shift-Tab */
		g_object_set_data (G_OBJECT (cell), DIRECTION_TAG, GINT_TO_POINTER (-1));
		utils_fake_return_key (event);
	} else if (event->keyval == GDK_KEY_KP_Down)
		event->keyval = GDK_KEY_Down;
	else if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up) {
		event->keyval = GDK_KEY_Up;
		g_object_set_data (G_OBJECT (cell), DIRECTION_TAG, GINT_TO_POINTER (-1));
	} else if (   event->keyval == GDK_KEY_Return
	           || event->keyval == GDK_KEY_ISO_Enter
	           || event->keyval == GDK_KEY_KP_Enter)
		g_object_set_data (G_OBJECT (cell), DO_NOT_CYCLE_TAG, GUINT_TO_POINTER (TRUE));

	return FALSE; /* Allow default handler to be called */
}

static void
address_line_info_destroy (gpointer data, GClosure *closure)
{
	g_slice_free (AddressLineInfo, data);
}

static void
cell_editing_started (GtkCellRenderer *cell,
                      GtkCellEditable *editable,
                      const gchar     *path,
                      gpointer         user_data)
{
	CEPageIP4 *self = CE_PAGE_IP4 (user_data);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeModel *model;
	GtkTreeIter iter;
	AddressLineInfo *info;

	if (!GTK_IS_ENTRY (editable)) {
		g_warning ("%s: Unexpected cell editable type.", __func__);
		return;
	}

	/* Initialize last_path and last_column, last_edited is initialized when the cell is edited */
	g_free (priv->last_edited);
	priv->last_edited = NULL;
	g_free (priv->last_path);
	priv->last_path = g_strdup (path);
	priv->last_column = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (cell), "column"));

	/* Set up the entry filter */
	g_signal_connect (G_OBJECT (editable), "insert-text",
	                  (GCallback) ip_address_filter_cb,
	                  user_data);

	g_signal_connect_after (G_OBJECT (editable), "delete-text",
	                        (GCallback) delete_text_cb,
	                        user_data);

	/* Set up handler for IP verifying and changing cell background */
	model = gtk_tree_view_get_model (priv->addr_list);
	gtk_tree_model_get_iter_from_string (model, &iter, priv->last_path);
	info = g_slice_new0 (AddressLineInfo);
	info->model = model;
	info->iter = iter;
	info->column = priv->last_column;
	g_signal_connect_data (G_OBJECT (editable), "changed",
	                       (GCallback) cell_changed_cb,
	                       info,
	                       address_line_info_destroy, 0);

	/* Set up key pressed handler - need to handle Tab key */
	g_signal_connect (G_OBJECT (editable), "key-press-event",
	                  (GCallback) key_pressed_cb,
	                  cell);
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
	const char *method;
	char *tmp;

	toplevel = gtk_widget_get_toplevel (CE_PAGE (self)->page);
	g_return_if_fail (gtk_widget_is_toplevel (toplevel));

	method = nm_setting_ip_config_get_method (priv->setting);
	if (!method || !strcmp (method, NM_SETTING_IP4_CONFIG_METHOD_AUTO))
		automatic = TRUE;

	dialog = ip4_routes_dialog_new (priv->setting, automatic);
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
	tmp = g_strdup_printf (_("Editing IPv4 routes for %s"), priv->connection_id);
	gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	g_free (tmp);

	g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK (routes_dialog_response_cb), self);

	gtk_widget_show_all (dialog);
}

static gboolean
tree_view_button_pressed_cb (GtkWidget *widget,
                             GdkEvent *event,
                             gpointer user_data)
{
	CEPageIP4 *self = CE_PAGE_IP4 (user_data);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	/* last_edited can be set e.g. when we get here by clicking an cell while
	 * editing another cell. GTK3 issue neither editing-canceled nor editing-done
	 * for cell renderer. Thus the previous cell value isn't saved. Store it now. */
	if (priv->last_edited && priv->last_path) {
		GtkTreeIter iter;
		GtkListStore *store = GTK_LIST_STORE (gtk_tree_view_get_model (priv->addr_list));
		GtkTreePath *last_treepath = gtk_tree_path_new_from_string (priv->last_path);

		gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, last_treepath);
		gtk_list_store_set (store, &iter, priv->last_column, priv->last_edited, -1);
		gtk_tree_path_free (last_treepath);

		g_free (priv->last_edited);
		priv->last_edited = NULL;
		g_free (priv->last_path);
		priv->last_path = NULL;
		priv->last_column = -1;
	}

	/* Ignore double clicks events. (They are issued after the single clicks, see GdkEventButton) */
	if (event->type == GDK_2BUTTON_PRESS)
		return TRUE;

	gtk_widget_grab_focus (GTK_WIDGET (priv->addr_list));
	return FALSE;
}

static void
cell_error_data_func (GtkTreeViewColumn *tree_column,
                      GtkCellRenderer *cell,
                      GtkTreeModel *tree_model,
                      GtkTreeIter *iter,
                      gpointer data)
{
	guint32 col = GPOINTER_TO_UINT (data);
	char *value = NULL;
	const char *color = NULL;
	guint32 prefix;
	gboolean invalid = FALSE;

	gtk_tree_model_get (tree_model, iter, col, &value, -1);

	if (col == COL_ADDRESS)
		invalid =    !value || !*value || !nm_utils_ipaddr_valid (AF_INET, value)
		          || is_address_unspecified (value);
	else if (col == COL_PREFIX)
		invalid = !parse_netmask (value, &prefix);
	else if (col == COL_GATEWAY) {
		invalid = value && *value && !nm_utils_ipaddr_valid (AF_INET, value);

		/* Check gateway against address and prefix */
		if (!invalid && possibly_wrong_gateway (tree_model, iter, value))
			color = "#DDC000"; /* darker than "yellow", else selected text is hard to read */
	} else
		g_warn_if_reached ();

	if (invalid)
		color = "red";
	utils_set_cell_background (cell, color, color ? value : NULL);
	g_free (value);
}

static void
finish_setup (CEPageIP4 *self, gpointer user_data)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeSelection *selection;
	gint offset;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	populate_ui (self);

	/* IP Address column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), self);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_ADDRESS));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), self);
	g_signal_connect (renderer, "editing-canceled", G_CALLBACK (cell_editing_canceled), self);
	priv->addr_cells[COL_ADDRESS] = GTK_CELL_RENDERER (renderer);

	offset = gtk_tree_view_insert_column_with_attributes (priv->addr_list,
	                                                      -1, _("Address"), renderer,
	                                                      "text", COL_ADDRESS,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->addr_list), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_cell_data_func (column, renderer, cell_error_data_func,
	                                         GUINT_TO_POINTER (COL_ADDRESS), NULL);

	/* Prefix/netmask column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), self);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_PREFIX));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), self);
	g_signal_connect (renderer, "editing-canceled", G_CALLBACK (cell_editing_canceled), self);
	priv->addr_cells[COL_PREFIX] = GTK_CELL_RENDERER (renderer);

	offset = gtk_tree_view_insert_column_with_attributes (priv->addr_list,
	                                                      -1, _("Netmask"), renderer,
	                                                      "text", COL_PREFIX,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->addr_list), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_cell_data_func (column, renderer, cell_error_data_func,
	                                         GUINT_TO_POINTER (COL_PREFIX), NULL);

	/* Gateway column */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), self);
	g_object_set_data (G_OBJECT (renderer), "column", GUINT_TO_POINTER (COL_GATEWAY));
	g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), self);
	g_signal_connect (renderer, "editing-canceled", G_CALLBACK (cell_editing_canceled), self);
	priv->addr_cells[COL_GATEWAY] = GTK_CELL_RENDERER (renderer);

	offset = gtk_tree_view_insert_column_with_attributes (priv->addr_list,
	                                                      -1, _("Gateway"), renderer,
	                                                      "text", COL_GATEWAY,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->addr_list), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_cell_data_func (column, renderer, cell_error_data_func,
	                                         GUINT_TO_POINTER (COL_GATEWAY), NULL);

	g_signal_connect (priv->addr_list, "button-press-event", G_CALLBACK (tree_view_button_pressed_cb), self);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_add), TRUE);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->addr_delete), FALSE);

	g_signal_connect (priv->addr_add, "clicked", G_CALLBACK (addr_add_clicked), self);
	g_signal_connect (priv->addr_delete, "clicked", G_CALLBACK (addr_delete_clicked), self);
	selection = gtk_tree_view_get_selection (priv->addr_list);
	g_signal_connect (selection, "changed", G_CALLBACK (list_selection_changed), priv->addr_delete);

	g_signal_connect_swapped (priv->dns_servers, "changed", G_CALLBACK (ce_page_changed), self);
	g_signal_connect (priv->dns_servers, "insert-text", G_CALLBACK (dns_servers_filter_cb), self);
	g_signal_connect_swapped (priv->dns_searches, "changed", G_CALLBACK (ce_page_changed), self);

	method_changed (priv->method, self);
	g_signal_connect (priv->method, "changed", G_CALLBACK (method_changed), self);

	g_signal_connect_swapped (priv->dhcp_client_id, "changed", G_CALLBACK (ce_page_changed), self);

	g_signal_connect_swapped (priv->ip4_required, "toggled", G_CALLBACK (ce_page_changed), self);

	g_signal_connect (priv->routes_button, "clicked", G_CALLBACK (routes_button_clicked_cb), self);
}

CEPage *
ce_page_ip4_new (NMConnectionEditor *editor,
                 NMConnection *connection,
                 GtkWindow *parent_window,
                 NMClient *client,
                 const char **out_secrets_setting_name,
                 GError **error)
{
	CEPageIP4 *self;
	CEPageIP4Private *priv;
	NMSettingConnection *s_con;

	self = CE_PAGE_IP4 (ce_page_new (CE_TYPE_PAGE_IP4,
	                                 editor,
	                                 connection,
	                                 parent_window,
	                                 client,
	                                 "/org/gnome/nm_connection_editor/ce-page-ip4.ui",
	                                 "IP4Page",
	                                 _("IPv4 Settings")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Could not load IPv4 user interface."));
		return NULL;
	}

	ip4_private_init (self, connection);
	priv = CE_PAGE_IP4_GET_PRIVATE (self);

	priv->window_group = gtk_window_group_new ();

	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con);
	priv->connection_id = g_strdup (nm_setting_connection_get_id (s_con));

	priv->setting = nm_connection_get_setting_ip4_config (connection);
	g_assert (priv->setting);

	g_signal_connect (self, CE_PAGE_INITIALIZED, G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
free_one_addr (gpointer data)
{
	nm_ip_address_unref ((NMIPAddress *) data);
}

static gboolean
ui_to_setting (CEPageIP4 *self, GError **error)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeModel *model;
	GtkTreeIter tree_iter;
	int int_method = IP4_METHOD_AUTO;
	const char *method;
	GPtrArray *tmp_array = NULL;
	char **dns_servers = NULL;
	char **search_domains = NULL;
	GPtrArray *addresses = NULL;
	char *gateway = NULL;
	gboolean valid = FALSE, iter_valid;
	const char *text;
	gboolean ignore_auto_dns = FALSE;
	const char *dhcp_client_id = NULL;
	char **items = NULL, **iter;
	gboolean may_fail = FALSE;

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
	case IP4_METHOD_DISABLED:
		method = NM_SETTING_IP4_CONFIG_METHOD_DISABLED;
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

	addresses = g_ptr_array_new_with_free_func (free_one_addr);
	while (iter_valid) {
		char *addr = NULL, *netmask = NULL, *addr_gw = NULL;
		NMIPAddress *nm_addr;
		guint32 prefix;

		gtk_tree_model_get (model, &tree_iter,
		                    COL_ADDRESS, &addr,
		                    COL_PREFIX, &netmask,
		                    COL_GATEWAY, &addr_gw,
		                    -1);

		if (   !addr
		    || !nm_utils_ipaddr_valid (AF_INET, addr)
		    || is_address_unspecified (addr)) {
			g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC, _("IPv4 address “%s” invalid"), addr ? addr : "");
			g_free (addr);
			g_free (netmask);
			g_free (addr_gw);
			goto out;
		}

		if (!parse_netmask (netmask, &prefix)) {
			g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC, _("IPv4 address netmask “%s” invalid"), netmask ? netmask : "");
			g_free (addr);
			g_free (netmask);
			g_free (addr_gw);
			goto out;
		}

		/* Gateway is optional... */
		if (addr_gw && *addr_gw && !nm_utils_ipaddr_valid (AF_INET, addr_gw)) {
			g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC, _("IPv4 gateway “%s” invalid"), addr_gw);
			g_free (addr);
			g_free (netmask);
			g_free (addr_gw);
			goto out;
		}

		nm_addr = nm_ip_address_new (AF_INET, addr, prefix, NULL);
		g_ptr_array_add (addresses, nm_addr);

		if (addresses->len == 1 && addr_gw && *addr_gw) {
			gateway = addr_gw;
			addr_gw = NULL;
		}

		g_free (addr);
		g_free (netmask);
		g_free (addr_gw);

		iter_valid = gtk_tree_model_iter_next (model, &tree_iter);
	}

	/* Don't pass empty array to the setting */
	if (!addresses->len) {
		g_ptr_array_free (addresses, TRUE);
		addresses = NULL;
	}

	/* DNS servers */
	tmp_array = g_ptr_array_new ();
	text = gtk_entry_get_text (GTK_ENTRY (priv->dns_servers));
	if (text && strlen (text)) {
		items = g_strsplit_set (text, ", ;:", 0);
		for (iter = items; *iter; iter++) {
			struct in_addr tmp_addr;
			char *stripped = g_strstrip (*iter);

			if (!*stripped)
				continue;

			if (inet_pton (AF_INET, stripped, &tmp_addr))
				g_ptr_array_add (tmp_array, g_strdup (stripped));
			else {
				g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC, _("IPv4 DNS server “%s” invalid"), stripped);
				g_strfreev (items);
				g_ptr_array_free (tmp_array, TRUE);
				goto out;
			}
		}
		g_strfreev (items);
	}
	g_ptr_array_add (tmp_array, NULL);
	dns_servers = (char **) g_ptr_array_free (tmp_array, FALSE);

	/* Search domains */
	tmp_array = g_ptr_array_new ();
	text = gtk_entry_get_text (GTK_ENTRY (priv->dns_searches));
	if (text && strlen (text)) {
		items = g_strsplit_set (text, ", ;:", 0);
		for (iter = items; *iter; iter++) {
			char *stripped = g_strstrip (*iter);

			if (strlen (stripped))
				g_ptr_array_add (tmp_array, g_strdup (stripped));
		}
		g_strfreev (items);
	}
	g_ptr_array_add (tmp_array, NULL);
	search_domains = (char **) g_ptr_array_free (tmp_array, FALSE);

	/* DHCP client ID */
	if (!strcmp (method, NM_SETTING_IP4_CONFIG_METHOD_AUTO)) {
		dhcp_client_id = gtk_entry_get_text (priv->dhcp_client_id);
		if (dhcp_client_id && !strlen (dhcp_client_id))
			dhcp_client_id = NULL;
	}

	may_fail = !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->ip4_required));

	/* Update setting */
	g_object_set (priv->setting,
	              NM_SETTING_IP_CONFIG_METHOD, method,
	              NM_SETTING_IP_CONFIG_ADDRESSES, addresses,
	              NM_SETTING_IP_CONFIG_GATEWAY, gateway,
	              NM_SETTING_IP_CONFIG_DNS, dns_servers,
	              NM_SETTING_IP_CONFIG_DNS_SEARCH, search_domains,
	              NM_SETTING_IP_CONFIG_IGNORE_AUTO_DNS, ignore_auto_dns,
	              NM_SETTING_IP4_CONFIG_DHCP_CLIENT_ID, dhcp_client_id,
	              NM_SETTING_IP_CONFIG_MAY_FAIL, may_fail,
	              NULL);
	valid = TRUE;

out:
	if (addresses)
		g_ptr_array_free (addresses, TRUE);
	g_free (gateway);

	g_strfreev (dns_servers);
	g_strfreev (search_domains);

	return valid;
}

static gboolean
ce_page_validate_v (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageIP4 *self = CE_PAGE_IP4 (page);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	if (!ui_to_setting (self, error))
		return FALSE;
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static gboolean
get_iter_for_method (GtkTreeModel *model, int column, GtkTreeIter *iter)
{
	int col;

	if (gtk_tree_model_get_iter_first (model, iter)) {
		do {
			gtk_tree_model_get (model, iter, METHOD_COL_NUM, &col, -1);
			if (col == column)
				return TRUE;
		} while (gtk_tree_model_iter_next (model, iter));
	}
	return FALSE;
}

static void
toggle_method_sensitivity (CEPage *page, int column, gboolean sensitive)
{
	CEPageIP4 *self = CE_PAGE_IP4 (page);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeModel *model = GTK_TREE_MODEL (priv->method_store);
	GtkTreeIter iter;

	if (get_iter_for_method (model, column, &iter))
		gtk_list_store_set (priv->method_store, &iter, METHOD_COL_ENABLED, sensitive, -1);
}

static gboolean
get_method_sensitivity (CEPage *page, int column)
{
	CEPageIP4 *self = CE_PAGE_IP4 (page);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	GtkTreeModel *model = GTK_TREE_MODEL (priv->method_store);
	GtkTreeIter iter;
	gboolean sensitive = FALSE;

	if (get_iter_for_method (model, column, &iter))
		gtk_tree_model_get (GTK_TREE_MODEL (priv->method_store), &iter, METHOD_COL_ENABLED, &sensitive, -1);
	return sensitive;
}

static void
change_method_combo (CEPage *page, gboolean is_hotspot)
{
	CEPageIP4 *self = CE_PAGE_IP4 (page);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	/* Store previous active method */
	if (get_method_sensitivity (page, IP4_METHOD_AUTO))
		priv->normal_method_idx = gtk_combo_box_get_active (priv->method);
	else
		priv->hotspot_method_idx = gtk_combo_box_get_active (priv->method);

	/* Set active method */
	if (is_hotspot) {
		if (priv->hotspot_method_idx == -1) {
			int method = IP4_METHOD_SHARED;
			if (g_strcmp0 (nm_setting_ip_config_get_method (priv->setting),
			               NM_SETTING_IP4_CONFIG_METHOD_DISABLED) == 0)
				method = IP4_METHOD_DISABLED;
			gtk_combo_box_set_active (priv->method, method);
		} else
			gtk_combo_box_set_active (priv->method, priv->hotspot_method_idx);
	} else {
		if (priv->normal_method_idx != -1)
			gtk_combo_box_set_active (priv->method, priv->normal_method_idx);
	}

	toggle_method_sensitivity (page, IP4_METHOD_AUTO, !is_hotspot);
	toggle_method_sensitivity (page, IP4_METHOD_AUTO_ADDRESSES, !is_hotspot);
	toggle_method_sensitivity (page, IP4_METHOD_MANUAL, !is_hotspot);
	toggle_method_sensitivity (page, IP4_METHOD_LINK_LOCAL, !is_hotspot);
}

static gboolean
inter_page_change (CEPage *page)
{
	gpointer wifi_mode_ap;

	if (nm_connection_editor_inter_page_get_value (page->editor, INTER_PAGE_CHANGE_WIFI_MODE, &wifi_mode_ap)) {
		/* For Wi-Fi AP mode restrict IPv4 methods to shared and disabled */
		if (GPOINTER_TO_UINT (wifi_mode_ap))
			change_method_combo (page, TRUE);
		else
			change_method_combo (page, FALSE);
	}
	return TRUE;
}

static void
ce_page_ip4_init (CEPageIP4 *self)
{
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);

	priv->last_column = -1;
	priv->normal_method_idx = -1;
	priv->hotspot_method_idx = -1;
}

static void
dispose (GObject *object)
{
	CEPageIP4 *self = CE_PAGE_IP4 (object);
	CEPageIP4Private *priv = CE_PAGE_IP4_GET_PRIVATE (self);
	int i;

	g_clear_object (&priv->window_group);

	/* Mark CEPageIP4 object as invalid; store this indication to cells to be usable in callbacks */
	for (i = 0; i <= COL_LAST; i++)
		g_object_set_data (G_OBJECT (priv->addr_cells[i]), "ce-page-not-valid", GUINT_TO_POINTER (1));

	g_clear_pointer (&priv->connection_id, g_free);

	G_OBJECT_CLASS (ce_page_ip4_parent_class)->dispose (object);
}

static void
ce_page_ip4_class_init (CEPageIP4Class *ip4_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (ip4_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (ip4_class);

	g_type_class_add_private (object_class, sizeof (CEPageIP4Private));

	/* virtual methods */
	parent_class->ce_page_validate_v = ce_page_validate_v;
	parent_class->inter_page_change = inter_page_change;
	object_class->dispose = dispose;
}
