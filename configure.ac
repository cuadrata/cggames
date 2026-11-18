dnl Process this file with autoreconf -vif to produce configure.

AC_PREREQ([2.71])
AC_INIT([cggames], [1.4.0], [], [cggames])
AC_CONFIG_SRCDIR([cgmahjong/cgmahjong.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.16 foreign subdir-objects])

AC_PROG_CC
AC_PROG_RANLIB

dnl ISO C99, as CGTK is, unless the user chose a language standard,
dnl with POSIX.1-2008 (SUSv4) for getline and clock_gettime.
case " $CC $CFLAGS " in
*" -std="*) ;;
*)
  AC_MSG_CHECKING([whether $CC accepts -std=c99])
  games_save_CC="$CC"
  CC="$CC -std=c99"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [return 0;])],
    [AC_MSG_RESULT(yes)], [AC_MSG_RESULT(no); CC="$games_save_CC"])
  ;;
esac
case " $CC $CPPFLAGS $CFLAGS " in
*" -D_XOPEN_SOURCE"* | *" -D_POSIX_C_SOURCE"*) ;;
*) CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=700" ;;
esac

if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *@<:@\ \	@:>@-Wall@<:@\ \	@:>@*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac
fi

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([GTK], [gtk+-1.4 >= 1.4.0])

dnl The games' icons are PNG files, which CGTK has read since it took
dnl libpng in; their cards, tiles and pieces are SVG files, which it
dnl has drawn since it took NanoSVG in; and it gives their windows the
dnl icons they are installed with. Its version did not change with any
dnl of these, so they are looked for.
games_save_LIBS="$LIBS"
LIBS="$GTK_LIBS $LIBS"
AC_CHECK_FUNC([gdk_png_load], [],
  [AC_MSG_ERROR([this CGTK is too old: it has no gdk_png_load()])])
AC_CHECK_FUNC([gdk_svg_render_straight], [],
  [AC_MSG_ERROR([this CGTK is too old: it has no gdk_svg_render_straight()])])
AC_CHECK_FUNC([gtk_window_set_default_icon_name], [],
  [AC_MSG_ERROR([this CGTK is too old: it has no gtk_window_set_default_icon_name()])])
LIBS="$games_save_LIBS"

dnl clock_gettime is in libc on current systems, -lrt on old ones.
AC_SEARCH_LIBS([clock_gettime], [rt])
dnl The cards are drawn with floor and fabs.
AC_SEARCH_LIBS([floor], [m])

dnl Translations, from the system's gettext: one text domain for all the
dnl games, cggames, its catalogues in po.
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.6])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.6])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$PACKAGE_TARNAME"],
  [The games' text domain.])

AC_CONFIG_FILES([
Makefile
po/Makefile.in
])
AC_OUTPUT
