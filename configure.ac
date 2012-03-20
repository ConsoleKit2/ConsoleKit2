
AC_PREREQ([2.68])
AC_INIT([ConsoleKit],
        [0.4.6],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=ConsoleKit],
        [ConsoleKit])

AC_CONFIG_SRCDIR([src/ck-manager.c])

AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-bzip2])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE

# for O_NOFOLLOW support
AC_USE_SYSTEM_EXTENSIONS

AC_SEARCH_LIBS([strerror],[cposix])
AC_PROG_CC
AM_PROG_CC_C_O
AC_HEADER_STDC
AC_DISABLE_STATIC
LT_INIT

AC_HEADER_STDC

AC_SUBST(VERSION)

# Save flags to aclocal
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

GETTEXT_PACKAGE=ConsoleKit
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])

AM_GLIB_GNU_GETTEXT

# Dependencies

DBUS_REQUIRED_VERSION=0.82
GLIB_REQUIRED_VERSION=2.14
X11_REQUIRED_VERSION=1.0.0
POLKIT_REQUIRED_VERSION=0.92

AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(paths.h)
AC_CHECK_HEADERS(sys/vt.h)
AC_CHECK_HEADERS(sys/consio.h)

AC_CHECK_FUNCS(getpeerucred getpeereid)

AC_TYPE_UID_T

PKG_CHECK_MODULES(CONSOLE_KIT,
  dbus-glib-1 >= $DBUS_REQUIRED_VERSION
  gobject-2.0 >= $GLIB_REQUIRED_VERSION
  gthread-2.0 >= $GLIB_REQUIRED_VERSION
)

PKG_CHECK_MODULES(POLKIT,
  polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION,
  have_polkit=yes,
  have_polkit=no)
if test "x$have_polkit" = "xyes" ; then
       AC_DEFINE(HAVE_POLKIT, [], [Define if we have polkit])
fi
AM_CONDITIONAL(HAVE_POLKIT, test x$have_polkit = xyes)
AC_SUBST(HAVE_POLKIT)

PKG_CHECK_MODULES(LIBDBUS,
  dbus-1 >= $DBUS_REQUIRED_VERSION
)
PKG_CHECK_MODULES(TOOLS,
  x11 >= $X11_REQUIRED_VERSION
  glib-2.0 >= $GLIB_REQUIRED_VERSION
)

PKG_CHECK_MODULES(HISTORY,
  glib-2.0 >= $GLIB_REQUIRED_VERSION
)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

EXTRA_COMPILE_WARNINGS(yes)

# Solaris requires libresolv for daemon()
case "$host" in
	*-*-solaris*)
		AC_CHECK_LIB(resolv, daemon, [CONSOLE_KIT_LIBS="$CONSOLE_KIT_LIBS -lresolv"])
	;;
esac


dnl ---------------------------------------------------------------------------
dnl - Are we specifying a different dbus root ?
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(dbus-sys,
	[AS_HELP_STRING([--with-dbus-sys=<dir>],[where D-BUS system.d directory is])])
AC_ARG_WITH(dbus-services,
	[AS_HELP_STRING([--with-dbus-services=<dir>],[where D-BUS services directory is])])
if ! test -z "$with_dbus_sys" ; then
        DBUS_SYS_DIR="$with_dbus_sys"
else
        DBUS_SYS_DIR="$sysconfdir/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)


dnl ---------------------------------------------------------------------------
dnl - Check for libz
dnl ---------------------------------------------------------------------------

have_libz=no
AC_CHECK_LIB(z, compress,
  [AC_CHECK_HEADER(zlib.h, [have_libz=yes], [])], [])
if test "x$have_libz" != "xyes"; then
  AC_MSG_ERROR([Unable to find libz])
fi
Z_LIBS="${Z_LIBS} -lz"
AC_SUBST(Z_LIBS)

dnl ---------------------------------------------------------------------------
dnl - PID file
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(pid-file,
	[AS_HELP_STRING([--with-pid-file=<file>],[pid file location])])

if ! test -z "$with_pid_file"; then
   CONSOLE_KIT_PID_FILE=$with_pid_file
else
   CONSOLE_KIT_PID_FILE=${localstatedir}/run/ConsoleKit/pid
fi

AC_SUBST(CONSOLE_KIT_PID_FILE)

dnl ---------------------------------------------------------------------------
dnl Figure out what tools backend to build
dnl ---------------------------------------------------------------------------

CK_BACKEND=""
KVM_LIBS=""
case "$host" in
        *-*-freebsd* | *-*-kfreebsd*-gnu)
        CK_BACKEND="freebsd"
        AC_CHECK_LIB(kvm, kvm_openfiles, have_kvm=yes,
                AC_MSG_ERROR([Unable to find libkvm which is needed on FreeBSD]))
        if test "x$have_kvm" = "xyes"; then
                KVM_LIBS="-lkvm"
        fi
        ;;
        *-*-linux*)
        CK_BACKEND="linux"
        ;;
        *-*-solaris*)
        CK_BACKEND="solaris"
        ;;
        *-*-gnu*)
        CK_BACKEND="gnu"
        ;;
	*)
	AC_MSG_ERROR([No sysdeps back-end implemented for host $host])
	;;
esac

AC_SUBST(KVM_LIBS)

AM_CONDITIONAL(CK_COMPILE_LINUX, test x$CK_BACKEND = xlinux, [Compiling for Linux])
AM_CONDITIONAL(CK_COMPILE_FREEBSD, test x$CK_BACKEND = xfreebsd, [Compiling for FreeBSD])
AM_CONDITIONAL(CK_COMPILE_SOLARIS, test x$CK_BACKEND = xsolaris, [Compiling for Solaris])
AM_CONDITIONAL(CK_COMPILE_GNU, test x$CK_BACKEND = xgnu, [Compiling for GNU])
AC_SUBST(CK_BACKEND)

dnl ---------------------------------------------------------------------------
dnl Check for PAM
dnl ---------------------------------------------------------------------------

have_pam=no
AC_CHECK_LIB(pam, pam_getenv, have_pam=yes)
AM_CONDITIONAL(HAVE_PAM, test x$have_pam = xyes)
if test "x$have_pam" = "xyes"; then
	PAM_LIBS="${PAM_LIBS} -lpam"
fi
AC_SUBST(HAVE_PAM)
AC_SUBST(PAM_LIBS)

AC_CHECK_HEADERS([security/pam_modutil.h security/pam_ext.h])
AC_CHECK_LIB(pam, pam_syslog, [AC_DEFINE(HAVE_PAM_SYSLOG, [], [Define to 1 if you have the pam_syslog function])])

# Check if we should build the PAM module
msg_pam_module=no
AC_ARG_ENABLE(pam-module,
	[AS_HELP_STRING([--enable-pam-module],[build PAM module])],
        , enable_pam_module=no)
if test "x$enable_pam_module" = "xyes"; then
	if test "x$have_pam" = "xno"; then
    		AC_MSG_ERROR([--enable-pam-module requires PAM but PAM was not found])
	fi
	AC_DEFINE(ENABLE_PAM_MODULE, [], [Set if we build PAM module])
	msg_pam_module=yes
fi
AM_CONDITIONAL(ENABLE_PAM_MODULE, test "x$enable_pam_module" = "xyes")

dnl ------------------------------------------------------------------------------
dnl udev-acl - apply ACLs for users with local forground sessions
dnl ------------------------------------------------------------------------------
AC_ARG_ENABLE([udev-acl],
	AS_HELP_STRING([--enable-udev-acl], [enable local user acl permissions support @<:@default=disabled@:>@]),
	[], [enable_udev_acl=no])
AS_IF([test "x$enable_udev_acl" = "xyes"], [

	PKG_CHECK_MODULES([UDEV_ACL], [glib-2.0 >= 2.22.0 gobject-2.0 >= 2.22.0 libudev])
	AC_CHECK_LIB([acl], [acl_init], [UDEV_ACL_LIBS="$UDEV_ACL_LIBS -lacl"], AC_MSG_ERROR([libacl not found]))
	AC_CHECK_HEADER([acl/libacl.h], [:], AC_MSG_ERROR([libacl header not found]))
	UDEVDIR=`$PKG_CONFIG --variable udevdir udev`
	if test -z "$UDEVDIR" ; then
		UDEVDIR="/lib/udev"
	fi
	AC_SUBST(UDEVDIR)
])
AM_CONDITIONAL([ENABLE_UDEV_ACL], [test "x$enable_udev_acl" = "xyes"])


dnl ---------------------------------------------------------------------------
dnl - Install directory for PAM security module
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(pam-module-dir,
	[AS_HELP_STRING([--with-pam-module-dir=<dir>],[directory to install PAM security module])])
if ! test -z "$with_pam_module_dir"; then
	PAM_MODULE_DIR="$with_pam_module_dir"
else
	PAM_MODULE_DIR="/lib/security"
fi
AC_SUBST(PAM_MODULE_DIR)

dnl ---------------------------------------------------------------------------
dnl - DocBook Documentation
dnl ---------------------------------------------------------------------------

AC_ARG_ENABLE(docbook-docs,
	[AS_HELP_STRING([--enable-docbook-docs],[build documentation (requires xmlto)])],
	enable_docbook_docs=$enableval,enable_docbook_docs=no)
AC_PATH_PROG(XMLTO, xmlto, no)
AC_MSG_CHECKING([whether to build DocBook documentation])
if test x$XMLTO = xno ; then
	have_docbook=no
else
	have_docbook=yes
fi
if test x$enable_docbook_docs = xauto ; then
	 if test x$have_docbook = xno ; then
        	enable_docbook_docs=no
	else
		enable_docbook_docs=yes
	fi
fi
if test x$enable_docbook_docs = xyes; then
	if test x$have_docbook = xno; then
		AC_MSG_ERROR([Building DocBook docs explicitly required, but DocBook not found])
	fi
fi
AM_CONDITIONAL(DOCBOOK_DOCS_ENABLED, test x$enable_docbook_docs = xyes)
AC_MSG_RESULT(yes)

AC_ARG_VAR([XMLTO],[Define/override the `xmlto' location.])
AC_ARG_VAR([XMLTO_FLAGS],[Define/override `xmlto' options, like `--skip-validation'.])

dnl ---------------------------------------------------------------------------
dnl Check for xsltproc
dnl ---------------------------------------------------------------------------
AC_PATH_PROG([XSLTPROC], [xsltproc])

dnl ---------------------------------------------------------------------------
dnl check for inotify
dnl ---------------------------------------------------------------------------

enable_inotify=no
AC_CHECK_FUNC(inotify_init,
	[AC_CHECK_HEADER([sys/inotify.h],
		[enable_inotify=yes],
		[])],
                [])

AM_CONDITIONAL(ENABLE_INOTIFY, test "x$enable_inotify" = "xyes")

if test "x$enable_inotify" = "xyes" ; then
	AC_DEFINE(ENABLE_INOTIFY, [], [Define if we have inotify])
fi

dnl ---------------------------------------------------------------------------
dnl check for RBAC
dnl ---------------------------------------------------------------------------

msg_rbac_shutdown=no
AC_ARG_ENABLE(rbac-shutdown,
	[AS_HELP_STRING([--enable-rbac-shutdown=<key>],[Build with RBAC support specifying shutdown/reboot RBAC authentication key])],
	enable_rbac_shutdown=$enableval,enable_rbac_shutdown=no)
if test "x$enable_rbac_shutdown" != "xno"; then
	RBAC_LIBS="-lsecdb -lsocket -lnsl"
	AC_DEFINE(ENABLE_RBAC_SHUTDOWN, [], [Set if we build with RBAC support])
	AC_DEFINE_UNQUOTED(RBAC_SHUTDOWN_KEY, "$enable_rbac_shutdown", [Set if we build with RBAC support])
	msg_rbac_shutdown="yes, using key $enable_rbac_shutdown"
fi
AC_SUBST(RBAC_LIBS)

dnl ---------------------------------------------------------------------------
dnl Finish
dnl ---------------------------------------------------------------------------

# Turn on the additional warnings last, so -Werror doesn't affect other tests.

AC_ARG_ENABLE(more-warnings,
	[AS_HELP_STRING([--enable-more-warnings],[Maximum compiler warnings])],
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
        -Wcast-align -Wsign-compare -Wp,-D_FORTIFY_SOURCE=2 \
        $CFLAGS"

        for option in -Wno-strict-aliasing -Wno-sign-compare; do
                SAVE_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[has_option=yes],[has_option=no])
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
# Enable Debug
#
AC_ARG_ENABLE(debug,
	[AS_HELP_STRING([--enable-debug],[turn on debugging])],
	, enable_debug=yes)
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

# systemd

AC_ARG_WITH([systemdsystemunitdir],
        AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
        [], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
AS_IF([test "x$with_systemdsystemunitdir" != "xno"], [ AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])])
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != "xno"])

# Files

AC_CONFIG_FILES([
Makefile
src/Makefile
tools/Makefile
tools/linux/Makefile
tools/freebsd/Makefile
tools/solaris/Makefile
data/Makefile
doc/Makefile
doc/dbus/ConsoleKit.xml
doc/dbus/Makefile
libck-connector/Makefile
libck-connector/ck-connector.pc
pam-ck-connector/Makefile
])

AC_CONFIG_HEADERS([config.h])

AC_OUTPUT

echo "
                    ConsoleKit $VERSION
                    ========================

	prefix:                   ${prefix}
	exec_prefix:              ${exec_prefix}
        libdir:                   ${libdir}
        bindir:                   ${bindir}
        sbindir:                  ${sbindir}
        sysconfdir:               ${sysconfdir}
        localstatedir:            ${localstatedir}
        datadir:                  ${datadir}
	source code location:	  ${srcdir}
	compiler:		  ${CC}
	cflags:		          ${CFLAGS}
        Base libs:                ${CONSOLE_KIT_LIBS}
        Tools libs:               ${TOOLS_LIBS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}

        dbus-1 system.d dir:      ${DBUS_SYS_DIR}
        Build backend:            ${CK_BACKEND}
        PAM module dir:           ${PAM_MODULE_DIR}
        Build PAM module:         ${msg_pam_module}
        Build udev-acl:           ${enable_udev_acl}
        Build docs:               ${enable_docbook_docs}

        PolicyKit support         ${have_polkit}
        RBAC (Solaris) support:   ${msg_rbac_shutdown}
"
if test x${have_polkit} = xno -a x${msg_rbac_shutdown} = xno; then
   echo "WARNING: PolicyKit and RBAC are disabled. You need to manually edit the ConsoleKit.conf"
   echo "         file to lock down the service. Failure to do so may be"
   echo "         a huge SECURITY HOLE. I repeat: YOU NEED TO EDIT THE FILE"
   echo "         ConsoleKit.conf to match your distro/site to avoid NASTY SECURITY HOLES."
   echo ""
fi
