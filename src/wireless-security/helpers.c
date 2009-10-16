/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * (C) Copyright 2009 Red Hat, Inc.
 */

#include "helpers.h"
#include "gconf-helpers.h"

void
helper_fill_secret_entry (NMConnection *connection,
                          GtkEntry *entry,
                          GType setting_type,
                          HelperSecretFunc func,
                          const char *setting_name,
                          const char *secret_name)
{
	NMSetting *setting;
	GHashTable *secrets;
	GError *error = NULL;
	GValue *value;
	const char *tmp;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (entry != NULL);
	g_return_if_fail (func != NULL);
	g_return_if_fail (setting_name != NULL);
	g_return_if_fail (secret_name != NULL);

	setting = nm_connection_get_setting (connection, setting_type);

	if (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_SYSTEM) {
		if (setting) {
			tmp = (*func) (setting);
			if (tmp)
				gtk_entry_set_text (entry, tmp);
		}
	} else {
		secrets = nm_gconf_get_keyring_items (connection, setting_name, TRUE, &error);
		if (!secrets) {
			g_clear_error (&error);
			return;
		}

		value = g_hash_table_lookup (secrets, secret_name);
		if (value)
			gtk_entry_set_text (entry, g_value_get_string (value));
		g_hash_table_destroy (secrets);
	}
}

