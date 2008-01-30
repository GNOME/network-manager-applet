SUBDIRS = src icons po

EXTRA_DIST =				\
	CONTRIBUTING			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in		\
	COPYING.LIB

dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = nm-applet.conf

toolsdir = $(datadir)/nm-applet
tools_DATA = nm-disconnect-wireless-on-exit.sh

EXTRA_DIST +=				\
	$(dbusservice_DATA)		\
	nm-applet.desktop		\
	$(tools_DATA)

DISTCLEANFILES = intltool-extract intltool-merge intltool-update

autostartdir = $(datadir)/gnome/autostart
autostart_DATA = nm-applet.desktop
