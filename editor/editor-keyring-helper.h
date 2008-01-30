/* Wireless Security Editor keyring helper functions
 *
 * Calvin Gaisford <cgaisford@novell.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2006 Novell, Inc.
 */


#ifndef EDITOR_KEYRING_HELPER_H
#define EDITOR_KEYRING_HELPER_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnome/libgnome.h>

#if !GTK_CHECK_VERSION(2,6,0)
#include <gnome.h>
#endif

#include <gnome-keyring.h>

#include "editor-app.h"

GnomeKeyringResult get_key_from_keyring (const char *essid, char **key);
GnomeKeyringResult get_eap_key_from_keyring (const char *essid, char **key);

GnomeKeyringResult set_key_in_keyring (const char *essid, const char *key);
GnomeKeyringResult set_eap_key_in_keyring (const char *essid, const char *key);

#endif // EDITOR_KEYRING_HELPER_H






