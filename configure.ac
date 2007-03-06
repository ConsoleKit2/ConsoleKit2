dnl -*- mode: m4 -*-

AC_INIT([ConsoleKit],
        [0.1.3],
        [mccann@jhu.edu])

AC_CONFIG_SRCDIR(src/ck-manager.c)

AM_INIT_AUTOMAKE(ConsoleKit, 0.1.3)

AM_CONFIG_HEADER(config.h)

AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
AC_PROG_LIBTOOL

AC_HEADER_STDC

AC_SUBST(VERSION)

# Save flags to aclocal
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

GETTEXT_PACKAGE=ConsoleKit
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])

AM_GLIB_GNU_GETTEXT

# Dependencies

DBUS_REQUIRED_VERSION=0.30
GLIB_REQUIRED_VERSION=2.7.0
GDK_REQUIRED_VERSION=2.8.0

AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(paths.h)

AC_TYPE_UID_T

AC_CHECK_FUNCS([setresuid setenv unsetenv clearenv])
PKG_CHECK_MODULES(CONSOLE_KIT,
        dbus-glib-1 >= $DBUS_REQUIRED_VERSION
        gobject-2.0 >= $GLIB_REQUIRED_VERSION
        gthread-2.0 >= $GLIB_REQUIRED_VERSION
)
PKG_CHECK_MODULES(LIBDBUS,
	dbus-1 >= $DBUS_REQUIRED_VERSION
)
PKG_CHECK_MODULES(TOOLS,
        gdk-2.0 >= $GDK_REQUIRED_VERSION
)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

EXTRA_COMPILE_WARNINGS(yes)

# Solaris requires libresolv for daemon()
case "$host" in
	*-*-solaris*)
		AC_CHECK_LIB(resolv, daemon, [CONSOLE_KIT_LIBS="$CONSOLE_KIT_LIBS -lresolv"])
	;;
esac

# Find out the version of DBUS we're using

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

if test "z$DBUS_VERSION_MAJOR" = "z0" -a "z$DBUS_VERSION_MINOR" = "z0" -a "z$DBUS_VERSION_MICRO" = "z0"; then        echo "Error: Couldn't determine the version of your DBUS package."
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

        AC_SUBST(DBUS_CFLAGS)
fi

dnl ---------------------------------------------------------------------------
dnl - Are we specifying a different dbus root ?
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(dbus-sys,
              [AC_HELP_STRING([--with-dbus-sys=<dir>],
              [where D-BUS system.d directory is])])
AC_ARG_WITH(dbus-services,
              [AC_HELP_STRING([--with-dbus-services=<dir>],
              [where D-BUS services directory is])])
if ! test -z "$with_dbus_sys" ; then
        DBUS_SYS_DIR="$with_dbus_sys"
else
        DBUS_SYS_DIR="$sysconfdir/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

dnl ---------------------------------------------------------------------------
dnl Expand vars
dnl ---------------------------------------------------------------------------

AS_AC_EXPAND(LOCALSTATEDIR, $localstatedir)
AS_AC_EXPAND(SYSCONFDIR, $sysconfdir)
AS_AC_EXPAND(DATADIR, $datadir)
AS_AC_EXPAND(BINDIR, $bindir)
AS_AC_EXPAND(SBINDIR, $sbindir)
AS_AC_EXPAND(LIBDIR, $libdir)
AS_AC_EXPAND(LIBEXECDIR, $libexecdir)

dnl ---------------------------------------------------------------------------
dnl - PID file
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(pid-file,    [  --with-pid-file=<file>     pid file])

if ! test -z "$with_pid_file"; then
   CONSOLE_KIT_PID_FILE=$with_pid_file
else
   CONSOLE_KIT_PID_FILE=${LOCALSTATEDIR}/run/ConsoleKit/pid
fi

AC_SUBST(CONSOLE_KIT_PID_FILE)
AC_DEFINE_UNQUOTED(CONSOLE_KIT_PID_FILE, "$CONSOLE_KIT_PID_FILE", [pid file])

dnl ---------------------------------------------------------------------------
dnl Figure out what tools backend to build
dnl ---------------------------------------------------------------------------

CK_BACKEND=""
case "$host" in
        *-*-freebsd*)
        ;;
        *-*-linux*)
        CK_BACKEND="linux"
        ;;
        *-*-solaris*)
        ;;
esac

AM_CONDITIONAL(CK_COMPILE_LINUX, test x$CK_BACKEND = xlinux, [Compiling for Linux])
AM_CONDITIONAL(CK_COMPILE_FREEBSD, test x$CK_BACKEND = xfreebsd, [Compiling for FreeBSD])
AM_CONDITIONAL(CK_COMPILE_SOLARIS, test x$CK_BACKEND = xsolaris, [Compiling for Solaris])
AC_SUBST(CK_BACKEND)

dnl ---------------------------------------------------------------------------
dnl Finish
dnl ---------------------------------------------------------------------------

# Turn on the additional warnings last, so -Werror doesn't affect other tests.

AC_ARG_ENABLE(more-warnings,
  AC_HELP_STRING([--enable-more-warnings], [Maximum compiler warnings]),
set_more_warnings="$enableval",[
if test -d $srcdir/.git; then
        set_more_warnings=yes
else
        set_more_warnings=no
fi
])
AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
        AC_MSG_RESULT(yes)
        CFLAGS="\
        -Wall \
        -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
        -Wnested-externs -Wpointer-arith \
        -Wcast-align -Wsign-compare \
        $CFLAGS"

        for option in -Wno-strict-aliasing -Wno-sign-compare; do
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

#
# Check for pam
#
have_pam=no
AC_CHECK_LIB(pam, pam_getenv, have_pam=yes)
AM_CONDITIONAL(HAVE_PAM, test x$have_pam = xyes)
if test "x$have_pam" = "xyes"; then
    PAM_LIBS="${PAM_LIBS} -lpam"
fi
AC_SUBST(HAVE_PAM)
AC_SUBST(PAM_LIBS)

#
# Check if we should build the PAM module
#
msg_pam_module=no
AC_ARG_ENABLE(pam-module, [AC_HELP_STRING([--enable-pam-module], [build PAM module])],, enable_pam_module=no)
if test "x$enable_pam_module" = "xyes"; then
  if test "x$have_pam" = "xno"; then
    AC_MSG_ERROR([--enable-pam-module requires PAM but PAM was not found])
  fi
  AC_DEFINE(ENABLE_PAM_MODULE, [], [Set if we build PAM module])
  msg_pam_module=yes
fi
AM_CONDITIONAL(ENABLE_PAM_MODULE, test "x$enable_pam_module" = "xyes")

#
# Enable Debug
#
AC_ARG_ENABLE(debug, [AC_HELP_STRING([--enable-debug=[no/yes]], [turn on debugging])],, enable_debug=yes)
if test "$enable_debug" = "yes"; then
  DEBUG_CFLAGS="-DG_ENABLE_DEBUG"
else
  if test "x$enable_debug" = "xno"; then
    DEBUG_CFLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS"
  else
    DEBUG_CFLAGS=""
  fi
fi
AC_SUBST(DEBUG_CFLAGS)

# Flags

AC_SUBST(CONSOLE_KIT_CFLAGS)
AC_SUBST(CONSOLE_KIT_LIBS)

AC_SUBST(CFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LDFLAGS)

# Files

AC_OUTPUT([
Makefile
src/Makefile
tools/Makefile
tools/linux/Makefile
data/Makefile
data/ConsoleKit
libck-connector/Makefile
libck-connector/ck-connector.pc
pam-ck-connector/Makefile
])

echo "
                    ConsoleKit $VERSION
                    ========================

	prefix:                   ${prefix}
	exec_prefix:              ${exec_prefix}
        libdir:                   ${LIBDIR}
        bindir:                   ${BINDIR}
        sbindir:                  ${SBINDIR}
        sysconfdir:               ${SYSCONFDIR}
        localstatedir:            ${LOCALSTATEDIR}
        datadir:                  ${DATADIR}
	source code location:	  ${srcdir}
	compiler:		  ${CC}
	cflags:		          ${CFLAGS}
        Base libs:                ${CONSOLE_KIT_LIBS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}

        Backend:                  ${CK_BACKEND}

        dbus-1 system.d dir:      ${DBUS_SYS_DIR}
        build PAM module:         ${msg_pam_module}

"
