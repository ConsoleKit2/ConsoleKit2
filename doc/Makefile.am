NULL =

SUBDIRS = xml

SPEC_XML_FILES =			\
	ConsoleKit.xml			\
	xml/ck-introduction.xml		\
	xml/ck-terms.xml		\
	xml/ck-design.xml		\
	xml/org.freedesktop.ConsoleKit.Manager.ref.xml \
	xml/org.freedesktop.ConsoleKit.Seat.ref.xml \
	xml/org.freedesktop.ConsoleKit.Session.ref.xml \
	$(NULL)

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(docdir)/spec
htmldoc_DATA = ConsoleKit.html

ConsoleKit.html : $(SPEC_XML_FILES)
	$(XMLTO) xhtml-nochunks -m config.xsl ConsoleKit.xml

endif # DOCBOOK_DOCS_ENABLED

EXTRA_DIST =				\
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
	rm -f *~
	rm -f ConsoleKit.html
