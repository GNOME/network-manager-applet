SUBDIRS = marshallers utils wireless-security libnm-gtk connection-editor gnome-bluetooth

bin_PROGRAMS = nm-applet

nm_applet_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	$(LIBSECRET_CFLAGS) \
	$(NOTIFY_CFLAGS) \
	-DICONDIR=\""$(datadir)/icons"\"						\
	-DUIDIR=\""$(uidir)"\"							\
	-DBINDIR=\""$(bindir)"\"								\
	-DSYSCONFDIR=\""$(sysconfdir)"\"						\
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DAUTOSTARTDIR=\""$(sysconfdir)/xdg/autostart"\"			\
	-DVPN_NAME_FILES_DIR=\""$(sysconfdir)/NetworkManager/VPN"\"	\
	-DNMALOCALEDIR=\"$(datadir)/locale\"					\
	$(DBUS_CFLAGS)										\
	$(DISABLE_DEPRECATED)	\
	-I${top_builddir}/src/marshallers \
	-I${top_srcdir}/src/utils \
	-I${top_srcdir}/src/wireless-security \
	-I${top_srcdir}/src/libnm-gtk

BUILT_SOURCES = applet-dbus-bindings.h

applet-dbus-bindings.h: nm-applet-introspection.xml
	$(AM_V_GEN) dbus-binding-tool --mode=glib-server --prefix=nma --output=$@ $<

nm_applet_SOURCES = \
	main.c \
	applet.c \
	applet.h \
	applet-agent.c \
	applet-agent.h \
	applet-vpn-request.c \
	applet-vpn-request.h \
	ethernet-dialog.h \
	ethernet-dialog.c \
	applet-dialogs.h \
	applet-dialogs.c \
	applet-device-ethernet.h \
	applet-device-ethernet.c \
	applet-device-wifi.h \
	applet-device-wifi.c \
	ap-menu-item.h \
	ap-menu-item.c \
	mb-menu-item.h \
	mb-menu-item.c \
	applet-device-gsm.h \
	applet-device-gsm.c \
	applet-device-cdma.h \
	applet-device-cdma.c \
	mobile-helpers.c \
	mobile-helpers.h \
	applet-device-bt.h \
	applet-device-bt.c \
	applet-device-wimax.h \
	applet-device-wimax.c \
	fallback-icon.h \
	shell-watcher.h \
	shell-watcher.c

nm_applet_LDADD = \
	-lm \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	$(LIBSECRET_LIBS) \
	$(NOTIFY_LIBS) \
	${top_builddir}/src/marshallers/libmarshallers.la \
	${top_builddir}/src/utils/libutils.la \
	${top_builddir}/src/wireless-security/libwireless-security.la \
	${top_builddir}/src/libnm-gtk/libnm-gtk.la

if WITH_MODEM_MANAGER_1
nm_applet_SOURCES += \
	applet-device-broadband.h \
	applet-device-broadband.c
nm_applet_CPPFLAGS += $(MM_GLIB_CFLAGS)
nm_applet_LDADD += $(MM_GLIB_LIBS)
endif

if BUILD_MIGRATION_TOOL
SUBDIRS += gconf-helpers

libexec_PROGRAMS = nm-applet-migration-tool
endif

nm_applet_migration_tool_CPPFLAGS = \
	$(nm_applet_CPPFLAGS) \
	$(MIGRATION_CFLAGS) \
	-I${top_srcdir}/src/gconf-helpers

nm_applet_migration_tool_SOURCES = \
	migration-tool.c

nm_applet_migration_tool_LDADD = \
	$(nm_applet_LDADD) \
	$(MIGRATION_LIBS) \
	${top_builddir}/src/gconf-helpers/libgconf-helpers.la


uidir = $(datadir)/nm-applet
ui_DATA = gsm-unlock.ui info.ui 8021x.ui keyring.png

CLEANFILES = *.bak $(BUILT_SOURCES)

EXTRA_DIST = \
	$(ui_DATA) \
	nm-applet-introspection.xml

