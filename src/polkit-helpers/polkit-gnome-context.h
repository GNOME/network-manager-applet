/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/***************************************************************************
 *
 * polkit-gnome-context.h : Convenience functions for using PolicyKit
 * from GTK+ and GNOME applications.
 *
 * Copyright (C) 2007 David Zeuthen, <david@fubar.dk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 **************************************************************************/

#ifndef __POLKIT_GNOME_CONTEXT_H__
#define __POLKIT_GNOME_CONTEXT_H__

#include <glib-object.h>
#include <polkit-dbus/polkit-dbus.h>

G_BEGIN_DECLS

#define POLKIT_GNOME_TYPE_CONTEXT            (polkit_gnome_context_get_type ())
#define POLKIT_GNOME_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), POLKIT_GNOME_TYPE_CONTEXT, PolKitGnomeContext))
#define POLKIT_GNOME_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), POLKIT_GNOME_TYPE_CONTEXT, PolKitGnomeContextClass))
#define POLKIT_GNOME_IS_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), POLKIT_GNOME_TYPE_CONTEXT))
#define POLKIT_GNOME_IS_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), POLKIT_GNOME_TYPE_CONTEXT))
#define POLKIT_GNOME_CONTEXT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), POLKIT_GNOME_TYPE_CONTEXT, PolKitGnomeContextClass))

/**
 * POLKIT_GNOME_CONTEXT_ERROR:
 *
 * Error domain for using the GNOME PolicyKit context. Errors in this
 * domain will be from the #PolKitGnomeContextError enumeration. See
 * #GError for information on error domains.
 */
#define POLKIT_GNOME_CONTEXT_ERROR polkit_gnome_context_error_quark ()

/**
 * PolKitGnomeContextError:
 * @POLKIT_GNOME_CONTEXT_ERROR_FAILED: General error
 *
 * Error codes describing how #PolKitGnomeContext can fail.
 */
typedef enum
{
        POLKIT_GNOME_CONTEXT_ERROR_FAILED
} PolKitGnomeContextError;


typedef struct _PolKitGnomeContext        PolKitGnomeContext;
typedef struct _PolKitGnomeContextPrivate PolKitGnomeContextPrivate;
typedef struct _PolKitGnomeContextClass   PolKitGnomeContextClass;

/**
 * PolKitGnomeContext:
 * @pk_context: for interfacing with PolicyKit; e.g. typically polkit_context_can_caller_do_action()
 * @pk_tracker: this is used for effieciently obtaining #PolKitCaller objects
 *
 * Provide access to #PolKitContext and #PolKitTracker instances
 * shared among many callers.
 */
struct _PolKitGnomeContext
{
        /*< private >*/
        GObject parent;

        PolKitGnomeContextPrivate *priv;

        /*< public >*/
        PolKitContext *pk_context;
};

struct _PolKitGnomeContextClass
{
        GObjectClass parent_class;

        void (* config_changed) (PolKitGnomeContext *context);
        void (* console_kit_db_changed) (PolKitGnomeContext *context);

        /* Padding for future expansion */
        void (*_reserved1) (void);
        void (*_reserved2) (void);
        void (*_reserved3) (void);
        void (*_reserved4) (void);
};

GType               polkit_gnome_context_get_type (void) G_GNUC_CONST;
PolKitGnomeContext *polkit_gnome_context_get      (GError **error);

GQuark polkit_gnome_context_error_quark (void);

DBusConnection *polkit_gnome_context_get_dbus_connection (PolKitGnomeContext *context);

G_END_DECLS

#endif  /* __POLKIT_GNOME_CONTEXT_H__ */
