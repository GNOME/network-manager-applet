/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * (C) Copyright 2010 Red Hat, Inc.
 */

#include <glib/gi18n.h>
#include <nm-utils.h>
#include <gnome-keyring.h>

#include "utils.h"
#include "mobile-helpers.h"
#include "applet-dialogs.h"

GdkPixbuf *
mobile_helper_get_status_pixbuf (guint32 quality,
                                 gboolean quality_valid,
                                 guint32 state,
                                 guint32 access_tech,
                                 NMApplet *applet)
{
	GdkPixbuf *pixbuf, *qual_pixbuf, *wwan_pixbuf, *tmp;

	wwan_pixbuf = nma_icon_check_and_load ("nm-wwan-tower", &applet->wwan_tower_icon, applet);

	if (!quality_valid)
		quality = 0;
	qual_pixbuf = mobile_helper_get_quality_icon (quality, applet);

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
	                         TRUE,
	                         gdk_pixbuf_get_bits_per_sample (qual_pixbuf),
	                         gdk_pixbuf_get_width (qual_pixbuf),
	                         gdk_pixbuf_get_height (qual_pixbuf));
	gdk_pixbuf_fill (pixbuf, 0xFFFFFF00);

	/* Composite the tower icon into the final icon at the bottom layer */
	gdk_pixbuf_composite (wwan_pixbuf, pixbuf,
	                      0, 0,
	                      gdk_pixbuf_get_width (wwan_pixbuf),
						  gdk_pixbuf_get_height (wwan_pixbuf),
						  0, 0, 1.0, 1.0,
						  GDK_INTERP_BILINEAR, 255);

	/* Composite the signal quality onto the icon on top of the WWAN tower */
	gdk_pixbuf_composite (qual_pixbuf, pixbuf,
	                      0, 0,
	                      gdk_pixbuf_get_width (qual_pixbuf),
						  gdk_pixbuf_get_height (qual_pixbuf),
						  0, 0, 1.0, 1.0,
						  GDK_INTERP_BILINEAR, 255);

	/* And finally the roaming or technology icon */
	if (state == MB_STATE_ROAMING) {
		tmp = nma_icon_check_and_load ("nm-mb-roam", &applet->mb_roaming_icon, applet);
		gdk_pixbuf_composite (tmp, pixbuf, 0, 0,
		                      gdk_pixbuf_get_width (tmp),
							  gdk_pixbuf_get_height (tmp),
							  0, 0, 1.0, 1.0,
							  GDK_INTERP_BILINEAR, 255);
	} else {
		tmp = mobile_helper_get_tech_icon (access_tech, applet);
		if (tmp) {
			gdk_pixbuf_composite (tmp, pixbuf, 0, 0,
				                  gdk_pixbuf_get_width (tmp),
								  gdk_pixbuf_get_height (tmp),
								  0, 0, 1.0, 1.0,
								  GDK_INTERP_BILINEAR, 255);
		}
	}

	/* 'pixbuf' will be freed by the caller */
	return pixbuf;
}

GdkPixbuf *
mobile_helper_get_quality_icon (guint32 quality, NMApplet *applet)
{
	if (quality > 80)
		return nma_icon_check_and_load ("nm-signal-100", &applet->wifi_100_icon, applet);
	else if (quality > 55)
		return nma_icon_check_and_load ("nm-signal-75", &applet->wifi_75_icon, applet);
	else if (quality > 30)
		return nma_icon_check_and_load ("nm-signal-50", &applet->wifi_50_icon, applet);
	else if (quality > 5)
		return nma_icon_check_and_load ("nm-signal-25", &applet->wifi_25_icon, applet);

	return nma_icon_check_and_load ("nm-signal-00", &applet->wifi_00_icon, applet);
}

GdkPixbuf *
mobile_helper_get_tech_icon (guint32 tech, NMApplet *applet)
{
	switch (tech) {
	case MB_TECH_1XRTT:
		return nma_icon_check_and_load ("nm-tech-cdma-1x", &applet->mb_tech_1x_icon, applet);
	case MB_TECH_EVDO_REV0:
	case MB_TECH_EVDO_REVA:
		return nma_icon_check_and_load ("nm-tech-evdo", &applet->mb_tech_evdo_icon, applet);
	case MB_TECH_GSM:
	case MB_TECH_GPRS:
		return nma_icon_check_and_load ("nm-tech-gprs", &applet->mb_tech_gprs_icon, applet);
	case MB_TECH_EDGE:
		return nma_icon_check_and_load ("nm-tech-edge", &applet->mb_tech_edge_icon, applet);
	case MB_TECH_UMTS:
		return nma_icon_check_and_load ("nm-tech-umts", &applet->mb_tech_umts_icon, applet);
	case MB_TECH_HSDPA:
	case MB_TECH_HSUPA:
	case MB_TECH_HSPA:
	case MB_TECH_HSPA_PLUS:
		return nma_icon_check_and_load ("nm-tech-hspa", &applet->mb_tech_hspa_icon, applet);
	case MB_TECH_LTE:
		return nma_icon_check_and_load ("nm-tech-lte", &applet->mb_tech_lte_icon, applet);
	case MB_TECH_WIMAX:
	default:
		return NULL;
	}
}

/********************************************************************/

typedef struct {
	AppletNewAutoConnectionCallback callback;
	gpointer callback_data;
	NMDeviceModemCapabilities requested_capability;
} AutoWizardInfo;

static void
mobile_wizard_done (NMAMobileWizard *wizard,
                    gboolean cancelled,
                    NMAMobileWizardAccessMethod *method,
                    gpointer user_data)
{
	AutoWizardInfo *info = user_data;
	NMConnection *connection = NULL;

	if (!cancelled && method) {
		NMSetting *setting;
		char *uuid, *id;
		const char *setting_name;

		if (method->devtype != info->requested_capability) {
			g_warning ("Unexpected device type");
			cancelled = TRUE;
			goto done;
		}

		connection = nm_connection_new ();

		if (method->devtype == NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO) {
			setting_name = NM_SETTING_CDMA_SETTING_NAME;
			setting = nm_setting_cdma_new ();
			g_object_set (setting,
			              NM_SETTING_CDMA_NUMBER, "#777",
			              NM_SETTING_CDMA_USERNAME, method->username,
			              NM_SETTING_CDMA_PASSWORD, method->password,
			              NULL);
			nm_connection_add_setting (connection, setting);
		} else if (method->devtype == NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS) {
			setting_name = NM_SETTING_GSM_SETTING_NAME;
			setting = nm_setting_gsm_new ();
			g_object_set (setting,
			              NM_SETTING_GSM_NUMBER, "*99#",
			              NM_SETTING_GSM_USERNAME, method->username,
			              NM_SETTING_GSM_PASSWORD, method->password,
			              NM_SETTING_GSM_APN, method->gsm_apn,
			              NULL);
			nm_connection_add_setting (connection, setting);
		} else
			g_assert_not_reached ();

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
		id = utils_create_mobile_connection_id (method->provider_name, method->plan_name);
		uuid = nm_utils_uuid_generate ();
		g_object_set (setting,
		              NM_SETTING_CONNECTION_ID, id,
		              NM_SETTING_CONNECTION_TYPE, setting_name,
		              NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
		              NM_SETTING_CONNECTION_UUID, uuid,
		              NULL);
		g_free (uuid);
		g_free (id);
		nm_connection_add_setting (connection, setting);
	}

done:
	(*(info->callback)) (connection, TRUE, cancelled, info->callback_data);

	if (wizard)
		nma_mobile_wizard_destroy (wizard);
	g_free (info);
}

gboolean
mobile_helper_wizard (NMDeviceModemCapabilities capabilities,
                      AppletNewAutoConnectionCallback callback,
                      gpointer callback_data)
{
	NMAMobileWizard *wizard;
	AutoWizardInfo *info;
	NMAMobileWizardAccessMethod *method;
	NMDeviceModemCapabilities wizard_capability;

	/* Convert the input capabilities mask into a single value */
	if (capabilities & NM_DEVICE_MODEM_CAPABILITY_LTE)
		/* All LTE modems treated as GSM/UMTS for the wizard */
		wizard_capability = NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS;
	else if (capabilities & NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS)
		wizard_capability = NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS;
	else if (capabilities & NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)
		wizard_capability = NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO;
	else {
		g_warning ("Unknown modem capabilities (0x%X): can't launch wizard", capabilities);
		return FALSE;
	}

	info = g_malloc0 (sizeof (AutoWizardInfo));
	info->callback = callback;
	info->callback_data = callback_data;
	info->requested_capability = wizard_capability;

	wizard = nma_mobile_wizard_new (NULL,
	                                NULL,
	                                wizard_capability,
	                                FALSE,
									mobile_wizard_done,
	                                info);
	if (wizard) {
		nma_mobile_wizard_present (wizard);
		return TRUE;
	}

	/* Fall back to something */
	method = g_malloc0 (sizeof (NMAMobileWizardAccessMethod));
	method->devtype = wizard_capability;

	if (wizard_capability == NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS)
		method->provider_name = _("GSM");
	else if (wizard_capability == NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)
		method->provider_name = _("CDMA");
	else
		g_assert_not_reached ();

	mobile_wizard_done (NULL, FALSE, method, info);
	g_free (method);

	return TRUE;
}

/********************************************************************/

static void
save_pin_cb (GnomeKeyringResult result, guint32 val, gpointer user_data)
{
	if (result != GNOME_KEYRING_RESULT_OK)
		g_warning ("%s: result %d", (const char *) user_data, result);
}

void
mobile_helper_save_pin_in_keyring (const char *devid,
                                   const char *simid,
                                   const char *pin)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringAttribute attr;
	const char *name;
	char *error_msg;

	name = g_strdup_printf (_("PIN code for SIM card '%s' on '%s'"),
	                        simid ? simid : "unknown",
	                        devid);

	attributes = gnome_keyring_attribute_list_new ();
	attr.name = g_strdup ("devid");
	attr.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attr.value.string = g_strdup (devid);
	g_array_append_val (attributes, attr);

	if (simid) {
		attr.name = g_strdup ("simid");
		attr.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
		attr.value.string = g_strdup (simid);
		g_array_append_val (attributes, attr);
	}

	error_msg = g_strdup_printf ("Saving PIN code in keyring for devid:%s simid:%s failed",
	                             devid, simid ? simid : "(unknown)");

	gnome_keyring_item_create (NULL,
	                           GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                           name,
	                           attributes,
	                           pin,
	                           TRUE,
	                           save_pin_cb,
	                           error_msg,
	                           (GDestroyNotify) g_free);

	gnome_keyring_attribute_list_free (attributes);
}

static void
delete_pin_cb (GnomeKeyringResult result, gpointer user_data)
{
	/* nothing to do */
}

static void
delete_pins_find_cb (GnomeKeyringResult result, GList *list, gpointer user_data)
{
	GList *iter;

	if (result == GNOME_KEYRING_RESULT_OK) {
		for (iter = list; iter; iter = g_list_next (iter)) {
			GnomeKeyringFound *found = iter->data;

			gnome_keyring_item_delete (found->keyring, found->item_id, delete_pin_cb, NULL, NULL);
		}
	}
}

void
mobile_helper_delete_pin_in_keyring (const char *devid)
{
	gnome_keyring_find_itemsv (GNOME_KEYRING_ITEM_GENERIC_SECRET,
	                           delete_pins_find_cb,
	                           NULL,
	                           NULL,
	                           "devid",
	                           GNOME_KEYRING_ATTRIBUTE_TYPE_STRING,
	                           devid,
	                           NULL);
}

/********************************************************************/

static void
free_secrets_info (SecretsRequest *req)
{
	MobileHelperSecretsInfo *info = (MobileHelperSecretsInfo *) req;

	if (info->dialog) {
		gtk_widget_hide (info->dialog);
		gtk_widget_destroy (info->dialog);
	}

	g_free (info->secret_name);
}

static void
get_secrets_cb (GtkDialog *dialog,
                gint response,
                gpointer user_data)
{
	SecretsRequest *req = user_data;
	MobileHelperSecretsInfo *info = (MobileHelperSecretsInfo *) req;
	GError *error = NULL;

	if (response == GTK_RESPONSE_OK) {
		if (info->capability == NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS) {
			NMSettingGsm *setting;

			setting = nm_connection_get_setting_gsm (req->connection);
			if (setting) {
				g_object_set (G_OBJECT (setting),
				              info->secret_name, gtk_entry_get_text (info->secret_entry),
				              NULL);
			} else {
				error = g_error_new (NM_SECRET_AGENT_ERROR,
				                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
				                     "%s.%d (%s): no GSM setting",
				                     __FILE__, __LINE__, __func__);
			}
		} else if (info->capability == NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO) {
				NMSettingCdma *setting;

				setting = nm_connection_get_setting_cdma (req->connection);
				if (setting) {
					g_object_set (G_OBJECT (setting),
					              info->secret_name, gtk_entry_get_text (info->secret_entry),
					              NULL);
				} else {
					error = g_error_new (NM_SECRET_AGENT_ERROR,
					                     NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
					                     "%s.%d (%s): no CDMA setting",
					                     __FILE__, __LINE__, __func__);
				}
		} else
			g_assert_not_reached ();
	} else {
		error = g_error_new (NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_USER_CANCELED,
		                     "%s.%d (%s): canceled",
		                     __FILE__, __LINE__, __func__);
	}

	if (info->capability == NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS)
		applet_secrets_request_complete_setting (req, NM_SETTING_GSM_SETTING_NAME, error);
	else if (info->capability == NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)
		applet_secrets_request_complete_setting (req, NM_SETTING_CDMA_SETTING_NAME, error);
	else
		g_assert_not_reached ();

	applet_secrets_request_free (req);
	g_clear_error (&error);
}

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

static GtkWidget *
ask_for_pin (GtkEntry **out_secret_entry)
{
	GtkDialog *dialog;
	GtkWidget *w = NULL, *ok_button = NULL;
	GtkBox *box = NULL, *vbox = NULL;

	dialog = GTK_DIALOG (gtk_dialog_new ());
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("PIN code required"));

	ok_button = gtk_dialog_add_button (dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	ok_button = gtk_dialog_add_button (dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default (GTK_WINDOW (dialog), ok_button);

	vbox = GTK_BOX (gtk_dialog_get_content_area (dialog));

	w = gtk_label_new (_("PIN code is needed for the mobile broadband device"));
	gtk_box_pack_start (vbox, w, TRUE, TRUE, 0);

	w = gtk_alignment_new (0.5, 0.5, 0, 1.0);
	gtk_box_pack_start (vbox, w, TRUE, TRUE, 0);

#if GTK_CHECK_VERSION(3,1,6)
	box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6));
#else
	box = GTK_BOX (gtk_hbox_new (FALSE, 6));
#endif
	gtk_container_set_border_width (GTK_CONTAINER (box), 6);
	gtk_container_add (GTK_CONTAINER (w), GTK_WIDGET (box));

	gtk_box_pack_start (box, gtk_label_new ("PIN:"), FALSE, FALSE, 0);

	w = gtk_entry_new ();
	*out_secret_entry = GTK_ENTRY (w);
	gtk_entry_set_max_length (GTK_ENTRY (w), 8);
	gtk_entry_set_width_chars (GTK_ENTRY (w), 8);
	gtk_entry_set_activates_default (GTK_ENTRY (w), TRUE);
	gtk_entry_set_visibility (GTK_ENTRY (w), FALSE);
	gtk_box_pack_start (box, w, FALSE, FALSE, 0);
	g_signal_connect (w, "changed", G_CALLBACK (pin_entry_changed), ok_button);
	pin_entry_changed (GTK_EDITABLE (w), ok_button);

	gtk_widget_show_all (GTK_WIDGET (vbox));
	return GTK_WIDGET (dialog);
}

gboolean
mobile_helper_get_secrets (NMDeviceModemCapabilities capabilities,
                           SecretsRequest *req,
                           GError **error)
{
	MobileHelperSecretsInfo *info = (MobileHelperSecretsInfo *) req;
	GtkWidget *widget;
	GtkEntry *secret_entry = NULL;

	applet_secrets_request_set_free_func (req, free_secrets_info);

	if (!req->hints || !g_strv_length (req->hints)) {
		g_set_error (error,
		             NM_SECRET_AGENT_ERROR,
		             NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): missing secrets hints.",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}
	info->secret_name = g_strdup (req->hints[0]);

	/* Convert the input capabilities mask into a single value */
	if (capabilities & NM_DEVICE_MODEM_CAPABILITY_LTE)
		/* All LTE modems treated as GSM/UMTS for the settings */
		info->capability = NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS;
	else if (capabilities & NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS)
		info->capability = NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS;
	else if (capabilities & NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)
		info->capability = NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO;
	else {
		g_set_error (error,
		             NM_SECRET_AGENT_ERROR,
		             NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): unknown modem capabilities (0x%X).",
		             __FILE__, __LINE__, __func__, capabilities);
		return FALSE;
	}

	if (!strcmp (info->secret_name, NM_SETTING_GSM_PIN)) {
		widget = ask_for_pin (&secret_entry);
	} else if (!strcmp (info->secret_name, NM_SETTING_GSM_PASSWORD) ||
	           !strcmp (info->secret_name, NM_SETTING_CDMA_PASSWORD))
		widget = applet_mobile_password_dialog_new (req->connection, &secret_entry);
	else {
		g_set_error (error,
		             NM_SECRET_AGENT_ERROR,
		             NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): unknown secrets hint '%s'.",
		             __FILE__, __LINE__, __func__, info->secret_name);
		return FALSE;
	}
	info->dialog = widget;
	info->secret_entry = secret_entry;

	if (!widget || !secret_entry) {
		g_set_error (error,
		             NM_SECRET_AGENT_ERROR,
		             NM_SECRET_AGENT_ERROR_INTERNAL_ERROR,
		             "%s.%d (%s): error asking for mobile secrets.",
		             __FILE__, __LINE__, __func__);
		return FALSE;
	}

	g_signal_connect (widget, "response", G_CALLBACK (get_secrets_cb), info);

	gtk_window_set_position (GTK_WINDOW (widget), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_realize (GTK_WIDGET (widget));
	gtk_window_present (GTK_WINDOW (widget));

	return TRUE;
}
