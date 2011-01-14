/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
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
 * Copyright (C) 2011 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <string.h>

#include "applet-agent.h"

G_DEFINE_TYPE (AppletAgent, applet_agent, NM_TYPE_SECRET_AGENT);

#define APPLET_AGENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), APPLET_TYPE_AGENT, AppletAgentPrivate))

typedef struct {


	gboolean disposed;
} AppletAgentPrivate;

/*******************************************************/

static void
get_secrets (NMSecretAgent *agent,
             NMConnection *connection,
             const char *connection_path,
             const char *setting_name,
             const char **hints,
             gboolean request_new,
             NMSecretAgentGetSecretsFunc callback,
             gpointer callback_data)
{
}

static void
cancel_get_secrets (NMSecretAgent *agent,
                    const char *connection_path,
                    const char *setting_name)
{
}

static void
save_secrets (NMSecretAgent *agent,
              NMConnection *connection,
              const char *connection_path,
              NMSecretAgentSaveSecretsFunc callback,
              gpointer callback_data)
{
}

static void
delete_secrets (NMSecretAgent *agent,
                NMConnection *connection,
                const char *connection_path,
                NMSecretAgentDeleteSecretsFunc callback,
                gpointer callback_data)
{
}

/*******************************************************/

AppletAgent *
applet_agent_new (void)
{
	return (AppletAgent *) g_object_new (APPLET_TYPE_AGENT,
	                                     NM_SECRET_AGENT_IDENTIFIER, "org.freedesktop.nm-applet",
	                                     NULL);
}

static void
applet_agent_init (AppletAgent *self)
{
}

static void
dispose (GObject *object)
{
	AppletAgent *self = APPLET_AGENT (object);
	AppletAgentPrivate *priv = APPLET_AGENT_GET_PRIVATE (self);

	if (!priv->disposed) {
		priv->disposed = TRUE;
	}

	G_OBJECT_CLASS (applet_agent_parent_class)->dispose (object);
}

static void
applet_agent_class_init (AppletAgentClass *agent_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (agent_class);
	NMSecretAgentClass *parent_class = NM_SECRET_AGENT_CLASS (agent_class);

	g_type_class_add_private (agent_class, sizeof (AppletAgentPrivate));

	/* virtual methods */
	object_class->dispose = dispose;
	parent_class->get_secrets = get_secrets;
	parent_class->cancel_get_secrets = cancel_get_secrets;
	parent_class->save_secrets = save_secrets;
	parent_class->delete_secrets = delete_secrets;
}

