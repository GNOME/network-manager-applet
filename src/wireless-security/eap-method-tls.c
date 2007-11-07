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

#include <glade/glade.h>
#include <ctype.h>
#include <string.h>
#include <nm-setting-wireless.h>

#include "eap-method.h"
#include "wireless-security.h"

static void
show_toggled_cb (GtkCheckButton *button, EAPMethod *method)
{
	GtkWidget *widget;
	gboolean visible;

	widget = glade_xml_get_widget (method->xml, "eap_tls_private_key_password_entry");
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static void
destroy (EAPMethod *parent)
{
	EAPMethodTLS *method = (EAPMethodTLS *) parent;

	g_object_unref (parent->xml);
	g_slice_free (EAPMethodTLS, method);
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
	const char *text;

	widget = glade_xml_get_widget (parent->xml, "eap_tls_identity_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!text || !strlen (text))
		return FALSE;

	if (!validate_filepicker (parent->xml, "eap_tls_user_cert_button"))
		return FALSE;

	if (!validate_filepicker (parent->xml, "eap_tls_ca_cert_button"))
		return FALSE;

	if (!validate_filepicker (parent->xml, "eap_tls_private_key_button"))
		return FALSE;

	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_password_entry");
	g_assert (widget);
	// FIXME: what if private key is unencrypted...
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!text || !strlen (text))
		return FALSE;

	return TRUE;
}

static void
add_to_size_group (EAPMethod *parent, GtkSizeGroup *group)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "eap_tls_identity_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_user_cert_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_ca_cert_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_password_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);
}

static void
fill_connection (EAPMethod *parent, NMConnection *connection)
{
	NMSettingWirelessSecurity *s_wireless_sec;
	GtkWidget *widget;
	char *filename;

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, 
										  NM_TYPE_SETTING_WIRELESS_SECURITY));
	g_assert (s_wireless_sec);

	s_wireless_sec->eap = g_slist_append (s_wireless_sec->eap, g_strdup ("tls"));

	// FIXME: allow protocol selection and filter on device capabilities
	// FIXME: allow pairwise cipher selection and filter on device capabilities
	// FIXME: allow group cipher selection and filter on device capabilities
	ws_wpa_fill_default_ciphers (connection);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_identity_entry");
	g_assert (widget);
	s_wireless_sec->identity = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));

	widget = glade_xml_get_widget (parent->xml, "eap_tls_user_cert_button");
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	g_object_set_data_full (G_OBJECT (connection),
	                        "nma-path-client-cert", g_strdup (filename),
	                        (GDestroyNotify) g_free);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_ca_cert_button");
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	g_object_set_data_full (G_OBJECT (connection),
	                        "nma-path-ca-cert", g_strdup (filename),
	                        (GDestroyNotify) g_free);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_button");
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	g_object_set_data_full (G_OBJECT (connection),
	                        "nma-path-private-key", g_strdup (filename),
	                        (GDestroyNotify) g_free);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_password_entry");
	g_assert (widget);
	s_wireless_sec->private_key_passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
setup_filepicker (GladeXML *xml, const char *name, WirelessSecurity *parent)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (xml, name);
	g_assert (widget);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
	g_signal_connect (G_OBJECT (widget), "selection-changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
}

EAPMethodTLS *
eap_method_tls_new (const char *glade_file, WirelessSecurity *parent)
{
	EAPMethodTLS *method;
	GtkWidget *widget;
	GladeXML *xml;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "eap_tls_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get eap_tls_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "eap_tls_notebook");
	g_assert (widget);

	method = g_slice_new0 (EAPMethodTLS);
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

	widget = glade_xml_get_widget (xml, "eap_tls_identity_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	widget = glade_xml_get_widget (xml, "eap_tls_private_key_password_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	setup_filepicker (xml, "eap_tls_user_cert_button", parent);
	setup_filepicker (xml, "eap_tls_ca_cert_button", parent);
	setup_filepicker (xml, "eap_tls_private_key_button", parent);

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  method);

	return method;
}

