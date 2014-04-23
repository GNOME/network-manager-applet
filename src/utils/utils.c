/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * (C) Copyright 2007 - 2011 Red Hat, Inc.
 */

#include <config.h>
#include <string.h>
#include <netinet/ether.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <nm-setting-connection.h>
#include <nm-utils.h>

#include "utils.h"

/*
 * utils_ether_addr_valid
 *
 * Compares an Ethernet address against known invalid addresses.
 *
 */
gboolean
utils_ether_addr_valid (const struct ether_addr *test_addr)
{
	guint8 invalid_addr1[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	guint8 invalid_addr2[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	guint8 invalid_addr3[ETH_ALEN] = {0x44, 0x44, 0x44, 0x44, 0x44, 0x44};
	guint8 invalid_addr4[ETH_ALEN] = {0x00, 0x30, 0xb4, 0x00, 0x00, 0x00}; /* prism54 dummy MAC */

	g_return_val_if_fail (test_addr != NULL, FALSE);

	/* Compare the AP address the card has with invalid ethernet MAC addresses. */
	if (!memcmp (test_addr->ether_addr_octet, &invalid_addr1, ETH_ALEN))
		return FALSE;

	if (!memcmp (test_addr->ether_addr_octet, &invalid_addr2, ETH_ALEN))
		return FALSE;

	if (!memcmp (test_addr->ether_addr_octet, &invalid_addr3, ETH_ALEN))
		return FALSE;

	if (!memcmp (test_addr->ether_addr_octet, &invalid_addr4, ETH_ALEN))
		return FALSE;

	if (test_addr->ether_addr_octet[0] & 1)			/* Multicast addresses */
		return FALSE;
	
	return TRUE;
}

char *
utils_hash_ap (const GByteArray *ssid,
               NM80211Mode mode,
               guint32 flags,
               guint32 wpa_flags,
               guint32 rsn_flags)
{
	unsigned char input[66];

	memset (&input[0], 0, sizeof (input));

	if (ssid)
		memcpy (input, ssid->data, ssid->len);

	if (mode == NM_802_11_MODE_INFRA)
		input[32] |= (1 << 0);
	else if (mode == NM_802_11_MODE_ADHOC)
		input[32] |= (1 << 1);
	else
		input[32] |= (1 << 2);

	/* Separate out no encryption, WEP-only, and WPA-capable */
	if (  !(flags & NM_802_11_AP_FLAGS_PRIVACY)
	    && (wpa_flags == NM_802_11_AP_SEC_NONE)
	    && (rsn_flags == NM_802_11_AP_SEC_NONE))
		input[32] |= (1 << 3);
	else if (   (flags & NM_802_11_AP_FLAGS_PRIVACY)
	         && (wpa_flags == NM_802_11_AP_SEC_NONE)
	         && (rsn_flags == NM_802_11_AP_SEC_NONE))
		input[32] |= (1 << 4);
	else if (   !(flags & NM_802_11_AP_FLAGS_PRIVACY)
	         &&  (wpa_flags != NM_802_11_AP_SEC_NONE)
	         &&  (rsn_flags != NM_802_11_AP_SEC_NONE))
		input[32] |= (1 << 5);
	else
		input[32] |= (1 << 6);

	/* duplicate it */
	memcpy (&input[33], &input[0], 32);
	return g_compute_checksum_for_data (G_CHECKSUM_MD5, input, sizeof (input));
}

typedef struct {
	const char *tag;
	const char *replacement;
} Tag;

static Tag escaped_tags[] = {
	{ "<center>", NULL },
	{ "</center>", NULL },
	{ "<p>", "\n" },
	{ "</p>", NULL },
	{ "<B>", "<b>" },
	{ "</B>", "</b>" },
	{ "<I>", "<i>" },
	{ "</I>", "</i>" },
	{ "<u>", "<u>" },
	{ "</u>", "</u>" },
	{ "&", "&amp;" },
	{ NULL, NULL }
};

char *
utils_escape_notify_message (const char *src)
{
	const char *p = src;
	GString *escaped;

	/* Filter the source text and get rid of some HTML tags since the
	 * notification spec only allows a subset of HTML.  Substitute
	 * HTML code for characters like & that are invalid in HTML.
	 */

	escaped = g_string_sized_new (strlen (src) + 5);
	while (*p) {
		Tag *t = &escaped_tags[0];
		gboolean found = FALSE;

		while (t->tag) {
			if (strncasecmp (p, t->tag, strlen (t->tag)) == 0) {
				p += strlen (t->tag);
				if (t->replacement)
					g_string_append (escaped, t->replacement);
				found = TRUE;
				break;
			}
			t++;
		}
		if (!found)
			g_string_append_c (escaped, *p++);
	}

	return g_string_free (escaped, FALSE);
}

char *
utils_create_mobile_connection_id (const char *provider, const char *plan_name)
{
	g_return_val_if_fail (provider != NULL, NULL);

	if (plan_name)
		return g_strdup_printf ("%s %s", provider, plan_name);

	/* The %s is a mobile provider name, eg "T-Mobile" */
	return g_strdup_printf (_("%s connection"), provider);
}

void
utils_show_error_dialog (const char *title,
                         const char *text1,
                         const char *text2,
                         gboolean modal,
                         GtkWindow *parent)
{
	GtkWidget *err_dialog;

	g_return_if_fail (text1 != NULL);

	err_dialog = gtk_message_dialog_new (parent,
	                                     GTK_DIALOG_DESTROY_WITH_PARENT,
	                                     GTK_MESSAGE_ERROR,
	                                     GTK_BUTTONS_CLOSE,
	                                     "%s",
	                                     text1);

	if (text2)
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (err_dialog), "%s", text2);
	if (title)
		gtk_window_set_title (GTK_WINDOW (err_dialog), title);

	if (modal) {
		gtk_dialog_run (GTK_DIALOG (err_dialog));
		gtk_widget_destroy (err_dialog);
	} else {
		g_signal_connect (err_dialog, "delete-event", G_CALLBACK (gtk_widget_destroy), NULL);
		g_signal_connect (err_dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);

		gtk_widget_show_all (err_dialog);
		gtk_window_present (GTK_WINDOW (err_dialog));
	}
}


gboolean
utils_char_is_ascii_print (char character)
{
	return g_ascii_isprint (character);
}

gboolean
utils_char_is_ascii_digit (char character)
{
	return g_ascii_isdigit (character);
}

gboolean
utils_char_is_ascii_ip4_address (char character)
{
	return g_ascii_isdigit (character) || character == '.';
}

gboolean
utils_char_is_ascii_ip6_address (char character)
{
	return g_ascii_isxdigit (character) || character == ':';
}

gboolean
utils_char_is_ascii_apn (char character)
{
	return g_ascii_isalnum (character)
	       || character == '.'
	       || character == '_'
	       || character == '-';
}

/**
 * Filters the characters from a text that was just input into GtkEditable.
 * Returns FALSE, if after filtering no characters were left. TRUE means,
 * that valid characters were added and the content of the GtkEditable changed.
 **/
gboolean
utils_filter_editable_on_insert_text (GtkEditable *editable,
                                      const gchar *text,
                                      gint length,
                                      gint *position,
                                      void *user_data,
                                      UtilsFilterGtkEditableFunc validate_character,
                                      gpointer block_func)
{
	int i, count = 0;
	gchar *result = g_new (gchar, length+1);

	for (i = 0; i < length; i++) {
		if (validate_character (text[i]))
			result[count++] = text[i];
	}
	result[count] = 0;

	if (count > 0) {
		if (block_func) {
			g_signal_handlers_block_by_func (G_OBJECT (editable),
			                                 G_CALLBACK (block_func),
			                                 user_data);
		}
		gtk_editable_insert_text (editable, result, count, position);
		if (block_func) {
			g_signal_handlers_unblock_by_func (G_OBJECT (editable),
			                                   G_CALLBACK (block_func),
			                                   user_data);
		}
	}
	g_signal_stop_emission_by_name (G_OBJECT (editable), "insert-text");

	g_free (result);

	return count > 0;
}

static void
change_password_storage_icon (GtkWidget *passwd_entry, int number)
{
	char *icon_name = "document-save";

	if (number == 1)
		icon_name = "document-save";
	else if (number == 2)
		icon_name = "document-save-as";

	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (passwd_entry), GTK_ENTRY_ICON_SECONDARY, icon_name);
}

typedef struct {
	NMConnection *connection;
	const char *setting_name;
	const char *password_flags_name;
	int item_number;
	GtkWidget *passwd_entry;
} PopupMenuItemInfo;

static void
popup_menu_item_info_destroy (gpointer data)
{
	g_slice_free (PopupMenuItemInfo, data);
}

static void
activate_menu_item_cb (GtkMenuItem *menuitem, gpointer user_data)
{
	PopupMenuItemInfo *info = (PopupMenuItemInfo *) user_data;
	NMSetting *setting;
	NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;

	/* Get current secret flags */
	setting = nm_connection_get_setting_by_name (info->connection, info->setting_name);
	if (setting)
		nm_setting_get_secret_flags (setting, info->password_flags_name, &secret_flags, NULL);

	/* Update password flags according to the password-storage popup menu */
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem))) {
		if (info->item_number == 1)
			secret_flags |= NM_SETTING_SECRET_FLAG_AGENT_OWNED;
		else
			secret_flags &= ~NM_SETTING_SECRET_FLAG_AGENT_OWNED;

		/* Update the secret flags */
		if (setting)
			nm_setting_set_secret_flags (setting, info->password_flags_name, secret_flags, NULL);

		/* Change icon */
		change_password_storage_icon (info->passwd_entry, info->item_number);
	}
}

static void
icon_release_cb (GtkEntry *entry,
                 GtkEntryIconPosition position,
                 GdkEventButton *event,
                 gpointer data)
{
	GtkMenu *menu = GTK_MENU (data);
	if (position == GTK_ENTRY_ICON_SECONDARY) {
		gtk_widget_show_all (GTK_WIDGET (data));
		gtk_menu_popup (menu, NULL, NULL, NULL, NULL,
		                event->button, event->time);
	}
}

#define PASSWORD_STORAGE_MENU_TAG "password-storage-menu"

/**
 * Add secondary icon and create popup menu for password entry.
 **/
void
utils_setup_password_storage (NMConnection *connection,
                              const char *setting_name,
                              GtkWidget *passwd_entry,
                              const char *password_flags_name)
{
	GtkWidget *popup_menu;
	GtkWidget *item1, *item2;
	GSList *group;
	PopupMenuItemInfo *info;
	NMSetting *setting;

	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (passwd_entry), GTK_ENTRY_ICON_SECONDARY, "document-save");
	popup_menu = gtk_menu_new ();
	g_object_set_data (G_OBJECT (popup_menu), PASSWORD_STORAGE_MENU_TAG, GUINT_TO_POINTER (TRUE));
	group = NULL;
	item1 = gtk_radio_menu_item_new_with_mnemonic (group, _("Store the password only for this _user"));
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item1));
	item2 = gtk_radio_menu_item_new_with_mnemonic (group, _("Store the password for _all users"));

	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item1);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item2);

	info = g_slice_new0 (PopupMenuItemInfo);
	info->connection = connection;
	info->setting_name = setting_name;
	info->password_flags_name = password_flags_name;
	info->item_number = 1;
	info->passwd_entry = passwd_entry;
	g_signal_connect_data (item1, "activate",
	                       G_CALLBACK (activate_menu_item_cb),
	                       info,
	                       (GClosureNotify) popup_menu_item_info_destroy, 0);

	info = g_slice_new0 (PopupMenuItemInfo);
	info->connection = connection;
	info->setting_name = setting_name;
	info->password_flags_name = password_flags_name;
	info->item_number = 2;
	info->passwd_entry = passwd_entry;
	g_signal_connect_data (item2, "activate",
	                       G_CALLBACK (activate_menu_item_cb),
	                       info,
	                       (GClosureNotify) popup_menu_item_info_destroy, 0);

	g_signal_connect (passwd_entry, "icon-release", G_CALLBACK (icon_release_cb), popup_menu);
	gtk_menu_attach_to_widget (GTK_MENU (popup_menu), passwd_entry, NULL);

	/* Initialize active item for password-storage popup menu */
	setting = nm_connection_get_setting_by_name (connection, setting_name);
	if (setting) {
		NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;
		nm_setting_get_secret_flags (setting, password_flags_name, &secret_flags, NULL);

		if (secret_flags & NM_SETTING_SECRET_FLAG_AGENT_OWNED)
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item1), TRUE);
		else {
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item2), TRUE);
			/* Use different icon for system-storage */
			change_password_storage_icon (passwd_entry, 2);
		}
	} else {
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item1), TRUE);
	}
}

/**
 * Updates secret flags and the storage popup menu.
 **/
void
utils_update_password_storage (NMSetting *setting,
                               NMSettingSecretFlags secret_flags,
                               GtkWidget *passwd_entry,
                               const char *password_flags_name)
{
	GList *menu_list, *iter;
	GtkWidget *menu = NULL;

	/* Update secret flags (WEP_KEY_FLAGS, PSK_FLAGS, ...) in the security setting */
	nm_setting_set_secret_flags (setting, password_flags_name, secret_flags, NULL);

	menu_list = gtk_menu_get_for_attach_widget (passwd_entry);
	for (iter = menu_list; iter; iter = g_list_next (iter)) {
		if (g_object_get_data (G_OBJECT (iter->data), PASSWORD_STORAGE_MENU_TAG)) {
			menu = iter->data;
			break;
		}
	}

	/* Update password-storage popup menu to reflect secret flags */
	if (menu) {
		GtkRadioMenuItem *item, *item_user, *item_system;
		GSList *group;

		/* radio menu group list contains the menu items in reverse order */
		item = (GtkRadioMenuItem *) gtk_menu_get_active (GTK_MENU (menu));
		group = gtk_radio_menu_item_get_group (item);
		item_system = group->data;
		item_user = group->next->data;

		if (secret_flags & NM_SETTING_SECRET_FLAG_AGENT_OWNED) {
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item_user), TRUE);
			change_password_storage_icon (passwd_entry, 1);
		} else {
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item_system), TRUE);
			change_password_storage_icon (passwd_entry, 2);
		}
	}
}

