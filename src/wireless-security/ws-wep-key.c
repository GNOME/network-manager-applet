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
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <glade/glade.h>
#include <ctype.h>
#include <string.h>

#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>

#include "wireless-security.h"
#include "utils.h"
#include "gnome-keyring-md5.h"
#include "gconf-helpers.h"


static void
show_toggled_cb (GtkCheckButton *button, WirelessSecurity *sec)
{
	GtkWidget *widget;
	gboolean visible;

	widget = glade_xml_get_widget (sec->xml, "wep_key_entry");
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static void
key_index_combo_changed_cb (GtkWidget *combo, WirelessSecurity *parent)
{
	WirelessSecurityWEPKey *sec = (WirelessSecurityWEPKey *) parent;
	GtkWidget *entry;
	const char *key;
	int key_index;

	/* Save WEP key for old key index */
	entry = glade_xml_get_widget (parent->xml, "wep_key_entry");
	key = gtk_entry_get_text (GTK_ENTRY (entry));
	if (key)
		strcpy (sec->keys[sec->cur_index], key);
	else
		memset (sec->keys[sec->cur_index], 0, sizeof (sec->keys[sec->cur_index]));

	key_index = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
	g_return_if_fail (key_index <= 3);
	g_return_if_fail (key_index >= 0);

	/* Populate entry with key from new index */
	gtk_entry_set_text (GTK_ENTRY (entry), sec->keys[key_index]);
	sec->cur_index = key_index;

	wireless_security_changed_cb (combo, parent);
}

static void
destroy (WirelessSecurity *parent)
{
	WirelessSecurityWEPKey *sec = (WirelessSecurityWEPKey *) parent;
	int i;

	for (i = 0; i < 4; i++)
		memset (sec->keys[i], 0, sizeof (sec->keys[i]));

	g_slice_free (WirelessSecurityWEPKey, sec);
}

static gboolean
validate (WirelessSecurity *parent, const GByteArray *ssid)
{
	WirelessSecurityWEPKey *sec = (WirelessSecurityWEPKey *) parent;
	GtkWidget *entry;
	const char *key;
	int i;

	entry = glade_xml_get_widget (parent->xml, "wep_key_entry");
	g_assert (entry);

	key = gtk_entry_get_text (GTK_ENTRY (entry));
	if (sec->type == WEP_KEY_TYPE_HEX) {
		if (!key || ((strlen (key) != 10) && (strlen (key) != 26)))
			return FALSE;

		for (i = 0; i < strlen (key); i++) {
			if (!isxdigit (key[i]))
				return FALSE;
		}
	} else if (sec->type == WEP_KEY_TYPE_ASCII) {
		if (!key || ((strlen (key) != 5) && (strlen (key) != 13)))
			return FALSE;

		for (i = 0; i < strlen (key); i++) {
			if (!isascii (key[i]))
				return FALSE;
		}
	} else if (sec->type == WEP_KEY_TYPE_PASSPHRASE) {
		if (!key || !strlen (key) || (strlen (key) > 64))
			return FALSE;
	}

	return TRUE;
}

static void
add_to_size_group (WirelessSecurity *parent, GtkSizeGroup *group)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "auth_method_label");
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "wep_key_label");
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "key_index_label");
	gtk_size_group_add_widget (group, widget);
}

static char *
wep128_passphrase_hash (const char *input)
{
	char md5_data[65];
	unsigned char digest[16];
	int input_len;
	int i;

	g_return_val_if_fail (input != NULL, NULL);

	input_len = strlen (input);
	if (input_len < 1)
		return NULL;

	/* Get at least 64 bytes */
	for (i = 0; i < 64; i++)
		md5_data [i] = input [i % input_len];

	/* Null terminate md5 seed data and hash it */
	md5_data[64] = 0;
	gnome_keyring_md5_string (md5_data, digest);
	return (utils_bin2hexstr ((const char *) &digest, 16, 26));
}

static void
fill_connection (WirelessSecurity *parent, NMConnection *connection)
{
	WirelessSecurityWEPKey *sec = (WirelessSecurityWEPKey *) parent;
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	GtkWidget *widget;
	gint auth_alg;
	const char *key;
	char *hashed = NULL;
	int i;

	widget = glade_xml_get_widget (parent->xml, "auth_method_combo");
	auth_alg = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));

	widget = glade_xml_get_widget (parent->xml, "wep_key_entry");
	key = gtk_entry_get_text (GTK_ENTRY (widget));
	strcpy (sec->keys[sec->cur_index], key);

	s_wireless = NM_SETTING_WIRELESS (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS));
	g_assert (s_wireless);

	if (s_wireless->security)
		g_free (s_wireless->security);
	s_wireless->security = g_strdup (NM_SETTING_WIRELESS_SECURITY_SETTING_NAME);

	/* Blow away the old security setting by adding a clear one */
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);

	s_wireless_sec->key_mgmt = g_strdup ("none");
	s_wireless_sec->wep_tx_keyidx = sec->cur_index;

	for (i = 0; i < 4; i++) {
		int key_len = strlen (sec->keys[i]);

		if (!key_len)
			continue;

		if (sec->type == WEP_KEY_TYPE_HEX)
			hashed = g_strdup (sec->keys[i]);
		else if (sec->type == WEP_KEY_TYPE_ASCII)
			hashed = utils_bin2hexstr (sec->keys[i], key_len, key_len * 2);
		else if (sec->type == WEP_KEY_TYPE_PASSPHRASE)
			hashed = wep128_passphrase_hash (sec->keys[i]);

		if (i == 0)
			s_wireless_sec->wep_key0 = hashed;
		else if (i == 1)
			s_wireless_sec->wep_key1 = hashed;
		else if (i == 2)
			s_wireless_sec->wep_key2 = hashed;
		else if (i == 3)
			s_wireless_sec->wep_key3 = hashed;
	}

	if (auth_alg == 0)
		s_wireless_sec->auth_alg = g_strdup ("open");
	else if (auth_alg == 1)
		s_wireless_sec->auth_alg = g_strdup ("shared");
	else
		g_assert_not_reached ();
}

static void
wep_entry_filter_cb (GtkEntry *   entry,
                     const gchar *text,
                     gint         length,
                     gint *       position,
                     gpointer     data)
{
	WirelessSecurityWEPKey *sec = (WirelessSecurityWEPKey *) data;
	GtkEditable *editable = GTK_EDITABLE (entry);
	int i, count = 0;
	gchar *result = g_new (gchar, length);

	if (sec->type == WEP_KEY_TYPE_HEX) {
		for (i = 0; i < length; i++) {
			if (isxdigit(text[i]))
				result[count++] = text[i];
		}
	} else if (sec->type == WEP_KEY_TYPE_ASCII) {
		for (i = 0; i < length; i++) {
			if (isascii(text[i]))
				result[count++] = text[i];
		}
	} else if (sec->type == WEP_KEY_TYPE_PASSPHRASE) {
		for (i = 0; i < length; i++)
			result[count++] = text[i];
	}

	if (count == 0)
		goto out;

	g_signal_handlers_block_by_func (G_OBJECT (editable),
	                                 G_CALLBACK (wep_entry_filter_cb),
	                                 data);
	gtk_editable_insert_text (editable, result, count, position);
	g_signal_handlers_unblock_by_func (G_OBJECT (editable),
	                                   G_CALLBACK (wep_entry_filter_cb),
	                                   data);

out:
	g_signal_stop_emission_by_name (G_OBJECT (editable), "insert-text");
	g_free (result);
}

WirelessSecurityWEPKey *
ws_wep_key_new (const char *glade_file,
                NMConnection *connection,
                const char *connection_id,
                WEPKeyType type,
                gboolean adhoc_create)
{
	WirelessSecurityWEPKey *sec;
	GtkWidget *widget;
	GladeXML *xml;
	NMSettingWirelessSecurity *s_wsec = NULL;
	guint8 default_key_idx = 0;
	gboolean is_adhoc = adhoc_create;
	gboolean is_shared_key = FALSE;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "wep_key_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get wep_key_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "wep_key_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	sec = g_slice_new0 (WirelessSecurityWEPKey);
	if (!sec) {
		g_object_unref (xml);
		g_object_unref (widget);
		return NULL;
	}

	wireless_security_init (WIRELESS_SECURITY (sec),
	                        validate,
	                        add_to_size_group,
	                        fill_connection,
	                        destroy,
	                        xml,
	                        widget);
	sec->type = type;

	widget = glade_xml_get_widget (xml, "wep_key_entry");
	g_assert (widget);

	/* Fill secrets, if any */
	if (connection && connection_id) {
		GHashTable *secrets;
		GError *error = NULL;
		GValue *value;

		secrets = nm_gconf_get_keyring_items (connection, connection_id,
		                                      NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
		                                      FALSE,
		                                      &error);
		if (secrets) {
			value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_WEP_KEY0);
			if (value)
				strcpy (sec->keys[0], g_value_get_string (value));

			value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_WEP_KEY1);
			if (value)
				strcpy (sec->keys[1], g_value_get_string (value));

			value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_WEP_KEY2);
			if (value)
				strcpy (sec->keys[2], g_value_get_string (value));

			value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_WEP_KEY3);
			if (value)
				strcpy (sec->keys[3], g_value_get_string (value));

			g_hash_table_destroy (secrets);
		} else if (error)
			g_error_free (error);
	}

	if (connection) {
		NMSettingWireless *s_wireless;

		s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS);
		if (s_wireless && s_wireless->mode && !strcmp (s_wireless->mode, "adhoc"))
			is_adhoc = TRUE;

		s_wsec = NM_SETTING_WIRELESS_SECURITY (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY));
		if (s_wsec && s_wsec->auth_alg && !strcmp (s_wsec->auth_alg, "shared"))
			is_shared_key = TRUE;
	}

	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);
	g_signal_connect (G_OBJECT (widget), "insert-text",
	                  (GCallback) wep_entry_filter_cb,
	                  sec);
	if (sec->type == WEP_KEY_TYPE_HEX)
		gtk_entry_set_max_length (GTK_ENTRY (widget), 26);
	else if (sec->type == WEP_KEY_TYPE_ASCII)
		gtk_entry_set_max_length (GTK_ENTRY (widget), 13);
	else if (sec->type == WEP_KEY_TYPE_PASSPHRASE)
		gtk_entry_set_max_length (GTK_ENTRY (widget), 64);

	widget = glade_xml_get_widget (xml, "key_index_combo");
	if (connection && s_wsec)
		default_key_idx = s_wsec->wep_tx_keyidx;

	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), default_key_idx);
	sec->cur_index = default_key_idx;
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) key_index_combo_changed_cb,
	                  sec);

	/* Key index is useless with adhoc networks */
	if (is_adhoc) {
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (xml, "key_index_label");
		gtk_widget_hide (widget);
	}

	/* Fill the key entry with the key for that index */
	widget = glade_xml_get_widget (xml, "wep_key_entry");
	if (strlen (sec->keys[default_key_idx]))
		gtk_entry_set_text (GTK_ENTRY (widget), sec->keys[default_key_idx]);

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  sec);

	widget = glade_xml_get_widget (xml, "auth_method_combo");
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), is_shared_key ? 1 : 0);

	/* Ad-Hoc connections can't use Shared Key auth */
	if (is_adhoc) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (xml, "auth_method_label");
		gtk_widget_hide (widget);
	}

	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);

	return sec;
}

static WEPKeyType
guess_type_for_key (const char *key)
{
	size_t len = key ? strlen (key) : 0;
	int i;

	if (!key || !len)
		return WEP_KEY_TYPE_PASSPHRASE;

	if ((len == 10) || (len == 26)) {
		gboolean hex = TRUE;

		for (i = 0; i < len; i++) {
			if (!isxdigit(key[i])) {
				hex = FALSE;
				break;
			}
		}
		if (hex)
			return WEP_KEY_TYPE_HEX;
	}

	if ((len == 5) || (len == 13)) {
		gboolean ascii = TRUE;

		for (i = 0; i < len; i++) {
			if (!isascii (key[i])) {
				ascii = FALSE;
				break;
			}
		}
		if (ascii)
			return WEP_KEY_TYPE_ASCII;
	}

	return WEP_KEY_TYPE_PASSPHRASE;
}

WEPKeyType
ws_wep_guess_key_type (NMConnection *connection, const char *connection_id)
{
	GHashTable *secrets;
	GError *error = NULL;
	GValue *value;
	WEPKeyType key_type = WEP_KEY_TYPE_PASSPHRASE;

	if (!connection)
		return WEP_KEY_TYPE_PASSPHRASE;

	g_return_val_if_fail (connection_id != NULL, WEP_KEY_TYPE_PASSPHRASE);

	secrets = nm_gconf_get_keyring_items (connection, connection_id,
	                                      NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                      FALSE,
	                                      &error);
	if (!secrets || (g_hash_table_size (secrets) == 0)) {
		if (error)
			g_error_free (error);
		return WEP_KEY_TYPE_PASSPHRASE;
	}

	value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_WEP_KEY0);
	if (value) {
		key_type = guess_type_for_key (g_value_get_string (value));
		goto out;
	}

	value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_WEP_KEY1);
	if (value) {
		key_type = guess_type_for_key (g_value_get_string (value));
		goto out;
	}

	value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_WEP_KEY2);
	if (value) {
		key_type = guess_type_for_key (g_value_get_string (value));
		goto out;
	}

	value = g_hash_table_lookup (secrets, NM_SETTING_WIRELESS_SECURITY_WEP_KEY3);
	if (value) {
		key_type = guess_type_for_key (g_value_get_string (value));
		goto out;
	}

out:
	g_hash_table_destroy (secrets);
	return key_type;
}

