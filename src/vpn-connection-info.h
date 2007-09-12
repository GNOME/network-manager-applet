/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#ifndef NM_VPN_CONNECTION_INFO_H
#define NM_VPN_CONNECTION_INFO_H

#include <glib.h>

typedef struct _VPNConnectionInfo VPNConnectionInfo;

GSList     *vpn_connection_info_list                     (void);
const char *vpn_connection_info_get_name                 (VPNConnectionInfo *info);
char       *vpn_connection_info_get_service              (VPNConnectionInfo *info);
GHashTable *vpn_connection_info_get_properties           (VPNConnectionInfo *info);
GSList     *vpn_connection_info_get_routes               (VPNConnectionInfo *info);
gboolean    vpn_connection_info_get_last_attempt_success (VPNConnectionInfo *info);
void        vpn_connection_info_set_last_attempt_success (VPNConnectionInfo *info,
											   gboolean success);

VPNConnectionInfo *vpn_connection_info_copy              (VPNConnectionInfo *info);

void        vpn_connection_info_destroy                  (VPNConnectionInfo *info);

#endif /* NM_VPN_CONNECTION_INFO_H */
