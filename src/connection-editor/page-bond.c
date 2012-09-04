/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * Copyright 2012 Red Hat, Inc.
 */

#include "config.h"

#include <stdlib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-bond.h>

#include "page-bond.h"
#include "page-ethernet.h"
#include "page-infiniband.h"
#include "nm-connection-editor.h"
#include "new-connection.h"

G_DEFINE_TYPE (CEPageBond, ce_page_bond, CE_TYPE_PAGE)

#define CE_PAGE_BOND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_BOND, CEPageBondPrivate))

typedef struct {
	NMRemoteConnection *connection;
	NMClient *client;
	NMRemoteSettings *settings;

	NMSettingBond *setting;
	const char *uuid;

	GType slave_type;
	PageNewConnectionFunc new_slave_func;

	GtkWindow *toplevel;

	GtkEntry *interface_name;
	GtkComboBox *mode;
	GtkComboBox *monitoring;
	GtkSpinButton *frequency;
	GtkSpinButton *updelay;
	GtkWidget *updelay_label;
	GtkWidget *updelay_box;
	GtkSpinButton *downdelay;
	GtkWidget *downdelay_label;
	GtkWidget *downdelay_box;
	GtkEntry *arp_targets;
	GtkWidget *arp_targets_label;

	GtkTable *table;
	int table_row_spacing;
	int updelay_row;
	int downdelay_row;
	int arp_targets_row;

	GtkTreeView *connections;
	GtkTreeModel *connections_model;
	GtkButton *add, *edit, *delete;

} CEPageBondPrivate;

#define MODE_BALANCE_RR    0
#define MODE_ACTIVE_BACKUP 1
#define MODE_BALANCE_XOR   2
#define MODE_BROADCAST     3
#define MODE_802_3AD       4
#define MODE_BALANCE_TLB   5
#define MODE_BALANCE_ALB   6

#define MONITORING_MII 0
#define MONITORING_ARP 1

enum {
	COL_CONNECTION,
	COL_NAME
};

static int
name_sort_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
	NMConnection *conn_a, *conn_b;
	int ret;

	/* We fetch COL_CONNECTION rather than COL_NAME to avoid a strdup/free. */
	gtk_tree_model_get (model, a, COL_CONNECTION, &conn_a, -1);
	gtk_tree_model_get (model, b, COL_CONNECTION, &conn_b, -1);
	ret = strcmp (nm_connection_get_id (conn_a), nm_connection_get_id (conn_b));
	g_object_unref (conn_a);
	g_object_unref (conn_b);

	return ret;
}

static void
bond_private_init (CEPageBond *self)
{
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	GtkBuilder *builder;

	builder = CE_PAGE (self)->builder;

	priv->interface_name = GTK_ENTRY (gtk_builder_get_object (builder, "bond_interface"));
	priv->connections = GTK_TREE_VIEW (gtk_builder_get_object (builder, "bond_connections"));
	priv->connections_model = GTK_TREE_MODEL (gtk_builder_get_object (builder, "bond_connections_model"));
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (priv->connections_model),
	                                 COL_NAME, name_sort_func,
	                                 NULL, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->connections_model),
	                                      COL_NAME, GTK_SORT_ASCENDING);

	priv->mode = GTK_COMBO_BOX (gtk_builder_get_object (builder, "bond_mode"));
	priv->monitoring = GTK_COMBO_BOX (gtk_builder_get_object (builder, "bond_monitoring"));
	priv->frequency = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bond_frequency"));
	priv->updelay = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bond_updelay"));
	priv->updelay_label = GTK_WIDGET (gtk_builder_get_object (builder, "bond_updelay_label"));
	priv->updelay_box = GTK_WIDGET (gtk_builder_get_object (builder, "bond_updelay_box"));
	priv->downdelay = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bond_downdelay"));
	priv->downdelay_label = GTK_WIDGET (gtk_builder_get_object (builder, "bond_downdelay_label"));
	priv->downdelay_box = GTK_WIDGET (gtk_builder_get_object (builder, "bond_downdelay_box"));
	priv->arp_targets = GTK_ENTRY (gtk_builder_get_object (builder, "bond_arp_targets"));
	priv->arp_targets_label = GTK_WIDGET (gtk_builder_get_object (builder, "bond_arp_targets_label"));
	priv->add = GTK_BUTTON (gtk_builder_get_object (builder, "bond_connection_add"));
	priv->edit = GTK_BUTTON (gtk_builder_get_object (builder, "bond_connection_edit"));
	priv->delete = GTK_BUTTON (gtk_builder_get_object (builder, "bond_connection_delete"));

	priv->toplevel = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (priv->connections),
	                                                      GTK_TYPE_WINDOW));

	priv->table = GTK_TABLE (gtk_builder_get_object (builder, "BondPage"));
	priv->table_row_spacing = gtk_table_get_default_row_spacing (priv->table);
	gtk_container_child_get (GTK_CONTAINER (priv->table), priv->updelay_label,
	                         "top-attach", &priv->updelay_row,
	                         NULL);
	gtk_container_child_get (GTK_CONTAINER (priv->table), priv->downdelay_label,
	                         "top-attach", &priv->downdelay_row,
	                         NULL);
	gtk_container_child_get (GTK_CONTAINER (priv->table), priv->arp_targets_label,
	                         "top-attach", &priv->arp_targets_row,
	                         NULL);
}

static void
dispose (GObject *object)
{
	CEPageBond *self = CE_PAGE_BOND (object);
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	GtkTreeIter iter;

	if (priv->settings) {
		g_signal_handlers_disconnect_matched (priv->settings, G_SIGNAL_MATCH_DATA,
		                                      0, 0, NULL, NULL, self);
		g_object_unref (priv->settings);
		priv->settings = NULL;
	}
	if (priv->client) {
		g_object_unref (priv->client);
		priv->client = NULL;
	}
	if (priv->connection) {
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	if (gtk_tree_model_get_iter_first (priv->connections_model, &iter)) {
		do {
			NMRemoteConnection *connection = NULL;

			gtk_tree_model_get (priv->connections_model, &iter,
			                    COL_CONNECTION, &connection,
			                    -1);
			g_signal_handlers_disconnect_matched (connection, G_SIGNAL_MATCH_DATA,
			                                      0, 0, NULL, NULL, self);
			g_object_unref (connection);
		} while (gtk_tree_model_iter_next (priv->connections_model, &iter));
	}

	G_OBJECT_CLASS (ce_page_bond_parent_class)->dispose (object);
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
connection_removed (NMRemoteConnection *connection, gpointer user_data)
{
	CEPageBond *self = CE_PAGE_BOND (user_data);
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	GtkTreeIter iter;

	if (!gtk_tree_model_get_iter_first (priv->connections_model, &iter))
		return;

	do {
		NMRemoteConnection *candidate = NULL;

		gtk_tree_model_get (priv->connections_model, &iter,
		                    COL_CONNECTION, &candidate,
		                    -1);
		if (candidate == connection) {
			gtk_list_store_remove (GTK_LIST_STORE (priv->connections_model), &iter);
			stuff_changed (NULL, self);

			if (!gtk_tree_model_get_iter_first (priv->connections_model, &iter)) {
				priv->slave_type = G_TYPE_INVALID;
				priv->new_slave_func = NULL;
			}
			return;
		}
	} while (gtk_tree_model_iter_next (priv->connections_model, &iter));
}

static void
connection_added (NMRemoteSettings *settings,
                  NMRemoteConnection *connection,
                  gpointer user_data)
{
	CEPageBond *self = CE_PAGE_BOND (user_data);
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	NMSettingConnection *s_con;
	const char *slave_type, *master;
	const char *interface_name;
	GtkTreeIter iter;

	s_con = nm_connection_get_setting_connection (NM_CONNECTION (connection));
	if (!s_con)
		return;

	slave_type = nm_setting_connection_get_slave_type (s_con);
	if (!slave_type || strcmp (slave_type, NM_SETTING_BOND_SETTING_NAME) != 0)
		return;

	master = nm_setting_connection_get_master (s_con);
	if (!master)
		return;

	interface_name = nm_setting_bond_get_interface_name (priv->setting);
	if (!strcmp (master, interface_name)) {
		/* Ugh. Fix that... */
		g_object_set (G_OBJECT (connection),
		              NM_SETTING_CONNECTION_MASTER, priv->uuid,
		              NULL);
		nm_remote_connection_commit_changes (connection, NULL, NULL);
	} else if (strcmp (master, priv->uuid) != 0)
		return;

	gtk_list_store_append (GTK_LIST_STORE (priv->connections_model), &iter);
	gtk_list_store_set (GTK_LIST_STORE (priv->connections_model), &iter,
	                    COL_CONNECTION, connection,
	                    COL_NAME, nm_setting_connection_get_id (s_con),
	                    -1);
	stuff_changed (NULL, self);

	/* FIXME: a bit kludgy */
	if (nm_connection_is_type (NM_CONNECTION (connection), NM_SETTING_INFINIBAND_SETTING_NAME)) {
		priv->slave_type = NM_TYPE_SETTING_INFINIBAND;
		priv->new_slave_func = infiniband_connection_new;
		gtk_combo_box_set_active (priv->mode, MODE_ACTIVE_BACKUP);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->mode), FALSE);
	} else {
		priv->slave_type = NM_TYPE_SETTING_WIRED;
		priv->new_slave_func = ethernet_connection_new;
		gtk_widget_set_sensitive (GTK_WIDGET (priv->mode), TRUE);
	}

	g_signal_connect (connection, NM_REMOTE_CONNECTION_REMOVED,
	                  G_CALLBACK (connection_removed), self);
}

static void
monitoring_mode_changed (GtkComboBox *combo, gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);

	if (gtk_combo_box_get_active (combo) == MONITORING_MII) {
		gtk_widget_show (GTK_WIDGET (priv->updelay));
		gtk_widget_show (priv->updelay_label);
		gtk_widget_show (priv->updelay_box);
		gtk_widget_show (GTK_WIDGET (priv->downdelay));
		gtk_widget_show (priv->downdelay_label);
		gtk_widget_show (priv->downdelay_box);
		gtk_widget_hide (GTK_WIDGET (priv->arp_targets));
		gtk_widget_hide (priv->arp_targets_label);

		gtk_table_set_row_spacing (priv->table, priv->updelay_row, priv->table_row_spacing);
		gtk_table_set_row_spacing (priv->table, priv->downdelay_row, priv->table_row_spacing);
		gtk_table_set_row_spacing (priv->table, priv->arp_targets_row, 0);
	} else {
		gtk_widget_hide (GTK_WIDGET (priv->updelay));
		gtk_widget_hide (priv->updelay_label);
		gtk_widget_hide (priv->updelay_box);
		gtk_widget_hide (GTK_WIDGET (priv->downdelay));
		gtk_widget_hide (priv->downdelay_label);
		gtk_widget_hide (priv->downdelay_box);
		gtk_widget_show (GTK_WIDGET (priv->arp_targets));
		gtk_widget_show (priv->arp_targets_label);

		gtk_table_set_row_spacing (priv->table, priv->updelay_row, 0);
		gtk_table_set_row_spacing (priv->table, priv->downdelay_row, 0);
		gtk_table_set_row_spacing (priv->table, priv->arp_targets_row, priv->table_row_spacing);
	}
}

static void
frequency_changed (GtkSpinButton *button, gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	int frequency, delay;

	frequency = gtk_spin_button_get_value_as_int (priv->frequency);

	/* Round updelay and downdelay up to a multiple of frequency */

	delay = gtk_spin_button_get_value_as_int (priv->updelay);
	if (frequency == 0) {
		if (delay != 0)
			gtk_spin_button_set_value (priv->updelay, 0.0);
	} else if (delay % frequency) {
		delay += frequency - (delay % frequency);
		gtk_spin_button_set_value (priv->updelay, delay);
	}
	gtk_spin_button_set_increments (priv->updelay, frequency, frequency);

	delay = gtk_spin_button_get_value_as_int (priv->downdelay);
	if (frequency == 0) {
		if (delay != 0)
			gtk_spin_button_set_value (priv->downdelay, 0.0);
	} else if (delay % frequency) {
		delay += frequency - (delay % frequency);
		gtk_spin_button_set_value (priv->downdelay, (gdouble)delay);
	}
	gtk_spin_button_set_increments (priv->downdelay, frequency, frequency);
}

static void
delay_changed (GtkSpinButton *button, gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	int frequency, delay;

	/* Clamp to nearest multiple of frequency */

	frequency = gtk_spin_button_get_value_as_int (priv->frequency);
	delay = gtk_spin_button_get_value_as_int (button);
	if (frequency == 0) {
		if (delay != 0)
			gtk_spin_button_set_value (button, 0.0);
	} else if (delay % frequency) {
		if (delay % frequency < frequency / 2)
			delay -= delay % frequency;
		else
			delay += frequency - (delay % frequency);
		gtk_spin_button_set_value (button, (gdouble)delay);
	}
}

static char *
prettify_targets (const char *text)
{
	char **addrs, *targets;

	if (!text || !*text)
		return NULL;

	addrs = g_strsplit (text, ",", -1);
	targets = g_strjoinv (", ", addrs);
	g_strfreev (addrs);

	return targets;
}

static char *
uglify_targets (const char *text)
{
	char **addrs, *targets;
	int i;

	if (!text || !*text)
		return NULL;

	addrs = g_strsplit (text, ",", -1);
	for (i = 0; addrs[i]; i++)
		g_strstrip (addrs[i]);
	targets = g_strjoinv (",", addrs);
	g_strfreev (addrs);

	return targets;
}

static void
populate_ui (CEPageBond *self)
{
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	NMSettingBond *setting = priv->setting;
	NMSettingConnection *s_con;
	const char *iface;
	GSList *connections, *c;
	const char *mode, *frequency, *updelay, *downdelay, *raw_targets;
	char *targets;
	int mode_idx = MODE_BALANCE_RR;

	s_con = nm_connection_get_setting_connection (NM_CONNECTION (priv->connection));
	g_return_if_fail (s_con != NULL);

	/* Interface name */
	iface = nm_setting_bond_get_interface_name (setting);
	gtk_entry_set_text (priv->interface_name, iface ? iface : "");

	/* Bonded connections */
	connections = nm_remote_settings_list_connections (priv->settings);
	for (c = connections; c; c = c->next)
		connection_added (priv->settings, c->data, self);

	/* Mode */
	mode = nm_setting_bond_get_option_by_name (setting, NM_SETTING_BOND_OPTION_MODE);
	if (mode) {
		if (!strcmp (mode, "balance-rr"))
			mode_idx = MODE_BALANCE_RR;
		else if (!strcmp (mode, "active-backup"))
			mode_idx = MODE_ACTIVE_BACKUP;
		else if (!strcmp (mode, "balance-xor"))
			mode_idx = MODE_BALANCE_XOR;
		else if (!strcmp (mode, "broadcast"))
			mode_idx = MODE_BROADCAST;
		else if (!strcmp (mode, "802.3ad"))
			mode_idx = MODE_802_3AD;
		else if (!strcmp (mode, "balance-tlb"))
			mode_idx = MODE_BALANCE_TLB;
		else if (!strcmp (mode, "balance-alb"))
			mode_idx = MODE_BALANCE_ALB;
	}
	gtk_combo_box_set_active (priv->mode, mode_idx);

	/* Monitoring mode/frequency */
	frequency = nm_setting_bond_get_option_by_name (setting, NM_SETTING_BOND_OPTION_ARP_INTERVAL);
	if (frequency) {
		gtk_combo_box_set_active (priv->monitoring, MONITORING_ARP);
	} else {
		gtk_combo_box_set_active (priv->monitoring, MONITORING_MII);
		frequency = nm_setting_bond_get_option_by_name (setting, NM_SETTING_BOND_OPTION_MIIMON);
	}
	g_signal_connect (priv->monitoring, "changed",
	                  G_CALLBACK (monitoring_mode_changed),
	                  self);
	monitoring_mode_changed (priv->monitoring, self);

	if (frequency)
		gtk_spin_button_set_value (priv->frequency, (gdouble) atoi (frequency));
	else
		gtk_spin_button_set_value (priv->frequency, 0.0);
	g_signal_connect (priv->frequency, "value-changed",
	                  G_CALLBACK (frequency_changed),
	                  self);

	updelay = nm_setting_bond_get_option_by_name (setting, NM_SETTING_BOND_OPTION_UPDELAY);
	if (updelay)
		gtk_spin_button_set_value (priv->updelay, (gdouble) atoi (updelay));
	else
		gtk_spin_button_set_value (priv->updelay, 0.0);
	g_signal_connect (priv->updelay, "value-changed",
	                  G_CALLBACK (delay_changed),
	                  self);
	downdelay = nm_setting_bond_get_option_by_name (setting, NM_SETTING_BOND_OPTION_DOWNDELAY);
	if (downdelay)
		gtk_spin_button_set_value (priv->downdelay, (gdouble) atoi (downdelay));
	else
		gtk_spin_button_set_value (priv->downdelay, 0.0);
	g_signal_connect (priv->downdelay, "value-changed",
	                  G_CALLBACK (delay_changed),
	                  self);

	/* ARP targets */
	raw_targets = nm_setting_bond_get_option_by_name (setting, NM_SETTING_BOND_OPTION_ARP_IP_TARGET);
	targets = prettify_targets (raw_targets);
	if (targets) {
		gtk_entry_set_text (priv->arp_targets, targets);
		g_free (targets);
	}
}

static void
connections_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	GtkTreeIter iter;
	GtkTreeModel *model;
	NMRemoteConnection *connection;
	NMSettingConnection *s_con;
	gboolean sensitive = FALSE;

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
		                    0, &connection,
		                    -1);
		s_con = nm_connection_get_setting_connection (NM_CONNECTION (connection));
		g_assert (s_con);
	
		sensitive = !nm_setting_connection_get_read_only (s_con);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (priv->edit), sensitive);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->delete), sensitive);
}

static void
add_response_cb (NMConnectionEditor *editor, NMRemoteConnection *connection,
                 gboolean added, gpointer user_data)
{
	g_object_unref (editor);
}

static void
add_bond_connection (NMConnection *connection,
                     gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	NMSettingConnection *s_con;
	NMConnectionEditor *editor;
	const char *iface_name;
	char *name;

	if (!connection)
		return;

	/* Mark the connection as a bond slave so that the editor knows not
	 * to add IPv4 and IPv6 pages, and rename it.
	 */
	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con != NULL);

	iface_name = gtk_entry_get_text (priv->interface_name);
	if (!*iface_name)
		iface_name = nm_setting_bond_get_interface_name (priv->setting);
	if (!*iface_name)
		iface_name = "bond";
	name = g_strdup_printf (_("%s slave %d"), iface_name,
	                        gtk_tree_model_iter_n_children (priv->connections_model, NULL) + 1);

	g_object_set (G_OBJECT (s_con),
	              NM_SETTING_CONNECTION_ID, name,
	              NM_SETTING_CONNECTION_MASTER, priv->uuid,
	              NM_SETTING_CONNECTION_SLAVE_TYPE, NM_SETTING_BOND_SETTING_NAME,
	              NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
	              NULL);
	g_free (name);

	editor = nm_connection_editor_new (priv->toplevel,
	                                   connection,
	                                   priv->client,
	                                   priv->settings);
	if (!editor) {
		g_object_unref (connection);
		return;
	}

	g_signal_connect (editor, "done", G_CALLBACK (add_response_cb), self);
	nm_connection_editor_run (editor);
}

static gboolean
connection_type_filter (GType type, gpointer user_data)
{
	if (type == NM_TYPE_SETTING_WIRED ||
	    type == NM_TYPE_SETTING_INFINIBAND)
		return TRUE;
	else
		return FALSE;
}

static void
add_clicked (GtkButton *button, gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);

	if (priv->new_slave_func) {
		new_connection_of_type (priv->toplevel,
		                        NULL,
		                        priv->settings,
		                        priv->new_slave_func,
		                        add_bond_connection,
		                        self);
	} else {
		new_connection_dialog (priv->toplevel,
		                       priv->settings,
		                       connection_type_filter,
		                       add_bond_connection,
		                       self);
	}
}

static NMRemoteConnection *
get_selected_connection (CEPageBond *self)
{
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	GtkTreeSelection *selection;
	GList *selected_rows;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	NMRemoteConnection *connection = NULL;

	selection = gtk_tree_view_get_selection (priv->connections);
	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
	if (!selected_rows)
		return NULL;

	if (gtk_tree_model_get_iter (model, &iter, (GtkTreePath *) selected_rows->data))
		gtk_tree_model_get (model, &iter, 0, &connection, -1);

	/* free memory */
	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);

	return connection;
}

static void
edit_done_cb (NMConnectionEditor *editor, GtkResponseType response, gpointer user_data)
{
	g_object_unref (editor);
}

static void
edit_clicked (GtkButton *button, gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	NMConnectionEditor *editor;
	NMRemoteConnection *connection;

	connection = get_selected_connection (self);
	if (!connection)
		return;

	editor = nm_connection_editor_get (NM_CONNECTION (connection));
	if (editor) {
		nm_connection_editor_present (editor);
		return;
	}

	editor = nm_connection_editor_new (priv->toplevel,
	                                   NM_CONNECTION (connection),
	                                   priv->client,
	                                   priv->settings);
	if (!editor)
		return;

	g_signal_connect (editor, "done", G_CALLBACK (edit_done_cb), self);
	nm_connection_editor_run (editor);
}

static void
connection_double_clicked_cb (GtkTreeView *tree_view,
                              GtkTreePath *path,
                              GtkTreeViewColumn *column,
                              gpointer user_data)
{
	edit_clicked (NULL, user_data);
}

static void
delete_clicked (GtkButton *button, gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	NMRemoteConnection *connection;

	connection = get_selected_connection (self);
	if (!connection)
		return;

	delete_connection (priv->toplevel, connection, NULL, NULL);
}

static void
finish_setup (CEPageBond *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	GtkTreeSelection *selection;

	if (error)
		return;

	populate_ui (self);

	g_signal_connect (priv->interface_name, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->mode, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->monitoring, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->frequency, "value-changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->updelay, "value-changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->downdelay, "value-changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->arp_targets, "changed", G_CALLBACK (stuff_changed), self);

	g_signal_connect (priv->add, "clicked", G_CALLBACK (add_clicked), self);
	g_signal_connect (priv->edit, "clicked", G_CALLBACK (edit_clicked), self);
	g_signal_connect (priv->delete, "clicked", G_CALLBACK (delete_clicked), self);

	g_signal_connect (priv->connections, "row-activated", G_CALLBACK (connection_double_clicked_cb), self);

	selection = gtk_tree_view_get_selection (priv->connections);
	g_signal_connect (selection, "changed", G_CALLBACK (connections_selection_changed_cb), self);
	connections_selection_changed_cb (selection, self);
}

CEPage *
ce_page_bond_new (NMConnection *connection,
				  GtkWindow *parent_window,
				  NMClient *client,
                  NMRemoteSettings *settings,
				  const char **out_secrets_setting_name,
				  GError **error)
{
	CEPageBond *self;
	CEPageBondPrivate *priv;
	NMSettingConnection *s_con;

	self = CE_PAGE_BOND (ce_page_new (CE_TYPE_PAGE_BOND,
	                                  connection,
	                                  parent_window,
	                                  client,
	                                  settings,
	                                  UIDIR "/ce-page-bond.ui",
	                                  "BondPage",
	                                  _("Bond")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC,
		                     _("Could not load bond user interface."));
		return NULL;
	}

	bond_private_init (self);
	priv = CE_PAGE_BOND_GET_PRIVATE (self);

	priv->connection = g_object_ref (connection);
	priv->client = g_object_ref (client);
	priv->settings = g_object_ref (settings);

	s_con = nm_connection_get_setting_connection (connection);
	priv->uuid = nm_setting_connection_get_uuid (s_con);

	g_signal_connect (settings, NM_REMOTE_SETTINGS_NEW_CONNECTION,
	                  G_CALLBACK (connection_added), self);

	priv->setting = (NMSettingBond *) nm_connection_get_setting (connection, NM_TYPE_SETTING_BOND);
	if (!priv->setting) {
		priv->setting = NM_SETTING_BOND (nm_setting_bond_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageBond *self)
{
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	const char *interface_name;
	const char *mode;
	const char *frequency;
	const char *updelay;
	const char *downdelay;
	char *targets;

	/* Interface name */
	interface_name = gtk_entry_get_text (priv->interface_name);
	g_object_set (priv->setting,
	              NM_SETTING_BOND_INTERFACE_NAME, interface_name,
	              NULL);

	/* Mode */
	switch (gtk_combo_box_get_active (priv->mode)) {
	case MODE_BALANCE_RR:
		mode = "balance-rr";
		break;
	case MODE_ACTIVE_BACKUP:
		mode = "active-backup";
		break;
	case MODE_BALANCE_XOR:
		mode = "balance-xor";
		break;
	case MODE_BROADCAST:
		mode = "broadcast";
		break;
	case MODE_802_3AD:
		mode = "802.3ad";
		break;
	case MODE_BALANCE_TLB:
		mode = "balance-tlb";
		break;
	case MODE_BALANCE_ALB:
		mode = "balance-alb";
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* Monitoring mode/frequency */
	frequency = gtk_entry_get_text (GTK_ENTRY (priv->frequency));
	updelay = gtk_entry_get_text (GTK_ENTRY (priv->updelay));
	downdelay = gtk_entry_get_text (GTK_ENTRY (priv->downdelay));
	targets = uglify_targets (gtk_entry_get_text (priv->arp_targets));

	switch (gtk_combo_box_get_active (priv->monitoring)) {
	case MONITORING_MII:
		nm_setting_bond_add_option (priv->setting, NM_SETTING_BOND_OPTION_MIIMON, frequency);
		nm_setting_bond_add_option (priv->setting, NM_SETTING_BOND_OPTION_UPDELAY, updelay);
		nm_setting_bond_add_option (priv->setting, NM_SETTING_BOND_OPTION_DOWNDELAY, downdelay);
		nm_setting_bond_remove_option (priv->setting, NM_SETTING_BOND_OPTION_ARP_INTERVAL);
		nm_setting_bond_remove_option (priv->setting, NM_SETTING_BOND_OPTION_ARP_IP_TARGET);
		break;
	case MONITORING_ARP:
		nm_setting_bond_add_option (priv->setting, NM_SETTING_BOND_OPTION_ARP_INTERVAL, frequency);
		if (targets)
			nm_setting_bond_add_option (priv->setting, NM_SETTING_BOND_OPTION_ARP_IP_TARGET, targets);
		else
			nm_setting_bond_remove_option (priv->setting, NM_SETTING_BOND_OPTION_ARP_IP_TARGET);
		nm_setting_bond_remove_option (priv->setting, NM_SETTING_BOND_OPTION_MIIMON);
		nm_setting_bond_remove_option (priv->setting, NM_SETTING_BOND_OPTION_UPDELAY);
		nm_setting_bond_remove_option (priv->setting, NM_SETTING_BOND_OPTION_DOWNDELAY);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_free (targets);

	/* Slaves are updated as they're edited, so nothing to do */
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageBond *self = CE_PAGE_BOND (page);
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	GtkTreeIter iter;

	/* Need at least one slave connection; we don't need to
	 * recursively check that the connections are valid because they
	 * can't end up in the table if they're not.
	 */
	if (!gtk_tree_model_get_iter_first (priv->connections_model, &iter))
		return FALSE;

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static void
ce_page_bond_init (CEPageBond *self)
{
}

static void
ce_page_bond_class_init (CEPageBondClass *bond_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (bond_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (bond_class);

	g_type_class_add_private (object_class, sizeof (CEPageBondPrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
}


void
bond_connection_new (GtkWindow *parent,
                     const char *detail,
                     NMRemoteSettings *settings,
                     PageNewConnectionResultFunc result_func,
                     gpointer user_data)
{
	NMConnection *connection;
	int bond_num, max_bond_num, num;
	GSList *connections, *iter;
	NMConnection *conn2;
	NMSettingBond *s_bond;
	const char *iface;
	char *my_iface;

	connection = ce_page_new_connection (_("Bond connection %d"),
	                                     NM_SETTING_BOND_SETTING_NAME,
	                                     TRUE,
	                                     settings,
	                                     user_data);
	nm_connection_add_setting (connection, nm_setting_bond_new ());

	/* Find an available interface name */
	bond_num = max_bond_num = 0;
	connections = nm_remote_settings_list_connections (settings);
	for (iter = connections; iter; iter = iter->next) {
		conn2 = iter->data;

		if (!nm_connection_is_type (conn2, NM_SETTING_BOND_SETTING_NAME))
			continue;
		s_bond = nm_connection_get_setting_bond (conn2);
		if (!s_bond)
			continue;
		iface = nm_setting_bond_get_interface_name (s_bond);
		if (!iface || strncmp (iface, "bond", 4) != 0 || !g_ascii_isdigit (iface[4]))
			continue;

		num = atoi (iface + 4);
		if (num > max_bond_num)
			max_bond_num = num;
		if (num == bond_num)
			bond_num = max_bond_num + 1;
	}
	g_slist_free (connections);

	my_iface = g_strdup_printf ("bond%d", bond_num);
	s_bond = nm_connection_get_setting_bond (connection);
	g_object_set (G_OBJECT (s_bond),
	              NM_SETTING_BOND_INTERFACE_NAME, my_iface,
	              NULL);
	g_free (my_iface);

	(*result_func) (connection, FALSE, NULL, user_data);
}

