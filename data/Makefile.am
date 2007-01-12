NULL =

dbusconfdir = $(DBUS_SYS_DIR)
dbusconf_DATA = ConsoleKit.conf

SCRIPT_IN_FILES = ConsoleKit.in
initddir=$(sysconfdir)/rc.d/init.d
initd_SCRIPTS= 		\
	ConsoleKit	\
	$(NULL)

EXTRA_DIST =						\
	$(dbusconf_DATA)				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
