include $(GLIB_MAKEFILE)

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

EXTRA_DIST =

SUBDIRS = \
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

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-more-warnings=yes

EXTRA_DIST += \
	$(convert_DATA) \
	$(appdata_in_files) \
	$(autostart_in_files) \
	$(desktop_in_files)

CLEANFILES = \
	$(autostart_DATA) \
	$(desktop_DATA) \
	$(appdata_DATA) \
	$(BUILT_SOURCES)

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update
