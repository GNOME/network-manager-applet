bin_PROGRAMS = nm-connection-editor

nm_connection_editor_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	-DICONDIR=\""$(datadir)/icons"\" \
	-DGLADEDIR=\""$(gladedir)"\" \
	-DBINDIR=\""$(bindir)"\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBDIR=\""$(libdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DNMALOCALEDIR=\"$(datadir)/locale\" \
	$(DBUS_CFLAGS) \
	$(DISABLE_DEPRECATED) \
	-I${top_srcdir}/src/gconf-helpers \
	-I${top_srcdir}/src/utils \
	-I${top_srcdir}/src/wireless-security \
	-I${top_builddir}/src/marshallers

nm_connection_editor_SOURCES = \
	nm-connection-editor.c \
	nm-connection-editor.h \
	nm-connection-list.c \
	nm-connection-list.h \
	main.c \
	ce-page.h \
	ce-page.c \
	page-wired.h \
	page-wired.c \
	page-wired-security.h \
	page-wired-security.c \
	page-wireless.h \
	page-wireless.c \
	page-wireless-security.h \
	page-wireless-security.c \
	page-ip4.h \
	page-ip4.c \
	page-ip6.h \
	page-ip6.c \
	page-dsl.h \
	page-dsl.c \
	page-mobile.h \
	page-mobile.c \
	page-ppp.h \
	page-ppp.c \
	page-vpn.h \
	page-vpn.c \
	vpn-helpers.h \
	vpn-helpers.c \
	ip4-routes-dialog.h \
	ip4-routes-dialog.c \
	ip6-routes-dialog.h \
	ip6-routes-dialog.c \
	ppp-auth-methods-dialog.c \
	ppp-auth-methods-dialog.h \
	ce-polkit-button.c \
	ce-polkit-button.h

nm-connection-editor-service-glue.h: $(top_srcdir)/src/connection-editor/nm-connection-editor-service.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_connection_editor_service --mode=glib-server --output=$@ $<

nm_connection_editor_LDADD = \
	$(top_builddir)/src/gconf-helpers/libgconf-helpers.la \
	${top_builddir}/src/wireless-security/libwireless-security.la \
	${top_builddir}/src/utils/libutils.la \
	${top_builddir}/src/marshallers/libmarshallers.la \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	-lm

gladedir = $(datadir)/nm-applet
glade_DATA = \
	nm-connection-editor.glade \
	ce-page-wired.glade \
	ce-page-wireless.glade \
	ce-page-wireless-security.glade \
	ce-page-ip4.glade \
	ce-page-ip6.glade \
	ce-page-dsl.glade \
	ce-page-mobile.glade \
	ce-page-ppp.glade \
	ce-vpn-wizard.glade

BUILT_SOURCES = nm-connection-editor-service-glue.h

CLEANFILES = *.bak *.gladep $(BUILT_SOURCES)

EXTRA_DIST = $(glade_DATA) nm-connection-editor-service.xml

