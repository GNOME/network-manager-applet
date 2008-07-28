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
 * (C) Copyright 2007 Red Hat, Inc.
 */

#ifndef WS_WEP_KEY_H
#define WS_WEP_KEY_H

typedef enum {
	WEP_KEY_TYPE_HEX = 0,
	WEP_KEY_TYPE_ASCII = 1,
	WEP_KEY_TYPE_PASSPHRASE = 2,
} WEPKeyType;

typedef struct {
	struct _WirelessSecurity parent;

	WEPKeyType type;
	char keys[4][65];
	guint8 cur_index;
} WirelessSecurityWEPKey;

WirelessSecurityWEPKey * ws_wep_key_new (const char *glade_file,
                                         NMConnection *connection,
                                         const char *connection_id,
                                         WEPKeyType type,
                                         gboolean adhoc_create);

WEPKeyType ws_wep_guess_key_type (NMConnection *connection, const char *connection_id);

#endif /* WS_WEP_KEY_H */

