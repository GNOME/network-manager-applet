/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
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
 * Copyright (C) 2009 Novell, Inc.
 * Author: Tambet Ingo (tambet@gmail.com).
 *
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

/* WARNING: this file is private API between nm-applet and various GNOME
 * bits; it may change without notice and is not guaranteed to be stable.
 */

#ifndef NM_MOBILE_PROVIDERS_H
#define NM_MOBILE_PROVIDERS_H

#include <glib.h>
#include <glib-object.h>

/******************************************************************************/
/* GSM MCCMNC type */

typedef struct {
    char *mcc;
    char *mnc;
} NMAGsmMccMnc;

/******************************************************************************/
/* Access method type */

typedef enum {
    NMA_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN = 0,
    NMA_MOBILE_ACCESS_METHOD_TYPE_GSM,
    NMA_MOBILE_ACCESS_METHOD_TYPE_CDMA
} NMAMobileAccessMethodType;

#define NMA_TYPE_MOBILE_ACCESS_METHOD (nma_mobile_access_method_get_type ())

typedef struct {
    char *name;
    /* maps lang (char *) -> name (char *) */
    GHashTable *lcl_names;

    char *username;
    char *password;
    char *gateway;
    GSList *dns; /* GSList of 'char *' */

    /* Only used with NMA_PROVIDER_TYPE_GSM */
    char *gsm_apn;

    NMAMobileAccessMethodType type;

    gint refs;
} NMAMobileAccessMethod;

GType                      nma_mobile_access_method_get_type        (void);
NMAMobileAccessMethod     *nma_mobile_access_method_ref             (NMAMobileAccessMethod *method);
void                       nma_mobile_access_method_unref           (NMAMobileAccessMethod *method);

/******************************************************************************/
/* Mobile provider type */

#define NMA_TYPE_MOBILE_PROVIDER (nma_mobile_provider_get_type ())

typedef struct {
    char *name;
    /* maps lang (char *) -> name (char *) */
    GHashTable *lcl_names;

    GSList *methods; /* GSList of NmaMobileAccessMethod */

    GSList *gsm_mcc_mnc; /* GSList of NmaGsmMccMnc */
    GSList *cdma_sid; /* GSList of guint32 */

    gint refs;
} NMAMobileProvider;

GType              nma_mobile_provider_get_type        (void);
NMAMobileProvider *nma_mobile_provider_ref             (NMAMobileProvider *provider);
void               nma_mobile_provider_unref           (NMAMobileProvider *provider);


/******************************************************************************/
/* Utils */

/* Returns a hash table where keys are country names 'char *',
   values are a 'GSList *' of 'NmaMobileProvider *'.
   Everything is destroyed with g_hash_table_destroy (). */

GHashTable *nma_mobile_providers_parse (GHashTable **out_ccs);

void nma_mobile_providers_dump (GHashTable *providers);

#endif /* NM_MOBILE_PROVIDERS_H */
