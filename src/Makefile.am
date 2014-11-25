SUBDIRS = utils wireless-security libnm-gtk connection-editor

bin_PROGRAMS = nm-applet

nm_applet_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	$(LIBSECRET_CFLAGS) \
	$(NOTIFY_CFLAGS) \
	$(MM_GLIB_CFLAGS) \
	$(APPINDICATOR_CFLAGS) \
	-DNM_VERSION_MIN_REQUIRED=NM_VERSION_0_9_10 \
	-DNM_VERSION_MAX_ALLOWED=NM_VERSION_0_9_10 \
	-DICONDIR=\""$(datadir)/icons"\"						\
	-DUIDIR=\""$(uidir)"\"							\
	-DBINDIR=\""$(bindir)"\"								\
	-DSYSCONFDIR=\""$(sysconfdir)"\"						\
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DAUTOSTARTDIR=\""$(sysconfdir)/xdg/autostart"\"			\
	-DVPN_NAME_FILES_DIR=\""$(sysconfdir)/NetworkManager/VPN"\"	\
	-DNMALOCALEDIR=\"$(datadir)/locale\"					\
	-DG_LOG_DOMAIN=\""nm-applet"\" \
	$(DBUS_CFLAGS)										\
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
	mobile-helpers.c \
	mobile-helpers.h \
	applet-device-bt.h \
	applet-device-bt.c \
	applet-device-wimax.h \
	applet-device-wimax.c \
	applet-device-vlan.h \
	applet-device-vlan.c \
	applet-device-bond.h \
	applet-device-bond.c \
	applet-device-team.h \
	applet-device-team.c \
	applet-device-bridge.h \
	applet-device-bridge.c \
	applet-device-infiniband.h \
	applet-device-infiniband.c \
	fallback-icon.h

if WITH_WWAN
nm_applet_SOURCES += \
	applet-device-broadband.h \
	applet-device-broadband.c
endif

nm_applet_LDADD = \
	-lm \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	$(LIBSECRET_LIBS) \
	$(NOTIFY_LIBS) \
	$(MM_GLIB_LIBS) \
	$(APPINDICATOR_LIBS) \
	${top_builddir}/src/utils/libutils.la \
	${top_builddir}/src/wireless-security/libwireless-security.la \
	${top_builddir}/src/libnm-gtk/libnm-gtk.la

uidir = $(datadir)/nm-applet
ui_DATA = gsm-unlock.ui info.ui 8021x.ui keyring.png

CLEANFILES = *.bak $(BUILT_SOURCES)

EXTRA_DIST = \
	$(ui_DATA) \
	nm-applet-introspection.xml

