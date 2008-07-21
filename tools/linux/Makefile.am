## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = \
	$(NULL)

scriptdir = $(prefix)/lib/ConsoleKit/scripts
script_SCRIPTS =			\
	ck-system-stop	\
	ck-system-restart		\
	$(NULL)

EXTRA_DIST =				\
	$(script_SCRIPTS)		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in


check:
	for f in $(script_SCRIPTS); do \
            echo -n "Validate bash syntax in $$f : "; \
            bash -n $(srcdir)/$$f 2> bash.error;\
            if test -s bash.error; then \
                echo failed; \
                cat bash.error; \
                rm -f bash.error; \
                exit 1; \
            else \
                echo ok; \
                rm -f bash.error; \
            fi; \
        done;

