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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <net/ethernet.h>
#include <netinet/ether.h>
#include <string.h>
#include <stdlib.h>

#include "ce-page.h"
#include "utils.h"

G_DEFINE_ABSTRACT_TYPE (CEPage, ce_page, G_TYPE_OBJECT)

enum {
	CHANGED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

gboolean
ce_page_validate (CEPage *self)
{
	if (CE_PAGE_GET_CLASS (self)->validate)
		return CE_PAGE_GET_CLASS (self)->validate (self);

	return TRUE;
}

void
ce_page_update_connection (CEPage *self, NMConnection *connection)
{
	CE_PAGE_GET_CLASS (self)->update_connection (self, connection);
}

void
ce_page_mac_to_entry (GByteArray *mac, GtkEntry *entry)
{
	struct ether_addr addr;
	char *str_addr;

	g_return_if_fail (entry != NULL);
	g_return_if_fail (GTK_IS_ENTRY (entry));

	if (!mac || !mac->len)
		return;

	memcpy (addr.ether_addr_octet, mac->data, ETH_ALEN);
	str_addr = utils_ether_ntop (&addr);
	gtk_entry_set_text (entry, str_addr);
	g_free (str_addr);
}

GByteArray *
ce_page_entry_to_mac (GtkEntry *entry, gboolean *invalid)
{
	struct ether_addr *ether;
	const char *temp;
	GByteArray *mac;

	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (GTK_IS_ENTRY (entry), NULL);

	if (invalid)
		g_return_val_if_fail (*invalid == FALSE, NULL);

	temp = gtk_entry_get_text (entry);
	if (!temp || !strlen (temp))
		return NULL;

	ether = ether_aton (temp);
	if (!ether || !utils_mac_valid (ether)) {
		if (invalid)
			*invalid = TRUE;
		return NULL;
	}

	mac = g_byte_array_sized_new (ETH_ALEN);
	g_byte_array_append (mac, (const guint8 *) ether->ether_addr_octet, ETH_ALEN);
	return mac;
}

static void
ce_page_init (CEPage *self)
{
	self->disposed = FALSE;
}

static void
dispose (GObject *object)
{
	CEPage *self = CE_PAGE (object);

	if (self->disposed)
		return;

	self->disposed = TRUE;

	if (self->page)
		g_object_unref (self->page);

	if (self->xml)
		g_object_unref (self->xml);

	G_OBJECT_CLASS (ce_page_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	CEPage *self = CE_PAGE (object);

	if (self->title)
		g_free (self->title);

	G_OBJECT_CLASS (ce_page_parent_class)->finalize (object);
}

GtkWidget *
ce_page_get_page (CEPage *self)
{
	g_return_val_if_fail (CE_IS_PAGE (self), NULL);

	return self->page;
}

const char *
ce_page_get_title (CEPage *self)
{
	g_return_val_if_fail (CE_IS_PAGE (self), NULL);

	return self->title;
}

void
ce_page_changed (CEPage *self)
{
	g_return_if_fail (CE_IS_PAGE (self));

	g_signal_emit (self, signals[CHANGED], 0);
}

static void
ce_page_class_init (CEPageClass *page_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (page_class);

	/* virtual methods */
	object_class->dispose     = dispose;
	object_class->finalize    = finalize;

	/* Signals */
	signals[CHANGED] = 
		g_signal_new ("changed",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (CEPageClass, changed),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__VOID,
					  G_TYPE_NONE, 0);
}
