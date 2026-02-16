<?xml version="1.0"?>
<xsl:stylesheet
    version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:exsl="http://exslt.org/common"
    extension-element-prefixes="exsl">

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

<xsl:template match="*"/>

<xsl:template match="application">
    <xsl:apply-templates/>
</xsl:template>

<xsl:template match="library">
    <xsl:apply-templates/>
</xsl:template>

<xsl:template match="/idl">
  <xsl:text>#==========[ Enumerations ]===================================</xsl:text>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="enum">
    <xsl:variable name="defaultDescriptionText">
      <xsl:if test="desc and desc!=''">
        <xsl:call-template name="enumDescription">
          <xsl:with-param name="text" select="desc" />
        </xsl:call-template>
      </xsl:if>
    </xsl:variable>

  <xsl:text>  </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>: &amp;</xsl:text>

  <xsl:call-template name="stringToUpper">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>

  <xsl:text>&#x0A;</xsl:text>

  <xsl:text>    description : </xsl:text>
  <xsl:choose>
    <xsl:when test="desc and desc!=''">
      <xsl:value-of select="$aposDouble"/>

      <xsl:call-template name="stringReplaceAll">
        <xsl:with-param name="text" select="$defaultDescriptionText" />
        <xsl:with-param name="replace" select="$aposDouble" />
        <xsl:with-param name="by" select="$apos" />
      </xsl:call-template>

      <xsl:value-of select="$aposDouble"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:text> "There is no description"</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:text>&#x0A;</xsl:text>

  <xsl:text>    type: string</xsl:text>
  <xsl:text>&#x0A;</xsl:text>

  <xsl:text>    enum:</xsl:text>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:for-each select="const">
    <xsl:text>     - </xsl:text>

    <xsl:choose>
        <xsl:when test="@name!='Null'">
            <xsl:variable name="enum_value" select="@name"/>
            <xsl:value-of select="$aposDouble"/>
            <!--  Uppercase conversion template approach-->
            <xsl:call-template name="stringToUpper">
              <xsl:with-param name="str" select="$enum_value"/>
            </xsl:call-template>
            <xsl:value-of select="$aposDouble"/>
        </xsl:when>
        <xsl:otherwise>
            <xsl:text>"null"</xsl:text>
        </xsl:otherwise>
    </xsl:choose>

    <xsl:text>&#x0A;</xsl:text>
  </xsl:for-each>

  <xsl:text>    x-vbox-type: enum</xsl:text>
  <xsl:text>&#x0A;</xsl:text>

  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

</xsl:stylesheet>
