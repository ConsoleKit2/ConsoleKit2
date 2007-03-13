<?xml version='1.0'?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:doc="http://www.freedesktop.org/dbus/1.0/doc.dtd"
  exclude-result-prefixes="doc">

  <xsl:output method="xml" indent="yes" encoding="UTF-8"
    omit-xml-declaration="no"
    doctype-system="http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd"
    doctype-public="-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" />

  <xsl:template match="*">
    <xsl:copy>
      <xsl:for-each select="@*">
        <xsl:if test="not(starts-with(name(.), 'doc:'))">
          <xsl:copy/>
        </xsl:if>
      </xsl:for-each>
      <xsl:apply-templates/>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="node">
    <node>
      <xsl:for-each select="@*">
        <xsl:if test="not(starts-with(name(.), 'xmlns'))">
          <xsl:copy/>
        </xsl:if>
      </xsl:for-each>
      <xsl:apply-templates/>
    </node>
  </xsl:template>

  <xsl:template match="doc:*"/>
  <xsl:template match="text()"/>

</xsl:stylesheet>
