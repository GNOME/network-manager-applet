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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2004 - 2011 Red Hat, Inc.
 */

#ifndef APPLET_VPN_REQEUST_H
#define APPLET_VPN_REQUEST_H

#include <glib-object.h>

#include <nm-connection.h>

#define APPLET_TYPE_VPN_REQUEST            (applet_vpn_request_get_type ())
#define APPLET_VPN_REQUEST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), APPLET_TYPE_VPN_REQUEST, AppletVpnRequest))
#define APPLET_VPN_REQUEST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), APPLET_TYPE_VPN_REQUEST, AppletVpnRequestClass))
#define APPLET_IS_VPN_REQUEST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), APPLET_TYPE_VPN_REQUEST))
#define APPLET_IS_VPN_REQUEST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), APPLET_TYPE_VPN_REQUEST))
#define APPLET_VPN_REQUEST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), APPLET_TYPE_VPN_REQUEST, AppletVpnRequestClass))

typedef struct {
	GObject parent;
} AppletVpnRequest;

typedef struct {
	GObjectClass parent;

	/* Signals */
	void (*done) (AppletVpnRequest *self,
	              GHashTable *secrets,
	              GError *error);
} AppletVpnRequestClass;

GType applet_vpn_request_get_type (void);

AppletVpnRequest *applet_vpn_request_new (NMConnection *connection, GError **error);

gboolean applet_vpn_request_get_secrets (AppletVpnRequest *req,
                                         gboolean retry,
                                         GError **error);

#endif  /* APPLET_VPN_REQUEST_H */

