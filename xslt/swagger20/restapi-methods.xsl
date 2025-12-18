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

<xsl:template name="tags">
  <xsl:text>#==========[ Tags ]===================================================&#x0A;</xsl:text>
  <xsl:text>tags:&#x0A;</xsl:text>
  <xsl:for-each select="exsl:node-set($G_uniqueTags)/tag">
    <xsl:call-template name="tag"/>
  </xsl:for-each>
</xsl:template>

<xsl:template name="tag">
    <xsl:text>- name: </xsl:text>
    <xsl:value-of select="@name" />
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$twoSpaces"/>
    <xsl:text>description: </xsl:text>
    <xsl:value-of select="$aposDouble"/>
    <xsl:text>Stuff that work with the interface </xsl:text>
    <xsl:value-of select="@interface" />
    <xsl:value-of select="$aposDouble"/>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$twoSpaces"/>
    <xsl:text>externalDocs: </xsl:text>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$fourSpaces"/>
    <xsl:text>url: </xsl:text>

    <xsl:variable name="interfaceSplittedByLetters">
      <xsl:call-template name="splitWord">
        <xsl:with-param name="word" select="@interface"/>
      </xsl:call-template>
    </xsl:variable>

    <xsl:text>https://www.virtualbox.org/sdkref/interface</xsl:text>
    <xsl:for-each select="exsl:node-set($interfaceSplittedByLetters)/letter">
      <xsl:choose>
        <xsl:when test="contains($G_upperCase, current())">
          <xsl:text>_</xsl:text>
          <xsl:value-of select="translate(current(), $G_upperCase, $G_lowerCase)"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="."/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:for-each>
    <xsl:text>.html</xsl:text>
    <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template name="methodDescription">
    <xsl:param name="text" />
    <xsl:apply-templates select="desc"/>
</xsl:template>

<xsl:template match="endpoint/desc">
    <xsl:apply-templates/>
</xsl:template>

<xsl:template match="result">
  <xsl:if test="count(preceding-sibling::result)+1=1">
    <xsl:text>&#x0A;</xsl:text>
    <xsl:value-of select="$eightSpaces"/>
    <xsl:text>&lt;h3&gt;Possible results&lt;/h3&gt;</xsl:text>
    <xsl:text>&#x0A;</xsl:text>
  </xsl:if>
  <xsl:value-of select="$eightSpaces"/>
  <xsl:value-of select="@name"/>
  <xsl:text>: </xsl:text>
  <xsl:apply-templates/>

  <xsl:if test="count(../result)!=count(preceding-sibling::result)+1">
    <xsl:text>&#x0A;</xsl:text>
  </xsl:if>
</xsl:template>

<xsl:template match="/idl">
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="library">
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="application">

    <xsl:call-template name="tags"/>

    <xsl:text>#==========[ Paths ]==================================================&#x0A;</xsl:text>
    <xsl:text>paths:&#x0A;</xsl:text>

    <xsl:for-each select="exsl:node-set($G_endpointsNodeSet)/endpoint">
        <xsl:sort select="@uri" order="ascending" lang="en-US"/>

        <xsl:variable name="httpRequest">
          <xsl:call-template name="stringToUpper">
            <xsl:with-param name="str" select="@httpRequest"/>
          </xsl:call-template>
        </xsl:variable>

        <xsl:if test="not(@uri=preceding-sibling::*[1]/@uri)">
            <xsl:value-of select="$twoSpaces"/>
            <xsl:value-of select="concat(@uri, ':')"/>
            <xsl:text>&#x0A;</xsl:text>
        </xsl:if>

        <xsl:call-template name="endpoint"/>
        <xsl:text>&#x0A;</xsl:text>
    </xsl:for-each>

</xsl:template>

<xsl:template name="endpoint">
    <xsl:variable name="interfaceName">
        <xsl:value-of select="@interfaceName"/>
    </xsl:variable>

    <xsl:variable name="restpath" select="@uri"/>
    <xsl:variable name="httpRequest" select="@httpRequest"/>

    <xsl:variable name="restname">
      <xsl:call-template name="stringToLower">
        <xsl:with-param name="str" select="@restName"/>
      </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="defaultOperationId">
      <xsl:value-of select="substring(@interfaceName, 2)"/>
      <xsl:text>_</xsl:text>
      <xsl:value-of select="@methodName"/>
    </xsl:variable>

    <xsl:variable name="defaultSummaryText">
      <xsl:text>Call interface method </xsl:text>
      <xsl:value-of select="@interfaceName"/>
      <xsl:text>::</xsl:text>
      <xsl:value-of select="@methodName"/>
    </xsl:variable>

    <xsl:variable name="defaultDescriptionText">
        <xsl:if test="desc and desc!=''">
            <xsl:apply-templates select="desc"/>
        </xsl:if>
    </xsl:variable>

    <xsl:variable name="numberOfOutputParams">
      <xsl:value-of select="count(param[@dir='return' or @dir='out'])"/>
    </xsl:variable>

    <xsl:variable name="numberOfInputParams">
      <xsl:value-of select="count(param[@dir='in'])"/>
    </xsl:variable>

    <xsl:variable name="numberOfAllParams">
       <xsl:value-of select="count(param)"/>
    </xsl:variable>

    <xsl:variable name="splittedPath">
      <xsl:call-template name="splitString">
        <xsl:with-param name="string" select="$restpath"/>
        <xsl:with-param name="separator" select="'/'"/>
      </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="placeholdersNodeSet">
      <xsl:for-each select="exsl:node-set($splittedPath)/token">
        <xsl:if test="starts-with(current(),'{') and contains(current(),'}')">
          <xsl:variable name="placeholder" select="substring-before(substring-after(current(),'{'), '}')"/>
          <token>
            <xsl:value-of select="$placeholder"/>
          </token>
        </xsl:if>
      </xsl:for-each>
    </xsl:variable>

    <xsl:variable name="fUseRequestBody">
        <xsl:if test="count(param[@dir='in'])>1 and ($httpRequest='post' or $httpRequest='put' or $httpRequest='patch')">
             <xsl:value-of select="true()"/>
        </xsl:if>
    </xsl:variable>

    <xsl:variable name="operationIdSplittedByLetters">
        <xsl:call-template name="splitWord">
            <xsl:with-param name="word" select="$defaultOperationId"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="parameterRequestBodyPrefix">
        <xsl:call-template name="replaceUnderscoreWithUppercase">
            <xsl:with-param name="lettersNodeSet" select="$operationIdSplittedByLetters"/>
            <xsl:with-param name="letter" select="letter"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="fSelectInQuery">
        <xsl:for-each select="param">
            <xsl:variable name="type" select="@type"/>
            <xsl:if test="(@dir='return')">
                <xsl:variable name="fInterface">
                    <xsl:value-of select="exsl:node-set($G_interfacesNames)/interface[@name=$type]/@name"/>
                </xsl:variable>
                <xsl:if test="$fInterface!='' and ($httpRequest='get')">
                    <xsl:value-of select="true()"/>
                </xsl:if>
            </xsl:if>
        </xsl:for-each>
    </xsl:variable>

    <xsl:variable name="basePath">
      <xsl:for-each select="exsl:node-set($splittedPath)/token">
        <xsl:if test="position()=2">
          <xsl:value-of select="current()"/>
        </xsl:if>
      </xsl:for-each>
    </xsl:variable>


    <xsl:variable name="replaceTag">
      <xsl:for-each select="exsl:node-set($G_tagsMap)/tag">
        <xsl:if test="@path=$basePath">
          <xsl:value-of select="@name"/>
        </xsl:if>
      </xsl:for-each>
    </xsl:variable>

    <xsl:variable name="defaultTag">
      <xsl:for-each select="exsl:node-set($G_uniqueTags)/tag">
         <xsl:if test="contains($basePath,@name) and (string-length($basePath)=string-length(@name)+1)">
           <xsl:value-of select="@name"/>
         </xsl:if>
      </xsl:for-each>
    </xsl:variable>

<!-- !!!!!!!!!!!!!!!!!!!!! OUTPUT STARTING HERE !!!!!!!!!!!!!!!!!!!!!!! -->
    <xsl:value-of select="$fourSpaces"/>
    <xsl:value-of select="concat($httpRequest,':')"/>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>tags: </xsl:text>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>- </xsl:text>
    <xsl:value-of select="$aposDouble"/>

    <xsl:variable name="interfaceLowCase">
      <xsl:call-template name="stringToLower">
        <xsl:with-param name="str" select="@interfaceName"/>
      </xsl:call-template>
    </xsl:variable>
    <xsl:value-of select="substring($interfaceLowCase,2)"/>

    <xsl:value-of select="$aposDouble"/>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>operationId: </xsl:text>

    <xsl:value-of select="$aposDouble"/>
    <xsl:call-template name="stringToLower">
      <xsl:with-param name="str" select="$defaultOperationId" />
    </xsl:call-template>
    <xsl:value-of select="$aposDouble"/>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>summary: </xsl:text>

    <xsl:value-of select="$aposDouble"/>
    <xsl:value-of select="$defaultSummaryText"/>
    <xsl:value-of select="$aposDouble"/>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>description: </xsl:text>
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

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>produces:</xsl:text>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>- "application/json"</xsl:text>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:if test="count(exsl:node-set($placeholdersNodeSet)/token)!=0 or $numberOfInputParams!=0 or $fSelectInQuery='true'">
      <xsl:value-of select="$sixSpaces"/>
      <xsl:text>parameters: </xsl:text>
      <xsl:text>&#x0A;</xsl:text>
    </xsl:if>

    <xsl:for-each select="exsl:node-set($placeholdersNodeSet)/token">
      <xsl:value-of select="$sixSpaces"/>
      <xsl:text>- name: </xsl:text>
      <xsl:value-of select="$aposDouble"/>
      <xsl:value-of select="current()"/>
      <xsl:value-of select="$aposDouble"/>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>description: </xsl:text>
      <xsl:value-of select="$aposDouble"/>
      <xsl:text>The Id of </xsl:text>
      <xsl:value-of select="substring(current(), 1, string-length())"/>
      <xsl:value-of select="$aposDouble"/>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>in: "path"</xsl:text>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>required: true</xsl:text>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>type: "string"</xsl:text>
      <xsl:text>&#x0A;</xsl:text>
    </xsl:for-each>  

    <xsl:if test="$fSelectInQuery='true'">
      <xsl:value-of select="$sixSpaces"/>
      <xsl:text>- name: "select"</xsl:text>
      <xsl:text>&#x0A;</xsl:text>
      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>in: "query"</xsl:text>
      <xsl:text>&#x0A;</xsl:text>
      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>description: "The object attributes separated by comma"</xsl:text>
      <xsl:text>&#x0A;</xsl:text>
      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>required: false</xsl:text>
      <xsl:text>&#x0A;</xsl:text>
      <xsl:value-of select="$eightSpaces"/>
      <xsl:text>type: "string"</xsl:text>
      <xsl:text>&#x0A;</xsl:text>
    </xsl:if>

    <xsl:choose>
        <xsl:when test="$fUseRequestBody='true'">
            <xsl:value-of select="$sixSpaces"/>
            <xsl:text>- name: </xsl:text>
            <xsl:value-of select="concat('o',$parameterRequestBodyPrefix, 'RequestBody')"/>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>

            <xsl:text>in: "body"</xsl:text>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>

            <xsl:text>required: true</xsl:text>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>

            <xsl:text>schema:</xsl:text>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$tenSpaces"/>

            <xsl:value-of select="concat('$ref: ', $aposDouble, '#/definitions/', $parameterRequestBodyPrefix, 'RequestBody', $aposDouble)"/>
            <xsl:text>&#x0A;</xsl:text>
        </xsl:when>
        <xsl:otherwise>
            <xsl:for-each select="param">
              <xsl:sort select="@dir"/>

              <xsl:variable name="type" select="@type"/>

              <xsl:variable name="fInterface">
                <xsl:value-of select="exsl:node-set($G_interfacesNames)/interface[@name=$type]/@name"/>
              </xsl:variable>

              <xsl:variable name="fEnumeration">
                <xsl:value-of select="exsl:node-set($G_enumerationsNames)/enumeration[@name=$type]/@name"/>
              </xsl:variable>

                <xsl:if test="@dir='in'">
                  <xsl:value-of select="$sixSpaces"/>
                  <xsl:text>- name: </xsl:text>
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
                  <xsl:text>&#x0A;</xsl:text>

                  <xsl:value-of select="$eightSpaces"/>
                  <xsl:text>in: "query"</xsl:text>
                  <xsl:text>&#x0A;</xsl:text>

                  <xsl:value-of select="$eightSpaces"/>
                  <xsl:choose>
                    <xsl:when test="$fEnumeration!=''">
                      <xsl:text>description: </xsl:text>
                      <xsl:value-of select="$aposDouble"/>
                      <xsl:text>For the possible values of enumeration look into #/definitions/</xsl:text>
                      <xsl:value-of select="$type"/>
                      <xsl:value-of select="$aposDouble"/>
                      <xsl:text>&#x0A;</xsl:text>

                      <xsl:value-of select="$eightSpaces"/>

                      <xsl:if test="@safearray='yes'">

                        <xsl:text>type: "array"</xsl:text>
                        <xsl:text>&#x0A;</xsl:text>

                        <xsl:value-of select="$eightSpaces"/>
                        <xsl:text>items:</xsl:text>
                        <xsl:text>&#x0A;</xsl:text>
                        <xsl:value-of select="$tenSpaces"/>

                      </xsl:if>

                      <xsl:text>type: "string"</xsl:text>
                    </xsl:when>
                    <xsl:when test="$fInterface!=''">
                      <xsl:text>description: </xsl:text>
                      <xsl:value-of select="$aposDouble"/>
                      <xsl:text>Put here an ID of requested </xsl:text>
                      <xsl:value-of select="$type"/>
                      <xsl:text> VirtualBox object</xsl:text>
                      <xsl:value-of select="$aposDouble"/>
                      <xsl:text>&#x0A;</xsl:text>

                      <xsl:value-of select="$eightSpaces"/>

                      <xsl:choose>
                        <xsl:when test="@safearray='yes'">
                          <xsl:text>type: "array"</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$eightSpaces"/>
                          <xsl:text>items:</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$tenSpaces"/>
                          <xsl:text>type: "string"</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$tenSpaces"/>
                          <xsl:text>format: "uuid"</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$tenSpaces"/>
                          <xsl:text>x-vbox-type: </xsl:text>
                          <xsl:value-of select="$type"/>
                        </xsl:when>
                        <xsl:otherwise>
                          <xsl:text>type: "string"</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$eightSpaces"/>
                          <xsl:text>format: "uuid"</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$eightSpaces"/>
                          <xsl:text>x-vbox-type: </xsl:text>
                          <xsl:value-of select="$type"/>
                        </xsl:otherwise>
                      </xsl:choose>
                    </xsl:when>

                    <xsl:otherwise>
                      <xsl:choose>
                        <xsl:when test="@safearray='yes'">
                          <xsl:text>type: "array"</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$eightSpaces"/>
                          <xsl:text>items:</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$tenSpaces"/>
                          <xsl:text>type: "string"</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                          <xsl:value-of select="$eightSpaces"/>
                          <xsl:text>collectionFormat: pipes</xsl:text>
                          <xsl:text>&#x0A;</xsl:text>
                        </xsl:when>
                        <xsl:otherwise>
                          <xsl:text>type: </xsl:text>
                          <xsl:value-of select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@type"/>
                        </xsl:otherwise>
                      </xsl:choose>
                    </xsl:otherwise>
                  </xsl:choose>
                  <xsl:text>&#x0A;</xsl:text>
                </xsl:if>
            </xsl:for-each>
        </xsl:otherwise>
    </xsl:choose>

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>responses:</xsl:text>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$eightSpaces"/>
    <xsl:text>"200":</xsl:text>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:value-of select="$tenSpaces"/>
    <xsl:text>description: "Successful operation"</xsl:text>
    <xsl:text>&#x0A;</xsl:text>

    <xsl:if test="$numberOfOutputParams!=0">
      <xsl:value-of select="$tenSpaces"/>
      <xsl:text>schema:</xsl:text>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:choose>
        <xsl:when test="$numberOfOutputParams=1">
          <xsl:value-of select="$twelveSpaces"/>
          <xsl:text>title: </xsl:text>

          <xsl:for-each select="param">
            <xsl:sort select="@dir"/>

            <xsl:variable name="type" select="@type"/>

            <xsl:variable name="fInterface">
              <xsl:value-of select="exsl:node-set($G_interfacesNames)/interface[@name=$type]/@name"/>
            </xsl:variable>

            <xsl:variable name="fEnumeration">
              <xsl:value-of select="exsl:node-set($G_enumerationsNames)/enumeration[@name=$type]/@name"/>
            </xsl:variable>

            <xsl:if test="(@dir='out' or @dir='return')">
              <xsl:choose>
                <xsl:when test="($fInterface!='' or $fEnumeration!='')">
                  <xsl:choose>
                    <xsl:when test="$fInterface!=''">
                      <xsl:value-of select="substring($fInterface,2)"/>
                      <xsl:text>Obj</xsl:text>
                    </xsl:when>
                    <xsl:otherwise>
                      <xsl:value-of select="$fEnumeration"/>
                      <xsl:text>Enum</xsl:text>
                    </xsl:otherwise>
                  </xsl:choose>
                  <xsl:if test="@safearray='yes'">
                  <xsl:text>Array</xsl:text>
                  </xsl:if>
                  <xsl:text>WrapperResponse</xsl:text>
                </xsl:when>

                <xsl:otherwise>
                  <xsl:value-of select="concat($aposDouble, $parameterRequestBodyPrefix, 'Response', $aposDouble)"/>
                </xsl:otherwise>

              </xsl:choose>
            </xsl:if>
          </xsl:for-each>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="$twelveSpaces"/>
          <xsl:text>title: </xsl:text>
          <xsl:value-of select="concat($aposDouble, $parameterRequestBodyPrefix, 'Response', $aposDouble)"/>
        </xsl:otherwise>
      </xsl:choose>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:value-of select="$twelveSpaces"/>
      <xsl:text>type: "object"</xsl:text>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:value-of select="$twelveSpaces"/>
      <xsl:text>properties:</xsl:text>
      <xsl:text>&#x0A;</xsl:text>
    </xsl:if>

    <xsl:for-each select="param">
      <xsl:sort select="@dir"/>

      <xsl:variable name="type" select="@type"/>

      <xsl:variable name="fInterface">
        <xsl:value-of select="exsl:node-set($G_interfacesNames)/interface[@name=$type]/@name"/>
      </xsl:variable>

      <xsl:variable name="fEnumeration">
        <xsl:value-of select="exsl:node-set($G_enumerationsNames)/enumeration[@name=$type]/@name"/>
      </xsl:variable>

      <xsl:if test="@dir='out' or @dir='return'">

        <xsl:value-of select="$fourteenSpaces"/>

        <xsl:choose>
          <xsl:when test="$numberOfOutputParams=1 and ($fInterface!='' or $fEnumeration!='')">
            <xsl:choose>
              <xsl:when test="$fInterface!=''">
                <xsl:call-template name="stringToLower">
                  <xsl:with-param name="str" select="substring($fInterface,2)" />
                </xsl:call-template>
              </xsl:when>
              <xsl:otherwise>
                <xsl:call-template name="stringToLower">
                  <xsl:with-param name="str" select="$fEnumeration" />
                </xsl:call-template>
              </xsl:otherwise>
            </xsl:choose>
            <xsl:if test="@safearray='yes'">
            <xsl:text>array</xsl:text>
            </xsl:if>
          </xsl:when>
          <xsl:otherwise>
            <xsl:value-of select="@name"/>
          </xsl:otherwise>
        </xsl:choose>

        <xsl:text>:&#x0A;</xsl:text>
        <xsl:value-of select="$sixteenSpaces"/>

        <xsl:choose>

          <xsl:when test="$fInterface!='' or $fEnumeration!=''">
            <xsl:if test="@safearray='yes'">
              <xsl:text>type: "array"</xsl:text>
              <xsl:text>&#x0A;</xsl:text>

              <xsl:value-of select="$sixteenSpaces"/>
              <xsl:text>items:</xsl:text>
              <xsl:text>&#x0A;</xsl:text>
              <xsl:value-of select="$eighteenSpaces"/>
            </xsl:if>

            <xsl:variable  name="temp">
                <xsl:choose>
                  <xsl:when test="$fInterface!=''">
                    <xsl:value-of select="substring($type,2)"/>
                  </xsl:when>
                  <xsl:otherwise>
                    <xsl:value-of select="$type"/>
                  </xsl:otherwise>
                </xsl:choose>
            </xsl:variable>

            <xsl:value-of select="concat('$ref: ', $aposDouble, '#/definitions/', $temp, $aposDouble)"/>
            <xsl:text>&#x0A;</xsl:text>
          </xsl:when>

          <xsl:when test="@type='octet'">
            <xsl:text>type: </xsl:text>
            <xsl:value-of select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@type"/>
            <xsl:text>&#x0A;</xsl:text>
            <xsl:variable name="swaggerFormat" select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@format"/>
            <xsl:if test="$swaggerFormat='byte'">
              <xsl:value-of select="$sixteenSpaces"/>
              <xsl:text>format: </xsl:text>
              <xsl:value-of select="$swaggerFormat"/>
              <xsl:text>&#x0A;</xsl:text>
            </xsl:if>
          </xsl:when>

          <xsl:otherwise>
            <xsl:if test="@safearray='yes' and @type!='octet'">
              <xsl:text>type: "array"</xsl:text>
              <xsl:text>&#x0A;</xsl:text>

              <xsl:value-of select="$sixteenSpaces"/>
              <xsl:text>items:</xsl:text>
              <xsl:text>&#x0A;</xsl:text>
              <xsl:value-of select="$eighteenSpaces"/>
            </xsl:if>

            <xsl:text>type: </xsl:text>
            <xsl:value-of select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@type"/>
            <xsl:text>&#x0A;</xsl:text>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:if>
    </xsl:for-each>

    <xsl:for-each select="exsl:node-set($G_httpResponses)/httpresponse">
      <xsl:value-of select="$eightSpaces"/>
      <xsl:value-of select="$aposDouble"/>
      <xsl:value-of select="@code"/>
      <xsl:value-of select="$aposDouble"/>
      <xsl:text>:</xsl:text>
      <xsl:text>&#x0A;</xsl:text>

      <xsl:value-of select="$tenSpaces"/>
      <xsl:value-of select="concat('$ref: ', $aposDouble, '#/responses/', @name, $aposDouble)"/>
      <xsl:text>&#x0A;</xsl:text>
    </xsl:for-each>

    <xsl:value-of select="$sixSpaces"/>
    <xsl:text>x-vbox-stub: </xsl:text>
    <xsl:value-of select="$aposDouble"/>
    <xsl:value-of select="@stub"/>
    <xsl:value-of select="$aposDouble"/>
    <xsl:text>&#x0A;</xsl:text>
</xsl:template>

</xsl:stylesheet>
