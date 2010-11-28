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
 * (C) Copyright 2007 - 2009 Red Hat, Inc.
 */

#ifndef EAP_METHOD_PEAP_H
#define EAP_METHOD_PEAP_H

#include "wireless-security.h"

typedef struct {
	struct _EAPMethod parent;

	GtkSizeGroup *size_group;
	WirelessSecurity *sec_parent;
	gboolean is_editor;
} EAPMethodPEAP;

EAPMethodPEAP * eap_method_peap_new (const char *ui_file,
                                     WirelessSecurity *parent,
                                     NMConnection *connection,
                                     gboolean is_editor);

#endif /* EAP_METHOD_PEAP_H */

