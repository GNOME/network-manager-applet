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
 * (C) Copyright 2008 - 2010 Red Hat, Inc.
 * (C) Copyright 2008 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <ctype.h>

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
#include "nmn-mobile-providers.h"

typedef enum {
    MM_MODEM_GSM_ACCESS_TECH_UNKNOWN     = 0,
    MM_MODEM_GSM_ACCESS_TECH_GSM         = 1,
    MM_MODEM_GSM_ACCESS_TECH_GSM_COMPACT = 2,
    MM_MODEM_GSM_ACCESS_TECH_GPRS        = 3,
    MM_MODEM_GSM_ACCESS_TECH_EDGE        = 4,  /* GSM w/EGPRS */
    MM_MODEM_GSM_ACCESS_TECH_UMTS        = 5,  /* UTRAN */
    MM_MODEM_GSM_ACCESS_TECH_HSDPA       = 6,  /* UTRAN w/HSDPA */
    MM_MODEM_GSM_ACCESS_TECH_HSUPA       = 7,  /* UTRAN w/HSUPA */
    MM_MODEM_GSM_ACCESS_TECH_HSPA        = 8,  /* UTRAN w/HSDPA and HSUPA */

    MM_MODEM_GSM_ACCESS_TECH_LAST = MM_MODEM_GSM_ACCESS_TECH_HSPA
} MMModemGsmAccessTech;

typedef struct {
	NMApplet *applet;
	NMDevice *device;

	DBusGProxy *props_proxy;
	DBusGProxy *card_proxy;
	DBusGProxy *net_proxy;

	gboolean quality_valid;
	guint32 quality;
	char *unlock_required;
	gboolean modem_enabled;
	MMModemGsmAccessTech act;

	/* reg_state is (1 + MM reg state) so that 0 means we haven't gotten a
	 * value from MM yet.  0 is a valid MM GSM reg state.
	 */
	guint reg_state;
	char *op_code;
	char *op_name;
	GHashTable *providers;

	guint32 poll_id;
	gboolean skip_reg_poll;
	gboolean skip_signal_poll;

	/* Unlock dialog stuff */
	GtkWidget *dialog;
} GsmDeviceInfo;

static void unlock_dialog_destroy (GsmDeviceInfo *info);
static void check_start_polling (GsmDeviceInfo *info);

typedef struct {
	NMApplet *applet;
	NMDevice *device;
	NMConnection *connection;
} GSMMenuItemInfo;

static void
gsm_menu_item_info_destroy (gpointer data)
{
	GSMMenuItemInfo *info = data;

	g_object_unref (G_OBJECT (info->device));
	if (info->connection)
		g_object_unref (info->connection);

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

static guint32
gsm_state_to_mb_state (GsmDeviceInfo *info)
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

static guint32
gsm_act_to_mb_act (GsmDeviceInfo *info)
{
	switch (info->act) {
	case MM_MODEM_GSM_ACCESS_TECH_GPRS:
		return MB_TECH_GPRS;
	case MM_MODEM_GSM_ACCESS_TECH_EDGE:
		return MB_TECH_EDGE;
	case MM_MODEM_GSM_ACCESS_TECH_UMTS:
		return MB_TECH_UMTS;
	case MM_MODEM_GSM_ACCESS_TECH_HSDPA:
		return MB_TECH_HSDPA;
	case MM_MODEM_GSM_ACCESS_TECH_HSUPA:
		return MB_TECH_HSUPA;
	case MM_MODEM_GSM_ACCESS_TECH_HSPA:
		return MB_TECH_HSPA;
	default:
		break;
	}

	return MB_TECH_GSM;
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

		s_con = (NMSettingConnection *) nm_connection_get_setting (active, NM_TYPE_SETTING_CONNECTION);
		g_assert (s_con);

		item = nm_mb_menu_item_new (nm_setting_connection_get_id (s_con),
		                            info->quality_valid ? info->quality : 0,
		                            info->op_name,
		                            gsm_act_to_mb_act (info),
		                            gsm_state_to_mb_state (info),
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
		item = nm_mb_menu_item_new (NULL,
		                            info->quality_valid ? info->quality : 0,
		                            info->op_name,
		                            gsm_act_to_mb_act (info),
		                            gsm_state_to_mb_state (info),
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
	GsmDeviceInfo *info;

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

	/* Start/stop polling of quality and registration when device state changes */
	info = g_object_get_data (G_OBJECT (device), "devinfo");
	check_start_polling (info);
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
	guint32 mb_state;

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
		mb_state = gsm_state_to_mb_state (info);
		pixbuf = mobile_helper_get_status_pixbuf (info->quality,
		                                          info->quality_valid,
		                                          mb_state,
		                                          gsm_act_to_mb_act (info),
		                                          applet);

		if ((mb_state != MB_STATE_UNKNOWN) && info->quality_valid) {
			gboolean roaming = (mb_state == MB_STATE_ROAMING);

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
	/* General stuff */
	GtkWidget *dialog;
	GtkEntry *secret_entry;
	char *secret_name;
	NMApplet *applet;
	NMANewSecretsRequestedFunc callback;
	gpointer callback_data;
	NMActiveConnection *active_connection;
	NMSettingsConnectionInterface *connection;
} NMGsmSecretsInfo;


static void
pin_entry_changed (GtkEditable *editable, gpointer user_data)
{
	GtkWidget *ok_button = GTK_WIDGET (user_data);
	const char *s;
	int i;
	gboolean valid = FALSE;
	guint32 len;

	s = gtk_entry_get_text (GTK_ENTRY (editable));
	if (s) {
		len = strlen (s);
		if ((len >= 4) && (len <= 8)) {
			valid = TRUE;
			for (i = 0; i < len; i++) {
				if (!g_ascii_isdigit (s[i])) {
					valid = FALSE;
					break;
				}
			}
		}
	}

	gtk_widget_set_sensitive (ok_button, valid);
}

static void
secrets_dialog_destroy (gpointer user_data, GObject *finalized)
{
	NMGsmSecretsInfo *info = user_data;

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
	NMGsmSecretsInfo *info = (NMGsmSecretsInfo *) user_data;
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
	secrets_dialog_destroy (info, NULL);
}

static void
get_gsm_secrets_cb (GtkDialog *dialog,
                    gint response,
                    gpointer user_data)
{
	NMGsmSecretsInfo *info = (NMGsmSecretsInfo *) user_data;
	GError *error = NULL;

	/* Got a user response, clear the NMActiveConnection destroy handler for
	 * this dialog since this function will now take over dialog destruction.
	 */
	g_object_weak_unref (G_OBJECT (info->active_connection), secrets_dialog_destroy, info);

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
		secrets_dialog_destroy (info, NULL);
	}
}

static GtkWidget *
ask_for_pin_puk (NMDevice *device,
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
	gtk_entry_set_max_length (GTK_ENTRY (w), 8);
	gtk_entry_set_width_chars (GTK_ENTRY (w), 8);
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
	NMGsmSecretsInfo *secrets_info;
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
	    || !strcmp (hints[0], NM_SETTING_GSM_PUK)) {
		GsmDeviceInfo *info = g_object_get_data (G_OBJECT (device), "devinfo");

		g_assert (info);
		/* A GetSecrets PIN dialog overrides the initial unlock dialog */
		if (info->dialog)
			unlock_dialog_destroy (info);

		widget = ask_for_pin_puk (device, hints[0], &secret_entry);
	} else if (!strcmp (hints[0], NM_SETTING_GSM_PASSWORD))
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

	secrets_info = g_malloc0 (sizeof (NMGsmSecretsInfo));
	secrets_info->callback = callback;
	secrets_info->callback_data = callback_data;
	secrets_info->applet = applet;
	secrets_info->active_connection = active_connection;
	secrets_info->connection = g_object_ref (connection);
	secrets_info->secret_name = g_strdup (hints[0]);
	secrets_info->dialog = widget;
	secrets_info->secret_entry = secret_entry;

	g_signal_connect (widget, "response", G_CALLBACK (get_gsm_secrets_cb), secrets_info);

	/* Attach a destroy notifier to the NMActiveConnection so we can destroy
	 * the dialog when the active connection goes away.
	 */
	g_object_weak_ref (G_OBJECT (active_connection), secrets_dialog_destroy, secrets_info);

	gtk_window_set_position (GTK_WINDOW (widget), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (GTK_WIDGET (widget));
	gtk_window_present (GTK_WINDOW (widget));

	return TRUE;
}

/********************************************************************/

static void
unlock_dialog_destroy (GsmDeviceInfo *info)
{
	applet_mobile_pin_dialog_destroy (info->dialog);
	info->dialog = NULL;
}

static void
unlock_pin_reply (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GError *error = NULL;
	const char *dbus_error, *msg = NULL;

	if (dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID)) {
		unlock_dialog_destroy (info);
		return;
	}

	dbus_error = dbus_g_error_get_name (error);
	if (dbus_error && !strcmp (dbus_error, "org.freedesktop.ModemManager.Modem.Gsm.IncorrectPassword"))
		msg = _("Wrong PIN code; please contact your provider.");
	else
		msg = error ? error->message : NULL;

	applet_mobile_pin_dialog_stop_spinner (info->dialog, msg);
	g_warning ("%s: error unlocking with PIN: %s", __func__, error->message);
	g_clear_error (&error);
}

static void
unlock_puk_reply (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GError *error = NULL;
	const char *dbus_error, *msg = NULL;

	if (dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID)) {
		unlock_dialog_destroy (info);
		return;
	}

	dbus_error = dbus_g_error_get_name (error);
	if (dbus_error && !strcmp (dbus_error, "org.freedesktop.ModemManager.Modem.Gsm.IncorrectPassword"))
		msg = _("Wrong PUK code; please contact your provider.");
	else
		msg = error ? error->message : NULL;

	applet_mobile_pin_dialog_stop_spinner (info->dialog, msg);
	g_warning ("%s: error unlocking with PIN: %s", __func__, error->message);
	g_clear_error (&error);
}

#define UNLOCK_CODE_PIN 1
#define UNLOCK_CODE_PUK 2

static void
unlock_dialog_response (GtkDialog *dialog,
                        gint response,
                        gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	const char *code1, *code2;
	guint32 unlock_code;

	if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT) {
		unlock_dialog_destroy (info);
		return;
	}

	/* Start the spinner to show the progress of the unlock */
	applet_mobile_pin_dialog_start_spinner (info->dialog, _("Sending unlock code..."));

	unlock_code = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (info->dialog), "unlock-code"));
	if (!unlock_code) {
		g_warn_if_fail (unlock_code != 0);
		unlock_dialog_destroy (info);
		return;
	}

	code1 = applet_mobile_pin_dialog_get_entry1 (info->dialog);
	if (!code1 || !strlen (code1)) {
		g_warn_if_fail (code1 != NULL);
		g_warn_if_fail (strlen (code1));
		unlock_dialog_destroy (info);
		return;
	}

	/* Send the code to ModemManager */
	if (unlock_code == UNLOCK_CODE_PIN) {
		dbus_g_proxy_begin_call (info->card_proxy, "SendPin",
		                         unlock_pin_reply, info, NULL,
		                         G_TYPE_STRING, code1, G_TYPE_INVALID);
	} else if (unlock_code == UNLOCK_CODE_PUK) {
		code2 = applet_mobile_pin_dialog_get_entry2 (info->dialog);
		if (!code2) {
			g_warn_if_fail (code2 != NULL);
			unlock_dialog_destroy (info);
			return;
		}

		dbus_g_proxy_begin_call (info->card_proxy, "SendPuk",
		                         unlock_puk_reply, info, NULL,
		                         G_TYPE_STRING, code1,
		                         G_TYPE_STRING, code2,
		                         G_TYPE_INVALID);
	}
}

static void
unlock_dialog_new (NMDevice *device, GsmDeviceInfo *info)
{
	const char *header = NULL;
	const char *title = NULL;
	char *desc = NULL;
	const char *label1 = NULL, *label2 = NULL, *label3 = NULL;
	const char *device_desc;
	gboolean match23 = FALSE;
	guint32 label1_min = 0, label2_min = 0, label3_min = 0;
	guint32 label1_max = 0, label2_max = 0, label3_max = 0;
	guint32 unlock_code = 0;

	g_return_if_fail (info->unlock_required != NULL);

	if (info->dialog)
		return;

	/* Figure out the dialog text based on the required unlock code */
	device_desc = utils_get_device_description (device);
	if (!strcmp (info->unlock_required, "sim-pin")) {
		title = _("SIM PIN unlock required");
		header = _("SIM PIN Unlock Required");
		/* FIXME: some warning about # of times you can enter incorrect PIN */
		desc = g_strdup_printf (_("The mobile broadband device '%s' requires a SIM PIN code before it can be used."), device_desc);
		label1 = _("PIN code:");
		label1_min = 4;
		label1_max = 8;
		unlock_code = UNLOCK_CODE_PIN;
	} else if (!strcmp (info->unlock_required, "sim-puk")) {
		title = _("SIM PUK unlock required");
		header = _("SIM PUK Unlock Required");
		/* FIXME: some warning about # of times you can enter incorrect PUK */
		desc = g_strdup_printf (_("The mobile broadband device '%s' requires a SIM PUK code before it can be used."), device_desc);
		label1 = _("PUK code:");
		label1_min = label1_max = 8;
		label2 = _("New PIN code:");
		label3 = _("Re-enter new PIN code:");
		label2_min = label3_min = 4;
		label2_max = label3_max = 8;
		match23 = TRUE;
		unlock_code = UNLOCK_CODE_PUK;
	} else {
		g_warning ("Unhandled unlock request for '%s'", info->unlock_required);
		return;
	}

	/* Construct and run the dialog */
	info->dialog = applet_mobile_pin_dialog_new (title, header, desc);
	g_free (desc);
	g_return_if_fail (info->dialog != NULL);

	g_object_set_data (G_OBJECT (info->dialog), "unlock-code", GUINT_TO_POINTER (unlock_code));
	applet_mobile_pin_dialog_match_23 (info->dialog, match23);

	applet_mobile_pin_dialog_set_entry1 (info->dialog, label1, label1_min, label1_max);
	if (label2)
		applet_mobile_pin_dialog_set_entry2 (info->dialog, label2, label2_min, label2_max);
	if (label3)
		applet_mobile_pin_dialog_set_entry3 (info->dialog, label3, label3_min, label3_max);

	g_signal_connect (info->dialog, "response", G_CALLBACK (unlock_dialog_response), info);
	applet_mobile_pin_dialog_present (info->dialog, FALSE);
}

/********************************************************************/

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

	if (info->providers)
		g_hash_table_destroy (info->providers);

	if (info->poll_id)
		g_source_remove (info->poll_id);

	if (info->dialog)
		unlock_dialog_destroy (info);

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
		applet_schedule_update_icon (info->applet);
	}

	g_clear_error (&error);
}

static char *
find_provider_for_mcc_mnc (GHashTable *table, const char *mccmnc)
{
	GHashTableIter iter;
	gpointer value;
	GSList *miter, *piter, *siter;
	const char *name2 = NULL, *name3 = NULL;
	gboolean done = FALSE;

	if (!mccmnc)
		return NULL;

	g_hash_table_iter_init (&iter, table);
	/* Search through each country */
	while (g_hash_table_iter_next (&iter, NULL, &value) && !done) {
		GSList *providers = value;

		/* Search through each country's providers */
		for (piter = providers; piter && !done; piter = g_slist_next (piter)) {
			NmnMobileProvider *provider = piter->data;

			/* Search through each provider's access methods */
			for (miter = provider->methods; miter && !done; miter = g_slist_next (miter)) {
				NmnMobileAccessMethod *method = miter->data;

				if (method->type != NMN_MOBILE_ACCESS_METHOD_TYPE_GSM)
					continue;

				/* Search through MCC/MNC list */
				for (siter = method->gsm_mcc_mnc; siter; siter = g_slist_next (siter)) {
					NmnGsmMccMnc *mcc = siter->data;

					/* Match both 2-digit and 3-digit MNC; prefer a
					 * 3-digit match if found, otherwise a 2-digit one.
					 */
					if (strncmp (mcc->mcc, mccmnc, 3))
						continue;  /* MCC was wrong */

					if (   !name3
					    && (strlen (mccmnc) == 6)
					    && !strncmp (mccmnc + 3, mcc->mnc, 3))
						name3 = provider->name;

					if (   !name2
					    && !strncmp (mccmnc + 3, mcc->mnc, 2))
						name2 = provider->name;

					if (name2 && name3) {
						done = TRUE;
						break;
					}
				}
			}
		}
	}

	if (name3)
		return g_strdup (name3);
	return name2 ? g_strdup (name2) : NULL;
}

static char *
parse_op_name (GsmDeviceInfo *info, const char *orig, const char *op_code)
{
	guint i, orig_len;

	/* Some devices return the MCC/MNC if they haven't fully initialized
	 * or gotten all the info from the network yet.  Handle that.
	 */

	orig_len = orig ? strlen (orig) : 0;
	if (orig_len == 0) {
		/* If the operator name isn't valid, maybe we can look up the MCC/MNC
		 * from the operator code instead.
		 */
		if (op_code && strlen (op_code)) {
			orig = op_code;
			orig_len = strlen (orig);
		} else
			return NULL;
	} else if (orig_len < 5 || orig_len > 6)
		return g_strdup (orig);  /* not an MCC/MNC */

	for (i = 0; i < orig_len; i++) {
		if (!isdigit (orig[i]))
			return strdup (orig);
	}

	/* At this point we have a 5 or 6 character all-digit string; that's
	 * probably an MCC/MNC.  Look that up.
	 */

	if (!info->providers)
		info->providers = nmn_mobile_providers_parse (NULL);
	if (!info->providers)
		return strdup (orig);

	return find_provider_for_mcc_mnc (info->providers, orig);
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
			if (G_VALUE_HOLDS_STRING (value)) {
				new_op_code = g_value_dup_string (value);
				if (new_op_code && !strlen (new_op_code)) {
					g_free (new_op_code);
					new_op_code = NULL;
				}
			}

			value = g_value_array_get_nth (array, 2);
			if (G_VALUE_HOLDS_STRING (value))
				new_op_name = parse_op_name (info, g_value_get_string (value), new_op_code);
		}

		g_value_array_free (array);
	}

	info->reg_state = new_state + 1;
	info->op_code = new_op_code;
	info->op_name = new_op_name;

	g_clear_error (&error);
}

static void
enabled_reply (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GError *error = NULL;
	GValue value = { 0 };

	if (dbus_g_proxy_end_call (proxy, call, &error,
	                           G_TYPE_VALUE, &value,
	                           G_TYPE_INVALID)) {
		if (G_VALUE_HOLDS_BOOLEAN (&value))
			info->modem_enabled = g_value_get_boolean (&value);
		g_value_unset (&value);
	}

	g_clear_error (&error);
	check_start_polling (info);
}

static char *
parse_unlock_required (GValue *value)
{
	const char *new_val;

	/* Empty string means NULL */
	new_val = g_value_get_string (value);
	if (new_val && strlen (new_val)) {
		/* PIN2/PUK2 only required for various dialing things that we don't care
		 * about; it doesn't inhibit normal operation.
		 */
		if (strcmp (new_val, "sim-puk2") && strcmp (new_val, "sim-pin2"))
			return g_strdup (new_val);
	}

	return NULL;
}

static void
unlock_reply (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GError *error = NULL;
	GValue value = { 0 };

	if (dbus_g_proxy_end_call (proxy, call, &error,
	                           G_TYPE_VALUE, &value,
	                           G_TYPE_INVALID)) {
		if (G_VALUE_HOLDS_STRING (&value)) {
			g_free (info->unlock_required);
			info->unlock_required = parse_unlock_required (&value);

			/* Show the unlock dialog if an unlock is now required */
			if (info->unlock_required)
				unlock_dialog_new (info->device, info);
		}
		g_value_unset (&value);
	}

	g_clear_error (&error);
	check_start_polling (info);
}

static void
access_tech_reply (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GError *error = NULL;
	GValue value = { 0 };

	if (dbus_g_proxy_end_call (proxy, call, &error,
	                           G_TYPE_VALUE, &value,
	                           G_TYPE_INVALID)) {
		if (G_VALUE_HOLDS_UINT (&value)) {
			info->act = g_value_get_uint (&value);
			applet_schedule_update_icon (info->applet);
		}
		g_value_unset (&value);
	}
	g_clear_error (&error);
}

static gboolean
gsm_poll_cb (gpointer user_data)
{
	GsmDeviceInfo *info = user_data;

	/* MM might have just sent an unsolicited update, in which case we just
	 * skip this poll and wait till the next one.
	 */

	if (!info->skip_reg_poll) {
		dbus_g_proxy_begin_call (info->net_proxy, "GetRegistrationInfo",
		                         reg_info_reply, info, NULL,
		                         G_TYPE_INVALID);
	}

	if (!info->skip_signal_poll) {
		dbus_g_proxy_begin_call (info->net_proxy, "GetSignalQuality",
		                         signal_reply, info, NULL,
		                         G_TYPE_INVALID);
	}

	info->skip_reg_poll = FALSE;
	info->skip_signal_poll = FALSE;

	return TRUE;  /* keep running until we're told to stop */
}

static void
check_start_polling (GsmDeviceInfo *info)
{
	NMDeviceState state;
	gboolean poll = TRUE;

	g_return_if_fail (info != NULL);

	/* Don't poll if any of the following are true:
	 *
	 * 1) NM says the device is not available
	 * 2) the modem requires an unlock code
	 * 3) the modem isn't enabled
	 */

	state = nm_device_get_state (info->device);
	if (   (state <= NM_DEVICE_STATE_UNAVAILABLE)
	    || info->unlock_required
	    || (info->modem_enabled == FALSE))
		poll = FALSE;

	if (poll) {
		if (!info->poll_id) {
			/* 33 seconds to be just a bit more than MM's poll interval, so
			 * that if we get an unsolicited update from MM between polls we'll
			 * skip the next poll.
			 */
	        info->poll_id = g_timeout_add_seconds (33, gsm_poll_cb, info);
		}
		gsm_poll_cb (info);
	} else {
		if (info->poll_id)
			g_source_remove (info->poll_id);
		info->poll_id = 0;
		info->skip_reg_poll = FALSE;
		info->skip_signal_poll = FALSE;
	}
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
	g_free (info->op_code);
	info->op_code = strlen (op_code) ? g_strdup (op_code) : NULL;
	g_free (info->op_name);
	info->op_name = parse_op_name (info, op_name, info->op_code);
	info->skip_reg_poll = TRUE;
}

static void
signal_quality_changed_cb (DBusGProxy *proxy,
                           guint32 quality,
                           gpointer user_data)
{
	GsmDeviceInfo *info = user_data;

	info->quality = quality;
	info->quality_valid = TRUE;
	info->skip_signal_poll = TRUE;

	applet_schedule_update_icon (info->applet);
}

#define MM_DBUS_INTERFACE_MODEM "org.freedesktop.ModemManager.Modem"
#define MM_DBUS_INTERFACE_MODEM_GSM_NETWORK "org.freedesktop.ModemManager.Modem.Gsm.Network"

static void
modem_properties_changed (DBusGProxy *proxy,
                          const char *interface,
                          GHashTable *props,
                          gpointer user_data)
{
	GsmDeviceInfo *info = user_data;
	GValue *value;

	if (!strcmp (interface, MM_DBUS_INTERFACE_MODEM)) {
		value = g_hash_table_lookup (props, "UnlockRequired");
		if (value && G_VALUE_HOLDS_STRING (value)) {
			g_free (info->unlock_required);
			info->unlock_required = parse_unlock_required (value);
			check_start_polling (info);
		}

		value = g_hash_table_lookup (props, "Enabled");
		if (value && G_VALUE_HOLDS_BOOLEAN (value)) {
			info->modem_enabled = g_value_get_boolean (value);
			check_start_polling (info);
		}
	} else if (!strcmp (interface, MM_DBUS_INTERFACE_MODEM_GSM_NETWORK)) {
		value = g_hash_table_lookup (props, "AccessTechnology");
		if (value && G_VALUE_HOLDS_UINT (value)) {
			info->act = g_value_get_uint (value);
			applet_schedule_update_icon (info->applet);
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

	udi = nm_device_get_udi (device);
	if (!udi)
		return;

	info = g_malloc0 (sizeof (GsmDeviceInfo));
	info->applet = applet;
	info->device = device;

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
	                                             MM_DBUS_INTERFACE_MODEM_GSM_NETWORK);
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
	                         G_TYPE_STRING, MM_DBUS_INTERFACE_MODEM,
	                         G_TYPE_STRING, "UnlockRequired",
	                         G_TYPE_INVALID);

	/* Ask whether the device needs to be unlocked */
	dbus_g_proxy_begin_call (info->props_proxy, "Get",
	                         enabled_reply, info, NULL,
	                         G_TYPE_STRING, MM_DBUS_INTERFACE_MODEM,
	                         G_TYPE_STRING, "Enabled",
	                         G_TYPE_INVALID);

	dbus_g_proxy_begin_call (info->props_proxy, "Get",
	                         access_tech_reply, info, NULL,
	                         G_TYPE_STRING, MM_DBUS_INTERFACE_MODEM_GSM_NETWORK,
	                         G_TYPE_STRING, "AccessTechnology",
	                         G_TYPE_INVALID);

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

