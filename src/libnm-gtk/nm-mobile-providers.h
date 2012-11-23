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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH.
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

#define NMA_TYPE_GSM_MCC_MNC (nma_gsm_mcc_mnc_get_type ())

typedef struct {
    char *mcc;
    char *mnc;
} NMAGsmMccMnc;

GType nma_gsm_mcc_mnc_get_type (void);

/******************************************************************************/
/* Access method type */

typedef enum {
    NMA_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN = 0,
    NMA_MOBILE_ACCESS_METHOD_TYPE_GSM,
    NMA_MOBILE_ACCESS_METHOD_TYPE_CDMA
} NMAMobileAccessMethodType;

#define NMA_TYPE_MOBILE_ACCESS_METHOD (nma_mobile_access_method_get_type ())

typedef struct _NMAMobileAccessMethod NMAMobileAccessMethod;

GType                      nma_mobile_access_method_get_type        (void);
NMAMobileAccessMethod     *nma_mobile_access_method_ref             (NMAMobileAccessMethod *method);
void                       nma_mobile_access_method_unref           (NMAMobileAccessMethod *method);
const gchar               *nma_mobile_access_method_get_name        (NMAMobileAccessMethod *method);
const gchar               *nma_mobile_access_method_get_username    (NMAMobileAccessMethod *method);
const gchar               *nma_mobile_access_method_get_password    (NMAMobileAccessMethod *method);
const gchar               *nma_mobile_access_method_get_gateway     (NMAMobileAccessMethod *method);
const GSList              *nma_mobile_access_method_get_dns         (NMAMobileAccessMethod *method);
const gchar               *nma_mobile_access_method_get_gsm_apn     (NMAMobileAccessMethod *method);
NMAMobileAccessMethodType  nma_mobile_access_method_get_method_type (NMAMobileAccessMethod *method);

/******************************************************************************/
/* Mobile provider type */

#define NMA_TYPE_MOBILE_PROVIDER (nma_mobile_provider_get_type ())

typedef struct _NMAMobileProvider NMAMobileProvider;

GType              nma_mobile_provider_get_type        (void);
NMAMobileProvider *nma_mobile_provider_ref             (NMAMobileProvider *provider);
void               nma_mobile_provider_unref           (NMAMobileProvider *provider);
const gchar       *nma_mobile_provider_get_name        (NMAMobileProvider *provider);
GSList            *nma_mobile_provider_get_methods     (NMAMobileProvider *provider);
GSList            *nma_mobile_provider_get_gsm_mcc_mnc (NMAMobileProvider *provider);
GSList            *nma_mobile_provider_get_cdma_sid    (NMAMobileProvider *provider);

/******************************************************************************/
/* Country Info type */

#define NMA_TYPE_COUNTRY_INFO (nma_country_info_get_type ())

typedef struct _NMACountryInfo NMACountryInfo;

GType           nma_country_info_get_type         (void);
NMACountryInfo *nma_country_info_ref              (NMACountryInfo *country_info);
void            nma_country_info_unref            (NMACountryInfo *country_info);
const gchar    *nma_country_info_get_country_code (NMACountryInfo *country_info);
const gchar    *nma_country_info_get_country_name (NMACountryInfo *country_info);
GSList         *nma_country_info_get_providers    (NMACountryInfo *country_info);

/******************************************************************************/
/* Utils */

/* Returns a table where keys are country codes and values are NMACountryInfo
 * values */
GHashTable *nma_mobile_providers_parse (const gchar *country_codes,
                                        const gchar *service_providers);
void        nma_mobile_providers_dump  (GHashTable *country_infos);

#endif /* NM_MOBILE_PROVIDERS_H */
