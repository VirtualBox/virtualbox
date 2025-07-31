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

<!-- - - - - - - - - - - - - - - - - - - - - -
  wildcard match, ignore everything which has no explicit match
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template match="*"/>


<!-- - - - - - - - - - - - - - - - - - - - - -
    attribute match
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template name="attribute">
    <xsl:param name="suppressNotImplementedException"/>
    <xsl:param name="vboxObjectName"/>
    <xsl:param name="swaggerObjectName"/>
    <xsl:param name="functionName"/>
    <xsl:param name="startIndent"/>

    <xsl:variable name="type">
        <xsl:choose>
            <xsl:when test="@rest and @rest='uuid'">
                <xsl:value-of select="@rest"/>
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="@type"/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:variable>

    <xsl:variable name="readonly" select="@readonly"/>
    <xsl:variable name="swaggerFormat" select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@format"/>

    <xsl:variable name="attributeSplittedByLetters">
        <xsl:call-template name="splitWord">
            <xsl:with-param name="word" select="@name"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="swaggerAttrName">
        <xsl:call-template name="replaceUppercaseWithUnderscore">
            <xsl:with-param name="lettersNodeSet" select="$attributeSplittedByLetters"/>
            <xsl:with-param name="letter" select="letter"/>
            <xsl:with-param name="fSkipFirst" select="false"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="attributeTypeSplittedByLetters">
        <xsl:call-template name="splitWord">
            <xsl:with-param name="word" select="$type"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="swaggerAttrType">
        <xsl:call-template name="replaceUppercaseWithUnderscore">
            <xsl:with-param name="lettersNodeSet" select="$attributeTypeSplittedByLetters"/>
            <xsl:with-param name="letter" select="letter"/>
            <xsl:with-param name="fSkipFirst" select="false"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:choose>
        <xsl:when test="key('G_keyEnumsByName', $type) or starts-with($type, 'I')">
            <xsl:choose>
                <xsl:when test="key('G_keyEnumsByName', $type)">
                    <xsl:choose>
                        <xsl:when test="@safearray='yes'">
                            <xsl:variable name="swaggerObjectsList" select="concat($swaggerObjectName, '.', $swaggerAttrName)"/>
                            <xsl:variable name="vboxObjectsList" select="concat('ol_', $swaggerAttrName)"/>

                            <xsl:text>try:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:value-of select="concat($vboxObjectsList, ' = ctx[', $apos, 'global', $apos, '].getArray(', $vboxObjectName, ', ', $apos, @name, $apos, ')')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>

                            <xsl:value-of select="concat($swaggerObjectsList, ' = list()')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>

                            <xsl:value-of select="concat('for count, item in enumerate(', $vboxObjectsList, '):')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$fourSpaces"/>
                            <xsl:value-of select="concat('o = ctx[', $apos, 'global', $apos, '].getEnumValueName(', $apos, $type, $apos, ', item)')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$fourSpaces"/>
                            <xsl:value-of select="concat('if ', $swaggerObjectsList, '.count(o) == 0 : ', $swaggerObjectsList, '.append(o)')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>

                            <xsl:text>except Exception as e:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:value-of select="concat('logging.info(', $apos, 'Error getting the array of ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                            <xsl:if test="$suppressNotImplementedException=false">
                                <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                                <xsl:value-of select="concat('raise Exception(', $apos, 'Error getting the array of ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                            </xsl:if>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>
                        </xsl:when>
                        <xsl:otherwise>
                            <xsl:text>try:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:value-of select="concat($swaggerObjectName, '.', $swaggerAttrName, ' = ')"/>
                            <xsl:value-of select="concat('ctx[ ', $apos, 'global', $apos, '].getEnumValueName(', $apos, $type, $apos, ', ', $vboxObjectName, '.', @name, ')')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>

                            <xsl:text>except Exception as e:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:value-of select="concat('logging.info(', $apos, 'Error getting the attribute ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                            <xsl:if test="$suppressNotImplementedException=false">
                                <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                                <xsl:value-of select="concat('raise Exception(', $apos, 'Error getting the array of ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                            </xsl:if>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>
                        </xsl:otherwise>
                    </xsl:choose>
                 
                </xsl:when>
                <xsl:otherwise>
                    <xsl:variable name="coreFuncName">
                        <xsl:choose>
                            <xsl:when test="starts-with($swaggerAttrType, 'i_')">
                                <xsl:value-of select="substring($swaggerAttrType,3)"/>
                            </xsl:when>
                            <xsl:otherwise>
                                <xsl:value-of select="substring($swaggerAttrType,2)"/>
                            </xsl:otherwise>
                        </xsl:choose>
                    </xsl:variable>

                    <xsl:choose>
                        <xsl:when test="@safearray='yes'">
                            <xsl:variable name="swaggerObjectsList" select="concat($swaggerObjectName, '.', $swaggerAttrName)"/>
                            <xsl:variable name="vboxObjectsList" select="concat('ol_', $swaggerAttrName)"/>

                            <xsl:text>try:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:value-of select="concat($vboxObjectsList, ' = ctx[', $apos, 'global', $apos, '].getArray(', $vboxObjectName, ',', $apos, @name, $apos, ')')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>

                            <xsl:value-of select="concat($swaggerObjectsList, ' = list()')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>

                            <xsl:value-of select="concat('for count, item in enumerate(', $vboxObjectsList, '):')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$fourSpaces"/>
                            <xsl:value-of select="concat('o = i_fill_', $coreFuncName, '(item)')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$fourSpaces"/>
                            <xsl:value-of select="concat($swaggerObjectsList, '.append(o)')"/>
                             <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>

                            <xsl:text>except Exception as e:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:value-of select="concat('logging.info(', $apos, 'Error getting the array of ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                            <xsl:if test="$suppressNotImplementedException=false">
                                <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                                <xsl:value-of select="concat('raise Exception(', $apos, 'Error getting the array of ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                            </xsl:if>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>
                        </xsl:when>
                        <xsl:otherwise>
                            <xsl:variable name="newObject" select="concat('o_', $swaggerAttrName)"/>
                            <xsl:text>try:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>

                            <xsl:value-of select="concat($newObject, ' = ', $vboxObjectName, '.', @name, ' if ', $vboxObjectName, '.', @name, ' is not None else None')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:value-of select="concat('if ', $newObject, ' is not None:')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$fourSpaces"/>
                            <xsl:value-of select="concat($swaggerObjectName, '.', $swaggerAttrName, ' = i_fill_', $coreFuncName, '(', $newObject, ')')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:text>else:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$fourSpaces"/>
                            <xsl:value-of select="concat($swaggerObjectName, '.', $swaggerAttrName, ' = None')"/>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>

                            <xsl:text>except Exception as e:</xsl:text>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                            <xsl:value-of select="concat('logging.info(', $apos, 'Error getting the interface object ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                            <xsl:if test="$suppressNotImplementedException=false">
                                <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                                <xsl:value-of select="concat('raise Exception(', $apos, 'Error getting the interface object ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                            </xsl:if>
                            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>
                        </xsl:otherwise>
                    </xsl:choose>
                </xsl:otherwise>
            </xsl:choose>
        </xsl:when>
        <xsl:when test="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@type">
            <xsl:variable name="idSuffix">
                <xsl:if test="$type='uuid' and key('G_keyInterfacesByRESTSupport', @type)">
                    <xsl:text>.id</xsl:text>
                </xsl:if>
            </xsl:variable>
            <xsl:choose>
                <xsl:when test="@safearray='yes'">
                    <xsl:variable name="swaggerObjectsList" select="concat($swaggerObjectName, '.', $swaggerAttrName)"/>
                    <xsl:variable name="vboxObjectsList" select="concat('ol_', $swaggerAttrName)"/>

                    <xsl:text>try:</xsl:text>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                    <xsl:value-of select="concat($vboxObjectsList, ' = ctx[', $apos, 'global', $apos, '].getArray(', $vboxObjectName, ',', $apos, @name, $apos, ')')"/>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>

                    <xsl:value-of select="concat($swaggerObjectsList, ' = list()')"/>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>

                    <xsl:value-of select="concat('for count, item in enumerate(', $vboxObjectsList, '):')"/>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$fourSpaces"/>
                    <xsl:value-of select="concat($swaggerObjectsList, '.append(item', $idSuffix, ')')"/><!-- TODO: need to check for the correct work with suffix -->
                     <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>

                    <xsl:text>except Exception as e:</xsl:text>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                    <xsl:value-of select="concat('logging.info(', $apos, 'Error getting the array of ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                    <xsl:if test="$suppressNotImplementedException=false">
                        <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                        <xsl:value-of select="concat('raise Exception(', $apos, 'Error getting the array of ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                    </xsl:if>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>
                </xsl:when>
                <xsl:otherwise>

                    <xsl:text>try:</xsl:text>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                    <xsl:value-of select="concat($swaggerObjectName, '.', $swaggerAttrName, ' = ', $vboxObjectName, '.', @name, $idSuffix)"/>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>
                    <xsl:text>except Exception as e:</xsl:text>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                    <xsl:value-of select="concat('logging.info(', $apos, 'Error getting the attribute ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                    <xsl:if test="$suppressNotImplementedException=false">
                        <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/><xsl:value-of select="$twoSpaces"/>
                         <xsl:value-of select="concat('raise Exception(', $apos, 'Error getting the attribute ', $aposDouble, @name, $aposDouble, $apos, ')' )"/>
                    </xsl:if>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$startIndent"/>
                </xsl:otherwise>
            </xsl:choose>
        </xsl:when>
    </xsl:choose>

</xsl:template>

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
    <xsl:text>&#x0A;</xsl:text>
    <xsl:text># import interfaces</xsl:text><xsl:text>&#x0A;</xsl:text>
    <xsl:for-each select="//interface[@rest='managed']">
        <xsl:variable name="interfaceSplittedByLetters">
            <xsl:call-template name="splitWord">
                <xsl:with-param name="word" select="@name"/>
            </xsl:call-template>
        </xsl:variable>

        <xsl:variable name="funcName">
            <xsl:call-template name="replaceUppercaseWithUnderscore">
                <xsl:with-param name="lettersNodeSet" select="$interfaceSplittedByLetters"/>
                <xsl:with-param name="letter" select="letter"/>
                <xsl:with-param name="fSkipFirst" select="false"/>
            </xsl:call-template>
        </xsl:variable>

        <xsl:variable name="module">
            <xsl:choose>
                <xsl:when test="starts-with($funcName, 'i_')">
                    <xsl:value-of select="substring($funcName,3)"/>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:value-of select="substring($funcName,2)"/>
                </xsl:otherwise>
            </xsl:choose>
        </xsl:variable>
        <xsl:value-of select="concat('from vbox_server.models.', $module, ' import ', substring(@name,2))"/>
        <xsl:text>&#x0A;</xsl:text>
    </xsl:for-each>
    <xsl:text>&#x0A;</xsl:text><xsl:text>&#x0A;</xsl:text>
  <xsl:apply-templates/>
</xsl:template>


<!-- - - - - - - - - - - - - - - - - - - - - -
  template createGeneralConversionFunc
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template name="createGeneralConversionFunc">
    <xsl:param name="functionName" />
    <xsl:param name="interfaceName" />

    <xsl:variable name="vboxObjectName" select="concat('oVBox', substring($interfaceName,2))"/>
    <xsl:variable name="swaggerObjectName" select="concat('o', substring($interfaceName,2))"/>
    <xsl:variable name="internalFuncDef" select="concat('def i_fill_', $functionName, '(', $vboxObjectName, ', select=None):')"/>
    <xsl:variable name="internalPartFuncCall" select="concat($swaggerObjectName, ' = ', 'i_fill_partial_', $functionName, '(', $vboxObjectName, ', select)')"/>
    <xsl:variable name="internalWholeFuncCall" select="concat($swaggerObjectName, ' = ', 'i_fill_whole_', $functionName, '(', $vboxObjectName, ')')"/>

    <xsl:value-of select="$internalFuncDef"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

    <xsl:value-of select="concat($aposDouble, $aposDouble, $aposDouble, 'Convert the passed VirtualBox object ',$vboxObjectName , ' with interface ', $interfaceName, 
    ' into Swagger object ', $swaggerObjectName, $aposDouble, $aposDouble, $aposDouble)"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

    <xsl:text>logging.info('Enter function ')</xsl:text>

    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:value-of select="concat($swaggerObjectName, ' = ', substring(@name,2), '()')"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:text>try:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>

    <xsl:text>if </xsl:text><xsl:value-of select="$vboxObjectName"/><xsl:text> is not None:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$sixSpaces"/>
    <xsl:text>if select is not None and len(select)>0:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>
    <xsl:value-of select="$internalPartFuncCall"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$sixSpaces"/>
    <xsl:text>else:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>
    <xsl:value-of select="$internalWholeFuncCall"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:text>except Exception as e:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>logging.info('Abnormal function exit')</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:value-of select="concat($swaggerObjectName, ' = None')"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:value-of select="concat('text = ', $apos, 'Exception trying to convert the VirtualBox object ',$vboxObjectName , ' into Swagger object ',$swaggerObjectName, '. ', $apos)"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>exceptionText = str(e)</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>raise Exception(text +  ' {Original: ' + exceptionText + '} ')</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

    <xsl:text>logging.info('Normal function exit')</xsl:text>

    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:value-of select="concat('return ', $swaggerObjectName)"/>
    <xsl:text>&#x0A;</xsl:text>
    <xsl:text>&#x0A;</xsl:text>
</xsl:template>


<!-- - - - - - - - - - - - - - - - - - - - - -
  template createWholeConversionFunc
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template name="createWholeConversionFunc">
    <xsl:param name="attrNodeSet" />
    <xsl:param name="functionName" />
    <xsl:param name="interfaceName" />

    <xsl:variable name="vboxObjectName" select="concat('oVBox', substring($interfaceName,2))"/>
    <xsl:variable name="swaggerObjectName" select="concat('o', substring($interfaceName,2))"/>
    <xsl:variable name="internalFuncDef" select="concat('def i_fill_whole_', $functionName, '(', $vboxObjectName, '):')"/>

    <xsl:value-of select="$internalFuncDef"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

    <xsl:text>logging.info('Enter function ')</xsl:text>

    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

    <xsl:value-of select="concat($swaggerObjectName, ' = ', substring(@name,2), '()')"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:text>try:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>if </xsl:text><xsl:value-of select="$vboxObjectName"/><xsl:text> is not None:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$sixSpaces"/>

    <xsl:choose>
        <xsl:when test="count($attrNodeSet)!=0">
            <xsl:for-each select="$attrNodeSet[not(@rest='suppress')]">
                <xsl:variable name="type">
                    <xsl:choose>
                    <xsl:when test="@type">
                        <xsl:value-of select="@type"/>
                    </xsl:when>
                    <xsl:when test="@rest and @rest!=''">
                        <xsl:value-of select="@rest"/>
                    </xsl:when>
                    <xsl:otherwise>
                        <xsl:value-of select="@type"/>
                    </xsl:otherwise>
                    </xsl:choose>
                </xsl:variable>
                <xsl:variable name="simpleIDLType" select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@idlname"/>
                <xsl:if test="$type=$simpleIDLType or key('G_keyInterfacesByRESTSupport', $type) or key('G_keyEnumsByName', $type)">
                    <xsl:call-template name="attribute">
                        <xsl:with-param name="suppressNotImplementedException" select="true()" />
                        <xsl:with-param name="vboxObjectName" select="$vboxObjectName" />
                        <xsl:with-param name="swaggerObjectName" select="$swaggerObjectName" />
                        <xsl:with-param name="functionName" select="$functionName" />
                        <xsl:with-param name="startIndent" select="$sixSpaces" />
                    </xsl:call-template>
                </xsl:if>
            </xsl:for-each>
        </xsl:when>
        <xsl:otherwise>
            <xsl:value-of select="concat('logging.info(', $apos, 'No attributes was found in the object ', substring(@name,2), $apos, ')')"/>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$sixSpaces"/>
        </xsl:otherwise>
    </xsl:choose>

    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:text>except Exception as e:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>logging.info('Abnormal function exit')</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:value-of select="concat($swaggerObjectName, ' = None')"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:value-of select="concat('text = ', $apos, 'Exception trying to fill the object ', $swaggerObjectName, '. ', $apos)"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>exceptionText = str(e)</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>raise Exception(text +  ' {Original: ' + exceptionText + '} ')</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

     <xsl:text>logging.info('Normal function exit')</xsl:text>

    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:value-of select="concat('return ', $swaggerObjectName)"/>
    <xsl:text>&#x0A;</xsl:text>
    <xsl:text>&#x0A;</xsl:text>
</xsl:template>


<!-- - - - - - - - - - - - - - - - - - - - - -
  template createPartialConversionFunc
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template name="createPartialConversionFunc">
    <xsl:param name="attrNodeSet" />
    <xsl:param name="functionName" />
    <xsl:param name="interfaceName" />

    <xsl:variable name="vboxObjectName" select="concat('oVBox', substring($interfaceName,2))"/>
    <xsl:variable name="swaggerObjectName" select="concat('o', substring($interfaceName,2))"/>
    <xsl:variable name="internalFuncDef" select="concat('def i_fill_partial_', $functionName, '(', $vboxObjectName, ', select):')"/>

    <xsl:value-of select="$internalFuncDef"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

    <xsl:text>logging.info('Enter function ')</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

    <xsl:value-of select="concat($swaggerObjectName, ' = ', substring(@name,2), '()')"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:text>try:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>if </xsl:text><xsl:value-of select="$vboxObjectName"/><xsl:text> is not None:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$sixSpaces"/>

    <xsl:choose>
        <xsl:when test="count($attrNodeSet)!=0">
            <xsl:text>olAttributesList = list()</xsl:text>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$sixSpaces"/>
            <xsl:text>if select is not None and len(select) > 0:</xsl:text>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>
            <xsl:text>olAttributesList = select.split(',')</xsl:text>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>
            <xsl:text>logging.info(olAttributesList)</xsl:text>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$eightSpaces"/>
            <xsl:text>for attr in olAttributesList:</xsl:text>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$tenSpaces"/>
            <xsl:text>currAttr = attr</xsl:text>
            <xsl:for-each select="$attrNodeSet[not(@rest='suppress')]">
                <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$tenSpaces"/>
                <xsl:variable name="type">
                    <xsl:choose>
                    <xsl:when test="@type">
                        <xsl:value-of select="@type"/>
                    </xsl:when>
                    <xsl:when test="@rest and @rest!=''">
                        <xsl:value-of select="@rest"/>
                    </xsl:when>
                    <xsl:otherwise>
                        <xsl:value-of select="@type"/>
                    </xsl:otherwise>
                    </xsl:choose>
                </xsl:variable>
                <xsl:variable name="simpleIDLType" select="exsl:node-set($G_aSwaggerTypes)/type[@idlname=$type]/@idlname"/>
                <xsl:if test="$type=$simpleIDLType or key('G_keyInterfacesByRESTSupport', $type) or key('G_keyEnumsByName', $type)">
                    <xsl:value-of  select="concat('if currAttr==', $apos, @name, $apos, ':')"/>
                    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twelveSpaces"/>
                    <xsl:call-template name="attribute">
                        <xsl:with-param name="suppressNotImplementedException" select="false()" />
                        <xsl:with-param name="vboxObjectName" select="$vboxObjectName" />
                        <xsl:with-param name="swaggerObjectName" select="$swaggerObjectName" />
                        <xsl:with-param name="functionName" select="$functionName" />
                        <xsl:with-param name="startIndent" select="$twelveSpaces" />
                    </xsl:call-template>
                </xsl:if>
            </xsl:for-each>
        </xsl:when>
        <xsl:otherwise>
            <xsl:value-of select="concat('logging.info(', $apos, 'No attributes was found in the object ', substring(@name,2), $apos, ')')"/>
            <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$sixSpaces"/>
        </xsl:otherwise>
    </xsl:choose>

    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:text>except Exception as e:</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>logging.info('Abnormal function exit')</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:value-of select="concat($swaggerObjectName, ' = None')"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:value-of select="concat('text = ', $apos, 'Exception trying to fill the object ', $swaggerObjectName, '. ', $apos)"/>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>exceptionText = str(e)</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$fourSpaces"/>
    <xsl:text>raise Exception(text +  ' {Original: ' + exceptionText + '} ')</xsl:text>
    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>

    <xsl:text>logging.info('Normal function exit')</xsl:text>

    <xsl:text>&#x0A;</xsl:text><xsl:value-of select="$twoSpaces"/>
    <xsl:value-of select="concat('return ', $swaggerObjectName)"/>
    <xsl:text>&#x0A;</xsl:text>
    <xsl:text>&#x0A;</xsl:text>
</xsl:template>


<!-- - - - - - - - - - - - - - - - - - - - - -
  interface match
- - - - - - - - - - - - - - - - - - - - - - -->
<xsl:template match="interface[@rest='managed']">

    <xsl:variable name="interfaceSplittedByLetters">
        <xsl:call-template name="splitWord">
            <xsl:with-param name="word" select="@name"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="funcName">
        <xsl:call-template name="replaceUppercaseWithUnderscore">
            <xsl:with-param name="lettersNodeSet" select="$interfaceSplittedByLetters"/>
            <xsl:with-param name="letter" select="letter"/>
            <xsl:with-param name="fSkipFirst" select="false"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:variable name="coreFuncName">
        <xsl:choose>
            <xsl:when test="starts-with($funcName, 'i_')">
                <xsl:value-of select="substring($funcName,3)"/>
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="substring($funcName,2)"/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:variable>

    <xsl:variable name="attrNodeSet" select="attribute" />

    <xsl:call-template name="createGeneralConversionFunc">
        <xsl:with-param name="functionName" select="$coreFuncName" />
        <xsl:with-param name="interfaceName" select="@name" />
    </xsl:call-template>

    <xsl:call-template name="createWholeConversionFunc">
        <xsl:with-param name="attrNodeSet" select="$attrNodeSet" />
        <xsl:with-param name="functionName" select="$coreFuncName" />
        <xsl:with-param name="interfaceName" select="@name" />
    </xsl:call-template>

    <xsl:call-template name="createPartialConversionFunc">
        <xsl:with-param name="attrNodeSet" select="$attrNodeSet" />
        <xsl:with-param name="functionName" select="$coreFuncName" />
        <xsl:with-param name="interfaceName" select="@name" />
    </xsl:call-template>

</xsl:template>

</xsl:stylesheet>
