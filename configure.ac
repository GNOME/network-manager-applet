AC_PREREQ([2.63])
AC_INIT([nm-applet],
        [0.8.3],
        [https://bugzilla.gnome.org/enter_bug.cgi?product=NetworkManager],
        [network-manager-applet])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.10 subdir-objects no-dist-gzip dist-bzip2])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([no])])
AM_MAINTAINER_MODE([enable])

# Support silent build rules, requires at least automake-1.11. Disable
# by either passing --disable-silent-rules to configure or passing V=1
# to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([no])])

dnl
dnl Require programs
dnl
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL

dnl Define _GNU_SOURCE for various things like strcasestr()
AC_GNU_SOURCE

dnl Initialize libtool
LT_PREREQ([2.2.6])
LT_INIT

dnl
dnl Required headers
dnl
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
IT_PROG_INTLTOOL([0.40.0])

GETTEXT_PACKAGE=nm-applet
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])
AM_GLIB_GNU_GETTEXT

dnl
dnl Make sha1.c happy on big endian systems
dnl
AC_C_BIGENDIAN

PKG_CHECK_MODULES(GOBJECT, gobject-2.0)

PKG_CHECK_MODULES(NMA,
		[dbus-glib-1 >= 0.74
		 glib-2.0 >= 2.16
		 NetworkManager >= 0.8.3
		 libnm-glib >= 0.8.3
		 libnm-util >= 0.8.3
		 libnm-glib-vpn >= 0.8.3
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
AC_ARG_WITH([bluetooth],
	    AS_HELP_STRING([--with-bluetooth|--without-bluetooth], [Enable Bluetooth support]),
	    with_bluetooth="$withval",with_bluetooth=yes)
have_gbt=no
case "${with_bluetooth}" in
	no)	AC_MSG_NOTICE(Bluetooth support disabled)
		;;
	*)
		AC_MSG_CHECKING(for gnome-bluetooth)
		PKG_CHECK_MODULES(GNOME_BLUETOOTH,
				  gconf-2.0
				  gnome-bluetooth-1.0 >= 2.27.6
				  libnm-util >= 0.8.2
				  libnm-glib >= 0.8.2,
				  have_gbt=yes, have_gbt=no)
		;;
esac

AM_CONDITIONAL(HAVE_GBT, test x"$have_gbt" = "xyes")

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

dnl GConf stuff
AC_PATH_PROG(GCONFTOOL, gconftool-2)
AM_GCONF_SOURCE_2

dnl
dnl Compiler flags
dnl
NM_COMPILER_WARNINGS
# Use --enable-maintainer-mode to disabled deprecated symbols
GNOME_MAINTAINER_MODE_DEFINES


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
