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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 */

#include "config.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <glib/gi18n.h>

#include "nm-mobile-providers.h"

#ifndef MOBILE_BROADBAND_PROVIDER_INFO
#define MOBILE_BROADBAND_PROVIDER_INFO DATADIR"/mobile-broadband-provider-info/serviceproviders.xml"
#endif

#define ISO_3166_COUNTRY_CODES ISO_CODES_PREFIX"/share/xml/iso-codes/iso_3166.xml"
#define ISO_CODES_LOCALESDIR ISO_CODES_PREFIX"/share/locale"

/******************************************************************************/
/* GSM MCCMNC type */

static NMAGsmMccMnc *
mcc_mnc_new (const char *mcc, const char *mnc)
{
    NMAGsmMccMnc *m;

    m = g_slice_new0 (NMAGsmMccMnc);
    m->mcc = g_strstrip (g_strdup (mcc));
    m->mnc = g_strstrip (g_strdup (mnc));
    return m;
}

static void
mcc_mnc_free (NMAGsmMccMnc *m)
{
    g_return_if_fail (m != NULL);
    g_free (m->mcc);
    g_free (m->mnc);
    g_slice_free (NMAGsmMccMnc, m);
}

/******************************************************************************/
/* Access method type */

static NMAMobileAccessMethod *
access_method_new (void)
{
    NMAMobileAccessMethod *method;

    method = g_slice_new0 (NMAMobileAccessMethod);
    method->refs = 1;
    method->lcl_names = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               (GDestroyNotify) g_free,
                                               (GDestroyNotify) g_free);

    return method;
}

NMAMobileAccessMethod *
nma_mobile_access_method_ref (NMAMobileAccessMethod *method)
{
    g_return_val_if_fail (method != NULL, NULL);
    g_return_val_if_fail (method->refs > 0, NULL);

    method->refs++;

    return method;
}

void
nma_mobile_access_method_unref (NMAMobileAccessMethod *method)
{
    g_return_if_fail (method != NULL);
    g_return_if_fail (method->refs > 0);

    if (--method->refs == 0) {
        g_free (method->name);
        g_hash_table_destroy (method->lcl_names);
        g_free (method->username);
        g_free (method->password);
        g_free (method->gateway);
        g_free (method->gsm_apn);
        g_slist_foreach (method->dns, (GFunc) g_free, NULL);
        g_slist_free (method->dns);

        g_slice_free (NMAMobileAccessMethod, method);
    }
}

GType
nma_mobile_access_method_get_type (void)
{
    static GType type = 0;

    if (G_UNLIKELY (type == 0)) {
        type = g_boxed_type_register_static ("NMAMobileAccessMethod",
                                             (GBoxedCopyFunc) nma_mobile_access_method_ref,
                                             (GBoxedFreeFunc) nma_mobile_access_method_unref);
    }
    return type;
}

/******************************************************************************/
/* Mobile provider type */

static NMAMobileProvider *
provider_new (void)
{
    NMAMobileProvider *provider;

    provider = g_slice_new0 (NMAMobileProvider);
    provider->refs = 1;
    provider->lcl_names = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 (GDestroyNotify) g_free,
                                                 (GDestroyNotify) g_free);

    return provider;
}

NMAMobileProvider *
nma_mobile_provider_ref (NMAMobileProvider *provider)
{
    provider->refs++;

    return provider;
}

void
nma_mobile_provider_unref (NMAMobileProvider *provider)
{
    if (--provider->refs == 0) {
        g_free (provider->name);
        g_hash_table_destroy (provider->lcl_names);

        g_slist_foreach (provider->methods, (GFunc) nma_mobile_access_method_unref, NULL);
        g_slist_free (provider->methods);

        g_slist_foreach (provider->gsm_mcc_mnc, (GFunc) mcc_mnc_free, NULL);
        g_slist_free (provider->gsm_mcc_mnc);

        g_slist_free (provider->cdma_sid);

        g_slice_free (NMAMobileProvider, provider);
    }
}

GType
nma_mobile_provider_get_type (void)
{
    static GType type = 0;

    if (G_UNLIKELY (type == 0)) {
        type = g_boxed_type_register_static ("NMAMobileProvider",
                                             (GBoxedCopyFunc) nma_mobile_provider_ref,
                                             (GBoxedFreeFunc) nma_mobile_provider_unref);
    }
    return type;
}

/******************************************************************************/
/* XML Parser for iso_3166.xml */

static void
iso_3166_parser_start_element (GMarkupParseContext *context,
                               const gchar *element_name,
                               const gchar **attribute_names,
                               const gchar **attribute_values,
                               gpointer data,
                               GError **error)
{
    int i;
    const char *country_code = NULL;
    const char *common_name = NULL;
    const char *name = NULL;
    GHashTable *table = (GHashTable *) data;

    if (!strcmp (element_name, "iso_3166_entry")) {
        for (i = 0; attribute_names && attribute_names[i]; i++) {
            if (!strcmp (attribute_names[i], "alpha_2_code"))
                country_code = attribute_values[i];
            else if (!strcmp (attribute_names[i], "common_name"))
                common_name = attribute_values[i];
            else if (!strcmp (attribute_names[i], "name"))
                name = attribute_values[i];
        }
        if (!country_code) {
            g_warning ("%s: missing mandatory 'alpha_2_code' atribute in '%s'"
                       " element.", __func__, element_name);
            return;
        }
        if (!name) {
            g_warning ("%s: missing mandatory 'name' atribute in '%s'"
                       " element.", __func__, element_name);
            return;
        }

        g_hash_table_insert (table, g_strdup (country_code), g_strdup (dgettext ("iso_3166", common_name ? common_name : name)));
    }
}

static const GMarkupParser iso_3166_parser = {
    iso_3166_parser_start_element,
    NULL, /* end element */
    NULL, /* text */
    NULL, /* passthrough */
    NULL  /* error */
};

static GHashTable *
read_country_codes (void)
{
    GHashTable *table = NULL;
    GMarkupParseContext *ctx;
    GError *error = NULL;
    char *buf;
    gsize buf_len;

    /* Set domain to iso_3166 for country name translation */
    bindtextdomain ("iso_3166", ISO_CODES_LOCALESDIR);
    bind_textdomain_codeset ("iso_3166", "UTF-8");

    if (g_file_get_contents (ISO_3166_COUNTRY_CODES, &buf, &buf_len, &error)) {
        table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        ctx = g_markup_parse_context_new (&iso_3166_parser, 0, table, NULL);

        if (!g_markup_parse_context_parse (ctx, buf, buf_len, &error)) {
            g_warning ("Failed to parse '%s': %s\n", ISO_3166_COUNTRY_CODES, error->message);
            g_error_free (error);
            g_hash_table_destroy (table);
            table = NULL;
        }

        g_markup_parse_context_free (ctx);
        g_free (buf);
    } else {
        g_warning ("Failed to load '%s': %s\n Consider installing 'iso-codes'\n",
                   ISO_3166_COUNTRY_CODES, error->message);
        g_error_free (error);
    }

    return table;
}

/******************************************************************************/
/* XML Parser for serviceproviders.xml */

typedef enum {
    PARSER_TOPLEVEL = 0,
    PARSER_COUNTRY,
    PARSER_PROVIDER,
    PARSER_METHOD_GSM,
    PARSER_METHOD_GSM_APN,
    PARSER_METHOD_CDMA,
    PARSER_ERROR
} MobileContextState;

typedef struct {
    GHashTable *country_codes;
    GHashTable *table;

    char *current_country;
    GSList *current_providers;
    NMAMobileProvider *current_provider;
    NMAMobileAccessMethod *current_method;

    char *text_buffer;
    MobileContextState state;
} MobileParser;

static void
provider_list_free (gpointer data)
{
    GSList *list = (GSList *) data;

    while (list) {
        nma_mobile_provider_unref ((NMAMobileProvider *) list->data);
        list = g_slist_delete_link (list, list);
    }
}

static void
parser_toplevel_start (MobileParser *parser,
                       const char *name,
                       const char **attribute_names,
                       const char **attribute_values)
{
    int i;

    if (!strcmp (name, "serviceproviders")) {
        for (i = 0; attribute_names && attribute_names[i]; i++) {
            if (!strcmp (attribute_names[i], "format")) {
                if (strcmp (attribute_values[i], "2.0")) {
                    g_warning ("%s: mobile broadband provider database format '%s'"
                               " not supported.", __func__, attribute_values[i]);
                    parser->state = PARSER_ERROR;
                    break;
                }
            }
        }
    } else if (!strcmp (name, "country")) {
        for (i = 0; attribute_names && attribute_names[i]; i++) {
            if (!strcmp (attribute_names[i], "code")) {
                char *country_code;
                char *country;

                country_code = g_ascii_strup (attribute_values[i], -1);
                country = g_hash_table_lookup (parser->country_codes, country_code);
                if (country) {
                    parser->current_country = g_strdup (country);
                    g_free (country_code);
                } else
                    parser->current_country = country_code;

                parser->state = PARSER_COUNTRY;
                break;
            }
        }
    }
}

static void
parser_country_start (MobileParser *parser,
                      const char *name,
                      const char **attribute_names,
                      const char **attribute_values)
{
    if (!strcmp (name, "provider")) {
        parser->state = PARSER_PROVIDER;
        parser->current_provider = provider_new ();
    }
}

static void
parser_provider_start (MobileParser *parser,
                       const char *name,
                       const char **attribute_names,
                       const char **attribute_values)
{
    if (!strcmp (name, "gsm"))
        parser->state = PARSER_METHOD_GSM;
    else if (!strcmp (name, "cdma")) {
        parser->state = PARSER_METHOD_CDMA;
        parser->current_method = access_method_new ();
    }
}

static void
parser_gsm_start (MobileParser *parser,
                  const char *name,
                  const char **attribute_names,
                  const char **attribute_values)
{
    if (!strcmp (name, "network-id")) {
        const char *mcc = NULL, *mnc = NULL;
        int i;

        for (i = 0; attribute_names && attribute_names[i]; i++) {
            if (!strcmp (attribute_names[i], "mcc"))
                mcc = attribute_values[i];
            else if (!strcmp (attribute_names[i], "mnc"))
                mnc = attribute_values[i];

            if (mcc && strlen (mcc) && mnc && strlen (mnc)) {
                parser->current_provider->gsm_mcc_mnc = g_slist_prepend (parser->current_provider->gsm_mcc_mnc,
                                                                         mcc_mnc_new (mcc, mnc));
                break;
            }
        }
    } else if (!strcmp (name, "apn")) {
        int i;

        for (i = 0; attribute_names && attribute_names[i]; i++) {
            if (!strcmp (attribute_names[i], "value")) {

                parser->state = PARSER_METHOD_GSM_APN;
                parser->current_method = access_method_new ();
                parser->current_method->gsm_apn = g_strstrip (g_strdup (attribute_values[i]));
                break;
            }
        }
    }
}

static void
parser_cdma_start (MobileParser *parser,
                   const char *name,
                   const char **attribute_names,
                   const char **attribute_values)
{
    if (!strcmp (name, "sid")) {
        int i;

        for (i = 0; attribute_names && attribute_names[i]; i++) {
            if (!strcmp (attribute_names[i], "value")) {
                unsigned long tmp;

                errno = 0;
                tmp = strtoul (attribute_values[i], NULL, 10);
                if (errno == 0 && tmp > 0)
                    parser->current_provider->cdma_sid = g_slist_prepend (parser->current_provider->cdma_sid,
                                                                          GUINT_TO_POINTER ((guint32) tmp));
                break;
            }
        }
    }
}

static void
mobile_parser_start_element (GMarkupParseContext *context,
                             const gchar *element_name,
                             const gchar **attribute_names,
                             const gchar **attribute_values,
                             gpointer data,
                             GError **error)
{
    MobileParser *parser = (MobileParser *) data;

    if (parser->text_buffer) {
        g_free (parser->text_buffer);
        parser->text_buffer = NULL;
    }

    switch (parser->state) {
    case PARSER_TOPLEVEL:
        parser_toplevel_start (parser, element_name, attribute_names, attribute_values);
        break;
    case PARSER_COUNTRY:
        parser_country_start (parser, element_name, attribute_names, attribute_values);
        break;
    case PARSER_PROVIDER:
        parser_provider_start (parser, element_name, attribute_names, attribute_values);
        break;
    case PARSER_METHOD_GSM:
        parser_gsm_start (parser, element_name, attribute_names, attribute_values);
        break;
    case PARSER_METHOD_CDMA:
        parser_cdma_start (parser, element_name, attribute_names, attribute_values);
        break;
    default:
        break;
    }
}

static void
parser_country_end (MobileParser *parser,
                    const char *name)
{
    if (!strcmp (name, "country")) {
        g_hash_table_insert (parser->table, parser->current_country, parser->current_providers);
        parser->current_country = NULL;
        parser->current_providers = NULL;
        parser->text_buffer = NULL;
        parser->state = PARSER_TOPLEVEL;
    }
}

static void
parser_provider_end (MobileParser *parser,
                     const char *name)
{
    if (!strcmp (name, "name")) {
        if (!parser->current_provider->name) {
            /* Use the first one. */
            parser->current_provider->name = parser->text_buffer;
            parser->text_buffer = NULL;
        }
    } else if (!strcmp (name, "provider")) {
        parser->current_provider->methods = g_slist_reverse (parser->current_provider->methods);

        parser->current_provider->gsm_mcc_mnc = g_slist_reverse (parser->current_provider->gsm_mcc_mnc);
        parser->current_provider->cdma_sid = g_slist_reverse (parser->current_provider->cdma_sid);

        parser->current_providers = g_slist_prepend (parser->current_providers, parser->current_provider);
        parser->current_provider = NULL;
        parser->text_buffer = NULL;
        parser->state = PARSER_COUNTRY;
    }
}

static void
parser_gsm_end (MobileParser *parser,
                 const char *name)
{
    if (!strcmp (name, "gsm")) {
        parser->text_buffer = NULL;
        parser->state = PARSER_PROVIDER;
    }
}

static void
parser_gsm_apn_end (MobileParser *parser,
                    const char *name)
{
    if (!strcmp (name, "name")) {
        if (!parser->current_method->name) {
            /* Use the first one. */
            parser->current_method->name = parser->text_buffer;
            parser->text_buffer = NULL;
        }
    } else if (!strcmp (name, "username")) {
        parser->current_method->username = parser->text_buffer;
        parser->text_buffer = NULL;
    } else if (!strcmp (name, "password")) {
        parser->current_method->password = parser->text_buffer;
        parser->text_buffer = NULL;
    } else if (!strcmp (name, "dns")) {
        parser->current_method->dns = g_slist_prepend (parser->current_method->dns, parser->text_buffer);
        parser->text_buffer = NULL;
    } else if (!strcmp (name, "gateway")) {
        parser->current_method->gateway = parser->text_buffer;
        parser->text_buffer = NULL;
    } else if (!strcmp (name, "apn")) {
        parser->current_method->type = NMA_MOBILE_ACCESS_METHOD_TYPE_GSM;
        parser->current_method->dns = g_slist_reverse (parser->current_method->dns);

        if (!parser->current_method->name)
            parser->current_method->name = g_strdup (_("Default"));

        parser->current_provider->methods = g_slist_prepend (parser->current_provider->methods,
                                                             parser->current_method);
        parser->current_method = NULL;
        parser->text_buffer = NULL;
        parser->state = PARSER_METHOD_GSM;
    }
}

static void
parser_cdma_end (MobileParser *parser,
                 const char *name)
{
    if (!strcmp (name, "username")) {
        parser->current_method->username = parser->text_buffer;
        parser->text_buffer = NULL;
    } else if (!strcmp (name, "password")) {
        parser->current_method->password = parser->text_buffer;
        parser->text_buffer = NULL;
    } else if (!strcmp (name, "dns")) {
        parser->current_method->dns = g_slist_prepend (parser->current_method->dns, parser->text_buffer);
        parser->text_buffer = NULL;
    } else if (!strcmp (name, "gateway")) {
        parser->current_method->gateway = parser->text_buffer;
        parser->text_buffer = NULL;
    } else if (!strcmp (name, "cdma")) {
        parser->current_method->type = NMA_MOBILE_ACCESS_METHOD_TYPE_CDMA;
        parser->current_method->dns = g_slist_reverse (parser->current_method->dns);

        if (!parser->current_method->name)
            parser->current_method->name = g_strdup (parser->current_provider->name);

        parser->current_provider->methods = g_slist_prepend (parser->current_provider->methods,
                                                             parser->current_method);
        parser->current_method = NULL;
        parser->text_buffer = NULL;
        parser->state = PARSER_PROVIDER;
    }
}

static void
mobile_parser_end_element (GMarkupParseContext *context,
                           const gchar *element_name,
                           gpointer data,
                           GError **error)
{
    MobileParser *parser = (MobileParser *) data;

    switch (parser->state) {
    case PARSER_COUNTRY:
        parser_country_end (parser, element_name);
        break;
    case PARSER_PROVIDER:
        parser_provider_end (parser, element_name);
        break;
    case PARSER_METHOD_GSM:
        parser_gsm_end (parser, element_name);
        break;
    case PARSER_METHOD_GSM_APN:
        parser_gsm_apn_end (parser, element_name);
        break;
    case PARSER_METHOD_CDMA:
        parser_cdma_end (parser, element_name);
        break;
    default:
        break;
    }
}

static void
mobile_parser_characters (GMarkupParseContext *context,
                          const gchar *text,
                          gsize text_len,
                          gpointer data,
                          GError **error)
{
    MobileParser *parser = (MobileParser *) data;

    g_free (parser->text_buffer);
    parser->text_buffer = g_strdup (text);
}

static const GMarkupParser mobile_parser = {
    mobile_parser_start_element,
    mobile_parser_end_element,
    mobile_parser_characters,
    NULL, /* passthrough */
    NULL /* error */
};

/******************************************************************************/
/* Parser interface */

GHashTable *
nma_mobile_providers_parse (GHashTable **out_ccs)
{
    GMarkupParseContext *ctx;
    GIOChannel *channel;
    MobileParser parser;
    GError *error = NULL;
    char buffer[4096];
    GIOStatus status;
    gsize len = 0;

    memset (&parser, 0, sizeof (MobileParser));

    parser.country_codes = read_country_codes ();
    if (!parser.country_codes)
        goto out;

    channel = g_io_channel_new_file (MOBILE_BROADBAND_PROVIDER_INFO, "r", &error);
    if (!channel) {
        if (error) {
            g_warning ("Could not read " MOBILE_BROADBAND_PROVIDER_INFO ": %s", error->message);
            g_error_free (error);
        } else
            g_warning ("Could not read " MOBILE_BROADBAND_PROVIDER_INFO ": Unknown error");

        goto out;
    }

    parser.table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, provider_list_free);
    parser.state = PARSER_TOPLEVEL;

    ctx = g_markup_parse_context_new (&mobile_parser, 0, &parser, NULL);

    status = G_IO_STATUS_NORMAL;
    while (status == G_IO_STATUS_NORMAL) {
        status = g_io_channel_read_chars (channel, buffer, sizeof (buffer), &len, &error);

        switch (status) {
        case G_IO_STATUS_NORMAL:
            if (!g_markup_parse_context_parse (ctx, buffer, len, &error)) {
                status = G_IO_STATUS_ERROR;
                g_warning ("Error while parsing XML: %s", error->message);
                g_error_free (error);;
            }
            break;
        case G_IO_STATUS_EOF:
            break;
        case G_IO_STATUS_ERROR:
            g_warning ("Error while reading: %s", error->message);
            g_error_free (error);
            break;
        case G_IO_STATUS_AGAIN:
            /* FIXME: Try again a few times, but really, it never happes, right? */
            break;
        }
    }

    g_io_channel_unref (channel);
    g_markup_parse_context_free (ctx);

    if (parser.current_provider) {
        g_warning ("pending current provider");
        nma_mobile_provider_unref (parser.current_provider);
    }

    if (parser.current_providers) {
        g_warning ("pending current providers");
        provider_list_free (parser.current_providers);
    }

    g_free (parser.current_country);
    g_free (parser.text_buffer);

 out:
    if (parser.country_codes) {
        if (out_ccs)
            *out_ccs = parser.country_codes;
        else
            g_hash_table_destroy (parser.country_codes);
    }

    return parser.table;
}

static void
dump_generic (NMAMobileAccessMethod *method)
{
    GSList *iter;
    GString *dns;

    g_print ("        username: %s\n", method->username ? method->username : "");
    g_print ("        password: %s\n", method->password ? method->password : "");

    dns = g_string_new (NULL);
    for (iter = method->dns; iter; iter = g_slist_next (iter))
        g_string_append_printf (dns, "%s%s", dns->len ? ", " : "", (char *) iter->data);
    g_print ("        dns     : %s\n", dns->str);
    g_string_free (dns, TRUE);

    g_print ("        gateway : %s\n", method->gateway ? method->gateway : "");
}

static void
dump_cdma (NMAMobileAccessMethod *method)
{
    g_print ("     CDMA: %s\n", method->name);

    dump_generic (method);
}

static void
dump_gsm (NMAMobileAccessMethod *method)
{
    g_print ("     APN: %s (%s)\n", method->name, method->gsm_apn);

    dump_generic (method);
}

static void
dump_country (gpointer key, gpointer value, gpointer user_data)
{
    GSList *citer, *miter;

    for (citer = value; citer; citer = g_slist_next (citer)) {
        NMAMobileProvider *provider = citer->data;

        g_print ("Provider: %s (%s)\n", provider->name, (const char *) key);
        for (miter = provider->methods; miter; miter = g_slist_next (miter)) {
            NMAMobileAccessMethod *method = miter->data;
            GSList *liter;


            for (liter = provider->gsm_mcc_mnc; liter; liter = g_slist_next (liter)) {
                NMAGsmMccMnc *m = liter->data;
                g_print ("        MCC/MNC: %s-%s\n", m->mcc, m->mnc);
            }

            for (liter = provider->cdma_sid; liter; liter = g_slist_next (liter))
                g_print ("        SID: %d\n", GPOINTER_TO_UINT (liter->data));

            switch (method->type) {
            case NMA_MOBILE_ACCESS_METHOD_TYPE_CDMA:
                dump_cdma (method);
                break;
            case NMA_MOBILE_ACCESS_METHOD_TYPE_GSM:
                dump_gsm (method);
                break;
            default:
                break;
            }
            g_print ("\n");
        }
    }
}

void
nma_mobile_providers_dump (GHashTable *providers)
{
    g_return_if_fail (providers != NULL);
    g_hash_table_foreach (providers, dump_country, NULL);
}
