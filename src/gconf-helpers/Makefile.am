SUBDIRS=. tests

noinst_LTLIBRARIES = \
	libgconf-helpers.la \
	libgconf-helpers-test.la

HELPERS_SOURCES = \
	gconf-helpers.h \
	gconf-helpers.c \
	gconf-upgrade.h \
	gconf-upgrade.c

libgconf_helpers_la_SOURCES = $(HELPERS_SOURCES)

libgconf_helpers_la_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	$(GCONF_CFLAGS) \
	$(GNOME_KEYRING_CFLAGS) \
	$(DISABLE_DEPRECATED)

libgconf_helpers_la_LIBADD = \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	$(GCONF_LIBS) \
	$(GNOME_KEYRING_LIBS)

#########################
# Test library
#########################

libgconf_helpers_test_la_SOURCES = $(HELPERS_SOURCES)

libgconf_helpers_test_la_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	$(GCONF_CFLAGS) \
	$(GNOME_KEYRING_CFLAGS) \
	$(DISABLE_DEPRECATED)

# no keyring or gconf libs since we'll override them
libgconf_helpers_test_la_LIBADD = \
	$(NMA_LIBS)

