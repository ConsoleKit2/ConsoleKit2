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

EXTRA_DIST =				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
