NULL=

INCLUDES = -I${top_srcdir}/../include -I${top_srcdir}/../libnm-util

bin_PROGRAMS = nm-editor

nm_editor_CPPFLAGS =									\
	$(DBUS_CFLAGS)										\
	$(GTHREAD_CFLAGS)									\
	$(HAL_CFLAGS)										\
	$(DBUS_GLIB_CFLAGS)									\
	$(GLADE_CFLAGS)									\
	$(GTK_CFLAGS)										\
	$(GCONF_CFLAGS)									\
	$(LIBGNOMEUI_CFLAGS)								\
	$(PANEL_APPLET_CFLAGS)								\
	$(GNOME_KEYRING_CFLAGS)								\
	$(NM_CFLAGS)									\
	-DICONDIR=\""$(datadir)/pixmaps"\"						\
	-DGLADEDIR=\""$(gladedir)"\"							\
	-DBINDIR=\""$(bindir)"\"								\
	-DSYSCONFDIR=\""$(sysconfdir)"\"						\
	-DAUTOSTARTDIR=\""$(datadir)/gnome/autostart"\"			\
	-DVPN_NAME_FILES_DIR=\""$(sysconfdir)/NetworkManager/VPN"\"	\
	-DDBUS_API_SUBJECT_TO_CHANGE							\
	-DG_DISABLE_DEPRECATED								\
	-DGDK_DISABLE_DEPRECATED								\
	-DGNOME_DISABLE_DEPRECATED							\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"					\
	-DVERSION=\"$(VERSION)\"								\
	$(NULL)

nm_editor_SOURCES =			\
	editor-app.c			\
	editor-app.h			\
	bssid-editor-dialog.c	\
	bssid-editor-dialog.h	\
	widget-wso.h			\
	widget-wso-wep.c		\
	widget-wso-wpa-personal.c		\
	widget-wso-wpa-enterprise.c		\
	editor-gconf-helper.h	\
	editor-gconf-helper.c	\
	editor-keyring-helper.h	\
	editor-keyring-helper.c	\
	$(NULL)

nm_editor_LDADD =						\
	$(DBUS_LIBS)						\
	$(HAL_LIBS)						\
	$(GTHREAD_LIBS)					\
	$(GLADE_LIBS)						\
	$(DBUS_GLIB_LIBS)					\
	$(PANEL_APPLET_LIBS)				\
	$(GTK_LIBS)						\
	$(GCONF_LIBS)						\
	$(LIBGNOMEUI_LIBS)					\
	$(GNOME_KEYRING_LIBS)				\
	$(NM_LIBS) \
	$(NULL)


gladedir = $(datadir)/nm-editor
glade_DATA = editor.glade
desktopdatadir = $(datadir)/applications
desktopdata_DATA = nm-editor.desktop

CLEANFILES = $(server_DATA) *.bak *.gladep

EXTRA_DIST = 				\
	$(glade_DATA)			\
	$(desktopdata_DATA)		\
	$(NULL)


