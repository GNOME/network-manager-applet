SUBDIRS = vpn-properties

NULL=

bin_PROGRAMS = nm-applet

nm_applet_CPPFLAGS =									\
	$(NMA_CFLAGS)										\
	-DICONDIR=\""$(datadir)/icons"\"						\
	-DGLADEDIR=\""$(gladedir)"\"							\
	-DBINDIR=\""$(bindir)"\"								\
	-DSYSCONFDIR=\""$(sysconfdir)"\"						\
	-DAUTOSTARTDIR=\""$(datadir)/gnome/autostart"\"			\
	-DVPN_NAME_FILES_DIR=\""$(sysconfdir)/NetworkManager/VPN"\"	\
	-DDBUS_API_SUBJECT_TO_CHANGE							\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"					\
	$(DISABLE_DEPRECATED)	\
	$(NULL)

if WITH_NOTIFY
nm_applet_CPPFLAGS += $(NOTIFY_CFLAGS)
endif

nm_applet_SOURCES =			\
	main.c				\
	applet.c				\
	applet.h				\
	applet-compat.c		\
	applet-compat.h		\
	applet-dbus.c			\
	applet-dbus.h			\
	applet-dbus-info.c		\
	applet-dbus-info.h		\
	applet-dbus-settings.c		\
	applet-dbus-settings.h		\
	menu-items.c			\
	menu-items.h			\
	vpn-password-dialog.c	\
	vpn-password-dialog.h	\
	gconf-helpers.c		\
	gconf-helpers.h		\
	nm-utils.c \
	nm-utils.h \
	gnome-keyring-md5.h \
	gnome-keyring-md5.c \
	$(NULL)

if !HAVE_GTK_2_10
nm_applet_SOURCES += \
	eggtrayicon.c	\
	eggtrayicon.h
endif

if WITH_NOTIFY
nm_applet_SOURCES += applet-notifications.c applet-notifications.h
endif

nm_applet_LDADD = $(NMA_LIBS)

if WITH_NOTIFY
nm_applet_LDADD += $(NOTIFY_LIBS)
endif


gladedir = $(datadir)/nm-applet
glade_DATA = applet.glade keyring.png

CLEANFILES = *.bak *.gladep

EXTRA_DIST = 				\
	$(glade_DATA)			\
	$(NULL)
