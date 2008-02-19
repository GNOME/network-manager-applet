SUBDIRS = libnma src editor icons po

EXTRA_DIST =				\
	CONTRIBUTING			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in		\
	COPYING.LIB

dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = nm-applet.conf

toolsdir = $(datadir)/nm-applet
tools_SCRIPTS = nm-disconnect-wireless-on-exit.sh

EXTRA_DIST +=				\
	$(dbusservice_DATA)		\
	$(autostart_DATA)		\
	$(tools_SCRIPTS)

DISTCLEANFILES = intltool-extract intltool-merge intltool-update

autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = nm-applet.desktop
