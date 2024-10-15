# -*- coding: utf-8 -*-
# $Id: testset.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - TestSet.
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
import os;
import zipfile;
import unittest;

# Validation Kit imports.
from common                         import utils;
from testmanager                    import config;
from testmanager.core               import db;
from testmanager.core.base          import ModelDataBase, ModelDataBaseTestCase, ModelLogicBase,  \
                                           TMExceptionBase, TMTooManyRows, TMRowNotFound;
from testmanager.core.testbox       import TestBoxData;
from testmanager.core.testresults   import TestResultFileDataEx;


class TestSetData(ModelDataBase):
    """
    TestSet Data.
    """

    ## @name TestStatus_T
    # @{
    ksTestStatus_Running    = 'running';
    ksTestStatus_Success    = 'success';
    ksTestStatus_Skipped    = 'skipped';
    ksTestStatus_BadTestBox = 'bad-testbox';
    ksTestStatus_Aborted    = 'aborted';
    ksTestStatus_Failure    = 'failure';
    ksTestStatus_TimedOut   = 'timed-out';
    ksTestStatus_Rebooted   = 'rebooted';
    ## @}

    ## List of relatively harmless (to testgroup/case) statuses.
    kasHarmlessTestStatuses = [ ksTestStatus_Skipped, ksTestStatus_BadTestBox, ksTestStatus_Aborted, ];
    ## List of bad statuses.
    kasBadTestStatuses      = [ ksTestStatus_Failure, ksTestStatus_TimedOut,   ksTestStatus_Rebooted, ];

    ksIdAttr    = 'idTestSet';

    ksParam_idTestSet           = 'TestSet_idTestSet';
    ksParam_tsConfig            = 'TestSet_tsConfig';
    ksParam_tsCreated           = 'TestSet_tsCreated';
    ksParam_tsDone              = 'TestSet_tsDone';
    ksParam_enmStatus           = 'TestSet_enmStatus';
    ksParam_idBuild             = 'TestSet_idBuild';
    ksParam_idBuildCategory     = 'TestSet_idBuildCategory';
    ksParam_idBuildTestSuite    = 'TestSet_idBuildTestSuite';
    ksParam_idGenTestBox        = 'TestSet_idGenTestBox';
    ksParam_idTestBox           = 'TestSet_idTestBox';
    ksParam_idSchedGroup        = 'TestSet_idSchedGroup';
    ksParam_idTestGroup         = 'TestSet_idTestGroup';
    ksParam_idGenTestCase       = 'TestSet_idGenTestCase';
    ksParam_idTestCase          = 'TestSet_idTestCase';
    ksParam_idGenTestCaseArgs   = 'TestSet_idGenTestCaseArgs';
    ksParam_idTestCaseArgs      = 'TestSet_idTestCaseArgs';
    ksParam_idTestResult        = 'TestSet_idTestResult';
    ksParam_sBaseFilename       = 'TestSet_sBaseFilename';
    ksParam_iGangMemberNo       = 'TestSet_iGangMemberNo';
    ksParam_idTestSetGangLeader = 'TestSet_idTestSetGangLeader';

    kasAllowNullAttributes      = [ 'tsDone', 'idBuildTestSuite', 'idTestSetGangLeader' ];
    kasValidValues_enmStatus    = [
        ksTestStatus_Running,
        ksTestStatus_Success,
        ksTestStatus_Skipped,
        ksTestStatus_BadTestBox,
        ksTestStatus_Aborted,
        ksTestStatus_Failure,
        ksTestStatus_TimedOut,
        ksTestStatus_Rebooted,
    ];
    kiMin_iGangMemberNo         = 0;
    kiMax_iGangMemberNo         = 1023;


    kcDbColumns                 = 20;

    def __init__(self):
        ModelDataBase.__init__(self);

        #
        # Initialize with defaults.
        # See the database for explanations of each of these fields.
        #
        self.idTestSet              = None;
        self.tsConfig               = None;
        self.tsCreated              = None;
        self.tsDone                 = None;
        self.enmStatus              = 'running';
        self.idBuild                = None;
        self.idBuildCategory        = None;
        self.idBuildTestSuite       = None;
        self.idGenTestBox           = None;
        self.idTestBox              = None;
        self.idSchedGroup           = None;
        self.idTestGroup            = None;
        self.idGenTestCase          = None;
        self.idTestCase             = None;
        self.idGenTestCaseArgs      = None;
        self.idTestCaseArgs         = None;
        self.idTestResult           = None;
        self.sBaseFilename          = None;
        self.iGangMemberNo          = 0;
        self.idTestSetGangLeader    = None;

    def initFromDbRow(self, aoRow):
        """
        Internal worker for initFromDbWithId and initFromDbWithGenId as well as
        TestBoxSetLogic.
        """

        if aoRow is None:
            raise TMRowNotFound('TestSet not found.');

        self.idTestSet              = aoRow[0];
        self.tsConfig               = aoRow[1];
        self.tsCreated              = aoRow[2];
        self.tsDone                 = aoRow[3];
        self.enmStatus              = aoRow[4];
        self.idBuild                = aoRow[5];
        self.idBuildCategory        = aoRow[6];
        self.idBuildTestSuite       = aoRow[7];
        self.idGenTestBox           = aoRow[8];
        self.idTestBox              = aoRow[9];
        self.idSchedGroup           = aoRow[10];
        self.idTestGroup            = aoRow[11];
        self.idGenTestCase          = aoRow[12];
        self.idTestCase             = aoRow[13];
        self.idGenTestCaseArgs      = aoRow[14];
        self.idTestCaseArgs         = aoRow[15];
        self.idTestResult           = aoRow[16];
        self.sBaseFilename          = aoRow[17];
        self.iGangMemberNo          = aoRow[18];
        self.idTestSetGangLeader    = aoRow[19];
        return self;


    def initFromDbWithId(self, oDb, idTestSet):
        """
        Initialize the object from the database.
        """
        oDb.execute('SELECT *\n'
                    'FROM   TestSets\n'
                    'WHERE  idTestSet = %s\n'
                    , (idTestSet, ) );
        aoRow = oDb.fetchOne()
        if aoRow is None:
            raise TMRowNotFound('idTestSet=%s not found' % (idTestSet,));
        return self.initFromDbRow(aoRow);


    def openFile(self, sFilename, sMode = 'rb'):
        """
        Opens a file.

        Returns (oFile, cbFile, fIsStream) on success.
        Returns (None,  sErrorMsg, None) on failure.
        Will not raise exceptions, unless the class instance is invalid.
        """
        assert sMode in [ 'rb', 'r', 'rU' ];

        # Try raw file first.
        sFile1 = os.path.join(config.g_ksFileAreaRootDir, self.sBaseFilename + '-' + sFilename);
        try:
            oFile = open(sFile1, sMode);                        # pylint: disable=consider-using-with,unspecified-encoding
            return (oFile, os.fstat(oFile.fileno()).st_size, False);
        except Exception as oXcpt1:
            # Try the zip archive next.
            sFile2 = os.path.join(config.g_ksZipFileAreaRootDir, self.sBaseFilename + '.zip');
            try:
                oZipFile    = zipfile.ZipFile(sFile2, 'r');                              # pylint: disable=consider-using-with
                oFile       = oZipFile.open(sFilename, sMode if sMode != 'rb' else 'r'); # pylint: disable=consider-using-with
                cbFile      = oZipFile.getinfo(sFilename).file_size;
                return (oFile, cbFile, True);
            except Exception as oXcpt2:
                # Construct a meaningful error message.
                try:
                    if os.path.exists(sFile1):
                        return (None, 'Error opening "%s": %s' % (sFile1, oXcpt1), None);
                    if not os.path.exists(sFile2):
                        return (None, 'File "%s" not found. [%s, %s]' % (sFilename, sFile1, sFile2,), None);
                    return (None, 'Error opening "%s" inside "%s": %s' % (sFilename, sFile2, oXcpt2), None);
                except Exception as oXcpt3:
                    return (None, 'OMG! %s; %s; %s' % (oXcpt1, oXcpt2, oXcpt3,), None);
        return (None, 'Code not reachable!', None);

    def createFile(self, sFilename, sMode = 'wb'):
        """
        Creates a new file.

        Returns oFile on success.
        Returns sErrorMsg on failure.
        """
        assert sMode in [ 'wb', 'w', 'wU' ];

        # Try raw file first.
        sFile1 = os.path.join(config.g_ksFileAreaRootDir, self.sBaseFilename + '-' + sFilename);
        try:
            if not os.path.exists(os.path.dirname(sFile1)):
                os.makedirs(os.path.dirname(sFile1), 0o755);
            oFile = open(sFile1, sMode);                        # pylint: disable=consider-using-with,unspecified-encoding
        except Exception as oXcpt1:
            return str(oXcpt1);
        return oFile;

    @staticmethod
    def findLogOffsetForTimestamp(sLogContent, tsTimestamp, offStart = 0, fAfter = False):
        """
        Log parsing utility function for finding the offset for the given timestamp.

        We ASSUME the log lines are prefixed with UTC timestamps on the format
        '09:43:55.789353'.

        Return index into the sLogContent string, 0 if not found.
        """
        # Turn tsTimestamp into a string compatible with what we expect to find in the log.
        oTsZulu   = db.dbTimestampToZuluDatetime(tsTimestamp);
        sWantedTs = oTsZulu.strftime('%H:%M:%S.%f');
        assert len(sWantedTs) == 15;

        # Now loop thru the string, line by line.
        offRet  = offStart;
        off     = offStart;
        while True:
            sThisTs = sLogContent[off : off + 15];
            if    len(sThisTs) >= 15 \
              and sThisTs[2]  == ':' \
              and sThisTs[5]  == ':' \
              and sThisTs[8]  == '.' \
              and sThisTs[14] in '0123456789':
                if sThisTs < sWantedTs:
                    offRet = off;
                elif sThisTs == sWantedTs:
                    if not fAfter:
                        return off;
                    offRet = off;
                else:
                    if fAfter:
                        offRet = off;
                    break;

            # next line.
            off = sLogContent.find('\n', off);
            if off < 0:
                if fAfter:
                    offRet = len(sLogContent);
                break;
            off += 1;

        return offRet;

    @staticmethod
    def extractLogSection(sLogContent, tsStart, tsLast):
        """
        Returns log section from tsStart to tsLast (or all if we cannot make sense of it).
        """
        offStart = TestSetData.findLogOffsetForTimestamp(sLogContent, tsStart);
        offEnd   = TestSetData.findLogOffsetForTimestamp(sLogContent, tsLast, offStart, fAfter = True);
        return sLogContent[offStart : offEnd];

    @staticmethod
    def extractLogSectionElapsed(sLogContent, tsStart, tsElapsed):
        """
        Returns log section from tsStart and tsElapsed forward (or all if we cannot make sense of it).
        """
        tsStart = db.dbTimestampToZuluDatetime(tsStart);
        tsLast  = tsStart + tsElapsed;
        return TestSetData.extractLogSection(sLogContent, tsStart, tsLast);



class TestSetLogic(ModelLogicBase):
    """
    TestSet logic.
    """


    def __init__(self, oDb):
        ModelLogicBase.__init__(self, oDb);


    def tryFetch(self, idTestSet):
        """
        Attempts to fetch a test set.

        Returns a TestSetData object on success.
        Returns None if no status was found.
        Raises exception on other errors.
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     TestSets\n'
                          'WHERE    idTestSet = %s\n',
                          (idTestSet,));
        if self._oDb.getRowCount() == 0:
            return None;
        oData = TestSetData();
        return oData.initFromDbRow(self._oDb.fetchOne());

    def strTabString(self, sString, fCommit = False):
        """
        Gets the string table id for the given string, adding it if new.
        """
        ## @todo move this and make a stored procedure for it.
        self._oDb.execute('SELECT   idStr\n'
                          'FROM     TestResultStrTab\n'
                          'WHERE    sValue = %s'
                          , (sString,));
        if self._oDb.getRowCount() == 0:
            self._oDb.execute('INSERT INTO TestResultStrTab (sValue)\n'
                              'VALUES   (%s)\n'
                              'RETURNING idStr\n'
                              , (sString,));
            if fCommit:
                self._oDb.commit();
        return self._oDb.fetchOne()[0];

    def complete(self, idTestSet, sStatus, fCommit = False):
        """
        Completes the testset.
        Returns the test set ID of the gang leader, None if no gang involvement.
        Raises exceptions on database errors and invalid input.
        """

        assert sStatus != TestSetData.ksTestStatus_Running;

        #
        # Get the basic test set data and check if there is anything to do here.
        #
        oData = TestSetData().initFromDbWithId(self._oDb, idTestSet);
        if oData.enmStatus != TestSetData.ksTestStatus_Running:
            raise TMExceptionBase('TestSet %s is already completed as %s.' % (idTestSet, oData.enmStatus));
        if oData.idTestResult is None:
            raise self._oDb.integrityException('idTestResult is NULL for TestSet %u' % (idTestSet,));

        #
        # Close open sub test results, count these as errors.
        # Note! No need to propagate error counts here. Only one tree line will
        #       have open sets, and it will go all the way to the root.
        #
        self._oDb.execute('SELECT idTestResult\n'
                          'FROM   TestResults\n'
                          'WHERE  idTestSet     = %s\n'
                          '   AND enmStatus     = %s\n'
                          '   AND idTestResult <> %s\n'
                          'ORDER BY idTestResult DESC\n'
                          , (idTestSet, TestSetData.ksTestStatus_Running, oData.idTestResult));
        aaoRows = self._oDb.fetchAll();
        if aaoRows:
            idStr = self.strTabString('Unclosed test result', fCommit = fCommit);
            for aoRow in aaoRows:
                self._oDb.execute('UPDATE   TestResults\n'
                                  'SET      enmStatus = \'failure\',\n'
                                  '         tsElapsed = CURRENT_TIMESTAMP - tsCreated,\n'
                                  '         cErrors   = cErrors + 1\n'
                                  'WHERE    idTestResult = %s\n'
                                  , (aoRow[0],));
                self._oDb.execute('INSERT INTO TestResultMsgs (idTestResult, idTestSet, idStrMsg, enmLevel)\n'
                                  'VALUES ( %s, %s, %s, \'failure\'::TestResultMsgLevel_T)\n'
                                  , (aoRow[0], idTestSet, idStr,));

        #
        # If it's a success result, check it against error counters.
        #
        if sStatus not in TestSetData.kasBadTestStatuses:
            self._oDb.execute('SELECT COUNT(*)\n'
                              'FROM   TestResults\n'
                              'WHERE  idTestSet = %s\n'
                              '   AND cErrors > 0\n'
                              , (idTestSet,));
            cErrors = self._oDb.fetchOne()[0];
            if cErrors > 0:
                sStatus = TestSetData.ksTestStatus_Failure;

        #
        # If it's an pure 'failure', check for timeouts and propagate it.
        #
        if sStatus == TestSetData.ksTestStatus_Failure:
            self._oDb.execute('SELECT COUNT(*)\n'
                              'FROM   TestResults\n'
                              'WHERE  idTestSet = %s\n'
                              '   AND enmStatus = %s\n'
                              , ( idTestSet, TestSetData.ksTestStatus_TimedOut, ));
            if self._oDb.fetchOne()[0] > 0:
                sStatus = TestSetData.ksTestStatus_TimedOut;

        #
        # Complete the top level test result and then the test set.
        #
        self._oDb.execute('UPDATE   TestResults\n'
                          'SET      cErrors = (SELECT COALESCE(SUM(cErrors), 0)\n'
                          '                    FROM   TestResults\n'
                          '                    WHERE idTestResultParent = %s)\n'
                          'WHERE    idTestResult = %s\n'
                          'RETURNING cErrors\n'
                          , (oData.idTestResult, oData.idTestResult));
        cErrors = self._oDb.fetchOne()[0];
        if cErrors == 0  and  sStatus in TestSetData.kasBadTestStatuses:
            self._oDb.execute('UPDATE   TestResults\n'
                              'SET      cErrors = 1\n'
                              'WHERE    idTestResult = %s\n'
                              , (oData.idTestResult,));
        elif cErrors > 0  and  sStatus not in TestSetData.kasBadTestStatuses:
            sStatus = TestSetData.ksTestStatus_Failure;  # Impossible.
        self._oDb.execute('UPDATE   TestResults\n'
                          'SET      enmStatus = %s,\n'
                          '         tsElapsed = CURRENT_TIMESTAMP - tsCreated\n'
                          'WHERE    idTestResult = %s\n'
                          , (sStatus, oData.idTestResult,));

        self._oDb.execute('UPDATE   TestSets\n'
                          'SET      enmStatus = %s,\n'
                          '         tsDone    = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestSet = %s\n'
                          , (sStatus, idTestSet,));

        self._oDb.maybeCommit(fCommit);
        return oData.idTestSetGangLeader;

    def completeAsAbandoned(self, idTestSet, fCommit = False):
        """
        Completes the testset as abandoned if necessary.

        See scenario #9:
        file://../../docs/AutomaticTestingRevamp.html#cleaning-up-abandond-testcase

        Returns True if successfully completed as abandond, False if it's already
        completed, and raises exceptions under exceptional circumstances.
        """

        #
        # Get the basic test set data and check if there is anything to do here.
        #
        oData = self.tryFetch(idTestSet);
        if oData is None:
            return False;
        if oData.enmStatus != TestSetData.ksTestStatus_Running:
            return False;

        if oData.idTestResult is not None:
            #
            # Clean up test results, adding a message why they failed.
            #
            self._oDb.execute('UPDATE   TestResults\n'
                              'SET      enmStatus = \'failure\',\n'
                              '         tsElapsed = CURRENT_TIMESTAMP - tsCreated,\n'
                              '         cErrors   = cErrors + 1\n'
                              'WHERE    idTestSet = %s\n'
                              '   AND   enmStatus = \'running\'::TestStatus_T\n'
                              , (idTestSet,));

            idStr = self.strTabString('The test was abandond by the testbox', fCommit = fCommit);
            self._oDb.execute('INSERT INTO TestResultMsgs (idTestResult, idTestSet, idStrMsg, enmLevel)\n'
                              'VALUES ( %s, %s, %s, \'failure\'::TestResultMsgLevel_T)\n'
                              , (oData.idTestResult, idTestSet, idStr,));

        #
        # Complete the testset.
        #
        self._oDb.execute('UPDATE   TestSets\n'
                          'SET      enmStatus = \'failure\',\n'
                          '         tsDone    = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestSet = %s\n'
                          '   AND   enmStatus = \'running\'::TestStatus_T\n'
                          , (idTestSet,));

        self._oDb.maybeCommit(fCommit);
        return True;

    def completeAsGangGatheringTimeout(self, idTestSet, fCommit = False):
        """
        Completes the testset with a gang-gathering timeout.
        Raises exceptions on database errors and invalid input.
        """
        #
        # Get the basic test set data and check if there is anything to do here.
        #
        oData = TestSetData().initFromDbWithId(self._oDb, idTestSet);
        if oData.enmStatus != TestSetData.ksTestStatus_Running:
            raise TMExceptionBase('TestSet %s is already completed as %s.' % (idTestSet, oData.enmStatus));
        if oData.idTestResult is None:
            raise self._oDb.integrityException('idTestResult is NULL for TestSet %u' % (idTestSet,));

        #
        # Complete the top level test result and then the test set.
        #
        self._oDb.execute('UPDATE   TestResults\n'
                          'SET      enmStatus = \'failure\',\n'
                          '         tsElapsed = CURRENT_TIMESTAMP - tsCreated,\n'
                          '         cErrors   = cErrors + 1\n'
                          'WHERE    idTestSet = %s\n'
                          '   AND   enmStatus = \'running\'::TestStatus_T\n'
                          , (idTestSet,));

        idStr = self.strTabString('Gang gathering timed out', fCommit = fCommit);
        self._oDb.execute('INSERT INTO TestResultMsgs (idTestResult, idTestSet, idStrMsg, enmLevel)\n'
                          'VALUES ( %s, %s, %s, \'failure\'::TestResultMsgLevel_T)\n'
                          , (oData.idTestResult, idTestSet, idStr,));

        self._oDb.execute('UPDATE   TestSets\n'
                          'SET      enmStatus = \'failure\',\n'
                          '         tsDone    = CURRENT_TIMESTAMP\n'
                          'WHERE    idTestSet = %s\n'
                          , (idTestSet,));

        self._oDb.maybeCommit(fCommit);
        return True;

    def createFile(self, oTestSet, sName, sMime, sKind, sDesc, cbFile, fCommit = False): # pylint: disable=too-many-locals
        """
        Creates a file and associating with the current test result record in
        the test set.

        Returns file object that the file content can be written to.
        Raises exception on database error, I/O errors, if there are too many
        files in the test set or if they take up too much disk space.

        The caller (testboxdisp.py) is expected to do basic input validation,
        so we skip that and get on with the bits only we can do.
        """

        #
        # Furhter input and limit checks.
        #
        if oTestSet.enmStatus != TestSetData.ksTestStatus_Running:
            raise TMExceptionBase('Cannot create files on a test set with status "%s".' % (oTestSet.enmStatus,));

        self._oDb.execute('SELECT   TestResultStrTab.sValue\n'
                          'FROM     TestResultFiles,\n'
                          '         TestResults,\n'
                          '         TestResultStrTab\n'
                          'WHERE    TestResults.idTestSet        = %s\n'
                          '     AND TestResultFiles.idTestResult = TestResults.idTestResult\n'
                          '     AND TestResultStrTab.idStr       = TestResultFiles.idStrFile\n'
                          , ( oTestSet.idTestSet,));
        if self._oDb.getRowCount() + 1 > config.g_kcMaxUploads:
            raise TMExceptionBase('Uploaded too many files already (%d).' % (self._oDb.getRowCount(),));

        dFiles = {}
        cbTotalFiles = 0;
        for aoRow in self._oDb.fetchAll():
            dFiles[aoRow[0].lower()] = 1; # For determining a unique filename further down.
            sFile = os.path.join(config.g_ksFileAreaRootDir, oTestSet.sBaseFilename + '-' + aoRow[0]);
            try:
                cbTotalFiles += os.path.getsize(sFile);
            except:
                cbTotalFiles += config.g_kcMbMaxUploadSingle * 1048576;
        if (cbTotalFiles + cbFile + 1048575) / 1048576 > config.g_kcMbMaxUploadTotal:
            raise TMExceptionBase('Will exceed total upload limit: %u bytes + %u bytes > %s MiB.' \
                                  % (cbTotalFiles, cbFile, config.g_kcMbMaxUploadTotal));

        #
        # Create a new file.
        #
        self._oDb.execute('SELECT   idTestResult\n'
                          'FROM     TestResults\n'
                          'WHERE    idTestSet = %s\n'
                          '     AND enmStatus = \'running\'::TestStatus_T\n'
                          'ORDER BY idTestResult DESC\n'
                          'LIMIT    1\n'
                          % ( oTestSet.idTestSet, ));
        if self._oDb.getRowCount() < 1:
            raise TMExceptionBase('No open test results - someone committed a capital offence or we ran into a race.');
        idTestResult = self._oDb.fetchOne()[0];

        if sName.lower() in dFiles:
            # Note! There is in theory a race here, but that's something the
            #       test driver doing parallel upload with non-unique names
            #       should worry about. The TD should always avoid this path.
            sOrgName = sName;
            for i in range(2, config.g_kcMaxUploads + 6):
                sName = '%s-%s' % (i, sName,);
                if sName not in dFiles:
                    break;
                sName = None;
            if sName is None:
                raise TMExceptionBase('Failed to find unique name for %s.' % (sOrgName,));

        self._oDb.execute('INSERT INTO TestResultFiles(idTestResult, idTestSet, idStrFile, idStrDescription,\n'
                          '                            idStrKind, idStrMime)\n'
                          'VALUES (%s, %s, %s, %s, %s, %s)\n'
                          , ( idTestResult,
                              oTestSet.idTestSet,
                              self.strTabString(sName),
                              self.strTabString(sDesc),
                              self.strTabString(sKind),
                              self.strTabString(sMime),
                          ));

        oFile = oTestSet.createFile(sName, 'wb');
        if utils.isString(oFile):
            raise TMExceptionBase('Error creating "%s": %s' % (sName, oFile));
        self._oDb.maybeCommit(fCommit);
        return oFile;

    def getGang(self, idTestSetGangLeader):
        """
        Returns an array of TestBoxData object representing the gang for the given testset.
        """
        self._oDb.execute('SELECT   TestBoxesWithStrings.*\n'
                          'FROM     TestBoxesWithStrings,\n'
                          '         TestSets'
                          'WHERE    TestSets.idTestSetGangLeader = %s\n'
                          '     AND TestSets.idGenTestBox        = TestBoxesWithStrings.idGenTestBox\n'
                          'ORDER BY iGangMemberNo ASC\n'
                          , ( idTestSetGangLeader,));
        aaoRows = self._oDb.fetchAll();
        aoTestBoxes = [];
        for aoRow in aaoRows:
            aoTestBoxes.append(TestBoxData().initFromDbRow(aoRow));
        return aoTestBoxes;

    def getFile(self, idTestSet, idTestResultFile):
        """
        Gets the TestResultFileEx corresponding to idTestResultFile.

        Raises an exception if the file wasn't found, doesn't belong to
        idTestSet, and on DB error.
        """
        self._oDb.execute('SELECT   TestResultFiles.*,\n'
                          '         StrTabFile.sValue AS sFile,\n'
                          '         StrTabDesc.sValue AS sDescription,\n'
                          '         StrTabKind.sValue AS sKind,\n'
                          '         StrTabMime.sValue AS sMime\n'
                          'FROM     TestResultFiles,\n'
                          '         TestResultStrTab AS StrTabFile,\n'
                          '         TestResultStrTab AS StrTabDesc,\n'
                          '         TestResultStrTab AS StrTabKind,\n'
                          '         TestResultStrTab AS StrTabMime,\n'
                          '         TestResults\n'
                          'WHERE    TestResultFiles.idTestResultFile = %s\n'
                          '     AND TestResultFiles.idStrFile        = StrTabFile.idStr\n'
                          '     AND TestResultFiles.idStrDescription = StrTabDesc.idStr\n'
                          '     AND TestResultFiles.idStrKind        = StrTabKind.idStr\n'
                          '     AND TestResultFiles.idStrMime        = StrTabMime.idStr\n'
                          '     AND TestResults.idTestResult         = TestResultFiles.idTestResult\n'
                          '     AND TestResults.idTestSet            = %s\n'
                          , ( idTestResultFile, idTestSet, ));
        return TestResultFileDataEx().initFromDbRow(self._oDb.fetchOne());


    def getById(self, idTestSet):
        """
        Get TestSet table record by its id
        """
        self._oDb.execute('SELECT   *\n'
                          'FROM     TestSets\n'
                          'WHERE    idTestSet=%s\n',
                          (idTestSet,))

        aRows = self._oDb.fetchAll()
        if len(aRows) not in (0, 1):
            raise TMTooManyRows('Found more than one test sets with the same credentials. Database structure is corrupted.')
        try:
            return TestSetData().initFromDbRow(aRows[0])
        except IndexError:
            return None


    def fetchOrphaned(self):
        """
        Returns a list of TestSetData objects of orphaned test sets.

        A test set is orphaned if tsDone is NULL and the testbox has created
        one or more newer testsets.
        """

        self._oDb.execute('SELECT   TestSets.*\n'
                          'FROM     TestSets,\n'
                          '         (SELECT idTestSet, idTestBox FROM TestSets WHERE tsDone is NULL) AS t\n'
                          'WHERE    TestSets.idTestSet = t.idTestSet\n'
                          '     AND EXISTS(SELECT 1 FROM TestSets st\n'
                          '                WHERE st.idTestBox = t.idTestBox AND st.idTestSet > t.idTestSet)\n'
                          '     AND NOT EXISTS(SELECT 1 FROM TestBoxStatuses tbs\n'
                          '                    WHERE tbs.idTestBox = t.idTestBox AND tbs.idTestSet = t.idTestSet)\n'
                          'ORDER by TestSets.idTestBox, TestSets.idTestSet'
                          );
        aoRet = [];
        for aoRow in self._oDb.fetchAll():
            aoRet.append(TestSetData().initFromDbRow(aoRow));
        return aoRet;

    def fetchByAge(self, tsNow = None, cHoursBack = 24):
        """
        Returns a list of TestSetData objects of a given time period (default is 24 hours).
        Returns an empty list if no testsets found with given criteria.
        """
        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();

        self._oDb.execute('SELECT *\n'
                          'FROM   TestSets\n'
                          'WHERE  tsDone <= %s\n'
                          '   AND tsDone >  (%s - interval \'%s hours\')\n'
                          '\n'
                        , ( tsNow, tsNow, cHoursBack, ));

        aoRet = [];
        for aoRow in self._oDb.fetchAll():
            aoRet.append(TestSetData().initFromDbRow(aoRow));
        return aoRet;

    def isTestBoxExecutingTooRapidly(self, idTestBox): ## s/To/Too/
        """
        Checks whether the specified test box is executing tests too rapidly.

        The parameters defining too rapid execution are defined in config.py.

        Returns True if it does, False if it doesn't.
        May raise database problems.
        """

        self._oDb.execute('(\n'
                          'SELECT   tsCreated\n'
                          'FROM     TestSets\n'
                          'WHERE    idTestBox = %s\n'
                          '     AND tsCreated >= (CURRENT_TIMESTAMP - interval \'%s seconds\')\n'
                          ') UNION (\n'
                          'SELECT   tsCreated\n'
                          'FROM     TestSets\n'
                          'WHERE    idTestBox = %s\n'
                          '     AND tsCreated >= (CURRENT_TIMESTAMP - interval \'%s seconds\')\n'
                          '     AND enmStatus >= \'failure\'\n'
                          ')'
                          , ( idTestBox, config.g_kcSecMinSinceLastTask,
                              idTestBox, config.g_kcSecMinSinceLastFailedTask, ));
        return self._oDb.getRowCount() > 0;


    #
    # The virtual test sheriff interface.
    #

    def fetchBadTestBoxIds(self, cHoursBack = 2, tsNow = None, aidFailureReasons = None):
        """
        Fetches a list of test box IDs which returned bad-testbox statuses in the
        given period (tsDone).
        """
        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();
        if aidFailureReasons is None:
            aidFailureReasons = [ -1, ];
        self._oDb.execute('(SELECT idTestBox\n'
                          ' FROM   TestSets\n'
                          ' WHERE  TestSets.enmStatus = \'bad-testbox\'\n'
                          '    AND tsDone           <= %s\n'
                          '    AND tsDone            > (%s - interval \'%s hours\')\n'
                          ') UNION (\n'
                          ' SELECT TestSets.idTestBox\n'
                          '   FROM TestSets,\n'
                          '        TestResultFailures\n'
                          '  WHERE TestSets.tsDone                   <= %s\n'
                          '    AND TestSets.tsDone                   >  (%s - interval \'%s hours\')\n'
                          '    AND TestSets.enmStatus                >= \'failure\'::TestStatus_T\n'
                          '    AND TestSets.idTestSet                 = TestResultFailures.idTestSet\n'
                          '    AND TestResultFailures.tsExpire        = \'infinity\'::TIMESTAMP\n'
                          '    AND TestResultFailures.idFailureReason IN ('
                          + ', '.join([str(i) for i in aidFailureReasons]) + ')\n'
                          ')\n'
                          , ( tsNow, tsNow, cHoursBack,
                              tsNow, tsNow, cHoursBack, ));
        return [aoRow[0] for aoRow in self._oDb.fetchAll()];

    def fetchSetsForTestBox(self, idTestBox, cHoursBack = 2, tsNow = None):
        """
        Fetches the TestSet rows for idTestBox for the given period (tsDone), w/o running ones.

        Returns list of TestSetData sorted by tsDone in descending order.
        """
        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();
        self._oDb.execute('SELECT *\n'
                          'FROM   TestSets\n'
                          'WHERE  TestSets.idTestBox = %s\n'
                          '   AND tsDone IS NOT NULL\n'
                          '   AND tsDone           <= %s\n'
                          '   AND tsDone            > (%s - interval \'%s hours\')\n'
                          'ORDER by tsDone DESC\n'
                          , ( idTestBox, tsNow, tsNow, cHoursBack,));
        return self._dbRowsToModelDataList(TestSetData);

    def fetchFailedSetsWithoutReason(self, cHoursBack = 2, tsNow = None):
        """
        Fetches the TestSet failure rows without any currently (CURRENT_TIMESTAMP
        not tsNow) assigned failure reason.

        Returns list of TestSetData sorted by tsDone in descending order.

        Note! Includes bad-testbox sets too as it can be useful to analyze these
              too even if we normally count them in the 'skipped' category.
        """
        if tsNow is None:
            tsNow = self._oDb.getCurrentTimestamp();
        self._oDb.execute('SELECT TestSets.*\n'
                          'FROM   TestSets\n'
                          '       LEFT OUTER JOIN TestResultFailures\n'
                          '                    ON TestResultFailures.idTestSet = TestSets.idTestSet\n'
                          '                   AND TestResultFailures.tsExpire  = \'infinity\'::TIMESTAMP\n'
                          'WHERE  TestSets.tsDone IS NOT NULL\n'
                          '   AND TestSets.enmStatus        IN ( %s, %s, %s, %s )\n'
                          '   AND TestSets.tsDone           <= %s\n'
                          '   AND TestSets.tsDone            > (%s - interval \'%s hours\')\n'
                          '   AND TestResultFailures.idTestSet IS NULL\n'
                          'ORDER by tsDone DESC\n'
                          , ( TestSetData.ksTestStatus_Failure, TestSetData.ksTestStatus_TimedOut,
                              TestSetData.ksTestStatus_Rebooted, TestSetData.ksTestStatus_BadTestBox,
                              tsNow,
                              tsNow, cHoursBack,));
        return self._dbRowsToModelDataList(TestSetData);



#
# Unit testing.
#

# pylint: disable=missing-docstring
class TestSetDataTestCase(ModelDataBaseTestCase):
    def setUp(self):
        self.aoSamples = [TestSetData(),];

if __name__ == '__main__':
    unittest.main();
    # not reached.
