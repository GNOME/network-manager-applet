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
 * Copyright (C) 2019 Red Hat, Inc.
 */

#ifndef NMA_PRIVATE_H

#if !GTK_CHECK_VERSION(3,96,0)
#define gtk_editable_set_text(editable,text)             gtk_entry_set_text(GTK_ENTRY(editable), (text))
#define gtk_editable_get_text(editable)                  gtk_entry_get_text(GTK_ENTRY(editable))
#define gtk_editable_set_width_chars(editable, n_chars)  gtk_entry_set_width_chars(GTK_ENTRY(editable), (n_chars))
#endif

#define NMA_PRIVATE_H

#endif /* NMA_PRIVATE_H */
