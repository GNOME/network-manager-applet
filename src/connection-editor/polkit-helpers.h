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
 * (C) Copyright 2008 Novell, Inc.
 * (C) Copyright 2008 - 2009 Red Hat, Inc.
 */

#ifndef _PK_HELPERS_H_
#define _PK_HELPERS_H_

#include <glib.h>
#ifdef NO_POLKIT_GNOME
#include "polkit-gnome.h"
#else
#include <polkit-gnome/polkit-gnome.h>
#endif

gboolean pk_helper_is_permission_denied_error (GError *error);

gboolean pk_helper_obtain_auth (GError *pk_error,
                                GtkWindow *parent,
                                PolKitGnomeAuthCB callback,
                                gpointer user_data,
                                GError **error);

#endif  /* _PK_HELPERS_H_ */

