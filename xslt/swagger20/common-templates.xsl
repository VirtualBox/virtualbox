<?xml version="1.0"?>
<xsl:stylesheet
    version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:exsl="http://exslt.org/common"
    xmlns:str="http://exslt.org/strings"
    extension-element-prefixes="exsl str"
    >

<xsl:template match="link">
    <xsl:value-of select="@to"/>
</xsl:template>

<xsl:template match="i">
  <xsl:text> &lt;i&gt;</xsl:text>
  <xsl:value-of select="."/>
  <xsl:text> &lt;/i&gt;</xsl:text>
</xsl:template>
<xsl:template match="b">
  <xsl:text> &lt;b&gt;</xsl:text>
  <xsl:value-of select="."/>
  <xsl:text> &lt;/b&gt;</xsl:text>
</xsl:template>
<xsl:template match="tt">
    <xsl:value-of select="."/>
</xsl:template>
<xsl:template match="h3">
  <xsl:text> &lt;h3&gt;</xsl:text>
  <xsl:value-of select="."/>
  <xsl:text> &lt;/h3&gt;</xsl:text>
</xsl:template>
<xsl:template match="pre">
    <xsl:value-of select="."/>
</xsl:template>

<xsl:template match="ol">
  <xsl:text> &lt;ol&gt;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text> &lt;/ol&gt;</xsl:text>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>
<xsl:template match="ul">
    <xsl:text> &lt;ul&gt;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text> &lt;/ul&gt;</xsl:text>
    <xsl:text>&#x0A;</xsl:text>
</xsl:template>
<xsl:template match="li">
    <xsl:text> &lt;li&gt;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text> &lt;/li&gt;</xsl:text>
    <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template match="note">
  <xsl:text>Note! </xsl:text>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template name="capitalize">
  <xsl:param name="str" select="."/>
  <xsl:value-of select="
        concat(
            translate(substring($str,1,1),$G_lowerCase,$G_upperCase),
            substring($str,2)
        )
  "/>
</xsl:template>

<xsl:template name="stringToLower">
  <xsl:param name="str" select="."/>
  <xsl:value-of select="translate($str, $G_upperCase, $G_lowerCase)"/>
</xsl:template>

<xsl:template name="stringToUpper">
  <xsl:param name="str" select="."/>
  <xsl:value-of select="translate($str, $G_lowerCase, $G_upperCase)"/>
</xsl:template>

<xsl:template name="pathPlaceholder">
  <xsl:param name="path"/>
  <xsl:if test="contains($path,'{') and contains($path,'}')">
    <xsl:variable name="start" select="substring-after($path,'{')"/>
    <xsl:value-of select="substring-before($start, '}')"/>
  </xsl:if>
</xsl:template>

<xsl:template name="splitString">
  <xsl:param name="string"/>
  <xsl:param name="separator" select="','"/>

  <xsl:if test="$string and $string != ''">
    <xsl:choose>
      <xsl:when test="contains($string,$separator)">
        <token>
          <xsl:value-of select="substring-before($string,$separator)"/>
        </token>
        <xsl:call-template name="splitString">
          <xsl:with-param name="string" select="substring-after($string,$separator)"/>
          <xsl:with-param name="separator" select="$separator"/>
        </xsl:call-template>
      </xsl:when>
      <xsl:otherwise>
        <token><xsl:value-of select="$string"/></token>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:if>
</xsl:template>

<xsl:template name="splitWord">
    <xsl:param name="word"/>
    <letter>
        <xsl:value-of select="substring($word, 1, 1)"/>
    </letter>
    <xsl:if test="string-length($word) > 1">
        <xsl:call-template name="splitWord">
            <xsl:with-param name="word" select="substring($word, 2, string-length($word) - 1)"/>
        </xsl:call-template>
    </xsl:if>
</xsl:template>

<xsl:template name="stringReplaceAll">
  <xsl:param name="text" />
  <xsl:param name="replace" />
  <xsl:param name="by" />
  <xsl:choose>
    <xsl:when test="$text = '' or $replace = '' or not($replace)" >
      <xsl:value-of select="$text" />
    </xsl:when>
    <xsl:when test="contains($text, $replace)">
      <xsl:value-of select="substring-before($text,$replace)" />
      <xsl:value-of select="$by" />
      <xsl:call-template name="stringReplaceAll">
        <xsl:with-param name="text" select="substring-after($text,$replace)" />
        <xsl:with-param name="replace" select="$replace" />
        <xsl:with-param name="by" select="$by" />
      </xsl:call-template>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$text" />
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template name="replaceUnderscoreWithUppercase">
    <xsl:param name="lettersNodeSet" />
    <xsl:param name="letter" />
    <xsl:param name="fSkipFirst" />
    <xsl:for-each select="exsl:node-set($lettersNodeSet)/letter">
        <xsl:choose>
            <xsl:when test="preceding-sibling::letter[1]='_'">
                <xsl:value-of select="translate(current(), $G_lowerCase, $G_upperCase)"/>
            </xsl:when>
            <xsl:when test="current()!='_'">
                <xsl:value-of select="."/>
            </xsl:when>
        </xsl:choose>
    </xsl:for-each>
</xsl:template>

<xsl:template name="replaceUppercaseWithUnderscore">
    <xsl:param name="lettersNodeSet" />
    <xsl:param name="letter" />
    <xsl:param name="fSkipFirst" />
    <xsl:for-each select="exsl:node-set($lettersNodeSet)/letter">
        <xsl:variable name="prev" select="preceding-sibling::letter[1]"/>
        <xsl:variable name="next">
            <xsl:if test="position()!=last()">
                <xsl:value-of select="following-sibling::letter[1]"/>
            </xsl:if>
        </xsl:variable>
        <xsl:variable name="f0" select="contains($G_upperCase, current())"/>
        <xsl:variable name="f1" select="contains($G_upperCase, $prev)"/>
        <xsl:variable name="f2">
            <xsl:if test="position()!=last()">
                <xsl:value-of select="contains($G_upperCase, $next)"/>
            </xsl:if>
        </xsl:variable>
        <xsl:variable name="f3" select="contains($G_lowerCase, $prev)"/>
        <xsl:variable name="f4">
            <xsl:if test="position()!=last()">
                <xsl:value-of select="contains($G_lowerCase, $next)"/>
            </xsl:if>
        </xsl:variable>
        <xsl:choose>
            <xsl:when test="contains($G_upperCase, current())">
                <xsl:choose>
                    <xsl:when test="fSkipFirst = 'false' and count(preceding-sibling::letter)+1 = 1">
                        <xsl:text>_</xsl:text>
                    </xsl:when>
                    <xsl:when test="count(preceding-sibling::letter)+1 != 1 and ($f4='true' or $f3='true')">
                        <xsl:text>_</xsl:text>
                    </xsl:when>
                </xsl:choose>
                <xsl:value-of select="translate(current(), $G_upperCase, $G_lowerCase)"/>
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="."/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:for-each>
</xsl:template>

</xsl:stylesheet>
