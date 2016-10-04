noinst_PROGRAMS = test-utils

test_utils_SOURCES = test-utils.c

test_utils_CPPFLAGS = \
	"-I${top_srcdir}/shared/" \
	-I$(top_srcdir)/src/utils \
	$(GTK_CFLAGS) \
	$(LIBNM_CFLAGS)

test_utils_LDADD = \
	${top_builddir}/src/utils/libutils-libnm.la \
	$(GTK_LIBS) \
	$(LIBNM_LIBS)

TESTS = $(noinst_PROGRAMS)
