#include <gtk/gtk.h>
#include "nma-vpn-password-dialog.h"

static void
_print_value (GValue *val)
{
#if 0
     11 GdkEventMask
     11 GtkContainer
     11 GtkStyle
     14 gfloat
     15 gboolean
     17 gchararray
      1 GtkCellArea
      1 GtkTreeModel
      1 GtkWidget
      2 GtkOrientation
      4 gint
      5 guint

GdkEventMask
GtkCellArea
GtkContainer
GtkOrientation
GtkStyle
GtkTreeModel
GtkWidget
#endif

	if (G_VALUE_HOLDS_BOOLEAN (val))
		g_printerr (g_value_get_boolean (val) ? "True" : "False");
	else if (G_VALUE_HOLDS_STRING (val))
		g_printerr ("%s", g_value_get_string (val));
	else if (G_VALUE_HOLDS_FLOAT (val))
		g_printerr ("%f", g_value_get_float (val));
	else if (G_VALUE_HOLDS_INT (val))
		g_printerr ("%d", g_value_get_int (val));
	else if (G_VALUE_HOLDS_UINT (val))
		g_printerr ("%u", g_value_get_uint (val));
	else if (G_VALUE_HOLDS (val, GTK_TYPE_WIDGET))
		g_printerr ("widget_%p", GTK_WIDGET (g_value_get_object (val)));
	else if (G_VALUE_HOLDS_ENUM (val))
		g_printerr ("%d", g_value_get_enum (val));
	else if (G_VALUE_HOLDS_FLAGS (val))
		g_printerr ("%d", g_value_get_flags (val));
	else
		g_printerr ("REMOVEME");

#if 0
	else if (G_VALUE_HOLDS (val, GTK_TYPE_WIDGET))
		g_printerr ("WIDGET:%s", gtk_widget_get_name (GTK_WIDGET (g_value_get_object (val))));
	else if (G_VALUE_HOLDS_ENUM (val))
		g_printerr ("ENUM:%s", g_enum_to_string (G_VALUE_TYPE (val), g_value_get_enum (val)));
	else if (G_VALUE_HOLDS_FLAGS (val))
		g_printerr ("FLAGS:%s", g_flags_to_string (G_VALUE_TYPE (val), g_value_get_flags (val)));
	else
		g_printerr ("{%p}", val);
#endif

//g_enum_to_string (GType g_enum_type, gint value);

}

static void
_i (guint indent)
{
	int i;
	for (i = 0; i < indent; i++)
		g_printerr ("  ");
}

static void
print_widgets (GtkWidget *widget, gpointer data);

static void
print_root (GtkWidget *widget)
{
	g_printerr ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	g_printerr ("<!-- Generated with glade 3.20.2 -->\n");
	g_printerr ("<interface>\n");
	g_printerr ("  <requires lib=\"gtk+\" version=\"3.20\"/>\n");
	print_widgets (widget, GUINT_TO_POINTER (1));
	g_printerr ("</interface>\n");
}

static int child_position;
static const char *child_title;

static void
print_child (GtkWidget *widget, gpointer data)
{
	guint indent = GPOINTER_TO_UINT (data);
	const char *title = child_title;

	_i (indent);
	g_printerr ("<child>\n");
	print_widgets (widget, GUINT_TO_POINTER (indent + 1));
	_i (indent + 1);
	g_printerr ("<packing>\n");
	_i (indent + 2);
	g_printerr ("<property name=\"position\">%d</property>\n", child_position++);

	if (title) {
		_i (indent + 2);
		g_printerr ("<property name=\"title\">%s</property>\n", title);
	}

	_i (indent + 1);
	g_printerr ("</packing>\n");
	_i (indent);
	g_printerr ("</child>\n");
}

static void
print_widgets (GtkWidget *widget, gpointer data)
{
	guint indent = GPOINTER_TO_UINT (data);
	GParamSpec **props;
	guint n_properties = 0;
	int i;

	_i (indent);
	//  <object class="GtkAssistant" id="assistant">
	g_printerr ("<object class=\"%s\" id=\"widget_%p\">\n", G_OBJECT_TYPE_NAME (widget), widget);
	//g_printerr (">%s %s<\n", gtk_widget_get_name (widget), G_OBJECT_TYPE_NAME (widget));

	props = g_object_class_list_properties (G_OBJECT_GET_CLASS (widget), &n_properties);
	for (i = 0; i < n_properties; i++) {
		GValue val = { 0, };

		if (!(props[i]->flags & G_PARAM_READABLE))
			continue;

		g_value_init (&val, G_PARAM_SPEC_VALUE_TYPE (props[i]));
		g_object_get_property (G_OBJECT (widget), g_param_spec_get_name (props[i]), &val);
		if (g_param_value_defaults (props[i], &val))
			continue;

		_i (indent + 1);
		// <property name="can_focus">False</property>
		g_printerr ("<property name=\"%s\">", g_param_spec_get_name (props[i]));
		//g_printerr ("  [%s] {%s} -- ", g_param_spec_get_name (props[i]), g_type_name (G_PARAM_SPEC_VALUE_TYPE (props[i])));
		_print_value (&val);
		g_printerr ("</property>\n");
		//g_printerr ("\n");
		//g_printerr ("XXX %s\n", g_type_name (G_PARAM_SPEC_VALUE_TYPE (props[i])));

		//val = g_param_spec_get_value (props[i]);
	}

	child_title = NULL;
	child_position = 0;
	if (GTK_IS_ASSISTANT (widget)) {
		gint n_pages;

		n_pages = gtk_assistant_get_n_pages (GTK_ASSISTANT (widget));
		for (i = 0; i < n_pages; i++) {
			child_position = i;
			child_title = gtk_assistant_get_page_title (GTK_ASSISTANT (widget), gtk_assistant_get_nth_page (GTK_ASSISTANT (widget), i));
			print_child (gtk_assistant_get_nth_page (GTK_ASSISTANT (widget), i), GUINT_TO_POINTER (indent + 1));
		}

	} else if (GTK_IS_CONTAINER (widget)) {
		gtk_container_foreach (GTK_CONTAINER (widget), print_child, GUINT_TO_POINTER (indent + 1));
	}

	_i (indent);
	g_printerr ("</object>\n");
}

#if 0
static void
wizard_cb (NMAMobileWizard *self, gboolean canceled, NMAMobileWizardAccessMethod *method, gpointer user_data)
{
	g_printerr ("cb\n");
	gtk_main_quit ();
}

/* Attribute mutators */
void nma_vpn_password_dialog_set_password                 (NMAVpnPasswordDialog *dialog,
                                                           const char           *password);
void nma_vpn_password_dialog_set_password_label           (NMAVpnPasswordDialog *dialog,
                                                           const char           *label);

void nma_vpn_password_dialog_set_show_password_secondary  (NMAVpnPasswordDialog *dialog,
                                                           gboolean              show);
void nma_vpn_password_dialog_focus_password_secondary     (NMAVpnPasswordDialog *dialog);
void nma_vpn_password_dialog_set_password_secondary       (NMAVpnPasswordDialog *dialog,
                                                           const char           *password_secondary);
void nma_vpn_password_dialog_set_password_secondary_label (NMAVpnPasswordDialog *dialog,
                                                           const char           *label);

void nma_vpn_password_dialog_set_show_password_ternary  (NMAVpnPasswordDialog *dialog,
                                                         gboolean              show);
void nma_vpn_password_dialog_focus_password_ternary     (NMAVpnPasswordDialog *dialog);
void nma_vpn_password_dialog_set_password_ternary       (NMAVpnPasswordDialog *dialog,
                                                         const char           *password_ternary);
void nma_vpn_password_dialog_set_password_ternary_label (NMAVpnPasswordDialog *dialog,
                                                         const char           *label);
#endif



int
main (int argc, char *argv[])
{
	GtkWidget *widget;

#if GTK_CHECK_VERSION(3,90,0)
	gtk_init ();
#else
	gtk_init (&argc, &argv);
#endif

	widget = nma_vpn_password_dialog_new ("ABC", "DEF", "GHI");

	nma_vpn_password_dialog_set_password (NMA_VPN_PASSWORD_DIALOG (widget), "XPASSWORD");
	nma_vpn_password_dialog_set_password_label (NMA_VPN_PASSWORD_DIALOG (widget), "XLABEL");
	nma_vpn_password_dialog_set_show_password (NMA_VPN_PASSWORD_DIALOG (widget), TRUE);

	nma_vpn_password_dialog_set_password_secondary (NMA_VPN_PASSWORD_DIALOG (widget), "XPASSWORD_secondary");
	nma_vpn_password_dialog_set_password_secondary_label (NMA_VPN_PASSWORD_DIALOG (widget), "XLABEL 2");
	nma_vpn_password_dialog_set_show_password_secondary (NMA_VPN_PASSWORD_DIALOG (widget), TRUE);

	nma_vpn_password_dialog_set_password_ternary (NMA_VPN_PASSWORD_DIALOG (widget), "XPASSWORD_ternary");
	nma_vpn_password_dialog_set_password_ternary_label (NMA_VPN_PASSWORD_DIALOG (widget), "XLABEL 3");
	nma_vpn_password_dialog_set_show_password_ternary (NMA_VPN_PASSWORD_DIALOG (widget), TRUE);

	if (0)
		print_root (widget);
	//gtk_dialog_run (GTK_DIALOG (widget));
	nma_vpn_password_dialog_run_and_block (NMA_VPN_PASSWORD_DIALOG (widget));
	//gtk_main ();
}

 
