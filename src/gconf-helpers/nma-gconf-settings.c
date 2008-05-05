/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <string.h>
#include <stdio.h>
#include "nma-gconf-settings.h"
#include "gconf-helpers.h"
#include "nma-marshal.h"
#include "nm-utils.h"

G_DEFINE_TYPE (NMAGConfSettings, nma_gconf_settings, NM_TYPE_SETTINGS)

#define NMA_GCONF_SETTINGS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NMA_TYPE_GCONF_SETTINGS, NMAGConfSettingsPrivate))

typedef struct {
	GConfClient *client;
	guint conf_notify_id;
	GSList *connections;
	guint read_connections_id;

	gboolean disposed;
} NMAGConfSettingsPrivate;

enum {
	NEW_SECRETS_REQUESTED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


NMAGConfSettings *
nma_gconf_settings_new (void)
{
	return (NMAGConfSettings *) g_object_new (NMA_TYPE_GCONF_SETTINGS, NULL);
}

static void
connection_new_secrets_requested_cb (NMAGConfConnection *connection,
                                     const char *setting_name,
                                     const char **hints,
                                     gboolean ask_user,
                                     DBusGMethodInvocation *context,
                                     gpointer user_data)
{
	NMAGConfSettings *self = NMA_GCONF_SETTINGS (user_data);

	/* Re-emit the signal to listeners so they don't have to know about
	 * every single connection
	 */
	g_signal_emit (self,
	               signals[NEW_SECRETS_REQUESTED],
	               0,
	               connection,
	               setting_name,
	               hints,
	               ask_user,
	               context);
}

static void
add_connection_real (NMAGConfSettings *self, NMAGConfConnection *connection)
{
	NMAGConfSettingsPrivate *priv = NMA_GCONF_SETTINGS_GET_PRIVATE (self);

	if (connection) {
		priv->connections = g_slist_prepend (priv->connections, connection);
		g_signal_connect (connection, "new-secrets-requested",
					   G_CALLBACK (connection_new_secrets_requested_cb),
					   self);

		nm_settings_signal_new_connection (NM_SETTINGS (self),
									NM_EXPORTED_CONNECTION (connection));
	}
}

NMAGConfConnection *
nma_gconf_settings_add_connection (NMAGConfSettings *self, NMConnection *connection)
{
	NMAGConfSettingsPrivate *priv;
	NMAGConfConnection *exported;
	guint32 i = 0;
	char *path = NULL;

	g_return_val_if_fail (NMA_IS_GCONF_SETTINGS (self), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	priv = NMA_GCONF_SETTINGS_GET_PRIVATE (self);

	/* Find free GConf directory */
	while (i++ < G_MAXUINT32) {
		char buf[255];

		snprintf (&buf[0], 255, GCONF_PATH_CONNECTIONS"/%d", i);
		if (!gconf_client_dir_exists (priv->client, buf, NULL)) {
			path = g_strdup_printf (buf);
			break;
		}
	}

	if (path == NULL) {
		nm_warning ("Couldn't find free GConf directory for new connection.");
		return NULL;
	}

	exported = nma_gconf_connection_new_from_connection (priv->client, path, connection);
	g_free (path);
	if (!exported)
		return NULL;

	add_connection_real (self, exported);

	/* Must save connection to GConf _after_ adding it to the connections
	 * list to avoid races with GConf notifications.
	 */
	nma_gconf_connection_save (exported);

	return exported;
}

NMAGConfConnection *
nma_gconf_settings_get_by_path (NMAGConfSettings *self, const char *path)
{
	NMAGConfSettingsPrivate *priv;
	GSList *iter;

	g_return_val_if_fail (NMA_IS_GCONF_SETTINGS (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	priv = NMA_GCONF_SETTINGS_GET_PRIVATE (self);
	for (iter = priv->connections; iter; iter = iter->next) {
		NMAGConfConnection *connection = NMA_GCONF_CONNECTION (iter->data);
		const char *gconf_path;

		gconf_path = nma_gconf_connection_get_path (connection);
		if (gconf_path && !strcmp (gconf_path, path))
			return connection;
	}

	return NULL;
}

NMAGConfConnection *
nma_gconf_settings_get_by_dbus_path (NMAGConfSettings *self,
							  const char *path)
{
	NMAGConfSettingsPrivate *priv;
	GSList *iter;

	g_return_val_if_fail (NMA_IS_GCONF_SETTINGS (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	priv = NMA_GCONF_SETTINGS_GET_PRIVATE (self);
	for (iter = priv->connections; iter; iter = iter->next) {
		NMAGConfConnection *connection = NMA_GCONF_CONNECTION (iter->data);
		NMConnection *wrapped;
		const char *sc_path;

		wrapped = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (connection));
		sc_path = nm_connection_get_path (wrapped);

		if (sc_path && !strcmp (sc_path, path))
			return connection;
	}

	return NULL;
}

NMAGConfConnection *
nma_gconf_settings_get_by_connection (NMAGConfSettings *self,
							   NMConnection *connection)
{
	NMAGConfSettingsPrivate *priv;
	GSList *iter;

	g_return_val_if_fail (NMA_IS_GCONF_SETTINGS (self), NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	priv = NMA_GCONF_SETTINGS_GET_PRIVATE (self);

	for (iter = priv->connections; iter; iter = iter->next) {
		NMConnection *wrapped;

		wrapped = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (iter->data));
		if (connection == wrapped)
			return NMA_GCONF_CONNECTION (iter->data);
	}

	return NULL;
}

static void
read_connections (NMAGConfSettings *settings)
{
	NMAGConfSettingsPrivate *priv = NMA_GCONF_SETTINGS_GET_PRIVATE (settings);
	GSList *dir_list;
	GSList *iter;

	dir_list = nm_gconf_get_all_connections (priv->client);
	if (!dir_list) {
		g_warning ("No connections defined");
		return;
	}

	for (iter = dir_list; iter; iter = iter->next) {
		char *dir = (char *) iter->data;

		add_connection_real (settings, nma_gconf_connection_new (priv->client, dir));
		g_free (dir);
	}

	g_slist_free (dir_list);
	priv->connections = g_slist_reverse (priv->connections);
}

static gboolean
read_connections_cb (gpointer data)
{
	NMA_GCONF_SETTINGS_GET_PRIVATE (data)->read_connections_id = 0;
	read_connections (NMA_GCONF_SETTINGS (data));

	return FALSE;
}

static GSList *
list_connections (NMSettings *settings)
{
	NMAGConfSettingsPrivate *priv = NMA_GCONF_SETTINGS_GET_PRIVATE (settings);

	if (priv->read_connections_id) {
		g_source_remove (priv->read_connections_id);
		priv->read_connections_id = 0;

		read_connections (NMA_GCONF_SETTINGS (settings));
	}

	return g_slist_copy (priv->connections);
}

static void
connections_changed_cb (GConfClient *conf_client,
                        guint cnxn_id,
                        GConfEntry *entry,
                        gpointer user_data)
{
	NMAGConfSettings *self = NMA_GCONF_SETTINGS (user_data);
	NMAGConfSettingsPrivate *priv = NMA_GCONF_SETTINGS_GET_PRIVATE (self);
	char **dirs = NULL;
	guint len;
	char *path = NULL;
	NMAGConfConnection *connection;
	gboolean valid = FALSE;

	dirs = g_strsplit (gconf_entry_get_key (entry), "/", -1);
	len = g_strv_length (dirs);
	if (len < 5)
		goto out;

	if (   strcmp (dirs[0], "")
	    || strcmp (dirs[1], "system")
	    || strcmp (dirs[2], "networking")
	    || strcmp (dirs[3], "connections"))
		goto out;

	path = g_strconcat ("/", dirs[1], "/", dirs[2], "/", dirs[3], "/", dirs[4], NULL);
	connection = nma_gconf_settings_get_by_path (self, path);
	if (!connection) {
		/* New connection */
		connection = nma_gconf_connection_new (priv->client, path);
		if (connection) {
			g_signal_connect (connection, "new-secrets-requested",
						   G_CALLBACK (connection_new_secrets_requested_cb),
						   self);
			priv->connections = g_slist_append (priv->connections, connection);
			nm_settings_signal_new_connection (NM_SETTINGS (self),
										NM_EXPORTED_CONNECTION (connection));
		}
	} else {
		/* Updated or removed connection */
		valid = nma_gconf_connection_changed (connection, entry);
		if (!valid)
			priv->connections = g_slist_remove (priv->connections, connection);
	}

out:
	g_free (path);
	g_strfreev (dirs);
}

/* GObject */

static void
nma_gconf_settings_init (NMAGConfSettings *settings)
{
	NMAGConfSettingsPrivate *priv = NMA_GCONF_SETTINGS_GET_PRIVATE (settings);

	priv->client = gconf_client_get_default ();

	gconf_client_add_dir (priv->client,
	                      GCONF_PATH_CONNECTIONS,
	                      GCONF_CLIENT_PRELOAD_NONE,
	                      NULL);

	priv->conf_notify_id = gconf_client_notify_add (priv->client,
										   GCONF_PATH_CONNECTIONS,
										   (GConfClientNotifyFunc) connections_changed_cb,
										   settings,
										   NULL, NULL);
}

static GObject *
constructor (GType type,
		   guint n_construct_params,
		   GObjectConstructParam *construct_params)
{
	GObject *object;
	NMAGConfSettingsPrivate *priv;

	object = G_OBJECT_CLASS (nma_gconf_settings_parent_class)->constructor (type, n_construct_params, construct_params);

	if (!object)
		return NULL;

	priv = NMA_GCONF_SETTINGS_GET_PRIVATE (object);

	priv->read_connections_id = g_idle_add (read_connections_cb, object);

	return object;
}

static void
dispose (GObject *object)
{
	NMAGConfSettingsPrivate *priv = NMA_GCONF_SETTINGS_GET_PRIVATE (object);

	if (priv->disposed)
		return;

	priv->disposed = TRUE;

	if (priv->read_connections_id) {
		g_source_remove (priv->read_connections_id);
		priv->read_connections_id = 0;
	}

	gconf_client_notify_remove (priv->client, priv->conf_notify_id);
	gconf_client_remove_dir (priv->client, GCONF_PATH_CONNECTIONS, NULL);

	g_slist_foreach (priv->connections, (GFunc) g_object_unref, NULL);
	g_slist_free (priv->connections);

	g_object_unref (priv->client);

	G_OBJECT_CLASS (nma_gconf_settings_parent_class)->dispose (object);
}

static void
nma_gconf_settings_class_init (NMAGConfSettingsClass *gconf_settings_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (gconf_settings_class);
	NMSettingsClass *settings_class = NM_SETTINGS_CLASS (gconf_settings_class);

	g_type_class_add_private (gconf_settings_class, sizeof (NMAGConfSettingsPrivate));

	/* Virtual methods */
	object_class->constructor = constructor;
	object_class->dispose = dispose;

	settings_class->list_connections = list_connections;

	/* Signals */
	signals[NEW_SECRETS_REQUESTED] =
		g_signal_new ("new-secrets-requested",
				    G_OBJECT_CLASS_TYPE (object_class),
				    G_SIGNAL_RUN_FIRST,
				    G_STRUCT_OFFSET (NMAGConfSettingsClass, new_secrets_requested),
				    NULL, NULL,
				    nma_marshal_VOID__OBJECT_STRING_POINTER_BOOLEAN_POINTER,
				    G_TYPE_NONE, 5,
				    G_TYPE_OBJECT, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN, G_TYPE_POINTER);
}
