dnl Copyright (c) 2002-2015
dnl         The Xfce development team. All rights reserved.
dnl
dnl Written for Xfce by Benedikt Meurer <benny@xfce.org>.
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License along
dnl with this program; if not, write to the Free Software Foundation, Inc.,
dnl 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
dnl
dnl xdt-depends
dnl -----------
dnl  Contains M4 macros to check for software dependencies.
dnl  Partly based on prior work of the XDG contributors.
dnl



dnl We need recent a autoconf version
AC_PREREQ([2.60])


dnl XDT_SUPPORTED_FLAGS(VAR, FLAGS)
dnl
dnl For each token in FLAGS, checks to be sure the compiler supports
dnl the flag, and if so, adds each one to VAR.
dnl
AC_DEFUN([XDT_SUPPORTED_FLAGS],
[
  for flag in $2; do
    AC_MSG_CHECKING([if $CC supports $flag])
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $flag"
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([ ])], [flag_supported=yes], [flag_supported=no])
    CFLAGS="$saved_CFLAGS"
    AC_MSG_RESULT([$flag_supported])

    if test "x$flag_supported" = "xyes"; then
      $1="$$1 $flag"
    fi
  done
])



dnl XDT_FEATURE_DEBUG(default_level=minimum)
dnl
AC_DEFUN([XDT_FEATURE_DEBUG],
[
  dnl weird indentation to keep output indentation correct
  AC_ARG_ENABLE([debug],
                AC_HELP_STRING([--enable-debug@<:@=no|minimum|yes|full@:>@],
                               [Build with debugging support @<:@default=m4_default([$1], [minimum])@:>@])
AC_HELP_STRING([--disable-debug], [Include no debugging support]),
                [enable_debug=$enableval], [enable_debug=m4_default([$1], [minimum])])

  AC_MSG_CHECKING([whether to build with debugging support])
  if test x"$enable_debug" = x"full" -o x"$enable_debug" = x"yes"; then
    AC_DEFINE([DEBUG], [1], [Define for debugging support])

    xdt_cv_additional_CFLAGS="-DXFCE_DISABLE_DEPRECATED \
                              -Wall -Wextra \
                              -Wno-missing-field-initializers \
                              -Wno-unused-parameter -Wold-style-definition \
                              -Wdeclaration-after-statement \
                              -Wmissing-declarations \
                              -Wmissing-noreturn -Wpointer-arith \
                              -Wcast-align -Wformat -Wformat-security -Wformat-y2k \
                              -Winit-self -Wmissing-include-dirs -Wundef \
                              -Wnested-externs"
    CPPFLAGS="$CPPFLAGS"

    if test x`uname` = x"Linux"; then
      xdt_cv_additional_CFLAGS="$xdt_cv_additional_CFLAGS -fstack-protector"
    fi

    dnl # signal.h inline is crapy on openbsd
    if test x`uname` != x"OpenBSD"; then
      xdt_cv_additional_CFLAGS="$xdt_cv_additional_CFLAGS -Wredundant-decls"
    fi

    if test x"$enable_debug" = x"full"; then
      AC_DEFINE([DEBUG_TRACE], [1], [Define for tracing support])
      xdt_cv_additional_CFLAGS="$xdt_cv_additional_CFLAGS -O0 -g -Werror"
      CPPFLAGS="$CPPFLAGS -DG_ENABLE_DEBUG"
      AC_MSG_RESULT([full])
    else
      xdt_cv_additional_CFLAGS="$xdt_cv_additional_CFLAGS -g -Wshadow"
      AC_MSG_RESULT([yes])
    fi

    XDT_SUPPORTED_FLAGS([supported_CFLAGS], [$xdt_cv_additional_CFLAGS])

    ifelse([$CXX], , , [
      dnl FIXME: should test on c++ compiler, but the following line causes
      dnl        autoconf errors for projects that don't check for a
      dnl        c++ compiler at all.
      dnl AC_LANG_PUSH([C++])
      dnl XDT_SUPPORTED_FLAGS([supported_CXXFLAGS], [$xdt_cv_additional_CFLAGS])
      dnl AC_LANG_POP()
      dnl        instead, just use supported_CFLAGS...
      supported_CXXFLAGS="$supported_CFLAGS"
    ])

    CFLAGS="$CFLAGS $supported_CFLAGS"
    CXXFLAGS="$CXXFLAGS $supported_CXXFLAGS"
  else
    CPPFLAGS="$CPPFLAGS -DNDEBUG"

    if test x"$enable_debug" = x"no"; then
      CPPFLAGS="$CPPFLAGS -DG_DISABLE_CAST_CHECKS -DG_DISABLE_ASSERT"
      AC_MSG_RESULT([no])
    else
      AC_MSG_RESULT([minimum])
    fi
  fi
])


dnl XDT_FEATURE_VISIBILITY()
dnl
dnl Checks to see if the compiler supports the 'visibility' attribute
dnl If so, adds -DHAVE_GNUC_VISIBILTY to CPPFLAGS.  Also sets the
dnl automake conditional HAVE_GNUC_VISIBILITY.
dnl
AC_DEFUN([XDT_FEATURE_VISIBILITY],
[
  AC_ARG_ENABLE([visibility],
                AC_HELP_STRING([--disable-visibility],
                               [Don't use ELF visibility attributes]),
                [enable_visibility=$enableval], [enable_visibility=yes])
  have_gnuc_visibility=no
  if test "x$enable_visibility" != "xno"; then
    XDT_SUPPORTED_FLAGS([xdt_vis_test_cflags], [-Wall -Werror -Wno-unused-parameter])
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $xdt_vis_test_cflags"
    AC_MSG_CHECKING([whether $CC supports the GNUC visibility attribute])
    AC_COMPILE_IFELSE([AC_LANG_SOURCE(
    [
      void test_default (void);
      void test_hidden (void);

      void __attribute__ ((visibility("default"))) test_default (void) {}
      void __attribute__ ((visibility("hidden"))) test_hidden (void) {}

      int main (int argc, char **argv) {
        test_default ();
        test_hidden ();
        return 0;
      }
    ])],
    [
      have_gnuc_visibility=yes
      AC_MSG_RESULT([yes])
    ],
    [
      AC_MSG_RESULT([no])
    ])
    CFLAGS="$saved_CFLAGS"
  fi

  if test "x$have_gnuc_visibility" = "xyes"; then
    CPPFLAGS="$CPPFLAGS -DHAVE_GNUC_VISIBILITY"
    xdt_vis_hidden_cflags=""
    XDT_SUPPORTED_FLAGS([xdt_vis_hidden_cflags], [-xldscope=hidden])
    if test "x$xdt_vis_hidden_cflags" = "x"; then
      XDT_SUPPORTED_FLAGS([xdt_vis_hidden_cflags], [-fvisibility=hidden])
    fi
    CFLAGS="$CFLAGS $xdt_vis_hidden_cflags"
  fi

  AM_CONDITIONAL([HAVE_GNUC_VISIBILITY], [test "x$have_gnuc_visibility" = "xyes"])
])

dnl XDT_FEATURE_LINKER_OPTS
dnl
dnl Checks for and enables any special linker optimizations.
dnl
AC_DEFUN([XDT_FEATURE_LINKER_OPTS],
[
  AC_ARG_ENABLE([linker-opts],
                AC_HELP_STRING([--disable-linker-opts],
                               [Disable linker optimizations]),
                [enable_linker_opts=$enableval], [enable_linker_opts=yes])

  if test "x$enable_linker_opts" != "xno"; then
    if test x`uname` != x"OpenBSD"; then
      AC_MSG_CHECKING([whether $LD accepts --as-needed])
      case `$LD --as-needed -v 2>&1 </dev/null` in
      *GNU* | *'with BFD'*)
        LDFLAGS="$LDFLAGS -Wl,--as-needed"
        AC_MSG_RESULT([yes])
        ;;
      *)
        AC_MSG_RESULT([no])
        ;;
      esac
    fi
    AC_MSG_CHECKING([whether $LD accepts -O1])
    case `$LD -O1 -v 2>&1 </dev/null` in
    *GNU* | *'with BFD'*)
      LDFLAGS="$LDFLAGS -Wl,-O1"
      AC_MSG_RESULT([yes])
      ;;
    *)
      AC_MSG_RESULT([no])
      ;;
    esac
  fi
])
