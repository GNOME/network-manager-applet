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
#include <nm-setting-8021x.h>

#include "eap-method.h"
#include "wireless-security.h"
#include "gconf-helpers.h"

static void
show_toggled_cb (GtkCheckButton *button, EAPMethod *method)
{
	GtkWidget *widget;
	gboolean visible;

	widget = glade_xml_get_widget (method->xml, "eap_simple_password_entry");
	g_assert (widget);

	visible = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
}

static void
destroy (EAPMethod *parent)
{
	EAPMethodSimple *method = (EAPMethodSimple *) parent;

	g_slice_free (EAPMethodSimple, method);
}

static gboolean
validate (EAPMethod *parent)
{
	GtkWidget *widget;
	const char *text;

	widget = glade_xml_get_widget (parent->xml, "eap_simple_username_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!text || !strlen (text))
		return FALSE;

	widget = glade_xml_get_widget (parent->xml, "eap_simple_password_entry");
	g_assert (widget);
	text = gtk_entry_get_text (GTK_ENTRY (widget));
	if (!text || !strlen (text))
		return FALSE;

	return TRUE;
}

static void
add_to_size_group (EAPMethod *parent, GtkSizeGroup *group)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (parent->xml, "eap_simple_username_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);

	widget = glade_xml_get_widget (parent->xml, "eap_simple_password_label");
	g_assert (widget);
	gtk_size_group_add_widget (group, widget);
}

static void
fill_connection (EAPMethod *parent, NMConnection *connection)
{
	EAPMethodSimple *method = (EAPMethodSimple *) parent;
	NMSetting8021x *s_8021x;
	GtkWidget *widget;

	s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
	g_assert (s_8021x);

	switch (method->type) {
		case EAP_METHOD_SIMPLE_TYPE_PAP:
			s_8021x->phase2_auth = g_strdup ("pap");
			break;
		case EAP_METHOD_SIMPLE_TYPE_MSCHAP:
			s_8021x->phase2_auth = g_strdup ("mschap");
			break;
		case EAP_METHOD_SIMPLE_TYPE_MSCHAP_V2:
			s_8021x->phase2_auth = g_strdup ("mschapv2");
			break;
		case EAP_METHOD_SIMPLE_TYPE_MD5:
			s_8021x->phase2_auth = g_strdup ("md5");
			break;
		case EAP_METHOD_SIMPLE_TYPE_CHAP:
			s_8021x->phase2_auth = g_strdup ("chap");
			break;
		default:
			g_assert_not_reached ();
			break;
	}

	widget = glade_xml_get_widget (parent->xml, "eap_simple_username_entry");
	g_assert (widget);
	s_8021x->identity = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));

	widget = glade_xml_get_widget (parent->xml, "eap_simple_password_entry");
	g_assert (widget);
	s_8021x->password = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));
}

EAPMethodSimple *
eap_method_simple_new (const char *glade_file,
                       WirelessSecurity *parent,
                       NMConnection *connection,
                       const char *connection_id,
                       EAPMethodSimpleType type)
{
	EAPMethodSimple *method;
	GtkWidget *widget;
	GladeXML *xml;

	g_return_val_if_fail (glade_file != NULL, NULL);
	if (connection)
		g_return_val_if_fail (connection_id != NULL, NULL);

	xml = glade_xml_new (glade_file, "eap_simple_notebook", NULL);
	if (xml == NULL) {
		g_warning ("Couldn't get eap_simple_widget from glade xml");
		return NULL;
	}

	widget = glade_xml_get_widget (xml, "eap_simple_notebook");
	g_assert (widget);
	g_object_ref_sink (widget);

	method = g_slice_new0 (EAPMethodSimple);
	if (!method) {
		g_object_unref (xml);
		g_object_unref (widget);
		return NULL;
	}

	eap_method_init (EAP_METHOD (method),
	                 validate,
	                 add_to_size_group,
	                 fill_connection,
	                 destroy,
	                 xml,
	                 widget);

	method->type = type;

	widget = glade_xml_get_widget (xml, "eap_simple_username_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
	if (connection) {
		NMSetting8021x *s_8021x;

		s_8021x = NM_SETTING_802_1X (nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X));
		if (s_8021x && s_8021x->identity)
			gtk_entry_set_text (GTK_ENTRY (widget), s_8021x->identity);
	}

	widget = glade_xml_get_widget (xml, "eap_simple_password_entry");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
	                  (GCallback) wireless_security_changed_cb,
	                  parent);
	/* Fill secrets, if any */
	if (connection) {
		GHashTable *secrets;
		GError *error = NULL;
		GValue *value;

		secrets = nm_gconf_get_keyring_items (connection, connection_id, NM_SETTING_802_1X_SETTING_NAME, &error);
		if (secrets) {
			value = g_hash_table_lookup (secrets, NM_SETTING_802_1X_PASSWORD);
			if (value)
				gtk_entry_set_text (GTK_ENTRY (widget), g_value_get_string (value));
			g_hash_table_destroy (secrets);
		}
	}

	widget = glade_xml_get_widget (xml, "show_checkbutton");
	g_assert (widget);
	g_signal_connect (G_OBJECT (widget), "toggled",
	                  (GCallback) show_toggled_cb,
	                  method);

	return method;
}

