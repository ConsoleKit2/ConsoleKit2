## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = \
	$(NULL)

AM_CFLAGS =							\
	-I.							\
	-I$(srcdir)						\
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
	$(LIBX11_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\" 				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\" 				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DRUNDIR=\""$(RUNDIR)"\" 				\
	-DCONSOLE_KIT_PID_FILE=\""$(CONSOLE_KIT_PID_FILE)"\"	\
	-DLOCALEDIR=\"$(localedir)\"		\
	$(NULL)

CONSOLE_KIT_LIBS = \
	$(CK2_LIBS)			\
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

libck_la_LIBADD = $(CONSOLE_KIT_LIBS)

if CK_COMPILE_LINUX
libck_la_SOURCES +=		\
	ck-sysdeps-linux.c	\
	$(NULL)
libck_la_LIBADD += $(SELINUX_LIBS)
endif
if CK_COMPILE_SOLARIS
libck_la_SOURCES +=		\
	ck-sysdeps-solaris.c	\
	$(NULL)
libck_la_LIBADD += -lscf
endif
if CK_COMPILE_FREEBSD
libck_la_SOURCES +=		\
	ck-sysdeps-freebsd.c	\
	$(NULL)
libck_la_LIBADD += $(KVM_LIBS)
endif
if CK_COMPILE_DRAGONFLY
libck_la_SOURCES +=		\
	ck-sysdeps-dragonfly.c	\
	$(NULL)
libck_la_LIBADD += $(KVM_LIBS)
endif
if CK_COMPILE_OPENBSD
libck_la_SOURCES +=		\
	ck-sysdeps-openbsd.c	\
	$(NULL)
libck_la_LIBADD += $(KVM_LIBS)
endif
if CK_COMPILE_GNU
libck_la_SOURCES +=		\
	ck-sysdeps-gnu.c	\
	$(NULL)
libck_la_LIBADD += -lps
endif
if CK_COMPILE_NETBSD
libck_la_SOURCES +=		\
	ck-sysdeps-netbsd.c	\
	$(NULL)
libck_la_LIBADD += $(KVM_LIBS)
endif


EXTRA_libck_la_SOURCES =		\
	ck-sysdeps-linux.c		\
	ck-sysdeps-solaris.c		\
	ck-sysdeps-freebsd.c		\
	ck-sysdeps-netbsd.c		\
	ck-sysdeps-gnu.c		\
	$(NULL)

sbin_PROGRAMS = 		\
	console-kit-daemon	\
	$(NULL)

BUILT_SOURCES = 		\
	ck-manager-generated.h	\
	ck-manager-generated.c	\
	ck-seat-generated.h	\
	ck-seat-generated.c	\
	ck-session-generated.h	\
	ck-session-generated.c	\
	ck-marshal.c 		\
	ck-marshal.h		\
	$(NULL)

dbusifdir = $(datadir)/dbus-1/interfaces
dbusif_DATA = 					\
	org.freedesktop.ConsoleKit.Manager.xml	\
	org.freedesktop.ConsoleKit.Seat.xml	\
	org.freedesktop.ConsoleKit.Session.xml	\
	$(NULL)

ck-manager-generated.c ck-manager-generated.h : org.freedesktop.ConsoleKit.Manager.xml Makefile.am
	gdbus-codegen \
		--c-namespace=ConsoleKit \
		--interface-prefix=org.freedesktop.ConsoleKit. \
		--generate-c-code=ck-manager-generated \
		$(srcdir)/org.freedesktop.ConsoleKit.Manager.xml

ck-seat-generated.c ck-seat-generated.h : org.freedesktop.ConsoleKit.Seat.xml Makefile.am
	gdbus-codegen \
		--c-namespace=ConsoleKit \
		--interface-prefix=org.freedesktop.ConsoleKit. \
		--generate-c-code=ck-seat-generated \
		$(srcdir)/org.freedesktop.ConsoleKit.Seat.xml

ck-session-generated.c ck-session-generated.h : org.freedesktop.ConsoleKit.Session.xml Makefile.am
	gdbus-codegen \
		--c-namespace=ConsoleKit \
		--interface-prefix=org.freedesktop.ConsoleKit. \
		--generate-c-code=ck-session-generated \
		$(srcdir)/org.freedesktop.ConsoleKit.Session.xml

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
	ck-inhibit.c		\
	ck-inhibit.h		\
	ck-inhibit-manager.c	\
	ck-inhibit-manager.h	\
	ck-process-group.h	\
	ck-process-group.c	\
	ck-device.h 		\
	$(BUILT_SOURCES)	\
	$(NULL)

if ENABLE_INOTIFY
FILE_MONITOR_BACKEND = ck-file-monitor-inotify.c
else
FILE_MONITOR_BACKEND = ck-file-monitor-dummy.c
endif

console_kit_daemon_SOURCES += $(FILE_MONITOR_BACKEND)

if USE_UDEV_BACKEND
console_kit_daemon_SOURCES += \
	ck-device-udev.c \
	$(NULL)
else
console_kit_daemon_SOURCES += \
	ck-device-none.c \
	$(NULL)
endif

EXTRA_console_kit_daemon_SOURCES = 	\
	ck-file-monitor-inotify.c	\
	ck-file-monitor-dummy.c		\
	$(NULL)

console_kit_daemon_LDADD =	\
	$(CONSOLE_KIT_LIBS) \
	libck.la			\
	libck-event-log.la	\
	$(NULL)

if BUILD_TESTS

noinst_PROGRAMS = 			\
	test-event-logger		\
	test-tty-idle-monitor		\
	test-vt-monitor			\
	test-inhibit			\
	test-session			\
	test-seat				\
	test-manager			\
	ck-collect-session-info	\
	test-session-leader		\
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

test_inhibit_CFLAGS = \
	$(AM_CFLAGS)	\
	-URUNDIR	\
	-DRUNDIR=\"/tmp\" 			\
	$(NULL)

test_inhibit_SOURCES = 	\
	ck-marshal.h		\
	ck-marshal.c		\
	ck-inhibit.c		\
	ck-inhibit.h		\
	ck-inhibit-manager.c	\
	ck-inhibit-manager.h	\
	test-inhibit.c		\
	$(NULL)

test_inhibit_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	$(POLKIT_LIBS)			\
	libck.la			\
	$(NULL)

test_session_CFLAGS = \
	$(AM_CFLAGS)	\
	-URUNDIR	\
	-DRUNDIR=\"/tmp\" 			\
	$(NULL)

test_session_SOURCES = 	\
	$(BUILT_SOURCES)	\
	test-session.c		\
	$(NULL)

test_session_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	$(POLKIT_LIBS)			\
	libck.la			\
	$(NULL)

test_seat_CFLAGS = \
	$(AM_CFLAGS)	\
	-URUNDIR	\
	-DRUNDIR=\"/tmp\" 			\
	$(NULL)

test_seat_SOURCES = 	\
	$(BUILT_SOURCES)	\
	test-seat.c		\
	$(NULL)

test_seat_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	$(POLKIT_LIBS)			\
	libck.la			\
	$(NULL)

test_manager_CFLAGS = \
	$(AM_CFLAGS)	\
	-URUNDIR	\
	-DRUNDIR=\"/tmp\" 			\
	$(NULL)

test_manager_SOURCES = 	\
	$(BUILT_SOURCES)	\
	test-manager.c		\
	$(NULL)

test_manager_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	$(POLKIT_LIBS)			\
	libck.la			\
	$(NULL)

ck_collect_session_info_CFLAGS = \
	$(AM_CFLAGS)	\
	$(NULL)

ck_collect_session_info_SOURCES = 	\
	$(BUILT_SOURCES)	\
	mock-ck-collect-session-info.c		\
	$(NULL)

ck_collect_session_info_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	$(NULL)


test_session_leader_CFLAGS = \
	$(AM_CFLAGS)	\
	-ULIBEXECDIR	\
	-DLIBEXECDIR=\".\" 			\
	$(NULL)

test_session_leader_SOURCES = 	\
	$(BUILT_SOURCES)	\
	ck-job.h		\
	ck-job.c		\
	ck-session-leader.c		\
	ck-session-leader.h		\
	test-session-leader.c		\
	$(NULL)

test_session_leader_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
	libck.la			\
	$(NULL)

endif # BUILD_TESTS

EXTRA_DIST =			\
	ck-marshal.list		\
	$(dbusif_DATA)		\
	valgrind.sh		\
	$(NULL)

if BUILD_TESTS
EXTRA_DIST +=		\
	test-open-session	\
	test-open-session-with-parameters	\
	test-session	\
	test-seat		\
	test-session-leader	\
	test-manager	\
	ck-collect-session-info	\
	$(NULL)
endif # BUILD_TESTS

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

install-data-local:
	-mkdir -p $(DESTDIR)$(sysconfdir)/ConsoleKit/run-session.d
	-mkdir -p $(DESTDIR)$(libdir)/ConsoleKit/run-session.d
	-mkdir -p $(DESTDIR)$(sysconfdir)/ConsoleKit/run-seat.d
	-mkdir -p $(DESTDIR)$(libdir)/ConsoleKit/run-seat.d
	-mkdir -p $(DESTDIR)$(RUNDIR)/ConsoleKit
	-mkdir -p $(DESTDIR)$(localstatedir)/log/ConsoleKit
