SUBDIRS=. tests

noinst_LTLIBRARIES = libutils.la

libutils_la_SOURCES = \
	utils.c \
	utils.h \
	nmn-mobile-providers.h \
	nmn-mobile-providers.c \
	mobile-wizard.h \
	mobile-wizard.c

libutils_la_CPPFLAGS = \
	$(NMA_CFLAGS) \
	-DGLADEDIR=\""$(gladedir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	$(DISABLE_DEPRECATED) \
	-I${top_srcdir}/src/gconf-helpers \
	-I${top_srcdir}/src

libutils_la_LIBADD = $(NMA_LIBS)
