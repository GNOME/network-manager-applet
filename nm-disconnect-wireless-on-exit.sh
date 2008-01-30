#!/bin/sh

GCONF_TOOL=/usr/bin/gconftool-2
GCONF_PATH=/apps/NetworkManagerApplet/disconnect_wireless_on_exit

if test "x$1" == "x"; then
	echo -n "Current value is: "
	$GCONF_TOOL --get $GCONF_PATH
	exit 0
fi

if test $1 == "true" || test $1 == "yes"; then
	echo "Setting to true"
	$GCONF_TOOL --set $GCONF_PATH --type=bool true
	exit 0
fi

if test $1 == "false" || test $1 == "no"; then
	echo "Setting to false"
	$GCONF_TOOL --set $GCONF_PATH --type=bool false
	exit 0
fi

echo "Usage: $0 [true|false]"
exit 1
