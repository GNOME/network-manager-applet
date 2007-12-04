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


#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "eap-method.h"
#include "crypto.h"

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

GtkWidget *
eap_method_nag_user (EAPMethod *method)
{
	g_return_val_if_fail (method != NULL, NULL);

	if (method->nag_user)
		return (*(method->nag_user)) (method);
	return NULL;
}

void
eap_method_init (EAPMethod *method,
                 EMValidateFunc validate,
                 EMAddToSizeGroupFunc add_to_size_group,
                 EMFillConnectionFunc fill_connection,
                 EMDestroyFunc destroy,
                 GladeXML *xml,
                 GtkWidget *ui_widget)
{                 
	method->refcount = 1;

	method->validate = validate;
	method->add_to_size_group = add_to_size_group;
	method->fill_connection = fill_connection;
	method->destroy = destroy;

	method->xml = xml;
	method->ui_widget = ui_widget;
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
	if (method->refcount == 0)
		(*(method->destroy)) (method);
}

gboolean
eap_method_validate_filepicker (GladeXML *xml,
                                const char *name,
                                gboolean ignore_blank,
                                gboolean is_private_key,
                                const char *pw_entry_name)
{
	GtkWidget *widget;
	char *filename;
	gboolean success = FALSE;
	GError *error = NULL;

	if (is_private_key)
		g_return_val_if_fail (pw_entry_name != NULL, FALSE);

	widget = glade_xml_get_widget (xml, name);
	g_assert (widget);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (!filename)
		return ignore_blank ? TRUE : FALSE;

	if (!g_file_test (filename, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))
		goto out;

	if (is_private_key) {
		GByteArray *key;
		const char *pw;
		guint32 key_type = NM_CRYPTO_KEY_TYPE_UNKNOWN;

		if (!pw_entry_name)
			goto out;

		/* Need the private key password to decrypt the private key */
		widget = glade_xml_get_widget (xml, pw_entry_name);
		g_assert (widget);
		pw = gtk_entry_get_text (GTK_ENTRY (widget));
		if (!pw || !strlen (pw))
			goto out;

		key = crypto_get_private_key (filename, pw, &key_type, &error);
		if (error != NULL)
			g_clear_error (&error);

		if (key) {
			memset (key->data, 0, key->len);
			g_byte_array_free (key, TRUE);
			success = TRUE;
		}
	} else {
		GByteArray *cert;

		cert = crypto_load_and_verify_certificate (filename, &error);
		if (error != NULL) {
			g_warning ("Error: couldn't verify certificate: %d %s",
			           error->code, error->message);
			g_clear_error (&error);
		}

		if (cert) {
			g_byte_array_free (cert, TRUE);
			success = TRUE;
		}
	}

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

static gboolean
default_filter (const GtkFileFilterInfo *filter_info, gpointer data)
{
	int fd;
	unsigned char buffer[1024];
	ssize_t bytes_read;
	gboolean show = FALSE;
	guint16 der_tag = 0x8230;
	char *p;
	char *ext;

	if (!filter_info->filename)
		return FALSE;

	p = strrchr (filter_info->filename, '.');
	if (!p)
		return FALSE;

	ext = g_ascii_strdown (p, -1);
	if (!ext)
		return FALSE;
	if (strcmp (ext, ".der") && strcmp (ext, ".pem")) {
		g_free (ext);
		return FALSE;
	}
	g_free (ext);

	fd = open (filter_info->filename, O_RDONLY);
	if (fd < 0)
		return FALSE;

	bytes_read = read (fd, buffer, sizeof (buffer) - 1);
	if (bytes_read < 400)  /* needs to be lower? */
		goto out;
	buffer[bytes_read] = '\0';

	/* Check for DER signature */
	if (!memcmp (buffer, &der_tag, 2)) {
		show = TRUE;
		goto out;
	}

	/* Check for PEM signatures */
	if (find_tag (pem_rsa_key_begin, (const char *) buffer, bytes_read)) {
		show = TRUE;
		goto out;
	}

	if (find_tag (pem_dsa_key_begin, (const char *) buffer, bytes_read)) {
		show = TRUE;
		goto out;
	}

	if (find_tag (pem_cert_begin, (const char *) buffer, bytes_read)) {
		show = TRUE;
		goto out;
	}

out:
	close (fd);
	return show;
}

GtkFileFilter *
eap_method_default_file_chooser_filter_new (void)
{
	GtkFileFilter *filter;

	filter = gtk_file_filter_new ();
	gtk_file_filter_add_custom (filter, GTK_FILE_FILTER_FILENAME, default_filter, NULL, NULL);
	gtk_file_filter_set_name (filter, _("DER or PEM certificates (*.der, *.pem)"));
	return filter;
}

