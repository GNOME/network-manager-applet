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
 * (C) Copyright 2008 Red Hat, Inc.
 */

#ifndef MOBILE_WIZARD_H
#define MOBILE_WIZARD_H

#include <glib.h>
#include <NetworkManager.h>
#include <nm-device.h>

typedef struct MobileWizard MobileWizard;

typedef struct {
	char *provider_name;
	char *plan_name;
	NMDeviceType devtype;
	char *username;
	char *password;
	char *gsm_apn;
} MobileWizardAccessMethod;

typedef void (*MobileWizardCallback) (MobileWizard *self,
                                      gboolean canceled,
                                      MobileWizardAccessMethod *method,
                                      gpointer user_data);

MobileWizard *mobile_wizard_new (GtkWindow *parent,
                                 GtkWindowGroup *window_group,
                                 NMDeviceType devtype,
                                 gboolean will_connect_after,
                                 MobileWizardCallback cb,
                                 gpointer user_data);

void mobile_wizard_present (MobileWizard *wizard);

void mobile_wizard_destroy (MobileWizard *self);

#endif /* MOBILE_WIZARD_H */

