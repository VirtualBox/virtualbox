# -*- coding: utf-8 -*-
# $Id: testresultfailures.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

## @todo Rename this file to testresult.py!

"""
Test Manager - Test result failures.
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
import sys;
import unittest;

# Validation Kit imports.
from testmanager.core.base          import ModelDataBase, ModelLogicBase, ModelDataBaseTestCase, TMInvalidData, TMRowNotFound, \
                                           TMRowAlreadyExists, ChangeLogEntry, AttributeChangeEntry;
from testmanager.core.failurereason import FailureReasonData;
from testmanager.core.useraccount   import UserAccountLogic;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name


class TestResultFailureData(ModelDataBase):
    """
    Test result failure reason data.
    """

    ksIdAttr                    = 'idTestResult';
    kfIdAttrIsForForeign        = True; # Modifies the 'add' validation.

    ksParam_idTestResult        = 'TestResultFailure_idTestResult';
    ksParam_tsEffective         = 'TestResultFailure_tsEffective';
    ksParam_tsExpire            = 'TestResultFailure_tsExpire';
    ksParam_uidAuthor           = 'TestResultFailure_uidAuthor';
    ksParam_idTestSet           = 'TestResultFailure_idTestSet';
    ksParam_idFailureReason     = 'TestResultFailure_idFailureReason';
    ksParam_sComment            = 'TestResultFailure_sComment';

    kasAllowNullAttributes      = ['tsEffective', 'tsExpire', 'uidAuthor', 'sComment', 'idTestSet' ];

    kcDbColumns                 = 7;

    def __init__(self):
        ModelDataBase.__init__(self)
        self.idTestResult       = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;
        self.idTestSet          = None;
        self.idFailureReason    = None;
        self.sComment           = None;

    def initFromDbRow(self, aoRow):
        """
        Reinitialize from a SELECT * FROM TestResultFailures.
        Return self. Raises exception if no row.
        """
        if aoRow is None:
            raise TMRowNotFound('Test result file record not found.')

        self.idTestResult       = aoRow[0];
        self.tsEffective        = aoRow[1];
        self.tsExpire           = aoRow[2];
        self.uidAuthor          = aoRow[3];
        self.idTestSet          = aoRow[4];
        self.idFailureReason    = aoRow[5];
        self.sComment           = aoRow[6];
        return self;

    def initFromDbWithId(self, oDb, idTestResult, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   TestResultFailures\n'
                                                       'WHERE  idTestResult = %s\n'
                                                       , ( idTestResult,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idTestResult=%s not found (tsNow=%s, sPeriodBack=%s)' % (idTestResult, tsNow, sPeriodBack));
        assert len(aoRow) == self.kcDbColumns;
        return self.initFromDbRow(aoRow);

    def initFromValues(self, idTestResult, idFailureReason, uidAuthor,
                       tsExpire = None, tsEffective = None, idTestSet = None, sComment = None):
        """
        Initialize from values.
        """
        self.idTestResult       = idTestResult;
        self.tsEffective        = tsEffective;
        self.tsExpire           = tsExpire;
        self.uidAuthor          = uidAuthor;
        self.idTestSet          = idTestSet;
        self.idFailureReason    = idFailureReason;
        self.sComment           = sComment;
        return self;



class TestResultFailureDataEx(TestResultFailureData):
    """
    Extends TestResultFailureData by resolving reasons and user.
    """

    def __init__(self):
        TestResultFailureData.__init__(self);
        self.oFailureReason     = None;
        self.oAuthor            = None;

    def initFromDbRowEx(self, aoRow, oFailureReasonLogic, oUserAccountLogic):
        """
        Reinitialize from a SELECT * FROM TestResultFailures.
        Return self. Raises exception if no row.
        """
        self.initFromDbRow(aoRow);
        self.oFailureReason = oFailureReasonLogic.cachedLookup(self.idFailureReason);
        self.oAuthor        = oUserAccountLogic.cachedLookup(self.uidAuthor);
        return self;


class TestResultListingData(ModelDataBase): # pylint: disable=too-many-instance-attributes
    """
    Test case result data representation for table listing
    """

    def __init__(self):
        """Initialize"""
        ModelDataBase.__init__(self)

        self.idTestSet               = None

        self.idBuildCategory         = None;
        self.sProduct                = None
        self.sRepository             = None;
        self.sBranch                 = None
        self.sType                   = None
        self.idBuild                 = None;
        self.sVersion                = None;
        self.iRevision               = None

        self.sOs                     = None;
        self.sOsVersion              = None;
        self.sArch                   = None;
        self.sCpuVendor              = None;
        self.sCpuName                = None;
        self.cCpus                   = None;
        self.fCpuHwVirt              = None;
        self.fCpuNestedPaging        = None;
        self.fCpu64BitGuest          = None;
        self.idTestBox               = None
        self.sTestBoxName            = None

        self.tsCreated               = None
        self.tsElapsed               = None
        self.enmStatus               = None
        self.cErrors                 = None;

        self.idTestCase              = None
        self.sTestCaseName           = None
        self.sBaseCmd                = None
        self.sArgs                   = None
        self.sSubName                = None;

        self.idBuildTestSuite        = None;
        self.iRevisionTestSuite      = None;

        self.oFailureReason          = None;
        self.oFailureReasonAssigner  = None;
        self.tsFailureReasonAssigned = None;
        self.sFailureReasonComment   = None;

    def initFromDbRowEx(self, aoRow, oFailureReasonLogic, oUserAccountLogic):
        """
        Reinitialize from a database query.
        Return self. Raises exception if no row.
        """
        if aoRow is None:
            raise TMRowNotFound('Test result record not found.')

        self.idTestSet               = aoRow[0];

        self.idBuildCategory         = aoRow[1];
        self.sProduct                = aoRow[2];
        self.sRepository             = aoRow[3];
        self.sBranch                 = aoRow[4];
        self.sType                   = aoRow[5];
        self.idBuild                 = aoRow[6];
        self.sVersion                = aoRow[7];
        self.iRevision               = aoRow[8];

        self.sOs                     = aoRow[9];
        self.sOsVersion              = aoRow[10];
        self.sArch                   = aoRow[11];
        self.sCpuVendor              = aoRow[12];
        self.sCpuName                = aoRow[13];
        self.cCpus                   = aoRow[14];
        self.fCpuHwVirt              = aoRow[15];
        self.fCpuNestedPaging        = aoRow[16];
        self.fCpu64BitGuest          = aoRow[17];
        self.idTestBox               = aoRow[18];
        self.sTestBoxName            = aoRow[19];

        self.tsCreated               = aoRow[20];
        self.tsElapsed               = aoRow[21];
        self.enmStatus               = aoRow[22];
        self.cErrors                 = aoRow[23];

        self.idTestCase              = aoRow[24];
        self.sTestCaseName           = aoRow[25];
        self.sBaseCmd                = aoRow[26];
        self.sArgs                   = aoRow[27];
        self.sSubName                = aoRow[28];

        self.idBuildTestSuite        = aoRow[29];
        self.iRevisionTestSuite      = aoRow[30];

        self.oFailureReason          = None;
        if aoRow[31] is not None:
            self.oFailureReason = oFailureReasonLogic.cachedLookup(aoRow[31]);
        self.oFailureReasonAssigner  = None;
        if aoRow[32] is not None:
            self.oFailureReasonAssigner = oUserAccountLogic.cachedLookup(aoRow[32]);
        self.tsFailureReasonAssigned = aoRow[33];
        self.sFailureReasonComment   = aoRow[34];

        return self



class TestResultFailureLogic(ModelLogicBase): # pylint: disable=too-few-public-methods
    """
    Test result failure reason logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)

    def fetchForChangeLog(self, idTestResult, iStart, cMaxRows, tsNow): # pylint: disable=too-many-locals
        """
        Fetches change log entries for a failure reason.

        Returns an array of ChangeLogEntry instance and an indicator whether
        there are more entries.
        Raises exception on error.
        """

        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();

        # 1. Get a list of the changes from both TestResultFailures and assoicated
        #    FailureReasons.  The latter is useful since the failure reason
        #    description may evolve along side the invidiual failure analysis.
        self._oDb.execute('( SELECT trf.tsEffective AS tsEffectiveChangeLog,\n'
                          '         trf.uidAuthor   AS uidAuthorChangeLog,\n'
                          '         trf.*,\n'
                          '         fr.*\n'
                          '  FROM   TestResultFailures trf,\n'
                          '         FailureReasons fr\n'
                          '  WHERE  trf.idTestResult = %s\n'
                          '     AND trf.tsEffective <= %s\n'
                          '     AND trf.idFailureReason = fr.idFailureReason\n'
                          '     AND fr.tsEffective      <= trf.tsEffective\n'
                          '     AND fr.tsExpire         >  trf.tsEffective\n'
                          ')\n'
                          'UNION\n'
                          '( SELECT fr.tsEffective AS tsEffectiveChangeLog,\n'
                          '         fr.uidAuthor   AS uidAuthorChangeLog,\n'
                          '         trf.*,\n'
                          '         fr.*\n'
                          '  FROM   TestResultFailures trf,\n'
                          '         FailureReasons fr\n'
                          '  WHERE  trf.idTestResult    = %s\n'
                          '     AND trf.tsEffective    <= %s\n'
                          '     AND trf.idFailureReason = fr.idFailureReason\n'
                          '     AND fr.tsEffective      > trf.tsEffective\n'
                          '     AND fr.tsEffective      < trf.tsExpire\n'
                          ')\n'
                          'ORDER BY tsEffectiveChangeLog DESC\n'
                          'LIMIT %s OFFSET %s\n'
                          , ( idTestResult, tsNow, idTestResult, tsNow, cMaxRows + 1, iStart, ));

        aaoRows = [];
        for aoChange in self._oDb.fetchAll():
            oTrf = TestResultFailureDataEx().initFromDbRow(aoChange[2:]);
            oFr  = FailureReasonData().initFromDbRow(aoChange[(2+TestResultFailureData.kcDbColumns):]);
            oTrf.oFailureReason = oFr;
            aaoRows.append([aoChange[0], aoChange[1], oTrf, oFr]);

        # 2. Calculate the changes.
        oFailureCategoryLogic = None;
        aoEntries = [];
        for i in xrange(0, len(aaoRows) - 1):
            aoNew = aaoRows[i];
            aoOld = aaoRows[i + 1];

            aoChanges = [];
            oNew = aoNew[2];
            oOld = aoOld[2];
            for sAttr in oNew.getDataAttributes():
                if sAttr not in [ 'tsEffective', 'tsExpire', 'uidAuthor', 'oFailureReason', 'oAuthor' ]:
                    oOldAttr = getattr(oOld, sAttr);
                    oNewAttr = getattr(oNew, sAttr);
                    if oOldAttr != oNewAttr:
                        if sAttr == 'idFailureReason':
                            oNewAttr = '%s (%s)' % (oNewAttr, oNew.oFailureReason.sShort, );
                            oOldAttr = '%s (%s)' % (oOldAttr, oOld.oFailureReason.sShort, );
                        aoChanges.append(AttributeChangeEntry(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));
            if oOld.idFailureReason == oNew.idFailureReason:
                oNew = aoNew[3];
                oOld = aoOld[3];
                for sAttr in oNew.getDataAttributes():
                    if sAttr not in [ 'tsEffective', 'tsExpire', 'uidAuthor', ]:
                        oOldAttr = getattr(oOld, sAttr);
                        oNewAttr = getattr(oNew, sAttr);
                        if oOldAttr != oNewAttr:
                            if sAttr == 'idFailureCategory':
                                if oFailureCategoryLogic is None:
                                    from testmanager.core.failurecategory import FailureCategoryLogic;
                                    oFailureCategoryLogic = FailureCategoryLogic(self._oDb);
                                oCat = oFailureCategoryLogic.cachedLookup(oNewAttr);
                                if oCat is not None:
                                    oNewAttr = '%s (%s)' % (oNewAttr, oCat.sShort, );
                                oCat = oFailureCategoryLogic.cachedLookup(oOldAttr);
                                if oCat is not None:
                                    oOldAttr = '%s (%s)' % (oOldAttr, oCat.sShort, );
                            aoChanges.append(AttributeChangeEntry(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));


            tsExpire    = aaoRows[i - 1][0] if i > 0 else aoNew[2].tsExpire;
            aoEntries.append(ChangeLogEntry(aoNew[1], None, aoNew[0], tsExpire, aoNew[2], aoOld[2], aoChanges));

        # If we're at the end of the log, add the initial entry.
        if len(aaoRows) <= cMaxRows and aaoRows:
            aoNew    = aaoRows[-1];
            tsExpire = aaoRows[-1 - 1][0] if len(aaoRows) > 1 else aoNew[2].tsExpire;
            aoEntries.append(ChangeLogEntry(aoNew[1], None, aoNew[0], tsExpire, aoNew[2], None, []));

        return (UserAccountLogic(self._oDb).resolveChangeLogAuthors(aoEntries), len(aaoRows) > cMaxRows);


    def getById(self, idTestResult):
        """Get Test result failure reason data by idTestResult"""

        self._oDb.execute('SELECT   *\n'
                          'FROM     TestResultFailures\n'
                          'WHERE    tsExpire   = \'infinity\'::timestamp\n'
                          '  AND    idTestResult = %s;', (idTestResult,))
        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise self._oDb.integrityException(
                'Found more than one failure reasons with the same credentials. Database structure is corrupted.')
        try:
            return TestResultFailureData().initFromDbRow(aRows[0])
        except IndexError:
            return None

    def addEntry(self, oData, uidAuthor, fCommit = False):
        """
        Add a test result failure reason record.
        """

        #
        # Validate inputs and read in the old(/current) data.
        #
        assert isinstance(oData, TestResultFailureData);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_AddForeignId);
        if dErrors:
            raise TMInvalidData('editEntry invalid input: %s' % (dErrors,));

        # Check if it exist first (we're adding, not editing, collisions not allowed).
        oOldData = self.getById(oData.idTestResult);
        if oOldData is not None:
            raise TMRowAlreadyExists('TestResult %d already have a failure reason associated with it:'
                                     '%s\n'
                                     'Perhaps someone else beat you to it? Or did you try resubmit?'
                                     % (oData.idTestResult, oOldData));
        oData = self._resolveSetTestIdIfMissing(oData);

        #
        # Add record.
        #
        self._readdEntry(uidAuthor, oData);
        self._oDb.maybeCommit(fCommit);
        return True;

    def editEntry(self, oData, uidAuthor, fCommit = False):
        """
        Modifies a test result failure reason.
        """

        #
        # Validate inputs and read in the old(/current) data.
        #
        assert isinstance(oData, TestResultFailureData);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Edit);
        if dErrors:
            raise TMInvalidData('editEntry invalid input: %s' % (dErrors,));

        oOldData = self.getById(oData.idTestResult)
        oData.idTestSet = oOldData.idTestSet;

        #
        # Update the data that needs updating.
        #
        if not oData.isEqualEx(oOldData, [ 'tsEffective', 'tsExpire', 'uidAuthor', ]):
            self._historizeEntry(oData.idTestResult);
            self._readdEntry(uidAuthor, oData);
        self._oDb.maybeCommit(fCommit);
        return True;


    def removeEntry(self, uidAuthor, idTestResult, fCascade = False, fCommit = False):
        """
        Deletes a test result failure reason.
        """
        _ = fCascade; # Not applicable.

        oData = self.getById(idTestResult)
        (tsCur, tsCurMinusOne) = self._oDb.getCurrentTimestamps();
        if oData.tsEffective not in (tsCur, tsCurMinusOne):
            self._historizeEntry(idTestResult, tsCurMinusOne);
            self._readdEntry(uidAuthor, oData, tsCurMinusOne);
            self._historizeEntry(idTestResult);
        self._oDb.execute('UPDATE   TestResultFailures\n'
                          'SET      tsExpire       = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestResult   = %s\n'
                          '     AND tsExpire       = \'infinity\'::TIMESTAMP\n'
                          , (idTestResult,));
        self._oDb.maybeCommit(fCommit);
        return True;

    #
    # Helpers.
    #

    def _readdEntry(self, uidAuthor, oData, tsEffective = None):
        """
        Re-adds the TestResultFailure entry. Used by addEntry, editEntry and removeEntry.
        """
        if tsEffective is None:
            tsEffective = self._oDb.getCurrentTimestamp();
        self._oDb.execute('INSERT INTO TestResultFailures (\n'
                          '         uidAuthor,\n'
                          '         tsEffective,\n'
                          '         idTestResult,\n'
                          '         idTestSet,\n'
                          '         idFailureReason,\n'
                          '         sComment)\n'
                          'VALUES (%s, %s, %s, %s, %s, %s)\n'
                          , ( uidAuthor,
                              tsEffective,
                              oData.idTestResult,
                              oData.idTestSet,
                              oData.idFailureReason,
                              oData.sComment,) );
        return True;


    def _historizeEntry(self, idTestResult, tsExpire = None):
        """ Historizes the current entry. """
        if tsExpire is None:
            tsExpire = self._oDb.getCurrentTimestamp();
        self._oDb.execute('UPDATE TestResultFailures\n'
                          'SET    tsExpire   = %s\n'
                          'WHERE  idTestResult = %s\n'
                          '   AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                          , (tsExpire, idTestResult,));
        return True;


    def _resolveSetTestIdIfMissing(self, oData):
        """ Resolve any missing idTestSet reference (it's a duplicate for speed efficiency). """
        if oData.idTestSet is None and oData.idTestResult is not None:
            self._oDb.execute('SELECT idTestSet FROM TestResults WHERE idTestResult = %s', (oData.idTestResult,));
            oData.idTestSet = self._oDb.fetchOne()[0];
        return oData;



#
# Unit testing.
#

# pylint: disable=missing-docstring
class TestResultFailureDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestResultFailureData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

