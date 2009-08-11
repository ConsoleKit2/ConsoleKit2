## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = \
	$(NULL)

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	$(CONSOLE_KIT_CFLAGS)					\
	$(POLKIT_CFLAGS)					\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\" 				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\" 				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DCONSOLE_KIT_PID_FILE=\""$(CONSOLE_KIT_PID_FILE)"\"	\
	$(WARN_CFLAGS)						\
	$(DEBUG_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(NULL)

noinst_LTLIBRARIES =            \
        libck.la		\
        libck-event-log.la	\
        $(NULL)

libck_event_log_la_SOURCES =	\
	ck-log-event.h		\
	ck-log-event.c		\
	$(NULL)

libck_la_SOURCES =		\
	ck-sysdeps.h		\
	ck-sysdeps-unix.c	\
	$(NULL)

if CK_COMPILE_LINUX
libck_la_SOURCES +=		\
	ck-sysdeps-linux.c	\
	$(NULL)
endif
if CK_COMPILE_SOLARIS
libck_la_SOURCES +=		\
	ck-sysdeps-solaris.c	\
	$(NULL)
endif
if CK_COMPILE_FREEBSD
libck_la_SOURCES +=		\
	ck-sysdeps-freebsd.c	\
	$(NULL)
libck_la_LIBADD = $(KVM_LIBS)
endif

EXTRA_libck_la_SOURCES =		\
	ck-sysdeps-linux.c		\
	ck-sysdeps-solaris.c		\
	ck-sysdeps-freebsd.c		\
	$(NULL)

sbin_PROGRAMS = 		\
	console-kit-daemon	\
	$(NULL)

BUILT_SOURCES = 		\
	ck-manager-glue.h	\
	ck-seat-glue.h		\
	ck-session-glue.h	\
	ck-marshal.c 		\
	ck-marshal.h		\
	$(NULL)

dbusifdir = $(datadir)/dbus-1/interfaces
dbusif_DATA = 					\
	org.freedesktop.ConsoleKit.Manager.xml	\
	org.freedesktop.ConsoleKit.Seat.xml	\
	org.freedesktop.ConsoleKit.Session.xml	\
	$(NULL)

ck-manager-glue.h: org.freedesktop.ConsoleKit.Manager.xml Makefile.am
	dbus-binding-tool --prefix=ck_manager --mode=glib-server --output=ck-manager-glue.h org.freedesktop.ConsoleKit.Manager.xml
ck-seat-glue.h: org.freedesktop.ConsoleKit.Seat.xml Makefile.am
	dbus-binding-tool --prefix=ck_seat --mode=glib-server --output=ck-seat-glue.h org.freedesktop.ConsoleKit.Seat.xml
ck-session-glue.h: org.freedesktop.ConsoleKit.Session.xml Makefile.am
	dbus-binding-tool --prefix=ck_session --mode=glib-server --output=ck-session-glue.h org.freedesktop.ConsoleKit.Session.xml

ck-marshal.c: ck-marshal.list
	echo "#include \"ck-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=ck_marshal --body >> $@

ck-marshal.h: ck-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=ck_marshal --header > $@

console_kit_daemon_SOURCES =	\
	main.c			\
	ck-manager.h		\
	ck-manager.c		\
	ck-vt-monitor.h		\
	ck-vt-monitor.c		\
	ck-tty-idle-monitor.h	\
	ck-tty-idle-monitor.c	\
	ck-file-monitor.h	\
	ck-job.h		\
	ck-job.c		\
	ck-seat.h		\
	ck-seat.c		\
	ck-session-leader.h	\
	ck-session-leader.c	\
	ck-session.h		\
	ck-session.c		\
	ck-log.h		\
	ck-log.c		\
	ck-run-programs.c	\
	ck-run-programs.h	\
	ck-event-logger.c	\
	ck-event-logger.h	\
	$(BUILT_SOURCES)	\
	$(NULL)

if ENABLE_INOTIFY
FILE_MONITOR_BACKEND = ck-file-monitor-inotify.c
else
FILE_MONITOR_BACKEND = ck-file-monitor-dummy.c
endif

console_kit_daemon_SOURCES += $(FILE_MONITOR_BACKEND)

EXTRA_console_kit_daemon_SOURCES = 	\
	ck-file-monitor-inotify.c	\
	ck-file-monitor-dummy.c		\
	$(NULL)

console_kit_daemon_LDADD =	\
	$(CONSOLE_KIT_LIBS)	\
	$(POLKIT_LIBS)		\
	$(RBAC_LIBS)		\
	libck.la		\
	libck-event-log.la	\
	$(NULL)

noinst_PROGRAMS = 			\
	test-event-logger		\
	test-tty-idle-monitor		\
	test-vt-monitor			\
	$(NULL)

test_event_logger_SOURCES = 		\
	ck-event-logger.h		\
	ck-event-logger.c		\
	test-event-logger.c 		\
	$(NULL)

test_event_logger_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	$(POLKIT_LIBS)			\
	$(RBAC_LIBS)			\
	libck-event-log.la		\
	$(NULL)

test_vt_monitor_SOURCES = 		\
	ck-vt-monitor.h			\
	ck-vt-monitor.c			\
	test-vt-monitor.c 		\
	$(NULL)

test_vt_monitor_LDADD =			\
	$(CONSOLE_KIT_LIBS)		\
	$(POLKIT_LIBS)			\
	libck.la			\
	$(NULL)

test_tty_idle_monitor_SOURCES = 	\
	ck-tty-idle-monitor.h		\
	ck-tty-idle-monitor.c		\
	ck-file-monitor.h		\
	$(FILE_MONITOR_BACKEND)		\
	test-tty-idle-monitor.c 	\
	$(NULL)

test_tty_idle_monitor_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	$(POLKIT_LIBS)			\
	libck.la			\
	$(NULL)

EXTRA_DIST =			\
	ck-marshal.list		\
	$(dbusif_DATA)		\
	valgrind.sh		\
	test-open-session	\
	test-open-session-with-parameters	\
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

install-data-local:
	-mkdir -p $(DESTDIR)$(sysconfdir)/ConsoleKit/run-session.d
	-mkdir -p $(DESTDIR)$(prefix)/lib/ConsoleKit/run-session.d
	-mkdir -p $(DESTDIR)$(sysconfdir)/ConsoleKit/run-seat.d
	-mkdir -p $(DESTDIR)$(prefix)/lib/ConsoleKit/run-seat.d
	-mkdir -p $(DESTDIR)$(localstatedir)/run/ConsoleKit
	-mkdir -p $(DESTDIR)$(localstatedir)/log/ConsoleKit
