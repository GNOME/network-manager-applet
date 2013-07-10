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

G_DEFINE_TYPE (CEPageBond, ce_page_bond, CE_TYPE_PAGE_MASTER)

#define CE_PAGE_BOND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_BOND, CEPageBondPrivate))

typedef struct {
	NMSettingBond *setting;

	GType slave_type;
	PageNewConnectionFunc new_slave_func;

	GtkWindow *toplevel;

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

static void
bond_private_init (CEPageBond *self)
{
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);
	GtkBuilder *builder;

	builder = CE_PAGE (self)->builder;

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

	priv->toplevel = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (priv->mode),
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
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
connection_removed (CEPageMaster *master, NMConnection *connection)
{
	CEPageBond *self = CE_PAGE_BOND (master);
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);

	if (!ce_page_master_has_slaves (master)) {
		priv->slave_type = G_TYPE_INVALID;
		priv->new_slave_func = NULL;
	}
}

static void
connection_added (CEPageMaster *master, NMConnection *connection)
{
	CEPageBond *self = CE_PAGE_BOND (master);
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);

	/* A bit kludgy... */
	if (nm_connection_is_type (connection, NM_SETTING_INFINIBAND_SETTING_NAME)) {
		priv->slave_type = NM_TYPE_SETTING_INFINIBAND;
		priv->new_slave_func = infiniband_connection_new;
		gtk_combo_box_set_active (priv->mode, MODE_ACTIVE_BACKUP);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->mode), FALSE);
	} else {
		priv->slave_type = NM_TYPE_SETTING_WIRED;
		priv->new_slave_func = ethernet_connection_new;
		gtk_widget_set_sensitive (GTK_WIDGET (priv->mode), TRUE);
	}
}

static void
bonding_mode_changed (GtkComboBox *combo, gpointer user_data)
{
	CEPageBond *self = user_data;
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);

	/* balance-tlb and balance-alb work only with MII monitoring */
	if (   gtk_combo_box_get_active (combo) == MODE_BALANCE_TLB
	    || gtk_combo_box_get_active (combo) == MODE_BALANCE_ALB) {
		gtk_combo_box_set_active (priv->monitoring, MONITORING_MII);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->monitoring), FALSE);
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (priv->monitoring), TRUE);
	}
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
	const char *mode, *frequency, *updelay, *downdelay, *raw_targets;
	char *targets;
	int mode_idx = MODE_BALANCE_RR;

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
	g_signal_connect (priv->mode, "changed",
	                  G_CALLBACK (bonding_mode_changed),
	                  self);
	bonding_mode_changed (priv->mode, self);

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
add_slave (CEPageMaster *master, NewConnectionResultFunc result_func)
{
	CEPageBond *self = CE_PAGE_BOND (master);
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);

	if (priv->new_slave_func) {
		new_connection_of_type (priv->toplevel,
		                        NULL,
		                        CE_PAGE (self)->settings,
		                        priv->new_slave_func,
		                        result_func,
		                        master);
	} else {
		new_connection_dialog (priv->toplevel,
		                       CE_PAGE (self)->settings,
		                       connection_type_filter,
		                       result_func,
		                       master);
	}
}

static void
finish_setup (CEPageBond *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);

	if (error)
		return;

	populate_ui (self);

	g_signal_connect (priv->mode, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->monitoring, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->frequency, "value-changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->updelay, "value-changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->downdelay, "value-changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->arp_targets, "changed", G_CALLBACK (stuff_changed), self);
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

	priv->setting = nm_connection_get_setting_bond (connection);
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
	const char *mode;
	const char *frequency;
	const char *updelay;
	const char *downdelay;
	char *targets;

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

	/* Set bond mode */
	nm_setting_bond_add_option (priv->setting, NM_SETTING_BOND_OPTION_MODE, mode);

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
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageBond *self = CE_PAGE_BOND (page);
	CEPageBondPrivate *priv = CE_PAGE_BOND_GET_PRIVATE (self);

	if (!CE_PAGE_CLASS (ce_page_bond_parent_class)->validate (page, connection, error))
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
	CEPageMasterClass *master_class = CE_PAGE_MASTER_CLASS (bond_class);

	g_type_class_add_private (object_class, sizeof (CEPageBondPrivate));

	/* virtual methods */
	parent_class->validate = validate;

	master_class->connection_added = connection_added;
	master_class->connection_removed = connection_removed;
	master_class->add_slave = add_slave;
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

