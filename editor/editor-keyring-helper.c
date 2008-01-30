/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

/* Wireless Security Editor keyring helper functions
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
#include <gnome-keyring.h>
#include <NetworkManager.h>

#include "editor-keyring-helper.h"


static GnomeKeyringResult real_get_key (const char *essid, const char *key_name, char **key)
{
	GnomeKeyringResult  ret;
	GList *         found_list = NULL;
	GnomeKeyringFound * found;

	*key = NULL;

	g_return_val_if_fail (essid != NULL, GNOME_KEYRING_RESULT_BAD_ARGUMENTS);

	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
								   &found_list,
								   key_name,
								   GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
								   essid,
								   NULL);

	if (ret != GNOME_KEYRING_RESULT_OK)
		return ret;

	if (found_list) {
		found = (GnomeKeyringFound *) found_list->data;
		*key = g_strdup (found->secret);
		gnome_keyring_found_list_free (found_list);
	}

	return ret;
}


GnomeKeyringResult get_key_from_keyring (const char *essid, char **key)
{
	return real_get_key (essid, "essid", key);
}

GnomeKeyringResult get_eap_key_from_keyring (const char *essid, char **key)
{
	return real_get_key (essid, "private-key-passwd", key);
}

static GnomeKeyringResult
real_set_key (const char *essid,
		    const char *key_name,
		    const char *description,
		    const char *key)
{
	GnomeKeyringAttributeList * attributes;
	GnomeKeyringAttribute       attr;
	GnomeKeyringResult          ret;
	guint32                 item_id;

	attributes = gnome_keyring_attribute_list_new ();
	attr.name = g_strdup (key_name);
	attr.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attr.value.string = g_strdup (essid);
	g_array_append_val (attributes, attr);

	ret = gnome_keyring_item_create_sync (NULL,
								   GNOME_KEYRING_ITEM_GENERIC_SECRET,
								   description,
								   attributes,
								   key,
								   TRUE,
								   &item_id);

	gnome_keyring_attribute_list_free (attributes);

	return ret;
}

GnomeKeyringResult set_key_in_keyring (const char *essid, const char *key)
{
	GnomeKeyringResult res;
	char *desc;

	desc = g_strdup_printf (_("Passphrase for wireless network %s"), essid);
	res = real_set_key (essid, "essid", desc, key);
	g_free (desc);

	return res;
}

GnomeKeyringResult set_eap_key_in_keyring (const char *essid, const char *key)
{
	GnomeKeyringResult res;
	char *desc;

	desc = g_strdup_printf (_("Private key password for wireless network %s"), essid);
	res = real_set_key (essid, "private-key-passwd", desc, key);
	g_free (desc);

	return res;
}
