# -*- coding: utf-8 -*-
# $Id: wuihlpform.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Web-UI - Form Helpers.
"""

__copyright__ = \
"""
Copyright (C) 2012-2024 Oracle and/or its affiliates.

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

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 106320 $"

# Standard python imports.
import copy;
import sys;

# Validation Kit imports.
from common                             import utils;
from common.webutils                    import escapeAttr, escapeElem;
from testmanager                        import config;
from testmanager.core.schedgroup        import SchedGroupMemberData, SchedGroupDataEx;
from testmanager.core.testcaseargs      import TestCaseArgsData;
from testmanager.core.testgroup         import TestGroupMemberData, TestGroupDataEx;
from testmanager.core.testbox           import TestBoxDataForSchedGroup;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    unicode = str;  # pylint: disable=redefined-builtin,invalid-name


class WuiHlpForm(object):
    """
    Helper for constructing a form.
    """

    ksItemsList = 'ksItemsList'

    ksOnSubmit_AddReturnToFieldWithCurrentUrl = '+AddReturnToFieldWithCurrentUrl+';

    def __init__(self, sId, sAction, dErrors = None, fReadOnly = False, sOnSubmit = None):
        self._fFinalized = False;
        self._fReadOnly  = fReadOnly;
        self._dErrors    = dErrors if dErrors is not None else {};

        if sOnSubmit == self.ksOnSubmit_AddReturnToFieldWithCurrentUrl:
            sOnSubmit = u'return addRedirectToInputFieldWithCurrentUrl(this)';
        if sOnSubmit is None:   sOnSubmit = u'';
        else:                   sOnSubmit = u' onsubmit=\"%s\"' % (escapeAttr(sOnSubmit),);

        self._sBody      = u'\n' \
                           u'<div id="%s" class="tmform">\n' \
                           u'  <form action="%s" method="post"%s>\n' \
                           u'    <ul>\n' \
                         % (sId, sAction, sOnSubmit);

    def _add(self, sText):
        """Internal worker for appending text to the body."""
        assert not self._fFinalized;
        if not self._fFinalized:
            self._sBody += utils.toUnicode(sText, errors='ignore');
            return True;
        return False;

    def _escapeErrorText(self, sText):
        """Escapes error text, preserving some predefined HTML tags."""
        if sText.find('<br>') >= 0:
            asParts = sText.split('<br>');
            for i, _ in enumerate(asParts):
                asParts[i] = escapeElem(asParts[i].strip());
            sText = '<br>\n'.join(asParts);
        else:
            sText = escapeElem(sText);
        return sText;

    def _addLabel(self, sName, sLabel, sDivSubClass = 'normal'):
        """Internal worker for adding a label."""
        if sName in self._dErrors:
            sError = self._dErrors[sName];
            if utils.isString(sError):          # List error trick (it's an associative array).
                return self._add(u'      <li>\n'
                                 u'        <div class="tmform-field"><div class="tmform-field-%s">\n'
                                 u'          <label for="%s" class="tmform-error-label">%s\n'
                                 u'              <span class="tmform-error-desc">%s</span>\n'
                                 u'          </label>\n'
                                 % (escapeAttr(sDivSubClass), escapeAttr(sName), escapeElem(sLabel),
                                    self._escapeErrorText(sError), ) );
        return self._add(u'      <li>\n'
                         u'        <div class="tmform-field"><div class="tmform-field-%s">\n'
                         u'          <label  for="%s">%s</label>\n'
                         % (escapeAttr(sDivSubClass), escapeAttr(sName), escapeElem(sLabel)) );


    def finalize(self):
        """
        Finalizes the form and returns the body.
        """
        if not self._fFinalized:
            self._add(u'    </ul>\n'
                      u'  </form>\n'
                      u'</div>\n'
                      u'<div class="clear"></div>\n' );
        return self._sBody;

    def addTextHidden(self, sName, sValue, sExtraAttribs = ''):
        """Adds a hidden text input."""
        return self._add(u'      <div class="tmform-field-hidden">\n'
                         u'        <input name="%s" id="%s" type="text" hidden%s value="%s" class="tmform-hidden">\n'
                         u'      </div>\n'
                         u'    </li>\n'
                          % ( escapeAttr(sName), escapeAttr(sName), sExtraAttribs, escapeElem(str(sValue)) ));
    #
    # Non-input stuff.
    #
    def addNonText(self, sValue, sLabel, sName = 'non-text', sPostHtml = ''):
        """Adds a read-only text input."""
        self._addLabel(sName, sLabel, 'string');
        if sValue is None: sValue = '';
        return self._add(u'          <p>%s%s</p>\n'
                         u'        </div></div>\n'
                         u'      </li>\n'
                         % (escapeElem(unicode(sValue)), sPostHtml ));

    def addRawHtml(self, sRawHtml, sLabel, sName = 'raw-html'):
        """Adds a read-only text input."""
        self._addLabel(sName, sLabel, 'string');
        self._add(sRawHtml);
        return self._add(u'        </div></div>\n'
                         u'      </li>\n');


    #
    # Text input fields.
    #
    def addText(self, sName, sValue, sLabel, sSubClass = 'string', sExtraAttribs = '', sPostHtml = ''):
        """Adds a text input."""
        if self._fReadOnly:
            return self.addTextRO(sName, sValue, sLabel, sSubClass, sExtraAttribs);
        if sSubClass not in ('int', 'long', 'string', 'uuid', 'timestamp', 'wide'): raise Exception(sSubClass);
        self._addLabel(sName, sLabel, sSubClass);
        if sValue is None: sValue = '';
        return self._add(u'          <input name="%s" id="%s" type="text"%s value="%s">%s\n'
                         u'        </div></div>\n'
                         u'      </li>\n'
                         % ( escapeAttr(sName), escapeAttr(sName), sExtraAttribs, escapeAttr(unicode(sValue)), sPostHtml ));

    def addTextRO(self, sName, sValue, sLabel, sSubClass = 'string', sExtraAttribs = '', sPostHtml = ''):
        """Adds a read-only text input."""
        if sSubClass not in ('int', 'long', 'string', 'uuid', 'timestamp', 'wide'): raise Exception(sSubClass);
        self._addLabel(sName, sLabel, sSubClass);
        if sValue is None: sValue = '';
        return self._add(u'          <input name="%s" id="%s" type="text" readonly%s value="%s" class="tmform-input-readonly">'
                         u'%s\n'
                         u'        </div></div>\n'
                         u'      </li>\n'
                         % ( escapeAttr(sName), escapeAttr(sName), sExtraAttribs, escapeAttr(unicode(sValue)), sPostHtml ));

    def addWideText(self, sName, sValue, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds a wide text input."""
        return self.addText(sName, sValue, sLabel, 'wide', sExtraAttribs, sPostHtml = sPostHtml);

    def addWideTextRO(self, sName, sValue, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds a wide read-only text input."""
        return self.addTextRO(sName, sValue, sLabel, 'wide', sExtraAttribs, sPostHtml = sPostHtml);

    def _adjustMultilineTextAttribs(self, sExtraAttribs, sValue):
        """ Internal helper for setting good default sizes for textarea based on content."""
        if sExtraAttribs.find('cols') < 0 and sExtraAttribs.find('width') < 0:
            sExtraAttribs = 'cols="96%" ' + sExtraAttribs;

        if sExtraAttribs.find('rows') < 0 and sExtraAttribs.find('width') < 0:
            if sValue is None:  sValue = '';
            else:               sValue = sValue.strip();

            cRows = sValue.count('\n') + (not sValue.endswith('\n'));
            if cRows * 80 < len(sValue):
                cRows += 2;
            cRows = max(min(cRows, 16), 2);
            sExtraAttribs = ('rows="%s" ' % (cRows,)) + sExtraAttribs;

        return sExtraAttribs;

    def addMultilineText(self, sName, sValue, sLabel, sSubClass = 'string', sExtraAttribs = ''):
        """Adds a multiline text input."""
        if self._fReadOnly:
            return self.addMultilineTextRO(sName, sValue, sLabel, sSubClass, sExtraAttribs);
        if sSubClass not in ('int', 'long', 'string', 'uuid', 'timestamp'): raise Exception(sSubClass)
        self._addLabel(sName, sLabel, sSubClass)
        if sValue is None: sValue = '';
        sNewValue = unicode(sValue) if not isinstance(sValue, list) else '\n'.join(sValue)
        return self._add(u'          <textarea name="%s" id="%s" %s>%s</textarea>\n'
                         u'        </div></div>\n'
                         u'      </li>\n'
                         % ( escapeAttr(sName), escapeAttr(sName), self._adjustMultilineTextAttribs(sExtraAttribs, sNewValue),
                             escapeElem(sNewValue)))

    def addMultilineTextRO(self, sName, sValue, sLabel, sSubClass = 'string', sExtraAttribs = ''):
        """Adds a multiline read-only text input."""
        if sSubClass not in ('int', 'long', 'string', 'uuid', 'timestamp'): raise Exception(sSubClass)
        self._addLabel(sName, sLabel, sSubClass)
        if sValue is None: sValue = '';
        sNewValue = unicode(sValue) if not isinstance(sValue, list) else '\n'.join(sValue)
        return self._add(u'          <textarea name="%s" id="%s" readonly %s>%s</textarea>\n'
                         u'        </div></div>\n'
                         u'      </li>\n'
                         % ( escapeAttr(sName), escapeAttr(sName), self._adjustMultilineTextAttribs(sExtraAttribs, sNewValue),
                             escapeElem(sNewValue)))

    def addInt(self, sName, iValue, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds an integer input."""
        return self.addText(sName, unicode(iValue), sLabel, 'int', sExtraAttribs, sPostHtml = sPostHtml);

    def addIntRO(self, sName, iValue, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds an integer input."""
        return self.addTextRO(sName, unicode(iValue), sLabel, 'int', sExtraAttribs, sPostHtml = sPostHtml);

    def addLong(self, sName, lValue, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds a long input."""
        return self.addText(sName, unicode(lValue), sLabel, 'long', sExtraAttribs, sPostHtml = sPostHtml);

    def addLongRO(self, sName, lValue, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds a long input."""
        return self.addTextRO(sName, unicode(lValue), sLabel, 'long', sExtraAttribs, sPostHtml = sPostHtml);

    def addUuid(self, sName, uuidValue, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds an UUID input."""
        return self.addText(sName, unicode(uuidValue), sLabel, 'uuid', sExtraAttribs, sPostHtml = sPostHtml);

    def addUuidRO(self, sName, uuidValue, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds a read-only UUID input."""
        return self.addTextRO(sName, unicode(uuidValue), sLabel, 'uuid', sExtraAttribs, sPostHtml = sPostHtml);

    def addTimestampRO(self, sName, sTimestamp, sLabel, sExtraAttribs = '', sPostHtml = ''):
        """Adds a read-only database string timstamp input."""
        return self.addTextRO(sName, sTimestamp, sLabel, 'timestamp', sExtraAttribs, sPostHtml = sPostHtml);


    #
    # Text areas.
    #


    #
    # Combo boxes.
    #
    def addComboBox(self, sName, sSelected, sLabel, aoOptions, sExtraAttribs = '', sPostHtml = ''):
        """Adds a combo box."""
        if self._fReadOnly:
            return self.addComboBoxRO(sName, sSelected, sLabel, aoOptions, sExtraAttribs, sPostHtml);
        self._addLabel(sName, sLabel, 'combobox');
        self._add('          <select name="%s" id="%s" class="tmform-combobox"%s>\n'
                  % (escapeAttr(sName), escapeAttr(sName), sExtraAttribs));
        sSelected = unicode(sSelected);
        for iValue, sText, _ in aoOptions:
            sValue = unicode(iValue);
            self._add('            <option value="%s"%s>%s</option>\n'
                      % (escapeAttr(sValue), ' selected' if sValue == sSelected else '',
                         escapeElem(sText)));
        return self._add(u'          </select>' + sPostHtml + '\n'
                         u'        </div></div>\n'
                         u'      </li>\n');

    def addComboBoxRO(self, sName, sSelected, sLabel, aoOptions, sExtraAttribs = '', sPostHtml = ''):
        """Adds a read-only combo box."""
        self.addTextHidden(sName, sSelected);
        self._addLabel(sName, sLabel, 'combobox-readonly');
        self._add(u'          <select name="%s" id="%s" disabled class="tmform-combobox"%s>\n'
                  % (escapeAttr(sName), escapeAttr(sName), sExtraAttribs));
        sSelected = unicode(sSelected);
        for iValue, sText, _ in aoOptions:
            sValue = unicode(iValue);
            self._add('            <option value="%s"%s>%s</option>\n'
                      % (escapeAttr(sValue), ' selected' if sValue == sSelected else '',
                         escapeElem(sText)));
        return self._add(u'          </select>' + sPostHtml + '\n'
                         u'        </div></div>\n'
                         u'      </li>\n');

    #
    # Check boxes.
    #
    @staticmethod
    def _reinterpretBool(fValue):
        """Reinterprets a value as a boolean type."""
        if fValue is not type(True):
            if fValue is None:
                fValue = False;
            elif str(fValue) in ('True', 'true', '1'):
                fValue = True;
            else:
                fValue = False;
        return fValue;

    def addCheckBox(self, sName, fChecked, sLabel, sExtraAttribs = ''):
        """Adds an check box."""
        if self._fReadOnly:
            return self.addCheckBoxRO(sName, fChecked, sLabel, sExtraAttribs);
        self._addLabel(sName, sLabel, 'checkbox');
        fChecked = self._reinterpretBool(fChecked);
        return self._add(u'          <input name="%s" id="%s" type="checkbox"%s%s value="1" class="tmform-checkbox">\n'
                         u'        </div></div>\n'
                         u'      </li>\n'
                         % (escapeAttr(sName), escapeAttr(sName), ' checked' if fChecked else '', sExtraAttribs));

    def addCheckBoxRO(self, sName, fChecked, sLabel, sExtraAttribs = ''):
        """Adds an readonly check box."""
        self._addLabel(sName, sLabel, 'checkbox');
        fChecked = self._reinterpretBool(fChecked);
        # Hack Alert! The onclick and onkeydown are for preventing editing and fake readonly/disabled.
        return self._add(u'          <input name="%s" id="%s" type="checkbox"%s readonly%s value="1" class="readonly"\n'
                         u'              onclick="return false" onkeydown="return false">\n'
                         u'        </div></div>\n'
                         u'      </li>\n'
                         % (escapeAttr(sName), escapeAttr(sName), ' checked' if fChecked else '', sExtraAttribs));

    #
    # List of items to check
    #
    def _addList(self, sName, aoRows, sLabel, fUseTable = False, sId = 'dummy', sExtraAttribs = ''):
        """
        Adds a list of items to check.

        @param sName  Name of HTML form element
        @param aoRows  List of [sValue, fChecked, sName] sub-arrays.
        @param sLabel Label of HTML form element
        """
        fReadOnly = self._fReadOnly; ## @todo add this as a parameter.
        if fReadOnly:
            sExtraAttribs += ' readonly onclick="return false" onkeydown="return false"';

        self._addLabel(sName, sLabel, 'list');
        if not aoRows:
            return self._add('No items</div></div></li>')
        sNameEscaped = escapeAttr(sName);

        self._add('          <div class="tmform-checkboxes-container" id="%s">\n' % (escapeAttr(sId),));
        if fUseTable:
            self._add('          <table>\n');
            for asRow in aoRows:
                assert len(asRow) == 3; # Don't allow sloppy input data!
                fChecked = self._reinterpretBool(asRow[1])
                self._add(u'            <tr>\n'
                          u'              <td><input type="checkbox" name="%s" value="%s"%s%s></td>\n'
                          u'              <td>%s</td>\n'
                          u'            </tr>\n'
                          % ( sNameEscaped, escapeAttr(unicode(asRow[0])), ' checked' if fChecked else '', sExtraAttribs,
                               escapeElem(unicode(asRow[2])), ));
            self._add(u'          </table>\n');
        else:
            for asRow in aoRows:
                assert len(asRow) == 3; # Don't allow sloppy input data!
                fChecked = self._reinterpretBool(asRow[1])
                self._add(u'            <div class="tmform-checkbox-holder">'
                          u'<input type="checkbox" name="%s" value="%s"%s%s> %s</input></div>\n'
                          % ( sNameEscaped, escapeAttr(unicode(asRow[0])), ' checked' if fChecked else '', sExtraAttribs,
                              escapeElem(unicode(asRow[2])),));
        return self._add(u'        </div></div></div>\n'
                         u'      </li>\n');


    def addListOfOsArches(self, sName, aoOsArches, sLabel, sExtraAttribs = ''):
        """
        List of checkboxes for OS/ARCH selection.
        asOsArches is a list of [sValue, fChecked, sName] sub-arrays.
        """
        return self._addList(sName, aoOsArches, sLabel, fUseTable = False, sId = 'tmform-checkbox-list-os-arches',
                             sExtraAttribs = sExtraAttribs);

    def addListOfTypes(self, sName, aoTypes, sLabel, sExtraAttribs = ''):
        """
        List of checkboxes for build type selection.
        aoTypes is a list of [sValue, fChecked, sName] sub-arrays.
        """
        return self._addList(sName, aoTypes, sLabel, fUseTable = False, sId = 'tmform-checkbox-list-build-types',
                             sExtraAttribs = sExtraAttribs);

    def addListOfTestCases(self, sName, aoTestCases, sLabel, sExtraAttribs = ''):
        """
        List of checkboxes for test box (dependency) selection.
        aoTestCases is a list of [sValue, fChecked, sName] sub-arrays.
        """
        return self._addList(sName, aoTestCases, sLabel, fUseTable = False, sId = 'tmform-checkbox-list-testcases',
                             sExtraAttribs = sExtraAttribs);

    def addListOfResources(self, sName, aoTestCases, sLabel, sExtraAttribs = ''):
        """
        List of checkboxes for resource selection.
        aoTestCases is a list of [sValue, fChecked, sName] sub-arrays.
        """
        return self._addList(sName, aoTestCases, sLabel, fUseTable = False, sId = 'tmform-checkbox-list-resources',
                             sExtraAttribs = sExtraAttribs);

    def addListOfTestGroups(self, sName, aoTestGroups, sLabel, sExtraAttribs = ''):
        """
        List of checkboxes for test group selection.
        aoTestGroups is a list of [sValue, fChecked, sName] sub-arrays.
        """
        return self._addList(sName, aoTestGroups, sLabel, fUseTable = False, sId = 'tmform-checkbox-list-testgroups',
                             sExtraAttribs = sExtraAttribs);

    def addListOfTestCaseArgs(self, sName, aoVariations, sLabel): # pylint: disable=too-many-statements
        """
        Adds a list of test case argument variations to the form.

        @param sName        Name of HTML form element
        @param aoVariations List of TestCaseArgsData instances.
        @param sLabel       Label of HTML form element
        """
        self._addLabel(sName, sLabel);

        sTableId = u'TestArgsExtendingListRoot';
        fReadOnly = self._fReadOnly;  ## @todo argument?
        sReadOnlyAttr = u' readonly class="tmform-input-readonly"' if fReadOnly else '';

        sHtml  = u'<li>\n'

        #
        # Define javascript function for extending the list of test case
        # variations.  Doing it here so we can use the python constants. This
        # also permits multiple argument lists on one page should that ever be
        # required...
        #
        if not fReadOnly:
            sHtml += u'<script type="text/javascript">\n'
            sHtml += u'\n';
            sHtml += u'g_%s_aItems = { %s };\n' % (sName, ', '.join(('%s: 1' % (i,)) for i in range(len(aoVariations))),);
            sHtml += u'g_%s_cItems = %s;\n' % (sName, len(aoVariations),);
            sHtml += u'g_%s_iIdMod = %s;\n' % (sName, len(aoVariations) + 32);
            sHtml += u'\n';
            sHtml += u'function %s_removeEntry(sId)\n' % (sName,);
            sHtml += u'{\n';
            sHtml += u'    if (g_%s_cItems > 1)\n' % (sName,);
            sHtml += u'    {\n';
            sHtml += u'        g_%s_cItems--;\n' % (sName,);
            sHtml += u'        delete g_%s_aItems[sId];\n' % (sName,);
            sHtml += u'        setElementValueToKeyList(\'%s\', g_%s_aItems);\n' % (sName, sName);
            sHtml += u'\n';
            for iInput in range(8):
                sHtml += u'        removeHtmlNode(\'%s[\' + sId + \'][%s]\');\n' % (sName, iInput,);
            sHtml += u'    }\n';
            sHtml += u'}\n';
            sHtml += u'\n';
            sHtml += u'function %s_extendListEx(sSubName, cGangMembers, cSecTimeout, sArgs, sTestBoxReqExpr, sBuildReqExpr)\n' \
                     % (sName,);
            sHtml += u'{\n';
            sHtml += u'    var oElement = document.getElementById(\'%s\');\n' % (sTableId,);
            sHtml += u'    var oTBody   = document.createElement(\'tbody\');\n';
            sHtml += u'    var sHtml    = \'\';\n';
            sHtml += u'    var sId;\n';
            sHtml += u'\n';
            sHtml += u'    g_%s_iIdMod += 1;\n' % (sName,);
            sHtml += u'    sId = g_%s_iIdMod.toString();\n' % (sName,);

            oVarDefaults = TestCaseArgsData();
            oVarDefaults.convertToParamNull();
            sHtml += u'\n';
            sHtml += u'    sHtml += \'<tr class="tmform-testcasevars-first-row">\';\n';
            sHtml += u'    sHtml += \'  <td>Sub-Name:</td>\';\n';
            sHtml += u'    sHtml += \'  <td class="tmform-field-subname">' \
                      '<input name="%s[\' + sId + \'][%s]" id="%s[\' + sId + \'][0]" value="\' + sSubName + \'"></td>\';\n' \
                   % (sName, TestCaseArgsData.ksParam_sSubName, sName,);
            sHtml += u'    sHtml += \'  <td>Gang Members:</td>\';\n';
            sHtml += u'    sHtml += \'  <td class="tmform-field-tiny-int">' \
                      '<input name="%s[\' + sId + \'][%s]" id="%s[\' + sId + \'][0]" value="\' + cGangMembers + \'"></td>\';\n' \
                   % (sName, TestCaseArgsData.ksParam_cGangMembers, sName,);
            sHtml += u'    sHtml += \'  <td>Timeout:</td>\';\n';
            sHtml += u'    sHtml += \'  <td class="tmform-field-int">' \
                     u'<input name="%s[\' + sId + \'][%s]" id="%s[\' + sId + \'][1]" value="\'+ cSecTimeout + \'"></td>\';\n' \
                   % (sName, TestCaseArgsData.ksParam_cSecTimeout, sName,);
            sHtml += u'    sHtml += \'  <td><a href="#" onclick="%s_removeEntry(\\\'\' + sId + \'\\\');"> Remove</a></td>\';\n' \
                   % (sName, );
            sHtml += u'    sHtml += \'  <td></td>\';\n';
            sHtml += u'    sHtml += \'</tr>\';\n'
            sHtml += u'\n';
            sHtml += u'    sHtml += \'<tr class="tmform-testcasevars-inner-row">\';\n';
            sHtml += u'    sHtml += \'  <td>Arguments:</td>\';\n';
            sHtml += u'    sHtml += \'  <td class="tmform-field-wide100" colspan="6">' \
                     u'<input name="%s[\' + sId + \'][%s]" id="%s[\' + sId + \'][2]" value="\' + sArgs + \'"></td>\';\n' \
                   % (sName, TestCaseArgsData.ksParam_sArgs, sName,);
            sHtml += u'    sHtml += \'  <td></td>\';\n';
            sHtml += u'    sHtml += \'</tr>\';\n'
            sHtml += u'\n';
            sHtml += u'    sHtml += \'<tr class="tmform-testcasevars-inner-row">\';\n';
            sHtml += u'    sHtml += \'  <td>TestBox Reqs:</td>\';\n';
            sHtml += u'    sHtml += \'  <td class="tmform-field-wide100" colspan="6">' \
                     u'<input name="%s[\' + sId + \'][%s]" id="%s[\' + sId + \'][2]" value="\' + sTestBoxReqExpr' \
                     u' + \'"></td>\';\n' \
                   % (sName, TestCaseArgsData.ksParam_sTestBoxReqExpr, sName,);
            sHtml += u'    sHtml += \'  <td></td>\';\n';
            sHtml += u'    sHtml += \'</tr>\';\n'
            sHtml += u'\n';
            sHtml += u'    sHtml += \'<tr class="tmform-testcasevars-final-row">\';\n';
            sHtml += u'    sHtml += \'  <td>Build Reqs:</td>\';\n';
            sHtml += u'    sHtml += \'  <td class="tmform-field-wide100" colspan="6">' \
                     u'<input name="%s[\' + sId + \'][%s]" id="%s[\' + sId + \'][2]" value="\' + sBuildReqExpr + \'"></td>\';\n' \
                   % (sName, TestCaseArgsData.ksParam_sBuildReqExpr, sName,);
            sHtml += u'    sHtml += \'  <td></td>\';\n';
            sHtml += u'    sHtml += \'</tr>\';\n'
            sHtml += u'\n';
            sHtml += u'    oTBody.id = \'%s[\' + sId + \'][6]\';\n' % (sName,);
            sHtml += u'    oTBody.innerHTML = sHtml;\n';
            sHtml += u'\n';
            sHtml += u'    oElement.appendChild(oTBody);\n';
            sHtml += u'\n';
            sHtml += u'    g_%s_aItems[sId] = 1;\n' % (sName,);
            sHtml += u'    g_%s_cItems++;\n' % (sName,);
            sHtml += u'    setElementValueToKeyList(\'%s\', g_%s_aItems);\n' % (sName, sName);
            sHtml += u'}\n';
            sHtml += u'function %s_extendList()\n' % (sName,);
            sHtml += u'{\n';
            sHtml += u'    %s_extendListEx("%s", "%s", "%s", "%s", "%s", "%s");\n' % (sName,
                escapeAttr(unicode(oVarDefaults.sSubName)), escapeAttr(unicode(oVarDefaults.cGangMembers)),
                escapeAttr(unicode(oVarDefaults.cSecTimeout)), escapeAttr(oVarDefaults.sArgs),
                escapeAttr(oVarDefaults.sTestBoxReqExpr), escapeAttr(oVarDefaults.sBuildReqExpr), );
            sHtml += u'}\n';
            if config.g_kfVBoxSpecific:
                sSecTimeoutDef = escapeAttr(unicode(oVarDefaults.cSecTimeout));
                sHtml += u'function vbox_%s_add_uni()\n' % (sName,);
                sHtml += u'{\n';
                sHtml += u'    %s_extendListEx("1-raw", "1", "%s", "--cpu-counts 1 --virt-modes raw", ' \
                         u' "", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("1-hw", "1", "%s", "--cpu-counts 1 --virt-modes hwvirt", ' \
                         u' "fCpuHwVirt is True", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("1-np", "1", "%s", "--cpu-counts 1 --virt-modes hwvirt-np", ' \
                         u' "fCpuNestedPaging is True", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("1-nem", "1", "%s", "--cpu-counts 1 --virt-modes native-api", ' \
                         u' "fNativeApi is True", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'}\n';
                sHtml += u'function vbox_%s_add_uni_amd64()\n' % (sName,);
                sHtml += u'{\n';
                sHtml += u'    %s_extendListEx("1-hw", "1", "%s", "--cpu-counts 1 --virt-modes hwvirt", ' \
                         u' "fCpuHwVirt is True", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("1-np", "%s", "--cpu-counts 1 --virt-modes hwvirt-np", ' \
                         u' "fCpuNestedPaging is True", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("1-nem", "%s", "--cpu-counts 1 --virt-modes native-api", ' \
                         u' "fNativeApi is True", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'}\n';
                sHtml += u'function vbox_%s_add_smp()\n' % (sName,);
                sHtml += u'{\n';
                sHtml += u'    %s_extendListEx("2-hw", "1", "%s", "--cpu-counts 2 --virt-modes hwvirt",' \
                         u' "fCpuHwVirt is True and cCpus >= 2", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("2-np", "1", "%s", "--cpu-counts 2 --virt-modes hwvirt-np",' \
                         u' "fCpuNestedPaging is True and cCpus >= 2", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("2-nem", "1", "%s", "--cpu-counts 2 --virt-modes native-api",' \
                         u' "fNativeApi is True and cCpus >= 2", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("3-hw", "1", "%s", "--cpu-counts 3 --virt-modes hwvirt",' \
                         u' "fCpuHwVirt is True and cCpus >= 3", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("3-nem", "1", "%s", "--cpu-counts 3 --virt-modes native-api",' \
                         u' "fNativeApi is True and cCpus >= 3", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("4-np", "1", "%s", "--cpu-counts 4 --virt-modes hwvirt-np ",' \
                         u' "fCpuNestedPaging is True and cCpus >= 4", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'    %s_extendListEx("4-nem", "1", "%s", "--cpu-counts 4 --virt-modes native-api",' \
                         u' "fNativeApi is True and cCpus >= 4", "");\n' % (sName, sSecTimeoutDef);
                #sHtml += u'    %s_extendListEx("6-hw", "1", "%s", "--cpu-counts 6 --virt-modes hwvirt",' \
                #         u' "fCpuHwVirt is True and cCpus >= 6", "");\n' % (sName, sSecTimeoutDef);
                #sHtml += u'    %s_extendListEx("8-np", "1", "%s", "--cpu-counts 8 --virt-modes hwvirt-np",' \
                #         u' "fCpuNestedPaging is True and cCpus >= 8", "");\n' % (sName, sSecTimeoutDef);
                sHtml += u'}\n';
            sHtml += u'</script>\n';


        #
        # List current entries.
        #
        sHtml += u'<input type="hidden" name="%s" id="%s" value="%s">\n' \
               % (sName, sName, ','.join(unicode(i) for i in range(len(aoVariations))), );
        sHtml += u'  <table id="%s" class="tmform-testcasevars">\n' % (sTableId,)
        if not fReadOnly:
            sHtml += u'  <caption>\n' \
                     u'    <a href="#" onClick="%s_extendList()">Add</a>\n' % (sName,);
            if config.g_kfVBoxSpecific:
                sHtml += u'    [<a href="#" onClick="vbox_%s_add_uni()">Single CPU Variations</a>\n' % (sName,);
                sHtml += u'    <a href="#" onClick="vbox_%s_add_uni_amd64()">amd64</a>]\n' % (sName,);
                sHtml += u'    [<a href="#" onClick="vbox_%s_add_smp()">SMP Variations</a>]\n' % (sName,);
            sHtml += u'  </caption>\n';

        dSubErrors = {};
        if sName in self._dErrors  and  isinstance(self._dErrors[sName], dict):
            dSubErrors = self._dErrors[sName];

        for iVar, _ in enumerate(aoVariations):
            oVar = copy.copy(aoVariations[iVar]);
            oVar.convertToParamNull();

            sHtml += u'<tbody id="%s[%s][6]">\n' % (sName, iVar,)
            sHtml += u'  <tr class="tmform-testcasevars-first-row">\n' \
                     u'    <td>Sub-name:</td>' \
                     u'    <td class="tmform-field-subname"><input name="%s[%s][%s]" id="%s[%s][1]" value="%s"%s></td>\n' \
                     u'    <td>Gang Members:</td>' \
                     u'    <td class="tmform-field-tiny-int"><input name="%s[%s][%s]" id="%s[%s][1]" value="%s"%s></td>\n' \
                     u'    <td>Timeout:</td>' \
                     u'    <td class="tmform-field-int"><input name="%s[%s][%s]" id="%s[%s][2]" value="%s"%s></td>\n' \
                   % ( sName, iVar, TestCaseArgsData.ksParam_sSubName, sName, iVar, oVar.sSubName, sReadOnlyAttr,
                       sName, iVar, TestCaseArgsData.ksParam_cGangMembers, sName, iVar, oVar.cGangMembers, sReadOnlyAttr,
                       sName, iVar, TestCaseArgsData.ksParam_cSecTimeout,  sName, iVar,
                       utils.formatIntervalSeconds2(oVar.cSecTimeout), sReadOnlyAttr, );
            if not fReadOnly:
                sHtml += u'    <td><a href="#" onclick="%s_removeEntry(\'%s\');">Remove</a></td>\n' \
                       % (sName, iVar);
            else:
                sHtml += u'    <td></td>\n';
            sHtml += u'    <td class="tmform-testcasevars-stupid-border-column"></td>\n' \
                     u'  </tr>\n';

            sHtml += u'  <tr class="tmform-testcasevars-inner-row">\n' \
                     u'    <td>Arguments:</td>' \
                     u'    <td class="tmform-field-wide100" colspan="6">' \
                     u'<input name="%s[%s][%s]" id="%s[%s][3]" value="%s"%s></td>\n' \
                     u'    <td></td>\n' \
                     u'  </tr>\n' \
                   % ( sName, iVar, TestCaseArgsData.ksParam_sArgs, sName, iVar, escapeAttr(oVar.sArgs), sReadOnlyAttr)

            sHtml += u'  <tr class="tmform-testcasevars-inner-row">\n' \
                     u'    <td>TestBox Reqs:</td>' \
                     u'    <td class="tmform-field-wide100" colspan="6">' \
                     u'<input name="%s[%s][%s]" id="%s[%s][4]" value="%s"%s></td>\n' \
                     u'    <td></td>\n' \
                     u'  </tr>\n' \
                   % ( sName, iVar, TestCaseArgsData.ksParam_sTestBoxReqExpr, sName, iVar,
                       escapeAttr(oVar.sTestBoxReqExpr), sReadOnlyAttr)

            sHtml += u'  <tr class="tmform-testcasevars-final-row">\n' \
                     u'    <td>Build Reqs:</td>' \
                     u'    <td class="tmform-field-wide100" colspan="6">' \
                     u'<input name="%s[%s][%s]" id="%s[%s][5]" value="%s"%s></td>\n' \
                     u'    <td></td>\n' \
                     u'  </tr>\n' \
                   % ( sName, iVar, TestCaseArgsData.ksParam_sBuildReqExpr, sName, iVar,
                       escapeAttr(oVar.sBuildReqExpr), sReadOnlyAttr)


            if iVar in dSubErrors:
                sHtml += u'  <tr><td colspan="4"><p align="left" class="tmform-error-desc">%s</p></td></tr>\n' \
                       % (self._escapeErrorText(dSubErrors[iVar]),);

            sHtml += u'</tbody>\n';
        sHtml += u'  </table>\n'
        sHtml += u'</li>\n'

        return self._add(sHtml)

    def addListOfTestGroupMembers(self, sName, aoTestGroupMembers, aoAllTestCases, sLabel,  # pylint: disable=too-many-locals
                                  fReadOnly = True):
        """
        For WuiTestGroup.
        """
        assert len(aoTestGroupMembers) <= len(aoAllTestCases);
        self._addLabel(sName, sLabel);
        if not aoAllTestCases:
            return self._add('<li>No testcases.</li>\n')

        self._add(u'<input name="%s" type="hidden" value="%s">\n'
                  % ( TestGroupDataEx.ksParam_aidTestCases,
                      ','.join([unicode(oTestCase.idTestCase) for oTestCase in aoAllTestCases]), ));

        self._add(u'<table class="tmformtbl">\n'
                  u' <thead>\n'
                  u'  <tr>\n'
                  u'    <th rowspan="2"></th>\n'
                  u'    <th rowspan="2">Test Case</th>\n'
                  u'    <th rowspan="2">All Vars</th>\n'
                  u'    <th rowspan="2">Priority [0..31]</th>\n'
                  u'    <th colspan="4" align="center">Variations</th>\n'
                  u'  </tr>\n'
                  u'  <tr>\n'
                  u'    <th>Included</th>\n'
                  u'    <th>Gang size</th>\n'
                  u'    <th>Timeout</th>\n'
                  u'    <th>Arguments</th>\n'
                  u'  </tr>\n'
                  u' </thead>\n'
                  u' <tbody>\n'
                  );

        if self._fReadOnly:
            fReadOnly = True;
        sCheckBoxAttr = ' readonly onclick="return false" onkeydown="return false"' if fReadOnly else '';

        oDefMember = TestGroupMemberData();
        aoTestGroupMembers = list(aoTestGroupMembers); # Copy it so we can pop.
        for iTestCase, oTestCase in enumerate(aoAllTestCases):

            # Is it a member?
            oMember = None;
            for i, _ in enumerate(aoTestGroupMembers):
                if aoTestGroupMembers[i].oTestCase.idTestCase == oTestCase.idTestCase:
                    oMember = aoTestGroupMembers.pop(i);
                    break;

            # Start on the rows...
            sPrefix = u'%s[%d]' % (sName, oTestCase.idTestCase,);
            self._add(u'  <tr class="%s">\n'
                      u'    <td rowspan="%d">\n'
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # idTestCase
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # idTestGroup
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # tsExpire
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # tsEffective
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # uidAuthor
                      u'      <input name="%s" type="checkbox"%s%s value="%d" class="tmform-checkbox" title="#%d - %s">\n' #(list)
                      u'    </td>\n'
                      % ( 'tmodd' if iTestCase & 1 else 'tmeven',
                          len(oTestCase.aoTestCaseArgs),
                          sPrefix, TestGroupMemberData.ksParam_idTestCase,  oTestCase.idTestCase,
                          sPrefix, TestGroupMemberData.ksParam_idTestGroup, -1 if oMember is None else oMember.idTestGroup,
                          sPrefix, TestGroupMemberData.ksParam_tsExpire,    '' if oMember is None else oMember.tsExpire,
                          sPrefix, TestGroupMemberData.ksParam_tsEffective, '' if oMember is None else oMember.tsEffective,
                          sPrefix, TestGroupMemberData.ksParam_uidAuthor,   '' if oMember is None else oMember.uidAuthor,
                          TestGroupDataEx.ksParam_aoMembers, '' if oMember is None else ' checked', sCheckBoxAttr,
                          oTestCase.idTestCase, oTestCase.idTestCase, escapeElem(oTestCase.sName),
                          ));
            self._add(u'    <td rowspan="%d" align="left">%s</td>\n'
                      % ( len(oTestCase.aoTestCaseArgs), escapeElem(oTestCase.sName), ));

            self._add(u'    <td rowspan="%d" title="Include all variations (checked) or choose a set?">\n'
                      u'      <input name="%s[%s]" type="checkbox"%s%s value="-1">\n'
                      u'    </td>\n'
                      % ( len(oTestCase.aoTestCaseArgs),
                          sPrefix, TestGroupMemberData.ksParam_aidTestCaseArgs,
                          ' checked' if oMember is None  or  oMember.aidTestCaseArgs is None else '', sCheckBoxAttr, ));

            self._add(u'    <td rowspan="%d" align="center">\n'
                      u'      <input name="%s[%s]" type="text" value="%s" style="max-width:3em;" %s>\n'
                      u'    </td>\n'
                      % ( len(oTestCase.aoTestCaseArgs),
                          sPrefix, TestGroupMemberData.ksParam_iSchedPriority,
                          (oMember if oMember is not None else oDefMember).iSchedPriority,
                          ' readonly class="tmform-input-readonly"' if fReadOnly else '', ));

            # Argument variations.
            aidTestCaseArgs = [] if oMember is None or oMember.aidTestCaseArgs is None else oMember.aidTestCaseArgs;
            for iVar, oVar in enumerate(oTestCase.aoTestCaseArgs):
                if iVar > 0:
                    self._add('  <tr class="%s">\n' % ('tmodd' if iTestCase & 1 else 'tmeven',));
                self._add(u'   <td align="center">\n'
                          u'     <input name="%s[%s]" type="checkbox"%s%s value="%d">'
                          u'   </td>\n'
                          % ( sPrefix, TestGroupMemberData.ksParam_aidTestCaseArgs,
                              ' checked' if oVar.idTestCaseArgs in aidTestCaseArgs else '', sCheckBoxAttr, oVar.idTestCaseArgs,
                              ));
                self._add(u'   <td align="center">%s</td>\n'
                          u'   <td align="center">%s</td>\n'
                          u'   <td align="left">%s</td>\n'
                          % ( oVar.cGangMembers,
                              'Default' if oVar.cSecTimeout is None else oVar.cSecTimeout,
                              escapeElem(oVar.sArgs) ));

                self._add(u'  </tr>\n');



            if not oTestCase.aoTestCaseArgs:
                self._add(u'    <td></td> <td></td> <td></td> <td></td>\n'
                          u'  </tr>\n');
        return self._add(u' </tbody>\n'
                         u'</table>\n');

    def addListOfSchedGroupMembers(self, sName, aoSchedGroupMembers, aoAllRelevantTestGroups,  # pylint: disable=too-many-locals
                                   sLabel, idSchedGroup, fReadOnly = True):
        """
        For WuiAdminSchedGroup.
        """
        if fReadOnly is None or self._fReadOnly:
            fReadOnly = self._fReadOnly;
        assert len(aoSchedGroupMembers) <= len(aoAllRelevantTestGroups);
        self._addLabel(sName, sLabel);
        if not aoAllRelevantTestGroups:
            return self._add(u'<li>No test groups.</li>\n')

        self._add(u'<input name="%s" type="hidden" value="%s">\n'
                  % ( SchedGroupDataEx.ksParam_aidTestGroups,
                      ','.join([unicode(oTestGroup.idTestGroup) for oTestGroup in aoAllRelevantTestGroups]), ));

        self._add(u'<table class="tmformtbl tmformtblschedgroupmembers">\n'
                  u' <thead>\n'
                  u'  <tr>\n'
                  u'    <th></th>\n'
                  u'    <th>Test Group</th>\n'
                  u'    <th>Priority [0..31]</th>\n'
                  u'    <th>Prerequisite Test Group</th>\n'
                  u'    <th>Weekly schedule</th>\n'
                  u'  </tr>\n'
                  u' </thead>\n'
                  u' <tbody>\n'
                  );

        sCheckBoxAttr = u' readonly onclick="return false" onkeydown="return false"' if fReadOnly else '';
        sComboBoxAttr = u' disabled' if fReadOnly else '';

        oDefMember = SchedGroupMemberData();
        aoSchedGroupMembers = list(aoSchedGroupMembers); # Copy it so we can pop.
        for iTestGroup, oTestGroup in enumerate(aoAllRelevantTestGroups):

            # Is it a member?
            oMember = None;
            for i, _ in enumerate(aoSchedGroupMembers):
                if aoSchedGroupMembers[i].oTestGroup.idTestGroup == oTestGroup.idTestGroup:
                    oMember = aoSchedGroupMembers.pop(i);
                    break;

            # Start on the rows...
            sPrefix = u'%s[%d]' % (sName, oTestGroup.idTestGroup,);
            self._add(u'  <tr class="%s">\n'
                      u'    <td>\n'
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # idTestGroup
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # idSchedGroup
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # tsExpire
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # tsEffective
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # uidAuthor
                      u'      <input name="%s" type="checkbox"%s%s value="%d" class="tmform-checkbox" title="#%d - %s">\n' #(list)
                      u'    </td>\n'
                      % ( 'tmodd' if iTestGroup & 1 else 'tmeven',
                          sPrefix, SchedGroupMemberData.ksParam_idTestGroup,    oTestGroup.idTestGroup,
                          sPrefix, SchedGroupMemberData.ksParam_idSchedGroup,   idSchedGroup,
                          sPrefix, SchedGroupMemberData.ksParam_tsExpire,       '' if oMember is None else oMember.tsExpire,
                          sPrefix, SchedGroupMemberData.ksParam_tsEffective,    '' if oMember is None else oMember.tsEffective,
                          sPrefix, SchedGroupMemberData.ksParam_uidAuthor,      '' if oMember is None else oMember.uidAuthor,
                          SchedGroupDataEx.ksParam_aoMembers, '' if oMember is None else ' checked', sCheckBoxAttr,
                          oTestGroup.idTestGroup, oTestGroup.idTestGroup, escapeElem(oTestGroup.sName),
                          ));
            self._add(u'    <td>%s</td>\n' % ( escapeElem(oTestGroup.sName), ));

            self._add(u'    <td>\n'
                      u'      <input name="%s[%s]" type="text" value="%s" style="max-width:3em;" %s>\n'
                      u'    </td>\n'
                      % ( sPrefix, SchedGroupMemberData.ksParam_iSchedPriority,
                          (oMember if oMember is not None else oDefMember).iSchedPriority,
                          ' readonly class="tmform-input-readonly"' if fReadOnly else '', ));

            self._add(u'    <td>\n'
                      u'      <select name="%s[%s]" id="%s[%s]" class="tmform-combobox"%s>\n'
                      u'        <option value="-1"%s>None</option>\n'
                      % ( sPrefix, SchedGroupMemberData.ksParam_idTestGroupPreReq,
                          sPrefix, SchedGroupMemberData.ksParam_idTestGroupPreReq,
                          sComboBoxAttr,
                          ' selected' if oMember is None or oMember.idTestGroupPreReq is None else '',
                          ));
            for oTestGroup2 in aoAllRelevantTestGroups:
                if oTestGroup2 != oTestGroup:
                    fSelected = oMember is not None and oTestGroup2.idTestGroup == oMember.idTestGroupPreReq;
                    self._add('        <option value="%s"%s>%s</option>\n'
                              % ( oTestGroup2.idTestGroup, ' selected' if fSelected else '', escapeElem(oTestGroup2.sName), ));
            self._add(u'      </select>\n'
                      u'    </td>\n');

            self._add(u'    <td>\n'
                      u'      Todo<input name="%s[%s]" type="hidden" value="%s">\n'
                      u'    </td>\n'
                      % ( sPrefix, SchedGroupMemberData.ksParam_bmHourlySchedule,
                          '' if oMember is None else oMember.bmHourlySchedule, ));

            self._add(u'  </tr>\n');
        return self._add(u' </tbody>\n'
                         u'</table>\n');

    def addListOfSchedGroupBoxes(self, sName, aoSchedGroupBoxes, # pylint: disable=too-many-locals
                                 aoAllRelevantTestBoxes, sLabel, idSchedGroup, fReadOnly = True,
                                 fUseTable = False): # (str, list[TestBoxDataEx], list[TestBoxDataEx], str, bool, bool) -> str
        """
        For WuiAdminSchedGroup.
        """
        if fReadOnly is None or self._fReadOnly:
            fReadOnly = self._fReadOnly;
        assert len(aoSchedGroupBoxes) <= len(aoAllRelevantTestBoxes);
        self._addLabel(sName, sLabel);
        if not aoAllRelevantTestBoxes:
            return self._add(u'<li>No test boxes.</li>\n')

        self._add(u'<input name="%s" type="hidden" value="%s">\n'
                  % ( SchedGroupDataEx.ksParam_aidTestBoxes,
                      ','.join([unicode(oTestBox.idTestBox) for oTestBox in aoAllRelevantTestBoxes]), ));

        sCheckBoxAttr     = u' readonly onclick="return false" onkeydown="return false"' if fReadOnly else '';
        oDefMember        = TestBoxDataForSchedGroup();
        aoSchedGroupBoxes = list(aoSchedGroupBoxes); # Copy it so we can pop.

        from testmanager.webui.wuiadmintestbox import WuiTestBoxDetailsLink;

        if not fUseTable:
            #
            # Non-table version (see also addListOfOsArches).
            #
            self._add('          <div class="tmform-checkboxes-container">\n');

            for iTestBox, oTestBox in enumerate(aoAllRelevantTestBoxes):
                # Is it a member?
                oMember = None;
                for i, _ in enumerate(aoSchedGroupBoxes):
                    if aoSchedGroupBoxes[i].oTestBox and aoSchedGroupBoxes[i].oTestBox.idTestBox == oTestBox.idTestBox:
                        oMember = aoSchedGroupBoxes.pop(i);
                        break;

                # Start on the rows...
                sPrf = u'%s[%d]' % (sName, oTestBox.idTestBox,);
                self._add(u'  <div class="tmform-checkbox-holder tmshade%u">\n'
                          u'  <input name="%s[%s]" type="hidden" value="%s">\n' # idTestBox
                          u'  <input name="%s[%s]" type="hidden" value="%s">\n' # idSchedGroup
                          u'  <input name="%s[%s]" type="hidden" value="%s">\n' # tsExpire
                          u'  <input name="%s[%s]" type="hidden" value="%s">\n' # tsEffective
                          u'  <input name="%s[%s]" type="hidden" value="%s">\n' # uidAuthor
                          u'  <input name="%s" type="checkbox"%s%s value="%d" class="tmform-checkbox" title="#%d - %s">\n' #(list)
                          % ( iTestBox & 7,
                              sPrf, TestBoxDataForSchedGroup.ksParam_idTestBox,    oTestBox.idTestBox,
                              sPrf, TestBoxDataForSchedGroup.ksParam_idSchedGroup, idSchedGroup,
                              sPrf, TestBoxDataForSchedGroup.ksParam_tsExpire,     '' if oMember is None else oMember.tsExpire,
                              sPrf, TestBoxDataForSchedGroup.ksParam_tsEffective,  '' if oMember is None else oMember.tsEffective,
                              sPrf, TestBoxDataForSchedGroup.ksParam_uidAuthor,    '' if oMember is None else oMember.uidAuthor,
                              SchedGroupDataEx.ksParam_aoTestBoxes, '' if oMember is None else ' checked', sCheckBoxAttr,
                              oTestBox.idTestBox, oTestBox.idTestBox, escapeElem(oTestBox.sName),
                              ));

                self._add(u'    <span class="tmform-priority tmform-testbox-priority">'
                          u'<input name="%s[%s]" type="text" value="%s" style="max-width:3em;" %s title="%s"></span>\n'
                          % ( sPrf, TestBoxDataForSchedGroup.ksParam_iSchedPriority,
                              (oMember if oMember is not None else oDefMember).iSchedPriority,
                              ' readonly class="tmform-input-readonly"' if fReadOnly else '',
                              escapeAttr("Priority [0..31].  Higher value means run more often.") ));

                self._add(u'    <span class="tmform-testbox-name">%s</span>\n'
                          % ( WuiTestBoxDetailsLink(oTestBox, sName = '%s (%s)' % (oTestBox.sName, oTestBox.sOs,)),));
                self._add(u'  </div>\n');
            return self._add(u'        </div></div></div>\n'
                             u'      </li>\n');

        #
        # Table version.
        #
        self._add(u'<table class="tmformtbl">\n'
                  u' <thead>\n'
                  u'  <tr>\n'
                  u'    <th></th>\n'
                  u'    <th>Test Box</th>\n'
                  u'    <th>Priority [0..31]</th>\n'
                  u'  </tr>\n'
                  u' </thead>\n'
                  u' <tbody>\n'
                  );

        for iTestBox, oTestBox in enumerate(aoAllRelevantTestBoxes):
            # Is it a member?
            oMember = None;
            for i, _ in enumerate(aoSchedGroupBoxes):
                if aoSchedGroupBoxes[i].oTestBox and aoSchedGroupBoxes[i].oTestBox.idTestBox == oTestBox.idTestBox:
                    oMember = aoSchedGroupBoxes.pop(i);
                    break;

            # Start on the rows...
            sPrefix = u'%s[%d]' % (sName, oTestBox.idTestBox,);
            self._add(u'  <tr class="%s">\n'
                      u'    <td>\n'
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # idTestBox
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # idSchedGroup
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # tsExpire
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # tsEffective
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # uidAuthor
                      u'      <input name="%s" type="checkbox"%s%s value="%d" class="tmform-checkbox" title="#%d - %s">\n' #(list)
                      u'    </td>\n'
                      % ( 'tmodd' if iTestBox & 1 else 'tmeven',
                          sPrefix, TestBoxDataForSchedGroup.ksParam_idTestBox,    oTestBox.idTestBox,
                          sPrefix, TestBoxDataForSchedGroup.ksParam_idSchedGroup, idSchedGroup,
                          sPrefix, TestBoxDataForSchedGroup.ksParam_tsExpire,     '' if oMember is None else oMember.tsExpire,
                          sPrefix, TestBoxDataForSchedGroup.ksParam_tsEffective,  '' if oMember is None else oMember.tsEffective,
                          sPrefix, TestBoxDataForSchedGroup.ksParam_uidAuthor,    '' if oMember is None else oMember.uidAuthor,
                          SchedGroupDataEx.ksParam_aoTestBoxes, '' if oMember is None else ' checked', sCheckBoxAttr,
                          oTestBox.idTestBox, oTestBox.idTestBox, escapeElem(oTestBox.sName),
                          ));
            self._add(u'    <td align="left">%s</td>\n' % ( escapeElem(oTestBox.sName), ));

            self._add(u'    <td align="center">\n'
                      u'      <input name="%s[%s]" type="text" value="%s" style="max-width:3em;" %s>\n'
                      u'    </td>\n'
                      % ( sPrefix,
                          TestBoxDataForSchedGroup.ksParam_iSchedPriority,
                          (oMember if oMember is not None else oDefMember).iSchedPriority,
                          ' readonly class="tmform-input-readonly"' if fReadOnly else '', ));

            self._add(u'  </tr>\n');
        return self._add(u' </tbody>\n'
                         u'</table>\n');

    def addListOfSchedGroupsForTestBox(self, sName, aoInSchedGroups, aoAllSchedGroups, sLabel,  # pylint: disable=too-many-locals
                                       idTestBox, fReadOnly = None):
        # type: (str, TestBoxInSchedGroupDataEx, SchedGroupData, str, bool) -> str
        """
        For WuiTestGroup.
        """
        from testmanager.core.testbox import TestBoxInSchedGroupData, TestBoxDataEx;

        if fReadOnly is None or self._fReadOnly:
            fReadOnly = self._fReadOnly;
        assert len(aoInSchedGroups) <= len(aoAllSchedGroups);

        # Only show selected groups in read-only mode.
        if fReadOnly:
            aoAllSchedGroups = [oCur.oSchedGroup for oCur in aoInSchedGroups]

        self._addLabel(sName, sLabel);
        if not aoAllSchedGroups:
            return self._add('<li>No scheduling groups.</li>\n')

        # Add special parameter with all the scheduling group IDs in the form.
        self._add(u'<input name="%s" type="hidden" value="%s">\n'
                  % ( TestBoxDataEx.ksParam_aidSchedGroups,
                      ','.join([unicode(oSchedGroup.idSchedGroup) for oSchedGroup in aoAllSchedGroups]), ));

        # Table header.
        self._add(u'<table class="tmformtbl">\n'
                  u' <thead>\n'
                  u'  <tr>\n'
                  u'    <th rowspan="2"></th>\n'
                  u'    <th rowspan="2">Scheduling Group</th>\n'
                  u'    <th rowspan="2">Priority [0..31]</th>\n'
                  u'  </tr>\n'
                  u' </thead>\n'
                  u' <tbody>\n'
                  );

        # Table body.
        if self._fReadOnly:
            fReadOnly = True;
        sCheckBoxAttr = ' readonly onclick="return false" onkeydown="return false"' if fReadOnly else '';

        oDefMember = TestBoxInSchedGroupData();
        aoInSchedGroups = list(aoInSchedGroups); # Copy it so we can pop.
        for iSchedGroup, oSchedGroup in enumerate(aoAllSchedGroups):

            # Is it a member?
            oMember = None;
            for i, _ in enumerate(aoInSchedGroups):
                if aoInSchedGroups[i].idSchedGroup == oSchedGroup.idSchedGroup:
                    oMember = aoInSchedGroups.pop(i);
                    break;

            # Start on the rows...
            sPrefix = u'%s[%d]' % (sName, oSchedGroup.idSchedGroup,);
            self._add(u'  <tr class="%s">\n'
                      u'    <td>\n'
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # idSchedGroup
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # idTestBox
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # tsExpire
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # tsEffective
                      u'      <input name="%s[%s]" type="hidden" value="%s">\n' # uidAuthor
                      u'      <input name="%s" type="checkbox"%s%s value="%d" class="tmform-checkbox" title="#%d - %s">\n' #(list)
                      u'    </td>\n'
                      % ( 'tmodd' if iSchedGroup & 1 else 'tmeven',
                          sPrefix, TestBoxInSchedGroupData.ksParam_idSchedGroup, oSchedGroup.idSchedGroup,
                          sPrefix, TestBoxInSchedGroupData.ksParam_idTestBox,   idTestBox,
                          sPrefix, TestBoxInSchedGroupData.ksParam_tsExpire,    '' if oMember is None else oMember.tsExpire,
                          sPrefix, TestBoxInSchedGroupData.ksParam_tsEffective, '' if oMember is None else oMember.tsEffective,
                          sPrefix, TestBoxInSchedGroupData.ksParam_uidAuthor,   '' if oMember is None else oMember.uidAuthor,
                          TestBoxDataEx.ksParam_aoInSchedGroups, '' if oMember is None else ' checked', sCheckBoxAttr,
                          oSchedGroup.idSchedGroup, oSchedGroup.idSchedGroup, escapeElem(oSchedGroup.sName),
                          ));
            self._add(u'    <td align="left">%s</td>\n' % ( escapeElem(oSchedGroup.sName), ));

            self._add(u'    <td align="center">\n'
                      u'      <input name="%s[%s]" type="text" value="%s" style="max-width:3em;" %s>\n'
                      u'    </td>\n'
                      % ( sPrefix, TestBoxInSchedGroupData.ksParam_iSchedPriority,
                          (oMember if oMember is not None else oDefMember).iSchedPriority,
                          ' readonly class="tmform-input-readonly"' if fReadOnly else '', ));
            self._add(u'  </tr>\n');

        return self._add(u' </tbody>\n'
                         u'</table>\n');


    #
    # Buttons.
    #
    def addSubmit(self, sLabel = 'Submit'):
        """Adds the submit button to the form."""
        if self._fReadOnly:
            return True;
        return self._add(u'      <li>\n'
                         u'        <br>\n'
                         u'        <div class="tmform-field"><div class="tmform-field-submit">\n'
                         u'           <label>&nbsp;</label>\n'
                         u'           <input type="submit" value="%s">\n'
                         u'        </div></div>\n'
                         u'      </li>\n'
                         % (escapeElem(sLabel),));

    def addReset(self):
        """Adds a reset button to the form."""
        if self._fReadOnly:
            return True;
        return self._add(u'      <li>\n'
                         u'        <div class="tmform-button"><div class="tmform-button-reset">\n'
                         u'          <input type="reset" value="%s">\n'
                         u'        </div></div>\n'
                         u'      </li>\n');

