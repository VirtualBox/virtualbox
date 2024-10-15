# -*- coding: utf-8 -*-
# $Id: systemlog.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - SystemLog.
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


# Standard python imports.
import unittest;

# Validation Kit imports.
from testmanager.core.base import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMExceptionBase;


class SystemLogData(ModelDataBase):  # pylint: disable=too-many-instance-attributes
    """
    SystemLog Data.
    """

    ## @name Event Constants
    # @{
    ksEvent_CmdNacked           = 'CmdNack ';
    ksEvent_TestBoxUnknown      = 'TBoxUnkn';
    ksEvent_TestSetAbandoned    = 'TSetAbdd';
    ksEvent_UserAccountUnknown  = 'TAccUnkn';
    ksEvent_XmlResultMalformed  = 'XmlRMalf';
    ksEvent_SchedQueueRecreate  = 'SchQRecr';
    ## @}

    ## Valid event types.
    kasEvents = \
    [ \
        ksEvent_CmdNacked,
        ksEvent_TestBoxUnknown,
        ksEvent_TestSetAbandoned,
        ksEvent_UserAccountUnknown,
        ksEvent_XmlResultMalformed,
        ksEvent_SchedQueueRecreate,
    ];

    ksParam_tsCreated           = 'tsCreated';
    ksParam_sEvent              = 'sEvent';
    ksParam_sLogText            = 'sLogText';

    kasValidValues_sEvent       = kasEvents;

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.tsCreated          = None;
        self.sEvent             = None;
        self.sLogText           = None;

    def initFromDbRow(self, aoRow):
        """
        Internal worker for initFromDbWithId and initFromDbWithGenId as well as
        SystemLogLogic.
        """

        if aoRow is None:
            raise TMExceptionBase('SystemLog row not found.');

        self.tsCreated          = aoRow[0];
        self.sEvent             = aoRow[1];
        self.sLogText           = aoRow[2];
        return self;


class SystemLogLogic(ModelLogicBase):
    """
    SystemLog logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb);

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches SystemLog entries.

        Returns an array (list) of SystemLogData items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;
        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SystemLog\n'
                              'ORDER BY tsCreated DESC\n'
                              'LIMIT %s OFFSET %s\n',
                              (cMaxRows, iStart));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     SystemLog\n'
                              'WHERE    tsCreated <= %s\n'
                              'ORDER BY tsCreated DESC\n'
                              'LIMIT %s OFFSET %s\n',
                              (tsNow, cMaxRows, iStart));
        aoRows = [];
        for _ in range(self._oDb.getRowCount()):
            oData = SystemLogData();
            oData.initFromDbRow(self._oDb.fetchOne());
            aoRows.append(oData);
        return aoRows;

    def addEntry(self, sEvent, sLogText, cHoursRepeat = 0, fCommit = False):
        """
        Adds an entry to the SystemLog table.
        Raises exception on problem.
        """
        if sEvent not in SystemLogData.kasEvents:
            raise TMExceptionBase('Unknown event type "%s"' % (sEvent,));

        # Check the repeat restriction first.
        if cHoursRepeat > 0:
            self._oDb.execute('SELECT   COUNT(*) as Stuff\n'
                              'FROM     SystemLog\n'
                              'WHERE    tsCreated >= (current_timestamp - interval \'%s hours\')\n'
                              '     AND sEvent = %s\n'
                              '     AND sLogText = %s\n',
                              (cHoursRepeat,
                               sEvent,
                               sLogText));
            aRow = self._oDb.fetchOne();
            if aRow[0] > 0:
                return None;

        # Insert it.
        self._oDb.execute('INSERT INTO SystemLog (sEvent, sLogText)\n'
                          'VALUES   (%s, %s)\n',
                          (sEvent, sLogText));

        if fCommit:
            self._oDb.commit();
        return True;

#
# Unit testing.
#

# pylint: disable=missing-docstring
class SystemLogDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [SystemLogData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

