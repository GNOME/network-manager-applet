SUBDIRS = src icons po

EXTRA_DIST =				\
	CONTRIBUTING			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in		
	
dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = nm-applet.conf

EXTRA_DIST +=				\
	$(dbusservice_DATA)		\
	nm-applet.desktop

DISTCLEANFILES = intltool-extract intltool-merge intltool-update

autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = nm-applet.desktop
