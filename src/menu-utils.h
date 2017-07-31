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
 * Copyright 2011 - 2014 Red Hat, Inc.
 */

#ifndef _MENU_UTILS_H_
#define _MENU_UTILS_H_

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <NetworkManager.h>

void nma_menu_add_separator_item (GtkWidget *menu);

void nma_menu_add_text_item (GtkWidget *menu, char *text);

void nma_menu_item_add_complex_separator_helper (GtkWidget *menu,
                                                 gboolean indicator_enabled,
                                                 const gchar *label);

GtkWidget *nma_new_menu_item_helper (NMConnection *connection,
                                     NMConnection *active,
                                     gboolean add_active);

#endif /* _MENU_UTILS_H */

