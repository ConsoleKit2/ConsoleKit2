NULL =

SPEC_XML_FILES =			\
	ConsoleKit.xml			\
	ck-introduction.xml		\
	ck-terms.xml			\
	ck-design.xml			\
	org.freedesktop.ConsoleKit.Manager.ref.xml	\
	org.freedesktop.ConsoleKit.Seat.ref.xml		\
	org.freedesktop.ConsoleKit.Session.ref.xml	\
	$(NULL)

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(docdir)/spec
htmldoc_DATA = ConsoleKit.html

ConsoleKit.html : $(SPEC_XML_FILES)
	$(XMLTO) $(XMLTO_FLAGS) xhtml-nochunks -m $(top_srcdir)/doc/dbus/config.xsl ConsoleKit.xml

endif # DOCBOOK_DOCS_ENABLED

org.freedesktop.ConsoleKit.Manager.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Manager.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.freedesktop.ConsoleKit.Seat.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Seat.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.freedesktop.ConsoleKit.Session.ref.xml : $(top_srcdir)/src/org.freedesktop.ConsoleKit.Session.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@

EXTRA_DIST =				\
	spec-to-docbook.xsl		\
	ConsoleKit.xml.in		\
	config.xsl			\
	docbook.css			\
	dbus-introspect-docs.dtd	\
	$(SPEC_XML_FILES)
	$(NULL)

MAINTAINERCLEANFILES =		\
	*~			\
	Makefile.in		\
	$(NULL)

clean-local:
	rm -f *~ *.ref.xml
	rm -f ConsoleKit.html

check-local:
	xmllint --noout --noent --valid ConsoleKit.xml
