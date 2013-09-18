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
#include "nm-connection-editor.h"
#include "new-connection.h"

G_DEFINE_TYPE (CEPageTeam, ce_page_team, CE_TYPE_PAGE_MASTER)

#define CE_PAGE_TEAM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_TEAM, CEPageTeamPrivate))

typedef struct {
	NMSettingTeam *setting;

	GtkWindow *toplevel;

	GtkEntry *json_config;
	GtkWidget *json_config_label;
} CEPageTeamPrivate;

static void
team_private_init (CEPageTeam *self)
{
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);
	GtkBuilder *builder;

	builder = CE_PAGE (self)->builder;

	priv->json_config = GTK_ENTRY (gtk_builder_get_object (builder, "team_json_config"));
	priv->json_config_label = GTK_WIDGET (gtk_builder_get_object (builder, "team_json_config_label"));

	priv->toplevel = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (priv->json_config),
	                                                      GTK_TYPE_WINDOW));
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
populate_ui (CEPageTeam *self)
{
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);
	NMSettingTeam *s_team = priv->setting;
	const char *json_config;

	json_config = nm_setting_team_get_config (s_team);
	gtk_entry_set_text (priv->json_config, json_config ? json_config : "");
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
connection_type_filter (GType type, gpointer user_data)
{
	if (type == NM_TYPE_SETTING_WIRED ||
	    type == NM_TYPE_SETTING_WIRELESS ||
	    type == NM_TYPE_SETTING_VLAN ||
	    type == NM_TYPE_SETTING_INFINIBAND)
		return TRUE;
	else
		return FALSE;
}

static void
add_slave (CEPageMaster *master, NewConnectionResultFunc result_func)
{
	CEPageTeam *self = CE_PAGE_TEAM (master);
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);

	new_connection_dialog (priv->toplevel,
	                       CE_PAGE (self)->settings,
	                       connection_type_filter,
	                       result_func,
	                       master);
}

static void
finish_setup (CEPageTeam *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPageTeamPrivate *priv = CE_PAGE_TEAM_GET_PRIVATE (self);

	if (error)
		return;

	populate_ui (self);

	g_signal_connect (priv->json_config, "changed", G_CALLBACK (stuff_changed), self);
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
	const char *json_config;

	json_config = gtk_entry_get_text (priv->json_config);
	if (!g_strcmp0(json_config, ""))
		json_config = NULL;
	g_object_set (priv->setting,
	              NM_SETTING_TEAM_CONFIG, json_config,
	              NULL);
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

