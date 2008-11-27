/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

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
	                           priv->dir);
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

	user_name = g_get_user_name ();
	g_assert (g_utf8_validate (user_name, -1, NULL));
	g_object_set (s_vpn, NM_SETTING_VPN_USER_NAME, user_name, NULL);
}

gboolean
nma_gconf_connection_changed (NMAGConfConnection *self)
{
	NMAGConfConnectionPrivate *priv;
	GHashTable *settings;
	NMConnection *wrapped_connection;
	NMConnection *gconf_connection;
	GHashTable *new_settings;
	GError *error = NULL;

	g_return_val_if_fail (NMA_IS_GCONF_CONNECTION (self), FALSE);

	priv = NMA_GCONF_CONNECTION_GET_PRIVATE (self);
	wrapped_connection = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (self));

	gconf_connection = nm_gconf_read_connection (priv->client, priv->dir);
	if (!gconf_connection) {
		g_warning ("No connection read from GConf at %s.", priv->dir);
		goto invalid;
	}

	utils_fill_connection_certs (gconf_connection);
	if (!nm_connection_verify (gconf_connection, &error)) {
		utils_clear_filled_connection_certs (gconf_connection);
		g_warning ("%s: Invalid connection %s: '%s' / '%s' invalid: %d",
		           __func__, priv->dir,
		           g_type_name (nm_connection_lookup_setting_type_by_quark (error->domain)),
		           error->message, error->code);
		goto invalid;
	}
	utils_clear_filled_connection_certs (gconf_connection);

	/* Ignore the GConf update if nothing changed */
	if (nm_connection_compare (wrapped_connection, gconf_connection, NM_SETTING_COMPARE_FLAG_EXACT))
		return TRUE;

	utils_fill_connection_certs (gconf_connection);
	new_settings = nm_connection_to_hash (gconf_connection);
	utils_clear_filled_connection_certs (gconf_connection);

	if (!nm_connection_replace_settings (wrapped_connection, new_settings, &error)) {
		utils_clear_filled_connection_certs (wrapped_connection);
		g_hash_table_destroy (new_settings);

		g_warning ("%s: '%s' / '%s' invalid: %d",
		           __func__,
		           error ? g_type_name (nm_connection_lookup_setting_type_by_quark (error->domain)) : "(none)",
		           (error && error->message) ? error->message : "(none)",
		           error ? error->code : -1);
		goto invalid;
	}
	g_object_unref (gconf_connection);
	g_hash_table_destroy (new_settings);

	fill_vpn_user_name (wrapped_connection);

	settings = nm_connection_to_hash (wrapped_connection);
	utils_clear_filled_connection_certs (wrapped_connection);

	nm_exported_connection_signal_updated (NM_EXPORTED_CONNECTION (self), settings);
	g_hash_table_destroy (settings);
	return TRUE;

invalid:
	g_clear_error (&error);
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
secrets_return_error (DBusGMethodInvocation *context, GError *error)
{
	nm_warning ("Error getting secrets: %s", error->message);
	dbus_g_method_return_error (context, error);
	g_error_free (error);
}

typedef struct {
	gboolean found;
	const char **hints;
} FindHintsInfo;

static void
find_hints_in_secrets (gpointer key, gpointer data, gpointer user_data)
{
	FindHintsInfo *info = (FindHintsInfo *) user_data;
	const char **iter;

	for (iter = info->hints; !info->found && *iter; iter++) {
		if (!strcmp (*iter, (const char *) key) && data && G_IS_VALUE (data))
			info->found = TRUE;
	}
}

static void
service_get_secrets (NMExportedConnection *exported,
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
	const char *connection_id;
	const char *connection_type;

	connection = nm_exported_connection_get_connection (exported);

	setting = nm_connection_get_setting_by_name (connection, setting_name);
	if (!setting) {
		g_set_error (&error, NM_SETTINGS_ERROR, NM_SETTINGS_ERROR_INVALID_CONNECTION,
		             "%s.%d - Connection didn't have requested setting '%s'.",
		             __FILE__, __LINE__, setting_name);
		secrets_return_error (context, error);
		return;
	}

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	connection_id = s_con ? nm_setting_connection_get_id (s_con) : NULL;
	connection_type = s_con ? nm_setting_connection_get_connection_type (s_con) : NULL;

	if (!s_con || !connection_id || !strlen (connection_id) || !connection_type) {
		g_set_error (&error, NM_SETTINGS_ERROR, NM_SETTINGS_ERROR_INVALID_CONNECTION,
		             "%s.%d - Connection didn't have required '"
		             NM_SETTING_CONNECTION_SETTING_NAME
		             "' setting , or the connection name was invalid.",
		             __FILE__, __LINE__);
		secrets_return_error (context, error);
		return;
	}

	/* VPN passwords are handled by the VPN plugin's auth dialog */
	if (!strcmp (connection_type, NM_SETTING_VPN_SETTING_NAME))
		goto get_secrets;

	if (request_new) {
		nm_info ("New secrets for %s/%s requested; ask the user",
		         connection_id, setting_name);
		nm_connection_clear_secrets (connection);
		goto get_secrets;
	}

	secrets = nm_gconf_get_keyring_items (connection, setting_name, FALSE, &error);
	if (!secrets) {
		if (error) {
			secrets_return_error (context, error);
			return;
		}
		nm_info ("No keyring secrets found for %s/%s; asking user.",
		         connection_id, setting_name);
		goto get_secrets;
	}

	if (g_hash_table_size (secrets) == 0) {
		g_hash_table_destroy (secrets);
		nm_warning ("%s.%d - Secrets were found for setting '%s' but none"
				  " were valid.", __FILE__, __LINE__, setting_name);
		goto get_secrets;
	}

	/* If there were hints, and none of the hints were returned by the keyring,
	 * get some new secrets.
	 */
	if (hints && g_strv_length ((char **) hints)) {
		FindHintsInfo info = { .found = FALSE, .hints = hints };

		g_hash_table_foreach (secrets, find_hints_in_secrets, &info);
		if (info.found == FALSE) {
			g_hash_table_destroy (secrets);
			goto get_secrets;
		}
	}

	/* Returned secrets are a{sa{sv}}; this is the outer a{s...} hash that
	 * will contain all the individual settings hashes.
	 */
	settings = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                  g_free,
	                                  (GDestroyNotify) g_hash_table_destroy);
	g_hash_table_insert (settings, g_strdup (setting_name), secrets);
	dbus_g_method_return (context, settings);
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

static gboolean
update (NMExportedConnection *exported, GHashTable *new_settings, GError **error)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (exported);
	NMConnection *tmp;
	gboolean success = FALSE;

	tmp = nm_connection_new_from_hash (new_settings, error);
	if (!tmp) {
		nm_warning ("%s: Invalid connection: '%s' / '%s' invalid: %d",
		            __func__,
		            g_type_name (nm_connection_lookup_setting_type_by_quark ((*error)->domain)),
		            (*error)->message, (*error)->code);
	} else {
		/* Copy private values to the connection that actually gets saved */
		nm_gconf_copy_private_connection_values (tmp, nm_exported_connection_get_connection (exported));

		nm_gconf_write_connection (tmp, priv->client, priv->dir);
		g_object_unref (tmp);

		gconf_client_notify (priv->client, priv->dir);
		gconf_client_suggest_sync (priv->client, NULL);
		success = TRUE;
	}

	return success;
}

static gboolean
do_delete (NMExportedConnection *exported, GError **err)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (exported);
	gboolean success;

	success = gconf_client_recursive_unset (priv->client, priv->dir, 0, err);
	gconf_client_suggest_sync (priv->client, NULL);

	return success;
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
	GError *error = NULL;

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

	utils_fill_connection_certs (connection);
	if (!nm_connection_verify (connection, &error)) {
		utils_clear_filled_connection_certs (connection);
		g_warning ("Invalid connection: '%s' / '%s' invalid: %d",
		           g_type_name (nm_connection_lookup_setting_type_by_quark (error->domain)),
		           error->message, error->code);
		g_error_free (error);
		goto err;
	}
	utils_clear_filled_connection_certs (connection);

	fill_vpn_user_name (connection);

	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (!bus) {
		nm_warning ("Could not get the system bus: %s", error->message);
		g_error_free (error);
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

	if (priv->disposed)
		return;
	priv->disposed = TRUE;

	g_object_unref (priv->client);

	G_OBJECT_CLASS (nma_gconf_connection_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMAGConfConnectionPrivate *priv = NMA_GCONF_CONNECTION_GET_PRIVATE (object);

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
		priv->client = GCONF_CLIENT (g_value_dup_object (value));
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
	connection_class->service_get_secrets = service_get_secrets;
	connection_class->update       = update;
	connection_class->do_delete    = do_delete;

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
