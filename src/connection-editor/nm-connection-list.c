/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
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
 * (C) Copyright 2007 - 2009 Red Hat, Inc.
 */

#include <config.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-connection.h>
#include <nm-setting.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-setting-vpn.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-setting-pppoe.h>
#include <nm-setting-ppp.h>
#include <nm-setting-serial.h>
#include <nm-vpn-plugin-ui-interface.h>
#include <nm-utils.h>
#include <nm-settings-system-interface.h>

#include "ce-page.h"
#include "page-wired.h"
#include "page-wireless.h"
#include "page-mobile.h"
#include "page-dsl.h"
#include "page-vpn.h"
#include "nm-connection-editor.h"
#include "nm-connection-list.h"
#include "gconf-helpers.h"
#include "utils.h"
#include "vpn-helpers.h"

G_DEFINE_TYPE (NMConnectionList, nm_connection_list, G_TYPE_OBJECT)

enum {
	LIST_DONE,
	LIST_LAST_SIGNAL
};

static guint list_signals[LIST_LAST_SIGNAL] = { 0 };

#define COL_ID 			0
#define COL_LAST_USED	1
#define COL_TIMESTAMP	2
#define COL_CONNECTION	3

typedef struct {
	NMConnectionList *list;
	GtkTreeView *treeview;
	GtkWindow *list_window;
	GtkWidget *button;
	PageNewConnectionFunc new_func;
} ActionInfo;

static void
error_dialog (GtkWindow *parent, const char *heading, const char *format, ...)
{
	GtkWidget *dialog;
	va_list args;
	char *message;

	dialog = gtk_message_dialog_new (parent,
	                                 GTK_DIALOG_DESTROY_WITH_PARENT,
	                                 GTK_MESSAGE_ERROR,
	                                 GTK_BUTTONS_CLOSE,
	                                 "%s", heading);

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	g_free (message);

	gtk_widget_show_all (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static NMSettingsConnectionInterface *
get_active_connection (GtkTreeView *treeview)
{
	GtkTreeSelection *selection;
	GList *selected_rows;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	NMSettingsConnectionInterface *connection = NULL;

	selection = gtk_tree_view_get_selection (treeview);
	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
	if (!selected_rows)
		return NULL;

	if (gtk_tree_model_get_iter (model, &iter, (GtkTreePath *) selected_rows->data))
		gtk_tree_model_get (model, &iter, COL_CONNECTION, &connection, -1);

	/* free memory */
	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);

	return connection;
}

#define TV_TYPE_TAG "ctype"

static GtkTreeView *
get_treeview_for_type (NMConnectionList *list, GType ctype)
{
	GSList *iter;

	for (iter = list->treeviews; iter; iter = g_slist_next (iter)) {
		GtkTreeView *candidate = GTK_TREE_VIEW (iter->data);
		GType candidate_type;

		candidate_type = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (candidate), TV_TYPE_TAG));
		if (candidate_type == ctype)
			return candidate;
	}

	return NULL;
}

static GtkListStore *
get_model_for_connection (NMConnectionList *list, NMSettingsConnectionInterface *connection)
{
	NMSettingConnection *s_con;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	const char *str_type;

	s_con = (NMSettingConnection *) nm_connection_get_setting (NM_CONNECTION (connection), NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);
	str_type = nm_setting_connection_get_connection_type (s_con);

	if (!str_type) {
		g_warning ("Ignoring incomplete connection");
		return NULL;
	}

	if (!strcmp (str_type, NM_SETTING_CDMA_SETTING_NAME))
		str_type = NM_SETTING_GSM_SETTING_NAME;

	treeview = get_treeview_for_type (list, nm_connection_lookup_setting_type (str_type));
	if (!treeview) {
		g_warning ("No registered treeview for connection type '%s'", str_type);
		return NULL;
	}

	model = gtk_tree_view_get_model (treeview);
	if (GTK_IS_TREE_MODEL_SORT (model))
		return GTK_LIST_STORE (gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (model)));

	return GTK_LIST_STORE (model);
}

static gboolean
get_iter_for_connection (GtkTreeModel *model,
                         NMSettingsConnectionInterface *connection,
                         GtkTreeIter *iter)
{
	GtkTreeIter temp_iter;
	gboolean found = FALSE;

	if (!gtk_tree_model_get_iter_first (model, &temp_iter))
		return FALSE;

	do {
		NMSettingsConnectionInterface *candidate = NULL;

		gtk_tree_model_get (model, &temp_iter, COL_CONNECTION, &candidate, -1);
		if (candidate && (candidate == connection)) {
			*iter = temp_iter;
			found = TRUE;
			break;
		}
	} while (gtk_tree_model_iter_next (model, &temp_iter));

	return found;
}

static char *
format_last_used (guint64 timestamp)
{
	GTimeVal now_tv;
	GDate *now, *last;
	char *last_used = NULL;

	if (!timestamp)
		return g_strdup (_("never"));

	g_get_current_time (&now_tv);
	now = g_date_new ();
	g_date_set_time_val (now, &now_tv);

	last = g_date_new ();
	g_date_set_time_t (last, (time_t) timestamp);

	/* timestamp is now or in the future */
	if (now_tv.tv_sec <= timestamp) {
		last_used = g_strdup (_("now"));
		goto out;
	}

	if (g_date_compare (now, last) <= 0) {
		guint minutes, hours;

		/* Same day */

		minutes = (now_tv.tv_sec - timestamp) / 60;
		if (minutes == 0) {
			last_used = g_strdup (_("now"));
			goto out;
		}

		hours = (now_tv.tv_sec - timestamp) / 3600;
		if (hours == 0) {
			/* less than an hour ago */
			last_used = g_strdup_printf (ngettext ("%d minute ago", "%d minutes ago", minutes), minutes);
			goto out;
		}

		last_used = g_strdup_printf (ngettext ("%d hour ago", "%d hours ago", hours), hours);
	} else {
		guint days, months, years;

		days = g_date_get_julian (now) - g_date_get_julian (last);
		if (days == 0) {
			last_used = g_strdup ("today");
			goto out;
		}

		months = days / 30;
		if (months == 0) {
			last_used = g_strdup_printf (ngettext ("%d day ago", "%d days ago", days), days);
			goto out;
		}

		years = days / 365;
		if (years == 0) {
			last_used = g_strdup_printf (ngettext ("%d month ago", "%d months ago", months), months);
			goto out;
		}

		last_used = g_strdup_printf (ngettext ("%d year ago", "%d years ago", years), years);
	}

out:
	g_date_free (now);
	g_date_free (last);
	return last_used;
}

static void
update_connection_row (GtkListStore *store,
                       GtkTreeIter *iter,
                       NMSettingsConnectionInterface *connection)
{
	NMSettingConnection *s_con;
	char *last_used;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (NM_CONNECTION (connection), NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	last_used = format_last_used (nm_setting_connection_get_timestamp (s_con));
	gtk_list_store_set (store, iter,
	                    COL_ID, nm_setting_connection_get_id (s_con),
	                    COL_LAST_USED, last_used,
	                    COL_TIMESTAMP, nm_setting_connection_get_timestamp (s_con),
	                    COL_CONNECTION, connection,
	                    -1);
	g_free (last_used);
}


/**********************************************/
/* Connection deleting */

typedef void (*DeleteResultFunc) (NMConnectionList *list,
                                  GError *error,
                                  gpointer user_data);

typedef struct {
	NMConnectionList *list;
	NMSettingsConnectionInterface *original;
	DeleteResultFunc callback;
	gpointer callback_data;
} DeleteInfo;

static void
delete_cb (NMSettingsConnectionInterface *connection_iface,
           GError *error,
           gpointer user_data)
{
	DeleteInfo *info = user_data;
	NMConnection *connection = NM_CONNECTION (connection_iface);
	NMConnectionScope scope;

	scope = nm_connection_get_scope (connection);
	if (!error && (scope == NM_CONNECTION_SCOPE_USER)) {
		NMSettingConnection *s_con;
		NMSettingVPN *s_vpn;
		NMVpnPluginUiInterface *plugin;
		GError *vpn_error = NULL;

		/* Clean up keyring keys */
		nm_gconf_clear_keyring_items (connection);

		s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
		g_assert (s_con);

		/* Clean up VPN secrets and any plugin-specific data */
		if (strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_VPN_SETTING_NAME))
			goto done;

		s_vpn = (NMSettingVPN *) nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN);
		if (!s_vpn)
			goto done;

		plugin = vpn_get_plugin_by_service (nm_setting_vpn_get_service_type (s_vpn));
		if (!plugin)
			goto done;

		if (!nm_vpn_plugin_ui_interface_delete_connection (plugin, connection, &vpn_error)) {
			g_warning ("%s: couldn't clean up VPN connection on delete: (%d) %s",
			           __func__,
			           vpn_error ? vpn_error->code : -1,
			           (vpn_error && vpn_error->message) ? vpn_error->message : "unknown");
			g_clear_error (&vpn_error);
		}
	}

done:
	info->callback (info->list, error, info->callback_data);
	g_free (info);
}

static void
delete_connection (NMConnectionList *list,
                   NMSettingsConnectionInterface *connection,
                   DeleteResultFunc callback,
                   gpointer user_data)
{
	DeleteInfo *info;

	info = g_malloc0 (sizeof (DeleteInfo));
	info->list = list;
	info->callback = callback;
	info->callback_data = user_data;

	nm_settings_connection_interface_delete (connection, delete_cb, info);
}

/**********************************************/
/* Connection adding */

typedef void (*AddResultFunc) (NMConnectionList *list,
                               GError *error,
                               gpointer user_data);

typedef struct {
	NMConnectionList *list;
	NMConnectionEditor *editor;
	AddResultFunc callback;
	gpointer callback_data;
} AddInfo;

static void
add_cb (NMSettingsInterface *settings,
        GError *error,
        gpointer user_data)
{
	AddInfo *info = user_data;
	NMConnection *connection;

	if (!error) {
		/* Let the VPN plugin save its secrets */
		connection = nm_connection_editor_get_connection (info->editor);
		if (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_USER)
			nm_connection_editor_save_vpn_secrets (info->editor);
	}

	info->callback (info->list, error, info->callback_data);
	g_free (info);
}

static void
add_connection (NMConnectionList *self,
                NMConnectionEditor *editor,
                AddResultFunc callback,
                gpointer callback_data)
{
	NMSettingsInterface *settings = NULL;
	NMConnection *connection;
	NMConnectionScope scope;
	AddInfo *info;

	info = g_malloc0 (sizeof (AddInfo));
	info->list = self;
	info->editor = editor;
	info->callback = callback;
	info->callback_data = callback_data;

	connection = nm_connection_editor_get_connection (editor);
	g_assert (connection);
	scope = nm_connection_get_scope (connection);
	if (scope == NM_CONNECTION_SCOPE_SYSTEM)
		settings = NM_SETTINGS_INTERFACE (self->system_settings);
	else if (scope == NM_CONNECTION_SCOPE_USER)
		settings = NM_SETTINGS_INTERFACE (self->gconf_settings);
	else
		g_assert_not_reached ();
	
	utils_fill_connection_certs (connection);
	nm_settings_interface_add_connection (settings, connection, add_cb, info);
	utils_clear_filled_connection_certs (connection);
}

/**********************************************/
/* Connection updating */

typedef void (*UpdateResultFunc) (NMConnectionList *list,
                                  GError *error,
                                  gpointer user_data);

typedef struct {
	NMConnectionList *list;
	NMConnectionEditor *editor;
	NMSettingsConnectionInterface *original;
	NMConnection *modified;
	UpdateResultFunc callback;
	gpointer callback_data;
} UpdateInfo;

static void
update_complete (UpdateInfo *info, GError *error)
{
	info->callback (info->list, error, info->callback_data);
	g_object_unref (info->original);
	g_object_unref (info->modified);
	g_free (info);
}

static void
update_remove_result_cb (NMConnectionList *list,
                         GError *error,
                         gpointer user_data)
{
	UpdateInfo *info = user_data;

	update_complete (info, error);
}

static void
update_add_result_cb (NMConnectionList *list, GError *error, gpointer user_data)
{
	UpdateInfo *info = user_data;

	if (error) {
		update_complete (info, error);
		return;
	}

	/* Now try to remove the original connection */
	delete_connection (list, info->original, update_remove_result_cb, info);
}

static void
update_cb (NMSettingsConnectionInterface *connection,
           GError *error,
           gpointer user_data)
{
	UpdateInfo *info = user_data;

	if (!error) {
		/* Save user-connection vpn secrets */
		if (nm_connection_get_scope (NM_CONNECTION (connection)) == NM_CONNECTION_SCOPE_USER)
			nm_connection_editor_save_vpn_secrets (info->editor);
	}

	update_complete (info, error);
}

static void
update_connection (NMConnectionList *list,
                   NMConnectionEditor *editor,
                   NMSettingsConnectionInterface *original,
                   UpdateResultFunc callback,
                   gpointer user_data)
{
	NMConnection *modified = nm_connection_editor_get_connection (editor);
	NMConnectionScope original_scope;
	UpdateInfo *info;

	info = g_malloc0 (sizeof (UpdateInfo));
	info->list = list;
	info->editor = editor;
	info->original = g_object_ref (original);
	info->modified = g_object_ref (modified);
	info->callback = callback;
	info->callback_data = user_data;

	original_scope = nm_connection_get_scope (NM_CONNECTION (original));
	if (nm_connection_get_scope (modified) == original_scope) {
		/* The easy part: Connection is updated */
		GHashTable *new_settings;
		GError *error = NULL;

		utils_fill_connection_certs (modified);
		new_settings = nm_connection_to_hash (modified);
		utils_clear_filled_connection_certs (modified);

		if (!nm_connection_replace_settings (NM_CONNECTION (original),
		                                     new_settings,
		                                     &error)) {
			callback (list, error, user_data);
			g_error_free (error);
		} else {
			/* Hack; make sure that gconf private values are copied */
			nm_gconf_copy_private_connection_values (NM_CONNECTION (original), modified);
			nm_settings_connection_interface_update (original, update_cb, info);
		}
	} else {
		/* The hard part: Connection scope changed:
		 * Add the modified connection to the new settings service, then delete
		 * the original connection from the old settings service.
		 */
		add_connection (list, editor, update_add_result_cb, info);
	}
}

/**********************************************/
/* dialog/UI handling stuff */

static void
add_finished_cb (NMConnectionList *list, GError *error, gpointer user_data)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (user_data);
	GtkWindow *parent;

	if (error) {
		parent = nm_connection_editor_get_window (editor);
		error_dialog (parent, _("Connection add failed"), "%s", error->message);
	}

	g_hash_table_remove (list->editors, nm_connection_editor_get_connection (editor));
}


static void
add_response_cb (NMConnectionEditor *editor, gint response, GError *error, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMConnection *connection;
	const char *message = _("An unknown error ocurred.");

	if (response == GTK_RESPONSE_OK) {
		add_connection (info->list, editor, add_finished_cb, editor);
		return;
	} else if (response == GTK_RESPONSE_NONE) {
		if (error && error->message)
			message = error->message;
		error_dialog (GTK_WINDOW (editor->window),
		              _("Error initializing editor"),
		              "%s", message);
	}

	connection = nm_connection_editor_get_connection (editor);
	g_hash_table_remove (info->list->editors, connection);
}

static void
really_add_connection (NMConnection *connection,
                       gboolean canceled,
                       GError *error,
                       gpointer user_data)
{
	ActionInfo *info = user_data;
	NMConnectionEditor *editor;
	GError *editor_error = NULL;
	const char *message = _("The connection editor dialog could not be initialized due to an unknown error.");
	gboolean can_modify = FALSE;

	g_return_if_fail (info != NULL);

	if (canceled)
		return;

	if (!connection) {
		error_dialog (info->list_window,
		              _("Could not create new connection"),
		              "%s",
		              (error && error->message) ? error->message : message);
		return;
	}

	g_object_get (G_OBJECT (info->list->system_settings),
	              NM_SETTINGS_SYSTEM_INTERFACE_CAN_MODIFY,
	              &can_modify,
	              NULL);

	editor = nm_connection_editor_new (connection, can_modify, &error);
	if (!editor) {
		error_dialog (info->list_window,
		              _("Could not edit new connection"),
		              "%s",
		              (editor_error && editor_error->message) ? editor_error->message : message);
		g_clear_error (&editor_error);
		return;
	}

	g_signal_connect (G_OBJECT (editor), "done", G_CALLBACK (add_response_cb), info);
	g_hash_table_insert (info->list->editors, connection, editor);

	nm_connection_editor_run (editor);
}

static GSList *
page_get_connections (gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;

	return g_slist_concat (nm_settings_interface_list_connections (NM_SETTINGS_INTERFACE (info->list->system_settings)),
	                       nm_settings_interface_list_connections (NM_SETTINGS_INTERFACE (info->list->gconf_settings)));
}

static void
add_clicked (GtkButton *button, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMConnectionList *list = info->list;
	GType ctype;

	if (!info->new_func) {
		ctype = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (info->treeview), TV_TYPE_TAG));
		g_warning ("No new-connection function registered for type '%s'",
		           g_type_name (ctype));
		return;
	}

	(*(info->new_func)) (GTK_WINDOW (list->dialog),
	                     really_add_connection,
	                     page_get_connections,
	                     info);
}

typedef struct {
	NMConnectionList *list;
	NMConnectionEditor *editor;
	NMSettingsConnectionInterface *original_connection;
} EditInfo;

static void
connection_updated_cb (NMConnectionList *list,
                       GError *error,
                       gpointer user_data)
{
	EditInfo *info = user_data;

	if (!error) {
		GtkListStore *store;
		GtkTreeIter iter;

		store = get_model_for_connection (list, info->original_connection);
		g_assert (store);
		if (get_iter_for_connection (GTK_TREE_MODEL (store), info->original_connection, &iter))
			update_connection_row (store, &iter, info->original_connection);
	}

	g_object_unref (info->original_connection);
	g_free (info);
}

static void
edit_done_cb (NMConnectionEditor *editor, gint response, GError *error, gpointer user_data)
{
	EditInfo *info = user_data;
	const char *message = _("An unknown error ocurred.");

	g_hash_table_remove (info->list->editors, info->original_connection);

	if (response == GTK_RESPONSE_NONE) {
		if (error && error->message)
			message = error->message;
		error_dialog (GTK_WINDOW (editor->window), _("Error initializing editor"), "%s", message);
		g_free (info);
		return;
	}

	if (response == GTK_RESPONSE_OK) {
		NMConnection *connection;
		GError *edit_error = NULL;
		gboolean success;

		connection = nm_connection_editor_get_connection (editor);

		utils_fill_connection_certs (connection);
		success = nm_connection_verify (connection, &edit_error);
		utils_clear_filled_connection_certs (connection);

		if (success) {
			update_connection (info->list, editor, info->original_connection,
			                   connection_updated_cb, info);
		} else {
			g_warning ("%s: invalid connection after update: bug in the "
			           "'%s' / '%s' invalid: %d",
			           __func__,
			           g_type_name (nm_connection_lookup_setting_type_by_quark (edit_error->domain)),
			           edit_error->message, edit_error->code);
			connection_updated_cb (info->list, edit_error, info);
			g_error_free (edit_error);
		}
	}
}

static void
do_edit (ActionInfo *info)
{
	NMSettingsConnectionInterface *connection;
	NMConnection *duplicated;
	NMConnectionEditor *editor;
	EditInfo *edit_info;
	GError *error = NULL;
	const char *message = _("The connection editor dialog could not be initialized due to an unknown error.");
	gboolean can_modify = FALSE;

	connection = get_active_connection (info->treeview);
	g_return_if_fail (connection != NULL);

	/* Don't allow two editors for the same connection */
	editor = (NMConnectionEditor *) g_hash_table_lookup (info->list->editors, connection);
	if (editor) {
		nm_connection_editor_present (editor);
		return;
	}

	g_object_get (G_OBJECT (info->list->system_settings),
	              NM_SETTINGS_SYSTEM_INTERFACE_CAN_MODIFY, &can_modify,
	              NULL);

	duplicated = nm_gconf_connection_duplicate (NM_CONNECTION (connection));
	editor = nm_connection_editor_new (duplicated, can_modify, &error);
	g_object_unref (duplicated);

	if (!editor) {
		error_dialog (info->list_window,
		              _("Could not edit connection"),
		              "%s",
		              (error && error->message) ? error->message : message);
		return;
	}

	edit_info = g_malloc0 (sizeof (EditInfo));
	edit_info->list = info->list;
	edit_info->editor = editor;
	edit_info->original_connection = g_object_ref (connection);

	g_signal_connect (editor, "done", G_CALLBACK (edit_done_cb), edit_info);
	g_hash_table_insert (info->list->editors, connection, editor);

	nm_connection_editor_run (editor);
}

static void
edit_connection_cb (GtkButton *button, gpointer user_data)
{
	do_edit ((ActionInfo *) user_data);
}

static void
delete_result_cb (NMConnectionList *list,
                  GError *error,
                  gpointer user_data)
{
	GtkWindow *parent = GTK_WINDOW (user_data);

	if (error)
		error_dialog (parent, _("Connection delete failed"), "%s", error->message);
}

static void
delete_clicked (GtkButton *button, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMSettingsConnectionInterface *connection;
	NMSettingConnection *s_con;
	GtkWidget *dialog;
	const char *id;
	guint result;

	connection = get_active_connection (info->treeview);
	g_return_if_fail (connection != NULL);

	s_con = (NMSettingConnection *) nm_connection_get_setting (NM_CONNECTION (connection), NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);
	id = nm_setting_connection_get_id (s_con);

	dialog = gtk_message_dialog_new (GTK_WINDOW (info->list->dialog),
	                                 GTK_DIALOG_DESTROY_WITH_PARENT,
	                                 GTK_MESSAGE_QUESTION,
	                                 GTK_BUTTONS_NONE,
	                                 _("Are you sure you wish to delete the connection %s?"),
	                                 id);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
	                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                        GTK_STOCK_DELETE, GTK_RESPONSE_YES,
	                        NULL);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (info->list->dialog));

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (result == GTK_RESPONSE_YES)
		delete_connection (info->list, connection, delete_result_cb, GTK_WINDOW (info->list->dialog));
}

typedef guint32 PolKitResult;
#define POLKIT_RESULT_UNKNOWN 0

static gboolean
check_sensitivity (ActionInfo *info, PolKitResult pk_result)
{
	gboolean sensitive = TRUE;
	NMSettingsConnectionInterface *connection = NULL;

	connection = get_active_connection (info->treeview);
	if (!connection)
		return FALSE;

	if (nm_connection_get_scope (NM_CONNECTION (connection)) != NM_CONNECTION_SCOPE_SYSTEM)
		return TRUE;

#if 0
	if (pk_result == POLKIT_RESULT_UNKNOWN)
		pk_result = polkit_gnome_action_get_polkit_result (info->gnome_action);

	if (pk_result == POLKIT_RESULT_NO || pk_result == POLKIT_RESULT_UNKNOWN)
		sensitive = FALSE;
#endif

	return sensitive;
}

#if 0
static void
system_pk_result_changed_cb (PolKitGnomeAction *gnome_action,
                             PolKitResult result,
                             ActionInfo *info)
{
	gtk_widget_set_sensitive (info->button, check_sensitivity (info, result));
}
#endif

static void
pk_button_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	GtkTreeIter iter;
	GtkTreeModel *model;
	NMSettingsConnectionInterface *connection;
	NMSettingConnection *s_con;
	gboolean can_do_action = FALSE;
	gboolean req_privs = FALSE;

	if (!gtk_tree_selection_get_selected (selection, &model, &iter))
		goto done;

	connection = get_active_connection (info->treeview);
	if (!connection)
		goto done;

	s_con = (NMSettingConnection *) nm_connection_get_setting (NM_CONNECTION (connection),
	                                                           NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);

	if (nm_connection_get_scope (NM_CONNECTION (connection)) != NM_CONNECTION_SCOPE_SYSTEM)
		can_do_action = !nm_setting_connection_get_read_only (s_con);
	else {
		if (!nm_setting_connection_get_read_only (s_con))
			can_do_action = check_sensitivity (info, POLKIT_RESULT_UNKNOWN);
		req_privs = TRUE;
	}

done:
	gtk_widget_set_sensitive (info->button, can_do_action);
#if 0
	g_object_set (info->gnome_action, "polkit-action", req_privs ? info->action : NULL, NULL);
	g_object_set (info->gnome_action, "master-sensitive", can_do_action, NULL);
#endif
}

static void
vpn_list_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMVpnPluginUiInterface *plugin;
	NMSettingsConnectionInterface *connection;
	NMSettingVPN *s_vpn;
	const char *service_type;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint32 caps;
	gboolean supported = FALSE;

	if (!gtk_tree_selection_get_selected (selection, &model, &iter))
		goto done;

	connection = get_active_connection (info->treeview);
	if (!connection)
		goto done;

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (NM_CONNECTION (connection), NM_TYPE_SETTING_VPN));
	service_type = s_vpn ? nm_setting_vpn_get_service_type (s_vpn) : NULL;

	if (!service_type)
		goto done;

	plugin = vpn_get_plugin_by_service (service_type);
	if (!plugin)
		goto done;

	caps = nm_vpn_plugin_ui_interface_get_capabilities (plugin);
	if (caps & NM_VPN_PLUGIN_UI_CAPABILITY_EXPORT)
		supported = TRUE;

done:
	gtk_widget_set_sensitive (info->button, supported);
}

static void
import_success_cb (NMConnection *connection, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMConnectionEditor *editor;
	NMSettingConnection *s_con;
	NMSettingVPN *s_vpn;
	const char *service_type;
	char *s;
	GError *error = NULL;
	const char *message = _("The connection editor dialog could not be initialized due to an unknown error.");
	gboolean can_modify = FALSE;

	/* Basic sanity checks of the connection */
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con) {
		s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
		nm_connection_add_setting (connection, NM_SETTING (s_con));
	}

	s = (char *) nm_setting_connection_get_id (s_con);
	if (!s) {
		GSList *connections;

		connections = nm_settings_interface_list_connections (NM_SETTINGS_INTERFACE (info->list->system_settings));
		connections = g_slist_concat (connections,
		                              nm_settings_interface_list_connections (NM_SETTINGS_INTERFACE (info->list->gconf_settings)));

		s = utils_next_available_name (connections, _("VPN connection %d"));
		g_object_set (s_con, NM_SETTING_CONNECTION_ID, s, NULL);
		g_free (s);

		g_slist_free (connections);
	}

	s = (char *) nm_setting_connection_get_connection_type (s_con);
	if (!s || strcmp (s, NM_SETTING_VPN_SETTING_NAME))
		g_object_set (s_con, NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME, NULL);

	s = (char *) nm_setting_connection_get_uuid (s_con);
	if (!s) {
		s = nm_utils_uuid_generate ();
		g_object_set (s_con, NM_SETTING_CONNECTION_UUID, s, NULL);
		g_free (s);
	}

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	service_type = s_vpn ? nm_setting_vpn_get_service_type (s_vpn) : NULL;

	if (!service_type || !strlen (service_type)) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (NULL,
		                                 GTK_DIALOG_DESTROY_WITH_PARENT,
		                                 GTK_MESSAGE_ERROR,
		                                 GTK_BUTTONS_OK,
		                                 _("Cannot import VPN connection"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
		                                 _("The VPN plugin failed to import the VPN connection correctly\n\nError: no VPN service type."));
		gtk_window_set_transient_for (GTK_WINDOW (dialog), info->list_window);
		g_signal_connect (dialog, "delete-event", G_CALLBACK (gtk_widget_destroy), NULL);
		g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
		gtk_widget_show_all (dialog);
		gtk_window_present (GTK_WINDOW (dialog));
		return;
	}

	g_object_get (G_OBJECT (info->list->system_settings),
	              NM_SETTINGS_SYSTEM_INTERFACE_CAN_MODIFY,
	              &can_modify,
	              NULL);

	editor = nm_connection_editor_new (connection, can_modify, &error);
	if (!editor) {
		error_dialog (info->list_window,
		              _("Could not edit imported connection"),
		              "%s",
		              (error && error->message) ? error->message : message);
		return;
	}

	g_signal_connect (G_OBJECT (editor), "done", G_CALLBACK (add_response_cb), info);
	g_hash_table_insert (info->list->editors, connection, editor);

	nm_connection_editor_run (editor);
}

static void
import_vpn_cb (GtkButton *button, gpointer user_data)
{
	vpn_import (import_success_cb, (ActionInfo *) user_data);
}

static void
export_vpn_cb (GtkButton *button, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMSettingsConnectionInterface *connection;

	connection = get_active_connection (info->treeview);
	if (connection)
		vpn_export (NM_CONNECTION (connection));
}

static void
connection_double_clicked_cb (GtkTreeView *tree_view,
                              GtkTreePath *path,
                              GtkTreeViewColumn *column,
                              gpointer user_data)
{
	ActionInfo *info = user_data;

	if (GTK_WIDGET_SENSITIVE (info->button))
		gtk_button_clicked (GTK_BUTTON (info->button));
}

static void
dialog_response_cb (GtkDialog *dialog, guint response, gpointer user_data)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
nm_connection_list_init (NMConnectionList *list)
{
#if 0
	list->system_action = polkit_action_new ();
	polkit_action_set_action_id (list->system_action, "org.freedesktop.network-manager-settings.system.modify");
#endif
}

static void
dispose (GObject *object)
{
	NMConnectionList *list = NM_CONNECTION_LIST (object);

	if (list->dialog)
		gtk_widget_hide (list->dialog);

	if (list->editors)
		g_hash_table_destroy (list->editors);

	if (list->wired_icon)
		g_object_unref (list->wired_icon);
	if (list->wireless_icon)
		g_object_unref (list->wireless_icon);
	if (list->wwan_icon)
		g_object_unref (list->wwan_icon);
	if (list->vpn_icon)
		g_object_unref (list->vpn_icon);
	if (list->unknown_icon)
		g_object_unref (list->unknown_icon);

#if 0
	polkit_action_unref (list->system_action);
#endif

	if (list->dialog)
		gtk_widget_destroy (list->dialog);
	if (list->gui)
		g_object_unref (list->gui);
	if (list->client)
		g_object_unref (list->client);

	g_slist_free (list->treeviews);

	if (list->gconf_settings)
		g_object_unref (list->gconf_settings);

	if (list->system_settings)
		g_object_unref (list->system_settings);

	G_OBJECT_CLASS (nm_connection_list_parent_class)->dispose (object);
}

static void
nm_connection_list_class_init (NMConnectionListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/* virtual methods */
	object_class->dispose = dispose;

	/* Signals */
	list_signals[LIST_DONE] =
		g_signal_new ("done",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMConnectionListClass, done),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__INT,
					  G_TYPE_NONE, 1, G_TYPE_INT);
}

static GtkTreeView *
add_connection_treeview (NMConnectionList *self, const char *prefix)
{
	GtkTreeModel *model;
	GtkTreeModel *sort_model;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GValue val = { 0, };
	char *name;
	GtkTreeView *treeview;

	name = g_strdup_printf ("%s_list", prefix);
	treeview = GTK_TREE_VIEW (glade_xml_get_widget (self->gui, name));
	g_free (name);
	gtk_tree_view_set_headers_visible (treeview, TRUE);

	/* Model */
	model = GTK_TREE_MODEL (gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_OBJECT));
	sort_model = gtk_tree_model_sort_new_with_model (model);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sort_model),
	                                      COL_TIMESTAMP, GTK_SORT_DESCENDING);
	gtk_tree_view_set_model (treeview, sort_model);

	/* Name column */
	gtk_tree_view_insert_column_with_attributes (treeview,
	                                             -1, _("Name"), gtk_cell_renderer_text_new (),
	                                             "text", COL_ID,
	                                             NULL);
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (treeview, 0), TRUE);

	/* Last Used column */
	renderer = gtk_cell_renderer_text_new ();
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, "SlateGray");
	g_object_set_property (G_OBJECT (renderer), "foreground", &val);

	gtk_tree_view_insert_column_with_attributes (treeview,
	                                             -1, _("Last Used"), renderer,
	                                             "text", COL_LAST_USED,
	                                             NULL);

	/* Selection */
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

	return treeview;
}

static void
action_info_free (ActionInfo *info)
{
	g_return_if_fail (info != NULL);

#if 0
	/* gnome_action should have been destroyed when the dialog got destroyed */
	polkit_action_unref (info->action);
#endif
	g_free (info);
}

static ActionInfo *
action_info_new (NMConnectionList *list,
                 GtkTreeView *treeview,
                 GtkWindow *list_window,
                 GtkWidget *button)
{
	ActionInfo *info;

	info = g_malloc0 (sizeof (ActionInfo));
	g_object_weak_ref (G_OBJECT (list), (GWeakNotify) action_info_free, info);

	info->list = list;
	info->treeview = treeview;
	info->list_window = list_window;
	info->button = button;
	return info;
}

static void
action_info_set_button (ActionInfo *info,
                        GtkWidget *button)
{
	g_return_if_fail (info != NULL);

	info->button = button;
#if 0
	if (info->gnome_action)
		g_object_unref (info->gnome_action);
	info->gnome_action = gnome_action;
#endif
}

static void
action_info_set_new_func (ActionInfo *info,
                          PageNewConnectionFunc func)
{
	g_return_if_fail (info != NULL);

	info->new_func = func;
}

static void
check_vpn_import_supported (gpointer key, gpointer data, gpointer user_data)
{
	NMVpnPluginUiInterface *plugin = NM_VPN_PLUGIN_UI_INTERFACE (data);
	gboolean *import_supported = user_data;

	if (*import_supported)
		return;

	if (nm_vpn_plugin_ui_interface_get_capabilities (plugin) & NM_VPN_PLUGIN_UI_CAPABILITY_IMPORT)
		*import_supported = TRUE;
}

static GtkWidget *
create_system_action_button (const char *name,
                             const char *auth_label,
                             const char *auth_tooltip,
                             const char *label,
                             const char *tooltip,
                             const char *stock_icon,
                             const char *auth_icon,
#if 0
                             GCallback result_callback,
#endif
                             GtkWidget *hbox,
                             gpointer user_data)
{
	GtkWidget *button;
#if 0
	PolKitGnomeAction *action;

	action = polkit_gnome_action_new (name);

	g_object_set (action,
	              "polkit-action", NULL,

	              "self-blocked-visible",       TRUE,
	              "self-blocked-sensitive",     FALSE,
	              "self-blocked-short-label",   label,
	              "self-blocked-label",         label,
	              "self-blocked-tooltip",       tooltip,
	              "self-blocked-icon-name",     stock_icon,

	              "no-visible",       TRUE,
	              "no-sensitive",     FALSE,
	              "no-short-label",   label,
	              "no-label",         label,
	              "no-tooltip",       tooltip,
	              "no-icon-name",     stock_icon,

	              "auth-visible",     TRUE,
	              "auth-sensitive",   TRUE,
	              "auth-short-label", auth_label,
	              "auth-label",       auth_label,
	              "auth-tooltip",     auth_tooltip,
	              "auth-icon-name",   auth_icon,

	              "yes-visible",      TRUE,
	              "yes-sensitive",    TRUE,
	              "yes-short-label",  label,
	              "yes-label",        label,
	              "yes-tooltip",      tooltip,
	              "yes-icon-name",    stock_icon,

	              "master-visible",   TRUE,
	              "master-sensitive", TRUE,
	              NULL);
	g_signal_connect (action, "polkit-result-changed",
	                  G_CALLBACK (result_callback), user_data);

	button = polkit_gnome_action_create_button (action);
#endif
	button = gtk_button_new_from_stock (stock_icon);
	gtk_box_pack_end (GTK_BOX (hbox), button, TRUE, TRUE, 0);

#if 0
	*out_action = action;
#endif
	return button;
}

static void
add_connection_buttons (NMConnectionList *self,
                        const char *prefix,
                        GtkTreeView *treeview,
                        GType ctype,
                        PageNewConnectionFunc new_func)
{
	char *name;
	GtkWidget *button, *hbox;
	ActionInfo *info;
	GtkTreeSelection *selection;
#if 0
	PolKitGnomeAction *action = NULL;
#endif

	selection = gtk_tree_view_get_selection (treeview);

	/* Add */
	name = g_strdup_printf ("%s_add", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), NULL);
	g_signal_connect (button, "clicked", G_CALLBACK (add_clicked), info);
	if (ctype == NM_TYPE_SETTING_VPN) {
		GHashTable *plugins;

		/* disable the "Add..." button if there aren't any VPN plugins */
		plugins = vpn_get_plugins (NULL);
		gtk_widget_set_sensitive (button, (plugins && g_hash_table_size (plugins)));
	}
	if (new_func)
		action_info_set_new_func (info, new_func);

	name = g_strdup_printf ("%s_button_box", prefix);
	hbox = glade_xml_get_widget (self->gui, name);
	g_free (name);

	/* Edit */
	info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), NULL);
	button = create_system_action_button ("system-edit",
	                                      _("Edit..."),
	                                      _("Authenticate to edit the selected connection."),
	                                      _("Edit"),
	                                      _("Edit the selected connection."),
	                                      GTK_STOCK_EDIT,
	                                      GTK_STOCK_DIALOG_AUTHENTICATION,
#if 0
	                                      G_CALLBACK (system_pk_result_changed_cb),
#endif
	                                      hbox,
	                                      info);
	action_info_set_button (info, button);
	g_signal_connect (button, "clicked", G_CALLBACK (edit_connection_cb), info);
	g_signal_connect (treeview, "row-activated", G_CALLBACK (connection_double_clicked_cb), info);
	g_signal_connect (selection, "changed", G_CALLBACK (pk_button_selection_changed_cb), info);
	pk_button_selection_changed_cb (selection, info);

	/* Delete */
	info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), NULL);
	button = create_system_action_button ("system-delete",
	                                      _("Delete..."),
	                                      _("Authenticate to delete the selected connection."),
	                                      _("Delete"),
	                                      _("Delete the selected connection."),
	                                      GTK_STOCK_DELETE,
	                                      GTK_STOCK_DIALOG_AUTHENTICATION,
#if 0
	                                      G_CALLBACK (system_pk_result_changed_cb),
#endif
	                                      hbox,
	                                      info);
	action_info_set_button (info, button);
	g_signal_connect (button, "clicked", G_CALLBACK (delete_clicked), info);
	g_signal_connect (selection, "changed", G_CALLBACK (pk_button_selection_changed_cb), info);
	pk_button_selection_changed_cb (selection, info);

	/* Import */
	name = g_strdup_printf ("%s_import", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	if (button) {
		gboolean import_supported = FALSE;

		info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), button);
		g_signal_connect (button, "clicked", G_CALLBACK (import_vpn_cb), info);

		g_hash_table_foreach (vpn_get_plugins (NULL), check_vpn_import_supported, &import_supported);
		gtk_widget_set_sensitive (button, import_supported);
	}

	/* Export */
	name = g_strdup_printf ("%s_export", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	if (button) {
		info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), button);
		g_signal_connect (button, "clicked", G_CALLBACK (export_vpn_cb), info);
		g_signal_connect (selection, "changed", G_CALLBACK (vpn_list_selection_changed_cb), info);
		gtk_widget_set_sensitive (button, FALSE);
	}
}

static void
add_connection_tab (NMConnectionList *self,
                    GType def_type,
                    GType ctype,
                    GdkPixbuf *pixbuf,
                    const char *prefix,
                    const char *label_text,
                    PageNewConnectionFunc new_func)
{
	char *name;
	GtkWidget *child, *hbox, *notebook;
	GtkTreeView *treeview;
	int pnum;

	name = g_strdup_printf ("%s_child", prefix);
	child = glade_xml_get_widget (self->gui, name);
	g_free (name);

	/* Notebook tab */
	hbox = gtk_hbox_new (FALSE, 6);
	if (pixbuf) {
		GtkWidget *image;

		image = gtk_image_new_from_pixbuf (pixbuf);
		gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
	}
	gtk_box_pack_start (GTK_BOX (hbox), gtk_label_new (label_text), FALSE, FALSE, 0);
	gtk_widget_show_all (hbox);

	notebook = glade_xml_get_widget (self->gui, "list_notebook");
	gtk_notebook_set_tab_label (GTK_NOTEBOOK (notebook), child, hbox);

	treeview = add_connection_treeview (self, prefix);
	add_connection_buttons (self, prefix, treeview, ctype, new_func);
	gtk_widget_show_all (GTK_WIDGET (notebook));

	g_object_set_data (G_OBJECT (treeview), TV_TYPE_TAG, GUINT_TO_POINTER (ctype));
	self->treeviews = g_slist_prepend (self->treeviews, treeview);

	if (def_type == ctype) {
		pnum = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), child);
		gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), pnum);
	}
}

static void
add_connection_tabs (NMConnectionList *self, GType def_type)
{
	add_connection_tab (self, def_type, NM_TYPE_SETTING_WIRED,
	                    self->wired_icon, "wired", _("Wired"),
	                    wired_connection_new);

	add_connection_tab (self, def_type, NM_TYPE_SETTING_WIRELESS,
	                    self->wireless_icon, "wireless", _("Wireless"),
	                    wifi_connection_new);

	add_connection_tab (self, def_type, NM_TYPE_SETTING_GSM,
	                    self->wwan_icon, "wwan", _("Mobile Broadband"),
	                    mobile_connection_new);

	add_connection_tab (self, def_type, NM_TYPE_SETTING_VPN,
	                    self->vpn_icon, "vpn", _("VPN"),
	                    vpn_connection_new);

	add_connection_tab (self, def_type, NM_TYPE_SETTING_PPPOE,
	                    self->wired_icon, "dsl", _("DSL"),
	                    dsl_connection_new);
}

static void
connection_removed (NMSettingsConnectionInterface *connection, gpointer user_data)
{
	GtkListStore *store = GTK_LIST_STORE (user_data);
	GtkTreeIter iter;

	if (get_iter_for_connection (GTK_TREE_MODEL (store), connection, &iter))
		gtk_list_store_remove (store, &iter);
}

static void
connection_updated (NMSettingsConnectionInterface *connection,
                    GHashTable *settings,
                    gpointer user_data)
{
	GtkListStore *store = GTK_LIST_STORE (user_data);
	GtkTreeIter iter;

	if (get_iter_for_connection (GTK_TREE_MODEL (store), connection, &iter))
		update_connection_row (store, &iter, connection);
}

static void
connection_added (NMSettingsInterface *settings,
                  NMSettingsConnectionInterface *connection,
                  gpointer user_data)
{
	NMConnectionList *self = NM_CONNECTION_LIST (user_data);
	GtkListStore *store;
	GtkTreeIter iter;
	NMSettingConnection *s_con;
	char *last_used;

	store = get_model_for_connection (self, connection);
	if (!store)
		return;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (NM_CONNECTION (connection), NM_TYPE_SETTING_CONNECTION));

	last_used = format_last_used (nm_setting_connection_get_timestamp (s_con));

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
	                    COL_ID, nm_setting_connection_get_id (s_con),
	                    COL_LAST_USED, last_used,
	                    COL_TIMESTAMP, nm_setting_connection_get_timestamp (s_con),
	                    COL_CONNECTION, connection,
	                    -1);

	g_free (last_used);

	g_signal_connect (connection, "removed", G_CALLBACK (connection_removed), store);
	g_signal_connect (connection, "updated", G_CALLBACK (connection_updated), store);
}

#define ICON_LOAD(x, y)	\
	{ \
		x = gtk_icon_theme_load_icon (list->icon_theme, y, 16, 0, &error); \
		if (x == NULL) { \
			g_warning ("Icon %s missing: %s", y, error->message); \
			g_error_free (error); \
			goto error; \
		} \
	}

NMConnectionList *
nm_connection_list_new (GType def_type)
{
	NMConnectionList *list;
	DBusGConnection *bus;
	GError *error = NULL;

	list = g_object_new (NM_TYPE_CONNECTION_LIST, NULL);
	if (!list)
		return NULL;

	/* load GUI */
	list->gui = glade_xml_new (GLADEDIR "/nm-connection-editor.glade", "NMConnectionList", NULL);
	if (!list->gui) {
		g_warning ("Could not load Glade file for connection list");
		goto error;
	}

	gtk_window_set_default_icon_name ("preferences-system-network");

	list->icon_theme = gtk_icon_theme_get_for_screen (gdk_screen_get_default ());

	/* Load icons */
	ICON_LOAD(list->wired_icon, "nm-device-wired");
	ICON_LOAD(list->wireless_icon, "nm-device-wireless");
	ICON_LOAD(list->wwan_icon, "nm-device-wwan");
	ICON_LOAD(list->vpn_icon, "nm-vpn-standalone-lock");
	ICON_LOAD(list->unknown_icon, "nm-no-connection");

	list->client = gconf_client_get_default ();
	if (!list->client)
		goto error;

	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		g_warning ("Could not connect to the system bus: %s", error->message);
		g_error_free (error);
		goto error;
	}

	list->system_settings = nm_remote_settings_system_new (bus);
	dbus_g_connection_unref (bus);
	g_signal_connect (list->system_settings,
	                  NM_SETTINGS_INTERFACE_NEW_CONNECTION,
	                  G_CALLBACK (connection_added),
	                  list);

	list->gconf_settings = nma_gconf_settings_new (NULL);
	g_signal_connect (list->gconf_settings,
	                  NM_SETTINGS_INTERFACE_NEW_CONNECTION,
	                  G_CALLBACK (connection_added),
	                  list);

	add_connection_tabs (list, def_type);

	list->editors = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, g_object_unref);

	list->dialog = glade_xml_get_widget (list->gui, "NMConnectionList");
	if (!list->dialog)
		goto error;
	g_signal_connect (G_OBJECT (list->dialog), "response", G_CALLBACK (dialog_response_cb), list);

	if (!vpn_get_plugins (&error)) {
		g_warning ("%s: failed to load VPN plugins: %s", __func__, error->message);
		g_error_free (error);
	}

	return list;

error:
	g_object_unref (list);
	return NULL;
}

void
nm_connection_list_present (NMConnectionList *list)
{
	g_return_if_fail (NM_IS_CONNECTION_LIST (list));

	gtk_window_present (GTK_WINDOW (list->dialog));
}

void
nm_connection_list_set_type (NMConnectionList *self, GType ctype)
{
	GtkNotebook *notebook;
	int i;

	g_return_if_fail (NM_IS_CONNECTION_LIST (self));

	/* If a notebook page is found that owns the requested type, set it
	 * as the current page.
	 */
	notebook = GTK_NOTEBOOK (glade_xml_get_widget (self->gui, "list_notebook"));
	for (i = 0; i < gtk_notebook_get_n_pages (notebook); i++) {
		GtkWidget *child;
		GType child_type;

		child = gtk_notebook_get_nth_page (notebook, i);
		child_type = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (child), TV_TYPE_TAG));
		if (child_type == ctype) {
			gtk_notebook_set_current_page (notebook, i);
			break;
		}
	}

	/* Bring the connection list to the front */
	nm_connection_list_present (self);
}

static void
list_response_cb (GtkDialog *dialog, gint response, gpointer user_data)
{
	g_signal_emit (NM_CONNECTION_LIST (user_data), list_signals[LIST_DONE], 0, response);
}

static void
list_close_cb (GtkDialog *dialog, gpointer user_data)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_CLOSE);
}

void
nm_connection_list_run (NMConnectionList *list)
{
	g_return_if_fail (NM_IS_CONNECTION_LIST (list));

	g_signal_connect (G_OBJECT (list->dialog), "response",
	                  G_CALLBACK (list_response_cb), list);
	g_signal_connect (G_OBJECT (list->dialog), "close",
	                  G_CALLBACK (list_close_cb), list);

	nm_connection_list_present (list);
}

