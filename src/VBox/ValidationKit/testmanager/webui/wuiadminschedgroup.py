# -*- coding: utf-8 -*-
# $Id: wuiadminschedgroup.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Scheduling groups.
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
from testmanager.core.buildsource       import BuildSourceData, BuildSourceLogic;
from testmanager.core.db                import isDbTimestampInfinity;
from testmanager.core.schedgroup        import SchedGroupData, SchedGroupDataEx;
from testmanager.core.testgroup         import TestGroupData, TestGroupLogic;
from testmanager.core.testbox           import TestBoxLogic;
from testmanager.webui.wuicontentbase   import WuiFormContentBase, WuiListContentBase, WuiTmLink, WuiRawHtml;
from testmanager.webui.wuiadmintestbox  import WuiTestBoxDetailsLink;


class WuiSchedGroup(WuiFormContentBase):
    """
    WUI Scheduling Groups HTML content generator.
    """

    def __init__(self, oData, sMode, oDisp):
        assert isinstance(oData, SchedGroupData);
        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'New Scheduling Group';
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Edit Scheduling Group'
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;
            sTitle = 'Scheduling Group';
        WuiFormContentBase.__init__(self, oData, sMode, 'SchedGroup', oDisp, sTitle);

        # Read additional bits form the DB, unless we're in
        if sMode != WuiFormContentBase.ksMode_Show:
            self._aoAllRelevantTestGroups = TestGroupLogic(oDisp.getDb()).getAll();
            self._aoAllRelevantTestBoxes  = TestBoxLogic(oDisp.getDb()).getAll();
        else:
            self._aoAllRelevantTestGroups = [oMember.oTestGroup for oMember in oData.aoMembers];
            self._aoAllRelevantTestBoxes  = [oMember.oTestBox   for oMember in oData.aoTestBoxes];

    def _populateForm(self, oForm, oData): # type: (WuiHlpForm, SchedGroupDataEx) -> bool
        """
        Construct an HTML form
        """

        oForm.addIntRO(     SchedGroupData.ksParam_idSchedGroup,    oData.idSchedGroup,     'ID')
        oForm.addTimestampRO(SchedGroupData.ksParam_tsEffective,    oData.tsEffective,      'Last changed')
        oForm.addTimestampRO(SchedGroupData.ksParam_tsExpire,       oData.tsExpire,         'Expires (excl)')
        oForm.addIntRO(     SchedGroupData.ksParam_uidAuthor,       oData.uidAuthor,        'Changed by UID')
        oForm.addText(      SchedGroupData.ksParam_sName,           oData.sName,            'Name')
        oForm.addText(      SchedGroupData.ksParam_sDescription,    oData.sDescription,     'Description')
        oForm.addCheckBox(  SchedGroupData.ksParam_fEnabled,        oData.fEnabled,         'Enabled')

        oForm.addComboBox(  SchedGroupData.ksParam_enmScheduler,    oData.enmScheduler,     'Scheduler type',
                            SchedGroupData.kasSchedulerDesc)

        aoBuildSrcIds = BuildSourceLogic(self._oDisp.getDb()).fetchForCombo();
        oForm.addComboBox(  SchedGroupData.ksParam_idBuildSrc,      oData.idBuildSrc,       'Build source',  aoBuildSrcIds);
        oForm.addComboBox(  SchedGroupData.ksParam_idBuildSrcTestSuite,
                            oData.idBuildSrcTestSuite, 'Test suite', aoBuildSrcIds);

        oForm.addListOfSchedGroupMembers(SchedGroupDataEx.ksParam_aoMembers,
                                         oData.aoMembers, self._aoAllRelevantTestGroups,    'Test groups',
                                         oData.idSchedGroup, fReadOnly = self._sMode == WuiFormContentBase.ksMode_Show);

        oForm.addListOfSchedGroupBoxes(SchedGroupDataEx.ksParam_aoTestBoxes,
                                       oData.aoTestBoxes, self._aoAllRelevantTestBoxes,     'Test boxes',
                                       oData.idSchedGroup, fReadOnly = self._sMode == WuiFormContentBase.ksMode_Show);

        oForm.addMultilineText(SchedGroupData.ksParam_sComment,     oData.sComment,         'Comment');
        oForm.addSubmit()

        return True;

class WuiAdminSchedGroupList(WuiListContentBase):
    """
    Content generator for the schedule group listing.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective,
                                    sTitle = 'Registered Scheduling Groups', sId = 'schedgroups',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);

        self._asColumnHeaders = [
            'ID',  'Name', 'Enabled', 'Scheduler Type',
            'Build Source', 'Validation Kit Source', 'Test Groups', 'TestBoxes', 'Note', 'Actions',
        ];

        self._asColumnAttribs = [
            'align="right"', 'align="center"', 'align="center"', 'align="center"',
            'align="center"', 'align="center"', '', '', 'align="center"', 'align="center"',
        ];

    def _formatListEntry(self, iEntry):
        """
        Format *show all* table entry
        """
        from testmanager.webui.wuiadmin import WuiAdmin
        oEntry  = self._aoEntries[iEntry]   # type: SchedGroupDataEx

        oBuildSrc = None;
        if oEntry.idBuildSrc is not None:
            oBuildSrc     = WuiTmLink(oEntry.oBuildSrc.sName if oEntry.oBuildSrc else str(oEntry.idBuildSrc),
                                      WuiAdmin.ksScriptName,
                                      { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildSrcDetails,
                                        BuildSourceData.ksParam_idBuildSrc: oEntry.idBuildSrc, });

        oValidationKitSrc = None;
        if oEntry.idBuildSrcTestSuite is not None:
            oValidationKitSrc = WuiTmLink(oEntry.oBuildSrcValidationKit.sName if oEntry.oBuildSrcValidationKit
                                      else str(oEntry.idBuildSrcTestSuite),
                                      WuiAdmin.ksScriptName,
                                      { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildSrcDetails,
                                        BuildSourceData.ksParam_idBuildSrc: oEntry.idBuildSrcTestSuite, });

        # Test groups
        aoMembers = [];
        for oMember in oEntry.aoMembers:
            aoMembers.append(WuiTmLink(oMember.oTestGroup.sName, WuiAdmin.ksScriptName,
                                       { WuiAdmin.ksParamAction: WuiAdmin.ksActionTestGroupDetails,
                                         TestGroupData.ksParam_idTestGroup: oMember.idTestGroup,
                                         WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, },
                                       sTitle = '#%s' % (oMember.idTestGroup,) if oMember.oTestGroup.sDescription is None
                                                else '#%s - %s' % (oMember.idTestGroup, oMember.oTestGroup.sDescription,) ));

        # Test boxes.
        aoTestBoxes = [];
        for oRelation in oEntry.aoTestBoxes:
            oTestBox = oRelation.oTestBox;
            if oTestBox:
                aoTestBoxes.append(WuiTestBoxDetailsLink(oTestBox, fBracketed = True, tsNow = self._tsEffectiveDate));
            else:
                aoTestBoxes.append(WuiRawHtml('#%s' % (oRelation.idTestBox,)));

        # Actions
        aoActions = [ WuiTmLink('Details', WuiAdmin.ksScriptName,
                                { WuiAdmin.ksParamAction: WuiAdmin.ksActionSchedGroupDetails,
                                  SchedGroupData.ksParam_idSchedGroup: oEntry.idSchedGroup,
                                  WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, } ),];
        if self._oDisp is None or not self._oDisp.isReadOnlyUser():

            if isDbTimestampInfinity(oEntry.tsExpire):
                aoActions.append(WuiTmLink('Modify', WuiAdmin.ksScriptName,
                                           { WuiAdmin.ksParamAction: WuiAdmin.ksActionSchedGroupEdit,
                                             SchedGroupData.ksParam_idSchedGroup: oEntry.idSchedGroup } ));
            aoActions.append(WuiTmLink('Clone', WuiAdmin.ksScriptName,
                                       { WuiAdmin.ksParamAction: WuiAdmin.ksActionSchedGroupClone,
                                         SchedGroupData.ksParam_idSchedGroup: oEntry.idSchedGroup,
                                         WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, } ));
            if isDbTimestampInfinity(oEntry.tsExpire):
                aoActions.append(WuiTmLink('Remove', WuiAdmin.ksScriptName,
                                           { WuiAdmin.ksParamAction: WuiAdmin.ksActionSchedGroupDoRemove,
                                             SchedGroupData.ksParam_idSchedGroup: oEntry.idSchedGroup },
                                           sConfirm = 'Are you sure you want to remove scheduling group #%d?'
                                                    % (oEntry.idSchedGroup,)));

        return [
            oEntry.idSchedGroup,
            oEntry.sName,
            oEntry.fEnabled,
            oEntry.enmScheduler,
            oBuildSrc,
            oValidationKitSrc,
            aoMembers,
            aoTestBoxes,
            self._formatCommentCell(oEntry.sComment),
            aoActions,
        ];

