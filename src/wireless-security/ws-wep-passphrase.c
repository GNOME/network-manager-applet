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

#include <glade/glade.h>
#include <ctype.h>
#include <string.h>

#include "wireless-security.h"
#include "utils.h"
#include "gnome-keyring-md5.h"


static void
show_toggled_cb (GtkCheckButton *button, WirelessSecurity *sec)
{
	GtkWidget *widget;
	gboolean visible;

	widget = glade_xml_get_widget (sec->xml, "wep_passphrase_entry");
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static void
destroy (WirelessSecurity *parent)
{
	WirelessSecurityWEPPassphrase *sec = (WirelessSecurityWEPPassphrase *) parent;

	g_object_unref (parent->xml);
	g_slice_free (WirelessSecurityWEPPassphrase, sec);
}

static gboolean
validate (WirelessSecurity *parent, const GByteArray *ssid)
{
	GtkWidget *entry;
	const char *key;

	entry = glade_xml_get_widget (parent->xml, "wep_passphrase_entry");
	g_assert (entry);

	key = gtk_entry_get_text (GTK_ENTRY (entry));
	return (key && ((strlen (key) > 0) && (strlen (key) < 65)));
}

static void
add_to_size_group (WirelessSecurity *parent, GtkSizeGroup *group)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "auth_method_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "wep_passphrase_label");
	g_assert (widget);
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
	GtkWidget *widget;
	gint auth_alg;
	const char *key;
	char *hashed;

	widget = glade_xml_get_widget (parent->xml, "auth_method_combo");
	g_assert (widget);
	auth_alg = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));

	widget = glade_xml_get_widget (parent->xml, "wep_passphrase_entry");
	g_assert (widget);
	key = gtk_entry_get_text (GTK_ENTRY (widget));

	hashed = wep128_passphrase_hash (key);
	ws_wep_fill_connection (connection, hashed, auth_alg);
	g_free (hashed);
}

WirelessSecurityWEPPassphrase *
ws_wep_passphrase_new (const char *glade_file)
{
	WirelessSecurityWEPPassphrase *sec;
	GtkWidget *widget;
	GladeXML *xml;

	g_return_val_if_fail (glade_file != NULL, NULL);

	xml = glade_xml_new (glade_file, "wep_passphrase_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get wep_passphrase_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "wep_passphrase_notebook");
	g_assert (widget);

	sec = g_slice_new0 (WirelessSecurityWEPPassphrase);
	if (!sec) {
		g_object_unref (xml);
		return NULL;
	}

	wireless_security_init (WIRELESS_SECURITY (sec),
	                        validate,
	                        add_to_size_group,
	                        fill_connection,
	                        destroy,
	                        xml,
	                        g_object_ref (widget));

	widget = glade_xml_get_widget (xml, "wep_passphrase_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  sec);

	widget = glade_xml_get_widget (xml, "auth_method_combo");
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);

	return sec;
}

