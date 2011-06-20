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

#ifndef WIRELESS_DIALOG_H
#define WIRELESS_DIALOG_H

#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include <nm-connection.h>
#include <nm-device.h>
#include <nm-access-point.h>

#define NMA_TYPE_WIRELESS_DIALOG            (nma_wireless_dialog_get_type ())
#define NMA_WIRELESS_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NMA_TYPE_WIRELESS_DIALOG, NMAWirelessDialog))
#define NMA_WIRELESS_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NMA_TYPE_WIRELESS_DIALOG, NMAWirelessDialogClass))
#define NMA_IS_WIRELESS_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NMA_TYPE_WIRELESS_DIALOG))
#define NMA_IS_WIRELESS_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NMA_TYPE_WIRELESS_DIALOG))
#define NMA_WIRELESS_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NMA_TYPE_WIRELESS_DIALOG, NMAWirelessDialogClass))

typedef struct {
	GtkDialog parent;
} NMAWirelessDialog;

typedef struct {
	GtkDialogClass parent;
} NMAWirelessDialogClass;

GType nma_wireless_dialog_get_type (void);

GtkWidget *nma_wireless_dialog_new (NMApplet *applet,
                                    NMConnection *connection,
                                    NMDevice *device,
                                    NMAccessPoint *ap,
                                    gboolean secrets_only);

GtkWidget *nma_wireless_dialog_new_for_other (NMApplet *applet);

GtkWidget *nma_wireless_dialog_new_for_create (NMApplet *applet);

NMConnection * nma_wireless_dialog_get_connection (NMAWirelessDialog *dialog,
                                                   NMDevice **device,
                                                   NMAccessPoint **ap);

GtkWidget * nma_wireless_dialog_nag_user (NMAWirelessDialog *dialog);

void nma_wireless_dialog_set_nag_ignored (NMAWirelessDialog *dialog, gboolean ignored);

gboolean nma_wireless_dialog_get_nag_ignored (NMAWirelessDialog *dialog);

#endif	/* WIRELESS_DIALOG_H */

