# -*- coding: utf-8 -*-
# $Id: testboxstatus.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - TestBoxStatus.
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
from testmanager.core.base      import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMTooManyRows, TMRowNotFound;
from testmanager.core.testbox   import TestBoxData;


class TestBoxStatusData(ModelDataBase):
    """
    TestBoxStatus Data.
    """

    ## @name TestBoxState_T
    # @{
    ksTestBoxState_Idle                     = 'idle';
    ksTestBoxState_Testing                  = 'testing';
    ksTestBoxState_GangGathering            = 'gang-gathering';
    ksTestBoxState_GangGatheringTimedOut    = 'gang-gathering-timedout';
    ksTestBoxState_GangTesting              = 'gang-testing';
    ksTestBoxState_GangCleanup              = 'gang-cleanup';
    ksTestBoxState_Rebooting                = 'rebooting';
    ksTestBoxState_Upgrading                = 'upgrading';
    ksTestBoxState_UpgradingAndRebooting    = 'upgrading-and-rebooting';
    ksTestBoxState_DoingSpecialCmd          = 'doing-special-cmd';
    ## @}

    ksParam_idTestBox           = 'TestBoxStatus_idTestBox';
    ksParam_idGenTestBox        = 'TestBoxStatus_idGenTestBox'
    ksParam_tsUpdated           = 'TestBoxStatus_tsUpdated';
    ksParam_enmState            = 'TestBoxStatus_enmState';
    ksParam_idTestSet           = 'TestBoxStatus_idTestSet';
    ksParam_iWorkItem           = 'TestBoxStatus_iWorkItem';

    kasAllowNullAttributes      = ['idTestSet', ];
    kasValidValues_enmState     = \
    [
        ksTestBoxState_Idle,                    ksTestBoxState_Testing,     ksTestBoxState_GangGathering,
        ksTestBoxState_GangGatheringTimedOut,   ksTestBoxState_GangTesting, ksTestBoxState_GangCleanup,
        ksTestBoxState_Rebooting,               ksTestBoxState_Upgrading,   ksTestBoxState_UpgradingAndRebooting,
        ksTestBoxState_DoingSpecialCmd,
    ];

    kcDbColumns                 = 6;

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestBox           = None;
        self.idGenTestBox        = None;
        self.tsUpdated           = None;
        self.enmState            = self.ksTestBoxState_Idle;
        self.idTestSet           = None;
        self.iWorkItem           = None;

    def initFromDbRow(self, aoRow):
        """
        Internal worker for initFromDbWithId and initFromDbWithGenId as well as
        TestBoxStatusLogic.
        """

        if aoRow is None:
            raise TMRowNotFound('TestBoxStatus not found.');

        self.idTestBox           = aoRow[0];
        self.idGenTestBox        = aoRow[1];
        self.tsUpdated           = aoRow[2];
        self.enmState            = aoRow[3];
        self.idTestSet           = aoRow[4];
        self.iWorkItem           = aoRow[5];
        return self;

    def initFromDbWithId(self, oDb, idTestBox):
        """
        Initialize the object from the database.
        """
        oDb.execute('SELECT *\n'
                    'FROM   TestBoxStatuses\n'
                    'WHERE  idTestBox    = %s\n'
                    , (idTestBox, ) );
        return self.initFromDbRow(oDb.fetchOne());

    def initFromDbWithGenId(self, oDb, idGenTestBox):
        """
        Initialize the object from the database.
        """
        oDb.execute('SELECT *\n'
                    'FROM   TestBoxStatuses\n'
                    'WHERE  idGenTestBox = %s\n'
                    , (idGenTestBox, ) );
        return self.initFromDbRow(oDb.fetchOne());


class TestBoxStatusLogic(ModelLogicBase):
    """
    TestBoxStatus logic.
    """

    ## The number of seconds between each time to call touchStatus() when
    # returning CMD_IDLE.
    kcSecIdleTouchStatus = 120;


    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb);


    def tryFetchStatus(self, idTestBox):
        """
        Attempts to fetch the status of the given testbox.

        Returns a TestBoxStatusData object on success.
        Returns None if no status was found.
        Raises exception on other errors.
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     TestBoxStatuses\n'
                          'WHERE    idTestBox = %s\n',
                          (idTestBox,));
        if self._oDb.getRowCount() == 0:
            return None;
        oStatus = TestBoxStatusData();
        return oStatus.initFromDbRow(self._oDb.fetchOne());

    def tryFetchStatusAndConfig(self, idTestBox, sTestBoxUuid, sTestBoxAddr):
        """
        Tries to fetch the testbox status and current testbox config.

        Returns (TestBoxStatusData, TestBoxData) on success, (None, None) if
        not found.  May throw an exception on database error.
        """
        self._oDb.execute('SELECT   TestBoxStatuses.*,\n'
                          '         TestBoxesWithStrings.*\n'
                          'FROM     TestBoxStatuses,\n'
                          '         TestBoxesWithStrings\n'
                          'WHERE    TestBoxStatuses.idTestBox       = %s\n'
                          '     AND TestBoxesWithStrings.idTestBox  = %s\n'
                          '     AND TestBoxesWithStrings.tsExpire   = \'infinity\'::TIMESTAMP\n'
                          '     AND TestBoxesWithStrings.uuidSystem = %s\n'
                          '     AND TestBoxesWithStrings.ip         = %s\n'
                          , ( idTestBox,
                              idTestBox,
                              sTestBoxUuid,
                              sTestBoxAddr,) );
        cRows = self._oDb.getRowCount();
        if cRows != 1:
            if cRows != 0:
                raise TMTooManyRows('tryFetchStatusForCommandReq got %s rows for idTestBox=%s' % (cRows, idTestBox));
            return (None, None);
        aoRow = self._oDb.fetchOne();
        return (TestBoxStatusData().initFromDbRow(aoRow[:TestBoxStatusData.kcDbColumns]),
                TestBoxData().initFromDbRow(aoRow[TestBoxStatusData.kcDbColumns:]));


    def insertIdleStatus(self, idTestBox, idGenTestBox, fCommit = False):
        """
        Inserts an idle status for the specified testbox.
        """
        self._oDb.execute('INSERT INTO TestBoxStatuses (\n'
                          '         idTestBox,\n'
                          '         idGenTestBox,\n'
                          '         enmState,\n'
                          '         idTestSet,\n'
                          '         iWorkItem)\n'
                          'VALUES ( %s,\n'
                          '         %s,\n'
                          '         \'idle\'::TestBoxState_T,\n'
                          '         NULL,\n'
                          '         0)\n'
                          , (idTestBox, idGenTestBox) );
        self._oDb.maybeCommit(fCommit);
        return True;

    def touchStatus(self, idTestBox, fCommit = False):
        """
        Touches the testbox status row, i.e. sets tsUpdated to the current time.
        """
        self._oDb.execute('UPDATE   TestBoxStatuses\n'
                          'SET      tsUpdated = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestBox = %s\n'
                          , (idTestBox,));
        self._oDb.maybeCommit(fCommit);
        return True;

    def updateState(self, idTestBox, sNewState, idTestSet = None, fCommit = False):
        """
        Updates the testbox state.
        """
        self._oDb.execute('UPDATE   TestBoxStatuses\n'
                          'SET      enmState = %s,\n'
                          '         idTestSet = %s,\n'
                          '         tsUpdated = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestBox = %s\n',
                          (sNewState, idTestSet, idTestBox));
        self._oDb.maybeCommit(fCommit);
        return True;

    def updateGangStatus(self, idTestSetGangLeader, sNewState, fCommit = False):
        """
        Update the state of all members of a gang.
        """
        self._oDb.execute('UPDATE   TestBoxStatuses\n'
                          'SET      enmState  = %s,\n'
                          '         tsUpdated = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestBox IN (SELECT idTestBox\n'
                          '                       FROM   TestSets\n'
                          '                       WHERE  idTestSetGangLeader = %s)\n'
                          , (sNewState, idTestSetGangLeader,) );
        self._oDb.maybeCommit(fCommit);
        return True;

    def updateWorkItem(self, idTestBox, iWorkItem, fCommit = False):
        """
        Updates the testbox state.
        """
        self._oDb.execute('UPDATE   TestBoxStatuses\n'
                          'SET      iWorkItem = %s\n'
                          'WHERE    idTestBox = %s\n'
                          , ( iWorkItem, idTestBox,));
        self._oDb.maybeCommit(fCommit);
        return True;

    def isWholeGangDoneTesting(self, idTestSetGangLeader):
        """
        Checks if the whole gang is done testing.
        """
        self._oDb.execute('SELECT   COUNT(*)\n'
                          'FROM     TestBoxStatuses, TestSets\n'
                          'WHERE    TestBoxStatuses.idTestSet    = TestSets.idTestSet\n'
                          '     AND TestSets.idTestSetGangLeader = %s\n'
                          '     AND TestBoxStatuses.enmState IN (%s, %s)\n'
                          , ( idTestSetGangLeader,
                              TestBoxStatusData.ksTestBoxState_GangGathering,
                              TestBoxStatusData.ksTestBoxState_GangTesting));
        return self._oDb.fetchOne()[0] == 0;

    def isTheWholeGangThere(self, idTestSetGangLeader):
        """
        Checks if the whole gang is done testing.
        """
        self._oDb.execute('SELECT   COUNT(*)\n'
                          'FROM     TestBoxStatuses, TestSets\n'
                          'WHERE    TestBoxStatuses.idTestSet    = TestSets.idTestSet\n'
                          '     AND TestSets.idTestSetGangLeader = %s\n'
                          '     AND TestBoxStatuses.enmState IN (%s, %s)\n'
                          , ( idTestSetGangLeader,
                              TestBoxStatusData.ksTestBoxState_GangGathering,
                              TestBoxStatusData.ksTestBoxState_GangTesting));
        return self._oDb.fetchOne()[0] == 0;

    def timeSinceLastChangeInSecs(self, oStatusData):
        """
        Figures the time since the last status change.
        """
        tsNow = self._oDb.getCurrentTimestamp();
        oDelta = tsNow - oStatusData.tsUpdated;
        return oDelta.seconds + oDelta.days * 24 * 3600;


#
# Unit testing.
#

# pylint: disable=missing-docstring
class TestBoxStatusDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestBoxStatusData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

