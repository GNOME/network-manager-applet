/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
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
 * Copyright 2007 - 2015 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gudev/gudev.h>

#include <nm-device.h>
#include <nm-device-bt.h>

#include "nm-ui-utils.h"

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

#define VENDOR_TAG "nma_utils_get_device_vendor"
#define PRODUCT_TAG "nma_utils_get_device_product"
#define DESCRIPTION_TAG "nma_utils_get_device_description"

static void
get_description (NMDevice *device)
{
	char *description = NULL;
	const char *dev_product;
	const char *dev_vendor;
	char *product = NULL;
	char *vendor = NULL;
	GString *str;

	dev_product = nm_device_get_product (device);
	dev_vendor = nm_device_get_vendor (device);
	if (!dev_product || !dev_vendor) {
		g_object_set_data (G_OBJECT (device),
		                   DESCRIPTION_TAG,
		                   (char *) nm_device_get_iface (device));
		return;
	}

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

	description = g_string_free (str, FALSE);

	g_object_set_data_full (G_OBJECT (device),
	                        VENDOR_TAG, vendor,
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (device),
	                        PRODUCT_TAG, product,
	                        (GDestroyNotify) g_free);
	g_object_set_data_full (G_OBJECT (device),
	                        DESCRIPTION_TAG, description,
	                        (GDestroyNotify) g_free);
}

/**
 * nma_utils_get_device_vendor:
 * @device: an #NMDevice
 *
 * Gets a cleaned-up version of #NMDevice:vendor for @device. This
 * removes strings like "Inc." that would just take up unnecessary
 * space in the UI.
 *
 * Returns: a cleaned-up vendor string, or %NULL if the vendor is
 *   not known
 */
const char *
nma_utils_get_device_vendor (NMDevice *device)
{
	const char *vendor;

	g_return_val_if_fail (device != NULL, NULL);

	vendor = g_object_get_data (G_OBJECT (device), VENDOR_TAG);
	if (!vendor) {
		get_description (device);
		vendor = g_object_get_data (G_OBJECT (device), VENDOR_TAG);
	}

	return vendor;
}

/**
 * nma_utils_get_device_product:
 * @device: an #NMDevice
 *
 * Gets a cleaned-up version of #NMDevice:product for @device. This
 * removes strings like "Wireless LAN Adapter" that would just take up
 * unnecessary space in the UI.
 *
 * Returns: a cleaned-up product string, or %NULL if the product name
 *   is not known
 */
const char *
nma_utils_get_device_product (NMDevice *device)
{
	const char *product;

	g_return_val_if_fail (device != NULL, NULL);

	product = g_object_get_data (G_OBJECT (device), PRODUCT_TAG);
	if (!product) {
		get_description (device);
		product = g_object_get_data (G_OBJECT (device), PRODUCT_TAG);
	}

	return product;
}

/**
 * nma_utils_get_device_description:
 * @device: an #NMDevice
 *
 * Gets a description of @device, incorporating the results of
 * nma_utils_get_device_vendor() and
 * nma_utils_get_device_product().
 *
 * Returns: a description of @device. If either the vendor or the
 *   product name is unknown, this returns the interface name.
 */
const char *
nma_utils_get_device_description (NMDevice *device)
{
	const char *description;

	g_return_val_if_fail (device != NULL, NULL);

	description = g_object_get_data (G_OBJECT (device), DESCRIPTION_TAG);
	if (!description) {
		get_description (device);
		description = g_object_get_data (G_OBJECT (device), DESCRIPTION_TAG);
	}

	return description;
}

static gboolean
find_duplicates (char     **names,
                 gboolean  *duplicates,
                 int        num_devices)
{
	int i, j;
	gboolean found_any = FALSE;

	memset (duplicates, 0, num_devices * sizeof (gboolean));
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i])
			continue;
		for (j = i + 1; j < num_devices; j++) {
			if (duplicates[j])
				continue;
			if (!strcmp (names[i], names[j]))
				duplicates[i] = duplicates[j] = found_any = TRUE;
		}
	}

	return found_any;
}

/**
 * nma_utils_get_device_generic_type_name:
 * @device: an #NMDevice
 *
 * Gets a "generic" name for the type of @device.
 *
 * Returns: @device's generic type name
 */
const char *
nma_utils_get_device_generic_type_name (NMDevice *device)
{
	switch (nm_device_get_device_type (device)) {
	case NM_DEVICE_TYPE_ETHERNET:
	case NM_DEVICE_TYPE_INFINIBAND:
		return _("Wired");
	default:
		return nma_utils_get_device_type_name (device);
	}
}

/**
 * nma_utils_get_device_type_name:
 * @device: an #NMDevice
 *
 * Gets a specific name for the type of @device.
 *
 * Returns: @device's generic type name
 */
const char *
nma_utils_get_device_type_name (NMDevice *device)
{
	switch (nm_device_get_device_type (device)) {
	case NM_DEVICE_TYPE_ETHERNET:
		return _("Ethernet");
	case NM_DEVICE_TYPE_WIFI:
		return _("Wi-Fi");
	case NM_DEVICE_TYPE_BT:
		return _("Bluetooth");
	case NM_DEVICE_TYPE_OLPC_MESH:
		return _("OLPC Mesh");
	case NM_DEVICE_TYPE_WIMAX:
		return _("WiMAX");
	case NM_DEVICE_TYPE_MODEM:
		return _("Mobile Broadband");
	case NM_DEVICE_TYPE_INFINIBAND:
		return _("InfiniBand");
	case NM_DEVICE_TYPE_BOND:
		return _("Bond");
	case NM_DEVICE_TYPE_TEAM:
		return _("Team");
	case NM_DEVICE_TYPE_BRIDGE:
		return _("Bridge");
	case NM_DEVICE_TYPE_VLAN:
		return _("VLAN");
	case NM_DEVICE_TYPE_ADSL:
		return _("ADSL");
	default:
		return _("Unknown");
	}
}

static char *
get_device_type_name_with_iface (NMDevice *device)
{
	const char *type_name = nma_utils_get_device_type_name (device);

	switch (nm_device_get_device_type (device)) {
	case NM_DEVICE_TYPE_BOND:
	case NM_DEVICE_TYPE_TEAM:
	case NM_DEVICE_TYPE_BRIDGE:
	case NM_DEVICE_TYPE_VLAN:
		return g_strdup_printf ("%s (%s)", type_name, nm_device_get_iface (device));
	default:
		return g_strdup (type_name);
	}
}

static char *
get_device_generic_type_name_with_iface (NMDevice *device)
{
	switch (nm_device_get_device_type (device)) {
	case NM_DEVICE_TYPE_ETHERNET:
	case NM_DEVICE_TYPE_INFINIBAND:
		return g_strdup (_("Wired"));
	default:
		return get_device_type_name_with_iface (device);
	}
}

#define BUS_TAG "nm-ui-utils.c:get_bus_name"

static const char *
get_bus_name (GUdevClient *uclient, NMDevice *device)
{
	GUdevDevice *udevice;
	const char *ifname, *bus;
	char *display_bus;

	bus = g_object_get_data (G_OBJECT (device), BUS_TAG);
	if (bus) {
		if (*bus)
			return bus;
		else
			return NULL;
	}

	ifname = nm_device_get_iface (device);
	if (!ifname)
		return NULL;

	udevice = g_udev_client_query_by_subsystem_and_name (uclient, "net", ifname);
	if (!udevice)
		udevice = g_udev_client_query_by_subsystem_and_name (uclient, "tty", ifname);
	if (!udevice)
		return NULL;

	bus = g_udev_device_get_property (udevice, "ID_BUS");
	if (!g_strcmp0 (bus, "pci"))
		display_bus = g_strdup (_("PCI"));
	else if (!g_strcmp0 (bus, "usb"))
		display_bus = g_strdup (_("USB"));
	else {
		/* Use "" instead of NULL so we can tell later that we've
		 * already tried.
		 */
		display_bus = g_strdup ("");
	}

	g_object_set_data_full (G_OBJECT (device),
	                        BUS_TAG, display_bus,
	                        (GDestroyNotify) g_free);
	if (*display_bus)
		return display_bus;
	else
		return NULL;
}

/**
 * nma_utils_disambiguate_device_names:
 * @devices: (array length=num_devices): a set of #NMDevice
 * @num_devices: length of @devices
 *
 * Generates a list of short-ish unique presentation names for the
 * devices in @devices.
 *
 * Returns: (transfer full) (array zero-terminated=1): the device names
 */
char **
nma_utils_disambiguate_device_names (NMDevice **devices,
                                     int        num_devices)
{
	static const char *subsys[3] = { "net", "tty", NULL };
	GUdevClient *uclient;
	char **names;
	gboolean *duplicates;
	int i;

	names = g_new (char *, num_devices + 1);
	duplicates = g_new (gboolean, num_devices);

	/* Generic device name */
	for (i = 0; i < num_devices; i++)
		names[i] = get_device_generic_type_name_with_iface (devices[i]);
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* Try specific names (eg, "Ethernet" and "InfiniBand" rather
	 * than "Wired")
	 */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i]) {
			g_free (names[i]);
			names[i] = get_device_type_name_with_iface (devices[i]);
		}
	}
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* Try prefixing bus name (eg, "PCI Ethernet" vs "USB Ethernet") */
	uclient = g_udev_client_new (subsys);
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i]) {
			const char *bus = get_bus_name (uclient, devices[i]);
			char *name;

			if (!bus)
				continue;

			g_free (names[i]);
			name = get_device_type_name_with_iface (devices[i]);
			/* Translators: the first %s is a bus name (eg, "USB") or
			 * product name, the second is a device type (eg,
			 * "Ethernet"). You can change this to something like
			 * "%2$s (%1$s)" if there's no grammatical way to combine
			 * the strings otherwise.
			 */
			names[i] = g_strdup_printf (C_("long device name", "%s %s"),
			                            bus, name);
			g_free (name);
		}
	}
	g_object_unref (uclient);
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* Try prefixing vendor name */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i]) {
			const char *vendor = nma_utils_get_device_vendor (devices[i]);
			char *name;

			if (!vendor)
				continue;

			g_free (names[i]);
			name = get_device_type_name_with_iface (devices[i]);
			names[i] = g_strdup_printf (C_("long device name", "%s %s"),
			                            vendor,
			                            nma_utils_get_device_type_name (devices[i]));
			g_free (name);
		}
	}
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* If dealing with Bluetooth devices, try to distinguish them by
	 * device name.
	 */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i] && NM_IS_DEVICE_BT (devices[i])) {
			const char *devname = nm_device_bt_get_name (NM_DEVICE_BT (devices[i]));

			if (!devname)
				continue;

			g_free (names[i]);
			names[i] = g_strdup_printf ("%s (%s)",
						    nma_utils_get_device_type_name (devices[i]),
						    devname);
		}
	}
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* We have multiple identical network cards, so we have to differentiate
	 * them by interface name.
	 */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i]) {
			const char *interface = nm_device_get_iface (devices[i]);

			if (!interface)
				continue;

			g_free (names[i]);
			names[i] = g_strdup_printf ("%s (%s)",
			                            nma_utils_get_device_type_name (devices[i]),
			                            interface);
		}
	}

 done:
	g_free (duplicates);
	names[num_devices] = NULL;
	return names;
}

/**
 * nma_utils_get_connection_device_name:
 * @connection: an #NMConnection for a virtual device type
 *
 * Returns the name that nma_utils_disambiguate_device_names() would
 * return for the virtual device that would be created for @connection.
 * Eg, "VLAN (eth1.1)".
 *
 * Returns: (transfer full): the name of @connection's device
 */
char *
nma_utils_get_connection_device_name (NMConnection *connection)
{
	const char *iface, *type, *display_type;
	NMSettingConnection *s_con;

	iface = nm_connection_get_virtual_iface_name (connection);
	g_return_val_if_fail (iface != NULL, NULL);

	s_con = nm_connection_get_setting_connection (connection);
	g_return_val_if_fail (s_con != NULL, NULL);
	type = nm_setting_connection_get_connection_type (s_con);

	if (!strcmp (type, NM_SETTING_BOND_SETTING_NAME))
		display_type = _("Bond");
	else if (!strcmp (type, NM_SETTING_TEAM_SETTING_NAME))
		display_type = _("Team");
	else if (!strcmp (type, NM_SETTING_BRIDGE_SETTING_NAME))
		display_type = _("Bridge");
	else if (!strcmp (type, NM_SETTING_VLAN_SETTING_NAME))
		display_type = _("VLAN");
	else {
		g_warning ("Unrecognized virtual device type '%s'", type);
		display_type = type;
	}

	return g_strdup_printf ("%s (%s)", display_type, iface);
}

/*---------------------------------------------------------------------------*/
/* Password storage icon */

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
	NMSetting *setting;
	const char *password_flags_name;
	int item_number;
	GtkWidget *passwd_entry;
} PopupMenuItemInfo;

static void
popup_menu_item_info_destroy (gpointer data)
{
	PopupMenuItemInfo *info = (PopupMenuItemInfo *) data;

	if (info->setting)
		g_object_unref (info->setting);
	g_slice_free (PopupMenuItemInfo, data);
}

static void
activate_menu_item_cb (GtkMenuItem *menuitem, gpointer user_data)
{
	PopupMenuItemInfo *info = (PopupMenuItemInfo *) user_data;
	NMSettingSecretFlags secret_flags = NM_SETTING_SECRET_FLAG_NONE;

	/* Get current secret flags */
	if (info->setting)
		nm_setting_get_secret_flags (info->setting, info->password_flags_name,
		                             &secret_flags, NULL);

	/* Update password flags according to the password-storage popup menu */
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem))) {
		if (info->item_number == 1)
			secret_flags |= NM_SETTING_SECRET_FLAG_AGENT_OWNED;
		else
			secret_flags &= ~NM_SETTING_SECRET_FLAG_AGENT_OWNED;

		/* Update the secret flags */
		if (info->setting)
			nm_setting_set_secret_flags (info->setting, info->password_flags_name,
			                             secret_flags, NULL);

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
 * nma_utils_setup_password_storage:
 * @setting: #NMSetting containing the password
 * @passwd_entry: password #GtkEntry which the icon is attached to
 * @password_flags_name: name of the storage flags for password
 *   (like psk-flags)
 *
 * Adds a secondary icon and creates a popup menu for password entry.
 */
void
nma_utils_setup_password_storage (NMSetting *setting,
                                  GtkWidget *passwd_entry,
                                  const char *password_flags_name)
{
	GtkWidget *popup_menu;
	GtkWidget *item1, *item2;
	GSList *group;
	PopupMenuItemInfo *info;

	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (passwd_entry), GTK_ENTRY_ICON_SECONDARY, "document-save");
	popup_menu = gtk_menu_new ();
	g_object_set_data (G_OBJECT (popup_menu), PASSWORD_STORAGE_MENU_TAG, GUINT_TO_POINTER (TRUE));
	group = NULL;
	item1 = gtk_radio_menu_item_new_with_mnemonic (group, _("Store the password only for this _user"));
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item1));
	item2 = gtk_radio_menu_item_new_with_mnemonic (group, _("Store the password for _all users"));

	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item1);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item2);

	if (setting)
		g_object_ref (setting);

	info = g_slice_new0 (PopupMenuItemInfo);
	info->setting = setting;
	info->password_flags_name = password_flags_name;
	info->item_number = 1;
	info->passwd_entry = passwd_entry;
	g_signal_connect_data (item1, "activate",
	                       G_CALLBACK (activate_menu_item_cb),
	                       info,
	                       (GClosureNotify) popup_menu_item_info_destroy, 0);

	info = g_slice_new0 (PopupMenuItemInfo);
	info->setting = setting;
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
 * nma_utils_update_password_storage:
 * @setting: #NMSetting containing the password
 * @secret_flags: secret flags to use
 * @passwd_entry: #GtkEntry with the password
 * @password_flags_name: name of the storage flags for password
 *   (like psk-flags)
 *
 * Updates secret flags and the storage popup menu.
 */
void
nma_utils_update_password_storage (NMSetting *setting,
                                   NMSettingSecretFlags secret_flags,
                                   GtkWidget *passwd_entry,
                                   const char *password_flags_name)
{
	GList *menu_list, *iter;
	GtkWidget *menu = NULL;

	if (!setting)
		return;

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
/*---------------------------------------------------------------------------*/

