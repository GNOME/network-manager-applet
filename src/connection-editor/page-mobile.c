/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-setting-serial.h>
#include <nm-setting-ppp.h>

#include "page-mobile.h"
#include "nm-connection-editor.h"
#include "gconf-helpers.h"
#include "mobile-wizard.h"

G_DEFINE_TYPE (CEPageMobile, ce_page_mobile, CE_TYPE_PAGE)

#define CE_PAGE_MOBILE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_MOBILE, CEPageMobilePrivate))

typedef struct {
	NMSetting *setting;

	/* Common to GSM and CDMA */
	GtkEntry *number;
	GtkEntry *username;
	GtkEntry *password;

	/* GSM only */
	GtkEntry *apn;
	GtkButton *apn_button;
	GtkEntry *network_id;
	GtkComboBox *network_type;
	GtkComboBox *band;
	GtkEntry *pin;

	GtkWindowGroup *window_group;
	gboolean window_added;

	gboolean disposed;
} CEPageMobilePrivate;

#define NET_TYPE_ANY         0
#define NET_TYPE_3G          1
#define NET_TYPE_2G          2
#define NET_TYPE_PREFER_3G   3
#define NET_TYPE_PREFER_2G   4

static void
mobile_private_init (CEPageMobile *self)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	GladeXML *xml;

	xml = CE_PAGE (self)->xml;

	priv->number = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_number"));
	priv->username = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_username"));
	priv->password = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_password"));

	priv->apn = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_apn"));
	priv->apn_button = GTK_BUTTON (glade_xml_get_widget (xml, "mobile_apn_button"));
	priv->network_id = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_network_id"));
	priv->network_type = GTK_COMBO_BOX (glade_xml_get_widget (xml, "mobile_network_type"));
	priv->band = GTK_COMBO_BOX (glade_xml_get_widget (xml, "mobile_band"));

	priv->pin = GTK_ENTRY (glade_xml_get_widget (xml, "mobile_pin"));

	priv->window_group = gtk_window_group_new ();
}

static void
populate_gsm_ui (CEPageMobile *self, NMConnection *connection)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	NMSettingGsm *setting = NM_SETTING_GSM (priv->setting);
	int type_idx;
	GtkWidget *widget;
	const char *s;

	s = nm_setting_gsm_get_number (setting);
	if (s)
		gtk_entry_set_text (priv->number, s);

	s = nm_setting_gsm_get_username (setting);
	if (s)
		gtk_entry_set_text (priv->username, s);

	s = nm_setting_gsm_get_apn (setting);
	if (s)
		gtk_entry_set_text (priv->apn, s);

	s = nm_setting_gsm_get_network_id (setting);
	if (s)
		gtk_entry_set_text (priv->network_id, s);

	switch (nm_setting_gsm_get_network_type (setting)) {
	case NM_GSM_NETWORK_UMTS_HSPA:
		type_idx = NET_TYPE_3G;
		break;
	case NM_GSM_NETWORK_GPRS_EDGE:
		type_idx = NET_TYPE_2G;
		break;
	case NM_GSM_NETWORK_PREFER_UMTS_HSPA:
		type_idx = NET_TYPE_PREFER_3G;
		break;
	case NM_GSM_NETWORK_PREFER_GPRS_EDGE:
		type_idx = NET_TYPE_PREFER_2G;
		break;
	case NM_GSM_NETWORK_ANY:
	default:
		type_idx = NET_TYPE_ANY;
		break;
	}
	gtk_combo_box_set_active (priv->network_type, type_idx);

	/* Hide network type widgets; not supported yet */
	gtk_widget_hide (GTK_WIDGET (priv->network_type));
	widget = glade_xml_get_widget (CE_PAGE (self)->xml, "type_label");
	gtk_widget_hide (widget);

	/* Hide Band widgets; not supported yet */
	widget = glade_xml_get_widget (CE_PAGE (self)->xml, "mobile_band");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (CE_PAGE (self)->xml, "band_label");
	gtk_widget_hide (widget);

	s = nm_setting_gsm_get_password (setting);
	if (s)
		gtk_entry_set_text (priv->password, s);

	s = nm_setting_gsm_get_pin (setting);
	if (s)
		gtk_entry_set_text (priv->pin, s);

	s = nm_setting_gsm_get_puk (setting);
	if (s)
		gtk_entry_set_text (priv->pin, s);
}

static void
populate_cdma_ui (CEPageMobile *self, NMConnection *connection)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	NMSettingCdma *setting = NM_SETTING_CDMA (priv->setting);
	const char *s;

	s = nm_setting_cdma_get_number (setting);
	if (s)
		gtk_entry_set_text (priv->number, s);

	s = nm_setting_cdma_get_username (setting);
	if (s)
		gtk_entry_set_text (priv->username, s);

	s = nm_setting_cdma_get_password (setting);
	if (s)
		gtk_entry_set_text (priv->password, s);

	/* Hide GSM specific widgets */
	gtk_widget_hide (glade_xml_get_widget (CE_PAGE (self)->xml, "mobile_basic_label"));
	gtk_widget_hide (glade_xml_get_widget (CE_PAGE (self)->xml, "mobile_advanced_vbox"));
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
show_passwords (GtkToggleButton *button, gpointer user_data)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (user_data);
	gboolean active;

	active = gtk_toggle_button_get_active (button);

	gtk_entry_set_visibility (priv->password, active);
	gtk_entry_set_visibility (priv->pin, active);
}

static void
apn_button_mobile_wizard_done (MobileWizard *wizard,
                               gboolean canceled,
                               MobileWizardAccessMethod *method,
                               gpointer user_data)
{
	CEPageMobile *self = CE_PAGE_MOBILE (user_data);
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	if (canceled || !method) {
		mobile_wizard_destroy (wizard);
		return;
	}

	if (!canceled && method) {
		switch (method->devtype) {
		case NM_DEVICE_TYPE_GSM:
			gtk_entry_set_text (GTK_ENTRY (priv->username),
			                    method->username ? method->username : "");
			gtk_entry_set_text (GTK_ENTRY (priv->password),
			                    method->password ? method->password : "");
			gtk_entry_set_text (GTK_ENTRY (priv->apn),
			                    method->gsm_apn ? method->gsm_apn : "");
			break;
		default:
			g_assert_not_reached ();
			break;
		}
	}

	mobile_wizard_destroy (wizard);
}

static void
apn_button_clicked (GtkButton *button, gpointer user_data)
{
	CEPageMobile *self = CE_PAGE_MOBILE (user_data);
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	MobileWizard *wizard;
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_toplevel (CE_PAGE (self)->page);
	g_return_if_fail (GTK_WIDGET_TOPLEVEL (toplevel));

	if (!priv->window_added) {
		gtk_window_group_add_window (priv->window_group, GTK_WINDOW (toplevel));
		priv->window_added = TRUE;
	}

	wizard = mobile_wizard_new (GTK_WINDOW (toplevel),
	                            priv->window_group,
	                            NM_DEVICE_TYPE_GSM,
	                            FALSE,
	                            apn_button_mobile_wizard_done,
	                            self);
	if (wizard)
		mobile_wizard_present (wizard);
}

static void
finish_setup (CEPageMobile *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPage *parent = CE_PAGE (self);
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	if (error)
		return;

	if (NM_IS_SETTING_GSM (priv->setting))
		populate_gsm_ui (self, parent->connection);
	else if (NM_IS_SETTING_CDMA (priv->setting))
		populate_cdma_ui (self, parent->connection);
	else
		g_assert_not_reached ();

	g_signal_connect (priv->number, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->username, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->password, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->apn, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->apn_button, "clicked", G_CALLBACK (apn_button_clicked), self);
	g_signal_connect (priv->network_id, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->network_type, "changed", G_CALLBACK (stuff_changed), self);
	g_signal_connect (priv->pin, "changed", G_CALLBACK (stuff_changed), self);

	g_signal_connect (glade_xml_get_widget (parent->xml, "mobile_show_passwords"),
	                  "toggled",
	                  G_CALLBACK (show_passwords),
	                  self);
}

CEPage *
ce_page_mobile_new (NMConnection *connection, GtkWindow *parent_window, GError **error)
{
	CEPageMobile *self;
	CEPageMobilePrivate *priv;
	CEPage *parent;
	const char *setting_name = NM_SETTING_GSM_SETTING_NAME;

	self = CE_PAGE_MOBILE (g_object_new (CE_TYPE_PAGE_MOBILE,
	                                     CE_PAGE_CONNECTION, connection,
	                                     CE_PAGE_PARENT_WINDOW, parent_window,
	                                     NULL));
	parent = CE_PAGE (self);

	parent->xml = glade_xml_new (GLADEDIR "/ce-page-mobile.glade", "MobilePage", NULL);
	if (!parent->xml) {
		g_set_error (error, 0, 0, "%s", _("Could not load mobile broadband user interface."));
		g_object_unref (self);
		return NULL;
	}

	parent->page = glade_xml_get_widget (parent->xml, "MobilePage");
	if (!parent->page) {
		g_set_error (error, 0, 0, "%s", _("Could not load mobile broadband user interface."));
		g_object_unref (self);
		return NULL;
	}
	g_object_ref_sink (parent->page);

	parent->title = g_strdup (_("Mobile Broadband"));

	mobile_private_init (self);
	priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	priv->setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_GSM);
	if (!priv->setting) {
		priv->setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_CDMA);
		setting_name = NM_SETTING_CDMA_SETTING_NAME;
	}

	if (!priv->setting) {
		g_set_error (error, 0, 0, "%s", _("Unsupported mobile broadband connection type."));
		g_object_unref (self);
		return NULL;
	}

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);
	if (!ce_page_initialize (parent, setting_name, error)) {
		g_object_unref (self);
		return NULL;
	}

	return CE_PAGE (self);
}

static const char *
nm_entry_get_text (GtkEntry *entry)
{
	const char *txt;

	txt = gtk_entry_get_text (entry);
	if (txt && strlen (txt) > 0)
		return txt;

	return NULL;
}

static void
gsm_ui_to_setting (CEPageMobile *self)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);
	int net_type;

	switch (gtk_combo_box_get_active (priv->network_type)) {
	case NET_TYPE_3G:
		net_type = NM_GSM_NETWORK_UMTS_HSPA;
		break;
	case NET_TYPE_2G:
		net_type = NM_GSM_NETWORK_GPRS_EDGE;
		break;
	case NET_TYPE_PREFER_3G:
		net_type = NM_GSM_NETWORK_PREFER_UMTS_HSPA;
		break;
	case NET_TYPE_PREFER_2G:
		net_type = NM_GSM_NETWORK_PREFER_GPRS_EDGE;
		break;
	case NET_TYPE_ANY:
	default:
		net_type = NM_GSM_NETWORK_ANY;
		break;
	}

	g_object_set (priv->setting,
				  NM_SETTING_GSM_NUMBER,   nm_entry_get_text (priv->number),
				  NM_SETTING_GSM_USERNAME, nm_entry_get_text (priv->username),
				  NM_SETTING_GSM_PASSWORD, nm_entry_get_text (priv->password),
				  NM_SETTING_GSM_APN, nm_entry_get_text (priv->apn),
				  NM_SETTING_GSM_NETWORK_ID, nm_entry_get_text (priv->network_id),
				  NM_SETTING_GSM_NETWORK_TYPE, net_type,
				  NM_SETTING_GSM_PIN, nm_entry_get_text (priv->pin),
				  NULL);
}

static void
cdma_ui_to_setting (CEPageMobile *self)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	g_object_set (priv->setting,
				  NM_SETTING_CDMA_NUMBER,   nm_entry_get_text (priv->number),
				  NM_SETTING_CDMA_USERNAME, nm_entry_get_text (priv->username),
				  NM_SETTING_CDMA_PASSWORD, nm_entry_get_text (priv->password),
				  NULL);
}

static void
ui_to_setting (CEPageMobile *self)
{
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	if (NM_IS_SETTING_GSM (priv->setting))
		gsm_ui_to_setting (self);
	else if (NM_IS_SETTING_CDMA (priv->setting))
		cdma_ui_to_setting (self);
	else
		g_error ("Invalid setting");
}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageMobile *self = CE_PAGE_MOBILE (page);
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	ui_to_setting (self);
	return nm_setting_verify (priv->setting, NULL, error);
}

static void
dispose (GObject *object)
{
	CEPageMobile *self = CE_PAGE_MOBILE (object);
	CEPageMobilePrivate *priv = CE_PAGE_MOBILE_GET_PRIVATE (self);

	if (priv->window_group)
		g_object_unref (priv->window_group);

	G_OBJECT_CLASS (ce_page_mobile_parent_class)->dispose (object);
}

static void
ce_page_mobile_init (CEPageMobile *self)
{
}

static void
ce_page_mobile_class_init (CEPageMobileClass *mobile_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (mobile_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (mobile_class);

	g_type_class_add_private (object_class, sizeof (CEPageMobilePrivate));

	/* virtual methods */
	parent_class->validate = validate;
	object_class->dispose = dispose;
}

static void
add_default_serial_setting (NMConnection *connection)
{
	NMSettingSerial *s_serial;

	s_serial = NM_SETTING_SERIAL (nm_setting_serial_new ());
	g_object_set (s_serial,
	              NM_SETTING_SERIAL_BAUD, 115200,
	              NM_SETTING_SERIAL_BITS, 8,
	              NM_SETTING_SERIAL_PARITY, 'n',
	              NM_SETTING_SERIAL_STOPBITS, 1,
	              NULL);

	nm_connection_add_setting (connection, NM_SETTING (s_serial));
}

typedef struct {
    PageNewConnectionResultFunc result_func;
    PageGetConnectionsFunc get_connections_func;
    gpointer user_data;
} WizardInfo;

static void
new_connection_mobile_wizard_done (MobileWizard *wizard,
                                   gboolean canceled,
                                   MobileWizardAccessMethod *method,
                                   gpointer user_data)
{
	WizardInfo *info = user_data;
	NMConnection *connection = NULL;

	if (!canceled && method) {
		NMSetting *type_setting;
		const char *ctype = NULL;
		char *detail = NULL;

		switch (method->devtype) {
		case NM_DEVICE_TYPE_GSM:
			ctype = NM_SETTING_GSM_SETTING_NAME;
			type_setting = nm_setting_gsm_new ();
			/* De-facto standard for GSM */
			g_object_set (type_setting,
			              NM_SETTING_GSM_NUMBER, "*99#",
			              NM_SETTING_GSM_USERNAME, method->username,
			              NM_SETTING_GSM_PASSWORD, method->password,
			              NM_SETTING_GSM_APN, method->gsm_apn,
			              NULL);
			break;
		case NM_DEVICE_TYPE_CDMA:
			ctype = NM_SETTING_CDMA_SETTING_NAME;
			type_setting = nm_setting_cdma_new ();
			/* De-facto standard for CDMA */
			g_object_set (type_setting,
			              NM_SETTING_CDMA_NUMBER, "#777",
			              NM_SETTING_GSM_USERNAME, method->username,
			              NM_SETTING_GSM_PASSWORD, method->password,
			              NULL);
			break;
		default:
			g_assert_not_reached ();
			break;
		}

		if (method->plan_name)
			detail = g_strdup_printf ("%s %s %%d", method->provider_name, method->plan_name);
		else
			detail = g_strdup_printf ("%s connection %%d", method->provider_name);
		connection = ce_page_new_connection (detail, ctype, FALSE, info->get_connections_func, info->user_data);
		g_free (detail);

		nm_connection_add_setting (connection, type_setting);
		add_default_serial_setting (connection);
		nm_connection_add_setting (connection, nm_setting_ppp_new ());
	}

	(*info->result_func) (connection, canceled, NULL, info->user_data);

	if (wizard)
		mobile_wizard_destroy (wizard);
	g_free (info);
}

static void
cancel_dialog (GtkDialog *dialog)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_CANCEL);
}

void
mobile_connection_new (GtkWindow *parent,
                       PageNewConnectionResultFunc result_func,
                       PageGetConnectionsFunc get_connections_func,
                       gpointer user_data)
{
	MobileWizard *wizard;
	WizardInfo *info;
	GtkWidget *dialog, *vbox, *gsm_radio, *cdma_radio, *label, *content, *alignment;
	GtkWidget *hbox, *image;
	gint response;
	MobileWizardAccessMethod method;

	info = g_malloc0 (sizeof (WizardInfo));
	info->result_func = result_func;
	info->get_connections_func = get_connections_func;
	info->user_data = user_data;

	wizard = mobile_wizard_new (parent, NULL, NM_DEVICE_TYPE_UNKNOWN, FALSE,
	                            new_connection_mobile_wizard_done, info);
	if (wizard) {
		mobile_wizard_present (wizard);
		return;
	}

	/* Fall back to just asking for GSM vs. CDMA */
	dialog = gtk_dialog_new_with_buttons (_("Select Mobile Broadband Provider Type"),
	                                      parent,
	                                      GTK_DIALOG_MODAL,
	                                      GTK_STOCK_CANCEL,
	                                      GTK_RESPONSE_CANCEL,
	                                      GTK_STOCK_OK,
	                                      GTK_RESPONSE_OK,
	                                      NULL);
	g_signal_connect (dialog, "delete-event", G_CALLBACK (cancel_dialog), NULL);
	gtk_window_set_icon_name (GTK_WINDOW (dialog), "nm-device-wwan");

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	alignment = gtk_alignment_new (0, 0, 0.5, 0.5);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 12, 12, 12, 12);
	gtk_box_pack_start (GTK_BOX (content), alignment, TRUE, FALSE, 6);

	hbox = gtk_hbox_new (FALSE, 6);
	gtk_container_add (GTK_CONTAINER (alignment), hbox);

	image = gtk_image_new_from_icon_name ("nm-device-wwan", GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0);
	gtk_misc_set_padding (GTK_MISC (image), 0, 6);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 6);

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, FALSE, 0);

	label = gtk_label_new (_("Select the technology your mobile broadband provider uses.  If you are unsure, ask your provider."));
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 12);

	gsm_radio = gtk_radio_button_new_with_mnemonic (NULL, _("My provider uses _GSM-based technology (i.e. GPRS, EDGE, UMTS, HSDPA)"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gsm_radio), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), gsm_radio, FALSE, FALSE, 6);

	cdma_radio = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (gsm_radio),
                                           _("My provider uses _CDMA-based technology (i.e. 1xRTT, EVDO)"));
	gtk_box_pack_start (GTK_BOX (vbox), cdma_radio, FALSE, FALSE, 6);

	gtk_widget_show_all (dialog);

	memset (&method, 0, sizeof (method));
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_OK) {
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cdma_radio))) {
			method.devtype = NM_DEVICE_TYPE_CDMA;
			method.provider_name = _("CDMA");
		} else {
			method.devtype = NM_DEVICE_TYPE_GSM;
			method.provider_name = _("GSM");
		}
	}
	gtk_widget_destroy (dialog);

	new_connection_mobile_wizard_done (NULL,
	                                   (response != GTK_RESPONSE_OK),
	                                   (response == GTK_RESPONSE_OK) ? &method : NULL,
	                                   info);
}

