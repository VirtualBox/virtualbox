# -*- coding: utf-8 -*-
# $Id: wuitestresultfailure.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Dummy Test Result Failure Reason Edit Dialog - just for error handling!
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
from testmanager.webui.wuicontentbase           import WuiFormContentBase, WuiContentBase, WuiTmLink;
from testmanager.webui.wuimain                  import WuiMain;
from testmanager.webui.wuiadminfailurereason    import WuiFailureReasonDetailsLink, WuiFailureReasonAddLink;
from testmanager.core.testresultfailures        import TestResultFailureData;
from testmanager.core.testset                   import TestSetData;
from testmanager.core.failurereason             import FailureReasonLogic;



class WuiTestResultFailureDetailsLink(WuiTmLink):
    """ Link for adding a failure reason. """
    def __init__(self, idTestResult, sName = WuiContentBase.ksShortDetailsLink, sTitle = None, fBracketed = None):
        if fBracketed is None:
            fBracketed = len(sName) > 2;
        WuiTmLink.__init__(self, sName = sName,
                           sUrlBase = WuiMain.ksScriptName,
                           dParams = { WuiMain.ksParamAction: WuiMain.ksActionTestResultFailureDetails,
                                       TestResultFailureData.ksParam_idTestResult: idTestResult, },
                           fBracketed = fBracketed,
                           sTitle = sTitle);
        self.idTestResult = idTestResult;



class WuiTestResultFailure(WuiFormContentBase):
    """
    WUI test result failure error form generator.
    """
    def __init__(self, oData, sMode, oDisp):
        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'Add Test Result Failure Reason';
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Modify Test Result Failure Reason';
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;
            sTitle = 'Test Result Failure Reason';
        ## submit access.
        WuiFormContentBase.__init__(self, oData, sMode, 'TestResultFailure', oDisp, sTitle);

    def _populateForm(self, oForm, oData):

        aoFailureReasons = FailureReasonLogic(self._oDisp.getDb()).fetchForCombo('Todo: Figure out why');
        sPostHtml = '';
        if oData.idFailureReason is not None and oData.idFailureReason >= 0:
            sPostHtml += u' ' + WuiFailureReasonDetailsLink(oData.idFailureReason).toHtml();
        sPostHtml += u' ' + WuiFailureReasonAddLink('New', fBracketed = False).toHtml();
        oForm.addComboBox(TestResultFailureData.ksParam_idFailureReason, oData.idFailureReason,
                          'Reason', aoFailureReasons, sPostHtml = sPostHtml);
        oForm.addMultilineText(TestResultFailureData.ksParam_sComment,   oData.sComment,     'Comment');
        oForm.addIntRO(      TestResultFailureData.ksParam_idTestResult, oData.idTestResult, 'Test Result ID');
        oForm.addIntRO(      TestResultFailureData.ksParam_idTestSet,    oData.idTestSet,    'Test Set ID');
        oForm.addTimestampRO(TestResultFailureData.ksParam_tsEffective,  oData.tsEffective,  'Effective Date');
        oForm.addTimestampRO(TestResultFailureData.ksParam_tsExpire,     oData.tsExpire,     'Expire (excl)');
        oForm.addIntRO(      TestResultFailureData.ksParam_uidAuthor,    oData.uidAuthor,    'Changed by UID');
        if self._sMode != WuiFormContentBase.ksMode_Show:
            oForm.addSubmit('Add' if self._sMode == WuiFormContentBase.ksMode_Add else 'Modify');
        return True;

    def _generateTopRowFormActions(self, oData):
        """
        We add a way to get back to the test set to the actions.
        """
        aoActions = super(WuiTestResultFailure, self)._generateTopRowFormActions(oData);
        if oData and oData.idTestResult and int(oData.idTestResult) > 0:
            aoActions.append(WuiTmLink('Associated Test Set', WuiMain.ksScriptName,
                                       { WuiMain.ksParamAction: WuiMain.ksActionTestSetDetailsFromResult,
                                         TestSetData.ksParam_idTestResult: oData.idTestResult }
                                       ));
        return aoActions;
