noinst_LTLIBRARIES = libgconf-helpers.la

libgconf_helpers_la_SOURCES =	\
	gconf-helpers.h		\
	gconf-helpers.c		\
	gconf-upgrade.h		\
	gconf-upgrade.c

libgconf_helpers_la_CPPFLAGS = \
	$(NMA_CFLAGS) \
	-I${top_srcdir}/src \
	-I${top_srcdir}/src/utils

libgconf_helpers_la_LIBADD = \
	$(NMA_LIBS) \
	${top_builddir}/src/utils/libutils.la

