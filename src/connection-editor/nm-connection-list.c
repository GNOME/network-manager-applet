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
 * (C) Copyright 2007 - 2008 Red Hat, Inc.
 */

#include <config.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>

#ifdef NO_POLKIT_GNOME
#include "polkit-gnome.h"
#else
#include <polkit-gnome/polkit-gnome.h>
#endif

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

#include "nm-connection-editor.h"
#include "nm-connection-list.h"
#include "gconf-helpers.h"
#include "mobile-wizard.h"
#include "utils.h"
#include "vpn-helpers.h"
#include "polkit-helpers.h"

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
	PolKitAction *action;
	PolKitGnomeAction *gnome_action;
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

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), message);
	g_free (message);

	gtk_widget_show_all (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
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
	const char *connection_type;

	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	connection_type = s_con ? nm_setting_connection_get_connection_type (s_con) : NULL;

	if (!connection_type) {
		g_warning ("Ignoring incomplete connection");
		return NULL;
	}

	treeview = (GtkTreeView *) g_hash_table_lookup (list->treeviews, connection_type);
	if (!treeview) {
		g_warning ("No registered treeview for connection type '%s'", connection_type);
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

	last_used = format_last_used (nm_setting_connection_get_timestamp (s_con));
	gtk_list_store_set (store, iter,
					COL_ID, nm_setting_connection_get_id (s_con),
					COL_LAST_USED, last_used,
					COL_TIMESTAMP, nm_setting_connection_get_timestamp (s_con),
					COL_CONNECTION, exported,
					-1);
	g_free (last_used);
}


/**********************************************/
/* Connection removing */

typedef void (*ConnectionRemovedFn) (NMExportedConnection *exported,
							  gboolean success,
							  gpointer user_data);

typedef struct {
	NMExportedConnection *exported;
	GtkWindow *parent;
	ConnectionRemovedFn callback;
	gpointer user_data;
} ConnectionRemoveInfo;

static void remove_connection (NMExportedConnection *exported,
                               GtkWindow *parent,
                               ConnectionRemovedFn callback,
                               gpointer user_data);

static void
remove_connection_cb (PolKitAction *action,
				  gboolean gained_privilege,
				  GError *error,
				  gpointer user_data)
{
	ConnectionRemoveInfo *info = (ConnectionRemoveInfo *) user_data;
	gboolean done = TRUE;

	if (gained_privilege) {
		remove_connection (info->exported, info->parent, info->callback, info->user_data);
		done = FALSE;
	} else if (error) {
		error_dialog (info->parent,
		              _("Could not obtain required privileges"),
		              "%s", error->message);
		g_error_free (error);
	} else {
		error_dialog (info->parent,
		              _("Could not delete connection"),
		              "%s", _("The connection could not be deleted due to an unknown error."));
	}

	if (done && info->callback)
		info->callback (info->exported, FALSE, info->user_data);

	g_object_unref (info->exported);
	g_slice_free (ConnectionRemoveInfo, info);
}

static void
remove_connection (NMExportedConnection *exported,
                   GtkWindow *parent,
                   ConnectionRemovedFn callback,
                   gpointer user_data)
{
	GError *error = NULL;
	gboolean success;

	success = nm_exported_connection_delete (exported, &error);
	if (!success) {
		gboolean auth_pending = FALSE;

		if (pk_helper_is_permission_denied_error (error)) {
			ConnectionRemoveInfo *info;
			GError *auth_error = NULL;

			info = g_slice_new (ConnectionRemoveInfo);
			info->exported = g_object_ref (exported);
			info->parent = parent;
			info->callback = callback;
			info->user_data = user_data;

			auth_pending = pk_helper_obtain_auth (error, parent, remove_connection_cb, info, &auth_error);
			if (auth_error) {
				error_dialog (parent,
				              _("Could not move connection"),
				              "%s", auth_error->message);
				g_error_free (auth_error);
			}

			if (!auth_pending) {
				g_object_unref (info->exported);
				g_slice_free (ConnectionRemoveInfo, info);
			}
		} else {
			error_dialog (parent,
			              _("Could not move connection"),
			              "%s", error->message);
		}

		g_error_free (error);

		if (auth_pending)
			return;
	} else {
		NMConnection *connection;
		NMSettingConnection *s_con;
		NMSettingVPN *s_vpn;
		NMVpnPluginUiInterface *plugin;

		connection = nm_exported_connection_get_connection (exported);
		s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
		g_assert (s_con);

		if (nm_connection_get_scope (connection) == NM_CONNECTION_SCOPE_USER) {
			/* Clean up keyring keys */
			nm_gconf_clear_keyring_items (connection);
		}

		/* Clean up VPN secrets and any plugin-specific data */
		if (!strcmp (nm_setting_connection_get_connection_type (s_con), NM_SETTING_VPN_SETTING_NAME)) {
			s_vpn = (NMSettingVPN *) nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN);
			if (s_vpn) {
				plugin = vpn_get_plugin_by_service (nm_setting_vpn_get_service_type (s_vpn));
				if (plugin)
					if (!nm_vpn_plugin_ui_interface_delete_connection (plugin, connection, &error)) {
						g_warning ("%s: couldn't clean up VPN connection on delete: (%d) %s",
						           __func__, error ? error->code : -1, error ? error->message : "unknown");
						if (error)
							g_error_free (error);
					}
			}
		}
	}

	if (callback)
		callback (exported, success, user_data);
}

/**********************************************/
/* Connection adding */

typedef void (*ConnectionAddedFn) (NMExportedConnection *exported,
							gboolean success,
							gpointer user_data);

typedef struct {
	NMConnectionList *list;
	NMConnectionEditor *editor;
	NMConnection *connection;
	ConnectionAddedFn callback;
	gpointer user_data;
} ConnectionAddInfo;

static void add_connection (NMConnectionList *self,
                            NMConnectionEditor *editor,
                            NMConnection *connection,
                            ConnectionAddedFn callback,
                            gpointer user_data);

static void
add_connection_cb (PolKitAction *action,
			    gboolean gained_privilege,
			    GError *error,
			    gpointer user_data)
{
	ConnectionAddInfo *info = (ConnectionAddInfo *) user_data;
	GtkWindow *parent = nm_connection_editor_get_window (info->editor);
	gboolean done = TRUE;

	if (gained_privilege) {
		add_connection (info->list, info->editor, info->connection, info->callback, info->user_data);
		done = FALSE;
	} else if (error) {
		error_dialog (parent,
		              _("Could not obtain required privileges"),
		              "%s", error->message);
		g_error_free (error);
	} else {
		error_dialog (parent,
		              _("Could not add connection"),
		              "%s", _("The connection could not be added due to an unknown error."));
	}

	if (done && info->callback)
		info->callback (NULL, FALSE, info->user_data);

	g_object_unref (info->connection);
	g_slice_free (ConnectionAddInfo, info);
}

static void
add_connection (NMConnectionList *self,
                NMConnectionEditor *editor,
                NMConnection *connection,
                ConnectionAddedFn callback,
                gpointer user_data)
{
	NMExportedConnection *exported = NULL;
	NMConnectionScope scope;
	gboolean success = FALSE;

	scope = nm_connection_get_scope (connection);
	if (scope == NM_CONNECTION_SCOPE_SYSTEM) {
		GError *error = NULL;

		utils_fill_connection_certs (connection);
		success = nm_dbus_settings_system_add_connection (self->system_settings, connection, &error);
		utils_clear_filled_connection_certs (connection);

		if (!success) {
			gboolean pending_auth = FALSE;
			GtkWindow *parent;

			parent = nm_connection_editor_get_window (editor);
			if (pk_helper_is_permission_denied_error (error)) {
				ConnectionAddInfo *info;
				GError *auth_error = NULL;

				info = g_slice_new (ConnectionAddInfo);
				info->list = self;
				info->editor = editor;
				info->connection = g_object_ref (connection);
				info->callback = callback;
				info->user_data = user_data;

				pending_auth = pk_helper_obtain_auth (error, parent, add_connection_cb, info, &auth_error);
				if (auth_error) {
					error_dialog (parent,
					              _("Could not add connection"),
					              "%s", auth_error->message);
					g_error_free (auth_error);
				}

				if (!pending_auth) {
					g_object_unref (info->connection);
					g_slice_free (ConnectionAddInfo, info);
				}
			} else {
				error_dialog (parent,
				              _("Could not add connection"),
				              "%s", error->message);
			}

			g_error_free (error);

			if (pending_auth)
				return;
		}
	} else if (scope == NM_CONNECTION_SCOPE_USER) {
		exported = (NMExportedConnection *) nma_gconf_settings_add_connection (self->gconf_settings, connection);
		success = exported != NULL;
		if (success && editor)
			nm_connection_editor_save_vpn_secrets (editor);
	} else
		g_warning ("%s: unhandled connection scope %d!", __func__, scope);

	if (callback)
		callback (exported, success, user_data);

	if (exported)
		g_object_unref (exported);
}

/**********************************************/
/* Connection updating */

typedef void (*ConnectionUpdatedFn) (NMConnectionList *list,
							  gboolean success,
							  gpointer user_data);

typedef struct {
	NMConnectionList *list;
	NMConnectionEditor *editor;
	NMExportedConnection *original;
	NMConnection *modified;
	ConnectionUpdatedFn callback;
	gpointer user_data;

	NMExportedConnection *added_connection;
} ConnectionUpdateInfo;

static void update_connection (NMConnectionList *list,
                               NMConnectionEditor *editor,
                               NMExportedConnection *original,
                               NMConnection *modified,
                               ConnectionUpdatedFn callback,
                               gpointer user_data);


static void
connection_update_done (ConnectionUpdateInfo *info, gboolean success)
{
	if (info->callback)
		info->callback (info->list, success, info->user_data);

	g_object_unref (info->original);
	g_object_unref (info->modified);
	if (info->added_connection)
		g_object_unref (info->added_connection);

	g_slice_free (ConnectionUpdateInfo, info);
}

static void
connection_update_remove_done (NMExportedConnection *exported,
						 gboolean success,
						 gpointer user_data)
{
	ConnectionUpdateInfo *info = (ConnectionUpdateInfo *) user_data;

	if (success)
		connection_update_done (info, success);
	else if (info->added_connection) {
		GtkWindow *parent;

		/* Revert the scope of the original connection and remove the connection we just successfully added */
		/* FIXME: loops forever on error */
		parent = nm_connection_editor_get_window (info->editor);
		remove_connection (info->added_connection, parent, connection_update_remove_done, info);
	}
}

static void
connection_update_add_done (NMExportedConnection *exported,
					   gboolean success,
					   gpointer user_data)
{
	ConnectionUpdateInfo *info = (ConnectionUpdateInfo *) user_data;

	if (success) {
		/* Adding the connection with different scope succeeded, now try to remove the original */
		info->added_connection = exported ? g_object_ref (exported) : NULL;
		remove_connection (info->original, GTK_WINDOW (info->editor), connection_update_remove_done, info);
	} else
		connection_update_done (info, success);
}

static void
update_connection_cb (PolKitAction *action,
				  gboolean gained_privilege,
				  GError *error,
				  gpointer user_data)
{
	ConnectionUpdateInfo *info = (ConnectionUpdateInfo *) user_data;
	gboolean done = TRUE;
	GtkWindow *parent;

	parent = nm_connection_editor_get_window (info->editor);
	if (gained_privilege) {
		update_connection (info->list, info->editor, info->original, info->modified, info->callback, info->user_data);
		done = FALSE;
	} else if (error) {
		error_dialog (parent,
		              _("Could not update connection"),
		              "%s", error->message);
		g_error_free (error);
	} else {
		error_dialog (parent,
		              _("Could not update connection"),
		              "%s", _("The connection could not be updated due to an unknown error."));
	}

	if (done)
		connection_update_done (info, FALSE);
	else {
		g_object_unref (info->original);
		g_object_unref (info->modified);
		g_slice_free (ConnectionUpdateInfo, info);
	}
}

static void
update_connection (NMConnectionList *list,
                   NMConnectionEditor *editor,
                   NMExportedConnection *original,
                   NMConnection *modified,
                   ConnectionUpdatedFn callback,
                   gpointer user_data)
{
	NMConnectionScope original_scope;
	ConnectionUpdateInfo *info;

	info = g_slice_new0 (ConnectionUpdateInfo);
	info->list = list;
	info->editor = editor;
	info->original = g_object_ref (original);
	info->modified = g_object_ref (modified);
	info->callback = callback;
	info->user_data = user_data;

	original_scope = nm_connection_get_scope (nm_exported_connection_get_connection (original));
	if (nm_connection_get_scope (modified) == original_scope) {
		/* The easy part: Connection is updated */
		GHashTable *new_settings;
		GError *error = NULL;
		gboolean success;
		gboolean pending_auth = FALSE;
		GtkWindow *parent;

		utils_fill_connection_certs (modified);
		new_settings = nm_connection_to_hash (modified);

		/* Hack; make sure that gconf private values are copied */
		nm_gconf_copy_private_connection_values (nm_exported_connection_get_connection (original),
		                                         modified);

		success = nm_exported_connection_update (original, new_settings, &error);
		g_hash_table_destroy (new_settings);
		utils_clear_filled_connection_certs (modified);

		parent = nm_connection_editor_get_window (editor);
		if (!success) {
			if (pk_helper_is_permission_denied_error (error)) {
				GError *auth_error = NULL;

				pending_auth = pk_helper_obtain_auth (error, parent, update_connection_cb, info, &auth_error);
				if (auth_error) {
					error_dialog (parent,
					              _("Could not update connection"),
					              "%s", auth_error->message);
					g_error_free (auth_error);
				}
			} else {
				error_dialog (parent,
				              _("Could not update connection"),
				              "%s", error->message);
			}

			g_error_free (error);
		} else {
			/* Save user-connection vpn secrets */
			if (editor && (original_scope == NM_CONNECTION_SCOPE_USER))
				nm_connection_editor_save_vpn_secrets (editor);
		}

		if (!pending_auth)
			connection_update_done (info, success);
	} else {
		/* The hard part: Connection scope changed:
		   Add the exported connection,
		   if it succeeds, remove the old one. */
		add_connection (list, editor, modified, connection_update_add_done, info);
	}
}

static void
add_done_cb (NMConnectionEditor *editor, gint response, GError *error, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMConnection *connection;
	const char *message = _("An unknown error ocurred.");

	connection = nm_connection_editor_get_connection (editor);

	switch (response) {
	case GTK_RESPONSE_NONE:
		if (error && error->message)
			message = error->message;
		error_dialog (GTK_WINDOW (editor->window),
		              _("Error initializing editor"),
		              "%s", message);
		break;
	case GTK_RESPONSE_OK:
		add_connection (info->list, editor, connection, NULL, NULL);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_hash_table_remove (info->list->editors, connection);
}

static void
add_one_name (gpointer data, gpointer user_data)
{
	NMExportedConnection *exported = NM_EXPORTED_CONNECTION (data);
	NMConnection *connection;
	NMSettingConnection *s_con;
	const char *id;
	GSList **list = (GSList **) user_data;

	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	id = nm_setting_connection_get_id (s_con);
	g_assert (id);
	*list = g_slist_append (*list, (gpointer) id);
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
	g_object_set (s_serial,
	              NM_SETTING_SERIAL_BAUD, 115200,
	              NM_SETTING_SERIAL_BITS, 8,
	              NM_SETTING_SERIAL_PARITY, 'n',
	              NM_SETTING_SERIAL_STOPBITS, 1,
	              NULL);

	nm_connection_add_setting (connection, NM_SETTING (s_serial));
}

static NMConnection *
create_new_connection_for_type (NMConnectionList *list, const char *connection_type)
{
	GType ctype;
	NMConnection *connection = NULL;
	NMSettingConnection *s_con;
	NMSetting *type_setting = NULL;
	char *id, *uuid;
	GType mb_type;

	ctype = nm_connection_lookup_setting_type (connection_type);

	connection = nm_connection_new ();
	nm_connection_set_scope (connection, NM_CONNECTION_SCOPE_USER);
	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
	uuid = nm_utils_uuid_generate ();
	g_object_set (s_con, NM_SETTING_CONNECTION_UUID, uuid, NULL);
	g_free (uuid);
	nm_connection_add_setting (connection, NM_SETTING (s_con));

	if (ctype == NM_TYPE_SETTING_WIRED) {
		id = get_next_available_name (list, _("Wired connection %d"));
		g_object_set (s_con,
		              NM_SETTING_CONNECTION_ID, id,
		              NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRED_SETTING_NAME,
		              NM_SETTING_CONNECTION_AUTOCONNECT, TRUE,
		              NULL);
		g_free (id);

		type_setting = nm_setting_wired_new ();
	} else if (ctype == NM_TYPE_SETTING_WIRELESS) {
		NMSettingWireless *s_wireless;

		id = get_next_available_name (list, _("Wireless connection %d"));
		g_object_set (s_con,
		              NM_SETTING_CONNECTION_ID, id,
		              NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
		              NM_SETTING_CONNECTION_AUTOCONNECT, TRUE,
		              NULL);
		g_free (id);

		type_setting = nm_setting_wireless_new ();
		s_wireless = NM_SETTING_WIRELESS (type_setting);
		g_object_set (s_wireless, NM_SETTING_WIRELESS_MODE, "infrastructure", NULL);
	} else if ((ctype == NM_TYPE_SETTING_GSM) || (ctype == NM_TYPE_SETTING_CDMA)) {
		/* Since GSM is a placeholder for both GSM and CDMA; ask the user which
		 * one they really want.
		 */
		mb_type = mobile_wizard_ask_connection_type ();
		if (mb_type == NM_TYPE_SETTING_GSM) {
			NMSettingGsm *s_gsm;

			id = get_next_available_name (list, _("GSM connection %d"));
			g_object_set (s_con,
					    NM_SETTING_CONNECTION_ID, id,
					    NM_SETTING_CONNECTION_TYPE, NM_SETTING_GSM_SETTING_NAME,
					    NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
					    NULL);
			g_free (id);

			add_default_serial_setting (connection);

			type_setting = nm_setting_gsm_new ();
			s_gsm = NM_SETTING_GSM (type_setting);
			/* De-facto standard for GSM */
			g_object_set (s_gsm, NM_SETTING_GSM_NUMBER, "*99#", NULL);

			nm_connection_add_setting (connection, nm_setting_ppp_new ());
		} else if (mb_type == NM_TYPE_SETTING_CDMA) {
			NMSettingCdma *s_cdma;

			id = get_next_available_name (list, _("CDMA connection %d"));
			g_object_set (s_con,
					    NM_SETTING_CONNECTION_ID, id,
					    NM_SETTING_CONNECTION_TYPE, NM_SETTING_CDMA_SETTING_NAME,
					    NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
					    NULL);
			g_free (id);

			add_default_serial_setting (connection);

			type_setting = nm_setting_cdma_new ();
			s_cdma = NM_SETTING_CDMA (type_setting);

			/* De-facto standard for CDMA */
			g_object_set (s_cdma, NM_SETTING_CDMA_NUMBER, "#777", NULL);

			nm_connection_add_setting (connection, nm_setting_ppp_new ());
		} else {
			/* user canceled; do nothing */
		}
	} else if (ctype == NM_TYPE_SETTING_VPN) {
		char *service = NULL;

		service = vpn_ask_connection_type ();
		if (service) {
			NMSettingVPN *s_vpn;

			id = get_next_available_name (list, _("VPN connection %d"));
			g_object_set (s_con,
					    NM_SETTING_CONNECTION_ID, id,
					    NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME,
					    NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
					    NULL);
			g_free (id);

			type_setting = nm_setting_vpn_new ();
			s_vpn = NM_SETTING_VPN (type_setting);
			g_object_set (s_vpn, NM_SETTING_VPN_SERVICE_TYPE, service, NULL);
			g_free (service);
		}		
	} else if (ctype == NM_TYPE_SETTING_PPPOE) {
		id = get_next_available_name (list, _("DSL connection %d"));
		g_object_set (s_con,
				    NM_SETTING_CONNECTION_ID, id,
				    NM_SETTING_CONNECTION_TYPE, NM_SETTING_PPPOE_SETTING_NAME,
				    NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
				    NULL);
		g_free (id);

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
add_connection_clicked (GtkButton *button, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	const char *connection_type;
	NMConnection *connection;
	NMConnectionEditor *editor;
	GError *error = NULL;
	const char *message = _("The connection editor dialog could not be initialized due to an unknown error.");

	connection_type = get_connection_type_from_treeview (info->list, info->treeview);
	g_assert (connection_type);

	connection = create_new_connection_for_type (info->list, connection_type);
	if (!connection) {
		g_warning ("Can't add new connection of type '%s'", connection_type);
		return;
	}

	editor = nm_connection_editor_new (connection,
	                                   nm_dbus_settings_system_get_can_modify (info->list->system_settings),
	                                   &error);
	if (!editor) {
		error_dialog (info->list_window,
		              _("Could not edit new connection"),
		              "%s",
		              (error && error->message) ? error->message : message);
		return;
	}

	g_signal_connect (G_OBJECT (editor), "done", G_CALLBACK (add_done_cb), info);
	g_hash_table_insert (info->list->editors, connection, editor);

	nm_connection_editor_run (editor);
}

typedef struct {
	NMConnectionList *list;
	NMExportedConnection *original_connection;
} EditConnectionInfo;

static void
connection_updated_cb (NMConnectionList *list,
				   gboolean success,
				   gpointer user_data)
{
	EditConnectionInfo *info = (EditConnectionInfo *) user_data;

	if (success) {
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
	EditConnectionInfo *info = (EditConnectionInfo *) user_data;
	const char *message = _("An unknown error ocurred.");

	g_hash_table_remove (info->list->editors, info->original_connection);

	if (response == GTK_RESPONSE_NONE) {
		if (error && error->message)
			message = error->message;
		error_dialog (GTK_WINDOW (editor->window), _("Error initializing editor"), "%s", message);
	} else if (response == GTK_RESPONSE_OK) {
		NMConnection *connection;
		GError *edit_error = NULL;
		gboolean success;

		connection = nm_connection_editor_get_connection (editor);

		utils_fill_connection_certs (connection);
		success = nm_connection_verify (connection, &edit_error);
		utils_clear_filled_connection_certs (connection);

		if (success) {
			update_connection (info->list, editor, info->original_connection,
			                   connection, connection_updated_cb, info);
		} else {
			g_warning ("%s: invalid connection after update: bug in the "
			           "'%s' / '%s' invalid: %d",
			           __func__,
			           g_type_name (nm_connection_lookup_setting_type_by_quark (edit_error->domain)),
			           edit_error->message, edit_error->code);
			g_error_free (edit_error);
			connection_updated_cb (info->list, FALSE, user_data);
		}
	}
}

static void
do_edit (ActionInfo *info)
{
	NMExportedConnection *exported;
	NMConnection *connection;
	NMConnectionEditor *editor;
	EditConnectionInfo *edit_info;
	GError *error = NULL;
	const char *message = _("The connection editor dialog could not be initialized due to an unknown error.");

	exported = get_active_connection (info->treeview);
	g_return_if_fail (exported != NULL);

	/* Don't allow two editors for the same connection */
	editor = NM_CONNECTION_EDITOR (g_hash_table_lookup (info->list->editors, exported));
	if (editor) {
		nm_connection_editor_present (editor);
		return;
	}

	connection = nm_gconf_connection_duplicate (nm_exported_connection_get_connection (exported));
	editor = nm_connection_editor_new (connection,
	                                   nm_dbus_settings_system_get_can_modify (info->list->system_settings),
	                                   &error);
	g_object_unref (connection);

	if (!editor) {
		error_dialog (info->list_window,
		              _("Could not edit connection"),
		              "%s",
		              (error && error->message) ? error->message : message);
		return;
	}

	edit_info = g_new (EditConnectionInfo, 1);
	edit_info->list = info->list;
	edit_info->original_connection = g_object_ref (exported);

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
connection_remove_done (NMExportedConnection *exported,
				    gboolean success,
				    gpointer user_data)
{
	if (success) {
		NMConnectionList *list = (NMConnectionList *) user_data;

		/* Close any open editor windows for this connection */
		g_hash_table_remove (list->editors, exported);
	}
}

static void
delete_connection_cb (GtkButton *button, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMExportedConnection *exported = NULL;
	NMConnection *connection;
	NMSettingConnection *s_con;
	GtkWidget *dialog;
	const char *id;
	guint result;

	exported = get_active_connection (info->treeview);
	g_return_if_fail (exported != NULL);
	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	id = s_con ? nm_setting_connection_get_id (s_con) : NULL;

	if (!id)
		return;

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
		remove_connection (exported, GTK_WINDOW (info->list->dialog), connection_remove_done, info->list);
}

static gboolean
check_sensitivity (ActionInfo *info, PolKitResult pk_result)
{
	gboolean sensitive = TRUE;
	NMExportedConnection *exported = NULL;
	NMConnection *connection = NULL;

	exported = get_active_connection (info->treeview);
	if (exported)
		connection = nm_exported_connection_get_connection (exported);
	if (!connection)
		return FALSE;

	if (nm_connection_get_scope (connection) != NM_CONNECTION_SCOPE_SYSTEM)
		return TRUE;

	if (pk_result == POLKIT_RESULT_UNKNOWN)
		pk_result = polkit_gnome_action_get_polkit_result (info->gnome_action);

	if (pk_result == POLKIT_RESULT_NO || pk_result == POLKIT_RESULT_UNKNOWN)
		sensitive = FALSE;

	return sensitive;
}

static void
system_pk_result_changed_cb (PolKitGnomeAction *gnome_action,
                             PolKitResult result,
                             ActionInfo *info)
{
	gtk_widget_set_sensitive (info->button, check_sensitivity (info, result));
}

static void
pk_button_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	GtkTreeIter iter;
	GtkTreeModel *model;
	NMExportedConnection *exported;
	NMConnection *connection = NULL;
	NMSettingConnection *s_con;
	gboolean can_do_action = FALSE;
	gboolean req_privs = FALSE;

	if (!gtk_tree_selection_get_selected (selection, &model, &iter))
		goto done;

	exported = get_active_connection (info->treeview);
	if (exported)
		connection = nm_exported_connection_get_connection (exported);
	if (!connection)
		goto done;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_assert (s_con);

	if (nm_connection_get_scope (connection) != NM_CONNECTION_SCOPE_SYSTEM)
		can_do_action = !nm_setting_connection_get_read_only (s_con);
	else {
		if (!nm_setting_connection_get_read_only (s_con))
			can_do_action = check_sensitivity (info, POLKIT_RESULT_UNKNOWN);
		req_privs = TRUE;
	}

done:
	g_object_set (info->gnome_action, "polkit-action", req_privs ? info->action : NULL, NULL);
	g_object_set (info->gnome_action, "master-sensitive", can_do_action, NULL);
}

static void
vpn_list_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	ActionInfo *info = (ActionInfo *) user_data;
	NMVpnPluginUiInterface *plugin;
	NMExportedConnection *exported;
	NMConnection *connection = NULL;
	NMSettingVPN *s_vpn;
	const char *service_type;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint32 caps;
	gboolean supported = FALSE;

	if (!gtk_tree_selection_get_selected (selection, &model, &iter))
		goto done;

	exported = get_active_connection (info->treeview);
	if (exported)
		connection = nm_exported_connection_get_connection (exported);
	if (!connection)
		goto done;

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
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
		s = get_next_available_name (info->list, _("VPN connection %d"));
		g_object_set (s_con, NM_SETTING_CONNECTION_ID, s, NULL);
		g_free (s);
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

	editor = nm_connection_editor_new (connection,
	                                   nm_dbus_settings_system_get_can_modify (info->list->system_settings),
	                                   &error);
	if (!editor) {
		error_dialog (info->list_window,
		              _("Could not edit imported connection"),
		              "%s",
		              (error && error->message) ? error->message : message);
		return;
	}

	g_signal_connect (G_OBJECT (editor), "done", G_CALLBACK (add_done_cb), info);
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
	NMExportedConnection *exported;
	NMConnection *connection = NULL;

	exported = get_active_connection (info->treeview);
	if (exported)
		connection = nm_exported_connection_get_connection (exported);
	if (!connection)
		return;

	vpn_export (connection);
}

static void
connection_double_clicked_cb (GtkTreeView *tree_view,
                              GtkTreePath *path,
                              GtkTreeViewColumn *column,
                              gpointer user_data)
{
	ActionInfo *info = user_data;

	if (GTK_WIDGET_SENSITIVE (info->button))
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

	list->system_action = polkit_action_new ();
	polkit_action_set_action_id (list->system_action, "org.freedesktop.network-manager-settings.system.modify");
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

	polkit_action_unref (list->system_action);

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
action_info_free (ActionInfo *info)
{
	g_return_if_fail (info != NULL);

	/* gnome_action should have been destroyed when the dialog got destroyed */
	polkit_action_unref (info->action);
	g_free (info);
}

static ActionInfo *
action_info_new (NMConnectionList *list,
                 GtkTreeView *treeview,
                 GtkWindow *list_window,
                 GtkWidget *button,
                 PolKitAction *action)
{
	ActionInfo *info;

	info = g_malloc0 (sizeof (ActionInfo));
	g_object_weak_ref (G_OBJECT (list), (GWeakNotify) action_info_free, info);

	info->list = list;
	info->treeview = treeview;
	info->list_window = list_window;
	info->button = button;
	info->action = polkit_action_ref (action);
	return info;
}

static void
action_info_set_button (ActionInfo *info,
                        GtkWidget *button,
                        PolKitGnomeAction *gnome_action)
{
	g_return_if_fail (info != NULL);

	info->button = button;
	if (info->gnome_action)
		g_object_unref (info->gnome_action);
	info->gnome_action = gnome_action;
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
                             GCallback result_callback,
                             GtkWidget *hbox,
                             PolKitGnomeAction **out_action,
                             gpointer user_data)
{
	PolKitGnomeAction *action;
	GtkWidget *button;

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
	gtk_box_pack_end (GTK_BOX (hbox), button, TRUE, TRUE, 0);

	*out_action = action;
	return button;
}

static void
add_connection_buttons (NMConnectionList *self,
                        const char *prefix,
                        GtkTreeView *treeview,
                        gboolean is_vpn)
{
	char *name;
	GtkWidget *button, *hbox;
	ActionInfo *info;
	GtkTreeSelection *selection;
	PolKitGnomeAction *action = NULL;

	selection = gtk_tree_view_get_selection (treeview);

	/* Add */
	name = g_strdup_printf ("%s_add", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), NULL, self->system_action);
	g_signal_connect (button, "clicked", G_CALLBACK (add_connection_clicked), info);
	if (is_vpn) {
		GHashTable *plugins;

		/* disable the "Add..." button if there aren't any VPN plugins */
		plugins = vpn_get_plugins (NULL);
		gtk_widget_set_sensitive (button, (plugins && g_hash_table_size (plugins)));
	}

	name = g_strdup_printf ("%s_button_box", prefix);
	hbox = glade_xml_get_widget (self->gui, name);
	g_free (name);

	/* Edit */
	info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), NULL, self->system_action);
	action = NULL;
	button = create_system_action_button ("system-edit",
	                                      _("Edit..."),
	                                      _("Authenticate to edit the selected connection."),
	                                      _("Edit"),
	                                      _("Edit the selected connection."),
	                                      GTK_STOCK_EDIT,
	                                      GTK_STOCK_DIALOG_AUTHENTICATION,
	                                      G_CALLBACK (system_pk_result_changed_cb),
	                                      hbox,
	                                      &action,
	                                      info);
	action_info_set_button (info, button, action);
	g_signal_connect (button, "clicked", G_CALLBACK (edit_connection_cb), info);
	g_signal_connect (treeview, "row-activated", G_CALLBACK (connection_double_clicked_cb), info);
	g_signal_connect (selection, "changed", G_CALLBACK (pk_button_selection_changed_cb), info);
	pk_button_selection_changed_cb (selection, info);

	/* Delete */
	info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), NULL, self->system_action);
	action = NULL;
	button = create_system_action_button ("system-delete",
	                                      _("Delete..."),
	                                      _("Authenticate to delete the selected connection."),
	                                      _("Delete"),
	                                      _("Delete the selected connection."),
	                                      GTK_STOCK_DELETE,
	                                      GTK_STOCK_DIALOG_AUTHENTICATION,
	                                      G_CALLBACK (system_pk_result_changed_cb),
	                                      hbox,
	                                      &action,
	                                      info);
	action_info_set_button (info, button, action);
	g_signal_connect (button, "clicked", G_CALLBACK (delete_connection_cb), info);
	g_signal_connect (selection, "changed", G_CALLBACK (pk_button_selection_changed_cb), info);
	pk_button_selection_changed_cb (selection, info);

	/* Import */
	name = g_strdup_printf ("%s_import", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	if (button) {
		gboolean import_supported = FALSE;

		info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), button, self->system_action);
		g_signal_connect (button, "clicked", G_CALLBACK (import_vpn_cb), info);

		g_hash_table_foreach (vpn_get_plugins (NULL), check_vpn_import_supported, &import_supported);
		gtk_widget_set_sensitive (button, import_supported);
	}

	/* Export */
	name = g_strdup_printf ("%s_export", prefix);
	button = glade_xml_get_widget (self->gui, name);
	g_free (name);
	if (button) {
		info = action_info_new (self, treeview, GTK_WINDOW (self->dialog), button, self->system_action);
		g_signal_connect (button, "clicked", G_CALLBACK (export_vpn_cb), info);
		g_signal_connect (selection, "changed", G_CALLBACK (vpn_list_selection_changed_cb), info);
		gtk_widget_set_sensitive (button, FALSE);
	}
}

static void
add_connection_tab (NMConnectionList *self,
                    const char *def_type,
                    GSList *connection_types,
                    GdkPixbuf *pixbuf,
                    const char *prefix,
                    const char *label_text,
                    gboolean is_vpn)
{
	char *name;
	GtkWidget *child, *hbox, *notebook;
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

	notebook = glade_xml_get_widget (self->gui, "list_notebook");
	gtk_notebook_set_tab_label (GTK_NOTEBOOK (notebook), child, hbox);

	treeview = add_connection_treeview (self, prefix);
	add_connection_buttons (self, prefix, treeview, is_vpn);

	g_object_set_data_full (G_OBJECT (child), "types",
	                        connection_types, (GDestroyNotify) g_slist_free);

	for (iter = connection_types; iter; iter = iter->next) {
		g_hash_table_insert (self->treeviews, g_strdup ((const char *) iter->data), treeview);
		if (def_type && !strcmp ((const char *) iter->data, def_type)) {
			int pnum;

			pnum = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), child);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), pnum);
		}
	}
}

static void
add_connection_tabs (NMConnectionList *self, const char *def_type)
{
	GSList *types;

	types = g_slist_append (NULL, NM_SETTING_WIRED_SETTING_NAME);
	add_connection_tab (self, def_type, types, self->wired_icon, "wired", _("Wired"), FALSE);

	types = g_slist_append (NULL, NM_SETTING_WIRELESS_SETTING_NAME);
	add_connection_tab (self, def_type, types, self->wireless_icon, "wireless", _("Wireless"), FALSE);

	types = g_slist_append (NULL, NM_SETTING_GSM_SETTING_NAME);
	types = g_slist_append (types, NM_SETTING_CDMA_SETTING_NAME);
	add_connection_tab (self, def_type, types, self->wwan_icon, "wwan", _("Mobile Broadband"), FALSE);

	types = g_slist_append (NULL, NM_SETTING_VPN_SETTING_NAME);
	add_connection_tab (self, def_type, types, self->vpn_icon, "vpn", _("VPN"), TRUE);

	types = g_slist_append (NULL, NM_SETTING_PPPOE_SETTING_NAME);
	add_connection_tab (self, def_type, types, self->wired_icon, "dsl", _("DSL"), FALSE);
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

	last_used = format_last_used (nm_setting_connection_get_timestamp (s_con));

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
	                    COL_ID, nm_setting_connection_get_id (s_con),
	                    COL_LAST_USED, last_used,
	                    COL_TIMESTAMP, nm_setting_connection_get_timestamp (s_con),
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
nm_connection_list_new (const char *def_type)
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

	add_connection_tabs (list, def_type);

	list->editors = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, g_object_unref);

	list->dialog = glade_xml_get_widget (list->gui, "NMConnectionList");
	if (!list->dialog)
		goto error;
	g_signal_connect (G_OBJECT (list->dialog), "response", G_CALLBACK (dialog_response_cb), list);

	if (!vpn_get_plugins (&error)) {
		g_message ("%s: failed to load VPN plugins: %s", __func__, error->message);
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
nm_connection_list_set_type (NMConnectionList *self, const char *type)
{
	GtkWidget *notebook;
	int i, num;
	gboolean found = FALSE;

	g_return_if_fail (NM_IS_CONNECTION_LIST (self));

	/* If a notebook page is found that owns the requested type, set it
	 * as the current page.
	 */
	notebook = glade_xml_get_widget (self->gui, "list_notebook");
	num = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));
	for (i = 0; i < num && !found; i++) {
		GtkWidget *child;
		GSList *types, *iter;

		child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), i);
		types = g_object_get_data (G_OBJECT (child), "types");
		for (iter = types; iter; iter = g_slist_next (iter)) {
			if (!strcmp (type, (const char *) iter->data)) {
				gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), i);
				found = TRUE;
				break;
			}
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

