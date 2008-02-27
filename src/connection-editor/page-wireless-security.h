/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#ifndef __PAGE_WIRELESS_SECURITY_H__
#define __PAGE_WIRELESS_SECURITY_H__

#include "nm-connection-editor.h"

#include <nm-connection.h>

#include <glib/gtypes.h>
#include <glib-object.h>

#include "ce-page.h"

#define CE_TYPE_PAGE_WIRELESS_SECURITY            (ce_page_wireless_security_get_type ())
#define CE_PAGE_WIRELESS_SECURITY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CE_TYPE_PAGE_WIRELESS_SECURITY, CEPageWirelessSecurity))
#define CE_PAGE_WIRELESS_SECURITY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CE_TYPE_PAGE_WIRELESS_SECURITY, CEPageWirelessSecurityClass))
#define CE_IS_PAGE_WIRELESS_SECURITY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CE_TYPE_PAGE_WIRELESS_SECURITY))
#define CE_IS_PAGE_WIRELESS_SECURITY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), CE_TYPE_PAGE_WIRELESS_SECURITY))
#define CE_PAGE_WIRELESS_SECURITY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CE_TYPE_PAGE_WIRELESS_SECURITY, CEPageWirelessSecurityClass))

typedef struct {
	CEPage parent;

	gboolean disposed;
	GtkSizeGroup *group;
	GtkWidget *ok_button;
	CEPageWireless *wireless_page;
} CEPageWirelessSecurity;

typedef struct {
	CEPageClass parent;
} CEPageWirelessSecurityClass;

GType ce_page_wireless_security_get_type (void);

CEPageWirelessSecurity *ce_page_wireless_security_new (NMConnection *connection,
                                                       GtkWidget *ok_button,
                                                       CEPageWireless *wireless_page);

#endif  /* __PAGE_WIRELESS_SECURITY_H__ */

