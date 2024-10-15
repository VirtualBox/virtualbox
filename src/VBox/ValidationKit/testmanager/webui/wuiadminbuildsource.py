# -*- coding: utf-8 -*-
# $Id: wuiadminbuildsource.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Build Sources.
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
from testmanager.core                   import coreconsts;
from testmanager.core.db                import isDbTimestampInfinity;
from testmanager.core.buildsource       import BuildSourceData;


class WuiAdminBuildSrc(WuiFormContentBase):
    """
    WUI Build Sources HTML content generator.
    """

    def __init__(self, oData, sMode, oDisp):
        assert isinstance(oData, BuildSourceData);
        if sMode == WuiFormContentBase.ksMode_Add:
            sTitle = 'New Build Source';
        elif sMode == WuiFormContentBase.ksMode_Edit:
            sTitle = 'Edit Build Source - %s (#%s)' % (oData.sName, oData.idBuildSrc,);
        else:
            assert sMode == WuiFormContentBase.ksMode_Show;
            sTitle = 'Build Source - %s (#%s)' % (oData.sName, oData.idBuildSrc,);
        WuiFormContentBase.__init__(self, oData, sMode, 'BuildSrc', oDisp, sTitle);

    def _populateForm(self, oForm, oData):
        oForm.addIntRO      (BuildSourceData.ksParam_idBuildSrc,       oData.idBuildSrc,       'Build Source item ID')
        oForm.addTimestampRO(BuildSourceData.ksParam_tsEffective,      oData.tsEffective,      'Last changed')
        oForm.addTimestampRO(BuildSourceData.ksParam_tsExpire,         oData.tsExpire,         'Expires (excl)')
        oForm.addIntRO      (BuildSourceData.ksParam_uidAuthor,        oData.uidAuthor,        'Changed by UID')
        oForm.addText       (BuildSourceData.ksParam_sName,            oData.sName,            'Name')
        oForm.addText       (BuildSourceData.ksParam_sDescription,     oData.sDescription,     'Description')
        oForm.addText       (BuildSourceData.ksParam_sProduct,         oData.sProduct,         'Product')
        oForm.addText       (BuildSourceData.ksParam_sBranch,          oData.sBranch,          'Branch')
        asTypes    = self.getListOfItems(coreconsts.g_kasBuildTypesAll, oData.asTypes);
        oForm.addListOfTypes(BuildSourceData.ksParam_asTypes,          asTypes,                'Build types')
        asOsArches = self.getListOfItems(coreconsts.g_kasOsDotCpusAll, oData.asOsArches);
        oForm.addListOfOsArches(BuildSourceData.ksParam_asOsArches,    asOsArches,             'Target architectures')
        oForm.addInt        (BuildSourceData.ksParam_iFirstRevision,   oData.iFirstRevision,   'Starting from revision')
        oForm.addInt        (BuildSourceData.ksParam_iLastRevision,    oData.iLastRevision,    'Ending by revision')
        oForm.addLong       (BuildSourceData.ksParam_cSecMaxAge,
                             utils.formatIntervalSeconds2(oData.cSecMaxAge) if oData.cSecMaxAge not in [-1, '', None] else '',
                             'Max age in seconds');
        oForm.addSubmit();
        return True;

class WuiAdminBuildSrcList(WuiListContentBase):
    """
    WUI Build Source content generator.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective,
                                    sTitle = 'Registered Build Sources', sId = 'build sources',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);
        self._asColumnHeaders = ['ID', 'Name', 'Description', 'Product',
                                 'Branch', 'Build Types', 'OS/ARCH', 'First Revision', 'Last Revision', 'Max Age',
                                 'Actions' ];
        self._asColumnAttribs = ['align="center"', 'align="center"', 'align="center"', 'align="center"', 'align="center"',
                                 'align="left"', 'align="left"', 'align="center"', 'align="center"', 'align="center"',
                                 'align="center"' ];

    def _getSubList(self, aList):
        """
        Convert pythonic list into HTML list
        """
        if aList not in (None, []):
            sHtml = '  <ul class="tmshowall">\n'
            for sTmp in aList:
                sHtml += '    <li class="tmshowall">%s</a></li>\n' % (webutils.escapeElem(sTmp),);
            sHtml += '  </ul>\n';
        else:
            sHtml = '<ul class="tmshowall"><li class="tmshowall">Any</li></ul>\n';

        return WuiRawHtml(sHtml);

    def _formatListEntry(self, iEntry):
        """
        Format *show all* table entry
        """

        from testmanager.webui.wuiadmin import WuiAdmin
        oEntry  = self._aoEntries[iEntry]

        aoActions = [
            WuiTmLink('Details', WuiAdmin.ksScriptName,
                      { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildSrcDetails,
                        BuildSourceData.ksParam_idBuildSrc: oEntry.idBuildSrc,
                        WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, }),
        ];
        if self._oDisp is None or not self._oDisp.isReadOnlyUser():
            aoActions += [
                WuiTmLink('Clone', WuiAdmin.ksScriptName,
                          { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildSrcClone,
                            BuildSourceData.ksParam_idBuildSrc: oEntry.idBuildSrc,
                            WuiAdmin.ksParamEffectiveDate: self._tsEffectiveDate, }),
            ];
            if isDbTimestampInfinity(oEntry.tsExpire):
                aoActions += [
                    WuiTmLink('Modify', WuiAdmin.ksScriptName,
                              { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildSrcEdit,
                                BuildSourceData.ksParam_idBuildSrc: oEntry.idBuildSrc } ),
                    WuiTmLink('Remove', WuiAdmin.ksScriptName,
                              { WuiAdmin.ksParamAction: WuiAdmin.ksActionBuildSrcDoRemove,
                                BuildSourceData.ksParam_idBuildSrc: oEntry.idBuildSrc },
                              sConfirm = 'Are you sure you want to remove build source #%d?' % (oEntry.idBuildSrc,) )
                ];

        return [ oEntry.idBuildSrc,
                 oEntry.sName,
                 oEntry.sDescription,
                 oEntry.sProduct,
                 oEntry.sBranch,
                 self._getSubList(oEntry.asTypes),
                 self._getSubList(oEntry.asOsArches),
                 oEntry.iFirstRevision,
                 oEntry.iLastRevision,
                 utils.formatIntervalSeconds2(oEntry.cSecMaxAge) if oEntry.cSecMaxAge is not None else None,
                 aoActions,
        ]
