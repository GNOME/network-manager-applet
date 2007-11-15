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

#ifndef EAP_METHOD_H
#define EAP_METHOD_H

#include <glib.h>
#include <gtk/gtk.h>
#include <gtk/gtksizegroup.h>
#include <glade/glade.h>
#include <gtk/gtkwidget.h>

#include <nm-connection.h>

typedef struct _EAPMethod EAPMethod;

typedef void        (*EMAddToSizeGroupFunc) (EAPMethod *method, GtkSizeGroup *group);
typedef void        (*EMFillConnectionFunc) (EAPMethod *method, NMConnection *connection);
typedef GtkWidget * (*EMNagUserFunc)        (EAPMethod *method);
typedef void        (*EMDestroyFunc)        (EAPMethod *method);
typedef gboolean    (*EMValidateFunc)       (EAPMethod *method);

struct _EAPMethod {
	guint32 refcount;
	GladeXML *xml;
	GtkWidget *ui_widget;

	EMAddToSizeGroupFunc add_to_size_group;
	EMFillConnectionFunc fill_connection;
	EMNagUserFunc nag_user;
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
                      GtkWidget *ui_widget);

GtkFileFilter * eap_method_default_file_chooser_filter_new (void);

gboolean eap_method_validate_filepicker (GladeXML *xml,
                                         const char *name,
                                         gboolean ignore_blank,
                                         gboolean is_private_key,
                                         const char *pw_entry_name);

#endif /* EAP_METHOD_H */

