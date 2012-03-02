/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * Copyright 2012 Red Hat, Inc.
 */

#include <config.h>

#include <glib/gi18n.h>

#include "new-connection.h"
#include "nm-connection-list.h"
#include "nm-connection-editor.h"
#include "page-ethernet.h"
#include "page-wifi.h"
#include "page-mobile.h"
#include "page-wimax.h"
#include "page-dsl.h"
#include "page-infiniband.h"
#include "page-bond.h"
#include "page-vpn.h"
#include "vpn-helpers.h"

static GSList *vpn_plugins;

#define COL_LABEL      0
#define COL_NEW_FUNC   1
#define COL_VPN_PLUGIN 2

static gint
sort_vpn_plugins (gconstpointer a, gconstpointer b)
{
	NMVpnPluginUiInterface *aa = NM_VPN_PLUGIN_UI_INTERFACE (a);
	NMVpnPluginUiInterface *bb = NM_VPN_PLUGIN_UI_INTERFACE (b);
	char *aa_desc = NULL, *bb_desc = NULL;
	int ret;

	g_object_get (aa, NM_VPN_PLUGIN_UI_INTERFACE_NAME, &aa_desc, NULL);
	g_object_get (bb, NM_VPN_PLUGIN_UI_INTERFACE_NAME, &bb_desc, NULL);

	ret = g_strcmp0 (aa_desc, bb_desc);

	g_free (aa_desc);
	g_free (bb_desc);

	return ret;
}

ConnectionTypeData *
get_connection_type_list (void)
{
	GArray *array;
	ConnectionTypeData data;
	static ConnectionTypeData *list;
	GHashTable *vpn_plugins_hash;
	gboolean have_vpn_plugins;

	if (list)
		return list;

	array = g_array_new (TRUE, FALSE, sizeof (ConnectionTypeData));

	data.name = _("Ethernet");
	data.new_connection_func = ethernet_connection_new;
	data.setting_type = NM_TYPE_SETTING_WIRED;
	g_array_append_val (array, data);

	data.name = _("Wi-Fi");
	data.new_connection_func = wifi_connection_new;
	data.setting_type = NM_TYPE_SETTING_WIRELESS;
	g_array_append_val (array, data);

	data.name = _("Mobile Broadband");
	data.new_connection_func = mobile_connection_new;
	data.setting_type = NM_TYPE_SETTING_GSM;
	g_array_append_val (array, data);

	data.name = _("WiMAX");
	data.new_connection_func = wimax_connection_new;
	data.setting_type = NM_TYPE_SETTING_WIMAX;
	g_array_append_val (array, data);

	data.name = _("DSL");
	data.new_connection_func = dsl_connection_new;
	data.setting_type = NM_TYPE_SETTING_PPPOE;
	g_array_append_val (array, data);

	data.name = _("InfiniBand");
	data.new_connection_func = infiniband_connection_new;
	data.setting_type = NM_TYPE_SETTING_INFINIBAND;
	g_array_append_val (array, data);

	data.name = _("Bond");
	data.new_connection_func = bond_connection_new;
	data.setting_type = NM_TYPE_SETTING_BOND;
	g_array_append_val (array, data);

	/* Add "VPN" only if there are plugins */
	vpn_plugins_hash = vpn_get_plugins (NULL);
	have_vpn_plugins  = vpn_plugins_hash && g_hash_table_size (vpn_plugins_hash);
	if (have_vpn_plugins) {
		GHashTableIter iter;
		gpointer name, plugin;

		data.name = _("VPN");
		data.new_connection_func = vpn_connection_new;
		data.setting_type = NM_TYPE_SETTING_VPN;
		g_array_append_val (array, data);

		vpn_plugins = NULL;
		g_hash_table_iter_init (&iter, vpn_plugins_hash);
		while (g_hash_table_iter_next (&iter, &name, &plugin))
			vpn_plugins = g_slist_prepend (vpn_plugins, plugin);
		vpn_plugins = g_slist_sort (vpn_plugins, sort_vpn_plugins);
	}

	return (ConnectionTypeData *)g_array_free (array, FALSE);
}

static gboolean
combo_row_separator_func (GtkTreeModel *model,
                          GtkTreeIter  *iter,
                          gpointer      data)
{
	char *label;

	gtk_tree_model_get (model, iter,
	                    COL_LABEL, &label,
	                    -1);
	if (label) {
		g_free (label);
		return FALSE;
	} else
		return TRUE;
}

static void
combo_changed_cb (GtkComboBox *combo, gpointer user_data)
{
	GtkLabel *label = GTK_LABEL (user_data);
	GtkTreeModel *model;
	GtkTreeIter iter;
	NMVpnPluginUiInterface *plugin = NULL;
	char *description, *markup;

	if (!gtk_combo_box_get_active_iter (combo, &iter))
		goto error;

	model = gtk_combo_box_get_model (combo);
	if (!model)
		goto error;

	gtk_tree_model_get (model, &iter, COL_VPN_PLUGIN, &plugin, -1);
	if (!plugin)
		goto error;

	g_object_get (G_OBJECT (plugin), NM_VPN_PLUGIN_UI_INTERFACE_DESC, &description, NULL);
	g_object_unref (plugin);
	if (!description)
		goto error;

	markup = g_markup_printf_escaped ("<i>%s</i>", description);
	gtk_label_set_markup (label, markup);
	g_free (markup);
	g_free (description);
	return;

error:
	gtk_label_set_text (label, "");
}

static void
set_up_connection_type_combo (GtkComboBox *combo,
                              GtkLabel *description_label,
                              NewConnectionTypeFilterFunc type_filter_func,
                              gpointer user_data)
{
	GtkListStore *model = GTK_LIST_STORE (gtk_combo_box_get_model (combo));
	ConnectionTypeData *list = get_connection_type_list ();
	GtkTreeIter iter;
	GSList *p;
	int i, vpn_index = -1;
	gboolean import_supported = FALSE;
	gboolean added_any = FALSE;

	gtk_combo_box_set_row_separator_func (combo, combo_row_separator_func, NULL, NULL);
	g_signal_connect (G_OBJECT (combo), "changed", G_CALLBACK (combo_changed_cb), description_label);

	for (i = 0; list[i].name; i++) {
		if (type_filter_func && !type_filter_func (list[i].setting_type, user_data))
			continue;

		if (list[i].setting_type == NM_TYPE_SETTING_VPN) {
			vpn_index = i;
			continue;
		}

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
		                    COL_LABEL, list[i].name,
		                    COL_NEW_FUNC, list[i].new_connection_func,
		                    -1);
		added_any = TRUE;
	}

	if (!vpn_plugins || vpn_index == -1)
		return;

	if (added_any) {
		/* Separator */
		gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	}

	for (p = vpn_plugins; p; p = p->next) {
		NMVpnPluginUiInterface *plugin = NM_VPN_PLUGIN_UI_INTERFACE (p->data);
		char *desc;

		g_object_get (plugin, NM_VPN_PLUGIN_UI_INTERFACE_NAME, &desc, NULL);

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
		                    COL_LABEL, desc,
		                    COL_NEW_FUNC, list[vpn_index].new_connection_func,
		                    COL_VPN_PLUGIN, plugin,
		                    -1);
		g_free (desc);

		if (nm_vpn_plugin_ui_interface_get_capabilities (plugin) & NM_VPN_PLUGIN_UI_CAPABILITY_IMPORT)
			import_supported = TRUE;
	}

	if (import_supported) {
		/* Separator */
		gtk_list_store_append (model, &iter);

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
		                    COL_LABEL, _("Import a saved VPN configuration..."),
		                    COL_NEW_FUNC, vpn_connection_import,
		                    -1);
	}
}

typedef struct {
	GtkWindow *parent_window;
	NMRemoteSettings *settings;
	NewConnectionResultFunc result_func;
	gpointer user_data;
} NewConnectionData;

static void
new_connection_result (NMConnection *connection,
                       gboolean canceled,
                       GError *error,
                       gpointer user_data)
{
	NewConnectionData *ncd = user_data;
	NewConnectionResultFunc result_func;
	GtkWindow *parent_window;
	const char *default_message = _("The connection editor dialog could not be initialized due to an unknown error.");

	result_func = ncd->result_func;
	user_data = ncd->user_data;
	parent_window = ncd->parent_window;
	g_slice_free (NewConnectionData, ncd);

	if (!connection) {
		nm_connection_editor_error (parent_window,
		                            _("Could not create new connection"),
		                            "%s",
		                            (error && error->message) ? error->message : default_message);
	}

	result_func (connection, user_data);
}

void
new_connection_of_type (GtkWindow *parent_window,
                        const char *detail,
                        NMRemoteSettings *settings,
                        PageNewConnectionFunc new_func,
                        NewConnectionResultFunc result_func,
                        gpointer user_data)
{
	NewConnectionData *ncd;

	ncd = g_slice_new (NewConnectionData);
	ncd->parent_window = parent_window;
	ncd->settings = settings;
	ncd->result_func = result_func;
	ncd->user_data = user_data;

	new_func (parent_window,
	          detail,
	          settings,
	          new_connection_result,
	          ncd);
}

void
new_connection_dialog (GtkWindow *parent_window,
                       NMRemoteSettings *settings,
                       NewConnectionTypeFilterFunc type_filter_func,
                       NewConnectionResultFunc result_func,
                       gpointer user_data)
{
	new_connection_dialog_full (parent_window, settings,
	                            NULL, NULL,
	                            type_filter_func,
	                            result_func,
	                            user_data);
}

void
new_connection_dialog_full (GtkWindow *parent_window,
                            NMRemoteSettings *settings,
                            const char *primary_label,
                            const char *secondary_label,
                            NewConnectionTypeFilterFunc type_filter_func,
                            NewConnectionResultFunc result_func,
                            gpointer user_data)
{

	GtkBuilder *gui;
	GtkDialog *type_dialog;
	GtkComboBox *combo;
	GtkLabel *label;
	GtkTreeIter iter;
	int response;
	PageNewConnectionFunc new_func = NULL;
	NMVpnPluginUiInterface *plugin = NULL;
	char *vpn_type = NULL;
	GError *error = NULL;

	/* load GUI */
	gui = gtk_builder_new ();
	if (!gtk_builder_add_from_file (gui,
	                                UIDIR "/ce-new-connection.ui",
	                                &error)) {
		g_warning ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
		g_object_unref (gui);
		return;
	}

	type_dialog = GTK_DIALOG (gtk_builder_get_object (gui, "new_connection_type_dialog"));
	gtk_window_set_transient_for (GTK_WINDOW (type_dialog), parent_window);

	combo = GTK_COMBO_BOX (gtk_builder_get_object (gui, "new_connection_type_combo"));
	label = GTK_LABEL (gtk_builder_get_object (gui, "new_connection_desc_label"));
	set_up_connection_type_combo (combo, label, type_filter_func, user_data);
	gtk_combo_box_set_active (combo, 0);

	if (primary_label) {
		label = GTK_LABEL (gtk_builder_get_object (gui, "new_connection_primary_label"));
		gtk_label_set_text (label, primary_label);
	}
	if (secondary_label) {
		label = GTK_LABEL (gtk_builder_get_object (gui, "new_connection_secondary_label"));
		gtk_label_set_text (label, secondary_label);
	}

	response = gtk_dialog_run (type_dialog);
	if (response == GTK_RESPONSE_OK) {
		gtk_combo_box_get_active_iter (combo, &iter);
		gtk_tree_model_get (gtk_combo_box_get_model (combo), &iter,
		                    COL_NEW_FUNC, &new_func,
		                    COL_VPN_PLUGIN, &plugin,
		                    -1);

		if (plugin) {
			g_object_get (G_OBJECT (plugin), NM_VPN_PLUGIN_UI_INTERFACE_SERVICE, &vpn_type, NULL);
			g_object_unref (plugin);
		}
	}

	gtk_widget_destroy (GTK_WIDGET (type_dialog));
	g_object_unref (gui);

	if (new_func)
		new_connection_of_type (parent_window, vpn_type, settings, new_func, result_func, user_data);
	else
		result_func (NULL, user_data);

	g_free (vpn_type);
}

typedef struct {
	GtkWindow *parent_window;
	NMConnectionEditor *editor;
	DeleteConnectionResultFunc result_func;
	gpointer user_data;
} DeleteInfo;

static void
delete_cb (NMRemoteConnection *connection,
           GError *error,
           gpointer user_data)
{
	DeleteInfo *info = user_data;
	DeleteConnectionResultFunc result_func;

	if (error) {
		nm_connection_editor_error (info->parent_window,
		                            _("Connection delete failed"),
		                            "%s", error->message);
	}

	if (info->editor) {
		nm_connection_editor_set_busy (info->editor, FALSE);
		g_object_unref (info->editor);
	}
	if (info->parent_window)
		g_object_unref (info->parent_window);

	result_func = info->result_func;
	user_data = info->user_data;
	g_free (info);

	if (result_func)
		(*result_func) (connection, error == NULL, user_data);
}

void
delete_connection (GtkWindow *parent_window,
                   NMRemoteConnection *connection,
                   DeleteConnectionResultFunc result_func,
                   gpointer user_data)
{
	NMConnectionEditor *editor;
	NMSettingConnection *s_con;
	GtkWidget *dialog;
	const char *id;
	guint result;
	DeleteInfo *info;

	editor = nm_connection_editor_get (NM_CONNECTION (connection));
	if (editor && nm_connection_editor_get_busy (editor)) {
		/* Editor already has an operation in progress, raise it */
		nm_connection_editor_present (editor);
		return;
	}

	s_con = nm_connection_get_setting_connection (NM_CONNECTION (connection));
	g_assert (s_con);
	id = nm_setting_connection_get_id (s_con);

	dialog = gtk_message_dialog_new (parent_window,
	                                 GTK_DIALOG_DESTROY_WITH_PARENT,
	                                 GTK_MESSAGE_QUESTION,
	                                 GTK_BUTTONS_NONE,
	                                 _("Are you sure you wish to delete the connection %s?"),
	                                 id);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
	                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                        GTK_STOCK_DELETE, GTK_RESPONSE_YES,
	                        NULL);

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (result != GTK_RESPONSE_YES)
		return;

	info = g_malloc0 (sizeof (DeleteInfo));
	info->editor = editor ? g_object_ref (editor) : NULL;
	info->parent_window = parent_window ? g_object_ref (parent_window) : NULL;
	info->result_func = result_func;
	info->user_data = user_data;

	if (editor)
		nm_connection_editor_set_busy (editor, TRUE);

	nm_remote_connection_delete (connection, delete_cb, info);
}
