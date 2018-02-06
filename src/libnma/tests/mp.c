#include <gtk/gtk.h>
#include "nma-mobile-wizard.h"

static void
wizard_cb (NMAMobileWizard *self, gboolean canceled, NMAMobileWizardAccessMethod *method, gpointer user_data)
{
	g_printerr ("cb\n");
	gtk_main_quit ();
}

static p (void)
{
	GtkWidget *dialog;
	NMClient *client = NULL;
	NMConnection *connection = NULL;
	NMDevice *device = NULL;
	NMAccessPoint *ap = NULL;
	gboolean secrets_only = FALSE;

	client = nm_client_new (NULL, NULL);
	connection = nm_client_get_connection_by_id (client, "Red Hat");

	g_printerr (">%p<\n", connection);
	dialog = nma_wifi_dialog_new (client, connection, device, ap, secrets_only);
	gtk_dialog_run (GTK_DIALOG (dialog));
}

int
main (int argc, char *argv[])
{
	NMAMobileWizard *wizard;

#if GTK_CHECK_VERSION(3,90,0)
	gtk_init ();
#else
	gtk_init (&argc, &argv);
#endif

	p ();
	return;
	wizard = nma_mobile_wizard_new (NULL, NULL, NM_DEVICE_MODEM_CAPABILITY_NONE, TRUE, wizard_cb, NULL);

	nma_mobile_wizard_present (wizard);
	gtk_main ();
	nma_mobile_wizard_destroy (wizard);
}
