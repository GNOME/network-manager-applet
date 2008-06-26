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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-ppp.h>

#include "page-ppp.h"
#include "nm-connection-editor.h"

G_DEFINE_TYPE (CEPagePpp, ce_page_ppp, CE_TYPE_PAGE)

#define CE_PAGE_PPP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_PPP, CEPagePppPrivate))

#define COL_NAME  0
#define COL_VALUE 1
#define COL_TAG 2

#define TAG_EAP 0
#define TAG_PAP 1
#define TAG_CHAP 2
#define TAG_MSCHAP 3
#define TAG_MSCHAPV2 4

typedef struct {
	NMSettingPPP *setting;

	GtkToggleButton *use_auth;

	GtkTreeView *auth_methods_view;
	GtkCellRendererToggle *check_renderer;
	GtkListStore *auth_methods_list;

	GtkToggleButton *use_mppe;
	GtkToggleButton *mppe_require_128;
	GtkToggleButton *use_mppe_stateful;

	GtkToggleButton *allow_bsdcomp;
	GtkToggleButton *allow_deflate;
	GtkToggleButton *use_vj_comp;

	GtkToggleButton *send_ppp_echo;

	gboolean disposed;
} CEPagePppPrivate;

static void
ppp_private_init (CEPagePpp *self)
{
	CEPagePppPrivate *priv = CE_PAGE_PPP_GET_PRIVATE (self);
	GladeXML *xml;

	xml = CE_PAGE (self)->xml;

	priv->auth_methods_list = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_UINT);

	priv->use_auth = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "ppp_use_auth"));
	priv->auth_methods_view = GTK_TREE_VIEW (glade_xml_get_widget (xml, "ppp_auth_methods"));
	priv->use_mppe = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "ppp_use_mppe"));
	priv->mppe_require_128 = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "ppp_require_mppe_128"));
	priv->use_mppe_stateful = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "ppp_use_stateful_mppe"));
	priv->allow_bsdcomp = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "ppp_allow_bsdcomp"));
	priv->allow_deflate = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "ppp_allow_deflate"));
	priv->use_vj_comp = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "ppp_usevj"));
	priv->send_ppp_echo = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "ppp_send_echo_packets"));
}

static void
set_auth_items_sensitive (CEPagePpp *self, gboolean sensitive)
{
	CEPagePppPrivate *priv = CE_PAGE_PPP_GET_PRIVATE (self);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->auth_methods_view), sensitive);
	g_object_set (G_OBJECT (priv->check_renderer), "sensitive", sensitive, NULL);

	/* MPPE depends on MSCHAPv2 auth */
	gtk_widget_set_sensitive (GTK_WIDGET (priv->use_mppe), sensitive);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->mppe_require_128), sensitive);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->use_mppe_stateful), sensitive);
}

static void
use_auth_toggled_cb (GtkToggleButton *check, gpointer user_data)
{
	CEPagePpp *self = CE_PAGE_PPP (user_data);

	set_auth_items_sensitive (self, gtk_toggle_button_get_active (check));
}

static void
add_one_auth_method (GtkListStore *store, const char *name, gboolean allowed, guint32 tag)
{
	GtkTreeIter iter;

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, COL_NAME, name, COL_VALUE, allowed, COL_TAG, tag, -1);
}

static void
populate_ui (CEPagePpp *self, NMConnection *connection)
{
	CEPagePppPrivate *priv = CE_PAGE_PPP_GET_PRIVATE (self);
	NMSettingPPP *setting = priv->setting;

	add_one_auth_method (priv->auth_methods_list, _("PAP"), !setting->refuse_pap, TAG_PAP);
	add_one_auth_method (priv->auth_methods_list, _("CHAP"), !setting->refuse_chap, TAG_CHAP);
	add_one_auth_method (priv->auth_methods_list, _("MSCHAPv2"), !setting->refuse_mschapv2, TAG_MSCHAPV2);
	add_one_auth_method (priv->auth_methods_list, _("MSCHAP"), !setting->refuse_mschap, TAG_MSCHAP);
	add_one_auth_method (priv->auth_methods_list, _("EAP"), !setting->refuse_eap, TAG_EAP);

	gtk_toggle_button_set_active (priv->use_auth, !setting->noauth);
	if (setting->noauth)
		set_auth_items_sensitive (self, !setting->noauth);
	g_signal_connect (G_OBJECT (priv->use_auth), "toggled", G_CALLBACK (use_auth_toggled_cb), self);

	gtk_toggle_button_set_active (priv->use_mppe, setting->require_mppe);
	gtk_toggle_button_set_active (priv->mppe_require_128, setting->require_mppe_128);
	gtk_toggle_button_set_active (priv->use_mppe_stateful, setting->mppe_stateful);

	gtk_toggle_button_set_active (priv->allow_bsdcomp, !setting->nobsdcomp);
	gtk_toggle_button_set_active (priv->allow_deflate, !setting->nodeflate);
	gtk_toggle_button_set_active (priv->use_vj_comp, !setting->no_vj_comp);

	gtk_toggle_button_set_active (priv->send_ppp_echo, (setting->lcp_echo_interval > 0) ? TRUE : FALSE);
}

static void
check_toggled_cb (GtkCellRendererToggle *cell, gchar *path_str, gpointer user_data)
{
	CEPagePpp *self = CE_PAGE_PPP (user_data);
	CEPagePppPrivate *priv = CE_PAGE_PPP_GET_PRIVATE (self);
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	GtkTreeModel *model = GTK_TREE_MODEL (priv->auth_methods_list);
	GtkTreeIter iter;
	gboolean toggle_item;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, COL_VALUE, &toggle_item, -1);

	toggle_item ^= 1;

	/* set new value */
	gtk_list_store_set (priv->auth_methods_list, &iter, COL_VALUE, toggle_item, -1);

	gtk_tree_path_free (path);
}

CEPagePpp *
ce_page_ppp_new (NMConnection *connection)
{
	CEPagePpp *self;
	CEPagePppPrivate *priv;
	CEPage *parent;
	GtkCellRenderer *renderer;
	gint offset;
	GtkTreeViewColumn *column;

	self = CE_PAGE_PPP (g_object_new (CE_TYPE_PAGE_PPP, NULL));
	parent = CE_PAGE (self);

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-ppp.glade", "PppPage", NULL);
	if (!parent->xml) {
		g_warning ("%s: Couldn't load ppp page glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "PppPage");
	if (!parent->page) {
		g_warning ("%s: Couldn't load ppp page from glade file.", __func__);
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("Point-to-Point Protocol (PPP)"));

	ppp_private_init (self);
	priv = CE_PAGE_PPP_GET_PRIVATE (self);

	priv->setting = (NMSettingPPP *) nm_connection_get_setting (connection, NM_TYPE_SETTING_PPP);
	if (!priv->setting) {
		priv->setting = NM_SETTING_PPP (nm_setting_ppp_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	populate_ui (self, connection);

	gtk_tree_view_set_model (priv->auth_methods_view, GTK_TREE_MODEL (priv->auth_methods_list));

	priv->check_renderer = GTK_CELL_RENDERER_TOGGLE (gtk_cell_renderer_toggle_new ());
	g_signal_connect (priv->check_renderer, "toggled", G_CALLBACK (check_toggled_cb), self);

	offset = gtk_tree_view_insert_column_with_attributes (priv->auth_methods_view,
	                                                      -1, "", GTK_CELL_RENDERER (priv->check_renderer),
	                                                      "active", COL_VALUE,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->auth_methods_view), offset - 1);
	gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (column), GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width (GTK_TREE_VIEW_COLUMN (column), 30);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

	renderer = gtk_cell_renderer_text_new ();
	offset = gtk_tree_view_insert_column_with_attributes (priv->auth_methods_view,
	                                                      -1, "", renderer,
	                                                      "text", COL_NAME,
	                                                      NULL);
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->auth_methods_view), offset - 1);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);

	return self;
}

static void
ui_to_setting (CEPagePpp *self)
{
	CEPagePppPrivate *priv = CE_PAGE_PPP_GET_PRIVATE (self);
	gboolean noauth;
	gboolean refuse_eap = FALSE;
	gboolean refuse_pap = FALSE;
	gboolean refuse_chap = FALSE;
	gboolean refuse_mschap = FALSE;
	gboolean refuse_mschapv2 = FALSE;
	gboolean require_mppe;
	gboolean require_mppe_128;
	gboolean mppe_stateful;
	gboolean nobsdcomp;
	gboolean nodeflate;
	gboolean no_vj_comp;
	guint32 lcp_echo_failure;
	guint32 lcp_echo_interval;
	GtkTreeIter iter;
	gboolean valid;

	noauth = !gtk_toggle_button_get_active (priv->use_auth);

	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->auth_methods_list), &iter);
	while (valid) {
		gboolean allowed;
		guint32 tag;

		gtk_tree_model_get (GTK_TREE_MODEL (priv->auth_methods_list), &iter,
		                    COL_VALUE, &allowed,
		                    COL_TAG, &tag,
		                    -1);

		switch (tag) {
		case TAG_EAP:
			refuse_eap = !allowed;
			break;
		case TAG_PAP:
			refuse_pap = !allowed;
			break;
		case TAG_CHAP:
			refuse_chap = !allowed;
			break;
		case TAG_MSCHAP:
			refuse_mschap = !allowed;
			break;
		case TAG_MSCHAPV2:
			refuse_mschapv2 = !allowed;
			break;
		default:
			break;
		}

		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->auth_methods_list), &iter);
	}

	require_mppe = gtk_toggle_button_get_active (priv->use_mppe);
	require_mppe_128 = gtk_toggle_button_get_active (priv->mppe_require_128);
	mppe_stateful = gtk_toggle_button_get_active (priv->use_mppe_stateful);

	nobsdcomp = !gtk_toggle_button_get_active (priv->allow_bsdcomp);
	nodeflate = !gtk_toggle_button_get_active (priv->allow_deflate);
	no_vj_comp = !gtk_toggle_button_get_active (priv->use_vj_comp);

	if (gtk_toggle_button_get_active (priv->send_ppp_echo)) {
		lcp_echo_failure = 5;
		lcp_echo_interval = 30;
	} else {
		lcp_echo_failure = 0;
		lcp_echo_interval = 0;
	}
	
	g_object_set (priv->setting,
				  NM_SETTING_PPP_NOAUTH, noauth,
				  NM_SETTING_PPP_REFUSE_EAP, refuse_eap,
				  NM_SETTING_PPP_REFUSE_PAP, refuse_pap,
				  NM_SETTING_PPP_REFUSE_CHAP, refuse_chap,
				  NM_SETTING_PPP_REFUSE_MSCHAP, refuse_mschap,
				  NM_SETTING_PPP_REFUSE_MSCHAPV2, refuse_mschapv2,
				  NM_SETTING_PPP_NOBSDCOMP, nobsdcomp,
				  NM_SETTING_PPP_NODEFLATE, nodeflate,
				  NM_SETTING_PPP_NO_VJ_COMP, no_vj_comp,
				  NM_SETTING_PPP_REQUIRE_MPPE, require_mppe,
				  NM_SETTING_PPP_REQUIRE_MPPE_128, require_mppe_128,
				  NM_SETTING_PPP_MPPE_STATEFUL, mppe_stateful,
				  NM_SETTING_PPP_LCP_ECHO_FAILURE, lcp_echo_failure,
				  NM_SETTING_PPP_LCP_ECHO_INTERVAL, lcp_echo_interval,
				  NULL);
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPagePpp *self = CE_PAGE_PPP (page);
	CEPagePppPrivate *priv = CE_PAGE_PPP_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), NULL, error);
}

static void
ce_page_ppp_init (CEPagePpp *self)
{
}

static void
ce_page_ppp_class_init (CEPagePppClass *ppp_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (ppp_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (ppp_class);

	g_type_class_add_private (object_class, sizeof (CEPagePppPrivate));

	/* virtual methods */
	parent_class->validate = validate;
}
