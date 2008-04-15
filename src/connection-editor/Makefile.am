NULL=

bin_PROGRAMS = nm-connection-editor

nm_connection_editor_CPPFLAGS =			\
	$(NMA_CFLAGS)				\
	-DICONDIR=\""$(datadir)/icons"\"	\
	-DGLADEDIR=\""$(gladedir)"\"		\
	-DBINDIR=\""$(bindir)"\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\"	\
	-DNMALOCALEDIR=\"$(datadir)/locale\"	\
	$(DBUS_CFLAGS)				\
	$(DISABLE_DEPRECATED)			\
	-I${top_srcdir}/src/gconf-helpers  \
	-I${top_srcdir}/src/utils \
	-I${top_srcdir}/src/wireless-security \
	$(NULL)

nm_connection_editor_SOURCES = \
	byte-array-editor.c \
	byte-array-editor.h \
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
	page-ip4-address.h \
	page-ip4-address.c \
	page-dsl.h \
	page-dsl.c \
	page-mobile.h \
	page-mobile.c

nm_connection_editor_LDADD = \
	$(top_builddir)/src/gconf-helpers/libgconf-helpers.la \
	${top_builddir}/src/wireless-security/libwireless-security.la \
	${top_builddir}/src/utils/libutils.la \
	$(NMA_LIBS)

gladedir = $(datadir)/nm-applet
glade_DATA = \
	nm-connection-editor.glade \
	ce-page-wired.glade \
	ce-page-wireless.glade \
	ce-page-wireless-security.glade \
	ce-page-ip4.glade \
	ce-page-ip4-address.glade \
	ce-page-dsl.glade \
	ce-page-mobile.glade

CLEANFILES = *.bak *.gladep

EXTRA_DIST = 		\
	$(glade_DATA)	\
	$(NULL)
