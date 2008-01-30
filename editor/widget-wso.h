/* NetworkManager Wireless Widgets -- Code for WSO Widgets
 *
 * Calvin Gaisford <cgaisford@novell.com>
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
 * (C) Copyright 2006 Novell, Inc.
 */

#ifndef WIDGET_WSO_H
#define WIDGET_WSO_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "editor-app.h"
#include "editor-keyring-helper.h"
#include "editor-gconf-helper.h"

GtkWidget *get_wep_widget(WE_DATA *we_data);
GtkWidget *get_wpa_personal_widget(WE_DATA *we_data);
GtkWidget *get_wpa_enterprise_widget(WE_DATA *we_data);


#endif // WIDGET_WSO_H
