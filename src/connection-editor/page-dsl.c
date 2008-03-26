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
#include <nm-setting-pppoe.h>

#include "page-dsl.h"
#include "nm-connection-editor.h"

G_DEFINE_TYPE (CEPageDsl, ce_page_dsl, CE_TYPE_PAGE)

CEPageDsl *
ce_page_dsl_new (NMConnection *connection)
{
	CEPageDsl *self;
	CEPage *parent;
	NMSettingPPPOE *s_pppoe;
	GtkWidget *w;

	self = CE_PAGE_DSL (g_object_new (CE_TYPE_PAGE_DSL, NULL));
	parent = CE_PAGE (self);

	s_pppoe = NM_SETTING_PPPOE (nm_connection_get_setting (connection, NM_TYPE_SETTING_PPPOE));
	if (!s_pppoe) {
		g_warning ("%s: Connection didn't have a PPPOE setting!", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-dsl.glade", "DslPage", NULL);
	if (!parent->xml) {
		g_warning ("%s: Couldn't load dsl page glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "DslPage");
	if (!parent->page) {
		g_warning ("%s: Couldn't load dsl page from glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("DSL"));

	if (s_pppoe->username) {
		w = glade_xml_get_widget (parent->xml, "dsl_username");
		gtk_entry_set_text (GTK_ENTRY (w), s_pppoe->username);
	}

	if (s_pppoe->password) {
		w = glade_xml_get_widget (parent->xml, "dsl_password");
		gtk_entry_set_text (GTK_ENTRY (w), s_pppoe->password);
	}

	if (s_pppoe->service) {
		w = glade_xml_get_widget (parent->xml, "dsl_service");
		gtk_entry_set_text (GTK_ENTRY (w), s_pppoe->service);
	}

	return self;
}

static void
update_connection (CEPage *page, NMConnection *connection)
{
	g_print ("FIXME: update DSL page\n");
}

static void
ce_page_dsl_init (CEPageDsl *self)
{
}

static void
ce_page_dsl_class_init (CEPageDslClass *dsl_class)
{
	CEPageClass *parent_class = CE_PAGE_CLASS (dsl_class);

	/* virtual methods */
	parent_class->update_connection = update_connection;
}
