## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = \
	$(NULL)

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	$(CONSOLE_KIT_CFLAGS)					\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\" 				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\" 				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DCONSOLE_KIT_PID_FILE=\""$(CONSOLE_KIT_PID_FILE)"\"	\
	$(WARN_CFLAGS)						\
	$(DEBUG_CFLAGS)						\
	$(DBUS_CFLAGS)						\
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

ck-manager-glue.h: ck-manager.xml Makefile.am
	dbus-binding-tool --prefix=ck_manager --mode=glib-server --output=ck-manager-glue.h ck-manager.xml
ck-seat-glue.h: ck-seat.xml Makefile.am
	dbus-binding-tool --prefix=ck_seat --mode=glib-server --output=ck-seat-glue.h ck-seat.xml
ck-session-glue.h: ck-session.xml Makefile.am
	dbus-binding-tool --prefix=ck_session --mode=glib-server --output=ck-session-glue.h ck-session.xml

ck-marshal.c: ck-marshal.list
	echo "#include \"ck-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=ck_marshal --body >> $@

ck-marshal.h: ck-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=ck_marshal --header > $@

PLATFORM_SOURCES =		\
	proc-linux.c		\
	$(NULL)

console_kit_daemon_SOURCES =	\
	main.c			\
	ck-manager.h		\
	ck-manager.c		\
	ck-vt-monitor.h		\
	ck-vt-monitor.c		\
	ck-job.h		\
	ck-job.c		\
	ck-seat.h		\
	ck-seat.c		\
	ck-session.h		\
	ck-session.c		\
	ck-log.h		\
	ck-log.c		\
	getfd.c			\
	proc.h			\
	$(PLATFORM_SOURCES)	\
	$(BUILT_SOURCES)	\
	$(NULL)

console_kit_daemon_LDADD =	\
	$(CONSOLE_KIT_LIBS)	\
	$(NULL)

EXTRA_DIST =			\
	ck-marshal.list		\
	ck-manager.xml		\
	ck-seat.xml		\
	ck-session.xml		\
	valgrind.sh		\
	test-open-session	\
	test-open-session-with-parameters	\
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
