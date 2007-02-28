
INCLUDES = $(LIBDBUS_CFLAGS)

slib_LTLIBRARIES=libck-connector.la
slibdir=/lib
libck_connectorincludedir=$(includedir)/ConsoleKit/ck-connector
libck_connectorinclude_HEADERS = ck-connector.h
libck_connector_la_SOURCES = ck-connector.c ck-connector.h

# soname management for libck-connector
LIBCKCON_LT_CURRENT=0
LIBCKCON_LT_REVISION=0
LIBCKCON_LT_AGE=0

libck_connector_la_LIBADD = $(LIBDBUS_LIBS)
libck_connector_la_LDFLAGS = -version-info $(LIBCKCON_LT_CURRENT):$(LIBCKCON_LT_REVISION):$(LIBCKCON_LT_AGE)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ck-connector.pc

clean-local :
	rm -f *~
