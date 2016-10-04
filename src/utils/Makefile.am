SUBDIRS= . tests

shared_files = \
	$(top_srcdir)/shared/nm-utils/nm-shared-utils.c \
	$(top_srcdir)/shared/nm-utils/nm-shared-utils.h \
	$(NULL)

noinst_LTLIBRARIES = \
	libutils-libnm-glib.la \
	libutils-libnm.la

libutils_libnm_glib_la_SOURCES = \
	$(shared_files) \
	utils.c \
	utils.h

libutils_libnm_glib_la_CPPFLAGS = \
	-DNETWORKMANAGER_COMPILATION=NM_NETWORKMANAGER_COMPILATION_LIB_LEGACY \
	$(GTK_CFLAGS) \
	$(LIBNM_GLIB_CFLAGS) \
	-I$(top_srcdir)/shared/

libutils_libnm_glib_la_LIBADD = \
	$(GTK_LIBS) \
	$(LIBNM_GLIB_LIBS)

libutils_libnm_la_SOURCES = \
	$(shared_files) \
	utils.c \
	utils.h

libutils_libnm_la_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(LIBNM_CFLAGS) \
	-I$(top_srcdir)/shared/

libutils_libnm_la_LIBADD = \
	$(GTK_LIBS) \
	$(LIBNM_LIBS)
