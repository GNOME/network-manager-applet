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
 * (C) Copyright 2015 Red Hat, Inc.
 */

#ifndef __NM_DEFAULT_H__
#define __NM_DEFAULT_H__

/* makefiles define NETWORKMANAGER_COMPILATION for compiling NetworkManager.
 * Depending on which parts are compiled, different values are set. */
#define NM_NETWORKMANAGER_COMPILATION_DEFAULT             0x0001
#define NM_NETWORKMANAGER_COMPILATION_LIB                 0x0002
#define NM_NETWORKMANAGER_COMPILATION_LIB_LEGACY          0x0004

#ifndef NETWORKMANAGER_COMPILATION
/* For convenience, we don't require our Makefile.am to define
 * -DNETWORKMANAGER_COMPILATION. As we now include this internal header,
 *  we know we do a NETWORKMANAGER_COMPILATION. */
#define NETWORKMANAGER_COMPILATION NM_NETWORKMANAGER_COMPILATION_DEFAULT
#endif

/*****************************************************************************/

/* always include these headers for our internal source files. */

#ifndef ___CONFIG_H__
#define ___CONFIG_H__
#include <config.h>
#endif

#include <stdlib.h>

#include "nm-utils/nm-macros-internal.h"

#include "nm-version.h"

#include <gtk/gtk.h>

/*****************************************************************************/

#if ((NETWORKMANAGER_COMPILATION) == NM_NETWORKMANAGER_COMPILATION_LIB) || ((NETWORKMANAGER_COMPILATION) == NM_NETWORKMANAGER_COMPILATION_LIB_LEGACY)

#include <glib/gi18n-lib.h>

#else

#include <glib/gi18n.h>

#endif /* NM_NETWORKMANAGER_COMPILATION_LIB || NM_NETWORKMANAGER_COMPILATION_LIB_LEGACY */

/*****************************************************************************/

#endif /* __NM_DEFAULT_H__ */
