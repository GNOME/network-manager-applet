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
#include <dbus/dbus-glib.h>
#if !GTK_CHECK_VERSION(2, 10, 0)
#include <gnome.h>
#endif

#define NM_VPN_API_SUBJECT_TO_CHANGE
#include "nm-vpn-ui-interface.h"
#include "clipboard.h"
#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>
#include <nm-setting-vpn-properties.h>
#include "gconf-helpers.h"

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

static GtkDialog *edit_dialog;

static GSList *vpn_types = NULL;

NetworkManagerVpnUI *current_vpn_ui;

static void edit_cb (GtkButton *button, gpointer user_data);

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
	VPNCONN_CONNECTION_COLUMN,
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
		NMConnection *connection = NULL;
		NMSettingVPN *s_vpn;

		gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), &iter,
		                    VPNCONN_CONNECTION_COLUMN, &connection,
		                    VPNCONN_USER_CAN_EDIT_COLUMN, &is_editable, -1);

		s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
		if (s_vpn) {
			vpn_ui = find_vpn_ui_by_service_name (s_vpn->service_type);
			if (vpn_ui)
				is_exportable = vpn_ui->can_export (vpn_ui);
		}
	}

	gtk_widget_set_sensitive (vpn_edit, is_editable);
	gtk_widget_set_sensitive (vpn_delete, is_editable);
	gtk_widget_set_sensitive (vpn_export, is_editable && is_exportable);
}

static void
clear_vpn_connection (NMConnection *connection)
{
	GSList *dirs = NULL, *elt;
	char *gconf_path;

	gconf_path = g_object_get_data (G_OBJECT (connection), "gconf-path");

	/* Delete all subdirs of gconf_path but not the root dir of the
	 * connection itself.
	 */
	dirs = gconf_client_all_dirs (gconf_client, gconf_path, NULL);
	if (!dirs)
		return;

	for (elt = dirs; elt; elt = g_slist_next (elt)) {
		gconf_client_recursive_unset (gconf_client,
		                              elt->data,
		                              GCONF_UNSET_INCLUDING_SCHEMA_NAMES,
		                              NULL);
		g_free (elt->data);
	}
	g_slist_free (dirs);

	gconf_client_suggest_sync (gconf_client, NULL);
}

static gboolean
write_vpn_connection_to_gconf (NMConnection *connection)
{
	const char *path;

	g_return_val_if_fail (connection != NULL, FALSE);

	g_warning ("Will write connection to GConf:");
	nm_connection_dump (connection);

	clear_vpn_connection (connection);

	path = g_object_get_data (G_OBJECT (connection), "gconf-path");
	g_assert (path);
	nm_gconf_write_connection (connection, gconf_client, path, NULL);

	gconf_client_suggest_sync (gconf_client, NULL);
	return TRUE;
}

static gboolean
add_vpn_connection (NMConnection *connection)
{
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	NMSettingConnection *s_con;
	guint32 i = 0;
	char * path = NULL;

	g_return_val_if_fail (connection != NULL, FALSE);

	/* Find free GConf directory */
	while (i++ < G_MAXUINT32) {
		char buf[255];

		snprintf (&buf[0], 255, GCONF_PATH_CONNECTIONS"/%d", i);
		if (!gconf_client_dir_exists (gconf_client, buf, NULL)) {
			path = g_strdup_printf (buf);
			break;
		}
	};

	if (path == NULL) {
		g_warning ("Couldn't find free GConf directory for new connection.");
		return FALSE;
	}

	if ((selection = gtk_tree_view_get_selection (vpn_conn_view)) == NULL)
		goto error;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con || !s_con->name)
		goto error;

	g_object_set_data_full (G_OBJECT (connection),
					    "gconf-path", path,
					    (GDestroyNotify) g_free);

	write_vpn_connection_to_gconf (connection);

	gtk_list_store_append (vpn_conn_list, &iter);
	gtk_list_store_set (vpn_conn_list, &iter,
			    VPNCONN_NAME_COLUMN, s_con->name,
			    VPNCONN_CONNECTION_COLUMN, connection,
			    VPNCONN_USER_CAN_EDIT_COLUMN, TRUE,
			    -1);
	gtk_tree_selection_select_iter (selection, &iter);
	return TRUE;

error:
	g_free (path);
	return FALSE;
}

static void
remove_vpn_connection (NMConnection *connection, GtkTreeIter *iter)
{
	GError *err = NULL;
	char *gconf_path;

	gconf_path = g_object_get_data (G_OBJECT (connection), "gconf-path");

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
	// FIXME: do we need to do anything here?

#if GTK_CHECK_VERSION(2, 10, 0)
	gtk_assistant_set_page_complete (GTK_ASSISTANT (assistant), assistant_details_page, is_valid);
#else
	gnome_druid_set_buttons_sensitive (druid, TRUE, is_valid, TRUE, FALSE);
#endif
}

static inline void
clear_vpn_details_widget (void)
{
	if (vpn_details_widget) {
		GtkWidget *parent = gtk_widget_get_parent (vpn_details_widget);

		if (parent)
			gtk_container_remove (GTK_CONTAINER (parent), vpn_details_widget);
		vpn_details_widget = NULL;
	}
}

static gboolean
vpn_druid_vpn_type_page_next (void *druidpage,
                              GtkWidget *widget,
                              gpointer user_data)
{
	/* show appropriate child */
	clear_vpn_details_widget ();
	current_vpn_ui = (NetworkManagerVpnUI *) g_slist_nth_data (vpn_types, gtk_combo_box_get_active (vpn_type_combo_box));
	vpn_details_widget = current_vpn_ui->get_widget (current_vpn_ui, NULL);
	gtk_container_add (GTK_CONTAINER (vpn_type_details), vpn_details_widget);

	current_vpn_ui->set_validity_changed_callback (current_vpn_ui, vpn_druid_vpn_validity_changed, NULL);

	return FALSE;
}

static void
vpn_druid_vpn_details_page_prepare (void *druidpage,
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

#if GTK_CHECK_VERSION(2, 10, 0)
#else
static gboolean
vpn_druid_vpn_details_page_next (void *druidpage,
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
#endif

static void
vpn_druid_vpn_confirm_page_prepare (void *druidpage,
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
static void
assistant_page_prepare (GtkAssistant *assistant,
                        GtkWidget *page,
                        gpointer user_data)
{
	if (page == assistant_details_page)
		vpn_druid_vpn_details_page_prepare (NULL, NULL, NULL);

	if (page == assistant_confirm_page)
		vpn_druid_vpn_confirm_page_prepare (NULL, NULL, NULL);
}
#endif

static NMConnection *
new_nm_connection_vpn (void)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingVPN *s_vpn;
	NMSettingVPNProperties *s_vpn_props;

	/* Create a new, blank NMConnection for the addition */
	connection = nm_connection_new ();

	s_con = (NMSettingConnection *) nm_setting_connection_new ();
	s_con->type = g_strdup (NM_SETTING_VPN_SETTING_NAME);
	nm_connection_add_setting (connection, (NMSetting *) s_con);

	s_vpn = (NMSettingVPN *) nm_setting_vpn_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_vpn);

	s_vpn_props = (NMSettingVPNProperties *) nm_setting_vpn_properties_new ();
	nm_connection_add_setting (connection, (NMSetting *) s_vpn_props);

	return connection;
}

static gboolean
fixup_nm_connection_vpn (NMConnection *connection,
                         NetworkManagerVpnUI *vpn_ui)
{                         
	NMSettingConnection *s_con;
	NMSettingVPN *s_vpn;
	const char *svc_name;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con) {
		g_warning ("Invalid connection received from VPN widget (no %s "
		           "setting).", NM_SETTING_CONNECTION_SETTING_NAME);
		return FALSE;
	}

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	if (!s_vpn) {
		g_warning ("Invalid connection received from VPN widget (no %s "
		           "setting).", NM_SETTING_VPN_SETTING_NAME);
		return FALSE;
	}

	/* Fill in standard stuff */
	if (s_con->type)
		g_free (s_con->type);
	s_con->type = g_strdup (NM_SETTING_VPN_SETTING_NAME);

	svc_name = vpn_ui->get_service_name (vpn_ui);
	if (s_vpn->service_type)
		g_free (s_vpn->service_type);
	s_vpn->service_type = g_strdup (svc_name);

	return TRUE;
}

static gboolean
vpn_druid_vpn_confirm_page_finish (void *druidpage,
                                   GtkWidget *widget,
                                   gpointer user_data)
{
	NetworkManagerVpnUI *vpn_ui;
	NMConnection *connection;
	gboolean error = TRUE;

	connection = new_nm_connection_vpn ();

	vpn_ui = (NetworkManagerVpnUI *) g_slist_nth_data (vpn_types, gtk_combo_box_get_active (vpn_type_combo_box));
	vpn_ui->fill_connection (vpn_ui, connection);

	if (fixup_nm_connection_vpn (connection, vpn_ui)) {
		add_vpn_connection (connection);
		error = FALSE;		
	}

	clear_vpn_details_widget ();

#if GTK_CHECK_VERSION(2, 10, 0)
	gtk_widget_hide (assistant);
#else
	gtk_dialog_response (GTK_DIALOG (druid_window),
	                     error ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_APPLY);
#endif

	return FALSE;
}

static gboolean vpn_druid_cancel (void *ignored, gpointer user_data)
{
	clear_vpn_details_widget ();

#if GTK_CHECK_VERSION(2, 10, 0)
	gtk_widget_hide (assistant);
#else
	gtk_dialog_response (GTK_DIALOG (druid_window), GTK_RESPONSE_CANCEL);
#endif

	return FALSE;
}

static void
add_cb (GtkButton *button, gpointer user_data)
{
#if !GTK_CHECK_VERSION(2, 10, 0)
    gint result;
#endif

	clear_vpn_details_widget ();

#if GTK_CHECK_VERSION(2, 10, 0)
	gtk_assistant_set_current_page (GTK_ASSISTANT (assistant), 0);
	gtk_window_set_default_size (GTK_WINDOW (assistant), -1, -1);
	gtk_window_set_position (GTK_WINDOW (assistant), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_show_all (assistant);
#else
	/* auto-shrink our window */
	gnome_druid_set_page (druid, GNOME_DRUID_PAGE (druid_start_page));
	gtk_window_set_policy (GTK_WINDOW (druid_window), FALSE, FALSE, TRUE);
	gtk_widget_show_all (GTK_WIDGET (druid_window));

	result = gtk_dialog_run (GTK_DIALOG (druid_window));

	clear_vpn_details_widget ();
	gtk_widget_hide (GTK_WIDGET (druid_window));
#endif
}


static void
import_settings (const char *svc_name, const char *file)
{
	NetworkManagerVpnUI *vpn_ui;
	NMConnection *connection;

	vpn_ui = find_vpn_ui_by_service_name (svc_name);

	/* Bail out if we don't have the requested VPN implementation on our system */
	if (vpn_ui == NULL) {
		char *basename;
		GtkWidget *err_dialog;

		basename = g_path_get_basename (file);

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
		return;
	}

	/* Create a new, blank NMConnection for the import */
	connection = new_nm_connection_vpn ();
	if (vpn_ui->import_file (vpn_ui, file, connection) == FALSE)
		goto error;

	if (!fixup_nm_connection_vpn (connection, vpn_ui))
		goto error;

	add_vpn_connection (connection);
	edit_cb (NULL, NULL);
	return;

error:
	g_object_unref (connection);
}

static void 
vpn_edit_vpn_validity_changed (NetworkManagerVpnUI *vpn_ui,
                               gboolean is_valid, 
                               gpointer user_data)
{
	// FIXME: do we need to do anything here?
	gtk_dialog_set_response_sensitive (edit_dialog, GTK_RESPONSE_ACCEPT, is_valid);
}

static void
edit_cb (GtkButton *button, gpointer user_data)
{
	NetworkManagerVpnUI *vpn_ui;
	NMConnection *connection = NULL;
	NMSettingConnection *s_con;
	NMSettingVPN *s_vpn;
	gint result;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	char *cur_name = NULL;

	if ((selection = gtk_tree_view_get_selection (vpn_conn_view)) == NULL)
		return;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), 
	                    &iter, 
	                    VPNCONN_NAME_COLUMN, &cur_name,
	                    VPNCONN_CONNECTION_COLUMN, &connection,
	                    -1);
	if (!connection || !cur_name)
		return;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con) {
		g_warning ("Invalid connection received from VPN widget (no %s "
		           "setting).", NM_SETTING_CONNECTION_SETTING_NAME);
		goto error;
	}

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	if (!s_vpn) {
		g_warning ("Invalid connection received from VPN widget (no %s "
		           "setting).", NM_SETTING_VPN_SETTING_NAME);
		goto error;
	}

	vpn_ui = find_vpn_ui_by_service_name (s_vpn->service_type);
	if (!vpn_ui) {
		g_warning ("Could not find VPN service of type '%s'.",
		           s_vpn->service_type ? s_vpn->service_type : "(null)");
		goto error;
	}

	/* show appropriate child */
	clear_vpn_details_widget ();
	vpn_details_widget = vpn_ui->get_widget (vpn_ui, connection);
	vpn_ui->set_validity_changed_callback (vpn_ui,
	                                       vpn_edit_vpn_validity_changed,
	                                       NULL);
	gtk_container_add (GTK_CONTAINER (edit_dialog->vbox), vpn_details_widget);
	gtk_widget_show (vpn_details_widget);

	gtk_window_set_policy (GTK_WINDOW (edit_dialog), FALSE, FALSE, TRUE);
	gtk_widget_show (GTK_WIDGET (edit_dialog));

	result = gtk_dialog_run (GTK_DIALOG (edit_dialog));

	if (result == GTK_RESPONSE_ACCEPT) {
		GtkTreeIter iter;

		vpn_ui->fill_connection (vpn_ui, connection);

		/* A bit of validation; make sure the VPN plugin hasn't screwed around
		 * with the NMConnection at all.
		 */
		// FIXME: validate the connection with standard NMConnection validation function
		if (!fixup_nm_connection_vpn (connection, vpn_ui))
			goto error;

		write_vpn_connection_to_gconf (connection);
		if (strcmp (s_con->name, cur_name) != 0) {
			/* Update the name in the list */
			gtk_list_store_set (vpn_conn_list, &iter, 
			                    VPNCONN_NAME_COLUMN, &s_con->name
			                    -1);
		}
	}

	clear_vpn_details_widget ();
	gtk_widget_hide (GTK_WIDGET (edit_dialog));
	return;

error:
	remove_vpn_connection (connection, &iter);
	g_object_unref (connection);
}

static void
delete_cb (GtkButton *button, gpointer user_data)
{
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	NMConnection *connection;
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

	gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), &iter,
	                    VPNCONN_CONNECTION_COLUMN, &connection,
	                    -1);

	if (connection != NULL) {
		remove_vpn_connection (connection, &iter);
		g_object_unref (connection);
	}

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
	clear_vpn_details_widget ();
	return FALSE;
}

static void
export_cb (GtkButton *button, gpointer user_data)
{
	NetworkManagerVpnUI *vpn_ui;
	NMConnection *connection;
	NMSettingVPN *s_vpn;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	if ((selection = gtk_tree_view_get_selection (vpn_conn_view)) == NULL)
		return;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (vpn_conn_list), &iter,
	                    VPNCONN_CONNECTION_COLUMN, &connection,
	                    -1);
	if (!connection)
		return;

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	if (!s_vpn) {
		g_warning ("Connection had no %s setting.", NM_SETTING_VPN_SETTING_NAME);
		return;
	}

	vpn_ui = find_vpn_ui_by_service_name (s_vpn->service_type);
	if (!vpn_ui) {
		g_warning ("Could not find the VPN service '%s' for this connection.",
		           s_vpn->service_type ? s_vpn->service_type : "(null)");
		return;
	}

	vpn_ui->export (vpn_ui, connection);
}

static void get_all_vpn_connections (void)
{
	GSList *conf_list;
	GtkTreeIter iter;

	conf_list = nm_gconf_get_all_connections (gconf_client);
	if (!conf_list) {
		g_warning ("No connections defined");
		return;
	}

	while (conf_list != NULL) {
		NMConnection *connection;
		NMSettingConnection *s_con;
		NMSettingVPN *s_vpn;
		gchar *dir = (gchar *) conf_list->data;

		connection = nm_gconf_read_connection (gconf_client, dir);
		if (!connection)
			goto next;

		g_object_set_data_full (G_OBJECT (connection),
						    "gconf-path", g_strdup (dir),
						    (GDestroyNotify) g_free);

		s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
		if (!s_con || strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME)) {
			g_object_unref (connection);
			goto next;
		}

		s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
		if (!s_vpn || !s_vpn->service_type) {
			g_object_unref (connection);
			goto next;
		}

		gtk_list_store_append (vpn_conn_list, &iter);
		gtk_list_store_set (vpn_conn_list, &iter,
				    VPNCONN_NAME_COLUMN, s_con->name,
				    VPNCONN_CONNECTION_COLUMN, connection,
				    VPNCONN_USER_CAN_EDIT_COLUMN, TRUE,
				    -1);

next:
		conf_list = g_slist_remove (conf_list, dir);
		g_free (dir);
	}
	g_slist_free (conf_list);
}

static void 
vpn_list_cursor_changed_cb (GtkTreeView *treeview,
			    gpointer user_data)
{
	update_edit_del_sensitivity ();
}

static NetworkManagerVpnUI *
load_properties_module (const char *path)
{
	GModule *module;
	NetworkManagerVpnUI* (*nm_vpn_properties_factory) (void) = NULL;
	NetworkManagerVpnUI* impl;

	module = g_module_open (path, G_MODULE_BIND_LAZY);
	if (module == NULL) {
		g_warning ("Cannot open module '%s'", path);
		return NULL;
	}

	if (!g_module_symbol (module, "nm_vpn_properties_factory", 
			      (gpointer) &nm_vpn_properties_factory)) {
		g_warning ("Cannot locate function 'nm_vpn_properties_factory' in '%s': %s", 
			   path, g_module_error ());
		g_module_close (module);
		return NULL;
	}

	impl = nm_vpn_properties_factory ();
	if (impl == NULL) {
		g_warning ("Function 'nm_vpn_properties_factory' in '%s' returned NULL", path);
		g_module_close (module);
		return NULL;
	}

	return impl;
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

static GSList *
load_vpn_types (void)
{
	GDir *dir;
	GSList *types = NULL;
	const char *f;
	NetworkManagerVpnUI *vpn_type;

	/* Load all VPN UI modules by inspecting .name files */
	dir = g_dir_open (VPN_NAME_FILES_DIR, 0, NULL);
	if (!dir) {
		g_warning ("Couldn't read VPN .name files directory %s.", VPN_NAME_FILES_DIR);
		return NULL;
	}

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
				vpn_type = load_properties_module (so_path);
				if (vpn_type)
					types = g_slist_append (types, vpn_type);
				g_free (so_path);
			}
		}
		g_key_file_free (keyfile);
		g_free (path);
	}
	g_dir_close (dir);

	return types;
}

static gboolean
init_app (void)
{
	GtkWidget *w;
	gchar *glade_file;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GSList *i;
    GdkColor druid_color;

	if (!vpn_get_clipboard ())
		return FALSE;

	gconf_client = gconf_client_get_default ();
	gconf_client_add_dir (gconf_client, GCONF_PATH_CONNECTIONS,
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

	vpn_types = load_vpn_types ();
	/* Bail out if we don't have any VPN implementations on our system */
	if (!vpn_types) {
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
		return FALSE;
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
	vpn_conn_list = gtk_list_store_new (VPNCONN_N_COLUMNS,
	                                    G_TYPE_STRING,
	                                    G_TYPE_OBJECT,
	                                    G_TYPE_BOOLEAN);

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
	/* fill in possible choices in the druid when adding a connection */
	vpn_type_combo_box = GTK_COMBO_BOX (gtk_combo_box_new_text ());
	for (i = vpn_types; i != NULL; i = g_slist_next (i)) {
		NetworkManagerVpnUI *vpn_ui = i->data;
		gtk_combo_box_append_text (vpn_type_combo_box, vpn_ui->get_display_name (vpn_ui));
	}
	gtk_combo_box_set_active (vpn_type_combo_box, 0);

    gdk_color_parse ("#7590AE", &druid_color);

    /* Druid Page 1 - Create VPN Connection */
    gchar *msg1 = g_strdup (_("This assistant will guide you through the creation of a connection to a Virtual Private Network (VPN)."));
    gchar *msg2 = g_strdup (_("It will require some information, such as IP addresses and secrets.  Please see your system administrator to obtain this information.")); 
    gchar *msg = g_strdup_printf ("%s\n\n%s", msg1, msg2); 

#if GTK_CHECK_VERSION(2, 10, 0)
    assistant = gtk_assistant_new ();
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
	w = gnome_druid_page_edge_new_with_vals (GNOME_EDGE_START, TRUE,
                                             _("Create VPN Connection"),
                                             msg, NULL, NULL, NULL));
    druid_start_page = GNOME_DRUID_PAGE_EDGE (w);
    gnome_druid_page_edge_set_bg_color (druid_start_page, &druid_color);
    gnome_druid_page_edge_set_logo_bg_color (druid_start_page, &druid_color);
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

    assistant_conn_type_page = gtk_vbox_new (12, FALSE);

    gtk_box_pack_start (GTK_BOX (assistant_conn_type_page), conn_type_label, TRUE, FALSE, 0);
    gtk_box_pack_end (GTK_BOX (assistant_conn_type_page), GTK_WIDGET (vpn_type_combo_box), FALSE, FALSE, 0);

    gtk_assistant_append_page (GTK_ASSISTANT (assistant), assistant_conn_type_page);
    gtk_assistant_set_page_title (GTK_ASSISTANT (assistant), assistant_conn_type_page, head);
    gtk_assistant_set_page_complete (GTK_ASSISTANT (assistant), assistant_conn_type_page, TRUE);

#else
	w = gnome_druid_page_standard_new_with_vals (head, NULL, NULL);
    druid_conn_type_page = GNOME_DRUID_PAGE_STANDARD (w);
    gnome_druid_page_standard_set_background (druid_conn_type_page, &druid_color);
    gnome_druid_page_standard_set_logo_background (druid_conn_type_page, &druid_color);
    gnome_druid_page_standard_append_item (druid_conn_type_page,
            _("Choose which type of VPN connection you wish to create:"),
            GTK_WIDGET (vpn_type_combo_box), NULL);
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
	w = gnome_druid_page_standard_new_with_vals (head, NULL, NULL);
    druid_details_page = GNOME_DRUID_PAGE_STANDARD (w);
    gnome_druid_page_standard_set_background (druid_details_page, &druid_color);
    gnome_druid_page_standard_set_logo_background (druid_details_page, &druid_color);
	gtk_signal_connect_after (GTK_OBJECT (druid_details_page), "prepare", GTK_SIGNAL_FUNC (vpn_druid_vpn_details_page_prepare), NULL);
	gtk_signal_connect_after (GTK_OBJECT (druid_details_page), "next", GTK_SIGNAL_FUNC (vpn_druid_vpn_details_page_next), NULL);
	vpn_type_details = GTK_VBOX (druid_details_page->vbox);
#endif

    gtk_widget_show (GTK_WIDGET (vpn_type_details));
    g_free (secondpage);
    g_free (head);
    g_free (msg);

#if GTK_CHECK_VERSION(2, 10, 0)
    assistant_confirm_page = gtk_label_new (NULL);
    gtk_assistant_append_page (GTK_ASSISTANT (assistant), assistant_confirm_page);

    gtk_assistant_set_page_type (GTK_ASSISTANT (assistant), assistant_confirm_page, GTK_ASSISTANT_PAGE_CONFIRM);
    gtk_assistant_set_page_title (GTK_ASSISTANT (assistant), assistant_confirm_page,  _("Finished Create VPN Connection"));
    gtk_assistant_set_page_complete (GTK_ASSISTANT (assistant), assistant_confirm_page, TRUE);
    gtk_assistant_set_page_side_image (GTK_ASSISTANT (assistant), assistant_confirm_page, pixbuf);
    gdk_pixbuf_unref (pixbuf);
#else
    /* Druid Page 4 - Finished Create VPN Connection */
	w = gnome_druid_page_edge_new_with_vals (GNOME_EDGE_FINISH, TRUE,
	                                        _("Finished Create VPN Connection"),
	                                        "", NULL, NULL, NULL);
    druid_confirm_page = GNOME_DRUID_PAGE_EDGE (w);
    gnome_druid_page_edge_set_bg_color (druid_confirm_page, &druid_color);
    gnome_druid_page_edge_set_logo_bg_color (druid_confirm_page, &druid_color);
	gtk_signal_connect_after (GTK_OBJECT (druid_confirm_page), "prepare", GTK_SIGNAL_FUNC (vpn_druid_vpn_confirm_page_prepare), NULL);
	gtk_signal_connect_after (GTK_OBJECT (druid_confirm_page), "finish", GTK_SIGNAL_FUNC (vpn_druid_vpn_confirm_page_finish), NULL);

	/* Druid */
    druid = GNOME_DRUID (gnome_druid_new ());
    gtk_signal_connect (GTK_OBJECT (druid), "cancel", GTK_SIGNAL_FUNC (vpn_druid_cancel), NULL);
    gnome_druid_append_page (druid, GNOME_DRUID_PAGE (druid_start_page));
    gnome_druid_append_page (druid, GNOME_DRUID_PAGE (druid_conn_type_page));
    gnome_druid_append_page (druid, GNOME_DRUID_PAGE (druid_details_page));
    gnome_druid_append_page (druid, GNOME_DRUID_PAGE (druid_confirm_page));

	druid_window = GTK_DIALOG (gtk_dialog_new_with_buttons (_("Create VPN Connection"),
							       NULL,
							       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							       NULL));
	gtk_dialog_set_has_separator (GTK_DIALOG (druid_window), FALSE);
	gtk_container_add (GTK_CONTAINER (druid_window->vbox), GTK_WIDGET (druid));
#endif

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
	DBusGConnection *ignore;
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

	/* Hack to init the dbus-glib type system */
	ignore = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	dbus_g_connection_unref (ignore);

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
