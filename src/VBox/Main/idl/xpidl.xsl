<?xml version="1.0"?>
<!-- $Id: xpidl.xsl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ -->

<!--
 *  A template to generate a XPCOM IDL compatible interface definition file
 *  from the generic interface definition expressed in XML.
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

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text"/>

<xsl:strip-space elements="*"/>

<xsl:include href="typemap-shared.inc.xsl"/>


<!-- - - - - - - - - - - - - - - - - - - - - - -
   XSLT parameters
 - - - - - - - - - - - - - - - - - - - - - - -->

<!-- xpidl doesn't support enums. This parameter performs certain hacks that helps
     us bridge the gap and get similar behaviour as msidl.

     The https://bugzilla.mozilla.org/show_bug.cgi?id=8781 bug discusses why xpidl
     doesn't do enums.  It boils down to the gcc short-enum option and similar
     portability concerns.
 -->
<xsl:param name="g_fHackEnumsOntoCppEnums" select="'yes'"/>


<!--
//  templates
/////////////////////////////////////////////////////////////////////////////
-->


<!--
 *  not explicitly matched elements and attributes
-->
<xsl:template match="*"/>


<!--
 *  header
-->
<xsl:template match="/idl">
  <xsl:text>
/*
 *  DO NOT EDIT! This is a generated file.
 *
 *  XPCOM IDL (XPIDL) definition for VirtualBox Main API (COM interfaces)
 *  generated from XIDL (XML interface definition).
 *
 *  Source    : src/VBox/Main/idl/VirtualBox.xidl
 *  Generator : src/VBox/Main/idl/xpidl.xsl
 */

#include "nsISupports.idl"
#include "nsIException.idl"

</xsl:text>
  <!-- native typedefs for the 'mod="ptr"' attribute -->
  <xsl:text>
[ptr] native booleanPtr (PRBool);
[ptr] native octetPtr   (PRUint8);
[ptr] native shortPtr   (PRInt16);
[ptr] native ushortPtr  (PRUint16);
[ptr] native longPtr    (PRInt32);
[ptr] native llongPtr   (PRInt64);
[ptr] native ulongPtr   (PRUint32);
[ptr] native ullongPtr  (PRUint64);
<!-- charPtr is already defined in nsrootidl.idl -->
<!-- [ptr] native charPtr    (char) -->
[ptr] native stringPtr  (string);
[ptr] native wcharPtr   (wchar);
[ptr] native wstringPtr (wstring);

</xsl:text>
  <xsl:apply-templates/>
</xsl:template>


<!--
 *  ignore all |if|s except those for XPIDL target
-->
<xsl:template match="if">
  <xsl:if test="@target='xpidl'">
    <xsl:apply-templates/>
  </xsl:if>
</xsl:template>
<xsl:template match="if" mode="forward">
  <xsl:if test="@target='xpidl'">
    <xsl:apply-templates mode="forward"/>
  </xsl:if>
</xsl:template>
<xsl:template match="if" mode="forwarder">
  <xsl:if test="@target='midl'">
    <xsl:apply-templates mode="forwarder"/>
  </xsl:if>
</xsl:template>


<!--
 *  cpp_quote
-->
<xsl:template match="cpp">
  <xsl:if test="text()">
    <xsl:text>%{C++</xsl:text>
    <xsl:value-of select="text()"/>
    <xsl:text>&#x0A;%}&#x0A;&#x0A;</xsl:text>
  </xsl:if>
  <xsl:if test="not(text()) and @line">
    <xsl:text>%{C++&#x0A;</xsl:text>
    <xsl:value-of select="@line"/>
    <xsl:text>&#x0A;%}&#x0A;&#x0A;</xsl:text>
  </xsl:if>
</xsl:template>


<!--
 *  #if statement (@if attribute)
 *  @note
 *      xpidl doesn't support any preprocessor defines other than #include
 *      (it just ignores them), so the generated IDL will most likely be
 *      invalid. So for now we forbid using @if attributes
-->
<xsl:template match="@if" mode="begin">
  <xsl:message terminate="yes">
    @if attributes are not currently allowed because xpidl lacks
    support for #ifdef and stuff.
  </xsl:message>
  <xsl:text>#if </xsl:text>
  <xsl:value-of select="."/>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>
<xsl:template match="@if" mode="end">
  <xsl:text>#endif&#x0A;</xsl:text>
</xsl:template>


<!--
 *  libraries
-->
<xsl:template match="library">
  <xsl:text>%{C++&#x0A;</xsl:text>
  <xsl:text>#ifndef VBOX_EXTERN_C&#x0A;</xsl:text>
  <xsl:text># ifdef __cplusplus&#x0A;</xsl:text>
  <xsl:text>#  define VBOX_EXTERN_C extern "C"&#x0A;</xsl:text>
  <xsl:text># else // !__cplusplus&#x0A;</xsl:text>
  <xsl:text>#  define VBOX_EXTERN_C extern&#x0A;</xsl:text>
  <xsl:text># endif // !__cplusplus&#x0A;</xsl:text>
  <xsl:text>#endif // !VBOX_EXTERN_C&#x0A;</xsl:text>
  <!-- result codes -->
  <xsl:text>// result codes declared in API spec&#x0A;</xsl:text>
  <xsl:for-each select="application/result">
    <xsl:apply-templates select="."/>
  </xsl:for-each>
  <xsl:text>%}&#x0A;&#x0A;</xsl:text>
  <!-- forward declarations -->
  <xsl:apply-templates select="application/if | application/interface" mode="forward"/>
  <xsl:text>&#x0A;</xsl:text>
  <!-- all enums go first -->
  <xsl:apply-templates select="application/enum | application/if/enum"/>
  <!-- everything else but result codes and enums
  <xsl:apply-templates select="*[not(self::application/result or self::application/enum) and
                                 not(self::application[result] or self::application/if[enum])]"/> -->
  <!-- the modules (i.e. everything else) -->
  <xsl:apply-templates select="application/interface | application/if[interface]
                                   | application/module | application/if[module]"/>
  <!-- -->
</xsl:template>


 <!--
 *  applications
-->
<xsl:template match="application">
  <xsl:apply-templates/>
</xsl:template>
<xsl:template match="application" mode="forward">
  <xsl:apply-templates mode="forward"/>
</xsl:template>

<!--
 *  result codes
-->
<xsl:template match="result">
  <xsl:value-of select="concat('#define ',@name,' ((nsresult)',@value, ')')"/>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>


<!--
 *  forward declarations
-->
<xsl:template match="interface" mode="forward">
  <xsl:text>interface </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>;&#x0A;</xsl:text>
</xsl:template>


<!--
 *  interfaces
-->
<xsl:template match="interface">[
    uuid(<xsl:value-of select="@uuid"/>),
    scriptable
]
<xsl:text>interface </xsl:text>
  <xsl:variable name="name" select="@name"/>
  <xsl:value-of select="$name"/>
  <xsl:text> : </xsl:text>
  <xsl:choose>
      <xsl:when test="@extends='$unknown'">nsISupports</xsl:when>
      <xsl:when test="@extends='$errorinfo'">nsIException</xsl:when>
      <xsl:otherwise><xsl:value-of select="@extends"/></xsl:otherwise>
  </xsl:choose>
  <xsl:call-template name="xsltprocNewlineOutputHack"/>
  <xsl:text>{&#x0A;</xsl:text>
  <!-- attributes (properties) -->
  <xsl:apply-templates select="attribute"/>
  <xsl:variable name="reservedAttributes" select="@reservedAttributes"/>
  <xsl:if test="$reservedAttributes > 0">
    <!-- tricky way to do a "for" loop without recursion -->
    <xsl:for-each select="(//*)[position() &lt;= $reservedAttributes]">
      <xsl:text>    readonly attribute unsigned long InternalAndReservedAttribute</xsl:text>
      <xsl:value-of select="concat(position(), $name)"/>
      <xsl:text>;&#x0A;&#x0A;</xsl:text>
    </xsl:for-each>
  </xsl:if>
  <!-- methods -->
  <xsl:apply-templates select="method"/>
  <xsl:variable name="reservedMethods" select="@reservedMethods"/>
  <xsl:if test="$reservedMethods > 0">
    <!-- tricky way to do a "for" loop without recursion -->
    <xsl:for-each select="(//*)[position() &lt;= $reservedMethods]">
      <xsl:text>    void InternalAndReservedMethod</xsl:text>
      <xsl:value-of select="concat(position(), $name)"/>
      <xsl:text>();&#x0A;&#x0A;</xsl:text>
    </xsl:for-each>
  </xsl:if>
  <!-- 'if' enclosed elements, unsorted -->
  <xsl:apply-templates select="if"/>
  <!-- -->
  <xsl:text>}; /* interface </xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text> */&#x0A;&#x0A;</xsl:text>
  <!-- Interface implementation forwarder macro -->
  <xsl:text>/* Interface implementation forwarder macro */&#x0A;</xsl:text>
  <xsl:text>%{C++&#x0A;</xsl:text>
  <!-- 1) individual methods -->
  <xsl:apply-templates select="attribute" mode="forwarder"/>
  <xsl:apply-templates select="method" mode="forwarder"/>
  <xsl:apply-templates select="if" mode="forwarder"/>
  <!-- 2) COM_FORWARD_Interface_TO(smth) -->
  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO(smth) NS_FORWARD_</xsl:text>
  <xsl:call-template name="string-to-upper">
    <xsl:with-param name="str" select="$name"/>
  </xsl:call-template>
  <xsl:text> (smth)&#x0A;</xsl:text>
  <!-- 3) COM_FORWARD_Interface_TO_OBJ(obj) -->
  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO_OBJ(obj) COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO ((obj)->)&#x0A;</xsl:text>
  <!-- 4) COM_FORWARD_Interface_TO_BASE(base) -->
  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO_BASE(base) COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO (base::)&#x0A;&#x0A;</xsl:text>
  <!-- -->
  <xsl:text>// for compatibility with Win32&#x0A;</xsl:text>
  <xsl:text>VBOX_EXTERN_C const nsID IID_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>;&#x0A;</xsl:text>
  <xsl:text>%}&#x0A;&#x0A;</xsl:text>
  <!-- end -->
</xsl:template>


<!--
 *  attributes
-->
<xsl:template match="interface//attribute">
  <xsl:apply-templates select="@if" mode="begin"/>
  <xsl:if test="@mod='ptr'">
    <!-- attributes using native types must be non-scriptable -->
    <xsl:text>    [noscript]&#x0A;</xsl:text>
  </xsl:if>
  <xsl:choose>
    <!-- safearray pseudo attribute -->
    <xsl:when test="@safearray='yes'">
      <!-- getter -->
      <xsl:text>    void get</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text> (&#x0A;</xsl:text>
      <!-- array size -->
      <xsl:text>        out unsigned long </xsl:text>
      <xsl:value-of select="@name"/>
      <xsl:text>Size,&#x0A;</xsl:text>
      <!-- array pointer -->
      <xsl:text>        [array, size_is(</xsl:text>
      <xsl:value-of select="@name"/>
      <xsl:text>Size), retval] out </xsl:text>
      <xsl:apply-templates select="@type"/>
      <xsl:text> </xsl:text>
      <xsl:value-of select="@name"/>
      <xsl:text>&#x0A;    );&#x0A;</xsl:text>
      <!-- setter -->
      <xsl:if test="not(@readonly='yes')">
        <xsl:text>    void set</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text> (&#x0A;</xsl:text>
        <!-- array size -->
        <xsl:text>        in unsigned long </xsl:text>
        <xsl:value-of select="@name"/>
        <xsl:text>Size,&#x0A;</xsl:text>
        <!-- array pointer -->
        <xsl:text>        [array, size_is(</xsl:text>
        <xsl:value-of select="@name"/>
        <xsl:text>Size)] in </xsl:text>
        <xsl:apply-templates select="@type"/>
        <xsl:text> </xsl:text>
        <xsl:value-of select="@name"/>
        <xsl:text>&#x0A;    );&#x0A;</xsl:text>
      </xsl:if>
    </xsl:when>
    <!-- normal attribute -->
    <xsl:otherwise>
      <xsl:text>    </xsl:text>
      <xsl:if test="@readonly='yes'">
        <xsl:text>readonly </xsl:text>
      </xsl:if>
      <xsl:text>attribute </xsl:text>
      <xsl:apply-templates select="@type"/>
      <xsl:text> </xsl:text>
      <xsl:value-of select="@name"/>
      <xsl:text>;&#x0A;</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:apply-templates select="@if" mode="end"/>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template match="interface//attribute" mode="forwarder">

  <xsl:variable name="parent" select="ancestor::interface"/>

  <xsl:apply-templates select="@if" mode="begin"/>

  <!-- getter: COM_FORWARD_Interface_GETTER_Name_TO(smth) -->
  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_GETTER_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO(smth) NS_IMETHOD Get</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text> (</xsl:text>
  <xsl:if test="@safearray='yes'">
    <xsl:text>PRUint32 * a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>Size, </xsl:text>
  </xsl:if>
  <xsl:apply-templates select="@type" mode="forwarder"/>
  <xsl:if test="@safearray='yes'">
    <xsl:text> *</xsl:text>
  </xsl:if>
  <xsl:text> * a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>) { return smth Get</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text> (</xsl:text>
  <xsl:if test="@safearray='yes'">
    <xsl:text>a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>Size, </xsl:text>
  </xsl:if>
  <xsl:text>a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>); }&#x0A;</xsl:text>
  <!-- getter: COM_FORWARD_Interface_GETTER_Name_TO_OBJ(obj) -->
  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_GETTER_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO_OBJ(obj) COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_GETTER_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO ((obj)->)&#x0A;</xsl:text>
  <!-- getter: COM_FORWARD_Interface_GETTER_Name_TO_BASE(base) -->
  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_GETTER_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO_BASE(base) COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_GETTER_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO (base::)&#x0A;</xsl:text>
  <!-- -->
  <xsl:if test="not(@readonly='yes')">
    <!-- setter: COM_FORWARD_Interface_SETTER_Name_TO(smth) -->
    <xsl:text>#define COM_FORWARD_</xsl:text>
    <xsl:value-of select="$parent/@name"/>
    <xsl:text>_SETTER_</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>_TO(smth) NS_IMETHOD Set</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text> (</xsl:text>
    <xsl:if test="@safearray='yes'">
      <xsl:text>PRUint32 a</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>Size, </xsl:text>
    </xsl:if>
    <xsl:if test="not(@safearray='yes') and (@type='string' or @type='wstring')">
      <xsl:text>const </xsl:text>
    </xsl:if>
    <xsl:apply-templates select="@type" mode="forwarder"/>
    <xsl:if test="@safearray='yes'">
      <xsl:text> *</xsl:text>
    </xsl:if>
    <xsl:text> a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>) { return smth Set</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text> (a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>); }&#x0A;</xsl:text>
    <!-- setter: COM_FORWARD_Interface_SETTER_Name_TO_OBJ(obj) -->
    <xsl:text>#define COM_FORWARD_</xsl:text>
    <xsl:value-of select="$parent/@name"/>
    <xsl:text>_SETTER_</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>_TO_OBJ(obj) COM_FORWARD_</xsl:text>
    <xsl:value-of select="$parent/@name"/>
    <xsl:text>_SETTER_</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>_TO ((obj)->)&#x0A;</xsl:text>
    <!-- setter: COM_FORWARD_Interface_SETTER_Name_TO_BASE(base) -->
    <xsl:text>#define COM_FORWARD_</xsl:text>
    <xsl:value-of select="$parent/@name"/>
    <xsl:text>_SETTER_</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>_TO_BASE(base) COM_FORWARD_</xsl:text>
    <xsl:value-of select="$parent/@name"/>
    <xsl:text>_SETTER_</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>_TO (base::)&#x0A;</xsl:text>
  </xsl:if>

  <xsl:apply-templates select="@if" mode="end"/>

</xsl:template>


<!--
 *  methods
-->
<xsl:template match="interface//method">
  <xsl:apply-templates select="@if" mode="begin"/>
  <xsl:if test="param/@mod='ptr'">
    <!-- methods using native types must be non-scriptable -->
    <xsl:text>    [noscript]&#x0A;</xsl:text>
  </xsl:if>
  <xsl:text>    void </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:if test="param">
    <xsl:text> (&#x0A;</xsl:text>
    <xsl:for-each select="param [position() != last()]">
      <xsl:text>        </xsl:text>
      <xsl:apply-templates select="."/>
      <xsl:text>,&#x0A;</xsl:text>
    </xsl:for-each>
    <xsl:text>        </xsl:text>
    <xsl:apply-templates select="param [last()]"/>
    <xsl:text>&#x0A;    );&#x0A;</xsl:text>
  </xsl:if>
  <xsl:if test="not(param)">
    <xsl:text>();&#x0A;</xsl:text>
  </xsl:if>
  <xsl:apply-templates select="@if" mode="end"/>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template match="interface//method" mode="forwarder">

  <xsl:variable name="parent" select="ancestor::interface"/>

  <xsl:apply-templates select="@if" mode="begin"/>

  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO(smth) NS_IMETHOD </xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:choose>
    <xsl:when test="param">
      <xsl:text> (</xsl:text>
      <xsl:for-each select="param [position() != last()]">
        <xsl:apply-templates select="." mode="forwarder"/>
        <xsl:text>, </xsl:text>
      </xsl:for-each>
      <xsl:apply-templates select="param [last()]" mode="forwarder"/>
      <xsl:text>) { return smth </xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text> (</xsl:text>
      <xsl:for-each select="param [position() != last()]">
        <xsl:if test="@safearray='yes'">
          <xsl:text>a</xsl:text>
          <xsl:call-template name="capitalize">
            <xsl:with-param name="str" select="@name"/>
          </xsl:call-template>
          <xsl:text>Size+++, </xsl:text>
        </xsl:if>
        <xsl:text>a</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text>, </xsl:text>
      </xsl:for-each>
      <xsl:if test="param [last()]/@safearray='yes'">
        <xsl:text>a</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="param [last()]/@name"/>
        </xsl:call-template>
        <xsl:text>Size, </xsl:text>
      </xsl:if>
      <xsl:text>a</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="param [last()]/@name"/>
      </xsl:call-template>
      <xsl:text>); }</xsl:text>
    </xsl:when>
    <xsl:otherwise test="not(param)">
      <xsl:text>() { return smth </xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>(); }</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:text>&#x0A;</xsl:text>
  <!-- COM_FORWARD_Interface_Method_TO_OBJ(obj) -->
  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO_OBJ(obj) COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO ((obj)->)&#x0A;</xsl:text>
  <!-- COM_FORWARD_Interface_Method_TO_BASE(base) -->
  <xsl:text>#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO_BASE(base) COM_FORWARD_</xsl:text>
  <xsl:value-of select="$parent/@name"/>
  <xsl:text>_</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_TO (base::)&#x0A;</xsl:text>

  <xsl:apply-templates select="@if" mode="end"/>

</xsl:template>


<!--
 *  modules
-->
<xsl:template match="module">
  <xsl:apply-templates select="class"/>
</xsl:template>


<!--
 *  co-classes
-->
<xsl:template match="module/class">
  <!-- class and contract id -->
  <xsl:text>%{C++&#x0A;</xsl:text>
  <xsl:text>// Definitions for module </xsl:text>
  <xsl:value-of select="../@name"/>
  <xsl:text>, class </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>:&#x0A;</xsl:text>
  <xsl:text>#define NS_</xsl:text>
  <xsl:call-template name="string-to-upper">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>_CID { \&#x0A;</xsl:text>
  <xsl:text>    0x</xsl:text><xsl:value-of select="substring(@uuid,1,8)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,10,4)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,15,4)"/>
  <xsl:text>, \&#x0A;    </xsl:text>
  <xsl:text>{ 0x</xsl:text><xsl:value-of select="substring(@uuid,20,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,22,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,25,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,27,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,29,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,31,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,33,2)"/>
  <xsl:text>, 0x</xsl:text><xsl:value-of select="substring(@uuid,35,2)"/>
  <xsl:text> } \&#x0A;}&#x0A;</xsl:text>
  <xsl:text>#define NS_</xsl:text>
  <xsl:call-template name="string-to-upper">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <!-- Contract ID -->
  <xsl:text>_CONTRACTID &quot;@</xsl:text>
  <xsl:value-of select="@namespace"/>
  <xsl:text>/</xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>;1&quot;&#x0A;</xsl:text>
  <!-- CLSID_xxx declarations for XPCOM, for compatibility with Win32 -->
  <xsl:text>// for compatibility with Win32&#x0A;</xsl:text>
  <xsl:text>VBOX_EXTERN_C const nsCID CLSID_</xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>;&#x0A;</xsl:text>
  <xsl:text>%}&#x0A;&#x0A;</xsl:text>
</xsl:template>


<!--
 *  enums
-->
<xsl:template match="enum">[
    uuid(<xsl:value-of select="@uuid"/>),
    scriptable
]
<xsl:text>interface </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>&#x0A;{&#x0A;</xsl:text>
  <xsl:for-each select="const">
    <xsl:text>    const PRUint32 </xsl:text>
    <xsl:value-of select="@name"/> = <xsl:value-of select="@value"/>
    <xsl:text>;&#x0A;</xsl:text>
  </xsl:for-each>
  <xsl:text>};&#x0A;&#x0A;</xsl:text>
  <xsl:choose>

    <xsl:when test="$g_fHackEnumsOntoCppEnums = 'yes'">
      <xsl:text>
/* IDL typedef for enum </xsl:text><xsl:value-of select="@name" /><xsl:text> and C++ mappings. */
%{C++
#ifndef VBOX_WITH_XPCOM_CPP_ENUM_HACK
%}
typedef PRUint32 </xsl:text><xsl:value-of select="concat(@name, '_T')" /><xsl:text>;
%{C++
</xsl:text>
      <xsl:for-each select="const">
        <xsl:value-of select="concat('# define ', ../@name, '_', @name, ' ', ../@name, '::', @name, '&#x0A;')"/>
      </xsl:for-each>
      <xsl:text>#else /* VBOX_WITH_XPCOM_CPP_ENUM_HACK */
typedef enum </xsl:text>
      <xsl:value-of select="concat(@name, '_T')" />
      <xsl:text> {
</xsl:text>
      <xsl:for-each select="const">
        <xsl:value-of select="concat('    ', ../@name, '_', @name, ' = ', ../@name, '::', @name, ',&#x0A;')"/>
      </xsl:for-each>
      <xsl:value-of select="concat('    ', @name, '_32BitHack = 0x7fffffff', '&#x0A;')"/>
      <xsl:text>} </xsl:text><xsl:value-of select="concat(@name, '_T')"/><xsl:text>;
# ifdef AssertCompileSize
AssertCompileSize(</xsl:text><xsl:value-of select="concat(@name, '_T')"/><xsl:text>, sizeof(PRUint32));
# endif
#endif /* VBOX_WITH_XPCOM_CPP_ENUM_HACK */
%}

</xsl:text>
    </xsl:when>

    <xsl:otherwise>
      <!-- -->
      <xsl:value-of select="concat('/* cross-platform type name for ', @name, ' */&#x0A;')"/>
      <xsl:text>%{C++&#x0A;</xsl:text>
      <xsl:value-of select="concat('#define ', @name, '_T', ' ',
                                   'PRUint32&#x0A;')"/>
      <xsl:text>%}&#x0A;&#x0A;</xsl:text>
      <!-- -->
      <xsl:value-of select="concat('/* cross-platform constants for ', @name, ' */&#x0A;')"/>
      <xsl:text>%{C++&#x0A;</xsl:text>
      <xsl:for-each select="const">
        <xsl:value-of select="concat('#define ', ../@name, '_', @name, ' ',
                                     ../@name, '::', @name, '&#x0A;')"/>
      </xsl:for-each>
      <xsl:text>%}&#x0A;&#x0A;</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>


<!--
 *  method parameters
-->
<xsl:template match="method/param">
  <xsl:choose>
    <!-- safearray parameters -->
    <xsl:when test="@safearray='yes'">
      <!-- array size -->
      <xsl:choose>
        <xsl:when test="@dir='in'">in </xsl:when>
        <xsl:when test="@dir='out'">out </xsl:when>
        <xsl:when test="@dir='return'">out </xsl:when>
        <xsl:otherwise>in </xsl:otherwise>
      </xsl:choose>
      <xsl:text>unsigned long </xsl:text>
      <xsl:value-of select="@name"/>
      <xsl:text>Size,&#x0A;</xsl:text>
      <!-- array pointer -->
      <xsl:text>        [array, size_is(</xsl:text>
      <xsl:value-of select="@name"/>
      <xsl:text>Size)</xsl:text>
      <xsl:choose>
        <xsl:when test="@dir='in'">] in </xsl:when>
        <xsl:when test="@dir='out'">] out </xsl:when>
        <xsl:when test="@dir='return'"> , retval] out </xsl:when>
        <xsl:otherwise>] in </xsl:otherwise>
      </xsl:choose>
      <xsl:apply-templates select="@type"/>
      <xsl:text> </xsl:text>
      <xsl:value-of select="@name"/>
    </xsl:when>
    <!-- normal and array parameters -->
    <xsl:otherwise>
      <xsl:choose>
        <xsl:when test="@dir='in'">in </xsl:when>
        <xsl:when test="@dir='out'">out </xsl:when>
        <xsl:when test="@dir='return'">[retval] out </xsl:when>
        <xsl:otherwise>in </xsl:otherwise>
      </xsl:choose>
      <xsl:apply-templates select="@type"/>
      <xsl:text> </xsl:text>
      <xsl:value-of select="@name"/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="method/param" mode="forwarder">
  <xsl:if test="@safearray='yes'">
    <xsl:text>PRUint32</xsl:text>
    <xsl:if test="@dir='out' or @dir='return'">
      <xsl:text> *</xsl:text>
    </xsl:if>
    <xsl:text> a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>Size, </xsl:text>
  </xsl:if>
  <xsl:apply-templates select="@type" mode="forwarder"/>
  <xsl:if test="@dir='out' or @dir='return'">
    <xsl:text> *</xsl:text>
  </xsl:if>
  <xsl:if test="@safearray='yes'">
    <xsl:text> *</xsl:text>
  </xsl:if>
  <xsl:text> a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
</xsl:template>


<!--
 *  attribute/parameter type conversion
-->
<xsl:template match="attribute/@type | param/@type">
  <xsl:variable name="self_target" select="current()/ancestor::if/@target"/>

  <xsl:choose>
    <!-- modifiers (ignored for 'enumeration' attributes)-->
    <xsl:when test="name(current())='type' and ../@mod">
      <xsl:choose>
        <xsl:when test="../@mod='ptr'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='boolean'">booleanPtr</xsl:when>
            <xsl:when test=".='octet'">octetPtr</xsl:when>
            <xsl:when test=".='short'">shortPtr</xsl:when>
            <xsl:when test=".='unsigned short'">ushortPtr</xsl:when>
            <xsl:when test=".='long'">longPtr</xsl:when>
            <xsl:when test=".='long long'">llongPtr</xsl:when>
            <xsl:when test=".='unsigned long'">ulongPtr</xsl:when>
            <xsl:when test=".='unsigned long long'">ullongPtr</xsl:when>
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
                <xsl:text>attribute 'mod=</xsl:text>
                <xsl:value-of select="concat('&quot;',../@mod,'&quot;')"/>
                <xsl:text>' cannot be used with type </xsl:text>
                <xsl:value-of select="concat('&quot;',current(),'&quot;!')"/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:when>
        <xsl:when test="../@mod='string'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='uuid'">wstring</xsl:when>
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
                <xsl:text>attribute 'mod=</xsl:text>
                <xsl:value-of select="concat('&quot;',../@mod,'&quot;')"/>
                <xsl:text>' cannot be used with type </xsl:text>
                <xsl:value-of select="concat('&quot;',current(),'&quot;!')"/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:when>
        <xsl:otherwise>
          <xsl:message terminate="yes">
            <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
            <xsl:value-of select="concat('value &quot;',../@mod,'&quot; ')"/>
            <xsl:text>of attribute 'mod' is invalid!</xsl:text>
          </xsl:message>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:when>
    <!-- no modifiers -->
    <xsl:otherwise>
      <xsl:choose>
        <!-- standard types -->
        <xsl:when test=".='result'">nsresult</xsl:when>
        <xsl:when test=".='boolean'">boolean</xsl:when>
        <xsl:when test=".='octet'">octet</xsl:when>
        <xsl:when test=".='short'">short</xsl:when>
        <xsl:when test=".='unsigned short'">unsigned short</xsl:when>
        <xsl:when test=".='long'">long</xsl:when>
        <xsl:when test=".='long long'">long long</xsl:when>
        <xsl:when test=".='unsigned long'">unsigned long</xsl:when>
        <xsl:when test=".='unsigned long long'">unsigned long long</xsl:when>
        <xsl:when test=".='char'">char</xsl:when>
        <xsl:when test=".='wchar'">wchar</xsl:when>
        <xsl:when test=".='string'">string</xsl:when>
        <xsl:when test=".='wstring'">wstring</xsl:when>
        <!-- UUID type -->
        <xsl:when test=".='uuid'">
          <xsl:choose>
            <xsl:when test="name(..)='attribute'">
              <xsl:choose>
                <xsl:when test="../@readonly='yes'">
                  <xsl:text>nsIDPtr</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:message terminate="yes">
                    <xsl:value-of select="../@name"/>
                    <xsl:text>: Non-readonly uuid attributes are not supported!</xsl:text>
                  </xsl:message>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:when>
            <xsl:when test="name(..)='param'">
              <xsl:choose>
                <xsl:when test="../@dir='in' and not(../@safearray='yes')">
                  <xsl:text>nsIDRef</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:text>nsIDPtr</xsl:text>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:when>
          </xsl:choose>
        </xsl:when>
        <!-- system interface types -->
        <xsl:when test=".='$unknown'">nsISupports</xsl:when>
        <xsl:otherwise>
          <xsl:choose>
            <!-- enum types -->
            <xsl:when test="
              (ancestor::library/application/enum[@name=current()]) or
              (ancestor::library/application/if[@target=$self_target]/enum[@name=current()])
            ">
              <xsl:choose>
                <xsl:when test="$g_fHackEnumsOntoCppEnums = 'yes'">
                  <xsl:value-of select="concat(., '_T')" />
                </xsl:when>
                <xsl:otherwise>
                  <xsl:text>PRUint32</xsl:text>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:when>
            <!-- custom interface types -->
            <xsl:when test="
              (ancestor::library/application/interface[@name=current()]) or
               (ancestor::library/application/if[@target=$self_target]/interface[@name=current()])
            ">
              <xsl:value-of select="."/>
            </xsl:when>
            <!-- other types -->
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:text>Unknown parameter type: </xsl:text>
                <xsl:value-of select="."/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="attribute/@type | param/@type" mode="forwarder">

  <xsl:variable name="self_target" select="current()/ancestor::if/@target"/>

  <xsl:choose>
    <!-- modifiers (ignored for 'enumeration' attributes)-->
    <xsl:when test="name(current())='type' and ../@mod">
      <xsl:choose>
        <xsl:when test="../@mod='ptr'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='boolean'">PRBool *</xsl:when>
            <xsl:when test=".='octet'">PRUint8 *</xsl:when>
            <xsl:when test=".='short'">PRInt16 *</xsl:when>
            <xsl:when test=".='unsigned short'">PRUint16 *</xsl:when>
            <xsl:when test=".='long'">PRInt32 *</xsl:when>
            <xsl:when test=".='long long'">PRInt64 *</xsl:when>
            <xsl:when test=".='unsigned long'">PRUint32 *</xsl:when>
            <xsl:when test=".='unsigned long long'">PRUint64 *</xsl:when>
            <xsl:when test=".='char'">char *</xsl:when>
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
                <xsl:text>attribute 'mod=</xsl:text>
                <xsl:value-of select="concat('&quot;',../@mod,'&quot;')"/>
                <xsl:text>' cannot be used with type </xsl:text>
                <xsl:value-of select="concat('&quot;',current(),'&quot;!')"/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:when>
        <xsl:when test="../@mod='string'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='uuid'">PRUnichar *</xsl:when>
            <xsl:otherwise>
              <xsl:message terminate="yes">
                <xsl:value-of select="concat(../../../@name,'::',../../@name,'::',../@name,': ')"/>
                <xsl:text>attribute 'mod=</xsl:text>
                <xsl:value-of select="concat('&quot;',../@mod,'&quot;')"/>
                <xsl:text>' cannot be used with type </xsl:text>
                <xsl:value-of select="concat('&quot;',current(),'&quot;!')"/>
              </xsl:message>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:when>
      </xsl:choose>
    </xsl:when>
    <!-- no modifiers -->
    <xsl:otherwise>
      <xsl:choose>
        <!-- standard types -->
        <xsl:when test=".='result'">nsresult</xsl:when>
        <xsl:when test=".='boolean'">PRBool</xsl:when>
        <xsl:when test=".='octet'">PRUint8</xsl:when>
        <xsl:when test=".='short'">PRInt16</xsl:when>
        <xsl:when test=".='unsigned short'">PRUint16</xsl:when>
        <xsl:when test=".='long'">PRInt32</xsl:when>
        <xsl:when test=".='long long'">PRInt64</xsl:when>
        <xsl:when test=".='unsigned long'">PRUint32</xsl:when>
        <xsl:when test=".='unsigned long long'">PRUint64</xsl:when>
        <xsl:when test=".='char'">char</xsl:when>
        <xsl:when test=".='wchar'">PRUnichar</xsl:when>
        <!-- string types -->
        <xsl:when test=".='string'">char *</xsl:when>
        <xsl:when test=".='wstring'">PRUnichar *</xsl:when>
        <!-- UUID type -->
        <xsl:when test=".='uuid'">
          <xsl:choose>
            <xsl:when test="name(..)='attribute'">
              <xsl:choose>
                <xsl:when test="../@readonly='yes'">
                  <xsl:text>nsID *</xsl:text>
                </xsl:when>
              </xsl:choose>
            </xsl:when>
            <xsl:when test="name(..)='param'">
              <xsl:choose>
                <xsl:when test="../@dir='in' and not(../@safearray='yes')">
                  <xsl:text>const nsID &amp;</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:text>nsID *</xsl:text>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:when>
          </xsl:choose>
        </xsl:when>
        <!-- system interface types -->
        <xsl:when test=".='$unknown'">nsISupports *</xsl:when>
        <xsl:otherwise>
          <xsl:choose>
            <!-- enum types -->
            <xsl:when test="
              (ancestor::library/application/enum[@name=current()]) or
              (ancestor::library/application/if[@target=$self_target]/enum[@name=current()])
            ">
              <xsl:text>PRUint32</xsl:text>
            </xsl:when>
            <!-- custom interface types -->
            <xsl:when test="
              (ancestor::library/application/interface[@name=current()]) or
              (ancestor::library/application/if[@target=$self_target]/interface[@name=current()])
            ">
              <xsl:value-of select="."/>
              <xsl:text> *</xsl:text>
            </xsl:when>
            <!-- other types -->
          </xsl:choose>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<!-- Filters for switch off VBoxSDS definitions -->

<xsl:template match="application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//module/class" />

<xsl:template match="application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']/if//interface
| application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//interface" />

<xsl:template match="application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//interface" mode="forward" />


</xsl:stylesheet>

