# -*- coding: utf-8 -*-
# $Id: testgroup.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Test groups management.
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
from testmanager.core.base              import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMRowInUse, \
                                               TMTooManyRows, TMInvalidData, TMRowNotFound, TMRowAlreadyExists;
from testmanager.core.testcase          import TestCaseData, TestCaseDataEx;


class TestGroupMemberData(ModelDataBase):
    """Representation of a test group member database row."""

    ksParam_idTestGroup         = 'TestGroupMember_idTestGroup';
    ksParam_idTestCase          = 'TestGroupMember_idTestCase';
    ksParam_tsEffective         = 'TestGroupMember_tsEffective';
    ksParam_tsExpire            = 'TestGroupMember_tsExpire';
    ksParam_uidAuthor           = 'TestGroupMember_uidAuthor';
    ksParam_iSchedPriority      = 'TestGroupMember_iSchedPriority';
    ksParam_aidTestCaseArgs     = 'TestGroupMember_aidTestCaseArgs';

    kasAllowNullAttributes      = ['idTestGroup', 'idTestCase', 'tsEffective', 'tsExpire', 'uidAuthor', 'aidTestCaseArgs' ];
    kiMin_iSchedPriority        = 0;
    kiMax_iSchedPriority        = 31;

    kcDbColumns                 = 7;

    def __init__(self):
        ModelDataBase.__init__(self)

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestGroup        = None;
        self.idTestCase         = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;
        self.iSchedPriority     = 16;
        self.aidTestCaseArgs    = None;

    def initFromDbRow(self, aoRow):
        """
        Reinitialize from a SELECT * FROM TestCaseGroupMembers.
        Return self. Raises exception if no row.
        """
        if aoRow is None:
            raise TMRowNotFound('Test group member not found.')

        self.idTestGroup     = aoRow[0];
        self.idTestCase      = aoRow[1];
        self.tsEffective     = aoRow[2];
        self.tsExpire        = aoRow[3];
        self.uidAuthor       = aoRow[4];
        self.iSchedPriority  = aoRow[5];
        self.aidTestCaseArgs = aoRow[6];
        return self


    def getAttributeParamNullValues(self, sAttr):
        # Arrays default to [] as NULL currently. That doesn't work for us.
        if sAttr == 'aidTestCaseArgs':
            aoNilValues = [None, '-1'];
        else:
            aoNilValues = ModelDataBase.getAttributeParamNullValues(self, sAttr);
        return aoNilValues;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        if sAttr != 'aidTestCaseArgs':
            return ModelDataBase._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);

        # -1 is a special value, which when present make the whole thing NULL (None).
        (aidVariations, sError) = self.validateListOfInts(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                          iMin = -1, iMax = 0x7ffffffe);
        if sError is None:
            if aidVariations is None:
                pass;
            elif -1 in aidVariations:
                aidVariations = None;
            elif 0 in aidVariations:
                sError = 'Invalid test case varation ID #0.';
            else:
                aidVariations = sorted(aidVariations);
        return (aidVariations, sError);



class TestGroupMemberDataEx(TestGroupMemberData):
    """Extended representation of a test group member."""

    def __init__(self):
        """Extend parent class"""
        TestGroupMemberData.__init__(self)
        self.oTestCase = None; # TestCaseDataEx.

    def initFromDbRowEx(self, aoRow, oDb, tsNow = None):
        """
        Reinitialize from a SELECT * FROM TestGroupMembers, TestCases row.
        Will query the necessary additional data from oDb using tsNow.

        Returns self.  Raises exception if no row or database error.
        """
        TestGroupMemberData.initFromDbRow(self, aoRow);
        self.oTestCase = TestCaseDataEx();
        self.oTestCase.initFromDbRowEx(aoRow[TestGroupMemberData.kcDbColumns:], oDb, tsNow);
        return self;

    def initFromParams(self, oDisp, fStrict = True):
        self.oTestCase = None;
        return TestGroupMemberData.initFromParams(self, oDisp, fStrict);

    def getDataAttributes(self):
        asAttributes = TestGroupMemberData.getDataAttributes(self);
        asAttributes.remove('oTestCase');
        return asAttributes;

    def _validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor = ModelDataBase.ksValidateFor_Other):
        dErrors = TestGroupMemberData._validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor);
        if self.ksParam_idTestCase not in dErrors:
            self.oTestCase = TestCaseDataEx()
            try:
                self.oTestCase.initFromDbWithId(oDb, self.idTestCase);
            except Exception as oXcpt:
                self.oTestCase = TestCaseDataEx()
                dErrors[self.ksParam_idTestCase] = str(oXcpt);
        return dErrors;


class TestGroupMemberData2(TestCaseData):
    """Special representation of a Test Group Member item"""

    def __init__(self):
        """Extend parent class"""
        TestCaseData.__init__(self)
        self.idTestGroup = None
        self.aidTestCaseArgs = []

    def initFromDbRowEx(self, aoRow):
        """
        Reinitialize from this query:

            SELECT TestCases.*,
                   TestGroupMembers.idTestGroup,
                   TestGroupMembers.aidTestCaseArgs
            FROM TestCases, TestGroupMembers
            WHERE TestCases.idTestCase = TestGroupMembers.idTestCase

        Represents complete test group member (test case) info.
        Returns object of type TestGroupMemberData2. Raises exception if no row.
        """
        TestCaseData.initFromDbRow(self, aoRow);
        self.idTestGroup     = aoRow[-2]
        self.aidTestCaseArgs = aoRow[-1]
        return self;


class TestGroupData(ModelDataBase):
    """
    Test group data.
    """

    ksIdAttr    = 'idTestGroup';

    ksParam_idTestGroup     = 'TestGroup_idTestGroup'
    ksParam_tsEffective     = 'TestGroup_tsEffective'
    ksParam_tsExpire        = 'TestGroup_tsExpire'
    ksParam_uidAuthor       = 'TestGroup_uidAuthor'
    ksParam_sName           = 'TestGroup_sName'
    ksParam_sDescription    = 'TestGroup_sDescription'
    ksParam_sComment        = 'TestGroup_sComment'

    kasAllowNullAttributes      = ['idTestGroup', 'tsEffective', 'tsExpire', 'uidAuthor', 'sDescription', 'sComment' ];

    kcDbColumns             = 7;

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestGroup     = None
        self.tsEffective     = None
        self.tsExpire        = None
        self.uidAuthor       = None
        self.sName           = None
        self.sDescription    = None
        self.sComment        = None

    def initFromDbRow(self, aoRow):
        """
        Reinitialize from a SELECT * FROM TestGroups row.
        Returns object of type TestGroupData. Raises exception if no row.
        """
        if aoRow is None:
            raise TMRowNotFound('Test group not found.')

        self.idTestGroup     = aoRow[0]
        self.tsEffective     = aoRow[1]
        self.tsExpire        = aoRow[2]
        self.uidAuthor       = aoRow[3]
        self.sName           = aoRow[4]
        self.sDescription    = aoRow[5]
        self.sComment        = aoRow[6]
        return self

    def initFromDbWithId(self, oDb, idTestGroup, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   TestGroups\n'
                                                       'WHERE  idTestGroup = %s\n'
                                                       , ( idTestGroup,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idTestGroup=%s not found (tsNow=%s sPeriodBack=%s)' % (idTestGroup, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);


class TestGroupDataEx(TestGroupData):
    """
    Extended test group data.
    """

    ksParam_aoMembers    = 'TestGroupDataEx_aoMembers';
    kasAltArrayNull      = [ 'aoMembers', ];

    ## Helper parameter containing the comma separated list with the IDs of
    #  potential members found in the parameters.
    ksParam_aidTestCases = 'TestGroupDataEx_aidTestCases';


    def __init__(self):
        TestGroupData.__init__(self);
        self.aoMembers = []; # TestGroupMemberDataEx.

    def _initExtraMembersFromDb(self, oDb, tsNow = None, sPeriodBack = None):
        """
        Worker shared by the initFromDb* methods.
        Returns self.  Raises exception if no row or database error.
        """
        self.aoMembers = [];
        _ = sPeriodBack; ## @todo sPeriodBack

        if tsNow is None:
            oDb.execute('SELECT TestGroupMembers.*, TestCases.*\n'
                        'FROM   TestGroupMembers\n'
                        'LEFT OUTER JOIN TestCases ON (\n'
                        '       TestGroupMembers.idTestCase  = TestCases.idTestCase\n'
                        '   AND TestCases.tsExpire           = \'infinity\'::TIMESTAMP)\n'
                        'WHERE  TestGroupMembers.idTestGroup = %s\n'
                        '   AND TestGroupMembers.tsExpire    = \'infinity\'::TIMESTAMP\n'
                        'ORDER BY TestCases.sName, TestCases.idTestCase\n'
                        , (self.idTestGroup,));
        else:
            oDb.execute('SELECT TestGroupMembers.*, TestCases.*\n'
                        'FROM   TestGroupMembers\n'
                        'LEFT OUTER JOIN TestCases ON (\n'
                        '       TestGroupMembers.idTestCase = TestCases.idTestCase\n'
                        '   AND TestCases.tsExpire            > %s\n'
                        '   AND TestCases.tsEffective        <= %s)\n'
                        'WHERE  TestGroupMembers.idTestGroup  = %s\n'
                        '   AND TestGroupMembers.tsExpire     > %s\n'
                        '   AND TestGroupMembers.tsEffective <= %s\n'
                        'ORDER BY TestCases.sName, TestCases.idTestCase\n'
                        , (tsNow, tsNow, self.idTestGroup, tsNow, tsNow));

        for aoRow in oDb.fetchAll():
            self.aoMembers.append(TestGroupMemberDataEx().initFromDbRowEx(aoRow, oDb, tsNow));
        return self;

    def initFromDbRowEx(self, aoRow, oDb, tsNow = None, sPeriodBack = None):
        """
        Reinitialize from a SELECT * FROM TestGroups row.  Will query the
        necessary additional data from oDb using tsNow.
        Returns self.  Raises exception if no row or database error.
        """
        TestGroupData.initFromDbRow(self, aoRow);
        return self._initExtraMembersFromDb(oDb, tsNow, sPeriodBack);

    def initFromDbWithId(self, oDb, idTestGroup, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        TestGroupData.initFromDbWithId(self, oDb, idTestGroup, tsNow, sPeriodBack);
        return self._initExtraMembersFromDb(oDb, tsNow, sPeriodBack);


    def getAttributeParamNullValues(self, sAttr):
        if sAttr != 'aoMembers':
            return TestGroupData.getAttributeParamNullValues(self, sAttr);
        return ['', [], None];

    def convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict):
        if sAttr != 'aoMembers':
            return TestGroupData.convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict);

        aoNewValue  = [];
        aidSelected = oDisp.getListOfIntParams(sParam, iMin = 1, iMax = 0x7ffffffe, aiDefaults = [])
        sIds        = oDisp.getStringParam(self.ksParam_aidTestCases, sDefault = '');
        for idTestCase in sIds.split(','):
            try:    idTestCase = int(idTestCase);
            except: pass;
            oDispWrapper = self.DispWrapper(oDisp, '%s[%s][%%s]' % (TestGroupDataEx.ksParam_aoMembers, idTestCase,))
            oMember = TestGroupMemberDataEx().initFromParams(oDispWrapper, fStrict = False);
            if idTestCase in aidSelected:
                aoNewValue.append(oMember);
        return aoNewValue;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        if sAttr != 'aoMembers':
            return TestGroupData._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);

        asErrors     = [];
        aoNewMembers = [];
        for oOldMember in oValue:
            oNewMember = TestGroupMemberDataEx().initFromOther(oOldMember);
            aoNewMembers.append(oNewMember);

            dErrors = oNewMember.validateAndConvert(oDb, ModelDataBase.ksValidateFor_Other);
            if dErrors:
                asErrors.append(str(dErrors));

        if not asErrors:
            for i, idTestCase in enumerate(aoNewMembers):
                for j in range(i + 1, len(aoNewMembers)):
                    if aoNewMembers[j].idTestCase == idTestCase:
                        asErrors.append('Duplicate testcase #%d!' % (idTestCase, ));
                        break;

        return (aoNewMembers, None if not asErrors else '<br>\n'.join(asErrors));


class TestGroupLogic(ModelLogicBase):
    """
    Test case management logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;

    #
    # Standard methods.
    #

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches test groups.

        Returns an array (list) of TestGroupDataEx items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;
        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestGroups\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sName ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart,));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestGroups\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY sName ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart,));

        aoRet = [];
        for aoRow in self._oDb.fetchAll():
            aoRet.append(TestGroupDataEx().initFromDbRowEx(aoRow, self._oDb, tsNow));
        return aoRet;

    def addEntry(self, oData, uidAuthor, fCommit = False):
        """
        Adds a testgroup to the database.
        """

        #
        # Validate inputs.
        #
        assert isinstance(oData, TestGroupDataEx);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Add);
        if dErrors:
            raise TMInvalidData('addEntry invalid input: %s' % (dErrors,));
        self._assertUniq(oData, None);

        #
        # Do the job.
        #
        self._oDb.execute('INSERT INTO TestGroups (uidAuthor, sName, sDescription, sComment)\n'
                          'VALUES (%s, %s, %s, %s)\n'
                          'RETURNING idTestGroup\n'
                          , ( uidAuthor,
                              oData.sName,
                              oData.sDescription,
                              oData.sComment ));
        idTestGroup = self._oDb.fetchOne()[0];
        oData.idTestGroup = idTestGroup;

        for oMember in oData.aoMembers:
            oMember.idTestGroup = idTestGroup;
            self._insertTestGroupMember(uidAuthor, oMember)

        self._oDb.maybeCommit(fCommit);
        return True;

    def editEntry(self, oData, uidAuthor, fCommit = False):
        """
        Modifies a test group.
        """

        #
        # Validate inputs and read in the old(/current) data.
        #
        assert isinstance(oData, TestGroupDataEx);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Edit);
        if dErrors:
            raise TMInvalidData('editEntry invalid input: %s' % (dErrors,));
        self._assertUniq(oData, oData.idTestGroup);

        oOldData = TestGroupDataEx().initFromDbWithId(self._oDb, oData.idTestGroup);

        #
        # Update the data that needs updating.
        #

        if not oData.isEqualEx(oOldData, [ 'aoMembers', 'tsEffective', 'tsExpire', 'uidAuthor', ]):
            self._historizeTestGroup(oData.idTestGroup);
            self._oDb.execute('INSERT INTO TestGroups\n'
                              '       (uidAuthor, idTestGroup, sName, sDescription, sComment)\n'
                              'VALUES (%s, %s, %s, %s, %s)\n'
                              , ( uidAuthor,
                                  oData.idTestGroup,
                                  oData.sName,
                                  oData.sDescription,
                                  oData.sComment ));

        # Create a lookup dictionary for old entries.
        dOld = {};
        for oOld in oOldData.aoMembers:
            dOld[oOld.idTestCase] = oOld;
        assert len(dOld) == len(oOldData.aoMembers);

        # Add new members, updated existing ones.
        dNew = {};
        for oNewMember in oData.aoMembers:
            oNewMember.idTestGroup = oData.idTestGroup;
            if oNewMember.idTestCase in dNew:
                raise TMRowAlreadyExists('Duplicate test group member: idTestCase=%d (%s / %s)'
                                         % (oNewMember.idTestCase, oNewMember, dNew[oNewMember.idTestCase],));
            dNew[oNewMember.idTestCase] = oNewMember;

            oOldMember = dOld.get(oNewMember.idTestCase, None);
            if oOldMember is not None:
                if oNewMember.isEqualEx(oOldMember, [ 'uidAuthor', 'tsEffective', 'tsExpire' ]):
                    continue; # Skip, nothing changed.
                self._historizeTestGroupMember(oData.idTestGroup, oNewMember.idTestCase);
            self._insertTestGroupMember(uidAuthor, oNewMember);

        # Expire members that have been removed.
        sQuery = self._oDb.formatBindArgs('UPDATE TestGroupMembers\n'
                                          'SET    tsExpire    = CURRENT_TIMESTAMP\n'
                                          'WHERE  idTestGroup = %s\n'
                                          '   AND tsExpire    = \'infinity\'::TIMESTAMP\n'
                                          , ( oData.idTestGroup, ));
        if dNew:
            sQuery += '   AND idTestCase NOT IN (%s)' % (', '.join([str(iKey) for iKey in dNew]),);
        self._oDb.execute(sQuery);

        self._oDb.maybeCommit(fCommit);
        return True;

    def removeEntry(self, uidAuthor, idTestGroup, fCascade = False, fCommit = False):
        """
        Deletes a test group.
        """
        _ = uidAuthor; ## @todo record uidAuthor.

        #
        # Cascade.
        #
        if fCascade is not True:
            self._oDb.execute('SELECT   SchedGroups.idSchedGroup, SchedGroups.sName\n'
                              'FROM     SchedGroupMembers, SchedGroups\n'
                              'WHERE    SchedGroupMembers.idTestGroup = %s\n'
                              '     AND SchedGroupMembers.tsExpire    = \'infinity\'::TIMESTAMP\n'
                              '     AND SchedGroups.idSchedGroup      = SchedGroupMembers.idSchedGroup\n'
                              '     AND SchedGroups.tsExpire          = \'infinity\'::TIMESTAMP\n'
                              , ( idTestGroup, ));
            aoGroups = self._oDb.fetchAll();
            if aoGroups:
                asGroups = ['%s (#%d)' % (sName, idSchedGroup) for idSchedGroup, sName in aoGroups];
                raise TMRowInUse('Test group #%d is member of one or more scheduling groups: %s'
                                 % (idTestGroup, ', '.join(asGroups),));
        else:
            self._oDb.execute('UPDATE   SchedGroupMembers\n'
                              'SET      tsExpire = CURRENT_TIMESTAMP\n'
                              'WHERE    idTestGroup = %s\n'
                              '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                              , ( idTestGroup, ));

        #
        # Remove the group.
        #
        self._oDb.execute('UPDATE   TestGroupMembers\n'
                          'SET      tsExpire    = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestGroup = %s\n'
                          '     AND tsExpire    = \'infinity\'::TIMESTAMP\n'
                          , (idTestGroup,))
        self._oDb.execute('UPDATE   TestGroups\n'
                          'SET      tsExpire    = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestGroup = %s\n'
                          '     AND tsExpire    = \'infinity\'::TIMESTAMP\n'
                          , (idTestGroup,))

        self._oDb.maybeCommit(fCommit)
        return True;

    def cachedLookup(self, idTestGroup):
        """
        Looks up the most recent TestGroupDataEx object for idTestGroup
        via an object cache.

        Returns a shared TestGroupDataEx object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('TestGroupDataEx');
        oEntry = self.dCache.get(idTestGroup, None);
        if oEntry is None:
            fNeedTsNow = False;
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestGroups\n'
                              'WHERE    idTestGroup = %s\n'
                              '     AND tsExpire    = \'infinity\'::TIMESTAMP\n'
                              , (idTestGroup, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     TestGroups\n'
                                  'WHERE    idTestGroup = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idTestGroup, ));
                fNeedTsNow = True;
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idTestGroup));

            if self._oDb.getRowCount() == 1:
                aaoRow = self._oDb.fetchOne();
                oEntry = TestGroupDataEx();
                tsNow  = oEntry.initFromDbRow(aaoRow).tsEffective if fNeedTsNow else None;
                oEntry.initFromDbRowEx(aaoRow, self._oDb, tsNow);
                self.dCache[idTestGroup] = oEntry;
        return oEntry;


    #
    # Other methods.
    #

    def fetchOrderedByName(self, tsNow = None):
        """
        Return list of objects of type TestGroupData ordered by name.
        May raise exception on database error.
        """
        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestGroups\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sName ASC\n');
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestGroups\n'
                              'WHERE    tsExpire     > %s\n'
                              '   AND   tsEffective <= %s\n'
                              'ORDER BY sName ASC\n'
                              , (tsNow, tsNow,));
        aoRet = []
        for _ in range(self._oDb.getRowCount()):
            aoRet.append(TestGroupData().initFromDbRow(self._oDb.fetchOne()));
        return aoRet;

    def getMembers(self, idTestGroup):
        """
        Fetches all test case records from DB which are
        belong to current Test Group.
        Returns list of objects of type TestGroupMemberData2 (!).
        """
        self._oDb.execute('SELECT TestCases.*,\n'
                          '       TestGroupMembers.idTestGroup,\n'
                          '       TestGroupMembers.aidTestCaseArgs\n'
                          'FROM   TestCases, TestGroupMembers\n'
                          'WHERE  TestCases.tsExpire = \'infinity\'::TIMESTAMP\n'
                          '   AND TestGroupMembers.tsExpire = \'infinity\'::TIMESTAMP\n'
                          '   AND TestGroupMembers.idTestCase = TestCases.idTestCase\n'
                          '   AND TestGroupMembers.idTestGroup = %s\n'
                          'ORDER BY TestCases.idTestCase ASC;',
                          (idTestGroup,))

        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            aoRet.append(TestGroupMemberData2().initFromDbRowEx(aoRow))

        return aoRet

    def getAll(self, tsNow=None):
        """Return list of objects of type TestGroupData"""

        if tsNow is None:
            self._oDb.execute('SELECT *\n'
                              'FROM   TestGroups\n'
                              'WHERE  tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY idTestGroup ASC;')
        else:
            self._oDb.execute('SELECT *\n'
                              'FROM   TestGroups\n'
                              'WHERE  tsExpire     > %s\n'
                              '   AND tsEffective <= %s\n'
                              'ORDER BY idTestGroup ASC;',
                              (tsNow, tsNow))

        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            aoRet.append(TestGroupData().initFromDbRow(aoRow))

        return aoRet

    def getById(self, idTestGroup, tsNow=None):
        """Get Test Group data by its ID"""

        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestGroups\n'
                              'WHERE    tsExpire     = \'infinity\'::timestamp\n'
                              '  AND    idTestGroup  = %s\n'
                              'ORDER BY idTestGroup ASC;'
                              , (idTestGroup,))
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestGroups\n'
                              'WHERE    tsExpire     > %s\n'
                              '  AND    tsEffective <= %s\n'
                              '  AND    idTestGroup  = %s\n'
                              'ORDER BY idTestGroup ASC;'
                              , (tsNow, tsNow, idTestGroup))

        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise TMTooManyRows('Found more than one test groups with the same credentials. Database structure is corrupted.')
        try:
            return TestGroupData().initFromDbRow(aRows[0])
        except IndexError:
            return None

    #
    # Helpers.
    #

    def _assertUniq(self, oData, idTestGroupIgnore):
        """ Checks that the test group name is unique, raises exception if it isn't. """
        self._oDb.execute('SELECT idTestGroup\n'
                          'FROM   TestGroups\n'
                          'WHERE  sName    = %s\n'
                          '   AND tsExpire = \'infinity\'::TIMESTAMP\n'
                          + ('' if idTestGroupIgnore is None else  '   AND idTestGroup <> %d\n' % (idTestGroupIgnore,))
                          , ( oData.sName, ))
        if self._oDb.getRowCount() > 0:
            raise TMRowAlreadyExists('A Test group with name "%s" already exist.' % (oData.sName,));
        return True;

    def _historizeTestGroup(self, idTestGroup):
        """ Historize Test Group record. """
        self._oDb.execute('UPDATE   TestGroups\n'
                          'SET      tsExpire    = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestGroup = %s\n'
                          '     AND tsExpire    = \'infinity\'::TIMESTAMP\n'
                          , ( idTestGroup, ));
        return True;

    def _historizeTestGroupMember(self, idTestGroup, idTestCase):
        """ Historize Test Group Member record. """
        self._oDb.execute('UPDATE   TestGroupMembers\n'
                          'SET      tsExpire    = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestGroup = %s\n'
                          '     AND idTestCase  = %s\n'
                          '     AND tsExpire    = \'infinity\'::timestamp\n'
                          , (idTestGroup, idTestCase,));
        return True;

    def _insertTestGroupMember(self, uidAuthor, oMember):
        """ Inserts a test group member. """
        self._oDb.execute('INSERT INTO TestGroupMembers\n'
                          '       (uidAuthor, idTestGroup, idTestCase, iSchedPriority, aidTestCaseArgs)\n'
                          'VALUES (%s, %s, %s, %s, %s)\n'
                          , ( uidAuthor,
                              oMember.idTestGroup,
                              oMember.idTestCase,
                              oMember.iSchedPriority,
                              oMember.aidTestCaseArgs, ));
        return True;



#
# Unit testing.
#

# pylint: disable=missing-docstring
class TestGroupMemberDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestGroupMemberData(),];

class TestGroupDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestGroupData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

