<?xml version="1.0"?>
<!-- $Id: midl.xsl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ -->

<!--
 *  A template to generate a MS IDL compatible interface definition file
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

<!-- Whether to generate proxy code and type library ('yes'), or just the type-library. -->
<xsl:param name="g_fGenProxy" select="'no'"/>

<!-- Whether to generate coclass and interfaces for VBoxSDS-->
<xsl:param name="g_fVBoxWithSDS" select="'no'"/>

<xsl:include href="typemap-shared.inc.xsl"/>


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
 *  MS IDL (MIDL) definition for VirtualBox Main API (COM interfaces)
 *  generated from XIDL (XML interface definition).
 *
 *  Source    : src/VBox/Main/idl/VirtualBox.xidl
 *  Generator : src/VBox/Main/idl/midl.xsl
 */

#if (__midl >= 501)
midl_pragma warning(disable:2039) /* Disable warning MIDL2039 regarding interface not being automation
                                     marshaling conformant and requiring NT 4.0 SP4 or greater. */
midl_pragma warning(disable:2456) /* Disable warning MIDL2456 regarding SAFEARRAY(interface pointer). */
midl_pragma warning(disable:2111) /* Disable warning MIDL2111 regarding identifier lengths exceeding 31 chars. */
#endif

import "unknwn.idl";

</xsl:text>
  <xsl:apply-templates/>
</xsl:template>


<!--
 *  ignore all |if|s except those for MIDL target
-->
<xsl:template match="if">
  <xsl:if test="@target='midl'">
    <xsl:apply-templates/>
  </xsl:if>
</xsl:template>
<xsl:template match="if" mode="forward">
  <xsl:if test="@target='midl'">
    <xsl:apply-templates mode="forward"/>
  </xsl:if>
</xsl:template>
<xsl:template match="if" mode="forwarder">
  <xsl:param name="nameOnly"/>
  <xsl:if test="@target='midl'">
    <xsl:apply-templates mode="forwarder">
      <xsl:with-param name="nameOnly" select="$nameOnly"/>
    </xsl:apply-templates>
  </xsl:if>
</xsl:template>


<!--
 *  cpp_quote
-->
<xsl:template match="cpp">
  <xsl:text>cpp_quote("</xsl:text>
  <xsl:call-template name="string-replace">
    <xsl:with-param name="haystack" select="@line"/>
    <xsl:with-param name="needle">&quot;</xsl:with-param>
    <xsl:with-param name="replacement">\&quot;</xsl:with-param>
  </xsl:call-template>
  <xsl:text>")&#x0A;&#x0A;</xsl:text>
</xsl:template>


<!--
 *  #if statement (@if attribute)
-->
<xsl:template match="@if" mode="begin">
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
  <xsl:if test="$g_fGenProxy = 'yes'">
    <!-- Declare everything outside the library and then reference these
         from inside the library statement.  See:
         http://msdn.microsoft.com/en-us/library/windows/desktop/aa366841(v=vs.85).aspx -->
    <xsl:text>&#x0A;</xsl:text>
    <!-- forward declarations -->
    <xsl:apply-templates select="descendant::application/if   | descendant::application/interface" mode="forward"/>
    <xsl:text>&#x0A;</xsl:text>
    <!-- all enums go first -->
    <xsl:apply-templates select="descendant::application/enum | descendant::application/if[enum]"/>
    <!-- declare the interfaces -->
    <xsl:apply-templates select="descendant::application/if   | descendant::application/interface"/>
  </xsl:if>

[
    uuid(<xsl:value-of select="@uuid"/>),
    version(<xsl:value-of select="@version"/>),
    helpstring("<xsl:value-of select="@name"/> Type Library")
]
<xsl:text>library </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>&#x0A;{&#x0A;</xsl:text>
  <xsl:text>&#x0A;importlib("stdole2.tlb");&#x0A;&#x0A;</xsl:text>
  <!-- result codes -->
  <xsl:for-each select="application/result">
    <xsl:apply-templates select="."/>
  </xsl:for-each>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:text>&#x0A;</xsl:text>
  <xsl:choose>
    <xsl:when test="$g_fGenProxy = 'yes'">
      <!-- reference enums and interfaces -->
      <xsl:apply-templates select="descendant::application/if          | descendant::application/interface" mode="forward"/>
      <xsl:apply-templates select="descendant::application/enum        | descendant::application/if[enum]" mode="forward"/>
      <!-- the modules (i.e. everything else) -->
      <xsl:apply-templates select="descendant::application/module      | descendant::application/if[module]"/>
    </xsl:when>
    <xsl:otherwise>
      <!-- forward declarations -->
      <xsl:apply-templates select="descendant::application/if          | descendant::application/interface" mode="forward"/>
      <!-- all enums go first -->
      <xsl:apply-templates select="descendant::application/enum        | descendant::application/if[enum]"/>
      <!-- everything else but result codes and enums -->
      <xsl:apply-templates select="  descendant::application/interface | descendant::application/if[interface]
                                   | descendant::application/module    | descendant::application/if[module]"/>
    </xsl:otherwise>
  </xsl:choose>
  <!-- -->
  <xsl:text>}; /* library </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text> */&#x0A;&#x0A;</xsl:text>
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
  <xsl:text>cpp_quote("</xsl:text>
  <xsl:value-of select="concat('#define ',@name,' ((HRESULT)',@value, ')')"/>
  <xsl:text>")&#x0A;</xsl:text>
</xsl:template>


<!--
 *  forward declarations
-->
<xsl:template match="interface" mode="forward" name="template_interface_forward">
  <xsl:text>interface </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>;&#x0A;</xsl:text>
</xsl:template>


<xsl:template match="enum" mode="forward">
  <xsl:text>enum </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>;&#x0A;&#x0A;</xsl:text>
</xsl:template>


<!--
 *  interfaces
-->
<xsl:template match="interface" name="template_interface">[
    uuid(<xsl:value-of select="@uuid"/>),
    object,
<xsl:if test="not(@notdual = 'yes')">    dual,</xsl:if>
    oleautomation
<xsl:if test="$g_fGenProxy = 'yes'">
  <!-- Indicates to the typelib that we are using a proxy stub DLL and that interfaces
       should have any ProxyStubClsid32 or TypeLib keys in the registry. -->
  <xsl:text>    , proxy</xsl:text>
</xsl:if>
]
<xsl:text>interface </xsl:text>
  <xsl:variable name="name" select="@name"/>
  <xsl:value-of select="$name"/>
  <xsl:text> : </xsl:text>
  <xsl:choose>
    <xsl:when test="(@extends = '$unknown') and (@notdual = 'yes')">IUnknown</xsl:when>
    <xsl:when test="@extends='$unknown'">IDispatch</xsl:when>
    <xsl:when test="@extends='$errorinfo'">IErrorInfo</xsl:when>
    <!-- TODO/FIXME/BUGBUG: The above $errorinfo value causes the following warning (/W4):
warning MIDL2460 : dual interface should be derived from IDispatch : IVirtualBoxErrorInfo [ Interface 'IVirtualBoxErrorInfo'  ]
    -->
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
      <xsl:text>    [propget] HRESULT InternalAndReservedAttribute</xsl:text>
      <xsl:value-of select="concat(position(), $name)"/>
      <xsl:text> ([out, retval] ULONG *aReserved);&#x0A;&#x0A;</xsl:text>
    </xsl:for-each>
  </xsl:if>
  <!-- methods -->
  <xsl:apply-templates select="method"/>
  <xsl:variable name="reservedMethods" select="@reservedMethods"/>
  <xsl:if test="$reservedMethods > 0">
    <!-- tricky way to do a "for" loop without recursion -->
    <xsl:for-each select="(//*)[position() &lt;= $reservedMethods]">
      <xsl:text>    HRESULT InternalAndReservedMethod</xsl:text>
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
  <!-- 1) individual methods -->
  <xsl:apply-templates select="attribute" mode="forwarder"/>
  <xsl:apply-templates select="method" mode="forwarder"/>
  <xsl:apply-templates select="if" mode="forwarder"/>
  <!-- 2) COM_FORWARD_Interface_TO(smth) -->
  <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO(smth) </xsl:text>
  <xsl:apply-templates select="attribute" mode="forwarder">
    <xsl:with-param name="nameOnly" select="'yes'"/>
  </xsl:apply-templates>
  <xsl:apply-templates select="method" mode="forwarder">
    <xsl:with-param name="nameOnly" select="'yes'"/>
  </xsl:apply-templates>
  <xsl:apply-templates select="if" mode="forwarder">
    <xsl:with-param name="nameOnly" select="'yes'"/>
  </xsl:apply-templates>
  <xsl:text>")&#x0A;</xsl:text>
  <!-- 3) COM_FORWARD_Interface_TO_OBJ(obj) -->
  <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO_OBJ(obj) COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO ((obj)->)")&#x0A;</xsl:text>
  <!-- 4) COM_FORWARD_Interface_TO_BASE(base) -->
  <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO_BASE(base) COM_FORWARD_</xsl:text>
  <xsl:value-of select="$name"/>
  <xsl:text>_TO (base::)")&#x0A;</xsl:text>
  <!-- end -->
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>


<!--
 *  attributes
-->
<xsl:template match="interface//attribute">
  <xsl:apply-templates select="@if" mode="begin"/>
  <!-- getter -->
  <xsl:text>    [propget] HRESULT </xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text> ([out, retval] </xsl:text>
  <xsl:if test="@safearray='yes'">
    <xsl:text>SAFEARRAY(</xsl:text>
  </xsl:if>
  <xsl:apply-templates select="@type"/>
  <xsl:if test="@safearray='yes'">
    <xsl:text>)</xsl:text>
  </xsl:if>
  <xsl:text> * a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:text>);&#x0A;</xsl:text>
  <!-- setter -->
  <xsl:if test="not(@readonly='yes')">
    <xsl:text>    [propput] HRESULT </xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text> ([in] </xsl:text>
    <xsl:if test="@safearray='yes'">
      <xsl:text>SAFEARRAY(</xsl:text>
    </xsl:if>
    <xsl:apply-templates select="@type"/>
    <xsl:if test="@safearray='yes'">
      <xsl:text>)</xsl:text>
    </xsl:if>
    <xsl:text> a</xsl:text>
    <xsl:call-template name="capitalize">
      <xsl:with-param name="str" select="@name"/>
    </xsl:call-template>
    <xsl:text>);&#x0A;</xsl:text>
  </xsl:if>
  <xsl:apply-templates select="@if" mode="end"/>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template match="interface//attribute" mode="forwarder">

  <!-- if nameOnly='yes' then only the macro name is composed
       followed by a space -->
  <xsl:param name="nameOnly"/>

  <xsl:variable name="parent" select="ancestor::interface"/>

  <xsl:apply-templates select="@if" mode="begin"/>

  <xsl:choose>
    <xsl:when test="$nameOnly='yes'">
      <!-- getter: COM_FORWARD_Interface_GETTER_Name_TO(smth) -->
      <xsl:text>COM_FORWARD_</xsl:text>
      <xsl:value-of select="$parent/@name"/>
      <xsl:text>_GETTER_</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>_TO (smth) </xsl:text>
      <!-- setter: COM_FORWARD_Interface_SETTER_Name_TO(smth) -->
      <xsl:if test="not(@readonly='yes')">
        <xsl:text>COM_FORWARD_</xsl:text>
        <xsl:value-of select="$parent/@name"/>
        <xsl:text>_SETTER_</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text>_TO (smth) </xsl:text>
      </xsl:if>
    </xsl:when>
    <xsl:otherwise>
      <!-- getter: COM_FORWARD_Interface_GETTER_Name_TO(smth) -->
      <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
      <xsl:value-of select="$parent/@name"/>
      <xsl:text>_GETTER_</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>_TO(smth) HRESULT STDMETHODCALLTYPE get_</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text> (</xsl:text>
      <xsl:choose>
        <xsl:when test="@safearray='yes'">
          <xsl:text>SAFEARRAY *</xsl:text>
        </xsl:when>
        <xsl:otherwise>
          <xsl:apply-templates select="@type"/>
        </xsl:otherwise>
      </xsl:choose>
      <xsl:text> * a</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>) { return smth get_</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text> (a</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>); }")&#x0A;</xsl:text>
      <!-- getter: COM_FORWARD_Interface_GETTER_Name_TO_OBJ(obj) -->
      <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
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
      <xsl:text>_TO ((obj)->)")&#x0A;</xsl:text>
      <!-- getter: COM_FORWARD_Interface_GETTER_Name_TO_BASE(base) -->
      <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
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
      <xsl:text>_TO (base::)")&#x0A;</xsl:text>
      <!-- -->
      <xsl:if test="not(@readonly='yes')">
        <!-- setter: COM_FORWARD_Interface_SETTER_Name_TO(smth) -->
        <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
        <xsl:value-of select="$parent/@name"/>
        <xsl:text>_SETTER_</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text>_TO(smth) HRESULT STDMETHODCALLTYPE put_</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text> (</xsl:text>
        <xsl:choose>
          <xsl:when test="@safearray='yes'">
            <xsl:text>SAFEARRAY *</xsl:text>
          </xsl:when>
          <xsl:otherwise>
            <xsl:apply-templates select="@type"/>
          </xsl:otherwise>
        </xsl:choose>
        <xsl:text> a</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text>) { return smth put_</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text> (a</xsl:text>
        <xsl:call-template name="capitalize">
          <xsl:with-param name="str" select="@name"/>
        </xsl:call-template>
        <xsl:text>); }")&#x0A;</xsl:text>
        <!-- setter: COM_FORWARD_Interface_SETTER_Name_TO_OBJ(obj) -->
        <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
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
        <xsl:text>_TO ((obj)->)")&#x0A;</xsl:text>
        <!-- setter: COM_FORWARD_Interface_SETTER_Name_TO_BASE(base) -->
        <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
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
        <xsl:text>_TO (base::)")&#x0A;</xsl:text>
      </xsl:if>
    </xsl:otherwise>
  </xsl:choose>

  <xsl:apply-templates select="@if" mode="end"/>

</xsl:template>


<!--
 *  methods
-->
<xsl:template match="interface//method">
  <xsl:apply-templates select="@if" mode="begin"/>
  <xsl:text>    HRESULT </xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
  <xsl:choose>
    <xsl:when test="param">
      <xsl:text> (&#x0A;</xsl:text>
      <xsl:for-each select="param [position() != last()]">
        <xsl:text>        </xsl:text>
        <xsl:apply-templates select="."/>
        <xsl:text>,&#x0A;</xsl:text>
      </xsl:for-each>
      <xsl:text>        </xsl:text>
      <xsl:apply-templates select="param [last()]"/>
      <xsl:text>&#x0A;    );&#x0A;</xsl:text>
    </xsl:when>
    <xsl:otherwise test="not(param)">
      <xsl:text>();&#x0A;</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:apply-templates select="@if" mode="end"/>
  <xsl:text>&#x0A;</xsl:text>
</xsl:template>

<xsl:template match="interface//method" mode="forwarder">

  <!-- if nameOnly='yes' then only the macro name is composed followed by \ -->
  <xsl:param name="nameOnly"/>

  <xsl:variable name="parent" select="ancestor::interface"/>

  <xsl:apply-templates select="@if" mode="begin"/>

  <xsl:choose>
    <xsl:when test="$nameOnly='yes'">
      <!-- COM_FORWARD_Interface_Method_TO(smth) -->
      <xsl:text>COM_FORWARD_</xsl:text>
      <xsl:value-of select="$parent/@name"/>
      <xsl:text>_</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>_TO (smth) </xsl:text>
    </xsl:when>
    <xsl:otherwise>
      <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
      <xsl:value-of select="$parent/@name"/>
      <xsl:text>_</xsl:text>
      <xsl:call-template name="capitalize">
        <xsl:with-param name="str" select="@name"/>
      </xsl:call-template>
      <xsl:text>_TO(smth) HRESULT STDMETHODCALLTYPE </xsl:text>
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
            <xsl:text>a</xsl:text>
            <xsl:call-template name="capitalize">
              <xsl:with-param name="str" select="@name"/>
            </xsl:call-template>
            <xsl:text>, </xsl:text>
          </xsl:for-each>
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
      <xsl:text>")&#x0A;</xsl:text>
      <!-- COM_FORWARD_Interface_Method_TO_OBJ(obj) -->
      <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
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
      <xsl:text>_TO ((obj)->)")&#x0A;</xsl:text>
      <!-- COM_FORWARD_Interface_Method_TO_BASE(base) -->
      <xsl:text>cpp_quote("#define COM_FORWARD_</xsl:text>
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
      <xsl:text>_TO (base::)")&#x0A;</xsl:text>
    </xsl:otherwise>
  </xsl:choose>

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
<xsl:template match="module/class" name="template_class">[
    uuid(<xsl:value-of select="@uuid"/>)
]
<xsl:text>coclass </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>&#x0A;{&#x0A;</xsl:text>
  <xsl:for-each select="interface">
    <xsl:text>    </xsl:text>
    <xsl:if test="@default='yes'">
      <xsl:text>[default] </xsl:text>
    </xsl:if>
    <xsl:text>interface </xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text>;&#x0A;</xsl:text>
  </xsl:for-each>
  <xsl:for-each select="eventsink">
    <xsl:text>    </xsl:text>
    <xsl:choose>
      <xsl:when test="@default='yes'"><xsl:text>[default,source]</xsl:text></xsl:when>
      <xsl:otherwise><xsl:text>[source]</xsl:text></xsl:otherwise>
    </xsl:choose>
    <xsl:text> interface </xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text>;&#x0A;</xsl:text>
  </xsl:for-each>
  <xsl:text>&#x0A;}; /* coclass </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text> */&#x0A;&#x0A;</xsl:text>
</xsl:template>


<!--
 *  enums
-->
<xsl:template match="enum">[
    uuid(<xsl:value-of select="@uuid"/>),
    v1_enum
]
<xsl:text>typedef enum &#x0A;{&#x0A;</xsl:text>
  <xsl:for-each select="const">
    <xsl:text>    </xsl:text>
    <xsl:value-of select="concat(../@name,'_',@name)"/> = <xsl:value-of select="@value"/>
    <xsl:choose>
      <xsl:when test="position()!=last()"><xsl:text>,&#x0A;</xsl:text></xsl:when>
      <xsl:otherwise><xsl:text>&#x0A;</xsl:text></xsl:otherwise>
    </xsl:choose>
  </xsl:for-each>
  <xsl:text>} </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>;&#x0A;&#x0A;</xsl:text>
  <!-- -->
  <xsl:value-of select="concat('/* cross-platform type name for ', @name, ' */&#x0A;')"/>
  <xsl:value-of select="concat('cpp_quote(&quot;#define ', @name, '_T', ' ',
                               @name, '&quot;)&#x0A;&#x0A;')"/>
  <xsl:text>&#x0A;&#x0A;</xsl:text>
</xsl:template>


<!--
 *  method parameters
-->
<xsl:template match="method/param">
  <xsl:text>[</xsl:text>
  <xsl:choose>
    <xsl:when test="@dir='in'">in</xsl:when>
    <xsl:when test="@dir='out'">out</xsl:when>
    <xsl:when test="@dir='return'">out, retval</xsl:when>
    <xsl:otherwise>in</xsl:otherwise>
  </xsl:choose>
  <xsl:text>] </xsl:text>
  <xsl:if test="@safearray='yes'">
    <xsl:text>SAFEARRAY(</xsl:text>
  </xsl:if>
  <xsl:apply-templates select="@type"/>
  <xsl:if test="@safearray='yes'">
    <xsl:text>)</xsl:text>
  </xsl:if>
  <xsl:if test="@dir='out' or @dir='return'">
    <xsl:text> *</xsl:text>
  </xsl:if>
  <xsl:text> a</xsl:text>
  <xsl:call-template name="capitalize">
    <xsl:with-param name="str" select="@name"/>
  </xsl:call-template>
</xsl:template>

<xsl:template match="method/param" mode="forwarder">
  <xsl:choose>
    <xsl:when test="@safearray='yes'">
      <xsl:text>SAFEARRAY *</xsl:text>
    </xsl:when>
    <xsl:otherwise>
      <xsl:apply-templates select="@type"/>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:if test="@dir='out' or @dir='return' or @safearray='yes'">
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
    <!-- modifiers -->
    <xsl:when test="name(current())='type' and ../@mod">
      <xsl:choose>
        <xsl:when test="../@mod='ptr'">
          <xsl:choose>
            <!-- standard types -->
            <!--xsl:when test=".='result'">??</xsl:when-->
            <xsl:when test=".='boolean'">BOOL *</xsl:when>
            <xsl:when test=".='octet'">BYTE *</xsl:when>
            <xsl:when test=".='short'">SHORT *</xsl:when>
            <xsl:when test=".='unsigned short'">USHORT *</xsl:when>
            <xsl:when test=".='long'">LONG *</xsl:when>
            <xsl:when test=".='long long'">LONG64 *</xsl:when>
            <xsl:when test=".='unsigned long'">ULONG *</xsl:when>
            <xsl:when test=".='unsigned long long'">
              <xsl:message terminate="yes">
                 <xsl:value-of select="'&quot;unsigned long long&quot; no longer supported'" />
              </xsl:message>
            </xsl:when>
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
            <xsl:when test=".='uuid'">BSTR</xsl:when>
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
        <xsl:when test=".='result'">HRESULT</xsl:when>
        <xsl:when test=".='boolean'">BOOL</xsl:when>
        <xsl:when test=".='octet'">BYTE</xsl:when>
        <xsl:when test=".='short'">SHORT</xsl:when>
        <xsl:when test=".='unsigned short'">USHORT</xsl:when>
        <xsl:when test=".='long'">LONG</xsl:when>
        <xsl:when test=".='long long'">LONG64</xsl:when>
        <xsl:when test=".='unsigned long'">ULONG</xsl:when>
        <xsl:when test=".='char'">CHAR</xsl:when>
        <xsl:when test=".='string'">CHAR *</xsl:when>
        <xsl:when test=".='wchar'">OLECHAR</xsl:when>
        <xsl:when test=".='wstring'">BSTR</xsl:when>
        <!-- UUID type -->
        <xsl:when test=".='uuid'">GUID</xsl:when>
        <!-- system interface types -->
        <xsl:when test=".='$unknown'">IUnknown *</xsl:when>
        <xsl:when test=".='unsigned long long'">
          <xsl:message terminate="yes">
            <xsl:value-of select="'&quot;unsigned long long&quot; no longer supported'" />
          </xsl:message>
        </xsl:when>
        <xsl:otherwise>
          <xsl:choose>
            <!-- enum types -->
            <xsl:when test="
                 (ancestor::library/application/enum[@name=current()])
              or (ancestor::library/if/application/enum[@name=current()])
              or (ancestor::library/application/if[@target=$self_target]/enum[@name=current()])
              or (ancestor::library/if/application/if[@target=$self_target]/enum[@name=current()])
            ">
              <xsl:value-of select="."/>
            </xsl:when>
            <!-- custom interface types -->
            <xsl:when test="
              (    (ancestor::library/application/interface[@name=current()])
                or (ancestor::library/if/application/interface[@name=current()])
                or (ancestor::library/application/if[@target=$self_target]/interface[@name=current()])
                or (ancestor::library/if/application/if[@target=$self_target]/interface[@name=current()])
              )
            ">
              <xsl:value-of select="."/><xsl:text> *</xsl:text>
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


<!-- Filters for switch on/off VBoxSDS definitions -->

<xsl:template match="if[@target='midl']/application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//module/class" >
    <xsl:if test="$g_fVBoxWithSDS='yes'" >
        <xsl:call-template name="template_class" />
    </xsl:if>
</xsl:template>

<xsl:template match="if[@target='midl']/application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']/if//interface
                                | application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//interface" >
    <xsl:if test="$g_fVBoxWithSDS='yes'" >
        <xsl:call-template name="template_interface" />
    </xsl:if>
</xsl:template>

<xsl:template match="if[@target='midl']/application[@uuid='ec0e78e8-fa43-43e8-ac0a-02c784c4a4fa']//interface" mode="forward" >
    <xsl:if test="$g_fVBoxWithSDS='yes'" >
        <xsl:call-template name="template_interface_forward" />
    </xsl:if>
</xsl:template>


</xsl:stylesheet>
