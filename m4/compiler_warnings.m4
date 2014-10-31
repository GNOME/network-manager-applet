AC_DEFUN([NM_COMPILER_WARNINGS],
[AC_ARG_ENABLE(more-warnings,
	AS_HELP_STRING([--enable-more-warnings], [Possible values: no/yes/error]),
	set_more_warnings="$enableval",set_more_warnings=error)
AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
	AC_MSG_RESULT(yes)
	CFLAGS="-Wall -std=gnu89 $CFLAGS"

	dnl clang only warns about unknown warnings, unless
	dnl called with "-Werror=unknown-warning-option"
	dnl Test if the compiler supports that, and if it does
	dnl attach it to the CFLAGS.
	SAVE_CFLAGS="$CFLAGS"
	EXTRA_CFLAGS="-Werror=unknown-warning-option"
	CFLAGS="$SAVE_CFLAGS $EXTRA_CFLAGS"
	AC_TRY_COMPILE([], [],
		has_option=yes,
		has_option=no,)
	if test $has_option = no; then
		EXTRA_CFLAGS=
	fi
	CFLAGS="$SAVE_CFLAGS"
	unset has_option
	unset SAVE_CFLAGS

	for option in -Wshadow -Wmissing-declarations -Wmissing-prototypes \
		      -Wdeclaration-after-statement -Wformat-security \
		      -Wstrict-prototypes \
		      -Wfloat-equal -Wno-unused-parameter -Wno-sign-compare \
		      -fno-strict-aliasing -Wno-unused-but-set-variable \
		      -Wundef -Wimplicit-function-declaration \
		      -Wpointer-arith -Winit-self \
		      -Wmissing-include-dirs -Waggregate-return; do
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $EXTRA_CFLAGS $option"
		AC_MSG_CHECKING([whether gcc understands $option])
		AC_TRY_COMPILE([], [],
			has_option=yes,
			has_option=no,)
		if test $has_option = no; then
			CFLAGS="$SAVE_CFLAGS"
		else
			CFLAGS="$SAVE_CFLAGS $option"
		fi
		AC_MSG_RESULT($has_option)
		unset has_option
		unset SAVE_CFLAGS
	done
	unset option
	unset EXTRA_CFLAGS
	if test "x$set_more_warnings" = xerror; then
		CFLAGS="$CFLAGS -Werror"
	fi
else
	AC_MSG_RESULT(no)
fi
])
