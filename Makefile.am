include $(GLIB_MAKEFILE)

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = src icons po man

EXTRA_DIST =				\
	CONTRIBUTING			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in		\
	Makefile.glib

autostartdir = $(sysconfdir)/xdg/autostart
autostart_in_files = nm-applet.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

applicationsdir = $(datadir)/applications
applications_DATA = $(autostart_DATA)

desktopdir = $(datadir)/applications
desktop_in_files = nm-connection-editor.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

schema_in_files = org.gnome.nm-applet.gschema.xml.in
gsettingsschema_DATA = $(schema_in_files:.xml.in=.xml)
@INTLTOOL_XML_NOMERGE_RULE@

convertdir=$(datadir)/GConf/gsettings
convert_DATA=nm-applet.convert

DISTCHECK_CONFIGURE_FLAGS = --enable-more-warnings=yes

EXTRA_DIST += \
	$(schema_in_files) \
	$(convert_DATA) \
	$(autostart_in_files) \
	$(desktop_in_files)

CLEANFILES = \
	$(autostart_DATA) \
	$(desktop_DATA) \
	$(gsettingsschema_DATA)

DISTCLEANFILES = intltool-extract intltool-merge intltool-update
