// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * Copyright 2008 - 2014 Red Hat, Inc.
 */

#include "nm-default.h"

#include <net/ethernet.h>
#include <netinet/ether.h>
#include <string.h>
#include <stdlib.h>

#include "ce-page.h"

G_DEFINE_ABSTRACT_TYPE (CEPage, ce_page, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_CONNECTION,
	PROP_PARENT_WINDOW,

	LAST_PROP
};

enum {
	CHANGED,
	INITIALIZED,
	NEW_EDITOR,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static gboolean
spin_output_with_default_string (GtkSpinButton *spin,
                                 int defvalue,
                                 const char *defstring)
{
	int val;
	gchar *buf = NULL;

	val = gtk_spin_button_get_value_as_int (spin);
	if (val == defvalue)
		buf = g_strdup (defstring);
	else
		buf = g_strdup_printf ("%d", val);

	if (strcmp (buf, gtk_entry_get_text (GTK_ENTRY (spin))))
		gtk_entry_set_text (GTK_ENTRY (spin), buf);

	g_free (buf);
	return TRUE;
}

static gboolean
ce_spin_output_with_automatic (GtkSpinButton *spin, gpointer user_data)
{
	return spin_output_with_default_string (spin,
	                                        GPOINTER_TO_INT (user_data),
	                                        _("automatic"));
}

static gboolean
ce_spin_output_with_default (GtkSpinButton *spin, gpointer user_data)
{
	return spin_output_with_default_string (spin,
	                                        GPOINTER_TO_INT (user_data),
	                                        _("default"));
}

static gint
spin_input_with_default_string (GtkSpinButton *spin,
                                int defvalue,
                                gdouble *new_val,
                                const char *defstring)
{
	const gchar *buf;

	buf = gtk_entry_get_text (GTK_ENTRY (spin));
	if (strcmp (buf, defstring) == 0) {
		*new_val = defvalue;
		return TRUE;
	}

	return FALSE;
}

static gint
ce_spin_input_with_automatic (GtkSpinButton *spin, gdouble *new_val, gpointer user_data)
{
	return spin_input_with_default_string (spin,
	                                       GPOINTER_TO_INT (user_data),
	                                       new_val,
	                                       _("automatic"));
}

static gint
ce_spin_input_with_default (GtkSpinButton *spin, gdouble *new_val, gpointer user_data)
{
	return spin_input_with_default_string (spin,
	                                       GPOINTER_TO_INT (user_data),
	                                       new_val,
	                                       _("default"));
}

void
ce_spin_automatic_val (GtkSpinButton *spin, int defvalue)
{
	g_signal_connect (spin, "output",
	                  G_CALLBACK (ce_spin_output_with_automatic),
	                  GINT_TO_POINTER (defvalue));
	g_signal_connect (spin, "input",
	                  G_CALLBACK (ce_spin_input_with_automatic),
	                  GINT_TO_POINTER (defvalue));
}

void
ce_spin_default_val (GtkSpinButton *spin, int defvalue)
{
	g_signal_connect (spin, "output",
	                  G_CALLBACK (ce_spin_output_with_default),
	                  GINT_TO_POINTER (defvalue));
	g_signal_connect (spin, "input",
	                  G_CALLBACK (ce_spin_input_with_default),
	                  GINT_TO_POINTER (defvalue));
}

int
ce_get_property_default (NMSetting *setting, const char *property_name)
{
	GParamSpec *spec;
	GValue value = { 0, };

	g_return_val_if_fail (NM_IS_SETTING (setting), -1);

	spec = g_object_class_find_property (G_OBJECT_GET_CLASS (setting), property_name);
	g_return_val_if_fail (spec != NULL, -1);

	g_value_init (&value, spec->value_type);
	g_param_value_set_default (spec, &value);

	if (G_VALUE_HOLDS_CHAR (&value))
		return (int) g_value_get_schar (&value);
	else if (G_VALUE_HOLDS_INT (&value))
		return g_value_get_int (&value);
	else if (G_VALUE_HOLDS_INT64 (&value))
		return (int) g_value_get_int64 (&value);
	else if (G_VALUE_HOLDS_LONG (&value))
		return (int) g_value_get_long (&value);
	else if (G_VALUE_HOLDS_UINT (&value))
		return (int) g_value_get_uint (&value);
	else if (G_VALUE_HOLDS_UINT64 (&value))
		return (int) g_value_get_uint64 (&value);
	else if (G_VALUE_HOLDS_ULONG (&value))
		return (int) g_value_get_ulong (&value);
	else if (G_VALUE_HOLDS_UCHAR (&value))
		return (int) g_value_get_uchar (&value);
	g_return_val_if_fail (FALSE, 0);
	return 0;
}

gboolean
ce_page_validate (CEPage *self, NMConnection *connection, GError **error)
{
	g_return_val_if_fail (CE_IS_PAGE (self), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	if (CE_PAGE_GET_CLASS (self)->ce_page_validate_v) {
		if (!CE_PAGE_GET_CLASS (self)->ce_page_validate_v (self, connection, error)) {
			if (error && !*error)
				g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("unspecified error"));
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
ce_page_last_update (CEPage *self, NMConnection *connection, GError **error)
{
	g_return_val_if_fail (CE_IS_PAGE (self), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	if (CE_PAGE_GET_CLASS (self)->last_update)
		return CE_PAGE_GET_CLASS (self)->last_update (self, connection, error);

	return TRUE;
}

gboolean
ce_page_inter_page_change (CEPage *self)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (CE_IS_PAGE (self), FALSE);

	if (self->inter_page_change_running)
		return FALSE;

	self->inter_page_change_running = TRUE;
	if (CE_PAGE_GET_CLASS (self)->inter_page_change)
		ret = CE_PAGE_GET_CLASS (self)->inter_page_change (self);
	self->inter_page_change_running = FALSE;

	return ret;
}

static void
_set_active_combo_item (GtkComboBox *combo, const char *item,
                        const char *combo_item, int combo_idx)
{
	GtkWidget *entry;

	if (item) {
		/* set active item */
		gtk_combo_box_set_active (combo, combo_idx);

		if (!combo_item)
			gtk_combo_box_text_prepend_text (GTK_COMBO_BOX_TEXT (combo), item);

		entry = gtk_bin_get_child (GTK_BIN (combo));
		if (entry)
			gtk_entry_set_text (GTK_ENTRY (entry), combo_item ? combo_item : item);
	}
}

/* Combo box storing data in the form of "text1 (text2)" */
void
ce_page_setup_data_combo (CEPage *self, GtkComboBox *combo,
                          const char *data, char **list)
{
	char **iter, *active_item = NULL;
	int i, active_idx = -1;
	int data_len;

	if (data)
		data_len = strlen (data);
	else
		data_len = -1;

	for (iter = list, i = 0; iter && *iter; iter++, i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), *iter);
		if (   data
		    && g_ascii_strncasecmp (*iter, data, data_len) == 0
		    && ((*iter)[data_len] == '\0' || (*iter)[data_len] == ' ')) {
			active_item = *iter;
			active_idx = i;
		}
	}
	_set_active_combo_item (combo, data, active_item, active_idx);
}

/* Combo box storing MAC addresses only */
void
ce_page_setup_mac_combo (CEPage *self, GtkComboBox *combo,
                         const char *mac, char **mac_list)
{
	char **iter, *active_mac = NULL;
	int i, active_idx = -1;

	for (iter = mac_list, i = 0; iter && *iter; iter++, i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), *iter);
		if (mac && *iter && nm_utils_hwaddr_matches (mac, -1, *iter, -1)) {
			active_mac = *iter;
			active_idx = i;
		}
	}
	_set_active_combo_item (combo, mac, active_mac, active_idx);
}

void
ce_page_setup_cloned_mac_combo (GtkComboBoxText *combo, const char *current)
{
	GtkWidget *entry;
	static const char *entries[][2] = { { "preserve",  N_("Preserve") },
	                                    { "permanent", N_("Permanent") },
	                                    { "random",    N_("Random") },
	                                    { "stable",    N_("Stable") } };
	int i, active = -1;

	gtk_widget_set_tooltip_text (GTK_WIDGET (combo),
		_("The MAC address entered here will be used as hardware address for "
		  "the network device this connection is activated on. This feature is "
		  "known as MAC cloning or spoofing. Example: 00:11:22:33:44:55"));

	gtk_combo_box_text_remove_all (combo);

	for (i = 0; i < G_N_ELEMENTS (entries); i++) {
		gtk_combo_box_text_append (combo, entries[i][0], _(entries[i][1]));
		if (nm_streq0 (current, entries[i][0]))
			active = i;
	}

	if (active != -1) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (combo), active);
	} else if (current && current[0]) {
		entry = gtk_bin_get_child (GTK_BIN (combo));
		g_assert (entry);
		gtk_entry_set_text (GTK_ENTRY (entry), current);
	}
}

char *
ce_page_cloned_mac_get (GtkComboBoxText *combo)
{
	const char *id;

	id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (combo));
	if (id)
		return g_strdup (id);

	return gtk_combo_box_text_get_active_text (combo);
}

static gboolean
mac_valid (const char *mac, int type, const char *property_name, GError **error)
{
	if (mac && *mac) {
		if (!nm_utils_hwaddr_valid (mac, nm_utils_hwaddr_len (type))) {
			const char *addr_type;

			addr_type = type == ARPHRD_ETHER ? _("MAC address") : _("HW address");
			if (property_name) {
				g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC,
				             _("invalid %s for %s (%s)"),
				             addr_type, property_name, mac);
			} else {
				g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC,
				             _("invalid %s (%s)"),
				             addr_type, mac);
			}
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
ce_page_cloned_mac_combo_valid (GtkComboBoxText *combo, int type, const char *property_name, GError **error)
{
	gs_free char *text = NULL;

	if (gtk_combo_box_get_active (GTK_COMBO_BOX (combo)) != -1)
		return TRUE;

	text = gtk_combo_box_text_get_active_text (combo);
	return mac_valid (text,
	                  type,
	                  property_name,
	                  error);
}

gboolean
ce_page_mac_entry_valid (GtkEntry *entry, int type, const char *property_name, GError **error)
{
	g_return_val_if_fail (GTK_IS_ENTRY (entry), FALSE);

	return mac_valid (gtk_entry_get_text (entry), type, property_name, error);
}

gboolean
ce_page_interface_name_valid (const char *iface, const char *property_name, GError **error)
{
	if (iface && *iface) {
		if (!nm_utils_is_valid_iface_name (iface, error)) {
			if (property_name) {
				g_prefix_error (error,
				                _("invalid interface-name for %s (%s): "),
				                property_name, iface);
			} else {
				g_prefix_error (error,
				                _("invalid interface-name (%s): "),
				                iface);
			}
			return FALSE;
		}
	}
	return TRUE;
}

static char **
_get_device_list (CEPage *self,
                  GType device_type,
                  gboolean set_ifname,
                  const char *mac_property)
{
	const GPtrArray *devices;
	GPtrArray *interfaces;
	int i;

	g_return_val_if_fail (CE_IS_PAGE (self), NULL);
	g_return_val_if_fail (set_ifname || mac_property, NULL);

	if (!self->client)
		return NULL;

	interfaces = g_ptr_array_new ();
	devices = nm_client_get_devices (self->client);
	for (i = 0; i < devices->len; i++) {
		NMDevice *dev = g_ptr_array_index (devices, i);
		const char *ifname;
		char *mac = NULL;
		char *item;

		if (   device_type != G_TYPE_NONE
		    && !G_TYPE_CHECK_INSTANCE_TYPE (dev, device_type))
			continue;

		if (device_type == NM_TYPE_DEVICE_BT)
			ifname = nm_device_bt_get_name (NM_DEVICE_BT (dev));
		else
			ifname = nm_device_get_iface (NM_DEVICE (dev));
		if (mac_property)
			g_object_get (G_OBJECT (dev), mac_property, &mac, NULL);

		if (mac && !mac[0])
			nm_clear_g_free (&mac);

		if (set_ifname && mac_property)
			item = g_strdup_printf ("%s%s%s%s", ifname, NM_PRINT_FMT_QUOTED (mac, " (", mac, ")", ""));
		else
			item = g_strdup (set_ifname ? ifname : mac);

		if (item)
			g_ptr_array_add (interfaces, item);

		g_free (mac);
	}
	g_ptr_array_add (interfaces, NULL);

	return (char **)g_ptr_array_free (interfaces, FALSE);
}

static gboolean
_device_entry_parse (const char *entry_text, char **first, char **second)
{
	const char *sp, *left, *right;

	if (!entry_text || !*entry_text) {
		*first = NULL;
		*second = NULL;
		return TRUE;
	}

	sp = strstr (entry_text, " (");
	if (sp) {
		*first = g_strndup (entry_text, sp - entry_text);
		left = sp + 1;
		right = strchr (left, ')');
		if (*left == '(' && right && right > left)
			*second = g_strndup (left + 1, right - left - 1);
		else {
			*second = NULL;
			return FALSE;
		}
	} else {
		*first = g_strdup (entry_text);
		*second = NULL;
	}
	return TRUE;
}

static gboolean
_device_entries_match (const char *ifname, const char *mac, const char *entry)
{
	char *first, *second;
	gboolean ifname_match = FALSE, mac_match = FALSE;
	gboolean both;

	if (!ifname && !mac)
		return FALSE;

	_device_entry_parse (entry, &first, &second);
	both = first && second;

	if (   ifname
	    && (   !g_strcmp0 (ifname, first)
	        || !g_strcmp0 (ifname, second)))
		ifname_match = TRUE;

	if (   mac
	    && (   (first && nm_utils_hwaddr_matches (mac, -1, first, -1))
	        || (second && nm_utils_hwaddr_matches (mac, -1, second, -1))))
		mac_match = TRUE;

	g_free (first);
	g_free (second);

	if (both)
		return ifname_match && mac_match;
	else {
		if (ifname)
			return ifname_match;
		else
			return mac_match;
	}
}

/* Combo box storing ifname and/or MAC */
void
ce_page_setup_device_combo (CEPage *self,
                            GtkComboBox *combo,
                            GType device_type,
                            const char *ifname,
                            const char *mac,
                            const char *mac_property)
{
	char **iter, *active_item = NULL;
	int i, active_idx = -1;
	char **device_list;
	char *item;

	device_list = _get_device_list (self, device_type, TRUE, mac_property);

	if (ifname && mac)
		item = g_strdup_printf ("%s (%s)", ifname, mac);
	else if (!ifname && !mac)
		item = NULL;
	else
		item = g_strdup (ifname ? ifname : mac);

	for (iter = device_list, i = 0; iter && *iter; iter++, i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), *iter);
		if (_device_entries_match (ifname, mac, *iter)) {
			active_item = *iter;
			active_idx = i;
		}
	}
	_set_active_combo_item (combo, item, active_item, active_idx);

	g_free (item);
	g_strfreev (device_list);
}

gboolean
ce_page_device_entry_get (GtkEntry *entry, int type, gboolean check_ifname,
                          char **ifname, char **mac, const char *device_name, GError **error)
{
	gs_free char *first = NULL;
	gs_free char *second = NULL;
	const char *ifname_tmp = NULL, *mac_tmp = NULL;
	const char *str;

	g_return_val_if_fail (entry != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_ENTRY (entry), FALSE);

	str = gtk_entry_get_text (entry);

	if (!_device_entry_parse (str, &first, &second)) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC,
		                     _("can’t parse device name"));
		goto invalid;
	}

	if (first) {
		if (nm_utils_hwaddr_valid (first, nm_utils_hwaddr_len (type)))
			mac_tmp = first;
		else if (!check_ifname || nm_utils_is_valid_iface_name (first, error))
			ifname_tmp = first;
		else
			goto invalid;
	}
	if (second) {
		if (nm_utils_hwaddr_valid (second, nm_utils_hwaddr_len (type))) {
			if (!mac_tmp) {
				mac_tmp = second;
			} else {
				g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC,
				                     _("invalid hardware address"));
				goto invalid;
			}
		} else if (!check_ifname || nm_utils_is_valid_iface_name (second, error)) {
			if (!ifname_tmp)
				ifname_tmp = second;
			else
				goto invalid;
		} else
			goto invalid;
	}

	if (ifname)
		*ifname = g_strdup (ifname_tmp);
	if (mac)
		*mac = g_strdup (mac_tmp);

	return TRUE;

invalid:
	if (error) {
		g_prefix_error (error,
		                _("invalid %s (%s): "),
		                device_name ? device_name : _("device"),
		                str);
	} else {
		g_set_error (error, NMA_ERROR, NMA_ERROR_GENERIC,
		             _("invalid %s (%s) "),
		            device_name ? device_name : _("device"),
		            str);
	}

	return FALSE;
}

char *
ce_page_get_next_available_name (const GPtrArray *connections, const char *format)
{
	GSList *names = NULL, *iter;
	char *cname = NULL;
	int i = 0;

	for (i = 0; i < connections->len; i++) {
		const char *id;

		id = nm_connection_get_id (connections->pdata[i]);
		g_assert (id);
		names = g_slist_append (names, (gpointer) id);
	}

	/* Find the next available unique connection name */
	for (i = 1; !cname && i < 10000; i++) {
		char *temp;
		gboolean found = FALSE;

		NM_PRAGMA_WARNING_DISABLE("-Wformat-nonliteral")
		temp = g_strdup_printf (format, i);
		NM_PRAGMA_WARNING_REENABLE
		for (iter = names; iter; iter = g_slist_next (iter)) {
			if (!strcmp (iter->data, temp)) {
				found = TRUE;
				break;
			}
		}
		if (!found)
			cname = temp;
		else
			g_free (temp);
	}

	g_slist_free (names);
	return cname;
}

void
ce_page_complete_init (CEPage *self,
                       const char *setting_name,
                       GVariant *secrets,
                       GError *error)
{
	GError *update_error = NULL;
	GVariant *setting_dict;
	char *dbus_err;

	g_return_if_fail (self != NULL);
	g_return_if_fail (CE_IS_PAGE (self));

	if (error) {
		/* Ignore missing settings errors */
		dbus_err = g_dbus_error_get_remote_error (error);
		if (   g_strcmp0 (dbus_err, "org.freedesktop.NetworkManager.Settings.InvalidSetting") == 0
		    || g_strcmp0 (dbus_err, "org.freedesktop.NetworkManager.Settings.Connection.SettingNotFound") == 0
		    || g_strcmp0 (dbus_err, "org.freedesktop.NetworkManager.AgentManager.NoSecrets") == 0)
			g_clear_error (&error);
		g_free (dbus_err);
	}

	if (error) {
		g_warning ("Couldn't fetch secrets: %s", error->message);
		g_error_free (error);
		goto out;
	}

	if (!setting_name || !secrets || g_variant_n_children (secrets) == 0) {
		/* Success, no secrets */
		goto out;
	}

	g_assert (setting_name);
	g_assert (secrets);

	setting_dict = g_variant_lookup_value (secrets, setting_name, NM_VARIANT_TYPE_SETTING);
	if (!setting_dict) {
		/* Success, no secrets */
		goto out;
	}
	g_variant_unref (setting_dict);

	/* Update the connection with the new secrets */
	if (!nm_connection_update_secrets (self->connection,
	                                  setting_name,
	                                  secrets,
	                                  &update_error)) {
		g_warning ("Couldn't update the secrets: %s", update_error->message);
		g_error_free (update_error);
		goto out;
	}

out:
	g_signal_emit (self, signals[INITIALIZED], 0, NULL);
}

static void
ce_page_init (CEPage *self)
{
	self->builder = gtk_builder_new ();
}

static void
dispose (GObject *object)
{
	CEPage *self = CE_PAGE (object);

	g_clear_object (&self->page);
	g_clear_object (&self->builder);
	g_clear_object (&self->connection);

	G_OBJECT_CLASS (ce_page_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	CEPage *self = CE_PAGE (object);

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

NMConnectionEditor *
ce_page_new_editor (CEPage *self,
                    GtkWindow *parent_window,
                    NMConnection *connection)
{
	NMConnectionEditor *editor;

	g_return_val_if_fail (CE_IS_PAGE (self), NULL);

	editor = nm_connection_editor_new (parent_window,
	                                   connection,
	                                   self->client);
	if (!editor)
		return NULL;

	g_signal_emit (self, signals[NEW_EDITOR], 0, editor);
	return editor;
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	CEPage *self = CE_PAGE (object);

	switch (prop_id) {
	case PROP_CONNECTION:
		g_value_set_object (value, self->connection);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	CEPage *self = CE_PAGE (object);

	switch (prop_id) {
	case PROP_CONNECTION:
		if (self->connection)
			g_object_unref (self->connection);
		self->connection = g_value_dup_object (value);
		break;
	case PROP_PARENT_WINDOW:
		self->parent_window = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
ce_page_class_init (CEPageClass *page_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (page_class);

	/* virtual methods */
	object_class->dispose      = dispose;
	object_class->finalize     = finalize;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_CONNECTION,
		 g_param_spec_object (CE_PAGE_CONNECTION,
		                      "Connection",
		                      "Connection",
		                      NM_TYPE_CONNECTION,
		                      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_PARENT_WINDOW,
		 g_param_spec_pointer (CE_PAGE_PARENT_WINDOW,
		                       "Parent window",
		                       "Parent window",
		                       G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	/* Signals */
	signals[CHANGED] = 
		g_signal_new ("changed",
	                      G_OBJECT_CLASS_TYPE (object_class),
	                      G_SIGNAL_RUN_FIRST,
	                      0, NULL, NULL, NULL,
	                      G_TYPE_NONE, 0);

	signals[INITIALIZED] = 
		g_signal_new (CE_PAGE_INITIALIZED,
	                      G_OBJECT_CLASS_TYPE (object_class),
	                      G_SIGNAL_RUN_FIRST,
	                      0, NULL, NULL, NULL,
	                      G_TYPE_NONE, 1, G_TYPE_POINTER);

	signals[NEW_EDITOR] =
		g_signal_new (CE_PAGE_NEW_EDITOR,
	                      G_OBJECT_CLASS_TYPE (object_class),
	                      G_SIGNAL_RUN_FIRST,
	                      0, NULL, NULL, NULL,
	                      G_TYPE_NONE, 1, G_TYPE_POINTER);
}


void
ce_page_complete_connection (NMConnection *connection,
                             const char *format,
                             const char *ctype,
                             gboolean autoconnect,
                             NMClient *client)
{
	NMSettingConnection *s_con;
	char *id, *uuid;
	const GPtrArray *connections;

	s_con = nm_connection_get_setting_connection (connection);
	if (!s_con) {
		s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());
		nm_connection_add_setting (connection, NM_SETTING (s_con));
	}

	if (!nm_setting_connection_get_id (s_con)) {
		connections = nm_client_get_connections (client);
		id = ce_page_get_next_available_name (connections, format);
		g_object_set (s_con, NM_SETTING_CONNECTION_ID, id, NULL);
		g_free (id);
	}

	uuid = nm_utils_uuid_generate ();
	g_object_set (s_con,
	              NM_SETTING_CONNECTION_UUID, uuid,
	              NM_SETTING_CONNECTION_TYPE, ctype,
	              NM_SETTING_CONNECTION_AUTOCONNECT, autoconnect,
	              NULL);
	g_free (uuid);
}

CEPage *
ce_page_new (GType page_type,
             NMConnectionEditor *editor,
             NMConnection *connection,
             GtkWindow *parent_window,
             NMClient *client,
             const char *ui_resource,
             const char *widget_name,
             const char *title)
{
	CEPage *self;
	GError *error = NULL;

	g_return_val_if_fail (title != NULL, NULL);
	if (ui_resource)
		g_return_val_if_fail (widget_name != NULL, NULL);

	self = CE_PAGE (g_object_new (page_type,
	                              CE_PAGE_CONNECTION, connection,
	                              CE_PAGE_PARENT_WINDOW, parent_window,
	                              NULL));
	self->title = g_strdup (title);
	self->client = client;
	self->editor = editor;

	if (ui_resource) {
		if (!gtk_builder_add_from_resource (self->builder, ui_resource, &error)) {
			g_warning ("Couldn't load builder resource: %s", error->message);
			g_error_free (error);
			g_object_unref (self);
			return NULL;
		}

		self->page = GTK_WIDGET (gtk_builder_get_object (self->builder, widget_name));
		if (!self->page) {
			g_warning ("Couldn't load page widget '%s' from %s", widget_name, ui_resource);
			g_object_unref (self);
			return NULL;
		}
		g_object_ref_sink (self->page);
	}
	return self;
}

