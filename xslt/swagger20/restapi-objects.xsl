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

<xsl:template match="*"/>

<xsl:template name="attribute">

  <xsl:variable name="readonly" select="@readonly"/>

  <xsl:variable name="typeFormat">
    <xsl:choose>
      <xsl:when test="@http-type!=''">
        <xsl:value-of select="@http-type"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="@type"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:variable name="swaggerFormat" select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$typeFormat]/@format"/>

  <xsl:value-of select="$sixSpaces"/>
  <xsl:value-of select="@name"/>
  <xsl:text>:&#x0A;</xsl:text>

  <xsl:choose>
    <xsl:when test="key('G_keyEnumsByName', $typeFormat) or starts-with($typeFormat, 'I')">

      <xsl:value-of select="$eightSpaces"/>

      <xsl:if test="@safearray='yes'">
        <xsl:text>type: array&#x0A;</xsl:text>
        <xsl:value-of select="$eightSpaces"/>
        <xsl:text>items:&#x0A;</xsl:text>
        <xsl:value-of select="$tenSpaces"/>
      </xsl:if>

      <xsl:choose>
        <xsl:when test="key('G_keyEnumsByName', $typeFormat)">
          <xsl:value-of select="concat('$ref: ', $aposDouble, '#/definitions/', $typeFormat, $aposDouble)"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="concat('$ref: ', $aposDouble, '#/definitions/', substring($typeFormat,2), $aposDouble)"/>
        </xsl:otherwise>
      </xsl:choose>

    </xsl:when>

    <xsl:when test="@type='octet'">
      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>type: </xsl:text>
      <xsl:value-of select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$typeFormat]/@type"/>
      <xsl:text>&#x0A;</xsl:text>
      <xsl:variable name="swaggerFormat" select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$typeFormat]/@format"/>
      <xsl:if test="$swaggerFormat='byte'">
        <xsl:value-of select="$eightSpaces"/>
        <xsl:text>format: </xsl:text>
        <xsl:value-of select="$swaggerFormat"/>
        <xsl:text>&#x0A;</xsl:text>
      </xsl:if>
    </xsl:when>

    <xsl:otherwise>
      <xsl:choose>
          <xsl:when test="@safearray='yes' and @type!='octet'">
            <xsl:value-of select="$eightSpaces"/>
            <xsl:text>type: array&#x0A;</xsl:text>
            <xsl:value-of select="$eightSpaces"/>
            <xsl:text>items:&#x0A;</xsl:text>
            <xsl:value-of select="$tenSpaces"/>
             <xsl:text>type: </xsl:text>
          </xsl:when>
          <xsl:otherwise>
              <xsl:value-of select="$eightSpaces"/>
              <xsl:text>type: </xsl:text>
          </xsl:otherwise>
      </xsl:choose>
      <xsl:value-of select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$typeFormat]/@type"/>
      <xsl:if test="$swaggerFormat">
        <xsl:text>&#x0A;</xsl:text>

        <xsl:choose>
          <xsl:when test="@safearray='yes' and @type!='octet'">
            <xsl:value-of select="$tenSpaces"/>
            <xsl:text>format: </xsl:text>
            <xsl:value-of select="$swaggerFormat"/>
          </xsl:when>
          <xsl:otherwise>
            <xsl:value-of select="$eightSpaces"/>
            <xsl:text>format: </xsl:text>
            <xsl:value-of select="$swaggerFormat"/>
          </xsl:otherwise>
        </xsl:choose>

      </xsl:if>
    </xsl:otherwise>
  </xsl:choose>

  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template match="interface/desc">
    <xsl:apply-templates/>
</xsl:template>

<xsl:template name="interfaceDescription">
    <xsl:param name="text" />
    <xsl:apply-templates select="desc"/>
</xsl:template>

<xsl:template match="application">
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="library">
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="/idl">
  <xsl:text>#==========[ Objects ]===================================================&#x0A;</xsl:text>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="interface[@rest='managed']">
    <xsl:variable name="defaultDescriptionText">
      <xsl:if test="desc and desc!=''">
        <xsl:call-template name="interfaceDescription">
          <xsl:with-param name="text" select="desc" />
        </xsl:call-template>
      </xsl:if>
    </xsl:variable>

    <xsl:text>  </xsl:text>
    <xsl:value-of select="substring(@name,2)"/>
    <xsl:text>:&#x0A;</xsl:text>

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

    <xsl:text>    type: object</xsl:text>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:if test="count(attribute)!=0">
      <xsl:text>    properties:</xsl:text>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:for-each select="attribute[not(@rest='suppress')]">
        <xsl:variable name="type" select="@type"/>
        <xsl:variable name="simpleIDLType" select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@idlname"/>

        <xsl:choose>
          <xsl:when test="@type=$simpleIDLType">
            <xsl:call-template name="attribute"/>
          </xsl:when>
          <xsl:when test="key('G_keyInterfacesByRESTSupport', @type)">
             <xsl:call-template name="attribute"/>
          </xsl:when>
          <xsl:when test="key('G_keyEnumsByName', @type)">
             <xsl:call-template name="attribute"/>
          </xsl:when>
        </xsl:choose>

     </xsl:for-each>
    </xsl:if>

    <xsl:text>&#x0A;</xsl:text>

</xsl:template>

</xsl:stylesheet>
