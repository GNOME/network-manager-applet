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

gboolean
nm_gconf_get_int_helper (GConfClient *client,
					const char *path,
					const char *key,
					const char *network,
					int *value);

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
nm_gconf_set_valuehash_helper (GConfClient *client,
			       const char *path,
			       const char *network,
			       GHashTable *value);

NMConnection *
nm_gconf_read_connection (GConfClient *client,
                          const char *dir);

void
nm_gconf_write_connection (NMConnection *connection,
                           GConfClient *client,
                           const char *dir);

#endif	/* GCONF_HELPERS_H */

