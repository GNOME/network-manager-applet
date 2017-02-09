include $(GLIB_MAKEFILE)
-include $(INTROSPECTION_MAKEFILE)

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

BUILT_SOURCES =

lib_LTLIBRARIES =

noinst_LTLIBRARIES =

noinst_PROGRAMS =

check_PROGRAMS =

check_programs =

check_local =

TESTS =

EXTRA_DIST =

CLEANFILES =

DISTCLEANFILES =

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-more-warnings=yes

pkgconfig_DATA =

pkgconfigdir = $(libdir)/pkgconfig

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)
BUILT_SOURCES += $(gir_DATA)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)
BUILT_SOURCES += $(typelib_DATA)

INTROSPECTION_GIRS =

SUBDIRS = \
	. \
	src \
	po \
	man \
	shared

###############################################################################

icon16dir = $(datadir)/icons/hicolor/16x16/apps
icon16_DATA = \
	icons/16/nm-device-wired.png \
	icons/16/nm-no-connection.png \
	icons/16/nm-vpn-standalone-lock.png
EXTRA_DIST += $(icon16_DATA)

icon22dir = $(datadir)/icons/hicolor/22x22/apps
icon22_DATA = \
	icons/22/nm-adhoc.png \
	icons/22/nm-device-wired.png \
	icons/22/nm-device-wwan.png \
	icons/22/nm-mb-roam.png \
	icons/22/nm-no-connection.png \
	icons/22/nm-secure-lock.png \
	icons/22/nm-signal-00.png \
	icons/22/nm-signal-100.png \
	icons/22/nm-signal-25.png \
	icons/22/nm-signal-50.png \
	icons/22/nm-signal-75.png \
	icons/22/nm-stage01-connecting01.png \
	icons/22/nm-stage01-connecting02.png \
	icons/22/nm-stage01-connecting03.png \
	icons/22/nm-stage01-connecting04.png \
	icons/22/nm-stage01-connecting05.png \
	icons/22/nm-stage01-connecting06.png \
	icons/22/nm-stage01-connecting07.png \
	icons/22/nm-stage01-connecting08.png \
	icons/22/nm-stage01-connecting09.png \
	icons/22/nm-stage01-connecting10.png \
	icons/22/nm-stage01-connecting11.png \
	icons/22/nm-stage02-connecting01.png \
	icons/22/nm-stage02-connecting02.png \
	icons/22/nm-stage02-connecting03.png \
	icons/22/nm-stage02-connecting04.png \
	icons/22/nm-stage02-connecting05.png \
	icons/22/nm-stage02-connecting06.png \
	icons/22/nm-stage02-connecting07.png \
	icons/22/nm-stage02-connecting08.png \
	icons/22/nm-stage02-connecting09.png \
	icons/22/nm-stage02-connecting10.png \
	icons/22/nm-stage02-connecting11.png \
	icons/22/nm-stage03-connecting01.png \
	icons/22/nm-stage03-connecting02.png \
	icons/22/nm-stage03-connecting03.png \
	icons/22/nm-stage03-connecting04.png \
	icons/22/nm-stage03-connecting05.png \
	icons/22/nm-stage03-connecting06.png \
	icons/22/nm-stage03-connecting07.png \
	icons/22/nm-stage03-connecting08.png \
	icons/22/nm-stage03-connecting09.png \
	icons/22/nm-stage03-connecting10.png \
	icons/22/nm-stage03-connecting11.png \
	icons/22/nm-tech-3g.png \
	icons/22/nm-tech-cdma-1x.png \
	icons/22/nm-tech-edge.png \
	icons/22/nm-tech-evdo.png \
	icons/22/nm-tech-gprs.png \
	icons/22/nm-tech-hspa.png \
	icons/22/nm-tech-lte.png \
	icons/22/nm-tech-umts.png \
	icons/22/nm-vpn-active-lock.png \
	icons/22/nm-vpn-connecting01.png \
	icons/22/nm-vpn-connecting02.png \
	icons/22/nm-vpn-connecting03.png \
	icons/22/nm-vpn-connecting04.png \
	icons/22/nm-vpn-connecting05.png \
	icons/22/nm-vpn-connecting06.png \
	icons/22/nm-vpn-connecting07.png \
	icons/22/nm-vpn-connecting08.png \
	icons/22/nm-vpn-connecting09.png \
	icons/22/nm-vpn-connecting10.png \
	icons/22/nm-vpn-connecting11.png \
	icons/22/nm-vpn-connecting12.png \
	icons/22/nm-vpn-connecting13.png \
	icons/22/nm-vpn-connecting14.png \
	icons/22/nm-wwan-tower.png
EXTRA_DIST += $(icon22_DATA)

icon32dir = $(datadir)/icons/hicolor/32x32/apps
icon32_DATA = \
	icons/32/nm-device-wired.png \
	icons/32/nm-no-connection.png
EXTRA_DIST += $(icon32_DATA)

icon48dir = $(datadir)/icons/hicolor/48x48/apps
icon48_DATA = \
	icons/48/nm-device-wireless.png
EXTRA_DIST += $(icon48_DATA)

iconscalabledir=$(datadir)/icons/hicolor/scalable/apps
iconscalable_DATA = \
	icons/scalable/nm-device-wired.svg \
	icons/scalable/nm-no-connection.svg
EXTRA_DIST += $(iconscalable_DATA)

###############################################################################

shared_files = \
	shared/nm-utils/nm-shared-utils.c \
	shared/nm-utils/nm-shared-utils.h \
	$(NULL)


noinst_LTLIBRARIES += src/utils/libutils-libnm.la

src_utils_libutils_libnm_la_CPPFLAGS = \
	"-I$(srcdir)/shared" \
	$(GTK_CFLAGS) \
	$(LIBNM_CFLAGS)

src_utils_libutils_libnm_la_SOURCES = \
	$(shared_files) \
	src/utils/utils.c \
	src/utils/utils.h

src_utils_libutils_libnm_la_LIBADD = \
	$(GTK_LIBS) \
	$(LIBNM_LIBS)


noinst_LTLIBRARIES += src/utils/libutils-libnm-glib.la

src_utils_libutils_libnm_glib_la_CPPFLAGS = \
	-DNETWORKMANAGER_COMPILATION=NM_NETWORKMANAGER_COMPILATION_LIB_LEGACY \
	"-I$(srcdir)/shared" \
	$(GTK_CFLAGS) \
	$(LIBNM_GLIB_CFLAGS)

src_utils_libutils_libnm_glib_la_SOURCES = \
	$(shared_files) \
	src/utils/utils.c \
	src/utils/utils.h

src_utils_libutils_libnm_glib_la_LIBADD = \
	$(GTK_LIBS) \
	$(LIBNM_GLIB_LIBS)


check_programs += src/utils/tests/test-utils

src_utils_tests_test_utils_SOURCES = src/utils/tests/test-utils.c

src_utils_tests_test_utils_CPPFLAGS = \
	"-I$(srcdir)/shared/" \
	"-I$(srcdir)/src/utils" \
	$(GTK_CFLAGS) \
	$(LIBNM_CFLAGS)

src_utils_tests_test_utils_LDADD = \
	src/utils/libutils-libnm.la \
	$(GTK_LIBS) \
	$(LIBNM_LIBS)

###############################################################################

BUILT_SOURCES += \
	src/wireless-security/ws-resources.h \
	src/wireless-security/ws-resources.c

src/wireless-security/ws-resources.h: src/wireless-security/ws.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=$(srcdir)/src/wireless-security --generate-header --internal

src/wireless-security/ws-resources.c: src/wireless-security/ws.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir)/src/wireless-security --generate-dependencies $(srcdir)/src/wireless-security/ws.gresource.xml)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=$(srcdir)/src/wireless-security --generate-source --internal


wireless_security_sources = \
	src/wireless-security/ws-resources.h \
	src/wireless-security/ws-resources.c \
	\
	src/wireless-security/wireless-security.h \
	src/wireless-security/wireless-security.c \
	src/wireless-security/ws-wep-key.h \
	src/wireless-security/ws-wep-key.c \
	src/wireless-security/ws-wpa-psk.h \
	src/wireless-security/ws-wpa-psk.c \
	src/wireless-security/ws-leap.h \
	src/wireless-security/ws-leap.c \
	src/wireless-security/ws-wpa-eap.h \
	src/wireless-security/ws-wpa-eap.c \
	src/wireless-security/ws-dynamic-wep.h \
	src/wireless-security/ws-dynamic-wep.c \
	src/wireless-security/eap-method.h \
	src/wireless-security/eap-method.c \
	src/wireless-security/eap-method-tls.h \
	src/wireless-security/eap-method-tls.c \
	src/wireless-security/eap-method-leap.h \
	src/wireless-security/eap-method-leap.c \
	src/wireless-security/eap-method-fast.h \
	src/wireless-security/eap-method-fast.c \
	src/wireless-security/eap-method-ttls.h \
	src/wireless-security/eap-method-ttls.c \
	src/wireless-security/eap-method-peap.h \
	src/wireless-security/eap-method-peap.c \
	src/wireless-security/eap-method-simple.h \
	src/wireless-security/eap-method-simple.c \
	src/wireless-security/helpers.h \
	src/wireless-security/helpers.c


noinst_LTLIBRARIES += src/wireless-security/libwireless-security-libnm.la

src_wireless_security_libwireless_security_libnm_la_SOURCES = \
	$(wireless_security_sources)

src_wireless_security_libwireless_security_libnm_la_CPPFLAGS = \
	"-I$(srcdir)/shared" \
	"-I$(srcdir)/src/utils" \
	"-I$(srcdir)/src/libnma" \
	$(GTK_CFLAGS) \
	$(LIBNM_CFLAGS)

src_wireless_security_libwireless_security_libnm_la_LIBADD = \
	src/utils/libutils-libnm.la \
	$(GTK_LIBS) \
	$(LIBNM_LIBS)


noinst_LTLIBRARIES += src/wireless-security/libwireless-security-libnm-glib.la

src_wireless_security_libwireless_security_libnm_glib_la_SOURCES = \
	$(wireless_security_sources)

src_wireless_security_libwireless_security_libnm_glib_la_CPPFLAGS = \
	$(GTK_CFLAGS) \
	-DNETWORKMANAGER_COMPILATION=NM_NETWORKMANAGER_COMPILATION_LIB_LEGACY \
	"-I$(srcdir)/shared" \
	"-I$(srcdir)/src/utils" \
	"-I$(srcdir)/src/libnma" \
	$(LIBNM_GLIB_CFLAGS)

src_wireless_security_libwireless_security_libnm_glib_la_LIBADD = \
	src/utils/libutils-libnm-glib.la \
	$(GTK_LIBS) \
	$(LIBNM_GLIB_LIBS)


EXTRA_DIST += \
	src/wireless-security/eap-method-fast.ui \
	src/wireless-security/eap-method-leap.ui \
	src/wireless-security/eap-method-peap.ui \
	src/wireless-security/eap-method-simple.ui \
	src/wireless-security/eap-method-tls.ui \
	src/wireless-security/eap-method-ttls.ui \
	src/wireless-security/ws-dynamic-wep.ui \
	src/wireless-security/ws-leap.ui \
	src/wireless-security/ws-wep-key.ui \
	src/wireless-security/ws-wpa-eap.ui \
	src/wireless-security/ws-wpa-psk.ui \
	src/wireless-security/ws.gresource.xml

###############################################################################

BUILT_SOURCES += \
	src/libnm-gtk/nm-resources.h \
	src/libnm-gtk/nm-resources.c

src/libnm-gtk/nm-resources.h: src/libnm-gtk/nm.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=$(srcdir)/src/libnm-gtk --generate-header --internal

src/libnm-gtk/nm-resources.c: src/libnm-gtk/nm.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir)/src/libnm-gtk --generate-dependencies $(srcdir)/src/libnm-gtk/nm.gresource.xml)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=$(srcdir)/src/libnm-gtk --generate-source --internal


src_libnm_gtk_libnmgtkdir = $(includedir)/libnm-gtk

src_libnm_gtk_libnmgtk_HEADERS = \
	src/libnm-gtk/nm-wifi-dialog.h \
	src/libnm-gtk/nm-wireless-dialog.h \
	src/libnm-gtk/nm-mobile-wizard.h \
	src/libnm-gtk/nm-ui-utils.h \
	src/libnm-gtk/nm-mobile-providers.h \
	src/libnm-gtk/nm-vpn-password-dialog.h


lib_LTLIBRARIES += src/libnm-gtk/libnm-gtk.la

src_libnm_gtk_libnm_gtk_la_SOURCES = \
	src/libnm-gtk/nm-resources.h \
	src/libnm-gtk/nm-resources.c \
	\
	src/libnm-gtk/nm-wifi-dialog.c \
	src/libnm-gtk/nm-wireless-dialog.c \
	src/libnm-gtk/nm-mobile-wizard.c \
	src/libnm-gtk/nm-ui-utils.c \
	src/libnm-gtk/nm-mobile-providers.c \
	src/libnm-gtk/nm-vpn-password-dialog.c \
	src/libnm-gtk/init.c

src_libnm_gtk_libnm_gtk_la_CFLAGS = \
	-DNETWORKMANAGER_COMPILATION=NM_NETWORKMANAGER_COMPILATION_LIB_LEGACY \
	-DICONDIR=\""$(datadir)/icons"\" \
	-DBINDIR=\""$(bindir)"\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DAUTOSTARTDIR=\""$(sysconfdir)/xdg/autostart"\" \
	-DNMALOCALEDIR=\"$(datadir)/locale\" \
	"-I$(srcdir)/shared" \
	"-I$(srcdir)/src/utils" \
	"-I$(srcdir)/src/wireless-security" \
	"-I$(srcdir)/src/libnm-gtk" \
	$(GTK_CFLAGS) \
	$(LIBNM_GLIB_CFLAGS) \
	$(GUDEV_CFLAGS)

src_libnm_gtk_libnm_gtk_la_LIBADD = \
	src/wireless-security/libwireless-security-libnm-glib.la \
	$(GTK_LIBS) \
	$(LIBNM_GLIB_LIBS) \
	$(GUDEV_LIBS)

EXTRA_src_libnm_gtk_libnm_gtk_la_DEPENDENCIES = src/libnm-gtk/libnm-gtk.ver

src_libnm_gtk_libnm_gtk_la_LDFLAGS = \
	-Wl,-no-undefined \
	-Wl,--version-script=$(srcdir)/src/libnm-gtk/libnm-gtk.ver

check-local-exports-libnm-gtk:
	$(srcdir)/src/libnm-gtk/check-exports.sh $(builddir)/src/libnm-gtk/.libs/libnm-gtk.so $(srcdir)/src/libnm-gtk/libnm-gtk.ver

check_local += check-local-exports-libnm-gtk


pkgconfig_DATA += src/libnm-gtk/libnm-gtk.pc

if HAVE_INTROSPECTION
src/libnm-gtk/NMGtk-1.0.gir: src/libnm-gtk/libnm-gtk.la
src_libnm_gtk_NMGtk_1_0_gir_INCLUDES = NMClient-1.0 NetworkManager-1.0 Gtk-3.0
src_libnm_gtk_NMGtk_1_0_gir_EXPORT_PACKAGES = libnm-gtk
src_libnm_gtk_NMGtk_1_0_gir_CFLAGS = $(src_libnm_gtk_libnm_gtk_la_CFLAGS)
src_libnm_gtk_NMGtk_1_0_gir_LIBS = src/libnm-gtk/libnm-gtk.la
src_libnm_gtk_NMGtk_1_0_gir_FILES = \
	$(filter-out \
		$(BUILT_SOURCES) src/libnm-gtk/nm-wireless-dialog.%, \
		$(src_libnm_gtk_libnmgtk_HEADERS) $(src_libnm_gtk_libnm_gtk_la_SOURCES))
src_libnm_gtk_NMGtk_1_0_gir_SCANNERFLAGS = --warn-all --identifier-prefix=NMA --symbol-prefix=nma
INTROSPECTION_GIRS += src/libnm-gtk/NMGtk-1.0.gir
endif


check_programs += src/libnm-gtk/tests/test-mobile-providers

src_libnm_gtk_tests_test_mobile_providers_CPPFLAGS = \
	-DTEST_DATA_DIR=\""$(abs_srcdir)/src/libnm-gtk/tests"\" \
	"-I$(srcdir)/shared" \
	"-I$(srcdir)/src/libnm-gtk" \
	$(GTK_CFLAGS) \
	$(LIBNM_GLIB_CFLAGS)

src_libnm_gtk_tests_test_mobile_providers_LDADD = \
	src/libnm-gtk/libnm-gtk.la \
	$(LIBNM_GLIB_LIBS)


EXTRA_DIST += \
	src/libnm-gtk/libnm-gtk.pc.in \
	src/libnm-gtk/libnm-gtk.ver \
	src/libnm-gtk/check-exports.sh \
	src/libnm-gtk/wifi.ui \
	src/libnm-gtk/nm.gresource.xml \
	\
	src/libnm-gtk/tests/iso3166-test.xml \
	src/libnm-gtk/tests/serviceproviders-test.xml \
	\
	src/libnm-gtk/examples/mobile-providers.js \
	$(NULL)

###############################################################################

check_PROGRAMS += $(check_programs)

check-local: $(check_local)

TESTS += $(check_programs)

EXTRA_DIST += \
	CONTRIBUTING \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	Makefile.glib \
	autogen.sh

autostartdir = $(sysconfdir)/xdg/autostart
autostart_in_files = nm-applet.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

applicationsdir = $(datadir)/applications
applications_DATA = $(autostart_DATA)

desktopdir = $(datadir)/applications
desktop_in_files = nm-connection-editor.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

gsettings_SCHEMAS = org.gnome.nm-applet.gschema.xml
@GSETTINGS_RULES@

appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files = nm-connection-editor.appdata.xml.in
@INTLTOOL_XML_RULE@

convertdir=$(datadir)/GConf/gsettings
convert_DATA=nm-applet.convert

EXTRA_DIST += \
	$(convert_DATA) \
	$(appdata_in_files) \
	$(autostart_in_files) \
	$(desktop_in_files)

CLEANFILES += \
	$(INTROSPECTION_GIRS) \
	$(typelib_DATA) \
	\
	$(autostart_DATA) \
	$(desktop_DATA) \
	$(appdata_DATA) \
	$(BUILT_SOURCES)

DISTCLEANFILES += \
	intltool-extract \
	intltool-merge \
	intltool-update
