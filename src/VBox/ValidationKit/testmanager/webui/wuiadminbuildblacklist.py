# -*- coding: utf-8 -*-
# $Id: wuiadminbuildblacklist.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Build Blacklist.
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
from testmanager.webui.wuicontentbase import WuiFormContentBase, WuiListContentBase, WuiTmLink
from testmanager.core.buildblacklist  import BuildBlacklistData
from testmanager.core.failurereason   import FailureReasonLogic
from testmanager.core.db              import TMDatabaseConnection
from testmanager.core                 import coreconsts


class WuiAdminBuildBlacklist(WuiFormContentBase):
    """
    WUI Build Black List Form.
    """

    def __init__(self, oData, sMode, oDisp):
        """
        Prepare & initialize parent
        """

        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'Add Build Blacklist Entry'
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Edit Build Blacklist Entry'
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;
            sTitle = 'Build Black';
        WuiFormContentBase.__init__(self, oData, sMode, 'BuildBlacklist', oDisp, sTitle);

        #
        # Additional data.
        #
        self.asTypes    = coreconsts.g_kasBuildTypesAll
        self.asOsArches = coreconsts.g_kasOsDotCpusAll

    def _populateForm(self, oForm, oData):
        """
        Construct an HTML form
        """

        aoFailureReasons = FailureReasonLogic(self._oDisp.getDb()).fetchForCombo()
        if not aoFailureReasons:
            from testmanager.webui.wuiadmin import WuiAdmin
            raise WuiException('Please <a href="%s?%s=%s">add</a> some Failure Reasons first.'
                               % (WuiAdmin.ksScriptName, WuiAdmin.ksParamAction, WuiAdmin.ksActionFailureReasonAdd));

        asTypes    = self.getListOfItems(self.asTypes,    oData.asTypes)
        asOsArches = self.getListOfItems(self.asOsArches, oData.asOsArches)

        oForm.addIntRO      (BuildBlacklistData.ksParam_idBlacklisting,  oData.idBlacklisting,  'Blacklist item ID')
        oForm.addTimestampRO(BuildBlacklistData.ksParam_tsEffective,     oData.tsEffective,     'Last changed')
        oForm.addTimestampRO(BuildBlacklistData.ksParam_tsExpire,        oData.tsExpire,        'Expires (excl)')
        oForm.addIntRO      (BuildBlacklistData.ksParam_uidAuthor,       oData.uidAuthor,        'Changed by UID')

        oForm.addComboBox   (BuildBlacklistData.ksParam_idFailureReason, oData.idFailureReason, 'Failure Reason',
                             aoFailureReasons)

        oForm.addText       (BuildBlacklistData.ksParam_sProduct,        oData.sProduct,        'Product')
        oForm.addText       (BuildBlacklistData.ksParam_sBranch,         oData.sBranch,         'Branch')
        oForm.addListOfTypes(BuildBlacklistData.ksParam_asTypes,         asTypes,               'Build types')
        oForm.addListOfOsArches(BuildBlacklistData.ksParam_asOsArches,   asOsArches,            'Target architectures')
        oForm.addInt        (BuildBlacklistData.ksParam_iFirstRevision,  oData.iFirstRevision,  'First revision')
        oForm.addInt        (BuildBlacklistData.ksParam_iLastRevision,   oData.iLastRevision,   'Last revision (incl)')

        oForm.addSubmit();

        return True;


class WuiAdminListOfBlacklistItems(WuiListContentBase):
    """
    WUI Admin Build Blacklist Content Generator.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective,
                                    sTitle = 'Build Blacklist', sId = 'buildsBlacklist',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);

        self._asColumnHeaders = ['ID', 'Failure Reason',
                                 'Product', 'Branch', 'Type',
                                 'OS(es)', 'First Revision', 'Last Revision',
                                 'Actions' ]
        self._asColumnAttribs = ['align="right"', 'align="center"', 'align="center"', 'align="center"',
                                 'align="center"', 'align="center"', 'align="center"', 'align="center"',
                                 'align="center"', 'align="center"', 'align="center"', 'align="center"',
                                 'align="center"' ]

    def _formatListEntry(self, iEntry):
        from testmanager.webui.wuiadmin import WuiAdmin
        oEntry = self._aoEntries[iEntry]

        sShortFailReason = FailureReasonLogic(TMDatabaseConnection()).getById(oEntry.idFailureReason).sShort

        aoActions = [
            WuiTmLink('Details', WuiAdmin.ksScriptName,
                      { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildBlacklistDetails,
                        BuildBlacklistData.ksParam_idBlacklisting: oEntry.idBlacklisting }),
        ];
        if self._oDisp is None or not self._oDisp.isReadOnlyUser():
            aoActions += [
              WuiTmLink('Edit', WuiAdmin.ksScriptName,
                        { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildBlacklistEdit,
                          BuildBlacklistData.ksParam_idBlacklisting: oEntry.idBlacklisting }),
              WuiTmLink('Clone', WuiAdmin.ksScriptName,
                        { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildBlacklistClone,
                          BuildBlacklistData.ksParam_idBlacklisting: oEntry.idBlacklisting,
                          WuiAdmin.ksParamEffectiveDate: oEntry.tsEffective,  }),
              WuiTmLink('Remove', WuiAdmin.ksScriptName,
                        { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildBlacklistDoRemove,
                          BuildBlacklistData.ksParam_idBlacklisting: oEntry.idBlacklisting },
                        sConfirm = 'Are you sure you want to remove black list entry #%d?' % (oEntry.idBlacklisting,)),
             ];

        return [ oEntry.idBlacklisting,
                 sShortFailReason,
                 oEntry.sProduct,
                 oEntry.sBranch,
                 oEntry.asTypes,
                 oEntry.asOsArches,
                 oEntry.iFirstRevision,
                 oEntry.iLastRevision,
                 aoActions
        ];
