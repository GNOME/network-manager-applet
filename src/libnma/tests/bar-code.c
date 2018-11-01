/*
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
 * Copyright 2018 Red Hat, Inc.
 */

#include "nm-default.h"

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include "nma-bar-code-widget.h"

static gboolean
delete (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
        gtk_main_quit ();

        return FALSE;
}

#if 0
#if 0
3.5 252
2  144
#endif

static void
draw_page (GtkPrintOperation *operation, GtkPrintContext *context, gint page_nr, gpointer user_data)
{
	NMABarCode *qr = NMA_BAR_CODE (user_data);
	cairo_t *cr = gtk_print_context_get_cairo_context (context);
	int size = nma_bar_code_get_size (qr);

	cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);

	cairo_set_line_width (cr, 0.01);
	cairo_translate (cr, 36, 36);
	cairo_rectangle (cr, 0, 0, 252, 144);
	cairo_stroke (cr);

	cairo_translate (cr, 12, 12);

	cairo_save (cr);
	cairo_scale (cr, (float)84/(float)size, (float)84/(float)size);
	cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
	nma_bar_code_draw (qr, cr);
	cairo_restore (cr);

	//cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

	cairo_set_font_size (cr, 12);
	cairo_move_to (cr, 0, 108);
	cairo_show_text(cr, "$ nmcli d wifi con kurwix \\");
	cairo_move_to (cr, 24, 120);
	cairo_show_text(cr, "pasword yoloswag");

	cairo_move_to (cr, 96, 12);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, 12);
	cairo_show_text(cr, "Network");

	cairo_move_to (cr, 96, 30);
	cairo_set_font_size (cr, 18);
	cairo_show_text(cr, "kurwix");

	cairo_move_to (cr, 96, 60);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, 12);
	cairo_show_text(cr, "Password");

	cairo_move_to (cr, 96, 78);
	cairo_set_font_size (cr, 18);
	cairo_show_text(cr, "yoloswag");
}


static gboolean
clicked_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	NMABarCode *qr = NMA_BAR_CODE (user_data);
	GtkPrintOperation *print;

	qr = nma_bar_code_new ("WIFI:T:WPA;S:mynetwork;P:mypass;;");
	print = gtk_print_operation_new ();

	gtk_print_operation_set_n_pages (print, 1);
	gtk_print_operation_set_use_full_page (print, TRUE);
	gtk_print_operation_set_unit (print, GTK_UNIT_POINTS);
	//gtk_print_operation_set_unit (print, GTK_UNIT_INCH);

	g_signal_connect (print, "draw_page", G_CALLBACK (draw_page), qr);

	gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, NULL, NULL);

	g_object_unref (print);

	return FALSE;
}

#endif

static void
ssid_changed (GtkEditable *editable, gpointer user_data)
{
	NMConnection *connection = NM_CONNECTION (user_data);
	NMSettingWireless *s_wireless = nm_connection_get_setting_wireless (connection);
	gs_unref_bytes GBytes *ssid = NULL;
	GtkEntryBuffer *buffer;

	g_return_if_fail (s_wireless);

	buffer = gtk_entry_get_buffer (GTK_ENTRY (editable));
	ssid = g_bytes_new_static (gtk_entry_buffer_get_text (buffer),
	                           gtk_entry_buffer_get_bytes (buffer));

	g_object_set (s_wireless,
	              NM_SETTING_WIRELESS_SSID, ssid,
	              NULL);

	g_printerr ("SSID CHANGED\n"); 
}

static void
password_changed (GtkEditable *editable, gpointer user_data)
{
	NMConnection *connection = NM_CONNECTION (user_data);
	NMSettingWirelessSecurity *s_wsec = nm_connection_get_setting_wireless_security (connection);

	if (!s_wsec)
		return;

	g_object_set (s_wsec,
	              NM_SETTING_WIRELESS_SECURITY_PSK,
	              gtk_entry_get_text (GTK_ENTRY (editable)),
	              NULL);

	g_printerr ("PASSWORD CHANGED\n"); 
}

static void
key_mgmt_changed (GtkComboBox *combo_box, gpointer user_data)
{
	NMConnection *connection = NM_CONNECTION (user_data);
	const char *key_mgmt = gtk_combo_box_get_active_id (combo_box);
	NMSettingWirelessSecurity *s_wsec = nm_connection_get_setting_wireless_security (connection);
	GtkWidget *pass = g_object_get_data (G_OBJECT (combo_box), "pass");

	if (!key_mgmt) {
		nm_connection_remove_setting (connection, NM_TYPE_SETTING_WIRELESS_SECURITY);
		gtk_widget_set_sensitive (pass, FALSE);
		return;
	}

	if (!s_wsec) {
		s_wsec = NM_SETTING_WIRELESS_SECURITY (nm_setting_wireless_security_new ());
		nm_connection_add_setting (connection, NM_SETTING (s_wsec));
		gtk_widget_set_sensitive (pass, TRUE);
		password_changed (GTK_EDITABLE (pass), connection);
	}

	g_object_set (s_wsec,
		      NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, key_mgmt,
		      NULL);

	g_printerr ("KEY_MGMT_CHANGED {%s}\n", key_mgmt);
}

int
main (int argc, char *argv[])
{
	GtkWidget *w, *pass;
	GtkWidget *grid;
	NMConnection *connection = NULL;
	gs_unref_bytes GBytes *ssid = g_bytes_new_static ("\"ab:cd\"", 13);

	connection = nm_simple_connection_new ();
	nm_connection_add_setting (connection,
		g_object_new (NM_TYPE_SETTING_CONNECTION,
		              NM_SETTING_CONNECTION_ID, "fifik",
		              NULL));
	nm_connection_add_setting (connection,
	                           nm_setting_wireless_new ());

        gtk_init (&argc, &argv);

        w = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_widget_show (w);
//	gtk_window_set_default_size (GTK_WINDOW (w), 800, 680);
	g_signal_connect (w, "delete-event", G_CALLBACK (delete), NULL);

	grid = gtk_grid_new ();
	gtk_widget_show (grid);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 6);
	gtk_container_add (GTK_CONTAINER (w), grid);

	w = gtk_label_new ("SSID");
	gtk_widget_show (w);
	//gtk_label_set_xalign (GTK_LABEL (w), 1);
	gtk_grid_attach (GTK_GRID (grid), w, 0, 0, 1, 1);

	w = gtk_entry_new ();
	g_signal_connect (w, "changed", G_CALLBACK (ssid_changed), connection);
	gtk_entry_set_text (GTK_ENTRY (w), "\"ab:cd\"");
	gtk_widget_show (w);
	gtk_grid_attach (GTK_GRID (grid), w, 1, 0, 1, 1);

	w = gtk_label_new ("Password");
	gtk_widget_show (w);
	//gtk_label_set_xalign (GTK_LABEL (w), 1);
	gtk_grid_attach (GTK_GRID (grid), w, 0, 1, 1, 1);

	pass = gtk_entry_new ();
	g_signal_connect (pass, "changed", G_CALLBACK (password_changed), connection);
	gtk_entry_set_text (GTK_ENTRY (pass), "lolofon");
	gtk_widget_show (pass);
	gtk_grid_attach (GTK_GRID (grid), pass, 1, 1, 1, 1);

	w = gtk_label_new ("Key Management");
	gtk_widget_show (w);
	//gtk_label_set_xalign (GTK_LABEL (w), 1);
	gtk_grid_attach (GTK_GRID (grid), w, 0, 2, 1, 1);

	w = gtk_combo_box_text_new ();
	gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (w), NULL, "No Password");
	gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (w), "none", "WEP: none");
	gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (w), "ieee8021x", "WEP: ieee8021x");
	gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (w), "wpa-none", "WPA: wpa-none");
	gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (w), "wpa-psk", "WPA: wpa-psk");
	g_object_set_data (G_OBJECT (w), "pass", pass);
	g_signal_connect (w, "changed", G_CALLBACK (key_mgmt_changed), connection);
	gtk_combo_box_set_active_id (GTK_COMBO_BOX (w), "wpa-psk");
	gtk_widget_show (w);
	gtk_grid_attach (GTK_GRID (grid), w, 1, 2, 1, 1);

	w = nma_bar_code_widget_new (connection);
	gtk_widget_show (w);
	gtk_widget_set_vexpand (w, TRUE);
	gtk_widget_set_hexpand (w, TRUE);
	gtk_grid_attach (GTK_GRID (grid), w, 0, 3, 2, 1);

	gtk_main ();
}
