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
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	-DUIDIR=\""$(uidir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	$(DISABLE_DEPRECATED) \
	-I${top_srcdir}/src/gconf-helpers \
	-I${top_srcdir}/src

libutils_la_LIBADD = $(GTK_LIBS) $(NMA_LIBS)
