NULL=

bin_PROGRAMS = nm-connection-editor

nm_connection_editor_CPPFLAGS =			\
	$(NMA_CFLAGS)				\
	-DICONDIR=\""$(datadir)/icons"\"	\
	-DGLADEDIR=\""$(gladedir)"\"		\
	-DBINDIR=\""$(bindir)"\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\"	\
	-DLIBDIR=\""$(libdir)"\"	\
	-DNMALOCALEDIR=\"$(datadir)/locale\"	\
	$(DBUS_CFLAGS)				\
	$(POLKIT_CFLAGS) \
	$(DISABLE_DEPRECATED)			\
	-I${top_srcdir}/src/gconf-helpers  \
	-I${top_srcdir}/src/utils \
	-I${top_srcdir}/src/wireless-security \
	$(NULL)

if NO_POLKIT_GNOME
nm_connection_editor_CPPFLAGS += \
	-I${top_srcdir}/src/polkit-helpers
endif

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
	page-dsl.h \
	page-dsl.c \
	page-mobile.h \
	page-mobile.c \
	mobile-wizard.h \
	mobile-wizard.c \
	page-ppp.h \
	page-ppp.c \
	page-vpn.h \
	page-vpn.c \
	vpn-helpers.h \
	vpn-helpers.c \
	ip4-routes-dialog.h \
	ip4-routes-dialog.c

nm-connection-editor-service-glue.h: $(top_srcdir)/src/connection-editor/nm-connection-editor-service.xml
	dbus-binding-tool --prefix=nm_connection_editor_service --mode=glib-server --output=$@ $<

nm_connection_editor_LDADD = \
	$(top_builddir)/src/gconf-helpers/libgconf-helpers.la \
	${top_builddir}/src/wireless-security/libwireless-security.la \
	${top_builddir}/src/utils/libutils.la \
	$(NMA_LIBS)

if NO_POLKIT_GNOME
nm_connection_editor_LDADD += \
	${top_builddir}/src/polkit-helpers/libpolkit-helpers.la
endif

gladedir = $(datadir)/nm-applet
glade_DATA = \
	nm-connection-editor.glade \
	ce-page-wired.glade \
	ce-page-wireless.glade \
	ce-page-wireless-security.glade \
	ce-page-ip4.glade \
	ce-page-dsl.glade \
	ce-page-mobile.glade \
	ce-mobile-wizard.glade \
	ce-page-ppp.glade \
	ce-vpn-wizard.glade

BUILT_SOURCES = nm-connection-editor-service-glue.h

CLEANFILES = *.bak *.gladep $(BUILT_SOURCES)

EXTRA_DIST = $(glade_DATA) nm-connection-editor-service.xml

