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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <glib/gi18n.h>
#include <glade/glade.h>
#include <ctype.h>
#include <string.h>
#include <nm-setting-8021x.h>

#include "eap-method.h"
#include "wireless-security.h"
#include "gconf-helpers.h"

#define I_NAME_COLUMN   0
#define I_METHOD_COLUMN 1

static void
destroy (EAPMethod *parent)
{
	EAPMethodPEAP *method = (EAPMethodPEAP *) parent;

	if (method->size_group)
		g_object_unref (method->size_group);
	g_slice_free (EAPMethodPEAP, method);
}

static gboolean
validate (EAPMethod *parent)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	EAPMethod *eap = NULL;
	gboolean valid = FALSE;

	if (!eap_method_validate_filepicker (parent->xml, "eap_peap_ca_cert_button", TYPE_CA_CERT, NULL, NULL))
		return FALSE;

	widget = glade_xml_get_widget (parent->xml, "eap_peap_inner_auth_combo");
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
	EAPMethodPEAP *method = (EAPMethodPEAP *) parent;
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	EAPMethod *eap;

	if (method->size_group)
		g_object_unref (method->size_group);
	method->size_group = g_object_ref (group);

	widget = glade_xml_get_widget (parent->xml, "eap_peap_anon_identity_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_peap_ca_cert_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_peap_version_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_peap_inner_auth_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_peap_inner_auth_combo");
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
	NMSetting8021x *s_8021x;
	GtkWidget *widget;
	const char *text;
	char *filename;
	EAPMethod *eap = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;
	int peapver_active = 0;

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	g_assert (s_8021x);

	nm_setting_802_1x_add_eap_method (s_8021x, "peap");

	widget = glade_xml_get_widget (parent->xml, "eap_peap_anon_identity_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (text && strlen (text))
		g_object_set (s_8021x, NM_SETTING_802_1X_ANONYMOUS_IDENTITY, text, NULL);

	widget = glade_xml_get_widget (parent->xml, "eap_peap_ca_cert_button");
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (filename) {
		g_object_set_data_full (G_OBJECT (connection),
		                        NMA_PATH_CA_CERT_TAG, g_strdup (filename),
		                        (GDestroyNotify) g_free);
		g_free (filename);
	} else {
		g_object_set_data (G_OBJECT (connection), NMA_PATH_CA_CERT_TAG, NULL);
	}

	if (eap_method_get_ignore_ca_cert (parent))
		g_object_set_data (G_OBJECT (connection), NMA_CA_CERT_IGNORE_TAG, GUINT_TO_POINTER (TRUE));
	else
		g_object_set_data (G_OBJECT (connection), NMA_CA_CERT_IGNORE_TAG, NULL);

	widget = glade_xml_get_widget (parent->xml, "eap_peap_version_combo");
	peapver_active = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
	switch (peapver_active) {
	case 1:  /* PEAP v0 */
		g_object_set (G_OBJECT (s_8021x), NM_SETTING_802_1X_PHASE1_PEAPVER, "0", NULL);
		break;
	case 2:  /* PEAP v1 */
		g_object_set (G_OBJECT (s_8021x), NM_SETTING_802_1X_PHASE1_PEAPVER, "1", NULL);
		break;
	default: /* Automatic */
		g_object_set (G_OBJECT (s_8021x), NM_SETTING_802_1X_PHASE1_PEAPVER, NULL, NULL);
		break;
	}

	widget = glade_xml_get_widget (parent->xml, "eap_peap_inner_auth_combo");
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
	EAPMethodPEAP *method = (EAPMethodPEAP *) parent;
	GtkWidget *vbox;
	EAPMethod *eap = NULL;
	GList *elt, *children;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *eap_widget;

	vbox = glade_xml_get_widget (parent->xml, "eap_peap_inner_auth_vbox");
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
inner_auth_combo_init (EAPMethodPEAP *method,
                       const char *glade_file,
                       NMConnection *connection)
{
	GladeXML *xml = EAP_METHOD (method)->xml;
	GtkWidget *combo;
	GtkListStore *auth_model;
	GtkTreeIter iter;
	EAPMethodSimple *em_mschap_v2;
	EAPMethodSimple *em_md5;

	auth_model = gtk_list_store_new (2, G_TYPE_STRING, eap_method_get_g_type ());

	em_mschap_v2 = eap_method_simple_new (glade_file,
	                                      method->sec_parent,
	                                      connection,
	                                      EAP_METHOD_SIMPLE_TYPE_MSCHAP_V2);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("MSCHAPv2"),
	                    I_METHOD_COLUMN, em_mschap_v2,
	                    -1);
	eap_method_unref (EAP_METHOD (em_mschap_v2));

	em_md5 = eap_method_simple_new (glade_file,
	                                 method->sec_parent,
	                                 connection,
	                                 EAP_METHOD_SIMPLE_TYPE_MD5);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("MD5"),
	                    I_METHOD_COLUMN, em_md5,
	                    -1);
	eap_method_unref (EAP_METHOD (em_md5));

	combo = glade_xml_get_widget (xml, "eap_peap_inner_auth_combo");
	g_assert (combo);

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (auth_model));
	g_object_unref (G_OBJECT (auth_model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

	g_signal_connect (G_OBJECT (combo), "changed",
	                  (GCallback) inner_auth_combo_changed_cb,
	                  method);
	return combo;
}

EAPMethodPEAP *
eap_method_peap_new (const char *glade_file,
                     WirelessSecurity *parent,
                     NMConnection *connection)
{
	EAPMethodPEAP *method;
	GtkWidget *widget;
	GladeXML *xml;
	GtkFileFilter *filter;
	NMSetting8021x *s_8021x = NULL;
	const char *filename;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "eap_peap_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get eap_peap_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "eap_peap_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	method = g_slice_new0 (EAPMethodPEAP);
	if (!method) {
		g_object_unref (xml);
		g_object_unref (widget);
		return NULL;
	}

	eap_method_init (EAP_METHOD (method),
	                 validate,
	                 add_to_size_group,
	                 fill_connection,
	                 destroy,
	                 xml,
	                 widget);

	eap_method_nag_init (EAP_METHOD (method),
	                     glade_file,
	                     "eap_peap_ca_cert_button",
	                     connection);

	method->sec_parent = parent;

	if (connection)
		s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));

	widget = glade_xml_get_widget (xml, "eap_peap_ca_cert_button");
	g_assert (widget);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
	gtk_file_chooser_button_set_title (GTK_FILE_CHOOSER_BUTTON (widget),
	                                   _("Choose a Certificate Authority certificate..."));
	g_signal_connect (G_OBJECT (widget), "file-set",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
	filter = eap_method_default_file_chooser_filter_new (FALSE);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (widget), filter);
	if (connection) {
		filename = g_object_get_data (G_OBJECT (connection), NMA_PATH_CA_CERT_TAG);
		if (filename)
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), filename);
	}

	widget = inner_auth_combo_init (method, glade_file, connection);
	inner_auth_combo_changed_cb (widget, (gpointer) method);

	widget = glade_xml_get_widget (xml, "eap_peap_version_combo");
	g_assert (widget);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
	if (s_8021x) {
		const char *peapver;

		peapver = nm_setting_802_1x_get_phase1_peapver (s_8021x);
		if (peapver) {
			/* Index 0 is "Automatic" */
			if (!strcmp (peapver, "0"))
				gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 1);
			else if (!strcmp (peapver, "1"))
				gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 2);
		}
	}
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	widget = glade_xml_get_widget (xml, "eap_peap_anon_identity_entry");
	if (s_8021x && nm_setting_802_1x_get_anonymous_identity (s_8021x))
		gtk_entry_set_text (GTK_ENTRY (widget), nm_setting_802_1x_get_anonymous_identity (s_8021x));
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	return method;
}

