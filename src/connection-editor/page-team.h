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

#ifndef __PAGE_TEAM_H__
#define __PAGE_TEAM_H__

#include <nm-connection.h>

#include <glib.h>
#include <glib-object.h>

#include "page-master.h"

#define CE_TYPE_PAGE_TEAM            (ce_page_team_get_type ())
#define CE_PAGE_TEAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CE_TYPE_PAGE_TEAM, CEPageTeam))
#define CE_PAGE_TEAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CE_TYPE_PAGE_TEAM, CEPageTeamClass))
#define CE_IS_PAGE_TEAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CE_TYPE_PAGE_TEAM))
#define CE_IS_PAGE_TEAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CE_TYPE_PAGE_TEAM))
#define CE_PAGE_TEAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CE_TYPE_PAGE_TEAM, CEPageTeamClass))

typedef struct {
	CEPageMaster parent;
} CEPageTeam;

typedef struct {
	CEPageMasterClass parent;
} CEPageTeamClass;

GType ce_page_team_get_type (void);

CEPage *ce_page_team_new (NMConnectionEditor *editor,
                          NMConnection *connection,
                          GtkWindow *parent,
                          NMClient *client,
                          NMRemoteSettings *settings,
                          const char **out_secrets_setting_name,
                          GError **error);

void team_connection_new (GtkWindow *parent,
                          const char *detail,
                          NMRemoteSettings *settings,
                          PageNewConnectionResultFunc result_func,
                          gpointer user_data);

#endif  /* __PAGE_TEAM_H__ */

