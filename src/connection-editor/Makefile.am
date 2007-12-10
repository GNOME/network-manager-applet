NULL=

bin_PROGRAMS = nm-connection-editor

nm_connection_editor_CPPFLAGS =			\
	$(NMA_CFLAGS)				\
	-DICONDIR=\""$(datadir)/icons"\"	\
	-DGLADEDIR=\""$(gladedir)"\"		\
	-DBINDIR=\""$(bindir)"\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\"	\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"	\
	$(DBUS_CFLAGS)				\
	$(DISABLE_DEPRECATED)			\
	-I${top_srcdir}/src/gconf-helpers  \
	$(NULL)

nm_connection_editor_SOURCES =	\
	byte-array-editor.c	\
	byte-array-editor.h	\
	nm-connection-editor.c	\
	nm-connection-editor.h	\
	nm-connection-list.c	\
	nm-connection-list.h	\
	main.c

nm_connection_editor_LDADD = \
	$(top_builddir)/src/gconf-helpers/libgconf-helpers.la \
	$(NMA_LIBS)

gladedir = $(datadir)/nm-applet
glade_DATA = nm-connection-editor.glade

CLEANFILES = *.bak *.gladep

EXTRA_DIST = 		\
	$(glade_DATA)	\
	$(NULL)
