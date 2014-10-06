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

if CK_COMPILE_SOLARIS
SUBDIRS += solaris
endif

DIST_SUBDIRS = 		\
	linux		\
	freebsd		\
	openbsd		\
	solaris		\
	$(NULL)

AM_CFLAGS =							\
	-I.							\
	-I$(srcdir)						\
	-I$(top_srcdir)/src					\
	-I$(top_srcdir)/libck-connector				\
	-U_FORTIFY_SOURCE						\
	$(CONSOLE_KIT_CFLAGS)					\
	$(XLIB_CFLAGS)							\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\" 				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\" 				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	$(HISTORY_CFLAGS)					\
	$(WARN_CFLAGS)						\
	$(DEBUG_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(NULL)

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
	$(HISTORY_LIBS)			\
	$(Z_LIBS)			\
	$(top_builddir)/src/libck-event-log.la	\
	$(NULL)

ck_log_system_start_SOURCES =		\
	ck-log-system-start.c		\
	$(NULL)

ck_log_system_start_LDADD =		\
	$(HISTORY_LIBS)			\
	$(top_builddir)/src/libck-event-log.la	\
	$(NULL)

ck_log_system_restart_SOURCES =		\
	ck-log-system-restart.c		\
	$(NULL)

ck_log_system_restart_LDADD =		\
	$(HISTORY_LIBS)			\
	$(top_builddir)/src/libck-event-log.la	\
	$(NULL)

ck_log_system_stop_SOURCES =		\
	ck-log-system-stop.c		\
	$(NULL)

ck_log_system_stop_LDADD =		\
	$(HISTORY_LIBS)			\
	$(top_builddir)/src/libck-event-log.la	\
	$(NULL)

libexec_PROGRAMS = 			\
	ck-collect-session-info		\
	ck-get-x11-server-pid		\
	ck-get-x11-display-device	\
	$(NULL)

ck_collect_session_info_SOURCES =	\
	ck-collect-session-info.c	\
	$(NULL)

ck_collect_session_info_LDADD =		\
	$(TOOLS_LIBS)			\
	$(top_builddir)/src/libck.la	\
	$(NULL)

ck_get_x11_server_pid_SOURCES =		\
	ck-get-x11-server-pid.c		\
	$(NULL)

ck_get_x11_server_pid_LDADD =		\
	$(TOOLS_LIBS)			\
	$(XLIB_LIBS)			\
	$(top_builddir)/src/libck.la	\
	$(NULL)

ck_get_x11_display_device_SOURCES =	\
	ck-get-x11-display-device.c	\
	$(NULL)

ck_get_x11_display_device_LDADD =	\
	$(TOOLS_LIBS)			\
	$(top_builddir)/src/libck.la	\
	$(NULL)

if ENABLE_UDEV_ACL
udevrulesdir = $(UDEVDIR)/rules.d

dist_udevrules_DATA = 70-udev-acl.rules
libexec_PROGRAMS += udev-acl

udev_acl_SOURCES = udev-acl.c
udev_acl_LDADD = $(UDEV_ACL_LIBS)
udev_acl_CFLAGS = $(UDEV_ACL_CFLAGS)

scriptdir = $(prefix)/lib/ConsoleKit/scripts
script_SCRIPTS =			\
	pam-foreground-compat.ck	\
	$(NULL)


install-exec-hook:
	mkdir -p $(DESTDIR)$(prefix)/lib/ConsoleKit/run-seat.d
	mkdir -p $(DESTDIR)$(prefix)/lib/ConsoleKit/run-session.d
	mkdir -p $(DESTDIR)$(UDEVDIR)
	ln -sf $(libexecdir)/udev-acl $(DESTDIR)$(prefix)/lib/ConsoleKit/run-seat.d/udev-acl.ck
	ln -sf $(libexecdir)/udev-acl $(DESTDIR)$(UDEVDIR)/udev-acl
	ln -sf $(prefix)/lib/ConsoleKit/scripts/pam-foreground-compat.ck $(DESTDIR)$(prefix)/lib/ConsoleKit/run-session.d/pam-foreground-compat.ck
endif

EXTRA_DIST =				\
	$(script_SCRIPTS)		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
