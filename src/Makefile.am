SUBDIRS = marshallers utils gconf-helpers wireless-security connection-editor gnome-bluetooth

bin_PROGRAMS = nm-applet

nm_applet_CPPFLAGS = \
	$(NMA_CFLAGS) \
	$(NOTIFY_CFLAGS) \
	-DICONDIR=\""$(datadir)/icons"\"						\
	-DGLADEDIR=\""$(gladedir)"\"							\
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
	wireless-helper.h \
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
	applet-device-bt.h \
	applet-device-bt.c

nm_applet_LDADD = \
	-lm \
	$(NMA_LIBS) \
	$(NOTIFY_LIBS) \
	${top_builddir}/src/marshallers/libmarshallers.la \
	${top_builddir}/src/utils/libutils.la \
	${top_builddir}/src/gconf-helpers/libgconf-helpers.la \
	${top_builddir}/src/wireless-security/libwireless-security.la

gladedir = $(datadir)/nm-applet
glade_DATA = applet.glade keyring.png

CLEANFILES = *.bak *.gladep

EXTRA_DIST = 				\
	$(glade_DATA)

