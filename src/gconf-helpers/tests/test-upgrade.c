/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
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
 * (C) Copyright 2011 Red Hat, Inc.
 */

#include <glib.h>
#include <string.h>
#include <gnome-keyring.h>

#include <nm-setting-wireless-security.h>
#include <nm-setting-vpn.h>

#include "gconf-helpers.h"
#include "fake-gconf.h"
#include "fake-keyring.h"

#define BASE_PATH "/system/networking/connections/"

static void
test_import_xml (void)
{
	GConfClient *client;
	GConfValue *val, *item;
	GConfEntry *entry;
	const char *tmp;
	GSList *list;
	gboolean success;

	client = gconf_client_get_default ();
	success = fake_gconf_add_xml (client, TESTDIR "/test-import.xml");
	g_assert (success);

	val = gconf_client_get (client, BASE_PATH "1/802-11-wireless/mode", NULL);
	g_assert (val);
	g_assert_cmpint (val->type, ==, GCONF_VALUE_STRING);
	tmp = gconf_value_get_string (val);
	g_assert (tmp);
	g_assert_cmpstr (tmp, ==, "infrastructure");
	gconf_value_free (val);

	val = gconf_client_get (client, BASE_PATH "1/802-11-wireless/seen-bssids", NULL);
	g_assert (val);
	g_assert_cmpint (val->type, ==, GCONF_VALUE_LIST);
	g_assert_cmpint (gconf_value_get_list_type (val), ==, GCONF_VALUE_STRING);
	list = gconf_value_get_list (val);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	item = g_slist_nth_data (list, 0);
	g_assert_cmpint (item->type, ==, GCONF_VALUE_STRING);
	g_assert_cmpstr (gconf_value_get_string (item), ==, "00:bb:cc:dd:ee:ff");
	gconf_value_free (val);

	val = gconf_client_get (client, BASE_PATH "1/802-11-wireless/ssid", NULL);
	g_assert (val);
	g_assert_cmpint (val->type, ==, GCONF_VALUE_LIST);
	g_assert_cmpint (gconf_value_get_list_type (val), ==, GCONF_VALUE_INT);
	list = gconf_value_get_list (val);
	g_assert_cmpint (g_slist_length (list), ==, 5);
	item = g_slist_nth_data (list, 0);
	g_assert_cmpint (item->type, ==, GCONF_VALUE_INT);
	g_assert_cmpint (gconf_value_get_int (item), ==, 97);
	item = g_slist_nth_data (list, 1);
	g_assert_cmpint (item->type, ==, GCONF_VALUE_INT);
	g_assert_cmpint (gconf_value_get_int (item), ==, 98);
	item = g_slist_nth_data (list, 2);
	g_assert_cmpint (item->type, ==, GCONF_VALUE_INT);
	g_assert_cmpint (gconf_value_get_int (item), ==, 99);
	item = g_slist_nth_data (list, 3);
	g_assert_cmpint (item->type, ==, GCONF_VALUE_INT);
	g_assert_cmpint (gconf_value_get_int (item), ==, 100);
	item = g_slist_nth_data (list, 4);
	g_assert_cmpint (item->type, ==, GCONF_VALUE_INT);
	g_assert_cmpint (gconf_value_get_int (item), ==, 101);
	gconf_value_free (val);

	val = gconf_client_get (client, BASE_PATH "17/connection/autoconnect", NULL);
	g_assert (val);
	g_assert_cmpint (val->type, ==, GCONF_VALUE_BOOL);
	g_assert (gconf_value_get_bool (val) == TRUE);
	gconf_value_free (val);

	val = gconf_client_get (client, BASE_PATH "17/serial/baud", NULL);
	g_assert (val);
	g_assert_cmpint (val->type, ==, GCONF_VALUE_INT);
	g_assert_cmpint (gconf_value_get_int (val), ==, 115200);
	gconf_value_free (val);

	list = gconf_client_all_dirs (client, BASE_PATH, NULL);
	g_assert_cmpint (g_slist_length (list), ==, 2);
	tmp = g_slist_nth_data (list, 0);
	g_assert_cmpstr (tmp, ==, BASE_PATH "1");
	tmp = g_slist_nth_data (list, 1);
	g_assert_cmpstr (tmp, ==, BASE_PATH "17");
	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);

	list = gconf_client_all_entries (client, BASE_PATH "1/802-11-wireless-security/", NULL);
	g_assert_cmpint (g_slist_length (list), ==, 2);
	entry = g_slist_nth_data (list, 0);
	g_assert_cmpstr (entry->key, ==, BASE_PATH "1/802-11-wireless-security/key-mgmt");
	g_assert (entry->value);
	g_assert_cmpint (entry->value->type, ==, GCONF_VALUE_STRING);
	g_assert_cmpstr (gconf_value_get_string (entry->value), ==, "wpa-psk");
	entry = g_slist_nth_data (list, 1);
	g_assert_cmpstr (entry->key, ==, BASE_PATH "1/802-11-wireless-security/name");
	g_assert (entry->value);
	g_assert_cmpint (entry->value->type, ==, GCONF_VALUE_STRING);
	g_assert_cmpstr (gconf_value_get_string (entry->value), ==, "802-11-wireless-security");

	g_object_unref (client);
}

static void
test_keyring (void)
{
	GnomeKeyringAttributeList *attrs = NULL;
	GnomeKeyringResult ret;
	guint32 first_id = 0, second_id = 0;
	GList *found_list = NULL;
	GnomeKeyringFound *found;

	/* Add an item to the keyring */
	attrs = gnome_keyring_attribute_list_new ();
	g_assert (attrs);
	gnome_keyring_attribute_list_append_string (attrs,
	                                            "FOOBAR",
	                                            "foobar-value");
	gnome_keyring_attribute_list_append_string (attrs,
	                                            "BAZ",
	                                            "baz-value");
	ret = gnome_keyring_item_create_sync (NULL,
	                                      GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      "blah blah blah",
	                                      attrs,
	                                      "really secret password",
	                                      TRUE,
	                                      &first_id);
	gnome_keyring_attribute_list_free (attrs);
	g_assert_cmpint (ret, ==, GNOME_KEYRING_RESULT_OK);
	g_assert_cmpint (first_id, !=, 0);

	/* Add a second item */
	attrs = gnome_keyring_attribute_list_new ();
	g_assert (attrs);
	gnome_keyring_attribute_list_append_string (attrs,
	                                            "BORKBORK",
	                                            "borkbork-value");
	gnome_keyring_attribute_list_append_string (attrs,
	                                            "DENOODLEZ",
	                                            "asdfasdf-value");
	ret = gnome_keyring_item_create_sync (NULL,
	                                      GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      "blahde blahde blah",
	                                      attrs,
	                                      "shh don't tell",
	                                      TRUE,
	                                      &second_id);
	gnome_keyring_attribute_list_free (attrs);
	g_assert_cmpint (ret, ==, GNOME_KEYRING_RESULT_OK);
	g_assert_cmpint (second_id, !=, 0);

	/* Find the first item */
	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      "FOOBAR",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      "foobar-value",
	                                      "BAZ",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      "baz-value",
	                                      NULL);
	g_assert_cmpint (ret, ==, GNOME_KEYRING_RESULT_OK);
	g_assert (g_list_length (found_list) == 1);
	found = found_list->data;
	g_assert (found->keyring == NULL);
	g_assert_cmpint (found->item_id, ==, first_id);
	g_assert_cmpstr (found->secret, ==, "really secret password");

	gnome_keyring_found_list_free (found_list);
	found_list = NULL;

	/* Make sure a bogus request is not found */
	ret = gnome_keyring_find_itemsv_sync (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      &found_list,
	                                      "asdfasdfasdf",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      "asdfasdfasdf",
	                                      "aagaegwge",
	                                      GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                                      "ahawwujerj23",
	                                      NULL);
	g_assert_cmpint (ret, ==, GNOME_KEYRING_RESULT_OK);
	g_assert (g_list_length (found_list) == 0);
}

#define KEYRING_UUID_TAG "connection-uuid"
#define KEYRING_SN_TAG "setting-name"
#define KEYRING_SK_TAG "setting-key"

static GnomeKeyringAttributeList *
_create_keyring_add_attr_list (const char *connection_uuid,
                               const char *connection_id,
                               const char *setting_name,
                               const char *setting_key,
                               char **out_display_name)
{
	GnomeKeyringAttributeList *attrs = NULL;

	g_return_val_if_fail (connection_uuid != NULL, NULL);
	g_return_val_if_fail (connection_id != NULL, NULL);
	g_return_val_if_fail (setting_name != NULL, NULL);
	g_return_val_if_fail (setting_key != NULL, NULL);

	if (out_display_name) {
		*out_display_name = g_strdup_printf ("Network secret for %s/%s/%s",
		                                     connection_id,
		                                     setting_name,
		                                     setting_key);
	}

	attrs = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (attrs,
	                                            KEYRING_UUID_TAG,
	                                            connection_uuid);
	gnome_keyring_attribute_list_append_string (attrs,
	                                            KEYRING_SN_TAG,
	                                            setting_name);
	gnome_keyring_attribute_list_append_string (attrs,
	                                            KEYRING_SK_TAG,
	                                            setting_key);
	return attrs;
}

static void
upgrade_08_wifi_cb (NMConnection *connection, gpointer user_data)
{
	NMSettingWirelessSecurity *s_wsec;
	NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;

	/* And check to make sure we've got our wpa-psk flags */
	s_wsec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY);
	g_assert (s_wsec);
	g_object_get (s_wsec, NM_SETTING_WIRELESS_SECURITY_PSK_FLAGS, &flags, NULL);
	g_assert_cmpint (flags, ==, NM_SETTING_SECRET_FLAG_AGENT_OWNED);

	/* and make sure the password isn't in the connection */
	g_assert (nm_setting_wireless_security_get_psk (s_wsec) == NULL);
}

static void
test_upgrade_08_wifi (void)
{
	GConfClient *client;
	gboolean success;
	GnomeKeyringAttributeList *attrs;
	char *display_name = NULL;
	GnomeKeyringResult ret;

	client = gconf_client_get_default ();
	success = fake_gconf_add_xml (client, TESTDIR "/08wifi.xml");
	g_assert (success);

	/* Add the WPA passphrase */
	attrs = _create_keyring_add_attr_list ("ca99c473-b0fb-4e16-82dd-a886f3edd099",
	                                       "Auto abcde",
	                                       NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
	                                       NM_SETTING_WIRELESS_SECURITY_PSK,
	                                       &display_name);
	g_assert (attrs);
	ret = gnome_keyring_item_create_sync (NULL,
	                                      GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      display_name,
	                                      attrs,
	                                      "really secret wpa passphrase",
	                                      TRUE,
	                                      NULL);
	g_assert_cmpint (ret, ==, GNOME_KEYRING_RESULT_OK);
	gnome_keyring_attribute_list_free (attrs);
	g_free (display_name);

	/* Now do the conversion */
	nm_gconf_move_connections_to_system (upgrade_08_wifi_cb, NULL);
}

static void
upgrade_08_vpn_cb (NMConnection *connection, gpointer user_data)
{
	NMSettingVPN *s_vpn;
	NMSettingSecretFlags flags = NM_SETTING_SECRET_FLAG_NONE;
	gboolean success;

	/* And check to make sure we've got our wpa-psk flags */
	s_vpn = (NMSettingVPN *) nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN);
	g_assert (s_vpn);

	success = nm_setting_get_secret_flags (NM_SETTING (s_vpn),
	                                       "IPSec secret",
	                                       &flags,
	                                       NULL);
	g_assert (success);
	g_assert_cmpint (flags, ==, NM_SETTING_SECRET_FLAG_AGENT_OWNED);

	success = nm_setting_get_secret_flags (NM_SETTING (s_vpn),
	                                       "XAuth password",
	                                       &flags,
	                                       NULL);
	g_assert (success);
	g_assert_cmpint (flags, ==, NM_SETTING_SECRET_FLAG_AGENT_OWNED | NM_SETTING_SECRET_FLAG_NOT_SAVED);
}

static void
test_upgrade_08_vpnc (void)
{
	GConfClient *client;
	gboolean success;
	GnomeKeyringAttributeList *attrs;
	char *display_name = NULL;
	GnomeKeyringResult ret;

	client = gconf_client_get_default ();
	success = fake_gconf_add_xml (client, TESTDIR "/08vpnc.xml");
	g_assert (success);

	/* Add the WPA passphrase */
	attrs = _create_keyring_add_attr_list ("5a4f5e4b-bfae-4ffc-ba9c-f73653a5070b",
	                                       "Test VPN",
	                                       NM_SETTING_VPN_SETTING_NAME,
	                                       "IPSec secret",
	                                       &display_name);
	g_assert (attrs);
	ret = gnome_keyring_item_create_sync (NULL,
	                                      GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                      display_name,
	                                      attrs,
	                                      "group password",
	                                      TRUE,
	                                      NULL);
	g_assert_cmpint (ret, ==, GNOME_KEYRING_RESULT_OK);
	gnome_keyring_attribute_list_free (attrs);
	g_free (display_name);

	/* Now do the conversion */
	nm_gconf_move_connections_to_system (upgrade_08_vpn_cb, NULL);
}

/*******************************************/

#if GLIB_CHECK_VERSION(2,25,12)
typedef GTestFixtureFunc TCFunc;
#else
typedef void (*TCFunc)(void);
#endif

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

int main (int argc, char **argv)
{
	GTestSuite *suite;

	g_test_init (&argc, &argv, NULL);
	g_type_init ();

	suite = g_test_get_root ();

	g_test_suite_add (suite, TESTCASE (test_import_xml, NULL));
	g_test_suite_add (suite, TESTCASE (test_keyring, NULL));

	g_test_suite_add (suite, TESTCASE (test_upgrade_08_wifi, NULL));
	g_test_suite_add (suite, TESTCASE (test_upgrade_08_vpnc, NULL));

	return g_test_run ();
}

