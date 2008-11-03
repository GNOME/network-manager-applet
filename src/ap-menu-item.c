/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* ap-menu-item.c - Class to represent a Wifi access point 
 *
 * Jonathan Blandford <jrb@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2005 - 2008 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <glib/gi18n.h>
#include <string.h>

#include <nm-utils.h>
#include "ap-menu-item.h"
#include "nm-access-point.h"
#include "utils.h"


G_DEFINE_TYPE (NMNetworkMenuItem, nm_network_menu_item, GTK_TYPE_CHECK_MENU_ITEM);

static void
nm_network_menu_item_init (NMNetworkMenuItem * item)
{
	PangoFontDescription * fontdesc;
	PangoFontMetrics * metrics;
	PangoContext * context;
	PangoLanguage * lang;
	int ascent;

	gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);
	item->hbox = gtk_hbox_new (FALSE, 6);
	item->ssid = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (item->ssid), 0.0, 0.5);

	item->detail = gtk_image_new ();

	gtk_container_add (GTK_CONTAINER (item), item->hbox);
	gtk_box_pack_start (GTK_BOX (item->hbox), item->ssid, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (item->hbox), item->detail, FALSE, FALSE, 0);

	item->strength = gtk_progress_bar_new ();
	
	/* get the font ascent for the current font and language */
	context = gtk_widget_get_pango_context (item->strength);
	fontdesc = pango_context_get_font_description (context);
	lang = pango_context_get_language (context);
	metrics = pango_context_get_metrics (context, fontdesc, lang);
	ascent = pango_font_metrics_get_ascent (metrics) * 1.5 / PANGO_SCALE;
	pango_font_metrics_unref (metrics);

	/* size our progress bar to be five ascents long */
	gtk_widget_set_size_request (item->strength, ascent * 5, -1);

	gtk_box_pack_end (GTK_BOX (item->hbox), item->strength, FALSE, TRUE, 0);

	gtk_widget_show (item->ssid);
	gtk_widget_show (item->strength);
	gtk_widget_show (item->detail);
	gtk_widget_show (item->hbox);
}

GtkWidget*
nm_network_menu_item_new (GtkSizeGroup * size_group,
                          guchar * hash,
                          guint32 hash_len)
{
	NMNetworkMenuItem * item;

	item = g_object_new (NM_TYPE_NETWORK_MENU_ITEM, NULL);
	if (item == NULL)
		return NULL;

	item->destroyed = FALSE;
	item->int_strength = 0;
	if (hash && hash_len) {
		item->hash = g_malloc0 (hash_len);
		memcpy (item->hash, hash, hash_len);
		item->hash_len = hash_len;
	}
	if (size_group)
		gtk_size_group_add_widget (size_group, item->detail);

	return GTK_WIDGET (item);
}

static void
nm_network_menu_item_class_dispose (GObject *object)
{
	NMNetworkMenuItem * item = NM_NETWORK_MENU_ITEM (object);

	if (item->destroyed) {
		G_OBJECT_CLASS (nm_network_menu_item_parent_class)->dispose (object);
		return;
	}

	gtk_widget_destroy (item->ssid);
	gtk_widget_destroy (item->strength);
	gtk_widget_destroy (item->detail);
	gtk_widget_destroy (item->hbox);

	item->destroyed = TRUE;
	g_free (item->hash);

	g_slist_foreach (item->dupes, (GFunc) g_free, NULL);
	g_slist_free (item->dupes);

	G_OBJECT_CLASS (nm_network_menu_item_parent_class)->dispose (object);
}

static void
nm_network_menu_item_class_init (NMNetworkMenuItemClass * klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/* virtual methods */
	object_class->dispose = nm_network_menu_item_class_dispose;
}

void
nm_network_menu_item_set_ssid (NMNetworkMenuItem * item, GByteArray * ssid)
{
	char *display_ssid = NULL;

	g_return_if_fail (item != NULL);
	g_return_if_fail (ssid != NULL);

	display_ssid = nm_utils_ssid_to_utf8 ((const char *) ssid->data, ssid->len);
	if (!display_ssid) {
		// FIXME: shouldn't happen; always coerce the SSID to _something_
		gtk_label_set_text (GTK_LABEL (item->ssid), "<unknown>");
	} else {
		gtk_label_set_text (GTK_LABEL (item->ssid), display_ssid);
		g_free (display_ssid);
	}
}

guint32
nm_network_menu_item_get_strength (NMNetworkMenuItem * item)
{
	g_return_val_if_fail (item != NULL, 0);

	return item->int_strength;
}

void
nm_network_menu_item_set_strength (NMNetworkMenuItem * item, guint32 strength)
{
	double percent;

	g_return_if_fail (item != NULL);

	item->int_strength = CLAMP (strength, 0, 100);
	percent = (double) item->int_strength / 100.0;
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (item->strength), percent);
}

const guchar *
nm_network_menu_item_get_hash (NMNetworkMenuItem * item,
                               guint32 * length)
{
	g_return_val_if_fail (item != NULL, NULL);
	g_return_val_if_fail (length != NULL, NULL);

	*length = item->hash_len;
	return item->hash;
}

void
nm_network_menu_item_set_detail (NMNetworkMenuItem * item,
                                 NMAccessPoint * ap,
                                 GdkPixbuf * adhoc_icon)
{
	gboolean encrypted = FALSE, adhoc = FALSE;
	guint32 flags, wpa_flags, rsn_flags;

	flags = nm_access_point_get_flags (ap);
	wpa_flags = nm_access_point_get_wpa_flags (ap);
	rsn_flags = nm_access_point_get_rsn_flags (ap);

	if (   (flags & NM_802_11_AP_FLAGS_PRIVACY)
	    || (wpa_flags != NM_802_11_AP_SEC_NONE)
	    || (rsn_flags != NM_802_11_AP_SEC_NONE))
		encrypted = TRUE;

	if (nm_access_point_get_mode (ap) == NM_802_11_MODE_ADHOC)
		adhoc = TRUE;

	if (adhoc) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (item->detail), adhoc_icon);
	} else if (encrypted) {
		if (gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), "network-wireless-encrypted"))
			gtk_image_set_from_icon_name (GTK_IMAGE (item->detail), "network-wireless-encrypted", GTK_ICON_SIZE_MENU);
		else
			gtk_image_set_from_icon_name (GTK_IMAGE (item->detail), "gnome-lockscreen", GTK_ICON_SIZE_MENU);
	} else {
		gtk_image_set_from_stock (GTK_IMAGE (item->detail), NULL, GTK_ICON_SIZE_MENU);
	}
}

gboolean
nm_network_menu_item_find_dupe (NMNetworkMenuItem *item, NMAccessPoint *ap)
{
	const char *path;
	GSList *iter;

	g_return_val_if_fail (NM_IS_NETWORK_MENU_ITEM (item), FALSE);
	g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), FALSE);

	path = nm_object_get_path (NM_OBJECT (ap));
	for (iter = item->dupes; iter; iter = g_slist_next (iter)) {
		if (!strcmp (path, iter->data))
			return TRUE;
	}
	return FALSE;
}

void
nm_network_menu_item_add_dupe (NMNetworkMenuItem *item, NMAccessPoint *ap)
{
	const char *path;

	g_return_if_fail (NM_IS_NETWORK_MENU_ITEM (item));
	g_return_if_fail (NM_IS_ACCESS_POINT (ap));

	path = nm_object_get_path (NM_OBJECT (ap));
	item->dupes = g_slist_prepend (item->dupes, g_strdup (path));
}

