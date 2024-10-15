# -*- coding: utf-8 -*-
# "$Id: wuiadminschedqueue.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $"

"""
Test Manager WUI - Admin - Scheduling Queue.
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


# Validation Kit imports
from testmanager.webui.wuicontentbase import WuiListContentBase


class WuiAdminSchedQueueList(WuiListContentBase):
    """
    WUI Scheduling Queue Content Generator.
    """
    def __init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp, aiSelectedSortColumns = None):
        tsEffective = None; # Not relevant, no history on the scheduling queue.
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective, 'Scheduling Queue',
                                    fnDPrint = fnDPrint, oDisp = oDisp, aiSelectedSortColumns = aiSelectedSortColumns,
                                    fTimeNavigation = False);
        self._asColumnHeaders = [
            'Last Run',       'Scheduling Group', 'Test Group',     'Test Case',      'Config State',   'Item ID'
        ];
        self._asColumnAttribs = [
            'align="center"', 'align="center"',   'align="center"', 'align="center"', 'align="center"', 'align="center"'
        ];
        self._iPrevPerSchedGroupRowNumber = 0;

    def _formatListEntry(self, iEntry):
        oEntry = self._aoEntries[iEntry] # type: SchedQueueEntry
        sState = 'up-to-date' if oEntry.fUpToDate else 'outdated';
        return [ oEntry.tsLastScheduled, oEntry.sSchedGroup, oEntry.sTestGroup, oEntry.sTestCase, sState, oEntry.idItem ];

    def _formatListEntryHtml(self, iEntry):
        sHtml = WuiListContentBase._formatListEntryHtml(self, iEntry);

        # Insert separator row?
        if iEntry < len(self._aoEntries):
            oEntry = self._aoEntries[iEntry] # type: SchedQueueEntry
            if oEntry.iPerSchedGroupRowNumber != self._iPrevPerSchedGroupRowNumber:
                if iEntry > 0 and iEntry + 1 < min(len(self._aoEntries), self._cItemsPerPage):
                    sHtml += '<tr class="tmseparator"><td colspan=%s> </td></tr>\n' % (len(self._asColumnHeaders),);
                self._iPrevPerSchedGroupRowNumber = oEntry.iPerSchedGroupRowNumber;
        return sHtml;

