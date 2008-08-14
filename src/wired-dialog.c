/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <nm-setting-connection.h>
#include <nm-setting-8021x.h>
#include <nm-setting-wireless.h>
#include <nm-utils.h>
#include "wired-dialog.h"
#include "wireless-security.h"
#include "applet-dialogs.h"
#include "gconf-helpers.h"

static void
stuff_changed_cb (WirelessSecurity *sec, gpointer user_data)
{
	GtkWidget *button = GTK_WIDGET (user_data);
	
	gtk_widget_set_sensitive (button, wireless_security_validate (sec, NULL));
}

static void
dialog_set_network_name (NMConnection *connection, GtkEntry *entry)
{
	NMSettingConnection *setting;

	setting = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));

	gtk_widget_set_sensitive (GTK_WIDGET (entry), FALSE);
	gtk_entry_set_text (entry, setting->id);
}

static WirelessSecurity *
dialog_set_security (NMConnection *connection,
					 const char *glade_file,
					 GtkBox *box)
{
	GList *children;
	GList *iter;
	WirelessSecurity *security;
	const char *id;

	id = g_object_get_data (G_OBJECT (connection), NMA_CONNECTION_ID_TAG);
	security = (WirelessSecurity *) ws_wpa_eap_new (glade_file, connection, id);

	/* Remove any previous wireless security widgets */
	children = gtk_container_get_children (GTK_CONTAINER (box));
	for (iter = children; iter; iter = iter->next)
		gtk_container_remove (GTK_CONTAINER (box), GTK_WIDGET (iter->data));

	gtk_box_pack_start (box, wireless_security_get_widget (security), TRUE, TRUE, 0);

	return security;
}

static gboolean
dialog_init (GtkWidget *dialog,
			 GladeXML *xml,
			 NMClient *nm_client,
			 const char *glade_file,
			 NMConnection *connection)
{
	WirelessSecurity *security;

	/* Hide bunch of wireless specific widgets */
	gtk_widget_hide (glade_xml_get_widget (xml, "device_label"));
	gtk_widget_hide (glade_xml_get_widget (xml, "device_combo"));
	gtk_widget_hide (glade_xml_get_widget (xml, "security_combo_label"));
	gtk_widget_hide (glade_xml_get_widget (xml, "security_combo"));

	gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget (xml, "wireless_dialog")),
						  _("Wired 802.1X authentication"));

	dialog_set_network_name (connection, GTK_ENTRY (glade_xml_get_widget (xml, "network_name_entry")));
	security = dialog_set_security (connection, glade_file, GTK_BOX (glade_xml_get_widget (xml, "security_vbox")));
	wireless_security_set_changed_notify (security, stuff_changed_cb, glade_xml_get_widget (xml, "ok_button"));

	g_object_set_data_full (G_OBJECT (dialog),
							"security", security,
							(GDestroyNotify) wireless_security_unref);

	return TRUE;
}

GtkWidget *
nma_wired_dialog_new (const char *glade_file,
					  NMClient *nm_client,
					  NMConnection *connection,
					  NMDevice *device)
{
	GladeXML *xml;
	GtkWidget *dialog;
	gboolean success;

	xml = glade_xml_new (glade_file, "wireless_dialog", NULL);
	if (!xml) {
		applet_warning_dialog_show (_("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		return NULL;
	}

	dialog = glade_xml_get_widget (xml, "wireless_dialog");
	if (!dialog) {
		nm_warning ("Couldn't find glade wireless_dialog widget.");
		g_object_unref (xml);
		return NULL;
	}

	success = dialog_init (dialog, xml, nm_client, glade_file, connection);
	if (!success) {
		nm_warning ("Couldn't create wired security dialog.");
		gtk_widget_destroy (dialog);
		return NULL;
	}

	g_object_set_data_full (G_OBJECT (dialog),
							"connection", g_object_ref (connection),
							(GDestroyNotify) g_object_unref);

	return dialog;
}
					  
NMConnection *
nma_wired_dialog_get_connection (GtkWidget *dialog)
{
	NMConnection *connection;
	WirelessSecurity *security;
	NMConnection *tmp_connection;
	NMSetting *s_8021x;

	g_return_val_if_fail (dialog != NULL, NULL);

	connection = g_object_get_data (G_OBJECT (dialog), "connection");
	security = g_object_get_data (G_OBJECT (dialog), "security");

	/* Here's a nice hack to work around the fact that ws_802_1x_fill_connection needs wireless setting. */
	tmp_connection = nm_connection_new ();
	nm_connection_add_setting (tmp_connection, nm_setting_wireless_new ());
	ws_802_1x_fill_connection (security, "wpa_eap_auth_combo", tmp_connection);

	s_8021x = nm_connection_get_setting (tmp_connection, NM_TYPE_SETTING_802_1X);
	nm_connection_add_setting (connection, NM_SETTING (g_object_ref (s_8021x)));

	g_object_unref (tmp_connection);

	return connection;
}
