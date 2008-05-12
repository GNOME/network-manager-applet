/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
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
 * (C) Copyright 2004-2005 Red Hat, Inc.
 */

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtkbutton.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkstock.h>
#include <gconf/gconf-client.h>
#include <polkit-gnome/polkit-gnome.h>
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

#include "nm-connection-editor.h"
#include "nm-connection-list.h"
#include "gconf-helpers.h"
#include "mobile-wizard.h"
#include "utils.h"

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
} ActionInfo;

enum {
	NM_MODIFY_CONNECTION_ADD,
	NM_MODIFY_CONNECTION_REMOVE,
	NM_MODIFY_CONNECTION_UPDATE
};

typedef void (*NMExportedConnectionChangedFn) (NMConnectionList *list,
									  NMExportedConnection *exported,
									  gboolean success,
									  gpointer user_data);

static void modify_connection (NMConnectionList *self,
						 NMExportedConnection *exported,
						 guint action,
						 NMExportedConnectionChangedFn callback,
						 gpointer user_data);

static void
show_error_dialog (const gchar *format, ...)
{
	GtkWidget *dialog;
	va_list args;
	char *msg;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);

	dialog = gtk_message_dialog_new (NULL,
							   GTK_DIALOG_DESTROY_WITH_PARENT,
							   GTK_MESSAGE_ERROR,
							   GTK_BUTTONS_CLOSE,
							   msg);
	g_free (msg);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static NMExportedConnection *
get_active_connection (GtkTreeView *treeview)
{
	GtkTreeSelection *selection;
	GList *selected_rows;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	NMExportedConnection *exported = NULL;

	selection = gtk_tree_view_get_selection (treeview);
	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
	if (!selected_rows)
		return NULL;

	if (gtk_tree_model_get_iter (model, &iter, (GtkTreePath *) selected_rows->data))
		gtk_tree_model_get (model, &iter, COL_CONNECTION, &exported, -1);

	/* free memory */
	g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (selected_rows);

	return exported;
}

static GtkListStore *
get_model_for_connection (NMConnectionList *list, NMExportedConnection *exported)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	GtkTreeView *treeview;
	GtkTreeModel *model;

	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con || !s_con->type) {
		g_warning ("Ignoring incomplete connection");
		return NULL;
	}

	treeview = (GtkTreeView *) g_hash_table_lookup (list->treeviews, s_con->type);
	if (!treeview) {
		g_warning ("No registered treeview for connection type '%s'", s_con->type);
		return NULL;
	}

	model = gtk_tree_view_get_model (treeview);
	if (GTK_IS_TREE_MODEL_SORT (model))
		return GTK_LIST_STORE (gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (model)));

	return GTK_LIST_STORE (model);
}

static gboolean
get_iter_for_connection (GtkTreeModel *model,
					NMExportedConnection *exported,
					GtkTreeIter *iter)
{
	GtkTreeIter temp_iter;
	gboolean found = FALSE;

	if (!gtk_tree_model_get_iter_first (model, &temp_iter))
		return FALSE;

	do {
		NMExportedConnection *candidate = NULL;

		gtk_tree_model_get (model, &temp_iter, COL_CONNECTION, &candidate, -1);
		if (candidate && (candidate == exported)) {
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
				   NMExportedConnection *exported)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	char *last_used;

	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	last_used = format_last_used (s_con->timestamp);
	gtk_list_store_set (store, iter,
					COL_ID, s_con->id,
					COL_LAST_USED, last_used,
					COL_TIMESTAMP, s_con->timestamp,
					COL_CONNECTION, exported,
					-1);
	g_free (last_used);
}

typedef struct {
	NMConnectionList *list;
	NMExportedConnection *exported;
	guint action;
	NMExportedConnectionChangedFn callback;
	gpointer callback_data;
} ModifyConnectionInfo;

static void
modify_connection_auth_cb (PolKitAction *action,
					  gboolean gained_privilege,
					  GError *error,
					  gpointer user_data)
{
	ModifyConnectionInfo *info = (ModifyConnectionInfo *) user_data;
	gboolean done = TRUE;

	if (gained_privilege) {
		modify_connection (info->list, info->exported, info->action, info->callback, info->callback_data);
		done = FALSE;
	} else if (error) {
		show_error_dialog (_("Could not obtain required privileges: %s."), error->message);
		g_error_free (error);
	} else
		show_error_dialog (_("Could not remove system connection: permission denied."));

	if (done && info->callback)
		info->callback (info->list, info->exported, FALSE, info->callback_data);

	g_object_unref (info->exported);
	g_free (info);
}

static void
modify_connection (NMConnectionList *self,
			    NMExportedConnection *exported,
			    guint action,
			    NMExportedConnectionChangedFn callback,
			    gpointer user_data)
{
	const char *error_str;
	NMConnection *connection;
	GHashTable *settings;
	GError *err = NULL;
	gboolean success;
	gboolean done = FALSE;

	switch (action) {
	case NM_MODIFY_CONNECTION_ADD:
		error_str = _("Adding connection failed");

		connection = nm_exported_connection_get_connection (exported);
		if (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_SYSTEM)
			success = nm_dbus_settings_system_add_connection (self->system_settings, connection, &err);
		else {
			NMExportedConnection *new_connection;

			new_connection = (NMExportedConnection *) nma_gconf_settings_add_connection (self->gconf_settings, connection);
			if (new_connection) {
				exported = new_connection;
				success = TRUE;
			} else
				success = FALSE;
		}
		break;
	case NM_MODIFY_CONNECTION_REMOVE:
		error_str = _("Removing connection failed");
		success = nm_exported_connection_delete (exported, &err);
		break;
	case NM_MODIFY_CONNECTION_UPDATE:
		error_str = _("Updating connection failed");
		settings = nm_connection_to_hash (nm_exported_connection_get_connection (exported));
		success = nm_exported_connection_update (exported, settings, &err);
		g_hash_table_destroy (settings);
		break;
	default:
		g_warning ("Invalid action '%d' in %s", action, __func__);
		return;
	}

	if (success)
		done = TRUE;

	if (err && (dbus_g_error_has_name (err, "org.freedesktop.NetworkManagerSettings.Connection.NotPrivileged") ||
			  dbus_g_error_has_name (err, "org.freedesktop.NetworkManagerSettings.System.NotPrivileged"))) {

		ModifyConnectionInfo *info;
		PolKitAction *pk_action;
		char **tokens;
		guint xid;
		pid_t pid;

		tokens = g_strsplit (err->message, " ", 2);
		if (g_strv_length (tokens) != 2) {
			g_warning ("helper return string malformed");
			g_strfreev (tokens);
			goto out;
		}

		pk_action = polkit_action_new ();
		polkit_action_set_action_id (pk_action, tokens[0]);
		g_strfreev (tokens);

		xid = 0;
		pid = getpid ();

		g_error_free (err);
		err = NULL;

		info = g_new (ModifyConnectionInfo, 1);
		info->list = self;
		info->exported = g_object_ref (exported);
		info->action = action;
		info->callback = callback;
		info->callback_data = user_data;

		if (!polkit_gnome_auth_obtain (pk_action, xid, pid, modify_connection_auth_cb, info, &err)) {
			g_object_unref (info->exported);
			g_free (info);
		}
	}

 out:
	if (err) {
		show_error_dialog ("%s: %s.", error_str, err->message);
		g_error_free (err);
	}

	if (done && callback)
		callback (self, exported, success, user_data);
}

static void
add_done_cb (NMConnectionEditor *editor, gint response, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMExportedConnection *exported;

	exported = nm_connection_editor_get_connection (editor);
	if (response == GTK_RESPONSE_OK)
		modify_connection (info->list, exported, NM_MODIFY_CONNECTION_ADD, NULL, NULL);

	g_hash_table_remove (info->list->editors, exported);
}

static void
add_one_name (gpointer data, gpointer user_data)
{
	NMExportedConnection *exported = NM_EXPORTED_CONNECTION (data);
	NMConnection *connection;
	NMSettingConnection *s_con;
	GSList **list = (GSList **) user_data;

	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con->id);
	*list = g_slist_append (*list, s_con->id);
}

static char *
get_next_available_name (NMConnectionList *list, const char *format)
{
	GSList *connections;
	GSList *names = NULL, *iter;
	char *cname = NULL;
	int i = 0;

	connections = nm_settings_list_connections (NM_SETTINGS (list->system_settings));
	connections = g_slist_concat (connections, nm_settings_list_connections (NM_SETTINGS (list->gconf_settings)));

	g_slist_foreach (connections, add_one_name, &names);
	g_slist_free (connections);

	if (g_slist_length (names) == 0)
		return g_strdup_printf (format, 1);

	/* Find the next available unique connection name */
	while (!cname && (i++ < 10000)) {
		char *temp;
		gboolean found = FALSE;

		temp = g_strdup_printf (format, i);
		for (iter = names; iter; iter = g_slist_next (iter)) {
			if (!strcmp (iter->data, temp)) {
				found = TRUE;
				break;
			}
		}
		if (!found)
			cname = temp;
		else
			g_free (temp);
	}

	g_slist_free (names);
	return cname;
}

static void
add_default_serial_setting (NMConnection *connection)
{
	NMSettingSerial *s_serial;

	s_serial = NM_SETTING_SERIAL (nm_setting_serial_new ());
	s_serial->baud = 115200;
	s_serial->bits = 8;
	s_serial->parity = 'n';
	s_serial->stopbits = 1;
	nm_connection_add_setting (connection, NM_SETTING (s_serial));
}

static NMConnection *
create_new_connection_for_type (NMConnectionList *list, const char *connection_type)
{
	GType ctype;
	NMConnection *connection = NULL;
	NMSettingConnection *s_con;
	NMSetting *type_setting = NULL;
	GType mb_type;

	ctype = nm_connection_lookup_setting_type (connection_type);

	connection = nm_connection_new ();
	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	nm_connection_add_setting (connection, NM_SETTING (s_con));

	if (ctype == NM_TYPE_SETTING_WIRED) {
		s_con->id = get_next_available_name (list, _("Wired connection %d"));
		s_con->type = g_strdup (NM_SETTING_WIRED_SETTING_NAME);
		s_con->autoconnect = TRUE;

		type_setting = nm_setting_wired_new ();
	} else if (ctype == NM_TYPE_SETTING_WIRELESS) {
		NMSettingWireless *s_wireless;

		s_con->id = get_next_available_name (list, _("Wireless connection %d"));
		s_con->type = g_strdup (NM_SETTING_WIRELESS_SETTING_NAME);
		s_con->autoconnect = TRUE;

		type_setting = nm_setting_wireless_new ();
		s_wireless = NM_SETTING_WIRELESS (type_setting);
		s_wireless->mode = g_strdup ("infrastructure");
	} else if (ctype == NM_TYPE_SETTING_GSM) {
		/* Since GSM is a placeholder for both GSM and CDMA; ask the user which
		 * one they really want.
		 */
		mb_type = mobile_wizard_ask_connection_type ();
		if (mb_type == NM_TYPE_SETTING_GSM) {
			NMSettingGsm *s_gsm;

			s_con->id = get_next_available_name (list, _("GSM connection %d"));
			s_con->type = g_strdup (NM_SETTING_GSM_SETTING_NAME);
			s_con->autoconnect = FALSE;

			add_default_serial_setting (connection);

			type_setting = nm_setting_gsm_new ();
			s_gsm = NM_SETTING_GSM (type_setting);
			s_gsm->number = g_strdup ("*99#"); /* De-facto standard for GSM */

			nm_connection_add_setting (connection, nm_setting_ppp_new ());
		} else if (mb_type == NM_TYPE_SETTING_CDMA) {
			NMSettingCdma *s_cdma;

			s_con->id = get_next_available_name (list, _("CDMA connection %d"));
			s_con->type = g_strdup (NM_SETTING_CDMA_SETTING_NAME);
			s_con->autoconnect = FALSE;

			add_default_serial_setting (connection);

			type_setting = nm_setting_cdma_new ();
			s_cdma = NM_SETTING_CDMA (type_setting);
			s_cdma->number = g_strdup ("#777"); /* De-facto standard for CDMA */

			nm_connection_add_setting (connection, nm_setting_ppp_new ());
		} else {
			/* user canceled; do nothing */
		}
	} else if (ctype == NM_TYPE_SETTING_VPN) {
		s_con->id = get_next_available_name (list, _("VPN connection %d"));
		s_con->type = g_strdup (NM_SETTING_VPN_SETTING_NAME);

		type_setting = nm_setting_vpn_new ();
	} else if (ctype == NM_TYPE_SETTING_PPPOE) {
		s_con->id = get_next_available_name (list, _("DSL connection %d"));
		s_con->type = g_strdup (NM_SETTING_PPPOE_SETTING_NAME);

		type_setting = nm_setting_pppoe_new ();

		nm_connection_add_setting (connection, nm_setting_wired_new ());
		nm_connection_add_setting (connection, nm_setting_ppp_new ());
	} else {
		g_warning ("%s: unhandled connection type '%s'", __func__, g_type_name (ctype)); 
	}

	if (type_setting) {
		nm_connection_add_setting (connection, type_setting);
	} else {
		g_object_unref (connection);
		connection = NULL;
	}

	return connection;
}

typedef struct {
	const char *type;
	GtkTreeView *treeview;
} LookupTreeViewInfo;

static void
lookup_treeview (gpointer key, gpointer value, gpointer user_data)
{
	LookupTreeViewInfo *info = (LookupTreeViewInfo *) user_data;

	if (!info->type && info->treeview == value)
		info->type = (const char *) key;
}

static const char *
get_connection_type_from_treeview (NMConnectionList *self,
							GtkTreeView *treeview)
{
	LookupTreeViewInfo info;

	info.type = NULL;
	info.treeview = treeview;

	g_hash_table_foreach (self->treeviews, lookup_treeview, &info);

	return info.type;
}

static void
add_connection_cb (GtkButton *button, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	const char *connection_type;
	NMConnection *connection;
	NMExportedConnection *exported;
	NMConnectionEditor *editor;

	connection_type = get_connection_type_from_treeview (info->list, info->treeview);
	g_assert (connection_type);

	connection = create_new_connection_for_type (info->list, connection_type);
	if (!connection) {
		g_warning ("Can't add new connection of type '%s'", connection_type);
		return;
	}

	exported = nm_exported_connection_new (connection);
	g_object_unref (connection);

	editor = nm_connection_editor_new (exported);
	g_signal_connect (G_OBJECT (editor), "done", G_CALLBACK (add_done_cb), info);
	g_hash_table_insert (info->list->editors, exported, editor);

	nm_connection_editor_run (editor);
}

typedef struct {
	NMConnectionList *list;
	NMExportedConnection *new_connection;
	NMExportedConnection *initial_connection;
	NMConnectionScope initial_scope;
} EditConnectionInfo;

static void
connection_update_done (NMConnectionList *list,
				    NMExportedConnection *exported,
				    gboolean success,
				    gpointer user_data)
{
	EditConnectionInfo *info = (EditConnectionInfo *) user_data;

	if (success) {
		GtkListStore *store;
		GtkTreeIter iter;

		store = get_model_for_connection (list, info->initial_connection);
		g_assert (store);
		if (get_iter_for_connection (GTK_TREE_MODEL (store), info->initial_connection, &iter))
			update_connection_row (store, &iter, info->initial_connection);
	}

	g_object_unref (info->initial_connection);
	g_free (info);
}

static void
connection_update_remove_done (NMConnectionList *list,
						 NMExportedConnection *exported,
						 gboolean success,
						 gpointer user_data)
{
	EditConnectionInfo *info = (EditConnectionInfo *) user_data;

	if (success)
		connection_update_done (list, exported, success, info);
	else {
		/* Revert the scope of the original connection and remove the connection we just successfully added */
		nm_connection_set_scope (nm_exported_connection_get_connection (info->initial_connection),
							info->initial_scope);

		modify_connection (list, info->new_connection,
					    NM_MODIFY_CONNECTION_REMOVE,
					    connection_update_remove_done, info);
	}
}

static void
connection_update_add_done (NMConnectionList *list,
					   NMExportedConnection *exported,
					   gboolean success,
					   gpointer user_data)
{
	EditConnectionInfo *info = (EditConnectionInfo *) user_data;

	if (success) {
		info->new_connection = exported;
		modify_connection (list, info->initial_connection,
					    NM_MODIFY_CONNECTION_REMOVE,
					    connection_update_remove_done, info);
	} else {
		/* Revert the scope and clean up */
		nm_connection_set_scope (nm_exported_connection_get_connection (info->initial_connection),
							info->initial_scope);

		connection_update_done (list, exported, success, info);
	}
}

static void
edit_done_cb (NMConnectionEditor *editor, gint response, gpointer user_data)
{
	EditConnectionInfo *info = (EditConnectionInfo *) user_data;

	g_hash_table_remove (info->list->editors, info->initial_connection);

	if (response == GTK_RESPONSE_OK) {
		NMConnection *connection;
		gboolean success;

		connection = nm_exported_connection_get_connection (info->initial_connection);
		utils_fill_connection_certs (connection);
		success = nm_connection_verify (connection);
		utils_clear_filled_connection_certs (connection);

		if (success) {
			if (info->initial_scope == nm_connection_get_scope (connection)) {
				/* The easy part: Connection is updated */
				modify_connection (info->list, info->initial_connection,
							    NM_MODIFY_CONNECTION_UPDATE,
							    connection_update_done, info);
			} else {
				/* The hard part: Connection scope changed:
				   Add the exported connection,
				   if it succeeds, remove the old one. */
				modify_connection (info->list, info->initial_connection,
							    NM_MODIFY_CONNECTION_ADD,
							    connection_update_add_done, info);
			}
		} else
			g_warning ("%s: connection invalid after update; bug in the connection editor.", __func__);
	}
}

static void
do_edit (ActionInfo *info)
{
	NMExportedConnection *exported;
	NMConnectionEditor *editor;
	EditConnectionInfo *edit_info;

	exported = get_active_connection (info->treeview);
	g_return_if_fail (exported != NULL);

	/* Don't allow two editors for the same connection */
	editor = NM_CONNECTION_EDITOR (g_hash_table_lookup (info->list->editors, exported));
	if (editor) {
		nm_connection_editor_present (editor);
		return;
	}

	editor = nm_connection_editor_new (exported);

	edit_info = g_new (EditConnectionInfo, 1);
	edit_info->list = info->list;
	edit_info->new_connection = NULL;
	edit_info->initial_connection = g_object_ref (exported);
	edit_info->initial_scope = nm_connection_get_scope (nm_exported_connection_get_connection (exported));

	g_signal_connect (editor, "done", G_CALLBACK (edit_done_cb), edit_info);
	g_hash_table_insert (info->list->editors, exported, editor);

	nm_connection_editor_run (editor);
}

static void
edit_connection_cb (GtkButton *button, gpointer user_data)
{
	do_edit ((ActionInfo *) user_data);
}

static void
connection_remove_done (NMConnectionList *list,
				    NMExportedConnection *exported,
				    gboolean success,
				    gpointer user_data)
{
	if (success)
		/* Close any open editor windows for this connection */
		g_hash_table_remove (list->editors, exported);
}

static void
delete_connection_cb (GtkButton *button, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMExportedConnection *exported = NULL;
	NMConnection *connection;
	NMSettingConnection *s_con;
	GtkWidget *dialog;
	guint result;

	exported = get_active_connection (info->treeview);
	g_return_if_fail (exported != NULL);
	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con || !s_con->id)
		return;

	dialog = gtk_message_dialog_new (GTK_WINDOW (info->list->dialog),
	                                 GTK_DIALOG_DESTROY_WITH_PARENT,
	                                 GTK_MESSAGE_QUESTION,
	                                 GTK_BUTTONS_NONE,
	                                 _("Are you sure you wish to delete the connection %s?"),
	                                 s_con->id);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
	                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                        GTK_STOCK_DELETE, GTK_RESPONSE_YES,
	                        NULL);

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (result == GTK_RESPONSE_YES)
		modify_connection (info->list, exported,
					    NM_MODIFY_CONNECTION_REMOVE, 
					    connection_remove_done, NULL);
}

static void
list_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	GtkWidget *button = GTK_WIDGET (user_data);
	GtkTreeIter iter;
	GtkTreeModel *model;

	if (gtk_tree_selection_get_selected (selection, &model, &iter))
		gtk_widget_set_sensitive (GTK_WIDGET (button), TRUE);
	else
		gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
}

static void
connection_double_clicked_cb (GtkTreeView *tree_view,
                              GtkTreePath *path,
                              GtkTreeViewColumn *column,
                              gpointer user_data)
{
	do_edit ((ActionInfo *) user_data);
}

static void
dialog_response_cb (GtkDialog *dialog, guint response, gpointer user_data)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
nm_connection_list_init (NMConnectionList *list)
{
	list->treeviews = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
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

	if (list->dialog)
		gtk_widget_destroy (list->dialog);
	if (list->gui)
		g_object_unref (list->gui);
	if (list->client)
		g_object_unref (list->client);

	g_hash_table_destroy (list->treeviews);

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

	/* Model */
	model = GTK_TREE_MODEL (gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_OBJECT));
	sort_model = gtk_tree_model_sort_new_with_model (model);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sort_model),
	                                      COL_TIMESTAMP, GTK_SORT_DESCENDING);
	gtk_tree_view_set_model (treeview, sort_model);

	/* Name column */
	gtk_tree_view_insert_column_with_attributes (treeview,
	                                             -1, "Name", gtk_cell_renderer_text_new (),
	                                             "text", COL_ID,
	                                             NULL);
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (treeview, 0), TRUE);

	/* Last Used column */
	renderer = gtk_cell_renderer_text_new ();
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, "SlateGray");
	g_object_set_property (G_OBJECT (renderer), "foreground", &val);

	gtk_tree_view_insert_column_with_attributes (treeview,
	                                             -1, "Last Used", renderer,
	                                             "text", COL_LAST_USED,
	                                             NULL);

	/* Selection */
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

	return treeview;
}

static void
add_connection_buttons (NMConnectionList *self,
				    const char *prefix,
				    GtkTreeView *treeview)
{
	char *name;
	GtkWidget *button;
	ActionInfo *info;
	GtkTreeSelection *selection;

	info = g_new (ActionInfo, 1);
	info->list = self;
	info->treeview = treeview;

	selection = gtk_tree_view_get_selection (treeview);

	g_object_weak_ref (G_OBJECT (self), (GWeakNotify) g_free, info);

	/* Add */
	name = g_strdup_printf ("%s_add", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	g_signal_connect (button, "clicked", G_CALLBACK (add_connection_cb), info);

	/* Edit */
	name = g_strdup_printf ("%s_edit", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	g_signal_connect (button, "clicked", G_CALLBACK (edit_connection_cb), info);
	g_signal_connect (selection, "changed", G_CALLBACK (list_selection_changed_cb), button);
	g_signal_connect (treeview, "row-activated", G_CALLBACK (connection_double_clicked_cb), info);

	/* Delete */
	name = g_strdup_printf ("%s_delete", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	g_signal_connect (button, "clicked", G_CALLBACK (delete_connection_cb), info);
	g_signal_connect (selection, "changed", G_CALLBACK (list_selection_changed_cb), button);
}

static void
add_connection_tab (NMConnectionList *self,
				GSList *connection_types,
				GdkPixbuf *pixbuf,
				const char *prefix,
				const char *label_text)
{
	char *name;
	GtkWidget *child;
	GtkWidget *hbox;
	GtkTreeView *treeview;
	GSList *iter;

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

	gtk_notebook_set_tab_label (GTK_NOTEBOOK (glade_xml_get_widget (self->gui, "list_notebook")), child, hbox);

	treeview = add_connection_treeview (self, prefix);
	add_connection_buttons (self, prefix, treeview);

	for (iter = connection_types; iter; iter = iter->next)
		g_hash_table_insert (self->treeviews, g_strdup ((const char *) iter->data), treeview);
}

static void
add_connection_tabs (NMConnectionList *self)
{
	GSList *types;

	types = g_slist_append (NULL, NM_SETTING_WIRED_SETTING_NAME);
	add_connection_tab (self, types, self->wired_icon, "wired", _("Wired"));
	g_slist_free (types);

	types = g_slist_append (NULL, NM_SETTING_WIRELESS_SETTING_NAME);
	add_connection_tab (self, types, self->wireless_icon, "wireless", _("Wireless"));
	g_slist_free (types);

	types = g_slist_append (NULL, NM_SETTING_GSM_SETTING_NAME);
	types = g_slist_append (types, NM_SETTING_CDMA_SETTING_NAME);
	add_connection_tab (self, types, self->wwan_icon, "wwan", _("Mobile Broadband"));
	g_slist_free (types);

	types = g_slist_append (NULL, NM_SETTING_VPN_SETTING_NAME);
	add_connection_tab (self, types, self->vpn_icon, "vpn", _("VPN"));
	g_slist_free (types);

	types = g_slist_append (NULL, NM_SETTING_PPPOE_SETTING_NAME);
	add_connection_tab (self, types, self->wired_icon, "dsl", _("DSL"));
	g_slist_free (types);
}

static void
connection_removed (NMExportedConnection *exported, gpointer user_data)
{
	GtkListStore *store = GTK_LIST_STORE (user_data);
	GtkTreeIter iter;

	if (get_iter_for_connection (GTK_TREE_MODEL (store), exported, &iter))
		gtk_list_store_remove (store, &iter);
}

static void
connection_updated (NMExportedConnection *exported,
				GHashTable *settings,
				gpointer user_data)
{
	GtkListStore *store = GTK_LIST_STORE (user_data);
	GtkTreeIter iter;

	if (get_iter_for_connection (GTK_TREE_MODEL (store), exported, &iter))
		update_connection_row (store, &iter, exported);
}

static void
connection_added (NMSettings *settings,
			   NMExportedConnection *exported,
			   gpointer user_data)
{
	NMConnectionList *self = NM_CONNECTION_LIST (user_data);
	GtkListStore *store;
	GtkTreeIter iter;
	NMConnection *connection;
	NMSettingConnection *s_con;
	char *last_used;

	store = get_model_for_connection (self, exported);
	if (!store)
		return;

	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));

	last_used = format_last_used (s_con->timestamp);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
	                    COL_ID, s_con->id,
	                    COL_LAST_USED, last_used,
	                    COL_TIMESTAMP, s_con->timestamp,
	                    COL_CONNECTION, exported,
	                    -1);

	g_free (last_used);

	g_signal_connect (exported, "removed", G_CALLBACK (connection_removed), store);
	g_signal_connect (exported, "updated", G_CALLBACK (connection_updated), store);
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
nm_connection_list_new (void)
{
	NMConnectionList *list;
	DBusGConnection *dbus_connection;
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

	dbus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		g_warning ("Could not connect to the system bus: %s", error->message);
		g_error_free (error);
		goto error;
	}

	list->system_settings = nm_dbus_settings_system_new (dbus_connection);
	dbus_g_connection_unref (dbus_connection);
	g_signal_connect (list->system_settings, "new-connection",
				   G_CALLBACK (connection_added),
				   list);

	list->gconf_settings = nma_gconf_settings_new ();
	g_signal_connect (list->gconf_settings, "new-connection",
				   G_CALLBACK (connection_added),
				   list);

	add_connection_tabs (list);

	list->editors = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, g_object_unref);

	list->dialog = glade_xml_get_widget (list->gui, "NMConnectionList");
	if (!list->dialog)
		goto error;
	g_signal_connect (G_OBJECT (list->dialog), "response", G_CALLBACK (dialog_response_cb), list);

	return list;

error:
	g_object_unref (list);
	return NULL;
}

static void
nm_connection_list_present (NMConnectionList *list)
{
	g_return_if_fail (NM_IS_CONNECTION_LIST (list));

	gtk_window_present (GTK_WINDOW (list->dialog));
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
