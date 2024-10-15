# -*- coding: utf-8 -*-
# $Id: wuiadmintestgroup.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Test Groups.
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
from testmanager.webui.wuicontentbase   import WuiFormContentBase, WuiListContentBase, WuiTmLink, WuiRawHtml;
from testmanager.core.db                import isDbTimestampInfinity;
from testmanager.core.testgroup         import TestGroupData, TestGroupDataEx;
from testmanager.core.testcase          import TestCaseData, TestCaseLogic;


class WuiTestGroup(WuiFormContentBase):
    """
    WUI test group content generator.
    """

    def __init__(self, oData, sMode, oDisp):
        assert isinstance(oData, TestGroupDataEx);

        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'Add Test Group';
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Modify Test Group';
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;
            sTitle = 'Test Group';
        WuiFormContentBase.__init__(self, oData, sMode, 'TestGroup', oDisp, sTitle);

        #
        # Fetch additional data.
        #
        if sMode in [WuiFormContentBase.ksMode_Add, WuiFormContentBase.ksMode_Edit]:
            self.aoAllTestCases = TestCaseLogic(oDisp.getDb()).fetchForListing(0, 0x7fff, None);
        else:
            self.aoAllTestCases = [oMember.oTestCase for oMember in oData.aoMembers];

    def _populateForm(self, oForm, oData):
        oForm.addIntRO          (TestGroupData.ksParam_idTestGroup,      self._oData.idTestGroup,    'Test Group ID')
        oForm.addTimestampRO    (TestGroupData.ksParam_tsEffective,      self._oData.tsEffective,    'Last changed')
        oForm.addTimestampRO    (TestGroupData.ksParam_tsExpire,         self._oData.tsExpire,       'Expires (excl)')
        oForm.addIntRO          (TestGroupData.ksParam_uidAuthor,        self._oData.uidAuthor,      'Changed by UID')
        oForm.addText           (TestGroupData.ksParam_sName,            self._oData.sName,          'Name')
        oForm.addText           (TestGroupData.ksParam_sDescription,     self._oData.sDescription,   'Description')

        oForm.addListOfTestGroupMembers(TestGroupDataEx.ksParam_aoMembers,
                                        oData.aoMembers, self.aoAllTestCases, 'Test Case List',
                                        fReadOnly = self._sMode == WuiFormContentBase.ksMode_Show);

        oForm.addMultilineText  (TestGroupData.ksParam_sComment,         self._oData.sComment,       'Comment');
        oForm.addSubmit();
        return True;


class WuiTestGroupList(WuiListContentBase):
    """
    WUI test group list content generator.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        assert not aoEntries or isinstance(aoEntries[0], TestGroupDataEx)

        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, sTitle = 'Test Groups',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);
        self._asColumnHeaders = [ 'ID', 'Name', 'Description', 'Test Cases', 'Note', 'Actions' ];
        self._asColumnAttribs = [ 'align="right"', '', '', '', 'align="center"', 'align="center"' ];


    def _formatListEntry(self, iEntry):
        oEntry = self._aoEntries[iEntry];
        from testmanager.webui.wuiadmin import WuiAdmin;

        #
        # Test case list.
        #
        sHtml = '';
        if oEntry.aoMembers:
            for oMember in oEntry.aoMembers:
                sHtml += '<dl>\n' \
                         '  <dd><strong>%s</strong> (priority: %d) %s %s</dd>\n' \
                       % ( webutils.escapeElem(oMember.oTestCase.sName),
                           oMember.iSchedPriority,
                           WuiTmLink('Details', WuiAdmin.ksScriptName,
                                     { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestCaseDetails,
                                       TestCaseData.ksParam_idGenTestCase: oMember.oTestCase.idGenTestCase, } ).toHtml(),
                           WuiTmLink('Edit', WuiAdmin.ksScriptName,
                                     { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestCaseEdit,
                                       TestCaseData.ksParam_idTestCase: oMember.oTestCase.idTestCase, } ).toHtml()
                           if     isDbTimestampInfinity(oMember.oTestCase.tsExpire)
                              and self._oDisp is not None
                              and not self._oDisp.isReadOnlyUser() else '',
                           );

                sHtml += '  <dt>\n';

                fNoGang = True;
                for oVar in oMember.oTestCase.aoTestCaseArgs:
                    if oVar.cGangMembers > 1:
                        fNoGang = False
                        break;

                sHtml += '    <table class="tminnertbl" width="100%">\n'
                if fNoGang:
                    sHtml += '      <tr><th>Timeout</th><th>Arguments</th></tr>\n';
                else:
                    sHtml += '      <tr><th>Gang Size</th><th>Timeout</th><th style="text-align:left;">Arguments</th></tr>\n';

                cArgsIncluded = 0;
                for oVar in oMember.oTestCase.aoTestCaseArgs:
                    if oMember.aidTestCaseArgs is None  or  oVar.idTestCaseArgs in oMember.aidTestCaseArgs:
                        cArgsIncluded += 1;
                        if fNoGang:
                            sHtml += '      <tr>';
                        else:
                            sHtml += '      <tr><td>%s</td>' % (oVar.cGangMembers,);
                        sHtml += '<td>%s</td><td>%s</td></tr>\n' \
                               % ( utils.formatIntervalSeconds(oMember.oTestCase.cSecTimeout if oVar.cSecTimeout is None
                                                               else oVar.cSecTimeout),
                                   webutils.escapeElem(oVar.sArgs), );
                if cArgsIncluded == 0:
                    sHtml += '      <tr><td colspan="%u">No arguments selected.</td></tr>\n' % ( 2 if fNoGang else 3, );
                sHtml += '    </table>\n' \
                         '  </dl>\n';
        oTestCases = WuiRawHtml(sHtml);

        #
        # Actions.
        #
        aoActions = [ WuiTmLink('Details', WuiAdmin.ksScriptName,
                                { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestGroupDetails,
                                  TestGroupData.ksParam_idTestGroup: oEntry.idTestGroup,
                                  WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, }) ];
        if self._oDisp is None or not self._oDisp.isReadOnlyUser():

            if isDbTimestampInfinity(oEntry.tsExpire):
                aoActions.append(WuiTmLink('Modify', WuiAdmin.ksScriptName,
                                           { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestGroupEdit,
                                             TestGroupData.ksParam_idTestGroup: oEntry.idTestGroup }));
                aoActions.append(WuiTmLink('Clone', WuiAdmin.ksScriptName,
                                           { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestGroupClone,
                                             TestGroupData.ksParam_idTestGroup: oEntry.idTestGroup,
                                             WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, }));
                aoActions.append(WuiTmLink('Remove', WuiAdmin.ksScriptName,
                                           { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestGroupDoRemove,
                                             TestGroupData.ksParam_idTestGroup: oEntry.idTestGroup },
                                           sConfirm = 'Do you really want to remove test group #%d?' % (oEntry.idTestGroup,)));
            else:
                aoActions.append(WuiTmLink('Clone', WuiAdmin.ksScriptName,
                                           { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestGroupClone,
                                             TestGroupData.ksParam_idTestGroup: oEntry.idTestGroup,
                                             WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, }));



        return [ oEntry.idTestGroup,
                 oEntry.sName,
                 oEntry.sDescription if oEntry.sDescription is not None else '',
                 oTestCases,
                 self._formatCommentCell(oEntry.sComment, cMaxLines = max(3, len(oEntry.aoMembers) * 2)),
                 aoActions ];

