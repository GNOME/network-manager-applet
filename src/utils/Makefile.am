SUBDIRS=. tests

noinst_LTLIBRARIES = libutils.la

libutils_la_SOURCES = \
	nm-glib-compat.h \
	utils.c \
	utils.h

libutils_la_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	-I${top_srcdir}/src

libutils_la_LIBADD = $(GTK_LIBS) $(NMA_LIBS)
