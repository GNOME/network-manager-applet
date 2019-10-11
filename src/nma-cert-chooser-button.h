// SPDX-License-Identifier: LGPL-2.1+
/* NetworkManager Applet -- allow user control over networking
 *
 * Lubomir Rintel <lkundrak@v3.sk>
 *
 * Copyright (C) 2016,2017 Red Hat, Inc.
 */

#ifndef __NMA_CERT_CHOOSER_BUTTON_H__
#define __NMA_CERT_CHOOSER_BUTTON_H__

#include <gtk/gtk.h>

/**
 * NMACertChooserButtonFlags:
 * @NMA_CERT_CHOOSER_BUTTON_FLAG_NONE: defaults
 * @NMA_CERT_CHOOSER_BUTTON_FLAG_KEY: only allow choosing a key
 *
 * Unless NMA_CERT_CHOOSER_BUTTON_FLAG_KEY is chosen, the
 * choosers allow picking a certificate or a certificate with
 * key in a single object (PKCS\#11 URI or a PKCS\#12 archive).
 */
typedef enum {
	NMA_CERT_CHOOSER_BUTTON_FLAG_NONE = 0,
	NMA_CERT_CHOOSER_BUTTON_FLAG_KEY  = 1,
} NMACertChooserButtonFlags;

typedef struct {
	GtkComboBox parent;
} NMACertChooserButton;

typedef struct {
	GtkComboBoxClass parent;
} NMACertChooserButtonClass;

#define NMA_TYPE_CERT_CHOOSER_BUTTON            (nma_cert_chooser_button_get_type ())
#define NMA_CERT_CHOOSER_BUTTON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NMA_TYPE_CERT_CHOOSER_BUTTON, NMACertChooserButton))
#define NMA_CERT_CHOOSER_BUTTON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NMA_TYPE_CERT_CHOOSER_BUTTON, NMACertChooserButtonClass))
#define NMA_IS_CERT_CHOOSER_BUTTON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NMA_TYPE_CERT_CHOOSER_BUTTON))
#define NMA_IS_CERT_CHOOSER_BUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NMA_TYPE_CERT_CHOOSER_BUTTON))
#define NMA_CERT_CHOOSER_BUTTON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NMA_TYPE_CERT_CHOOSER_BUTTON, NMACertChooserButtonClass))

GType nma_cert_chooser_button_get_type (void);

void nma_cert_chooser_button_set_title (NMACertChooserButton *button,
                                        const gchar *title);

const gchar *nma_cert_chooser_button_get_uri (NMACertChooserButton *button);

void nma_cert_chooser_button_set_uri (NMACertChooserButton *button,
                                      const gchar *uri);

gchar *nma_cert_chooser_button_get_pin (NMACertChooserButton *button);

gboolean nma_cert_chooser_button_get_remember_pin (NMACertChooserButton *button);

GtkWidget *nma_cert_chooser_button_new (NMACertChooserButtonFlags flags);

#endif /* __NMA_CERT_CHOOSER_BUTTON_H__ */
