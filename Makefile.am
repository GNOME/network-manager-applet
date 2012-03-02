ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = src icons po

EXTRA_DIST =				\
	CONTRIBUTING			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in

autostartdir = $(sysconfdir)/xdg/autostart
autostart_in_files = nm-applet.desktop.in
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

applicationsdir = $(datadir)/applications
applications_DATA = $(autostart_DATA)

desktopdir = $(datadir)/applications
desktop_in_files = nm-connection-editor.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

schemasdir   = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = nm-applet.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)
@INTLTOOL_SCHEMAS_RULE@


DISTCHECK_CONFIGURE_FLAGS = --enable-more-warnings=yes

EXTRA_DIST += \
	$(schemas_in_files) \
	$(autostart_in_files) \
	$(desktop_in_files)

CLEANFILES = \
	$(autostart_DATA) \
	$(desktop_DATA) \
	$(schemas_DATA)

DISTCLEANFILES = intltool-extract intltool-merge intltool-update

install-data-local:
if GCONF_SCHEMAS_INSTALL
	if test -z "$(DESTDIR)" ; then \
	for p in $(schemas_DATA) ; do \
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/$$p >&1 > /dev/null; \
	done \
	fi
endif
