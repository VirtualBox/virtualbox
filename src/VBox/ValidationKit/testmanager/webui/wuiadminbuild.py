# -*- coding: utf-8 -*-
# $Id: wuiadminbuild.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Builds.
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
from testmanager.webui.wuicontentbase   import WuiFormContentBase, WuiListContentBase, WuiTmLink, WuiBuildLogLink, \
                                               WuiSvnLinkWithTooltip;
from testmanager.core.build             import BuildData, BuildCategoryLogic;
from testmanager.core.buildblacklist    import BuildBlacklistData;
from testmanager.core.db                import isDbTimestampInfinity;


class WuiAdminBuild(WuiFormContentBase):
    """
    WUI Build HTML content generator.
    """

    def __init__(self, oData, sMode, oDisp):
        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'Add Build'
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Modify Build - #%s' % (oData.idBuild,);
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;
            sTitle = 'Build - #%s' % (oData.idBuild,);
        WuiFormContentBase.__init__(self, oData, sMode, 'Build', oDisp, sTitle);

    def _populateForm(self, oForm, oData):
        oForm.addIntRO      (BuildData.ksParam_idBuild,             oData.idBuild,            'Build ID')
        oForm.addTimestampRO(BuildData.ksParam_tsCreated,           oData.tsCreated,          'Created')
        oForm.addTimestampRO(BuildData.ksParam_tsEffective,         oData.tsEffective,        'Last changed')
        oForm.addTimestampRO(BuildData.ksParam_tsExpire,            oData.tsExpire,           'Expires (excl)')
        oForm.addIntRO      (BuildData.ksParam_uidAuthor,           oData.uidAuthor,          'Changed by UID')

        oForm.addComboBox   (BuildData.ksParam_idBuildCategory,     oData.idBuildCategory,    'Build category',
                             BuildCategoryLogic(self._oDisp.getDb()).fetchForCombo());

        oForm.addInt        (BuildData.ksParam_iRevision,           oData.iRevision,          'Revision')
        oForm.addText       (BuildData.ksParam_sVersion,            oData.sVersion,           'Version')
        oForm.addWideText   (BuildData.ksParam_sLogUrl,             oData.sLogUrl,            'Log URL')
        oForm.addWideText   (BuildData.ksParam_sBinaries,           oData.sBinaries,          'Binaries')
        oForm.addCheckBox   (BuildData.ksParam_fBinariesDeleted,    oData.fBinariesDeleted,   'Binaries deleted')

        oForm.addSubmit()
        return True;


class WuiAdminBuildList(WuiListContentBase):
    """
    WUI Admin Build List Content Generator.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective,
                                    sTitle = 'Builds', sId = 'builds', fnDPrint = fnDPrint, oDisp = oDisp,
                                    aiSelectedSortColumns = aiSelectedSortColumns);

        self._asColumnHeaders = ['ID', 'Product', 'Branch', 'Version',
                                 'Type', 'OS(es)', 'Author', 'Added',
                                 'Files', 'Action' ];
        self._asColumnAttribs = ['align="right"', 'align="center"', 'align="center"', 'align="center"',
                                 'align="center"', 'align="center"', 'align="center"', 'align="center"',
                                 '', 'align="center"'];

    def _formatListEntry(self, iEntry):
        from testmanager.webui.wuiadmin import WuiAdmin
        oEntry = self._aoEntries[iEntry];

        aoActions = [];
        if oEntry.sLogUrl is not None:
            aoActions.append(WuiBuildLogLink(oEntry.sLogUrl, 'Build Log'));

        dParams = { WuiAdmin.ksParamAction:                    WuiAdmin.ksActionBuildBlacklistAdd,
                    BuildBlacklistData.ksParam_sProduct:       oEntry.oCat.sProduct,
                    BuildBlacklistData.ksParam_sBranch:        oEntry.oCat.sBranch,
                    BuildBlacklistData.ksParam_asTypes:        oEntry.oCat.sType,
                    BuildBlacklistData.ksParam_asOsArches:     oEntry.oCat.asOsArches,
                    BuildBlacklistData.ksParam_iFirstRevision: oEntry.iRevision,
                    BuildBlacklistData.ksParam_iLastRevision:  oEntry.iRevision }

        if self._oDisp is None or not self._oDisp.isReadOnlyUser():
            aoActions += [
                WuiTmLink('Blacklist', WuiAdmin.ksScriptName, dParams),
                WuiTmLink('Details', WuiAdmin.ksScriptName,
                          { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildDetails,
                            BuildData.ksParam_idBuild: oEntry.idBuild,
                            WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, }),
                WuiTmLink('Clone', WuiAdmin.ksScriptName,
                          { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildClone,
                            BuildData.ksParam_idBuild: oEntry.idBuild,
                            WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, }),
            ];
            if isDbTimestampInfinity(oEntry.tsExpire):
                aoActions += [
                    WuiTmLink('Modify', WuiAdmin.ksScriptName,
                              { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildEdit,
                                BuildData.ksParam_idBuild: oEntry.idBuild }),
                    WuiTmLink('Remove', WuiAdmin.ksScriptName,
                              { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildDoRemove,
                                BuildData.ksParam_idBuild: oEntry.idBuild },
                              sConfirm = 'Are you sure you want to remove build #%d?' % (oEntry.idBuild,) ),
                ];

        return [ oEntry.idBuild,
                 oEntry.oCat.sProduct,
                 oEntry.oCat.sBranch,
                 WuiSvnLinkWithTooltip(oEntry.iRevision, oEntry.oCat.sRepository,
                                       sName = '%s r%s' % (oEntry.sVersion, oEntry.iRevision,)),
                 oEntry.oCat.sType,
                 ' '.join(oEntry.oCat.asOsArches),
                 'batch' if oEntry.uidAuthor is None else oEntry.uidAuthor,
                 self.formatTsShort(oEntry.tsCreated),
                 oEntry.sBinaries if not oEntry.fBinariesDeleted else '<Deleted>',
                 aoActions,
        ];

