/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#ifndef NM_WIRED_DIALOG_H
#define NM_WIRED_DIALOG_H 1

#include "applet.h"
#include "nm-gconf-wso.h"

void nma_wired_dialog_create       (NMApplet *applet);
void nma_wired_dialog_ask_password (NMApplet *applet,
							 const char *network_id,
							 DBusMessage *message);

GSList *nma_wired_read_networks    (GConfClient *gconf_client);

GtkWidget *nma_wired_menu_item_new (NMApplet *applet,
							 NetworkDevice *device,
							 char *network_id,
							 NMGConfWSO *opt);

#endif /* NM_WIRED_DIALOG_H */
