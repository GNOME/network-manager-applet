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

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>

#include "page-ip4.h"

G_DEFINE_TYPE (CEPageIP4, ce_page_ip4, CE_TYPE_PAGE)

CEPageIP4 *
ce_page_ip4_new (NMConnection *connection)
{
	CEPageIP4 *self;
	CEPage *parent;
	NMSettingIP4Config *s_ip4;

	self = CE_PAGE_IP4 (g_object_new (CE_TYPE_PAGE_IP4, NULL));
	parent = CE_PAGE (self);

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-ip4.glade", "IP4Page", NULL);
	if (!parent->xml) {
		g_warning ("%s: Couldn't load wired page glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "IP4Page");
	if (!parent->page) {
		g_warning ("%s: Couldn't load wired page from glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("IPv4 Settings"));

	s_ip4 = NM_SETTING_IP4_CONFIG (nm_connection_get_setting (connection, NM_TYPE_SETTING_IP4_CONFIG));
	if (s_ip4 == NULL)
		goto out;

	// FIXME: fill in UI from setting

out:
	return self;
}


static void
ce_page_ip4_init (CEPageIP4 *self)
{
}

static void
ce_page_ip4_class_init (CEPageIP4Class *ip4_class)
{
}

