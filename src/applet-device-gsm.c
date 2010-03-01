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
 * (C) Copyright 2008 - 2009 Red Hat, Inc.
 * (C) Copyright 2008 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <nm-device.h>
#include <nm-setting-connection.h>
#include <nm-setting-gsm.h>
#include <nm-setting-serial.h>
#include <nm-setting-ppp.h>
#include <nm-gsm-device.h>
#include <nm-utils.h>

#include "applet.h"
#include "applet-device-gsm.h"
#include "utils.h"
#include "mobile-wizard.h"
#include "applet-dialogs.h"
#include "mb-menu-item.h"
#include "nma-marshal.h"

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMConnection *connection;
} GSMMenuItemInfo;

static void
gsm_menu_item_info_destroy (gpointer data)
{
	g_slice_free (GSMMenuItemInfo, data);
}

typedef struct {
	AppletNewAutoConnectionCallback callback;
	gpointer callback_data;
} AutoGsmWizardInfo;

static void
mobile_wizard_done (MobileWizard *wizard,
                    gboolean canceled,
                    MobileWizardAccessMethod *method,
                    gpointer user_data)
{
	AutoGsmWizardInfo *info = user_data;
	NMConnection *connection = NULL;

	if (!canceled && method) {
		NMSetting *setting;
		char *uuid, *id;

		if (method->devtype != NM_DEVICE_TYPE_GSM) {
			g_warning ("Unexpected device type (not GSM).");
			canceled = TRUE;
			goto done;
		}

		connection = nm_connection_new ();

		setting = nm_setting_gsm_new ();
		g_object_set (setting,
		              NM_SETTING_GSM_NUMBER, "*99#",
		              NM_SETTING_GSM_USERNAME, method->username,
		              NM_SETTING_GSM_PASSWORD, method->password,
		              NM_SETTING_GSM_APN, method->gsm_apn,
		              NULL);
		nm_connection_add_setting (connection, setting);

		/* Serial setting */
		setting = nm_setting_serial_new ();
		g_object_set (setting,
		              NM_SETTING_SERIAL_BAUD, 115200,
		              NM_SETTING_SERIAL_BITS, 8,
		              NM_SETTING_SERIAL_PARITY, 'n',
		              NM_SETTING_SERIAL_STOPBITS, 1,
		              NULL);
		nm_connection_add_setting (connection, setting);

		nm_connection_add_setting (connection, nm_setting_ppp_new ());

		setting = nm_setting_connection_new ();
		if (method->plan_name)
			id = g_strdup_printf ("%s %s", method->provider_name, method->plan_name);
		else
			id = g_strdup_printf ("%s connection", method->provider_name);
		uuid = nm_utils_uuid_generate ();
		g_object_set (setting,
		              NM_SETTING_CONNECTION_ID, id,
		              NM_SETTING_CONNECTION_TYPE, NM_SETTING_GSM_SETTING_NAME,
		              NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
		              NM_SETTING_CONNECTION_UUID, uuid,
		              NULL);
		g_free (uuid);
		g_free (id);
		nm_connection_add_setting (connection, setting);
	}

done:
	(*(info->callback)) (connection, TRUE, canceled, info->callback_data);

	if (wizard)
		mobile_wizard_destroy (wizard);
	g_free (info);
}

static gboolean
gsm_new_auto_connection (NMDevice *device,
                         gpointer dclass_data,
                         AppletNewAutoConnectionCallback callback,
                         gpointer callback_data)
{
	MobileWizard *wizard;
	AutoGsmWizardInfo *info;
	MobileWizardAccessMethod *method;

	info = g_malloc0 (sizeof (AutoGsmWizardInfo));
	info->callback = callback;
	info->callback_data = callback_data;

	wizard = mobile_wizard_new (NULL, NULL, NM_DEVICE_TYPE_GSM, FALSE,
	                            mobile_wizard_done, info);
	if (wizard) {
		mobile_wizard_present (wizard);
		return TRUE;
	}

	/* Fall back to something */
	method = g_malloc0 (sizeof (MobileWizardAccessMethod));
	method->devtype = NM_DEVICE_TYPE_GSM;
	method->provider_name = _("GSM");
	mobile_wizard_done (NULL, FALSE, method, info);
	g_free (method);

	return TRUE;
}

static void
gsm_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	GSMMenuItemInfo *info = (GSMMenuItemInfo *) user_data;

	applet_menu_item_activate_helper (info->device,
	                                  info->connection,
	                                  "/",
	                                  info->applet,
	                                  user_data);
}

static void
add_connection_item (NMDevice *device,
                     NMConnection *connection,
                     GtkWidget *item,
                     GtkWidget *menu,
                     NMApplet *applet)
{
	GSMMenuItemInfo *info;

	info = g_slice_new0 (GSMMenuItemInfo);
	info->applet = applet;
	info->device = g_object_ref (G_OBJECT (device));
	info->connection = connection ? g_object_ref (connection) : NULL;

	g_signal_connect_data (item, "activate",
	                       G_CALLBACK (gsm_menu_item_activate),
	                       info,
	                       (GClosureNotify) gsm_menu_item_info_destroy, 0);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

typedef struct {
	DBusGProxy *props_proxy;
	DBusGProxy *card_proxy;
	DBusGProxy *net_proxy;

	gboolean quality_valid;
	guint32 quality;

	char *unlock_required;

	/* reg_state is (1 + MM reg state) so that 0 means we haven't gotten a
	 * value from MM yet.  0 is a valid MM GSM reg state.
	 */
	guint reg_state;
	char *op_code;
	char *op_name;

	gboolean nopoll;
	guint32 poll_id;
} GsmDeviceInfo;

static guint32
state_for_info (GsmDeviceInfo *info)
{
	switch (info->reg_state) {
	case 1:  /* IDLE */
		return MB_STATE_IDLE;
	case 2:  /* HOME */
		return MB_STATE_HOME;
	case 3:  /* SEARCHING */
		return MB_STATE_SEARCHING;
	case 4:  /* DENIED */
		return MB_STATE_DENIED;
	case 6:  /* ROAMING */
		return MB_STATE_ROAMING;
	case 5:  /* UNKNOWN */
	default:
		break;
	}

	return MB_STATE_UNKNOWN;
}

static void
gsm_add_menu_item (NMDevice *device,
                   guint32 n_devices,
                   NMConnection *active,
                   GtkWidget *menu,
                   NMApplet *applet)
{
	GsmDeviceInfo *info;
	char *text;
	GtkWidget *item;
	GSList *connections, *all, *iter;

	info = g_object_get_data (G_OBJECT (device), "devinfo");

	all = applet_get_all_connections (applet);
	connections = utils_filter_connections_for_device (device, all);
	g_slist_free (all);

	if (n_devices > 1) {
		char *desc;

		desc = (char *) utils_get_device_description (device);
		if (!desc)
			desc = (char *) nm_device_get_iface (device);
		g_assert (desc);

		text = g_strdup_printf (_("Mobile Broadband (%s)"), desc);
	} else {
		text = g_strdup (_("Mobile Broadband"));
	}

	item = applet_menu_item_create_device_item_helper (device, applet, text);
	gtk_widget_set_sensitive (item, FALSE);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
	g_free (text);

	/* Add the active connection */
	if (active) {
		NMSettingConnection *s_con;
		guint32 mb_state;

		s_con = (NMSettingConnection *) nm_connection_get_setting (active, NM_TYPE_SETTING_CONNECTION);
		g_assert (s_con);

		mb_state = state_for_info (info);

		item = nm_mb_menu_item_new (nm_setting_connection_get_id (s_con),
		                            info->quality_valid ? info->quality : 0,
		                            info->op_name,
		                            MB_TECH_GSM,
		                            mb_state,
		                            applet);

		add_connection_item (device, active, item, menu, applet);
	}

	/* Notify user of unmanaged or unavailable device */
	if (nm_device_get_state (device) > NM_DEVICE_STATE_DISCONNECTED) {
		item = nma_menu_device_get_menu_item (device, applet, NULL);
		if (item) {
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);
		}
	} else {
		guint32 mb_state;

		mb_state = state_for_info (info);
		item = nm_mb_menu_item_new (NULL,
		                            info->quality_valid ? info->quality : 0,
		                            info->op_name,
		                            MB_TECH_GSM,
		                            mb_state,
		                            applet);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	/* Add the default / inactive connection items */
	if (!nma_menu_device_check_unusable (device)) {
		if ((!active && g_slist_length (connections)) || (active && g_slist_length (connections) > 1))
			applet_menu_item_add_complex_separator_helper (menu, applet, _("Available"), -1);

		if (g_slist_length (connections)) {
			for (iter = connections; iter; iter = g_slist_next (iter)) {
				NMConnection *connection = NM_CONNECTION (iter->data);

				if (connection != active) {
					item = applet_new_menu_item_helper (connection, NULL, FALSE);
					add_connection_item (device, connection, item, menu, applet);
				}
			}
		} else {
			/* Default connection item */
			item = gtk_check_menu_item_new_with_label (_("New Mobile Broadband (GSM) connection..."));
			add_connection_item (device, NULL, item, menu, applet);
		}
	}

	g_slist_free (connections);
}

static void
gsm_device_state_changed (NMDevice *device,
                          NMDeviceState new_state,
                          NMDeviceState old_state,
                          NMDeviceStateReason reason,
                          NMApplet *applet)
{
	if (new_state == NM_DEVICE_STATE_ACTIVATED) {
		NMConnection *connection;
		NMSettingConnection *s_con = NULL;
		char *str = NULL;

		connection = applet_find_active_connection_for_device (device, applet, NULL);
		if (connection) {
			const char *id;

			s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
			id = s_con ? nm_setting_connection_get_id (s_con) : NULL;
			if (id)
				str = g_strdup_printf (_("You are now connected to '%s'."), id);
		}

		applet_do_notify_with_pref (applet,
		                            _("Connection Established"),
		                            str ? str : _("You are now connected to the GSM network."),
		                            "nm-device-wwan",
		                            PREF_DISABLE_CONNECTED_NOTIFICATIONS);
		g_free (str);
	}
}

static GdkPixbuf *
gsm_get_icon (NMDevice *device,
              NMDeviceState state,
              NMConnection *connection,
              char **tip,
              NMApplet *applet)
{
	NMSettingConnection *s_con;
	GdkPixbuf *pixbuf = NULL;
	const char *id;
	GsmDeviceInfo *info;

	info = g_object_get_data (G_OBJECT (device), "devinfo");
	g_assert (info);

	id = nm_device_get_iface (NM_DEVICE (device));
	if (connection) {
		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		id = nm_setting_connection_get_id (s_con);
	}

	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
		*tip = g_strdup_printf (_("Preparing mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_CONFIG:
		*tip = g_strdup_printf (_("Configuring mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_NEED_AUTH:
		*tip = g_strdup_printf (_("User authentication required for mobile broadband connection '%s'..."), id);
		break;
	case NM_DEVICE_STATE_IP_CONFIG:
		*tip = g_strdup_printf (_("Requesting a network address for '%s'..."), id);
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		pixbuf = nma_icon_check_and_load ("nm-device-wwan", &applet->wwan_icon, applet);
		if (info->reg_state && info->quality_valid) {
			gboolean roaming = (info->reg_state == 6);

			*tip = g_strdup_printf (_("Mobile broadband connection '%s' active: (%d%%%s%s)"),
			                        id, info->quality,
			                        roaming ? ", " : "",
			                        roaming ? _("roaming") : "");
		} else
			*tip = g_strdup_printf (_("Mobile broadband connection '%s' active"), id);
		break;
	default:
		break;
	}

	return pixbuf;
}

typedef struct {
	NMANewSecretsRequestedFunc callback;
	gpointer callback_data;
	NMApplet *applet;
	NMActiveConnection *active_connection;
	GtkWidget *dialog;
	NMSettingsConnectionInterface *connection;
	GtkEntry *secret_entry;
	char *secret_name;
} NMGsmInfo;


static void
pin_entry_changed (GtkEditable *editable, gpointer user_data)
{
	GtkWidget *ok_button = GTK_WIDGET (user_data);
	const char *s;
	int i;
	gboolean valid = FALSE;

	s = gtk_entry_get_text (GTK_ENTRY (editable));
	if (s && strlen (s) == 4) {
		valid = TRUE;
		for (i = 0; i < 4; i++) {
			if (!g_ascii_isdigit (s[i])) {
				valid = FALSE;
				break;
			}
		}
	}

	gtk_widget_set_sensitive (ok_button, valid);
}

static void
destroy_gsm_dialog (gpointer user_data, GObject *finalized)
{
	NMGsmInfo *info = user_data;

	gtk_widget_hide (info->dialog);
	gtk_widget_destroy (info->dialog);

	g_object_unref (info->connection);
	g_free (info->secret_name);
	g_free (info);
}

static void
update_cb (NMSettingsConnectionInterface *connection,
           GError *error,
           gpointer user_data)
{
	if (error) {
		g_warning ("%s: failed to update connection: (%d) %s",
		           __func__, error->code, error->message);
	}
}

static void
get_existing_secrets_cb (NMSettingsConnectionInterface *connection,
                         GHashTable *existing_secrets,
                         GError *secrets_error,
                         gpointer user_data)
{
	NMGsmInfo *info = (NMGsmInfo *) user_data;
	GHashTable *settings;
	GError *error = NULL;
	gboolean save_secret = FALSE;
	const char *new_secret = NULL;

	if (secrets_error) {
		error = g_error_copy (secrets_error);
		goto done;
	}

	/* Be a bit paranoid */
	if (connection != info->connection) {
		g_set_error (&error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): unexpected reply for wrong connection.",
		             __FILE__, __LINE__, __func__);
		goto done;
	}

	/* Update connection's secrets so we can hash them for sending back to
	 * NM, and so we can save them all back out to GConf if needed.
	 */
	if (existing_secrets) {
		GHashTableIter iter;
		gpointer key, value;

		g_hash_table_iter_init (&iter, existing_secrets);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			GError *update_error = NULL;
			const char *setting_name = key;
			GHashTable *setting_hash = value;

			/* Keep track of whether or not the user originall saved the secret */
			if (!strcmp (setting_name, NM_SETTING_GSM_SETTING_NAME)) {
				if (g_hash_table_lookup (setting_hash, info->secret_name))
					save_secret = TRUE;
			}

			if (!nm_connection_update_secrets (NM_CONNECTION (info->connection),
			                                   setting_name,
			                                   setting_hash,
			                                   &update_error)) {
				g_warning ("%s: error updating connection secrets: (%d) %s",
				           __func__,
				           update_error ? update_error->code : -1,
				           update_error && update_error->message ? update_error->message : "(unknown)");
				g_clear_error (&update_error);
			}
		}
	}

	/* Now update the secret the user just entered */
	if (   !strcmp (info->secret_name, NM_SETTING_GSM_PIN)
	    || !strcmp (info->secret_name, NM_SETTING_GSM_PASSWORD)) {
		NMSetting *s_gsm;

		s_gsm = (NMSetting *) nm_connection_get_setting (NM_CONNECTION (info->connection),
		                                                 NM_TYPE_SETTING_GSM);
		if (s_gsm) {
			new_secret = gtk_entry_get_text (info->secret_entry);
			g_object_set (G_OBJECT (s_gsm), info->secret_name, new_secret, NULL);
		}
	}

	settings = nm_connection_to_hash (NM_CONNECTION (info->connection));
	info->callback (info->connection, settings, NULL, info->callback_data);
	g_hash_table_destroy (settings);

	/* Save secrets back to GConf if the user had entered them into the
	 * connection originally.  This lets users enter their secret every time if
	 * they want.
	 */
	if (new_secret && save_secret)
		nm_settings_connection_interface_update (info->connection, update_cb, NULL);

 done:
	if (error) {
		g_warning ("%s", error->message);
		info->callback (info->connection, NULL, error, info->callback_data);
		g_clear_error (&error);
	}

	nm_connection_clear_secrets (NM_CONNECTION (info->connection));
	destroy_gsm_dialog (info, NULL);
}

static void
get_gsm_secrets_cb (GtkDialog *dialog,
                    gint response,
                    gpointer user_data)
{
	NMGsmInfo *info = (NMGsmInfo *) user_data;
	GError *error = NULL;

	/* Got a user response, clear the NMActiveConnection destroy handler for
	 * this dialog since this function will now take over dialog destruction.
	 */
	g_object_weak_unref (G_OBJECT (info->active_connection), destroy_gsm_dialog, info);

	if (response == GTK_RESPONSE_OK) {
		const char *hints[2] = { info->secret_name, NULL };

		/* Get existing connection secrets since NM will want those too */
		if (!nm_settings_connection_interface_get_secrets (info->connection,
		                                                   NM_SETTING_GSM_SETTING_NAME,
		                                                   (const char **) &hints,
		                                                   FALSE,
		                                                   get_existing_secrets_cb,
		                                                   info)) {
			g_set_error (&error,
			             NM_SETTINGS_INTERFACE_ERROR,
			             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
			             "%s.%d (%s): failed to get existing connection secrets",
			             __FILE__, __LINE__, __func__);
		}
	} else {
		g_set_error (&error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): canceled",
		             __FILE__, __LINE__, __func__);
	}

	if (error) {
		g_warning ("%s", error->message);
		info->callback (info->connection, NULL, error, info->callback_data);
		g_error_free (error);

		nm_connection_clear_secrets (NM_CONNECTION (info->connection));
		destroy_gsm_dialog (info, NULL);
	}
}

static GtkWidget *
ask_for_pin_puk (NMDevice *device,
                 NMConnection *connection,
                 const char *secret_name,
                 GtkEntry **out_secret_entry)
{
	GtkDialog *dialog;
	GtkWidget *w = NULL, *ok_button;
	GtkBox *box;
	char *dev_str;

	dialog = GTK_DIALOG (gtk_dialog_new ());
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	if (!strcmp (secret_name, NM_SETTING_GSM_PIN))
		gtk_window_set_title (GTK_WINDOW (dialog), _("PIN code required"));
	else if (!strcmp (secret_name, NM_SETTING_GSM_PUK))
		gtk_window_set_title (GTK_WINDOW (dialog), _("PUK code required"));
	else
		g_assert_not_reached ();

	ok_button = gtk_dialog_add_button (dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	ok_button = gtk_dialog_add_button (dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default (GTK_WINDOW (dialog), ok_button);

	if (!strcmp (secret_name, NM_SETTING_GSM_PIN))
		w = gtk_label_new (_("PIN code is needed for the mobile broadband device"));
	else if (!strcmp (secret_name, NM_SETTING_GSM_PUK))
		w = gtk_label_new (_("PUK code is needed for the mobile broadband device"));
	if (w)
		gtk_box_pack_start (GTK_BOX (dialog->vbox), w, TRUE, TRUE, 0);

	dev_str = g_strdup_printf ("<b>%s</b>", utils_get_device_description (device));
	w = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (w), dev_str);
	g_free (dev_str);
	gtk_box_pack_start (GTK_BOX (dialog->vbox), w, TRUE, TRUE, 0);

	w = gtk_alignment_new (0.5, 0.5, 0, 1.0);
	gtk_box_pack_start (GTK_BOX (dialog->vbox), w, TRUE, TRUE, 0);

	box = GTK_BOX (gtk_hbox_new (FALSE, 6));
	gtk_container_set_border_width (GTK_CONTAINER (box), 6);
	gtk_container_add (GTK_CONTAINER (w), GTK_WIDGET (box));

	gtk_box_pack_start (box, gtk_label_new ("PIN:"), FALSE, FALSE, 0);

	w = gtk_entry_new ();
	*out_secret_entry = GTK_ENTRY (w);
	gtk_entry_set_max_length (GTK_ENTRY (w), 4);
	gtk_entry_set_width_chars (GTK_ENTRY (w), 4);
	gtk_entry_set_activates_default (GTK_ENTRY (w), TRUE);
	gtk_box_pack_start (box, w, FALSE, FALSE, 0);
	g_signal_connect (w, "changed", G_CALLBACK (pin_entry_changed), ok_button);
	pin_entry_changed (GTK_EDITABLE (w), ok_button);

	gtk_widget_show_all (dialog->vbox);
	return GTK_WIDGET (dialog);
}

static gboolean
gsm_get_secrets (NMDevice *device,
                 NMSettingsConnectionInterface *connection,
                 NMActiveConnection *active_connection,
                 const char *setting_name,
                 const char **hints,
                 NMANewSecretsRequestedFunc callback,
                 gpointer callback_data,
                 NMApplet *applet,
                 GError **error)
{
	NMGsmInfo *info;
	GtkWidget *widget;
	GtkEntry *secret_entry = NULL;

	if (!hints || !g_strv_length ((char **) hints)) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): missing secrets hints.",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	if (   !strcmp (hints[0], NM_SETTING_GSM_PIN)
	    || !strcmp (hints[0], NM_SETTING_GSM_PUK))
		widget = ask_for_pin_puk (device, NM_CONNECTION (connection), hints[0], &secret_entry);
	else if (!strcmp (hints[0], NM_SETTING_GSM_PASSWORD))
		widget = applet_mobile_password_dialog_new (device, NM_CONNECTION (connection), &secret_entry);
	else {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): unknown secrets hint '%s'.",
		             __FILE__, __LINE__, __func__, hints[0]);
		return FALSE;
	}

	if (!widget || !secret_entry) {
		g_set_error (error,
		             NM_SETTINGS_INTERFACE_ERROR,
		             NM_SETTINGS_INTERFACE_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): error asking for GSM secrets.",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	info = g_malloc0 (sizeof (NMGsmInfo));
	info->callback = callback;
	info->callback_data = callback_data;
	info->applet = applet;
	info->active_connection = active_connection;
	info->connection = g_object_ref (connection);
	info->secret_name = g_strdup (hints[0]);
	info->dialog = widget;
	info->secret_entry = secret_entry;

	g_signal_connect (widget, "response", G_CALLBACK (get_gsm_secrets_cb), info);

	/* Attach a destroy notifier to the NMActiveConnection so we can destroy
	 * the dialog when the active connection goes away.
	 */
	g_object_weak_ref (G_OBJECT (active_connection), destroy_gsm_dialog, info);

	gtk_window_set_position (GTK_WINDOW (widget), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (GTK_WIDGET (widget));
	gtk_window_present (GTK_WINDOW (widget));

	return TRUE;
}

static void
gsm_device_info_free (gpointer data)
{
	GsmDeviceInfo *info = data;

	if (info->props_proxy)
		g_object_unref (info->props_proxy);
	if (info->card_proxy)
		g_object_unref (info->card_proxy);
	if (info->net_proxy)
		g_object_unref (info->net_proxy);

	if (info->poll_id)
		g_source_remove (info->poll_id);

	g_free (info->op_code);
	g_free (info->op_name);
	memset (info, 0, sizeof (GsmDeviceInfo));
	g_free (info);
}

static void
signal_reply (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GError *error = NULL;
	guint32 quality = 0;

	if (dbus_g_proxy_end_call (proxy, call, &error,
	                           G_TYPE_UINT, &quality,
	                           G_TYPE_INVALID)) {
		info->quality = quality;
		info->quality_valid = TRUE;
	}

	g_clear_error (&error);
}

#define REG_INFO_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
#define DBUS_TYPE_G_MAP_OF_VARIANT (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))

static void
reg_info_reply (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GError *error = NULL;
	GValueArray *array = NULL;
	guint32 new_state = 0;
	char *new_op_code = NULL;
	char *new_op_name = NULL;
	GValue *value;

	if (dbus_g_proxy_end_call (proxy, call, &error, REG_INFO_TYPE, &array, G_TYPE_INVALID)) {
		if (array->n_values == 3) {
			value = g_value_array_get_nth (array, 0);
			if (G_VALUE_HOLDS_UINT (value))
				new_state = g_value_get_uint (value);

			value = g_value_array_get_nth (array, 1);
			if (G_VALUE_HOLDS_STRING (value))
				new_op_code = g_value_dup_string (value);

			value = g_value_array_get_nth (array, 2);
			if (G_VALUE_HOLDS_STRING (value))
				new_op_name = g_value_dup_string (value);
		}

		g_value_array_free (array);
	}

	info->reg_state = new_state + 1;
	info->op_code = new_op_code;
	info->op_name = new_op_name;

	g_clear_error (&error);
}

static void
unlock_reply (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GError *error = NULL;
	char *unlock = NULL;

	if (dbus_g_proxy_end_call (proxy, call, &error,
	                           G_TYPE_STRING, &unlock,
	                           G_TYPE_INVALID)) {
		g_free (info->unlock_required);
		info->unlock_required = unlock;

		if (info->unlock_required) {
			/* Handle unlock */
		}
	}

	g_clear_error (&error);
}

static gboolean
gsm_poll_cb (gpointer user_data)
{
	GsmDeviceInfo *info = user_data;

	if (info->nopoll)
		return TRUE;

	dbus_g_proxy_begin_call (info->net_proxy, "GetRegistrationInfo",
	                         reg_info_reply, info, NULL,
	                         G_TYPE_INVALID);

	dbus_g_proxy_begin_call (info->net_proxy, "GetSignalQuality",
	                         signal_reply, info, NULL,
	                         G_TYPE_INVALID);

	return TRUE;
}

static void
reg_info_changed_cb (DBusGProxy *proxy,
                     guint32 reg_state,
                     const char *op_code,
                     const char *op_name,
                     gpointer user_data)
{
	GsmDeviceInfo *info = user_data;

	info->reg_state = reg_state + 1;
	info->op_code = g_strdup (op_code);
	info->op_name = g_strdup (op_name);
}

static void
signal_quality_changed_cb (DBusGProxy *proxy,
                           guint32 quality,
                           gpointer user_data)
{
	GsmDeviceInfo *info = user_data;

	info->quality = quality;
	info->quality_valid = TRUE;
}

#define MM_DBUS_INTERFACE_MODEM "org.freedesktop.ModemManager.Modem"

static void
modem_properties_changed (DBusGProxy *proxy,
                          const char *interface,
                          GHashTable *props,
                          gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GValue *value;

	if (strcmp (interface, MM_DBUS_INTERFACE_MODEM))
		return;

	value = g_hash_table_lookup (props, "UnlockRequired");
	if (value && G_VALUE_HOLDS_STRING (value)) {
		g_free (info->unlock_required);
		info->unlock_required = g_value_dup_string (value);

		if (info->unlock_required) {
			/* Handle unlock */
		}
	}
}

static void
gsm_device_added (NMDevice *device, NMApplet *applet)
{
	NMGsmDevice *gsm = NM_GSM_DEVICE (device);
	AppletDBusManager *dbus_mgr = applet_dbus_manager_get ();
	DBusGConnection *bus = applet_dbus_manager_get_connection (dbus_mgr);
	GsmDeviceInfo *info;
	const char *udi;
	NMDeviceState state;

	udi = nm_device_get_udi (device);
	if (!udi)
		return;

	info = g_malloc0 (sizeof (GsmDeviceInfo));

	/* Don't bother polling if the device isn't usable */
	state = nm_device_get_state (device);
	if (state <= NM_DEVICE_STATE_UNAVAILABLE)
		info->nopoll = TRUE;

	info->props_proxy = dbus_g_proxy_new_for_name (bus,
	                                               "org.freedesktop.ModemManager",
	                                               udi,
	                                               "org.freedesktop.DBus.Properties");
	if (!info->props_proxy) {
		g_message ("%s: failed to create D-Bus properties proxy.", __func__);
		gsm_device_info_free (info);
		return;
	}

	info->card_proxy = dbus_g_proxy_new_for_name (bus,
	                                              "org.freedesktop.ModemManager",
	                                              udi,
	                                              "org.freedesktop.ModemManager.Modem.Gsm.Card");
	if (!info->card_proxy) {
		g_message ("%s: failed to create GSM Card proxy.", __func__);
		gsm_device_info_free (info);
		return;
	}

	info->net_proxy = dbus_g_proxy_new_for_name (bus,
	                                             "org.freedesktop.ModemManager",
	                                             udi,
	                                             "org.freedesktop.ModemManager.Modem.Gsm.Network");
	if (!info->net_proxy) {
		g_message ("%s: failed to create GSM Network proxy.", __func__);
		gsm_device_info_free (info);
		return;
	}

	g_object_set_data_full (G_OBJECT (gsm), "devinfo", info, gsm_device_info_free);

	/* Registration info signal */
	dbus_g_object_register_marshaller (nma_marshal_VOID__UINT_STRING_STRING,
	                                   G_TYPE_NONE,
	                                   G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (info->net_proxy, "RegistrationInfo",
	                         G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (info->net_proxy, "RegistrationInfo",
	                             G_CALLBACK (reg_info_changed_cb), info, NULL);

	/* Signal quality change signal */
	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__UINT,
	                                   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (info->net_proxy, "SignalQuality", G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (info->net_proxy, "SignalQuality",
	                             G_CALLBACK (signal_quality_changed_cb), info, NULL);

	/* Modem property change signal */
	dbus_g_object_register_marshaller (nma_marshal_VOID__STRING_BOXED,
	                                   G_TYPE_NONE, G_TYPE_STRING, DBUS_TYPE_G_MAP_OF_VARIANT,
	                                   G_TYPE_INVALID);
	dbus_g_proxy_add_signal (info->props_proxy, "MmPropertiesChanged",
	                         G_TYPE_STRING, DBUS_TYPE_G_MAP_OF_VARIANT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (info->props_proxy, "MmPropertiesChanged",
	                             G_CALLBACK (modem_properties_changed),
	                             info, NULL);

	/* Ask whether the device needs to be unlocked */
	dbus_g_proxy_begin_call (info->props_proxy, "Get",
	                         unlock_reply, info, NULL,
	                         G_TYPE_STRING, "org.freedesktop.ModemManager.Modem",
	                         G_TYPE_STRING, "UnlockRequired",
	                         G_TYPE_INVALID);

	/* periodically poll for signal quality and registration state */
	info->poll_id = g_timeout_add_seconds (10, gsm_poll_cb, info);
	if (!info->nopoll)
		gsm_poll_cb (info);

	g_object_unref (dbus_mgr);
}

NMADeviceClass *
applet_device_gsm_get_class (NMApplet *applet)
{
	NMADeviceClass *dclass;

	dclass = g_slice_new0 (NMADeviceClass);
	if (!dclass)
		return NULL;

	dclass->new_auto_connection = gsm_new_auto_connection;
	dclass->add_menu_item = gsm_add_menu_item;
	dclass->device_state_changed = gsm_device_state_changed;
	dclass->get_icon = gsm_get_icon;
	dclass->get_secrets = gsm_get_secrets;
	dclass->device_added = gsm_device_added;

	return dclass;
}

