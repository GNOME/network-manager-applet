uidir = $(datadir)/libnm-gtk
ui_DATA = wifi.ui

libnmgtkdir = $(includedir)/libnm-gtk

libnmgtk_HEADERS = \
	nm-wireless-dialog.h \
	nm-mobile-wizard.h

lib_LTLIBRARIES = libnm-gtk.la

libnm_gtk_la_SOURCES = \
	nm-wireless-dialog.c \
	nm-mobile-wizard.c

libnm_gtk_la_CFLAGS = \
	$(GTK_CFLAGS) \
	$(NMA_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(GNOME_KEYRING_CFLAGS) \
	-DICONDIR=\""$(datadir)/icons"\" \
	-DUIDIR=\""$(uidir)"\" \
	-DBINDIR=\""$(bindir)"\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DAUTOSTARTDIR=\""$(sysconfdir)/xdg/autostart"\" \
	-DVPN_NAME_FILES_DIR=\""$(sysconfdir)/NetworkManager/VPN"\" \
	-DNMALOCALEDIR=\"$(datadir)/locale\" \
	$(DISABLE_DEPRECATED) \
	-I${top_builddir}/src/marshallers \
	-I${top_srcdir}/src/utils \
	-I${top_srcdir}/src/wireless-security

libnm_gtk_la_LIBADD = \
	$(GTK_LIBS) \
	$(NMA_LIBS) \
	$(GNOME_KEYRING_LIBS) \
	$(top_builddir)/src/marshallers/libmarshallers.la \
	$(top_builddir)/src/wireless-security/libwireless-security.la

libnm_gtk_la_LDFLAGS = -Wl,-no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libnm-gtk.pc

EXTRA_DIST = libnm-gtk.pc.in $(ui_DATA)