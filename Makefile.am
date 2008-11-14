SUBDIRS = src icons po

EXTRA_DIST =				\
	CONTRIBUTING			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in

dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = nm-applet.conf

autostart_in_files = nm-applet.desktop.in
autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

desktop_in_files = nm-connection-editor.desktop.in
desktopdir = $(datadir)/applications
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST += \
	$(dbusservice_DATA) \
	$(autostart_DATA) \
	$(desktop_DATA)

CLEANFILES = \
	$(autostart_DATA) \
	$(desktop_DATA)

DISTCLEANFILES = intltool-extract intltool-merge intltool-update

