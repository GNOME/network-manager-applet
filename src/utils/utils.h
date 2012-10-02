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
 * (C) Copyright 2007 - 2012 Red Hat, Inc.
 */

#ifndef UTILS_H
#define UTILS_H

#include <glib.h>
#include <gtk/gtk.h>
#include <nm-connection.h>
#include <nm-device.h>
#include <net/ethernet.h>
#include <nm-access-point.h>

guint32 utils_freq_to_channel (guint32 freq);
guint32 utils_channel_to_freq (guint32 channel, char *band);
guint32 utils_find_next_channel (guint32 channel, int direction, char *band);

gboolean utils_ether_addr_valid (const struct ether_addr *test_addr);

char *utils_hash_ap (const GByteArray *ssid,
                     NM80211Mode mode,
                     guint32 flags,
                     guint32 wpa_flags,
                     guint32 rsn_flags);

char *utils_escape_notify_message (const char *src);

char *utils_create_mobile_connection_id (const char *provider,
                                         const char *plan_name);

void utils_show_error_dialog (const char *title,
                              const char *text1,
                              const char *text2,
                              gboolean modal,
                              GtkWindow *parent);

#define NMA_ERROR (g_quark_from_static_string ("nma-error-quark"))

typedef enum  {
	NMA_ERROR_GENERIC
} NMAError;

#endif /* UTILS_H */

