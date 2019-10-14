// SPDX-License-Identifier: LGPL-2.1+
/* NetworkManager Applet -- allow user control over networking
 *
 * Lubomir Rintel <lkundrak@v3.sk>
 *
 * Copyright (C) 2017,2018 Red Hat, Inc.
 */

#include "nm-default.h"
#include "nma-private.h"
#include "nma-cert-chooser-private.h"
#include "nma-cert-chooser-button.h"
#include "nma-ui-utils.h"
#include "utils.h"

#include <glib/gstdio.h>
#include <gck/gck.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE(self) (&(_NM_GET_PRIVATE (self, NMACertChooser, NMA_IS_CERT_CHOOSER)->_sub.pkcs11))

static void
set_key_password (NMACertChooser *cert_chooser, const gchar *password)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	g_return_if_fail (priv->key_password != NULL);
	if (password)
		gtk_editable_set_text (GTK_EDITABLE (priv->key_password), password);
}

static const gchar *
get_key_password (NMACertChooser *cert_chooser)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);
	const gchar *text;

	g_return_val_if_fail (priv->key_password != NULL, NULL);
	text = gtk_editable_get_text (GTK_EDITABLE (priv->key_password));

	return text && text[0] ? text : NULL;
}

static void
set_key_uri (NMACertChooser *cert_chooser, const gchar *uri)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	gtk_widget_set_sensitive (priv->key_button, TRUE);
	gtk_widget_set_sensitive (priv->key_button_label, TRUE);
	gtk_widget_set_sensitive (priv->key_password, TRUE);
	gtk_widget_set_sensitive (priv->key_password_label, TRUE);
	gtk_widget_show (priv->key_password);
	gtk_widget_show (priv->key_password_label);
	gtk_widget_show (priv->show_password);
	nma_cert_chooser_button_set_uri (NMA_CERT_CHOOSER_BUTTON (priv->key_button), uri);
}

static gchar *
get_key_uri (NMACertChooser *cert_chooser)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	return g_strdup (nma_cert_chooser_button_get_uri (NMA_CERT_CHOOSER_BUTTON (priv->key_button)));
}

static void
set_cert_password (NMACertChooser *cert_chooser, const gchar *password)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	g_return_if_fail (priv->cert_password != NULL);
	if (password)
		gtk_editable_set_text (GTK_EDITABLE (priv->cert_password), password);
}

static const gchar *
get_cert_password (NMACertChooser *cert_chooser)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);
	const gchar *text;

	g_return_val_if_fail (priv->cert_password != NULL, NULL);
	text = gtk_editable_get_text (GTK_EDITABLE (priv->cert_password));

	return text && text[0] ? text : NULL;
}

static void
set_cert_uri (NMACertChooser *cert_chooser, const gchar *uri)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	if (g_str_has_prefix (uri, NM_SETTING_802_1X_CERT_SCHEME_PREFIX_PATH)) {
		gtk_widget_set_sensitive (priv->cert_password, FALSE);
		gtk_widget_set_sensitive (priv->cert_password_label, FALSE);
	} else if (g_str_has_prefix (uri, NM_SETTING_802_1X_CERT_SCHEME_PREFIX_PKCS11)) {
		gtk_widget_set_sensitive (priv->cert_password, TRUE);
		gtk_widget_set_sensitive (priv->cert_password_label, TRUE);
		gtk_widget_show (priv->cert_password);
		gtk_widget_show (priv->cert_password_label);
		gtk_widget_show (priv->show_password);
	} else {
		g_warning ("The certificate '%s' uses an unknown scheme\n", uri);
		return;
	}

	nma_cert_chooser_button_set_uri (NMA_CERT_CHOOSER_BUTTON (priv->cert_button), uri);
}

static gchar *
get_cert_uri (NMACertChooser *cert_chooser)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	return g_strdup (nma_cert_chooser_button_get_uri (NMA_CERT_CHOOSER_BUTTON (priv->cert_button)));
}

static void
add_to_size_group (NMACertChooser *cert_chooser, GtkSizeGroup *group)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	gtk_size_group_add_widget (group, priv->cert_button_label);
	gtk_size_group_add_widget (group, priv->cert_password_label);
	gtk_size_group_add_widget (group, priv->key_button_label);
	gtk_size_group_add_widget (group, priv->key_password_label);
}

static gboolean
validate (NMACertChooser *cert_chooser, GError **error)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);
	GError *local = NULL;

	if (!nma_cert_chooser_button_get_uri (NMA_CERT_CHOOSER_BUTTON (priv->cert_button))) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("No certificate set"));
		return FALSE;
	}

	g_signal_emit_by_name (cert_chooser, "cert-validate", &local);
	if (local) {
		widget_set_error (priv->cert_button);
		g_propagate_error (error, local);
		return FALSE;
	} else {
		widget_unset_error (priv->cert_button);
	}

	g_signal_emit_by_name (cert_chooser, "cert-password-validate", &local);
	if (local) {
		widget_set_error (priv->cert_password);
		g_propagate_error (error, local);
		return FALSE;
	} else {
		widget_unset_error (priv->cert_password);
	}

	if (gtk_widget_get_visible (priv->key_button)) {
		if (!nma_cert_chooser_button_get_uri (NMA_CERT_CHOOSER_BUTTON (priv->cert_button))) {
			g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC, _("No key set"));
			return FALSE;
		}

		g_signal_emit_by_name (cert_chooser, "key-validate", &local);
		if (local) {
			widget_set_error (priv->key_button);
			g_propagate_error (error, local);
			return FALSE;
		} else {
			widget_unset_error (priv->key_button);
		}

		g_signal_emit_by_name (cert_chooser, "key-password-validate", &local);
		if (local) {
			widget_set_error (priv->key_password);
			g_propagate_error (error, local);
			return FALSE;
		} else {
			widget_unset_error (priv->key_password);
		}
	}

	return TRUE;
}

static void
setup_key_password_storage (NMACertChooser *cert_chooser,
                            NMSettingSecretFlags initial_flags,
                            NMSetting *setting,
                            const char *password_flags_name,
                            gboolean with_not_required,
                            gboolean ask_mode)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	nma_utils_setup_password_storage (priv->key_password,
	                                  initial_flags,
	                                  setting,
	                                  password_flags_name,
	                                  with_not_required,
	                                  ask_mode);
}

static void
update_key_password_storage (NMACertChooser *cert_chooser,
                             NMSettingSecretFlags secret_flags,
                             NMSetting *setting,
                             const char *password_flags_name)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	nma_utils_update_password_storage (priv->key_password,
	                                   secret_flags,
	                                   setting,
	                                   password_flags_name);
}

static NMSettingSecretFlags
get_key_password_flags (NMACertChooser *cert_chooser)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	return nma_utils_menu_to_secret_flags (priv->key_password);
}

static void
setup_cert_password_storage (NMACertChooser *cert_chooser,
                             NMSettingSecretFlags initial_flags,
                             NMSetting *setting,
                             const char *password_flags_name,
                             gboolean with_not_required,
                             gboolean ask_mode)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	nma_utils_setup_password_storage (priv->cert_password,
	                                  initial_flags,
	                                  setting,
	                                  password_flags_name,
	                                  with_not_required,
	                                  ask_mode);
}

static void
update_cert_password_storage (NMACertChooser *cert_chooser,
                              NMSettingSecretFlags secret_flags,
                              NMSetting *setting,
                              const char *password_flags_name)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	nma_utils_update_password_storage (priv->cert_password,
	                                   secret_flags,
	                                   setting,
	                                   password_flags_name);
}

static NMSettingSecretFlags
get_cert_password_flags (NMACertChooser *cert_chooser)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	return nma_utils_menu_to_secret_flags (priv->cert_password);
}

static void
cert_changed_cb (NMACertChooserButton *button, gpointer user_data)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (NMA_CERT_CHOOSER (user_data));
	GckUriData *uri_data;
	gchar *pin = NULL;
	const gchar *uri;

	uri = nma_cert_chooser_button_get_uri (button);
	if (!uri)
		return;
	uri_data = gck_uri_parse (uri, GCK_URI_FOR_OBJECT, NULL);

	if (nma_cert_chooser_button_get_remember_pin (button))
		pin = nma_cert_chooser_button_get_pin (button);
	if (pin)
		gtk_editable_set_text (GTK_EDITABLE (priv->cert_password), pin);

	gtk_widget_set_sensitive (priv->cert_password, uri_data != NULL);
	gtk_widget_set_sensitive (priv->cert_password_label, uri_data != NULL);

	if (!gtk_widget_get_sensitive (priv->key_button)) {
		gtk_widget_set_sensitive (priv->key_button, TRUE);
		gtk_widget_set_sensitive (priv->key_button_label, TRUE);

		if (uri_data) {
			/* URI that is good both for a certificate and for a key. */
			if (!gck_attributes_find (uri_data->attributes, CKA_CLASS)) {
				nma_cert_chooser_button_set_uri (NMA_CERT_CHOOSER_BUTTON (priv->key_button), uri);
				gtk_widget_set_sensitive (priv->key_password, TRUE);
				gtk_widget_set_sensitive (priv->key_password_label, TRUE);
				if (pin)
					gtk_editable_set_text (GTK_EDITABLE (priv->key_password), pin);
			}
		}
	}

	if (uri_data)
		gck_uri_data_free (uri_data);
	if (pin)
		g_free (pin);

	g_signal_emit_by_name (user_data, "changed");
}

static void
key_changed_cb (NMACertChooserButton *button, gpointer user_data)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (NMA_CERT_CHOOSER (user_data));
	gchar *pin = NULL;

	if (nma_cert_chooser_button_get_remember_pin (button))
		pin = nma_cert_chooser_button_get_pin (button);
	if (pin) {
		gtk_editable_set_text (GTK_EDITABLE (priv->key_password), pin);
		g_free (pin);
	}

	gtk_widget_set_sensitive (priv->key_password, TRUE);
	gtk_widget_set_sensitive (priv->key_password_label, TRUE);
	g_signal_emit_by_name (user_data, "changed");
}

static void
key_password_changed_cb (GtkEntry *entry, gpointer user_data)
{
	g_signal_emit_by_name (user_data, "changed");
}

static void
cert_password_changed_cb (GtkEntry *entry, gpointer user_data)
{
	g_signal_emit_by_name (user_data, "changed");
}


static void
show_toggled_cb (GtkCheckButton *button, gpointer user_data)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (NMA_CERT_CHOOSER (user_data));
	gboolean active;

	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
	gtk_entry_set_visibility (GTK_ENTRY (priv->cert_password), active);
	if (priv->key_password)
		gtk_entry_set_visibility (GTK_ENTRY (priv->key_password), active);
}

static void
set_title (NMACertChooser *cert_chooser, const gchar *title)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);
	gs_free gchar *mnemonic_escaped = NULL;
	gchar *text;
	char **split;

	split = g_strsplit (title, "_", -1);
	mnemonic_escaped = g_strjoinv("__", split);
	g_strfreev (split);

	text = g_strdup_printf (_("Choose a key for %s Certificate"), title);
	nma_cert_chooser_button_set_title (NMA_CERT_CHOOSER_BUTTON (priv->key_button), text);
	g_free (text);

	text = g_strdup_printf (_("%s private _key"), mnemonic_escaped);
	gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->key_button_label), text);
	g_free (text);

	text = g_strdup_printf (_("%s key _password"), mnemonic_escaped);
	gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->key_password_label), text);
	g_free (text);

	text = g_strdup_printf (_("Choose a %s Certificate"), title);
	nma_cert_chooser_button_set_title (NMA_CERT_CHOOSER_BUTTON (priv->cert_button), text);
	g_free (text);

	text = g_strdup_printf (_("%s _certificate"), mnemonic_escaped);
	gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->cert_button_label), text);
	g_free (text);

	text = g_strdup_printf (_("%s certificate _password"), mnemonic_escaped);
	gtk_label_set_text_with_mnemonic (GTK_LABEL (priv->cert_password_label), text);
	g_free (text);
}

static void
set_flags (NMACertChooser *cert_chooser, NMACertChooserFlags flags)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	if (flags & NMA_CERT_CHOOSER_FLAG_CERT) {
		gtk_widget_hide (priv->key_button);
		gtk_widget_hide (priv->key_button_label);
		gtk_widget_hide (priv->key_password);
		gtk_widget_hide (priv->key_password_label);
	}

	if (flags & NMA_CERT_CHOOSER_FLAG_PASSWORDS) {
		gtk_widget_hide (priv->cert_button);
		gtk_widget_hide (priv->cert_button_label);
		gtk_widget_hide (priv->key_button);
		gtk_widget_hide (priv->key_button_label);

		/* With FLAG_PASSWORDS the user can't pick a different key or a
		 * certificate, so there's no point in showing inactive password
		 * inputs. */
		if (!gtk_widget_get_sensitive (priv->cert_password)) {
			gtk_widget_hide (priv->cert_password);
			gtk_widget_hide (priv->cert_password_label);
		}
		if (!gtk_widget_get_sensitive (priv->key_password)) {
			gtk_widget_hide (priv->key_password);
			gtk_widget_hide (priv->key_password_label);
		}
		if (   !gtk_widget_get_visible (priv->cert_password)
		    && !gtk_widget_get_visible (priv->key_password)) {
			gtk_widget_hide (priv->show_password);
		}
	}
}

static void
init (NMACertChooser *cert_chooser)
{
	NMAPkcs11CertChooserPrivate *priv = NMA_PKCS11_CERT_CHOOSER_GET_PRIVATE (cert_chooser);

	gtk_grid_insert_column (GTK_GRID (cert_chooser), 2);
	gtk_grid_set_row_spacing (GTK_GRID (cert_chooser), 6);
	gtk_grid_set_column_spacing (GTK_GRID (cert_chooser), 6);

	/* Show password */
	gtk_grid_insert_row (GTK_GRID (cert_chooser), 0);
	priv->show_password = gtk_check_button_new_with_mnemonic _("Sho_w passwords");
	gtk_grid_attach (GTK_GRID (cert_chooser), priv->show_password, 1, 2, 1, 1);
	gtk_widget_show (priv->show_password);
	gtk_widget_set_no_show_all (priv->show_password, TRUE);
	g_signal_connect (priv->show_password, "toggled",
	                  G_CALLBACK (show_toggled_cb), cert_chooser);

	/* The key chooser */
	gtk_grid_insert_row (GTK_GRID (cert_chooser), 0);

	priv->key_button = nma_cert_chooser_button_new (NMA_CERT_CHOOSER_BUTTON_FLAG_KEY);

	gtk_grid_attach (GTK_GRID (cert_chooser), priv->key_button, 1, 0, 1, 1);
	gtk_widget_set_hexpand (priv->key_button, TRUE);
	gtk_widget_set_sensitive (priv->key_button, FALSE);
	gtk_widget_show (priv->key_button);
	gtk_widget_set_no_show_all (priv->key_button, TRUE);

        g_signal_connect (priv->key_button, "changed",
	                  G_CALLBACK (key_changed_cb), cert_chooser);

	priv->key_button_label = gtk_label_new (NULL);
	g_object_set (priv->key_button_label, "xalign", (gfloat) 1, NULL);
	gtk_label_set_mnemonic_widget (GTK_LABEL (priv->key_button_label), priv->key_button);
	gtk_grid_attach (GTK_GRID (cert_chooser), priv->key_button_label, 0, 0, 1, 1);
	gtk_widget_set_sensitive (priv->key_button_label, FALSE);
	gtk_widget_show (priv->key_button_label);
	gtk_widget_set_no_show_all (priv->key_button_label, TRUE);

	/* The key password entry */
	gtk_grid_insert_row (GTK_GRID (cert_chooser), 1);

	priv->key_password = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (priv->key_password), FALSE);
	gtk_grid_attach (GTK_GRID (cert_chooser), priv->key_password, 1, 1, 1, 1);
	gtk_widget_set_hexpand (priv->key_password, TRUE);
	gtk_widget_set_sensitive (priv->key_password, FALSE);
	gtk_widget_show (priv->key_password);
	gtk_widget_set_no_show_all (priv->key_password, TRUE);

	g_signal_connect (priv->key_password, "changed",
	                  G_CALLBACK (key_password_changed_cb), cert_chooser);

	priv->key_password_label = gtk_label_new (NULL);
	g_object_set (priv->key_password_label, "xalign", (gfloat) 1, NULL);
	gtk_label_set_mnemonic_widget (GTK_LABEL (priv->key_password_label), priv->key_password);
	gtk_grid_attach (GTK_GRID (cert_chooser), priv->key_password_label, 0, 1, 1, 1);
	gtk_widget_set_sensitive (priv->key_password_label, FALSE);
	gtk_widget_show (priv->key_password_label);
	gtk_widget_set_no_show_all (priv->key_password_label, TRUE);

	/* The certificate chooser */
	gtk_grid_insert_row (GTK_GRID (cert_chooser), 0);

	priv->cert_button = nma_cert_chooser_button_new (NMA_CERT_CHOOSER_BUTTON_FLAG_NONE);

	gtk_grid_attach (GTK_GRID (cert_chooser), priv->cert_button, 1, 0, 1, 1);
	gtk_widget_set_hexpand (priv->cert_button, TRUE);
	gtk_widget_show (priv->cert_button);
	gtk_widget_set_no_show_all (priv->cert_button, TRUE);

	g_signal_connect (priv->cert_button, "changed",
	                  G_CALLBACK (cert_changed_cb), cert_chooser);

	priv->cert_button_label = gtk_label_new (NULL);
	g_object_set (priv->cert_button_label, "xalign", (gfloat) 1, NULL);
	gtk_label_set_mnemonic_widget (GTK_LABEL (priv->cert_button_label), priv->cert_button);
	gtk_grid_attach (GTK_GRID (cert_chooser), priv->cert_button_label, 0, 0, 1, 1);
	gtk_widget_show (priv->cert_button_label);
	gtk_widget_set_no_show_all (priv->cert_button_label, TRUE);

	/* The cert password entry */
	gtk_grid_insert_row (GTK_GRID (cert_chooser), 1);

	priv->cert_password = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (priv->cert_password), FALSE);
	gtk_grid_attach (GTK_GRID (cert_chooser), priv->cert_password, 1, 1, 1, 1);
	gtk_widget_set_hexpand (priv->cert_password, TRUE);
	gtk_widget_set_sensitive (priv->cert_password, FALSE);
	gtk_widget_show (priv->cert_password);
	gtk_widget_set_no_show_all (priv->cert_password, TRUE);

	g_signal_connect (priv->cert_password, "changed",
	                  G_CALLBACK (cert_password_changed_cb), cert_chooser);

	priv->cert_password_label = gtk_label_new (NULL);
	g_object_set (priv->cert_password_label, "xalign", (gfloat) 1, NULL);
	gtk_label_set_mnemonic_widget (GTK_LABEL (priv->cert_password_label), priv->cert_password);
	gtk_grid_attach (GTK_GRID (cert_chooser), priv->cert_password_label, 0, 1, 1, 1);
	gtk_widget_set_sensitive (priv->cert_password_label, FALSE);
	gtk_widget_show (priv->cert_password_label);
	gtk_widget_set_no_show_all (priv->cert_password_label, TRUE);
}

const NMACertChooserVtable nma_cert_chooser_vtable_pkcs11 = {
	.init = init,

	.set_title = set_title,
	.set_flags = set_flags,
	.set_cert_uri = set_cert_uri,
	.get_cert_uri = get_cert_uri,

	.set_cert_password = set_cert_password,
	.get_cert_password = get_cert_password,
	.set_key_uri = set_key_uri,
	.get_key_uri = get_key_uri,
	.set_key_password = set_key_password,
	.get_key_password = get_key_password,

	.add_to_size_group = add_to_size_group,
	.validate = validate,

	.setup_key_password_storage = setup_key_password_storage,
	.update_key_password_storage = update_key_password_storage,
	.get_key_password_flags = get_key_password_flags,
	.setup_cert_password_storage = setup_cert_password_storage,
	.update_cert_password_storage = update_cert_password_storage,
	.get_cert_password_flags = get_cert_password_flags,
};
