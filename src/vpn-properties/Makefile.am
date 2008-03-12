
bin_PROGRAMS = nm-vpn-properties

nm_vpn_propertiesdir=$(includedir)/NetworkManager

nm_vpn_properties_HEADERS = 					\
	nm-vpn-ui-interface.h

nm_vpn_properties_SOURCES = 					\
					nm-vpn-properties.c		\
					clipboard.c			\
					clipboard.h			\
					nm-vpn-ui-interface.h

gladedir = $(datadir)/gnome-vpn-properties
glade_DATA = nm-vpn-properties.glade

nm_vpn_properties_CFLAGS = \
	$(NMA_CFLAGS) \
	-DICONDIR=\""$(datadir)/icons"\" \
	-DGLADEDIR=\""$(gladedir)"\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DG_DISABLE_DEPRECATED \
	-DGDK_DISABLE_DEPRECATED \
	-DNMALOCALEDIR=\"$(datadir)/locale\" \
	-I${top_srcdir}/src/gconf-helpers  \
	-DVERSION=\"$(VERSION)\"

nm_vpn_properties_LDADD = \
	$(NMA_LIBS) \
	${top_builddir}/src/gconf-helpers/libgconf-helpers.la

CLEANFILES = $(server_DATA) *.bak *.gladep *~

EXTRA_DIST = $(glade_DATA)



