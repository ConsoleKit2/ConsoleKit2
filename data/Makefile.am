NULL =

dbusconfdir = $(DBUS_SYS_DIR)
dbusconf_DATA = ConsoleKit.conf

seatdir = $(sysconfdir)/ConsoleKit/seats.d
seat_DATA = 00-primary.seat

if HAVE_POLKIT
polkit_policydir = $(datadir)/polkit-1/actions
dist_polkit_policy_DATA =	\
	org.freedesktop.consolekit.policy	\
	$(NULL)
endif

servicedir = $(datadir)/dbus-1/system-services
service_in_files = org.freedesktop.ConsoleKit.service.in
service_DATA = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	$(edit) $< >$@

edit = sed \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@CONSOLE_KIT_PID_FILE[@]|$(CONSOLE_KIT_PID_FILE)|g'

EXTRA_DIST =						\
	ck-log-system-restart				\
	ck-log-system-start				\
	ck-log-system-stop				\
	$(dbusconf_DATA)				\
	$(seat_DATA)					\
	$(service_in_files)				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

CLEANFILES = $(service_DATA)
