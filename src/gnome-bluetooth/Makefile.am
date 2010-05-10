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
	$(DISABLE_DEPRECATED) \
	$(WARN_CFLAGS)

BT_WIDGET_SOURCES = bt-widget.c

if HAVE_GBT
plugin_LTLIBRARIES = libnma.la

libnma_la_SOURCES = $(BT_WIDGET_SOURCES)

libnma_la_LDFLAGS = -module -avoid-version

libnma_la_LIBADD = \
	$(top_builddir)/src/gconf-helpers/libgconf-helpers.la \
	$(GNOME_BLUETOOTH_LIBS)
endif

EXTRA_DIST = $(BT_WIDGET_SOURCES)
