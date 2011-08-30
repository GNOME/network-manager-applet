AC_DEFUN([TRANSLATE_COUNTRY_NAMES], [
	AC_CONFIG_COMMANDS([country-names], [
	if test -f "$1/Rules-iso3166"; then
		cat "$1/Rules-iso3166" >> po/Makefile.in
	fi
	])
])
