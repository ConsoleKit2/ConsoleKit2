
AC_COPYRIGHT([Copyright 2014-2017 The ConsoleKit2 Team])

AC_PREREQ([2.62])
AC_INIT([ConsoleKit2],
        [1.2.0],
        [https://github.com/ConsoleKit2/ConsoleKit2/issues],
        [ConsoleKit2],
        [https://github.com/ConsoleKit2/ConsoleKit2])
VERSION_MAJOR=1
VERSION_MINOR=2
VERSION_REVISION=0


COPYRIGHT_YEARS="2014 - 2017"

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([autotools])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/ck-manager.c])

AM_INIT_AUTOMAKE([1.9 dist-bzip2 tar-ustar no-dist-gzip])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE([enable])

# for O_NOFOLLOW support
AC_USE_SYSTEM_EXTENSIONS

AC_SEARCH_LIBS([strerror],[cposix])
AC_PROG_CC
AM_PROG_CC_C_O
AC_HEADER_STDC
LT_INIT([disable-static])
AC_HEADER_STDC
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_HEADER_MAJOR
AC_PATH_X
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UID_T

AC_SUBST(VERSION)

# Save flags to aclocal
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

AM_GNU_GETTEXT_VERSION([0.19])
GETTEXT_PACKAGE=ConsoleKit2
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])
AM_GNU_GETTEXT([external])
AM_NLS

# Dependencies
m4_define([glib_minimum_version], [2.40])
m4_define([dbus_minimum_version], [0.82])
m4_define([x11_minimum_version], [1.0.0])
m4_define([polkit_minimum_version], [0.92])
m4_define([cgmanager_minimum_version], [0.0.0])
m4_define([libevdev_minimum_version], [0.2])
m4_define([libdrm_minimum_version], [2.4.60])
m4_define([libudev_minimum_version], [190])
m4_define([selinux_minimum_version], [1.0])

AC_CHECK_HEADERS([unistd.h paths.h sys/vt.h sys/consio.h fcntl.h limits.h \
                  sys/ioctl.h sys/param.h sys/socket.h syslog.h kvm.h \
                  sys/sysctl.h sys/user.h poll.h libintl.h locale.h \
                  sys/wait.h sys/resource.h sys/mount.h sys/param.h ftw.h \
		  sys/sysmacros.h sys/types.h libudev.h linux/input.h \
		  sys/mkdev.h devattr.h sys/kd.h sys/kbio.h libprop/proplib.h \
		  linux/kd.h sys/queue.h sys/stat.h sys/fcntl.h \
		  dev/wscons/wsdisplay_usl_io.h dev/wscons/wsconsio.h])

AC_CHECK_FUNCS([getpeerucred getpeereid memset setenv strchr strdup \
                strerror strrchr strspn strstr strtol strtoul uname \
                setlocale mount umount unmount])

AC_CHECK_MEMBERS([struct stat.st_rdev])

AC_CHECK_FILE(/sys/class/tty/tty0/active,
              [AC_DEFINE([HAVE_SYS_VT_SIGNAL], [1], [System has a means of signaling VT changes])],
              [])


XDT_CHECK_PACKAGE([LIBDBUS], [dbus-1], [dbus_minimum_version])
XDT_CHECK_PACKAGE([GLIB], [glib-2.0], [glib_minimum_version])
XDT_CHECK_PACKAGE([GIO], [gio-2.0], [glib_minimum_version])
XDT_CHECK_PACKAGE([GIO_UNIX], [gio-unix-2.0], [glib_minimum_version])


AC_DEFINE(GLIB_VERSION_MIN_REQUIRED, GLIB_VERSION_2_40, [Ignore post 2.40 deprecations])
AC_DEFINE(GLIB_VERSION_MAX_ALLOWED, GLIB_VERSION_2_40, [Prevent post 2.40 APIs])


# Check for X11 installed
XDT_CHECK_LIBX11_REQUIRE()

# Check for Polkit/PolicyKit
XDT_CHECK_OPTIONAL_PACKAGE([POLKIT], [polkit-gobject-1], [polkit_minimum_version],
                           [polkit], [Enable PolicyKit support @<:@default=auto@:>@])


AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)


# Solaris requires libresolv for daemon()
case "$host_os" in
	solaris*)
		AC_CHECK_LIB(resolv, daemon, [CK2_LIBS="$CK2_LIBS -lresolv"])
	;;
	kfreebsd*-gnu)
		have_getpeereid=no
		AC_CHECK_LIB(bsd, getpeereid, have_getpeereid=yes)
		if test "x$have_getpeereid" = "xyes"; then
			CK2_LIBS="${CK2_LIBS} -lbsd"
			TOOLS_LIBS="${TOOLS_LIBS} -lbsd"
			AC_DEFINE(HAVE_GETPEEREID, [], [Define if we have getpeereid])
		fi
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
dnl - Path to rundir e.g. /var/run
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(rundir,
	[AS_HELP_STRING([--with-rundir=<dir>],[run directory location, e.g. /var/run])])
if ! test -z "$with_rundir" ; then
        RUNDIR="$with_rundir"
else
        RUNDIR="$localstatedir/run"
fi
AC_SUBST(RUNDIR)


dnl ---------------------------------------------------------------------------
dnl - PID file
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(pid-file,
	[AS_HELP_STRING([--with-pid-file=<file>],[pid file location])])

if ! test -z "$with_pid_file"; then
   CONSOLE_KIT_PID_FILE=$with_pid_file
else
   CONSOLE_KIT_PID_FILE=${RUNDIR}/ConsoleKit/pid
fi

AC_SUBST(CONSOLE_KIT_PID_FILE)

dnl ---------------------------------------------------------------------------
dnl Figure out what tools backend to build
dnl ---------------------------------------------------------------------------

CK_BACKEND=""
KVM_LIBS=""
case "$host" in
        *-*-openbsd*)
        CK_BACKEND="openbsd"
        AC_CHECK_LIB(kvm, kvm_openfiles, have_kvm=yes,
                AC_MSG_ERROR([Unable to find libkvm which is needed on OpenBSD]))
        if test "x$have_kvm" = "xyes"; then
                KVM_LIBS="-lkvm"
        fi
        ;;
        *-*-freebsd* | *-*-kfreebsd*-gnu )
        CK_BACKEND="freebsd"
        AC_CHECK_LIB(kvm, kvm_openfiles, have_kvm=yes,
                AC_MSG_ERROR([Unable to find libkvm which is needed on FreeBSD/DragonflyBSD]))
        if test "x$have_kvm" = "xyes"; then
                KVM_LIBS="-lkvm"
        fi
	AC_CHECK_LIB(procstat, procstat_open_sysctl, have_procstat=yes,
                AC_MSG_WARN([Unable to find libprocstat which is used on FreeBSD]))
	if test "x$have_procstat" = "xyes"; then
                KVM_LIBS="${KVM_LIBS} -lprocstat"
        fi
        ;;
        *-*-dragonfly*)
        CK_BACKEND="dragonfly"
        AC_CHECK_LIB(kvm, kvm_openfiles, have_kvm=yes,
        AC_MSG_ERROR([Unable to find libkvm which is needed on DragonflyBSD]))
        if test "x$have_kvm" = "xyes"; then
        KVM_LIBS="-lkvm"
        fi
	      AC_CHECK_LIB(procstat, procstat_open_sysctl, have_procstat=yes,
        AC_MSG_WARN([Unable to find libprocstat which is used on FreeBSD]))
	      if test "x$have_procstat" = "xyes"; then
        KVM_LIBS="${KVM_LIBS} -lprocstat"
        fi
        ;;
        *-*-netbsd*)
        CK_BACKEND="netbsd"
        AC_CHECK_LIB(kvm, kvm_openfiles, have_kvm=yes,
                AC_MSG_ERROR([Unable to find libkvm which is needed on NetBSD]))
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
AM_CONDITIONAL(CK_COMPILE_DRAGONFLY, test x$CK_BACKEND = xdragonfly, [Compiling for DragonFly])
AM_CONDITIONAL(CK_COMPILE_NETBSD, test x$CK_BACKEND = xnetbsd, [Compiling for NetBSD])
AM_CONDITIONAL(CK_COMPILE_OPENBSD, test x$CK_BACKEND = xopenbsd, [Compiling for OpenBSD])
AM_CONDITIONAL(CK_COMPILE_SOLARIS, test x$CK_BACKEND = xsolaris, [Compiling for Solaris])
AM_CONDITIONAL(CK_COMPILE_GNU, test x$CK_BACKEND = xgnu, [Compiling for GNU])
AC_SUBST(CK_BACKEND)

dnl ---------------------------------------------------------------------------
dnl Check for PAM
dnl ---------------------------------------------------------------------------

have_pam=no
have_linuxpam=no
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

#Check if we can build an optional test program
AC_CHECK_LIB(pam_misc, misc_conv, have_linuxpam=yes)
AM_CONDITIONAL(HAVE_LINUXPAM, test "x$have_linuxpam" = "xyes")


# cgroups - Using the kernel to track processes instead of the cookie
XDT_CHECK_OPTIONAL_PACKAGE([CGMANAGER], [libcgmanager], [cgmanager_minimum_version],
    [libcgmanager],
    [cgmanager - Using the kernel to track processes instead of a session cookie])

# libevdev - for device management with a session controller
XDT_CHECK_OPTIONAL_PACKAGE([LIBEVDEV], [libevdev], [libevdev_minimum_version],
    [libevdev],
    [libevdev - for device management with a sessions controller])

# libdrm - for device management with a session controller
XDT_CHECK_OPTIONAL_PACKAGE([LIBDRM], [libdrm], [libdrm_minimum_version],
    [libdrm],
    [libdrm - for device management with a sessions controller])

# libudev - use udev as a device manager for ck-device-udev.c
XDT_CHECK_OPTIONAL_PACKAGE([LIBUDEV], [libudev], [libudev_minimum_version],
    [libudev],
    [libudev - use udev as a device manager for ck-device-udev.c])

# selinux
XDT_CHECK_OPTIONAL_PACKAGE([SELINUX], [libselinux], [selinux_minimum_version], [libselinux],
                           [Security-Enhanced Linux support])


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




# DragonFly BSD supplies devattr, we already checked for the header above
# make sure we can find the lib
have_devattr=no
AC_CHECK_LIB([devattr], [udev_device_unref], [
	LIBS="$LIBS -ldevattr"
	have_devattr=yes
	])

have_libprop=no
AC_CHECK_LIB([prop], [prop_dictionary_get_cstring], [
	LIBS="$LIBS -lprop"
	have_libprop=yes
	])


use_udev=no
if test ["x$LIBUDEV_FOUND" = "xyes" -a "x$LIBDRM_FOUND" = "xyes"]; then
	use_udev=yes
fi
if test ["$have_devattr" = "yes" -a "$have_libprop" = "yes"]; then
	use_udev=yes
fi



# we need both udev/devattr and libdrm for udev support
AS_IF(test ["$use_udev" = "yes"], [
  AC_DEFINE([USE_UDEV_BACKEND], [1], [Define if we meet requirements to use the udev backend])
])
AM_CONDITIONAL([USE_UDEV_BACKEND], [test ["$use_udev" = "yes"]])
AC_SUBST(USE_UDEV_BACKEND)



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
dnl - Install directory for xinitrc file
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(xinitrc-dir,
	[AS_HELP_STRING([--with-xinitrc-dir=<dir>],[directory to install Xsession.d script for ck-launch-session])])
if ! test -z "$with_xinitrc_dir"; then
	XINITRC_DIR="$with_xinitrc_dir"
else
	XINITRC_DIR="$sysconfdir/X11/xinit/xinitrc.d"
fi
AC_SUBST(XINITRC_DIR)

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

AC_ARG_VAR([XMLTO],[Define/override the xmlto location.])
AC_ARG_VAR([XMLTO_FLAGS],[Define/override xmlto options, like --skip-validation.])

dnl ---------------------------------------------------------------------------
dnl Check for xsltproc
dnl ---------------------------------------------------------------------------
AC_PATH_PROG([XSLTPROC], [xsltproc])


# check for gtk-doc
m4_ifdef([GTK_DOC_CHECK], [
GTK_DOC_CHECK([1.14],[--flavour no-tmpl])
],[
AM_CONDITIONAL([ENABLE_GTK_DOC], false)
])

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

#
# Enable Tests
#
AC_ARG_ENABLE(tests,
	[AS_HELP_STRING([--enable-tests],[enable validation test framework @<:@default=@<:@disabled@:>@@:>@])],
	enable_tests=yes, enable_tests=no)
AC_MSG_CHECKING([whether to build the validation test framework])
AM_CONDITIONAL(BUILD_TESTS, test "x$enable_tests" = "xyes")
AC_MSG_RESULT($enable_tests)

dnl ---------------------------------------------------------------------------
dnl Finish
dnl ---------------------------------------------------------------------------

# Check for introspection for libck2
GOBJECT_INTROSPECTION_CHECK([1.30.0])

# Enable Debug
XDT_FEATURE_DEBUG

# Check for linker optimizations
XDT_FEATURE_LINKER_OPTS()

# Flags

AC_SUBST(CK2_LIBS)
AC_SUBST(TOOLS_LIBS)

AC_SUBST(CFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LDFLAGS)

# systemd
AC_ARG_WITH([systemdsystemunitdir],
        AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
        [], [with_systemdsystemunitdir=no])
AS_IF([test "x$with_systemdsystemunitdir" != "xno"], [ AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])])
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != "xno"])

# Misc version info
VERSION_MESSAGE="${VERSION}"
COPYRIGHT_MESSAGE="Copyright © ${COPYRIGHT_YEARS} the ConsoleKit2 team"
AC_DEFINE_UNQUOTED(VERSION_MESSAGE, "${VERSION_MESSAGE}", [Simple version string])
AC_DEFINE_UNQUOTED(COPYRIGHT_MESSAGE, "${COPYRIGHT_MESSAGE}", [Copyright string])
AC_DEFINE_UNQUOTED(COPYRIGHT_YEARS, "${COPYRIGHT_YEARS}", [The copyright years])
AC_DEFINE_UNQUOTED(PACKAGE_VERSION_MAJOR,${VERSION_MAJOR}, [version major number])
AC_DEFINE_UNQUOTED(PACKAGE_VERSION_MINOR,${VERSION_MINOR}, [version minor number])
AC_DEFINE_UNQUOTED(PACKAGE_VERSION_REVISION,${VERSION_REVISION}, [version revision number])
AC_DEFINE_UNQUOTED(PACKAGE, "ConsoleKit2", [The name of this package])
AC_SUBST(COPYRIGHT_MESSAGE)
AC_SUBST(VERSION_MESSAGE)
AC_SUBST(VERSION_MAJOR)
AC_SUBST(VERSION_MINOR)
AC_SUBST(VERSION_REVISION)
AC_SUBST(COPYRIGHT_YEARS)
AC_SUBST(PACKAGE)

# Files

AC_CONFIG_FILES([
Makefile
src/Makefile
tools/70-udev-acl.rules
tools/71-udev-seat.rules
tools/Makefile
tools/linux/Makefile
tools/freebsd/Makefile
tools/netbsd/Makefile
tools/openbsd/Makefile
tools/solaris/Makefile
data/Makefile
doc/Makefile
doc/console-kit-daemon.1m
doc/dbus/ConsoleKit.xml
doc/dbus/Makefile
doc/libconsolekit/Makefile
libconsolekit/Makefile
libconsolekit/libconsolekit.pc
libck-connector/Makefile
libck-connector/ck-connector.pc
pam-ck-connector/Makefile
po/Makefile.in
])

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
        rundir:                   ${RUNDIR}
        datadir:                  ${datadir}
	source code location:	  ${srcdir}
	compiler:		  ${CC}
	cflags:		          ${CFLAGS}
        Debugging support:        $enable_debug
	build tests:              ${enable_tests}
        Maintainer mode:          ${USE_MAINTAINER_MODE}

        dbus-1 system.d dir:      ${DBUS_SYS_DIR}
        Build backend:            ${CK_BACKEND}
        PAM module dir:           ${PAM_MODULE_DIR}
        Build PAM module:         ${msg_pam_module}
        Build udev-acl:           ${enable_udev_acl}"

if test x"$LIBUDEV_FOUND" = x"yes"; then
echo "        udev support              yes"
else
echo "        udev support              no"
fi

if test x"$CGMANAGER_FOUND" = x"yes"; then
echo "        cgroup support            yes"
else
echo "        cgroup support            no"
fi

if test x"$LIBEVDEV_FOUND" = x"yes"; then
echo "        libevdev support          yes"
else
echo "        libevdev support          no"
fi

if test x"$LIBDRM_FOUND" = x"yes"; then
echo "        libdrm support            yes"
else
echo "        libdrm support            no"
fi

if test "$use_udev" = "yes"; then
echo "        session controller        udev"
else
echo "        session controller        none"
fi

if test x"$POLKIT_FOUND" = x"yes"; then
echo "        SELinux support:          yes"
else
echo "        SELinux support:          no"
fi

echo "
        Build docs:               ${enable_docbook_docs}
        xinitrc dir:              ${XINITRC_DIR}"

if test x"$POLKIT_FOUND" = x"yes"; then
echo "        PolicyKit support         yes"
else
echo "        PolicyKit support         no"
fi

echo "        RBAC (Solaris) support:   ${msg_rbac_shutdown}"

if test x"$POLKIT_FOUND" != x"yes" -a x${msg_rbac_shutdown} = xno; then
   echo "WARNING: PolicyKit and RBAC are disabled. You need to manually edit the ConsoleKit.conf"
   echo "         file to lock down the service. Failure to do so may be"
   echo "         a huge SECURITY HOLE. I repeat: YOU NEED TO EDIT THE FILE"
   echo "         ConsoleKit.conf to match your distro/site to avoid NASTY SECURITY HOLES."
   echo ""
fi
