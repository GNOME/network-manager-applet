SUBDIRS=. tests

noinst_LTLIBRARIES = libutils.la

libutils_la_SOURCES = \
	utils.c \
	utils.h \
	nmn-mobile-providers.h \
	nmn-mobile-providers.c \
	mobile-wizard.h \
	mobile-wizard.c

if USE_GTK2
libutils_la_SOURCES += \
	nma-bling-spinner.h \
	nma-bling-spinner.c
endif

libutils_la_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	-DGLADEDIR=\""$(gladedir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	$(DISABLE_DEPRECATED) \
	-I${top_srcdir}/src/gconf-helpers \
	-I${top_srcdir}/src

libutils_la_LIBADD = $(GTK_LIBS) $(NMA_LIBS)
