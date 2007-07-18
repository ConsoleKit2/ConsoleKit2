## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = \
	$(NULL)

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	-I$(top_srcdir)/src					\
	$(CONSOLE_KIT_CFLAGS)					\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\" 				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\" 				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	$(WARN_CFLAGS)						\
	$(DEBUG_CFLAGS)						\
	$(TOOLS_CFLAGS)						\
	$(NULL)

libexec_PROGRAMS = 			\
	ck-collect-session-info		\
	ck-get-x11-server-pid		\
	ck-get-x11-display-device	\
	$(NULL)

ck_collect_session_info_SOURCES =	\
	$(top_srcdir)/src/proc.h	\
	$(top_srcdir)/src/proc-linux.c	\
	ck-collect-session-info.c	\
	$(NULL)

ck_collect_session_info_LDADD =		\
	$(TOOLS_LIBS)			\
	$(NULL)

ck_get_x11_server_pid_SOURCES =		\
	ck-get-x11-server-pid.c		\
	$(NULL)

ck_get_x11_server_pid_LDADD =		\
	$(TOOLS_LIBS)			\
	$(NULL)

ck_get_x11_display_device_SOURCES =		\
	$(top_srcdir)/src/proc.h	\
	$(top_srcdir)/src/proc-linux.c	\
	ck-get-x11-display-device.c		\
	$(NULL)

ck_get_x11_display_device_LDADD =		\
	$(TOOLS_LIBS)			\
	$(NULL)

EXTRA_DIST =				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
