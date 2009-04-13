noinst_LTLIBRARIES = libutils.la

libutils_la_SOURCES = \
	gnome-keyring-md5.h \
	gnome-keyring-md5.c \
	utils.c \
	utils.h

libutils_la_CPPFLAGS = \
	$(NMA_CFLAGS) \
	-I${top_srcdir}/src/gconf-helpers \
	-I${top_srcdir}/src

libutils_la_LIBADD = $(NMA_LIBS)
