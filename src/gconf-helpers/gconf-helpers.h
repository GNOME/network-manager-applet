/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
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
 * (C) Copyright 2005 Red Hat, Inc.
 */

#ifndef GCONF_HELPERS_H
#define GCONF_HELPERS_H

#include <gconf/gconf-client.h>
#include <glib.h>
#include <nm-connection.h>

#define GCONF_PATH_CONNECTIONS "/system/networking/connections"

/* The stamp is a mechanism for determining which applet version last
 * updated GConf for various GConf update tasks in newer applet versions.
 */
#define APPLET_CURRENT_STAMP 1
#define APPLET_PREFS_STAMP "/apps/nm-applet/stamp"


/* 
   ATTENTION: Make sure to update nm_gconf_connection_duplicate() 
   when new connection tag is added! Otherwise duplicating connection
   will not work correctly.
*/
#define NMA_CA_CERT_IGNORE_TAG  "nma-ca-cert-ignore"
#define NMA_PHASE2_CA_CERT_IGNORE_TAG  "nma-phase2-ca-cert-ignore"
#define NMA_PATH_CLIENT_CERT_TAG "nma-path-client-cert"
#define NMA_PATH_PHASE2_CLIENT_CERT_TAG "nma-path-phase2-client-cert"
#define NMA_PATH_CA_CERT_TAG "nma-path-ca-cert"
#define NMA_PATH_PHASE2_CA_CERT_TAG "nma-path-phase2-ca-cert"
#define NMA_PATH_PRIVATE_KEY_TAG "nma-path-private-key"
#define NMA_PRIVATE_KEY_PASSWORD_TAG "nma-private-key-password"
#define NMA_PATH_PHASE2_PRIVATE_KEY_TAG "nma-path-phase2-private-key"
#define NMA_PHASE2_PRIVATE_KEY_PASSWORD_TAG "nma-phase2-private-key-password"

NMConnection *nm_gconf_connection_duplicate (NMConnection *connection);

void nm_gconf_copy_private_connection_values (NMConnection *dst, NMConnection *src);
void nm_gconf_clear_private_connection_values (NMConnection *connection);

#define KEYRING_UUID_TAG "connection-uuid"
#define KEYRING_SN_TAG "setting-name"
#define KEYRING_SK_TAG "setting-key"

gboolean
nm_gconf_get_int_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *setting,
					int *value);

gboolean
nm_gconf_get_float_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *setting,
					gfloat *value);

gboolean
nm_gconf_get_string_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *setting,
					char **value);

gboolean
nm_gconf_get_bool_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *setting,
					gboolean *value);

gboolean
nm_gconf_get_stringlist_helper (GConfClient *client,
				const char *path,
				const char *key,
				const char *setting,
				GSList **value);

gboolean
nm_gconf_get_bytearray_helper (GConfClient *client,
			       const char *path,
			       const char *key,
			       const char *setting,
			       GByteArray **value);

gboolean
nm_gconf_get_uint_array_helper (GConfClient *client,
				const char *path,
				const char *key,
				const char *setting,
				GArray **value);


#if UNUSED
gboolean
nm_gconf_get_valuehash_helper (GConfClient *client,
			       const char *path,
			       const char *setting,
			       GHashTable **value);
#endif

gboolean
nm_gconf_get_stringhash_helper (GConfClient *client,
                                const char *path,
                                const char *setting,
                                GHashTable **value);

gboolean
nm_gconf_get_ip4_helper (GConfClient *client,
						  const char *path,
						  const char *key,
						  const char *setting,
						  guint32 tuple_len,
						  GPtrArray **value);

/* Setters */

gboolean
nm_gconf_set_int_helper (GConfClient *client,
                         const char *path,
                         const char *key,
                         const char *setting,
                         int value);

gboolean
nm_gconf_set_float_helper (GConfClient *client,
                           const char *path,
                           const char *key,
                           const char *setting,
                           gfloat value);

gboolean
nm_gconf_set_string_helper (GConfClient *client,
                            const char *path,
                            const char *key,
                            const char *setting,
                            const char *value);

gboolean
nm_gconf_set_bool_helper (GConfClient *client,
                          const char *path,
                          const char *key,
                          const char *setting,
                          gboolean value);

gboolean
nm_gconf_set_stringlist_helper (GConfClient *client,
                                const char *path,
                                const char *key,
                                const char *setting,
                                GSList *value);

gboolean
nm_gconf_set_bytearray_helper (GConfClient *client,
                               const char *path,
                               const char *key,
                               const char *setting,
                               GByteArray *value);

gboolean
nm_gconf_set_uint_array_helper (GConfClient *client,
				const char *path,
				const char *key,
				const char *setting,
				GArray *value);

#if UNUSED
gboolean
nm_gconf_set_valuehash_helper (GConfClient *client,
			       const char *path,
			       const char *setting,
			       GHashTable *value);
#endif

gboolean
nm_gconf_set_stringhash_helper (GConfClient *client,
                                const char *path,
                                const char *setting,
                                GHashTable *value);

gboolean
nm_gconf_set_ip4_helper (GConfClient *client,
					  const char *path,
					  const char *key,
					  const char *setting,
					  guint32 tuple_len,
					  GPtrArray *value);

GSList *
nm_gconf_get_all_connections (GConfClient *client);

NMConnection *
nm_gconf_read_connection (GConfClient *client,
                          const char *dir);

void
nm_gconf_write_connection (NMConnection *connection,
                           GConfClient *client,
                           const char *dir);

void
nm_gconf_add_keyring_item (const char *connection_uuid,
                           const char *connection_name,
                           const char *setting_name,
                           const char *setting_key,
                           const char *secret);

GHashTable *
nm_gconf_get_keyring_items (NMConnection *connection,
                            const char *setting_name,
                            gboolean include_private_passwords,
                            GError **error);

typedef void (*PreKeyringCallback) (gpointer user_data);
void nm_gconf_set_pre_keyring_callback (PreKeyringCallback func, gpointer user_data);

#endif	/* GCONF_HELPERS_H */

