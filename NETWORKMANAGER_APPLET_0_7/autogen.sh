#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.
REQUIRED_AUTOMAKE_VERSION=1.7
PKG_NAME=nm-applet

(test -f $srcdir/configure.ac \
  && test -f $srcdir/src/applet.c) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level $PKG_NAME directory"
    exit 1
}

(cd $srcdir;
    autoreconf --install --symlink &&
    intltoolize --force &&
    autoreconf &&
    ./configure --enable-maintainer-mode $@
)

