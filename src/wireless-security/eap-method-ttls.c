/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
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

#include <nm-setting-connection.h>
#include <nm-setting-8021x.h>

#include "eap-method.h"
#include "wireless-security.h"
#include "gconf-helpers.h"

#define I_NAME_COLUMN   0
#define I_METHOD_COLUMN 1

static void
destroy (EAPMethod *parent)
{
	EAPMethodTTLS *method = (EAPMethodTTLS *) parent;

	if (method->size_group)
		g_object_unref (method->size_group);
	g_slice_free (EAPMethodTTLS, method);
}

static gboolean
validate (EAPMethod *parent)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	EAPMethod *eap = NULL;
	gboolean valid = FALSE;

	if (!eap_method_validate_filepicker (parent->xml, "eap_ttls_ca_cert_button", TYPE_CA_CERT, NULL, NULL))
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
	NMSettingConnection *s_con;
	NMSetting8021x *s_8021x;
	NMSetting8021xCKFormat format = NM_SETTING_802_1X_CK_FORMAT_UNKNOWN;
	GtkWidget *widget;
	const char *text;
	char *filename;
	EAPMethod *eap = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GError *error = NULL;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	g_assert (s_8021x);

	nm_setting_802_1x_add_eap_method (s_8021x, "ttls");

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_anon_identity_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (text && strlen (text))
		g_object_set (s_8021x, NM_SETTING_802_1X_ANONYMOUS_IDENTITY, text, NULL);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_ca_cert_button");
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (!nm_setting_802_1x_set_ca_cert (s_8021x, filename, NM_SETTING_802_1X_CK_SCHEME_PATH, &format, &error)) {
		g_warning ("Couldn't read CA certificate '%s': %s", filename, error ? error->message : "(unknown)");
		g_clear_error (&error);
	}

	nm_gconf_set_ignore_ca_cert (nm_setting_connection_get_uuid (s_con),
	                             FALSE,
	                             eap_method_get_ignore_ca_cert (parent));

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
inner_auth_combo_init (EAPMethodTTLS *method,
                       const char *glade_file,
                       NMConnection *connection,
                       NMSetting8021x *s_8021x)
{
	GladeXML *xml = EAP_METHOD (method)->xml;
	GtkWidget *combo;
	GtkListStore *auth_model;
	GtkTreeIter iter;
	EAPMethodSimple *em_pap;
	EAPMethodSimple *em_mschap;
	EAPMethodSimple *em_mschap_v2;
	EAPMethodSimple *em_chap;
	guint32 active = 0;
	const char *phase2_auth = NULL;

	auth_model = gtk_list_store_new (2, G_TYPE_STRING, eap_method_get_g_type ());

	if (s_8021x) {
		if (nm_setting_802_1x_get_phase2_auth (s_8021x))
			phase2_auth = nm_setting_802_1x_get_phase2_auth (s_8021x);
		else if (nm_setting_802_1x_get_phase2_autheap (s_8021x))
			phase2_auth = nm_setting_802_1x_get_phase2_autheap (s_8021x);
	}

	em_pap = eap_method_simple_new (glade_file,
	                                method->sec_parent,
	                                connection,
	                                EAP_METHOD_SIMPLE_TYPE_PAP);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("PAP"),
	                    I_METHOD_COLUMN, em_pap,
	                    -1);
	eap_method_unref (EAP_METHOD (em_pap));

	/* Check for defaulting to PAP */
	if (phase2_auth && !strcasecmp (phase2_auth, "pap"))
		active = 0;

	em_mschap = eap_method_simple_new (glade_file,
	                                   method->sec_parent,
	                                   connection,
	                                   EAP_METHOD_SIMPLE_TYPE_MSCHAP);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("MSCHAP"),
	                    I_METHOD_COLUMN, em_mschap,
	                    -1);
	eap_method_unref (EAP_METHOD (em_mschap));

	/* Check for defaulting to MSCHAP */
	if (phase2_auth && !strcasecmp (phase2_auth, "mschap"))
		active = 1;

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

	/* Check for defaulting to MSCHAPv2 */
	if (phase2_auth && !strcasecmp (phase2_auth, "mschapv2"))
		active = 2;

	em_chap = eap_method_simple_new (glade_file,
	                                 method->sec_parent,
	                                 connection,
	                                 EAP_METHOD_SIMPLE_TYPE_CHAP);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("CHAP"),
	                    I_METHOD_COLUMN, em_chap,
	                    -1);
	eap_method_unref (EAP_METHOD (em_chap));

	/* Check for defaulting to CHAP */
	if (phase2_auth && !strcasecmp (phase2_auth, "chap"))
		active = 3;

	combo = glade_xml_get_widget (xml, "eap_ttls_inner_auth_combo");
	g_assert (combo);

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (auth_model));
	g_object_unref (G_OBJECT (auth_model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), active);

	g_signal_connect (G_OBJECT (combo), "changed",
	                  (GCallback) inner_auth_combo_changed_cb,
	                  method);
	return combo;
}

EAPMethodTTLS *
eap_method_ttls_new (const char *glade_file,
                     WirelessSecurity *parent,
                     NMConnection *connection)
{
	EAPMethodTTLS *method;
	GtkWidget *widget;
	GladeXML *xml;
	GtkFileFilter *filter;
	NMSetting8021x *s_8021x = NULL;
	const char *filename;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "eap_ttls_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get eap_ttls_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "eap_ttls_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	method = g_slice_new0 (EAPMethodTTLS);
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
	                 widget,
	                 "eap_ttls_anon_identity_entry");

	eap_method_nag_init (EAP_METHOD (method),
	                     glade_file,
	                     "eap_ttls_ca_cert_button",
	                     connection,
	                     FALSE);

	method->sec_parent = parent;

	if (connection)
		s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));

	widget = glade_xml_get_widget (xml, "eap_ttls_ca_cert_button");
	g_assert (widget);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
	gtk_file_chooser_button_set_title (GTK_FILE_CHOOSER_BUTTON (widget),
	                                   _("Choose a Certificate Authority certificate..."));
	g_signal_connect (G_OBJECT (widget), "selection-changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
	filter = eap_method_default_file_chooser_filter_new (FALSE);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (widget), filter);
	if (connection && s_8021x) {
		if (nm_setting_802_1x_get_ca_cert_scheme (s_8021x) == NM_SETTING_802_1X_CK_SCHEME_PATH) {
			filename = nm_setting_802_1x_get_ca_cert_path (s_8021x);
			if (filename)
				gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), filename);
		}
	}

	widget = glade_xml_get_widget (xml, "eap_ttls_anon_identity_entry");
	if (s_8021x && nm_setting_802_1x_get_anonymous_identity (s_8021x))
		gtk_entry_set_text (GTK_ENTRY (widget), nm_setting_802_1x_get_anonymous_identity (s_8021x));
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	widget = inner_auth_combo_init (method, glade_file, connection, s_8021x);
	inner_auth_combo_changed_cb (widget, (gpointer) method);

	return method;
}

