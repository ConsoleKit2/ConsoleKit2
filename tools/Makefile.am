## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = \
	$(NULL)

if CK_COMPILE_LINUX
SUBDIRS += linux
endif

DIST_SUBDIRS = 		\
	linux		\
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
	$(DBUS_CFLAGS)						\
	$(NULL)

bin_PROGRAMS = \
	ck-list-sessions		\
	$(NULL)

ck_list_sessions_SOURCES =		\
	list-sessions.c			\
	$(NULL)

ck_list_sessions_LDADD =		\
	$(CONSOLE_KIT_LIBS)		\
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
	$(top_builddir)/src/libckproc.la\
	$(NULL)

ck_get_x11_server_pid_SOURCES =		\
	ck-get-x11-server-pid.c		\
	$(NULL)

ck_get_x11_server_pid_LDADD =		\
	$(TOOLS_LIBS)			\
	$(top_builddir)/src/libckproc.la\
	$(NULL)

ck_get_x11_display_device_SOURCES =	\
	ck-get-x11-display-device.c	\
	$(NULL)

ck_get_x11_display_device_LDADD =	\
	$(TOOLS_LIBS)			\
	$(top_builddir)/src/libckproc.la\
	$(NULL)


EXTRA_DIST =				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
