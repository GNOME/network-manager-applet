// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * Copyright 2008 - 2014 Red Hat, Inc.
 */

#include "nm-default.h"

#include <string.h>

#include "nma-ws.h"
#include "page-wifi.h"
#include "page-wifi-security.h"
#include "nm-connection-editor.h"

G_DEFINE_TYPE (CEPageWifiSecurity, ce_page_wifi_security, CE_TYPE_PAGE)

#define CE_PAGE_WIFI_SECURITY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_WIFI_SECURITY, CEPageWifiSecurityPrivate))

typedef struct {
	GtkSizeGroup *group;
	GtkComboBox *security_combo;
	NM80211Mode mode;
} CEPageWifiSecurityPrivate;

#define S_NAME_COLUMN   0
#define S_SEC_COLUMN    1
#define S_ADHOC_VALID_COLUMN  2
#define S_HOTSPOT_VALID_COLUMN  3

static const char *known_wsec_props[] = {
	NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
	NM_SETTING_WIRELESS_SECURITY_WEP_TX_KEYIDX,
	NM_SETTING_WIRELESS_SECURITY_AUTH_ALG,
	NM_SETTING_WIRELESS_SECURITY_PROTO,
	NM_SETTING_WIRELESS_SECURITY_PAIRWISE,
	NM_SETTING_WIRELESS_SECURITY_GROUP,
	NM_SETTING_WIRELESS_SECURITY_LEAP_USERNAME,
	NM_SETTING_WIRELESS_SECURITY_WEP_KEY0,
	NM_SETTING_WIRELESS_SECURITY_WEP_KEY1,
	NM_SETTING_WIRELESS_SECURITY_WEP_KEY2,
	NM_SETTING_WIRELESS_SECURITY_WEP_KEY3,
	NM_SETTING_WIRELESS_SECURITY_WEP_KEY_FLAGS,
	NM_SETTING_WIRELESS_SECURITY_WEP_KEY_TYPE,
	NM_SETTING_WIRELESS_SECURITY_PSK,
	NM_SETTING_WIRELESS_SECURITY_PSK_FLAGS,
	NM_SETTING_WIRELESS_SECURITY_LEAP_PASSWORD,
	NM_SETTING_WIRELESS_SECURITY_LEAP_PASSWORD_FLAGS,
	NULL
};

static const char *known_8021x_props[] = {
	NM_SETTING_802_1X_EAP,
	NM_SETTING_802_1X_IDENTITY,
	NM_SETTING_802_1X_ANONYMOUS_IDENTITY,
	NM_SETTING_802_1X_PAC_FILE,
	NM_SETTING_802_1X_CA_CERT,
	NM_SETTING_802_1X_CA_PATH,
	NM_SETTING_802_1X_CLIENT_CERT,
	NM_SETTING_802_1X_PHASE1_PEAPVER,
	NM_SETTING_802_1X_PHASE2_AUTH,
	NM_SETTING_802_1X_PHASE2_AUTHEAP,
	NM_SETTING_802_1X_PHASE2_CA_CERT,
	NM_SETTING_802_1X_PHASE2_CA_PATH,
	NM_SETTING_802_1X_PHASE2_CLIENT_CERT,
	NM_SETTING_802_1X_PASSWORD,
	NM_SETTING_802_1X_PASSWORD_FLAGS,
	NM_SETTING_802_1X_PRIVATE_KEY,
	NM_SETTING_802_1X_PRIVATE_KEY_PASSWORD,
	NM_SETTING_802_1X_PRIVATE_KEY_PASSWORD_FLAGS,
	NM_SETTING_802_1X_PHASE2_PRIVATE_KEY,
	NM_SETTING_802_1X_PHASE2_PRIVATE_KEY_PASSWORD,
	NM_SETTING_802_1X_PHASE2_PRIVATE_KEY_PASSWORD_FLAGS,
	NM_SETTING_802_1X_DOMAIN_SUFFIX_MATCH,
	NM_SETTING_802_1X_PHASE2_DOMAIN_SUFFIX_MATCH,
	NULL
};

static gboolean
find_proto (NMSettingWirelessSecurity *sec, const char *item)
{
	guint32 i;

	for (i = 0; i < nm_setting_wireless_security_get_num_protos (sec); i++) {
		if (!strcmp (item, nm_setting_wireless_security_get_proto (sec, i)))
			return TRUE;
	}
	return FALSE;
}

static NMUtilsSecurityType
get_default_type_for_security (NMSettingWirelessSecurity *sec)
{
	const char *key_mgmt, *auth_alg;

	g_return_val_if_fail (sec != NULL, NMU_SEC_NONE);

	key_mgmt = nm_setting_wireless_security_get_key_mgmt (sec);
	auth_alg = nm_setting_wireless_security_get_auth_alg (sec);

	/* No IEEE 802.1X */
	if (!strcmp (key_mgmt, "none"))
		return NMU_SEC_STATIC_WEP;

	if (!strcmp (key_mgmt, "ieee8021x")) {
		if (auth_alg && !strcmp (auth_alg, "leap"))
			return NMU_SEC_LEAP;
		return NMU_SEC_DYNAMIC_WEP;
	}

	if (   !strcmp (key_mgmt, "wpa-none")
	    || !strcmp (key_mgmt, "wpa-psk")) {
		if (find_proto (sec, "rsn"))
			return NMU_SEC_WPA2_PSK;
		else if (find_proto (sec, "wpa"))
			return NMU_SEC_WPA_PSK;
		else
			return NMU_SEC_WPA_PSK;
	}

	if (!strcmp (key_mgmt, "wpa-eap")) {
		if (find_proto (sec, "rsn"))
			return NMU_SEC_WPA2_ENTERPRISE;
		else if (find_proto (sec, "wpa"))
			return NMU_SEC_WPA_ENTERPRISE;
		else
			return NMU_SEC_WPA_ENTERPRISE;
	}

#if NM_CHECK_VERSION(1,22,0)
	if (!strcmp (key_mgmt, "sae"))
		return NMU_SEC_SAE;
#endif

#if NM_CHECK_VERSION(1,24,0)
	if (!strcmp (key_mgmt, "owe"))
		return NMU_SEC_OWE;
#endif

	return NMU_SEC_INVALID;
}

static void
stuff_changed_cb (NMAWs *ws, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
wsec_size_group_clear (GtkSizeGroup *group)
{
	GSList *iter;

	g_return_if_fail (group != NULL);

	iter = gtk_size_group_get_widgets (group);
	while (iter) {
		gtk_size_group_remove_widget (group, GTK_WIDGET (iter->data));
		iter = gtk_size_group_get_widgets (group);
	}
}

static NMAWs *
wireless_security_combo_get_active (CEPageWifiSecurity *self)
{
	CEPageWifiSecurityPrivate *priv = CE_PAGE_WIFI_SECURITY_GET_PRIVATE (self);
	GtkTreeIter iter;
	GtkTreeModel *model;
	NMAWs *ws = NULL;

	model = gtk_combo_box_get_model (priv->security_combo);
	gtk_combo_box_get_active_iter (priv->security_combo, &iter);
	gtk_tree_model_get (model, &iter, S_SEC_COLUMN, &ws, -1);

	return ws;
}

static void
wireless_security_combo_changed (GtkComboBox *combo,
                                 gpointer user_data)
{
	CEPageWifiSecurity *self = CE_PAGE_WIFI_SECURITY (user_data);
	CEPageWifiSecurityPrivate *priv = CE_PAGE_WIFI_SECURITY_GET_PRIVATE (self);
	GtkWidget *vbox;
	GList *elt, *children;
	NMAWs *ws;

	vbox = GTK_WIDGET (gtk_builder_get_object (CE_PAGE (self)->builder, "wifi_security_vbox"));
	g_assert (vbox);

	wsec_size_group_clear (priv->group);

	/* Remove any previous wifi security widgets */
	children = gtk_container_get_children (GTK_CONTAINER (vbox));
	for (elt = children; elt; elt = g_list_next (elt))
		gtk_container_remove (GTK_CONTAINER (vbox), GTK_WIDGET (elt->data));

	ws = wireless_security_combo_get_active (self);
	if (ws) {
		GtkWidget *widget, *parent;

		parent = gtk_widget_get_parent (GTK_WIDGET (ws));
		if (parent)
			gtk_container_remove (GTK_CONTAINER (parent), GTK_WIDGET (ws));

		widget = GTK_WIDGET (gtk_builder_get_object (CE_PAGE (self)->builder, "wifi_security_combo_label"));
		gtk_size_group_add_widget (priv->group, widget);
		nma_ws_add_to_size_group (ws, priv->group);

		gtk_container_add (GTK_CONTAINER (vbox), GTK_WIDGET (ws));
		g_object_unref (ws);
	}

	ce_page_changed (CE_PAGE (self));
}

static void
add_security_item (CEPageWifiSecurity *self,
                   NMAWs *ws,
                   GtkListStore *model,
                   GtkTreeIter *iter,
                   const char *text,
                   gboolean adhoc_valid,
                   gboolean hotspot_valid)
{
	if (G_IS_INITIALLY_UNOWNED (ws))
		g_object_ref_sink (ws);
	g_signal_connect (ws, "ws-changed", G_CALLBACK (stuff_changed_cb), self);
	gtk_list_store_append (model, iter);
	gtk_list_store_set (model, iter,
	                    S_NAME_COLUMN, text,
	                    S_SEC_COLUMN, ws,
	                    S_ADHOC_VALID_COLUMN, adhoc_valid,
	                    S_HOTSPOT_VALID_COLUMN, hotspot_valid,
	                    -1);
	g_object_unref (ws);
}

static void
set_sensitive (GtkCellLayout *cell_layout,
               GtkCellRenderer *cell,
               GtkTreeModel *tree_model,
               GtkTreeIter *iter,
               gpointer data)
{
	NM80211Mode *mode = data;
	gboolean sensitive = TRUE;

	if (*mode == NM_802_11_MODE_ADHOC)
		gtk_tree_model_get (tree_model, iter, S_ADHOC_VALID_COLUMN, &sensitive, -1);
	else if (*mode == NM_802_11_MODE_AP)
		gtk_tree_model_get (tree_model, iter, S_HOTSPOT_VALID_COLUMN, &sensitive, -1);

	g_object_set (cell, "sensitive", sensitive, NULL);
}

static gboolean
security_valid (NMUtilsSecurityType sectype, NM80211Mode mode)
{
	guint32 dev_caps = 0;

	/* Fake some device capabilities here since we don't know about the
	 * NMDevice object to get the card's real capabilities.
	 */
	dev_caps =   NM_WIFI_DEVICE_CAP_CIPHER_WEP40
	           | NM_WIFI_DEVICE_CAP_CIPHER_WEP104
	           | NM_WIFI_DEVICE_CAP_CIPHER_TKIP
	           | NM_WIFI_DEVICE_CAP_CIPHER_CCMP
	           | NM_WIFI_DEVICE_CAP_WPA
	           | NM_WIFI_DEVICE_CAP_RSN;

	switch (mode) {
	case NM_802_11_MODE_AP:
#if NM_CHECK_VERSION(1,22,0)
		if (sectype == NMU_SEC_SAE)
			return TRUE;
#endif
		return nm_utils_ap_mode_security_valid (sectype, NM_WIFI_DEVICE_CAP_AP);
	case NM_802_11_MODE_ADHOC:
	case NM_802_11_MODE_INFRA:
	default:
		return nm_utils_security_valid (sectype,
		                                dev_caps,
		                                FALSE,
		                                (mode == NM_802_11_MODE_ADHOC),
		                                0, 0, 0);
	}
	g_assert_not_reached ();
}

static void
finish_setup (CEPageWifiSecurity *self, gpointer user_data)
{
	CEPage *parent = CE_PAGE (self);
	CEPageWifiSecurityPrivate *priv = CE_PAGE_WIFI_SECURITY_GET_PRIVATE (self);
	NMSettingWireless *s_wireless;
	NMSettingWirelessSecurity *s_wireless_sec;
	NMConnection *connection = parent->connection;
	NM80211Mode mode = NM_802_11_MODE_INFRA;
	GtkListStore *sec_model;
	GtkTreeIter iter;
	NMUtilsSecurityType default_type = NMU_SEC_NONE;
	int active = -1;
	int item = 0;
	GtkComboBox *combo;
	GtkCellRenderer *renderer;

	s_wireless = nm_connection_get_setting_wireless (connection);
	g_assert (s_wireless);

	if (!g_strcmp0 (nm_setting_wireless_get_mode (s_wireless), "adhoc"))
		mode = NM_802_11_MODE_ADHOC;
	else if (!g_strcmp0 (nm_setting_wireless_get_mode (s_wireless), "ap"))
		mode = NM_802_11_MODE_AP;

	s_wireless_sec = nm_connection_get_setting_wireless_security (connection);

	if (s_wireless_sec)
		default_type = get_default_type_for_security (s_wireless_sec);

	sec_model = gtk_list_store_new (4, G_TYPE_STRING, NMA_TYPE_WS, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);

	if (security_valid (NMU_SEC_NONE, mode)) {
		gtk_list_store_append (sec_model, &iter);
		gtk_list_store_set (sec_model, &iter,
		                    S_NAME_COLUMN, C_("Wi-Fi/Ethernet security", "None"),
		                    S_ADHOC_VALID_COLUMN, TRUE,
		                    S_HOTSPOT_VALID_COLUMN, TRUE,
		                    -1);
		if (default_type == NMU_SEC_NONE)
			active = item;
		item++;
	}

	if (security_valid (NMU_SEC_STATIC_WEP, mode)) {
		NMAWsWepKey *ws_wep;
		NMWepKeyType wep_type = NM_WEP_KEY_TYPE_KEY;

		if (default_type == NMU_SEC_STATIC_WEP) {
			NMSettingWirelessSecurity *s_wsec;

			s_wsec = nm_connection_get_setting_wireless_security (connection);
			if (s_wsec)
				wep_type = nm_setting_wireless_security_get_wep_key_type (s_wsec);
			if (wep_type == NM_WEP_KEY_TYPE_UNKNOWN)
				wep_type = NM_WEP_KEY_TYPE_KEY;
		}

		ws_wep = nma_ws_wep_key_new (connection, NM_WEP_KEY_TYPE_KEY, FALSE, FALSE);
		if (ws_wep) {
			add_security_item (self, NMA_WS (ws_wep), sec_model,
			                   &iter, _("WEP 40/128-bit Key (Hex or ASCII)"),
			                   TRUE, TRUE);
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP) && (wep_type == NM_WEP_KEY_TYPE_KEY))
				active = item;
			item++;
		}

		ws_wep = nma_ws_wep_key_new (connection, NM_WEP_KEY_TYPE_PASSPHRASE, FALSE, FALSE);
		if (ws_wep) {
			add_security_item (self, NMA_WS (ws_wep), sec_model,
			                   &iter, _("WEP 128-bit Passphrase"), TRUE, TRUE);
			if ((active < 0) && (default_type == NMU_SEC_STATIC_WEP) && (wep_type == NM_WEP_KEY_TYPE_PASSPHRASE))
				active = item;
			item++;
		}
	}

	if (security_valid (NMU_SEC_LEAP, mode)) {
		NMAWsLeap *ws_leap;

		ws_leap = nma_ws_leap_new (connection, FALSE);
		if (ws_leap) {
			add_security_item (self, NMA_WS (ws_leap), sec_model,
			                   &iter, _("LEAP"), FALSE, FALSE);
			if ((active < 0) && (default_type == NMU_SEC_LEAP))
				active = item;
			item++;
		}
	}

	if (security_valid (NMU_SEC_DYNAMIC_WEP, mode)) {
		NMAWsDynamicWep *ws_dynamic_wep;

		ws_dynamic_wep = nma_ws_dynamic_wep_new (connection, TRUE, FALSE);
		if (ws_dynamic_wep) {
			add_security_item (self, NMA_WS (ws_dynamic_wep), sec_model,
			                   &iter, _("Dynamic WEP (802.1X)"), FALSE, FALSE);
			if ((active < 0) && (default_type == NMU_SEC_DYNAMIC_WEP))
				active = item;
			item++;
		}
	}

	if (security_valid (NMU_SEC_WPA_PSK, mode) || security_valid (NMU_SEC_WPA2_PSK, mode)) {
		NMAWsWpaPsk *ws_wpa_psk;

		ws_wpa_psk = nma_ws_wpa_psk_new (connection, FALSE);
		if (ws_wpa_psk) {
			add_security_item (self, NMA_WS (ws_wpa_psk), sec_model,
			                   &iter, _("WPA & WPA2 Personal"), TRUE, TRUE);
			if ((active < 0) && ((default_type == NMU_SEC_WPA_PSK) || (default_type == NMU_SEC_WPA2_PSK)))
				active = item;
			item++;
		}
	}

	if (security_valid (NMU_SEC_WPA_ENTERPRISE, mode) || security_valid (NMU_SEC_WPA2_ENTERPRISE, mode)) {
		NMAWsWpaEap *ws_wpa_eap;

		ws_wpa_eap = nma_ws_wpa_eap_new (connection, TRUE, FALSE, NULL);
		if (ws_wpa_eap) {
			add_security_item (self, NMA_WS (ws_wpa_eap), sec_model,
			                   &iter, _("WPA & WPA2 Enterprise"), FALSE, FALSE);
			if ((active < 0) && ((default_type == NMU_SEC_WPA_ENTERPRISE) || (default_type == NMU_SEC_WPA2_ENTERPRISE)))
				active = item;
			item++;
		}
	}

#if NM_CHECK_VERSION(1,22,0)
	if (security_valid (NMU_SEC_SAE, mode)) {
		NMAWsSae *ws_sae;

		ws_sae = nma_ws_sae_new (connection, FALSE);
		if (ws_sae) {
			add_security_item (self, NMA_WS (ws_sae), sec_model,
			                   &iter, _("WPA3 Personal"), TRUE, TRUE);
			if ((active < 0) && ((default_type == NMU_SEC_SAE)))
				active = item;
			item++;
		}
	}
#endif

#if NM_CHECK_VERSION(1,24,0)
	if (security_valid (NMU_SEC_OWE, mode)) {
		gtk_list_store_append (sec_model, &iter);
		gtk_list_store_set (sec_model, &iter,
		                    S_NAME_COLUMN, _("Enhanced Open"),
		                    S_ADHOC_VALID_COLUMN, FALSE,
		                    S_HOTSPOT_VALID_COLUMN, TRUE,
		                    -1);
		if ((active < 0) && (default_type == NMU_SEC_OWE))
			active = item;
		item++;
	}
#endif

	combo = GTK_COMBO_BOX (gtk_builder_get_object (parent->builder, "wifi_security_combo"));
	gtk_combo_box_set_model (combo, GTK_TREE_MODEL (sec_model));
	gtk_cell_layout_clear (GTK_CELL_LAYOUT (combo));

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), renderer, "text", S_NAME_COLUMN, NULL);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combo), renderer, set_sensitive, &priv->mode, NULL);

	gtk_combo_box_set_active (combo, active < 0 ? 0 : (guint32) active);
	g_object_unref (G_OBJECT (sec_model));

	priv->security_combo = combo;

	wireless_security_combo_changed (combo, self);
	g_signal_connect (combo, "changed",
	                  G_CALLBACK (wireless_security_combo_changed),
	                  self);
}

CEPage *
ce_page_wifi_security_new (NMConnectionEditor *editor,
                           NMConnection *connection,
                           GtkWindow *parent_window,
                           NMClient *client,
                           const char **out_secrets_setting_name,
                           GError **error)
{
	CEPageWifiSecurity *self;
	NMSettingWireless *s_wireless;
	NMSetting8021x *s_8021x;
	NMSettingWirelessSecurity *s_wsec = NULL;
	NMUtilsSecurityType default_type = NMU_SEC_NONE;

	s_wireless = nm_connection_get_setting_wireless (connection);
	if (!s_wireless) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Could not load Wi-Fi security user interface; missing Wi-Fi setting."));
		return NULL;
	}

	self = CE_PAGE_WIFI_SECURITY (ce_page_new (CE_TYPE_PAGE_WIFI_SECURITY,
	                                           editor,
	                                           connection,
	                                           parent_window,
	                                           client,
	                                           "/org/gnome/nm_connection_editor/ce-page-wifi-security.ui",
	                                           "WifiSecurityPage",
	                                           _("Wi-Fi Security")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Could not load Wi-Fi security user interface."));
		return NULL;
	}

	CE_PAGE_WIFI_SECURITY_GET_PRIVATE (self)->group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	s_wsec = nm_connection_get_setting_wireless_security (connection);
	s_8021x = nm_connection_get_setting_802_1x (connection);

	if (s_wsec)
		default_type = get_default_type_for_security (s_wsec);

	/* Get secrets if the connection is not 802.1X enabled */
	if (   default_type == NMU_SEC_STATIC_WEP
	    || default_type == NMU_SEC_LEAP
#if NM_CHECK_VERSION(1,22,0)
	    || default_type == NMU_SEC_SAE
#endif
	    || default_type == NMU_SEC_WPA_PSK
	    || default_type == NMU_SEC_WPA2_PSK) {
		*out_secrets_setting_name = NM_SETTING_WIRELESS_SECURITY_SETTING_NAME;
	}

	/* Or if it is 802.1X enabled */
	if (   default_type == NMU_SEC_DYNAMIC_WEP
	    || default_type == NMU_SEC_WPA_ENTERPRISE
	    || default_type == NMU_SEC_WPA2_ENTERPRISE) {
		*out_secrets_setting_name = NM_SETTING_802_1X_SETTING_NAME;
		nm_connection_editor_check_unsupported_properties (editor, (NMSetting *) s_8021x, known_8021x_props);
	}

	nm_connection_editor_check_unsupported_properties (editor, (NMSetting *) s_wsec, known_wsec_props);

	g_signal_connect (self, CE_PAGE_INITIALIZED, G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ce_page_wifi_security_init (CEPageWifiSecurity *self)
{
}

static void
dispose (GObject *object)
{
	CEPageWifiSecurityPrivate *priv = CE_PAGE_WIFI_SECURITY_GET_PRIVATE (object);

	g_clear_object (&priv->group);

	G_OBJECT_CLASS (ce_page_wifi_security_parent_class)->dispose (object);
}

static gboolean
ce_page_validate_v (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageWifiSecurity *self = CE_PAGE_WIFI_SECURITY (page);
	CEPageWifiSecurityPrivate *priv = CE_PAGE_WIFI_SECURITY_GET_PRIVATE (self);
	NMSettingWireless *s_wireless;
	NMAWs *ws;
	gboolean valid = FALSE;
	const char *mode;

	s_wireless = nm_connection_get_setting_wireless (connection);
	g_assert (s_wireless);

	mode = nm_setting_wireless_get_mode (s_wireless);
	if (g_strcmp0 (mode, NM_SETTING_WIRELESS_MODE_ADHOC) == 0)
		priv->mode = NM_802_11_MODE_ADHOC;
	else if (g_strcmp0 (mode, NM_SETTING_WIRELESS_MODE_AP) == 0)
		priv->mode = NM_802_11_MODE_AP;
	else
		priv->mode = NM_802_11_MODE_INFRA;

	ws = wireless_security_combo_get_active (self);
	if (ws) {
		GBytes *ssid = nm_setting_wireless_get_ssid (s_wireless);

		if (ssid) {
			valid = nma_ws_validate (ws, error);
			if (valid)
				nma_ws_fill_connection (ws, connection);
		} else {
			g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC, _("missing SSID"));
			valid = FALSE;
		}

		if (priv->mode == NM_802_11_MODE_ADHOC) {
			if (!nma_ws_adhoc_compatible (ws)) {
				g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Security not compatible with Ad-Hoc mode"));
				valid = FALSE;
			}
		}

		g_object_unref (ws);
	} else {
		/* No security, unencrypted */
		nm_connection_remove_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY);
		nm_connection_remove_setting (connection, NM_TYPE_SETTING_802_1X);
		valid = TRUE;
	}

	return valid;
}

static void
ce_page_wifi_security_class_init (CEPageWifiSecurityClass *wireless_security_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (wireless_security_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (wireless_security_class);

	g_type_class_add_private (object_class, sizeof (CEPageWifiSecurityPrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->ce_page_validate_v = ce_page_validate_v;
}
