/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * Copyright 2004 - 2014 Red Hat, Inc.
 */

#include "nm-default.h"

#include "applet-vpn-request.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

typedef struct {
	char *uuid;
	char *id;
	char *service_type;

	guint watch_id;
	GPid pid;

	GSList *lines;
	int child_stdin;
	int child_stdout;
	int num_newlines;
	GIOChannel *channel;
	guint channel_eventid;
} RequestData;

/*****************************************************************************/

static void request_data_free (RequestData *req_data);

/*****************************************************************************/

typedef struct {
	SecretsRequest req;
	RequestData *req_data;
} VpnSecretsInfo;

static void
child_finished_cb (GPid pid, gint status, gpointer user_data)
{
	SecretsRequest *req = user_data;
	VpnSecretsInfo *info = (VpnSecretsInfo *) req;
	RequestData *req_data = info->req_data;
	gs_free_error GError *error = NULL;
	gs_unref_variant GVariant *settings = NULL;
	GVariantBuilder settings_builder, vpn_builder, secrets_builder;

	if (status == 0) {
		GSList *iter;

		g_variant_builder_init (&settings_builder, NM_VARIANT_TYPE_CONNECTION);
		g_variant_builder_init (&vpn_builder, NM_VARIANT_TYPE_SETTING);
		g_variant_builder_init (&secrets_builder, G_VARIANT_TYPE ("a{ss}"));

		/* The length of 'lines' must be divisible by 2 since it must contain
		 * key:secret pairs with the key on one line and the associated secret
		 * on the next line.
		 */
		for (iter = req_data->lines; iter; iter = g_slist_next (iter)) {
			if (!iter->next)
				break;
			g_variant_builder_add (&secrets_builder, "{ss}", iter->data, iter->next->data);
			iter = iter->next;
		}

		g_variant_builder_add (&vpn_builder, "{sv}",
		                       NM_SETTING_VPN_SECRETS,
		                       g_variant_builder_end (&secrets_builder));
		g_variant_builder_add (&settings_builder, "{sa{sv}}",
		                       NM_SETTING_VPN_SETTING_NAME,
		                       &vpn_builder);
		settings = g_variant_builder_end (&settings_builder);
	} else {
		error = g_error_new (NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_USER_CANCELED,
		                     "%s.%d (%s): canceled", __FILE__, __LINE__, __func__);
	}

	/* Complete the secrets request */
	applet_secrets_request_complete (req, settings, error);
	applet_secrets_request_free (req);
}

static gboolean
child_stdout_data_cb (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
	VpnSecretsInfo *info = user_data;
	RequestData *req_data = info->req_data;
	char *str;
	int len;

	if (!(condition & G_IO_IN))
		return TRUE;

	if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
		len = strlen (str);
		if (len == 1 && str[0] == '\n') {
			/* on second line with a newline newline */
			if (++req_data->num_newlines == 2) {
				const char *buf = "QUIT\n\n";

				/* terminate the child */
				if (write (req_data->child_stdin, buf, strlen (buf)) == -1)
					return TRUE;
			}
		} else if (len > 0) {
			/* remove terminating newline */
			str[len - 1] = '\0';
			req_data->lines = g_slist_append (req_data->lines, str);
		}
	}
	return TRUE;
}

static char *
find_auth_dialog_binary (const char *service,
                         gboolean *out_hints_supported,
                         GError **error)
{
	const char *auth_dialog;
	gs_unref_object NMVpnPluginInfo *plugin = NULL;

	plugin = nm_vpn_plugin_info_new_search_file (NULL, service);

	auth_dialog = plugin ? nm_vpn_plugin_info_get_auth_dialog (plugin) : NULL;
	if (!auth_dialog) {
		g_set_error (error,
		             NM_SECRET_AGENT_ERROR,
		             NM_SECRET_AGENT_ERROR_FAILED,
		             "Could not find the authentication dialog for VPN connection type '%s'",
		             service);
		return NULL;
	}

	*out_hints_supported = nm_vpn_plugin_info_supports_hints (plugin);
	return g_strdup (auth_dialog);
}

static void
free_vpn_secrets_info (SecretsRequest *req)
{
	request_data_free (((VpnSecretsInfo *) req)->req_data);
}

size_t
applet_vpn_request_get_secrets_size (void)
{
	return sizeof (VpnSecretsInfo);
}

typedef struct {
	int fd;
	gboolean secret;
	GError **error;
} WriteItemInfo;

static const char *data_key_tag = "DATA_KEY=";
static const char *data_val_tag = "DATA_VAL=";
static const char *secret_key_tag = "SECRET_KEY=";
static const char *secret_val_tag = "SECRET_VAL=";

static gboolean
write_item (int fd, const char *item, GError **error)
{
	size_t item_len = strlen (item);

	errno = 0;
	if (write (fd, item, item_len) != item_len) {
		g_set_error (error,
			         NM_SECRET_AGENT_ERROR,
			         NM_SECRET_AGENT_ERROR_FAILED,
			         "Failed to write connection to VPN UI: errno %d", errno);
		return FALSE;
	}
	return TRUE;
}

static void
write_one_key_val (const char *key, const char *value, gpointer user_data)
{
	WriteItemInfo *info = user_data;
	const char *tag;

	if (info->error && *(info->error))
		return;

	/* Write the key name */
	tag = info->secret ? secret_key_tag : data_key_tag;
	if (!write_item (info->fd, tag, info->error))
		return;
	if (!write_item (info->fd, key, info->error))
		return;
	if (!write_item (info->fd, "\n", info->error))
		return;

	/* Write the key value */
	tag = info->secret ? secret_val_tag : data_val_tag;
	if (!write_item (info->fd, tag, info->error))
		return;
	if (!write_item (info->fd, value ? value : "", info->error))
		return;
	if (!write_item (info->fd, "\n\n", info->error))
		return;
}

static gboolean
write_connection_to_child (int fd, NMConnection *connection, GError **error)
{
	NMSettingVpn *s_vpn;
	WriteItemInfo info = { .fd = fd, .secret = FALSE, .error = error };

	s_vpn = nm_connection_get_setting_vpn (connection);
	if (!s_vpn) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_FAILED,
		                     "Connection had no VPN setting");
		return FALSE;
	}

	nm_setting_vpn_foreach_data_item (s_vpn, write_one_key_val, &info);
	if (error && *error)
		return FALSE;

	info.secret = TRUE;
	nm_setting_vpn_foreach_secret (s_vpn, write_one_key_val, &info);
	if (error && *error)
		return FALSE;

	if (!write_item (fd, "DONE\n\n", error))
		return FALSE;

	return TRUE;
}

static void
vpn_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	/* We are in the child process at this point */
	pid_t pid = getpid ();
	setpgid (pid, pid);
}

gboolean
applet_vpn_request_get_secrets (SecretsRequest *req, GError **error)
{
	VpnSecretsInfo *info = (VpnSecretsInfo *) req;
	RequestData *req_data;
	NMSettingConnection *s_con;
	NMSettingVpn *s_vpn;
	const char *connection_type;
	const char *service_type;
	gs_free char *bin_path = NULL;
	gs_free const char **argv = NULL;
	guint i = 0, u;
	gsize hints_len;
	gboolean supports_hints = FALSE;

	applet_secrets_request_set_free_func (req, free_vpn_secrets_info);

	s_con = nm_connection_get_setting_connection (req->connection);
	g_return_val_if_fail (s_con, FALSE);

	connection_type = nm_setting_connection_get_connection_type (s_con);
	g_return_val_if_fail (connection_type, FALSE);
	g_return_val_if_fail (strcmp (connection_type, NM_SETTING_VPN_SETTING_NAME) == 0, FALSE);

	s_vpn = nm_connection_get_setting_vpn (req->connection);
	g_return_val_if_fail (s_vpn, FALSE);

	service_type = nm_setting_vpn_get_service_type (s_vpn);
	g_return_val_if_fail (service_type, FALSE);

	bin_path = find_auth_dialog_binary (service_type, &supports_hints, error);
	if (!bin_path)
		return FALSE;

	info->req_data = g_slice_new0 (RequestData);
	if (!info->req_data) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_FAILED,
		                     "Could not create VPN secrets request object");
		return FALSE;
	}
	req_data = info->req_data;

	hints_len = NM_PTRARRAY_LEN (req->hints);
	argv = g_new (const char *, 10 + (2 * hints_len));
	argv[i++] = bin_path;
	argv[i++] = "-u";
	argv[i++] = nm_setting_connection_get_uuid (s_con);
	argv[i++] = "-n";
	argv[i++] = nm_setting_connection_get_id (s_con);
	argv[i++] = "-s";
	argv[i++] = service_type;
	if (req->flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION)
		argv[i++] = "-i";
	if (req->flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_REQUEST_NEW)
		argv[i++] = "-r";
	for (u = 0; supports_hints && (u < hints_len); u++) {
		argv[i++] = "-t";
		argv[i++] = req->hints[u];
	}
	nm_assert (i <= 10 + (2 * hints_len));
	argv[i++] = NULL;

	if (!g_spawn_async_with_pipes (NULL,                       /* working_directory */
	                               (char **) argv,             /* argv */
	                               NULL,                       /* envp */
	                               G_SPAWN_DO_NOT_REAP_CHILD,  /* flags */
	                               vpn_child_setup,            /* child_setup */
	                               NULL,                       /* user_data */
	                               &req_data->pid,             /* child_pid */
	                               &req_data->child_stdin,     /* standard_input */
	                               &req_data->child_stdout,    /* standard_output */
	                               NULL,                       /* standard_error */
	                               error))                     /* error */
		return FALSE;

	/* catch when child is reaped */
	req_data->watch_id = g_child_watch_add (req_data->pid, child_finished_cb, info);

	/* listen to what child has to say */
	req_data->channel = g_io_channel_unix_new (req_data->child_stdout);
	req_data->channel_eventid = g_io_add_watch (req_data->channel, G_IO_IN, child_stdout_data_cb, info);
	g_io_channel_set_encoding (req_data->channel, NULL, NULL);

	/* Dump parts of the connection to the child */
	return write_connection_to_child (req_data->child_stdin, req->connection, error);
}

static gboolean
ensure_killed (gpointer data)
{
	pid_t pid = GPOINTER_TO_INT (data);

	if (kill (pid, 0) == 0)
		kill (pid, SIGKILL);
	/* ensure the child is reaped */
	waitpid (pid, NULL, 0);
	return FALSE;
}

static void
request_data_free (RequestData *req_data)
{
	if (!req_data)
		return;

	g_free (req_data->uuid);
	g_free (req_data->id);
	g_free (req_data->service_type);

	nm_clear_g_source (&req_data->watch_id);

	nm_clear_g_source (&req_data->channel_eventid);
	if (req_data->channel)
		g_io_channel_unref (req_data->channel);

	if (req_data->pid) {
		g_spawn_close_pid (req_data->pid);
		if (kill (req_data->pid, SIGTERM) == 0)
			g_timeout_add_seconds (2, ensure_killed, GINT_TO_POINTER (req_data->pid));
		else {
			kill (req_data->pid, SIGKILL);
			/* ensure the child is reaped */
			waitpid (req_data->pid, NULL, 0);
		}
	}

	g_slist_foreach (req_data->lines, (GFunc) g_free, NULL);
	g_slist_free (req_data->lines);

	g_slice_free (RequestData, req_data);
}
