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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2013 Red Hat, Inc.
 */

#ifndef __PAGE_DCB_H__
#define __PAGE_DCB_H__

#include "nm-connection-editor.h"

#include <nm-connection.h>

#include <glib.h>
#include <glib-object.h>

#include "ce-page.h"

#define CE_TYPE_PAGE_DCB            (ce_page_dcb_get_type ())
#define CE_PAGE_DCB(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CE_TYPE_PAGE_DCB, CEPageDcb))
#define CE_PAGE_DCB_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CE_TYPE_PAGE_DCB, CEPageDcbClass))
#define CE_IS_PAGE_DCB(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CE_TYPE_PAGE_DCB))
#define CE_IS_PAGE_DCB_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CE_TYPE_PAGE_DCB))
#define CE_PAGE_DCB_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CE_TYPE_PAGE_DCB, CEPageDcbClass))

typedef struct {
	CEPage parent;
} CEPageDcb;

typedef struct {
	CEPageClass parent;
} CEPageDcbClass;

GType ce_page_dcb_get_type (void);

CEPage *ce_page_dcb_new (NMConnection *connection,
                         GtkWindow *parent,
                         NMClient *client,
                         NMRemoteSettings *settings,
                         const char **out_secrets_setting_name,
                         GError **error);

#endif  /* __PAGE_DCB_H__ */
