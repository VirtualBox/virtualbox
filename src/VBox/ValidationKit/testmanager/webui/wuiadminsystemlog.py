# -*- coding: utf-8 -*-
# $Id: wuiadminsystemlog.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager WUI - Admin - System Log.
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
from testmanager.webui.wuicontentbase   import WuiListContentBase, WuiTmLink;
from testmanager.core.testbox           import TestBoxData;
from testmanager.core.systemlog         import SystemLogData;
from testmanager.core.useraccount       import UserAccountData;


class WuiAdminSystemLogList(WuiListContentBase):
    """
    WUI System Log Content Generator.
    """

    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, 'System Log',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns);
        self._asColumnHeaders = ['Date', 'Event', 'Message', 'Action'];
        self._asColumnAttribs = ['', '', '', 'align="center"'];

    def _formatListEntry(self, iEntry):
        from testmanager.webui.wuiadmin import WuiAdmin;
        oEntry  = self._aoEntries[iEntry];

        oAction = None

        if self._oDisp is None or not self._oDisp.isReadOnlyUser():
            if    oEntry.sEvent == SystemLogData.ksEvent_TestBoxUnknown \
              and oEntry.sLogText.find('addr=') >= 0 \
              and oEntry.sLogText.find('uuid=') >= 0:
                sUuid = (oEntry.sLogText[(oEntry.sLogText.find('uuid=') + 5):])[:36];
                sAddr = (oEntry.sLogText[(oEntry.sLogText.find('addr=') + 5):]).split(' ')[0];
                oAction = WuiTmLink('Add TestBox', WuiAdmin.ksScriptName,
                                    { WuiAdmin.ksParamAction:         WuiAdmin.ksActionTestBoxAdd,
                                      TestBoxData.ksParam_uuidSystem: sUuid,
                                      TestBoxData.ksParam_ip:         sAddr });

            elif oEntry.sEvent == SystemLogData.ksEvent_UserAccountUnknown:
                sUserName = oEntry.sLogText[oEntry.sLogText.find('(') + 1:
                                          oEntry.sLogText.find(')')]
                oAction = WuiTmLink('Add User', WuiAdmin.ksScriptName,
                                    { WuiAdmin.ksParamAction: WuiAdmin.ksActionUserAdd,
                                      UserAccountData.ksParam_sLoginName: sUserName });

        return [oEntry.tsCreated, oEntry.sEvent, oEntry.sLogText, oAction];

