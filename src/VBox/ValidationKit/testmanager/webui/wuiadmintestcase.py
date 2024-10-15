# -*- coding: utf-8 -*-
# $Id: wuiadmintestcase.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Test Cases.
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


# Validation Kit imports.
from common                             import utils, webutils;
from testmanager.webui.wuicontentbase   import WuiFormContentBase, WuiListContentBase, WuiContentBase, WuiTmLink, WuiRawHtml;
from testmanager.core.db                import isDbTimestampInfinity;
from testmanager.core.testcase          import TestCaseDataEx, TestCaseData, TestCaseDependencyLogic;
from testmanager.core.globalresource    import GlobalResourceData, GlobalResourceLogic;



class WuiTestCaseDetailsLink(WuiTmLink):
    """  Test case details link by ID. """

    def __init__(self, idTestCase, sName = WuiContentBase.ksShortDetailsLink, fBracketed = False, tsNow = None):
        from testmanager.webui.wuiadmin import WuiAdmin;
        dParams = {
            WuiAdmin.ksParamAction:             WuiAdmin.ksActionTestCaseDetails,
            TestCaseData.ksParam_idTestCase:    idTestCase,
        };
        if tsNow is not None:
            dParams[WuiAdmin.ksParamEffectiveDate] = tsNow; ## ??
        WuiTmLink.__init__(self, sName, WuiAdmin.ksScriptName, dParams, fBracketed = fBracketed);
        self.idTestCase = idTestCase;


class WuiTestCaseList(WuiListContentBase):
    """
    WUI test case list content generator.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, sTitle = 'Test Cases',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);
        self._asColumnHeaders = \
        [
            'Name', 'Active', 'Timeout', 'Base Command / Variations', 'Validation Kit Files',
            'Test Case Prereqs', 'Global Rsrces', 'Note', 'Actions'
        ];
        self._asColumnAttribs = \
        [
            '', '', 'align="center"', '', '',
            'valign="top"', 'valign="top"', 'align="center"', 'align="center"'
        ];

    def _formatListEntry(self, iEntry):
        oEntry = self._aoEntries[iEntry];
        from testmanager.webui.wuiadmin import WuiAdmin;

        aoRet = \
        [
            oEntry.sName.replace('-', u'\u2011'),
            'Enabled' if oEntry.fEnabled else 'Disabled',
            utils.formatIntervalSeconds(oEntry.cSecTimeout),
        ];

        # Base command and variations.
        fNoGang = True;
        fNoSubName = True;
        fAllDefaultTimeouts = True;
        for oVar in oEntry.aoTestCaseArgs:
            if fNoSubName and oVar.sSubName is not None and oVar.sSubName.strip():
                fNoSubName = False;
            if oVar.cGangMembers > 1:
                fNoGang = False;
            if oVar.cSecTimeout is not None:
                fAllDefaultTimeouts = False;

        sHtml  = '  <table class="tminnertbl" width=100%>\n' \
                 '    <tr>\n' \
                 '      ';
        if not fNoSubName:
            sHtml += '<th class="tmtcasubname">Sub-name</th>';
        if not fNoGang:
            sHtml += '<th class="tmtcagangsize">Gang Size</th>';
        if not fAllDefaultTimeouts:
            sHtml += '<th class="tmtcatimeout">Timeout</th>';
        sHtml += '<th>Additional Arguments</b></th>\n' \
                 '    </tr>\n'
        for oTmp in oEntry.aoTestCaseArgs:
            sHtml += '<tr>';
            if not fNoSubName:
                sHtml += '<td>%s</td>' % (webutils.escapeElem(oTmp.sSubName) if oTmp.sSubName is not None else '');
            if not fNoGang:
                sHtml += '<td>%d</td>' % (oTmp.cGangMembers,)
            if not fAllDefaultTimeouts:
                sHtml += '<td>%s</td>' \
                       % (utils.formatIntervalSeconds(oTmp.cSecTimeout) if oTmp.cSecTimeout is not None else 'Default',)
            sHtml += u'<td>%s</td></tr>' \
                % ( webutils.escapeElem(oTmp.sArgs.replace('-', u'\u2011')) if oTmp.sArgs else u'\u2011',);
            sHtml += '</tr>\n';
        sHtml += '  </table>'

        aoRet.append([oEntry.sBaseCmd.replace('-', u'\u2011'), WuiRawHtml(sHtml)]);

        # Next.
        aoRet += [ oEntry.sValidationKitZips if oEntry.sValidationKitZips is not None else '', ];

        # Show dependency on other testcases
        if oEntry.aoDepTestCases not in (None, []):
            sHtml = '  <ul class="tmshowall">\n'
            for sTmp in oEntry.aoDepTestCases:
                sHtml += '    <li class="tmshowall"><a href="%s?%s=%s&%s=%s">%s</a></li>\n' \
                       % (WuiAdmin.ksScriptName,
                          WuiAdmin.ksParamAction, WuiAdmin.ksActionTestCaseEdit,
                          TestCaseData.ksParam_idTestCase, sTmp.idTestCase,
                          sTmp.sName)
            sHtml += '  </ul>\n'
        else:
            sHtml = '<ul class="tmshowall"><li class="tmshowall">None</li></ul>\n'
        aoRet.append(WuiRawHtml(sHtml));

        # Show dependency on global resources
        if oEntry.aoDepGlobalResources not in (None, []):
            sHtml = '  <ul class="tmshowall">\n'
            for sTmp in oEntry.aoDepGlobalResources:
                sHtml += '    <li class="tmshowall"><a href="%s?%s=%s&%s=%s">%s</a></li>\n' \
                       % (WuiAdmin.ksScriptName,
                          WuiAdmin.ksParamAction, WuiAdmin.ksActionGlobalRsrcShowEdit,
                          GlobalResourceData.ksParam_idGlobalRsrc, sTmp.idGlobalRsrc,
                          sTmp.sName)
            sHtml += '  </ul>\n'
        else:
            sHtml = '<ul class="tmshowall"><li class="tmshowall">None</li></ul>\n'
        aoRet.append(WuiRawHtml(sHtml));

        # Comment (note).
        aoRet.append(self._formatCommentCell(oEntry.sComment));

        # Show actions that can be taken.
        aoActions = [ WuiTmLink('Details', WuiAdmin.ksScriptName,
                                { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestCaseDetails,
                                  TestCaseData.ksParam_idGenTestCase: oEntry.idGenTestCase }), ];
        if self._oDisp is None or not self._oDisp.isReadOnlyUser():
            if isDbTimestampInfinity(oEntry.tsExpire):
                aoActions.append(WuiTmLink('Modify', WuiAdmin.ksScriptName,
                                           { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestCaseEdit,
                                             TestCaseData.ksParam_idTestCase: oEntry.idTestCase }));
            aoActions.append(WuiTmLink('Clone', WuiAdmin.ksScriptName,
                                       { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestCaseClone,
                                         TestCaseData.ksParam_idGenTestCase: oEntry.idGenTestCase }));
            if isDbTimestampInfinity(oEntry.tsExpire):
                aoActions.append(WuiTmLink('Remove', WuiAdmin.ksScriptName,
                                           { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestCaseDoRemove,
                                             TestCaseData.ksParam_idTestCase: oEntry.idTestCase },
                                           sConfirm = 'Are you sure you want to remove test case #%d?' % (oEntry.idTestCase,)));
        aoRet.append(aoActions);

        return aoRet;


class WuiTestCase(WuiFormContentBase):
    """
    WUI user account content generator.
    """

    def __init__(self, oData, sMode, oDisp):
        assert isinstance(oData, TestCaseDataEx);

        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'New Test Case';
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Edit Test Case - %s (#%s)' % (oData.sName, oData.idTestCase);
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;
            sTitle = 'Test Case - %s (#%s)' % (oData.sName, oData.idTestCase);
        WuiFormContentBase.__init__(self, oData, sMode, 'TestCase', oDisp, sTitle);

        # Read additional bits form the DB.
        oDepLogic = TestCaseDependencyLogic(oDisp.getDb());
        self._aoAllTestCases    = oDepLogic.getApplicableDepTestCaseData(-1 if oData.idTestCase is None else oData.idTestCase);
        self._aoAllGlobalRsrcs  = GlobalResourceLogic(oDisp.getDb()).getAll();

    def _populateForm(self, oForm, oData):
        oForm.addIntRO      (TestCaseData.ksParam_idTestCase,       oData.idTestCase,       'Test Case ID')
        oForm.addTimestampRO(TestCaseData.ksParam_tsEffective,      oData.tsEffective,      'Last changed')
        oForm.addTimestampRO(TestCaseData.ksParam_tsExpire,         oData.tsExpire,         'Expires (excl)')
        oForm.addIntRO      (TestCaseData.ksParam_uidAuthor,        oData.uidAuthor,        'Changed by UID')
        oForm.addIntRO      (TestCaseData.ksParam_idGenTestCase,    oData.idGenTestCase,    'Test Case generation ID')
        oForm.addText       (TestCaseData.ksParam_sName,            oData.sName,            'Name')
        oForm.addText       (TestCaseData.ksParam_sDescription,     oData.sDescription,     'Description')
        oForm.addCheckBox   (TestCaseData.ksParam_fEnabled,         oData.fEnabled,         'Enabled')
        oForm.addLong       (TestCaseData.ksParam_cSecTimeout,
                             utils.formatIntervalSeconds2(oData.cSecTimeout),               'Default timeout')
        oForm.addWideText   (TestCaseData.ksParam_sTestBoxReqExpr,  oData.sTestBoxReqExpr,  'TestBox requirements (python)');
        oForm.addWideText   (TestCaseData.ksParam_sBuildReqExpr,    oData.sBuildReqExpr,    'Build requirement (python)');
        oForm.addWideText   (TestCaseData.ksParam_sBaseCmd,         oData.sBaseCmd,         'Base command')
        oForm.addText       (TestCaseData.ksParam_sValidationKitZips,   oData.sValidationKitZips,   'Test suite files')

        oForm.addListOfTestCaseArgs(TestCaseDataEx.ksParam_aoTestCaseArgs, oData.aoTestCaseArgs, 'Argument variations')

        aoTestCaseDeps = [];
        for oTestCase in self._aoAllTestCases:
            if oTestCase.idTestCase == oData.idTestCase:
                continue;
            fSelected = False;
            for oDep in oData.aoDepTestCases:
                if oDep.idTestCase == oTestCase.idTestCase:
                    fSelected = True;
                    break;
            aoTestCaseDeps.append([oTestCase.idTestCase, fSelected, oTestCase.sName]);
        oForm.addListOfTestCases(TestCaseDataEx.ksParam_aoDepTestCases, aoTestCaseDeps,     'Depends on test cases')

        aoGlobalResrcDeps = [];
        for oGlobalRsrc in self._aoAllGlobalRsrcs:
            fSelected = False;
            for oDep in oData.aoDepGlobalResources:
                if oDep.idGlobalRsrc == oGlobalRsrc.idGlobalRsrc:
                    fSelected = True;
                    break;
            aoGlobalResrcDeps.append([oGlobalRsrc.idGlobalRsrc, fSelected, oGlobalRsrc.sName]);
        oForm.addListOfResources(TestCaseDataEx.ksParam_aoDepGlobalResources, aoGlobalResrcDeps, 'Depends on resources')

        oForm.addMultilineText(TestCaseDataEx.ksParam_sComment, oData.sComment, 'Comment');

        oForm.addSubmit();

        return True;

