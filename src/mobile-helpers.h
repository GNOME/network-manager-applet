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

#ifndef APPLET_MOBILE_HELPERS_H
#define APPLET_MOBILE_HELPERS_H

#include <gtk/gtk.h>
#include "applet.h"

enum {
	MB_STATE_UNKNOWN = 0,
	MB_STATE_IDLE,
	MB_STATE_HOME,
	MB_STATE_SEARCHING,
	MB_STATE_DENIED,
	MB_STATE_ROAMING
};

enum {
	MB_TECH_UNKNOWN = 0,
	MB_TECH_1XRTT,
	MB_TECH_EVDO_REV0,
	MB_TECH_EVDO_REVA,
	MB_TECH_GSM,
	MB_TECH_GPRS,
	MB_TECH_EDGE,
	MB_TECH_UMTS,
	MB_TECH_HSDPA,
	MB_TECH_HSUPA,
	MB_TECH_HSPA,
	MB_TECH_HSPA_PLUS,
	MB_TECH_LTE,
	MB_TECH_WIMAX,
};

GdkPixbuf *mobile_helper_get_status_pixbuf (guint32 quality,
                                            gboolean quality_valid,
                                            guint32 state,
                                            guint32 access_tech,
                                            NMApplet *applet);

GdkPixbuf *mobile_helper_get_quality_icon (guint32 quality, NMApplet *applet);

GdkPixbuf *mobile_helper_get_tech_icon (guint32 tech, NMApplet *applet);

#endif  /* APPLET_MOBILE_HELPERS_H */

