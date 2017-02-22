/* NetworkManager Applet -- allow user control over networking
 *
 * Lubomir Rintel <lkundrak@v3.sk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2015,2017 Red Hat, Inc.
 */

#ifndef NMA_FILE_CERT_CHOOSER_H
#define NMA_FILE_CERT_CHOOSER_H

#include <gtk/gtk.h>
#include "nma-cert-chooser.h"

G_BEGIN_DECLS

#define NMA_TYPE_FILE_CERT_CHOOSER                   (nma_file_cert_chooser_get_type ())
#define NMA_FILE_CERT_CHOOSER(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), NMA_TYPE_FILE_CERT_CHOOSER, NMAFileCertChooser))
#define NMA_FILE_CERT_CHOOSER_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST ((klass), NMA_TYPE_FILE_CERT_CHOOSER, NMAFileCertChooserClass))
#define NMA_IS_FILE_CERT_CHOOSER(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NMA_TYPE_FILE_CERT_CHOOSER))
#define NMA_IS_FILE_CERT_CHOOSER_CLASS(klass)        (G_TYPE_CHECK_CLASS_TYPE ((klass), NMA_TYPE_FILE_CERT_CHOOSER))
#define NMA_FILE_CERT_CHOOSER_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), NMA_TYPE_FILE_CERT_CHOOSER, NMAFileCertChooserClass))

typedef struct {
	NMACertChooser parent;
} NMAFileCertChooser;

typedef struct {
	NMACertChooserClass parent_class;
} NMAFileCertChooserClass;

GType nma_file_cert_chooser_get_type (void);

G_END_DECLS

#endif /* NMA_FILE_CERT_CHOOSER_H */
