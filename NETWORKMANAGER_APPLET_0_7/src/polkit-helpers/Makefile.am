if NO_POLKIT_GNOME

noinst_LTLIBRARIES = libpolkit-helpers.la

libpolkit_helpers_la_SOURCES = \
	polkit-gnome-action.c \
	polkit-gnome-action.h \
	polkit-gnome-auth.c \
	polkit-gnome-auth.h \
	polkit-gnome-context.c \
	polkit-gnome-context.h \
	polkit-gnome.h

libpolkit_helpers_la_CPPFLAGS = \
	$(POLKIT_CFLAGS) \
	$(POLKIT_DBUS_CFLAGS) \
	$(NMA_CFLAGS)

libpolkit_helpers_la_LIBADD = \
	$(POLKIT_LIBS) \
	$(POLKIT_DBUS_LIBS) \
	$(NMA_LIBS)

endif

