/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
 *
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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#ifndef __APPLET_DIALOGS_H__
#define __APPLET_DIALOGS_H__

#include <gtk/gtk.h>

#include "applet.h"

void applet_info_dialog_show (NMApplet *applet);

void applet_about_dialog_show (NMApplet *applet);

GtkWidget *applet_warning_dialog_show (const char *message);

GtkWidget *applet_mobile_password_dialog_new (NMDevice *device,
                                              NMConnection *connection,
                                              GtkEntry **out_secret_entry);

#endif /* __APPLET_DIALOGS_H__ */
