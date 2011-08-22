bin_PROGRAMS = nm-connection-editor

nm_connection_editor_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	-DICONDIR=\""$(datadir)/icons"\" \
	-DUIDIR=\""$(uidir)"\" \
	-DBINDIR=\""$(bindir)"\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBDIR=\""$(libdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DNMALOCALEDIR=\"$(datadir)/locale\" \
	$(DBUS_CFLAGS) \
	$(DISABLE_DEPRECATED) \
	-I${top_srcdir}/src/utils \
	-I${top_srcdir}/src/wireless-security \
	-I${top_srcdir}/src/libnm-gtk \
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
	${top_builddir}/src/wireless-security/libwireless-security.la \
	${top_builddir}/src/utils/libutils.la \
	${top_builddir}/src/marshallers/libmarshallers.la \
	${top_builddir}/src/libnm-gtk/libnm-gtk.la \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	-lm

uidir = $(datadir)/nm-applet
ui_DATA = \
	nm-connection-editor.ui \
	ce-page-wired.ui \
	ce-page-wireless.ui \
	ce-page-wireless-security.ui \
	ce-page-ip4.ui \
	ce-ip4-routes.ui \
	ce-page-ip6.ui \
	ce-ip6-routes.ui \
	ce-page-dsl.ui \
	ce-page-mobile.ui \
	ce-page-ppp.ui \
	ce-ppp-auth-methods.ui \
	ce-vpn-wizard.ui

BUILT_SOURCES = nm-connection-editor-service-glue.h

CLEANFILES = *.bak $(BUILT_SOURCES)

EXTRA_DIST = $(ui_DATA) nm-connection-editor-service.xml

