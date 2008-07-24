NULL =

all : org.freedesktop.ConsoleKit.Manager.ref.xml org.freedesktop.ConsoleKit.Seat.ref.xml org.freedesktop.ConsoleKit.Session.ref.xml

org.freedesktop.ConsoleKit.Manager.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Manager.xml spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@
org.freedesktop.ConsoleKit.Seat.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Seat.xml spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@
org.freedesktop.ConsoleKit.Session.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Session.xml spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

EXTRA_DIST = spec-to-docbook.xsl dbus-introspect-docs.dtd

MAINTAINERCLEANFILES =		\
	*~			\
	Makefile.in		\
	$(NULL)

clean-local:
	rm -f *~
