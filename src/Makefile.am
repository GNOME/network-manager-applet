SUBDIRS = marshallers utils gconf-helpers wireless-security connection-editor gnome-bluetooth

bin_PROGRAMS = nm-applet

nm_applet_CPPFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
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
	-I${top_srcdir}/src/wireless-security

nm_applet_SOURCES =			\
	main.c				\
	applet.c				\
	applet.h				\
	applet-dbus-manager.c	\
	applet-dbus-manager.h	\
	vpn-password-dialog.c	\
	vpn-password-dialog.h	\
	wired-dialog.h \
	wired-dialog.c \
	wireless-dialog.h \
	wireless-dialog.c \
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
	fallback-icon.h

nm_applet_LDADD = \
	-lm \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	$(NOTIFY_LIBS) \
	${top_builddir}/src/marshallers/libmarshallers.la \
	${top_builddir}/src/utils/libutils.la \
	${top_builddir}/src/gconf-helpers/libgconf-helpers.la \
	${top_builddir}/src/wireless-security/libwireless-security.la

uidir = $(datadir)/nm-applet
ui_DATA = applet.ui keyring.png

CLEANFILES = *.bak

EXTRA_DIST = 				\
	$(ui_DATA)

