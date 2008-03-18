/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

#ifndef WIRED_DIALOG_H
#define WIRED_DIALOG_H

#include <gtk/gtk.h>
#include <nm-client.h>
#include <nm-connection.h>
#include <nm-device.h>

GtkWidget *nma_wired_dialog_new (const char *glade_file,
								 NMClient *nm_client,
								 NMConnection *connection,
								 NMDevice *device);

NMConnection *nma_wired_dialog_get_connection (GtkWidget *dialog);

#endif /* WIRED_DIALOG_H */
