/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/***************************************************************************
 *
 * polkit-gnome-auth.h : Show authentication dialogs to gain privileges
 *
 * Copyright (C) 2007 David Zeuthen, <david@fubar.dk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 **************************************************************************/

#ifndef __POLKIT_GNOME_AUTH_H
#define __POLKIT_GNOME_AUTH_H

#include <polkit/polkit.h>

/**
 * PolKitGnomeAuthCB:
 * @action: the #PolKitAction passed in polkit_gnome_auth_show_dialog()
 * @gained_privilege: whether the user gained the privilege. Set to
 * #FALSE if error is set. If set to #TRUE, error will not be set.
 * @error: if the call failed, this parameter will be non-#NULL. The
 * callee shall free the error.
 * @user_data: user data
 *
 * The type of the callback function for
 * polkit_gnome_auth_show_dialog().
 */
typedef void (*PolKitGnomeAuthCB) (PolKitAction *action, gboolean gained_privilege, GError *error, gpointer user_data);

gboolean polkit_gnome_auth_obtain (PolKitAction *action, 
                                   guint xid, 
                                   pid_t pid,
                                   PolKitGnomeAuthCB callback, 
                                   gpointer user_data, 
                                   GError **error);

#endif /* __POLKIT_GNOME_AUTH_H */
