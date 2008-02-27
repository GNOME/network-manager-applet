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
#include <nm-setting-wired.h>

#include "page-wired.h"
#include "nm-connection-editor.h"

G_DEFINE_TYPE (CEPageWired, ce_page_wired, CE_TYPE_PAGE)

CEPageWired *
ce_page_wired_new (NMConnection *connection)
{
	CEPageWired *self;
	CEPage *parent;
	NMSettingWired *s_wired;
	GtkWidget *port;
	int port_idx = 0;
	GtkWidget *speed;
	int speed_idx = 0;
	GtkWidget *duplex;
	GtkWidget *autoneg;
	GtkWidget *mtu;
	int mtu_def;

	self = CE_PAGE_WIRED (g_object_new (CE_TYPE_PAGE_WIRED, NULL));
	parent = CE_PAGE (self);

	s_wired = NM_SETTING_WIRED (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRED));
	if (!s_wired) {
		g_warning ("%s: Connection didn't have a wired setting!", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-wired.glade", "WiredPage", NULL);
	if (!parent->xml) {
		g_warning ("%s: Couldn't load wired page glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "WiredPage");
	if (!parent->page) {
		g_warning ("%s: Couldn't load wired page from glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("Wired"));

	port = glade_xml_get_widget (parent->xml, "wired_port");
	speed = glade_xml_get_widget (parent->xml, "wired_speed");
	duplex = glade_xml_get_widget (parent->xml, "wired_duplex");
	autoneg = glade_xml_get_widget (parent->xml, "wired_autonegotiate");

	mtu = glade_xml_get_widget (parent->xml, "wired_mtu");
	mtu_def = ce_get_property_default (NM_SETTING (s_wired), NM_SETTING_WIRED_MTU);
	g_signal_connect (G_OBJECT (mtu), "output",
	                  (GCallback) ce_spin_output_with_default,
	                  GINT_TO_POINTER (mtu_def));

	if (s_wired->port) {
		if (!strcmp (s_wired->port, "tp"))
			port_idx = 1;
		else if (!strcmp (s_wired->port, "aui"))
			port_idx = 2;
		else if (!strcmp (s_wired->port, "bnc"))
			port_idx = 3;
		else if (!strcmp (s_wired->port, "mii"))
			port_idx = 4;
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (port), port_idx);

	switch (s_wired->speed) {
	case 10:
		speed_idx = 1;
		break;
	case 100:
		speed_idx = 2;
		break;
	case 1000:
		speed_idx = 3;
		break;
	case 10000:
		speed_idx = 4;
		break;
	default:
		break;
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (speed), speed_idx);

	if (!strcmp (s_wired->duplex ? s_wired->duplex : "", "half"))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (duplex), FALSE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (duplex), TRUE);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoneg), s_wired->auto_negotiate);

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (mtu), (gdouble) s_wired->mtu);

	/* FIXME: MAC address */

	return self;
}

static void
ce_page_wired_init (CEPageWired *self)
{
}

static void
ce_page_wired_class_init (CEPageWiredClass *wired_class)
{
}

