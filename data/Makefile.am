NULL =

BUILT_SOURCES =

dbusconfdir = $(DBUS_SYS_DIR)
dbusconf_DATA = ConsoleKit.conf

seatdir = $(sysconfdir)/ConsoleKit/seats.d
seat_DATA = 00-primary.seat

xinitrcdir = $(XINITRC_DIR)

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

install-xinitrc:
	$(INSTALL_PROGRAM) 90-consolekit $(xinitrcdir) && \
	chmod +x $(xinitrcdir)/90-consolekit

install-data-local: install-logrotate
	$(MKDIR_P) $(DESTDIR)$(localstatedir)/log/ConsoleKit/

uninstall-local:: uninstall-logrotate
	rmdir $(DESTDIR)$(localstatedir)/log/ConsoleKit/

LOGROTATE_CONFS = consolekit.logrotate

BUILT_SOURCES += $(LOGROTATE_CONFS)

consolekit.logrotate: consolekit.logrotate.in
	sed							\
	    -e 's![@]localstatedir[@]!$(localstatedir)!g'	\
	    < $< > $@-t
	mv $@-t $@

install-logrotate: $(LOGROTATE_CONFS)
	$(MKDIR_P) $(DESTDIR)$(localstatedir)/log/ConsoleKit/ \
		   $(DESTDIR)$(sysconfdir)/logrotate.d/
	$(INSTALL_DATA) consolekit.logrotate $(DESTDIR)$(sysconfdir)/logrotate.d/consolekit


uninstall-logrotate:
	rm -f $(DESTDIR)$(sysconfdir)/logrotate.d/consolekit

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
	90-consolekit                                   \
	consolekit.logrotate.in                         \
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

CLEANFILES =                                            \
	$(service_DATA)                                 \
	$(BUILT_SOURCES)                                \
	console-kit-daemon.service                      \
	console-kit-log-system-start.service        	\
	console-kit-log-system-stop.service     	\
	console-kit-log-system-restart.service
