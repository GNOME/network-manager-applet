/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * Copyright (C) 2004 - 2017 Red Hat, Inc.
 */

#include "menu-utils.h"

void
nma_menu_add_separator_item (GtkWidget *menu)
{
	GtkWidget *menu_item;

	menu_item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}

void
nma_menu_add_text_item (GtkWidget *menu, char *text)
{
	GtkWidget		*menu_item;

	g_return_if_fail (text != NULL);
	g_return_if_fail (menu != NULL);

	menu_item = gtk_menu_item_new_with_label (text);
	gtk_widget_set_sensitive (menu_item, FALSE);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}

void
applet_menu_item_add_complex_separator_helper (GtkWidget *menu,
                                               gboolean indicator_enabled,
                                               const gchar *label)
{
	GtkWidget *menu_item, *box, *xlabel, *separator;

	if (indicator_enabled) {
		/* Indicator doesn't draw complex separators */
		return;
	}

	menu_item = gtk_menu_item_new ();
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

	if (label) {
		xlabel = gtk_label_new (NULL);
		gtk_label_set_markup (GTK_LABEL (xlabel), label);

		separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
		g_object_set (G_OBJECT (separator), "valign", GTK_ALIGN_CENTER, NULL);
		gtk_box_pack_start (GTK_BOX (box), separator, TRUE, TRUE, 0);

		gtk_box_pack_start (GTK_BOX (box), xlabel, FALSE, FALSE, 2);
	}

	separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	g_object_set (G_OBJECT (separator), "valign", GTK_ALIGN_CENTER, NULL);
	gtk_box_pack_start (GTK_BOX (box), separator, TRUE, TRUE, 0);

	g_object_set (G_OBJECT (menu_item),
		          "child", box,
		          "sensitive", FALSE,
		          NULL);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
}
