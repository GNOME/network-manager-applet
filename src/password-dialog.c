/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
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

#include <ctype.h>
#include <config.h>
#include <nm-connection.h>
#include <dbus/dbus-glib.h>
#include <glade/glade.h>
#include <gnome-keyring.h>
#include <string.h>
#include <iwlib.h>

#include <nm-settings.h>
#include <nm-utils.h>

#include "password-dialog.h"
#include "nm-utils.h"
#include "sha1.h"

/*
 * cipher_bin2hexstr
 *
 * Convert a byte-array into a hexadecimal string.
 *
 * Code originally by Alex Larsson <alexl@redhat.com> and
 *  copyright Red Hat, Inc. under terms of the LGPL.
 *
 */
char *
cipher_bin2hexstr (const char *bytes,
                   int len,
                   int final_len)
{
	static char	hex_digits[] = "0123456789abcdef";
	char *		result;
	int			i;

	g_return_val_if_fail (bytes != NULL, NULL);
	g_return_val_if_fail (len > 0, NULL);
	g_return_val_if_fail (len < 256, NULL);	/* Arbitrary limit */

	result = g_malloc0 (len * 2 + 1);
	for (i = 0; i < len; i++)
	{
		result[2*i] = hex_digits[(bytes[i] >> 4) & 0xf];
		result[2*i+1] = hex_digits[bytes[i] & 0xf];
	}
	/* Cut converted key off at the correct length for this cipher type */
	if (final_len > -1)
		result[final_len] = '\0';

	return result;
}

#define WPA_PMK_LEN 32
char * 
nma_wpa_passphrase_to_hex (const char * key, NMConnection * connection)
{
	char *buf = NULL;
	char *output = NULL;
	NMSettingWireless *s_wireless;
	int ssid_len;

	g_return_val_if_fail (connection != NULL, NULL);

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, "802-11-wireless");
	g_return_val_if_fail (s_wireless, NULL);
	g_return_val_if_fail (s_wireless->ssid > 0, NULL);
	g_return_val_if_fail (s_wireless->ssid->len > 0, NULL);

	ssid_len = MIN (s_wireless->ssid->len, IW_ESSID_MAX_SIZE);

	buf = g_malloc0 (WPA_PMK_LEN * 2);
	pbkdf2_sha1 (key, (char *) s_wireless->ssid->data, ssid_len, 4096, (unsigned char *) buf, WPA_PMK_LEN);
	output = cipher_bin2hexstr (buf, WPA_PMK_LEN, WPA_PMK_LEN * 2);
	g_free (buf);

	return output;
}

static void
update_button_cb (GtkWidget *unused, gpointer user_data)
{
	GtkDialog *dialog = GTK_DIALOG (user_data);
	GtkWidget *button;
	GtkWidget *entry;
	GladeXML *xml;
	NMConnection *connection;
	NMSettingWirelessSecurity *s_wireless_sec;
	gboolean enable = FALSE;
	const char *key;
	guint32 key_len, i;

	g_return_if_fail (dialog != NULL);

	xml = (GladeXML *) g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_return_if_fail (xml != NULL);

	connection = NM_CONNECTION (g_object_get_data (G_OBJECT (dialog), "connection"));
	g_return_if_fail (connection != NULL);

	s_wireless_sec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection, "802-11-wireless-security");
	if (!s_wireless_sec || !s_wireless_sec->key_mgmt)
		goto out;

	entry = glade_xml_get_widget (xml, "password_entry");
	key = gtk_entry_get_text (GTK_ENTRY (entry));
	if (!key)
		goto out;
	key_len = strlen (key);

	/* Static WEP */
	if (!strcmp (s_wireless_sec->key_mgmt, "none")) {
		if ((key_len != 26) && (key_len != 10))
			goto out;

		for (i = 0; i < key_len; i++) {
			if (!isxdigit (key[i]))
				goto out;
		}

		enable = TRUE;
	} else if (!strcmp (s_wireless_sec->key_mgmt, "wpa-none") || !strcmp (s_wireless_sec->key_mgmt, "wpa-psk")) {
		if ((key_len < 8) || (key_len > 64))
			goto out;

		if (key_len == 64) {
			for (i = 0; i < key_len; i++) {
				if (!isxdigit (key[i]))
					goto out;
			}
		}

		enable = TRUE;
	}

out:
	button = glade_xml_get_widget (xml, "connect_button");
	gtk_widget_set_sensitive (button, enable);
}

static void
entry_changed_cb (GtkWidget *entry,
                  gpointer user_data)
{
	GtkDialog *	dialog = GTK_DIALOG (user_data);

	g_return_if_fail (dialog != NULL);

	update_button_cb (NULL, dialog);
}

static void
showpasscb_toggled_cb (GtkWidget *cb,
				   gpointer user_data)
{
	GtkEntry *      entry = GTK_ENTRY (user_data);

	g_return_if_fail (entry != NULL);

	gtk_entry_set_visibility (entry, gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(cb)));
}

static void
destroy_gvalue (gpointer data)
{
	GValue *value = (GValue *) data;

	g_value_unset (value);
	g_slice_free (GValue, value);
}

static void
response_cb (GtkWidget *dialog, gint response, gpointer user_data)
{
	GladeXML *xml;
	GtkWidget *entry;
	const char *key;
	NMConnection *connection = NULL;
	NMSettingConnection *s_con;
	NMSettingWirelessSecurity *s_wireless_sec;
	const char *setting_name;
	GnomeKeyringAttributeList *attributes;
	char *name = NULL, *key_name = NULL;
	guint32 id;
	GHashTable *secrets;
	GValue *val;
	DBusGMethodInvocation *context;

	context = (DBusGMethodInvocation *) g_object_get_data (G_OBJECT (dialog), "context");

	connection = NM_CONNECTION (g_object_get_data (G_OBJECT (dialog), "connection"));
	if (response != GTK_RESPONSE_OK) {
		GError *error;
		error = nm_settings_new_error ("%s.%d: canceled", __FILE__, __LINE__);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		goto out;
	}

	xml = (GladeXML *) g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_return_if_fail (xml != NULL);

	setting_name = g_object_get_data (G_OBJECT (dialog), "setting-name");

	entry = glade_xml_get_widget (xml, "password_entry");
	key = gtk_entry_get_text (GTK_ENTRY (entry));
	if (!key)
		goto out;

	s_con = (NMSettingConnection *) nm_connection_get_setting (connection, "connection");
	s_wireless_sec = (NMSettingWirelessSecurity *) nm_connection_get_setting (connection, "802-11-wireless-security");
	if (!s_con || !s_wireless_sec || !s_wireless_sec->key_mgmt)
		goto out;

	/* Put the secret into the keyring */
	if (!strcmp (s_wireless_sec->key_mgmt, "none")) {
		key_name = "wep-key0";
	} else if (!strcmp (s_wireless_sec->key_mgmt, "wpa-none") || !strcmp (s_wireless_sec->key_mgmt, "wpa-psk")) {
		key_name = "psk";
		if (strlen (key) != 64) { /* convert passphrase */
			key = nma_wpa_passphrase_to_hex (key, connection);
			if (!key) {
				GError *error;
				error = nm_settings_new_error ("%s.%d: error hashing passphrase", __FILE__, __LINE__);
				dbus_g_method_return_error (context, error);
				g_error_free (error);
				goto out;
			}
		}
	}

	attributes = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (attributes, "connection-name", s_con->name);
	gnome_keyring_attribute_list_append_string (attributes, "setting-name", "802-11-wireless-security");
	if (key_name)
		gnome_keyring_attribute_list_append_string (attributes, "setting-key", key_name);

	g_assert (key_name);
	name = g_strdup_printf ("Network secret for %s/%s/%s", s_con->name, setting_name, key_name);
	gnome_keyring_item_create_sync (NULL, GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                                name, attributes, key, TRUE, &id);
	g_free (name);
	gnome_keyring_attribute_list_free (attributes);

	/* Send the secret back to NM */
	secrets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, destroy_gvalue);

	val = g_slice_new0 (GValue);
	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, key);

	g_hash_table_insert (secrets, g_strdup (key_name), val);
	dbus_g_method_return (context, secrets);
	g_hash_table_destroy (secrets);

out:
	if (connection)
		g_object_set_data (G_OBJECT (connection), "dialog", NULL);
	if (dialog) {
		gtk_widget_hide (dialog);
		gtk_widget_destroy (dialog);
	}
}

static void
clear_connection_dialog (gpointer data)
{
	GtkDialog *dialog = GTK_DIALOG (data);

	gtk_widget_hide (GTK_WIDGET (dialog));
	g_object_unref (dialog);
}

GtkWidget *
nma_password_dialog_new (NMConnection *connection,
                         const char *setting_name,
                         DBusGMethodInvocation *context)
{
	NMSettingWireless *s_wireless;
	char *glade_file;
	GladeXML *xml;
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWidget *label;
	GtkWidget *entry;
	GtkWidget *cb;
	const char *orig_label_text;
	char *new_label_text;
	char buf[33];
	int buf_len;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);
	g_return_val_if_fail (setting_name != NULL, NULL);
	g_return_val_if_fail (context != NULL, NULL);

	glade_file = g_build_filename (GLADEDIR, "applet.glade", NULL);
	xml = glade_xml_new (glade_file, "password_dialog", NULL);
	g_free (glade_file);
	if (!xml) {
		g_warning ("Couldn't find the glade file at %s.", glade_file);
		return NULL;
	}

	dialog = glade_xml_get_widget (xml, "password_dialog");
	gtk_widget_hide (dialog);

	g_object_set_data_full (G_OBJECT (dialog), "glade-xml",
	                        xml,
	                        (GDestroyNotify) g_object_unref);

	/* Dialog only lives as long as the connection does */
	g_object_set_data (G_OBJECT (dialog), "connection", connection);
	g_object_set_data_full (G_OBJECT (connection), "dialog",
	                        GTK_WIDGET (g_object_ref (dialog)),
	                        clear_connection_dialog);

	g_object_set_data_full (G_OBJECT (dialog), "setting-name",
	                        g_strdup (setting_name),
	                        (GDestroyNotify) g_free);

	g_object_set_data (G_OBJECT (dialog), "context", context);

	button = glade_xml_get_widget (xml, "connect_button");
	gtk_widget_grab_default (button);
	gtk_widget_set_sensitive (button, FALSE);

	entry = glade_xml_get_widget (xml, "password_entry");
	g_signal_connect (entry, "changed", GTK_SIGNAL_FUNC (entry_changed_cb), dialog);

	cb = glade_xml_get_widget (xml, "showpass_checkbutton");
	g_signal_connect (cb, "toggled", GTK_SIGNAL_FUNC (showpasscb_toggled_cb), entry);

	/* Insert the network name into the dialog text */
	label = glade_xml_get_widget (xml, "label1");
	orig_label_text = gtk_label_get_label (GTK_LABEL (label));

	s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, "802-11-wireless");
	memset (buf, 0, sizeof (buf));
	buf_len = MIN (s_wireless->ssid->len, sizeof (buf) - 1);
	memcpy (buf, s_wireless->ssid->data, buf_len);
	new_label_text = g_strdup_printf (orig_label_text, nm_utils_ssid_to_utf8 (buf, buf_len));
	gtk_label_set_label (GTK_LABEL (label), new_label_text);
	g_free (new_label_text);

	g_signal_connect (dialog, "response", GTK_SIGNAL_FUNC (response_cb), dialog);

	return dialog;
}

