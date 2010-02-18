AC_PREREQ(2.52)

AC_INIT(nm-applet, 0.8, dcbw@redhat.com, network-manager-applet)
AM_INIT_AUTOMAKE([subdir-objects no-dist-gzip dist-bzip2])
AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

dnl
dnl Require programs
dnl
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LIBTOOL

AC_PATH_PROG(GCONFTOOL, gconftool-2)
AM_GCONF_SOURCE_2

dnl ensure that when the Automake generated makefile calls aclocal,
dnl it honours the $ACLOCAL_FLAGS environment variable
ACLOCAL_AMFLAGS="\${ACLOCAL_FLAGS}"
if test -n "$ac_macro_dir"; then
    ACLOCAL_AMFLAGS="-I $ac_macro_dir $ACLOCAL_AMFLAGS"
fi
AC_SUBST([ACLOCAL_AMFLAGS])

if test $USE_MAINTAINER_MODE = yes; then
	DISABLE_DEPRECATED="-DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DPANGO_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGCONF_DISABLE_DEPRECATED -DLIBGLADE_DISABLE_DEPRECATED"
else
	DISABLE_DEPRECATED=""
fi
AC_SUBST(DISABLE_DEPRECATED)

dnl
dnl Required headers
dnl
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h paths.h sys/ioctl.h sys/time.h syslog.h unistd.h)

dnl
dnl Checks for typedefs, structures, and compiler characteristics.
dnl
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_HEADER_TIME

dnl
dnl Checks for library functions.
dnl
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MEMCMP
AC_CHECK_FUNCS(select socket uname)

dnl
dnl translation support
dnl
GETTEXT_PACKAGE=nm-applet
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])
IT_PROG_INTLTOOL([0.35.0])
AM_GLIB_GNU_GETTEXT

dnl
dnl Make sha1.c happy on big endian systems
dnl
AC_C_BIGENDIAN

PKG_CHECK_MODULES(GOBJECT, gobject-2.0)

PKG_CHECK_MODULES(NMA,
		[dbus-glib-1 >= 0.74
		 glib-2.0 >= 2.16
		 NetworkManager >= 0.8
		 libnm-glib >= 0.8
		 libnm-util >= 0.8
		 libnm-glib-vpn >= 0.8
		 gtk+-2.0 >= 2.14
		 libglade-2.0
		 gmodule-export-2.0
		 gconf-2.0
		 gnome-keyring-1
		 libnotify >= 0.4.3])

# Check for dbus-1.2.6 or later for deny-by-default rules
PKG_CHECK_MODULES(DBUS_126, [dbus-1 >= 1.2.6], [have_dbus_126=yes],[have_dbus_126=no])
AM_CONDITIONAL(HAVE_DBUS_126, test x"$have_dbus_126" = "xyes")

#### PolicyKit checks; need at least 0.92 or later
PKG_CHECK_MODULES(POLKIT, [polkit-gobject-1 >= 0.92])

AC_ARG_WITH(dbus-sys, AS_HELP_STRING([--with-dbus-sys=DIR], [where D-BUS system.d directory is]))

if ! test -z "$with_dbus_sys" ; then
    DBUS_SYS_DIR="$with_dbus_sys"
else
    DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

dnl Check for gnome-bluetooth
PKG_CHECK_MODULES(GNOME_BLUETOOTH,
		  gconf-2.0
		  gnome-bluetooth-1.0 >= 2.27.6
		  libnm-util >= 0.8
		  libnm-glib >= 0.8,
		  have_gbt=yes, have_gbt=no)
AM_CONDITIONAL(HAVE_GBT, test x"$have_gbt" = "xyes")

GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

NM_COMPILER_WARNINGS

AC_CONFIG_FILES([
Makefile
src/Makefile
src/marshallers/Makefile
src/utils/Makefile
src/utils/tests/Makefile
src/gconf-helpers/Makefile
src/wireless-security/Makefile
src/connection-editor/Makefile
src/gnome-bluetooth/Makefile
icons/Makefile
icons/16/Makefile
icons/22/Makefile
icons/32/Makefile
icons/48/Makefile
icons/scalable/Makefile
po/Makefile.in
])
AC_OUTPUT
