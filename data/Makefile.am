NULL =

BUILT_SOURCES =

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

edit += \
	-e 's|@SystemdService[@]|SystemdService=console-kit-daemon.service|g'
else
edit += \
	-e 's|@SystemdService[@]||g'
endif


install-data-local: install-logrotate install-90-consolekit
	$(MKDIR_P) $(DESTDIR)$(localstatedir)/log/ConsoleKit/

uninstall-local:: uninstall-logrotate uninstall-90-consolekit
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(localstatedir)/log/ConsoleKit/

LOGROTATE_CONFS = consolekit.logrotate

XINITRC_CONFS = 90-consolekit

BUILT_SOURCES += \
	$(LOGROTATE_CONFS)	\
	$(XINITRC_CONFS)	\
	$(NULL)

consolekit.logrotate: consolekit.logrotate.in
	sed							\
	    -e 's![@]localstatedir[@]!$(localstatedir)!g'	\
	    < $< > $@-t
	mv $@-t $@

90-consolekit: 90-consolekit.in
	sed							\
	    -e 's![@]CK_LAUNCH_SESSION[@]!CK_LAUNCH_SESSION=$(bindir)/ck-launch-session!g'	\
	    < $< > $@-t
	mv $@-t $@

install-logrotate: $(LOGROTATE_CONFS)
	$(MKDIR_P) $(DESTDIR)$(localstatedir)/log/ConsoleKit/ \
		   $(DESTDIR)$(sysconfdir)/logrotate.d/
	$(INSTALL_DATA) consolekit.logrotate $(DESTDIR)$(sysconfdir)/logrotate.d/consolekit

install-90-consolekit: $(XINITRC_CONFS)
	$(MKDIR_P) $(DESTDIR)$(XINITRC_DIR)/
	$(INSTALL_SCRIPT) 90-consolekit $(DESTDIR)$(XINITRC_DIR)

uninstall-logrotate:
	rm -f $(DESTDIR)$(sysconfdir)/logrotate.d/consolekit

uninstall-90-consolekit:
	rm -f $(DESTDIR)$(XINITRC_DIR)/90-consolekit

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
	90-consolekit.in                                \
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
