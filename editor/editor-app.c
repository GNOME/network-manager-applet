/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

/* NetworkManager Wireless Editor -- Edit wireless access points
 *
 * Calvin Gaisford <cgaisford@novell.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2006 Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnome/libgnome.h>

#if !GTK_CHECK_VERSION(2,6,0)
#include <gnome.h>
#endif

#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <NetworkManager.h>

#include "editor-app.h"
#include "editor-gconf-helper.h"
#include "bssid-editor-dialog.h"
#include "widget-wso.h"

#define WE_GCONF_WIRELESS_PATH     "/system/networking/wireless/networks"
#define WE_GCONF_WIRED_PATH        "/system/networking/wired/networks"

#define WE_ICON_SIZE	22

// WirelessNetworkTreeView defines
#define WNTV_DISPLAY_COLUMN	0
#define WNTV_PIXBUF_COLUMN	1
#define WNTV_DATA_COLUMN	2
#define WNTV_NUM_COLUMNS	3

// Security Options for Combo Box
#define SEC_OPTION_NONE				0
#define SEC_OPTION_WEP64			1
#define SEC_OPTION_WEP128			2
#define SEC_OPTION_WPA_PERSONAL		3
#define SEC_OPTION_WPA2_PERSONAL	4
#define SEC_OPTION_WPA_ENTERPRISE	5
#define SEC_OPTION_WPA2_ENTERPRISE	6
#define SEC_OPTION_LEAP				7

// This function not only updated the gconf entry with the new
// essid entry, but also moves the gconf dir for the entire set
// of entries since they are stored in gconf by the essid
static void
essid_entry_changed (GtkEntry *essid_entry, gpointer data)
{
	WE_DATA		*we_data;
	const gchar		*new_essid;
	gboolean	do_update;

	do_update = FALSE;
	we_data = (WE_DATA *)data;
	new_essid = gtk_entry_get_text(essid_entry);

	if(we_data->essid_value == NULL)
		do_update = TRUE;
	else if(g_ascii_strcasecmp(new_essid, we_data->essid_value) != 0)
		do_update = TRUE;

	if(we_data->essid_value != NULL)
	{
		g_free(we_data->essid_value);
		we_data->essid_value = g_strdup(new_essid);
	}

	if(do_update)
	{
		GtkTreeSelection	*selection;
		GError				*err = NULL;
		GtkTreeIter			iter;
		GtkTreeIter			childIter;
		GtkTreeModel		*model;
		GtkListStore		*store;
		gchar				*key = NULL;
		gchar				*gconf_path;
		gchar				*gconf_new_path;
		GSList				*gconf_entries;

		selection = gtk_tree_view_get_selection (
				GTK_TREE_VIEW (we_data->treeview));

		if(!gtk_tree_selection_get_selected (selection, &model, &iter))
			return;

		gtk_tree_model_get (model, &iter, WNTV_DATA_COLUMN, &gconf_path, -1);
		if(gconf_path == NULL)
			return;

		// go through and move all of the gconf entries to the new entry
		key = g_strdup_printf("%s/essid", gconf_path);
		gconf_client_set_string(we_data->gconf_client, key, new_essid, &err);
		g_free(key);

		gconf_new_path = g_strdup_printf("%s/%s", WE_GCONF_WIRELESS_PATH, 
				new_essid);

		err = NULL;
		gconf_entries = gconf_client_all_entries(we_data->gconf_client,
				gconf_path,
				&err);
		while (gconf_entries)
		{
			GConfEntry	*gconf_entry;
			gchar		*entry_tail;

			gconf_entry = gconf_entries->data;

			entry_tail = g_strrstr(gconf_entry->key, "/");
			if(entry_tail != NULL)
			{
				gchar		*new_key;

				new_key = g_strdup_printf("%s%s", gconf_new_path, entry_tail);

				gconf_client_set(we_data->gconf_client,
						new_key,
						gconf_entry->value,
						&err);

				gconf_client_unset(we_data->gconf_client,
						gconf_entry->key,
						&err);
				g_free(new_key);
			}
			gconf_entry_free(gconf_entry);
			gconf_entries = g_slist_delete_link (gconf_entries, gconf_entries);
		}
		gconf_client_suggest_sync(we_data->gconf_client, &err);


		// now update the data in the treeview
		store = GTK_LIST_STORE(gtk_tree_model_filter_get_model(
					GTK_TREE_MODEL_FILTER(model)));

		gtk_tree_model_filter_convert_iter_to_child_iter(
				GTK_TREE_MODEL_FILTER(model),
				&childIter,
				&iter);

		gtk_list_store_set (
				store,
				&childIter,
				WNTV_DISPLAY_COLUMN, g_strdup(new_essid),
				WNTV_DATA_COLUMN, g_strdup(gconf_new_path),
				-1);

		g_free(gconf_new_path);
	}
}

static gboolean
essid_entry_focus_lost (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
	essid_entry_changed (GTK_ENTRY (widget), data);
	return FALSE;
}

static void
change_security_settings (gint option, gpointer data)
{
	WE_DATA		*we_data;

	we_data = data;
	g_return_if_fail(we_data != NULL);

	switch(option)
	{
		default:
		case SEC_OPTION_NONE:
			eh_gconf_client_set_int(we_data, "we_cipher", 
					IW_AUTH_CIPHER_NONE);
			eh_gconf_client_unset(we_data, "wep_auth_algorithm");
			eh_gconf_client_unset(we_data, "wpa_psk_wpa_version");
			break;
		case SEC_OPTION_WEP64:
			eh_gconf_client_set_int(we_data, "we_cipher", 
					IW_AUTH_CIPHER_WEP40);
			eh_gconf_client_set_int(we_data, "wep_auth_algorithm", 
					IW_AUTH_ALG_OPEN_SYSTEM);
			eh_gconf_client_unset(we_data, "wpa_psk_wpa_version");
			break;
		case SEC_OPTION_WEP128:
			eh_gconf_client_set_int(we_data, "we_cipher", 
					IW_AUTH_CIPHER_WEP104);
			eh_gconf_client_set_int(we_data, "wep_auth_algorithm", 
					IW_AUTH_ALG_OPEN_SYSTEM);
			eh_gconf_client_unset(we_data, "wpa_psk_wpa_version");
			break;
		case SEC_OPTION_WPA_PERSONAL:
			eh_gconf_client_set_int(we_data, "we_cipher", 0); // auto
			eh_gconf_client_set_int(we_data, "wpa_psk_wpa_version",
					IW_AUTH_WPA_VERSION_WPA);
			eh_gconf_client_unset(we_data, "wep_auth_algorithm");
			break;
		case SEC_OPTION_WPA2_PERSONAL:
			eh_gconf_client_set_int(we_data, "we_cipher", 0); // auto
			eh_gconf_client_set_int(we_data, "wpa_psk_wpa_version",
					IW_AUTH_WPA_VERSION_WPA2);
			eh_gconf_client_unset(we_data, "wep_auth_algorithm");
			break;
		case SEC_OPTION_WPA_ENTERPRISE:
			eh_gconf_client_set_int(we_data, "we_cipher", 
					NM_AUTH_TYPE_WPA_EAP);
			eh_gconf_client_set_int(we_data, "wpa_eap_wpa_version",
					IW_AUTH_WPA_VERSION_WPA);
			eh_gconf_client_unset(we_data, "wep_auth_algorithm");
			break;
		case SEC_OPTION_WPA2_ENTERPRISE:
			eh_gconf_client_set_int(we_data, "we_cipher", 
					NM_AUTH_TYPE_WPA_EAP);
			eh_gconf_client_set_int(we_data, "wpa_eap_wpa_version",
					IW_AUTH_WPA_VERSION_WPA2);
			eh_gconf_client_unset(we_data, "wep_auth_algorithm");
			break;
		case SEC_OPTION_LEAP:
			eh_gconf_client_set_int(we_data, "we_cipher", NM_AUTH_TYPE_LEAP);
			eh_gconf_client_unset(we_data, "wep_auth_algorithm");
			break;
	}
}

static void
update_security_widget (gint option, gpointer data)
{
	WE_DATA		*we_data;
	GtkWidget	*vbox;
	GList		*children;
	GtkWidget	*childWidget;

	we_data = data;
	g_return_if_fail(we_data != NULL);

	vbox = GTK_WIDGET( glade_xml_get_widget(we_data->editor_xml, 
				"swapout_vbox"));
	g_return_if_fail(vbox != NULL);

	// loop through and remove all of the existing children
	for(children = gtk_container_get_children(GTK_CONTAINER(vbox));
			children;
			children = g_list_next(children))
	{
		GtkWidget *child = GTK_WIDGET(children->data);
		gtk_container_remove(GTK_CONTAINER(vbox), child);
	}

	if(we_data->sub_xml != NULL)
	{
		g_object_unref(we_data->sub_xml);
		we_data->sub_xml = NULL;
	}

	switch(option)
	{
		default:
		case SEC_OPTION_NONE:
			childWidget = NULL;
			break;
		case SEC_OPTION_WEP64:
		case SEC_OPTION_WEP128:
			childWidget = get_wep_widget(we_data);
			break;
		case SEC_OPTION_WPA_PERSONAL:
		case SEC_OPTION_WPA2_PERSONAL:
			childWidget = get_wpa_personal_widget(we_data);
			break;
		case SEC_OPTION_WPA_ENTERPRISE:
		case SEC_OPTION_WPA2_ENTERPRISE:
			childWidget = get_wpa_enterprise_widget(we_data);
			break;
		case SEC_OPTION_LEAP:
			childWidget = get_leap_widget(we_data);
			break;
	}

	if(childWidget != NULL)
		gtk_container_add(GTK_CONTAINER(vbox), childWidget);
}

static void
security_combo_changed (GtkWidget *combo, gpointer data)
{
	int active;

	active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	change_security_settings (active, data);
	update_security_widget (active, data);
}

static void
time_stamp_changed (GnomeDateEdit *dateedit, gpointer user_data)
{
	WE_DATA				*we_data;
	GtkTreeSelection	*selection;
	gchar				*key = NULL;
	GtkTreeIter			iter;
	GtkTreeModel		*model;
	gchar				*gconf_path;
	GError 				*err = NULL;
	time_t				t;

	we_data = user_data;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (we_data->treeview));
	if(!gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

	gtk_tree_model_get (model, &iter, WNTV_DATA_COLUMN, &gconf_path, -1);
	if(gconf_path == NULL)
		return;

	key = g_strdup_printf("%s/timestamp", gconf_path);

	t = gnome_date_edit_get_time(dateedit);

	gconf_client_set_int(we_data->gconf_client, key, t, NULL);

	gconf_client_suggest_sync(we_data->gconf_client, &err);

	g_free(key);
}

static void
connect_signal_handlers (WE_DATA *we_data, gboolean connect)
{
	if(g_signal_handler_is_connected(G_OBJECT(we_data->essid_entry), 
				we_data->essid_shid))
	{
		g_signal_handler_disconnect(G_OBJECT(we_data->essid_entry), 
				we_data->essid_shid);
		we_data->essid_shid = 0;
	}
	if(g_signal_handler_is_connected(G_OBJECT(we_data->essid_entry), 
				we_data->essid_focus_shid))
	{
		g_signal_handler_disconnect(G_OBJECT(we_data->essid_entry), 
				we_data->essid_focus_shid);
		we_data->essid_focus_shid = 0;
	}
	if(g_signal_handler_is_connected(G_OBJECT(we_data->security_combo), 
				we_data->combo_shid))
	{
		g_signal_handler_disconnect(G_OBJECT(we_data->security_combo), 
				we_data->combo_shid);
		we_data->essid_shid = 0;
	}
	if(g_signal_handler_is_connected(G_OBJECT(we_data->stamp_editor), 
				we_data->stamp_date_shid))
	{
		g_signal_handler_disconnect(G_OBJECT(we_data->stamp_editor), 
				we_data->stamp_date_shid);
		we_data->stamp_date_shid = 0;
	}
	if(g_signal_handler_is_connected(G_OBJECT(we_data->stamp_editor), 
				we_data->stamp_time_shid))
	{
		g_signal_handler_disconnect(G_OBJECT(we_data->stamp_editor), 
				we_data->stamp_time_shid);
		we_data->stamp_time_shid = 0;
	}
	if(connect)
	{
		we_data->essid_shid = g_signal_connect(
				G_OBJECT(we_data->essid_entry), "activate", 
				GTK_SIGNAL_FUNC (essid_entry_changed), we_data);
		we_data->essid_focus_shid = g_signal_connect(
				G_OBJECT(we_data->essid_entry), "focus-out-event", 
				GTK_SIGNAL_FUNC (essid_entry_focus_lost), we_data);
		we_data->combo_shid = g_signal_connect(
				G_OBJECT(we_data->security_combo), "changed", 
				GTK_SIGNAL_FUNC (security_combo_changed), we_data);
		we_data->stamp_date_shid = g_signal_connect(
				G_OBJECT(we_data->stamp_editor), "date-changed", 
				GTK_SIGNAL_FUNC (time_stamp_changed), we_data);
		we_data->stamp_time_shid = g_signal_connect(
				G_OBJECT(we_data->stamp_editor), "time-changed", 
				GTK_SIGNAL_FUNC (time_stamp_changed), we_data);
	}
}

static void
network_remove_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	WE_DATA		*we_data;
	GtkTreeSelection	*selection;
	GError				*err = NULL;
	GtkTreeIter			iter;
	GtkTreeIter			childIter;
	GtkTreeModel		*model;
	GtkListStore		*store;
	gchar				*gconf_path;
	GSList				*gconf_entries;

	we_data = (WE_DATA *)user_data;

	selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (we_data->treeview));

	if(!gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

	gtk_tree_model_get (model, &iter, WNTV_DATA_COLUMN, &gconf_path, -1);
	if(gconf_path == NULL)
		return;

	err = NULL;
	gconf_entries = gconf_client_all_entries(we_data->gconf_client,
			gconf_path,
			&err);

	while (gconf_entries)
	{
		GConfEntry	*gconf_entry;

		gconf_entry = gconf_entries->data;

		gconf_client_unset(we_data->gconf_client,
				gconf_entry->key,
				&err);

		gconf_entry_free(gconf_entry);
		gconf_entries = g_slist_delete_link (gconf_entries, gconf_entries);
	}

	gconf_client_suggest_sync(we_data->gconf_client, &err);

	// now update the data in the treeview
	store = GTK_LIST_STORE(gtk_tree_model_filter_get_model(
				GTK_TREE_MODEL_FILTER(model)));

	gtk_tree_model_filter_convert_iter_to_child_iter(
			GTK_TREE_MODEL_FILTER(model),
			&childIter,
			&iter);

	gtk_list_store_remove ( store, &childIter);

}

static gint
sort_networks (GtkTreeModel *model,
			   GtkTreeIter *a,
			   GtkTreeIter *b,
			   gpointer user_data)
{
	char *aa = NULL;
	char *bb = NULL;
	gint result;

	gtk_tree_model_get (model, a, WNTV_DISPLAY_COLUMN, &aa, -1);
	gtk_tree_model_get (model, b, WNTV_DISPLAY_COLUMN, &bb, -1);

	if (aa == NULL)
		result = -1;
	else if (bb == NULL)
		result = 1;
	else
		result = strcmp (aa, bb);

	g_free (aa);
	g_free (bb);

	return result;
}

static void
add_networks (GtkListStore *store, GConfClient *gconf_client, const char *gconf_prefix, GdkPixbuf *pixbuf)
{
	GSList *gconf_dirs;

	gconf_dirs = gconf_client_all_dirs (gconf_client, gconf_prefix, NULL);
	while (gconf_dirs) {
		gchar *gconf_entry_dir = gconf_dirs->data;
		gchar *key;
		gchar *ssid;

		key = g_strdup_printf ("%s/essid", gconf_entry_dir);
		ssid = gconf_client_get_string (gconf_client, key, NULL);
		g_free (key);

		if (ssid) {
			GtkTreeIter iter;

			gtk_list_store_append (store, &iter);

			gtk_list_store_set (store, &iter,
							WNTV_DISPLAY_COLUMN, ssid,
							WNTV_PIXBUF_COLUMN, pixbuf,
							WNTV_DATA_COLUMN, gconf_entry_dir,
							-1);

			g_free (ssid);
		}

		gconf_dirs = g_slist_delete_link (gconf_dirs, gconf_dirs);
	}
}

static void
populate_model (WE_DATA *we_data, GtkListStore *store)
{
	GdkPixbuf    *pixbuf;
	GtkIconTheme *theme;

	if (gtk_widget_has_screen (we_data->treeview))
		theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (we_data->treeview));
	else
		theme = gtk_icon_theme_get_default ();

	/* Wireless */
	pixbuf = gtk_icon_theme_load_icon (theme, "nm-device-wireless", WE_ICON_SIZE, 0, NULL);
	add_networks (store, we_data->gconf_client, WE_GCONF_WIRELESS_PATH, pixbuf);
	g_object_unref (pixbuf);

	/* Wired */
	pixbuf = gtk_icon_theme_load_icon (theme, "nm-device-wired", WE_ICON_SIZE, 0, NULL);
	add_networks (store, we_data->gconf_client, WE_GCONF_WIRED_PATH, pixbuf);
	g_object_unref (pixbuf);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
								   WNTV_DISPLAY_COLUMN,
								   GTK_SORT_ASCENDING);
}

static void
setup_dialog (WE_DATA *we_data)
{
	GtkListStore      *store;
	GtkTreeViewColumn *column;
	GtkCellRenderer   *renderer;
	GtkTreeModel      *filter;
	GtkTreeIter			iter;

	store = gtk_list_store_new (WNTV_NUM_COLUMNS,
						   G_TYPE_STRING,
						   GDK_TYPE_PIXBUF,
						   G_TYPE_POINTER);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
							   WNTV_DISPLAY_COLUMN,
							   sort_networks,
							   NULL,
							   NULL);

	populate_model (we_data, store);

	filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (store), NULL);

	gtk_tree_view_set_model (GTK_TREE_VIEW (we_data->treeview), filter);

	g_object_unref (store);
	g_object_unref (filter);

	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes ("Image", renderer, "pixbuf", WNTV_PIXBUF_COLUMN, NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (we_data->treeview), column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer, "markup", WNTV_DISPLAY_COLUMN, NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (we_data->treeview), column);
	gtk_tree_view_column_set_sort_column_id (column, 0);

	store = gtk_list_store_new (1, G_TYPE_STRING);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
			0, _("None"), 
			-1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
			0, _("WEP 64-bit"), 
			-1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
			0, _("WEP 128-bit"), 
			-1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
			0, _("WPA Personal"), 
			-1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
			0, _("WPA2 Personal"), 
			-1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
			0, _("WPA Enterprise"), 
			-1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
			0, _("WPA2 Enterprise"), 
			-1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 
			0, _("LEAP"), 
			-1);

	gtk_combo_box_set_model (GTK_COMBO_BOX(we_data->security_combo), GTK_TREE_MODEL (store));
	g_object_unref (store);
}

static gboolean
setup_dialog_idle (gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;

	setup_dialog (we_data);

	return FALSE;
}

static gboolean
quit_editor (WE_DATA *we_data)
{
	if (we_data) {
		if (we_data->editor_xml)
			g_object_unref (we_data->editor_xml);
		if (we_data->gconf_client)
			g_object_unref (we_data->gconf_client);
		if (we_data->glade_file)
			g_free (we_data->glade_file);

		g_free (we_data);
	}

	gtk_exit (0);
	return TRUE;
}

static void
about_dialog_activate_link_cb (GtkAboutDialog *about,
						 const gchar *url,
						 gpointer data)
{
	gnome_url_show (url, NULL);
}

static void
about_editor_cb (GtkMenuItem *menuitem, gpointer user_data)
{
	static const gchar *authors[] =
	{
		"Calvin Gaisford <cgaisford@novell.com>",
		"\nNetworkManager Developers:",
		"Christopher Aillon <caillon@redhat.com>",
		"Jonathan Blandford <jrb@redhat.com>",
		"John Palmieri <johnp@redhat.com>",
		"Ray Strode <rstrode@redhat.com>",
		"Colin Walters <walters@redhat.com>",
		"Dan Williams <dcbw@redhat.com>",
		"David Zeuthen <davidz@redhat.com>",
		"Bill Moss <bmoss@clemson.edu>",
		"Tom Parker",
		"j@bootlab.org",
		"Peter Jones <pjones@redhat.com>",
		"Robert Love <rml@novell.com>",
		"Tim Niemueller <tim@niemueller.de>",
		NULL
	};

	static const gchar *artists[] =
	{
		NULL
	};

	static const gchar *documenters[] =
	{
		NULL
	};

#if !GTK_CHECK_VERSION(2,6,0)
	GdkPixbuf	*pixbuf;
	char		*file;
	GtkWidget	*about_dialog;

	/* GTK 2.4 and earlier, have to use libgnome for about dialog */
	file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, 
			"gnome-networktool.png", FALSE, NULL);
	pixbuf = gdk_pixbuf_new_from_file (file, NULL);
	g_free (file);

	about_dialog = gnome_about_new (_("NetworkManager Editor"),
			VERSION,
			_("Copyright \xc2\xa9 2007 Novell, Inc."),
			_("Editor for managing your wireless networks"),
			authors,
			documenters,
			_("translator-credits"),
			pixbuf);
	g_object_unref (pixbuf);

	gtk_window_set_screen (GTK_WINDOW (about_dialog), 
			gtk_widget_get_screen (GTK_WIDGET (applet)));
	g_signal_connect (about_dialog, "destroy", 
			G_CALLBACK (gtk_widget_destroyed), &about_dialog);
	gtk_widget_show (about_dialog);

#else
	static gboolean been_here = FALSE;
	if (!been_here)
	{
		been_here = TRUE;
		gtk_about_dialog_set_url_hook (about_dialog_activate_link_cb, 
				NULL, NULL);
	}

	/* GTK 2.6 and later code */
	gtk_show_about_dialog (NULL,
			"name", _("NetworkManager Editor"),
			"version", VERSION,
			"copyright", _("Copyright \xc2\xa9 2007 Novell, Inc."),
			"comments", _("Editor for managing your wireless networks."),
			"website", "http://www.gnome.org/projects/NetworkManager/",
			"authors", authors,
			"artists", artists,
			"documenters", documenters,
			"translator-credits", _("translator-credits"),
			"logo-icon-name", GTK_STOCK_NETWORK,
			NULL);
#endif
}

static void
set_security_combo (gint option, gpointer data)
{
	WE_DATA *we_data = (WE_DATA *) data;
	GtkWidget	*combo;

	combo = glade_xml_get_widget (we_data->editor_xml, "security_combo");
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), option);

	if (we_data->cur_gconf_dir && !strncmp (we_data->cur_gconf_dir, WE_GCONF_WIRELESS_PATH, strlen (WE_GCONF_WIRELESS_PATH)))
		gtk_widget_set_sensitive (combo, TRUE);
	else
		gtk_widget_set_sensitive (combo, FALSE);

	update_security_widget (option, data);
}

static void
update_dialog_for_current_network (WE_DATA *we_data, gboolean enabled)
{
	GError				*err = NULL;
	gchar				*key = NULL;
	gchar				*value;
	GtkWidget			*widget;
	GSList				*values;
	gint				intValue;
	gint				we_cipher;
	gint				wpa_version;

	if(!enabled)
	{
		gtk_widget_set_sensitive(we_data->remove_button, TRUE);
		gtk_entry_set_text(GTK_ENTRY(we_data->essid_entry), "");

		widget = glade_xml_get_widget(we_data->editor_xml, "bssids_entry");
		gtk_entry_set_text(GTK_ENTRY(widget), "");

		widget = glade_xml_get_widget(we_data->editor_xml, "lastused_dateedit");
		gnome_date_edit_set_time(GNOME_DATE_EDIT(widget), 0);
		gtk_widget_set_sensitive(widget, FALSE);

		widget = glade_xml_get_widget(we_data->editor_xml, "bssids_modify");
		gtk_widget_set_sensitive(widget, FALSE);

		set_security_combo(SEC_OPTION_NONE, we_data);
		widget = glade_xml_get_widget(we_data->editor_xml, "security_combo");
		gtk_widget_set_sensitive(widget, FALSE);
		return;	
	}

	// ============ COMMON PROPERTIES ============
	// Get the essid
	value = eh_gconf_client_get_string(we_data, "essid");
	if(err == 0)
	{
		if(we_data->essid_value != NULL)
		{
			g_free(we_data->essid_value);
			we_data->essid_value = NULL;
		}
		gtk_entry_set_text(GTK_ENTRY(we_data->essid_entry), value);
		we_data->essid_value = g_strdup(value);
		g_free(value);
	}

	// Get the bssids
	widget = glade_xml_get_widget(we_data->editor_xml,
			"bssids_entry");

	key = g_strdup_printf("%s/bssids", we_data->cur_gconf_dir);
	values = gconf_client_get_list(we_data->gconf_client, key, 
			GCONF_VALUE_STRING, &err);
	if(err == 0)
	{
		gchar	*strvalue = NULL; 

		while(values)
		{
			if(strvalue == NULL)
				strvalue = g_strdup(values->data);
			else
			{
				gchar *tmpstr;

				tmpstr = strvalue;
				strvalue = g_strdup_printf("%s, %s", tmpstr, (char *) values->data);
				g_free(tmpstr);
			}

			g_free(values->data);

			values = g_slist_delete_link (values, values);
		}

		if(strvalue != NULL)
		{
			gtk_entry_set_text(GTK_ENTRY(widget), strvalue);
			g_free(strvalue);
		}
		else
			gtk_entry_set_text(GTK_ENTRY(widget), "");
	}
	else
		gtk_entry_set_text(GTK_ENTRY(widget), "");

	g_free(key);


	// set the Calendar editor
	intValue = eh_gconf_client_get_int(we_data, "timestamp");
	{
		time_t t;

		t = intValue;

		widget = glade_xml_get_widget(we_data->editor_xml, 
				"lastused_dateedit");

		gnome_date_edit_set_time(GNOME_DATE_EDIT(widget), t);
		gtk_widget_set_sensitive(widget, TRUE);
	}

	widget = glade_xml_get_widget(we_data->editor_xml, "bssids_modify");
	gtk_widget_set_sensitive(widget, TRUE);


	// ============ GET CIPHER PROPERTY AND SWITCH ============
	// Get the cipher
	we_cipher = eh_gconf_client_get_int(we_data, "we_cipher");

	switch(we_cipher)
	{
		default:
		case IW_AUTH_CIPHER_NONE:		// no security
			set_security_combo(SEC_OPTION_NONE, we_data);
			break;
		case IW_AUTH_CIPHER_WEP40:		// WEP 64bit
			set_security_combo(SEC_OPTION_WEP64, we_data);
			break;
		case IW_AUTH_CIPHER_WEP104:		// WEP 128bit
			set_security_combo(SEC_OPTION_WEP128, we_data);
			break;
		case NM_AUTH_TYPE_WPA_PSK_AUTO:	// WPA or WPA2 PERSONAL
		case IW_AUTH_CIPHER_TKIP:
		case IW_AUTH_CIPHER_CCMP:
			{
				wpa_version = eh_gconf_client_get_int(we_data, 
						"wpa_psk_wpa_version");
				switch(wpa_version)
				{
					case IW_AUTH_WPA_VERSION_WPA:
						set_security_combo(SEC_OPTION_WPA_PERSONAL, we_data);
						break;
					default:
					case IW_AUTH_WPA_VERSION_WPA2:
						set_security_combo(SEC_OPTION_WPA2_PERSONAL, we_data);
						break;
				}
				break;
			}
		case NM_AUTH_TYPE_WPA_EAP:		// WPA or WPA2 Enterprise
			{
				wpa_version = eh_gconf_client_get_int(we_data, 
						"wpa_eap_wpa_version");
				switch(wpa_version)
				{
					case IW_AUTH_WPA_VERSION_WPA:
						set_security_combo(SEC_OPTION_WPA_ENTERPRISE, we_data);
						break;
					default:
					case IW_AUTH_WPA_VERSION_WPA2:
						set_security_combo(SEC_OPTION_WPA2_ENTERPRISE, we_data);
						break;
				}
				break;
			}
		case NM_AUTH_TYPE_LEAP:
			set_security_combo(SEC_OPTION_LEAP, we_data);
			break;
	}
}

static void
network_selection_changed_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel		*model;
	GtkTreeIter			iter;
	WE_DATA				*we_data;

	we_data = data;

	if(!gtk_tree_selection_get_selected (selection, &model, &iter))
	{
		update_dialog_for_current_network(we_data, FALSE);
		return;
	}

	gtk_tree_model_get (model, &iter, WNTV_DATA_COLUMN, 
			&(we_data->cur_gconf_dir), -1);

	if(we_data->cur_gconf_dir != NULL)
	{
		connect_signal_handlers(we_data, FALSE);
		update_dialog_for_current_network(we_data, TRUE);
		connect_signal_handlers(we_data, TRUE);
	}
	else
		update_dialog_for_current_network(we_data, FALSE);
}

static void
bssids_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	WE_DATA				*we_data;
	GtkTreeSelection	*selection;
	gchar				*key = NULL;
	GtkTreeIter			iter;
	GtkTreeModel		*model;
	gchar				*gconf_path;
	GSList				*values;
	GtkWidget			*widget;
	GError 				*err = NULL;

	we_data = user_data;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (we_data->treeview));
	if(!gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

	gtk_tree_model_get (model, &iter, WNTV_DATA_COLUMN, &gconf_path, -1);
	if(gconf_path == NULL)
		return;

	key = g_strdup_printf("%s/bssids", gconf_path);

	widget = glade_xml_get_widget(we_data->editor_xml,
			"bssids_entry");

	if(run_bssid_editor(we_data->glade_file, we_data->window, key))
	{
		values = gconf_client_get_list(we_data->gconf_client, key, 
				GCONF_VALUE_STRING, &err);

		if(err == 0)
		{
			gchar	*strvalue = NULL; 

			while(values)
			{
				if(strvalue == NULL)
					strvalue = g_strdup(values->data);
				else
				{
					gchar *tmpstr;

					tmpstr = strvalue;
					strvalue = g_strdup_printf("%s, %s", tmpstr, (char *) values->data);
					g_free(tmpstr);
				}

				g_free(values->data);

				values = g_slist_delete_link (values, values);
			}

			if(strvalue != NULL)
			{
				gtk_entry_set_text(GTK_ENTRY(widget), strvalue);
				g_free(strvalue);
			}
			else
				gtk_entry_set_text(GTK_ENTRY(widget), "");
		}
		else
			gtk_entry_set_text(GTK_ENTRY(widget), "");
	}

	g_free(key);
}

static WE_DATA *
get_editor (void)
{
	WE_DATA *we_data;
	GtkTreeSelection *select;
	GtkWidget *widget;

	we_data = g_new0 (WE_DATA, 1);
	
	we_data->glade_file = g_build_filename (GLADEDIR, "applet.glade", NULL);
	if (!g_file_test (we_data->glade_file, G_FILE_TEST_IS_REGULAR)) {
		g_print("Error loading glade file!\n");
		g_free (we_data->glade_file);
		return NULL;
	}

	we_data->editor_xml = glade_xml_new (we_data->glade_file, 
			"wireless_editor", NULL);

	we_data->window = glade_xml_get_widget (we_data->editor_xml, 
			"wireless_editor");

	we_data->gconf_client = gconf_client_get_default();

	g_signal_connect_swapped (we_data->window, "delete_event", 
			G_CALLBACK (quit_editor), we_data);

	widget = glade_xml_get_widget(we_data->editor_xml, "quit_nm_editor");

	g_signal_connect_swapped (widget, "activate", 
			G_CALLBACK (quit_editor), we_data);


	widget = glade_xml_get_widget(we_data->editor_xml, "about_nm_editor");

	g_signal_connect_swapped (widget, "activate", 
			G_CALLBACK (about_editor_cb), we_data->window);

	we_data->treeview = glade_xml_get_widget(we_data->editor_xml, 
			"wireless_treeview");

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (we_data->treeview));
	gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

	g_signal_connect (G_OBJECT (select), "changed",
			G_CALLBACK (network_selection_changed_cb), we_data);

	we_data->essid_entry = glade_xml_get_widget(we_data->editor_xml, 
			"essid_entry");
	we_data->security_combo = glade_xml_get_widget(we_data->editor_xml, 
			"security_combo");
	we_data->stamp_editor = glade_xml_get_widget(we_data->editor_xml, 
			"lastused_dateedit");
	we_data->remove_button = glade_xml_get_widget(we_data->editor_xml, 
			"remove_button");

	widget = glade_xml_get_widget(we_data->editor_xml, "bssids_modify");

	g_signal_connect (G_OBJECT (widget), "clicked",
			G_CALLBACK (bssids_button_clicked_cb), we_data);

	g_signal_connect (G_OBJECT (we_data->remove_button), "clicked",
			G_CALLBACK (network_remove_button_clicked_cb), we_data);

	g_idle_add (setup_dialog_idle, we_data);

	return we_data;
}

int
main (int argc, char **argv)
{
	WE_DATA *we_data;

	gnome_program_init (PACKAGE, VERSION, LIBGNOMEUI_MODULE, argc, argv,
					GNOME_PARAM_NONE, GNOME_PARAM_NONE);

	we_data = get_editor ();
	if (we_data) {
		gtk_widget_show_all (we_data->window);
		gtk_main ();
		return 0; 
	}

	return -1; 
}
