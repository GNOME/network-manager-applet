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

#ifndef WIRELESS_DIALOG_H
#define WIRELESS_DIALOG_H

#include <gtk/gtk.h>
#include <nm-connection.h>

GtkWidget *	nma_wireless_dialog_new (const char *glade_file,
                                     NMConnection *connection,
                                     NMClient *nm_client);

NMConnection * nma_wireless_dialog_get_connection (GtkWidget *dialog,
                                                   NMDevice **device);

#endif	/* WIRELESS_DIALOG_H */

