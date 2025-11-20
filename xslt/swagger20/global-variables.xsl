<?xml version="1.0"?>
<xsl:stylesheet
    version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:exsl="http://exslt.org/common"
    xmlns:str="http://exslt.org/strings"
    extension-element-prefixes="exsl str"
    >

<xsl:variable name="G_lowerCase" select="'abcdefghijklmnopqrstuvwxyz{}'" />
<xsl:variable name="G_upperCase" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ{}'" />
<xsl:variable name="G_sNewLine">
  <xsl:text>
  </xsl:text>
</xsl:variable>

<xsl:variable name="G_sWhiteSpace" select="' &#10;&#13;&#09;'"/>

<xsl:key name="G_keyEnumsByName" match="//enum[@name]" use="@name"/>
<xsl:key name="G_keyInterfacesByName" match="//interface[@name]" use="@name"/>
<xsl:key name="G_keyMethodsByRESTPath" match="method" use="rest/@path"/>
<xsl:key name="G_keyInterfacesByRESTSupport" match="//interface[@rest='managed']" use="@name"/>
<xsl:key name="G_keyRESTMethodsByName" match="//idl/library/application/interface/method[rest]" use="@name"/>

<xsl:variable name="G_aSwaggerTypes">
  <!-- <type idlname="octet" type="string" format="binary"/> -->
  <!-- <type idlname="octet" type="string" format="byte"/> -->
  <type idlname="octet" type="string"/>
  <type idlname="boolean" type="boolean"/>
  <type idlname="short" type="integer" format="int32"/>
  <type idlname="unsigned short" type="integer" format="int32"/>
  <type idlname="long" type="integer" format="int32"/>
  <type idlname="unsigned long" type="integer" format="int32"/>
  <type idlname="long long" type="integer" format="int64"/>
  <type idlname="unsigned long long" type="integer" format="int64"/>
  <type idlname="double" type="number" format="double"/>
  <type idlname="float" type="number" format="float"/>
  <type idlname="wstring" type="string"/>
  <type idlname="uuid" type="string" format="uuid"/>
  <type idlname="result" type="integer" format="int32"/>
  <type idlname="safearray" type="array"/>
  <type idlname="uuidstring" type="string" format="attruuid"/>
</xsl:variable>

<xsl:variable name="apos">'</xsl:variable>
<xsl:variable name="aposDouble">"</xsl:variable>

<xsl:variable name="twoSpaces">
  <xsl:text>  </xsl:text>
</xsl:variable>
<xsl:variable name="fourSpaces">
  <xsl:text>    </xsl:text>
</xsl:variable>
<xsl:variable name="sixSpaces">
  <xsl:text>      </xsl:text>
</xsl:variable>
<xsl:variable name="eightSpaces">
  <xsl:text>        </xsl:text>
</xsl:variable>
<xsl:variable name="tenSpaces">
  <xsl:text>          </xsl:text>
</xsl:variable>
<xsl:variable name="twelveSpaces">
  <xsl:text>            </xsl:text>
</xsl:variable>
<xsl:variable name="fourteenSpaces">
  <xsl:text>              </xsl:text>
</xsl:variable>
<xsl:variable name="sixteenSpaces">
  <xsl:text>                </xsl:text>
</xsl:variable>
<xsl:variable name="eighteenSpaces">
  <xsl:text>                  </xsl:text>
</xsl:variable>
<xsl:variable name="twentySpaces">
  <xsl:text>                    </xsl:text>
</xsl:variable>

<xsl:variable name="G_tagsMap">
  <!-- <tag name="server" interface="IVirtualBox" path="server"/>
  <tag name="vm" interface="IMachine" path="vms"/>
  <tag name="progress" interface="IProgress" path="progresses"/>
  <tag name="medium" interface="IMedium" path="media"/>   -->
</xsl:variable>

<xsl:variable name="G_httpResponses">
  <httpresponse code="400" name="BadRequest" desc="Bad Request"/>
  <httpresponse code="401" name="Unauthorized" desc="Unauthorized"/>
  <httpresponse code="403" name="Forbidden" desc="Forbidden"/>
  <httpresponse code="404" name="NotFound" desc="Not Found"/>
  <httpresponse code="405" name="MethodNotAllowed" desc="Method Not Allowed"/>
  <httpresponse code="409" name="Conflict" desc="Internal Conflict"/>
  <httpresponse code="411" name="ContentLengthRequired" desc="Content-Length Required"/>
  <httpresponse code="412" name="PreconditionFailed" desc="Precondition Failed"/>
  <httpresponse code="429" name="TooManyRequests" desc="Too Many Requests"/>
  <httpresponse code="500" name="InternalServerError" desc="Internal Server Error"/>
  <httpresponse code="501" name="NotImplemented" desc="Not Implemented"/>
  <httpresponse code="503" name="ServerUnavailable" desc="Server Unavailable"/>
</xsl:variable>

<xsl:variable name="G_interfacesNames">
  <xsl:for-each select="//interface[@rest='managed']">
    <xsl:element name="interface">
     <xsl:attribute name="name">
      <xsl:value-of select="@name"/>
     </xsl:attribute>
    </xsl:element>
  </xsl:for-each>
</xsl:variable>

<xsl:variable name="G_enumerationsNames">
  <xsl:for-each select="//enum">
    <xsl:element name="enumeration">
     <xsl:attribute name="name">
      <xsl:value-of select="@name"/>
     </xsl:attribute>
    </xsl:element>
  </xsl:for-each>
</xsl:variable>

<xsl:variable name="G_uniqueTags">
  <xsl:for-each select="//interface[@rest='managed']">
    <xsl:variable name="interface" select="@name"/>
    <xsl:variable name="interfaceLowCase">
      <xsl:call-template name="stringToLower">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="replaceTag">
     <xsl:for-each select="exsl:node-set($G_tagsMap)/tag">
      <xsl:if test="@interface=$interface">
        <xsl:value-of select="@name"/>
      </xsl:if>
     </xsl:for-each>
    </xsl:variable>

    <xsl:element name="tag">
     <xsl:attribute name="name">
      <xsl:choose>
       <xsl:when test="string-length($replaceTag)!=0">
        <xsl:value-of select="$replaceTag"/>
       </xsl:when>
       <xsl:otherwise>
        <xsl:value-of select="substring($interfaceLowCase,2)"/>
       </xsl:otherwise>
      </xsl:choose>
     </xsl:attribute>
     <xsl:attribute name="interface">
      <xsl:value-of select="@name"/>
     </xsl:attribute>
    </xsl:element>
  </xsl:for-each>
</xsl:variable>

<xsl:variable name="G_nonUniqueTags">
  <xsl:for-each select="//interface/method[rest]">
    <xsl:sort select="rest/@path" order="descending" lang="en-US"/>
    <xsl:sort select="rest/@name" order="descending" lang="en-US"/>

    <xsl:variable name="interface" select="../@name"/>
    <xsl:variable name="restpath" select="rest/@path"/>

    <xsl:variable name="splittedPath">
      <xsl:call-template name="splitString">
        <xsl:with-param name="string" select="$restpath"/>
        <xsl:with-param name="separator" select="'/'"/>
      </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="currTagName">
      <xsl:for-each select="exsl:node-set($splittedPath)/token">
        <xsl:if test="position()=2">
          <xsl:value-of select="current()"/>
        </xsl:if>
      </xsl:for-each>
    </xsl:variable>

    <xsl:element name="tag">
      <xsl:attribute name="name">
        <xsl:value-of select="$currTagName"/>
      </xsl:attribute>
      <xsl:attribute name="interface">
        <xsl:value-of select="$interface"/>
      </xsl:attribute>
    </xsl:element>

  </xsl:for-each>
</xsl:variable>


<xsl:variable name="G_nonUniquePaths">
  <xsl:for-each select="//interface/method[rest]">
    <xsl:sort select="rest/@path" order="descending" lang="en-US"/>
    <xsl:sort select="rest/@name" order="descending" lang="en-US"/>
      <xsl:variable name="restpath" select="rest/@path"/>
      <xsl:element name="path">
        <xsl:attribute name="name">
          <xsl:value-of select="rest/@path"/>
        </xsl:attribute>
      </xsl:element>
  </xsl:for-each>
</xsl:variable>

<xsl:variable name="G_uniquePaths">
  <xsl:for-each select="exsl:node-set($G_nonUniquePaths)/path[not(@name=preceding-sibling::path/@name)]">
    <xsl:element name="uniquePath">
      <xsl:attribute name="name">
        <xsl:value-of select="@name"/>
      </xsl:attribute>
    </xsl:element>
  </xsl:for-each>
</xsl:variable>

<xsl:variable name="G_syntheticEndpointsNodeSet">
    <endpoint uri="/server/" restName="find" httpRequest="get" restPath="/server/"
        interfaceName="ISynthetic" methodName="getServer" precedingSibling="">
        <desc>Getting VirtualBox server object</desc>
        <param name="server" type="IVirtualBox" dir="return"/>
    </endpoint>
    <endpoint uri="/media/{{mediumid}}/" restName="find" httpRequest="get" restPath="/media/{mediumid}/"
        interfaceName="ISynthetic" methodName="getMedium" precedingSibling="">
        <desc>Getting VirtualBox Medium object</desc>
        <param name="medium" type="IMedium" dir="return"/>
    </endpoint>
</xsl:variable>

<xsl:variable name="G_endpointsNodeSet">
  <xsl:for-each select="//interface/method[rest]">
      <xsl:sort select="rest/@path" order="descending" lang="en-US"/>
      <xsl:sort select="rest/@name" order="descending" lang="en-US"/>
      <xsl:sort select="@name" order="descending" lang="en-US"/>

      <xsl:variable name="restpath" select="rest/@path"/>

      <xsl:variable name="name">
        <xsl:call-template name="stringToLower">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
      </xsl:variable>

      <xsl:variable name="restname">
        <xsl:call-template name="stringToLower">
          <xsl:with-param name="str" select="rest/@name"/>
        </xsl:call-template>
      </xsl:variable>

      <xsl:variable name="fin-restname">
        <xsl:choose>
            <xsl:when test="$restname!=''">
              <xsl:value-of select="$restname"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:value-of select="$name"/>
            </xsl:otherwise>
        </xsl:choose>
      </xsl:variable>

      <xsl:variable name="desc-node-set" select="desc"/>
      <xsl:variable name="desc-tree">
          <xsl:copy-of select="desc/*" />
      </xsl:variable>

      <xsl:variable name="param-node-set" select="param"/>
      <xsl:variable name="param-tree">
          <xsl:copy-of select="param" />
      </xsl:variable>

      <xsl:element name="endpoint">
        <xsl:attribute name="uri">
            <xsl:choose>
                <xsl:when test="(rest/@name='create' and rest/@request='post')
                                or (rest/@name='find' and rest/@request='get')
                                or (rest/@name='remove' and rest/@request='delete')
                                or (rest/@name='update' and rest/@request='patch')">
                    <xsl:value-of select="rest/@path"/>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:value-of select="concat($restpath, $fin-restname)"/>
                </xsl:otherwise>
            </xsl:choose>
        </xsl:attribute>
        <xsl:attribute name="restPath">
          <xsl:value-of select="$restpath"/>
        </xsl:attribute>
        <xsl:attribute name="restName">
          <xsl:value-of select="$restname"/>
        </xsl:attribute>
        <xsl:attribute name="httpRequest">
          <xsl:value-of select="rest/@request"/>
        </xsl:attribute>
        <xsl:attribute name="methodName">
          <xsl:value-of select="@name"/>
        </xsl:attribute>
        <xsl:attribute name="interfaceName">
          <xsl:value-of select="../@name"/>
        </xsl:attribute>
        <xsl:attribute name="precedingSibling">
            <xsl:value-of select="preceding-sibling::*[1]/@name"/>
        </xsl:attribute>
        <xsl:copy-of select="$desc-node-set" />
        <xsl:copy-of select="$param-node-set" />
      </xsl:element>
  </xsl:for-each>
  <xsl:copy-of select="$G_syntheticEndpointsNodeSet"/>
</xsl:variable>

</xsl:stylesheet>
