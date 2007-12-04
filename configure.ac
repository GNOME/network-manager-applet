AC_PREREQ(2.52)

AC_INIT(nm-applet, 0.7.0, dcbw@redhat.com, nm-applet)
AM_INIT_AUTOMAKE([subdir-objects])
AM_MAINTAINER_MODE

AM_CONFIG_HEADER(config.h)

dnl
dnl Require programs
dnl
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LIBTOOL

dnl
dnl This doesn't depend on gnome support, only on gnome-common, autogen already
dnl uses gnome-autogen
dnl
GNOME_COMMON_INIT
GNOME_DEBUG_CHECK
GNOME_COMPILE_WARNINGS([maximum])
GNOME_MAINTAINER_MODE_DEFINES

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

PKG_CHECK_MODULES(NMA,
		[dbus-glib-1 >= 0.72
		 glib-2.0 >= 2.10
		 NetworkManager >= 0.7.0
		 libnm_glib
		 libnm-util
		 gtk+-2.0 >= 2.6
		 libglade-2.0
		 gconf-2.0
		 gnome-keyring-1
		 libgnomeui-2.0])

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

AC_ARG_WITH(notify, AC_HELP_STRING([--with-notify], [Use libnotify]), enable_notify=$withval, enable_notify=auto)

LIBNOTIFY_REQUIRED=0.3.0

# Building with GTK+ >= 2.10 will use the GtkStatusIcon applet
# implementation.  Earlier versions of GTK+ will use the EggTrayIcon
# applet implementation.

PKG_CHECK_EXISTS([gtk+-2.0 >= 2.10],[have_gtk_2_10=yes LIBNOTIFY_REQUIRED=0.4.3],[have_gtk_2_10=no])
if test x"$have_gtk_2_10" != xno; then
	AC_DEFINE_UNQUOTED([HAVE_STATUS_ICON], [1],
		[Use GtkStatusIcon for the applet's icon rather than an EggTrayIcon])
fi

if test x"$enable_notify" != xno; then
	PKG_CHECK_MODULES([NOTIFY], [libnotify >= $LIBNOTIFY_REQUIRED], [enable_notify=yes],
				[enable_notify=no])
	if test "x$enable_notify" != "xno"; then
		AC_DEFINE_UNQUOTED([ENABLE_NOTIFY], [1],
		[Enable notifications with libnotify])
	fi
fi

AM_CONDITIONAL(WITH_NOTIFY, test x"$enable_notify" != xno) # can't do it from inside 'if'
AM_CONDITIONAL([HAVE_GTK_2_10],[test "$have_gtk_2_10" = "yes"])

AC_MSG_CHECKING([for wireless-tools >= 28pre9])
AC_TRY_COMPILE([#include <iwlib.h>],
               [#ifndef IWEVGENIE
                #error "not found"
                #endif],
               [ac_have_iwevgenie=yes],
               [ac_have_iwevgenie=no])
AC_MSG_RESULT($ac_have_iwevgenie)
if test "$ac_have_iwevgenie" = no; then
	AC_MSG_ERROR(wireless-tools >= 28pre9 not installed or not functional)
fi
IWLIB=-liw
AC_SUBST(IWLIB)

AC_ARG_WITH(nss, AC_HELP_STRING([--with-nss], [Use NSS library for certificate and key operations]), ac_nss=$withval, ac_nss=auto)
if test x"$ac_nss" != xno; then
  PKG_CHECK_MODULES(NSS, [nss >= 3.11])
  AC_DEFINE(HAVE_NSS, 1, [Define if you have NSS])
fi
AM_CONDITIONAL(WITH_NSS, test x"$ac_nss" != xno)

AC_ARG_WITH(gnutls, AC_HELP_STRING([--with-gnutls], [Use gnutls and gcrypt libraries for certificate and key operations]), ac_gnutls=$withval, ac_gnutls=no)
if test x"$ac_gnutls" != xno; then
  PKG_CHECK_MODULES(GNUTLS, [gnutls >= 1.2])
  AC_PATH_PROG(LIBGCRYPT_CONFIG, libgcrypt-config, no)
else
  LIBGCRYPT_CONFIG=no
fi 
if test x"$LIBGCRYPT_CONFIG" = xno; then
   if test x"$ac_gnutls" = xyes; then
      AC_MSG_ERROR([gnutls explicitly requested but gcrypt not found on system])
   fi
   ac_gnutls=no
else
   if test x"$ac_gnutls" != xno; then
     AC_DEFINE(HAVE_GNUTLS, 1, [Define if you have libgnutls])
     LIBGCRYPT_CFLAGS=`$LIBGCRYPT_CONFIG --cflags`
     LIBGCRYPT_LIBS=`$LIBGCRYPT_CONFIG --libs`
     AC_SUBST(LIBGCRYPT_CFLAGS)
     AC_SUBST(LIBGCRYPT_LIBS)
   fi
fi
AM_CONDITIONAL(WITH_GNUTLS, test x"$ac_gnutls" != xno)

if test x"$ac_nss" = xno -a x"$ac_gnutls" = xno; then
  AC_MSG_ERROR([Please choose either NSS or gnutls for certificate and key operations])
fi

if test x"$ac_nss" = xyes -a x"$ac_gnutls" = xyes; then
  AC_MSG_ERROR([Please choose _one_ of NSS or gnutls for certificate and key operations])
fi

AC_ARG_WITH(dbus-sys, AC_HELP_STRING([--with-dbus-sys=DIR], [where D-BUS system.d directory is]))

if ! test -z "$with_dbus_sys" ; then
    DBUS_SYS_DIR="$with_dbus_sys"
else
    DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

AC_ARG_ENABLE(more-warnings,
AC_HELP_STRING([--enable-more-warnings], [Maximum compiler warnings]), set_more_warnings="$enableval",set_more_warnings=yes)
AC_MSG_CHECKING(for more warnings, including -Werror)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
	AC_MSG_RESULT(yes)
	CFLAGS="-Wall -Werror -std=gnu89 $CFLAGS"

	for option in -Wshadow -Wmissing-declarations -Wmissing-prototypes \
		      -Wdeclaration-after-statement -Wstrict-prototypes \
		      -Wfloat-equal -Wno-unused-parameter -Wno-sign-compare; do
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $option"
		AC_MSG_CHECKING([whether gcc understands $option])
		AC_TRY_COMPILE([], [],
			has_option=yes,
			has_option=no,)
		if test $has_option = no; then
			CFLAGS="$SAVE_CFLAGS"
		fi
		AC_MSG_RESULT($has_option)
		unset has_option
		unset SAVE_CFLAGS
	done
	unset option
else
	AC_MSG_RESULT(no)
fi

AC_OUTPUT([
Makefile
src/Makefile
src/utils/Makefile
src/gconf-helpers/Makefile
src/wireless-security/Makefile
src/connection-editor/Makefile
src/vpn-properties/Makefile
icons/Makefile
po/Makefile.in
])
