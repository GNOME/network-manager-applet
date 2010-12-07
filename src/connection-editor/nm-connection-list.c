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
 * (C) Copyright 2007 - 2010 Red Hat, Inc.
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
#include "ce-polkit-button.h"

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

		candidate_type = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (candidate), TV_TYPE_TAG));
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
	if (!treeview)
		return NULL;

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
	NMConnectionScope orig_scope;
	NMConnectionEditor *editor;
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

	if (info->editor)
		nm_connection_editor_set_busy (info->editor, FALSE);

	if (!error && (info->orig_scope == NM_CONNECTION_SCOPE_USER)) {
		NMSettingConnection *s_con;
		NMSettingVPN *s_vpn;
		NMVpnPluginUiInterface *plugin;
		GError *vpn_error = NULL;

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
                   NMConnectionScope orig_scope,
                   DeleteResultFunc callback,
                   gpointer user_data)
{
	DeleteInfo *info;
	NMConnectionEditor *editor;

	editor = g_hash_table_lookup (list->editors, connection);

	info = g_malloc0 (sizeof (DeleteInfo));
	info->list = list;
	info->orig_scope = orig_scope;
	info->callback = callback;
	info->callback_data = user_data;
	info->editor = editor;

	if (editor)
		nm_connection_editor_set_busy (editor, TRUE);

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

	nm_connection_editor_set_busy (info->editor, FALSE);

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

	nm_connection_editor_set_busy (editor, TRUE);
	nm_settings_interface_add_connection (settings, connection, add_cb, info);
}

/**********************************************/
/* Connection updating */

typedef void (*UpdateResultFunc) (NMConnectionList *list,
                                  NMSettingsConnectionInterface *connection,
                                  GError *error,
                                  gpointer user_data);

typedef struct {
	NMConnectionList *list;
	NMConnectionEditor *editor;
	NMSettingsConnectionInterface *connection;
	NMConnectionScope orig_scope;
	UpdateResultFunc callback;
	gpointer callback_data;
} UpdateInfo;

static void
update_complete (UpdateInfo *info, GError *error)
{
	info->callback (info->list, info->connection, error, info->callback_data);
	g_object_unref (info->connection);
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
		/* set the old scope back on the connection */
		nm_connection_set_scope (NM_CONNECTION (info->connection), info->orig_scope);
		update_complete (info, error);
		return;
	}

	/* Now try to remove the original connection */
	delete_connection (list, info->connection, info->orig_scope, update_remove_result_cb, info);
}

static void
update_cb (NMSettingsConnectionInterface *connection,
           GError *error,
           gpointer user_data)
{
	UpdateInfo *info = user_data;

	nm_connection_editor_set_busy (info->editor, FALSE);

	if (!error) {
		/* Save user-connection vpn secrets */
		if (nm_connection_get_scope (NM_CONNECTION (connection)) == NM_CONNECTION_SCOPE_USER)
			nm_connection_editor_save_vpn_secrets (info->editor);
	}

	/* Clear secrets so they don't lay around in memory; they'll get requested
	 * again anyway next time the connection is edited.
	 */
	nm_connection_clear_secrets (NM_CONNECTION (connection));

	update_complete (info, error);
}

static void
update_connection (NMConnectionList *list,
                   NMConnectionEditor *editor,
                   NMSettingsConnectionInterface *connection,
                   NMConnectionScope orig_scope,
                   UpdateResultFunc callback,
                   gpointer user_data)
{
	NMConnectionScope new_scope;
	UpdateInfo *info;

	info = g_malloc0 (sizeof (UpdateInfo));
	info->list = list;
	info->editor = editor;
	info->connection = g_object_ref (connection);
	info->orig_scope = orig_scope;
	info->callback = callback;
	info->callback_data = user_data;

	new_scope = nm_connection_get_scope (NM_CONNECTION (connection));
	if (new_scope == orig_scope) {
		/* The easy part: Connection is just updated and has the same scope */
		GHashTable *new_settings;
		GError *error = NULL;

		/* System connections need the certificates filled because the
		 * applet private values that we use to store the path to certificates
		 * and private keys don't go through D-Bus; they are private of course!
		 */
		if (new_scope == NM_CONNECTION_SCOPE_SYSTEM) {
			new_settings = nm_connection_to_hash (NM_CONNECTION (connection));
			if (!nm_connection_replace_settings (NM_CONNECTION (connection),
			                                     new_settings,
			                                     &error)) {
				update_complete (info, error);
				g_error_free (error);
				return;
			}
		}

		/* Update() actually saves the connection settings to backing storage,
		 * either GConf or over D-Bus.
		 */
		nm_connection_editor_set_busy (editor, TRUE);
		nm_settings_connection_interface_update (connection, update_cb, info);
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
	GError *add_error = NULL;

	/* if the dialog is busy waiting for authorization or something,
	 * don't destroy it until authorization returns.
	 */
	if (nm_connection_editor_get_busy (editor))
		return;

	if (response == GTK_RESPONSE_OK) {
		/* Verify and commit user changes */
		if (nm_connection_editor_update_connection (editor, &add_error)) {
			/* Yay we can try to add the connection; it'll get removed from
			 * list->editors when the add finishes.
			 */
			add_connection (info->list, editor, add_finished_cb, editor);
			return;
		} else {
			error_dialog (GTK_WINDOW (editor->window),
			              _("Error saving connection"),
			              _("The property '%s' / '%s' is invalid: %d"),
			              g_type_name (nm_connection_lookup_setting_type_by_quark (add_error->domain)),
			              (add_error && add_error->message) ? add_error->message : "(unknown)",
			              add_error ? add_error->code : -1);
			g_clear_error (&add_error);
		}
	} else if (response == GTK_RESPONSE_NONE) {
		const char *message = _("An unknown error occurred.");

		if (error && error->message)
			message = error->message;
		error_dialog (GTK_WINDOW (editor->window),
		              _("Error initializing editor"),
		              "%s", message);
	}

	g_hash_table_remove (info->list->editors, nm_connection_editor_get_connection (editor));
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

	editor = nm_connection_editor_new (connection, info->list->system_settings, &error);
	if (!editor) {
		g_object_unref (connection);

		error_dialog (info->list_window,
		              _("Could not edit new connection"),
		              "%s",
		              (editor_error && editor_error->message) ? editor_error->message : message);
		g_clear_error (&editor_error);
		return;
	}

	g_signal_connect (editor, "done", G_CALLBACK (add_response_cb), info);
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
		ctype = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (info->treeview), TV_TYPE_TAG));
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
	NMConnectionScope orig_scope;
} EditInfo;

static void
connection_updated_cb (NMConnectionList *list,
                       NMSettingsConnectionInterface *connection,
                       GError *error,
                       gpointer user_data)
{
	EditInfo *info = user_data;

	if (!error) {
		GtkListStore *store;
		GtkTreeIter iter;

		store = get_model_for_connection (list, connection);
		g_assert (store);
		if (get_iter_for_connection (GTK_TREE_MODEL (store), connection, &iter))
			update_connection_row (store, &iter, connection);
	}

	g_hash_table_remove (list->editors, connection);
	g_free (info);
}

static void
edit_done_cb (NMConnectionEditor *editor, gint response, GError *error, gpointer user_data)
{
	EditInfo *info = user_data;
	const char *message = _("An unknown error occurred.");
	NMConnection *connection;
	GError *edit_error = NULL;

	/* if the dialog is busy waiting for authorization or something,
	 * don't destroy it until authorization returns.
	 */
	if (nm_connection_editor_get_busy (editor))
		return;

	connection = nm_connection_editor_get_connection (editor);
	g_assert (connection);

	switch (response) {
	case GTK_RESPONSE_OK:
		/* Verify and commit user changes */
		if (nm_connection_editor_update_connection (editor, &edit_error)) {
			/* Save the connection to backing storage */
			update_connection (info->list,
			                   editor,
			                   NM_SETTINGS_CONNECTION_INTERFACE (connection),
			                   info->orig_scope,
			                   connection_updated_cb,
			                   info);
		} else {
			g_warning ("%s: invalid connection after update: bug in the "
			           "'%s' / '%s' invalid: %d",
			           __func__,
			           g_type_name (nm_connection_lookup_setting_type_by_quark (edit_error->domain)),
			           edit_error->message, edit_error->code);
			connection_updated_cb (info->list,
			                       NM_SETTINGS_CONNECTION_INTERFACE (connection),
			                       edit_error,
			                       info);
			g_error_free (edit_error);
		}
		break;
	case GTK_RESPONSE_NONE:
		/* Show an error dialog if the editor initialization failed */
		if (error && error->message)
			message = error->message;
		error_dialog (GTK_WINDOW (editor->window), _("Error initializing editor"), "%s", message);
		/* fall through */
	case GTK_RESPONSE_CANCEL:
	default:
		g_hash_table_remove (info->list->editors, connection);
		g_free (info);
		break;
	}
}

static void
do_edit (ActionInfo *info)
{
	NMSettingsConnectionInterface *connection;
	NMConnectionEditor *editor;
	EditInfo *edit_info;
	GError *error = NULL;
	const char *message = _("The connection editor dialog could not be initialized due to an unknown error.");

	connection = get_active_connection (info->treeview);
	g_return_if_fail (connection != NULL);

	/* Don't allow two editors for the same connection */
	editor = (NMConnectionEditor *) g_hash_table_lookup (info->list->editors, connection);
	if (editor) {
		nm_connection_editor_present (editor);
		return;
	}

	editor = nm_connection_editor_new (NM_CONNECTION (connection), info->list->system_settings, &error);
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
	edit_info->orig_scope = nm_connection_get_scope (NM_CONNECTION (connection));

	g_signal_connect (editor, "done", G_CALLBACK (edit_done_cb), edit_info);
	g_hash_table_insert (info->list->editors, connection, editor);

	nm_connection_editor_run (editor);
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
	NMConnectionEditor *editor;
	NMSettingConnection *s_con;
	GtkWidget *dialog;
	const char *id;
	guint result;

	connection = get_active_connection (info->treeview);
	g_return_if_fail (connection != NULL);

	editor = g_hash_table_lookup (info->list->editors, connection);
	if (editor && nm_connection_editor_get_busy (editor)) {
		/* Editor already has an operation in progress, raise it */
		nm_connection_editor_present (editor);
		return;
	}

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

	if (result == GTK_RESPONSE_YES) {
		delete_connection (info->list,
		                   connection,
		                   nm_connection_get_scope (NM_CONNECTION (connection)),
		                   delete_result_cb,
		                   GTK_WINDOW (info->list->dialog));
	}
}

static void
pk_button_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	GtkTreeIter iter;
	GtkTreeModel *model;
	NMSettingsConnectionInterface *connection;
	NMSettingConnection *s_con;
	NMConnectionScope scope;
	gboolean sensitive = FALSE;
	gboolean use_polkit = FALSE;

	if (!gtk_tree_selection_get_selected (selection, &model, &iter))
		goto done;

	connection = get_active_connection (info->treeview);
	if (!connection)
		goto done;

	scope = nm_connection_get_scope (NM_CONNECTION (connection));
	use_polkit = (scope == NM_CONNECTION_SCOPE_SYSTEM);

	s_con = (NMSettingConnection *) nm_connection_get_setting (NM_CONNECTION (connection),
	                                                           NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);

	sensitive = !nm_setting_connection_get_read_only (s_con);

done:
	ce_polkit_button_set_use_polkit (CE_POLKIT_BUTTON (info->button), use_polkit);
	ce_polkit_button_set_master_sensitive (CE_POLKIT_BUTTON (info->button), sensitive);
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

		g_object_unref (connection);

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

	editor = nm_connection_editor_new (connection, info->list->system_settings, &error);
	if (!editor) {
		g_object_unref (connection);
		error_dialog (info->list_window,
		              _("Could not edit imported connection"),
		              "%s",
		              (error && error->message) ? error->message : message);
		return;
	}

	g_signal_connect (editor, "done", G_CALLBACK (add_response_cb), info);
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

	if (ce_polkit_button_get_actionable (CE_POLKIT_BUTTON (info->button)))
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
	treeview = GTK_TREE_VIEW (GTK_WIDGET (gtk_builder_get_object (self->gui, name)));
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

	selection = gtk_tree_view_get_selection (treeview);

	/* Add */
	name = g_strdup_printf ("%s_add", prefix);
	button = GTK_WIDGET (gtk_builder_get_object (self->gui, name));
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
	hbox = GTK_WIDGET (gtk_builder_get_object (self->gui, name));
	g_free (name);

	/* Edit */
	info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), NULL);
	button = ce_polkit_button_new (_("_Edit"),
	                               _("Edit the selected connection"),
	                               _("_Edit..."),
	                               _("Authenticate to edit the selected connection"),
	                               GTK_STOCK_EDIT,
	                               self->system_settings,
	                               NM_SETTINGS_SYSTEM_PERMISSION_CONNECTION_MODIFY);
	gtk_button_set_use_underline (GTK_BUTTON (button), TRUE);
	gtk_box_pack_end (GTK_BOX (hbox), button, TRUE, TRUE, 0);

	action_info_set_button (info, button);
	g_signal_connect_swapped (button, "clicked", G_CALLBACK (do_edit), info);
	g_signal_connect (treeview, "row-activated", G_CALLBACK (connection_double_clicked_cb), info);
	g_signal_connect (selection, "changed", G_CALLBACK (pk_button_selection_changed_cb), info);
	pk_button_selection_changed_cb (selection, info);

	/* Delete */
	info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), NULL);
	button = ce_polkit_button_new (_("_Delete"),
	                               _("Delete the selected connection"),
	                               _("_Delete..."),
	                               _("Authenticate to delete the selected connection"),
	                               GTK_STOCK_DELETE,
	                               self->system_settings,
	                               NM_SETTINGS_SYSTEM_PERMISSION_CONNECTION_MODIFY);
	gtk_button_set_use_underline (GTK_BUTTON (button), TRUE);
	gtk_box_pack_end (GTK_BOX (hbox), button, TRUE, TRUE, 0);

	action_info_set_button (info, button);
	g_signal_connect (button, "clicked", G_CALLBACK (delete_clicked), info);
	g_signal_connect (selection, "changed", G_CALLBACK (pk_button_selection_changed_cb), info);
	pk_button_selection_changed_cb (selection, info);

	/* Import */
	name = g_strdup_printf ("%s_import", prefix);
	button = GTK_WIDGET (gtk_builder_get_object (self->gui, name));
	g_free (name);
	if (button) {
		gboolean import_supported = FALSE;
		GHashTable *plugins;

		info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), button);
		g_signal_connect (button, "clicked", G_CALLBACK (import_vpn_cb), info);

		plugins = vpn_get_plugins (NULL);
		if (plugins)
			g_hash_table_foreach (plugins, check_vpn_import_supported, &import_supported);
		gtk_widget_set_sensitive (button, import_supported);
	}

	/* Export */
	name = g_strdup_printf ("%s_export", prefix);
	button = GTK_WIDGET (gtk_builder_get_object (self->gui, name));
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
	child = GTK_WIDGET (gtk_builder_get_object (self->gui, name));
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

	notebook = GTK_WIDGET (gtk_builder_get_object (self->gui, "list_notebook"));
	gtk_notebook_set_tab_label (GTK_NOTEBOOK (notebook), child, hbox);

	treeview = add_connection_treeview (self, prefix);
	add_connection_buttons (self, prefix, treeview, ctype, new_func);
	gtk_widget_show_all (GTK_WIDGET (notebook));

	g_object_set_data (G_OBJECT (treeview), TV_TYPE_TAG, GSIZE_TO_POINTER (ctype));
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
	list->gui = gtk_builder_new ();

	if (!gtk_builder_add_from_file (list->gui, UIDIR "/nm-connection-editor.ui", &error)) {
		g_warning ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
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

	list->editors = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);

	list->dialog = GTK_WIDGET (gtk_builder_get_object (list->gui, "NMConnectionList"));
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
	notebook = GTK_NOTEBOOK (GTK_WIDGET (gtk_builder_get_object (self->gui, "list_notebook")));
	for (i = 0; i < gtk_notebook_get_n_pages (notebook); i++) {
		GtkWidget *child;
		GType child_type;

		child = gtk_notebook_get_nth_page (notebook, i);
		child_type = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (child), TV_TYPE_TAG));
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

