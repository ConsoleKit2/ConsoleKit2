NULL =

SPEC_XML_FILES =			\
	ConsoleKit.xml			\
	xml/ck-introduction.xml		\
	xml/ref-ck-manager.xml		\
	xml/ref-ck-seat.xml		\
	xml/ref-ck-session.xml		\
	$(NULL)

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(DOCDIR)/spec
htmldoc_DATA = ConsoleKit.html

ConsoleKit.html : $(SPEC_XML_FILES)
	$(XMLTO) xhtml-nochunks -m config.xsl ConsoleKit.xml

endif # DOCBOOK_DOCS_ENABLED

EXTRA_DIST =				\
	ConsoleKit.html			\
	ConsoleKit.xml.in		\
	config.xsl			\
	docbook.css			\
	$(SPEC_XML_FILES)
	$(NULL)


MAINTAINERCLEANFILES =		\
	*~			\
	Makefile.in		\
	$(NULL)

clean-local:
	rm -f *~
	rm -f ConsoleKit.html
