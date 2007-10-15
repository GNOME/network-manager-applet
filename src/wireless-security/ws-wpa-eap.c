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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <glib/gi18n.h>
#include <glade/glade.h>
#include <ctype.h>
#include <string.h>

#include "wireless-security.h"
#include "eap-method.h"

#define A_NAME_COLUMN   0
#define A_METHOD_COLUMN 1

static void
destroy (WirelessSecurity *parent)
{
	WirelessSecurityWPAEAP *sec = (WirelessSecurityWPAEAP *) parent;
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;

	widget = glade_xml_get_widget (parent->xml, "wpa_eap_auth_combo");
	g_assert (widget);

	/* destroy eap methods */
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			EAPMethod *eap;

			gtk_tree_model_get (model, &iter, A_METHOD_COLUMN, &eap, -1);
			eap_method_destroy (eap);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	g_object_unref (model);

	g_object_unref (parent->xml);
	if (sec->size_group)
		g_object_unref (sec->size_group);
	g_slice_free (WirelessSecurityWPAEAP, sec);
}

static gboolean
validate (WirelessSecurity *parent, const GByteArray *ssid)
{
	WirelessSecurityWPAEAP *sec = (WirelessSecurityWPAEAP *) parent;
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	EAPMethod *eap = NULL;

	widget = glade_xml_get_widget (parent->xml, "wpa_eap_auth_combo");
	g_assert (widget);

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter))
		gtk_tree_model_get (model, &iter, A_METHOD_COLUMN, &eap, -1);
	g_assert (eap);
	return eap_method_validate (eap);
}

static void
add_to_size_group (WirelessSecurity *parent, GtkSizeGroup *group)
{
	WirelessSecurityWPAEAP *sec = (WirelessSecurityWPAEAP *) parent;
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	EAPMethod *eap;

	if (sec->size_group)
		g_object_unref (sec->size_group);
	sec->size_group = group;

	widget = glade_xml_get_widget (parent->xml, "wpa_eap_auth_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "wpa_eap_auth_combo");
	g_assert (widget);

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter))
		gtk_tree_model_get (model, &iter, A_METHOD_COLUMN, &eap, -1);
	g_assert (eap);
	eap_method_add_to_size_group (eap, group);
}

static void
fill_connection (WirelessSecurity *parent, NMConnection *connection)
{
	WirelessSecurityWPAEAP *sec = (WirelessSecurityWPAEAP *) parent;
	GtkWidget *widget;
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	EAPMethod *eap = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, NM_SETTING_WIRELESS);
	g_assert (s_wireless);

	if (s_wireless->security)
		g_free (s_wireless->security);
	s_wireless->security = g_strdup (NM_SETTING_WIRELESS_SECURITY);

	/* Blow away the old security setting by adding a clear one */
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);

	s_wireless_sec->key_mgmt = g_strdup ("wpa-eap");

	widget = glade_xml_get_widget (parent->xml, "wpa_eap_auth_combo");
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter))
		gtk_tree_model_get (model, &iter, A_METHOD_COLUMN, &eap, -1);
	g_assert (eap);

	eap_method_fill_connection (eap, connection);
}

static void
auth_combo_changed_cb (GtkWidget *combo, gpointer user_data)
{
	WirelessSecurity *parent = (WirelessSecurity *) user_data;
	WirelessSecurityWPAEAP *sec = (WirelessSecurityWPAEAP *) parent;
	GtkWidget *vbox;
	EAPMethod *eap = NULL;
	GList *elt, *children;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *eap_widget;

	vbox = glade_xml_get_widget (parent->xml, "wpa_eap_method_vbox");
	g_assert (vbox);

	/* Remove any previous wireless security widgets */
	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (elt = children; elt; elt = g_list_next (elt))
		gtk_container_remove (GTK_CONTAINER (vbox), GTK_WIDGET (elt->data));

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter))
		gtk_tree_model_get (model, &iter, A_METHOD_COLUMN, &eap, -1);
	g_assert (eap);

	eap_widget = eap_method_get_widget (eap);
	g_assert (eap_widget);

	if (sec->size_group)
		eap_method_add_to_size_group (eap, sec->size_group);
	gtk_container_add (GTK_CONTAINER (vbox), eap_widget);

	wireless_security_changed_cb (combo, WIRELESS_SECURITY (sec));
}

static GtkWidget *
auth_combo_init (WirelessSecurityWPAEAP *sec, const char *glade_file)
{
	GladeXML *xml = WIRELESS_SECURITY (sec)->xml;
	GtkWidget *combo;
	GtkListStore *auth_model;
	GtkTreeIter iter;
	EAPMethodTLS *em_tls;

	auth_model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);

	em_tls = eap_method_tls_new (glade_file, WIRELESS_SECURITY (sec));
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    A_NAME_COLUMN, _("TLS"),
	                    A_METHOD_COLUMN, em_tls,
	                    -1);

	combo = glade_xml_get_widget (xml, "wpa_eap_auth_combo");
	g_assert (combo);

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (auth_model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

	g_signal_connect (G_OBJECT (combo), "changed",
	                  (GCallback) auth_combo_changed_cb,
	                  sec);
	return combo;
}

WirelessSecurityWPAEAP *
ws_wpa_eap_new (const char *glade_file)
{
	WirelessSecurityWPAEAP *sec;
	GtkWidget *widget;
	GladeXML *xml;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "wpa_eap_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get wpa_eap_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "wpa_eap_notebook");
	g_assert (widget);

	sec = g_slice_new0 (WirelessSecurityWPAEAP);
	if (!sec) {
		g_object_unref (xml);
		return NULL;
	}

	WIRELESS_SECURITY (sec)->validate = validate;
	WIRELESS_SECURITY (sec)->add_to_size_group = add_to_size_group;
	WIRELESS_SECURITY (sec)->fill_connection = fill_connection;
	WIRELESS_SECURITY (sec)->destroy = destroy;

	WIRELESS_SECURITY (sec)->xml = xml;
	WIRELESS_SECURITY (sec)->ui_widget = g_object_ref (widget);

	widget = auth_combo_init (sec, glade_file);
	auth_combo_changed_cb (widget, (gpointer) sec);

	return sec;
}

