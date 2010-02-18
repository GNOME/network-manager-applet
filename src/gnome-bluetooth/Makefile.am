plugindir = $(libdir)/gnome-bluetooth/plugins

INCLUDES = \
	-DDATADIR=\"$(datadir)\" \
	-DICONDIR=\"$(icondir)\" \
	-DLOCALEDIR="\"$(datadir)/locale\""	\
	-I$(top_srcdir)/src/gconf-helpers/ \
	-I$(top_builddir) \
	-I${top_builddir}/src/marshallers \
	-I${top_srcdir}/src/utils \
	$(GNOME_BLUETOOTH_CFLAGS) \
	$(WARN_CFLAGS)

if HAVE_GBT
plugin_LTLIBRARIES = libnma.la

libnma_la_SOURCES = \
	network-manager-applet.c \
	nma-bling-spinner.c \
	nma-bling-spinner.h

libnma_la_LDFLAGS = -module -avoid-version

libnma_la_LIBADD = \
	$(top_builddir)/src/gconf-helpers/libgconf-helpers.la \
	$(GNOME_BLUETOOTH_LIBS)
endif

EXTRA_DIST = \
	network-manager-applet.c \
	nma-bling-spinner.c \
	nma-bling-spinner.h
