## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = \
	$(NULL)

scriptdir = $(prefix)/lib/ConsoleKit/scripts
script_SCRIPTS =			\
	ck-system-stop			\
	ck-system-restart		\
	ck-system-suspend		\
	ck-system-hibernate		\
	ck-get-x11-display-device	\
	ck-get-x11-server-pid		\
	$(NULL)

EXTRA_DIST =				\
	$(script_SCRIPTS)		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in


check:
	for f in $(script_SCRIPTS); do \
            echo -n "Validate shell syntax in $$f : "; \
            sh -n $(srcdir)/$$f 2> sh.error;\
            if test -s sh.error; then \
                echo failed; \
                cat sh.error; \
                rm -f sh.error; \
                exit 1; \
            else \
                echo ok; \
                rm -f sh.error; \
            fi; \
        done;

