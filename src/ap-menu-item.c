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


G_DEFINE_TYPE (NMNetworkMenuItem, nm_network_menu_item, GTK_TYPE_IMAGE_MENU_ITEM);

#define NM_NETWORK_MENU_ITEM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_NETWORK_MENU_ITEM, NMNetworkMenuItemPrivate))

typedef struct {
	GtkWidget * ssid;
	char      * ssid_string;
	GtkWidget * strength;
	guint32     int_strength;
	GtkWidget * detail;
	GtkWidget * hbox;
	gchar *     hash;
	GSList *    dupes;
	gboolean    has_connections;
	gboolean    is_adhoc;
	gboolean    is_encrypted;
} NMNetworkMenuItemPrivate;

static void
nm_network_menu_item_init (NMNetworkMenuItem *item)
{
	NMNetworkMenuItemPrivate *priv = NM_NETWORK_MENU_ITEM_GET_PRIVATE (item);

	priv->hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	priv->ssid = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (priv->ssid), 0.0, 0.5);

	priv->detail = gtk_image_new ();

	gtk_container_add (GTK_CONTAINER (item), priv->hbox);
	gtk_box_pack_start (GTK_BOX (priv->hbox), priv->ssid, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (priv->hbox), priv->detail, FALSE, FALSE, 0);

	priv->strength = gtk_image_new ();
	gtk_box_pack_end (GTK_BOX (priv->hbox), priv->strength, FALSE, TRUE, 0);

	gtk_widget_show (priv->ssid);
	gtk_widget_show (priv->strength);
	gtk_widget_show (priv->detail);
	gtk_widget_show (priv->hbox);
}

GtkWidget*
nm_network_menu_item_new (const char *hash, gboolean has_connections)
{
	NMNetworkMenuItem *item;

	item = g_object_new (NM_TYPE_NETWORK_MENU_ITEM, NULL);
	g_assert (item);
	NM_NETWORK_MENU_ITEM_GET_PRIVATE (item)->has_connections = has_connections;
	NM_NETWORK_MENU_ITEM_GET_PRIVATE (item)->hash = g_strdup (hash);
	return GTK_WIDGET (item);
}

static void
finalize (GObject *object)
{
	NMNetworkMenuItemPrivate *priv = NM_NETWORK_MENU_ITEM_GET_PRIVATE (object);

	g_free (priv->hash);
	g_free (priv->ssid_string);

	g_slist_foreach (priv->dupes, (GFunc) g_free, NULL);
	g_slist_free (priv->dupes);

	G_OBJECT_CLASS (nm_network_menu_item_parent_class)->finalize (object);
}

static void
nm_network_menu_item_class_init (NMNetworkMenuItemClass * klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMNetworkMenuItemPrivate));

	/* virtual methods */
	object_class->finalize = finalize;
}

void
nm_network_menu_item_set_ssid (NMNetworkMenuItem *item, GByteArray *ssid)
{
	NMNetworkMenuItemPrivate *priv;

	g_return_if_fail (NM_IS_NETWORK_MENU_ITEM (item));
	g_return_if_fail (ssid != NULL);

	priv = NM_NETWORK_MENU_ITEM_GET_PRIVATE (item);

	g_free (priv->ssid_string);

	priv->ssid_string = nm_utils_ssid_to_utf8 (ssid);
	if (!priv->ssid_string) {
		// FIXME: shouldn't happen; always coerce the SSID to _something_
		priv->ssid_string = g_strdup ("<unknown>");
	}
	gtk_label_set_text (GTK_LABEL (priv->ssid), priv->ssid_string);
}

const char *
nm_network_menu_item_get_ssid (NMNetworkMenuItem *item)
{
	g_return_val_if_fail (NM_IS_NETWORK_MENU_ITEM (item), NULL);

	return NM_NETWORK_MENU_ITEM_GET_PRIVATE (item)->ssid_string;
}

guint32
nm_network_menu_item_get_strength (NMNetworkMenuItem *item)
{
	g_return_val_if_fail (NM_IS_NETWORK_MENU_ITEM (item), 0);

	return NM_NETWORK_MENU_ITEM_GET_PRIVATE (item)->int_strength;
}

void
nm_network_menu_item_best_strength (NMNetworkMenuItem *item,
                                    guint8 strength,
                                    NMApplet *applet)
{
	NMNetworkMenuItemPrivate *priv;
	GdkPixbuf *icon = NULL, *pixbuf, *top;

	g_return_if_fail (NM_IS_NETWORK_MENU_ITEM (item));

	priv = NM_NETWORK_MENU_ITEM_GET_PRIVATE (item);

	strength = MIN (strength, 100);

	/* Just do nothing if the new strength is less */
	if (strength < priv->int_strength)
		return;

	priv->int_strength = strength;

	if (strength > 80)
		icon = nma_icon_check_and_load ("nm-signal-100", applet);
	else if (strength > 55)
		icon = nma_icon_check_and_load ("nm-signal-75", applet);
	else if (strength > 30)
		icon = nma_icon_check_and_load ("nm-signal-50", applet);
	else if (strength > 5)
		icon = nma_icon_check_and_load ("nm-signal-25", applet);
	else
		icon = nma_icon_check_and_load ("nm-signal-00", applet);

	pixbuf = gdk_pixbuf_copy (icon);

	/* If the AP is "secure", composite the lock icon on top of the signal bars */
	if (priv->is_encrypted) {
		top = nma_icon_check_and_load ("nm-secure-lock", applet);
		gdk_pixbuf_composite (top, pixbuf, 0, 0, gdk_pixbuf_get_width (top),
							  gdk_pixbuf_get_height (top),
							  0, 0, 1.0, 1.0,
							  GDK_INTERP_NEAREST, 255);
	}

	/* Scale to menu size if larger so the menu doesn't look awful */
	if (gdk_pixbuf_get_height (pixbuf) > 24 || gdk_pixbuf_get_width (pixbuf) > 24) {
		GdkPixbuf *scaled;

		scaled = gdk_pixbuf_scale_simple (pixbuf, 24, 24, GDK_INTERP_BILINEAR);
		g_object_unref (pixbuf);
		pixbuf = scaled;
	}

	gtk_image_set_from_pixbuf (GTK_IMAGE (priv->strength), pixbuf);
	g_object_unref (pixbuf);
}

const char *
nm_network_menu_item_get_hash (NMNetworkMenuItem *item)
{
	g_return_val_if_fail (NM_IS_NETWORK_MENU_ITEM (item), NULL);

	return NM_NETWORK_MENU_ITEM_GET_PRIVATE (item)->hash;
}

void
nm_network_menu_item_set_detail (NMNetworkMenuItem *item,
                                 NMAccessPoint *ap,
                                 GdkPixbuf *adhoc_icon,
                                 guint32 dev_caps)
{
	NMNetworkMenuItemPrivate *priv;
	gboolean is_adhoc = FALSE;
	guint32 ap_flags, ap_wpa, ap_rsn;

	g_return_if_fail (NM_IS_NETWORK_MENU_ITEM (item));

	priv = NM_NETWORK_MENU_ITEM_GET_PRIVATE (item);

	ap_flags = nm_access_point_get_flags (ap);
	ap_wpa = nm_access_point_get_wpa_flags (ap);
	ap_rsn = nm_access_point_get_rsn_flags (ap);

	if ((ap_flags & NM_802_11_AP_FLAGS_PRIVACY) || ap_wpa || ap_rsn)
		priv->is_encrypted = TRUE;

	if (nm_access_point_get_mode (ap) == NM_802_11_MODE_ADHOC) {
		GdkPixbuf *scaled = NULL;

		priv->is_adhoc = is_adhoc = TRUE;

		if (gdk_pixbuf_get_height (adhoc_icon) > 24 || gdk_pixbuf_get_width (adhoc_icon) > 24)
			scaled = gdk_pixbuf_scale_simple (adhoc_icon, 24, 24, GDK_INTERP_BILINEAR);

		gtk_image_set_from_pixbuf (GTK_IMAGE (priv->detail), scaled ? scaled : adhoc_icon);

		g_clear_object (&scaled);
	} else
		gtk_image_set_from_stock (GTK_IMAGE (priv->detail), NULL, GTK_ICON_SIZE_MENU);

	/* Don't enable the menu item the device can't even connect to the AP */
	if (   !nm_utils_security_valid (NMU_SEC_NONE, dev_caps, TRUE, is_adhoc, ap_flags, ap_wpa, ap_rsn)
        && !nm_utils_security_valid (NMU_SEC_STATIC_WEP, dev_caps, TRUE, is_adhoc, ap_flags, ap_wpa, ap_rsn)
	    && !nm_utils_security_valid (NMU_SEC_LEAP, dev_caps, TRUE, is_adhoc, ap_flags, ap_wpa, ap_rsn)
	    && !nm_utils_security_valid (NMU_SEC_DYNAMIC_WEP, dev_caps, TRUE, is_adhoc, ap_flags, ap_wpa, ap_rsn)
	    && !nm_utils_security_valid (NMU_SEC_WPA_PSK, dev_caps, TRUE, is_adhoc, ap_flags, ap_wpa, ap_rsn)
	    && !nm_utils_security_valid (NMU_SEC_WPA2_PSK, dev_caps, TRUE, is_adhoc, ap_flags, ap_wpa, ap_rsn)
	    && !nm_utils_security_valid (NMU_SEC_WPA_ENTERPRISE, dev_caps, TRUE, is_adhoc, ap_flags, ap_wpa, ap_rsn)
	    && !nm_utils_security_valid (NMU_SEC_WPA2_ENTERPRISE, dev_caps, TRUE, is_adhoc, ap_flags, ap_wpa, ap_rsn)) {
		gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
	}
}

gboolean
nm_network_menu_item_find_dupe (NMNetworkMenuItem *item, NMAccessPoint *ap)
{
	NMNetworkMenuItemPrivate *priv;
	const char *path;
	GSList *iter;

	g_return_val_if_fail (NM_IS_NETWORK_MENU_ITEM (item), FALSE);
	g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), FALSE);

	priv = NM_NETWORK_MENU_ITEM_GET_PRIVATE (item);

	path = nm_object_get_path (NM_OBJECT (ap));
	for (iter = priv->dupes; iter; iter = g_slist_next (iter)) {
		if (!strcmp (path, iter->data))
			return TRUE;
	}
	return FALSE;
}

void
nm_network_menu_item_set_active (NMNetworkMenuItem *item, gboolean active)
{
	NMNetworkMenuItemPrivate *priv;
	char *markup;

	g_return_if_fail (NM_IS_NETWORK_MENU_ITEM (item));

	priv = NM_NETWORK_MENU_ITEM_GET_PRIVATE (item);

	gtk_label_set_use_markup (GTK_LABEL (priv->ssid), active);
	if (active) {
		markup = g_markup_printf_escaped ("<b>%s</b>", priv->ssid_string);
		gtk_label_set_markup (GTK_LABEL (priv->ssid), markup);
		g_free (markup);
	} else
		gtk_label_set_text (GTK_LABEL (priv->ssid), priv->ssid_string);
}

void
nm_network_menu_item_add_dupe (NMNetworkMenuItem *item, NMAccessPoint *ap)
{
	NMNetworkMenuItemPrivate *priv;
	const char *path;

	g_return_if_fail (NM_IS_NETWORK_MENU_ITEM (item));
	g_return_if_fail (NM_IS_ACCESS_POINT (ap));

	priv = NM_NETWORK_MENU_ITEM_GET_PRIVATE (item);
	path = nm_object_get_path (NM_OBJECT (ap));
	priv->dupes = g_slist_prepend (priv->dupes, g_strdup (path));
}

gboolean
nm_network_menu_item_get_has_connections (NMNetworkMenuItem *item)
{
	g_return_val_if_fail (NM_IS_NETWORK_MENU_ITEM (item), FALSE);

	return NM_NETWORK_MENU_ITEM_GET_PRIVATE (item)->has_connections;
}

gboolean
nm_network_menu_item_get_is_adhoc (NMNetworkMenuItem *item)
{
	g_return_val_if_fail (NM_IS_NETWORK_MENU_ITEM (item), FALSE);

	return NM_NETWORK_MENU_ITEM_GET_PRIVATE (item)->is_adhoc;
}

gboolean
nm_network_menu_item_get_is_encrypted (NMNetworkMenuItem *item)
{
	g_return_val_if_fail (NM_IS_NETWORK_MENU_ITEM (item), FALSE);

	return NM_NETWORK_MENU_ITEM_GET_PRIVATE (item)->is_encrypted;
}

