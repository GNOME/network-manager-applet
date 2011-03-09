noinst_LTLIBRARIES = libgconf-helpers.la

libgconf_helpers_la_SOURCES = \
	gconf-helpers.h \
	gconf-helpers.c \
	gconf-upgrade.h \
	gconf-upgrade.c

libgconf_helpers_la_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	$(GCONF_CFLAGS) \
	$(GNOME_KEYRING_CFLAGS) \
	$(DISABLE_DEPRECATED) \
	-I${top_srcdir}/src \
	-I${top_srcdir}/src/utils

libgconf_helpers_la_LIBADD = \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	$(GCONF_LIBS) \
	$(GNOME_KEYRING_LIBS) \
	${top_builddir}/src/utils/libutils.la

