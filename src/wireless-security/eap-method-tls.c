/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

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

#include <glade/glade.h>
#include <glib/gi18n.h>
#include <ctype.h>
#include <string.h>
#include <nm-setting-8021x.h>

#include "gconf-helpers.h"
#include "eap-method.h"
#include "wireless-security.h"
#include "utils.h"

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

	g_object_unref (method->nag_dialog_xml);
	g_slice_free (EAPMethodTLS, method);
}

static gboolean
validate (EAPMethod *parent)
{
	NMSetting8021xCKType ck_type = NM_SETTING_802_1X_CK_TYPE_UNKNOWN;
	GtkWidget *widget;
	const char *password, *identity;

	widget = glade_xml_get_widget (parent->xml, "eap_tls_identity_entry");
	g_assert (widget);
	identity = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!identity || !strlen (identity))
		return FALSE;

	if (!eap_method_validate_filepicker (parent->xml, "eap_tls_ca_cert_button", TYPE_CA_CERT, NULL, NULL))
		return FALSE;

	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_password_entry");
	g_assert (widget);
	password = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!password || !strlen (password))
		return FALSE;

	if (!eap_method_validate_filepicker (parent->xml,
	                                     "eap_tls_private_key_button",
	                                     TYPE_PRIVATE_KEY,
	                                     password,
	                                     &ck_type))
		return FALSE;

	if (ck_type != NM_SETTING_802_1X_CK_TYPE_PKCS12) {
		if (!eap_method_validate_filepicker (parent->xml, "eap_tls_user_cert_button", TYPE_CLIENT_CERT, NULL, NULL))
			return FALSE;
	}

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
free_password (gpointer data)
{
	g_return_if_fail (data != NULL);

	/* Try not to leave passwords around in memory */
	memset (data, 0, strlen (data));
	g_free (data);
}

static void
fill_connection (EAPMethod *parent, NMConnection *connection)
{
	EAPMethodTLS *method = (EAPMethodTLS *) parent;
	NMSetting8021xCKType key_type = NM_SETTING_802_1X_CK_TYPE_UNKNOWN;
	NMSetting8021x *s_8021x;
	GtkWidget *widget;
	char *filename, *pk_filename, *cc_filename;
	char *password = NULL;
	GError *error = NULL;

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	g_assert (s_8021x);

	if (method->phase2)
		g_object_set (s_8021x, NM_SETTING_802_1X_PHASE2_AUTH, "tls", NULL);
	else
		nm_setting_802_1x_add_eap_method (s_8021x, "tls");

	widget = glade_xml_get_widget (parent->xml, "eap_tls_identity_entry");
	g_assert (widget);
	g_object_set (s_8021x, NM_SETTING_802_1X_IDENTITY, gtk_entry_get_text (GTK_ENTRY (widget)), NULL);

	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_password_entry");
	g_assert (widget);
	password = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));
	if (method->phase2) {
		g_object_set_data_full (G_OBJECT (connection),
		                        NMA_PHASE2_PRIVATE_KEY_PASSWORD_TAG,
		                        password,
		                        (GDestroyNotify) free_password);
	} else {
		g_object_set_data_full (G_OBJECT (connection),
		                        NMA_PRIVATE_KEY_PASSWORD_TAG,
		                        password,
		                        (GDestroyNotify) free_password);
	}

	/* TLS private key */
	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_button");
	g_assert (widget);
	pk_filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	g_assert (pk_filename);
	g_object_set_data_full (G_OBJECT (connection),
	                        method->phase2 ? NMA_PATH_PHASE2_PRIVATE_KEY_TAG : NMA_PATH_PRIVATE_KEY_TAG,
	                        g_strdup (pk_filename),
	                        (GDestroyNotify) g_free);
	if (method->phase2) {
		if (!nm_setting_802_1x_set_phase2_private_key_from_file (s_8021x, pk_filename, password, &key_type, &error)) {
			g_warning ("Couldn't read phase2 private key '%s': %s", pk_filename, error ? error->message : "(unknown)");
			g_clear_error (&error);
		}
	} else {
		if (!nm_setting_802_1x_set_private_key_from_file (s_8021x, pk_filename, password, &key_type, &error)) {
			g_warning ("Couldn't read private key '%s': %s", pk_filename, error ? error->message : "(unknown)");
			g_clear_error (&error);
		}
	}

	/* TLS client certificate */
	if (key_type == NM_SETTING_802_1X_CK_TYPE_PKCS12) {
		/* if the key is pkcs#12, the cert is filled with the same data */
		cc_filename = g_strdup (pk_filename);
	} else {
		widget = glade_xml_get_widget (parent->xml, "eap_tls_user_cert_button");
		g_assert (widget);
		cc_filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	}

	g_assert (cc_filename);
	g_object_set_data_full (G_OBJECT (connection),
	                        method->phase2 ? NMA_PATH_PHASE2_CLIENT_CERT_TAG : NMA_PATH_CLIENT_CERT_TAG,
	                        g_strdup (cc_filename),
	                        (GDestroyNotify) g_free);
	g_free (cc_filename);
	g_free (pk_filename);

	/* TLS CA certificate */
	widget = glade_xml_get_widget (parent->xml, "eap_tls_ca_cert_button");
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (filename) {
		g_object_set_data_full (G_OBJECT (connection),
		                        method->phase2 ? NMA_PATH_PHASE2_CA_CERT_TAG : NMA_PATH_CA_CERT_TAG,
		                        g_strdup (filename),
		                        (GDestroyNotify) g_free);
		g_free (filename);
	} else {
		g_object_set_data (G_OBJECT (connection),
		                   method->phase2 ? NMA_PATH_PHASE2_CA_CERT_TAG : NMA_PATH_CA_CERT_TAG,
		                   NULL);
	}

	if (method->ignore_ca_cert) {
		g_object_set_data (G_OBJECT (connection),
		                   method->phase2 ? NMA_PHASE2_CA_CERT_IGNORE_TAG : NMA_CA_CERT_IGNORE_TAG,
		                   GUINT_TO_POINTER (TRUE));
	} else {
		g_object_set_data (G_OBJECT (connection),
		                   method->phase2 ? NMA_PHASE2_CA_CERT_IGNORE_TAG : NMA_CA_CERT_IGNORE_TAG,
		                   NULL);
	}
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
	EAPMethodTLS *method = (EAPMethodTLS *) user_data;
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
	EAPMethodTLS *method = (EAPMethodTLS *) parent;
	char *filename = NULL;
	char *text;

	if (method->ignore_ca_cert)
		return NULL;

	/* Nag the user if the CA Cert is blank, since it's a security risk. */
	widget = glade_xml_get_widget (parent->xml, "eap_tls_ca_cert_button");
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
private_key_picker_helper (EAPMethod *parent, const char *filename, gboolean changed)
{
	NMSetting8021x *setting;
	NMSetting8021xCKType cert_type = NM_SETTING_802_1X_CK_TYPE_UNKNOWN;
	const char *password;
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "eap_tls_private_key_password_entry");
	g_assert (widget);
	password = gtk_entry_get_text (GTK_ENTRY (widget));

	setting = (NMSetting8021x *) nm_setting_802_1x_new ();
	nm_setting_802_1x_set_private_key_from_file (setting, filename, password, &cert_type, NULL);
	g_object_unref (setting);

	/* With PKCS#12, the client cert must be the same as the private key */
	widget = glade_xml_get_widget (parent->xml, "eap_tls_user_cert_button");
	if (cert_type == NM_SETTING_802_1X_CK_TYPE_PKCS12) {
		gtk_file_chooser_unselect_all (GTK_FILE_CHOOSER (widget));
		gtk_widget_set_sensitive (widget, FALSE);
	} else if (changed)
		gtk_widget_set_sensitive (widget, TRUE);
}

static void
private_key_picker_file_set_cb (GtkWidget *chooser, gpointer user_data)
{
	EAPMethod *parent = (EAPMethod *) user_data;
	char *filename;

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
	if (filename)
		private_key_picker_helper (parent, filename, TRUE);
	g_free (filename);
}

static void reset_filter (GtkWidget *widget, GParamSpec *spec, gpointer user_data)
{
	if (!gtk_file_chooser_get_filter (GTK_FILE_CHOOSER (widget))) {
		g_signal_handlers_block_by_func (widget, reset_filter, user_data);
		gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (widget), GTK_FILE_FILTER (user_data));
		g_signal_handlers_unblock_by_func (widget, reset_filter, user_data);
	}
}

static void
setup_filepicker (GladeXML *xml,
                  const char *name,
                  const char *title,
                  WirelessSecurity *parent,
                  EAPMethodTLS *method,
                  NMConnection *connection,
                  const char *tag)
{
	GtkWidget *widget;
	GtkFileFilter *filter;
	const char *filename = NULL;
	gboolean privkey = FALSE, client_cert = FALSE;

	if (!strcmp (tag, NMA_PATH_PHASE2_PRIVATE_KEY_TAG) || !strcmp (tag, NMA_PATH_PRIVATE_KEY_TAG))
		privkey = TRUE;
	if (!strcmp (tag, NMA_PATH_PHASE2_CLIENT_CERT_TAG) || !strcmp (tag, NMA_PATH_CLIENT_CERT_TAG))
		client_cert = TRUE;

	widget = glade_xml_get_widget (xml, name);
	g_assert (widget);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
	gtk_file_chooser_button_set_title (GTK_FILE_CHOOSER_BUTTON (widget), title);

	if (connection && tag) {
		filename = g_object_get_data (G_OBJECT (connection), tag);
		if (filename)
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), filename);
	}

	/* Connect a special handler for private keys to intercept PKCS#12 key types
	 * and desensitize the user cert button.
	 */
	if (privkey) {
		g_signal_connect (G_OBJECT (widget), "file-set",
		                  (GCallback) private_key_picker_file_set_cb,
		                  method);
		if (filename)
			private_key_picker_helper ((EAPMethod *) method, filename, FALSE);
	}

	g_signal_connect (G_OBJECT (widget), "file-set",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	filter = eap_method_default_file_chooser_filter_new (privkey);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (widget), filter);

	/* For some reason, GTK+ calls set_current_filter (..., NULL) from 
	 * gtkfilechooserdefault.c::show_and_select_files_finished_loading() on our
	 * dialog; so force-reset the filter to what we want it to be whenever
	 * it gets cleared.
	 */
	if (client_cert)
		g_signal_connect (G_OBJECT (widget), "notify::filter", (GCallback) reset_filter, filter);
}

static gboolean
revalidate (gpointer user_data)
{
	wireless_security_changed_cb (NULL, (WirelessSecurity *) user_data);
	return FALSE;
}

EAPMethodTLS *
eap_method_tls_new (const char *glade_file,
                    WirelessSecurity *parent,
                    NMConnection *connection,
                    gboolean phase2)
{
	EAPMethodTLS *method;
	GtkWidget *widget;
	GladeXML *xml;
	GladeXML *nag_dialog_xml;
	NMSetting8021x *s_8021x = NULL;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "eap_tls_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get eap_tls_widget from glade xml");
		return NULL;
	}

	nag_dialog_xml = glade_xml_new (glade_file, "nag_user_dialog", NULL);
	if (nag_dialog_xml == NULL) {
		g_warning ("Couldn't get nag_user_dialog from glade xml");
		g_object_unref (xml);
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "eap_tls_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	method = g_slice_new0 (EAPMethodTLS);
	if (!method) {
		g_object_unref (xml);
		g_object_unref (nag_dialog_xml);
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

	method->phase2 = phase2;

	if (connection) {
		method->ignore_ca_cert = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (connection), NMA_CA_CERT_IGNORE_TAG));
		s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	}

	widget = glade_xml_get_widget (xml, "eap_tls_identity_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
	if (s_8021x && nm_setting_802_1x_get_identity (s_8021x))
		gtk_entry_set_text (GTK_ENTRY (widget), nm_setting_802_1x_get_identity (s_8021x));

	widget = glade_xml_get_widget (xml, "eap_tls_private_key_password_entry");
	/* Fill secrets, if any */
	if (connection) {
		GHashTable *secrets;
		GError *error = NULL;
		GValue *value;
		const char *pw_secret_name = phase2 ? NM_SETTING_802_1X_PHASE2_PRIVATE_KEY_PASSWORD :
		                                      NM_SETTING_802_1X_PRIVATE_KEY_PASSWORD;

		secrets = nm_gconf_get_keyring_items (connection,
		                                      NM_SETTING_802_1X_SETTING_NAME,
		                                      TRUE,
		                                      &error);
		if (secrets) {
			value = g_hash_table_lookup (secrets, pw_secret_name);
			if (value)
				gtk_entry_set_text (GTK_ENTRY (widget), g_value_get_string (value));
			g_hash_table_destroy (secrets);
		}
		g_clear_error (&error);
	}
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);

	setup_filepicker (xml, "eap_tls_user_cert_button",
	                  _("Choose your personal certificate..."),
	                  parent, method, connection,
	                  phase2 ? NMA_PATH_PHASE2_CLIENT_CERT_TAG : NMA_PATH_CLIENT_CERT_TAG);
	setup_filepicker (xml, "eap_tls_ca_cert_button",
	                  _("Choose a Certificate Authority certificate..."),
	                  parent, method, connection,
	                  phase2 ? NMA_PATH_PHASE2_CA_CERT_TAG : NMA_PATH_CA_CERT_TAG);
	setup_filepicker (xml,
	                  "eap_tls_private_key_button",
	                  _("Choose your private key..."),
	                  parent, method, connection,
	                  phase2 ? NMA_PATH_PHASE2_PRIVATE_KEY_TAG : NMA_PATH_PRIVATE_KEY_TAG);

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  method);

	/* Re-validate from an idle-handler becuase file chooser widgets set their
	 * file asynchronously, not when gtk_file_chooser_set_filename() is called.
	 */
	g_idle_add (revalidate, parent);

	return method;
}

