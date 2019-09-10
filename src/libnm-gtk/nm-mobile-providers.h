// SPDX-License-Identifier: LGPL-2.1+
/*
 * Copyright (C) 2009 Novell, Inc.
 * Author: Tambet Ingo (tambet@gmail.com).
 *
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH.
 */

#ifndef NM_MOBILE_PROVIDERS_H
#define NM_MOBILE_PROVIDERS_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

/******************************************************************************/
/* Access method type */

typedef enum {
    NMA_MOBILE_FAMILY_UNKNOWN = 0,
    NMA_MOBILE_FAMILY_3GPP,
    NMA_MOBILE_FAMILY_CDMA
} NMAMobileFamily;

#define NMA_TYPE_MOBILE_ACCESS_METHOD (nma_mobile_access_method_get_type ())

typedef struct _NMAMobileAccessMethod NMAMobileAccessMethod;

GType                   nma_mobile_access_method_get_type     (void);
NMAMobileAccessMethod  *nma_mobile_access_method_ref          (NMAMobileAccessMethod *method);
void                    nma_mobile_access_method_unref        (NMAMobileAccessMethod *method);
const gchar            *nma_mobile_access_method_get_name     (NMAMobileAccessMethod *method);
const gchar            *nma_mobile_access_method_get_username (NMAMobileAccessMethod *method);
const gchar            *nma_mobile_access_method_get_password (NMAMobileAccessMethod *method);
const gchar            *nma_mobile_access_method_get_gateway  (NMAMobileAccessMethod *method);
const gchar           **nma_mobile_access_method_get_dns      (NMAMobileAccessMethod *method);
const gchar            *nma_mobile_access_method_get_3gpp_apn (NMAMobileAccessMethod *method);
NMAMobileFamily         nma_mobile_access_method_get_family   (NMAMobileAccessMethod *method);

/******************************************************************************/
/* Mobile provider type */

#define NMA_TYPE_MOBILE_PROVIDER (nma_mobile_provider_get_type ())

typedef struct _NMAMobileProvider NMAMobileProvider;

GType               nma_mobile_provider_get_type         (void);
NMAMobileProvider  *nma_mobile_provider_ref              (NMAMobileProvider *provider);
void                nma_mobile_provider_unref            (NMAMobileProvider *provider);
const gchar        *nma_mobile_provider_get_name         (NMAMobileProvider *provider);
GSList             *nma_mobile_provider_get_methods      (NMAMobileProvider *provider);
const gchar       **nma_mobile_provider_get_3gpp_mcc_mnc (NMAMobileProvider *provider);
const guint32      *nma_mobile_provider_get_cdma_sid     (NMAMobileProvider *provider);

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
/* Mobile providers database type */

#define NMA_TYPE_MOBILE_PROVIDERS_DATABASE            (nma_mobile_providers_database_get_type ())
#define NMA_MOBILE_PROVIDERS_DATABASE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NMA_TYPE_MOBILE_PROVIDERS_DATABASE, NMAMobileProvidersDatabase))
#define NMA_MOBILE_PROVIDERS_DATABASE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NMA_TYPE_MOBILE_PROVIDERS_DATABASE, NMAMobileProvidersDatabaseClass))
#define NMA_IS_MOBILE_PROVIDERS_DATABASE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NMA_TYPE_MOBILE_PROVIDERS_DATABASE))
#define NMA_IS_MOBILE_PROVIDERS_DATABASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NMA_TYPE_MOBILE_PROVIDERS_DATABASE))
#define NMA_MOBILE_PROVIDERS_DATABASE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NMA_TYPE_MOBILE_PROVIDERS_DATABASE, NMAMobileProvidersDatabaseClass))

typedef struct _NMAMobileProvidersDatabase NMAMobileProvidersDatabase;
typedef struct _NMAMobileProvidersDatabaseClass NMAMobileProvidersDatabaseClass;
typedef struct _NMAMobileProvidersDatabasePrivate NMAMobileProvidersDatabasePrivate;

struct _NMAMobileProvidersDatabase {
	GObject parent;
	NMAMobileProvidersDatabasePrivate *priv;
};

struct _NMAMobileProvidersDatabaseClass {
	GObjectClass parent;
};

GType nma_mobile_providers_database_get_type (void);

void                        nma_mobile_providers_database_new        (const gchar *country_codes,
                                                                      const gchar *service_providers,
                                                                      GCancellable *cancellable,
                                                                      GAsyncReadyCallback callback,
                                                                      gpointer user_data);
NMAMobileProvidersDatabase *nma_mobile_providers_database_new_finish (GAsyncResult *res,
                                                                      GError **error);
NMAMobileProvidersDatabase *nma_mobile_providers_database_new_sync   (const gchar *country_codes,
                                                                      const gchar *service_providers,
                                                                      GCancellable *cancellable,
                                                                      GError **error);

GHashTable        *nma_mobile_providers_database_get_countries       (NMAMobileProvidersDatabase *self);

void               nma_mobile_providers_database_dump                (NMAMobileProvidersDatabase *self);

NMACountryInfo    *nma_mobile_providers_database_lookup_country      (NMAMobileProvidersDatabase *self,
                                                                      const gchar *country_code);
NMAMobileProvider *nma_mobile_providers_database_lookup_3gpp_mcc_mnc (NMAMobileProvidersDatabase *self,
                                                                      const gchar *mccmnc);
NMAMobileProvider *nma_mobile_providers_database_lookup_cdma_sid     (NMAMobileProvidersDatabase *self,
                                                                      guint32 sid);

/******************************************************************************/
/* Utils */

gboolean nma_mobile_providers_split_3gpp_mcc_mnc (const gchar *mccmnc,
                                                  gchar **mcc,
                                                  gchar **mnc);

#endif /* NM_MOBILE_PROVIDERS_H */
