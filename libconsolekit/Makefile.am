NULL =
CLEANFILES =

AM_CFLAGS =							\
	-I.							\
	$(GLIB_CFLAGS)							\
	$(GIO_CFLAGS)							\
	$(GIO_UNIX_CFLAGS)						\
	$(NULL)

lib_LTLIBRARIES = 			\
	libconsolekit.la		\
	$(NULL)

libconsolekitincludedir = $(includedir)/ConsoleKit/

libconsolekitinclude_HEADERS =	\
	libconsolekit.h			\
	sd-login.h			\
	$(NULL)

libconsolekit_la_SOURCES = 		\
	libconsolekit.c			\
	libconsolekit.h			\
	sd-compat.c			\
	sd-login.h			\
	$(NULL)


# soname management for liblibconsolekit
LIBCONSOLEKIT_LT_CURRENT=1
LIBCONSOLEKIT_LT_REVISION=0
LIBCONSOLEKIT_LT_AGE=0

libconsolekit_la_LIBADD = 		\
	$(GLIB_LIBS)		\
	$(GIO_LIBS)			\
	$(GIO_UNIX_LIBS)	\
	$(NULL)

libconsolekit_la_LDFLAGS = 		\
	-version-info $(LIBCONSOLEKIT_LT_CURRENT):$(LIBCONSOLEKIT_LT_REVISION):$(LIBCONSOLEKIT_LT_AGE)	\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libconsolekit.pc

CLEANFILES += libconsolekit.pc

if BUILD_TESTS

noinst_PROGRAMS = 			\
	test-libconsolekit		\
	$(NULL)

test_libconsolekit_SOURCES = 		\
	libconsolekit.h		\
	test-libconsolekit.c 		\
	$(NULL)

test_libconsolekit_LDADD =		\
	libconsolekit.la		\
	$(GLIB_LIBS)		\
	$(GIO_LIBS)			\
	$(GIO_UNIX_LIBS)	\
	$(NULL)

endif
#build tests

EXTRA_DIST = 				\
	libconsolekit.pc.in		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ENV = CC="$(CC)"
INTROSPECTION_SCANNER_ARGS = \
	--add-include-path=$(srcdir) \
	--warn-all \
	--identifier-prefix=Lib \
	--symbol-prefix=lib_ \
	--symbol-prefix=sd_
INTROSPECTION_COMPILER_ARGS = \
	--includedir=$(srcdir) \
	--includedir=.

if HAVE_INTROSPECTION
introspection_sources = $(libconsolekit_la_SOURCES)

libconsolekit-1.0.gir: libconsolekit.la Makefile
libconsolekit_1_0_gir_INCLUDES = GObject-2.0 GLib-2.0 Gio-2.0
libconsolekit_1_0_gir_CFLAGS = $(AM_CFLAGS)
libconsolekit_1_0_gir_LIBS = libconsolekit.la
libconsolekit_1_0_gir_FILES = $(introspection_sources)
libconsolekit_1_0_gir_EXPORT_PACKAGES = libconsolekit-1.0
INTROSPECTION_GIRS += libconsolekit-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

clean-local:
	rm -f *~
