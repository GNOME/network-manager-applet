// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * Copyright 2009 - 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include <string.h>

#include "ce-polkit.h"

typedef struct {
	char *tooltip;
	char *auth_tooltip;
	char *validation_error;

	NMClientPermission permission;
	NMClientPermissionResult permission_result;
} CePolkitData;

static void
ce_polkit_data_free (CePolkitData *data)
{
	g_free (data->tooltip);
	g_free (data->auth_tooltip);
	g_free (data->validation_error);
	g_slice_free (CePolkitData, data);
}

static void
update_widget (GtkWidget *widget)
{
	CePolkitData *data = g_object_get_data (G_OBJECT (widget), "ce-polkit-data");

	if (data->validation_error) {
		gtk_widget_set_sensitive (widget, FALSE);
		gtk_widget_set_tooltip_text (widget, data->validation_error);
	} else if (data->permission_result == NM_CLIENT_PERMISSION_RESULT_AUTH) {
		gtk_widget_set_sensitive (widget, TRUE);
		gtk_widget_set_tooltip_text (widget, data->auth_tooltip);
	} else if (data->permission_result == NM_CLIENT_PERMISSION_RESULT_YES) {
		gtk_widget_set_sensitive (widget, TRUE);
		gtk_widget_set_tooltip_text (widget, data->tooltip);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
		gtk_widget_set_tooltip_text (widget, _("No polkit authorization to perform the action"));
	}
}

static void
permission_changed_cb (NMClient *client,
                       NMClientPermission permission,
                       NMClientPermissionResult result,
                       GtkWidget *widget)
{
	CePolkitData *data = g_object_get_data (G_OBJECT (widget), "ce-polkit-data");

	data->permission_result = result;
	update_widget (widget);
}

void ce_polkit_set_widget_validation_error (GtkWidget *widget,
                                            const char *validation_error)
{
	CePolkitData *data = g_object_get_data (G_OBJECT (widget), "ce-polkit-data");

	g_free (data->validation_error);
	data->validation_error = g_strdup (validation_error);
	update_widget (widget);
}

void
ce_polkit_connect_widget (GtkWidget *widget,
                          const char *tooltip,
                          const char *auth_tooltip,
                          NMClient *client,
                          NMClientPermission permission)
{
	CePolkitData *data;

	data = g_slice_new0 (CePolkitData);
	data->tooltip = g_strdup (tooltip);
	data->auth_tooltip = g_strdup (auth_tooltip);
	data->permission = permission;
	g_object_set_data_full (G_OBJECT (widget),
	                        "ce-polkit-data",
	                        data,
	                        (GDestroyNotify) ce_polkit_data_free);

	g_signal_connect_object (client,
	                         "permission-changed",
	                         G_CALLBACK (permission_changed_cb),
	                         G_OBJECT (widget),
	                         0);

	permission_changed_cb (client,
	                       permission,
	                       nm_client_get_permission_result (client, permission),
	                       widget);
}
