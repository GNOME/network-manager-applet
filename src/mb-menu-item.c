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
 * Copyright (C) 2005 - 2010 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <glib/gi18n.h>
#include <string.h>

#include "mb-menu-item.h"
#include "utils.h"

G_DEFINE_TYPE (NMMbMenuItem, nm_mb_menu_item, GTK_TYPE_IMAGE_MENU_ITEM);

#define NM_MB_MENU_ITEM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_MB_MENU_ITEM, NMMbMenuItemPrivate))

typedef struct {
	GtkWidget *desc;
	char *desc_string;
	GtkWidget *strength;
	guint32    int_strength;
	GtkWidget *detail;
	GtkWidget *hbox;

	gboolean   destroyed;
} NMMbMenuItemPrivate;

static const char *
get_tech_name (guint32 tech)
{
	switch (tech) {
	case MB_TECH_1XRTT:
		return _("CDMA");
		break;
	case MB_TECH_EVDO_REV0:
	case MB_TECH_EVDO_REVA:
		return _("EVDO");
		break;
	case MB_TECH_GSM:
		return _("GSM");
		break;
	case MB_TECH_GPRS:
		return _("GPRS");
		break;
	case MB_TECH_EDGE:
		return _("EDGE");
		break;
	case MB_TECH_UMTS:
		return _("UMTS");
		break;
	case MB_TECH_HSDPA:
		return _("HSDPA");
		break;
	case MB_TECH_HSUPA:
		return _("HSUPA");
		break;
	case MB_TECH_HSPA:
		return _("HSPA");
		break;
	default:
		g_assert_not_reached ();
	}
	return NULL;
}

GtkWidget *
nm_mb_menu_item_new (const char *connection_name,
                     guint32 strength,
                     const char *provider,
                     guint32 technology,
                     guint32 state,
                     NMApplet *applet)
{
	NMMbMenuItem *item;
	NMMbMenuItemPrivate *priv;
	const char *tech_name;
	GdkPixbuf *icon = NULL, *pixbuf;

	g_return_val_if_fail (technology != MB_TECH_UNKNOWN, NULL);

	item = g_object_new (NM_TYPE_MB_MENU_ITEM, NULL);
	if (!item)
		return NULL;

	priv = NM_MB_MENU_ITEM_GET_PRIVATE (item);
	priv->int_strength = strength;

	/* Construct the description string */
	tech_name = get_tech_name (technology);
	switch (state) {
	default:
	case MB_STATE_IDLE:
		if (connection_name)
			priv->desc_string = g_strdup (connection_name);
		else
			priv->desc_string = g_strdup (_("not registered"));
		break;
	case MB_STATE_HOME:
		if (connection_name) {
			if (provider)
				priv->desc_string = g_strdup_printf ("%s (%s %s)", connection_name, provider, tech_name);
			else
				priv->desc_string = g_strdup_printf ("%s (%s)", connection_name, tech_name);
		} else {
			if (provider)
				priv->desc_string = g_strdup_printf ("%s %s", provider, tech_name);
			else
				priv->desc_string = g_strdup_printf (_("Home network (%s)"), tech_name);
		}
		break;
	case MB_STATE_SEARCHING:
		if (connection_name)
			priv->desc_string = g_strdup (connection_name);
		else
			priv->desc_string = g_strdup (_("searching"));
		break;
	case MB_STATE_DENIED:
		priv->desc_string = g_strdup (_("registration denied"));
		break;
	case MB_STATE_ROAMING:
		if (connection_name)
			priv->desc_string = g_strdup_printf (_("%s (roaming %s)"), connection_name, tech_name);
		else {
			if (provider)
				priv->desc_string = g_strdup_printf (_("%s (%s roaming)"), provider, tech_name);
			else
				priv->desc_string = g_strdup_printf (_("Roaming network (%s)"), tech_name);
		}
		break;
	}

	/* Assume a connection name means the label should be active */
	if (connection_name) {
		char *markup;

		gtk_label_set_use_markup (GTK_LABEL (priv->desc), TRUE);
		markup = g_markup_printf_escaped ("<b>%s</b>", priv->desc_string);
		gtk_label_set_markup (GTK_LABEL (priv->desc), markup);
		g_free (markup);
		gtk_widget_set_sensitive (GTK_WIDGET (item), TRUE);
	} else {
		gtk_label_set_use_markup (GTK_LABEL (priv->desc), FALSE);
		gtk_label_set_text (GTK_LABEL (priv->desc), priv->desc_string);
		gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
	}

	/* And the strength icon, if applicable */
	if (strength) {
		if (strength > 80)
			icon = nma_icon_check_and_load ("nm-signal-100", &applet->wireless_100_icon, applet);
		else if (strength > 55)
			icon = nma_icon_check_and_load ("nm-signal-75", &applet->wireless_75_icon, applet);
		else if (strength > 30)
			icon = nma_icon_check_and_load ("nm-signal-50", &applet->wireless_50_icon, applet);
		else if (strength > 5)
			icon = nma_icon_check_and_load ("nm-signal-25", &applet->wireless_25_icon, applet);
		else
			icon = nma_icon_check_and_load ("nm-signal-00", &applet->wireless_00_icon, applet);

		pixbuf = gdk_pixbuf_copy (icon);

#if 0
		/* Composite technology icon here */
		if (item->is_encrypted) {
			top = nma_icon_check_and_load ("nm-secure-lock", &applet->secure_lock_icon, applet);
			gdk_pixbuf_composite (top, pixbuf, 0, 0, gdk_pixbuf_get_width (top),
								  gdk_pixbuf_get_height (top),
								  0, 0, 1.0, 1.0,
								  GDK_INTERP_NEAREST, 255);
		}
#endif

		gtk_image_set_from_pixbuf (GTK_IMAGE (priv->strength), pixbuf);
		g_object_unref (pixbuf);
	}

	return GTK_WIDGET (item);
}

/*******************************************************/

static void
nm_mb_menu_item_init (NMMbMenuItem *self)
{
	NMMbMenuItemPrivate *priv = NM_MB_MENU_ITEM_GET_PRIVATE (self);

	priv->hbox = gtk_hbox_new (FALSE, 6);
	priv->desc = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (priv->desc), 0.0, 0.5);

	gtk_container_add (GTK_CONTAINER (self), priv->hbox);
	gtk_box_pack_start (GTK_BOX (priv->hbox), priv->desc, TRUE, TRUE, 0);

	priv->strength = gtk_image_new ();
	gtk_box_pack_end (GTK_BOX (priv->hbox), priv->strength, FALSE, TRUE, 0);

	gtk_widget_show (priv->desc);
	gtk_widget_show (priv->strength);
	gtk_widget_show (priv->hbox);
}

static void
dispose (GObject *object)
{
	NMMbMenuItem *self = NM_MB_MENU_ITEM (object);
	NMMbMenuItemPrivate *priv = NM_MB_MENU_ITEM_GET_PRIVATE (self);

	if (priv->destroyed) {
		G_OBJECT_CLASS (nm_mb_menu_item_parent_class)->dispose (object);
		return;
	}
	priv->destroyed = TRUE;

	gtk_widget_destroy (priv->desc);
	gtk_widget_destroy (priv->strength);
	gtk_widget_destroy (priv->hbox);

	G_OBJECT_CLASS (nm_mb_menu_item_parent_class)->dispose (object);
}

static void
nm_mb_menu_item_class_init (NMMbMenuItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMMbMenuItemPrivate));

	/* virtual methods */
	object_class->dispose = dispose;
}

