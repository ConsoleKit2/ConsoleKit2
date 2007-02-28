
if ENABLE_PAM_MODULE

INCLUDES = $(LIBDBUS_CFLAGS) -I$(top_builddir)/libck-connector

pamlib_LTLIBRARIES=pam_ck_connector.la
pamlibdir=/lib/security
pam_ck_connector_la_SOURCES = pam-ck-connector.c
pam_ck_connector_la_LDFLAGS = -no-undefined -module -avoid-version
pam_ck_connector_la_LIBADD =  -lpam $(LIBDBUS_LIBS) $(top_builddir)/libck-connector/libck-connector.la

man_MANS = pam_ck_connector.8

endif

clean-local :
	rm -f *~
