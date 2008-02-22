/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

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
 * (C) Copyright 2005 Red Hat, Inc.
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>
#include <glade/glade.h>

#include "NetworkManager.h"
#include "wireless-security-manager.h"
#include "wireless-security-option.h"
#include "nm-utils.h"

#include "wso-none.h"
#include "wso-private.h"
#include "wso-wep-ascii.h"
#include "wso-wep-hex.h"
#include "wso-wep-passphrase.h"
#include "wso-wpa-eap.h"
#include "wso-wpa-psk.h"
#include "wso-leap.h"

struct WirelessSecurityManager
{
	char *	glade_file;
	GHashTable *options;
};


WirelessSecurityManager * wsm_new (const char * glade_file)
{
	WirelessSecurityManager *	wsm = NULL;

	g_return_val_if_fail (glade_file, NULL);

	wsm = g_malloc0 (sizeof (WirelessSecurityManager));
	wsm->glade_file = g_strdup (glade_file);
	wsm->options = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) wso_free);

	return wsm;
}


static gboolean
remove_all (gpointer key, gpointer value, gpointer user_data)
{
	return TRUE;
}

gboolean wsm_set_capabilities (WirelessSecurityManager *wsm, guint32 capabilities)
{
	WirelessSecurityOption *		opt;
	gboolean ret = TRUE;

	g_return_val_if_fail (wsm != NULL, FALSE);

	/* Free previous options */
	g_hash_table_foreach_remove (wsm->options, remove_all, NULL);

	if (capabilities & NM_802_11_CAP_PROTO_NONE)
	{
		opt = wso_none_new (wsm->glade_file);
		if (opt)
			g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_NONE), opt);
	}

	if (capabilities & NM_802_11_CAP_PROTO_WEP)
	{
		if ((opt = wso_wep_passphrase_new (wsm->glade_file)))
			g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_WEP_PASSPHRASE), opt);

		if ((opt = wso_wep_hex_new (wsm->glade_file)))
			g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_WEP_HEX), opt);

		if ((opt = wso_wep_ascii_new (wsm->glade_file)))
			g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_WEP_ASCII), opt);
	}

	if (capabilities & NM_802_11_CAP_PROTO_WPA)
	{
		if (capabilities & NM_802_11_CAP_KEY_MGMT_802_1X)
		{
			if ((opt = wso_wpa_eap_new (wsm->glade_file, capabilities, FALSE)))
				g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_WPA_EAP), opt);
		}
		if (capabilities & NM_802_11_CAP_KEY_MGMT_PSK)
		{
			if ((opt = wso_wpa_psk_new (wsm->glade_file, capabilities, FALSE)))
				g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_WPA_PSK), opt);
		}
	}

	if (capabilities & NM_802_11_CAP_PROTO_WPA2)
	{
		if (capabilities & NM_802_11_CAP_KEY_MGMT_802_1X)
		{
			if ((opt = wso_wpa_eap_new (wsm->glade_file, capabilities, TRUE)))
				g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_WPA2_EAP), opt);
		}
		if (capabilities & NM_802_11_CAP_KEY_MGMT_PSK)
		{
			if ((opt = wso_wpa_psk_new (wsm->glade_file, capabilities, TRUE)))
				g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_WPA2_PSK), opt);
		}
	}

	if ((opt = wso_leap_new (wsm->glade_file, capabilities)))
		g_hash_table_insert (wsm->options, GINT_TO_POINTER (WSO_TYPE_LEAP), opt);

	if (g_hash_table_size (wsm->options) == 0)
	{
		nm_warning ("capabilities='%x' and did not match any protocals, not even none!", capabilities);
		ret = FALSE;
	}

	return ret;
}

#define NAME_COLUMN	0
#define OPT_COLUMN	1

static void
add_wso_type (gpointer key, gpointer val, gpointer user_data)
{
	GSList **list = (GSList **) user_data;

	*list = g_slist_prepend (*list, key);
}

static int
sort_wso_types (gconstpointer a, gconstpointer b)
{
	int aa = GPOINTER_TO_INT (a);
	int bb = GPOINTER_TO_INT (b);

	if (aa < bb)
		return -1;
	if (aa > bb)
		return 1;

	return 0;
}

void wsm_update_combo (WirelessSecurityManager *wsm, GtkComboBox *combo)
{
	GtkListStore *model;
	GSList *iter;
	GSList *wso_types = NULL;

	g_return_if_fail (wsm != NULL);
	g_return_if_fail (combo != NULL);

	/* All this messing around with wso_types list is for sorting.
	   We can't just sort the model since the model itself doesn't
	   have enough information for sorting. */

	g_hash_table_foreach (wsm->options, add_wso_type, &wso_types);
	wso_types = g_slist_sort (wso_types, sort_wso_types);

	model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);

	for (iter = wso_types; iter; iter = iter->next) {
		WirelessSecurityOption *opt = (WirelessSecurityOption *) g_hash_table_lookup (wsm->options, iter->data);
		GtkTreeIter iter;

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, NAME_COLUMN, wso_get_name (opt), OPT_COLUMN, opt, -1);
	}

	g_slist_free (wso_types);

	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
}


static WirelessSecurityOption * get_active_option_from_combo (GtkComboBox *combo)
{
	WirelessSecurityOption * opt = NULL;
	GtkTreeIter			iter;
	GtkTreeModel *			model;

	g_return_val_if_fail (combo != NULL, NULL);

	model = gtk_combo_box_get_model (combo);
	g_assert (model);
	if (gtk_combo_box_get_active_iter (combo, &iter))
		gtk_tree_model_get (model, &iter, OPT_COLUMN, &opt, -1);

	return opt;
}


GtkWidget * wsm_get_widget_for_active (WirelessSecurityManager *wsm, GtkComboBox *combo,
							    GtkSignalFunc validate_cb, gpointer user_data)
{
	WirelessSecurityOption * opt;

	g_return_val_if_fail (wsm != NULL, NULL);
	g_return_val_if_fail (combo != NULL, NULL);

	opt = get_active_option_from_combo (combo);
	g_return_val_if_fail (opt != NULL, NULL);

	return wso_get_widget (opt, validate_cb, user_data);
}

gboolean wsm_validate_active (WirelessSecurityManager *wsm, GtkComboBox *combo, const char *ssid)
{
	WirelessSecurityOption * opt = NULL;

	g_return_val_if_fail (wsm != NULL, FALSE);
	g_return_val_if_fail (combo != NULL, FALSE);

	opt = get_active_option_from_combo (combo);
	g_return_val_if_fail (opt != NULL, FALSE);
	return wso_validate_input (opt, ssid, NULL);
}


WirelessSecurityOption * wsm_get_option_for_active (WirelessSecurityManager *wsm, GtkComboBox *combo)
{
	g_return_val_if_fail (wsm != NULL, NULL);
	g_return_val_if_fail (combo != NULL, NULL);

	return get_active_option_from_combo (combo);
}


WirelessSecurityOption * wsm_get_option_by_type (WirelessSecurityManager *wsm, WSOType type)
{
	g_return_val_if_fail (wsm != NULL, NULL);

	return (WirelessSecurityOption *) g_hash_table_lookup (wsm->options, GINT_TO_POINTER (type));
}

void wsm_free (WirelessSecurityManager *wsm)
{
	g_return_if_fail (wsm != NULL);

	g_free (wsm->glade_file);
	g_hash_table_destroy (wsm->options);
	memset (wsm, 0, sizeof (WirelessSecurityManager));
	g_free (wsm);
}
