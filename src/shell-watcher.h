/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * Copyright (C) 2012 Red Hat, Inc.
 */

#ifndef SHELL_WATCHER_H
#define SHELL_WATCHER_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>

#define NM_TYPE_SHELL_WATCHER			(nm_shell_watcher_get_type())
#define NM_SHELL_WATCHER(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), NM_TYPE_SHELL_WATCHER, NMShellWatcher))
#define NM_SHELL_WATCHER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), NM_TYPE_SHELL_WATCHER, NMShellWatcherClass))
#define NM_IS_SHELL_WATCHER(object)		(G_TYPE_CHECK_INSTANCE_TYPE((object), NM_TYPE_SHELL_WATCHER))
#define NM_IS_SHELL_WATCHER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), NM_TYPE_SHELL_WATCHER))
#define NM_SHELL_WATCHER_GET_CLASS(object)	(G_TYPE_INSTANCE_GET_CLASS((object), NM_TYPE_SHELL_WATCHER, NMShellWatcherClass))

typedef struct NMShellWatcherPrivate NMShellWatcherPrivate;

typedef struct {
	GObject parent_instance;

	NMShellWatcherPrivate *priv;
} NMShellWatcher;

typedef struct {
	GObjectClass parent_class;
} NMShellWatcherClass;

GType nm_shell_watcher_get_type (void);

NMShellWatcher *nm_shell_watcher_new (void);

gboolean nm_shell_watcher_version_at_least (NMShellWatcher *watcher,
                                            guint major, guint minor);

#endif
