# -*- coding: utf-8 -*-
# $Id: testcaseargs.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Test Case Arguments Variations.
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
import sys;

# Validation Kit imports.
from common                             import utils;
from testmanager.core.base              import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMExceptionBase, \
                                               TMRowNotFound;
from testmanager.core.testcase          import TestCaseData, TestCaseDependencyLogic, TestCaseGlobalRsrcDepLogic;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


class TestCaseArgsData(ModelDataBase):
    """
    Test case argument variation.
    """

    ksIdAttr    = 'idTestCaseArgs';
    ksIdGenAttr = 'idGenTestCaseArgs';

    ksParam_idTestCase         = 'TestCaseArgs_idTestCase';
    ksParam_idTestCaseArgs     = 'TestCaseArgs_idTestCaseArgs';
    ksParam_tsEffective        = 'TestCaseArgs_tsEffective';
    ksParam_tsExpire           = 'TestCaseArgs_tsExpire';
    ksParam_uidAuthor          = 'TestCaseArgs_uidAuthor';
    ksParam_idGenTestCaseArgs  = 'TestCaseArgs_idGenTestCaseArgs';
    ksParam_sArgs              = 'TestCaseArgs_sArgs';
    ksParam_cSecTimeout        = 'TestCaseArgs_cSecTimeout';
    ksParam_sTestBoxReqExpr    = 'TestCaseArgs_sTestBoxReqExpr';
    ksParam_sBuildReqExpr      = 'TestCaseArgs_sBuildReqExpr';
    ksParam_cGangMembers       = 'TestCaseArgs_cGangMembers';
    ksParam_sSubName           = 'TestCaseArgs_sSubName';

    kcDbColumns                 = 12;

    kasAllowNullAttributes      = [ 'idTestCase', 'idTestCaseArgs', 'tsEffective', 'tsExpire', 'uidAuthor', 'idGenTestCaseArgs',
                                    'cSecTimeout', 'sTestBoxReqExpr', 'sBuildReqExpr', 'sSubName', ];

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestCase         = None;
        self.idTestCaseArgs     = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;
        self.idGenTestCaseArgs  = None;
        self.sArgs              = '';
        self.cSecTimeout        = None;
        self.sTestBoxReqExpr    = None;
        self.sBuildReqExpr      = None;
        self.cGangMembers       = 1;
        self.sSubName           = None;

    def initFromDbRow(self, aoRow):
        """
        Re-initializes the object from a SELECT * FROM TestCaseArgs row.
        Returns self.  Raises exception if aoRow is None.
        """
        if aoRow is None:
            raise TMRowNotFound('TestBoxStatus not found.');

        self.idTestCase         = aoRow[0];
        self.idTestCaseArgs     = aoRow[1];
        self.tsEffective        = aoRow[2];
        self.tsExpire           = aoRow[3];
        self.uidAuthor          = aoRow[4];
        self.idGenTestCaseArgs  = aoRow[5];
        self.sArgs              = aoRow[6];
        self.cSecTimeout        = aoRow[7];
        self.sTestBoxReqExpr    = aoRow[8];
        self.sBuildReqExpr      = aoRow[9];
        self.cGangMembers       = aoRow[10];
        self.sSubName           = aoRow[11];
        return self;

    def initFromDbWithId(self, oDb, idTestCaseArgs, tsNow = None, sPeriodBack = None):
        """
        Initialize from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   TestCaseArgs\n'
                                                       'WHERE  idTestCaseArgs = %s\n'
                                                       , ( idTestCaseArgs,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idTestCaseArgs=%s not found (tsNow=%s sPeriodBack=%s)'
                                % (idTestCaseArgs, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);

    def initFromDbWithGenId(self, oDb, idGenTestCaseArgs):
        """
        Initialize from the database, given the generation ID of a row.
        """
        oDb.execute('SELECT * FROM TestCaseArgs WHERE idGenTestCaseArgs = %s', (idGenTestCaseArgs,));
        return self.initFromDbRow(oDb.fetchOne());

    def initFromValues(self, sArgs, cSecTimeout = None, sTestBoxReqExpr = None, sBuildReqExpr = None,  # pylint: disable=too-many-arguments
                       cGangMembers = 1, idTestCase = None, idTestCaseArgs = None, tsEffective = None, tsExpire = None,
                       uidAuthor = None, idGenTestCaseArgs = None, sSubName = None):
        """
        Reinitialize from values.
        Returns self.
        """
        self.idTestCase         = idTestCase;
        self.idTestCaseArgs     = idTestCaseArgs;
        self.tsEffective        = tsEffective;
        self.tsExpire           = tsExpire;
        self.uidAuthor          = uidAuthor;
        self.idGenTestCaseArgs  = idGenTestCaseArgs;
        self.sArgs              = sArgs;
        self.cSecTimeout        = utils.parseIntervalSeconds(cSecTimeout);
        self.sTestBoxReqExpr    = sTestBoxReqExpr;
        self.sBuildReqExpr      = sBuildReqExpr;
        self.cGangMembers       = cGangMembers;
        self.sSubName           = sSubName;
        return self;

    def getAttributeParamNullValues(self, sAttr):
        aoNilValues = ModelDataBase.getAttributeParamNullValues(self, sAttr);
        if sAttr == 'cSecTimeout':
            aoNilValues.insert(0, '');  # Prettier NULL value for cSecTimeout.
        elif sAttr == 'sArgs':
            aoNilValues = [];           # No NULL value here, thank you.
        return aoNilValues;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        if sAttr == 'cSecTimeout' and oValue not in aoNilValues: # Allow human readable interval formats.
            return utils.parseIntervalSeconds(oValue);

        (oValue, sError) = ModelDataBase._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);
        if sError is None:
            if sAttr == 'sTestBoxReqExpr':
                sError = TestCaseData.validateTestBoxReqExpr(oValue);
            elif sAttr == 'sBuildReqExpr':
                sError = TestCaseData.validateBuildReqExpr(oValue);
        return (oValue, sError);




class TestCaseArgsDataEx(TestCaseArgsData):
    """
    Complete data set.
    """

    def __init__(self):
        TestCaseArgsData.__init__(self);
        self.oTestCase          = None;
        self.aoTestCasePreReqs  = [];
        self.aoGlobalRsrc       = [];

    def initFromDbRow(self, aoRow):
        raise TMExceptionBase('Do not call me: %s' % (aoRow,))

    def initFromDbRowEx(self, aoRow, oDb, tsConfigEff = None, tsRsrcEff = None):
        """
        Extended version of initFromDbRow that fills in the rest from the database.
        """
        TestCaseArgsData.initFromDbRow(self, aoRow);

        if tsConfigEff is None: tsConfigEff = oDb.getCurrentTimestamp();
        if tsRsrcEff is None:   tsRsrcEff   = oDb.getCurrentTimestamp();

        self.oTestCase         = TestCaseData().initFromDbWithId(oDb, self.idTestCase, tsConfigEff);
        self.aoTestCasePreReqs = TestCaseDependencyLogic(oDb).getTestCaseDeps(self.idTestCase, tsConfigEff);
        self.aoGlobalRsrc      = TestCaseGlobalRsrcDepLogic(oDb).getTestCaseDeps(self.idTestCase, tsRsrcEff);

        return self;

    def initFromDbWithId(self, oDb, idTestCaseArgs, tsNow = None, sPeriodBack = None):
        _ = oDb; _ = idTestCaseArgs; _ = tsNow; _ = sPeriodBack;
        raise TMExceptionBase('Not supported.');

    def initFromDbWithGenId(self, oDb, idGenTestCaseArgs):
        _ = oDb; _ = idGenTestCaseArgs;
        raise TMExceptionBase('Use initFromDbWithGenIdEx...');

    def initFromDbWithGenIdEx(self, oDb, idGenTestCaseArgs, tsConfigEff = None, tsRsrcEff = None):
        """
        Initialize from the database, given the ID of a row.
        """
        oDb.execute('SELECT *, CURRENT_TIMESTAMP FROM TestCaseArgs WHERE idGenTestCaseArgs = %s', (idGenTestCaseArgs,));
        aoRow = oDb.fetchOne();
        return self.initFromDbRowEx(aoRow, oDb, tsConfigEff, tsRsrcEff);

    def convertFromParamNull(self):
        raise TMExceptionBase('Not implemented');

    def convertToParamNull(self):
        raise TMExceptionBase('Not implemented');

    def isEqual(self, oOther):
        raise TMExceptionBase('Not implemented');

    def matchesTestBoxProps(self, oTestBoxData):
        """
        Checks if the all of the testbox related test requirements matches the
        given testbox.

        Returns True or False according to the expression, None on exception or
        non-boolean expression result.
        """
        return TestCaseData.matchesTestBoxPropsEx(oTestBoxData, self.oTestCase.sTestBoxReqExpr) \
           and TestCaseData.matchesTestBoxPropsEx(oTestBoxData, self.sTestBoxReqExpr);

    def matchesBuildProps(self, oBuildDataEx):
        """
        Checks if the all of the build related test requirements matches the
        given build.

        Returns True or False according to the expression, None on exception or
        non-boolean expression result.
        """
        return TestCaseData.matchesBuildPropsEx(oBuildDataEx, self.oTestCase.sBuildReqExpr) \
           and TestCaseData.matchesBuildPropsEx(oBuildDataEx, self.sBuildReqExpr);


class TestCaseArgsLogic(ModelLogicBase):
    """
    TestCaseArgs database logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb);
        self.dCache = None;


    def areResourcesFree(self, oDataEx):
        """
        Checks if all global resources are currently still in existance and free.
        Returns True/False.  May raise exception on database error.
        """

        # Create a set of global resource IDs.
        if not oDataEx.aoGlobalRsrc:
            return True;
        asIdRsrcs = [str(oDep.idGlobalRsrc) for oDep, _ in oDataEx.aoGlobalRsrc];

        # A record in the resource status table means it's allocated.
        self._oDb.execute('SELECT   COUNT(*)\n'
                          'FROM     GlobalResourceStatuses\n'
                          'WHERE    GlobalResourceStatuses.idGlobalRsrc IN (' + ', '.join(asIdRsrcs) + ')\n');
        if self._oDb.fetchOne()[0] == 0:
            # Check for disabled or deleted resources (we cannot allocate them).
            self._oDb.execute('SELECT   COUNT(*)\n'
                              'FROM     GlobalResources\n'
                              'WHERE    GlobalResources.idGlobalRsrc IN (' + ', '.join(asIdRsrcs) + ')\n'
                              '     AND GlobalResources.tsExpire      = \'infinity\'::TIMESTAMP\n'
                              '     AND GlobalResources.fEnabled      = TRUE\n');
            if self._oDb.fetchOne()[0] == len(oDataEx.aoGlobalRsrc):
                return True;
        return False;

    def getAll(self):
        """Get list of objects of type TestCaseArgsData"""
        self._oDb.execute('SELECT   *\n'
                          'FROM     TestCaseArgs\n'
                          'WHERE    tsExpire = \'infinity\'::TIMESTAMP')
        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            aoRet.append(TestCaseArgsData().initFromDbRow(aoRow))

        return aoRet

    def getTestCaseArgs(self, idTestCase, tsNow = None, aiWhiteList = None):
        """Get list of testcase's arguments variations"""
        if aiWhiteList is None:
            if tsNow is None:
                self._oDb.execute('SELECT   *\n'
                                  'FROM     TestCaseArgs\n'
                                  'WHERE    idTestCase = %s\n'
                                  '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                                  'ORDER BY TestCaseArgs.idTestCaseArgs\n'
                                  , (idTestCase,));
            else:
                self._oDb.execute('SELECT   *\n'
                                  'FROM     TestCaseArgs\n'
                                  'WHERE    idTestCase   = %s\n'
                                  '     AND tsExpire     > %s\n'
                                  '     AND tsEffective <= %s\n'
                                  'ORDER BY TestCaseArgs.idTestCaseArgs\n'
                                  , (idTestCase, tsNow, tsNow));
        else:
            sWhiteList = ','.join((str(x) for x in aiWhiteList));
            if tsNow is None:
                self._oDb.execute('SELECT   *\n'
                                  'FROM     TestCaseArgs\n'
                                  'WHERE    idTestCase = %s\n'
                                  '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                                  '     AND idTestCaseArgs IN (' + sWhiteList + ')\n'
                                  'ORDER BY TestCaseArgs.idTestCaseArgs\n'
                                  , (idTestCase,));
            else:
                self._oDb.execute('SELECT   *\n'
                                  'FROM     TestCaseArgs\n'
                                  'WHERE    idTestCase   = %s\n'
                                  '     AND tsExpire     > %s\n'
                                  '     AND tsEffective <= %s\n'
                                  '     AND idTestCaseArgs IN (' + sWhiteList + ')\n'
                                  'ORDER BY TestCaseArgs.idTestCaseArgs\n'
                                  , (idTestCase, tsNow, tsNow));

        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            aoRet.append(TestCaseArgsData().initFromDbRow(aoRow))

        return aoRet

    def addTestCaseArgs(self, oTestCaseArgsData):
        """Add Test Case Args record into DB"""
        pass;                               # pylint: disable=unnecessary-pass

    def cachedLookup(self, idTestCaseArgs):
        """
        Looks up the most recent TestCaseArgsDataEx object for idTestCaseArg
        via in an object cache.

        Returns a shared TestCaseArgDataEx object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('TestCaseArgsDataEx');
        oEntry = self.dCache.get(idTestCaseArgs, None);
        if oEntry is None:
            fNeedTsNow = False;
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCaseArgs\n'
                              'WHERE    idTestCaseArgs = %s\n'
                              '     AND tsExpire       = \'infinity\'::TIMESTAMP\n'
                              , (idTestCaseArgs, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     TestCaseArgs\n'
                                  'WHERE    idTestCaseArgs = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idTestCaseArgs, ));
                fNeedTsNow = True;
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idTestCaseArgs));

            if self._oDb.getRowCount() == 1:
                aaoRow = self._oDb.fetchOne();
                oEntry = TestCaseArgsDataEx();
                tsNow  = TestCaseArgsData().initFromDbRow(aaoRow).tsEffective if fNeedTsNow else None;
                oEntry.initFromDbRowEx(aaoRow, self._oDb, tsNow, tsNow);
                self.dCache[idTestCaseArgs] = oEntry;
        return oEntry;


#
# Unit testing.
#

# pylint: disable=missing-docstring
class TestCaseArgsDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestCaseArgsData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

