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
destroy (WirelessSecurity *parent)
{
	WirelessSecurityWEPKey *sec = (WirelessSecurityWEPKey *) parent;

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
}

static void
fill_connection (WirelessSecurity *parent, NMConnection *connection)
{
	WirelessSecurityWEPKey *sec = (WirelessSecurityWEPKey *) parent;
	GtkWidget *widget;
	gint auth_alg;
	const char *key;

	widget = glade_xml_get_widget (parent->xml, "auth_method_combo");
	auth_alg = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));

	widget = glade_xml_get_widget (parent->xml, "wep_key_entry");
	key = gtk_entry_get_text (GTK_ENTRY (widget));

	if (sec->type == WEP_KEY_TYPE_HEX) {
		ws_wep_fill_connection (connection, key, auth_alg);
	} else {
		char *hashed;

		hashed = utils_bin2hexstr (key, strlen (key), strlen (key) * 2);
		ws_wep_fill_connection (connection, hashed, auth_alg);
		g_free (hashed);
	}
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
                WEPKeyType type)
{
	WirelessSecurityWEPKey *sec;
	GtkWidget *widget;
	GladeXML *xml;

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
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  sec);
	g_signal_connect (G_OBJECT (widget), "insert-text",
	                  (GCallback) wep_entry_filter_cb,
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

