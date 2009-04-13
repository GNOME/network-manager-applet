/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/***************************************************************************
 *
 * polkit-gnome-action.h : 
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 **************************************************************************/

#ifndef __POLKIT_GNOME_ACTION_H__
#define __POLKIT_GNOME_ACTION_H__

#include <gtk/gtk.h>
#include <polkit/polkit.h>

G_BEGIN_DECLS

#define POLKIT_GNOME_TYPE_ACTION            (polkit_gnome_action_get_type ())
#define POLKIT_GNOME_ACTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), POLKIT_GNOME_TYPE_ACTION, PolKitGnomeAction))
#define POLKIT_GNOME_ACTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), POLKIT_GNOME_TYPE_ACTION, PolKitGnomeActionClass))
#define POLKIT_GNOME_IS_ACTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), POLKIT_GNOME_TYPE_ACTION))
#define POLKIT_GNOME_IS_ACTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), POLKIT_GNOME_TYPE_ACTION))
#define POLKIT_GNOME_ACTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), POLKIT_GNOME_TYPE_ACTION, PolKitGnomeActionClass))

typedef struct _PolKitGnomeAction        PolKitGnomeAction;
typedef struct _PolKitGnomeActionPrivate PolKitGnomeActionPrivate;
typedef struct _PolKitGnomeActionClass   PolKitGnomeActionClass;

/**
 * PolKitGnomeAction:
 *
 * The PolKitGnomeAction struct contains only private data members and should not be accessed directly.
 */
struct _PolKitGnomeAction
{
        /*< private >*/
        GtkAction parent;        
        PolKitGnomeActionPrivate *priv;
};

struct _PolKitGnomeActionClass
{
        GtkActionClass parent_class;

        /* Signals */
        void (* auth_start) (PolKitGnomeAction *action);
        void (* auth_end) (PolKitGnomeAction *action, gboolean gained_privilege);
        void (* polkit_result_changed) (PolKitGnomeAction *action, PolKitResult current_result);

        /* Padding for future expansion */
        void (*_reserved1) (void);
        void (*_reserved2) (void);
        void (*_reserved3) (void);
        void (*_reserved4) (void);
};

GType              polkit_gnome_action_get_type          (void) G_GNUC_CONST;
PolKitGnomeAction *polkit_gnome_action_new               (const gchar           *name);
PolKitGnomeAction *polkit_gnome_action_new_default       (const gchar           *name, 
                                                          PolKitAction          *polkit_action, 
                                                          const gchar           *label, 
                                                          const gchar           *tooltip);
PolKitResult       polkit_gnome_action_get_polkit_result (PolKitGnomeAction     *action);

gboolean           polkit_gnome_action_get_sensitive          (PolKitGnomeAction     *action);
void               polkit_gnome_action_set_sensitive          (PolKitGnomeAction     *action,
                                                               gboolean               sensitive);

gboolean           polkit_gnome_action_get_visible            (PolKitGnomeAction     *action);
void               polkit_gnome_action_set_visible            (PolKitGnomeAction     *action,
                                                               gboolean               visible);

GtkWidget         *polkit_gnome_action_create_button        (PolKitGnomeAction     *action);

G_END_DECLS

#endif  /* __POLKIT_GNOME_ACTION_H__ */
