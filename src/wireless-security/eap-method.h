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

#ifndef EAP_METHOD_H
#define EAP_METHOD_H

#include <glib.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <nm-connection.h>
#include <nm-setting-8021x.h>

typedef struct _EAPMethod EAPMethod;

typedef void        (*EMAddToSizeGroupFunc) (EAPMethod *method, GtkSizeGroup *group);
typedef void        (*EMFillConnectionFunc) (EAPMethod *method, NMConnection *connection);
typedef void        (*EMDestroyFunc)        (EAPMethod *method);
typedef gboolean    (*EMValidateFunc)       (EAPMethod *method);

struct _EAPMethod {
	guint32 refcount;
	GladeXML *xml;
	GtkWidget *ui_widget;

	GladeXML *nag_dialog_xml;
	char *ca_cert_chooser;
	const char *default_field;
	GtkWidget *nag_dialog;

	gboolean ignore_ca_cert;

	EMAddToSizeGroupFunc add_to_size_group;
	EMFillConnectionFunc fill_connection;
	EMValidateFunc validate;
	EMDestroyFunc destroy;
};

#define EAP_METHOD(x) ((EAPMethod *) x)


GtkWidget *eap_method_get_widget (EAPMethod *method);

gboolean eap_method_validate (EAPMethod *method);

void eap_method_add_to_size_group (EAPMethod *method, GtkSizeGroup *group);

void eap_method_fill_connection (EAPMethod *method, NMConnection *connection);

GtkWidget * eap_method_nag_user (EAPMethod *method);

EAPMethod *eap_method_ref (EAPMethod *method);

void eap_method_unref (EAPMethod *method);

GType eap_method_get_g_type (void);

/* Below for internal use only */

#include "eap-method-tls.h"
#include "eap-method-leap.h"
#include "eap-method-ttls.h"
#include "eap-method-peap.h"
#include "eap-method-simple.h"

void eap_method_init (EAPMethod *method,
                      EMValidateFunc validate,
                      EMAddToSizeGroupFunc add_to_size_group,
                      EMFillConnectionFunc fill_connection,
                      EMDestroyFunc destroy,
                      GladeXML *xml,
                      GtkWidget *ui_widget,
                      const char *default_field);

GtkFileFilter * eap_method_default_file_chooser_filter_new (gboolean privkey);

gboolean eap_method_is_encrypted_private_key (const char *path);

#define TYPE_CLIENT_CERT 0
#define TYPE_CA_CERT     1
#define TYPE_PRIVATE_KEY 2

gboolean eap_method_validate_filepicker (GladeXML *xml,
                                         const char *name,
                                         guint32 item_type,
                                         const char *password,
                                         NMSetting8021xCKType *out_ck_type);

gboolean eap_method_nag_init (EAPMethod *method,
                              const char *glade_file,
                              const char *ca_cert_chooser,
                              NMConnection *connection);

gboolean eap_method_get_ignore_ca_cert (EAPMethod *method);

#endif /* EAP_METHOD_H */

