<?xml version="1.0"?>
<xsl:stylesheet
    version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:exsl="http://exslt.org/common"
    extension-element-prefixes="exsl">

<xsl:param name="case-style" select="'camel'"/>

<xsl:output method="text"/>

<xsl:include href="global-variables.xsl" />

<xsl:include href="common-templates.xsl" />

<xsl:strip-space elements="*"/>

<xsl:template match="enum/desc">
    <xsl:apply-templates/>
</xsl:template>

<xsl:template name="enumDescription">
    <xsl:param name="text" />
    <xsl:apply-templates select="desc"/>
</xsl:template>


<!-- - - - - - - - - - - - - - - - - - - - - -
  wildcard match, ignore everything which has no explicit match
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template match="*"/>


<!-- - - - - - - - - - - - - - - - - - - - - -
  application match
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template match="application">
    <xsl:apply-templates/>
</xsl:template>

<!-- - - - - - - - - - - - - - - - - - - - - -
  library match
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template match="library">
    <xsl:apply-templates/>
</xsl:template>

<!-- - - - - - - - - - - - - - - - - - - - - -
  root match
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template match="/idl">
<!-- Caution -->
  <xsl:text># Caution: auto-generated file. Don't edit.</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:text>&#x0A;</xsl:text>

<!-- Adding license -->
  <xsl:text>"""VBox REST API</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:text>Copyright (c) 2024-2025 Oracle and/or its affiliates.</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:text>Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl</xsl:text>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:text>SPDX-License-Identifier: UPL-1.0</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:text>"""</xsl:text><xsl:text>&#x0A;</xsl:text>

  <!-- Create imports -->
  <xsl:text>import logging</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:text>import inspect</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:text>from vbox_server.global_settings import *</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:text>&#x0A;</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:text># import enumerations</xsl:text><xsl:text>&#x0A;</xsl:text>

  <xsl:for-each select="//enum">
    <xsl:variable name="enumSplittedByLetters">
      <xsl:call-template name="splitWord">
        <xsl:with-param name="word" select="@name"/>
      </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="module">
      <xsl:call-template name="replaceUppercaseWithUnderscore">
        <xsl:with-param name="lettersNodeSet" select="$enumSplittedByLetters"/>
        <xsl:with-param name="letter" select="letter"/>
        <xsl:with-param name="fSkipFirst" select="false"/>
      </xsl:call-template>
    </xsl:variable>

    <xsl:value-of select="concat('from vbox_server.models.', $module, ' import ', @name)"/>
    <xsl:text>&#x0A;</xsl:text>
  </xsl:for-each>

  <xsl:text>&#x0A;</xsl:text>

  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="enum">
  <xsl:variable name="enumOriginalName" select="@name"/>

  <xsl:variable name="enumSplittedByLetters">
      <xsl:call-template name="splitWord">
          <xsl:with-param name="word" select="@name"/>
      </xsl:call-template>
  </xsl:variable>

  <xsl:variable name="enumName">
    <xsl:choose>
        <xsl:when test="$case-style='snake'">
          <xsl:call-template name="replaceUppercaseWithUnderscore">
              <xsl:with-param name="lettersNodeSet" select="$enumSplittedByLetters"/>
              <xsl:with-param name="letter" select="letter"/>
              <xsl:with-param name="fSkipFirst" select="false"/>
          </xsl:call-template>
        </xsl:when>
        <xsl:otherwise>
          <xsl:call-template name="stringToLower">
            <xsl:with-param name="str" select="@name"/>
          </xsl:call-template>
        </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:variable name="hashline">
    <xsl:text>###########################</xsl:text>
  </xsl:variable>

  <xsl:variable name="inVBoxVal">
    <xsl:text>inEnumVal</xsl:text>
  </xsl:variable>
  <xsl:variable name="outSwaggerVal">
    <xsl:text>out</xsl:text>
  </xsl:variable>

  <xsl:variable name="outVBoxVal" select="$outSwaggerVal"/>

  <xsl:variable name="inSwaggerVal" select="$inVBoxVal"/>

  <xsl:value-of select="concat($hashline, ' ', @name,': VirtualBox-Swagger vice versa conversion ', $hashline )"/>
  <xsl:text>&#x0A;</xsl:text>

  <!-- Conversion VirtualBox -> Swagger -->
  <xsl:value-of select="concat('def vbox_to_swagger_', $enumName, '(', $inVBoxVal, '):')"/>
  <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
  <xsl:value-of select="concat($outSwaggerVal, ' = ', @name, '()')"/>
  <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
  <xsl:value-of select="concat($outSwaggerVal, ' = ctx[', $apos, 'global', $apos, '].getEnumValueName(', $apos, @name, $apos, ', ', $inVBoxVal, ')' )"/>
  <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
  <xsl:value-of select="concat('return ', $outSwaggerVal, '.upper()' )"/>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:text>&#x0A;</xsl:text>

  <!-- Conversion Swagger -> VirtualBox -->
  <xsl:value-of select="concat('def swagger_to_vbox_', $enumName, '(', $inSwaggerVal, ': str):')"/>

  <xsl:for-each select="const">

    <xsl:variable name="upperCaseEnumName">
      <xsl:call-template name="stringToUpper">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
    </xsl:variable>

    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>

    <xsl:choose>
      <xsl:when test="position()=1">
        <xsl:value-of select="concat('if ', $inSwaggerVal, ' == ', $apos, $upperCaseEnumName, $apos, ':' )"/>
        <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>
        <xsl:value-of select="concat($outVBoxVal, ' = ctx[', $apos, 'const', $apos, '].', $enumOriginalName, '_', @name )"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="concat('elif ', $inSwaggerVal, ' == ', $apos, $upperCaseEnumName, $apos, ':' )"/>
        <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>
        <xsl:value-of select="concat($outVBoxVal, ' = ctx[', $apos, 'const', $apos, '].', $enumOriginalName, '_', @name )"/>
      </xsl:otherwise>
    </xsl:choose>

  </xsl:for-each>

  <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
  <xsl:text>else:</xsl:text>
  <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>
  <xsl:value-of select="concat($outVBoxVal, ' = None' )"/>

  <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
  <xsl:value-of select="concat('return ', $outVBoxVal)"/>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

</xsl:stylesheet>
