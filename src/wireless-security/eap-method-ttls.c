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

	g_object_unref (method->nag_dialog_xml);
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

	if (!eap_method_validate_filepicker (parent->xml, "eap_ttls_ca_cert_button", TRUE, FALSE, NULL))
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
	EAPMethodTTLS *method = (EAPMethodTTLS *) parent;
	NMSetting8021x *s_8021x;
	GtkWidget *widget;
	const char *text;
	char *filename;
	EAPMethod *eap = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	g_assert (s_8021x);

	s_8021x->eap = g_slist_append (s_8021x->eap, g_strdup ("ttls"));

	// FIXME: allow protocol selection and filter on device capabilities
	// FIXME: allow pairwise cipher selection and filter on device capabilities
	// FIXME: allow group cipher selection and filter on device capabilities
	ws_wpa_fill_default_ciphers (connection);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_anon_identity_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (text && strlen (text))
		s_8021x->anonymous_identity = g_strdup (text);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_ca_cert_button");
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

	if (method->ignore_ca_cert)
		g_object_set_data (G_OBJECT (connection), NMA_CA_CERT_IGNORE_TAG, GUINT_TO_POINTER (TRUE));
	else
		g_object_set_data (G_OBJECT (connection), NMA_CA_CERT_IGNORE_TAG, NULL);

	widget = glade_xml_get_widget (parent->xml, "eap_ttls_inner_auth_combo");
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter);
	gtk_tree_model_get (model, &iter, I_METHOD_COLUMN, &eap, -1);
	g_assert (eap);

	eap_method_fill_connection (eap, connection);
	eap_method_unref (eap);
}

static gboolean
nag_dialog_destroy (gpointer user_data)
{
	GtkWidget *nag_dialog = GTK_WIDGET (user_data);

	gtk_widget_destroy (nag_dialog);
	return FALSE;
}

static void
nag_dialog_response_cb (GtkDialog *nag_dialog,
                        gint response,
                        gpointer user_data)
{
	EAPMethodTTLS *method = (EAPMethodTTLS *) user_data;
	GtkWidget *widget;

	if (response != GTK_RESPONSE_NO)
		goto out;

	/* Grab the value of the "don't bother me" checkbox */
	widget = glade_xml_get_widget (method->nag_dialog_xml, "ignore_checkbox");
	g_assert (widget);

	method->ignore_ca_cert = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

out:
	gtk_widget_hide (GTK_WIDGET (nag_dialog));
	g_idle_add (nag_dialog_destroy, nag_dialog);
}

static GtkWidget *
nag_user (EAPMethod *parent)
{
	GtkWidget *dialog;
	GtkWidget *widget;
	EAPMethodTTLS *method = (EAPMethodTTLS *) parent;
	char *filename = NULL;
	char *text;

	if (method->ignore_ca_cert)
		return NULL;

	/* Nag the user if the CA Cert is blank, since it's a security risk. */
	widget = glade_xml_get_widget (parent->xml, "eap_ttls_ca_cert_button");
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (filename != NULL) {
		g_free (filename);
		return NULL;
	}

	dialog = glade_xml_get_widget (method->nag_dialog_xml, "nag_user_dialog");
	g_assert (dialog);
	g_signal_connect (dialog, "response", G_CALLBACK (nag_dialog_response_cb), method);
	
	widget = glade_xml_get_widget (method->nag_dialog_xml, "content_label");
	g_assert (widget);

	text = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
	                        _("No Certificate Authority certificate chosen"),
	                        _("Not using a Certificate Authority (CA) certificate can result in connections to insecure, rogue wireless networks.  Would you like to choose a Certificate Authority certificate?"));
	gtk_label_set_markup (GTK_LABEL (widget), text);
	g_free (text);

	widget = glade_xml_get_widget (method->nag_dialog_xml, "ignore_button");
	gtk_button_set_label (GTK_BUTTON (widget), _("Ignore"));
	g_assert (widget);

	widget = glade_xml_get_widget (method->nag_dialog_xml, "change_button");
	gtk_button_set_label (GTK_BUTTON (widget), _("Choose CA Certificate"));
	g_assert (widget);

	gtk_widget_realize (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
	return dialog;
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
                       const char *connection_id,
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
	char *phase2_auth = NULL;

	auth_model = gtk_list_store_new (2, G_TYPE_STRING, eap_method_get_g_type ());

	if (s_8021x) {
		if (s_8021x->phase2_auth)
			phase2_auth = s_8021x->phase2_auth;
		else if (s_8021x->phase2_autheap)
			phase2_auth = s_8021x->phase2_autheap;
	}

	em_pap = eap_method_simple_new (glade_file,
	                                method->sec_parent,
	                                connection,
	                                connection_id,
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
	                                   connection_id,
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
	                                      connection_id,
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
	                                 connection_id,
	                                 EAP_METHOD_SIMPLE_TYPE_CHAP);
	gtk_list_store_append (auth_model, &iter);
	gtk_list_store_set (auth_model, &iter,
	                    I_NAME_COLUMN, _("CHAP"),
	                    I_METHOD_COLUMN, em_chap,
	                    -1);
	eap_method_unref (EAP_METHOD (em_chap));

	/* Check for defaulting to CHAP */
	if (phase2_auth && !strcasecmp (phase2_auth, "chap"))
		active = 4;

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
                     NMConnection *connection,
                     const char *connection_id)
{
	EAPMethodTTLS *method;
	GtkWidget *widget;
	GladeXML *xml;
	GladeXML *nag_dialog_xml;
	GtkFileFilter *filter;
	NMSetting8021x *s_8021x = NULL;
	const char *filename;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "eap_ttls_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get eap_ttls_widget from glade xml");
		return NULL;
	}

	nag_dialog_xml = glade_xml_new (glade_file, "nag_user_dialog", NULL);
	if (nag_dialog_xml == NULL) {
		g_warning ("Couldn't get nag_user_dialog from glade xml");
		g_object_unref (xml);
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "eap_ttls_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	method = g_slice_new0 (EAPMethodTTLS);
	if (!method) {
		g_object_unref (nag_dialog_xml);
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

	EAP_METHOD (method)->nag_user = nag_user;
	method->nag_dialog_xml = nag_dialog_xml;
	method->sec_parent = parent;

	if (connection) {
		method->ignore_ca_cert = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (connection), NMA_CA_CERT_IGNORE_TAG));
		s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	}

	widget = glade_xml_get_widget (xml, "eap_ttls_ca_cert_button");
	g_assert (widget);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
	gtk_file_chooser_button_set_title (GTK_FILE_CHOOSER_BUTTON (widget),
	                                   _("Choose a Certificate Authority certificate..."));
	g_signal_connect (G_OBJECT (widget), "selection-changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
	filter = eap_method_default_file_chooser_filter_new ();
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (widget), filter);
	if (connection) {
		filename = g_object_get_data (G_OBJECT (connection), NMA_PATH_CA_CERT_TAG);
		if (filename)
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), filename);
	}

	widget = glade_xml_get_widget (xml, "eap_ttls_anon_identity_entry");
	if (s_8021x && s_8021x->anonymous_identity)
		gtk_entry_set_text (GTK_ENTRY (widget), s_8021x->anonymous_identity);

	widget = inner_auth_combo_init (method, glade_file, connection, connection_id, s_8021x);
	inner_auth_combo_changed_cb (widget, (gpointer) method);

	return method;
}

