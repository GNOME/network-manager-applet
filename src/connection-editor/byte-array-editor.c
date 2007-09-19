/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
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
 * (C) Copyright 2004-2005 Red Hat, Inc.
 */

#include <gtk/gtkentry.h>
#include <gtk/gtkhbox.h>
#include "byte-array-editor.h"

GtkWidget *
create_byte_array_editor (void)
{
     GtkWidget *box;
     guint i;

     box = gtk_hbox_new (TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (box), 0);
     for (i = 0; i < 6; i++) {
		GtkWidget *entry;

		entry = gtk_entry_new ();
		gtk_entry_set_max_length (GTK_ENTRY (entry), 2);
		gtk_entry_set_width_chars (GTK_ENTRY (entry), 2);
		gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 1);
     }

	gtk_widget_show_all (box);

     return box;
}
