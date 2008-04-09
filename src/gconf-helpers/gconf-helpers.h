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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 */

#ifndef GCONF_HELPERS_H
#define GCONF_HELPERS_H

#include <gconf/gconf-client.h>
#include <glib.h>
#include <nm-connection.h>

#define GCONF_PATH_CONNECTIONS "/system/networking/connections"

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

#define KEYRING_CID_TAG "connection-id"
#define KEYRING_SN_TAG "setting-name"
#define KEYRING_SK_TAG "setting-key"

#define NMA_CONNECTION_ID_TAG "nma-connection-id"

gboolean
nm_gconf_get_int_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					int *value);

gboolean
nm_gconf_get_float_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					gfloat *value);

gboolean
nm_gconf_get_string_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					char **value);

gboolean
nm_gconf_get_bool_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					gboolean *value);

gboolean
nm_gconf_get_stringlist_helper (GConfClient *client,
				const char *path,
				const char *key,
				const char *network,
				GSList **value);

gboolean
nm_gconf_get_bytearray_helper (GConfClient *client,
			       const char *path,
			       const char *key,
			       const char *network,
			       GByteArray **value);

gboolean
nm_gconf_get_uint_array_helper (GConfClient *client,
				const char *path,
				const char *key,
				const char *network,
				GArray **value);


gboolean
nm_gconf_get_valuehash_helper (GConfClient *client,
			       const char *path,
			       const char *network,
			       GHashTable **value);

/* Setters */

gboolean
nm_gconf_set_int_helper (GConfClient *client,
                         const char *path,
                         const char *key,
                         const char *network,
                         int value);

gboolean
nm_gconf_set_float_helper (GConfClient *client,
                           const char *path,
                           const char *key,
                           const char *network,
                           gfloat value);

gboolean
nm_gconf_set_string_helper (GConfClient *client,
                            const char *path,
                            const char *key,
                            const char *network,
                            const char *value);

gboolean
nm_gconf_set_bool_helper (GConfClient *client,
                          const char *path,
                          const char *key,
                          const char *network,
                          gboolean value);

gboolean
nm_gconf_set_stringlist_helper (GConfClient *client,
                                const char *path,
                                const char *key,
                                const char *network,
                                GSList *value);

gboolean
nm_gconf_set_bytearray_helper (GConfClient *client,
                               const char *path,
                               const char *key,
                               const char *network,
                               GByteArray *value);

gboolean
nm_gconf_set_uint_array_helper (GConfClient *client,
				const char *path,
				const char *key,
				const char *network,
				GArray *value);

gboolean
nm_gconf_set_valuehash_helper (GConfClient *client,
			       const char *path,
			       const char *network,
			       GHashTable *value);

GSList *
nm_gconf_get_all_connections (GConfClient *client);

NMConnection *
nm_gconf_read_connection (GConfClient *client,
                          const char *dir);

void
nm_gconf_write_connection (NMConnection *connection,
                           GConfClient *client,
                           const char *dir,
                           const char *connection_id);

void
nm_gconf_add_keyring_item (const char *connection_id,
                           const char *connection_name,
                           const char *setting_name,
                           const char *setting_key,
                           const char *secret);

GHashTable *
nm_gconf_get_keyring_items (NMConnection *connection,
                            const char *connection_id,
                            const char *setting_name,
                            GError **error);

#endif	/* GCONF_HELPERS_H */

