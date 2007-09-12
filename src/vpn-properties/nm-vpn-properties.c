/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/***************************************************************************
 * CVSID: $Id: nm-vpn-properties.c 2650 2007-07-26 15:02:54Z dcbw $
 *
 * nm-vpn-properties.c : GNOME UI dialogs for manipulating VPN connections
 *
 * Copyright (C) 2005 David Zeuthen, <davidz@redhat.com>
 *
 * === 
 * NOTE NOTE NOTE: All source for nm-vpn-properties is licensed to you
 * under your choice of the Academic Free License version 2.0, or the
 * GNU General Public License version 2.
 * ===
 *
 * Licensed under the Academic Free License version 2.0
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gtk/gtkwindow.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <glib/gi18n.h>
#if !GTK_CHECK_VERSION(2, 10, 0)
#include <gnome.h>
#endif

#define NM_VPN_API_SUBJECT_TO_CHANGE
#include "nm-vpn-ui-interface.h"
#include "clipboard.h"

#define NM_GCONF_VPN_CONNECTIONS_PATH "/system/networking/vpn_connections"

static GladeXML *xml;
static GConfClient *gconf_client;

static GtkWidget *dialog;
static GtkTreeView *vpn_conn_view;
static GtkListStore *vpn_conn_list;
static GtkWidget *vpn_edit;
static GtkWidget *vpn_export;
static GtkWidget *vpn_delete;

#if GTK_CHECK_VERSION(2, 10, 0)
static GtkWidget *assistant;
static GtkWidget *assistant_start_page, *assistant_confirm_page;
static GtkWidget *assistant_conn_type_page, *assistant_details_page;
#else
static GtkDialog *druid_window;
static GnomeDruid *druid;
static GnomeDruidPageEdge *druid_start_page, *druid_confirm_page;
static GnomeDruidPageStandard *druid_conn_type_page, *druid_details_page;
#endif
static GtkComboBox *vpn_type_combo_box;
static GtkVBox *vpn_type_details;
static GtkWidget *vpn_details_widget;
static GtkWidget *vpn_details_widget_old_parent;

static GtkDialog *edit_dialog;

static GSList *vpn_types;

NetworkManagerVpnUI *current_vpn_ui;

static NetworkManagerVpnUI *
find_vpn_ui_by_service_name (const char *service_name)
{
	GSList *i;

	g_return_val_if_fail (service_name != NULL, NULL);

	for (i = vpn_types; i != NULL; i = g_slist_next (i)) {
		NetworkManagerVpnUI *vpn_ui;
		const char * vpn_ui_service_name;

		vpn_ui = i->data;
		vpn_ui_service_name = vpn_ui->get_service_name (vpn_ui);
		if (vpn_ui_service_name && strcmp (vpn_ui_service_name, service_name) == 0)
			return vpn_ui;
	}

	return NULL;
}

enum {
	VPNCONN_NAME_COLUMN,
	VPNCONN_SVC_NAME_COLUMN,
	VPNCONN_GCONF_COLUMN,
	VPNCONN_USER_CAN_EDIT_COLUMN,
	VPNCONN_N_COLUMNS
};

static void
update_edit_del_sensitivity (void)
{
	GtkTreeSelection *selection;
	gboolean is_editable = FALSE, is_exportable = FALSE;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection (vpn_conn_view);
	if (!selection || !gtk_tree_selection_get_selected (selection, NULL, &iter))
		is_editable = is_exportable = FALSE;
	else {
		NetworkManagerVpnUI *vpn_ui;
		const char *service_name = NULL;

		gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), &iter, VPNCONN_USER_CAN_EDIT_COLUMN, &is_editable, -1);
		gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), &iter, VPNCONN_SVC_NAME_COLUMN, &service_name, -1);

		vpn_ui = find_vpn_ui_by_service_name (service_name);
		if (vpn_ui)
			is_exportable = vpn_ui->can_export (vpn_ui);
	}

	gtk_widget_set_sensitive (vpn_edit, is_editable);
	gtk_widget_set_sensitive (vpn_delete, is_editable);
	gtk_widget_set_sensitive (vpn_export, is_editable && is_exportable);
}

static void
property_value_destroy (gpointer data)
{
	GValue *value = (GValue *) data;

	g_value_unset (value);
	g_slice_free (GValue, data);
}

static GHashTable *
create_empty_properties (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal,
							(GDestroyNotify) g_free,
							property_value_destroy);
}

static void
add_property (GHashTable *properties, const char *key, GConfValue *gconf_value)
{
	GValue *value = NULL;

	if (!gconf_value)
		return;

	switch (gconf_value->type) {
	case GCONF_VALUE_STRING:
		value = g_slice_new0 (GValue);
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, gconf_value_get_string (gconf_value));
		break;
	case GCONF_VALUE_INT:
		value = g_slice_new0 (GValue);
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, gconf_value_get_int (gconf_value));
		break;
	case GCONF_VALUE_BOOL:
		value = g_slice_new0 (GValue);
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, gconf_value_get_bool (gconf_value));
		break;
	default:
		break;
	}

	if (value)
		g_hash_table_insert (properties, gconf_unescape_key (key, -1), value);
}

static GHashTable *
read_properties (const char *path)
{
	GHashTable *properties;
	GSList *gconf_entries;
	GSList *iter;
	int prefix_len;
	GError *err = NULL;

	gconf_entries = gconf_client_all_entries (gconf_client, path, &err);

	if (err) {
		g_warning ("Could not read properties: %s", err->message);
		g_error_free (err);
		return NULL;
	}

	properties = create_empty_properties ();
	prefix_len = strlen (path);

	for (iter = gconf_entries; iter; iter = iter->next) {
		GConfEntry *entry = (GConfEntry *) iter->data;
		const char *key;

		key = gconf_entry_get_key (entry);
		key += prefix_len + 1; /* get rid of the full path */

		if (!strcmp (key, "name") ||
		    !strcmp (key, "service_name") ||
		    !strcmp (key, "routes") ||
		    !strcmp (key, "last_attempt_success")) {
			gconf_entry_free (entry);
			continue;
		}

		add_property (properties, key, gconf_entry_get_value (entry));
		gconf_entry_free (entry);
	}

	g_slist_free (gconf_entries);

	return properties;
}


static void
write_properties (gpointer key, gpointer val, gpointer user_data)
{
	GValue *value = (GValue *) val;
	char *path = (char *) user_data;
	char *esc_key;
	char *full_key;

	esc_key = gconf_escape_key ((char *) key, -1);
	full_key = g_strconcat (path, "/", esc_key, NULL);
	g_free (esc_key);

	if (G_VALUE_HOLDS_STRING (value))
		gconf_client_set_string (gconf_client, full_key, g_value_get_string (value), NULL);
	else if (G_VALUE_HOLDS_INT (value))
		gconf_client_set_int (gconf_client, full_key, g_value_get_int (value), NULL);
	else if (G_VALUE_HOLDS_BOOLEAN (value))
		gconf_client_set_bool (gconf_client, full_key, g_value_get_boolean (value), NULL);
	else
		g_warning ("Don't know how to write '%s' to gconf", G_VALUE_TYPE_NAME (value));

	g_free (full_key);
}

static void
update_properties (GHashTable *properties, const char *path)
{
	GSList *gconf_entries;
	GSList *iter;
	int prefix_len;
	GError *err = NULL;

	/* First, remove the gconf entries that are not in current properties */

	gconf_entries = gconf_client_all_entries (gconf_client, path, &err);
	if (err) {
		g_warning ("Could not read properties: %s", err->message);
		g_error_free (err);
		return;
	}

	prefix_len = strlen (path);

	for (iter = gconf_entries; iter; iter = iter->next) {
		GConfEntry *entry = (GConfEntry *) iter->data;
		const char *key;
		char *esc_key;

		key = gconf_entry_get_key (entry);
		key += prefix_len + 1; /* get rid of the full path */

		if (!strcmp (key, "name") ||
		    !strcmp (key, "service_name") ||
		    !strcmp (key, "routes") ||
		    !strcmp (key, "last_attempt_success")) {
			gconf_entry_free (entry);
			continue;
		}

		esc_key = gconf_unescape_key (key, -1);
		if (!g_hash_table_lookup (properties, esc_key))
			gconf_client_unset (gconf_client, gconf_entry_get_key (entry), NULL);

		g_free (esc_key);
		gconf_entry_free (entry);
	}

	g_slist_free (gconf_entries);

	/* Now add/update all new/existing values */
	g_hash_table_foreach (properties, write_properties, (gpointer) path);
}

static gboolean
add_vpn_connection (const char *conn_name, const char *service_name, GHashTable *properties, GSList *routes)
{
	char *gconf_key;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	char conn_gconf_path[PATH_MAX];
	char *escaped_conn_name;
	gboolean ret;
	gboolean conn_user_can_edit = TRUE;

	ret = FALSE;

	escaped_conn_name = gconf_escape_key (conn_name, strlen (conn_name));

	g_snprintf (conn_gconf_path, 
		    sizeof (conn_gconf_path), 
		    NM_GCONF_VPN_CONNECTIONS_PATH "/%s",
		    escaped_conn_name);

	if (gconf_client_dir_exists (gconf_client, conn_gconf_path, NULL))
		goto out;

	if ((selection = gtk_tree_view_get_selection (vpn_conn_view)) == NULL)
		goto out;

	/* User-visible name of connection */
	gconf_key = g_strdup_printf ("%s/name", conn_gconf_path);
	gconf_client_set_string (gconf_client, gconf_key, conn_name, NULL);

	/* Service name of connection */
	gconf_key = g_strdup_printf ("%s/service_name", conn_gconf_path);
	gconf_client_set_string (gconf_client, gconf_key, service_name, NULL);

	/* vpn-daemon specific data */
	g_hash_table_foreach (properties, write_properties, conn_gconf_path);

	/* routes */
	gconf_key = g_strdup_printf ("%s/routes", conn_gconf_path);

#if 0
	{
		GSList *i;

		i = NULL;
		i = g_slist_append (i, "172.16.0.0/16");
		gconf_client_set_list (gconf_client, gconf_key, GCONF_VALUE_STRING, routes, NULL);
		g_slist_free (i);
	}
#else
		gconf_client_set_list (gconf_client, gconf_key, GCONF_VALUE_STRING, routes, NULL);
#endif

	gconf_client_suggest_sync (gconf_client, NULL);

	conn_user_can_edit = TRUE;

	gtk_list_store_append (vpn_conn_list, &iter);
	gtk_list_store_set (vpn_conn_list, &iter,
			    VPNCONN_NAME_COLUMN, conn_name,
			    VPNCONN_SVC_NAME_COLUMN, service_name,
			    VPNCONN_GCONF_COLUMN, conn_gconf_path,
			    VPNCONN_USER_CAN_EDIT_COLUMN, &conn_user_can_edit,
			    -1);
	gtk_tree_selection_select_iter (selection, &iter);

	ret = TRUE;

out:
	g_free (escaped_conn_name);
	return ret;
}

static void
remove_vpn_connection (const char *gconf_path, GtkTreeIter *iter)
{
	GError *err = NULL;

	if (!gconf_client_recursive_unset (gconf_client, gconf_path, 
								GCONF_UNSET_INCLUDING_SCHEMA_NAMES, &err))
		g_warning ("Remove VPN connection failed: %s", err->message);

	gconf_client_suggest_sync (gconf_client, NULL);

	if (gtk_list_store_remove (vpn_conn_list, iter)) {
		GtkTreeSelection *selection;

		selection = gtk_tree_view_get_selection (vpn_conn_view);
		gtk_tree_selection_select_iter (selection, iter);
	}
}

static void 
vpn_druid_vpn_validity_changed (NetworkManagerVpnUI *vpn_ui,
				gboolean is_valid, 
				gpointer user_data)
{
	char *conn_name;
	GtkTreeIter iter;

	conn_name = vpn_ui->get_connection_name (vpn_ui);

	/* get list of existing connection names */
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (vpn_conn_list), &iter)) {
		do {
			char *name;

			gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list),
					    &iter,
					    VPNCONN_NAME_COLUMN,
					    &name,
					    -1);
			
			if (strcmp (name, conn_name) == 0) {
				is_valid = FALSE;
				break;
			}

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (vpn_conn_list), &iter));
	}

	g_free (conn_name);

#if GTK_CHECK_VERSION(2, 10, 0)
        gtk_assistant_set_page_complete (GTK_ASSISTANT (assistant), assistant_details_page, is_valid);
#else
	gnome_druid_set_buttons_sensitive (druid, 
					   TRUE,
					   is_valid,
					   TRUE,
					   FALSE);
#endif
}

static void vpn_details_widget_reparent(GtkWidget *new_parent)
{
  if ((new_parent==NULL) && (vpn_details_widget_old_parent==NULL)) {
    return;
  } else if (new_parent==NULL) {
    gtk_widget_reparent(vpn_details_widget,vpn_details_widget_old_parent);
    vpn_details_widget_old_parent=NULL;
    return;
  } else if (vpn_details_widget_old_parent==NULL) {
    vpn_details_widget_old_parent=gtk_widget_get_parent(vpn_details_widget);
  }

  gtk_widget_reparent(vpn_details_widget,new_parent);
}

static void
vpn_details_widget_get_current (GHashTable *properties, GSList *routes, const char *connection_name)
{
    if (vpn_details_widget) {
        vpn_details_widget_reparent (NULL);
        vpn_details_widget = NULL;
    }

	if (current_vpn_ui != NULL) {
		vpn_details_widget = current_vpn_ui->get_widget (current_vpn_ui, properties, routes, connection_name);
	}
}

static gboolean vpn_druid_vpn_type_page_next (void *druidpage,
					      GtkWidget *widget,
					      gpointer user_data)
{
	/* show appropriate child */
	current_vpn_ui = (NetworkManagerVpnUI *) g_slist_nth_data (vpn_types, gtk_combo_box_get_active (vpn_type_combo_box));
    vpn_details_widget_get_current(NULL,NULL,NULL);
	current_vpn_ui->set_validity_changed_callback (current_vpn_ui, vpn_druid_vpn_validity_changed, NULL);
    vpn_details_widget_reparent(GTK_WIDGET (vpn_type_details));

	return FALSE;
}

static void vpn_druid_vpn_details_page_prepare (void *druidpage,
						GtkWidget *widget,
						gpointer user_data)
{
	gboolean is_valid;
	NetworkManagerVpnUI *vpn_ui;

#if GTK_CHECK_VERSION(2, 10, 0)
        vpn_druid_vpn_type_page_next (NULL, NULL, NULL); 
#endif
	is_valid = FALSE;

	/* validate input, in case we are coming in via 'Back' */
	vpn_ui = (NetworkManagerVpnUI *) g_slist_nth_data (vpn_types, gtk_combo_box_get_active (vpn_type_combo_box));
	if (vpn_ui != NULL)
		is_valid = vpn_ui->is_valid (vpn_ui);

#if GTK_CHECK_VERSION(2, 10, 0)
        gtk_assistant_set_page_complete (GTK_ASSISTANT (assistant), assistant_details_page, is_valid);
#else
	gnome_druid_set_buttons_sensitive (druid, TRUE, is_valid, TRUE, FALSE);
#endif	
}

static gboolean vpn_druid_vpn_details_page_next (void *druidpage,
						 GtkWidget *widget,
						 gpointer user_data)
{
	gboolean is_valid;
	NetworkManagerVpnUI *vpn_ui;

	is_valid = FALSE;

	/* validate input */
	vpn_ui = (NetworkManagerVpnUI *) g_slist_nth_data (vpn_types, gtk_combo_box_get_active (vpn_type_combo_box));
	if (vpn_ui != NULL)
		is_valid = vpn_ui->is_valid (vpn_ui);

	return !is_valid;
}

static void vpn_druid_vpn_confirm_page_prepare (void *druidpage,
						GtkWidget *widget,
						gpointer user_data)
{
	NetworkManagerVpnUI *vpn_ui;

	vpn_ui = (NetworkManagerVpnUI *) g_slist_nth_data (vpn_types, gtk_combo_box_get_active (vpn_type_combo_box));
	if (vpn_ui != NULL) {
		gchar *confirm_text;

		vpn_ui->get_confirmation_details (vpn_ui, &confirm_text);
		
#if GTK_CHECK_VERSION(2, 10, 0)
		gtk_label_set_text ( GTK_LABEL (assistant_confirm_page), confirm_text);
#else
		gnome_druid_page_edge_set_text (druid_confirm_page,
						confirm_text);
#endif
		g_free (confirm_text);
	}
}

#if GTK_CHECK_VERSION(2, 10, 0)
static void assistant_page_prepare (GtkAssistant *assistant,
					GtkWidget *page,
					gpointer user_data)
{
	if (page == assistant_details_page) {
		vpn_druid_vpn_details_page_prepare (NULL, NULL, NULL);
	}
	if (page == assistant_confirm_page) {
		vpn_druid_vpn_confirm_page_prepare (NULL, NULL, NULL);
	}
}
#endif

static gboolean vpn_druid_vpn_confirm_page_finish (void *druidpage,
						   GtkWidget *widget,
						   gpointer user_data)
{
	GHashTable *properties;
	GSList *conn_routes;
	char *conn_name;
	NetworkManagerVpnUI *vpn_ui;

	vpn_ui = (NetworkManagerVpnUI *) g_slist_nth_data (vpn_types, gtk_combo_box_get_active (vpn_type_combo_box));
	conn_name = vpn_ui->get_connection_name (vpn_ui);
	properties = create_empty_properties ();
	vpn_ui->get_properties (vpn_ui, properties);
	conn_routes = vpn_ui->get_routes (vpn_ui);

	add_vpn_connection (conn_name, vpn_ui->get_service_name (vpn_ui), properties, conn_routes);

	vpn_details_widget_reparent (NULL);
#if GTK_CHECK_VERSION(2, 10, 0)
    gtk_widget_hide (assistant);
#else
    gtk_dialog_response (GTK_DIALOG (druid_window), GTK_RESPONSE_APPLY);
#endif
	return FALSE;
}

static gboolean vpn_druid_cancel (void *ignored, gpointer user_data)
{
    vpn_details_widget_reparent(NULL);
#if GTK_CHECK_VERSION(2, 10, 0)
    gtk_widget_hide(assistant);
#else
    gtk_dialog_response(GTK_DIALOG(druid_window),GTK_RESPONSE_CANCEL);
#endif
//	gtk_widget_hide (GTK_WIDGET (druid_window));
	return FALSE;
}

//static gboolean vpn_window_close (GtkWidget *ignored, gpointer user_data)
//{
//	gtk_widget_hide (GTK_WIDGET (druid_window));
//	return TRUE;
//}

static void
add_cb (GtkButton *button, gpointer user_data)
{
    gint result;

	/* Bail out if we don't have any VPN implementations on our system */
	if (vpn_types == NULL || g_slist_length (vpn_types) == 0) {
		GtkWidget *err_dialog;

		err_dialog = gtk_message_dialog_new (NULL,
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Cannot add VPN connection"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (err_dialog),
		   _("No suitable VPN software was found on your system. Contact your system administrator."));
		gtk_dialog_run (GTK_DIALOG (err_dialog));
		gtk_widget_destroy (err_dialog);
		goto out;
	}

	vpn_details_widget_reparent (NULL);

#if GTK_CHECK_VERSION(2, 10, 0)
	gtk_window_set_default_size (GTK_WINDOW (assistant), -1, -1);
	gtk_window_set_position (GTK_WINDOW (assistant), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_show_all (assistant);
#else
	/* auto-shrink our window */
	gnome_druid_set_page (druid, GNOME_DRUID_PAGE (druid_start_page));

	gtk_window_set_policy (GTK_WINDOW(druid_window), FALSE, FALSE, TRUE);

//	gtk_widget_show (GTK_WIDGET (druid));
	gtk_widget_show_all (GTK_WIDGET (druid_window));

	result = gtk_dialog_run (GTK_DIALOG (druid_window));

	gtk_widget_hide (GTK_WIDGET (druid_window));
#endif
    vpn_details_widget_reparent(NULL);

out:
	;
}


static void
import_settings (const char *svc_name, const char *name)
{
	current_vpn_ui = find_vpn_ui_by_service_name (svc_name);

	/* Bail out if we don't have the requested VPN implementation on our system */
	if (current_vpn_ui == NULL) {
		char *basename;
		GtkWidget *err_dialog;

		basename = g_path_get_basename (name);

		err_dialog = gtk_message_dialog_new (NULL,
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Cannot import VPN connection"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (err_dialog),
							  _("Cannot find suitable software for VPN connection type '%s' to import the file '%s'. Contact your system administrator."),
							  svc_name, basename);
		gtk_dialog_run (GTK_DIALOG (err_dialog));
		gtk_widget_destroy (err_dialog);
		g_free (basename);
		goto out;
	}

#if !GTK_CHECK_VERSION(2, 10, 0)
	gnome_druid_set_page (druid, GNOME_DRUID_PAGE (druid_details_page));
#endif
	/* show appropriate child */
	vpn_details_widget_get_current(NULL, NULL, NULL);
	vpn_details_widget_reparent(GTK_WIDGET(vpn_type_details));
	current_vpn_ui->set_validity_changed_callback (current_vpn_ui, vpn_druid_vpn_validity_changed, NULL);

	current_vpn_ui->import_file (current_vpn_ui, name);

	gtk_widget_set_sensitive (vpn_details_widget, TRUE);

#if !GTK_CHECK_VERSION(2, 10, 0)
	gtk_window_set_policy (GTK_WINDOW(druid_window), FALSE, FALSE, TRUE);
	gtk_widget_show (GTK_WIDGET (druid_window));
#endif

out:
	;
}


static void 
vpn_edit_vpn_validity_changed (NetworkManagerVpnUI *vpn_ui,
				gboolean is_valid, 
				gpointer user_data)
{
	const char *orig_conn_name;
	char *conn_name;
	GtkTreeIter iter;

	orig_conn_name = (const char *) user_data;

	conn_name = vpn_ui->get_connection_name (vpn_ui);

	/* get list of existing connection names */
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (vpn_conn_list), &iter)) {
		do {
			char *name;

			gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list),
					    &iter,
					    VPNCONN_NAME_COLUMN,
					    &name,
					    -1);

			/* Can override the original name (stored in user_data, see edit_cb()) */
			if (strcmp (name, orig_conn_name) != 0) {			
				if (strcmp (name, conn_name) == 0) {
					is_valid = FALSE;
					break;
				}
			}

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (vpn_conn_list), &iter));
	}

	g_free (conn_name);

	gtk_dialog_set_response_sensitive (edit_dialog, GTK_RESPONSE_ACCEPT, is_valid);

}

static gboolean
retrieve_data_from_selected_connection (NetworkManagerVpnUI **vpn_ui,
								GHashTable **conn_properties,
								GSList **conn_routes,
								const char **conn_name,
								char **conn_gconf_path)
{
	gboolean result;
	const char *conn_service_name;
	GSList *conn_routes_gconfvalue;
	GSList *i;
	char key[PATH_MAX];
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GConfValue *value;

	result = FALSE;

	if ((selection = gtk_tree_view_get_selection (vpn_conn_view)) == NULL)
		goto out;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		goto out;

	gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), 
					&iter, 
					VPNCONN_GCONF_COLUMN, 
					conn_gconf_path, 
					-1);
	gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), 
					&iter, 
					VPNCONN_NAME_COLUMN,
					conn_name, 
					-1);

	g_snprintf (key, sizeof (key), "%s/service_name", *conn_gconf_path);
	if ((value = gconf_client_get (gconf_client, key, NULL)) == NULL ||
	    (conn_service_name = gconf_value_get_string (value)) == NULL)
		goto out;

	*vpn_ui = find_vpn_ui_by_service_name (conn_service_name);
	if (*vpn_ui == NULL) {
		GtkWidget *err_dialog;

		err_dialog = gtk_message_dialog_new (NULL,
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Error retrieving VPN connection '%s'"),
						 *conn_name);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (err_dialog),
		    _("Could not find the UI files for VPN connection type '%s'. Contact your system administrator."),
		    conn_service_name);
		gtk_dialog_run (GTK_DIALOG (err_dialog));
		gtk_widget_destroy (err_dialog);
		goto out;
	}

	*conn_properties = read_properties (*conn_gconf_path);

	/* routes may be an empty list */
	*conn_routes = NULL;
	g_snprintf (key, sizeof (key), "%s/routes", *conn_gconf_path);

	value = gconf_client_get (gconf_client, key, NULL);
	if (value) {
		if (gconf_value_get_list_type (value) == GCONF_VALUE_STRING) {
			conn_routes_gconfvalue = gconf_value_get_list (value);

			for (i = conn_routes_gconfvalue; i != NULL; i = g_slist_next (i)) {
				const char *val;
				val = gconf_value_get_string ((GConfValue *) i->data);
				*conn_routes = g_slist_append (*conn_routes, (gpointer) val);
			}
		}

		gconf_value_free (value);
	}

	result = TRUE;

out:
	return result;
}

static void
edit_cb (GtkButton *button, gpointer user_data)
{
	gint result;
	GHashTable *conn_properties;
	GSList *conn_routes;
	const char *conn_name;
	char key[PATH_MAX];
	char *conn_gconf_path;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	if (!retrieve_data_from_selected_connection (&current_vpn_ui, &conn_properties, &conn_routes, &conn_name, &conn_gconf_path))
		goto out;

	if ((selection = gtk_tree_view_get_selection (vpn_conn_view)) == NULL)
		goto out;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		goto out;

	vpn_details_widget_get_current (conn_properties, conn_routes, conn_name);
	g_hash_table_destroy (conn_properties);
	g_slist_free (conn_routes);

	current_vpn_ui->set_validity_changed_callback (current_vpn_ui, vpn_edit_vpn_validity_changed, (gpointer) conn_name);
	vpn_details_widget_reparent (GTK_WIDGET (edit_dialog->vbox));

/*	gtk_widget_set_sensitive (vpn_details_widget, TRUE);*/

	gtk_widget_show (vpn_details_widget);
	/*gtk_widget_set_sensitive (vpn_details_widget, TRUE);*/

	/* auto-shrink our window */
	gtk_window_set_policy (GTK_WINDOW (edit_dialog), FALSE, FALSE, TRUE);

	gtk_widget_show (GTK_WIDGET (edit_dialog));

	result = gtk_dialog_run (GTK_DIALOG (edit_dialog));

	if (result == GTK_RESPONSE_ACCEPT) {
		char *new_conn_name;
		GHashTable *new_properties;
		GSList *new_conn_routes;

		new_conn_name = current_vpn_ui->get_connection_name (current_vpn_ui);
		new_properties = create_empty_properties ();
		current_vpn_ui->get_properties (current_vpn_ui, new_properties);
		new_conn_routes = current_vpn_ui->get_routes (current_vpn_ui);

		if (strcmp (new_conn_name, conn_name) == 0) {
			/* same name, just update properties and routes */

			update_properties (new_properties, conn_gconf_path);

			g_snprintf (key, sizeof (key), "%s/routes", conn_gconf_path);
			gconf_client_set_list (gconf_client, key, GCONF_VALUE_STRING, new_conn_routes, NULL);

			gconf_client_suggest_sync (gconf_client, NULL);
		} else {
			selection = gtk_tree_view_get_selection (vpn_conn_view);
			gtk_tree_selection_get_selected (selection, NULL, &iter);
			remove_vpn_connection (conn_gconf_path, &iter);
			add_vpn_connection (new_conn_name, current_vpn_ui->get_service_name (current_vpn_ui), 
							new_properties, new_conn_routes);
		}

		if (new_properties)
			g_hash_table_destroy (new_properties);

		if (new_conn_routes != NULL) {
			g_slist_foreach (new_conn_routes, (GFunc)g_free, NULL);
			g_slist_free (new_conn_routes);
		}
	}

    vpn_details_widget_reparent(NULL);
	gtk_widget_hide (GTK_WIDGET (edit_dialog));

out:
	;
}

static void
delete_cb (GtkButton *button, gpointer user_data)
{
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	gchar *conn_gconf_path;
	gchar *conn_name;
	GtkWidget *confirm_dialog;
	int response;

	if ((selection = gtk_tree_view_get_selection (vpn_conn_view)) == NULL)
		goto out;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		goto out;

	gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), &iter, VPNCONN_NAME_COLUMN, &conn_name, -1);
	confirm_dialog = gtk_message_dialog_new (NULL,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_CANCEL,
					 _("Delete VPN connection \"%s\"?"), conn_name);
	gtk_dialog_add_buttons (GTK_DIALOG (confirm_dialog), GTK_STOCK_DELETE, GTK_RESPONSE_OK, NULL);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (confirm_dialog),
						  _("All information about the VPN connection \"%s\" will be lost and you may need your system administrator to provide information to create a new connection."), conn_name);
	response = gtk_dialog_run (GTK_DIALOG (confirm_dialog));
	gtk_widget_destroy (confirm_dialog);

	if (response != GTK_RESPONSE_OK)
		goto out;

	gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), &iter, VPNCONN_GCONF_COLUMN, &conn_gconf_path, -1);

	if (conn_gconf_path != NULL)
		remove_vpn_connection (conn_gconf_path, &iter);

	update_edit_del_sensitivity ();

out:
	;
}

static void
response_cb (void)
{
	gtk_widget_destroy (dialog);
	gtk_main_quit ();
}

static gboolean
delete_event_cb (GtkDialog *the_dialog)
{
    vpn_details_widget_reparent(NULL);
//	gtk_dialog_response (the_dialog, GTK_RESPONSE_DELETE_EVENT);
	return FALSE;
}

static void
export_cb (GtkButton *button, gpointer user_data)
{
	NetworkManagerVpnUI *vpn_ui;
	GHashTable *conn_properties;
	GSList *conn_routes;
	const char *conn_name;
	char *conn_gconf_path;

	if (retrieve_data_from_selected_connection (&vpn_ui, &conn_properties,
									    &conn_routes, &conn_name, &conn_gconf_path))
		vpn_ui->export (vpn_ui, conn_properties, conn_routes, conn_name);
}

static void get_all_vpn_connections (void)
{
	GtkTreeIter iter;
	GSList *vpn_conn = NULL;

	for (vpn_conn = gconf_client_all_dirs (gconf_client, NM_GCONF_VPN_CONNECTIONS_PATH, NULL);
	     vpn_conn != NULL;
	     vpn_conn = g_slist_next (vpn_conn)) {
		char key[PATH_MAX];
		GConfValue *value;
		const char *conn_gconf_path;
		const char *conn_name;
		const char *conn_service_name;
		gboolean conn_user_can_edit = TRUE;

		conn_gconf_path = (const char *) (vpn_conn->data);
		conn_name = g_strdup (conn_gconf_path + strlen (NM_GCONF_VPN_CONNECTIONS_PATH) + 1);

		g_snprintf (key, sizeof (key), "%s/service_name", conn_gconf_path);
		if ((value = gconf_client_get (gconf_client, key, NULL)) == NULL ||
		    (conn_service_name = gconf_value_get_string (value)) == NULL)
			goto error;

		gtk_list_store_append (vpn_conn_list, &iter);
		gtk_list_store_set (vpn_conn_list, &iter,
				    VPNCONN_NAME_COLUMN, conn_name,
				    VPNCONN_SVC_NAME_COLUMN, conn_service_name,
				    VPNCONN_GCONF_COLUMN, conn_gconf_path,
				    VPNCONN_USER_CAN_EDIT_COLUMN, conn_user_can_edit,
				    -1);

error:
		g_free (vpn_conn->data);
	}
}

static void 
vpn_list_cursor_changed_cb (GtkTreeView *treeview,
			    gpointer user_data)
{
	update_edit_del_sensitivity ();
}

static void
load_properties_module (GSList **vpn_types_list, const char *path)
{
	GModule *module;
	NetworkManagerVpnUI* (*nm_vpn_properties_factory) (void) = NULL;
	NetworkManagerVpnUI* impl;

	module = g_module_open (path, G_MODULE_BIND_LAZY);
	if (module == NULL) {
		g_warning ("Cannot open module '%s'", path);
		goto out;
	}

	if (!g_module_symbol (module, "nm_vpn_properties_factory", 
			      (gpointer) &nm_vpn_properties_factory)) {
		g_warning ("Cannot locate function 'nm_vpn_properties_factory' in '%s': %s", 
			   path, g_module_error ());
		g_module_close (module);
		goto out;		
	}

	impl = nm_vpn_properties_factory ();
	if (impl == NULL) {
		g_warning ("Function 'nm_vpn_properties_factory' in '%s' returned NULL", path);
		g_module_close (module);
		goto out;
	}

	*vpn_types_list = g_slist_append (*vpn_types_list, impl);

out:
	;
}

#define VPN_NAME_FILES_DIR SYSCONFDIR"/NetworkManager/VPN"

static gint
vpn_list_sorter(GtkTreeModel *model,
                GtkTreeIter *a,
                GtkTreeIter *b,
                gpointer user_data)
{
	GValue aval = {0};
	GValue bval = {0};
	const char *aname;
	const char *bname;
	gint res;

	gtk_tree_model_get_value(model, a, VPNCONN_NAME_COLUMN, &aval);
	gtk_tree_model_get_value(model, b, VPNCONN_NAME_COLUMN, &bval);
	aname = g_value_get_string(&aval);
	bname = g_value_get_string(&bval);
	res = strcasecmp(aname, bname);
	g_value_unset(&aval);
	g_value_unset(&bval);
	return res;
}


static gboolean
init_app (void)
{
	GtkWidget *w;
	gchar *glade_file;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GSList *i;
//	GtkWidget *toplevel;
	GDir *dir;
    GdkColor druid_color;

	if (!vpn_get_clipboard ())
		return FALSE;

	gconf_client = gconf_client_get_default ();
	gconf_client_add_dir (gconf_client, NM_GCONF_VPN_CONNECTIONS_PATH,
			      GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	glade_file = g_strdup_printf ("%s/%s", GLADEDIR, "nm-vpn-properties.glade");
	xml = glade_xml_new (glade_file, NULL, NULL);
	g_free (glade_file);
	if (!xml) {
		GtkWidget *err_dialog;

		err_dialog = gtk_message_dialog_new (NULL,
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Unable to load"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (err_dialog),
		   _("Cannot find some needed resources (the glade file)!"));
		gtk_dialog_run (GTK_DIALOG (err_dialog));
		gtk_widget_destroy (err_dialog);

		return FALSE;
	}

	/* Load all VPN UI modules by inspecting .name files */
	vpn_types = NULL;
	if ((dir = g_dir_open (VPN_NAME_FILES_DIR, 0, NULL)) != NULL) {
		const char *f;

		while ((f = g_dir_read_name (dir)) != NULL) {
			char *path;
			GKeyFile *keyfile;

			if (!g_str_has_suffix (f, ".name"))
				continue;

			path = g_strdup_printf ("%s/%s", VPN_NAME_FILES_DIR, f);

			keyfile = g_key_file_new ();
			if (g_key_file_load_from_file (keyfile, path, 0, NULL)) {
				char *so_path;

				if ((so_path = g_key_file_get_string (keyfile, 
								      "GNOME", 
								      "properties", NULL)) != NULL) {
					load_properties_module (&vpn_types, so_path);
					g_free (so_path);
				}
			}
			g_key_file_free (keyfile);
			g_free (path);
		}
		g_dir_close (dir);
	}

/* Main connecection selection dialog  */
	dialog = glade_xml_get_widget (xml, "vpn-ui-properties");
	g_signal_connect (dialog, "response",
			  G_CALLBACK (response_cb), NULL);
	g_signal_connect (dialog, "delete_event",
			  G_CALLBACK (delete_event_cb), NULL);

	w = glade_xml_get_widget (xml, "add");
	gtk_signal_connect (GTK_OBJECT (w), "clicked", GTK_SIGNAL_FUNC (add_cb), NULL);

	vpn_edit = glade_xml_get_widget (xml, "edit");
	gtk_signal_connect (GTK_OBJECT (vpn_edit), "clicked", GTK_SIGNAL_FUNC (edit_cb), NULL);

	vpn_export = glade_xml_get_widget (xml, "export");
	gtk_signal_connect (GTK_OBJECT (vpn_export), "clicked", GTK_SIGNAL_FUNC (export_cb), NULL);

	vpn_delete = glade_xml_get_widget (xml, "delete");
	gtk_signal_connect (GTK_OBJECT (vpn_delete), "clicked", GTK_SIGNAL_FUNC (delete_cb), NULL);

	vpn_conn_view = GTK_TREE_VIEW (glade_xml_get_widget (xml, "vpnlist"));
	vpn_conn_list = gtk_list_store_new (VPNCONN_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (vpn_conn_list),
	                                 VPNCONN_NAME_COLUMN,
	                                 vpn_list_sorter,
	                                 NULL,
	                                 NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (vpn_conn_list),
	                                      VPNCONN_NAME_COLUMN,
	                                      GTK_SORT_ASCENDING);

	gtk_signal_connect_after (GTK_OBJECT (vpn_conn_view), "cursor-changed",
				  GTK_SIGNAL_FUNC (vpn_list_cursor_changed_cb), NULL);

	get_all_vpn_connections ();

	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "text", VPNCONN_NAME_COLUMN,
					     NULL);
	gtk_tree_view_append_column (vpn_conn_view, column);

	gtk_tree_view_set_model (vpn_conn_view, GTK_TREE_MODEL (vpn_conn_list));
	gtk_tree_view_expand_all (vpn_conn_view);

	gtk_widget_show (dialog);

/* Add connection dialog  */
	/* fill in possibly choices in the druid when adding a connection */
	vpn_type_combo_box = GTK_COMBO_BOX (gtk_combo_box_new_text ());
	for (i = vpn_types; i != NULL; i = g_slist_next (i)) {
		NetworkManagerVpnUI *vpn_ui = i->data;
		gtk_combo_box_append_text (vpn_type_combo_box, vpn_ui->get_display_name (vpn_ui));
	}
	gtk_combo_box_set_active (vpn_type_combo_box, 0);

    gdk_color_parse("#7590AE",&druid_color);

    /* Druid Page 1 - Create VPN Connection */
    gchar *msg1 = g_strdup(_("This assistant will guide you through the creation of a connection to a Virtual Private Network (VPN)."));
    gchar *msg2 = g_strdup(_("It will require some information, such as IP addresses and secrets.  Please see your system administrator to obtain this information.")); 
    gchar *msg = g_strdup_printf("%s\n\n%s", msg1, msg2); 

#if GTK_CHECK_VERSION(2, 10, 0)
    assistant = gtk_assistant_new();
    gtk_window_set_title (GTK_WINDOW (assistant), _("Create VPN connection"));
    gtk_signal_connect (GTK_OBJECT (assistant), "cancel", GTK_SIGNAL_FUNC (vpn_druid_cancel), NULL);
    gtk_signal_connect_after (GTK_OBJECT (assistant), "close", GTK_SIGNAL_FUNC (vpn_druid_vpn_confirm_page_finish), NULL);
    gtk_signal_connect_after (GTK_OBJECT (assistant), "prepare", GTK_SIGNAL_FUNC (assistant_page_prepare), NULL);

    assistant_start_page = gtk_label_new (msg);
    gtk_label_set_line_wrap (GTK_LABEL (assistant_start_page), TRUE);
    gtk_assistant_append_page (GTK_ASSISTANT (assistant), assistant_start_page);
    gtk_assistant_set_page_type (GTK_ASSISTANT (assistant), assistant_start_page, GTK_ASSISTANT_PAGE_INTRO);
    gtk_assistant_set_page_title (GTK_ASSISTANT (assistant), assistant_start_page, _("Create VPN connection"));
    gtk_assistant_set_page_complete (GTK_ASSISTANT (assistant), assistant_start_page, TRUE);
 
    GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 100, 10);
    gtk_assistant_set_page_side_image (GTK_ASSISTANT (assistant), assistant_start_page, pixbuf);
    gdk_pixbuf_unref (pixbuf);

#else
    druid_start_page = GNOME_DRUID_PAGE_EDGE (
                   gnome_druid_page_edge_new_with_vals(
         GNOME_EDGE_START, TRUE, _("Create VPN Connection"),
/*     _("This assistant will guide you through the creation of a connection to "
     "a Virtual Private Network (VPN).\n\nIt will require some information, "
     "such as IP addresses and secrets.  Please see your system administrator "
     "to obtain this information."), */ 
	msg, NULL, NULL, NULL));
    gnome_druid_page_edge_set_bg_color(druid_start_page,&druid_color);
    gnome_druid_page_edge_set_logo_bg_color(druid_start_page,&druid_color);
#endif

    g_free(msg1);
    g_free(msg2);
    g_free(msg);

    /* Druid Page 2 - Select Connection Type */
    /*Translators: this will be "Create VPN Connection - [1|2] of 2"*/
    msg = g_strdup(_("Create VPN Connection"));  
    /*Translators: this will be "Create VPN Connection - 1 of 2"*/
    gchar *firstpage = g_strdup(_("1 of 2")); 
    gchar *head = g_strdup_printf("%s%s", msg, firstpage);
    
#if GTK_CHECK_VERSION(2, 10, 0)
    GtkWidget * conn_type_label = gtk_label_new( _("Choose which type of VPN connection you wish to create:"));

    assistant_conn_type_page = gtk_vbox_new(12, FALSE);

    gtk_box_pack_start (GTK_BOX (assistant_conn_type_page), conn_type_label, TRUE, FALSE, 0);
    gtk_box_pack_end (GTK_BOX (assistant_conn_type_page), GTK_WIDGET(vpn_type_combo_box), FALSE, FALSE, 0);

    gtk_assistant_append_page (GTK_ASSISTANT (assistant), assistant_conn_type_page);
    gtk_assistant_set_page_title (GTK_ASSISTANT (assistant), assistant_conn_type_page, head);
    gtk_assistant_set_page_complete (GTK_ASSISTANT (assistant), assistant_conn_type_page, TRUE);

#else
    druid_conn_type_page = GNOME_DRUID_PAGE_STANDARD (
                   gnome_druid_page_standard_new_with_vals(head, NULL, NULL));
    gnome_druid_page_standard_set_background(druid_conn_type_page,&druid_color);
    gnome_druid_page_standard_set_logo_background(druid_conn_type_page,&druid_color);
    gnome_druid_page_standard_append_item(druid_conn_type_page,
            _("Choose which type of VPN connection you wish to create:"),
            GTK_WIDGET(vpn_type_combo_box), NULL);
	gtk_signal_connect_after (GTK_OBJECT (druid_conn_type_page), "next", GTK_SIGNAL_FUNC (vpn_druid_vpn_type_page_next), NULL);
#endif
    g_free(firstpage); 
    g_free(head);

    /* Druid Page 3 - Connection Details */
    /*Translators: this will be "Create VPN Connection - [1|2] of 2"*/
    gchar *secondpage = g_strdup(_("2 of 2")); 
    head = g_strdup_printf("%s%s", msg, secondpage);

#if GTK_CHECK_VERSION(2, 10, 0)
    assistant_details_page = gtk_vbox_new (12, FALSE);

    gtk_assistant_append_page (GTK_ASSISTANT (assistant), assistant_details_page);
    gtk_assistant_set_page_title (GTK_ASSISTANT (assistant), assistant_details_page, head);
    vpn_type_details = GTK_VBOX (assistant_details_page);

#else
    druid_details_page = GNOME_DRUID_PAGE_STANDARD (
                   gnome_druid_page_standard_new_with_vals(head, NULL, NULL));
    gnome_druid_page_standard_set_background(druid_details_page,&druid_color);
    gnome_druid_page_standard_set_logo_background(druid_details_page,&druid_color);
	gtk_signal_connect_after (GTK_OBJECT (druid_details_page), "prepare", GTK_SIGNAL_FUNC (vpn_druid_vpn_details_page_prepare), NULL);
	gtk_signal_connect_after (GTK_OBJECT (druid_details_page), "next", GTK_SIGNAL_FUNC (vpn_druid_vpn_details_page_next), NULL);
	vpn_type_details = GTK_VBOX(druid_details_page->vbox);
#endif
    gtk_widget_show(GTK_WIDGET(vpn_type_details));
    g_free(secondpage);
    g_free(head);
    g_free(msg);

#if GTK_CHECK_VERSION(2, 10, 0)

    assistant_confirm_page = gtk_label_new (NULL);
    gtk_assistant_append_page (GTK_ASSISTANT (assistant), assistant_confirm_page);

    gtk_assistant_set_page_type (GTK_ASSISTANT (assistant), assistant_confirm_page, GTK_ASSISTANT_PAGE_CONFIRM);
    gtk_assistant_set_page_title (GTK_ASSISTANT (assistant), assistant_confirm_page,  _("Finished Create VPN Connection"));
    gtk_assistant_set_page_complete (GTK_ASSISTANT (assistant), assistant_confirm_page, TRUE);
    gtk_assistant_set_page_side_image (GTK_ASSISTANT (assistant), assistant_confirm_page, pixbuf);
    gdk_pixbuf_unref (pixbuf);
#else
    /* Druid Page 4 - FInished Create VPN Connection */
    druid_confirm_page = GNOME_DRUID_PAGE_EDGE (
                   gnome_druid_page_edge_new_with_vals(
         GNOME_EDGE_FINISH, TRUE, _("Finished Create VPN Connection"),
         "", NULL, NULL, NULL));
    gnome_druid_page_edge_set_bg_color(druid_confirm_page,&druid_color);
    gnome_druid_page_edge_set_logo_bg_color(druid_confirm_page,&druid_color);
	gtk_signal_connect_after (GTK_OBJECT (druid_confirm_page), "prepare", GTK_SIGNAL_FUNC (vpn_druid_vpn_confirm_page_prepare), NULL);
	gtk_signal_connect_after (GTK_OBJECT (druid_confirm_page), "finish", GTK_SIGNAL_FUNC (vpn_druid_vpn_confirm_page_finish), NULL);

	/* Druid */
    druid = GNOME_DRUID(gnome_druid_new());
    gtk_signal_connect (GTK_OBJECT (druid), "cancel", GTK_SIGNAL_FUNC (vpn_druid_cancel), NULL);
    gnome_druid_append_page(druid,GNOME_DRUID_PAGE(druid_start_page));
    gnome_druid_append_page(druid,GNOME_DRUID_PAGE(druid_conn_type_page));
    gnome_druid_append_page(druid,GNOME_DRUID_PAGE(druid_details_page));
    gnome_druid_append_page(druid,GNOME_DRUID_PAGE(druid_confirm_page));

//	druid_window = GTK_DIALOG (gtk_dialog_new_with_buttons (_("Create VPN Connection"),
//							       NULL,
//							       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
//							       GTK_STOCK_CANCEL,
//							       GTK_RESPONSE_REJECT,
//							       GTK_STOCK_APPLY,
//							       GTK_RESPONSE_ACCEPT,
//							       NULL));
	druid_window = GTK_DIALOG (gtk_dialog_new_with_buttons (_("Create VPN Connection"),
							       NULL,
							       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							       NULL));
        gtk_dialog_set_has_separator(GTK_DIALOG(druid_window),FALSE);
	gtk_container_add (GTK_CONTAINER (druid_window->vbox), GTK_WIDGET(druid));
#endif
//	gtk_container_add (GTK_CONTAINER (druid_window->vbox), GTK_WIDGET(gtk_label_new("Some label")));
//	gtk_box_pack_start (GTK_BOX (druid_window->vbox), GTK_WIDGET(druid), TRUE,TRUE,0);
//	gtk_box_pack_start (GTK_BOX (druid_window->vbox), GTK_WIDGET(gtk_label_new("Some label")), TRUE,TRUE,0);

//	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (druid));
//	gtk_signal_connect (GTK_OBJECT (toplevel), "delete_event", GTK_SIGNAL_FUNC (vpn_window_close), NULL);

	/* make the druid window modal wrt. our main window */
	/* gtk_window_set_modal (druid_window, TRUE); */
/*	gtk_window_set_transient_for (GTK_WINDOW(druid_window), GTK_WINDOW (dialog)); */

/* Edit dialog */
	edit_dialog = GTK_DIALOG (gtk_dialog_new_with_buttons (_("Edit VPN Connection"),
							       NULL,
							       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							       GTK_STOCK_CANCEL,
							       GTK_RESPONSE_REJECT,
							       GTK_STOCK_APPLY,
							       GTK_RESPONSE_ACCEPT,
							       NULL));

	/* update "Edit" and "Delete" for current selection */
	update_edit_del_sensitivity ();

	return TRUE;
}

int
main (int argc, char *argv[])
{
	GOptionContext *context;
	int ret;
	gboolean bad_opts;
	gboolean do_import;
	gchar *import_svc = NULL;
	gchar *import_file = NULL;
	GOptionEntry entries[] =  {
		{ "import-service", 's', 0, G_OPTION_ARG_STRING, &import_svc, N_("VPN Service for importing"), NULL},
		{ "import-file", 'f', 0, G_OPTION_ARG_FILENAME, &import_file, N_("File to import"), NULL},
		{ NULL }
	};

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/*Translators: this is the description seen when running nm-vpn-properties --help*/
	context = g_option_context_new (N_("- NetworkManager VPN properties"));
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_set_translation_domain(context, GETTEXT_PACKAGE);

	GError *error = NULL;
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	gtk_init(&argc, &argv);

	bad_opts = FALSE;
	do_import = FALSE;
	if (import_svc != NULL) {
		if (import_file != NULL)
			do_import = TRUE;
		else
			bad_opts = TRUE;
	} else if (import_file != NULL)
			bad_opts = TRUE;

	if (bad_opts) {
		fprintf (stderr, "Have to supply both service and file\n");
		ret = EXIT_FAILURE;
		goto out;
	}

	if (init_app () == FALSE) {
		ret = EXIT_FAILURE;
		goto out;
	}

	if (do_import)
		import_settings (import_svc, import_file);

	gtk_main ();

	ret = EXIT_SUCCESS;

out:
	return ret;
}
