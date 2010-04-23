ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = src icons po

EXTRA_DIST =				\
	CONTRIBUTING			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in

dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = nm-applet.conf

DBUS_126_CONF = nm-applet-dbus-126.conf
DBUS_PRE_126_CONF = nm-applet-pre-dbus-126.conf

nm-applet.conf:
if HAVE_DBUS_126
	cp -f $(top_srcdir)/$(DBUS_126_CONF) nm-applet.conf
else
	cp -f $(top_srcdir)/$(DBUS_PRE_126_CONF) nm-applet.conf
endif

autostartdir = $(sysconfdir)/xdg/autostart
autostart_in_files = nm-applet.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

desktopdir = $(datadir)/applications
desktop_in_files = nm-connection-editor.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

schemasdir   = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = nm-applet.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)
@INTLTOOL_SCHEMAS_RULE@


EXTRA_DIST += \
	$(schemas_in_files) \
	$(autostart_in_files) \
	$(desktop_in_files) \
	$(DBUS_126_CONF) \
	$(DBUS_PRE_126_CONF)

CLEANFILES = \
	$(autostart_DATA) \
	$(desktop_DATA) \
	$(schemas_DATA) \
	$(dbusservice_DATA)

DISTCLEANFILES = intltool-extract intltool-merge intltool-update

ACLOCAL_AMFLAGS = -I m4

install-data-local:
if GCONF_SCHEMAS_INSTALL
	if test -z "$(DESTDIR)" ; then \
	for p in $(schemas_DATA) ; do \
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/$$p >&1 > /dev/null; \
	done \
	fi
endif
