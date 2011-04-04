INCLUDES = -I$(top_srcdir)/src/utils

noinst_PROGRAMS = test-upgrade

test_upgrade_SOURCES = \
	test-upgrade.c \
	fake-keyring.h \
	fake-keyring.c \
	fake-gconf.h \
	fake-gconf.c

test_upgrade_CPPFLAGS = \
	-I ${srcdir}/../ \
	-DTESTDIR=\"$(srcdir)\" \
	$(NMA_CFLAGS) \
	$(GCONF_CFLAGS) \
	$(GNOME_KEYRING_CFLAGS)

test_upgrade_LDADD = \
	${builddir}/../libgconf-helpers-test.la \
	$(NMA_LIBS)

check-local: test-upgrade
	$(abs_builddir)/test-upgrade

EXTRA_DIST = \
	test-import.xml \
	08wifi.xml \
	08vpnc.xml \
	08openvpn-not-saved.xml \
	08openvpn-saved.xml

