NULL =

all : org.freedesktop.ConsoleKit.Manager.ref.xml org.freedesktop.ConsoleKit.Seat.ref.xml org.freedesktop.ConsoleKit.Session.ref.xml

org.freedesktop.ConsoleKit.Manager.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Manager.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/xml/spec-to-docbook.xsl $< | tail -n +2 >> $@
org.freedesktop.ConsoleKit.Seat.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Seat.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/xml/spec-to-docbook.xsl $< | tail -n +2 >> $@
org.freedesktop.ConsoleKit.Session.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Session.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/xml/spec-to-docbook.xsl $< | tail -n +2 >> $@

EXTRA_DIST = spec-to-docbook.xsl

MAINTAINERCLEANFILES =		\
	*~			\
	Makefile.in		\
	$(NULL)

clean-local:
	rm -f *~ *.ref.xml
