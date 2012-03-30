SUBDIRS = marshallers utils gconf-helpers wireless-security libnm-gtk connection-editor gnome-bluetooth

bin_PROGRAMS = nm-applet

nm_applet_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	$(GCONF_CFLAGS) \
	$(GNOME_KEYRING_CFLAGS) \
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
	-I${top_srcdir}/src/gconf-helpers  \
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
	wired-dialog.h \
	wired-dialog.c \
	applet-dialogs.h \
	applet-dialogs.c \
	applet-device-wired.h \
	applet-device-wired.c \
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
	$(GCONF_LIBS) \
	$(GNOME_KEYRING_LIBS) \
	$(NOTIFY_LIBS) \
	${top_builddir}/src/marshallers/libmarshallers.la \
	${top_builddir}/src/utils/libutils.la \
	${top_builddir}/src/gconf-helpers/libgconf-helpers.la \
	${top_builddir}/src/wireless-security/libwireless-security.la \
	${top_builddir}/src/libnm-gtk/libnm-gtk.la

uidir = $(datadir)/nm-applet
ui_DATA = gsm-unlock.ui info.ui wired-8021x.ui keyring.png

CLEANFILES = *.bak $(BUILT_SOURCES)

EXTRA_DIST = \
	$(ui_DATA) \
	nm-applet-introspection.xml

