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
#include "gconf-helpers.h"

G_DEFINE_TYPE (CEPageDsl, ce_page_dsl, CE_TYPE_PAGE)

#define CE_PAGE_DSL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_DSL, CEPageDslPrivate))

typedef struct {
	NMSettingPPPOE *setting;

	GtkEntry *username;
	GtkEntry *password;
	GtkEntry *service;

	gboolean disposed;
} CEPageDslPrivate;

static void
dsl_private_init (CEPageDsl *self)
{
	CEPageDslPrivate *priv = CE_PAGE_DSL_GET_PRIVATE (self);
	GladeXML *xml;

	xml = CE_PAGE (self)->xml;

	priv->username = GTK_ENTRY (glade_xml_get_widget (xml, "dsl_username"));
	priv->password = GTK_ENTRY (glade_xml_get_widget (xml, "dsl_password"));
	priv->service = GTK_ENTRY (glade_xml_get_widget (xml, "dsl_service"));
}

static void
populate_ui (CEPageDsl *self, NMConnection *connection)
{
	CEPageDslPrivate *priv = CE_PAGE_DSL_GET_PRIVATE (self);
	NMSettingPPPOE *setting = priv->setting;
	char *password = setting->password;
	const char *connection_id;
	GHashTable *secrets = NULL;

	if (setting->username)
		gtk_entry_set_text (priv->username, setting->username);

	/* Grab password from keyring if possible */
	connection_id = g_object_get_data (G_OBJECT (connection), NMA_CONNECTION_ID_TAG);
	if (!password && connection_id) {
		GError *error = NULL;
		GValue *value;

		secrets = nm_gconf_get_keyring_items (connection, connection_id,
		                                      nm_setting_get_name (NM_SETTING (priv->setting)),
		                                      FALSE,
		                                      &error);
		if (secrets) {
			value = g_hash_table_lookup (secrets, NM_SETTING_PPPOE_PASSWORD);
			if (value)
				password = (char *) g_value_get_string (value);
		} else
			g_error_free (error);
	}

	if (password)
		gtk_entry_set_text (priv->password, password);

	if (secrets)
		g_hash_table_destroy (secrets);

	if (setting->service)
		gtk_entry_set_text (priv->service, setting->service);
}

static void
stuff_changed (GtkEditable *editable, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
show_password (GtkToggleButton *button, gpointer user_data)
{
	CEPageDslPrivate *priv = CE_PAGE_DSL_GET_PRIVATE (user_data);

	gtk_entry_set_visibility (priv->password, gtk_toggle_button_get_active (button));
}

CEPageDsl *
ce_page_dsl_new (NMConnection *connection)
{
	CEPageDsl *self;
	CEPageDslPrivate *priv;
	CEPage *parent;
	NMSettingPPPOE *s_pppoe;

	self = CE_PAGE_DSL (g_object_new (CE_TYPE_PAGE_DSL, NULL));
	parent = CE_PAGE (self);

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

	dsl_private_init (self);
	priv = CE_PAGE_DSL_GET_PRIVATE (self);

	s_pppoe = (NMSettingPPPOE *) nm_connection_get_setting (connection, NM_TYPE_SETTING_PPPOE);
	if (s_pppoe)
		priv->setting = NM_SETTING_PPPOE (nm_setting_duplicate (NM_SETTING (s_pppoe)));
	else
		priv->setting = NM_SETTING_PPPOE (nm_setting_pppoe_new ());

	populate_ui (self, connection);

	g_signal_connect (priv->username, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->password, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->service, "changed", G_CALLBACK (stuff_changed), self);

	g_signal_connect (glade_xml_get_widget (parent->xml, "dsl_show_password"), "toggled",
					  G_CALLBACK (show_password), self);

	return self;
}

static void
ui_to_setting (CEPageDsl *self)
{
	CEPageDslPrivate *priv = CE_PAGE_DSL_GET_PRIVATE (self);
	const char *username;
	const char *password;
	const char *service;

	username = gtk_entry_get_text (priv->username);
	if (username && strlen (username) < 1)
		username = NULL;

	password = gtk_entry_get_text (priv->password);
	if (password && strlen (password) < 1)
		password = NULL;

	service = gtk_entry_get_text (priv->service);
	if (service && strlen (service) < 1)
		service = NULL;

	g_object_set (priv->setting,
				  NM_SETTING_PPPOE_USERNAME, username,
				  NM_SETTING_PPPOE_PASSWORD, password,
				  NM_SETTING_PPPOE_SERVICE, service,
				  NULL);
}

static gboolean
validate (CEPage *page)
{
	CEPageDsl *self = CE_PAGE_DSL (page);
	CEPageDslPrivate *priv = CE_PAGE_DSL_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL);
}

static void
update_connection (CEPage *page, NMConnection *connection)
{
	CEPageDsl *self = CE_PAGE_DSL (page);
	CEPageDslPrivate *priv = CE_PAGE_DSL_GET_PRIVATE (self);

	ui_to_setting (self);
	g_object_ref (priv->setting); /* Add setting steals the reference. */
	nm_connection_add_setting (connection, NM_SETTING (priv->setting));
}

static void
ce_page_dsl_init (CEPageDsl *self)
{
}

static void
dispose (GObject *object)
{
	CEPageDslPrivate *priv = CE_PAGE_DSL_GET_PRIVATE (object);

	if (priv->disposed)
		return;

	priv->disposed = TRUE;
	g_object_unref (priv->setting);

	G_OBJECT_CLASS (ce_page_dsl_parent_class)->dispose (object);
}

static void
ce_page_dsl_class_init (CEPageDslClass *dsl_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (dsl_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (dsl_class);

	g_type_class_add_private (object_class, sizeof (CEPageDslPrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
	parent_class->update_connection = update_connection;
}
