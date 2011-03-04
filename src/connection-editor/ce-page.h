/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
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
 * (C) Copyright 2008 - 2011 Red Hat, Inc.
 */

#ifndef __CE_PAGE_H__
#define __CE_PAGE_H__

#include <glib.h>
#include <glib-object.h>

#include <gtk/gtk.h>

#include <dbus/dbus-glib.h>
#include <nm-connection.h>

typedef void (*PageNewConnectionResultFunc) (NMConnection *connection,
                                             gboolean canceled,
                                             GError *error,
                                             gpointer user_data);

typedef GSList * (*PageGetConnectionsFunc) (gpointer user_data);

typedef void (*PageNewConnectionFunc) (GtkWindow *parent,
                                       PageNewConnectionResultFunc result_func,
                                       PageGetConnectionsFunc get_connections_func,
                                       gpointer user_data);

#define CE_TYPE_PAGE            (ce_page_get_type ())
#define CE_PAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CE_TYPE_PAGE, CEPage))
#define CE_PAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CE_TYPE_PAGE, CEPageClass))
#define CE_IS_PAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CE_TYPE_PAGE))
#define CE_IS_PAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), CE_TYPE_PAGE))
#define CE_PAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CE_TYPE_PAGE, CEPageClass))

#define CE_PAGE_CONNECTION    "connection"
#define CE_PAGE_INITIALIZED   "initialized"
#define CE_PAGE_PARENT_WINDOW "parent-window"

typedef struct {
	GObject parent;

	gboolean initialized;
	GtkBuilder *builder;
	GtkWidget *page;
	char *title;

	DBusGProxy *proxy;
	gulong secrets_done_validate;

	NMConnection *connection;
	GtkWindow *parent_window;

	gboolean disposed;
} CEPage;

typedef struct {
	GObjectClass parent;

	/* Virtual functions */
	gboolean    (*validate)    (CEPage *self, NMConnection *connection, GError **error);

	/* Signals */
	void        (*changed)     (CEPage *self);
	void        (*initialized) (CEPage *self, GError *error);
} CEPageClass;


typedef CEPage* (*CEPageNewFunc)(NMConnection *connection,
                                 GtkWindow *parent,
                                 const char **out_secrets_setting_name,
                                 GError **error);


GType ce_page_get_type (void);

GtkWidget *  ce_page_get_page (CEPage *self);

const char * ce_page_get_title (CEPage *self);

gboolean ce_page_validate (CEPage *self, NMConnection *connection, GError **error);

void ce_page_changed (CEPage *self);

void ce_page_mac_to_entry (const GByteArray *mac, GtkEntry *entry);

GByteArray *ce_page_entry_to_mac (GtkEntry *entry, gboolean *invalid);

gint ce_spin_output_with_default (GtkSpinButton *spin, gpointer user_data);

int ce_get_property_default (NMSetting *setting, const char *property_name);

void ce_page_complete_init (CEPage *self,
                            const char *setting_name,
                            GHashTable *secrets,
                            GError *error);

gboolean ce_page_get_initialized (CEPage *self);

char *ce_page_get_next_available_name (GSList *connections, const char *format);

/* Only for subclasses */
NMConnection *ce_page_new_connection (const char *format,
                                      const char *ctype,
                                      gboolean autoconnect,
                                      PageGetConnectionsFunc get_connections_func,
                                      gpointer user_data);

CEPage *ce_page_new (GType page_type,
                     NMConnection *connection,
                     GtkWindow *parent_window,
                     const char *ui_file,
                     const char *widget_name,
                     const char *title);

#endif  /* __CE_PAGE_H__ */

