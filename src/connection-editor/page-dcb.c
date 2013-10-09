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
 * (C) Copyright 2013 Red Hat, Inc.
 */

#include "config.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <NetworkManager.h>
#include <nm-setting-connection.h>
#include <nm-setting-dcb.h>
#include <nm-utils.h>

#include "page-dcb.h"

G_DEFINE_TYPE (CEPageDcb, ce_page_dcb, CE_TYPE_PAGE)

#define CE_PAGE_DCB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_DCB, CEPageDcbPrivate))

typedef struct {
	/* Copy of initial setting, if any; changes in the Options dialogs
	 * update this setting, which is then copied to the final setting when
	 * required.
	 */
	NMSettingDcb *options;

	GtkToggleButton *enabled;
	GtkBox *box;

	gboolean initial_have_dcb;
} CEPageDcbPrivate;

/***************************************************************************/

static void
fcoe_dialog_show (CEPageDcb *self)
{
	CEPageDcbPrivate *priv = CE_PAGE_DCB_GET_PRIVATE (self);
	CEPage *parent = CE_PAGE (self);
	GtkWidget *dialog, *toplevel, *combo;
	gint result;

	dialog = GTK_WIDGET (gtk_builder_get_object (parent->builder, "fcoe_dialog"));
	g_assert (dialog);
	toplevel = gtk_widget_get_toplevel (parent->page);
	if (gtk_widget_is_toplevel (toplevel))
	    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (toplevel));

	/* Set the initial value */
	combo = GTK_WIDGET (gtk_builder_get_object (parent->builder, "fcoe_mode_combo"));
	g_assert (combo);
	gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), nm_setting_dcb_get_app_fcoe_mode (priv->options));

	/* Run the dialog */
	result = gtk_dialog_run (GTK_DIALOG (dialog));
	if (result == GTK_RESPONSE_OK) {
		g_object_set (G_OBJECT (priv->options),
		              NM_SETTING_DCB_APP_FCOE_MODE, gtk_combo_box_get_active_id (GTK_COMBO_BOX (combo)),
		              NULL);
	}

	gtk_widget_hide (dialog);
	ce_page_changed (CE_PAGE (self));
}

static void
pfc_dialog_show (CEPageDcb *self)
{
	CEPageDcbPrivate *priv = CE_PAGE_DCB_GET_PRIVATE (self);
	CEPage *parent = CE_PAGE (self);
	GtkWidget *dialog, *toplevel;
	GtkToggleButton *check;
	gint result;
	guint i;
	gboolean active;
	char *tmp;

	dialog = GTK_WIDGET (gtk_builder_get_object (parent->builder, "pfc_dialog"));
	g_assert (dialog);
	toplevel = gtk_widget_get_toplevel (parent->page);
	if (gtk_widget_is_toplevel (toplevel))
	    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (toplevel));

	/* Set the initial value */
	for (i = 0; i < 8; i++ ) {
		tmp = g_strdup_printf ("pfc_prio%u_checkbutton", i);
		check = GTK_TOGGLE_BUTTON (gtk_builder_get_object (parent->builder, tmp));
		g_free (tmp);
		g_assert (check);

		gtk_toggle_button_set_active (check, nm_setting_dcb_get_priority_flow_control (priv->options, i));
	}

	/* Run the dialog */
	result = gtk_dialog_run (GTK_DIALOG (dialog));
	if (result == GTK_RESPONSE_OK) {
		for (i = 0; i < 8; i++ ) {
			tmp = g_strdup_printf ("pfc_prio%u_checkbutton", i);
			check = GTK_TOGGLE_BUTTON (gtk_builder_get_object (parent->builder, tmp));
			g_free (tmp);
			g_assert (check);

			active = gtk_toggle_button_get_active (check);
			nm_setting_dcb_set_priority_flow_control (priv->options, i, active);
		}
	}

	gtk_widget_hide (dialog);
	ce_page_changed (CE_PAGE (self));
}

static gboolean
uint_entries_validate (GtkBuilder *builder, const char *fmt, gint max, gboolean sum)
{
	unsigned long int num;
	GtkEntry *entry;
	char *tmp;
	const char *text;
	guint i, total = 0;
	gboolean valid = TRUE;
	GdkRGBA bgcolor;

	for (i = 0; i < 8; i++) {
		tmp = g_strdup_printf (fmt, i);
		entry = GTK_ENTRY (gtk_builder_get_object (builder, tmp));
		g_free (tmp);
		g_assert (entry);

		text = gtk_entry_get_text (entry);
		if (text) {
			errno = 0;
			num = strtol (text, NULL, 10);
			if (errno || num < 0 || num > max) {
				/* FIXME: only sets highlight color? */
				gdk_rgba_parse (&bgcolor, "red3");
				gtk_widget_override_background_color (GTK_WIDGET (entry), GTK_STATE_NORMAL, &bgcolor);
				valid = FALSE;
			} else
				gtk_widget_override_background_color (GTK_WIDGET (entry), GTK_STATE_NORMAL, NULL);

			total += (guint) num;
		}
	}
	if (sum && total != 100)
		valid = FALSE;

	return valid;
}

static void
pg_dialog_valid_func (GtkBuilder *builder)
{
	GtkDialog *dialog;
	gboolean valid = FALSE;

	if (!uint_entries_validate (builder, "pg_pgid%u_entry", 7, FALSE))
		goto done;
	if (!uint_entries_validate (builder, "pg_pgpct%u_entry", 100, TRUE))
		goto done;
	if (!uint_entries_validate (builder, "pg_uppct%u_entry", 100, FALSE))
		goto done;
	if (!uint_entries_validate (builder, "pg_up2tc%u_entry", 7, FALSE))
		goto done;

	valid = TRUE;

done:
	dialog = GTK_DIALOG (gtk_builder_get_object (builder, "pg_dialog"));
	gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_OK, valid);
}


static void
uint_filter_cb (GtkEditable *editable,
               gchar *text,
               gint length,
               gint *position,
               gpointer user_data)
{
	utils_filter_editable_on_insert_text (editable,
	                                      text, length, position, user_data,
	                                      utils_char_is_ascii_digit,
	                                      uint_filter_cb);
}

static void
uint_entries_handle (GtkBuilder *builder,
                     NMSettingDcb *s_dcb,
                     const char *fmt,
                     guint (*get_func) (NMSettingDcb *s_dcb, guint n),
                     void (*set_func) (NMSettingDcb *s_dcb, guint n, guint val))
{
	char *tmp;
	GtkEntry *entry;
	guint i;
	const char *text;

	for (i = 0; i < 8; i++) {
		tmp = g_strdup_printf (fmt, i);
		entry = GTK_ENTRY (gtk_builder_get_object (builder, tmp));
		g_free (tmp);
		g_assert (entry);

		if (get_func) {
			tmp = g_strdup_printf ("%u", get_func (s_dcb, i));
			gtk_entry_set_text (entry, tmp);
			g_free (tmp);

			g_signal_connect (entry, "insert-text", (GCallback) uint_filter_cb, NULL);
			g_signal_connect_swapped (entry, "changed", (GCallback) pg_dialog_valid_func, builder);
		} else if (set_func) {
			unsigned long int num;

			text = gtk_entry_get_text (entry);
			if (text) {
				errno = 0;
				num = strtol (text, NULL, 10);
				if (errno == 0 && num >= 0 && num <= 100)
					set_func (s_dcb, i, (guint) num);
			}
		} else
			g_assert_not_reached ();
	}
}

static void
bool_entries_handle (GtkBuilder *builder,
                     NMSettingDcb *s_dcb,
                     const char *fmt,
                     gboolean (*get_func) (NMSettingDcb *s_dcb, guint n),
                     void (*set_func) (NMSettingDcb *s_dcb, guint n, gboolean val))
{
	char *tmp;
	GtkToggleButton *toggle;
	guint i;

	for (i = 0; i < 8; i++) {
		tmp = g_strdup_printf (fmt, i);
		toggle = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, tmp));
		g_free (tmp);
		g_assert (toggle);

		if (get_func)
			gtk_toggle_button_set_active (toggle, get_func (s_dcb, i));
		else if (set_func)
			set_func (s_dcb, i, gtk_toggle_button_get_active (toggle));
		else
			g_assert_not_reached ();
	}
}

static void
pg_dialog_show (CEPageDcb *self)
{
	CEPageDcbPrivate *priv = CE_PAGE_DCB_GET_PRIVATE (self);
	CEPage *parent = CE_PAGE (self);
	GtkWidget *dialog, *toplevel;
	gint result;

	dialog = GTK_WIDGET (gtk_builder_get_object (parent->builder, "pg_dialog"));
	g_assert (dialog);
	toplevel = gtk_widget_get_toplevel (parent->page);
	if (gtk_widget_is_toplevel (toplevel))
	    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (toplevel));

	uint_entries_handle (parent->builder,
	                     priv->options,
	                     "pg_pgid%u_entry",
	                     nm_setting_dcb_get_priority_group_id,
	                     NULL);

	uint_entries_handle (parent->builder,
	                     priv->options,
	                     "pg_pgpct%u_entry",
	                     nm_setting_dcb_get_priority_group_bandwidth,
	                     NULL);

	uint_entries_handle (parent->builder,
	                     priv->options,
	                     "pg_uppct%u_entry",
	                     nm_setting_dcb_get_priority_bandwidth,
	                     NULL);

	bool_entries_handle (parent->builder,
	                     priv->options,
	                     "pg_strict%u_checkbutton",
	                     nm_setting_dcb_get_priority_strict_bandwidth,
	                     NULL);

	uint_entries_handle (parent->builder,
	                     priv->options,
	                     "pg_up2tc%u_entry",
	                     nm_setting_dcb_get_priority_traffic_class,
	                     NULL);

	pg_dialog_valid_func (parent->builder);

	/* Run the dialog */
	result = gtk_dialog_run (GTK_DIALOG (dialog));
	if (result == GTK_RESPONSE_OK) {
		uint_entries_handle (parent->builder,
		                     priv->options,
		                     "pg_pgid%u_entry",
		                     NULL,
		                     nm_setting_dcb_set_priority_group_id);

		uint_entries_handle (parent->builder,
		                     priv->options,
		                     "pg_pgpct%u_entry",
		                     NULL,
		                     nm_setting_dcb_set_priority_group_bandwidth);

		uint_entries_handle (parent->builder,
		                     priv->options,
		                     "pg_uppct%u_entry",
		                     NULL,
		                     nm_setting_dcb_set_priority_bandwidth);

		bool_entries_handle (parent->builder,
		                     priv->options,
		                     "pg_strict%u_checkbutton",
		                     NULL,
		                     nm_setting_dcb_set_priority_strict_bandwidth);

		uint_entries_handle (parent->builder,
		                     priv->options,
		                     "pg_up2tc%u_entry",
		                     NULL,
		                     nm_setting_dcb_set_priority_traffic_class);
	}

	gtk_widget_hide (dialog);
	ce_page_changed (CE_PAGE (self));
}

/***************************************************************************/

typedef void (*OptionsFunc) (CEPageDcb *self);

typedef struct {
	const char *prefix;
	const char *flags_prop;
	const char *priority_prop;
	const OptionsFunc options_func;
} Feature;

static const Feature features[] = {
	{ "fcoe",  NM_SETTING_DCB_APP_FCOE_FLAGS,              NM_SETTING_DCB_APP_FCOE_PRIORITY,  fcoe_dialog_show },
	{ "iscsi", NM_SETTING_DCB_APP_ISCSI_FLAGS,             NM_SETTING_DCB_APP_ISCSI_PRIORITY, NULL },
	{ "fip",   NM_SETTING_DCB_APP_FIP_FLAGS,               NM_SETTING_DCB_APP_FIP_PRIORITY,   NULL },
	{ "pfc",   NM_SETTING_DCB_PRIORITY_FLOW_CONTROL_FLAGS, NULL,                              pfc_dialog_show },
	{ "pg",    NM_SETTING_DCB_PRIORITY_GROUP_FLAGS,        NULL,                              pg_dialog_show },
};

typedef struct {
	const Feature *f;
	GtkBuilder *builder;
} EnableInfo;

static GtkWidget *
get_widget (GtkBuilder *builder, const char *prefix, const char *suffix)
{
	GtkWidget *widget;
	char *s;

	s = g_strdup_printf ("%s%s", prefix, suffix);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, s));
	g_assert (widget);
	g_free (s);
	return widget;
}

static void
enable_toggled_cb (GtkToggleButton *button, EnableInfo *info)
{
	gboolean enabled = gtk_toggle_button_get_active (button);
	GtkWidget *widget;

	/* Set other feature widgets sensitive or not depending on enabled */

	widget = get_widget (info->builder, info->f->prefix, "_advertise_checkbutton");
	gtk_widget_set_sensitive (widget, enabled);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);

	widget = get_widget (info->builder, info->f->prefix, "_willing_checkbutton");
	gtk_widget_set_sensitive (widget, enabled);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);

	if (info->f->priority_prop) {
		widget = get_widget (info->builder, info->f->prefix, "_priority_combo");
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
		gtk_widget_set_sensitive (widget, enabled);
	}

	if (info->f->options_func) {
		widget = get_widget (info->builder, info->f->prefix, "_options_button");
		gtk_widget_set_sensitive (widget, enabled);
	}
}

static void
feature_setup (CEPageDcb *self, NMSettingDcb *s_dcb, const Feature *f)
{
	CEPage *parent = CE_PAGE (self);
	GtkWidget *widget;
	NMSettingDcbFlags flags = NM_SETTING_DCB_FLAG_NONE;
	gboolean enabled;
	EnableInfo *info;

	if (s_dcb)
		g_object_get (G_OBJECT (s_dcb), f->flags_prop, (guint32 *) &flags, NULL);
	enabled = flags & NM_SETTING_DCB_FLAG_ENABLE;

	/* Enable */
	widget = get_widget (parent->builder, f->prefix, "_enable_checkbutton");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);

	info = g_malloc0 (sizeof (EnableInfo));
	info->f = f;
	info->builder = parent->builder;
g_message ("setting up enable button");
	g_signal_connect (widget, "toggled", G_CALLBACK (enable_toggled_cb), info);
	g_object_weak_ref (G_OBJECT (widget), (GWeakNotify) g_free, info);

	/* Advertise */
	widget = get_widget (parent->builder, f->prefix, "_advertise_checkbutton");
	gtk_widget_set_sensitive (widget, enabled);
	if (enabled)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), flags & NM_SETTING_DCB_FLAG_ADVERTISE);
	g_signal_connect_swapped (widget, "toggled", G_CALLBACK (ce_page_changed), self);

	/* Willing */
	widget = get_widget (parent->builder, f->prefix, "_willing_checkbutton");
	gtk_widget_set_sensitive (widget, enabled);
	if (enabled)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), flags & NM_SETTING_DCB_FLAG_WILLING);
	g_signal_connect_swapped (widget, "toggled", G_CALLBACK (ce_page_changed), self);

	if (f->priority_prop) {
		gint priority = -1;

		if (s_dcb)
			g_object_get (G_OBJECT (s_dcb), f->priority_prop, &priority, NULL);
		priority = CLAMP (priority, -1, 7);

		widget = get_widget (parent->builder, f->prefix, "_priority_combo");
		gtk_widget_set_sensitive (widget, enabled);
		if (enabled)
			gtk_combo_box_set_active (GTK_COMBO_BOX (widget), priority + 1);
		g_signal_connect_swapped (widget, "changed", G_CALLBACK (ce_page_changed), self);
	}

	if (f->options_func) {
		widget = get_widget (parent->builder, f->prefix, "_options_button");
		gtk_widget_set_sensitive (widget, enabled);
		g_signal_connect_swapped (widget, "clicked", G_CALLBACK (f->options_func), self);
	}
}

static void
enable_toggled (GtkToggleButton *button, gpointer user_data)
{
	CEPageDcbPrivate *priv = CE_PAGE_DCB_GET_PRIVATE (user_data);

	gtk_widget_set_sensitive (GTK_WIDGET (priv->box), gtk_toggle_button_get_active (button));
	ce_page_changed (CE_PAGE (user_data));
}

static void
finish_setup (CEPageDcb *self, gpointer unused, GError *error, gpointer user_data)
{
	CEPage *parent = CE_PAGE (self);
	CEPageDcbPrivate *priv = CE_PAGE_DCB_GET_PRIVATE (self);
	NMSettingDcb *s_dcb = nm_connection_get_setting_dcb (parent->connection);
	guint i;

	if (error)
		return;

	gtk_toggle_button_set_active (priv->enabled, priv->initial_have_dcb);
	g_signal_connect (priv->enabled, "toggled", G_CALLBACK (enable_toggled), self);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->box), priv->initial_have_dcb);

	for (i = 0; i < G_N_ELEMENTS (features); i++)
		feature_setup (self, s_dcb, &features[i]);
}

CEPage *
ce_page_dcb_new (NMConnection *connection,
                 GtkWindow *parent_window,
                 NMClient *client,
                 NMRemoteSettings *settings,
                 const char **out_secrets_setting_name,
                 GError **error)
{
	CEPageDcb *self;
	CEPageDcbPrivate *priv;
	CEPage *parent;
	NMSettingDcb *s_dcb;

	self = CE_PAGE_DCB (ce_page_new (CE_TYPE_PAGE_DCB,
	                                 connection,
	                                 parent_window,
	                                 client,
	                                 settings,
	                                 UIDIR "/ce-page-dcb.ui",
	                                 "DcbPage",
	                                 _("DCB")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("Could not load DCB user interface."));
		return NULL;
	}

	priv = CE_PAGE_DCB_GET_PRIVATE (self);
	parent = CE_PAGE (self);

	priv->enabled = GTK_TOGGLE_BUTTON (gtk_builder_get_object (parent->builder, "dcb_enabled_checkbutton"));
	priv->box = GTK_BOX (gtk_builder_get_object (parent->builder, "dcb_box"));

	s_dcb = nm_connection_get_setting_dcb (connection);
	if (s_dcb) {
		priv->initial_have_dcb = TRUE;
		priv->options = (NMSettingDcb *) nm_setting_duplicate (NM_SETTING (s_dcb));
	} else
		priv->options = (NMSettingDcb *) nm_setting_dcb_new ();

	g_signal_connect (self, "initialized", G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageDcb *self, NMSettingDcb *s_dcb)
{
	CEPage *parent = CE_PAGE (self);
	CEPageDcbPrivate *priv = CE_PAGE_DCB_GET_PRIVATE (self);
	NMSettingDcbFlags flags = NM_SETTING_DCB_FLAG_NONE;
	gboolean enabled, b;
	const char *tmp;
	guint i, num;

	enabled = gtk_toggle_button_get_active (priv->enabled);
	for (i = 0; i < G_N_ELEMENTS (features); i++) {
		const Feature *f = &features[i];
		GtkWidget *widget;

		flags = NM_SETTING_DCB_FLAG_NONE;

		/* Enable */
		widget = get_widget (parent->builder, f->prefix, "_enable_checkbutton");
		if (enabled && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
			flags |= NM_SETTING_DCB_FLAG_ENABLE;

		/* Advertise */
		widget = get_widget (parent->builder, f->prefix, "_advertise_checkbutton");
		if (enabled && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
			flags |= NM_SETTING_DCB_FLAG_ADVERTISE;

		/* Willing */
		widget = get_widget (parent->builder, f->prefix, "_willing_checkbutton");
		if (enabled && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
			flags |= NM_SETTING_DCB_FLAG_WILLING;

		g_object_set (G_OBJECT (s_dcb), f->flags_prop, flags, NULL);

		if (f->priority_prop) {
			gint idx = 0;

			widget = get_widget (parent->builder, f->prefix, "_priority_combo");
			if (enabled)
				idx = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
			g_object_set (G_OBJECT (s_dcb), f->priority_prop, (gint) (idx - 1), NULL);
		}
	}

	/* FCoE Mode */
	flags = nm_setting_dcb_get_app_fcoe_flags (s_dcb);
	tmp = NULL;
	if (flags & NM_SETTING_DCB_FLAG_ENABLE)
		tmp = nm_setting_dcb_get_app_fcoe_mode (priv->options);
	g_object_set (G_OBJECT (s_dcb),
	              NM_SETTING_DCB_APP_FCOE_MODE, tmp ? tmp : NM_SETTING_DCB_FCOE_MODE_FABRIC,
	              NULL);

	/* Priority Flow Control */
	flags = nm_setting_dcb_get_priority_flow_control_flags (s_dcb);
	for (i = 0; i < 8; i++) {
		b = FALSE;
		if (flags & NM_SETTING_DCB_FLAG_ENABLE)
			b = nm_setting_dcb_get_priority_flow_control (priv->options, i);
		nm_setting_dcb_set_priority_flow_control (s_dcb, i, b);
	}

	/* Priority Groups */
	flags = nm_setting_dcb_get_priority_group_flags (s_dcb);
	for (i = 0; i < 8; i++) {
		/* Group ID */
		num = 0;
		if (flags & NM_SETTING_DCB_FLAG_ENABLE)
			num = nm_setting_dcb_get_priority_group_id (priv->options, i);
		nm_setting_dcb_set_priority_group_id (s_dcb, i, num);

		num = 0;
		if (flags & NM_SETTING_DCB_FLAG_ENABLE)
			num = nm_setting_dcb_get_priority_group_bandwidth (priv->options, i);
		nm_setting_dcb_set_priority_group_bandwidth (s_dcb, i, num);

		num = 0;
		if (flags & NM_SETTING_DCB_FLAG_ENABLE)
			num = nm_setting_dcb_get_priority_bandwidth (priv->options, i);
		nm_setting_dcb_set_priority_bandwidth (s_dcb, i, num);

		b = 0;
		if (flags & NM_SETTING_DCB_FLAG_ENABLE)
			b = nm_setting_dcb_get_priority_strict_bandwidth (priv->options, i);
		nm_setting_dcb_set_priority_strict_bandwidth (s_dcb, i, b);

		num = 0;
		if (flags & NM_SETTING_DCB_FLAG_ENABLE)
			num = nm_setting_dcb_get_priority_traffic_class (priv->options, i);
		nm_setting_dcb_set_priority_traffic_class (s_dcb, i, num);
	}

}

static gboolean
validate (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageDcb *self = CE_PAGE_DCB (page);
	CEPageDcbPrivate *priv = CE_PAGE_DCB_GET_PRIVATE (self);
	NMSettingDcb *s_dcb;

	if (!gtk_toggle_button_get_active (priv->enabled)) {
		nm_connection_remove_setting (connection, NM_TYPE_SETTING_DCB);
		return TRUE;
	}

	s_dcb = nm_connection_get_setting_dcb (connection);
	if (!s_dcb) {
		s_dcb = (NMSettingDcb *) nm_setting_dcb_new ();
		nm_connection_add_setting (connection, NM_SETTING (s_dcb));
	}
	ui_to_setting (self, s_dcb);

	return nm_setting_verify (NM_SETTING (s_dcb), NULL, error);
}

static void
ce_page_dcb_init (CEPageDcb *self)
{
}

static void
dispose (GObject *object)
{
	CEPageDcbPrivate *priv = CE_PAGE_DCB_GET_PRIVATE (object);

	g_clear_object (&priv->options);

	G_OBJECT_CLASS (ce_page_dcb_parent_class)->dispose (object);
}

static void
ce_page_dcb_class_init (CEPageDcbClass *security_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (security_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (security_class);

	g_type_class_add_private (object_class, sizeof (CEPageDcbPrivate));

	/* virtual methods */
	object_class->dispose = dispose;

	parent_class->validate = validate;
}
