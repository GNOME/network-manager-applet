/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2013 Jiri Pirko <jiri@resnulli.us>
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
 */

#include "config.h"

#include <stdlib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-team.h>

#include "page-team.h"
#include "page-ethernet.h"
#include "page-wifi.h"
#include "page-infiniband.h"
#include "page-vlan.h"
#include "nm-connection-editor.h"
#include "new-connection.h"

G_DEFINE_TYPE (CEPageTeam, ce_page_team, CE_TYPE_PAGE_MASTER)

#define CE_PAGE_TEAM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_TEAM, CEPageTeamPrivate))

typedef struct {
	NMSettingTeam *setting;

	GType slave_type;

	GtkWindow *toplevel;

	GtkTextView *json_config_widget;
	GtkWidget *import_config_button;
} CEPageTeamPrivate;

static void
team_private_init (CEPageTeam *self)
{
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);
	GtkBuilder *builder;

	builder = CE_PAGE (self)->builder;

	priv->json_config_widget = GTK_TEXT_VIEW (gtk_builder_get_object (builder, "team_json_config"));
	priv->import_config_button = GTK_WIDGET (gtk_builder_get_object (builder, "import_config_button"));

	priv->toplevel = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (priv->json_config_widget),
	                                                      GTK_TYPE_WINDOW));
}

static void
json_config_changed (GObject *object, CEPageTeam *self)
{
	ce_page_changed (CE_PAGE (self));
}

static void
import_config_from_file_cb (GtkWidget *dialog, gint response, gpointer user_data)
{
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (user_data);
	GtkTextBuffer *buffer;
	char *filename;
	char *buf = NULL;
	gsize buf_len;

	if (response != GTK_RESPONSE_ACCEPT)
		goto out;

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
	if (!filename) {
		g_warning ("%s: didn't get a filename back from the chooser!", __func__);
		goto out;
	}

	/* Put the file content into JSON config text view. */
	// FIXME: do a cleverer file validity check
	g_file_get_contents (filename, &buf, &buf_len, NULL);
	if (buf_len > 100000) {
		g_free (buf);
		buf = g_strdup (_("Error: file doesn't contain a valid JSON configuration"));
	}

	buffer = gtk_text_view_get_buffer (priv->json_config_widget);
	gtk_text_buffer_set_text (buffer, buf ? buf : "", -1);

	g_free (filename);
	g_free (buf);

out:
	gtk_widget_hide (dialog);
	gtk_widget_destroy (dialog);
}

static void
import_button_clicked_cb (GtkWidget *widget, CEPageTeam *self)
{
	GtkWidget *dialog;

	dialog = gtk_file_chooser_dialog_new (_("Select file to import"),
	                                      NULL,
	                                      GTK_FILE_CHOOSER_ACTION_OPEN,
	                                      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
	                                      NULL);

	g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK (import_config_from_file_cb), self);
	gtk_widget_show_all (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void
populate_ui (CEPageTeam *self)
{
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);
	NMSettingTeam *s_team = priv->setting;
	GtkTextBuffer *buffer;
	const char *json_config;

	buffer = gtk_text_view_get_buffer (priv->json_config_widget);
	json_config = nm_setting_team_get_config (s_team);
	gtk_text_buffer_set_text (buffer, json_config ? json_config : "", -1);

	g_signal_connect (buffer, "changed", G_CALLBACK (json_config_changed), self);
	g_signal_connect (priv->import_config_button, "clicked", G_CALLBACK (import_button_clicked_cb), self);
}

static void
connection_removed (CEPageMaster *master, NMConnection *connection)
{
	CEPageTeam *self = CE_PAGE_TEAM (master);
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);

	if (!ce_page_master_has_slaves (master))
		priv->slave_type = G_TYPE_INVALID;
}

static void
connection_added (CEPageMaster *master, NMConnection *connection)
{
	CEPageTeam *self = CE_PAGE_TEAM (master);
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);

	/* A bit kludgy... */
	if (nm_connection_is_type (connection, NM_SETTING_INFINIBAND_SETTING_NAME))
		priv->slave_type = NM_TYPE_SETTING_INFINIBAND;
	else if (nm_connection_is_type (connection, NM_SETTING_WIRED_SETTING_NAME))
		priv->slave_type = NM_TYPE_SETTING_WIRED;
	else if (nm_connection_is_type (connection, NM_SETTING_WIRELESS_SETTING_NAME))
		priv->slave_type = NM_TYPE_SETTING_WIRELESS;
	else
		priv->slave_type = NM_TYPE_SETTING_VLAN;
}

static void
create_connection (CEPageMaster *master, NMConnection *connection)
{
	NMSetting *s_port;

	s_port = nm_connection_get_setting (connection, NM_TYPE_SETTING_TEAM_PORT);
	if (!s_port) {
		s_port = nm_setting_team_port_new ();
		nm_connection_add_setting (connection, s_port);
	}
}

static gboolean
connection_type_filter_all (GType type, gpointer user_data)
{
	if (type == NM_TYPE_SETTING_WIRED ||
	    type == NM_TYPE_SETTING_WIRELESS ||
	    type == NM_TYPE_SETTING_VLAN ||
	    type == NM_TYPE_SETTING_INFINIBAND)
		return TRUE;
	else
		return FALSE;
}

static gboolean
connection_type_filter_eth (GType type, gpointer user_data)
{
	if (type == NM_TYPE_SETTING_WIRED ||
	    type == NM_TYPE_SETTING_WIRELESS ||
	    type == NM_TYPE_SETTING_VLAN)
		return TRUE;
	else
		return FALSE;
}

static void
add_slave (CEPageMaster *master, NewConnectionResultFunc result_func)
{
	CEPageTeam *self = CE_PAGE_TEAM (master);
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);

	if (priv->slave_type == NM_TYPE_SETTING_INFINIBAND) {
		new_connection_of_type (priv->toplevel,
		                        NULL,
		                        CE_PAGE (self)->settings,
		                        infiniband_connection_new,
		                        result_func,
		                        master);
	} else {
		new_connection_dialog (priv->toplevel,
		                       CE_PAGE (self)->settings,
		                       priv->slave_type == G_TYPE_INVALID ? connection_type_filter_all : connection_type_filter_eth,
		                       result_func,
		                       master);
	}
}

static void
finish_setup (CEPageTeam *self, gpointer unused, GError *error, gpointer user_data)
{
	if (error)
		return;

	populate_ui (self);
}

CEPage *
ce_page_team_new (NMConnection *connection,
				  GtkWindow *parent_window,
				  NMClient *client,
                  NMRemoteSettings *settings,
				  const char **out_secrets_setting_name,
				  GError **error)
{
	CEPageTeam *self;
	CEPageTeamPrivate *priv;

	self = CE_PAGE_TEAM (ce_page_new (CE_TYPE_PAGE_TEAM,
	                                  connection,
	                                  parent_window,
	                                  client,
	                                  settings,
	                                  UIDIR "/ce-page-team.ui",
	                                  "TeamPage",
	                                  _("Team")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC,
		                     _("Could not load team user interface."));
		return NULL;
	}

	team_private_init (self);
	priv = CE_PAGE_TEAM_GET_PRIVATE (self);

	priv->setting = nm_connection_get_setting_team (connection);
	if (!priv->setting) {
		priv->setting = NM_SETTING_TEAM (nm_setting_team_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageTeam *self)
{
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);
	GtkTextBuffer *buffer;
	GtkTextIter start, end;
	char *json_config;

	buffer = gtk_text_view_get_buffer (priv->json_config_widget);
	gtk_text_buffer_get_iter_at_offset (buffer, &start, 0);
	gtk_text_buffer_get_iter_at_offset (buffer, &end, -1);
	json_config = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

	if (g_strcmp0 (json_config, "") == 0)
		json_config = NULL;
	g_object_set (priv->setting,
	              NM_SETTING_TEAM_CONFIG, json_config,
	              NULL);
	g_free (json_config);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageTeam *self = CE_PAGE_TEAM (page);
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);

	if (!CE_PAGE_CLASS (ce_page_team_parent_class)->validate (page, connection, error))
		return FALSE;

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static void
ce_page_team_init (CEPageTeam *self)
{
}

static void
ce_page_team_class_init (CEPageTeamClass *team_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (team_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (team_class);
	CEPageMasterClass *master_class = CE_PAGE_MASTER_CLASS (team_class);

	g_type_class_add_private (object_class, sizeof (CEPageTeamPrivate));

	/* virtual methods */
	parent_class->validate = validate;
	master_class->create_connection = create_connection;
	master_class->connection_added = connection_added;
	master_class->connection_removed = connection_removed;
	master_class->add_slave = add_slave;
}


void
team_connection_new (GtkWindow *parent,
                     const char *detail,
                     NMRemoteSettings *settings,
                     PageNewConnectionResultFunc result_func,
                     gpointer user_data)
{
	NMConnection *connection;
	int team_num, num;
	GSList *connections, *iter;
	NMConnection *conn2;
	NMSettingTeam *s_team;
	const char *iface;
	char *my_iface;

	connection = ce_page_new_connection (_("Team connection %d"),
	                                     NM_SETTING_TEAM_SETTING_NAME,
	                                     TRUE,
	                                     settings,
	                                     user_data);
	nm_connection_add_setting (connection, nm_setting_team_new ());

	/* Find an available interface name */
	team_num = 0;
	connections = nm_remote_settings_list_connections (settings);
	for (iter = connections; iter; iter = iter->next) {
		conn2 = iter->data;

		if (!nm_connection_is_type (conn2, NM_SETTING_TEAM_SETTING_NAME))
			continue;
		s_team = nm_connection_get_setting_team (conn2);
		if (!s_team)
			continue;
		iface = nm_setting_team_get_interface_name (s_team);
		if (!iface || strncmp (iface, "team", 4) != 0 || !g_ascii_isdigit (iface[4]))
			continue;

		num = atoi (iface + 4);
		if (team_num <= num)
			team_num = num + 1;
	}
	g_slist_free (connections);

	my_iface = g_strdup_printf ("team%d", team_num);
	s_team = nm_connection_get_setting_team (connection);
	g_object_set (G_OBJECT (s_team),
	              NM_SETTING_TEAM_INTERFACE_NAME, my_iface,
	              NULL);
	g_free (my_iface);

	(*result_func) (connection, FALSE, NULL, user_data);
}

