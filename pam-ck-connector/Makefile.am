NULL =

if ENABLE_PAM_MODULE

INCLUDES = 					\
	$(LIBDBUS_CFLAGS)			\
	-I$(top_builddir)/libck-connector	\
	$(NULL)

pamlibdir = $(PAM_MODULE_DIR)
pamlib_LTLIBRARIES = 				\
	pam_ck_connector.la			\
	$(NULL)

pam_ck_connector_la_SOURCES = pam-ck-connector.c
pam_ck_connector_la_LDFLAGS = -no-undefined -module -avoid-version
pam_ck_connector_la_LIBADD =  			\
	$(PAM_LIBS)				\
	$(LIBDBUS_LIBS)				\
	$(top_builddir)/libck-connector/libck-connector.la	\
	$(NULL)

man_MANS = pam_ck_connector.8

if CK_COMPILE_LINUX
noinst_PROGRAMS = 				\
	test-pam				\
	$(NULL)

test_pam_SOURCES = 				\
	test-pam.c 				\
	$(NULL)

test_pam_LDADD =				\
	$(PAM_LIBS)				\
	-lpam_misc				\
	$(NULL)
endif

endif

MAINTAINERCLEANFILES =				\
	*~					\
	Makefile.in				\
	$(NULL)

EXTRA_DIST =                                    \
       pam_ck_connector.8                       \
       $(NULL)

clean-local :
	rm -f *~
