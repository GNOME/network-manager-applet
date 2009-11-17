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


#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <nm-setting-8021x.h>
#include "eap-method.h"
#include "gconf-helpers.h"


GType
eap_method_get_g_type (void)
{
	static GType type_id = 0;

	if (!type_id) {
		type_id = g_boxed_type_register_static ("EAPMethod",
		                                        (GBoxedCopyFunc) eap_method_ref,
		                                        (GBoxedFreeFunc) eap_method_unref);
	}

	return type_id;
}

GtkWidget *
eap_method_get_widget (EAPMethod *method)
{
	g_return_val_if_fail (method != NULL, NULL);

	return method->ui_widget;
}

gboolean
eap_method_validate (EAPMethod *method)
{
	g_return_val_if_fail (method != NULL, FALSE);

	g_assert (method->validate);
	return (*(method->validate)) (method);
}

void
eap_method_add_to_size_group (EAPMethod *method, GtkSizeGroup *group)
{
	g_return_if_fail (method != NULL);
	g_return_if_fail (group != NULL);

	g_assert (method->add_to_size_group);
	return (*(method->add_to_size_group)) (method, group);
}

void
eap_method_fill_connection (EAPMethod *method, NMConnection *connection)
{
	g_return_if_fail (method != NULL);
	g_return_if_fail (connection != NULL);

	g_assert (method->fill_connection);
	return (*(method->fill_connection)) (method, connection);
}

static void
nag_dialog_response_cb (GtkDialog *nag_dialog,
                        gint response,
                        gpointer user_data)
{
	EAPMethod *method = (EAPMethod *) user_data;
	GtkWidget *widget;

	if (response == GTK_RESPONSE_NO) {
		/* Grab the value of the "don't bother me" checkbox */
		widget = glade_xml_get_widget (method->nag_dialog_xml, "ignore_checkbox");
		g_assert (widget);

		method->ignore_ca_cert = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	}

	gtk_widget_hide (GTK_WIDGET (nag_dialog));
}

GtkWidget *
eap_method_nag_user (EAPMethod *method)
{
	GtkWidget *widget;
	char *filename = NULL;

	g_return_val_if_fail (method != NULL, NULL);

	if (!method->nag_dialog || method->ignore_ca_cert)
		return NULL;

	/* Checkbox should be unchecked each time dialog comes up */
	widget = glade_xml_get_widget (method->nag_dialog_xml, "ignore_checkbox");
	g_assert (widget);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);

	/* Nag the user if the CA Cert is blank, since it's a security risk. */
	widget = glade_xml_get_widget (method->xml, method->ca_cert_chooser);
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (filename != NULL) {
		g_free (filename);
		return NULL;
	}

	gtk_window_present (GTK_WINDOW (method->nag_dialog));
	return method->nag_dialog;
}

gboolean
eap_method_nag_init (EAPMethod *method,
                     const char *glade_file,
                     const char *ca_cert_chooser,
                     NMConnection *connection)
{
	GtkWidget *dialog, *widget;
	char *text;

	g_return_val_if_fail (method != NULL, FALSE);
	g_return_val_if_fail (glade_file != NULL, FALSE);
	g_return_val_if_fail (ca_cert_chooser != NULL, FALSE);

	method->nag_dialog_xml = glade_xml_new (glade_file, "nag_user_dialog", NULL);
	if (method->nag_dialog_xml == NULL) {
		g_warning ("Couldn't get nag_user_dialog from glade xml");
		return FALSE;
	}

	method->ca_cert_chooser = g_strdup (ca_cert_chooser);
	if (connection)
		method->ignore_ca_cert = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (connection), NMA_CA_CERT_IGNORE_TAG));

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

	method->nag_dialog = dialog;
	return TRUE;
}

gboolean
eap_method_get_ignore_ca_cert (EAPMethod *method)
{
	g_return_val_if_fail (method != NULL, FALSE);

	return method->ignore_ca_cert;
}

void
eap_method_init (EAPMethod *method,
                 EMValidateFunc validate,
                 EMAddToSizeGroupFunc add_to_size_group,
                 EMFillConnectionFunc fill_connection,
                 EMDestroyFunc destroy,
                 GladeXML *xml,
                 GtkWidget *ui_widget,
                 const char *default_field)
{                 
	method->refcount = 1;

	method->validate = validate;
	method->add_to_size_group = add_to_size_group;
	method->fill_connection = fill_connection;
	method->destroy = destroy;

	method->xml = xml;
	method->ui_widget = ui_widget;
	method->default_field = default_field;
}


EAPMethod *
eap_method_ref (EAPMethod *method)
{
	g_return_val_if_fail (method != NULL, NULL);
	g_return_val_if_fail (method->refcount > 0, NULL);

	method->refcount++;
	return method;
}

void
eap_method_unref (EAPMethod *method)
{
	g_return_if_fail (method != NULL);
	g_return_if_fail (method->refcount > 0);

	g_assert (method->destroy);

	method->refcount--;
	if (method->refcount == 0) {
		if (method->nag_dialog)
			gtk_widget_destroy (method->nag_dialog);
		if (method->nag_dialog_xml)
			g_object_unref (method->nag_dialog_xml);
		g_free (method->ca_cert_chooser);
		g_object_unref (method->xml);
		g_object_unref (method->ui_widget);
		(*(method->destroy)) (method);
	}
}

gboolean
eap_method_validate_filepicker (GladeXML *xml,
                                const char *name,
                                guint32 item_type,
                                const char *password,
                                NMSetting8021xCKType *out_ck_type)
{
	GtkWidget *widget;
	char *filename;
	NMSetting8021x *setting;
	gboolean success = FALSE;
	GError *error = NULL;

	if (item_type == TYPE_PRIVATE_KEY) {
		g_return_val_if_fail (password != NULL, NM_SETTING_802_1X_CK_TYPE_UNKNOWN);
		g_return_val_if_fail (strlen (password), NM_SETTING_802_1X_CK_TYPE_UNKNOWN);
	}

	widget = glade_xml_get_widget (xml, name);
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (!filename)
		return (item_type == TYPE_CA_CERT) ? NM_SETTING_802_1X_CK_TYPE_X509 : NM_SETTING_802_1X_CK_TYPE_UNKNOWN;

	if (!g_file_test (filename, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))
		goto out;

	setting = (NMSetting8021x *) nm_setting_802_1x_new ();

	if (item_type == TYPE_PRIVATE_KEY) {
		if (!nm_setting_802_1x_set_private_key_from_file (setting, filename, password, out_ck_type, &error)) {
			g_warning ("Error: couldn't verify private key: %d %s",
			           error ? error->code : -1, error ? error->message : "(none)");
			g_clear_error (&error);
		} else
			success = TRUE;
	} else if (item_type == TYPE_CLIENT_CERT) {
		if (!nm_setting_802_1x_set_client_cert_from_file (setting, filename, out_ck_type, &error)) {
			g_warning ("Error: couldn't verify client certificate: %d %s",
			           error ? error->code : -1, error ? error->message : "(none)");
			g_clear_error (&error);
		} else
			success = TRUE;
	} else if (item_type == TYPE_CA_CERT) {
		if (!nm_setting_802_1x_set_ca_cert_from_file (setting, filename, out_ck_type, &error)) {
			g_warning ("Error: couldn't verify CA certificate: %d %s",
			           error ? error->code : -1, error ? error->message : "(none)");
			g_clear_error (&error);
		} else
			success = TRUE;
	} else
		g_warning ("%s: invalid item type %d.", __func__, item_type);

	g_object_unref (setting);

out:
	g_free (filename);
	return success;
}

static const char *
find_tag (const char *tag, const char *buf, gsize len)
{
	gsize i, taglen;

	taglen = strlen (tag);
	if (len < taglen)
		return NULL;

	for (i = 0; i < len - taglen; i++) {
		if (memcmp (buf + i, tag, taglen) == 0)
			return buf + i;
	}
	return NULL;
}

static const char *pem_rsa_key_begin = "-----BEGIN RSA PRIVATE KEY-----";
static const char *pem_dsa_key_begin = "-----BEGIN DSA PRIVATE KEY-----";
static const char *pem_cert_begin = "-----BEGIN CERTIFICATE-----";
static const char *proc_type_tag = "Proc-Type: 4,ENCRYPTED";
static const char *dek_info_tag = "DEK-Info:";

static gboolean
file_has_extension (const char *filename, const char *extensions[])
{
	char *p, *ext;
	int i = 0;
	gboolean found = FALSE;

	p = strrchr (filename, '.');
	if (!p)
		return FALSE;

	ext = g_ascii_strdown (p, -1);
	if (ext) {
		while (extensions[i]) {
			if (!strcmp (ext, extensions[i++])) {
				found = TRUE;
				break;
			}
		}
	}
	g_free (ext);

	return found;
}

static gboolean
file_is_der_or_pem (const char *filename,
                    gboolean privkey,
                    gboolean *out_privkey_encrypted)
{
	int fd;
	unsigned char buffer[8192];
	ssize_t bytes_read;
	guint16 der_tag = 0x8230;
	gboolean success = FALSE;
	gboolean encrypted = FALSE;

	fd = open (filename, O_RDONLY);
	if (fd < 0)
		return FALSE;

	bytes_read = read (fd, buffer, sizeof (buffer) - 1);
	if (bytes_read < 400)  /* needs to be lower? */
		goto out;
	buffer[bytes_read] = '\0';

	/* Check for DER signature */
	if (!memcmp (buffer, &der_tag, 2)) {
		success = TRUE;
		goto out;
	}

	/* Check for PEM signatures */
	if (privkey) {
		if (find_tag (pem_rsa_key_begin, (const char *) buffer, bytes_read)) {
			success = TRUE;
			goto out;
		}

		if (find_tag (pem_dsa_key_begin, (const char *) buffer, bytes_read)) {
			success = TRUE;
			goto out;
		}

		/* Check if the private key is encrypted or not by looking for the
		 * old OpenSSL-style proc-type and dec-info tags.
		 */
		if (out_privkey_encrypted) {
			if (find_tag (proc_type_tag, (const char *) buffer, bytes_read)) {
				if (find_tag (dek_info_tag, (const char *) buffer, bytes_read))
					encrypted = TRUE;
			}
			*out_privkey_encrypted = encrypted;
		}
	} else {
		if (find_tag (pem_cert_begin, (const char *) buffer, bytes_read)) {
			success = TRUE;
			goto out;
		}
	}

out:
	close (fd);
	return success;
}

static gboolean
default_filter_privkey (const GtkFileFilterInfo *filter_info, gpointer user_data)
{
	const char *extensions[] = { ".der", ".pem", ".p12", NULL };
	gboolean require_encrypted = !!user_data;
	gboolean is_encrypted = TRUE;

	if (!filter_info->filename)
		return FALSE;

	if (!file_has_extension (filter_info->filename, extensions))
		return FALSE;

	if (!file_is_der_or_pem (filter_info->filename, TRUE, &is_encrypted))
		return FALSE;

	return require_encrypted ? is_encrypted : TRUE;
}

static gboolean
default_filter_cert (const GtkFileFilterInfo *filter_info, gpointer user_data)
{
	const char *extensions[] = { ".der", ".pem", ".crt", ".cer", NULL };

	if (!filter_info->filename)
		return FALSE;

	if (!file_has_extension (filter_info->filename, extensions))
		return FALSE;

	if (!file_is_der_or_pem (filter_info->filename, FALSE, NULL))
		return FALSE;

	return TRUE;
}

GtkFileFilter *
eap_method_default_file_chooser_filter_new (gboolean privkey)
{
	GtkFileFilter *filter;

	filter = gtk_file_filter_new ();
	if (privkey) {
		gtk_file_filter_add_custom (filter, GTK_FILE_FILTER_FILENAME, default_filter_privkey, NULL, NULL);
		gtk_file_filter_set_name (filter, _("DER, PEM, or PKCS#12 private keys (*.der, *.pem, *.p12)"));
	} else {
		gtk_file_filter_add_custom (filter, GTK_FILE_FILTER_FILENAME, default_filter_cert, NULL, NULL);
		gtk_file_filter_set_name (filter, _("DER or PEM certificates (*.der, *.pem, *.crt, *.cer)"));
	}
	return filter;
}

gboolean
eap_method_is_encrypted_private_key (const char *path)
{
	GtkFileFilterInfo info = { .filename = path };

	return default_filter_privkey (&info, (gpointer) TRUE);
}

