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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2007 Red Hat, Inc.
 */

#ifndef UTILS_H
#define UTILS_H

#include <glib.h>
#include <nm-connection.h>
#include <nm-device.h>

char * utils_bin2hexstr (const char *bytes, int len, int final_len);

const char * utils_get_device_description (NMDevice *device);

gboolean utils_fill_one_crypto_object (NMConnection *connection,
                                       const char *key_name,
                                       gboolean is_private_key,
                                       const char *password,
                                       GByteArray **field,
                                       GError **error);

void utils_fill_connection_certs (NMConnection *connection);

void utils_clear_filled_connection_certs (NMConnection *connection);

guint32 utils_freq_to_channel (guint32 freq);
guint32 utils_channel_to_freq (guint32 channel, char *band);
guint32 utils_find_next_channel (guint32 channel, int direction, char *band);

#endif /* UTILS_H */

