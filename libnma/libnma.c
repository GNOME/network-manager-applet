/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <NetworkManager.h>
#include <iwlib.h>
#include <glib/gi18n.h>
#include "libnma.h"

GtkTreeModel *
wso_wpa_create_key_type_model (int capabilities, gboolean wpa_eap, int *num_added)
{
	GtkListStore *	model;
	GtkTreeIter	iter;
	int			num = 1;
	const char *	name;

	g_return_val_if_fail (num_added != NULL, NULL);

	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

	name = _("Automatic (Default)");
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
					WPA_KEY_TYPE_CIPHER_COL, NM_AUTH_TYPE_WPA_PSK_AUTO, -1);

	if (capabilities & NM_802_11_CAP_CIPHER_CCMP)
	{
		name = _("AES-CCMP");
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
			WPA_KEY_TYPE_CIPHER_COL, IW_AUTH_CIPHER_CCMP, -1);
		num++;
	}
	if (capabilities & NM_802_11_CAP_CIPHER_TKIP)
	{
		name = _("TKIP");
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
			WPA_KEY_TYPE_CIPHER_COL, IW_AUTH_CIPHER_TKIP, -1);
		num++;
	}
	if (wpa_eap && capabilities & NM_802_11_CAP_KEY_MGMT_802_1X)
	{
		name = _("Dynamic WEP");
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
			WPA_KEY_TYPE_CIPHER_COL, IW_AUTH_CIPHER_WEP104, -1);
		num++;
	}

	*num_added = num;
	return GTK_TREE_MODEL (model);
}

gboolean
wso_wpa_key_type_get_iter (GtkTreeModel *model, uint key_type, GtkTreeIter *iter)
{
	gboolean valid;

	valid = gtk_tree_model_get_iter_first (model, iter);
	while (valid) {
		int current;

		gtk_tree_model_get (model, iter, WPA_KEY_TYPE_CIPHER_COL, &current, -1);
		if (current == key_type)
			return TRUE;

		valid = gtk_tree_model_iter_next (model, iter);
	}

	iter = NULL;
	return FALSE;
}

GtkTreeModel *
wso_wpa_create_phase2_type_model (int capabilities, int *num_added)
{
	GtkListStore *	model;
	GtkTreeIter	iter;
	const char *	name;

	g_return_val_if_fail (num_added != NULL, NULL);

	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

	name = _("None (Default)");
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
					WPA_KEY_TYPE_CIPHER_COL, NM_PHASE2_AUTH_NONE, -1);

	name = _("PAP");
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
					WPA_KEY_TYPE_CIPHER_COL, NM_PHASE2_AUTH_PAP, -1);

	name = _("MSCHAP");
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
					WPA_KEY_TYPE_CIPHER_COL, NM_PHASE2_AUTH_MSCHAP, -1);

	name = _("MSCHAPV2");
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
					WPA_KEY_TYPE_CIPHER_COL, NM_PHASE2_AUTH_MSCHAPV2, -1);

	name = _("GTC");
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, WPA_KEY_TYPE_NAME_COL, name,
					WPA_KEY_TYPE_CIPHER_COL, NM_PHASE2_AUTH_GTC, -1);

	*num_added = 5;
	return GTK_TREE_MODEL (model);
}

gboolean
wso_wpa_phase2_type_get_iter (GtkTreeModel *model, uint phase2_type, GtkTreeIter *iter)
{
	gboolean valid;

	valid = gtk_tree_model_get_iter_first (model, iter);
	while (valid) {
		int current;

		gtk_tree_model_get (model, iter, WPA_KEY_TYPE_CIPHER_COL, &current, -1);
		if (current == phase2_type)
			return TRUE;

		valid = gtk_tree_model_iter_next (model, iter);
	}

	iter = NULL;
	return FALSE;
}

GtkTreeModel *
wso_wpa_create_eap_method_model (void)
{
	GtkListStore *	model;
	GtkTreeIter	iter;
	struct {
		const char *		name;
		int				value;
	} *list, eap_method_list[] = {
		{ _("PEAP"),		NM_EAP_METHOD_PEAP },
		{ _("TLS"),		NM_EAP_METHOD_TLS },
		{ _("TTLS"),		NM_EAP_METHOD_TTLS },
		{ NULL,			0 }
	};

	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
	list = eap_method_list;
	while (list->name) {
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, WPA_EAP_NAME_COL, list->name, WPA_EAP_VALUE_COL, list->value, -1);
		list++;
	}

	return GTK_TREE_MODEL (model);
}

gboolean
wso_wpa_eap_method_get_iter (GtkTreeModel *model, uint eap_method, GtkTreeIter *iter)
{
	gboolean valid;

	valid = gtk_tree_model_get_iter_first (model, iter);
	while (valid) {
		int current;

		gtk_tree_model_get (model, iter, WPA_EAP_VALUE_COL, &current, -1);
		if (current == eap_method)
			return TRUE;

		valid = gtk_tree_model_iter_next (model, iter);
	}

	iter = NULL;
	return FALSE;
}

/* LEAP */

GtkTreeModel *
wso_leap_create_key_mgmt_model (void)
{
	GtkListStore *model;
	GtkTreeIter iter;

	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, LEAP_KEY_MGMT_NAME_COL, "IEEE 802.1X",
					LEAP_KEY_MGMT_VALUE_COL, "IEEE8021X", -1);
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, LEAP_KEY_MGMT_NAME_COL, "WPA-EAP",
					LEAP_KEY_MGMT_VALUE_COL, "WPA-EAP", -1);

	return GTK_TREE_MODEL (model);
}

gboolean
wso_leap_key_mgmt_get_iter (GtkTreeModel *model, const char *key_mgmt, GtkTreeIter *iter)
{
	gboolean valid;

	g_return_val_if_fail (key_mgmt != NULL, FALSE);

	valid = gtk_tree_model_get_iter_first (model, iter);
	while (valid) {
		char *current;

		gtk_tree_model_get (model, iter, LEAP_KEY_MGMT_VALUE_COL, &current, -1);
		if (current && !strcmp (current, key_mgmt)) {
			g_free (current);
			return TRUE;
		}

		g_free (current);

		valid = gtk_tree_model_iter_next (model, iter);
	}

	iter = NULL;
	return FALSE;
}
