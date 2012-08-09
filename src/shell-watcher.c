/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * Copyright (C) 2012 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>

#include "shell-watcher.h"

#if GLIB_CHECK_VERSION(2,26,0)

G_DEFINE_TYPE (NMShellWatcher, nm_shell_watcher, G_TYPE_OBJECT)

struct NMShellWatcherPrivate {
	GDBusProxy *shell_proxy;
	guint signal_id;

	guint retry_timeout;
	guint retries;

	guint shell_version;
};

enum {
	PROP_0,
	PROP_SHELL_VERSION,
	LAST_PROP
};

static void create_gnome_shell_proxy (NMShellWatcher *watcher);

static gboolean
retry_create_shell_proxy (gpointer user_data)
{
	NMShellWatcher *watcher = user_data;
	NMShellWatcherPrivate *priv = watcher->priv;

	priv->retry_timeout = 0;
	create_gnome_shell_proxy (watcher);
	return FALSE;
}

static void
try_update_version (NMShellWatcher *watcher)
{
	NMShellWatcherPrivate *priv = watcher->priv;
	GVariant *v;
	char *version, *p;

	v = g_dbus_proxy_get_cached_property (priv->shell_proxy, "ShellVersion");
	if (!v) {
		/* The shell has claimed the name, but not yet registered its interfaces...
		 * (https://bugzilla.gnome.org/show_bug.cgi?id=673182). There's no way
		 * to make GDBusProxy re-read the properties at this point, so we
		 * have to destroy this proxy and try again.
		 */
		if (priv->signal_id) {
			g_signal_handler_disconnect (priv->shell_proxy, priv->signal_id);
			priv->signal_id = 0;
		}
		g_object_unref (priv->shell_proxy);
		priv->shell_proxy = NULL;

		priv->retry_timeout = g_timeout_add_seconds (2, retry_create_shell_proxy, watcher); 
		return;
	}

	g_warn_if_fail (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING));
	version = g_variant_dup_string (v, NULL);
	if (version) {
		guint major, minor;

		major = strtoul (version, &p, 10);
		if (*p == '.')
			minor = strtoul (p + 1, NULL, 10);
		else
			minor = 0;

		g_warn_if_fail (major < 256);
		g_warn_if_fail (minor < 256);

		priv->shell_version = (major << 8) | minor;
		g_object_notify (G_OBJECT (watcher), "shell-version");
	}

	g_variant_unref (v);
}

static void
name_owner_changed_cb (GDBusProxy *proxy, GParamSpec *pspec, gpointer user_data)
{
	NMShellWatcher *watcher = user_data;
	NMShellWatcherPrivate *priv = watcher->priv;
	char *owner;

	owner = g_dbus_proxy_get_name_owner (proxy);
	if (owner) {
		try_update_version (watcher);
		g_free (owner);
	} else if (priv->shell_version) {
		priv->shell_version = 0;
		g_object_notify (G_OBJECT (watcher), "shell-version");
	}
}

static void
got_shell_proxy (GObject *source, GAsyncResult *result, gpointer user_data)
{
	NMShellWatcher *watcher = user_data;
	NMShellWatcherPrivate *priv = watcher->priv;
	GError *error = NULL;

	priv->shell_proxy = g_dbus_proxy_new_for_bus_finish (result, &error);
	if (!priv->shell_proxy) {
		g_warning ("Could not create GDBusProxy for org.gnome.Shell: %s", error->message);
		g_error_free (error);
		return;
	}

	priv->signal_id = g_signal_connect (priv->shell_proxy,
	                                    "notify::g-name-owner",
	                                    G_CALLBACK (name_owner_changed_cb),
	                                    watcher);

	name_owner_changed_cb (priv->shell_proxy, NULL, watcher);
	g_object_unref (watcher);
}

static void
create_gnome_shell_proxy (NMShellWatcher *watcher)
{
	NMShellWatcherPrivate *priv = watcher->priv;

	if (priv->retries++ == 5) {
		g_warning ("Could not find ShellVersion property on org.gnome.Shell after 5 tries");
		return;
	}

	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
	                          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
	                          NULL,
	                          "org.gnome.Shell",
	                          "/org/gnome/Shell",
	                          "org.gnome.Shell",
	                          NULL,
	                          got_shell_proxy,
	                          g_object_ref (watcher));
}

static void
nm_shell_watcher_init (NMShellWatcher *watcher)
{
	watcher->priv = G_TYPE_INSTANCE_GET_PRIVATE (watcher, NM_TYPE_SHELL_WATCHER,
	                                             NMShellWatcherPrivate);
	create_gnome_shell_proxy (watcher);
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMShellWatcher *watcher = NM_SHELL_WATCHER (object);

	switch (prop_id) {
	case PROP_SHELL_VERSION:
		g_value_set_uint (value, watcher->priv->shell_version);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
finalize (GObject *object)
{
	NMShellWatcher *watcher = NM_SHELL_WATCHER (object);
	NMShellWatcherPrivate *priv = watcher->priv;

	if (priv->retry_timeout)
		g_source_remove (priv->retry_timeout);
	if (priv->signal_id)
		g_signal_handler_disconnect (priv->shell_proxy, priv->signal_id);
	if (priv->shell_proxy)
		g_object_unref (priv->shell_proxy);

	G_OBJECT_CLASS (nm_shell_watcher_parent_class)->finalize (object);
}

static void
nm_shell_watcher_class_init (NMShellWatcherClass *klass)
{
	GObjectClass *oclass = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMShellWatcherPrivate));

	oclass->get_property = get_property;
	oclass->finalize = finalize;

	g_object_class_install_property (oclass, PROP_SHELL_VERSION,
	                                 g_param_spec_uint ("shell-version",
	                                                    "Shell version",
	                                                    "Running GNOME Shell version, eg, 0x0304",
	                                                    0, 0xFFFF, 0,
	                                                    G_PARAM_READABLE |
	                                                    G_PARAM_STATIC_STRINGS));
}

NMShellWatcher *
nm_shell_watcher_new (void)
{
	return g_object_new (NM_TYPE_SHELL_WATCHER, NULL);
}

gboolean
nm_shell_watcher_version_at_least (NMShellWatcher *watcher, guint major, guint minor)
{
	guint version = (major << 8) | minor;

	return watcher->priv->shell_version >= version;
}

#endif /* GLIB_CHECK_VERSION(2,26,0) */
