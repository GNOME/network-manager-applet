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

#define CE_PAGE_WIRED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_WIRED, CEPageWiredPrivate))

typedef struct {
	NMSettingWired *setting;

	GtkComboBox *port;
	GtkComboBox *speed;
	GtkToggleButton *duplex;
	GtkToggleButton *autonegotiate;
	GtkSpinButton *mtu;

	gboolean disposed;
} CEPageWiredPrivate;

#define PORT_DEFAULT  0
#define PORT_TP       1
#define PORT_AUI      2
#define PORT_BNC      3
#define PORT_MII      4

#define SPEED_DEFAULT 0
#define SPEED_10      1
#define SPEED_100     2
#define SPEED_1000    3
#define SPEED_10000   4

static void
wired_private_init (CEPageWired *self)
{
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);
	GladeXML *xml;

	xml = CE_PAGE (self)->xml;

	priv->port = GTK_COMBO_BOX (glade_xml_get_widget (xml, "wired_port"));
	priv->speed = GTK_COMBO_BOX (glade_xml_get_widget (xml, "wired_speed"));
	priv->duplex = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "wired_duplex"));
	priv->autonegotiate = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "wired_autonegotiate"));
	priv->mtu = GTK_SPIN_BUTTON (glade_xml_get_widget (xml, "wired_mtu"));
}

static void
populate_ui (CEPageWired *self)
{
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);
	NMSettingWired *setting = priv->setting;
	int port_idx = PORT_DEFAULT;
	int speed_idx;
	int mtu_def;

	/* Port */
	if (setting->port) {
		if (!strcmp (setting->port, "tp"))
			port_idx = PORT_TP;
		else if (!strcmp (setting->port, "aui"))
			port_idx = PORT_AUI;
		else if (!strcmp (setting->port, "bnc"))
			port_idx = PORT_BNC;
		else if (!strcmp (setting->port, "mii"))
			port_idx = PORT_MII;
	}
	gtk_combo_box_set_active (priv->port, port_idx);

	/* Speed */
	switch (setting->speed) {
	case 10:
		speed_idx = SPEED_10;
		break;
	case 100:
		speed_idx = SPEED_100;
		break;
	case 1000:
		speed_idx = SPEED_1000;
		break;
	case 10000:
		speed_idx = SPEED_10000;
		break;
	default:
		speed_idx = SPEED_DEFAULT;
		break;
	}
	gtk_combo_box_set_active (priv->speed, speed_idx);

	/* Duplex */
	if (!strcmp (setting->duplex ? setting->duplex : "", "half"))
		gtk_toggle_button_set_active (priv->duplex, FALSE);
	else
		gtk_toggle_button_set_active (priv->duplex, TRUE);

	/* Autonegotiate */
	gtk_toggle_button_set_active (priv->autonegotiate, setting->auto_negotiate);

	/* FIXME: MAC address */

	/* MTU */
	mtu_def = ce_get_property_default (NM_SETTING (setting), NM_SETTING_WIRED_MTU);
	g_signal_connect (priv->mtu, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (mtu_def));

	gtk_spin_button_set_value (priv->mtu, (gdouble) setting->mtu);
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

CEPageWired *
ce_page_wired_new (NMConnection *connection)
{
	CEPageWired *self;
	CEPageWiredPrivate *priv;
	CEPage *parent;
	NMSettingWired *s_wired;

	self = CE_PAGE_WIRED (g_object_new (CE_TYPE_PAGE_WIRED, NULL));
	parent = CE_PAGE (self);

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

	wired_private_init (self);
	priv = CE_PAGE_WIRED_GET_PRIVATE (self);

	s_wired = (NMSettingWired *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRED);
	if (s_wired)
		priv->setting = NM_SETTING_WIRED (nm_setting_duplicate (NM_SETTING (s_wired)));
	else
		priv->setting = NM_SETTING_WIRED (nm_setting_wired_new ());

	populate_ui (self);

	g_signal_connect (priv->port, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->speed, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->duplex, "toggled", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->autonegotiate, "toggled", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->mtu, "value-changed", G_CALLBACK (stuff_changed), self);

	return self;
}

static void
ui_to_setting (CEPageWired *self)
{
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);
	const char *port;
	guint32 speed;

	/* Port */
	switch (gtk_combo_box_get_active (priv->port)) {
	case PORT_TP:
		port = "tp";
		break;
	case PORT_AUI:
		port = "aui";
		break;
	case PORT_BNC:
		port = "bnc";
		break;
	case PORT_MII:
		port = "mii";
		break;
	default:
		port = NULL;
		break;
	}

	/* Speed */
	switch (gtk_combo_box_get_active (priv->speed)) {
	case SPEED_10:
		speed = 10;
		break;
	case SPEED_100:
		speed = 100;
		break;
	case SPEED_1000:
		speed = 1000;
		break;
	case SPEED_10000:
		speed = 10000;
		break;
	default:
		speed = 0;
		break;
	}

	g_object_set (priv->setting,
				  NM_SETTING_WIRED_PORT, port,
				  NM_SETTING_WIRED_SPEED, speed,
				  NM_SETTING_WIRED_DUPLEX, gtk_toggle_button_get_active (priv->duplex) ? "full" : "half",
				  NM_SETTING_WIRED_AUTO_NEGOTIATE, gtk_toggle_button_get_active (priv->autonegotiate),
				  NM_SETTING_WIRED_MTU, (guint32) gtk_spin_button_get_value_as_int (priv->mtu),
				  NULL);
}

static gboolean
validate (CEPage *page)
{
	CEPageWired *self = CE_PAGE_WIRED (page);
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL);
}

static void
update_connection (CEPage *page, NMConnection *connection)
{
	CEPageWired *self = CE_PAGE_WIRED (page);
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (self);

	ui_to_setting (self);
	g_object_ref (priv->setting); /* Add setting steals the reference. */
	nm_connection_add_setting (connection, NM_SETTING (priv->setting));
}

static void
ce_page_wired_init (CEPageWired *self)
{
}

static void
dispose (GObject *object)
{
	CEPageWiredPrivate *priv = CE_PAGE_WIRED_GET_PRIVATE (object);

	if (priv->disposed)
		return;

	priv->disposed = TRUE;
	g_object_unref (priv->setting);

	G_OBJECT_CLASS (ce_page_wired_parent_class)->dispose (object);
}

static void
ce_page_wired_class_init (CEPageWiredClass *wired_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (wired_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (wired_class);

	g_type_class_add_private (object_class, sizeof (CEPageWiredPrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
	parent_class->update_connection = update_connection;
}
