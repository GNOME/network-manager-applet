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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2004 Red Hat, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <glib/gi18n.h>
#include <unistd.h>

#include "vpn-password-dialog.h"
#include "nm-utils.h"
#include <nm-connection.h>
#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>
#include <nm-settings.h>


typedef struct {
	GSList *lines;
	int child_stdin;
	int num_newlines;
} IOUserData;

static void 
child_finished_cb (GPid pid, gint status, gpointer userdata)
{
	int *child_status = (gboolean *) userdata;

	*child_status = status;
}

static gboolean 
child_stdout_data_cb (GIOChannel *source, GIOCondition condition, gpointer userdata)
{
	char *str;
	IOUserData *io_user_data = (IOUserData *) userdata;

	if (! (condition & G_IO_IN))
		goto out;

	if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
		int len;

		len = strlen (str);
		if (len == 1 && str[0] == '\n') {

			/* on second line with a newline newline */
			if (++io_user_data->num_newlines == 2) {
				char buf[1];
				/* terminate the child */
				if (write (io_user_data->child_stdin, buf, sizeof (buf)) == -1)
					goto out;
			}
		} else if (len > 0) {
			/* remove terminating newline */
			str[len - 1] = '\0';
			io_user_data->lines = g_slist_append (io_user_data->lines, str);
		}
	}

out:
	return TRUE;
}

static char *
find_auth_dialog_binary (const char *service, const char *name)
{
	GDir * dir;
	char * prog = NULL;
	const char *f;

	dir = g_dir_open (VPN_NAME_FILES_DIR, 0, NULL);
	if (!dir)
		goto out;

	while (prog == NULL && (f = g_dir_read_name (dir)) != NULL) {
		char *path;
		GKeyFile *keyfile;

		if (!g_str_has_suffix (f, ".name"))
			continue;

		path = g_strdup_printf ("%s/%s", VPN_NAME_FILES_DIR, f);

		keyfile = g_key_file_new ();
		if (g_key_file_load_from_file (keyfile, path, 0, NULL)) {
			char *thisservice;

			if ((thisservice = g_key_file_get_string (keyfile, 
								  "VPN Connection", 
								  "service", NULL)) != NULL &&
			    strcmp (thisservice, service) == 0) {

				prog = g_key_file_get_string (keyfile, "GNOME", "auth-dialog", NULL);
			}
			g_free (thisservice);
		}
		g_key_file_free (keyfile);
		g_free (path);
	}
	g_dir_close (dir);

out:
	if (prog == NULL) {
		/* could find auth-dialog */
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (NULL,
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Cannot start VPN connection '%s'"),
						 name);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
  _("Could not find the authentication dialog for VPN connection type '%s'. Contact your system administrator."),
							  service);
		gtk_window_present (GTK_WINDOW (dialog));
		g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
	} else {
		char *basename;

		/* Remove any path component, then reconstruct path to the auth
		 * dialog in LIBEXECDIR.
		 */
		basename = g_path_get_basename (prog);
		g_free (prog);
		prog = g_strdup_printf ("%s/%s", LIBEXECDIR, basename);
		g_free (basename);
	}

	return prog;
}

static void
destroy_gvalue (gpointer data)
{
	GValue *value = (GValue *) data;

	g_value_unset (value);
	g_slice_free (GValue, value);
}

gboolean
nma_vpn_request_password (NMExportedConnection *exported,
                          gboolean retry,
                          DBusGMethodInvocation *context)
{
	const char *argv[] = { NULL /*"/usr/libexec/nm-vpnc-auth-dialog"*/, 
	                       "-u", NULL /*"2a5d52b5-95b4-4431-b96e-3dd46128f9a7"*/, 
	                       "-n", NULL /*"davidznet42"*/,
	                       "-s", NULL /*"org.freedesktop.vpnc"*/, 
	                       "-r",
	                       NULL
	                     };
	int         child_stdin;
	int         child_stdout;
	GPid        child_pid;
	int         child_status;
	GIOChannel *child_stdout_channel;
	guint       child_stdout_channel_eventid;
	char       *auth_dialog_binary = NULL;
	IOUserData io_user_data;
	NMSettingConnection *s_con;
	NMSettingVPN *s_vpn;
	gboolean success = FALSE;
	GError *error = NULL;
	NMConnection *connection;

	g_return_val_if_fail (NM_IS_EXPORTED_CONNECTION (exported), FALSE);

	connection = nm_exported_connection_get_connection (exported);
	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	g_return_val_if_fail (s_con != NULL, FALSE);
	g_return_val_if_fail (s_con->id != NULL, FALSE);
	g_return_val_if_fail (s_con->type != NULL, FALSE);
	g_return_val_if_fail (strcmp (s_con->type, "vpn") == 0, FALSE);

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	g_return_val_if_fail (s_vpn != NULL, FALSE);
	g_return_val_if_fail (s_vpn->service_type != NULL, FALSE);

	/* find the auth-dialog binary */
	auth_dialog_binary = find_auth_dialog_binary (s_vpn->service_type, s_con->id);
	if (!auth_dialog_binary) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't find VPN auth dialog  helper program '%s'.",
		             __FILE__, __LINE__, __func__, s_vpn->service_type);
		goto out;
	}

	/* Fix up parameters with what we got */
	argv[0] = auth_dialog_binary;
	argv[2] = s_con->uuid;
	argv[4] = s_con->id;
	argv[6] = s_vpn->service_type;
	if (!retry)
		argv[7] = NULL;

	child_status = -1;

	if (!g_spawn_async_with_pipes (NULL,                       /* working_directory */
				       (gchar **) argv,            /* argv */
				       NULL,                       /* envp */
				       G_SPAWN_DO_NOT_REAP_CHILD,  /* flags */
				       NULL,                       /* child_setup */
				       NULL,                       /* user_data */
				       &child_pid,                 /* child_pid */
				       &child_stdin,               /* standard_input */
				       &child_stdout,              /* standard_output */
				       NULL,                       /* standard_error */
				       NULL)) {                    /* error */
		/* could not spawn */
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (NULL,
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Cannot start VPN connection '%s'"),
						 s_con->id);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
  _("There was a problem launching the authentication dialog for VPN connection type '%s'. Contact your system administrator."),
							  s_vpn->service_type);
		gtk_window_present (GTK_WINDOW (dialog));
		g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): couldn't run VPN auth dialog.",
		             __FILE__, __LINE__, __func__);
		goto out;
	}

	/* catch when child is reaped */
	g_child_watch_add (child_pid, child_finished_cb, (gpointer) &child_status);

	io_user_data.lines = NULL;
	io_user_data.child_stdin = child_stdin;
	io_user_data.num_newlines = 0;

	/* listen to what child has to say */
	child_stdout_channel = g_io_channel_unix_new (child_stdout);
	child_stdout_channel_eventid = g_io_add_watch (child_stdout_channel, G_IO_IN, child_stdout_data_cb, 
						       &io_user_data);
	g_io_channel_set_encoding (child_stdout_channel, NULL, NULL);

	/* recurse mainloop here until the child is finished (child_status is set in child_finished_cb) */
	while (child_status == -1)
		g_main_context_iteration (NULL, TRUE);

	g_spawn_close_pid (child_pid);
	g_source_remove (child_stdout_channel_eventid);
	g_io_channel_unref (child_stdout_channel);

	if (child_status == 0) {
		GSList *iter;
		GHashTable *settings;
		GHashTable *secrets;

		settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
		                                  (GDestroyNotify) g_hash_table_destroy);

		/* Send the secrets back to NM */
		secrets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, destroy_gvalue);

		for (iter = io_user_data.lines; iter; iter = iter->next) {
			GValue *val;

			if (!iter->next)
				break;

			val = g_slice_new0 (GValue);
			g_value_init (val, G_TYPE_STRING);
			g_value_set_string (val, iter->next->data);

			g_hash_table_insert (secrets, g_strdup (iter->data), val);
			iter = iter->next;
		}
		g_hash_table_insert (settings, g_strdup (NM_SETTING_VPN_SETTING_NAME), secrets);

		dbus_g_method_return (context, settings);
		g_hash_table_destroy (settings);
		success = TRUE;
	} else {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d (%s): canceled", __FILE__, __LINE__, __func__);
	}

	g_slist_foreach (io_user_data.lines, (GFunc) g_free, NULL);
	g_slist_free (io_user_data.lines);

 out:
	g_free (auth_dialog_binary);

	if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	return success;
}
