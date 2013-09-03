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

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-team-port.h>
#include <nm-utils.h>

#include "page-team-port.h"

G_DEFINE_TYPE (CEPageTeamPort, ce_page_team_port, CE_TYPE_PAGE)

#define CE_PAGE_TEAM_PORT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_TEAM_PORT, CEPageTeamPortPrivate))

typedef struct {
	NMSettingTeamPort *setting;

	GtkEntry *json_config;

} CEPageTeamPortPrivate;

static void
team_port_private_init (CEPageTeamPort *self)
{
	CEPageTeamPortPrivate *priv = CE_PAGE_TEAM_PORT_GET_PRIVATE (self);
	GtkBuilder *builder;

	builder = CE_PAGE (self)->builder;

	priv->json_config = GTK_ENTRY (gtk_builder_get_object (builder, "team_port_json_config"));
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
populate_ui (CEPageTeamPort *self)
{
	CEPageTeamPortPrivate *priv = CE_PAGE_TEAM_PORT_GET_PRIVATE (self);
	NMSettingTeamPort *s_port = priv->setting;
	const char *json_config;

	json_config = nm_setting_team_port_get_config (s_port);
	gtk_entry_set_text (priv->json_config, json_config ? json_config : "");
}

static void
finish_setup (CEPageTeamPort *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPageTeamPortPrivate *priv = CE_PAGE_TEAM_PORT_GET_PRIVATE (self);

	if (error)
		return;

	populate_ui (self);

	g_signal_connect (priv->json_config, "changed", G_CALLBACK (stuff_changed), self);
}

CEPage *
ce_page_team_port_new (NMConnection *connection,
                         GtkWindow *parent_window,
                         NMClient *client,
                         NMRemoteSettings *settings,
                         const char **out_secrets_setting_name,
                         GError **error)
{
	CEPageTeamPort *self;
	CEPageTeamPortPrivate *priv;

	self = CE_PAGE_TEAM_PORT (ce_page_new (CE_TYPE_PAGE_TEAM_PORT,
	                                       connection,
	                                       parent_window,
	                                       client,
	                                       settings,
	                                       UIDIR "/ce-page-team-port.ui",
	                                       "TeamPortPage",
	                                       /* Translators: a "Team Port" is a network
	                                        * device that is part of a team.
	                                        */
	                                       _("Team Port")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Could not load team port user interface."));
		return NULL;
	}

	team_port_private_init (self);
	priv = CE_PAGE_TEAM_PORT_GET_PRIVATE (self);

	priv->setting = nm_connection_get_setting_team_port (connection);
	if (!priv->setting) {
		priv->setting = NM_SETTING_TEAM_PORT (nm_setting_team_port_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageTeamPort *self)
{
	CEPageTeamPortPrivate *priv = CE_PAGE_TEAM_PORT_GET_PRIVATE (self);
	const char *json_config;

	json_config = gtk_entry_get_text (priv->json_config);
	if (!g_strcmp0(json_config, ""))
		json_config = NULL;
	g_object_set (priv->setting,
	              NM_SETTING_TEAM_PORT_CONFIG, json_config,
	              NULL);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageTeamPort *self = CE_PAGE_TEAM_PORT (page);
	CEPageTeamPortPrivate *priv = CE_PAGE_TEAM_PORT_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static void
ce_page_team_port_init (CEPageTeamPort *self)
{
}

static void
ce_page_team_port_class_init (CEPageTeamPortClass *team_port_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (team_port_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (team_port_class);

	g_type_class_add_private (object_class, sizeof (CEPageTeamPortPrivate));

	/* virtual methods */
	parent_class->validate = validate;
}
