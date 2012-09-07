/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2010 Red Hat, Inc.
 */

#include "mobile-helpers.h"

GdkPixbuf *
mobile_helper_get_status_pixbuf (guint32 quality,
                                 gboolean quality_valid,
                                 guint32 state,
                                 guint32 access_tech,
                                 NMApplet *applet)
{
	GdkPixbuf *pixbuf, *qual_pixbuf, *wwan_pixbuf, *tmp;

	wwan_pixbuf = nma_icon_check_and_load ("nm-wwan-tower", &applet->wwan_tower_icon, applet);

	if (!quality_valid)
		quality = 0;
	qual_pixbuf = mobile_helper_get_quality_icon (quality, applet);

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
	                         TRUE,
	                         gdk_pixbuf_get_bits_per_sample (qual_pixbuf),
	                         gdk_pixbuf_get_width (qual_pixbuf),
	                         gdk_pixbuf_get_height (qual_pixbuf));
	gdk_pixbuf_fill (pixbuf, 0xFFFFFF00);

	/* Composite the tower icon into the final icon at the bottom layer */
	gdk_pixbuf_composite (wwan_pixbuf, pixbuf,
	                      0, 0,
	                      gdk_pixbuf_get_width (wwan_pixbuf),
						  gdk_pixbuf_get_height (wwan_pixbuf),
						  0, 0, 1.0, 1.0,
						  GDK_INTERP_BILINEAR, 255);

	/* Composite the signal quality onto the icon on top of the WWAN tower */
	gdk_pixbuf_composite (qual_pixbuf, pixbuf,
	                      0, 0,
	                      gdk_pixbuf_get_width (qual_pixbuf),
						  gdk_pixbuf_get_height (qual_pixbuf),
						  0, 0, 1.0, 1.0,
						  GDK_INTERP_BILINEAR, 255);

	/* And finally the roaming or technology icon */
	if (state == MB_STATE_ROAMING) {
		tmp = nma_icon_check_and_load ("nm-mb-roam", &applet->mb_roaming_icon, applet);
		gdk_pixbuf_composite (tmp, pixbuf, 0, 0,
		                      gdk_pixbuf_get_width (tmp),
							  gdk_pixbuf_get_height (tmp),
							  0, 0, 1.0, 1.0,
							  GDK_INTERP_BILINEAR, 255);
	} else {
		tmp = mobile_helper_get_tech_icon (access_tech, applet);
		if (tmp) {
			gdk_pixbuf_composite (tmp, pixbuf, 0, 0,
				                  gdk_pixbuf_get_width (tmp),
								  gdk_pixbuf_get_height (tmp),
								  0, 0, 1.0, 1.0,
								  GDK_INTERP_BILINEAR, 255);
		}
	}

	/* 'pixbuf' will be freed by the caller */
	return pixbuf;
}

GdkPixbuf *
mobile_helper_get_quality_icon (guint32 quality, NMApplet *applet)
{
	if (quality > 80)
		return nma_icon_check_and_load ("nm-signal-100", &applet->wifi_100_icon, applet);
	else if (quality > 55)
		return nma_icon_check_and_load ("nm-signal-75", &applet->wifi_75_icon, applet);
	else if (quality > 30)
		return nma_icon_check_and_load ("nm-signal-50", &applet->wifi_50_icon, applet);
	else if (quality > 5)
		return nma_icon_check_and_load ("nm-signal-25", &applet->wifi_25_icon, applet);

	return nma_icon_check_and_load ("nm-signal-00", &applet->wifi_00_icon, applet);
}

GdkPixbuf *
mobile_helper_get_tech_icon (guint32 tech, NMApplet *applet)
{
	switch (tech) {
	case MB_TECH_1XRTT:
		return nma_icon_check_and_load ("nm-tech-cdma-1x", &applet->mb_tech_1x_icon, applet);
	case MB_TECH_EVDO_REV0:
	case MB_TECH_EVDO_REVA:
		return nma_icon_check_and_load ("nm-tech-evdo", &applet->mb_tech_evdo_icon, applet);
	case MB_TECH_GSM:
	case MB_TECH_GPRS:
		return nma_icon_check_and_load ("nm-tech-gprs", &applet->mb_tech_gprs_icon, applet);
	case MB_TECH_EDGE:
		return nma_icon_check_and_load ("nm-tech-edge", &applet->mb_tech_edge_icon, applet);
	case MB_TECH_UMTS:
		return nma_icon_check_and_load ("nm-tech-umts", &applet->mb_tech_umts_icon, applet);
	case MB_TECH_HSDPA:
	case MB_TECH_HSUPA:
	case MB_TECH_HSPA:
	case MB_TECH_HSPA_PLUS:
		return nma_icon_check_and_load ("nm-tech-hspa", &applet->mb_tech_hspa_icon, applet);
	case MB_TECH_LTE:
		return nma_icon_check_and_load ("nm-tech-lte", &applet->mb_tech_lte_icon, applet);
	case MB_TECH_WIMAX:
	default:
		return NULL;
	}
}

