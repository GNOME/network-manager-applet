AC_PREREQ(2.52)

AC_INIT(nm-applet, 0.7.1.996, dcbw@redhat.com, network-manager-applet)
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
		 glib-2.0 >= 2.10
		 NetworkManager >= 0.7.1.996
		 libnm_glib >= 0.7.1.996
		 libnm-util >= 0.7.1.996
		 libnm_glib_vpn >= 0.7.1.996
		 gtk+-2.0 >= 2.14
		 libglade-2.0
		 gmodule-export-2.0
		 gconf-2.0
		 gnome-keyring-1
		 libnotify >= 0.4.3])

##### Find out the version of DBUS we're using
dbus_version=`pkg-config --modversion dbus-1`
DBUS_VERSION_MAJOR=`echo $dbus_version | awk -F. '{print $1}'`
DBUS_VERSION_MINOR=`echo $dbus_version | awk -F. '{print $2}'`
DBUS_VERSION_MICRO=`echo $dbus_version | awk -F. '{print $3}'`
if test "z$DBUS_VERSION_MAJOR" = "z"; then
	DBUS_VERSION_MAJOR="0"
fi
if test "z$DBUS_VERSION_MINOR" = "z"; then
	DBUS_VERSION_MINOR="0"
fi
if test "z$DBUS_VERSION_MICRO" = "z"; then
	DBUS_VERSION_MICRO="0"
fi

if test "z$DBUS_VERSION_MAJOR" = "z0" -a "z$DBUS_VERSION_MINOR" = "z0" -a "z$DBUS_VERSION_MICRO" = "z0"; then
	echo "Error: Couldn't determine the version of your DBUS package."
	echo "  This is probably an error in this script, please report it"
	echo "  along with the following information:"
	echo "      Base DBUS version ='$dbus_version'"
	echo "      DBUS_VERSION_MAJOR='$DBUS_VERSION_MAJOR'"
	echo "      DBUS_VERSION_MINOR='$DBUS_VERSION_MINOR'"
	echo "      DBUS_VERSION_MICRO='$DBUS_VERSION_MICRO'"
	exit 1
else

	echo "Your dbus version is $DBUS_VERSION_MAJOR,$DBUS_VERSION_MINOR,$DBUS_VERSION_MICRO."
	DBUS_CFLAGS="$DBUS_CFLAGS -DDBUS_VERSION_MAJOR=$DBUS_VERSION_MAJOR"
	DBUS_CFLAGS="$DBUS_CFLAGS -DDBUS_VERSION_MINOR=$DBUS_VERSION_MINOR"
	DBUS_CFLAGS="$DBUS_CFLAGS -DDBUS_VERSION_MICRO=$DBUS_VERSION_MICRO"
fi
AC_SUBST(DBUS_CFLAGS)

# Check for dbus-1.2.6 or later for deny-by-default rules
PKG_CHECK_MODULES(DBUS_126, [dbus-1 >= 1.2.6], [have_dbus_126=yes],[have_dbus_126=no])
AM_CONDITIONAL(HAVE_DBUS_126, test x"$have_dbus_126" = "xyes")

#### PolicyKit checks; need at least 0.6 or later
PKG_CHECK_EXISTS(polkit-gnome,[have_polkit_gnome=yes],[have_polkit_gnome=no])
if test x"$have_polkit_gnome" = "xyes"; then
	PKG_CHECK_MODULES(POLKIT, polkit-gnome)
else
	PKG_CHECK_MODULES(POLKIT, [polkit >= 0.6])
	PKG_CHECK_MODULES(POLKIT_DBUS, [polkit-dbus >= 0.6])
	AC_DEFINE([NO_POLKIT_GNOME],[1],[Define if you don't have PolicyKit-gnome 0.7 or later])
fi
AM_CONDITIONAL(NO_POLKIT_GNOME, test x"$have_polkit_gnome" = "xno")

##### Find out the version of PolicyKit we're using
polkit_version=`pkg-config --modversion polkit`
POLKIT_VERSION_MAJOR=`echo $polkit_version | awk -F. '{print $1}'`
POLKIT_VERSION_MINOR=`echo $polkit_version | awk -F. '{print $2}'`
POLKIT_VERSION_MICRO=`echo $polkit_version | awk -F. '{print $3}'`
if test "z$POLKIT_VERSION_MAJOR" = "z"; then
	POLKIT_VERSION_MAJOR="0"
fi
if test "z$POLKIT_VERSION_MINOR" = "z"; then
	POLKIT_VERSION_MINOR="0"
fi
if test "z$POLKIT_VERSION_MICRO" = "z"; then
	POLKIT_VERSION_MICRO="0"
fi

if test "z$POLKIT_VERSION_MAJOR" = "z0" -a "z$POLKIT_VERSION_MINOR" = "z0" -a "z$POLKIT_VERSION_MICRO" = "z0"; then
	echo "Error: Couldn't determine the version of your PolicyKit package."
	echo "  This is probably an error in this script, please report it"
	echo "  along with the following information:"
	echo "      Base PolicyKit version ='$polkit_version'"
	echo "      POLKIT_VERSION_MAJOR='$POLKIT_VERSION_MAJOR'"
	echo "      POLKIT_VERSION_MINOR='$POLKIT_VERSION_MINOR'"
	echo "      POLKIT_VERSION_MICRO='$POLKIT_VERSION_MICRO'"
	exit 1
else
	echo "Your PolicyKit version is $POLKIT_VERSION_MAJOR,$POLKIT_VERSION_MINOR,$POLKIT_VERSION_MICRO."
	POLKIT_CFLAGS="$POLKIT_CFLAGS -DPOLKIT_VERSION_MAJOR=$POLKIT_VERSION_MAJOR"
	POLKIT_CFLAGS="$POLKIT_CFLAGS -DPOLKIT_VERSION_MINOR=$POLKIT_VERSION_MINOR"
	POLKIT_CFLAGS="$POLKIT_CFLAGS -DPOLKIT_VERSION_MICRO=$POLKIT_VERSION_MICRO"
fi
AC_SUBST(POLKIT_CFLAGS)

AC_MSG_CHECKING([Linux Wireless Extensions >= 18])
AC_TRY_COMPILE([#ifndef __user
                #define __user
                #endif
                #include <sys/types.h>
                #include <linux/types.h>
                #include <sys/socket.h>
                #include <net/if.h>
                #include <wireless.h>],
               [#ifndef IWEVGENIE
                #error "not found"
                #endif],
               [ac_have_iwevgenie=yes],
               [ac_have_iwevgenie=no])
AC_MSG_RESULT($ac_have_iwevgenie)
if test "$ac_have_iwevgenie" = no; then
	AC_MSG_ERROR(wireless-tools >= 28pre9 not installed or not functional)
fi

AC_ARG_WITH(dbus-sys, AS_HELP_STRING([--with-dbus-sys=DIR], [where D-BUS system.d directory is]))

if ! test -z "$with_dbus_sys" ; then
    DBUS_SYS_DIR="$with_dbus_sys"
else
    DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

NM_COMPILER_WARNINGS

AC_CONFIG_FILES([
Makefile
src/Makefile
src/marshallers/Makefile
src/utils/Makefile
src/gconf-helpers/Makefile
src/wireless-security/Makefile
src/polkit-helpers/Makefile
src/connection-editor/Makefile
icons/Makefile
icons/16/Makefile
icons/22/Makefile
icons/32/Makefile
icons/48/Makefile
icons/scalable/Makefile
po/Makefile.in
])
AC_OUTPUT
