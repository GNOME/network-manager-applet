/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <config.h>
#include <string.h>
#include <glade/glade.h>
#include <glib/gi18n.h>

#include "nm-wired-dialog.h"
#include "applet-dbus-info.h"
#include "applet-dbus-devices.h"
#include "nm-utils.h"
#include "NetworkManager.h"
#include "wso-wpa-eap.h"
#include "nm-gconf-wso-wpa-eap.h"


#define WIRED_DIALOG_INFO_TAG "wired-dialog-info-tag"

typedef struct {
	NMApplet *applet;
	GladeXML *xml;
	WirelessSecurityOption *opt;

	char *network_id;
	DBusMessage *message;
} WiredDialogInfo;

static void
wired_dialog_info_destroy (gpointer data)
{
	WiredDialogInfo *info = (WiredDialogInfo *) data;

	g_object_unref (info->xml);
	wso_free (info->opt);
	g_free (info->network_id);

	if (info->message)
		dbus_message_unref (info->message);

	g_free (data);
}

static void
wired_dialog_destroyed (gpointer data, GObject *destroyed_object)
{
	NMApplet *applet = (NMApplet *) data;

	applet->passphrase_dialog = NULL;
}

static void
create_response (GtkDialog *dialog, gint response, gpointer data)
{
	WiredDialogInfo *info;

	info = (WiredDialogInfo *) g_object_get_data (G_OBJECT (dialog), WIRED_DIALOG_INFO_TAG);
	g_assert (info);

	if (response == GTK_RESPONSE_OK) {
		const char *network_id;
		GSList *iter;
		NetworkDevice *dev = NULL;

		for (iter = info->applet->device_list; iter; iter = iter->next) {
			dev = (NetworkDevice *) iter->data;
			if (network_device_is_wired (dev) && network_device_get_link (dev))
				break;

			dev = NULL;
		}

		g_assert (dev);

		network_id = gtk_entry_get_text (GTK_ENTRY (glade_xml_get_widget (info->xml, "network_name_entry")));
		nma_dbus_set_device (info->applet->connection, dev, network_id, info->opt);
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
ask_password_response (GtkDialog *dialog, gint response, gpointer data)
{
	WiredDialogInfo *info;
	NMGConfWSO *gconf_wso;

	info = (WiredDialogInfo *) g_object_get_data (G_OBJECT (dialog), WIRED_DIALOG_INFO_TAG);
	g_assert (info);

	if (response != GTK_RESPONSE_OK) {
		DBusMessage *reply;

		reply = dbus_message_new_error (info->message, NMI_DBUS_USER_KEY_CANCELED_ERROR, "Request was cancelled.");
		dbus_connection_send (info->applet->connection, reply, NULL);
		goto out;
	}

	gconf_wso = nm_gconf_wso_new_from_wso (info->opt, info->network_id);

	/* Return new security information to NM */
	nmi_dbus_return_user_key (info->applet->connection, info->message, gconf_wso);
	g_object_unref (gconf_wso);

out:
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
wired_dialog_modified (GtkWidget *widget, gpointer user_data)
{
	WiredDialogInfo *info = (WiredDialogInfo *) user_data;
	const char *network_id;
	gboolean enabled = FALSE;

	network_id = gtk_entry_get_text (GTK_ENTRY (glade_xml_get_widget (info->xml, "network_name_entry")));
	if (network_id && strlen (network_id) > 0)
		enabled = wso_validate_input (info->opt, network_id, NULL);

	gtk_widget_set_sensitive (glade_xml_get_widget (info->xml, "ok_button"), enabled);
}

static GtkDialog *
wired_dialog_init (NMApplet *applet, const char *network_id, DBusMessage *message)
{
	GladeXML *xml;
	GtkDialog *dialog;
	GtkWidget *w;
	char *label;
	GtkWidget *wso_widget;
	WiredDialogInfo *info;

	xml = glade_xml_new (applet->glade_file, "other_network_dialog", NULL);
	if (!xml) {
		nma_schedule_warning_dialog (applet, _("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		return NULL;
	}

	dialog = GTK_DIALOG (glade_xml_get_widget (xml, "other_network_dialog"));
	if (!dialog) {
		g_object_unref (xml);
		return NULL;
	}

	gtk_window_set_title (GTK_WINDOW (dialog), _("Connect to 802.1X protected wired network"));
	gtk_widget_hide (glade_xml_get_widget (xml, "security_combo_label"));
	gtk_widget_hide (glade_xml_get_widget (xml, "security_combo"));

	/* FIXME: For now, hide the device selector and use the first wired device */
	gtk_widget_hide (glade_xml_get_widget (xml, "wireless_adapter_label"));
	gtk_widget_hide (glade_xml_get_widget (xml, "wireless_adapter_combo"));

	if (network_id) {
		label = g_strdup_printf (_("<span size=\"larger\" weight=\"bold\">Connect to a 802.1X protected wired network</span>\n\n"
							  "A passphrase or encryption key is required to access the network '%s'."), network_id);
		gtk_label_set_markup (GTK_LABEL (glade_xml_get_widget (xml, "caption_label")), label);
		g_free (label);
	} else {
		label = _("<span size=\"larger\" weight=\"bold\">Connect to a 802.1X protected wired network</span>\n\n"
				"Enter the name and security settings of the network you wish to join.");
		gtk_label_set_markup (GTK_LABEL (glade_xml_get_widget (xml, "caption_label")), label);
	}

	info = g_new (WiredDialogInfo, 1);
	info->applet = applet;
	info->xml = xml;
	info->opt = wso_wpa_eap_new (applet->glade_file, NM_802_11_CAP_KEY_MGMT_802_1X, FALSE);
	info->network_id = network_id ? g_strdup (network_id) : NULL;
	info->message = message ? dbus_message_ref (message) : NULL;

	g_object_set_data_full (G_OBJECT (dialog), WIRED_DIALOG_INFO_TAG,
					    info, wired_dialog_info_destroy);

	wso_widget = wso_get_widget (info->opt, GTK_SIGNAL_FUNC (wired_dialog_modified), info);
	if (wso_widget) {
		gtk_container_add (GTK_CONTAINER (glade_xml_get_widget (xml, "wireless_security_vbox")), wso_widget);
		wso_wpa_eap_set_wired (info->opt);
	}

	w = glade_xml_get_widget (xml, "ok_button");
	gtk_widget_grab_default (w);
	gtk_widget_set_sensitive (w, FALSE);

#if GTK_CHECK_VERSION(2,6,0)
	{
		GtkWidget *connect_image = gtk_image_new_from_stock (GTK_STOCK_CONNECT, GTK_ICON_SIZE_BUTTON);
		gtk_button_set_image (GTK_BUTTON (w), connect_image);
	}
#endif

	w = glade_xml_get_widget (xml, "network_name_entry");
	if (network_id)
		gtk_entry_set_text (GTK_ENTRY (w), network_id);

	gtk_widget_grab_focus (w);
	g_signal_connect (w, "changed", G_CALLBACK (wired_dialog_modified), info);

	return dialog;
}

void
nma_wired_dialog_create (NMApplet *applet)
{
	GtkWidget *dialog;

	g_return_if_fail (applet != NULL);

	dialog = GTK_WIDGET (wired_dialog_init (applet, NULL, NULL));
	if (!dialog)
		return;

	g_signal_connect (dialog, "response", G_CALLBACK (create_response), NULL);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (GTK_WIDGET (dialog));
	gdk_x11_window_set_user_time (dialog->window, gdk_x11_get_server_time (dialog->window));
	gtk_window_present (GTK_WINDOW (dialog));
}

static void
populate_dialog (GtkWidget *dialog, GConfClient *gconf_client, const char *network_id)
{
	char *escaped_network;
	NMGConfWSO *gconf_wso;

	if (!network_id)
		return;

	escaped_network = gconf_escape_key (network_id, strlen (network_id));
	gconf_wso = nm_gconf_wso_new_deserialize_gconf (gconf_client, NETWORK_TYPE_WIRED, escaped_network);
	g_free (escaped_network);

	if (gconf_wso && NM_IS_GCONF_WSO_WPA_EAP (gconf_wso)) {
		WiredDialogInfo *info;

		info = g_object_get_data (G_OBJECT (dialog), WIRED_DIALOG_INFO_TAG);
		nm_gconf_wso_populate_wso (gconf_wso, info->opt);
	}
}

void
nma_wired_dialog_ask_password (NMApplet *applet,
						 const char *network_id,
						 DBusMessage *message)
{
	GtkWidget *dialog;

	g_return_if_fail (applet != NULL);

	if (applet->passphrase_dialog)
		gtk_widget_destroy (applet->passphrase_dialog);

	dialog = GTK_WIDGET (wired_dialog_init (applet, network_id, message));
	if (!dialog)
		return;

	applet->passphrase_dialog = dialog;
	g_object_weak_ref (G_OBJECT (dialog), wired_dialog_destroyed, applet);

	g_signal_connect (dialog, "response", G_CALLBACK (ask_password_response), NULL);

	populate_dialog (dialog, applet->gconf_client, network_id);

	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (dialog);
	gdk_x11_window_set_user_time (dialog->window, gdk_x11_get_server_time (dialog->window));
	gtk_window_present (GTK_WINDOW (dialog));
}

/*****************************************************************************/

GSList *
nma_wired_read_networks (GConfClient *gconf_client)
{
	g_return_val_if_fail (gconf_client != NULL, NULL);

	GSList *networks = NULL;
	GSList *dir_list;
	GSList *iter;

	dir_list = gconf_client_all_dirs (gconf_client, GCONF_PATH_WIRED_NETWORKS, NULL);

	for (iter = dir_list; iter; iter = iter->next) {
		char *dir = (char *) iter->data;
		char key[100];
		GConfValue *value;

		g_snprintf (&key[0], 99, "%s/essid", dir);
		if ((value = gconf_client_get (gconf_client, key, NULL))) {
			if (value->type == GCONF_VALUE_STRING)
				networks = g_slist_prepend (networks, g_strdup (gconf_value_get_string (value)));

			gconf_value_free (value);
		}
		g_free (dir);
	}

	g_slist_free (dir_list);

	networks = g_slist_sort (networks, (GCompareFunc) strcmp);

	return networks;
}

/*****************************************************************************/

typedef struct {
	NMApplet *applet;
	NetworkDevice *device;
	char *network_id;
	NMGConfWSO *opt;
} WiredMenuItemInfo;

static void
wired_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	WiredMenuItemInfo *info = (WiredMenuItemInfo *) user_data;

	g_print ("Activate!\n");
	nma_dbus_set_device_with_gconf_wso (info->applet->connection, info->device, info->network_id, info->opt);
}

static void
wired_menu_item_info_destroy (gpointer data, GObject *destroyed_object)
{
	WiredMenuItemInfo *info = (WiredMenuItemInfo *) data;

	g_free (info->network_id);
	g_object_unref (info->opt);
	g_free (info);
}

static GtkWidget *
wired_menu_item_get_image ()
{
	GtkWidget *image;
	GtkIconTheme *icon_theme;
	GdkPixbuf *pixbuf = NULL;

	icon_theme = gtk_icon_theme_get_default ();

	if (gtk_icon_theme_has_icon (icon_theme, "network-wireless-encrypted"))
		pixbuf = gtk_icon_theme_load_icon (icon_theme, "network-wireless-encrypted", GTK_ICON_SIZE_MENU, 0, NULL);
	if (!pixbuf)
		pixbuf = gtk_icon_theme_load_icon (icon_theme, "gnome-lockscreen", GTK_ICON_SIZE_MENU, 0, NULL);

	image = gtk_image_new_from_pixbuf (pixbuf);
	g_object_unref (pixbuf);

	return image;
}

/* Takes over the ownership of network_id and wireless option. */
GtkWidget *
nma_wired_menu_item_new (NMApplet *applet,
					NetworkDevice *device,
					char *network_id,
					NMGConfWSO *opt)
{
	GtkWidget *menu_item;
	GtkWidget *hbox;
	GtkWidget *w;
	WiredMenuItemInfo *info;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (device != NULL, NULL);
	g_return_val_if_fail (network_id != NULL, NULL);
	g_return_val_if_fail (opt != NULL, NULL);

	menu_item = gtk_check_menu_item_new ();
	gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (menu_item), TRUE);

	hbox = gtk_hbox_new (FALSE, 6);
	gtk_container_add (GTK_CONTAINER (menu_item), hbox);

	w = gtk_label_new (network_id);
	gtk_misc_set_alignment (GTK_MISC (w), 0.0, 0.5);
	gtk_box_pack_start_defaults (GTK_BOX (hbox), w);

	w = wired_menu_item_get_image ();
	gtk_box_pack_end (GTK_BOX (hbox), w, FALSE, FALSE, 0);

	if (network_device_get_active (device)) {
		const char *active_network_id = network_device_get_active_wired_network (device);

		if (active_network_id && !strcmp (active_network_id, network_id))
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);
	}

	info = g_new (WiredMenuItemInfo, 1);
	info->applet = applet;
	info->device = device;
	info->network_id = network_id;
	info->opt = opt;

	g_object_weak_ref (G_OBJECT (menu_item), wired_menu_item_info_destroy, info);
	g_signal_connect (menu_item, "activate", G_CALLBACK (wired_menu_item_activate), info);

	return menu_item;
}
