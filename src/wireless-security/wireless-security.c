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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2007 Red Hat, Inc.
 */


#include <glib.h>
#include <gtk/gtk.h>

#include "wireless-security.h"

GType
wireless_security_get_g_type (void)
{
	static GType type_id = 0;

	if (!type_id) {
		type_id = g_boxed_type_register_static ("WirelessSecurity",
		                                        (GBoxedCopyFunc) wireless_security_ref,
		                                        (GBoxedFreeFunc) wireless_security_unref);
	}

	return type_id;
}

GtkWidget *
wireless_security_get_widget (WirelessSecurity *sec)
{
	g_return_val_if_fail (sec != NULL, NULL);

	return sec->ui_widget;
}

void
wireless_security_set_changed_notify (WirelessSecurity *sec,
                                      WSChangedFunc func,
                                      gpointer user_data)
{
	g_return_if_fail (sec != NULL);

	sec->changed_notify = func;
	sec->changed_notify_data = user_data;
}

void
wireless_security_changed_cb (GtkWidget *ignored, gpointer user_data)
{
	WirelessSecurity *sec = WIRELESS_SECURITY (user_data);

	if (sec->changed_notify)
		(*(sec->changed_notify)) (sec, sec->changed_notify_data);
}

gboolean
wireless_security_validate (WirelessSecurity *sec, const GByteArray *ssid)
{
	g_return_val_if_fail (sec != NULL, FALSE);

	g_assert (sec->validate);
	return (*(sec->validate)) (sec, ssid);
}

void
wireless_security_add_to_size_group (WirelessSecurity *sec, GtkSizeGroup *group)
{
	g_return_if_fail (sec != NULL);
	g_return_if_fail (group != NULL);

	g_assert (sec->add_to_size_group);
	return (*(sec->add_to_size_group)) (sec, group);
}

void
wireless_security_fill_connection (WirelessSecurity *sec,
                                   NMConnection *connection)
{
	g_return_if_fail (sec != NULL);
	g_return_if_fail (connection != NULL);

	g_assert (sec->fill_connection);
	return (*(sec->fill_connection)) (sec, connection);
}

WirelessSecurity *
wireless_security_ref (WirelessSecurity *sec)
{
	g_return_if_fail (sec != NULL);
	g_return_if_fail (sec->refcount > 0);

	sec->refcount++;
	return sec;
}

void
wireless_security_unref (WirelessSecurity *sec)
{
	g_return_if_fail (sec != NULL);
	g_return_if_fail (sec->refcount > 0);

	g_assert (sec->destroy);

	sec->refcount--;
	if (sec->refcount == 0)
		(*(sec->destroy)) (sec);
}

void
ws_wep_fill_connection (NMConnection *connection,
                        const char *key,
                        int auth_alg)
{
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, NM_SETTING_WIRELESS);
	g_assert (s_wireless);

	if (s_wireless->security)
		g_free (s_wireless->security);
	s_wireless->security = g_strdup (NM_SETTING_WIRELESS_SECURITY);

	/* Blow away the old security setting by adding a clear one */
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_setting_wireless_security_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_wireless_sec);

	s_wireless_sec->wep_key0 = g_strdup (key);
	s_wireless_sec->key_mgmt = g_strdup ("none");
	s_wireless_sec->wep_tx_keyidx = 0;

	switch (auth_alg) {
		case 0:
			s_wireless_sec->auth_alg = g_strdup ("open");
			break;
		case 1:
			s_wireless_sec->auth_alg = g_strdup ("shared");
			break;
		default:
			g_assert_not_reached ();
			break;
	}
}

void
wireless_security_init (WirelessSecurity *sec,
                        WSValidateFunc validate,
                        WSAddToSizeGroupFunc add_to_size_group,
                        WSFillConnectionFunc fill_connection,
                        WSDestroyFunc destroy,
                        GladeXML *xml,
                        GtkWidget *ui_widget)
{
	sec->refcount = 1;

	sec->validate = validate;
	sec->add_to_size_group = add_to_size_group;
	sec->fill_connection = fill_connection;
	sec->destroy = destroy;

	sec->xml = xml;
	sec->ui_widget = ui_widget;
}

void
ws_wpa_fill_default_ciphers (NMConnection *connection)
{
	NMSettingWirelessSecurity *s_wireless_sec;

	g_return_if_fail (connection != NULL);

	s_wireless_sec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection, NM_SETTING_WIRELESS_SECURITY);
	g_assert (s_wireless_sec);

	// FIXME: allow protocol selection and filter on device capabilities
	s_wireless_sec->proto = g_slist_append (s_wireless_sec->proto, g_strdup ("wpa"));
	s_wireless_sec->proto = g_slist_append (s_wireless_sec->proto, g_strdup ("rsn"));

	// FIXME: allow pairwise cipher selection and filter on device capabilities
	s_wireless_sec->pairwise = g_slist_append (s_wireless_sec->pairwise, g_strdup ("tkip"));
	s_wireless_sec->pairwise = g_slist_append (s_wireless_sec->pairwise, g_strdup ("ccmp"));

	// FIXME: allow group cipher selection and filter on device capabilities
	s_wireless_sec->group = g_slist_append (s_wireless_sec->group, g_strdup ("wep40"));
	s_wireless_sec->group = g_slist_append (s_wireless_sec->group, g_strdup ("wep104"));
	s_wireless_sec->group = g_slist_append (s_wireless_sec->group, g_strdup ("tkip"));
	s_wireless_sec->group = g_slist_append (s_wireless_sec->group, g_strdup ("ccmp"));
}

