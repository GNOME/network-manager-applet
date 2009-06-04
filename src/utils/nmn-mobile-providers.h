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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef NMN_MOBILE_PROVIDERS_H
#define NMN_MOBILE_PROVIDERS_H

#include <glib.h>
#include <glib-object.h>

#define NMN_TYPE_MOBILE_PROVIDER (nmn_mobile_provider_get_type ())
#define NMN_TYPE_MOBILE_ACCESS_METHOD (nmn_mobile_access_method_get_type ())

typedef enum {
    NMN_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN = 0,
    NMN_MOBILE_ACCESS_METHOD_TYPE_GSM,
    NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA
} NmnMobileAccessMethodType;

typedef struct {
    char *mcc;
    char *mnc;
} NmnGsmMccMnc;

typedef struct {
    char *name;
    /* maps lang (char *) -> name (char *) */
    GHashTable *lcl_names;

    char *username;
    char *password;
    char *gateway;
    GSList *dns; /* GSList of 'char *' */

    /* Only used with NMN_PROVIDER_TYPE_GSM */
    char *gsm_apn;
    GSList *gsm_mcc_mnc; /* GSList of NmnGsmMccMnc */

    /* Only used with NMN_PROVIDER_TYPE_CDMA */
    GSList *cdma_sid; /* GSList of guint32 */

    NmnMobileAccessMethodType type;

    gint refs;
} NmnMobileAccessMethod;

typedef struct {
    char *name;
    /* maps lang (char *) -> name (char *) */
    GHashTable *lcl_names;

    GSList *methods; /* GSList of NmnMobileAccessMethod */

    gint refs;
} NmnMobileProvider;


GType nmn_mobile_provider_get_type (void);
GType nmn_mobile_access_method_get_type (void);

NmnMobileProvider *nmn_mobile_provider_ref   (NmnMobileProvider *provider);
void               nmn_mobile_provider_unref (NmnMobileProvider *provider);

NmnMobileAccessMethod *nmn_mobile_access_method_ref   (NmnMobileAccessMethod *method);
void                   nmn_mobile_access_method_unref (NmnMobileAccessMethod *method);

/* Returns a hash table where keys are country names 'char *',
   values are a 'GSList *' of 'NmnMobileProvider *'.
   Everything is destroyed with g_hash_table_destroy (). */

GHashTable *nmn_mobile_providers_parse (GHashTable **out_ccs);

void nmn_mobile_providers_dump (GHashTable *providers);

#endif /* NMN_MOBILE_PROVIDERS_H */
