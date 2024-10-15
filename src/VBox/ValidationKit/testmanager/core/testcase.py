# -*- coding: utf-8 -*-
# $Id: testcase.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
Test Manager - Test Case.
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
import copy;
import sys;
import unittest;

# Validation Kit imports.
from common                             import utils;
from testmanager.core.base              import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase, TMExceptionBase, \
                                               TMInvalidData, TMRowNotFound, ChangeLogEntry, AttributeChangeEntry;
from testmanager.core.globalresource    import GlobalResourceData;
from testmanager.core.useraccount       import UserAccountLogic;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name



class TestCaseGlobalRsrcDepData(ModelDataBase):
    """
    Test case dependency on a global resource - data.
    """

    ksParam_idTestCase          = 'TestCaseDependency_idTestCase';
    ksParam_idGlobalRsrc        = 'TestCaseDependency_idGlobalRsrc';
    ksParam_tsEffective         = 'TestCaseDependency_tsEffective';
    ksParam_tsExpire            = 'TestCaseDependency_tsExpire';
    ksParam_uidAuthor           = 'TestCaseDependency_uidAuthor';

    kasAllowNullAttributes      = ['idTestSet', ];

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestCase         = None;
        self.idGlobalRsrc       = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;

    def initFromDbRow(self, aoRow):
        """
        Reinitialize from a SELECT * FROM TestCaseDeps row.
        """
        if aoRow is None:
            raise TMRowNotFound('Test case not found.');

        self.idTestCase         = aoRow[0];
        self.idGlobalRsrc       = aoRow[1];
        self.tsEffective        = aoRow[2];
        self.tsExpire           = aoRow[3];
        self.uidAuthor          = aoRow[4];
        return self;


class TestCaseGlobalRsrcDepLogic(ModelLogicBase):
    """
    Test case dependency on a global resources - logic.
    """

    def getTestCaseDeps(self, idTestCase, tsNow = None):
        """
        Returns an array of (TestCaseGlobalRsrcDepData, GlobalResourceData)
        with the global resources required by idTestCase.
        Returns empty array if none found. Raises exception on database error.

        Note! Maybe a bit overkill...
        """
        ## @todo This code isn't entirely kosher... Should use a DataEx with a oGlobalRsrc = GlobalResourceData().
        if tsNow is not None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCaseGlobalRsrcDeps, GlobalResources\n'
                              'WHERE    TestCaseGlobalRsrcDeps.idTestCase   = %s\n'
                              '     AND TestCaseGlobalRsrcDeps.tsExpire     > %s\n'
                              '     AND TestCaseGlobalRsrcDeps.tsEffective <= %s\n'
                              '     AND GlobalResources.idGlobalRsrc        = TestCaseGlobalRsrcDeps.idGlobalRsrc\n'
                              '     AND GlobalResources.tsExpire            > %s\n'
                              '     AND GlobalResources.tsEffective        <= %s\n'
                              , (idTestCase, tsNow, tsNow, tsNow, tsNow) );
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCaseGlobalRsrcDeps, GlobalResources\n'
                              'WHERE    TestCaseGlobalRsrcDeps.idTestCase   =  %s\n'
                              '     AND GlobalResources.idGlobalRsrc        = TestCaseGlobalRsrcDeps.idGlobalRsrc\n'
                              '     AND TestCaseGlobalRsrcDeps.tsExpire     = \'infinity\'::TIMESTAMP\n'
                              '     AND GlobalResources.tsExpire            = \'infinity\'::TIMESTAMP\n'
                              , (idTestCase,))
        aaoRows = self._oDb.fetchAll();
        aoRet = []
        for aoRow in aaoRows:
            oItem = [TestCaseDependencyData().initFromDbRow(aoRow),
                     GlobalResourceData().initFromDbRow(aoRow[5:])];
            aoRet.append(oItem);

        return aoRet

    def getTestCaseDepsIds(self, idTestCase, tsNow = None):
        """
        Returns an array of global resources that idTestCase require.
        Returns empty array if none found. Raises exception on database error.
        """
        if tsNow is not None:
            self._oDb.execute('SELECT   idGlobalRsrc\n'
                              'FROM     TestCaseGlobalRsrcDeps\n'
                              'WHERE    TestCaseGlobalRsrcDeps.idTestCase   = %s\n'
                              '     AND TestCaseGlobalRsrcDeps.tsExpire     > %s\n'
                              '     AND TestCaseGlobalRsrcDeps.tsEffective <= %s\n'
                              , (idTestCase, tsNow, tsNow, ) );
        else:
            self._oDb.execute('SELECT   idGlobalRsrc\n'
                              'FROM     TestCaseGlobalRsrcDeps\n'
                              'WHERE    TestCaseGlobalRsrcDeps.idTestCase   =  %s\n'
                              '     AND TestCaseGlobalRsrcDeps.tsExpire     = \'infinity\'::TIMESTAMP\n'
                              , (idTestCase,))
        aidGlobalRsrcs = []
        for aoRow in self._oDb.fetchAll():
            aidGlobalRsrcs.append(aoRow[0]);
        return aidGlobalRsrcs;


    def getDepGlobalResourceData(self, idTestCase, tsNow = None):
        """
        Returns an array of objects of type GlobalResourceData on which the
        specified test case depends on.
        """
        if tsNow is None :
            self._oDb.execute('SELECT   GlobalResources.*\n'
                              'FROM     TestCaseGlobalRsrcDeps, GlobalResources\n'
                              'WHERE    TestCaseGlobalRsrcDeps.idTestCase  =  %s\n'
                              '     AND GlobalResources.idGlobalRsrc       = TestCaseGlobalRsrcDeps.idGlobalRsrc\n'
                              '     AND TestCaseGlobalRsrcDeps.tsExpire    = \'infinity\'::TIMESTAMP\n'
                              '     AND GlobalResources.tsExpire           = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY GlobalResources.idGlobalRsrc\n'
                              , (idTestCase,))
        else:
            self._oDb.execute('SELECT   GlobalResources.*\n'
                              'FROM     TestCaseGlobalRsrcDeps, GlobalResources\n'
                              'WHERE    TestCaseGlobalRsrcDeps.idTestCase  =  %s\n'
                              '     AND GlobalResources.idGlobalRsrc       = TestCaseGlobalRsrcDeps.idGlobalRsrc\n'
                              '     AND TestCaseGlobalRsrcDeps.tsExpire    > %s\n'
                              '     AND TestCaseGlobalRsrcDeps.tsExpire   <= %s\n'
                              '     AND GlobalResources.tsExpire           > %s\n'
                              '     AND GlobalResources.tsEffective       <= %s\n'
                              'ORDER BY GlobalResources.idGlobalRsrc\n'
                              , (idTestCase, tsNow, tsNow, tsNow, tsNow));

        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            aoRet.append(GlobalResourceData().initFromDbRow(aoRow));

        return aoRet


class TestCaseDependencyData(ModelDataBase):
    """
    Test case dependency data
    """

    ksParam_idTestCase          = 'TestCaseDependency_idTestCase';
    ksParam_idTestCasePreReq    = 'TestCaseDependency_idTestCasePreReq';
    ksParam_tsEffective         = 'TestCaseDependency_tsEffective';
    ksParam_tsExpire            = 'TestCaseDependency_tsExpire';
    ksParam_uidAuthor           = 'TestCaseDependency_uidAuthor';


    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestCase         = None;
        self.idTestCasePreReq   = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;

    def initFromDbRow(self, aoRow):
        """
        Reinitialize from a SELECT * FROM TestCaseDeps row.
        """
        if aoRow is None:
            raise TMRowNotFound('Test case not found.');

        self.idTestCase         = aoRow[0];
        self.idTestCasePreReq   = aoRow[1];
        self.tsEffective        = aoRow[2];
        self.tsExpire           = aoRow[3];
        self.uidAuthor          = aoRow[4];
        return self;

    def initFromParams(self, oDisp, fStrict=True):
        """
        Initialize the object from parameters.
        The input is not validated at all, except that all parameters must be
        present when fStrict is True.
        Note! Returns parameter NULL values, not database ones.
        """

        self.convertToParamNull();
        fn = oDisp.getStringParam; # Shorter...

        self.idTestCase         = fn(self.ksParam_idTestCase,       None, None  if fStrict else self.idTestCase);
        self.idTestCasePreReq   = fn(self.ksParam_idTestCasePreReq, None, None  if fStrict else self.idTestCasePreReq);
        self.tsEffective        = fn(self.ksParam_tsEffective,      None, None  if fStrict else self.tsEffective);
        self.tsExpire           = fn(self.ksParam_tsExpire,         None, None  if fStrict else self.tsExpire);
        self.uidAuthor          = fn(self.ksParam_uidAuthor,        None, None  if fStrict else self.uidAuthor);

        return True

    def validateAndConvert(self, oDb = None, enmValidateFor = ModelDataBase.ksValidateFor_Other):
        """
        Validates the input and converts valid fields to their right type.
        Returns a dictionary with per field reports, only invalid fields will
        be returned, so an empty dictionary means that the data is valid.

        The dictionary keys are ksParam_*.
        """
        dErrors = {}

        self.idTestCase         = self._validateInt(   dErrors, self.ksParam_idTestCase,        self.idTestCase);
        self.idTestCasePreReq   = self._validateInt(   dErrors, self.ksParam_idTestCasePreReq,  self.idTestCasePreReq);
        self.tsEffective        = self._validateTs(    dErrors, self.ksParam_tsEffective,       self.tsEffective);
        self.tsExpire           = self._validateTs(    dErrors, self.ksParam_tsExpire,          self.tsExpire);
        self.uidAuthor          = self._validateInt(   dErrors, self.ksParam_uidAuthor,         self.uidAuthor);

        _ = oDb;
        _ = enmValidateFor;
        return dErrors

    def convertFromParamNull(self):
        """
        Converts from parameter NULL values to database NULL values (None).
        """
        if self.idTestCase        in [-1, '']: self.idTestCase          = None;
        if self.idTestCasePreReq  in [-1, '']: self.idTestCasePreReq    = None;
        if self.tsEffective             == '': self.tsEffective         = None;
        if self.tsExpire                == '': self.tsExpire            = None;
        if self.uidAuthor         in [-1, '']: self.uidAuthor           = None;
        return True;

    def convertToParamNull(self):
        """
        Converts from database NULL values (None) to special values we can
        pass thru parameters list.
        """
        if self.idTestCase            is None: self.idTestCase          = -1;
        if self.idTestCasePreReq      is None: self.idTestCasePreReq    = -1;
        if self.tsEffective           is None: self.tsEffective         = '';
        if self.tsExpire              is None: self.tsExpire            = '';
        if self.uidAuthor             is None: self.uidAuthor           = -1;
        return True;

    def isEqual(self, oOther):
        """ Compares two instances. """
        return self.idTestCase       == oOther.idTestCase \
           and self.idTestCasePreReq == oOther.idTestCasePreReq \
           and self.tsEffective      == oOther.tsEffective \
           and self.tsExpire         == oOther.tsExpire \
           and self.uidAuthor        == oOther.uidAuthor;

    def getTestCasePreReqIds(self, aTestCaseDependencyData):
        """
        Get list of Test Case IDs which current
        Test Case depends on
        """
        if not aTestCaseDependencyData:
            return []

        aoRet = []
        for oTestCaseDependencyData in aTestCaseDependencyData:
            aoRet.append(oTestCaseDependencyData.idTestCasePreReq)

        return aoRet

class TestCaseDependencyLogic(ModelLogicBase):
    """Test case dependency management logic"""

    def getTestCaseDeps(self, idTestCase, tsEffective = None):
        """
        Returns an array of TestCaseDependencyData with the prerequisites of
        idTestCase.
        Returns empty array if none found. Raises exception on database error.
        """
        if tsEffective is not None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCaseDeps\n'
                              'WHERE    idTestCase   = %s\n'
                              '     AND tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              , (idTestCase, tsEffective, tsEffective, ) );
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCaseDeps\n'
                              'WHERE    idTestCase   = %s\n'
                              '     AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                              , (idTestCase, ) );
        aaoRows = self._oDb.fetchAll();
        aoRet = [];
        for aoRow in aaoRows:
            aoRet.append(TestCaseDependencyData().initFromDbRow(aoRow));

        return aoRet

    def getTestCaseDepsIds(self, idTestCase, tsNow = None):
        """
        Returns an array of test case IDs of the prerequisites of idTestCase.
        Returns empty array if none found. Raises exception on database error.
        """
        if tsNow is not None:
            self._oDb.execute('SELECT   idTestCase\n'
                              'FROM     TestCaseDeps\n'
                              'WHERE    idTestCase   = %s\n'
                              '     AND tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              , (idTestCase, tsNow, tsNow, ) );
        else:
            self._oDb.execute('SELECT   idTestCase\n'
                              'FROM     TestCaseDeps\n'
                              'WHERE    idTestCase   = %s\n'
                              '     AND tsExpire     = \'infinity\'::TIMESTAMP\n'
                              , (idTestCase, ) );
        aidPreReqs = [];
        for aoRow in self._oDb.fetchAll():
            aidPreReqs.append(aoRow[0]);
        return aidPreReqs;


    def getDepTestCaseData(self, idTestCase, tsNow = None):
        """
        Returns an array of objects of type TestCaseData2 on which
        specified test case depends on
        """
        if tsNow is None:
            self._oDb.execute('SELECT   TestCases.*\n'
                              'FROM     TestCases, TestCaseDeps\n'
                              'WHERE    TestCaseDeps.idTestCase       = %s\n'
                              '     AND TestCaseDeps.idTestCasePreReq = TestCases.idTestCase\n'
                              '     AND TestCaseDeps.tsExpire         = \'infinity\'::TIMESTAMP\n'
                              '     AND TestCases.tsExpire            = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY TestCases.idTestCase\n'
                              , (idTestCase, ) );
        else:
            self._oDb.execute('SELECT   TestCases.*\n'
                              'FROM     TestCases, TestCaseDeps\n'
                              'WHERE    TestCaseDeps.idTestCase       = %s\n'
                              '     AND TestCaseDeps.idTestCasePreReq = TestCases.idTestCase\n'
                              '     AND TestCaseDeps.tsExpire         > %s\n'
                              '     AND TestCaseDeps.tsEffective     <= %s\n'
                              '     AND TestCases.tsExpire            > %s\n'
                              '     AND TestCases.tsEffective        <= %s\n'
                              'ORDER BY TestCases.idTestCase\n'
                              , (idTestCase, tsNow, tsNow, tsNow, tsNow, ) );

        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            aoRet.append(TestCaseData().initFromDbRow(aoRow));

        return aoRet

    def getApplicableDepTestCaseData(self, idTestCase):
        """
        Returns an array of objects of type TestCaseData on which
        specified test case might depends on (all test
        cases except the specified one and those testcases which are
        depend on idTestCase)
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     TestCases\n'
                          'WHERE    idTestCase <> %s\n'
                          '     AND idTestCase NOT IN (SELECT idTestCase\n'
                          '                            FROM   TestCaseDeps\n'
                          '                            WHERE  idTestCasePreReq=%s\n'
                          '                               AND tsExpire = \'infinity\'::TIMESTAMP)\n'
                          '     AND tsExpire = \'infinity\'::TIMESTAMP\n'
                          , (idTestCase, idTestCase) )

        aaoRows = self._oDb.fetchAll()
        aoRet = []
        for aoRow in aaoRows:
            aoRet.append(TestCaseData().initFromDbRow(aoRow));

        return aoRet

class TestCaseData(ModelDataBase):
    """
    Test case data
    """

    ksIdAttr    = 'idTestCase';
    ksIdGenAttr = 'idGenTestCase';

    ksParam_idTestCase      = 'TestCase_idTestCase'
    ksParam_tsEffective     = 'TestCase_tsEffective'
    ksParam_tsExpire        = 'TestCase_tsExpire'
    ksParam_uidAuthor       = 'TestCase_uidAuthor'
    ksParam_idGenTestCase   = 'TestCase_idGenTestCase'
    ksParam_sName           = 'TestCase_sName'
    ksParam_sDescription    = 'TestCase_sDescription'
    ksParam_fEnabled        = 'TestCase_fEnabled'
    ksParam_cSecTimeout     = 'TestCase_cSecTimeout'
    ksParam_sTestBoxReqExpr = 'TestCase_sTestBoxReqExpr';
    ksParam_sBuildReqExpr   = 'TestCase_sBuildReqExpr';
    ksParam_sBaseCmd        = 'TestCase_sBaseCmd'
    ksParam_sValidationKitZips = 'TestCase_sValidationKitZips'
    ksParam_sComment        = 'TestCase_sComment'

    kasAllowNullAttributes  = [ 'idTestCase', 'tsEffective', 'tsExpire', 'uidAuthor', 'idGenTestCase', 'sDescription',
                                'sTestBoxReqExpr', 'sBuildReqExpr', 'sValidationKitZips', 'sComment' ];

    kcDbColumns             = 14;

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestCase         = None;
        self.tsEffective        = None;
        self.tsExpire           = None;
        self.uidAuthor          = None;
        self.idGenTestCase      = None;
        self.sName              = None;
        self.sDescription       = None;
        self.fEnabled           = False;
        self.cSecTimeout        = 10; # Init with minimum timeout value
        self.sTestBoxReqExpr    = None;
        self.sBuildReqExpr      = None;
        self.sBaseCmd           = None;
        self.sValidationKitZips = None;
        self.sComment           = None;

    def initFromDbRow(self, aoRow):
        """
        Reinitialize from a SELECT * FROM TestCases row.
        Returns self. Raises exception if no row.
        """
        if aoRow is None:
            raise TMRowNotFound('Test case not found.');

        self.idTestCase         = aoRow[0];
        self.tsEffective        = aoRow[1];
        self.tsExpire           = aoRow[2];
        self.uidAuthor          = aoRow[3];
        self.idGenTestCase      = aoRow[4];
        self.sName              = aoRow[5];
        self.sDescription       = aoRow[6];
        self.fEnabled           = aoRow[7];
        self.cSecTimeout        = aoRow[8];
        self.sTestBoxReqExpr    = aoRow[9];
        self.sBuildReqExpr      = aoRow[10];
        self.sBaseCmd           = aoRow[11];
        self.sValidationKitZips = aoRow[12];
        self.sComment           = aoRow[13];
        return self;

    def initFromDbWithId(self, oDb, idTestCase, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        oDb.execute(self.formatSimpleNowAndPeriodQuery(oDb,
                                                       'SELECT *\n'
                                                       'FROM   TestCases\n'
                                                       'WHERE  idTestCase   = %s\n'
                                                       , ( idTestCase,), tsNow, sPeriodBack));
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idTestCase=%s not found (tsNow=%s sPeriodBack=%s)' % (idTestCase, tsNow, sPeriodBack,));
        return self.initFromDbRow(aoRow);

    def initFromDbWithGenId(self, oDb, idGenTestCase, tsNow = None):
        """
        Initialize the object from the database.
        """
        _ = tsNow; # For relevant for the TestCaseDataEx version only.
        oDb.execute('SELECT *\n'
                    'FROM   TestCases\n'
                    'WHERE  idGenTestCase = %s\n'
                    , (idGenTestCase, ) );
        return self.initFromDbRow(oDb.fetchOne());

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        if sAttr == 'cSecTimeout' and oValue not in aoNilValues: # Allow human readable interval formats.
            return utils.parseIntervalSeconds(oValue);

        (oValue, sError) = ModelDataBase._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);
        if sError is None:
            if sAttr == 'sTestBoxReqExpr':
                sError = TestCaseData.validateTestBoxReqExpr(oValue);
            elif sAttr == 'sBuildReqExpr':
                sError = TestCaseData.validateBuildReqExpr(oValue);
            elif sAttr == 'sBaseCmd':
                _, sError = TestCaseData.validateStr(oValue, fAllowUnicodeSymbols=False);
        return (oValue, sError);


    #
    # Misc.
    #

    def needValidationKitBit(self):
        """
        Predicate method for checking whether a validation kit build is required.
        """
        return self.sValidationKitZips is None \
            or self.sValidationKitZips.find('@VALIDATIONKIT_ZIP@') >= 0;

    def matchesTestBoxProps(self, oTestBoxData):
        """
        Checks if the all of the testbox related test requirements matches the
        given testbox.

        Returns True or False according to the expression, None on exception or
        non-boolean expression result.
        """
        return TestCaseData.matchesTestBoxPropsEx(oTestBoxData, self.sTestBoxReqExpr);

    def matchesBuildProps(self, oBuildDataEx):
        """
        Checks if the all of the build related test requirements matches the
        given build.

        Returns True or False according to the expression, None on exception or
        non-boolean expression result.
        """
        return TestCaseData.matchesBuildPropsEx(oBuildDataEx, self.sBuildReqExpr);


    #
    # Expression validation code shared with TestCaseArgsDataEx.
    #
    @staticmethod
    def _safelyEvalExpr(sExpr, dLocals, fMayRaiseXcpt = False):
        """
        Safely evaluate requirment expression given a set of locals.

        Returns True or False according to the expression.  If the expression
        causes an exception to be raised or does not return a boolean result,
        None will be returned.
        """
        if sExpr is None  or  sExpr == '':
            return True;

        dGlobals = \
        {
            '__builtins__':     None,
            'long':             long,
            'int':              int,
            'bool':             bool,
            'True':             True,
            'False':            False,
            'len':              len,
            'isinstance':       isinstance,
            'type':             type,
            'dict':             dict,
            'dir':              dir,
            'list':             list,
            'versionCompare':   utils.versionCompare,
        };

        try:
            fRc = eval(sExpr, dGlobals, dLocals);
        except:
            if fMayRaiseXcpt:
                raise;
            return None;

        if not isinstance(fRc, bool):
            if fMayRaiseXcpt:
                raise Exception('not a boolean result: "%s" - %s' % (fRc, type(fRc)) );
            return None;

        return fRc;

    @staticmethod
    def _safelyValidateReqExpr(sExpr, adLocals):
        """
        Validates a requirement expression using the given sets of locals,
        returning None on success and an error string on failure.
        """
        for dLocals in adLocals:
            try:
                TestCaseData._safelyEvalExpr(sExpr, dLocals, True);
            except Exception as oXcpt:
                return str(oXcpt);
        return None;

    @staticmethod
    def validateTestBoxReqExpr(sExpr):
        """
        Validates a testbox expression, returning None on success and an error
        string on failure.
        """
        adTestBoxes = \
        [
            {
                'sOs':                  'win',
                'sOsVersion':           '3.1',
                'sCpuVendor':           'VirtualBox',
                'sCpuArch':             'x86',
                'cCpus':                1,
                'fCpuHwVirt':           False,
                'fCpuNestedPaging':     False,
                'fCpu64BitGuest':       False,
                'fChipsetIoMmu':        False,
                'fRawMode':             False,
                'fNativeApi':           False,
                'cMbMemory':            985034,
                'cMbScratch':           1234089,
                'iTestBoxScriptRev':    1,
                'sName':                'emanon',
                'uuidSystem':           '8FF81BE5-3901-4AB1-8A65-B48D511C0321',
            },
            {
                'sOs':                  'linux',
                'sOsVersion':           '3.1',
                'sCpuVendor':           'VirtualBox',
                'sCpuArch':             'amd64',
                'cCpus':                8191,
                'fCpuHwVirt':           True,
                'fCpuNestedPaging':     True,
                'fCpu64BitGuest':       True,
                'fChipsetIoMmu':        True,
                'fRawMode':             True,
                'fNativeApi':           True,
                'cMbMemory':            9999999999,
                'cMbScratch':           9999999999999,
                'iTestBoxScriptRev':    9999999,
                'sName':                'emanon',
                'uuidSystem':           '00000000-0000-0000-0000-000000000000',
            },
        ];
        return TestCaseData._safelyValidateReqExpr(sExpr, adTestBoxes);

    @staticmethod
    def matchesTestBoxPropsEx(oTestBoxData, sExpr):
        """ Worker for TestCaseData.matchesTestBoxProps and TestCaseArgsDataEx.matchesTestBoxProps. """
        if sExpr is None:
            return True;
        dLocals = \
        {
            'sOs':                  oTestBoxData.sOs,
            'sOsVersion':           oTestBoxData.sOsVersion,
            'sCpuVendor':           oTestBoxData.sCpuVendor,
            'sCpuArch':             oTestBoxData.sCpuArch,
            'iCpuFamily':           oTestBoxData.getCpuFamily(),
            'iCpuModel':            oTestBoxData.getCpuModel(),
            'cCpus':                oTestBoxData.cCpus,
            'fCpuHwVirt':           oTestBoxData.fCpuHwVirt,
            'fCpuNestedPaging':     oTestBoxData.fCpuNestedPaging,
            'fCpu64BitGuest':       oTestBoxData.fCpu64BitGuest,
            'fChipsetIoMmu':        oTestBoxData.fChipsetIoMmu,
            'fRawMode':             oTestBoxData.fRawMode,
            'fNativeApi':           oTestBoxData.fNativeApi,
            'cMbMemory':            oTestBoxData.cMbMemory,
            'cMbScratch':           oTestBoxData.cMbScratch,
            'iTestBoxScriptRev':    oTestBoxData.iTestBoxScriptRev,
            'iPythonHexVersion':    oTestBoxData.iPythonHexVersion,
            'sName':                oTestBoxData.sName,
            'uuidSystem':           oTestBoxData.uuidSystem,
        };
        return TestCaseData._safelyEvalExpr(sExpr, dLocals);

    @staticmethod
    def validateBuildReqExpr(sExpr):
        """
        Validates a testbox expression, returning None on success and an error
        string on failure.
        """
        adBuilds = \
        [
            {
                'sProduct':             'VirtualBox',
                'sBranch':              'trunk',
                'sType':                'release',
                'asOsArches':           ['win.amd64', 'win.x86'],
                'sVersion':             '1.0',
                'iRevision':            1234,
                'uidAuthor':            None,
                'idBuild':              953,
            },
            {
                'sProduct':             'VirtualBox',
                'sBranch':              'VBox-4.1',
                'sType':                'release',
                'asOsArches':           ['linux.x86',],
                'sVersion':             '4.2.15',
                'iRevision':            89876,
                'uidAuthor':            None,
                'idBuild':              945689,
            },
            {
                'sProduct':             'VirtualBox',
                'sBranch':              'VBox-4.1',
                'sType':                'strict',
                'asOsArches':           ['solaris.x86', 'solaris.amd64',],
                'sVersion':             '4.3.0_RC3',
                'iRevision':            97939,
                'uidAuthor':            33,
                'idBuild':              9456893,
            },
        ];
        return TestCaseData._safelyValidateReqExpr(sExpr, adBuilds);

    @staticmethod
    def matchesBuildPropsEx(oBuildDataEx, sExpr):
        """
        Checks if the all of the build related test requirements matches the
        given build.
        """
        if sExpr is None:
            return True;
        dLocals = \
        {
            'sProduct':             oBuildDataEx.oCat.sProduct,
            'sBranch':              oBuildDataEx.oCat.sBranch,
            'sType':                oBuildDataEx.oCat.sType,
            'asOsArches':           oBuildDataEx.oCat.asOsArches,
            'sVersion':             oBuildDataEx.sVersion,
            'iRevision':            oBuildDataEx.iRevision,
            'uidAuthor':            oBuildDataEx.uidAuthor,
            'idBuild':              oBuildDataEx.idBuild,
        };
        return TestCaseData._safelyEvalExpr(sExpr, dLocals);




class TestCaseDataEx(TestCaseData):
    """
    Test case data.
    """

    ksParam_aoTestCaseArgs          = 'TestCase_aoTestCaseArgs';
    ksParam_aoDepTestCases          = 'TestCase_aoDepTestCases';
    ksParam_aoDepGlobalResources    = 'TestCase_aoDepGlobalResources';

    # Use [] instead of None.
    kasAltArrayNull = [ 'aoTestCaseArgs', 'aoDepTestCases', 'aoDepGlobalResources' ];


    def __init__(self):
        TestCaseData.__init__(self);

        # List of objects of type TestCaseData (or TestCaseDataEx, we don't
        # care) on which current Test Case depends.
        self.aoDepTestCases         = [];

        # List of objects of type GlobalResourceData on which current Test Case depends.
        self.aoDepGlobalResources   = [];

        # List of objects of type TestCaseArgsData.
        self.aoTestCaseArgs         = [];

    def _initExtraMembersFromDb(self, oDb, tsNow = None, sPeriodBack = None):
        """
        Worker shared by the initFromDb* methods.
        Returns self.  Raises exception if no row or database error.
        """
        _ = sPeriodBack; ## @todo sPeriodBack
        from testmanager.core.testcaseargs import TestCaseArgsLogic;
        self.aoDepTestCases         = TestCaseDependencyLogic(oDb).getDepTestCaseData(self.idTestCase, tsNow);
        self.aoDepGlobalResources   = TestCaseGlobalRsrcDepLogic(oDb).getDepGlobalResourceData(self.idTestCase, tsNow);
        self.aoTestCaseArgs         = TestCaseArgsLogic(oDb).getTestCaseArgs(self.idTestCase, tsNow);
        # Note! The above arrays are sorted by their relvant IDs for fetchForChangeLog's sake.
        return self;

    def initFromDbRowEx(self, aoRow, oDb, tsNow = None):
        """
        Reinitialize from a SELECT * FROM TestCases row.  Will query the
        necessary additional data from oDb using tsNow.
        Returns self.  Raises exception if no row or database error.
        """
        TestCaseData.initFromDbRow(self, aoRow);
        return self._initExtraMembersFromDb(oDb, tsNow);

    def initFromDbWithId(self, oDb, idTestCase, tsNow = None, sPeriodBack = None):
        """
        Initialize the object from the database.
        """
        TestCaseData.initFromDbWithId(self, oDb, idTestCase, tsNow, sPeriodBack);
        return self._initExtraMembersFromDb(oDb, tsNow, sPeriodBack);

    def initFromDbWithGenId(self, oDb, idGenTestCase, tsNow = None):
        """
        Initialize the object from the database.
        """
        TestCaseData.initFromDbWithGenId(self, oDb, idGenTestCase);
        if tsNow is None and not oDb.isTsInfinity(self.tsExpire):
            tsNow = self.tsEffective;
        return self._initExtraMembersFromDb(oDb, tsNow);

    def getAttributeParamNullValues(self, sAttr):
        if sAttr in ['aoDepTestCases', 'aoDepGlobalResources', 'aoTestCaseArgs']:
            return [[], ''];
        return TestCaseData.getAttributeParamNullValues(self, sAttr);

    def convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict):
        """For dealing with the arrays."""
        if sAttr not in ['aoDepTestCases', 'aoDepGlobalResources', 'aoTestCaseArgs']:
            return TestCaseData.convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict);

        aoNewValues = [];
        if sAttr == 'aoDepTestCases':
            for idTestCase in oDisp.getListOfIntParams(sParam, 1, 0x7ffffffe, []):
                oDep = TestCaseData();
                oDep.idTestCase = str(idTestCase);
                aoNewValues.append(oDep);

        elif sAttr == 'aoDepGlobalResources':
            for idGlobalRsrc in oDisp.getListOfIntParams(sParam, 1, 0x7ffffffe, []):
                oGlobalRsrc = GlobalResourceData();
                oGlobalRsrc.idGlobalRsrc = str(idGlobalRsrc);
                aoNewValues.append(oGlobalRsrc);

        elif sAttr == 'aoTestCaseArgs':
            from testmanager.core.testcaseargs import TestCaseArgsData;
            for sArgKey in oDisp.getStringParam(TestCaseDataEx.ksParam_aoTestCaseArgs, sDefault = '').split(','):
                oDispWrapper = self.DispWrapper(oDisp, '%s[%s][%%s]' % (TestCaseDataEx.ksParam_aoTestCaseArgs, sArgKey,))
                aoNewValues.append(TestCaseArgsData().initFromParams(oDispWrapper, fStrict = False));
        return aoNewValues;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb): # pylint: disable=too-many-locals
        """
        Validate special arrays and requirement expressions.

        For the two dependency arrays we have to supply missing bits by
        looking them up in the database.  In the argument variation case we
        need to validate each item.
        """
        if sAttr not in ['aoDepTestCases', 'aoDepGlobalResources', 'aoTestCaseArgs']:
            return TestCaseData._validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb);

        asErrors = [];
        aoNewValues = [];
        if sAttr == 'aoDepTestCases':
            for oTestCase in self.aoDepTestCases:
                if utils.isString(oTestCase.idTestCase): # Stored as string convertParamToAttribute.
                    oTestCase = copy.copy(oTestCase);
                    try:
                        oTestCase.idTestCase = int(oTestCase.idTestCase);
                        oTestCase.initFromDbWithId(oDb, oTestCase.idTestCase);
                    except Exception as oXcpt:
                        asErrors.append('Test case dependency #%s: %s' % (oTestCase.idTestCase, oXcpt));
                aoNewValues.append(oTestCase);

        elif sAttr == 'aoDepGlobalResources':
            for oGlobalRsrc in self.aoDepGlobalResources:
                if utils.isString(oGlobalRsrc.idGlobalRsrc): # Stored as string convertParamToAttribute.
                    oGlobalRsrc = copy.copy(oGlobalRsrc);
                    try:
                        oGlobalRsrc.idTestCase = int(oGlobalRsrc.idGlobalRsrc);
                        oGlobalRsrc.initFromDbWithId(oDb, oGlobalRsrc.idGlobalRsrc);
                    except Exception as oXcpt:
                        asErrors.append('Resource dependency #%s: %s' % (oGlobalRsrc.idGlobalRsrc, oXcpt));
                aoNewValues.append(oGlobalRsrc);

        else:
            assert sAttr == 'aoTestCaseArgs';
            if not self.aoTestCaseArgs:
                return (None, 'The testcase requires at least one argument variation to be valid.');

            # Note! We'll be returning an error dictionary instead of an string here.
            dErrors = {};

            for iVar, oVar in enumerate(self.aoTestCaseArgs):
                oVar = copy.copy(oVar);
                oVar.idTestCase = self.idTestCase;
                dCurErrors = oVar.validateAndConvert(oDb, ModelDataBase.ksValidateFor_Other);
                if not dCurErrors:
                    pass; ## @todo figure out the ID?
                else:
                    asErrors = [];
                    for sKey in dCurErrors:
                        asErrors.append('%s: %s' % (sKey[len('TestCaseArgs_'):], dCurErrors[sKey]));
                    dErrors[iVar] = '<br>\n'.join(asErrors)
                aoNewValues.append(oVar);

            for iVar, oVar in enumerate(self.aoTestCaseArgs):
                sArgs = oVar.sArgs;
                for iVar2 in range(iVar + 1, len(self.aoTestCaseArgs)):
                    if self.aoTestCaseArgs[iVar2].sArgs == sArgs:
                        sMsg = 'Duplicate argument variation "%s".' % (sArgs);
                        if iVar in dErrors:     dErrors[iVar]  += '<br>\n' + sMsg;
                        else:                   dErrors[iVar]   = sMsg;
                        if iVar2 in dErrors:    dErrors[iVar2] += '<br>\n' + sMsg;
                        else:                   dErrors[iVar2]  = sMsg;
                        break;

            return (aoNewValues, dErrors if dErrors else None);

        return (aoNewValues, None if not asErrors else ' <br>'.join(asErrors));

    def _validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor = ModelDataBase.ksValidateFor_Other):
        dErrors = TestCaseData._validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor);

        # Validate dependencies a wee bit for paranoid reasons. The scheduler
        # queue generation code does the real validation here!
        if not dErrors and self.idTestCase is not None:
            for oDep in self.aoDepTestCases:
                if oDep.idTestCase == self.idTestCase:
                    if self.ksParam_aoDepTestCases in dErrors:
                        dErrors[self.ksParam_aoDepTestCases] += ' Depending on itself!';
                    else:
                        dErrors[self.ksParam_aoDepTestCases]   = 'Depending on itself!';
        return dErrors;





class TestCaseLogic(ModelLogicBase):
    """
    Test case management logic.
    """

    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb)
        self.dCache = None;

    def getAll(self):
        """
        Fetches all test case records from DB (TestCaseData).
        """
        self._oDb.execute('SELECT *\n'
                          'FROM   TestCases\n'
                          'WHERE  tsExpire = \'infinity\'::TIMESTAMP\n'
                          'ORDER BY idTestCase ASC;')

        aaoRows = self._oDb.fetchAll()
        aoRet = [];
        for aoRow in aaoRows:
            aoRet.append(TestCaseData().initFromDbRow(aoRow))
        return aoRet

    def fetchForListing(self, iStart, cMaxRows, tsNow, aiSortColumns = None):
        """
        Fetches test cases.

        Returns an array (list) of TestCaseDataEx items, empty list if none.
        Raises exception on error.
        """
        _ = aiSortColumns;
        if tsNow is None:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCases\n'
                              'WHERE    tsExpire = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY sName ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (cMaxRows, iStart, ));
        else:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCases\n'
                              'WHERE    tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY sName ASC\n'
                              'LIMIT %s OFFSET %s\n'
                              , (tsNow, tsNow, cMaxRows, iStart, ));

        aoRows = [];
        for aoRow in self._oDb.fetchAll():
            aoRows.append(TestCaseDataEx().initFromDbRowEx(aoRow, self._oDb, tsNow));
        return aoRows;

    def fetchForChangeLog(self, idTestCase, iStart, cMaxRows, tsNow): # pylint: disable=too-many-locals
        """
        Fetches change log entries for a testbox.

        Returns an array of ChangeLogEntry instance and an indicator whether
        there are more entries.
        Raises exception on error.
        """

        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();

        # 1. Get a list of the relevant change times.
        self._oDb.execute('( SELECT tsEffective, uidAuthor FROM TestCases       WHERE idTestCase = %s AND tsEffective <= %s )\n'
                          'UNION\n'
                          '( SELECT tsEffective, uidAuthor FROM TestCaseArgs    WHERE idTestCase = %s AND tsEffective <= %s )\n'
                          'UNION\n'
                          '( SELECT tsEffective, uidAuthor FROM TestCaseDeps    WHERE idTestCase = %s AND tsEffective <= %s )\n'
                          'UNION\n'
                          '( SELECT tsEffective, uidAuthor FROM TestCaseGlobalRsrcDeps \n' \
                          '  WHERE idTestCase = %s AND tsEffective <= %s )\n'
                          'ORDER BY tsEffective DESC\n'
                          'LIMIT %s OFFSET %s\n'
                          , ( idTestCase, tsNow,
                              idTestCase, tsNow,
                              idTestCase, tsNow,
                              idTestCase, tsNow,
                              cMaxRows + 1, iStart, ));
        aaoChanges = self._oDb.fetchAll();

        # 2. Collect data sets for each of those points.
        #    (Doing it the lazy + inefficient way for now.)
        aoRows = [];
        for aoChange in aaoChanges:
            aoRows.append(TestCaseDataEx().initFromDbWithId(self._oDb, idTestCase, aoChange[0]));

        # 3. Calculate the changes.
        aoEntries = [];
        for i in range(0, len(aoRows) - 1):
            oNew = aoRows[i];
            oOld = aoRows[i + 1];
            (tsEffective, uidAuthor) = aaoChanges[i];
            (tsExpire, _) = aaoChanges[i - 1] if i > 0 else (oNew.tsExpire, None)
            assert self._oDb.isTsInfinity(tsEffective) != self._oDb.isTsInfinity(tsExpire) or tsEffective < tsExpire, \
                '%s vs %s' % (tsEffective, tsExpire);

            aoChanges = [];

            # The testcase object.
            if oNew.tsEffective != oOld.tsEffective:
                for sAttr in oNew.getDataAttributes():
                    if sAttr not in [ 'tsEffective', 'tsExpire', 'uidAuthor', \
                                      'aoTestCaseArgs', 'aoDepTestCases', 'aoDepGlobalResources']:
                        oOldAttr = getattr(oOld, sAttr);
                        oNewAttr = getattr(oNew, sAttr);
                        if oOldAttr != oNewAttr:
                            aoChanges.append(AttributeChangeEntry(sAttr, oNewAttr, oOldAttr, str(oNewAttr), str(oOldAttr)));

            # The argument variations.
            iChildOld = 0;
            for oChildNew in oNew.aoTestCaseArgs:
                # Locate the old entry, emitting removed markers for old items we have to skip.
                while iChildOld < len(oOld.aoTestCaseArgs) \
                  and oOld.aoTestCaseArgs[iChildOld].idTestCaseArgs < oChildNew.idTestCaseArgs:
                    oChildOld = oOld.aoTestCaseArgs[iChildOld];
                    aoChanges.append(AttributeChangeEntry('Variation #%s' % (oChildOld.idTestCaseArgs,),
                                                          None, oChildOld, 'Removed', str(oChildOld)));
                    iChildOld += 1;

                if    iChildOld < len(oOld.aoTestCaseArgs) \
                  and oOld.aoTestCaseArgs[iChildOld].idTestCaseArgs == oChildNew.idTestCaseArgs:
                    oChildOld = oOld.aoTestCaseArgs[iChildOld];
                    if oChildNew.tsEffective != oChildOld.tsEffective:
                        for sAttr in oChildNew.getDataAttributes():
                            if sAttr not in [ 'tsEffective', 'tsExpire', 'uidAuthor', 'idGenTestCase', ]:
                                oOldAttr = getattr(oChildOld, sAttr);
                                oNewAttr = getattr(oChildNew, sAttr);
                                if oOldAttr != oNewAttr:
                                    aoChanges.append(AttributeChangeEntry('Variation[#%s].%s'
                                                                          % (oChildOld.idTestCaseArgs, sAttr,),
                                                                          oNewAttr, oOldAttr,
                                                                          str(oNewAttr), str(oOldAttr)));
                    iChildOld += 1;
                else:
                    aoChanges.append(AttributeChangeEntry('Variation #%s' % (oChildNew.idTestCaseArgs,),
                                                          oChildNew, None,
                                                          str(oChildNew), 'Did not exist'));

            # The testcase dependencies.
            iChildOld = 0;
            for oChildNew in oNew.aoDepTestCases:
                # Locate the old entry, emitting removed markers for old items we have to skip.
                while iChildOld < len(oOld.aoDepTestCases) \
                  and oOld.aoDepTestCases[iChildOld].idTestCase < oChildNew.idTestCase:
                    oChildOld = oOld.aoDepTestCases[iChildOld];
                    aoChanges.append(AttributeChangeEntry('Dependency #%s' % (oChildOld.idTestCase,),
                                                          None, oChildOld, 'Removed',
                                                          '%s (#%u)' % (oChildOld.sName, oChildOld.idTestCase,)));
                    iChildOld += 1;
                if    iChildOld < len(oOld.aoDepTestCases) \
                  and oOld.aoDepTestCases[iChildOld].idTestCase == oChildNew.idTestCase:
                    iChildOld += 1;
                else:
                    aoChanges.append(AttributeChangeEntry('Dependency #%s' % (oChildNew.idTestCase,),
                                                          oChildNew, None,
                                                          '%s (#%u)' % (oChildNew.sName, oChildNew.idTestCase,),
                                                          'Did not exist'));

            # The global resource dependencies.
            iChildOld = 0;
            for oChildNew in oNew.aoDepGlobalResources:
                # Locate the old entry, emitting removed markers for old items we have to skip.
                while iChildOld < len(oOld.aoDepGlobalResources) \
                  and oOld.aoDepGlobalResources[iChildOld].idGlobalRsrc < oChildNew.idGlobalRsrc:
                    oChildOld = oOld.aoDepGlobalResources[iChildOld];
                    aoChanges.append(AttributeChangeEntry('Global Resource #%s' % (oChildOld.idGlobalRsrc,),
                                                          None, oChildOld, 'Removed',
                                                          '%s (#%u)' % (oChildOld.sName, oChildOld.idGlobalRsrc,)));
                    iChildOld += 1;
                if    iChildOld < len(oOld.aoDepGlobalResources) \
                  and oOld.aoDepGlobalResources[iChildOld].idGlobalRsrc == oChildNew.idGlobalRsrc:
                    iChildOld += 1;
                else:
                    aoChanges.append(AttributeChangeEntry('Global Resource #%s' % (oChildNew.idGlobalRsrc,),
                                                          oChildNew, None,
                                                          '%s (#%u)' % (oChildNew.sName, oChildNew.idGlobalRsrc,),
                                                          'Did not exist'));

            # Done.
            aoEntries.append(ChangeLogEntry(uidAuthor, None, tsEffective, tsExpire, oNew, oOld, aoChanges));

        # If we're at the end of the log, add the initial entry.
        if len(aoRows) <= cMaxRows and aoRows:
            oNew = aoRows[-1];
            aoEntries.append(ChangeLogEntry(oNew.uidAuthor, None,
                                            aaoChanges[-1][0], aaoChanges[-2][0] if len(aaoChanges) > 1 else oNew.tsExpire,
                                            oNew, None, []));

        return (UserAccountLogic(self._oDb).resolveChangeLogAuthors(aoEntries), len(aoRows) > cMaxRows);


    def addEntry(self, oData, uidAuthor, fCommit = False):
        """
        Add a new testcase to the DB.
        """

        #
        # Validate the input first.
        #
        assert isinstance(oData, TestCaseDataEx);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Add);
        if dErrors:
            raise TMInvalidData('Invalid input data: %s' % (dErrors,));

        #
        # Add the testcase.
        #
        self._oDb.callProc('TestCaseLogic_addEntry',
                           ( uidAuthor, oData.sName, oData.sDescription, oData.fEnabled, oData.cSecTimeout,
                             oData.sTestBoxReqExpr, oData.sBuildReqExpr, oData.sBaseCmd, oData.sValidationKitZips,
                             oData.sComment ));
        oData.idTestCase = self._oDb.fetchOne()[0];

        # Add testcase dependencies.
        for oDep in oData.aoDepTestCases:
            self._oDb.execute('INSERT INTO TestCaseDeps (idTestCase, idTestCasePreReq, uidAuthor) VALUES (%s, %s, %s)'
                              , (oData.idTestCase, oDep.idTestCase, uidAuthor))

        # Add global resource dependencies.
        for oDep in oData.aoDepGlobalResources:
            self._oDb.execute('INSERT INTO TestCaseGlobalRsrcDeps (idTestCase, idGlobalRsrc, uidAuthor) VALUES (%s, %s, %s)'
                              , (oData.idTestCase, oDep.idGlobalRsrc, uidAuthor))

        # Set Test Case Arguments variations
        for oVar in oData.aoTestCaseArgs:
            self._oDb.execute('INSERT INTO TestCaseArgs (\n'
                             '          idTestCase, uidAuthor, sArgs, cSecTimeout,\n'
                              '         sTestBoxReqExpr, sBuildReqExpr, cGangMembers, sSubName)\n'
                              'VALUES   (%s, %s, %s, %s, %s, %s, %s, %s)'
                              , ( oData.idTestCase, uidAuthor, oVar.sArgs, oVar.cSecTimeout,
                                  oVar.sTestBoxReqExpr, oVar.sBuildReqExpr, oVar.cGangMembers, oVar.sSubName, ));

        self._oDb.maybeCommit(fCommit);
        return True;

    def editEntry(self, oData, uidAuthor, fCommit = False):  # pylint: disable=too-many-locals
        """
        Edit a testcase entry (extended).
        Caller is expected to rollback the database transactions on exception.
        """

        #
        # Validate the input.
        #
        assert isinstance(oData, TestCaseDataEx);
        dErrors = oData.validateAndConvert(self._oDb, oData.ksValidateFor_Edit);
        if dErrors:
            raise TMInvalidData('Invalid input data: %s' % (dErrors,));

        #
        # Did anything change? If not return straight away.
        #
        oOldDataEx = TestCaseDataEx().initFromDbWithId(self._oDb, oData.idTestCase);
        if oOldDataEx.isEqual(oData):
            self._oDb.maybeCommit(fCommit);
            return True;

        #
        # Make the necessary changes.
        #

        # The test case itself.
        if not TestCaseData().initFromOther(oOldDataEx).isEqual(oData):
            self._oDb.callProc('TestCaseLogic_editEntry', ( uidAuthor, oData.idTestCase, oData.sName, oData.sDescription,
                                                            oData.fEnabled, oData.cSecTimeout, oData.sTestBoxReqExpr,
                                                            oData.sBuildReqExpr, oData.sBaseCmd, oData.sValidationKitZips,
                                                            oData.sComment ));
            oData.idGenTestCase = self._oDb.fetchOne()[0];

        #
        # Its dependencies on other testcases.
        #
        aidNewDeps = [oDep.idTestCase for oDep in oData.aoDepTestCases];
        aidOldDeps = [oDep.idTestCase for oDep in oOldDataEx.aoDepTestCases];

        sQuery = self._oDb.formatBindArgs('UPDATE   TestCaseDeps\n'
                                          'SET      tsExpire   = CURRENT_TIMESTAMP\n'
                                          'WHERE    idTestCase = %s\n'
                                          '     AND tsExpire   = \'infinity\'::timestamp\n'
                                          , (oData.idTestCase,));
        asKeepers = [];
        for idDep in aidOldDeps:
            if idDep in aidNewDeps:
                asKeepers.append(str(idDep));
        if asKeepers:
            sQuery += '     AND idTestCasePreReq NOT IN (' + ', '.join(asKeepers) + ')\n';
        self._oDb.execute(sQuery);

        for idDep in aidNewDeps:
            if idDep not in aidOldDeps:
                self._oDb.execute('INSERT INTO TestCaseDeps (idTestCase, idTestCasePreReq, uidAuthor)\n'
                                  'VALUES (%s, %s, %s)\n'
                                  , (oData.idTestCase, idDep, uidAuthor) );

        #
        # Its dependencies on global resources.
        #
        aidNewDeps = [oDep.idGlobalRsrc for oDep in oData.aoDepGlobalResources];
        aidOldDeps = [oDep.idGlobalRsrc for oDep in oOldDataEx.aoDepGlobalResources];

        sQuery = self._oDb.formatBindArgs('UPDATE   TestCaseGlobalRsrcDeps\n'
                                          'SET      tsExpire   = CURRENT_TIMESTAMP\n'
                                          'WHERE    idTestCase = %s\n'
                                          '     AND tsExpire   = \'infinity\'::timestamp\n'
                                          , (oData.idTestCase,));
        asKeepers = [];
        for idDep in aidOldDeps:
            if idDep in aidNewDeps:
                asKeepers.append(str(idDep));
        if asKeepers:
            sQuery = '     AND idGlobalRsrc NOT IN (' + ', '.join(asKeepers) + ')\n';
        self._oDb.execute(sQuery);

        for idDep in aidNewDeps:
            if idDep not in aidOldDeps:
                self._oDb.execute('INSERT INTO TestCaseGlobalRsrcDeps (idTestCase, idGlobalRsrc, uidAuthor)\n'
                                  'VALUES (%s, %s, %s)\n'
                                  , (oData.idTestCase, idDep, uidAuthor) );

        #
        # Update Test Case Args
        # Note! Primary key is idTestCase, tsExpire, sArgs.
        #

        # Historize rows that have been removed.
        sQuery = self._oDb.formatBindArgs('UPDATE TestCaseArgs\n'
                                          'SET    tsExpire   = CURRENT_TIMESTAMP\n'
                                          'WHERE  idTestCase = %s\n'
                                          '   AND tsExpire   = \'infinity\'::TIMESTAMP'
                                          , (oData.idTestCase, ));
        for oNewVar in oData.aoTestCaseArgs:
            asKeepers.append(self._oDb.formatBindArgs('%s', (oNewVar.sArgs,)));
        if asKeepers:
            sQuery += '    AND  sArgs NOT IN (' + ', '.join(asKeepers) + ')\n';
        self._oDb.execute(sQuery);

        # Add new TestCaseArgs records if necessary, reusing old IDs when possible.
        from testmanager.core.testcaseargs import TestCaseArgsData;
        for oNewVar in oData.aoTestCaseArgs:
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCaseArgs\n'
                              'WHERE    idTestCase = %s\n'
                              '     AND sArgs      = %s\n'
                              'ORDER BY tsExpire DESC\n'
                              'LIMIT 1\n'
                              , (oData.idTestCase, oNewVar.sArgs,));
            aoRow = self._oDb.fetchOne();
            if aoRow is None:
                # New
                self._oDb.execute('INSERT INTO TestCaseArgs (\n'
                                 '          idTestCase, uidAuthor, sArgs, cSecTimeout,\n'
                                  '         sTestBoxReqExpr, sBuildReqExpr, cGangMembers, sSubName)\n'
                                  'VALUES   (%s, %s, %s, %s, %s, %s, %s, %s)'
                                  , ( oData.idTestCase, uidAuthor, oNewVar.sArgs, oNewVar.cSecTimeout,
                                      oNewVar.sTestBoxReqExpr, oNewVar.sBuildReqExpr, oNewVar.cGangMembers, oNewVar.sSubName));
            else:
                oCurVar = TestCaseArgsData().initFromDbRow(aoRow);
                if self._oDb.isTsInfinity(oCurVar.tsExpire):
                    # Existing current entry, updated if changed.
                    if    oNewVar.cSecTimeout     == oCurVar.cSecTimeout \
                      and oNewVar.sTestBoxReqExpr == oCurVar.sTestBoxReqExpr \
                      and oNewVar.sBuildReqExpr   == oCurVar.sBuildReqExpr \
                      and oNewVar.cGangMembers    == oCurVar.cGangMembers \
                      and oNewVar.sSubName        == oCurVar.sSubName:
                        oNewVar.idTestCaseArgs    = oCurVar.idTestCaseArgs;
                        oNewVar.idGenTestCaseArgs = oCurVar.idGenTestCaseArgs;
                        continue; # Unchanged.
                    self._oDb.execute('UPDATE TestCaseArgs SET tsExpire = CURRENT_TIMESTAMP WHERE idGenTestCaseArgs = %s\n'
                                      , (oCurVar.idGenTestCaseArgs, ));
                else:
                    # Existing old entry, re-use the ID.
                    pass;
                self._oDb.execute('INSERT INTO TestCaseArgs (\n'
                                  '         idTestCaseArgs, idTestCase, uidAuthor, sArgs, cSecTimeout,\n'
                                  '         sTestBoxReqExpr, sBuildReqExpr, cGangMembers, sSubName)\n'
                                  'VALUES   (%s, %s, %s, %s, %s, %s, %s, %s, %s)\n'
                                  'RETURNING idGenTestCaseArgs\n'
                                  , ( oCurVar.idTestCaseArgs, oData.idTestCase, uidAuthor, oNewVar.sArgs, oNewVar.cSecTimeout,
                                      oNewVar.sTestBoxReqExpr, oNewVar.sBuildReqExpr, oNewVar.cGangMembers, oNewVar.sSubName));
                oNewVar.idGenTestCaseArgs = self._oDb.fetchOne()[0];

        self._oDb.maybeCommit(fCommit);
        return True;

    def removeEntry(self, uidAuthor, idTestCase, fCascade = False, fCommit = False):
        """ Deletes the test case if possible. """
        self._oDb.callProc('TestCaseLogic_delEntry', (uidAuthor, idTestCase, fCascade));
        self._oDb.maybeCommit(fCommit);
        return True


    def getTestCasePreReqIds(self, idTestCase, tsEffective = None, cMax = None):
        """
        Returns an array of prerequisite testcases (IDs) for the given testcase.
        May raise exception on database error or if the result exceeds cMax.
        """
        if tsEffective is None:
            self._oDb.execute('SELECT   idTestCasePreReq\n'
                              'FROM     TestCaseDeps\n'
                              'WHERE    idTestCase = %s\n'
                              '     AND tsExpire   = \'infinity\'::TIMESTAMP\n'
                              'ORDER BY idTestCasePreReq\n'
                              , (idTestCase,) );
        else:
            self._oDb.execute('SELECT   idTestCasePreReq\n'
                              'FROM     TestCaseDeps\n'
                              'WHERE    idTestCase   = %s\n'
                              '     AND tsExpire     > %s\n'
                              '     AND tsEffective <= %s\n'
                              'ORDER BY idTestCasePreReq\n'
                              , (idTestCase, tsEffective, tsEffective) );


        if cMax is not None  and  self._oDb.getRowCount() > cMax:
            raise TMExceptionBase('Too many prerequisites for testcase %s: %s, max %s'
                                  % (idTestCase, cMax, self._oDb.getRowCount(),));

        aidPreReqs = [];
        for aoRow in self._oDb.fetchAll():
            aidPreReqs.append(aoRow[0]);
        return aidPreReqs;


    def cachedLookup(self, idTestCase):
        """
        Looks up the most recent TestCaseDataEx object for idTestCase
        via an object cache.

        Returns a shared TestCaseDataEx object.  None if not found.
        Raises exception on DB error.
        """
        if self.dCache is None:
            self.dCache = self._oDb.getCache('TestCaseDataEx');
        oEntry = self.dCache.get(idTestCase, None);
        if oEntry is None:
            fNeedTsNow = False;
            self._oDb.execute('SELECT   *\n'
                              'FROM     TestCases\n'
                              'WHERE    idTestCase = %s\n'
                              '     AND tsExpire   = \'infinity\'::TIMESTAMP\n'
                              , (idTestCase, ));
            if self._oDb.getRowCount() == 0:
                # Maybe it was deleted, try get the last entry.
                self._oDb.execute('SELECT   *\n'
                                  'FROM     TestCases\n'
                                  'WHERE    idTestCase = %s\n'
                                  'ORDER BY tsExpire DESC\n'
                                  'LIMIT 1\n'
                                  , (idTestCase, ));
                fNeedTsNow = True;
            elif self._oDb.getRowCount() > 1:
                raise self._oDb.integrityException('%s infinity rows for %s' % (self._oDb.getRowCount(), idTestCase));

            if self._oDb.getRowCount() == 1:
                aaoRow = self._oDb.fetchOne();
                oEntry = TestCaseDataEx();
                tsNow  = oEntry.initFromDbRow(aaoRow).tsEffective if fNeedTsNow else None;
                oEntry.initFromDbRowEx(aaoRow, self._oDb, tsNow);
                self.dCache[idTestCase] = oEntry;
        return oEntry;



#
# Unit testing.
#

# pylint: disable=missing-docstring
class TestCaseGlobalRsrcDepDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestCaseGlobalRsrcDepData(),];

class TestCaseDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestCaseData(),];

    def testEmptyExpr(self):
        self.assertEqual(TestCaseData.validateTestBoxReqExpr(None), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr(''), None);

    def testSimpleExpr(self):
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('cMbMemory > 10'), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('cMbScratch < 10'), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('fChipsetIoMmu'), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('fChipsetIoMmu is True'), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('fChipsetIoMmu is False'), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('fChipsetIoMmu is None'), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('isinstance(fChipsetIoMmu, bool)'), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('isinstance(iTestBoxScriptRev, int)'), None);
        self.assertEqual(TestCaseData.validateTestBoxReqExpr('isinstance(cMbScratch, long)'), None);

    def testBadExpr(self):
        self.assertNotEqual(TestCaseData.validateTestBoxReqExpr('this is an bad expression, surely it must be'), None);
        self.assertNotEqual(TestCaseData.validateTestBoxReqExpr('x = 1 + 1'), None);
        self.assertNotEqual(TestCaseData.validateTestBoxReqExpr('__import__(\'os\').unlink(\'/tmp/no/such/file\')'), None);
        self.assertNotEqual(TestCaseData.validateTestBoxReqExpr('print "foobar"'), None);

class TestCaseDataExTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestCaseDataEx(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.

