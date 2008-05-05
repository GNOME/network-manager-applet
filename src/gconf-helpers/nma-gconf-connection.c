/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <string.h>
#include <nm-setting-connection.h>
#include <nm-setting-vpn.h>
#include "nma-gconf-connection.h"
#include "gconf-helpers.h"
#include "nm-utils.h"
#include "utils.h"
#include "nma-marshal.h"

G_DEFINE_TYPE (NMAGConfConnection, nma_gconf_connection, NM_TYPE_EXPORTED_CONNECTION)

#define NMA_GCONF_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NMA_TYPE_GCONF_CONNECTION, NMAGConfConnectionPrivate))

typedef struct {
	GConfClient *client;
	char *dir;
	char *id;

	gboolean disposed;
} NMAGConfConnectionPrivate;

enum {
	PROP_0,
	PROP_CLIENT,
	PROP_DIR,

	LAST_PROP
};

enum {
	NEW_SECRETS_REQUESTED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

NMAGConfConnection *
nma_gconf_connection_new (GConfClient *client, const char *conf_dir)
{
	NMConnection *connection;
	NMAGConfConnection *gconf_connection;

	g_return_val_if_fail (GCONF_IS_CLIENT (client), NULL);
	g_return_val_if_fail (conf_dir != NULL, NULL);

	/* retrieve GConf data */
	connection = nm_gconf_read_connection (client, conf_dir);
	if (connection) {
		gconf_connection = nma_gconf_connection_new_from_connection (client, conf_dir, connection);
		g_object_unref (connection);
	} else {
		nm_warning ("No connection read from GConf at %s.", conf_dir);
		gconf_connection = NULL;
	}
	
	return gconf_connection;
}

NMAGConfConnection *
nma_gconf_connection_new_from_connection (GConfClient *client,
								  const char *conf_dir,
								  NMConnection *connection)
{
	g_return_val_if_fail (GCONF_IS_CLIENT (client), NULL);
	g_return_val_if_fail (conf_dir != NULL, NULL);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	return (NMAGConfConnection *) g_object_new (NMA_TYPE_GCONF_CONNECTION,
									    NMA_GCONF_CONNECTION_CLIENT, client,
									    NMA_GCONF_CONNECTION_DIR, conf_dir,
									    NM_EXPORTED_CONNECTION_CONNECTION, connection,
									    NULL);
}

const char *
nma_gconf_connection_get_path (NMAGConfConnection *self)
{
	g_return_val_if_fail (NMA_IS_GCONF_CONNECTION (self), NULL);

	return NMA_GCONF_CONNECTION_GET_PRIVATE (self)->dir;
}

/* FIXME: Remove and replace the callers with nm_exported_connection_update() */
void
nma_gconf_connection_save (NMAGConfConnection *self)
{
	NMAGConfConnectionPrivate *priv;
	NMConnection *connection;

	g_return_if_fail (NMA_IS_GCONF_CONNECTION (self));

	priv = NMA_GCONF_CONNECTION_GET_PRIVATE (self);

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (self));
	nm_gconf_write_connection (connection,
	                           priv->client,
	                           priv->dir,
	                           priv->id);
	gconf_client_notify (priv->client, priv->dir);
	gconf_client_suggest_sync (priv->client, NULL);
}

static void
fill_vpn_user_name (NMConnection *connection)
{
	const char *user_name;
	NMSettingVPN *s_vpn;

	s_vpn = NM_SETTING_VPN (nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN));
	if (!s_vpn)
		return;

	g_free (s_vpn->user_name);
	user_name = g_get_user_name ();
	g_assert (g_utf8_validate (user_name, -1, NULL));
	s_vpn->user_name = g_strdup (user_name);
}

gboolean
nma_gconf_connection_changed (NMAGConfConnection *self,
						GConfEntry *entry)
{
	NMAGConfConnectionPrivate *priv;
	GHashTable *settings;
	NMConnection *wrapped_connection;
	NMConnection *gconf_connection;
	GHashTable *new_settings;

	g_return_val_if_fail (NMA_IS_GCONF_CONNECTION (self), FALSE);
	g_return_val_if_fail (entry != NULL, FALSE);

	priv = NMA_GCONF_CONNECTION_GET_PRIVATE (self);
	wrapped_connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (self));

	/* FIXME: just update the modified field, no need to re-read all */
	gconf_connection = nm_gconf_read_connection (priv->client, priv->dir);
	if (!gconf_connection) {
		g_warning ("No connection read from GConf at %s.", priv->dir);
		goto invalid;
	}

	utils_fill_connection_certs (gconf_connection);
	if (!nm_connection_verify (gconf_connection)) {
		utils_clear_filled_connection_certs (gconf_connection);
		g_warning ("Invalid connection read from GConf at %s.", priv->dir);
		goto invalid;
	}
	utils_clear_filled_connection_certs (gconf_connection);

	/* Ignore the GConf update if nothing changed */
	if (nm_connection_compare (wrapped_connection, gconf_connection, COMPARE_FLAGS_EXACT))
		return TRUE;

	new_settings = nm_connection_to_hash (gconf_connection);
	nm_connection_replace_settings (wrapped_connection, new_settings);
	g_object_unref (gconf_connection);

	fill_vpn_user_name (wrapped_connection);

	utils_fill_connection_certs (wrapped_connection);
	settings = nm_connection_to_hash (wrapped_connection);
	utils_clear_filled_connection_certs (wrapped_connection);

	nm_exported_connection_signal_updated (NM_EXPORTED_CONNECTION (self), settings);
	g_hash_table_destroy (settings);
	return TRUE;

invalid:
	nm_exported_connection_signal_removed (NM_EXPORTED_CONNECTION (self));
	return FALSE;
}


static GHashTable *
get_settings (NMExportedConnection *exported)
{
	NMConnection *connection;
	GHashTable *settings;

	connection = nm_exported_connection_get_connection (exported);

	utils_fill_connection_certs (connection);
	settings = nm_connection_to_hash (connection);
	utils_clear_filled_connection_certs (connection);

	return settings;
}

static void
get_secrets (NMExportedConnection *exported,
		   const gchar *setting_name,
		   const gchar **hints,
		   gboolean request_new,
		   DBusGMethodInvocation *context)
{
	NMConnection *connection;
	GError *error = NULL;
	GHashTable *settings = NULL;
	GHashTable *secrets = NULL;
	NMSettingConnection *s_con;
	NMSetting *setting;
	const char *id;

	connection = nm_exported_connection_get_connection (exported);

	setting = nm_connection_get_setting_by_name (connection, setting_name);
	if (!setting) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d - Connection didn't have requested setting '%s'.",
		             __FILE__, __LINE__, setting_name);
		nm_warning ("%s", error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con || !s_con->id || !strlen (s_con->id) || !s_con->type) {
		g_set_error (&error, NM_SETTINGS_ERROR, 1,
		             "%s.%d - Connection didn't have required '"
		             NM_SETTING_CONNECTION_SETTING_NAME
		             "' setting , or the connection name was invalid.",
		             __FILE__, __LINE__);
		nm_warning ("%s", error->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	/* VPN passwords are handled by the VPN plugin's auth dialog */
	if (!strcmp (s_con->type, NM_SETTING_VPN_SETTING_NAME))
		goto get_secrets;

	if (request_new) {
		nm_info ("New secrets for %s/%s requested; ask the user",
		         s_con->id, setting_name);
		nm_connection_clear_secrets (connection);
		goto get_secrets;
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                  g_free, (GDestroyNotify) g_hash_table_destroy);

	id = nm_exported_connection_get_id (exported);
	secrets = nm_gconf_get_keyring_items (connection, id, setting_name, FALSE, &error);
	if (!secrets) {
		if (error) {
			nm_warning ("Error getting secrets: %s", error->message);
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else {
			nm_info ("No keyring secrets found for %s/%s; asking user.",
			         s_con->id, setting_name);
			goto get_secrets;
		}
	} else {
		if (g_hash_table_size (secrets) == 0) {
			g_hash_table_destroy (secrets);
			nm_warning ("%s.%d - Secrets were found for setting '%s' but none"
					  " were valid.", __FILE__, __LINE__, setting_name);
			goto get_secrets;
		} else {
			g_hash_table_insert (settings, g_strdup (setting_name), secrets);
			dbus_g_method_return (context, settings);
		}
	}

	g_hash_table_destroy (settings);
	return;

get_secrets:
	g_signal_emit (exported,
	               signals[NEW_SECRETS_REQUESTED],
	               0,
	               setting_name,
	               hints,
	               request_new,
	               context);
}

static const char *
get_id (NMExportedConnection *self)
{
	return NMA_GCONF_CONNECTION_GET_PRIVATE (self)->id;
}

static void
update (NMExportedConnection *exported, GHashTable *new_settings)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (exported);

	nm_gconf_write_connection (nm_exported_connection_get_connection (exported),
	                           priv->client,
	                           priv->dir,
	                           priv->id);
	gconf_client_notify (priv->client, priv->dir);
	gconf_client_suggest_sync (priv->client, NULL);
}

static void
delete (NMExportedConnection *exported)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (exported);
	GError *err = NULL;

	if (!gconf_client_recursive_unset (priv->client, priv->dir, 0, &err)) {
		g_warning ("Can not delete GConf connection: %s", err->message);
		g_error_free (err);
	}
}

/* GObject */

static void
nma_gconf_connection_init (NMAGConfConnection *connection)
{
}

static GObject *
constructor (GType type,
		   guint n_construct_params,
		   GObjectConstructParam *construct_params)
{
	GObject *object;
	NMAGConfConnectionPrivate *priv;
	NMConnection *connection;
	DBusGConnection *bus;
	GError *err = NULL;

	object = G_OBJECT_CLASS (nma_gconf_connection_parent_class)->constructor (type, n_construct_params, construct_params);

	if (!object)
		return NULL;

	priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	if (!priv->client) {
		nm_warning ("GConfClient not provided.");
		goto err;
	}

	if (!priv->dir) {
		nm_warning ("GConf directory not provided.");
		goto err;
	}

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (object));

	priv->id = g_path_get_basename (priv->dir);
	g_object_set_data (G_OBJECT (connection), NMA_CONNECTION_ID_TAG, priv->id);

	utils_fill_connection_certs (connection);
	if (!nm_connection_verify (connection)) {
		utils_clear_filled_connection_certs (connection);
		nm_warning ("Invalid connection read from GConf at %s.", priv->dir);
		goto err;
	}
	utils_clear_filled_connection_certs (connection);

	fill_vpn_user_name (connection);

	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &err);
	if (!bus) {
		nm_warning ("Could not get the system bus: %s", err->message);
		g_error_free (err);
		goto err;
	}

	nm_exported_connection_register_object (NM_EXPORTED_CONNECTION (object),
	                                        NM_CONNECTION_SCOPE_USER,
	                                        bus);
	dbus_g_connection_unref (bus);

	return object;

 err:
	g_object_unref (object);

	return NULL;
}

static void
dispose (GObject *object)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);
	NMConnection *connection;

	if (priv->disposed)
		return;

	priv->disposed = TRUE;

	connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (object));
	if (connection)
		g_object_set_data (G_OBJECT (connection), NMA_CONNECTION_ID_TAG, NULL);

	g_object_unref (priv->client);

	G_OBJECT_CLASS (nma_gconf_connection_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	g_free (priv->id);
	g_free (priv->dir);

	G_OBJECT_CLASS (nma_gconf_connection_parent_class)->finalize (object);
}

static void
set_property (GObject *object, guint prop_id,
		    const GValue *value, GParamSpec *pspec)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_CLIENT:
		/* Construct only */
		priv->client = g_value_dup_object (value);
		break;
	case PROP_DIR:
		/* Construct only */
		priv->dir = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
		    GValue *value, GParamSpec *pspec)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_CLIENT:
		g_value_set_object (value, priv->client);
		break;
	case PROP_DIR:
		g_value_set_string (value, priv->dir);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nma_gconf_connection_class_init (NMAGConfConnectionClass *gconf_connection_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (gconf_connection_class);
	NMExportedConnectionClass *connection_class = NM_EXPORTED_CONNECTION_CLASS (gconf_connection_class);

	g_type_class_add_private (gconf_connection_class, sizeof (NMAGConfConnectionPrivate));

	/* Virtual methods */
	object_class->constructor  = constructor;
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->dispose      = dispose;
	object_class->finalize     = finalize;

	connection_class->get_settings = get_settings;
	connection_class->get_secrets  = get_secrets;
	connection_class->get_id       = get_id;
	connection_class->update       = update;
	connection_class->delete       = delete;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_CLIENT,
		 g_param_spec_object (NMA_GCONF_CONNECTION_CLIENT,
						  "GConfClient",
						  "GConfClient",
						  GCONF_TYPE_CLIENT,
						  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_DIR,
		 g_param_spec_string (NMA_GCONF_CONNECTION_DIR,
						  "GConf directory",
						  "GConf directory",
						  NULL,
						  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/* Signals */
	signals[NEW_SECRETS_REQUESTED] =
		g_signal_new ("new-secrets-requested",
				    G_OBJECT_CLASS_TYPE (object_class),
				    G_SIGNAL_RUN_FIRST,
				    G_STRUCT_OFFSET (NMAGConfConnectionClass, new_secrets_requested),
				    NULL, NULL,
				    nma_marshal_VOID__STRING_POINTER_BOOLEAN_POINTER,
				    G_TYPE_NONE, 4,
				    G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN, G_TYPE_POINTER);
}
