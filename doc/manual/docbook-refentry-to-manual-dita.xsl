<?xml version="1.0"?>
<!--
    docbook-refentry-to-manual-dita.xsl:
        XSLT stylesheet for converting a refentry (manpage)
        to dita for use in the user manual.
-->
<!--
    Copyright (C) 2006-2024 Oracle and/or its affiliates.

    This file is part of VirtualBox base platform packages, as
    available from https://www.virtualbox.org.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, in version 3 of the
    License.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <https://www.gnu.org/licenses>.

    SPDX-License-Identifier: GPL-3.0-only
-->

<xsl:stylesheet
  version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:str="http://xsltsl.org/string"
  >

  <xsl:import href="string.xsl"/>
  <xsl:import href="common-formatcfg.xsl"/>

  <xsl:output method="xml" version="1.0" encoding="utf-8" indent="no"/>
  <xsl:preserve-space elements="*"/>
  <!-- xsl:strip-space elements="*"/ - never -->


<!-- - - - - - - - - - - - - - - - - - - - - - -
  parameters
 - - - - - - - - - - - - - - - - - - - - - - -->
<!-- Replace dashes with non-breaking dashes.
     Note! If the monospace font used in the PDF doesn't support it,
           then '#' shows up instead for instance.  This is currently
           the case, so it's disabled by default.  When we switch to
           4.0.x with the latest com.elovirta.pdf plugin (2023-03-xx
           or later), we can enable this by default again. -->
<xsl:param name="g_fReplaceHyphens">true</xsl:param>

<!-- Render the syntax diagram more as text than as proper markup. -->
<xsl:param name="g_fRenderSyntaxAsText">true</xsl:param>

<!-- Convert to reference if true, to topic structure if not.  -->
<xsl:param name="g_fToReference">false</xsl:param>


<!-- - - - - - - - - - - - - - - - - - - - - - -
  global XSLT variables
 - - - - - - - - - - - - - - - - - - - - - - -->



<!-- - - - - - - - - - - - - - - - - - - - - - -
  base operation is to fail on nodes w/o explicit matching.
 - - - - - - - - - - - - - - - - - - - - - - -->

<xsl:template match="*">
  <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>unhandled element</xsl:message>
</xsl:template>


<!-- - - - - - - - - - - - - - - - - - - - - - -
  transformations starting with root and going deeper.
 - - - - - - - - - - - - - - - - - - - - - - -->

<!-- Rename refentry to reference.
     Also we need to wrap the refsync and refsect1 elements in a refbody. -->
<xsl:template match="refentry">
  <xsl:variable name="sRootElement">
    <xsl:choose>
      <xsl:when test="$g_fToReference = 'true'">reference</xsl:when>
      <xsl:otherwise>topic</xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <!-- !DOCTYPE -->
  <xsl:choose>
    <xsl:when test="$g_fToReference = 'true'">
      <xsl:text disable-output-escaping='yes'>&lt;!DOCTYPE reference PUBLIC "-//OASIS//DTD DITA Reference//EN" "reference.dtd"&gt;</xsl:text>
    </xsl:when>
    <xsl:otherwise>
      <xsl:text disable-output-escaping='yes'>&lt;!DOCTYPE topic PUBLIC "-//OASIS//DTD DITA Topic//EN" "topic.dtd"&gt;</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:text>
</xsl:text>

  <!-- Root element -->
  <xsl:element name="{$sRootElement}">
    <xsl:if test="not(@id)">
      <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>refentry must have an id attribute!</xsl:message>
    </xsl:if>
    <xsl:attribute name="rev">refentry</xsl:attribute>
    <xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>

    <!-- Copy title element from refentryinfo -->
    <xsl:if test="./refentryinfo/title">
      <xsl:copy-of select="./refentryinfo/title"/>
    </xsl:if>

    <!-- Create a shortdesc element from the text in refnamediv/refpurpose -->
    <xsl:if test="./refnamediv/refpurpose">
      <xsl:element name="shortdesc">
        <xsl:attribute name="rev">refnamediv/refpurpose</xsl:attribute>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="text" select="normalize-space(./refnamediv/refpurpose)"/>
        </xsl:call-template>
      </xsl:element>
    </xsl:if>

    <!-- Put everything else side a refbody element if in 'reference' mode, otherwise no body. -->
    <xsl:choose>
      <xsl:when test="$g_fToReference = 'true'">
        <xsl:element name="refbody">
          <xsl:apply-templates />
        </xsl:element>
      </xsl:when>

      <xsl:otherwise>
        <xsl:apply-templates />
      </xsl:otherwise>
    </xsl:choose>

  </xsl:element>
</xsl:template>

<!-- Remove refentryinfo (we extracted the title element already). -->
<xsl:template match="refentryinfo" />

<!-- Remove refmeta (manpage info). -->
<xsl:template match="refmeta"/>

<!-- Remove the refnamediv (we extracted a shortdesc from it already). -->
<xsl:template match="refnamediv"/>

<!-- Morph the refsynopsisdiv part into a refsyn section. -->
<xsl:template match="refsynopsisdiv">
  <xsl:if test="name(*[1]) != 'cmdsynopsis'"><xsl:message terminate="yes">Expected refsynopsisdiv to start with cmdsynopsis</xsl:message></xsl:if>
  <xsl:if test="title"><xsl:message terminate="yes">No title element supported in refsynopsisdiv</xsl:message></xsl:if>

  <xsl:choose>
    <xsl:when test="$g_fToReference = 'true'">
      <xsl:element name="refsyn">
        <xsl:attribute name="rev">refsynopsisdiv</xsl:attribute>
        <xsl:element name="title">
          <xsl:text>Synopsis</xsl:text>
        </xsl:element>
        <xsl:apply-templates />
      </xsl:element>
    </xsl:when>

    <xsl:otherwise>
      <xsl:element name="topic">
        <xsl:attribute name="rev">refsynopsisdiv</xsl:attribute>
        <!-- <xsl:attribute name="toc">no</xsl:attribute> - topicref only. sigh -->
        <xsl:attribute name="id"><xsl:value-of select="concat(../@id,'-synopsis')"/></xsl:attribute>
        <xsl:element name="title">
          <xsl:text>Synopsis</xsl:text>
        </xsl:element>

        <xsl:element name="body">
          <xsl:apply-templates />
        </xsl:element>
      </xsl:element>
    </xsl:otherwise>

  </xsl:choose>
</xsl:template>

<!-- refsect1 -> section or topic -->
<xsl:template match="refsect1">
  <xsl:if test="not(title)"><xsl:message terminate="yes">refsect1 requires title</xsl:message></xsl:if>

  <xsl:choose>
    <xsl:when test="$g_fToReference = 'true'">
      <xsl:element name="section">
        <xsl:attribute name="rev">refsect1</xsl:attribute>
        <xsl:if test="@id">
          <xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
        </xsl:if>
        <xsl:apply-templates />
      </xsl:element>
    </xsl:when>

    <xsl:otherwise>
      <xsl:element name="topic">
        <xsl:attribute name="rev">refsect1</xsl:attribute>
        <!-- <xsl:attribute name="toc">no</xsl:attribute> - topicref only. sigh -->
        <xsl:attribute name="id">
          <xsl:choose>
            <xsl:when test="@id"><xsl:value-of select="@id"/></xsl:when>
            <xsl:otherwise><xsl:value-of select="concat(../@id, '-no', count(./preceding-sibling::refsect1))"/></xsl:otherwise>
          </xsl:choose>
        </xsl:attribute>

        <xsl:apply-templates select="title"/>

        <!-- Must put cmdsynopsis in a paragraph or it'll get too close to any preceding section title -->
        <xsl:element name="body">
          <xsl:for-each select="node()">
            <xsl:choose>
              <xsl:when test="self::title"/>
              <xsl:when test="self::refsect2"/>
              <xsl:when test="self::cmdsynopsis">
                <xsl:element name="p">
                  <xsl:attribute name="rev">refsect1/cmdsynopsis</xsl:attribute>
                  <xsl:apply-templates select="." />
                </xsl:element>
              </xsl:when>
              <xsl:otherwise>
                <xsl:apply-templates select="." />
              </xsl:otherwise>
            </xsl:choose>
          </xsl:for-each>
        </xsl:element>

        <xsl:apply-templates select="refsect2"/>
      </xsl:element>
    </xsl:otherwise>

  </xsl:choose>
</xsl:template>

<!-- refsect2 -> sectiondiv or topic. -->
<xsl:template match="refsect2">
  <xsl:if test="not(title)"><xsl:message terminate="yes">refsect2 requires title</xsl:message></xsl:if>

  <xsl:choose>
    <xsl:when test="$g_fToReference = 'true'">
      <xsl:element name="sectiondiv">
        <xsl:attribute name="rev">refsect2</xsl:attribute>
        <xsl:attribute name="outputclass">refsect2</xsl:attribute> <!-- how to make xhtml pass these thru... -->
        <xsl:if test="@id">
          <xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
        </xsl:if>

        <xsl:apply-templates />

      </xsl:element>
    </xsl:when>

    <xsl:otherwise>
      <xsl:element name="topic">
        <xsl:attribute name="rev">refsect2</xsl:attribute>
        <!-- <xsl:attribute name="toc">no</xsl:attribute> - topicref only. sigh -->
        <xsl:attribute name="id">
          <xsl:choose>
            <xsl:when test="@id"><xsl:value-of select="@id"/></xsl:when>
            <xsl:otherwise>
              <xsl:if test="not(../@id)">
                <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>both refsect2 and parent refsect1 are missing an @id attribute! Please fix add at least one of these to facilitate proper dita topic splitting.</xsl:message>
              </xsl:if>
              <xsl:value-of select="concat(../@id, '-no', count(./preceding-sibling::refsect2))"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:attribute>

        <xsl:apply-templates select="title"/>

        <xsl:element name="body">
          <xsl:for-each select="node()">
            <xsl:choose>
              <xsl:when test="self::title"/>
              <xsl:when test="self::refsect3"/>
              <xsl:when test="self::cmdsynopsis">
                <xsl:element name="p">
                  <xsl:attribute name="rev">refsect2/cmdsynopsis</xsl:attribute>
                  <xsl:apply-templates select="." />
                </xsl:element>
                <xsl:element name="p">
                  <xsl:attribute name="rev">space hack</xsl:attribute>
                  <xsl:text> </xsl:text>
                </xsl:element>
              </xsl:when>
              <xsl:otherwise>
                <xsl:apply-templates select="." />
              </xsl:otherwise>
            </xsl:choose>
          </xsl:for-each>
        </xsl:element>

        <xsl:apply-templates select="refsect3"/>
      </xsl:element>
    </xsl:otherwise>

  </xsl:choose>
</xsl:template>

<!-- refsect1/title -> title -->
<xsl:template match="refsect1/title">
  <xsl:copy>
    <xsl:apply-templates />
  </xsl:copy>
</xsl:template>

<!-- refsect2/title -> b or title -->
<xsl:template match="refsect2/title">
  <xsl:choose>
    <xsl:when test="$g_fToReference = 'true'">
      <xsl:element name="b">
        <xsl:attribute name="rev">refsect2/title</xsl:attribute>
        <xsl:attribute name="outputclass">refsect2title</xsl:attribute> <!-- how to make xhtml pass these thru... -->
        <xsl:apply-templates />
      </xsl:element>
    </xsl:when>

    <xsl:otherwise>
      <xsl:copy>
        <xsl:apply-templates />
      </xsl:copy>
    </xsl:otherwise>

  </xsl:choose>
</xsl:template>

<!-- para -> p -->
<xsl:template match="para">
  <xsl:element name="p">
    <xsl:attribute name="rev">para</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- note in a section -> note (no change needed) -->
<xsl:template match="refsect1/note | refsect2/note">
  <xsl:copy>
    <xsl:apply-templates />
  </xsl:copy>
</xsl:template>

<!-- variablelist -> parml -->
<xsl:template match="variablelist">
  <xsl:element name="parml">
    <xsl:attribute name="rev">variablelist</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- varlistentry -> plentry -->
<xsl:template match="varlistentry">
  <xsl:element name="plentry">
    <xsl:attribute name="rev">varlistentry</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- term (in varlistentry) -> pt -->
<xsl:template match="varlistentry/term">
  <xsl:element name="pt">
    <xsl:attribute name="rev">term</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- listitem (in varlistentry) -> pd -->
<xsl:template match="varlistentry/listitem">
  <xsl:element name="pd">
    <xsl:attribute name="rev">listitem</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- itemizedlist -> ul -->
<xsl:template match="itemizedlist">
  <xsl:element name="ul">
    <xsl:attribute name="rev">itemizedlist</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- listitem in itemizedlist -> li -->
<xsl:template match="itemizedlist/listitem">
  <xsl:element name="li">
    <xsl:attribute name="rev">listitem</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- orderedlist -> ol -->
<xsl:template match="orderedlist">
  <xsl:element name="ol">
    <xsl:attribute name="rev">orderedlist</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- listitem in orderedlist -> li -->
<xsl:template match="orderedlist/listitem">
  <xsl:element name="li">
    <xsl:attribute name="rev">listitem</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- cmdsynopsis -> syntaxdiagram
     If sbr is used, this gets a bit more complicated... -->
<xsl:template match="cmdsynopsis[not(sbr)]">
  <xsl:element name="syntaxdiagram">
    <xsl:attribute name="rev">cmdsynopsis</xsl:attribute>
    <xsl:if test="@id">
      <xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
    </xsl:if>
    <xsl:choose>
      <xsl:when test="$g_fRenderSyntaxAsText = 'true'">
        <xsl:element name="groupseq">
          <xsl:apply-templates />
        </xsl:element>
      </xsl:when>

      <xsl:otherwise>
        <xsl:apply-templates />
      </xsl:otherwise>
    </xsl:choose>
  </xsl:element>

  <!-- HACK ALERT! Add an empty paragraph to keep syntax diagrams apart in the
       PDF output, otherwise the commands becomes hard to tell apart. -->
  <xsl:if test="position() &lt; last()">
    <xsl:element name="p">
      <xsl:attribute name="platform">ohc</xsl:attribute> <!-- 'och', so it gets filtered out from the html(help) docs. -->
      <xsl:attribute name="rev">pdf space hack</xsl:attribute>
      <xsl:text> </xsl:text>
    </xsl:element>
  </xsl:if>
</xsl:template>

<!-- TODO: sbr cannot be translated, it seems. Whether we wrap things in
     synblk, groupcomp or groupseq elements, the result is always the same:
        - HTML: ignored.
        - PDF: condensed arguments w/o spaces between.  4.0.2 doesn't seem
          to condense stuff any more inside synblk elements, but then the
          rending isn't much changed for PDFs anyway since its one element
          per line.
     Update: Turns out the condensing was because we stripped element
             whitespace instead of preserving it. svn copy. sigh. -->
<xsl:template match="cmdsynopsis[sbr]">
  <xsl:variable name="sWrapperElement">
    <xsl:choose>
      <xsl:when test="$g_fRenderSyntaxAsText = 'true'"><xsl:text>groupseq</xsl:text></xsl:when>
      <xsl:otherwise><!--synblk--></xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:element name="syntaxdiagram">
    <xsl:attribute name="rev">cmdsynopsis</xsl:attribute>
    <xsl:if test="@id">
      <xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
    </xsl:if>
    <xsl:for-each select="sbr">
      <xsl:variable name="idxSbr" select="position()"/>

      <xsl:choose>
        <xsl:when test="$sWrapperElement != ''">
          <xsl:element name="{$sWrapperElement}">
            <xsl:attribute name="rev">sbr/<xsl:value-of select="position()"/></xsl:attribute>

            <xsl:if test="$idxSbr = 1">
              <xsl:apply-templates select="preceding-sibling::node()"/>
            </xsl:if>
            <xsl:if test="$idxSbr != 1">
              <xsl:apply-templates select="preceding-sibling::node()[  count(. | ../sbr[$idxSbr - 1]/following-sibling::node())
                                                                     =     count(../sbr[$idxSbr - 1]/following-sibling::node())]"/>
            </xsl:if>
          </xsl:element>
        </xsl:when>

        <xsl:otherwise>
          <xsl:if test="$idxSbr = 1">
            <xsl:apply-templates select="preceding-sibling::node()"/>
          </xsl:if>
          <xsl:if test="$idxSbr != 1">
            <xsl:apply-templates select="preceding-sibling::node()[  count(. | ../sbr[$idxSbr - 1]/following-sibling::node())
                                                                   =     count(../sbr[$idxSbr - 1]/following-sibling::node())]"/>
          </xsl:if>
        </xsl:otherwise>
      </xsl:choose>

      <!-- Ensure some space between these.-->
      <xsl:text>
 </xsl:text>

      <xsl:if test="$idxSbr = last()">
        <xsl:choose>
          <xsl:when test="$sWrapperElement != ''">
            <xsl:element name="{$sWrapperElement}">
              <xsl:attribute name="rev">sbr/<xsl:value-of select="position()"/></xsl:attribute>
              <xsl:apply-templates select="following-sibling::node()"/>
            </xsl:element>
          </xsl:when>

          <xsl:otherwise>
            <xsl:apply-templates select="following-sibling::node()"/>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:if>
    </xsl:for-each>
  </xsl:element>

  <!-- HACK ALERT! Add an empty paragraph to keep syntax diagrams apart in the
       PDF output, otherwise the commands becomes hard to tell apart. -->
  <xsl:if test="position() &lt; last()">
    <xsl:element name="p">
      <xsl:attribute name="platform">ohc</xsl:attribute> <!-- 'och', so it gets filtered out from the html(help) docs. -->
      <xsl:attribute name="rev">pdf space hack</xsl:attribute>
      <xsl:text> </xsl:text>
    </xsl:element>
  </xsl:if>
</xsl:template>

<!-- text (whitespace) under synopsis may need removing. -->
<xsl:template match="cmdsynopsis/text()">
  <xsl:if test="normalize-space(.) != ''">
    <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>text in cmdsynopsis '<xsl:value-of select="."/>'</xsl:message>
  </xsl:if>

  <xsl:choose>
    <xsl:when test="$g_fRenderSyntaxAsText = 'true'">
    </xsl:when>

    <xsl:otherwise>
      <xsl:value-of select="."/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>


<!-- command with text and/or replaceable in cmdsynopsis -> groupseq + kwd -->
<xsl:template match="cmdsynopsis/command | cmdsynopsis/*/command" >
  <xsl:element name="groupseq">
    <xsl:attribute name="rev">command</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<xsl:template match="cmdsynopsis/command/text() | cmdsynopsis/*/command/text()" >
  <xsl:element name="kwd">
    <xsl:attribute name="rev">command/text</xsl:attribute>
    <xsl:call-template name="emit-text-with-replacements"/>
  </xsl:element>
</xsl:template>

<xsl:template match="cmdsynopsis/command/replaceable | cmdsynopsis/*/command/replaceable" >
  <xsl:call-template name="check-children"/>
  <xsl:element name="var">
    <xsl:attribute name="rev">command/replaceable</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- command with text and/or replaceable in not cmdsynopsis -> userinput + cmdname -->
<xsl:template match="command[not(ancestor::cmdsynopsis)]">
  <xsl:element name="userinput">
    <xsl:attribute name="rev">command</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<xsl:template match="command[not(ancestor::cmdsynopsis)]/text()">
  <xsl:element name="cmdname">
    <xsl:attribute name="rev">command/text</xsl:attribute>
    <xsl:value-of select="."/>
  </xsl:element>
</xsl:template>

<xsl:template match="command[not(ancestor::cmdsynopsis)]/replaceable">
  <xsl:call-template name="check-children"/>
  <xsl:element name="varname">
    <xsl:attribute name="rev">command/replaceable</xsl:attribute>
    <xsl:value-of select="."/>
  </xsl:element>
</xsl:template>

<!--
   arg -> groupseq; A bit complicated though, because text needs to be wrapping
   in 'kwd' and any nested arguments needs explicit 'sep' elements containing a
   space or the nested arguments gets bunched up tight.
   Examples:
      {arg}-output={replaceable}file{/replaceable}{/arg}
    = {groupcomp importance="optional"}{kwd}-output{/kwd}{sep}={/sep}{var}file{/var}{/groupcomp}

      {arg}-output {replaceable}file{/replaceable}{/arg}
    = {groupcomp importance="optional"}{kwd}-output{/kwd}{sep} {/sep}{var}file{/var}{/groupcomp}

      {arg}-R {arg}-L{/arg}{/arg}
    = {groupseq importance="optional"}{groupcomp}{kwd}-R{/groupcomp}{sep} {/sep}
   or {groupseq importance="optional"}{kwd}-R{sep} {/sep}{groupcomp}{kwd}-L{/groupcomp}{/groupseq}
      note: Important to specify {sep} here as whitespace might otherwise be squashed.
-->

<!-- Plaintext within arg is generally translated to kwd, but value separators
     like '=' and ',' should be wrapped in a delim element. -->
<xsl:template match="arg/text()">
  <xsl:choose>
    <!-- put trailing '=' inside <sep> -->
    <xsl:when test="substring(., string-length(.)) = '='">
      <xsl:element name="kwd">
        <xsl:attribute name="rev">arg=</xsl:attribute>
        <xsl:call-template name="emit-text-with-replacements">
          <xsl:with-param name="a_sText" select="substring(., 1, string-length(.) - 1)"/>
        </xsl:call-template>
      </xsl:element>
      <xsl:element name="delim">
        <xsl:attribute name="rev">arg=</xsl:attribute>
        <xsl:text>=</xsl:text>
      </xsl:element>
    </xsl:when>

    <!-- Special case, single space, assuming it's deliberate so put in inside a sep element. -->
    <xsl:when test=". = ' '">
      <xsl:element name="sep">
        <xsl:attribute name="rev">arg-space</xsl:attribute>
        <xsl:text> </xsl:text>
      </xsl:element>
    </xsl:when>

    <!-- Don't wrap other pure whitespace kwd sequences, but emit single space 'sep'
         element if a arg or groups follows.  If the whitespace includes a newline
         we'll emit it, but otherways we'll generally suppress it to avoid
         accidentally padding spaces between arguments.  -->
    <xsl:when test="normalize-space(.) = ''">
      <xsl:if test="following::*[position() = 1 and (self::arg or self::group)] and not(ancestor-or-self::*[@role='compact'])">
        <xsl:element name="sep">
          <xsl:attribute name="rev">arg-whitespace</xsl:attribute>
          <xsl:text> </xsl:text>
        </xsl:element>
      </xsl:if>
      <xsl:if test="contains(., '&#10;') and $g_fRenderSyntaxAsText != 'true'">
        <xsl:value-of select="."/>
      </xsl:if>
    </xsl:when>

    <!-- Remainder is all wrapped in kwd, after space normalization. -->
    <xsl:otherwise>
      <xsl:element name="kwd">
        <xsl:attribute name="rev">arg</xsl:attribute>
        <xsl:call-template name="emit-text-with-replacements">
          <xsl:with-param name="a_sText" select="normalize-space(.)"/>
        </xsl:call-template>
      </xsl:element>
      <xsl:if test="normalize-space(substring(., string-length(.), 1)) = ''
                and following::*[position() = 1 and (self::arg or self::group)]
                and not(ancestor-or-self::*[@role='compact'])">
        <xsl:element name="sep">
          <xsl:attribute name="rev">arg-trailing</xsl:attribute>
          <xsl:text> </xsl:text>
        </xsl:element>
      </xsl:if>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<!-- arg -> groupseq or groupcomp and optionally a repsep element if repeatable. -->
<xsl:template match="arg" >
  <xsl:choose>
    <xsl:when test="$g_fRenderSyntaxAsText = 'true'">
      <xsl:call-template name="arg_or_group_as_text"/>
    </xsl:when>

    <xsl:otherwise>
      <!-- If it's a tighly packed arg, we use groupcomp instead of groupseq to try
           avoid it being split in the middle. -->
      <xsl:variable name="sGroupType">
        <xsl:call-template name="determine_arg_wrapper_element"/>
      </xsl:variable>
      <xsl:element name="{$sGroupType}">
        <xsl:attribute name="rev">arg[<xsl:value-of select="concat(@choice,',',@rep)"/>]</xsl:attribute>
        <xsl:choose>
          <xsl:when test="not(@choice) or @choice = 'opt'">
            <xsl:attribute name="importance">optional</xsl:attribute>
          </xsl:when>
          <xsl:when test="@choice = 'req'">
            <xsl:attribute name="importance">required</xsl:attribute>
          </xsl:when>
          <xsl:when test="@choice = 'plain'"/>
          <xsl:otherwise>
            <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>Unexpected @choice value: <xsl:value-of select="@choice"/></xsl:message>
          </xsl:otherwise>
        </xsl:choose>

        <xsl:apply-templates />

        <xsl:if test="@rep = 'repeat'">
          <!-- repsep can only be placed at the start of a groupseq/whatever and
               the documenation and examples of the element is very sparse.  The
               PDF output plugin will place the '...' where it finds it and do
               nothing if it's empty.  The XHTML output plugin ignores it, it seems. -->
          <xsl:element name="sep">
            <xsl:attribute name="rev">arg[<xsl:value-of select="@choice"/>,repeat]</xsl:attribute>
            <xsl:text> </xsl:text>
          </xsl:element>
          <xsl:element name="groupcomp">
            <xsl:attribute name="importance">optional</xsl:attribute>
            <xsl:attribute name="rev">arg[<xsl:value-of select="@choice"/>,repeat]</xsl:attribute>
            <xsl:attribute name="outputclass">repeatarg</xsl:attribute> <!-- how to make xhtml pass these thru... -->
            <xsl:element name="repsep">
              <xsl:attribute name="rev">arg[<xsl:value-of select="@choice"/>,repeat]</xsl:attribute>
              <xsl:text>...</xsl:text>
            </xsl:element>
          </xsl:element>
        </xsl:if>

      </xsl:element>
    </xsl:otherwise>
  </xsl:choose>

  <xsl:if test="parent::group and @choice != 'plain'">
    <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>Expected arg in group to be plain, not optional.</xsl:message>
  </xsl:if>
</xsl:template>

<xsl:template name="determine_arg_wrapper_element">
  <xsl:choose>
    <xsl:when test="not(descendant::group) and not(descendant::text()[contains(.,' ') or normalize-space(.) != .])">
      <xsl:text>groupcomp</xsl:text>
    </xsl:when>
    <xsl:otherwise>
      <xsl:text>groupseq</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<!-- replaceable under arg -> var -->
<xsl:template match="arg/replaceable" >
  <xsl:element name="var">
    <xsl:attribute name="rev">replaceable</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- replaceable in para or term -> synph+var -->
<xsl:template match="para/replaceable | term/replaceable | screen/replaceable" >
  <xsl:element name="synph">
    <xsl:attribute name="rev">replaceable</xsl:attribute>
    <xsl:element name="var">
      <xsl:attribute name="rev">replaceable</xsl:attribute>
      <xsl:apply-templates />
    </xsl:element>
  </xsl:element>
</xsl:template>

<!-- replaceable in option -> var -->
<xsl:template match="option/replaceable" >
  <xsl:element name="var">
    <xsl:attribute name="rev">option/replaceable</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- replaceable in computeroutput or filename -> varname -->
<xsl:template match="computeroutput/replaceable | filename/replaceable" >
  <xsl:element name="varname">
    <xsl:attribute name="rev">computeroutput/replaceable</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- replaceable/text() in a cmdsynopsis should have hyphens replaced. -->
<xsl:template match="replaceable/text()[ancestor::cmdsynopsis]" >
  <xsl:call-template name="emit-text-with-replacements"/>
</xsl:template>

<!--
    DocBook 'group' elements are only ever used for multiple choice options
    in our refentry XML, it is never used for argument groupings.  For
    grouping arguments we use nested 'arg' elements.

    This is because 'group' with 'group' parent is poorly defned/handled.
    Whether the DocBook HTML formatters uses ' | ' separators depends on what
    other elements are in the group and their order. arg1+group2+group3 won't
    get any, but group1+arg2+group3 will get one between the first two.
-->

<xsl:template match="group[group]" priority="3.0">
  <xsl:message terminate="yes">
    <xsl:call-template name="error-prefix"/>Immediate group nesting is not allowed! Put nested group inside arg element.
  </xsl:message>
</xsl:template>

<xsl:template match="group[count(arg) &lt; 2]" priority="3.0">
  <xsl:message terminate="yes">
    <xsl:call-template name="error-prefix"/>Group with fewer than two 'arg' elements is not allowed!
  </xsl:message>
</xsl:template>

<!-- group -> groupchoice w/attrib -->
<xsl:template match="group">
  <xsl:choose>
    <xsl:when test="$g_fRenderSyntaxAsText = 'true'">
      <xsl:call-template name="arg_or_group_as_text"/>
    </xsl:when>
    <xsl:otherwise>

      <xsl:element name="groupchoice">
        <xsl:choose>
          <xsl:when test="@choice = 'req'">
            <xsl:attribute name="rev">group[req]</xsl:attribute>
            <xsl:attribute name="importance">required</xsl:attribute>
          </xsl:when>
          <xsl:when test="@choice = 'plain'">
            <xsl:attribute name="rev">group[plain]</xsl:attribute>
            <!-- We don't set the importance here. @todo Check what it does to the output formatting -->
          </xsl:when>
          <xsl:otherwise>
            <xsl:attribute name="rev">group[opt]</xsl:attribute>
            <xsl:attribute name="importance">optional</xsl:attribute>
          </xsl:otherwise>
        </xsl:choose>

        <xsl:apply-templates />

        <xsl:if test="@rep = 'repeat'">
          <!-- repsep can only be placed at the start of a groupseq/whatever and
               the documenation and examples of the element is very sparse.  The
               PDF output plugin will place the '...' where it finds it and do
               nothing if it's empty.  The XHTML output plugin ignores it, it seems. -->
          <xsl:message terminate="no"><xsl:call-template name="error-prefix"/>Repeating group is not a good idea...</xsl:message>
          <xsl:element name="sep">
            <xsl:attribute name="rev">arg[<xsl:value-of select="@choice"/>,repeat]</xsl:attribute>
            <xsl:text> </xsl:text>
          </xsl:element>
          <xsl:element name="groupcomp">
            <xsl:attribute name="importance">optional</xsl:attribute>
            <xsl:attribute name="rev">arg[<xsl:value-of select="@choice"/>,repeat]</xsl:attribute>
            <xsl:attribute name="outputclass">repeatarg</xsl:attribute> <!-- how to make xhtml pass these thru... -->
            <xsl:element name="repsep">
              <xsl:attribute name="rev">arg[<xsl:value-of select="@choice"/>,repeat]</xsl:attribute>
              <xsl:text>...</xsl:text>
            </xsl:element>
          </xsl:element>
        </xsl:if>
      </xsl:element>

    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<!-- text under a group may need removing. -->
<xsl:template match="group/text()">
  <xsl:if test="normalize-space(.) != ''">
    <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>text in group: '<xsl:value-of select="."/>'</xsl:message>
  </xsl:if>

  <xsl:choose>
    <xsl:when test="$g_fRenderSyntaxAsText = 'true'">
    </xsl:when>

    <xsl:otherwise>
      <xsl:value-of select="."/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>


<!--
   arg or group -> kwd + text
  (Code duplicated in docbook-refentry-to-C-help.xsl & docbook2latex.xsl, with local differences.)
-->
<xsl:template name="arg_or_group_as_text" >
  <xsl:variable name="fWrappers" select="not(ancestor::group)"/>

  <!-- lead separators -->
  <xsl:variable name="sLeadSeps">
    <xsl:call-template name="arg_or_group_as_text_calc_lead_seps">
      <xsl:with-param name="a_fWrappers" select="$fWrappers"/>
    </xsl:call-template>
  </xsl:variable>
  <xsl:if test="$sLeadSeps != ''">
    <xsl:element name="sep">
      <xsl:attribute name="rev"><xsl:value-of select="concat(local-name(.), '-leadseps')"/></xsl:attribute>
      <xsl:value-of select="$sLeadSeps"/>
    </xsl:element>
  </xsl:if>

  <!-- render the arg (TODO: may need to do more work here) -->
  <xsl:apply-templates />

  <!-- repeat wrapping -->
  <xsl:choose>
    <xsl:when test="@rep = 'norepeat' or not(@rep) or @rep = ''"/>
    <xsl:when test="@rep = 'repeat'">                 <xsl:element name="sep"><xsl:value-of select="$arg.rep.repeat.str"/></xsl:element></xsl:when>
    <xsl:otherwise><xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>Invalid rep choice: "<xsl:value-of select="@rep"/>"</xsl:message></xsl:otherwise>
  </xsl:choose>

  <!-- close wrapping -->
  <xsl:if test="$fWrappers">
    <xsl:choose>
      <xsl:when test="not(@choice) or @choice = ''">  <xsl:element name="sep"><xsl:value-of select="$arg.choice.def.close.str"/></xsl:element></xsl:when>
      <xsl:when test="@choice = 'opt'">               <xsl:element name="sep"><xsl:value-of select="$arg.choice.opt.close.str"/></xsl:element></xsl:when>
      <xsl:when test="@choice = 'req'">               <xsl:element name="sep"><xsl:value-of select="$arg.choice.req.close.str"/></xsl:element></xsl:when>
    </xsl:choose>
    <!-- Add a space padding if we're the last element in a repeating arg or group -->
    <!-- 2023-03-22 bird: This is incorrectly written. Fix as needed...
    <xsl:if test="(parent::arg or parent::group) and not(following-sibiling) and not(ancestor::*[@role='compact'])">
      <xsl:text> </xsl:text>
    </xsl:if>
    -->
  </xsl:if>

</xsl:template>

<!-- Helper for arg_or_group_as_text. -->
<xsl:template name="arg_or_group_as_text_calc_lead_seps">
  <xsl:param name="a_fWrappers"/>
  <xsl:variable name="idSelf" select="generate-id(.)"/>

  <!-- separator char if we're not the first child -->
  <xsl:if test="../*[generate-id(.) = $idSelf and position() > 1]">
    <!--<xsl:value-of select="concat('*',name(),'=', position(),'#')"/>-->
    <xsl:choose>
      <xsl:when test="parent::group and ancestor::*[@role='compact']"><xsl:value-of select="$arg.or.sep.compact"/></xsl:when>
      <xsl:when test="parent::group"><xsl:value-of select="$arg.or.sep"/></xsl:when>
      <xsl:when test="ancestor::*[@role='compact']"></xsl:when>
      <xsl:when test="ancestor::*/@sepchar"><xsl:value-of select="ancestor::*/@sepchar"/></xsl:when>
      <xsl:otherwise><xsl:text> </xsl:text></xsl:otherwise>
    </xsl:choose>
  </xsl:if>

  <!-- open wrapping -->
  <xsl:if test="$a_fWrappers">
    <xsl:choose>
      <xsl:when test="not(@choice) or @choice = ''">  <xsl:value-of select="$arg.choice.def.open.str"/></xsl:when>
      <xsl:when test="@choice = 'opt'">               <xsl:value-of select="$arg.choice.opt.open.str"/></xsl:when>
      <xsl:when test="@choice = 'req'">               <xsl:value-of select="$arg.choice.req.open.str"/></xsl:when>
      <xsl:when test="@choice = 'plain'"/>
      <xsl:otherwise><xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>Invalid arg choice: "<xsl:value-of select="@choice"/>"</xsl:message></xsl:otherwise>
    </xsl:choose>
  </xsl:if>

</xsl:template>


<!-- option -->
<xsl:template match="option/text()" >
  <xsl:element name="kwd">
    <xsl:attribute name="rev">option</xsl:attribute>
    <xsl:value-of select="."/>
  </xsl:element>
</xsl:template>

<xsl:template match="option" >
  <xsl:element name="synph">
    <xsl:attribute name="rev">option</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- literal w/o sub-elements -> codeph -->
<xsl:template match="literal[not(*)]" >
  <xsl:element name="codeph">
    <xsl:attribute name="rev">literal</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- literal with replaceable sub-elements -> synph -->
<xsl:template match="literal[replaceable]" >
  <xsl:element name="synph">
    <xsl:attribute name="rev">literal/replaceable</xsl:attribute>
    <xsl:for-each select="node()">
      <xsl:choose>
        <xsl:when test="self::text()">
          <xsl:element name="kwd">
            <xsl:value-of select="."/>
          </xsl:element>
        </xsl:when>
        <xsl:when test="self::replaceable">
          <xsl:if test="./*">
            <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>Unexpected literal/replaceable child</xsl:message>
          </xsl:if>
          <xsl:element name="var">
            <xsl:value-of select="."/>
          </xsl:element>
        </xsl:when>
        <xsl:otherwise>
          <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>Unexpected literal child:
            <xsl:value-of select="name(.)" />
          </xsl:message>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:for-each>
  </xsl:element>
</xsl:template>

<!-- filename -> filepath -->
<xsl:template match="filename" >
  <xsl:element name="filepath">
    <xsl:attribute name="rev">filename</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- screen - pass thru -->
<xsl:template match="screen" >
  <xsl:copy>
    <xsl:apply-templates />
  </xsl:copy>
</xsl:template>

<!-- computeroutput -> systemoutput-->
<xsl:template match="computeroutput">
  <xsl:element name="systemoutput">
    <xsl:attribute name="rev">computeroutput</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- xref -> xref, but attributes differ. -->
<xsl:template match="xref">
  <xsl:element name="xref">
    <xsl:attribute name="href"><xsl:value-of select="@linkend"/></xsl:attribute>
    <xsl:if test="contains(@linkend, 'http')"><xsl:message terminate="yes">xref/linkend with http</xsl:message></xsl:if>
  </xsl:element>
</xsl:template>

<!-- ulink -> xref -->
<xsl:template match="ulink">
  <xsl:element name="xref">
    <xsl:attribute name="rev">ulink</xsl:attribute>
    <xsl:attribute name="scope">external</xsl:attribute> <!-- Just assumes this is external. -->
    <xsl:attribute name="href"><xsl:value-of select="@url"/></xsl:attribute>
    <xsl:attribute name="format">html</xsl:attribute>
    <xsl:if test="not(starts-with(@url, 'http'))"><xsl:message terminate="yes">ulink url is not http: <xsl:value-of select="@url"/></xsl:message></xsl:if>
  </xsl:element>
</xsl:template>

<!-- emphasis -> i -->
<xsl:template match="emphasis">
  <xsl:if test="*">
    <xsl:message terminate="yes"><xsl:call-template name="error-prefix"/>Did not expect emphasis to have children!</xsl:message>
  </xsl:if>
  <xsl:element name="i">
    <xsl:attribute name="rev">emphasis</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!-- note -> note -->
<xsl:template match="note">
  <xsl:copy>
    <xsl:apply-templates />
  </xsl:copy>
</xsl:template>

<!-- citetitle -> cite -->
<xsl:template match="citetitle">
  <xsl:element name="cite">
    <xsl:attribute name="rev">citetitle</xsl:attribute>
    <xsl:apply-templates />
  </xsl:element>
</xsl:template>

<!--
 remark extensions:
 -->
<!-- Default: remove all remarks. -->
<xsl:template match="remark"/>


<!--
 Captializes the given text.
 -->
<xsl:template name="capitalize">
  <xsl:param name="text"/>
  <xsl:call-template name="str:to-upper">
    <xsl:with-param name="text" select="substring($text,1,1)"/>
  </xsl:call-template>
  <xsl:value-of select="substring($text,2)"/>
</xsl:template>


<!--
 Maybe replace hyphens (dashes) with non-breaking ones.
 -->
<xsl:template name="emit-text-with-replacements">
  <xsl:param name="a_sText" select="."/>
  <xsl:choose>
    <xsl:when test="$g_fReplaceHyphens = 'true'">
      <xsl:call-template name="str:subst">
          <xsl:with-param name="text" select="$a_sText"/>
          <xsl:with-param name="replace">-</xsl:with-param>
          <xsl:with-param name="with">‑</xsl:with-param> <!-- U+2011 / &#8209; -->
      </xsl:call-template>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$a_sText"/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>


<!--
  Debug/Diagnostics: Return the path to the specified node (by default the current).
  -->
<xsl:template name="get-node-path">
  <xsl:param name="Node" select="."/>
  <xsl:for-each select="$Node">
    <xsl:for-each select="ancestor-or-self::node()">
      <xsl:choose>
        <xsl:when test="name(.) = ''">
          <xsl:value-of select="concat('/text(',')')"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="concat('/', name(.))"/>
          <xsl:choose>
            <xsl:when test="@id">
              <xsl:text>[@id=</xsl:text>
              <xsl:value-of select="@id"/>
              <xsl:text>]</xsl:text>
            </xsl:when>
            <xsl:otherwise>
              <!-- Use generate-id() to find the current node position among its siblings. -->
              <xsl:variable name="id" select="generate-id(.)"/>
              <xsl:for-each select="../node()">
                <xsl:if test="generate-id(.) = $id">
                  <xsl:text>[</xsl:text><xsl:value-of select="position()"/><xsl:text>]</xsl:text>
                </xsl:if>
              </xsl:for-each>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:for-each>
  </xsl:for-each>
</xsl:template>

<!--
  Debug/Diagnostics: Return error message prefix.
  -->
<xsl:template name="error-prefix">
  <xsl:param name="Node" select="."/>
  <xsl:text>error: </xsl:text>
  <xsl:call-template name="get-node-path">
    <xsl:with-param name="Node" select="$Node"/>
  </xsl:call-template>
  <xsl:text>: </xsl:text>
</xsl:template>

<!--
  Debug/Diagnostics: Print list of nodes (by default all children of current node).
  -->
<xsl:template name="list-nodes">
  <xsl:param name="Nodes" select="node()"/>
  <xsl:for-each select="$Nodes">
    <xsl:if test="position() != 1">
      <xsl:text>, </xsl:text>
    </xsl:if>
    <xsl:choose>
      <xsl:when test="name(.) = ''">
        <xsl:text>text:text()</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="name(.)"/>
        <xsl:if test="@id">
          <xsl:text>[@id=</xsl:text>
          <xsl:value-of select="@id"/>
          <xsl:text>]</xsl:text>
        </xsl:if>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:for-each>
</xsl:template>

<xsl:template name="check-children">
  <xsl:param name="Node"             select="."/>
  <xsl:param name="UnsupportedNodes" select="*"/>
  <xsl:param name="SupportedNames"   select="'none'"/>
  <xsl:if test="count($UnsupportedNodes) != 0">
    <xsl:message terminate="yes">
      <xsl:call-template name="get-node-path">
        <xsl:with-param name="Node" select="$Node"/>
      </xsl:call-template>
      <!-- -->: error: Only <xsl:value-of select="$SupportedNames"/> are supported as children to <!-- -->
      <xsl:value-of select="name($Node)"/>
      <!-- -->
Unsupported children: <!-- -->
      <xsl:call-template name="list-nodes">
        <xsl:with-param name="Nodes" select="$UnsupportedNodes"/>
      </xsl:call-template>
    </xsl:message>
  </xsl:if>
</xsl:template>

</xsl:stylesheet>

