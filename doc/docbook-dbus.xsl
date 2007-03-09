<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version="1.0">
<!--
     Convert D-Bus Glib xml into DocBook refentries
     Copyright (C) 2007 William Jon McCann
     License: GPL
-->
<xsl:output method="xml" indent="yes"/>

<xsl:template match="/">

<xsl:variable name="interface" select="//interface/@name"/>
<xsl:variable name="basename">
  <xsl:call-template name="interface-basename">
    <xsl:with-param name="str" select="$interface"/>
  </xsl:call-template>
</xsl:variable>

<refentry><xsl:attribute name="id"><xsl:value-of select="$basename"/></xsl:attribute>
  <refmeta>
    <refentrytitle role="top_of_page"><xsl:value-of select="//interface/@name"/></refentrytitle>
  </refmeta>

  <refnamediv>
    <refname><xsl:value-of select="//interface/@name"/></refname>
    <refpurpose><xsl:value-of select="$basename"/> interface</refpurpose>
  </refnamediv>

  <refsynopsisdiv role="synopsis">
    <title role="synopsis.title">Methods</title>
    <synopsis>
  <xsl:call-template name="methods-synopsis">
    <xsl:with-param name="basename" select="$basename"/>
  </xsl:call-template>
    </synopsis>
  </refsynopsisdiv>

  <refsect1 role="signal_proto">
    <title role="signal_proto.title">Signals</title>
    <synopsis>
  <xsl:call-template name="signals-synopsis">
    <xsl:with-param name="basename" select="$basename"/>
  </xsl:call-template>
    </synopsis>
  </refsect1>

  <refsect1 role="impl_interfaces">
    <title role="impl_interfaces.title">Implemented Interfaces</title>
    <para>
    <xsl:value-of select="$interface"/> implements
    org.freedesktop.DBus.Introspectable,
    org.freedesktop.DBus.Properties
    </para>
  </refsect1>


  <refsect1 role="properties">
    <title role="properties.title">Properties</title>
    <synopsis>
  <xsl:call-template name="properties-synopsis">
    <xsl:with-param name="basename" select="$basename"/>
  </xsl:call-template>
    </synopsis>
  </refsect1>

  <refsect1 role="desc">
    <title role="desc.title">Description</title>
    <para>
    </para>
  </refsect1>


  <refsect1 role="details">
    <title role="details.title">Details</title>
    <xsl:call-template name="method-details">
      <xsl:with-param name="basename" select="$basename"/>
    </xsl:call-template>
  </refsect1>

  <refsect1 role="signals">
    <title role="signals.title">Signal Details</title>
    <xsl:call-template name="signal-details">
      <xsl:with-param name="basename" select="$basename"/>
    </xsl:call-template>
  </refsect1>

  <refsect1 role="property_details">
    <title role="property_details.title">Property Details</title>
    <xsl:call-template name="property-details">
      <xsl:with-param name="basename" select="$basename"/>
    </xsl:call-template>
  </refsect1>

</refentry>
</xsl:template>


<xsl:template name="property-details">
  <xsl:param name="basename"/>
  <xsl:variable name="longest">
    <xsl:call-template name="find-longest">
      <xsl:with-param name="set" select="@name"/>
    </xsl:call-template>
  </xsl:variable>
  <xsl:for-each select="///property">
  <refsect2>
    <title><anchor role="function"><xsl:attribute name="id"><xsl:value-of select="$basename"/>-property-<xsl:value-of select="@name"/></xsl:attribute></anchor>'<xsl:value-of select="@name"/>'</title>
<indexterm><primary><xsl:value-of select="@name"/></primary><secondary><xsl:value-of select="$basename"/></secondary><tertiary>property</tertiary></indexterm>
<programlisting>'<xsl:value-of select="@name"/>'<xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="2"/></xsl:call-template>
<xsl:call-template name="property-args"><xsl:with-param name="indent" select="string-length(@name) + 2"/></xsl:call-template></programlisting>
  </refsect2>
  </xsl:for-each>
</xsl:template>


<xsl:template name="signal-details">
  <xsl:param name="basename"/>
  <xsl:variable name="longest">
    <xsl:call-template name="find-longest">
      <xsl:with-param name="set" select="@name"/>
    </xsl:call-template>
  </xsl:variable>
  <xsl:for-each select="///signal">
  <refsect2>
    <title><anchor role="function"><xsl:attribute name="id"><xsl:value-of select="$basename"/>-signal-<xsl:value-of select="@name"/></xsl:attribute></anchor><xsl:value-of select="@name"/> ()</title>
<indexterm><primary><xsl:value-of select="@name"/></primary><secondary><xsl:value-of select="$basename"/></secondary><tertiary>signal</tertiary></indexterm>
<programlisting><xsl:value-of select="@name"/> (<xsl:call-template name="signal-args"><xsl:with-param name="indent" select="string-length(@name) + 2"/><xsl:with-param name="prefix" select="."/></xsl:call-template>)</programlisting>
  </refsect2>
  </xsl:for-each>
</xsl:template>


<xsl:template name="method-details">
  <xsl:param name="basename"/>
  <xsl:variable name="longest">
    <xsl:call-template name="find-longest">
      <xsl:with-param name="set" select="@name"/>
    </xsl:call-template>
  </xsl:variable>
  <xsl:for-each select="///method">
  <refsect2>
    <title><anchor role="function"><xsl:attribute name="id"><xsl:value-of select="$basename"/>-<xsl:value-of select="@name"/></xsl:attribute></anchor><xsl:value-of select="@name"/> ()</title>
<indexterm><primary><xsl:value-of select="@name"/></primary><secondary><xsl:value-of select="$basename"/></secondary><tertiary>method</tertiary></indexterm>
<programlisting><xsl:value-of select="@name"/> (<xsl:call-template name="method-args"><xsl:with-param name="indent" select="string-length(@name) + 2"/><xsl:with-param name="prefix" select="."/></xsl:call-template>)</programlisting>
  </refsect2>
  </xsl:for-each>
</xsl:template>


<xsl:template name="properties-synopsis">
  <xsl:param name="basename"/>
  <xsl:variable name="longest">
    <xsl:call-template name="find-longest">
      <xsl:with-param name="set" select="///property/@name"/>
    </xsl:call-template>
  </xsl:variable>
  <xsl:for-each select="///property">
<link><xsl:attribute name="linkend"><xsl:value-of select="$basename"/>-property-<xsl:value-of select="@name"/></xsl:attribute>'<xsl:value-of select="@name"/>'</link><xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="$longest - string-length(@name) + 1"/></xsl:call-template> <xsl:call-template name="property-args"><xsl:with-param name="indent" select="$longest + 2"/></xsl:call-template>
</xsl:for-each>
</xsl:template>


<xsl:template name="signals-synopsis">
  <xsl:param name="basename"/>
  <xsl:variable name="longest">
    <xsl:call-template name="find-longest">
      <xsl:with-param name="set" select="///signal/@name"/>
    </xsl:call-template>
  </xsl:variable>
  <xsl:for-each select="///signal">
<link><xsl:attribute name="linkend"><xsl:value-of select="$basename"/>-signal-<xsl:value-of select="@name"/></xsl:attribute><xsl:value-of select="@name"/></link><xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="$longest - string-length(@name) + 1"/></xsl:call-template>(<xsl:call-template name="signal-args"><xsl:with-param name="indent" select="$longest + 2"/><xsl:with-param name="prefix" select="///signal"/></xsl:call-template>)
</xsl:for-each>
</xsl:template>


<xsl:template name="methods-synopsis">
  <xsl:param name="basename"/>
  <xsl:variable name="longest">
    <xsl:call-template name="find-longest">
      <xsl:with-param name="set" select="///method/@name"/>
    </xsl:call-template>
  </xsl:variable>
  <xsl:for-each select="///method">
<link><xsl:attribute name="linkend"><xsl:value-of select="$basename"/>-<xsl:value-of select="@name"/></xsl:attribute><xsl:value-of select="@name"/></link><xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="$longest - string-length(@name) + 1"/></xsl:call-template>(<xsl:call-template name="method-args"><xsl:with-param name="indent" select="$longest + 2"/><xsl:with-param name="prefix" select="///method"/></xsl:call-template>)
</xsl:for-each>
</xsl:template>


<xsl:template name="method-args"><xsl:param name="indent"/><xsl:param name="prefix"/><xsl:variable name="longest"><xsl:call-template name="find-longest"><xsl:with-param name="set" select="$prefix/arg/@type"/></xsl:call-template></xsl:variable><xsl:for-each select="arg"><xsl:value-of select="@direction"/>
<xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="4 - string-length(@direction)"/></xsl:call-template>'<xsl:value-of select="@type"/>'<xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="$longest - string-length(@type) + 1"/></xsl:call-template>
<xsl:value-of select="@name"/><xsl:if test="not(position() = last())">,
<xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="$indent"/></xsl:call-template></xsl:if>
</xsl:for-each>
</xsl:template>


<xsl:template name="signal-args"><xsl:param name="indent"/><xsl:param name="prefix"/><xsl:variable name="longest"><xsl:call-template name="find-longest"><xsl:with-param name="set" select="$prefix/arg/@type"/></xsl:call-template></xsl:variable><xsl:for-each select="arg">'<xsl:value-of select="@type"/>'<xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="$longest - string-length(@type) + 1"/></xsl:call-template>
<xsl:value-of select="@name"/><xsl:if test="not(position() = last())">,
<xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="$indent"/></xsl:call-template></xsl:if>
</xsl:for-each>
</xsl:template>


<xsl:template name="property-args"><xsl:param name="indent"/>
<xsl:value-of select="@access"/><xsl:call-template name="pad-spaces"><xsl:with-param name="width" select="9 - string-length(@access) + 1"/></xsl:call-template>'<xsl:value-of select="@type"/>'
</xsl:template>


<xsl:template name="pad-spaces">
  <xsl:param name="width"/>
  <xsl:variable name="spaces" xml:space="preserve">                                                                        </xsl:variable>
  <xsl:value-of select="substring($spaces,1,$width)"/>
</xsl:template>


<xsl:template name="find-longest">
  <xsl:param name="set"/>
  <xsl:param name="index" select="1"/>
  <xsl:param name="longest" select="0"/>

  <xsl:choose>
    <xsl:when test="$index > count($set)">
      <!--finished looking-->
      <xsl:value-of select="$longest"/>
    </xsl:when>
    <xsl:when test="string-length($set[$index])>$longest">
      <!--found new longest-->
      <xsl:call-template name="find-longest">
        <xsl:with-param name="set" select="$set"/>
        <xsl:with-param name="index" select="$index + 1"/>
        <xsl:with-param name="longest" select="string-length($set[$index])"/>
      </xsl:call-template>
    </xsl:when>
    <xsl:otherwise>
      <!--this isn't any longer-->
      <xsl:call-template name="find-longest">
        <xsl:with-param name="set" select="$set"/>
        <xsl:with-param name="index" select="$index + 1"/>
        <xsl:with-param name="longest" select="$longest"/>
      </xsl:call-template>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>


<xsl:template name="interface-basename">
  <xsl:param name="str"/>
  <xsl:choose>
    <xsl:when test="contains($str,'.')">
      <xsl:call-template name="interface-basename">
	<xsl:with-param name="str" select="substring-after($str,'.')"/>
      </xsl:call-template>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$str"/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

</xsl:stylesheet>
