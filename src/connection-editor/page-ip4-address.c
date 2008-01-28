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

#include "page-ip4-address.h"

GtkWidget *
page_ip4_address_new (NMConnection *connection, const char **title)
{
	GladeXML *xml;
	GtkWidget *page;
	NMSettingIP4Config *s_ip4;

	xml = glade_xml_new (GLADEDIR "/ce-page-ip4-address.glade", "IP4AddressPage", NULL);
	g_return_val_if_fail (xml != NULL, NULL);
	*title = _("IPv4 Addresses");

	page = glade_xml_get_widget (xml, "IP4AddressPage");
	g_return_val_if_fail (page != NULL, NULL);
	g_object_set_data_full (G_OBJECT (page),
	                        "glade-xml", xml,
	                        (GDestroyNotify) g_object_unref);

	s_ip4 = NM_SETTING_IP4_CONFIG (nm_connection_get_setting (connection, NM_TYPE_SETTING_IP4_CONFIG));
	if (s_ip4 == NULL)
		goto out;

out:
	return page;
}


