<?xml version="1.0"?>
<xsl:stylesheet
    version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:exsl="http://exslt.org/common"
    xmlns:str="http://exslt.org/strings"
    extension-element-prefixes="exsl str"
    >

<xsl:output method="text"/>

<xsl:include href="global-variables.xsl" />

<xsl:include href="common-templates.xsl" />

<xsl:strip-space elements="*"/>

<xsl:template match="*"/>

<xsl:template match="/idl">
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="library">
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="application">
  <xsl:text>#==========[ Request body definitions ]====================================&#x0A;</xsl:text>

  <xsl:for-each select="exsl:node-set($G_endpointsNodeSet)/endpoint">
      <xsl:sort select="@uri" order="ascending" lang="en-US"/>
      <xsl:call-template name="endpoint"/>
  </xsl:for-each>
</xsl:template>

<xsl:template name="endpoint">
    <xsl:variable name="interfaceName">
        <xsl:value-of select="@interfaceName"/>
    </xsl:variable>

    <xsl:variable name="restpath" select="@uri"/>
    <xsl:variable name="httpRequest" select="@httpRequest"/>

    <xsl:if test="count(param[@dir='in'])>1 and ($httpRequest='post' or $httpRequest='put' or $httpRequest='patch')">
        <xsl:variable name="defaultOperationId">
          <xsl:value-of select="substring(@interfaceName, 2)"/>
          <xsl:text>_</xsl:text>
          <xsl:value-of select="@methodName"/>
        </xsl:variable>

        <xsl:variable name="numberOfInputParams">
          <xsl:value-of select="count(param[@dir='in'])"/>
        </xsl:variable>

        <xsl:variable name="operationIdSplittedByLetters">
            <xsl:call-template name="splitWord">
                <xsl:with-param name="word" select="$defaultOperationId"/>
            </xsl:call-template>
        </xsl:variable>

        <xsl:variable name="requestBodyPrefix">
            <xsl:call-template name="replaceUnderscoreWithUppercase">
                <xsl:with-param name="lettersNodeSet" select="$operationIdSplittedByLetters"/>
                <xsl:with-param name="letter" select="letter"/>
            </xsl:call-template>
        </xsl:variable>

<!-- !!!!!!!!!!!!!!!!!!!!! OUTPUT STARTING HERE !!!!!!!!!!!!!!!!!!!!!!! -->
        <xsl:value-of select="$twoSpaces"/>
        <xsl:value-of select="concat($requestBodyPrefix, 'RequestBody:')"/>
        <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
        <xsl:text>type: object</xsl:text>
        <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
        <xsl:text>properties:</xsl:text>
        <xsl:text>&#x0A;</xsl:text>

        <xsl:for-each select="param[@dir='in']">
            <xsl:sort select="@dir"/>

            <xsl:variable name="type" select="@type"/>

            <xsl:variable name="fInterface">
              <xsl:value-of select="exsl:node-set($G_interfacesNames)/interface[@name=$type]/@name"/>
            </xsl:variable>

            <xsl:variable name="fEnumeration">
              <xsl:value-of select="exsl:node-set($G_enumerationsNames)/enumeration[@name=$type]/@name"/>
            </xsl:variable>

            <xsl:variable name="typeFinal">
                <xsl:choose>
                    <xsl:when test="$fInterface!='' ">
                        <xsl:text>uuid</xsl:text>
                    </xsl:when>
                    <xsl:otherwise>
                        <xsl:value-of select="$type"/>
                    </xsl:otherwise>
                </xsl:choose>
            </xsl:variable>

            <xsl:value-of select="$sixSpaces"/>

            <!-- check replacement -->
            <xsl:variable name="curName" select="@name"/>
            <xsl:variable name="replacement" select="exsl:node-set($G_aSwaggerReservedWords)/word[@reserved=$curName]/@replacement"/>
            <xsl:choose>
                <xsl:when test="$replacement and $replacement!=''">
                <xsl:value-of select="$replacement"/>
                </xsl:when>
                <xsl:otherwise>
                <xsl:value-of select="@name"/>
                </xsl:otherwise>
            </xsl:choose>
            <xsl:text>:&#x0A;</xsl:text>

            <xsl:value-of select="$eightSpaces"/>
            <xsl:choose>
                <xsl:when test="$fEnumeration!='' ">
                    <xsl:if test="@safearray='yes'">
                        <xsl:text>type: array</xsl:text>
                        <xsl:text>&#x0A;</xsl:text>

                        <xsl:value-of select="$eightSpaces"/>
                        <xsl:text>items:</xsl:text>
                        <xsl:text>&#x0A;</xsl:text>
                        <xsl:value-of select="$tenSpaces"/>
                    </xsl:if>

                    <xsl:value-of select="concat('$ref: ', $aposDouble, '#/definitions/', $typeFinal, $aposDouble)"/>
                </xsl:when>

                <xsl:otherwise>
                    <xsl:if test="@safearray='yes'">
                        <xsl:text>type: array</xsl:text>
                        <xsl:text>&#x0A;</xsl:text>

                        <xsl:value-of select="$eightSpaces"/>
                        <xsl:text>items:</xsl:text>
                        <xsl:text>&#x0A;</xsl:text>
                        <xsl:value-of select="$tenSpaces"/>
                    </xsl:if>

                    <xsl:if test="$typeFinal='uuid'">
                        <xsl:text>description: UUID of object</xsl:text>
                        <xsl:text>&#x0A;</xsl:text>
                        <xsl:value-of select="$eightSpaces"/>
                    </xsl:if>
                    <xsl:text>type: </xsl:text>
                    <xsl:value-of select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$typeFinal]/@type"/>
                </xsl:otherwise>
            </xsl:choose>
            <xsl:text>&#x0A;</xsl:text>
        </xsl:for-each>
    </xsl:if>
</xsl:template>

</xsl:stylesheet>
