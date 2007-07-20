NULL =

dbusconfdir = $(DBUS_SYS_DIR)
dbusconf_DATA = ConsoleKit.conf

seatdir = $(sysconfdir)/ConsoleKit/seats.d
seat_DATA = 00-primary.seat

SCRIPT_IN_FILES = ConsoleKit.in
initddir=$(sysconfdir)/rc.d/init.d
initd_SCRIPTS= 		\
	ConsoleKit	\
	$(NULL)


ConsoleKit: ConsoleKit.in Makefile
	$(edit) $< >$@

edit = sed \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@CONSOLE_KIT_PID_FILE[@]|$(CONSOLE_KIT_PID_FILE)|g'

EXTRA_DIST =						\
	$(dbusconf_DATA)				\
	$(seat_DATA)					\
	$(SCRIPT_IN_FILES)				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

CLEANFILES = ConsoleKit
