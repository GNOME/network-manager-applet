/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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

GtkWidget *applet_mobile_password_dialog_new (NMConnection *connection,
                                              GtkEntry **out_secret_entry);

/******** Mobile PIN dialog ********/

GtkWidget *applet_mobile_pin_dialog_new (const char *title,
                                         const char *header,
                                         const char *desc,
                                         const char *show_password_label,
                                         gboolean show_auto_unlock_checkbox);

void applet_mobile_pin_dialog_present (GtkWidget *dialog, gboolean now);

void applet_mobile_pin_dialog_destroy (GtkWidget *dialog);

void applet_mobile_pin_dialog_set_entry1 (GtkWidget *dialog,
                                          const char *label,
                                          guint32 minlen,
                                          guint32 maxlen);
const char *applet_mobile_pin_dialog_get_entry1 (GtkWidget *dialog);

void applet_mobile_pin_dialog_set_entry2 (GtkWidget *dialog,
                                          const char *label,
                                          guint32 minlen,
                                          guint32 maxlen);
const char *applet_mobile_pin_dialog_get_entry2 (GtkWidget *dialog);

void applet_mobile_pin_dialog_set_entry3 (GtkWidget *dialog,
                                          const char *label,
                                          guint32 minlen,
                                          guint32 maxlen);
const char *applet_mobile_pin_dialog_get_entry3 (GtkWidget *dialog);

void applet_mobile_pin_dialog_match_23 (GtkWidget *dialog, gboolean match);

gboolean applet_mobile_pin_dialog_get_auto_unlock (GtkWidget *dialog);

void applet_mobile_pin_dialog_start_spinner (GtkWidget *dialog, const char *text);
void applet_mobile_pin_dialog_stop_spinner (GtkWidget *dialog, const char *text);

#endif /* __APPLET_DIALOGS_H__ */
