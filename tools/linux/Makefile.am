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


EXTRA_DIST =				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
