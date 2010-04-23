noinst_LTLIBRARIES = libgconf-helpers.la

libgconf_helpers_la_SOURCES =	\
	gconf-helpers.h		\
	gconf-helpers.c		\
	gconf-upgrade.h		\
	gconf-upgrade.c		\
	nma-gconf-connection.h	\
	nma-gconf-connection.c	\
	nma-gconf-settings.h	\
	nma-gconf-settings.c

libgconf_helpers_la_CPPFLAGS = \
	$(NMA_CFLAGS) \
	$(DISABLE_DEPRECATED) \
	-I${top_srcdir}/src \
	-I${top_builddir}/src/marshallers \
	-I${top_srcdir}/src/utils

libgconf_helpers_la_LIBADD = \
	$(NMA_LIBS) \
	${top_builddir}/src/marshallers/libmarshallers.la \
	${top_builddir}/src/utils/libutils.la

