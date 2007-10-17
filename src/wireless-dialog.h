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

/* The wireless dialog is used in a few situations:
 * 1) Connecting to an unseen network, in which case 'cur_device' and 'cur_ap'
 *      can be NULL because the user gets to choose
 * 2) As the password/passphrase/key dialog, in which case 'cur_device' and
 *      'cur_ap' are given and shouldn't change (and the dialog should filter
 *      options based on the capability of the device and the AP
 * 3) Creating an adhoc network, which is much like (1)
 */
GtkWidget *	nma_wireless_dialog_new (const char *glade_file,
                                     NMClient *nm_client,
                                     NMConnection *connection,
                                     NMDevice *cur_device,
                                     NMAccessPoint *cur_ap);

NMConnection * nma_wireless_dialog_get_connection (GtkWidget *dialog,
                                                   NMDevice **device);

#endif	/* WIRELESS_DIALOG_H */

