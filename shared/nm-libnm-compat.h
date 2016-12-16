/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2016 Red Hat, Inc.
 */

#ifndef __NM_LIBNM_COMPAT_H__
#define __NM_LIBNM_COMPAT_H__

#define NM_LIBNM_COMPAT_UNDEPRECATE(cmd) \
	({ \
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS \
		(cmd); \
		G_GNUC_END_IGNORE_DEPRECATIONS \
	})

#define NM_LIBNM_COMPAT_PROXY_SUPPORTED (NM_CHECK_VERSION (1, 5, 0))

#define nm_setting_proxy_new(setting)               NM_LIBNM_COMPAT_UNDEPRECATE (nm_setting_proxy_new (setting))
#define nm_setting_proxy_get_method(setting)        NM_LIBNM_COMPAT_UNDEPRECATE (nm_setting_proxy_get_method (setting))
#define nm_setting_proxy_get_pac_url(setting)       NM_LIBNM_COMPAT_UNDEPRECATE (nm_setting_proxy_get_pac_url (setting))
#define nm_setting_proxy_get_pac_script(setting)    NM_LIBNM_COMPAT_UNDEPRECATE (nm_setting_proxy_get_pac_script (setting))
#define nm_setting_proxy_get_browser_only(setting)  NM_LIBNM_COMPAT_UNDEPRECATE (nm_setting_proxy_get_browser_only (setting))
#define nm_connection_get_setting_proxy(connection) NM_LIBNM_COMPAT_UNDEPRECATE (nm_connection_get_setting_proxy (connection))

#endif /* __NM_LIBNM_COMPAT_H__ */
