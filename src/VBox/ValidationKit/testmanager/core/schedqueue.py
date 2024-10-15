# -*- coding: utf-8 -*-
# "$Id: schedqueue.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $"

"""
Test Manager - Test Case Queue.
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

## Standard python imports.
#import unittest

from testmanager.core.base import ModelDataBase, ModelLogicBase, TMExceptionBase #, ModelDataBaseTestCase


class SchedQueueEntry(ModelDataBase):
    """
    SchedQueue listing entry

    Note! This could be turned into a SchedQueueDataEx class if we start
          fetching all the fields from the scheduing queue.
    """

    def __init__(self):
        ModelDataBase.__init__(self)

        self.idItem                     = None
        self.tsLastScheduled            = None
        self.sSchedGroup                = None
        self.sTestGroup                 = None
        self.sTestCase                  = None
        self.fUpToDate                  = None
        self.iPerSchedGroupRowNumber    = None;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the object from a SchedQueueLogic::fetchForListing select.
        Returns self. Raises exception if aoRow is None.
        """
        if aoRow is None:
            raise TMExceptionBase('TestCaseQueue row not found.')

        self.idItem                     = aoRow[0]
        self.tsLastScheduled            = aoRow[1]
        self.sSchedGroup                = aoRow[2]
        self.sTestGroup                 = aoRow[3]
        self.sTestCase                  = aoRow[4]
        self.fUpToDate                  = aoRow[5]
        self.iPerSchedGroupRowNumber    = aoRow[6];
        return self


class SchedQueueLogic(ModelLogicBase):
    """
    SchedQueues logic.
    """
    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches SchedQueues entries.

        Returns an array (list) of SchedQueueEntry items, empty list if none.
        Raises exception on error.
        """
        _, _ = tsNow, aiSortColumns
        self._oDb.execute('''
SELECT SchedQueues.idItem,
       SchedQueues.tsLastScheduled,
       SchedGroups.sName,
       TestGroups.sName,
       TestCases.sName,
           SchedGroups.tsExpire  = 'infinity'::TIMESTAMP
       AND TestGroups.tsExpire   = 'infinity'::TIMESTAMP
       AND TestGroups.tsExpire   = 'infinity'::TIMESTAMP
       AND TestCaseArgs.tsExpire = 'infinity'::TIMESTAMP
       AND TestCases.tsExpire    = 'infinity'::TIMESTAMP AS fUpToDate,
       ROW_NUMBER() OVER (PARTITION BY SchedQueues.idSchedGroup
                              ORDER BY SchedQueues.tsLastScheduled,
                                       SchedQueues.idItem) AS iPerSchedGroupRowNumber
FROM   SchedQueues
       INNER JOIN SchedGroups
               ON SchedGroups.idSchedGroup       = SchedQueues.idSchedGroup
              AND SchedGroups.tsExpire           > SchedQueues.tsConfig
              AND SchedGroups.tsEffective       <= SchedQueues.tsConfig
       INNER JOIN TestGroups
               ON TestGroups.idTestGroup         = SchedQueues.idTestGroup
              AND TestGroups.tsExpire            > SchedQueues.tsConfig
              AND TestGroups.tsEffective        <= SchedQueues.tsConfig
       INNER JOIN TestCaseArgs
               ON TestCaseArgs.idGenTestCaseArgs = SchedQueues.idGenTestCaseArgs
       INNER JOIN TestCases
               ON TestCases.idTestCase           = TestCaseArgs.idTestCase
              AND TestCases.tsExpire             > SchedQueues.tsConfig
              AND TestCases.tsEffective         <= SchedQueues.tsConfig
ORDER BY iPerSchedGroupRowNumber,
         SchedGroups.sName DESC
LIMIT %s OFFSET %s''' % (cMaxRows, iStart,))
        aoRows = []
        for _ in range(self._oDb.getRowCount()):
            aoRows.append(SchedQueueEntry().initFromDbRow(self._oDb.fetchOne()))
        return aoRows

#
# Unit testing.
#

## @todo SchedQueueEntry isn't a typical ModelDataBase child (not fetching all
##       fields; is an extended data class mixing data from multiple tables), so
##       this won't work yet.
#
## pylint: disable=missing-docstring
#class TestCaseQueueDataTestCase(ModelDataBaseTestCase):
#    def setUp(self):
#        self.aoSamples = [SchedQueueEntry(),]
#
#
#if __name__ == '__main__':
#    unittest.main()
#    # not reached.
#
