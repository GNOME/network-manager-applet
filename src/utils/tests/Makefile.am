INCLUDES = -I$(top_srcdir)/src/utils

noinst_PROGRAMS = test-utils

test_utils_SOURCES = test-utils.c

test_utils_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	$(GNOME_KEYRING_CFLAGS)

test_utils_LDADD = \
	${top_builddir}/src/utils/libutils.la \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	$(GNOME_KEYRING_LIBS)

check-local: test-utils
	$(abs_builddir)/test-utils

