# -*- coding: utf-8 -*-
# $Id: wuiadminfailurereason.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Failure Reasons Web content generator.
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
from testmanager.webui.wuibase        import WuiException
from testmanager.webui.wuicontentbase import WuiFormContentBase, WuiListContentBase, WuiContentBase, WuiTmLink;
from testmanager.core.failurereason   import FailureReasonData;
from testmanager.core.failurecategory import FailureCategoryLogic;
from testmanager.core.db              import TMDatabaseConnection;



class WuiFailureReasonDetailsLink(WuiTmLink):
    """ Short link to a failure reason. """
    def __init__(self, idFailureReason, sName = WuiContentBase.ksShortDetailsLink, sTitle = None, fBracketed = None):
        if fBracketed is None:
            fBracketed = len(sName) > 2;
        from testmanager.webui.wuiadmin import WuiAdmin;
        WuiTmLink.__init__(self, sName = sName,
                           sUrlBase = WuiAdmin.ksScriptName,
                           dParams = { WuiAdmin.ksParamAction: WuiAdmin.ksActionFailureReasonDetails,
                                       FailureReasonData.ksParam_idFailureReason: idFailureReason, },
                           fBracketed = fBracketed);
        self.idFailureReason = idFailureReason;



class WuiFailureReasonAddLink(WuiTmLink):
    """ Link for adding a failure reason. """
    def __init__(self, sName = WuiContentBase.ksShortAddLink, sTitle = None, fBracketed = None):
        if fBracketed is None:
            fBracketed = len(sName) > 2;
        from testmanager.webui.wuiadmin import WuiAdmin;
        WuiTmLink.__init__(self, sName = sName,
                           sUrlBase = WuiAdmin.ksScriptName,
                           dParams = { WuiAdmin.ksParamAction: WuiAdmin.ksActionFailureReasonAdd, },
                           fBracketed = fBracketed);



class WuiAdminFailureReason(WuiFormContentBase):
    """
    WUI Failure Reason HTML content generator.
    """

    def __init__(self, oFailureReasonData, sMode, oDisp):
        """
        Prepare & initialize parent
        """

        sTitle = 'Failure Reason';
        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'Add' + sTitle;
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Edit' + sTitle;
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;

        WuiFormContentBase.__init__(self, oFailureReasonData, sMode, 'FailureReason', oDisp, sTitle);

    def _populateForm(self, oForm, oData):
        """
        Construct an HTML form
        """

        aoFailureCategories = FailureCategoryLogic(TMDatabaseConnection()).getFailureCategoriesForCombo()
        if not aoFailureCategories:
            from testmanager.webui.wuiadmin import WuiAdmin
            sExceptionMsg = 'Please <a href="%s?%s=%s">add</a> Failure Category first.' % \
                (WuiAdmin.ksScriptName, WuiAdmin.ksParamAction, WuiAdmin.ksActionFailureCategoryAdd)

            raise WuiException(sExceptionMsg)

        oForm.addIntRO        (FailureReasonData.ksParam_idFailureReason,    oData.idFailureReason,    'Failure Reason ID')
        oForm.addTimestampRO  (FailureReasonData.ksParam_tsEffective,        oData.tsEffective,        'Last changed')
        oForm.addTimestampRO  (FailureReasonData.ksParam_tsExpire,           oData.tsExpire,           'Expires (excl)')
        oForm.addIntRO        (FailureReasonData.ksParam_uidAuthor,          oData.uidAuthor,          'Changed by UID')

        oForm.addComboBox     (FailureReasonData.ksParam_idFailureCategory,  oData.idFailureCategory,  'Failure Category',
                               aoFailureCategories)

        oForm.addText         (FailureReasonData.ksParam_sShort,             oData.sShort,             'Short Description')
        oForm.addText         (FailureReasonData.ksParam_sFull,              oData.sFull,              'Full Description')
        oForm.addInt          (FailureReasonData.ksParam_iTicket,            oData.iTicket,            'Ticket Number')
        oForm.addMultilineText(FailureReasonData.ksParam_asUrls,             oData.asUrls,             'Other URLs to reports '
                                                                                                       'or discussions of the '
                                                                                                       'observed symptoms')
        oForm.addSubmit()

        return True


class WuiAdminFailureReasonList(WuiListContentBase):
    """
    WUI Admin Failure Reasons Content Generator.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective,
                                    sTitle = 'Failure Reasons', sId = 'failureReasons',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);

        self._asColumnHeaders = ['ID', 'Category', 'Short Description',
                                 'Full Description', 'Ticket', 'External References', 'Actions' ]

        self._asColumnAttribs = ['align="right"', 'align="center"', 'align="center"',
                                 'align="center"',' align="center"', 'align="center"', 'align="center"']

    def _formatListEntry(self, iEntry):
        from testmanager.webui.wuiadmin                 import WuiAdmin
        from testmanager.webui.wuiadminfailurecategory  import WuiFailureReasonCategoryLink;
        oEntry = self._aoEntries[iEntry]

        aoActions = [
            WuiTmLink('Details', WuiAdmin.ksScriptName,
                      { WuiAdmin.ksParamAction: WuiAdmin.ksActionFailureReasonDetails,
                        FailureReasonData.ksParam_idFailureReason: oEntry.idFailureReason } ),
        ];
        if self._oDisp is None or not self._oDisp.isReadOnlyUser():
            aoActions += [
                WuiTmLink('Modify', WuiAdmin.ksScriptName,
                          { WuiAdmin.ksParamAction: WuiAdmin.ksActionFailureReasonEdit,
                            FailureReasonData.ksParam_idFailureReason: oEntry.idFailureReason } ),
                WuiTmLink('Remove', WuiAdmin.ksScriptName,
                          { WuiAdmin.ksParamAction: WuiAdmin.ksActionFailureReasonDoRemove,
                            FailureReasonData.ksParam_idFailureReason: oEntry.idFailureReason },
                          sConfirm = 'Are you sure you want to remove failure reason #%d?' % (oEntry.idFailureReason,)),
            ];

        return [ oEntry.idFailureReason,
                 WuiFailureReasonCategoryLink(oEntry.idFailureCategory, sName = oEntry.oCategory.sShort, fBracketed = False),
                 oEntry.sShort,
                 oEntry.sFull,
                 oEntry.iTicket,
                 oEntry.asUrls,
                 aoActions,
        ]
