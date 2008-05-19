/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#ifndef NMA_GCONF_CONNECTION_H
#define NMA_GCONF_CONNECTION_H

#include <gconf/gconf-client.h>
#include <dbus/dbus-glib.h>
#include <nm-settings.h>

G_BEGIN_DECLS

#define NMA_TYPE_GCONF_CONNECTION            (nma_gconf_connection_get_type ())
#define NMA_GCONF_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NMA_TYPE_GCONF_CONNECTION, NMAGConfConnection))
#define NMA_GCONF_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NMA_TYPE_GCONF_CONNECTION, NMAGConfConnectionClass))
#define NMA_IS_GCONF_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NMA_TYPE_GCONF_CONNECTION))
#define NMA_IS_GCONF_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NMA_TYPE_GCONF_CONNECTION))
#define NMA_GCONF_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NMA_TYPE_GCONF_CONNECTION, NMAGConfConnectionClass))

#define NMA_GCONF_CONNECTION_CLIENT "client"
#define NMA_GCONF_CONNECTION_DIR    "dir"

typedef struct {
	NMExportedConnection parent;
} NMAGConfConnection;

typedef struct {
	NMExportedConnectionClass parent;

	/* Signals */
	void (*new_secrets_requested)  (NMAGConfConnection *self,
	                                const char *setting_name,
	                                const char **hints,
	                                gboolean ask_user,
	                                DBusGMethodInvocation *context);
} NMAGConfConnectionClass;

GType nma_gconf_connection_get_type (void);

NMAGConfConnection *nma_gconf_connection_new  (GConfClient *client,
									  const char *conf_dir);

NMAGConfConnection *nma_gconf_connection_new_from_connection (GConfClient *client,
												  const char *conf_dir,
												  NMConnection *connection);

const char         *nma_gconf_connection_get_path (NMAGConfConnection *self);

void                nma_gconf_connection_save (NMAGConfConnection *self);

gboolean            nma_gconf_connection_changed (NMAGConfConnection *self);

G_END_DECLS

#endif /* NMA_GCONF_CONNECTION_H */
