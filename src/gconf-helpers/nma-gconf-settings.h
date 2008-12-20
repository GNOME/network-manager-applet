/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * (C) Copyright 2008 Novell, Inc.
 */

#ifndef NMA_GCONF_SETTINGS_H
#define NMA_GCONF_SETTINGS_H

#include <dbus/dbus-glib.h>
#include <nm-connection.h>
#include <nm-settings.h>
#include "nma-gconf-connection.h"

G_BEGIN_DECLS

#define NMA_TYPE_GCONF_SETTINGS            (nma_gconf_settings_get_type ())
#define NMA_GCONF_SETTINGS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NMA_TYPE_GCONF_SETTINGS, NMAGConfSettings))
#define NMA_GCONF_SETTINGS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NMA_TYPE_GCONF_SETTINGS, NMAGConfSettingsClass))
#define NMA_IS_GCONF_SETTINGS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NMA_TYPE_GCONF_SETTINGS))
#define NMA_IS_GCONF_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NMA_TYPE_GCONF_SETTINGS))
#define NMA_GCONF_SETTINGS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NMA_TYPE_GCONF_SETTINGS, NMAGConfSettingsClass))

typedef struct {
	NMSettings parent;
} NMAGConfSettings;

typedef struct {
	NMSettingsClass parent;

	/* Signals */
	void (*new_secrets_requested) (NMAGConfSettings *self,
							 NMAGConfConnection *exported,
							 const char *setting_name,
							 const char **hints,
							 gboolean ask_user,
							 DBusGMethodInvocation *context);
} NMAGConfSettingsClass;

GType nma_gconf_settings_get_type (void);

NMAGConfSettings *nma_gconf_settings_new (void);

NMAGConfConnection *nma_gconf_settings_add_connection (NMAGConfSettings *self,
											NMConnection *connection);

NMAGConfConnection *nma_gconf_settings_get_by_path (NMAGConfSettings *self,
										  const char *path);

NMAGConfConnection *nma_gconf_settings_get_by_dbus_path (NMAGConfSettings *self,
											  const char *path);

NMAGConfConnection *nma_gconf_settings_get_by_connection (NMAGConfSettings *self,
											   NMConnection *connection);

G_END_DECLS

#endif /* NMA_GCONF_SETTINGS_H */
