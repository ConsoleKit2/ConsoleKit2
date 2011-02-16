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

if HAVE_SYSTEMD
systemdsystemunit_DATA = \
	console-kit-daemon.service \
	console-kit-log-system-start.service \
	console-kit-log-system-stop.service \
	console-kit-log-system-restart.service

console-kit-daemon.service: console-kit-daemon.service.in
	$(edit) $< >$@

console-kit-log-system-start.service: console-kit-log-system-start.service.in
	$(edit) $< >$@

console-kit-log-system-stop.service: console-kit-log-system-stop.service.in
	$(edit) $< >$@

console-kit-log-system-restart.service: console-kit-log-system-restart.service.in
	$(edit) $< >$@

install-data-hook:
	$(MKDIR_P) -m 755 \
		$(DESTDIR)$(systemdsystemunitdir)/basic.target.wants \
		$(DESTDIR)$(systemdsystemunitdir)/halt.target.wants \
		$(DESTDIR)$(systemdsystemunitdir)/poweroff.target.wants \
		$(DESTDIR)$(systemdsystemunitdir)/reboot.target.wants \
		$(DESTDIR)$(systemdsystemunitdir)/kexec.target.wants
	( cd $(DESTDIR)$(systemdsystemunitdir)/basic.target.wants && \
		rm -f console-kit-log-system-start.service && \
		$(LN_S) ../console-kit-log-system-start.service )
	( cd $(DESTDIR)$(systemdsystemunitdir)/halt.target.wants && \
		rm -f console-kit-log-system-stop.service && \
		$(LN_S) ../console-kit-log-system-stop.service )
	( cd $(DESTDIR)$(systemdsystemunitdir)/poweroff.target.wants && \
		rm -f console-kit-log-system-stop.service && \
		$(LN_S) ../console-kit-log-system-stop.service )
	( cd $(DESTDIR)$(systemdsystemunitdir)/reboot.target.wants && \
		rm -f console-kit-log-system-restart.service && \
		$(LN_S) ../console-kit-log-system-restart.service )
	( cd $(DESTDIR)$(systemdsystemunitdir)/kexec.target.wants && \
		rm -f console-kit-log-system-restart.service && \
		$(LN_S) ../console-kit-log-system-restart.service )
endif

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
	console-kit-daemon.service.in                   \
	console-kit-log-system-start.service.in         \
	console-kit-log-system-stop.service.in          \
	console-kit-log-system-restart.service.in       \
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

CLEANFILES =                                            \
	$(service_DATA)                                 \
	console-kit-daemon.service                      \
	console-kit-log-system-start.service        	\
	console-kit-log-system-stop.service     	\
	console-kit-log-system-restart.service
