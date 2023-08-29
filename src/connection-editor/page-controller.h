// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Copyright 2012 - 2014 Red Hat, Inc.
 */

#ifndef __PAGE_CONTROLLER_H__
#define __PAGE_CONTROLLER_H__

#include <glib.h>
#include <glib-object.h>

#include "ce-page.h"
#include "connection-helpers.h"

#define CE_TYPE_PAGE_CONTROLLER            (ce_page_controller_get_type ())
#define CE_PAGE_CONTROLLER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CE_TYPE_PAGE_CONTROLLER, CEPageController))
#define CE_PAGE_CONTROLLER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CE_TYPE_PAGE_CONTROLLER, CEPageControllerClass))
#define CE_IS_PAGE_CONTROLLER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CE_TYPE_PAGE_CONTROLLER))
#define CE_IS_PAGE_CONTROLLER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CE_TYPE_PAGE_CONTROLLER))
#define CE_PAGE_CONTROLLER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CE_TYPE_PAGE_CONTROLLER, CEPageControllerClass))

typedef struct {
	CEPage parent;

	gboolean aggregating;
} CEPageController;

typedef struct {
	CEPageClass parent;

	/* signals */
	void (*create_connection)  (CEPageController *self, NMConnection *connection);
	void (*connection_added)   (CEPageController *self, NMConnection *connection);
	void (*connection_removed) (CEPageController *self, NMConnection *connection);

	/* methods */
	void (*add_port) (CEPageController *self, NewConnectionResultFunc result_func);
} CEPageControllerClass;

GType ce_page_controller_get_type (void);

gboolean ce_page_controller_has_ports (CEPageController *self);

#endif  /* __PAGE_CONTROLLER_H__ */

