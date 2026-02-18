## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = \
	$(NULL)

if CK_COMPILE_LINUX
SUBDIRS += linux
endif

if CK_COMPILE_FREEBSD
SUBDIRS += freebsd
endif

if CK_COMPILE_NETBSD
SUBDIRS += netbsd
endif

if CK_COMPILE_OPENBSD
SUBDIRS += openbsd
endif

if CK_COMPILE_SOLARIS
SUBDIRS += solaris
endif

DIST_SUBDIRS = 		\
	linux		\
	freebsd		\
	netbsd		\
	openbsd		\
	solaris		\
	$(NULL)

AM_CFLAGS =							\
	-I.							\
	-I$(srcdir)						\
	-I$(top_srcdir)/src					\
	-I$(top_srcdir)/libck-connector				\
	$(GLIB_CFLAGS)							\
	$(GIO_CFLAGS)							\
	$(GIO_UNIX_CFLAGS)						\
	$(POLKIT_CFLAGS)					\
	$(LIBDBUS_CFLAGS)						\
	$(CGMANAGER_CFLAGS)					\
	$(LIBUDEV_CFLAGS)					\
	$(LIBDRM_CFLAGS)					\
	$(SELINUX_CFLAGS)					\
	$(LIBEVDEV_CFLAGS)					\
	$(RBAC_CFLAGS)					\
	$(LIBX11_CFLAGS)							\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\" 				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\" 				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DRUNDIR=\""$(RUNDIR)"\" 			\
	-DLOCALEDIR=\"$(localedir)\"		\
	$(NULL)

CONSOLE_KIT_LIBS = \
	$(CK2_LIBS)			\
	$(TOOLS_LIBS)		\
	$(GLIB_LIBS)		\
	$(GIO_LIBS)			\
	$(GIO_UNIX_LIBS)	\
	$(LIBDBUS_LIBS)		\
	$(SELINUX_LIBS)		\
	$(LIBEVDEV_LIBS)	\
	$(POLKIT_LIBS)		\
	$(RBAC_LIBS)		\
	$(CGMANAGER_LIBS)	\
	$(LIBUDEV_LIBS)		\
	$(LIBDRM_LIBS)		\
	$(LIBX11_LIBS)

bin_PROGRAMS = \
	ck-launch-session		\
	ck-list-sessions		\
	ck-history			\
	$(NULL)

sbin_PROGRAMS = \
	ck-log-system-start		\
	ck-log-system-restart		\
	ck-log-system-stop		\
	$(NULL)

ck_launch_session_SOURCES =		\
	ck-launch-session.c		\
	$(NULL)

ck_launch_session_LDADD =		\
	$(top_builddir)/libck-connector/libck-connector.la	\
	$(CONSOLE_KIT_LIBS)		\
	$(NULL)

ck_list_sessions_SOURCES =		\
	list-sessions.c			\
	$(NULL)

ck_list_sessions_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	$(XLIB_LIBS)			\
	$(NULL)

ck_history_SOURCES =			\
	ck-history.c			\
	$(NULL)

ck_history_LDADD =			\
	$(CONSOLE_KIT_LIBS)			\
	$(Z_LIBS)			\
	$(top_builddir)/src/libck-event-log.la	\
	$(NULL)

ck_log_system_start_SOURCES =		\
	ck-log-system-start.c		\
	$(NULL)

ck_log_system_start_LDADD =		\
	$(CONSOLE_KIT_LIBS)			\
	$(top_builddir)/src/libck-event-log.la	\
	$(NULL)

ck_log_system_restart_SOURCES =		\
	ck-log-system-restart.c		\
	$(NULL)

ck_log_system_restart_LDADD =		\
	$(CONSOLE_KIT_LIBS)			\
	$(top_builddir)/src/libck-event-log.la	\
	$(NULL)

ck_log_system_stop_SOURCES =		\
	ck-log-system-stop.c		\
	$(NULL)

ck_log_system_stop_LDADD =		\
	$(CONSOLE_KIT_LIBS)			\
	$(top_builddir)/src/libck-event-log.la	\
	$(NULL)

libexec_PROGRAMS = 			\
	ck-collect-session-info		\
	ck-remove-directory			\
	$(NULL)

if USE_LIBX11
libexec_PROGRAMS += 			\
	ck-get-x11-server-pid		\
	ck-get-x11-display-device
endif

ck_collect_session_info_SOURCES =	\
	ck-collect-session-info.c	\
	$(NULL)

ck_collect_session_info_LDADD =		\
	$(CONSOLE_KIT_LIBS)			\
	$(top_builddir)/src/libck.la	\
	$(NULL)

ck_get_x11_server_pid_SOURCES =		\
	ck-get-x11-server-pid.c		\
	$(NULL)

ck_get_x11_server_pid_LDADD =		\
	$(CONSOLE_KIT_LIBS)			\
	$(XLIB_LIBS)			\
	$(top_builddir)/src/libck.la	\
	$(NULL)

ck_get_x11_display_device_SOURCES =	\
	ck-get-x11-display-device.c	\
	$(NULL)

ck_get_x11_display_device_LDADD =	\
	$(CONSOLE_KIT_LIBS)			\
	$(top_builddir)/src/libck.la	\
	$(NULL)

ck_remove_directory_SOURCES =	\
	ck-remove-directory.c	\
	$(NULL)

ck_remove_directory_LDADD =		\
	$(CONSOLE_KIT_LIBS)			\
	$(top_builddir)/src/libck.la	\
	$(NULL)

if ENABLE_UDEV_ACL
udevrulesdir = $(UDEVDIR)/rules.d

dist_udevrules_DATA =	\
	70-udev-acl.rules	\
	71-udev-seat.rules

libexec_PROGRAMS += udev-acl

udev_acl_SOURCES = udev-acl.c
udev_acl_LDADD = $(UDEV_ACL_LIBS)
udev_acl_CFLAGS =       \
	$(UDEV_ACL_CFLAGS)  \
	$(AM_CFLAGS)        \
	$(NULL)

install-exec-hook:
	mkdir -p $(DESTDIR)$(libdir)/ConsoleKit/run-seat.d
	mkdir -p $(DESTDIR)$(UDEVDIR)
	ln -sf $(libexecdir)/udev-acl $(DESTDIR)$(libdir)/ConsoleKit/run-seat.d/udev-acl.ck
	ln -sf $(libexecdir)/udev-acl $(DESTDIR)$(UDEVDIR)/udev-acl
endif

EXTRA_DIST =				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
