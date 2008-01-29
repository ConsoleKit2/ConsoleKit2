NULL =

INCLUDES = 				\
	-I.				\
	$(LIBDBUS_CFLAGS)		\
	$(NULL)

lib_LTLIBRARIES = 			\
	libck-connector.la		\
	$(NULL)

libck_connectorincludedir = $(includedir)/ConsoleKit/ck-connector

libck_connectorinclude_HEADERS =	\
	ck-connector.h			\
	$(NULL)

libck_connector_la_SOURCES = 		\
	ck-connector.c			\
	ck-connector.h			\
	$(NULL)

noinst_PROGRAMS = 			\
	test-connector			\
	$(NULL)

test_connector_SOURCES = 		\
	test-connector.c 		\
	$(NULL)


test_connector_LDADD =			\
	libck-connector.la	\
	$(LIBDBUS_LIBS)			\
	$(NULL)

# soname management for libck-connector
LIBCKCON_LT_CURRENT=0
LIBCKCON_LT_REVISION=0
LIBCKCON_LT_AGE=0

libck_connector_la_LIBADD = 		\
	$(LIBDBUS_LIBS)			\
	$(NULL)

libck_connector_la_LDFLAGS = 		\
	-version-info $(LIBCKCON_LT_CURRENT):$(LIBCKCON_LT_REVISION):$(LIBCKCON_LT_AGE)	\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ck-connector.pc

EXTRA_DIST = 				\
	ck-connector.pc.in		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

clean-local:
	rm -f *~
