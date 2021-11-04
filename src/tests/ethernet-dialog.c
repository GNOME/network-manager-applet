// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Red Hat, Inc.
 */

#include "nm-default.h"
#include "ethernet-dialog.h"

#include <gtk/gtk.h>
#include <NetworkManager.h>

int
main (int argc, char *argv[])
{
	NMConnection *connection, *new_connection;
	GHashTable *diff = NULL, *setting_diff;
	GHashTableIter iter, setting_iter;
	const char *setting, *key;
	GtkWidget *dialog;
	GError *error = NULL;
	gs_unref_bytes GBytes *ssid = g_bytes_new_static ("<Secured Wired>",
	                                                  NM_STRLEN ("<Secured Wired>"));

	gtk_init (&argc, &argv);

	connection = nm_simple_connection_new ();
	nm_connection_add_setting (connection,
		g_object_new (NM_TYPE_SETTING_CONNECTION,
		              NM_SETTING_CONNECTION_ID, "<Secured Wired>",
		              NULL));
	nm_connection_add_setting (connection,
		g_object_new (NM_TYPE_SETTING_WIRED,
		              NULL));
	nm_connection_add_setting (connection,
		g_object_new (NM_TYPE_SETTING_802_1X,
		              NM_SETTING_802_1X_EAP, (const char * const []){ "peap", NULL },
		              NM_SETTING_802_1X_IDENTITY, "budulinek",
		              NM_SETTING_802_1X_PHASE2_AUTH, "gtc",
		              NULL));

	if (!nm_connection_normalize (connection, NULL, NULL, &error)) {
		nm_connection_dump (connection);
		g_printerr ("Error: %s\n", error->message);
		g_error_free (error);
		return 1;
	}

	dialog = nma_ethernet_dialog_new (nm_simple_connection_new_clone (connection));
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		g_print ("settings changed:\n");
		new_connection = nma_ethernet_dialog_get_connection (dialog);
		nm_connection_diff (connection, new_connection, NM_SETTING_COMPARE_FLAG_EXACT, &diff);
		if (diff) {
			g_hash_table_iter_init (&iter, diff);
			while (g_hash_table_iter_next (&iter, (gpointer) &setting, (gpointer) &setting_diff)) {
				g_hash_table_iter_init (&setting_iter, setting_diff);
				while (g_hash_table_iter_next (&setting_iter, (gpointer) &key, NULL))
					g_print (" %s.%s\n", setting, key);
			}

			g_hash_table_destroy (diff);
		}
	}
	gtk_widget_destroy (dialog);
	g_object_unref (connection);
}
