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

#include "eap-method.h"
#include "wireless-security.h"

#define I_NAME_COLUMN   0
#define I_METHOD_COLUMN 1

static void
destroy (EAPMethod *parent)
{
	EAPMethodTTLS *method = (EAPMethodTTLS *) parent;

	g_object_unref (parent->xml);
	if (method->size_group)
		g_object_unref (method->size_group);
	g_slice_free (EAPMethodTTLS, method);
}

static gboolean
validate_filepicker (GladeXML *xml, const char *name)
{
	GtkWidget *widget;
	char *filename;
	gboolean success = TRUE;

	widget = glade_xml_get_widget (xml, name);
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (!filename)
		return FALSE;
	success = g_file_test (filename, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR);
	g_free (filename);
	return success;
}

static gboolean
validate (EAPMethod *parent)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	EAPMethod *eap = NULL;
	gboolean valid = FALSE;

	if (!validate_filepicker (parent->xml, "eap_ttls_ca_cert_button"))
		return FALSE;

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_inner_auth_combo");
	g_assert (widget);

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter);
	gtk_tree_model_get (model, &iter, I_METHOD_COLUMN, &eap, -1);
	g_assert (eap);
	valid = eap_method_validate (eap);
	eap_method_unref (eap);
	return valid;
}

static void
add_to_size_group (EAPMethod *parent, GtkSizeGroup *group)
{
	EAPMethodTTLS *method = (EAPMethodTTLS *) parent;
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	EAPMethod *eap;

	if (method->size_group)
		g_object_unref (method->size_group);
	method->size_group = g_object_ref (group);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_anon_identity_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_ca_cert_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_inner_auth_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_inner_auth_combo");
	g_assert (widget);

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter);
	gtk_tree_model_get (model, &iter, I_METHOD_COLUMN, &eap, -1);
	g_assert (eap);
	eap_method_add_to_size_group (eap, group);
	eap_method_unref (eap);
}

static void
fill_connection (EAPMethod *parent, NMConnection *connection)
{
	NMSettingWirelessSecurity *s_wireless_sec;
	GtkWidget *widget;
	const char *text;
	char *filename;
	EAPMethod *eap = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;

	s_wireless_sec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection, NM_SETTING_WIRELESS_SECURITY);
	g_assert (s_wireless_sec);

	s_wireless_sec->eap = g_slist_append (s_wireless_sec->eap, g_strdup ("ttls"));

	// FIXME: allow protocol selection and filter on device capabilities
	// FIXME: allow pairwise cipher selection and filter on device capabilities
	// FIXME: allow group cipher selection and filter on device capabilities
	ws_wpa_fill_default_ciphers (connection);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_anon_identity_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (text && strlen (text))
		s_wireless_sec->anonymous_identity = g_strdup (text);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_ca_cert_button");
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	g_object_set_data_full (G_OBJECT (connection),
	                        "nma-path-ca-cert", g_strdup (filename),
	                        (GDestroyNotify) g_free);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_inner_auth_combo");
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter);
	gtk_tree_model_get (model, &iter, I_METHOD_COLUMN, &eap, -1);
	g_assert (eap);

	eap_method_fill_connection (eap, connection);
	eap_method_unref (eap);
}

static void
inner_auth_combo_changed_cb (GtkWidget *combo, gpointer user_data)
{
	EAPMethod *parent = (EAPMethod *) user_data;
	EAPMethodTTLS *method = (EAPMethodTTLS *) parent;
	GtkWidget *vbox;
	EAPMethod *eap = NULL;
	GList *elt, *children;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *eap_widget;

	vbox = glade_xml_get_widget (parent->xml, "eap_ttls_inner_auth_vbox");
	g_assert (vbox);

	/* Remove any previous wireless security widgets */
	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (elt = children; elt; elt = g_list_next (elt))
		gtk_container_remove (GTK_CONTAINER (vbox), GTK_WIDGET (elt->data));

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter);
	gtk_tree_model_get (model, &iter, I_METHOD_COLUMN, &eap, -1);
	g_assert (eap);

	eap_widget = eap_method_get_widget (eap);
	g_assert (eap_widget);

	if (method->size_group)
		eap_method_add_to_size_group (eap, method->size_group);
	gtk_container_add (GTK_CONTAINER (vbox), eap_widget);

	eap_method_unref (eap);

	wireless_security_changed_cb (combo, method->sec_parent);
}

static GtkWidget *
inner_auth_combo_init (EAPMethodTTLS *method, const char *glade_file)
{
	GladeXML *xml = EAP_METHOD (method)->xml;
	GtkWidget *combo;
	GtkListStore *auth_model;
	GtkTreeIter iter;
	EAPMethodSimple *em_pap;
	EAPMethodSimple *em_mschap;
	EAPMethodSimple *em_mschap_v2;
	EAPMethodSimple *em_chap;

	auth_model = gtk_list_store_new (2, G_TYPE_STRING, eap_method_get_g_type ());

	em_pap = eap_method_simple_new (glade_file, method->sec_parent, EAP_METHOD_SIMPLE_TYPE_PAP);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("PAP"),
	                    I_METHOD_COLUMN, em_pap,
	                    -1);
	eap_method_unref (EAP_METHOD (em_pap));

	em_mschap = eap_method_simple_new (glade_file, method->sec_parent, EAP_METHOD_SIMPLE_TYPE_MSCHAP);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("MSCHAP"),
	                    I_METHOD_COLUMN, em_mschap,
	                    -1);
	eap_method_unref (EAP_METHOD (em_mschap));

	em_mschap_v2 = eap_method_simple_new (glade_file, method->sec_parent, EAP_METHOD_SIMPLE_TYPE_MSCHAP_V2);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("MSCHAPv2"),
	                    I_METHOD_COLUMN, em_mschap_v2,
	                    -1);
	eap_method_unref (EAP_METHOD (em_mschap_v2));

	em_chap = eap_method_simple_new (glade_file, method->sec_parent, EAP_METHOD_SIMPLE_TYPE_CHAP);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("CHAP"),
	                    I_METHOD_COLUMN, em_chap,
	                    -1);
	eap_method_unref (EAP_METHOD (em_chap));

	combo = glade_xml_get_widget (xml, "eap_ttls_inner_auth_combo");
	g_assert (combo);

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (auth_model));
	g_object_unref (G_OBJECT (auth_model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

	g_signal_connect (G_OBJECT (combo), "changed",
	                  (GCallback) inner_auth_combo_changed_cb,
	                  method);
	return combo;
}

EAPMethodTTLS *
eap_method_ttls_new (const char *glade_file, WirelessSecurity *parent)
{
	EAPMethodTTLS *method;
	GtkWidget *widget;
	GladeXML *xml;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "eap_ttls_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get eap_ttls_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "eap_ttls_notebook");
	g_assert (widget);

	method = g_slice_new0 (EAPMethodTTLS);
	if (!method) {
		g_object_unref (xml);
		return NULL;
	}

	eap_method_init (EAP_METHOD (method),
	                 validate,
	                 add_to_size_group,
	                 fill_connection,
	                 destroy,
	                 xml,
	                 g_object_ref (widget));

	method->sec_parent = parent;

	widget = glade_xml_get_widget (xml, "eap_ttls_ca_cert_button");
	g_assert (widget);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
	g_signal_connect (G_OBJECT (widget), "selection-changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	widget = inner_auth_combo_init (method, glade_file);
	inner_auth_combo_changed_cb (widget, (gpointer) method);

	return method;
}

