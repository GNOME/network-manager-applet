/* Wireless Security Editor gconf helper functions
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnome/libgnome.h>

#if !GTK_CHECK_VERSION(2,6,0)
#include <gnome.h>
#endif

#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <NetworkManager.h>

#include "editor-gconf-helper.h"


void eh_gconf_client_set_string(WE_DATA *we_data, gchar *subkey, gchar *value)
{
	gchar 	*key;

	key = g_strdup_printf("%s/%s", we_data->cur_gconf_dir, subkey);
	gconf_client_set_string(we_data->gconf_client, key, value, NULL);
	gconf_client_suggest_sync(we_data->gconf_client, NULL);
	g_free(key);
}

gchar *eh_gconf_client_get_string(WE_DATA *we_data, gchar *subkey)
{
	gchar 	*key;
	gchar	*value;

	key = g_strdup_printf("%s/%s", we_data->cur_gconf_dir, subkey);
	value = gconf_client_get_string(we_data->gconf_client, key, NULL);
	g_free(key);

	return value;
}

void eh_gconf_client_set_int(WE_DATA *we_data, gchar *subkey, gint value)
{
	gchar 	*key;

	key = g_strdup_printf("%s/%s", we_data->cur_gconf_dir, subkey);
	gconf_client_set_int(we_data->gconf_client, key, value, NULL);
	gconf_client_suggest_sync(we_data->gconf_client, NULL);
	g_free(key);
}

gint eh_gconf_client_get_int(WE_DATA *we_data, gchar *subkey)
{
	gchar 	*key;
	gint	value = 0;

	key = g_strdup_printf("%s/%s", we_data->cur_gconf_dir, subkey);
	value = gconf_client_get_int(we_data->gconf_client, key, NULL);
	g_free(key);

	return value;
}

void eh_gconf_client_unset(WE_DATA *we_data, gchar *subkey)
{
	gchar 	*key;

	key = g_strdup_printf("%s/%s", we_data->cur_gconf_dir, subkey);
	gconf_client_unset(we_data->gconf_client,
				key,
				NULL);
	gconf_client_suggest_sync(we_data->gconf_client, NULL);
	g_free(key);
}

