/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
#include <gtk/gtk.h>

#include <nm-setting-connection.h>
#include <nm-utils.h>

#include "utils.h"

static char *ignored_words[] = {
	"Semiconductor",
	"Components",
	"Corporation",
	"Communications",
	"Company",
	"Corp.",
	"Corp",
	"Co.",
	"Inc.",
	"Inc",
	"Incorporated",
	"Ltd.",
	"Limited.",
	"Intel?",
	"chipset",
	"adapter",
	"[hex]",
	"NDIS",
	"Module",
	NULL
};

static char *ignored_phrases[] = {
	"Multiprotocol MAC/baseband processor",
	"Wireless LAN Controller",
	"Wireless LAN Adapter",
	"Wireless Adapter",
	"Network Connection",
	"Wireless Cardbus Adapter",
	"Wireless CardBus Adapter",
	"54 Mbps Wireless PC Card",
	"Wireless PC Card",
	"Wireless PC",
	"PC Card with XJACK(r) Antenna",
	"Wireless cardbus",
	"Wireless LAN PC Card",
	"Technology Group Ltd.",
	"Communication S.p.A.",
	"Business Mobile Networks BV",
	"Mobile Broadband Minicard Composite Device",
	"Mobile Communications AB",
	"(PC-Suite Mode)",
	NULL
};

static char *
fixup_desc_string (const char *desc)
{
	char *p, *temp;
	char **words, **item;
	GString *str;

	p = temp = g_strdup (desc);
	while (*p) {
		if (*p == '_' || *p == ',')
			*p = ' ';
		p++;
	}

	/* Attempt to shorten ID by ignoring certain phrases */
	for (item = ignored_phrases; *item; item++) {
		guint32 ignored_len = strlen (*item);

		p = strstr (temp, *item);
		if (p)
			memmove (p, p + ignored_len, strlen (p + ignored_len) + 1); /* +1 for the \0 */
	}

	/* Attmept to shorten ID by ignoring certain individual words */
	words = g_strsplit (temp, " ", 0);
	str = g_string_new_len (NULL, strlen (temp));
	g_free (temp);

	for (item = words; *item; item++) {
		int i = 0;
		gboolean ignore = FALSE;

		if (g_ascii_isspace (**item) || (**item == '\0'))
			continue;

		while (ignored_words[i] && !ignore) {
			if (!strcmp (*item, ignored_words[i]))
				ignore = TRUE;
			i++;
		}

		if (!ignore) {
			if (str->len)
				g_string_append_c (str, ' ');
			g_string_append (str, *item);
		}
	}
	g_strfreev (words);

	temp = str->str;
	g_string_free (str, FALSE);

	return temp;
}

#define DESC_TAG "description"

const char *
utils_get_device_description (NMDevice *device)
{
	char *description = NULL;
	const char *dev_product;
	const char *dev_vendor;
	char *product = NULL;
	char *vendor = NULL;
	GString *str;

	g_return_val_if_fail (device != NULL, NULL);

	description = g_object_get_data (G_OBJECT (device), DESC_TAG);
	if (description)
		return description;

	dev_product = nm_device_get_product (device);
	dev_vendor = nm_device_get_vendor (device);
	if (!dev_product || !dev_vendor)
		return NULL;

	product = fixup_desc_string (dev_product);
	vendor = fixup_desc_string (dev_vendor);

	str = g_string_new_len (NULL, strlen (vendor) + strlen (product) + 1);

	/* Another quick hack; if all of the fixed up vendor string
	 * is found in product, ignore the vendor.
	 */
	if (!strcasestr (product, vendor)) {
		g_string_append (str, vendor);
		g_string_append_c (str, ' ');
	}

	g_string_append (str, product);
	g_free (product);
	g_free (vendor);

	description = str->str;
	g_string_free (str, FALSE);

	g_object_set_data_full (G_OBJECT (device),
	                        "description", description,
	                        (GDestroyNotify) g_free);

	return description;
}

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

