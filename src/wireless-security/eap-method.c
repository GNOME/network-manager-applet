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


#include <glib.h>
#include <gtk/gtk.h>

#include "eap-method.h"

GtkWidget *
eap_method_get_widget (EAPMethod *method)
{
	g_return_val_if_fail (method != NULL, NULL);

	return method->ui_widget;
}

gboolean
eap_method_validate (EAPMethod *method)
{
	g_return_val_if_fail (method != NULL, FALSE);

	g_assert (method->validate);
	return (*(method->validate)) (method);
}

void
eap_method_add_to_size_group (EAPMethod *method, GtkSizeGroup *group)
{
	g_return_if_fail (method != NULL);
	g_return_if_fail (group != NULL);

	g_assert (method->add_to_size_group);
	return (*(method->add_to_size_group)) (method, group);
}

void
eap_method_fill_connection (EAPMethod *method, NMConnection *connection)
{
	g_return_if_fail (method != NULL);
	g_return_if_fail (connection != NULL);

	g_assert (method->fill_connection);
	return (*(method->fill_connection)) (method, connection);
}

void
eap_method_destroy (EAPMethod *method)
{
	g_return_if_fail (method != NULL);

	g_assert (method->destroy);
	(*(method->destroy)) (method);
}

