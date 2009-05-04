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
#include <glib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <glib/gi18n.h>

#include <nm-connection.h>
#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>

#include "vpn-helpers.h"

#define NM_VPN_API_SUBJECT_TO_CHANGE
#include "nm-vpn-plugin-ui-interface.h"

#define VPN_NAME_FILES_DIR SYSCONFDIR"/NetworkManager/VPN"

static GHashTable *plugins = NULL;

NMVpnPluginUiInterface *
vpn_get_plugin_by_service (const char *service)
{
	g_return_val_if_fail (service != NULL, NULL);

	return g_hash_table_lookup (plugins, service);
}

GHashTable *
vpn_get_plugins (GError **error)
{
	GDir *dir;
	const char *f;

	if (error)
		g_return_val_if_fail (*error == NULL, NULL);

	if (plugins)
		return plugins;

	dir = g_dir_open (VPN_NAME_FILES_DIR, 0, NULL);
	if (!dir) {
		g_set_error (error, 0, 0, "Couldn't read VPN .name files directory " VPN_NAME_FILES_DIR ".");
		return NULL;
	}

	plugins = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                 (GDestroyNotify) g_free, (GDestroyNotify) g_object_unref);

	while ((f = g_dir_read_name (dir))) {
		char *path = NULL, *service = NULL;
		char *so_path = NULL, *so_name = NULL;
		GKeyFile *keyfile = NULL;
		GModule *module;
		NMVpnPluginUiFactory factory = NULL;

		if (!g_str_has_suffix (f, ".name"))
			continue;

		path = g_strdup_printf ("%s/%s", VPN_NAME_FILES_DIR, f);

		keyfile = g_key_file_new ();
		if (!g_key_file_load_from_file (keyfile, path, 0, NULL))
			goto next;

		service = g_key_file_get_string (keyfile, "VPN Connection", "service", NULL);
		if (!service)
			goto next;

		so_path = g_key_file_get_string (keyfile,  "GNOME", "properties", NULL);
		if (!so_path)
			goto next;

		/* Remove any path and extension components, then reconstruct path
		 * to the SO in LIBDIR
		 */
		so_name = g_path_get_basename (so_path);
		g_free (so_path);
		so_path = g_strdup_printf ("%s/NetworkManager/%s", LIBDIR, so_name);
		g_free (so_name);

		module = g_module_open (so_path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
		if (!module) {
			g_set_error (error, 0, 0, "Cannot load the VPN plugin which provides the "
			             "service '%s'.", service);
			goto next;
		}

		if (g_module_symbol (module, "nm_vpn_plugin_ui_factory", (gpointer) &factory)) {
			NMVpnPluginUiInterface *plugin;
			GError *factory_error = NULL;
			gboolean success = FALSE;

			plugin = factory (&factory_error);
			if (plugin) {
				char *plug_name = NULL, *plug_service = NULL;

				/* Validate plugin properties */
				g_object_get (G_OBJECT (plugin),
				              NM_VPN_PLUGIN_UI_INTERFACE_NAME, &plug_name,
				              NM_VPN_PLUGIN_UI_INTERFACE_SERVICE, &plug_service,
				              NULL);
				if (!plug_name || !strlen (plug_name)) {
					g_set_error (error, 0, 0, "cannot load VPN plugin in '%s': missing plugin name", 
					             g_module_name (module));
				} else if (!plug_service || strcmp (plug_service, service)) {
					g_set_error (error, 0, 0, "cannot load VPN plugin in '%s': invalid service name", 
					             g_module_name (module));
				} else {
					/* Success! */
					g_object_set_data_full (G_OBJECT (plugin), "gmodule", module,
					                        (GDestroyNotify) g_module_close);
					g_hash_table_insert (plugins, g_strdup (service), plugin);
					success = TRUE;
				}
				g_free (plug_name);
				g_free (plug_service);
			} else {
				g_set_error (error, 0, 0, "cannot load VPN plugin in '%s': %s", 
				             g_module_name (module), g_module_error ());
			}

			if (!success)
				g_module_close (module);
		} else {
			g_set_error (error, 0, 0, "cannot locate nm_vpn_plugin_ui_factory() in '%s': %s", 
			             g_module_name (module), g_module_error ());
			g_module_close (module);
		}

	next:
		g_free (so_path);
		g_free (service);
		g_key_file_free (keyfile);
		g_free (path);
	}
	g_dir_close (dir);

	return plugins;
}


typedef struct {
	char *filename;
	NMConnection *connection;
	GError *error;
} VpnImportInfo;

static void
try_import (gpointer key, gpointer value, gpointer user_data)
{
	VpnImportInfo *info = user_data;
	NMVpnPluginUiInterface *plugin = NM_VPN_PLUGIN_UI_INTERFACE (value);

	if (info->connection)
		return;

	if (info->error) {
		g_error_free (info->error);
		info->error = NULL;
	}

	info->connection = nm_vpn_plugin_ui_interface_import (plugin, info->filename, &(info->error));
}

typedef struct {
	VpnImportSuccessCallback callback;
	gpointer user_data;
} ActionInfo;

static void
import_vpn_from_file_cb (GtkWidget *dialog, gint response, gpointer user_data)
{
	char *filename = NULL;
	ActionInfo *info = (ActionInfo *) user_data;
	VpnImportInfo import_info;
	NMConnection *connection;

	if (response != GTK_RESPONSE_ACCEPT)
		goto out;

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
	if (!filename) {
		g_warning ("%s: didn't get a filename back from the chooser!", __func__);
		goto out;
	}

	import_info.filename = filename;
	import_info.connection = NULL;
	import_info.error = NULL;
	g_hash_table_foreach (plugins, try_import, (gpointer) &import_info);

	connection = import_info.connection;
	if (connection) {
		if (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_UNKNOWN)
			nm_connection_set_scope (connection, NM_CONNECTION_SCOPE_USER);
		info->callback (connection, info->user_data);
	} else {
		GtkWidget *err_dialog;
		char *basename = g_path_get_basename (filename);

		err_dialog = gtk_message_dialog_new (NULL,
		                                     GTK_DIALOG_DESTROY_WITH_PARENT,
		                                     GTK_MESSAGE_ERROR,
		                                     GTK_BUTTONS_OK,
		                                     _("Cannot import VPN connection"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (err_dialog),
		                                 _("The file '%s' could not be read or does not contain recognized VPN connection information\n\nError: %s."),
		                                 basename, import_info.error ? import_info.error->message : "unknown error");
		g_free (basename);
		g_signal_connect (err_dialog, "delete-event", G_CALLBACK (gtk_widget_destroy), NULL);
		g_signal_connect (err_dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
		gtk_widget_show_all (err_dialog);
		gtk_window_present (GTK_WINDOW (err_dialog));
	}

	if (import_info.error)
		g_error_free (import_info.error);
	g_free (filename);

out:
	gtk_widget_hide (dialog);
	gtk_widget_destroy (dialog);
	g_free (info);
}

static void
destroy_import_chooser (GtkWidget *dialog, gpointer user_data)
{
	g_free (user_data);
	gtk_widget_destroy (dialog);
}

void
vpn_import (VpnImportSuccessCallback callback, gpointer user_data)
{
	GtkWidget *dialog;
	ActionInfo *info;

	dialog = gtk_file_chooser_dialog_new (_("Select file to import"),
	                                      NULL,
	                                      GTK_FILE_CHOOSER_ACTION_OPEN,
	                                      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
	                                      NULL);
	info = g_malloc0 (sizeof (ActionInfo));
	info->callback = callback;
	info->user_data = user_data;

	g_signal_connect (G_OBJECT (dialog), "close", G_CALLBACK (destroy_import_chooser), info);
	g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK (import_vpn_from_file_cb), info);
	gtk_widget_show_all (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void
export_vpn_to_file_cb (GtkWidget *dialog, gint response, gpointer user_data)
{
	NMConnection *connection = NM_CONNECTION (user_data);
	char *filename = NULL;
	GError *error = NULL;
	NMVpnPluginUiInterface *plugin;
	NMSettingConnection *s_con = NULL;
	NMSettingVPN *s_vpn = NULL;
	const char *service_type;
	const char *id = NULL;
	gboolean success = FALSE;

	if (response != GTK_RESPONSE_ACCEPT)
		goto out;

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
	if (!filename) {
		g_set_error (&error, 0, 0, "no filename");
		goto done;
	}

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		int replace_response;
		GtkWidget *replace_dialog;
		char *basename;

		basename = g_path_get_basename (filename);
		replace_dialog = gtk_message_dialog_new (NULL,
		                                         GTK_DIALOG_DESTROY_WITH_PARENT,
		                                         GTK_MESSAGE_QUESTION,
		                                         GTK_BUTTONS_CANCEL,
		                                         _("A file named \"%s\" already exists."),
		                                         basename);
		gtk_dialog_add_buttons (GTK_DIALOG (replace_dialog), _("_Replace"), GTK_RESPONSE_OK, NULL);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (replace_dialog),
							  _("Do you want to replace %s with the VPN connection you are saving?"), basename);
		g_free (basename);
		replace_response = gtk_dialog_run (GTK_DIALOG (replace_dialog));
		gtk_widget_destroy (replace_dialog);
		if (replace_response != GTK_RESPONSE_OK)
			goto out;
	}

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	id = s_con ? nm_setting_connection_get_id (s_con) : NULL;
	if (!id) {
		g_set_error (&error, 0, 0, "connection setting invalid");
		goto done;
	}

	s_vpn = (NMSettingVPN *) nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN);
	service_type = s_vpn ? nm_setting_vpn_get_service_type (s_vpn) : NULL;

	if (!service_type) {
		g_set_error (&error, 0, 0, "VPN setting invalid");
		goto done;
	}

	plugin = vpn_get_plugin_by_service (service_type);
	if (plugin)
		success = nm_vpn_plugin_ui_interface_export (plugin, filename, connection, &error);

done:
	if (!success) {
		GtkWidget *err_dialog;
		char *basename = filename ? g_path_get_basename (filename) : g_strdup ("(none)");

		err_dialog = gtk_message_dialog_new (NULL,
		                                     GTK_DIALOG_DESTROY_WITH_PARENT,
		                                     GTK_MESSAGE_ERROR,
		                                     GTK_BUTTONS_OK,
		                                     _("Cannot export VPN connection"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (err_dialog),
		                                 _("The VPN connection '%s' could not be exported to %s.\n\nError: %s."),
		                                 id ? id : "(unknown)", basename, error ? error->message : "unknown error");
		g_free (basename);
		g_signal_connect (err_dialog, "delete-event", G_CALLBACK (gtk_widget_destroy), NULL);
		g_signal_connect (err_dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
		gtk_widget_show_all (err_dialog);
		gtk_window_present (GTK_WINDOW (err_dialog));
	}

out:
	if (error)
		g_error_free (error);
	g_object_unref (connection);

	gtk_widget_hide (dialog);
	gtk_widget_destroy (dialog);
}

void
vpn_export (NMConnection *connection)
{
	GtkWidget *dialog;
	NMVpnPluginUiInterface *plugin;
	NMSettingVPN *s_vpn = NULL;
	const char *service_type;

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	service_type = s_vpn ? nm_setting_vpn_get_service_type (s_vpn) : NULL;

	if (!service_type) {
		g_warning ("%s: invalid VPN connection!", __func__);
		return;
	}

	dialog = gtk_file_chooser_dialog_new (_("Export VPN connection..."),
	                                      NULL,
	                                      GTK_FILE_CHOOSER_ACTION_SAVE,
	                                      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                      GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
	                                      NULL);

	plugin = vpn_get_plugin_by_service (service_type);
	if (plugin) {
		char *suggested = NULL;

		suggested = nm_vpn_plugin_ui_interface_get_suggested_name (plugin, connection);
		if (suggested) {
			gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), suggested);
			g_free (suggested);
		}
	}

	g_signal_connect (G_OBJECT (dialog), "close", G_CALLBACK (gtk_widget_destroy), NULL);
	g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK (export_vpn_to_file_cb), g_object_ref (connection));
	gtk_widget_show_all (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void
add_plugins_to_list (gpointer key, gpointer data, gpointer user_data)
{
	GSList **list = (GSList **) user_data;

	*list = g_slist_append (*list, NM_VPN_PLUGIN_UI_INTERFACE (data));
}

static gint
sort_plugins (gconstpointer a, gconstpointer b)
{
	NMVpnPluginUiInterface *aa = NM_VPN_PLUGIN_UI_INTERFACE (a);
	NMVpnPluginUiInterface *bb = NM_VPN_PLUGIN_UI_INTERFACE (b);
	const char *aa_desc = NULL, *bb_desc = NULL;

	g_object_get (aa, NM_VPN_PLUGIN_UI_INTERFACE_NAME, &aa_desc, NULL);
	g_object_get (bb, NM_VPN_PLUGIN_UI_INTERFACE_NAME, &bb_desc, NULL);

	if (!aa_desc)
		return -1;
	if (!bb_desc)
		return 1;

	return strcmp (aa_desc, bb_desc);
}

#define COL_PLUGIN_DESC 0
#define COL_PLUGIN_OBJ  1

static void
combo_changed_cb (GtkComboBox *combo, gpointer user_data)
{
	GtkLabel *label = GTK_LABEL (user_data);
	GtkTreeModel *model;
	GtkTreeIter iter;
	NMVpnPluginUiInterface *plugin = NULL;
	const char *desc = NULL;
	char *tmp;

	if (!gtk_combo_box_get_active_iter (combo, &iter))
		goto error;

	model = gtk_combo_box_get_model (combo);
	if (!model)
		goto error;

	gtk_tree_model_get (model, &iter, COL_PLUGIN_OBJ, &plugin, -1);
	if (!plugin)
		goto error;

	g_object_get (G_OBJECT (plugin), NM_VPN_PLUGIN_UI_INTERFACE_DESC, &desc, NULL);
	if (!desc)
		goto error;

	tmp = g_strdup_printf ("<i>%s</i>", desc);
	gtk_label_set_markup (label, tmp);
	g_free (tmp);
	return;

error:
	gtk_label_set_text (label, "");
}

char *
vpn_ask_connection_type (void)
{
	GladeXML *xml;
	GtkWidget *dialog, *combo, *widget;
	GtkTreeModel *model;
	GSList *plugin_list = NULL, *iter;
	gint response;
	GtkTreeIter tree_iter;
	char *service_type = NULL;

	if (!plugins || !g_hash_table_size (plugins)) {
		g_warning ("%s: no VPN plugins could be found!", __func__);
		return NULL;
	}

	xml = glade_xml_new (GLADEDIR "/ce-vpn-wizard.glade", "vpn_type_dialog", NULL);
	if (!xml) {
		g_warning ("%s: couldn't load VPN wizard glade file '%s'!",
		           __func__, GLADEDIR "/ce-vpn-wizard.glade");
		return NULL;
	}

	dialog = glade_xml_get_widget (xml, "vpn_type_dialog");
	if (!dialog) {
		g_warning ("%s: couldn't load VPN wizard dialog!", __func__);
		g_object_unref (xml);
		return NULL;
	}

	model = GTK_TREE_MODEL (gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_OBJECT));
	g_hash_table_foreach (plugins, add_plugins_to_list, &plugin_list);

	plugin_list = g_slist_sort (plugin_list, sort_plugins);
	for (iter = plugin_list; iter; iter = g_slist_next (iter)) {
		NMVpnPluginUiInterface *plugin = NM_VPN_PLUGIN_UI_INTERFACE (iter->data);
		const char *desc;

		gtk_list_store_append (GTK_LIST_STORE (model), &tree_iter);
		g_object_get (plugin, NM_VPN_PLUGIN_UI_INTERFACE_NAME, &desc, NULL);
		gtk_list_store_set (GTK_LIST_STORE (model), &tree_iter,
		                    COL_PLUGIN_DESC, desc,
		                    COL_PLUGIN_OBJ, plugin, -1);
	}

	combo = glade_xml_get_widget (xml, "vpn_type_combo");
	widget = glade_xml_get_widget (xml, "vpn_desc_label");
	g_signal_connect (G_OBJECT (combo), "changed", G_CALLBACK (combo_changed_cb), widget);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), model);
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

	gtk_widget_show_all (dialog);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response != GTK_RESPONSE_OK)
		goto out;

	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &tree_iter)) {
		NMVpnPluginUiInterface *plugin = NULL;

		gtk_tree_model_get (model, &tree_iter, COL_PLUGIN_OBJ, &plugin, -1);
		if (plugin)
			g_object_get (G_OBJECT (plugin), NM_VPN_PLUGIN_UI_INTERFACE_SERVICE, &service_type, NULL);
	}

out:
	gtk_widget_destroy (dialog);
	g_object_unref (xml);
	if (service_type)
		return g_strdup (service_type);
	return NULL;
}

