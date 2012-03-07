/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Rodrigo Moya <rodrigo@gnome-db.org>
 * Dan Williams <dcbw@redhat.com>
 * Tambet Ingo <tambet@gmail.com>
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
 * (C) Copyright 2007 - 2011 Red Hat, Inc.
 * (C) Copyright 2007 - 2008 Novell, Inc.
 */

#include "config.h"

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>

#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-ip6-config.h>
#include <nm-setting-wired.h>
#include <nm-setting-8021x.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wireless-security.h>
#include <nm-setting-vpn.h>
#include <nm-setting-pppoe.h>
#include <nm-setting-ppp.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-utils.h>

#include <nm-remote-connection.h>

#include "nm-connection-editor.h"
#include "nma-marshal.h"

#include "ce-page.h"
#include "page-wired.h"
#include "page-wired-security.h"
#include "page-wireless.h"
#include "page-wireless-security.h"
#include "page-ip4.h"
#include "page-ip6.h"
#include "page-dsl.h"
#include "page-mobile.h"
#include "page-ppp.h"
#include "page-vpn.h"
#include "ce-polkit-button.h"

G_DEFINE_TYPE (NMConnectionEditor, nm_connection_editor, G_TYPE_OBJECT)

enum {
	EDITOR_DONE,
	EDITOR_LAST_SIGNAL
};

static guint editor_signals[EDITOR_LAST_SIGNAL] = { 0 };

static gboolean nm_connection_editor_set_connection (NMConnectionEditor *editor,
                                                     NMConnection *connection,
                                                     GError **error);

struct GetSecretsInfo {
	NMConnectionEditor *self;
	CEPage *page;
	char *setting_name;
	gboolean canceled;
};

static void
nm_connection_editor_update_title (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	const char *id;

	g_return_if_fail (editor != NULL);

	s_con = nm_connection_get_setting_connection (editor->connection);
	g_assert (s_con);

	id = nm_setting_connection_get_id (s_con);
	if (id && strlen (id)) {
		char *title = g_strdup_printf (_("Editing %s"), id);
		gtk_window_set_title (GTK_WINDOW (editor->window), title);
		g_free (title);
	} else
		gtk_window_set_title (GTK_WINDOW (editor->window), _("Editing un-named connection"));
}

static gboolean
ui_to_setting (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	GtkWidget *widget;
	const char *name;
	gboolean autoconnect = FALSE, everyone = FALSE;

	s_con = nm_connection_get_setting_connection (editor->connection);
	g_assert (s_con);

	widget = GTK_WIDGET (gtk_builder_get_object (editor->builder, "connection_name"));
	name = gtk_entry_get_text (GTK_ENTRY (widget));

	g_object_set (G_OBJECT (s_con), NM_SETTING_CONNECTION_ID, name, NULL);
	nm_connection_editor_update_title (editor);

	if (!name || !strlen (name))
		return FALSE;

	widget = GTK_WIDGET (gtk_builder_get_object (editor->builder, "connection_autoconnect"));
	autoconnect = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	g_object_set (G_OBJECT (s_con), NM_SETTING_CONNECTION_AUTOCONNECT, autoconnect, NULL);

	/* Handle visibility */
	everyone = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (editor->all_checkbutton));
	g_object_set (G_OBJECT (s_con), NM_SETTING_CONNECTION_PERMISSIONS, NULL, NULL);
	if (everyone == FALSE) {
		/* Only visible to this user */
		nm_setting_connection_add_permission (s_con, "user", g_get_user_name (), NULL);
	}

	return TRUE;
}

static gboolean
editor_is_initialized (NMConnectionEditor *editor)
{
	return (g_slist_length (editor->initializing_pages) == 0);
}

static void
update_sensitivity (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	gboolean actionable = FALSE, authorized = FALSE, sensitive = FALSE;
	GtkWidget *widget;
	GSList *iter;

	s_con = nm_connection_get_setting_connection (editor->connection);

	/* Can't modify read-only connections; can't modify anything before the
	 * editor is initialized either.
	 */
	if (   !nm_setting_connection_get_read_only (s_con)
	    && editor_is_initialized (editor)) {
		if (editor->can_modify) {
			actionable = ce_polkit_button_get_actionable (CE_POLKIT_BUTTON (editor->ok_button));
			authorized = ce_polkit_button_get_authorized (CE_POLKIT_BUTTON (editor->ok_button));
		}

		/* If the user cannot ever be authorized to change system connections,
		 * we desensitize the entire dialog.
		 */
		sensitive = authorized;
	}

	gtk_widget_set_sensitive (GTK_WIDGET (editor->all_checkbutton), actionable && authorized);

	/* Cancel button is always sensitive */
	gtk_widget_set_sensitive (GTK_WIDGET (editor->cancel_button), TRUE);

	widget = GTK_WIDGET (gtk_builder_get_object (editor->builder, "connection_name_label"));
	gtk_widget_set_sensitive (widget, sensitive);

	widget = GTK_WIDGET (gtk_builder_get_object (editor->builder, "connection_name"));
	gtk_widget_set_sensitive (widget, sensitive);

	widget = GTK_WIDGET (gtk_builder_get_object (editor->builder, "connection_autoconnect"));
	gtk_widget_set_sensitive (widget, sensitive);

	widget = GTK_WIDGET (gtk_builder_get_object (editor->builder, "connection_name"));
	gtk_widget_set_sensitive (widget, sensitive);

	for (iter = editor->pages; iter; iter = g_slist_next (iter)) {
		widget = ce_page_get_page (CE_PAGE (iter->data));
		gtk_widget_set_sensitive (widget, sensitive);
	}
}

static void
connection_editor_validate (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	gboolean valid = FALSE;
	GSList *iter;

	if (!editor_is_initialized (editor))
		goto done;

	s_con = nm_connection_get_setting_connection (editor->connection);
	g_assert (s_con);
	if (nm_setting_connection_get_read_only (s_con))
		goto done;

	if (!ui_to_setting (editor))
		goto done;

	for (iter = editor->pages; iter; iter = g_slist_next (iter)) {
		GError *error = NULL;

		if (!ce_page_validate (CE_PAGE (iter->data), editor->connection, &error)) {
			/* FIXME: use the error to indicate which UI widgets are invalid */
			if (error) {
				g_warning ("Invalid setting %s: %s", CE_PAGE (iter->data)->title, error->message);
				g_error_free (error);
			} else
				g_warning ("Invalid setting %s", CE_PAGE (iter->data)->title);

			goto done;
		}
	}
	valid = TRUE;

done:
	ce_polkit_button_set_master_sensitive (CE_POLKIT_BUTTON (editor->ok_button), valid);
	update_sensitivity (editor);
}

static void
ok_button_actionable_cb (GtkWidget *button,
                         gboolean actionable,
                         NMConnectionEditor *editor)
{
	connection_editor_validate (editor);
}

static void
permissions_changed_cb (NMClient *client,
	                    NMClientPermission permission,
	                    NMClientPermissionResult result,                       
                        NMConnectionEditor *editor)
{
	if (permission != NM_CLIENT_PERMISSION_SETTINGS_MODIFY_SYSTEM)
		return;

	if (result == NM_CLIENT_PERMISSION_RESULT_YES || result == NM_CLIENT_PERMISSION_RESULT_AUTH)
		editor->can_modify = TRUE;
	else
		editor->can_modify = FALSE;

	connection_editor_validate (editor);
}

static void
all_checkbutton_toggled_cb (GtkWidget *widget, NMConnectionEditor *editor)
{
	connection_editor_validate (editor);
}

static void
nm_connection_editor_init (NMConnectionEditor *editor)
{
	GtkWidget *dialog;
	GError *error = NULL;
	const char *objects[] = { "nm-connection-editor", NULL };

	editor->builder = gtk_builder_new ();

	if (!gtk_builder_add_objects_from_file (editor->builder,
	                                        UIDIR "/nm-connection-editor.ui",
	                                        (char **) objects,
	                                        &error)) {
		g_warning ("Couldn't load builder file " UIDIR "/nm-connection-editor.ui: %s", error->message);
		g_error_free (error);

		dialog = gtk_message_dialog_new (NULL, 0,
		                                 GTK_MESSAGE_ERROR,
		                                 GTK_BUTTONS_OK,
		                                 "%s",
		                                 _("The connection editor could not find some required resources (the .ui file was not found)."));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		gtk_main_quit ();
		return;
	}

	editor->window = GTK_WIDGET (gtk_builder_get_object (editor->builder, "nm-connection-editor"));
	editor->cancel_button = GTK_WIDGET (gtk_builder_get_object (editor->builder, "cancel_button"));
	editor->all_checkbutton = GTK_WIDGET (gtk_builder_get_object (editor->builder, "system_checkbutton"));
}

static void
get_secrets_info_free (GetSecretsInfo *info)
{
	g_free (info->setting_name);
	g_free (info);
}

static void
dispose (GObject *object)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (object);
	GSList *iter;

	if (editor->disposed)
		goto out;
	editor->disposed = TRUE;

	g_slist_foreach (editor->initializing_pages, (GFunc) g_object_unref, NULL);
	g_slist_free (editor->initializing_pages);
	editor->initializing_pages = NULL;

	g_slist_foreach (editor->pages, (GFunc) g_object_unref, NULL);
	g_slist_free (editor->pages);
	editor->pages = NULL;

	/* Mark any in-progress secrets call as canceled; it will clean up after itself. */
	if (editor->secrets_call)
		editor->secrets_call->canceled = TRUE;

	/* Kill any pending secrets calls */
	for (iter = editor->pending_secrets_calls; iter; iter = g_slist_next (iter)) {
		get_secrets_info_free ((GetSecretsInfo *) iter->data);
	}
	g_slist_free (editor->pending_secrets_calls);
	editor->pending_secrets_calls = NULL;

	if (editor->connection) {
		g_object_unref (editor->connection);
		editor->connection = NULL;
	}
	if (editor->orig_connection) {
		g_object_unref (editor->orig_connection);
		editor->orig_connection = NULL;
	}
	if (editor->window) {
		gtk_widget_destroy (editor->window);
		editor->window = NULL;
	}
	if (editor->builder) {
		g_object_unref (editor->builder);
		editor->builder = NULL;
	}

	g_signal_handler_disconnect (editor->client, editor->permission_id);
	g_object_unref (editor->client);

out:
	G_OBJECT_CLASS (nm_connection_editor_parent_class)->dispose (object);
}

static void
nm_connection_editor_class_init (NMConnectionEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/* virtual methods */
	object_class->dispose = dispose;

	/* Signals */
	editor_signals[EDITOR_DONE] =
		g_signal_new ("done",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (NMConnectionEditorClass, done),
		              NULL, NULL,
		              _nma_marshal_VOID__INT_POINTER,
		              G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
}

NMConnectionEditor *
nm_connection_editor_new (NMConnection *connection,
                          NMClient *client,
                          GError **error)
{
	NMConnectionEditor *editor;
	GtkWidget *hbox;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	editor = g_object_new (NM_TYPE_CONNECTION_EDITOR, NULL);
	if (!editor) {
		g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC, "%s", _("Error creating connection editor dialog."));
		return NULL;
	}

	editor->client = g_object_ref (client);

	editor->can_modify = nm_client_get_permission_result (client, NM_CLIENT_PERMISSION_SETTINGS_MODIFY_SYSTEM);
	editor->permission_id = g_signal_connect (editor->client,
	                                          "permission-changed",
	                                          G_CALLBACK (permissions_changed_cb),
	                                          editor);

	editor->ok_button = ce_polkit_button_new (_("_Save"),
	                                          _("Save any changes made to this connection."),
	                                          _("_Save..."),
	                                          _("Authenticate to save this connection for all users of this machine."),
	                                          GTK_STOCK_APPLY,
	                                          client,
	                                          NM_CLIENT_PERMISSION_SETTINGS_MODIFY_SYSTEM);
	gtk_button_set_use_underline (GTK_BUTTON (editor->ok_button), TRUE);

	g_signal_connect (editor->ok_button, "actionable",
	                  G_CALLBACK (ok_button_actionable_cb), editor);
	g_signal_connect (editor->ok_button, "authorized",
	                  G_CALLBACK (ok_button_actionable_cb), editor);
	hbox = GTK_WIDGET (gtk_builder_get_object (editor->builder, "action_area_hbox"));
	gtk_box_pack_end (GTK_BOX (hbox), editor->ok_button, TRUE, TRUE, 0);
	gtk_widget_show_all (editor->ok_button);

	if (!nm_connection_editor_set_connection (editor, connection, error)) {
		g_object_unref (editor);
		return NULL;
	}

	return editor;
}

NMConnection *
nm_connection_editor_get_connection (NMConnectionEditor *editor)
{
	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), NULL);

	return editor->orig_connection;
}

static void
update_secret_flags (NMSetting *setting,
                     const char *key,
                     const GValue *value,
                     GParamFlags flags,
                     gpointer user_data)
{
	gboolean everyone = !!GPOINTER_TO_UINT (user_data);
	NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;

	if (!(flags & NM_SETTING_PARAM_SECRET))
		return;

	/* VPN connections never get changed */
	if (NM_IS_SETTING_VPN (setting))
		return;

	/* 802.1x passwords don't get changed either */
	if (NM_IS_SETTING_802_1X (setting)) {
		if (   g_strcmp0 (key, NM_SETTING_802_1X_PASSWORD) == 0
		    || g_strcmp0 (key, NM_SETTING_802_1X_PRIVATE_KEY_PASSWORD) == 0
		    || g_strcmp0 (key, NM_SETTING_802_1X_PHASE2_PRIVATE_KEY_PASSWORD) == 0)
			return;
	}

	nm_setting_get_secret_flags (setting, key, &secret_flags, NULL);
	if (everyone)
		secret_flags &= ~NM_SETTING_SECRET_FLAG_AGENT_OWNED;
	else
		secret_flags |= NM_SETTING_SECRET_FLAG_AGENT_OWNED;

	nm_setting_set_secret_flags (setting, key, secret_flags, NULL);
}

gboolean
nm_connection_editor_update_connection (NMConnectionEditor *editor, GError **error)
{
	GHashTable *settings;
	gboolean everyone = FALSE;

	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), FALSE);

	if (!nm_connection_verify (editor->connection, error))
		return FALSE;

	/* Update secret flags at the end after all other settings have updated,
	 * otherwise the secret flags we set here might be overwritten during
	 * setting validation.
	 */
	everyone = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (editor->all_checkbutton));
	nm_connection_for_each_setting_value (editor->connection, update_secret_flags, GUINT_TO_POINTER (everyone));

	/* Copy the modified connection to the original connection */
	settings = nm_connection_to_hash (editor->connection, NM_SETTING_HASH_FLAG_ALL);
	nm_connection_replace_settings (editor->orig_connection, settings, NULL);
	g_hash_table_destroy (settings);

	return TRUE;
}

static void
populate_connection_ui (NMConnectionEditor *editor)
{
	NMSettingConnection *s_con;
	GtkWidget *name;
	GtkWidget *autoconnect;
	gboolean system_connection = TRUE;

	name = GTK_WIDGET (gtk_builder_get_object (editor->builder, "connection_name"));
	autoconnect = GTK_WIDGET (gtk_builder_get_object (editor->builder, "connection_autoconnect"));

	s_con = nm_connection_get_setting_connection (editor->connection);
	if (s_con) {
		const char *id = nm_setting_connection_get_id (s_con);

		gtk_entry_set_text (GTK_ENTRY (name), id);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autoconnect),
		                              nm_setting_connection_get_autoconnect (s_con));

		if (nm_setting_connection_get_num_permissions (s_con))
			system_connection = FALSE;
	} else {
		gtk_entry_set_text (GTK_ENTRY (name), NULL);
	}

	g_signal_connect_swapped (name, "changed", G_CALLBACK (connection_editor_validate), editor);
	g_signal_connect_swapped (autoconnect, "toggled", G_CALLBACK (connection_editor_validate), editor);

	g_signal_connect (editor->all_checkbutton, "toggled", G_CALLBACK (all_checkbutton_toggled_cb), editor);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (editor->all_checkbutton), system_connection);

	connection_editor_validate (editor);
}

static void
page_changed (CEPage *page, gpointer user_data)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (user_data);

	connection_editor_validate (editor);
}

static gboolean
idle_validate (gpointer user_data)
{
	connection_editor_validate (NM_CONNECTION_EDITOR (user_data));
	return FALSE;
}

static void
recheck_initialization (NMConnectionEditor *editor)
{
	if (!editor_is_initialized (editor) || editor->init_run)
		return;

	editor->init_run = TRUE;

	populate_connection_ui (editor);

	/* When everything is initialized, re-present the window to ensure it's on top */
	nm_connection_editor_present (editor);

	/* Validate the connection from an idle handler to ensure that stuff like
	 * GtkFileChoosers have had a chance to asynchronously find their files.
	 */
	g_idle_add (idle_validate, editor);
}

static void
page_initialized (CEPage *page, GError *error, gpointer user_data)
{
	NMConnectionEditor *editor = NM_CONNECTION_EDITOR (user_data);
	GtkWidget *widget, *parent;
	GtkNotebook *notebook;
	GtkWidget *label;

	if (error) {
		gtk_widget_hide (editor->window);
		g_signal_emit (editor, editor_signals[EDITOR_DONE], 0, GTK_RESPONSE_NONE, error);
		return;
	}

	/* Add the page to the UI */
	notebook = GTK_NOTEBOOK (gtk_builder_get_object (editor->builder, "notebook"));
	label = gtk_label_new (ce_page_get_title (page));
	widget = ce_page_get_page (page);
	parent = gtk_widget_get_parent (widget);
	if (parent)
		gtk_container_remove (GTK_CONTAINER (parent), widget);
	gtk_notebook_append_page (notebook, widget, label);

	/* Move the page from the initializing list to the main page list */
	editor->initializing_pages = g_slist_remove (editor->initializing_pages, page);
	editor->pages = g_slist_append (editor->pages, page);

	recheck_initialization (editor);
}

static void request_secrets (GetSecretsInfo *info);

static void
get_secrets_cb (NMRemoteConnection *connection,
                GHashTable *secrets,
                GError *error,
                gpointer user_data)
{
	GetSecretsInfo *info = user_data;
	NMConnectionEditor *self;

	if (info->canceled) {
		get_secrets_info_free (info);
		return;
	}

	self = info->self;

	/* Complete this secrets request; completion can actually dispose of the
	 * dialog if there was an error.
	 */
	self->secrets_call = NULL;
	ce_page_complete_init (info->page, info->setting_name, secrets, error);
	get_secrets_info_free (info);

	/* Kick off the next secrets request if there is one queued; if the dialog
	 * was disposed of by the completion above we don't need to do anything.
	 */
	if (!self->disposed && self->pending_secrets_calls) {
		self->secrets_call = g_slist_nth_data (self->pending_secrets_calls, 0);
		self->pending_secrets_calls = g_slist_remove (self->pending_secrets_calls, self->secrets_call);

		request_secrets (self->secrets_call);
	}
}

static void
request_secrets (GetSecretsInfo *info)
{
	g_return_if_fail (info != NULL);

	nm_remote_connection_get_secrets (NM_REMOTE_CONNECTION (info->self->orig_connection),
	                                  info->setting_name,
	                                  get_secrets_cb,
	                                  info);
}

static void
get_secrets_for_page (NMConnectionEditor *self,
                      CEPage *page,
                      const char *setting_name)
{
	GetSecretsInfo *info;

	info = g_malloc0 (sizeof (GetSecretsInfo));
	info->self = self;
	info->page = page;
	info->setting_name = g_strdup (setting_name);

	/* PolicyKit doesn't queue up authorization requests internally.  Instead,
	 * if there's a pending authorization request, subsequent requests for that
	 * same authorization will return NotAuthorized+Challenge.  That's pretty
	 * inconvenient and it would be a lot nicer if PK just queued up subsequent
	 * authorization requests and executed them when the first one was finished.
	 * But it since it doesn't do that, we have to serialize the authorization
	 * requests ourselves to get the right authorization result.
	 */
	/* NOTE: PolicyKit-gnome 0.95 now serializes auth requests as of this commit:
	 * http://git.gnome.org/cgit/PolicyKit-gnome/commit/?id=f32cb7faa7197b9db55b569677732742c3c7fdc1
	 */

	/* If there's already an in-progress call, queue up the new one */
	if (self->secrets_call)
		self->pending_secrets_calls = g_slist_append (self->pending_secrets_calls, info);
	else {
		/* Request secrets for this page */
		self->secrets_call = info;
		request_secrets (info);
	}
}

#define SECRETS_TAG "secrets-setting-name"

static gboolean
add_page (NMConnectionEditor *editor,
          CEPageNewFunc func,
          NMConnection *connection,
          GError **error)
{
	CEPage *page;
	const char *secrets_setting_name = NULL;

	g_return_val_if_fail (editor != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);
	g_return_val_if_fail (connection != NULL, FALSE);

	page = (*func) (connection, GTK_WINDOW (editor->window), editor->client, &secrets_setting_name, error);
	if (page) {
		g_object_set_data_full (G_OBJECT (page),
		                        SECRETS_TAG,
		                        g_strdup (secrets_setting_name),
		                        g_free);

		editor->initializing_pages = g_slist_append (editor->initializing_pages, page);
		g_signal_connect (page, "changed", G_CALLBACK (page_changed), editor);
		g_signal_connect (page, "initialized", G_CALLBACK (page_initialized), editor);
	}
	return !!page;
}

static gboolean
nm_connection_editor_set_connection (NMConnectionEditor *editor,
                                     NMConnection *orig_connection,
                                     GError **error)
{
	NMSettingConnection *s_con;
	const char *connection_type;
	gboolean success = FALSE;
	GSList *iter, *copy;

	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (orig_connection), FALSE);

	/* clean previous connection */
	if (editor->connection)
		g_object_unref (editor->connection);

	editor->connection = nm_connection_duplicate (orig_connection);

	editor->orig_connection = g_object_ref (orig_connection);
	nm_connection_editor_update_title (editor);

	s_con = nm_connection_get_setting_connection (editor->connection);
	g_assert (s_con);

	connection_type = nm_setting_connection_get_connection_type (s_con);
	if (!strcmp (connection_type, NM_SETTING_WIRED_SETTING_NAME)) {
		if (!add_page (editor, ce_page_wired_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_wired_security_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip6_new, editor->connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_WIRELESS_SETTING_NAME)) {
		if (!add_page (editor, ce_page_wireless_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_wireless_security_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip6_new, editor->connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_VPN_SETTING_NAME)) {
		if (!add_page (editor, ce_page_vpn_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, editor->connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_PPPOE_SETTING_NAME)) {
		if (!add_page (editor, ce_page_dsl_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_wired_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ppp_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, editor->connection, error))
			goto out;
	} else if (!strcmp (connection_type, NM_SETTING_GSM_SETTING_NAME) || 
	           !strcmp (connection_type, NM_SETTING_CDMA_SETTING_NAME)) {
		if (!add_page (editor, ce_page_mobile_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ppp_new, editor->connection, error))
			goto out;
		if (!add_page (editor, ce_page_ip4_new, editor->connection, error))
			goto out;
	} else {
		g_warning ("Unhandled setting type '%s'", connection_type);
	}

	/* After all pages are created, then kick off secrets requests that any
	 * the pages may need to make; if they don't need any secrets, then let
	 * them finish initialization.  The list might get modified during the loop
	 * which is why copy the list here.
	 */
	copy = g_slist_copy (editor->initializing_pages);
	for (iter = copy; iter; iter = g_slist_next (iter)) {
		CEPage *page = CE_PAGE (iter->data);
		const char *setting_name = g_object_get_data (G_OBJECT (page), SECRETS_TAG);

		if (!setting_name) {
			/* page doesn't need any secrets */
			ce_page_complete_init (page, NULL, NULL, NULL);
		} else if (!NM_IS_REMOTE_CONNECTION (editor->orig_connection)) {
			/* We want to get secrets using ->orig_connection, since that's the
			 * remote connection which can actually respond to secrets requests.
			 * ->connection is a plain NMConnection copy of ->orig_connection
			 * which is what gets changed when users modify anything.  But when
			 * creating or importing, ->orig_connection will be an NMConnection
			 * since the new connection hasn't been added to NetworkManager yet.
			 * So basically, skip requesting secrets if the connection can't
			 * handle a secrets request.
			 */
			ce_page_complete_init (page, setting_name, NULL, NULL);
		} else {
			/* Page wants secrets, get them */
			get_secrets_for_page (editor, page, setting_name);
		}
		g_object_set_data (G_OBJECT (page), SECRETS_TAG, NULL);
	}
	g_slist_free (copy);

	/* set the UI */
	recheck_initialization (editor);
	success = TRUE;

out:
	return success;
}

void
nm_connection_editor_present (NMConnectionEditor *editor)
{
	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));

	gtk_window_present (GTK_WINDOW (editor->window));
}

static void
cancel_button_clicked_cb (GtkWidget *widget, gpointer user_data)
{
	NMConnectionEditor *self = NM_CONNECTION_EDITOR (user_data);

	g_signal_emit (self, editor_signals[EDITOR_DONE], 0, GTK_RESPONSE_CANCEL, NULL);
}

static void
editor_closed_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	cancel_button_clicked_cb (widget, user_data);
}

static void
ok_button_clicked_cb (GtkWidget *widget, gpointer user_data)
{
	NMConnectionEditor *self = NM_CONNECTION_EDITOR (user_data);

	g_signal_emit (self, editor_signals[EDITOR_DONE], 0, GTK_RESPONSE_OK, NULL);
}

void
nm_connection_editor_run (NMConnectionEditor *self)
{
	g_return_if_fail (NM_IS_CONNECTION_EDITOR (self));

	g_signal_connect (G_OBJECT (self->window), "delete-event",
	                  G_CALLBACK (editor_closed_cb), self);

	g_signal_connect (G_OBJECT (self->ok_button), "clicked",
	                  G_CALLBACK (ok_button_clicked_cb), self);
	g_signal_connect (G_OBJECT (self->cancel_button), "clicked",
	                  G_CALLBACK (cancel_button_clicked_cb), self);

	nm_connection_editor_present (self);
}

GtkWindow *
nm_connection_editor_get_window (NMConnectionEditor *editor)
{
	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), NULL);

	return GTK_WINDOW (editor->window);
}

gboolean
nm_connection_editor_get_busy (NMConnectionEditor *editor)
{
	g_return_val_if_fail (NM_IS_CONNECTION_EDITOR (editor), FALSE);

	return editor->busy;
}

void
nm_connection_editor_set_busy (NMConnectionEditor *editor, gboolean busy)
{
	g_return_if_fail (NM_IS_CONNECTION_EDITOR (editor));

	if (busy != editor->busy) {
		editor->busy = busy;
		gtk_widget_set_sensitive (editor->window, !busy);
	}
}

