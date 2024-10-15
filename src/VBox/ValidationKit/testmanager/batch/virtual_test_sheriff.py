#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: virtual_test_sheriff.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=line-too-long

"""
Virtual Test Sheriff.

Duties:
    - Try to a assign failure reasons to recently failed tests.
    - Reboot or disable bad test boxes.

"""

from __future__ import print_function;

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


# Standard python imports
import hashlib;
import os;
import re;
import smtplib;
#import subprocess;
import sys;
from email.mime.multipart   import MIMEMultipart;
from email.mime.text        import MIMEText;
from email.utils            import COMMASPACE;

if sys.version_info[0] >= 3:
    from io       import BytesIO as BytesIO;        # pylint: disable=import-error,no-name-in-module,useless-import-alias
else:
    from StringIO import StringIO as BytesIO;       # pylint: disable=import-error,no-name-in-module,useless-import-alias
from optparse import OptionParser;                  # pylint: disable=deprecated-module
from PIL import Image;                              # pylint: disable=import-error

# Add Test Manager's modules path
g_ksTestManagerDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksTestManagerDir);

# Test Manager imports
from common                                 import utils;
from testmanager.core.db                    import TMDatabaseConnection;
from testmanager.core.build                 import BuildDataEx;
from testmanager.core.failurereason         import FailureReasonLogic;
from testmanager.core.testbox               import TestBoxLogic, TestBoxData;
from testmanager.core.testcase              import TestCaseDataEx;
from testmanager.core.testgroup             import TestGroupData;
from testmanager.core.testset               import TestSetLogic, TestSetData;
from testmanager.core.testresults           import TestResultLogic, TestResultFileData;
from testmanager.core.testresultfailures    import TestResultFailureLogic, TestResultFailureData;
from testmanager.core.useraccount           import UserAccountLogic;
from testmanager.config                     import g_ksSmtpHost, g_kcSmtpPort, g_ksAlertFrom, \
                                                   g_ksAlertSubject, g_asAlertList #, g_ksLomPassword;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name


class VirtualTestSheriffCaseFile(object):
    """
    A failure investigation case file.

    """


    ## Max log file we'll read into memory. (256 MB)
    kcbMaxLogRead = 0x10000000;

    def __init__(self, oSheriff, oTestSet, oTree, oBuild, oTestBox, oTestGroup, oTestCase):
        self.oSheriff       = oSheriff;
        self.oTestSet       = oTestSet;     # TestSetData
        self.oTree          = oTree;        # TestResultDataEx
        self.oBuild         = oBuild;       # BuildDataEx
        self.oTestBox       = oTestBox;     # TestBoxData
        self.oTestGroup     = oTestGroup;   # TestGroupData
        self.oTestCase      = oTestCase;    # TestCaseDataEx
        self.sMainLog       = '';           # The main log file.  Empty string if not accessible.
        self.sSvcLog        = '';           # The VBoxSVC log file.  Empty string if not accessible.

        # Generate a case file name.
        self.sName          = '#%u: %s' % (self.oTestSet.idTestSet, self.oTestCase.sName,)
        self.sLongName      = '#%u: "%s" on "%s" running %s %s (%s), "%s" by %s, using %s %s %s r%u'  \
                            % ( self.oTestSet.idTestSet,
                                self.oTestCase.sName,
                                self.oTestBox.sName,
                                self.oTestBox.sOs,
                                self.oTestBox.sOsVersion,
                                self.oTestBox.sCpuArch,
                                self.oTestBox.sCpuName,
                                self.oTestBox.sCpuVendor,
                                self.oBuild.oCat.sProduct,
                                self.oBuild.oCat.sBranch,
                                self.oBuild.oCat.sType,
                                self.oBuild.iRevision, );

        # Investigation notes.
        self.tReason                = None; # None or one of the ktReason_XXX constants.
        self.dReasonForResultId     = {};   # Reason assignments indexed by idTestResult.
        self.dCommentForResultId    = {};   # Comment assignments indexed by idTestResult.

    #
    # Reason.
    #

    def noteReason(self, tReason):
        """ Notes down a possible reason. """
        self.oSheriff.dprint(u'noteReason: %s -> %s' % (self.tReason, tReason,));
        self.tReason = tReason;
        return True;

    def noteReasonForId(self, tReason, idTestResult, sComment = None):
        """ Notes down a possible reason for a specific test result. """
        self.oSheriff.dprint(u'noteReasonForId: %u: %s -> %s%s'
                             % (idTestResult, self.dReasonForResultId.get(idTestResult, None), tReason,
                                (u' (%s)' % (sComment,)) if sComment is not None else ''));
        self.dReasonForResultId[idTestResult] = tReason;
        if sComment is not None:
            self.dCommentForResultId[idTestResult] = sComment;
        return True;


    #
    # Test classification.
    #

    def isVBoxTest(self):
        """ Test classification: VirtualBox (using the build) """
        return self.oBuild.oCat.sProduct.lower() in [ 'virtualbox', 'vbox' ];

    def isVBoxUnitTest(self):
        """ Test case classification: The unit test doing all our testcase/*.cpp stuff. """
        return self.isVBoxTest() \
           and (self.oTestCase.sName.lower() == 'unit tests' or self.oTestCase.sName.lower().startswith('misc: unit tests'));

    def isVBoxInstallTest(self):
        """ Test case classification: VirtualBox Guest installation test. """
        return self.isVBoxTest() \
           and self.oTestCase.sName.lower().startswith('install:');

    def isVBoxUnattendedInstallTest(self):
        """ Test case classification: VirtualBox Guest installation test. """
        return self.isVBoxTest() \
           and self.oTestCase.sName.lower().startswith('uinstall:');

    def isVBoxUSBTest(self):
        """ Test case classification: VirtualBox USB test. """
        return self.isVBoxTest() \
           and self.oTestCase.sName.lower().startswith('usb:');

    def isVBoxStorageTest(self):
        """ Test case classification: VirtualBox Storage test. """
        return self.isVBoxTest() \
           and self.oTestCase.sName.lower().startswith('storage:');

    def isVBoxGAsTest(self):
        """ Test case classification: VirtualBox Guest Additions test. """
        return self.isVBoxTest() \
           and (   self.oTestCase.sName.lower().startswith('guest additions')
                or self.oTestCase.sName.lower().startswith('ga\'s tests'));

    def isVBoxAPITest(self):
        """ Test case classification: VirtualBox API test. """
        return self.isVBoxTest() \
           and self.oTestCase.sName.lower().startswith('api:');

    def isVBoxBenchmarkTest(self):
        """ Test case classification: VirtualBox Benchmark test. """
        return self.isVBoxTest() \
           and self.oTestCase.sName.lower().startswith('benchmark:');

    def isVBoxSmokeTest(self):
        """ Test case classification: Smoke test. """
        return self.isVBoxTest() \
           and self.oTestCase.sName.lower().startswith('smoketest');

    def isVBoxSerialTest(self):
        """ Test case classification: Smoke test. """
        return self.isVBoxTest() \
           and self.oTestCase.sName.lower().startswith('serial:');


    #
    # Utility methods.
    #

    def getMainLog(self):
        """
        Tries to read the main log file since this will be the first source of information.
        """
        if self.sMainLog:
            return self.sMainLog;
        (oFile, oSizeOrError, _) = self.oTestSet.openFile('main.log', 'rb');
        if oFile is not None:
            try:
                self.sMainLog = oFile.read(min(self.kcbMaxLogRead, oSizeOrError)).decode('utf-8', 'replace');
            except Exception as oXcpt:
                self.oSheriff.vprint(u'Error reading main log file: %s' % (oXcpt,))
                self.sMainLog = '';
        else:
            self.oSheriff.vprint(u'Error opening main log file: %s' % (oSizeOrError,));
        return self.sMainLog;

    def getLogFile(self, oFile):
        """
        Tries to read the given file as a utf-8 log file.
        oFile is a TestFileDataEx instance.
        Returns empty string if problems opening or reading the file.
        """
        sContent = '';
        (oFile, oSizeOrError, _) = self.oTestSet.openFile(oFile.sFile, 'rb');
        if oFile is not None:
            try:
                sContent = oFile.read(min(self.kcbMaxLogRead, oSizeOrError)).decode('utf-8', 'replace');
            except Exception as oXcpt:
                self.oSheriff.vprint(u'Error reading the "%s" log file: %s' % (oFile.sFile, oXcpt,))
        else:
            self.oSheriff.vprint(u'Error opening the "%s" log file: %s' % (oFile.sFile, oSizeOrError,));
        return sContent;

    def getSvcLog(self):
        """
        Tries to read the VBoxSVC log file as it typically not associated with a failing test result.
        Note! Returns the first VBoxSVC log file we find.
        """
        if not self.sSvcLog:
            aoSvcLogFiles = self.oTree.getListOfLogFilesByKind(TestResultFileData.ksKind_LogReleaseSvc);
            if aoSvcLogFiles:
                self.sSvcLog = self.getLogFile(aoSvcLogFiles[0]);
        return self.sSvcLog;

    def getScreenshotSha256(self, oFile):
        """
        Tries to read the given screenshot file, uncompress it, and do SHA-2
        on the raw pixels.
        Returns SHA-2 digest string on success, None on failure.
        """
        (oImgFile, _, _) = self.oTestSet.openFile(oFile.sFile, 'rb');
        try:
            abImageFile = oImgFile.read();
        except Exception as oXcpt:
            self.oSheriff.vprint(u'Error reading the "%s" image file: %s' % (oFile.sFile, oXcpt,))
        else:
            try:
                oImage = Image.open(BytesIO(abImageFile));
            except Exception as oXcpt:
                self.oSheriff.vprint(u'Error opening the "%s" image bytes using PIL.Image.open: %s' % (oFile.sFile, oXcpt,))
            else:
                try:
                    oHash = hashlib.sha256();
                    if hasattr(oImage, 'tobytes'):
                        oHash.update(oImage.tobytes());
                    else:
                        oHash.update(oImage.tostring()); # pylint: disable=no-member
                except Exception as oXcpt:
                    self.oSheriff.vprint(u'Error hashing the uncompressed image bytes for "%s": %s' % (oFile.sFile, oXcpt,))
                else:
                    return oHash.hexdigest();
        return None;



    def isSingleTestFailure(self):
        """
        Figure out if this is a single test failing or if it's one of the
        more complicated ones.
        """
        if self.oTree.cErrors == 1:
            return True;
        if self.oTree.deepCountErrorContributers() <= 1:
            return True;
        return False;



class VirtualTestSheriff(object): # pylint: disable=too-few-public-methods
    """
    Add build info into Test Manager database.
    """

    ## The user account for the virtual sheriff.
    ksLoginName = 'vsheriff';

    def __init__(self):
        """
        Parse command line.
        """
        self.oDb                     = None;
        self.tsNow                   = None;
        self.oTestResultLogic        = None;
        self.oTestSetLogic           = None;
        self.oFailureReasonLogic     = None;    # FailureReasonLogic;
        self.oTestResultFailureLogic = None;    # TestResultFailureLogic
        self.oLogin                  = None;
        self.uidSelf                 = -1;
        self.oLogFile                = None;
        self.asBsodReasons           = [];
        self.asUnitTestReasons       = [];

        oParser = OptionParser();
        oParser.add_option('--start-hours-ago', dest = 'cStartHoursAgo', metavar = '<hours>', default = 0, type = 'int',
                           help = 'When to start specified as hours relative to current time.  Defauls is right now.', );
        oParser.add_option('--hours-period', dest = 'cHoursBack', metavar = '<period-in-hours>', default = 2, type = 'int',
                           help = 'Work period specified in hours.  Defauls is 2 hours.');
        oParser.add_option('--real-run-back', dest = 'fRealRun', action = 'store_true', default = False,
                           help = 'Whether to commit the findings to the database. Default is a dry run.');
        oParser.add_option('--testset', dest = 'aidTestSets', metavar = '<id>', default = [], type = 'int', action = 'append',
                           help = 'Only investigate this one.  Accumulates IDs when repeated.');
        oParser.add_option('-q', '--quiet', dest = 'fQuiet', action = 'store_true', default = False,
                           help = 'Quiet execution');
        oParser.add_option('-l', '--log', dest = 'sLogFile', metavar = '<logfile>', default = None,
                           help = 'Where to log messages.');
        oParser.add_option('--debug', dest = 'fDebug', action = 'store_true', default = False,
                           help = 'Enables debug mode.');

        (self.oConfig, _) = oParser.parse_args();

        if self.oConfig.sLogFile:
            self.oLogFile = open(self.oConfig.sLogFile, "a");   # pylint: disable=consider-using-with,unspecified-encoding
            self.oLogFile.write('VirtualTestSheriff: $Revision: 106320 $ \n');


    def eprint(self, sText):
        """
        Prints error messages.
        Returns 1 (for exit code usage.)
        """
        print('error: %s' % (sText,));
        if self.oLogFile is not None:
            if sys.version_info[0] >= 3:
                self.oLogFile.write(u'error: %s\n' % (sText,));
            else:
                self.oLogFile.write((u'error: %s\n' % (sText,)).encode('utf-8'));
        return 1;

    def dprint(self, sText):
        """
        Prints debug info.
        """
        if self.oConfig.fDebug:
            if not self.oConfig.fQuiet:
                print('debug: %s' % (sText, ));
            if self.oLogFile is not None:
                if sys.version_info[0] >= 3:
                    self.oLogFile.write(u'debug: %s\n' % (sText,));
                else:
                    self.oLogFile.write((u'debug: %s\n' % (sText,)).encode('utf-8'));
        return 0;

    def vprint(self, sText):
        """
        Prints verbose info.
        """
        if not self.oConfig.fQuiet:
            print('info: %s' % (sText,));
        if self.oLogFile is not None:
            if sys.version_info[0] >= 3:
                self.oLogFile.write(u'info: %s\n' % (sText,));
            else:
                self.oLogFile.write((u'info: %s\n' % (sText,)).encode('utf-8'));
        return 0;

    def getFailureReason(self, tReason):
        """ Gets the failure reason object for tReason. """
        return self.oFailureReasonLogic.cachedLookupByNameAndCategory(tReason[1], tReason[0]);

    def selfCheck(self):
        """ Does some self checks, looking up things we expect to be in the database and such. """
        rcExit = 0;
        for sAttr in dir(self.__class__):
            if sAttr.startswith('ktReason_'):
                tReason = getattr(self.__class__, sAttr);
                oFailureReason = self.getFailureReason(tReason);
                if oFailureReason is None:
                    rcExit = self.eprint(u'Failed to find failure reason "%s" in category "%s" in the database!'
                                         % (tReason[1], tReason[0],));

        # Check the user account as well.
        if self.oLogin is None:
            oLogin = UserAccountLogic(self.oDb).tryFetchAccountByLoginName(VirtualTestSheriff.ksLoginName);
            if oLogin is None:
                rcExit = self.eprint(u'Cannot find my user account "%s"!' % (VirtualTestSheriff.ksLoginName,));
        return rcExit;

    def sendEmailAlert(self, uidAuthor, sBodyText):
        """
        Sends email alert.
        """

        # Get author email
        self.oDb.execute('SELECT sEmail FROM Users WHERE uid=%s', (uidAuthor,));
        sFrom = self.oDb.fetchOne();
        if sFrom is not None:
            sFrom = sFrom[0];
        else:
            sFrom = g_ksAlertFrom;

        # Gather recipient list.
        asEmailList = [];
        for sUser in g_asAlertList:
            self.oDb.execute('SELECT sEmail FROM Users WHERE sUsername=%s', (sUser,));
            sEmail = self.oDb.fetchOne();
            if sEmail:
                asEmailList.append(sEmail[0]);
        if not asEmailList:
            return self.eprint('No email addresses to send alter to!');

        # Compose the message.
        oMsg = MIMEMultipart();
        oMsg['From'] = sFrom;
        oMsg['To'] = COMMASPACE.join(asEmailList);
        oMsg['Subject'] = g_ksAlertSubject;
        oMsg.attach(MIMEText(sBodyText, 'plain'))

        # Try send it.
        try:
            oSMTP = smtplib.SMTP(g_ksSmtpHost, g_kcSmtpPort);
            oSMTP.sendmail(sFrom, asEmailList, oMsg.as_string())
            oSMTP.quit()
        except smtplib.SMTPException as oXcpt:
            return self.eprint('Failed to send mail: %s' % (oXcpt,));

        return 0;

    def badTestBoxManagement(self):
        """
        Looks for bad test boxes and first tries once to reboot them then disables them.
        """
        rcExit = 0;

        #
        # We skip this entirely if we're running in the past and not in harmless debug mode.
        #
        if    self.oConfig.cStartHoursAgo != 0 \
          and (not self.oConfig.fDebug or self.oConfig.fRealRun):
            return rcExit;
        tsNow      = self.tsNow              if self.oConfig.fDebug else None;
        cHoursBack = self.oConfig.cHoursBack if self.oConfig.fDebug else 2;
        oTestBoxLogic = TestBoxLogic(self.oDb);

        #
        # Generate a list of failures reasons we consider bad-testbox behavior.
        #
        aidFailureReasons = [
            self.getFailureReason(self.ktReason_Host_DriverNotLoaded).idFailureReason,
            self.getFailureReason(self.ktReason_Host_DriverNotUnloading).idFailureReason,
            self.getFailureReason(self.ktReason_Host_DriverNotCompilable).idFailureReason,
            self.getFailureReason(self.ktReason_Host_InstallationFailed).idFailureReason,
        ];

        #
        # Get list of bad test boxes for given period and check them out individually.
        #
        aidBadTestBoxes = self.oTestSetLogic.fetchBadTestBoxIds(cHoursBack = cHoursBack, tsNow = tsNow,
                                                                aidFailureReasons = aidFailureReasons);
        for idTestBox in aidBadTestBoxes:
            # Skip if the testbox is already disabled or has a pending reboot command.
            try:
                oTestBox = TestBoxData().initFromDbWithId(self.oDb, idTestBox);
            except Exception as oXcpt:
                rcExit = self.eprint('Failed to get data for test box #%u in badTestBoxManagement: %s' % (idTestBox, oXcpt,));
                continue;
            if not oTestBox.fEnabled:
                self.dprint(u'badTestBoxManagement: Skipping test box #%u (%s) as it has been disabled already.'
                            % ( idTestBox, oTestBox.sName, ));
                continue;
            if oTestBox.enmPendingCmd != TestBoxData.ksTestBoxCmd_None:
                self.dprint(u'badTestBoxManagement: Skipping test box #%u (%s) as it has a command pending: %s'
                            % ( idTestBox, oTestBox.sName, oTestBox.enmPendingCmd));
                continue;

            # Get the most recent testsets for this box (descending on tsDone) and see how bad it is.
            aoSets  = self.oTestSetLogic.fetchSetsForTestBox(idTestBox, cHoursBack = cHoursBack, tsNow = tsNow);
            cOkay      = 0;
            cBad       = 0;
            iFirstOkay = len(aoSets);
            for iSet, oSet in enumerate(aoSets):
                if oSet.enmStatus == TestSetData.ksTestStatus_BadTestBox:
                    cBad += 1;
                else:
                    # Check for bad failure reasons.
                    oFailure = None;
                    if oSet.enmStatus in TestSetData.kasBadTestStatuses:
                        (oTree, _ ) = self.oTestResultLogic.fetchResultTree(oSet.idTestSet)
                        aoFailedResults = oTree.getListOfFailures();
                        for oFailedResult in aoFailedResults:
                            oFailure = self.oTestResultFailureLogic.getById(oFailedResult.idTestResult);
                            if oFailure is not None and oFailure.idFailureReason in aidFailureReasons:
                                break;
                            oFailure = None;
                    if oFailure is not None:
                        cBad += 1;
                    else:
                        # This is an okay test result then.
                        ## @todo maybe check the elapsed time here, it could still be a bad run?
                        cOkay += 1;
                        iFirstOkay = min(iFirstOkay, iSet);
                if iSet > 10:
                    break;

            # We react if there are two or more bad-testbox statuses at the head of the
            # history and at least three in the last 10 results.
            if iFirstOkay >= 2 and cBad > 2:
                if oTestBoxLogic.hasTestBoxRecentlyBeenRebooted(idTestBox, cHoursBack = cHoursBack, tsNow = tsNow):
                    sComment = u'Disabling testbox #%u (%s) - iFirstOkay=%u cBad=%u cOkay=%u' \
                             % (idTestBox, oTestBox.sName, iFirstOkay, cBad, cOkay);
                    self.vprint(sComment);
                    self.sendEmailAlert(self.uidSelf, sComment);
                    if self.oConfig.fRealRun is True:
                        try:
                            oTestBoxLogic.disableTestBox(idTestBox, self.uidSelf, fCommit = True,
                                                         sComment = 'Automatically disabled (iFirstOkay=%u cBad=%u cOkay=%u)'
                                                                  % (iFirstOkay, cBad, cOkay),);
                        except Exception as oXcpt:
                            rcExit = self.eprint(u'Error disabling testbox #%u (%u): %s\n' % (idTestBox, oTestBox.sName, oXcpt,));
                else:
                    sComment = u'Rebooting testbox #%u (%s) - iFirstOkay=%u cBad=%u cOkay=%u' \
                             % (idTestBox, oTestBox.sName, iFirstOkay, cBad, cOkay);
                    self.vprint(sComment);
                    self.sendEmailAlert(self.uidSelf, sComment);
                    if self.oConfig.fRealRun is True:
                        try:
                            oTestBoxLogic.rebootTestBox(idTestBox, self.uidSelf, fCommit = True,
                                                        sComment = 'Automatically rebooted (iFirstOkay=%u cBad=%u cOkay=%u)'
                                                                 % (iFirstOkay, cBad, cOkay),);
                        except Exception as oXcpt:
                            rcExit = self.eprint(u'Error rebooting testbox #%u (%s): %s\n' % (idTestBox, oTestBox.sName, oXcpt,));
            else:
                self.dprint(u'badTestBoxManagement: #%u (%s) looks ok:  iFirstOkay=%u cBad=%u cOkay=%u'
                            % ( idTestBox, oTestBox.sName, iFirstOkay, cBad, cOkay));

        ## @todo r=bird: review + rewrite;
        ##  - no selecting here, that belongs in the core/*.py files.
        ##  - preserve existing comments.
        ##  - doing way too much in the try/except block.
        ##  - No password quoting in the sshpass command that always fails (127).
        ##  - Timeout is way to low. testboxmem1 need more than 10 min to take a dump, ages to
        ##    get thru POST and another 5 just to time out in grub.  Should be an hour or so.
        ##    Besides, it need to be constant elsewhere in the file, not a variable here.
        ##
        ##
        ## Reset hanged testboxes
        ##
        #cStatusTimeoutMins = 10;
        #
        #self.oDb.execute('SELECT TestBoxStatuses.idTestBox\n'
        #                 '  FROM TestBoxStatuses, TestBoxes\n'
        #                 ' WHERE TestBoxStatuses.tsUpdated >= (CURRENT_TIMESTAMP - interval \'%s hours\')\n'
        #                 '   AND TestBoxStatuses.tsUpdated < (CURRENT_TIMESTAMP - interval \'%s minutes\')\n'
        #                 '   AND TestBoxStatuses.idTestBox = TestBoxes.idTestBox\n'
        #                 '   AND Testboxes.tsExpire = \'infinity\'::timestamp', (cHoursBack,cStatusTimeoutMins));
        #for idTestBox in self.oDb.fetchAll():
        #    idTestBox = idTestBox[0];
        #    try:
        #        oTestBox = TestBoxData().initFromDbWithId(self.oDb, idTestBox);
        #    except Exception as oXcpt:
        #        rcExit = self.eprint('Failed to get data for test box #%u in badTestBoxManagement: %s' % (idTestBox, oXcpt,));
        #        continue;
        #    # Skip if the testbox is already disabled, already reset or there's no iLOM
        #    if not oTestBox.fEnabled or oTestBox.ipLom is None or oTestBox.sComment is not None and oTestBox.sComment.find('Automatically reset') >= 0:
        #        self.dprint(u'badTestBoxManagement: Skipping test box #%u (%s) as it has been disabled already.'
        #                    % ( idTestBox, oTestBox.sName, ));
        #        continue;
        #    ## @todo get iLOM credentials from a table?
        #    sCmd = 'sshpass -p%s ssh -oStrictHostKeyChecking=no root@%s show /SP && reset /SYS' % (g_ksLomPassword, oTestBox.ipLom,);
        #    try:
        #        oPs = subprocess.Popen(sCmd, stdout=subprocess.PIPE, shell=True);
        #        sStdout = oPs.communicate()[0];
        #        iRC = oPs.wait();
        #
        #        oTestBox.sComment = 'Automatically reset (iRC=%u sStdout=%s)' % (iRC, sStdout,);
        #        oTestBoxLogic.editEntry(oTestBox, self.uidSelf, fCommit = True);
        #
        #        sComment = u'Reset testbox #%u (%s) - iRC=%u sStduot=%s' % ( idTestBox, oTestBox.sName, iRC, sStdout);
        #        self.vprint(sComment);
        #        self.sendEmailAlert(self.uidSelf, sComment);
        #
        #    except Exception as oXcpt:
        #        rcExit = self.eprint(u'Error resetting testbox #%u (%s): %s\n' % (idTestBox, oTestBox.sName, oXcpt,));
        #
        return rcExit;


    ## @name Failure reasons we know.
    ## @{

    ktReason_Add_Installer_Win_Failed                  = ( 'Additions',         'Win GA install' );
    ktReason_Add_ShFl                                  = ( 'Additions',         'ShFl' );
    ktReason_Add_ShFl_Automount                        = ( 'Additions',         'Automounting' );
    ktReason_Add_ShFl_FsPerf                           = ( 'Additions',         'FsPerf' );
    ktReason_Add_ShFl_FsPerf_Abend                     = ( 'Additions',         'FsPerf abend' );
    ktReason_Add_GstCtl                                = ( 'Additions',         'GstCtl' );
    ktReason_Add_GstCtl_Preparations                   = ( 'Additions',         'GstCtl preparations' );
    ktReason_Add_GstCtl_SessionBasics                  = ( 'Additions',         'Session basics' );
    ktReason_Add_GstCtl_SessionProcRefs                = ( 'Additions',         'Session process' );
    ktReason_Add_GstCtl_Session_Reboot                 = ( 'Additions',         'Session reboot' );
    ktReason_Add_GstCtl_CopyFromGuest_Timeout          = ( 'Additions',         'CopyFromGuest timeout' );
    ktReason_Add_GstCtl_CopyToGuest_Timeout            = ( 'Additions',         'CopyToGuest timeout' );
    ktReason_Add_GstCtl_CopyToGuest_DstEmpty           = ( 'Additions',         'CopyToGuest dst empty' );
    ktReason_Add_GstCtl_CopyToGuest_DstExists          = ( 'Additions',         'CopyToGuest dst exists' );
    ktReason_Add_FlushViewOfFile                       = ( 'Additions',         'FlushViewOfFile' );
    ktReason_Add_Mmap_Coherency                        = ( 'Additions',         'mmap coherency' );
    ktReason_BSOD_Recovery                             = ( 'BSOD',              'Recovery' );
    ktReason_BSOD_Automatic_Repair                     = ( 'BSOD',              'Automatic Repair' );
    ktReason_BSOD_0000007F                             = ( 'BSOD',              '0x0000007F' );
    ktReason_BSOD_000000D1                             = ( 'BSOD',              '0x000000D1' );
    ktReason_BSOD_C0000225                             = ( 'BSOD',              '0xC0000225 (boot)' );
    ktReason_Guru_Generic                              = ( 'Guru Meditations',  'Generic Guru Meditation' );
    ktReason_Guru_VERR_IEM_INSTR_NOT_IMPLEMENTED       = ( 'Guru Meditations',  'VERR_IEM_INSTR_NOT_IMPLEMENTED' );
    ktReason_Guru_VERR_IEM_ASPECT_NOT_IMPLEMENTED      = ( 'Guru Meditations',  'VERR_IEM_ASPECT_NOT_IMPLEMENTED' );
    ktReason_Guru_VERR_TRPM_DONT_PANIC                 = ( 'Guru Meditations',  'VERR_TRPM_DONT_PANIC' );
    ktReason_Guru_VERR_PGM_PHYS_PAGE_RESERVED          = ( 'Guru Meditations',  'VERR_PGM_PHYS_PAGE_RESERVED' );
    ktReason_Guru_VERR_VMX_INVALID_GUEST_STATE         = ( 'Guru Meditations',  'VERR_VMX_INVALID_GUEST_STATE' );
    ktReason_Guru_VINF_EM_TRIPLE_FAULT                 = ( 'Guru Meditations',  'VINF_EM_TRIPLE_FAULT' );
    ktReason_Host_HostMemoryLow                        = ( 'Host',              'HostMemoryLow' );
    ktReason_Host_DriverNotLoaded                      = ( 'Host',              'Driver not loaded' );
    ktReason_Host_DriverNotUnloading                   = ( 'Host',              'Driver not unloading' );
    ktReason_Host_DriverNotCompilable                  = ( 'Host',              'Driver not compilable' );
    ktReason_Host_InstallationFailed                   = ( 'Host',              'Installation failed' );
    ktReason_Host_InstallationWantReboot               = ( 'Host',              'Installation want reboot' );
    ktReason_Host_InvalidPackage                       = ( 'Host',              'ERROR_INSTALL_PACKAGE_INVALID' );
    ktReason_Host_InstallSourceAbsent                  = ( 'Host',              'ERROR_INSTALL_SOURCE_ABSENT' );
    ktReason_Host_Install_Hang                         = ( 'Host',              'Install hang' );
    ktReason_Host_NotSignedWithBuildCert               = ( 'Host',              'Not signed with build cert' );
    ktReason_Host_DiskFull                             = ( 'Host',              'Host disk full' );
    ktReason_Host_DoubleFreeHeap                       = ( 'Host',              'Double free or corruption' );
    ktReason_Host_LeftoverService                      = ( 'Host',              'Leftover service' );
    ktReason_Host_win32com_gen_py_not_found            = ( 'Host',              'win32com.gen_py not found' );
    ktReason_Host_Reboot_OSX_Watchdog_Timeout          = ( 'Host Reboot',       'OSX Watchdog Timeout' );
    ktReason_Host_Modprobe_Failed                      = ( 'Host',              'Modprobe failed' );
    ktReason_Host_NetworkMisconfiguration              = ( 'Host',              'Network misconfiguration' );
    ktReason_Host_Python_vboxapi_not_found             = ( 'Host',              'Python API (vboxapi) not found' );
    ktReason_Host_Python_xpcom_not_found               = ( 'Host',              'Python API (xpcom) not found' );
    ktReason_Host_TSTInfo_Accuracy_OOR                 = ( 'Host',              'TSTInfo accuracy out of range' );
    ktReason_Host_UninstallationFailed                 = ( 'Host',              'Uninstallation failed' );
    ktReason_Networking_Nonexistent_host_nic           = ( 'Networking',        'Nonexistent host networking interface' );
    ktReason_Networking_VERR_INTNET_FLT_IF_NOT_FOUND   = ( 'Networking',        'VERR_INTNET_FLT_IF_NOT_FOUND' );
    ktReason_OSInstall_GRUB_hang                       = ( 'O/S Install',       'GRUB hang' );
    ktReason_OSInstall_Udev_hang                       = ( 'O/S Install',       'udev hang' );
    ktReason_OSInstall_Sata_no_BM                      = ( 'O/S Install',       'SATA busmaster bit not set' );
    ktReason_Panic_BootManagerC000000F                 = ( 'Panic',             'Hardware Changed' );
    ktReason_Panic_MP_BIOS_IO_APIC                     = ( 'Panic',             'MP-BIOS/IO-APIC' );
    ktReason_Panic_HugeMemory                          = ( 'Panic',             'Huge memory assertion' );
    ktReason_Panic_IOAPICDoesntWork                    = ( 'Panic',             'IO-APIC and timer does not work' );
    ktReason_Panic_TxUnitHang                          = ( 'Panic',             'Tx Unit Hang' );
    ktReason_API_std_bad_alloc                         = ( 'API / (XP)COM',     'std::bad_alloc' );
    ktReason_API_Digest_Mismatch                       = ( 'API / (XP)COM',     'Digest mismatch' );
    ktReason_API_MoveVM_SharingViolation               = ( 'API / (XP)COM',     'MoveVM sharing violation' );
    ktReason_API_MoveVM_InvalidParameter               = ( 'API / (XP)COM',     'MoveVM invalid parameter' );
    ktReason_API_Open_Session_Failed                   = ( 'API / (XP)COM',     'Open session failed' );
    ktReason_XPCOM_Exit_Minus_11                       = ( 'API / (XP)COM',     'exit -11' );
    ktReason_XPCOM_VBoxSVC_Hang                        = ( 'API / (XP)COM',     'VBoxSVC hang' );
    ktReason_XPCOM_VBoxSVC_Hang_Plus_Heap_Corruption   = ( 'API / (XP)COM',     'VBoxSVC hang + heap corruption' );
    ktReason_XPCOM_NS_ERROR_CALL_FAILED                = ( 'API / (XP)COM',     'NS_ERROR_CALL_FAILED' );
    ktReason_BootManager_Image_corrupt                 = ( 'Unknown',           'BOOTMGR Image corrupt' );
    ktReason_Unknown_Heap_Corruption                   = ( 'Unknown',           'Heap corruption' );
    ktReason_Unknown_Reboot_Loop                       = ( 'Unknown',           'Reboot loop' );
    ktReason_Unknown_File_Not_Found                    = ( 'Unknown',           'File not found' );
    ktReason_Unknown_HalReturnToFirmware               = ( 'Unknown',           'HalReturnToFirmware' );
    ktReason_Unknown_VM_Crash                          = ( 'Unknown',           'VM crash' );
    ktReason_Unknown_VM_Terminated                     = ( 'Unknown',           'VM terminated' );
    ktReason_Unknown_VM_Start_Error                    = ( 'Unknown',           'VM Start Error' );
    ktReason_Unknown_VM_Runtime_Error                  = ( 'Unknown',           'VM Runtime Error' );
    ktReason_VMM_Assert                                = ( 'VMM',               'Assert' );
    ktReason_VMM_kvm_lock_spinning                     = ( 'VMM',               'kvm_lock_spinning' );
    ktReason_Ignore_Buggy_Test_Driver                  = ( 'Ignore',            'Buggy test driver' );
    ktReason_Ignore_Stale_Files                        = ( 'Ignore',            'Stale files' );
    ktReason_Buggy_Build_Broken_Build                  = ( 'Broken Build',      'Buggy build' );
    ktReason_GuestBug_CompizVBoxQt                     = ( 'Guest Bug',         'Compiz + VirtualBox Qt GUI crash' );
    ## @}

    ## BSOD category.
    ksBsodCategory      = 'BSOD';
    ## Special reason indicating that the flesh and blood sheriff has work to do.
    ksBsodAddNew        = 'Add new BSOD';

    ## Unit test category.
    ksUnitTestCategory  = 'Unit';
    ## Special reason indicating that the flesh and blood sheriff has work to do.
    ksUnitTestAddNew    = 'Add new';

    ## Used for indica that we shouldn't report anything for this test result ID and
    ## consider promoting the previous error to test set level if it's the only one.
    ktHarmless = ( 'Probably', 'Caused by previous error' );


    def caseClosed(self, oCaseFile):
        """
        Reports the findings in the case and closes it.
        """
        #
        # Log it and create a dReasonForReasultId we can use below.
        #
        dCommentForResultId = oCaseFile.dCommentForResultId;
        if oCaseFile.dReasonForResultId:
            # Must weed out ktHarmless.
            dReasonForResultId = {};
            for idKey, tReason in oCaseFile.dReasonForResultId.items():
                if tReason is not self.ktHarmless:
                    dReasonForResultId[idKey] = tReason;
            if not dReasonForResultId:
                self.vprint(u'TODO: Closing %s without a real reason, only %s.'
                            % (oCaseFile.sName, oCaseFile.dReasonForResultId));
                return False;

            # Try promote to single reason.
            atValues = dReasonForResultId.values();
            fSingleReason = True;
            if len(dReasonForResultId) == 1 and next(iter(dReasonForResultId.keys())) != oCaseFile.oTestSet.idTestResult:
                self.dprint(u'Promoting single reason to whole set: %s' % (next(iter(atValues)),));
            elif len(dReasonForResultId) > 1 and len(atValues) == list(atValues).count(next(iter(atValues))):
                self.dprint(u'Merged %d reasons to a single one: %s' % (len(atValues), next(iter(atValues))));
            else:
                fSingleReason = False;
            if fSingleReason:
                dReasonForResultId = { oCaseFile.oTestSet.idTestResult: next(iter(atValues)), };
                if dCommentForResultId:
                    dCommentForResultId = { oCaseFile.oTestSet.idTestResult: next(iter(dCommentForResultId.values())), };
        elif oCaseFile.tReason is not None:
            dReasonForResultId = { oCaseFile.oTestSet.idTestResult: oCaseFile.tReason, };
        else:
            self.vprint(u'Closing %s without a reason - this should not happen!' % (oCaseFile.sName,));
            return False;

        self.vprint(u'Closing %s with following reason%s: %s'
                    % ( oCaseFile.sName, 's' if len(dReasonForResultId) > 1 else '', dReasonForResultId, ));

        #
        # Add the test failure reason record(s).
        #
        for idTestResult, tReason in dReasonForResultId.items():
            oFailureReason = self.getFailureReason(tReason);
            if oFailureReason is not None:
                sComment = 'Set by $Revision: 106320 $' # Handy for reverting later.
                if idTestResult in dCommentForResultId:
                    sComment += ': ' + dCommentForResultId[idTestResult];

                oAdd = TestResultFailureData();
                oAdd.initFromValues(idTestResult    = idTestResult,
                                    idFailureReason = oFailureReason.idFailureReason,
                                    uidAuthor       = self.uidSelf,
                                    idTestSet       = oCaseFile.oTestSet.idTestSet,
                                    sComment        = sComment,);
                if self.oConfig.fRealRun:
                    try:
                        self.oTestResultFailureLogic.addEntry(oAdd, self.uidSelf, fCommit = True);
                    except Exception as oXcpt:
                        self.eprint(u'caseClosed: Exception "%s" while adding reason %s for %s'
                                    % (oXcpt, oAdd, oCaseFile.sLongName,));
            else:
                self.eprint(u'caseClosed: Cannot locate failure reason: %s / %s' % ( tReason[0], tReason[1],));
        return True;

    #
    # Tools for assiting log parsing.
    #

    @staticmethod
    def matchFollowedByLines(sStr, off, asFollowingLines):
        """ Worker for isThisFollowedByTheseLines. """

        # Advance off to the end of the line.
        off = sStr.find('\n', off);
        if off < 0:
            return False;
        off += 1;

        # Match each string with the subsequent lines.
        for iLine, sLine in enumerate(asFollowingLines):
            offEnd = sStr.find('\n', off);
            if offEnd < 0:
                return  iLine + 1 == len(asFollowingLines) and sStr.find(sLine, off) < 0;
            if sLine and sStr.find(sLine, off, offEnd) < 0:
                return False;

            # next line.
            off = offEnd + 1;

        return True;

    @staticmethod
    def isThisFollowedByTheseLines(sStr, sFirst, asFollowingLines):
        """
        Looks for a line contining sFirst which is then followed by lines
        with the strings in asFollowingLines.  (No newline chars anywhere!)
        Returns True / False.
        """
        off = sStr.find(sFirst, 0);
        while off >= 0:
            if VirtualTestSheriff.matchFollowedByLines(sStr, off, asFollowingLines):
                return True;
            off = sStr.find(sFirst, off + 1);
        return False;

    @staticmethod
    def findAndReturnRestOfLine(sHaystack, sNeedle):
        """
        Looks for sNeedle in sHaystack.
        Returns The text following the needle up to the end of the line.
        Returns None if not found.
        """
        if sHaystack is None:
            return None;
        off = sHaystack.find(sNeedle);
        if off < 0:
            return None;
        off += len(sNeedle)
        offEol = sHaystack.find('\n', off);
        if offEol < 0:
            offEol = len(sHaystack);
        return sHaystack[off:offEol]

    @staticmethod
    def findInAnyAndReturnRestOfLine(asHaystacks, sNeedle):
        """
        Looks for sNeedle in zeroe or more haystacks (asHaystack).
        Returns The text following the first needed found up to the end of the line.
        Returns None if not found.
        """
        for sHaystack in asHaystacks:
            sRet = VirtualTestSheriff.findAndReturnRestOfLine(sHaystack, sNeedle);
            if sRet is not None:
                return sRet;
        return None;


    #
    # The investigative units.
    #

    katSimpleInstallUninstallMainLogReasons = [
        # ( Whether to stop on hit, reason tuple, needle text. )
        ( False, ktReason_Host_LeftoverService,
          'SERVICE_NAME: vbox' ),
        ( False, ktReason_Host_LeftoverService,
          'Seems installation was skipped. Old version lurking behind? Not the fault of this build/test run!'),
    ];

    kdatSimpleInstallUninstallMainLogReasonsPerOs = {
        'darwin': [
            # ( Whether to stop on hit, reason tuple, needle text. )
            ( True, ktReason_Host_DriverNotUnloading,
              'Can\'t remove kext org.virtualbox.kext.VBoxDrv; services failed to terminate - 0xe00002c7' ),
        ],
        'linux': [
            # ( Whether to stop on hit, reason tuple, needle text. )
            ( True, ktReason_Host_DriverNotCompilable,
              'This system is not currently set up to build kernel modules' ),
            ( True, ktReason_Host_DriverNotCompilable,
              'This system is currently not set up to build kernel modules' ),
            ( True, ktReason_Host_InstallationFailed,
              'vboxdrv.sh: failed: Look at /var/log/vbox-install.log to find out what went wrong.' ),
            ( True, ktReason_Host_DriverNotUnloading,
              'Cannot unload module vboxdrv'),
        ],
        'solaris': [
            # ( Whether to stop on hit, reason tuple, needle text. )
            ( True, ktReason_Host_DriverNotUnloading, 'can\'t unload the module: Device busy' ),
            ( True, ktReason_Host_DriverNotUnloading, 'Unloading: Host module ...FAILED!' ),
            ( True, ktReason_Host_DriverNotUnloading, 'Unloading: NetFilter (Crossbow) module ...FAILED!' ),
            ( True, ktReason_Host_InstallationFailed, 'svcadm: Couldn\'t bind to svc.configd.' ),
            ( True, ktReason_Host_InstallationFailed, 'pkgadd: ERROR: postinstall script did not complete successfully' ),
        ],
        'win': [
            # ( Whether to stop on hit, reason tuple, needle text. )
            ( True,  ktReason_Host_InstallationWantReboot, 'ERROR_SUCCESS_REBOOT_REQUIRED' ),
            ( False, ktReason_Host_InstallationFailed, 'Installation error.' ),
            ( True,  ktReason_Host_InvalidPackage, 'Uninstaller failed, exit code: 1620' ),
            ( True,  ktReason_Host_InstallSourceAbsent, 'Uninstaller failed, exit code: 1612' ),
        ],
    };


    def investigateInstallUninstallFailure(self, oCaseFile, oFailedResult, sResultLog, fInstall):
        """
        Investigates an install or uninstall failure.

        We lump the two together since the installation typically also performs
        an uninstall first and will be seeing similar issues to the uninstall.
        """
        self.dprint(u'%s + %s <<\n%s\n<<' % (oFailedResult.tsCreated, oFailedResult.tsElapsed, sResultLog,));

        if fInstall and oFailedResult.enmStatus == TestSetData.ksTestStatus_TimedOut:
            oCaseFile.noteReasonForId(self.ktReason_Host_Install_Hang, oFailedResult.idTestResult)
            return True;

        atSimple = self.katSimpleInstallUninstallMainLogReasons;
        if oCaseFile.oTestBox.sOs in self.kdatSimpleInstallUninstallMainLogReasonsPerOs:
            atSimple = self.kdatSimpleInstallUninstallMainLogReasonsPerOs[oCaseFile.oTestBox.sOs] + atSimple;

        fFoundSomething = False;
        for fStopOnHit, tReason, sNeedle in atSimple:
            if sResultLog.find(sNeedle) > 0:
                oCaseFile.noteReasonForId(tReason, oFailedResult.idTestResult);
                if fStopOnHit:
                    return True;
                fFoundSomething = True;

        # If we didn't find something specific, just add a general install/uninstall
        # failure reason so it's easier to get an idea why a test failed when
        # looking at the failure listing in the test manager.
        if not fFoundSomething:
            if fInstall:
                oCaseFile.noteReasonForId(self.ktReason_Host_InstallationFailed, oFailedResult.idTestResult);
            else:
                oCaseFile.noteReasonForId(self.ktReason_Host_UninstallationFailed, oFailedResult.idTestResult);

        return fFoundSomething if fFoundSomething else None;


    def investigateBadTestBox(self, oCaseFile):
        """
        Checks out bad-testbox statuses.
        """
        _ = oCaseFile;
        return False;


    def investigateVBoxUnitTest(self, oCaseFile):
        """
        Checks out a VBox unittest problem.
        """

        # Determine if this is a host or guest run before we start.
        fRunsInGuest = '(guest)' in oCaseFile.oTestCase.sName or 'selected VMs' in oCaseFile.oTestCase.sName;

        #
        # Process simple test case failures first, using their name as reason.
        # We do the reason management just like for BSODs.
        #
        cRelevantOnes   = 0;
        sMainLog        = oCaseFile.getMainLog();
        aoFailedResults = oCaseFile.oTree.getListOfFailures();
        for oFailedResult in aoFailedResults:
            if oFailedResult is oCaseFile.oTree:
                self.vprint('TODO: toplevel failure');
                cRelevantOnes += 1

            elif oFailedResult.sName == 'Installing VirtualBox':
                sResultLog = TestSetData.extractLogSectionElapsed(sMainLog, oFailedResult.tsCreated, oFailedResult.tsElapsed);
                self.investigateInstallUninstallFailure(oCaseFile, oFailedResult, sResultLog, fInstall = True)
                cRelevantOnes += 1

            elif oFailedResult.sName == 'Uninstalling VirtualBox':
                sResultLog = TestSetData.extractLogSectionElapsed(sMainLog, oFailedResult.tsCreated, oFailedResult.tsElapsed);
                self.investigateInstallUninstallFailure(oCaseFile, oFailedResult, sResultLog, fInstall = False)
                cRelevantOnes += 1

            elif oFailedResult.oParent is not None:
                # Host:  Get the 2nd level node because that's where we'll find the unit test name.
                # Guest: Get the 6th level node.
                aoParents = [];
                oParent = oFailedResult.oParent;
                while oParent is not None:
                    aoParents.insert(0, oParent);
                    oParent = oParent.oParent;
                if not fRunsInGuest:
                    oFailedResult = aoParents[min(2, len(aoParents) - 1)];
                else:
                    oFailedResult = aoParents[min(5, len(aoParents) - 1)];

                # Only report a failure once.
                if oFailedResult.idTestResult not in oCaseFile.dReasonForResultId:
                    sKey = oFailedResult.sName;
                    if sKey.startswith('testcase/'):
                        sKey = sKey[9:];
                    if sKey in self.asUnitTestReasons:
                        tReason = ( self.ksUnitTestCategory, sKey );
                        oCaseFile.noteReasonForId(tReason, oFailedResult.idTestResult);
                    else:
                        self.dprint(u'Unit test failure "%s" not found in %s;' % (sKey, self.asUnitTestReasons));
                        sResultLog = TestSetData.extractLogSectionElapsed(sMainLog, oFailedResult.tsCreated,
                                                                          oFailedResult.tsElapsed);
                        if 'AudioMixer.cpp' in sResultLog:  # Pipe drain assertion.
                            tReason = self.ktReason_VMM_Assert;
                            oCaseFile.noteReasonForId(tReason, oFailedResult.idTestResult, sComment = 'AudioMixer.cpp');
                        else:
                            tReason = ( self.ksUnitTestCategory, self.ksUnitTestAddNew );
                            oCaseFile.noteReasonForId(tReason, oFailedResult.idTestResult, sComment = sKey);
                    cRelevantOnes += 1
            else:
                self.vprint(u'Internal error: expected oParent to NOT be None for %s' % (oFailedResult,));

        #
        # If we've caught all the relevant ones by now, report the result.
        #
        if len(oCaseFile.dReasonForResultId) >= cRelevantOnes:
            return self.caseClosed(oCaseFile);
        return False;

    def extractGuestCpuStack(self, sInfoText):
        """
        Extracts the guest CPU stacks from the input file.

        Returns a dictionary keyed by the CPU number, value being a list of
        raw stack lines (no header).
        Returns empty dictionary if no stacks where found.
        """
        dRet = {};
        off = 0;
        while True:
            # Find the stack.
            offStart = sInfoText.find('=== start guest stack VCPU ', off);
            if offStart < 0:
                break;
            offEnd  = sInfoText.find('=== end guest stack', offStart + 20);
            if offEnd >= 0:
                offEnd += 3;
            else:
                offEnd = sInfoText.find('=== start guest stack VCPU', offStart + 20);
                if offEnd < 0:
                    offEnd = len(sInfoText);

            sStack = sInfoText[offStart : offEnd];
            sStack = sStack.replace('\r',''); # paranoia
            asLines = sStack.split('\n');

            # Figure the CPU.
            asWords = asLines[0].split();
            if len(asWords) < 6 or not asWords[5].isdigit():
                break;
            iCpu = int(asWords[5]);

            # Add it and advance.
            dRet[iCpu] = [sLine.rstrip() for sLine in asLines[2:-1]]
            off = offEnd;
        return dRet;

    def investigateInfoKvmLockSpinning(self, oCaseFile, sInfoText, dLogs):
        """ Investigates kvm_lock_spinning deadlocks """
        #
        # Extract the stacks.  We need more than one CPU to create a deadlock.
        #
        dStacks = self.extractGuestCpuStack(sInfoText);
        self.dprint('kvm_lock_spinning: found %s stacks' % (len(dStacks),));
        if len(dStacks) >= 2:
            #
            # Examin each of the stacks.  Each must have kvm_lock_spinning in
            # one of the first three entries.
            #
            cHits = 0;
            for asBacktrace in dStacks.values():
                for iFrame in xrange(min(3, len(asBacktrace))):
                    if asBacktrace[iFrame].find('kvm_lock_spinning') >= 0:
                        cHits += 1;
                        break;
            self.dprint('kvm_lock_spinning: %s/%s hits' % (cHits, len(dStacks),));
            if cHits == len(dStacks):
                return (True, self.ktReason_VMM_kvm_lock_spinning);

        _ = dLogs; _ = oCaseFile;
        return (False, None);

    def investigateInfoHalReturnToFirmware(self, oCaseFile, sInfoText, dLogs):
        """ Investigates HalReturnToFirmware hangs """
        del oCaseFile
        del sInfoText
        del dLogs
        # hope that's sufficient
        return (True, self.ktReason_Unknown_HalReturnToFirmware);

    ## Things we search a main or VM log for to figure out why something went bust.
    ## @note DO NOT ADD MORE STUFF HERE!
    ##       Please use katSimpleMainLogReasons and katSimpleVmLogReasons instead!
    katSimpleMainAndVmLogReasonsDeprecated = [
        # ( Whether to stop on hit, reason tuple, needle text. )
        ( False, ktReason_Guru_Generic,                             'GuruMeditation' ),
        ( False, ktReason_Guru_Generic,                             'Guru Meditation' ),
        ( True,  ktReason_Guru_VERR_IEM_INSTR_NOT_IMPLEMENTED,      'VERR_IEM_INSTR_NOT_IMPLEMENTED' ),
        ( True,  ktReason_Guru_VERR_IEM_ASPECT_NOT_IMPLEMENTED,     'VERR_IEM_ASPECT_NOT_IMPLEMENTED' ),
        ( True,  ktReason_Guru_VERR_TRPM_DONT_PANIC,                'VERR_TRPM_DONT_PANIC' ),
        ( True,  ktReason_Guru_VERR_PGM_PHYS_PAGE_RESERVED,         'VERR_PGM_PHYS_PAGE_RESERVED' ),
        ( True,  ktReason_Guru_VERR_VMX_INVALID_GUEST_STATE,        'VERR_VMX_INVALID_GUEST_STATE' ),
        ( True,  ktReason_Guru_VINF_EM_TRIPLE_FAULT,                'VINF_EM_TRIPLE_FAULT' ),
        ( True,  ktReason_Networking_Nonexistent_host_nic,
          'rc=E_FAIL text="Nonexistent host networking interface, name \'eth0\' (VERR_INTERNAL_ERROR)"' ),
        ( True,  ktReason_Networking_VERR_INTNET_FLT_IF_NOT_FOUND,
          'Failed to attach the network LUN (VERR_INTNET_FLT_IF_NOT_FOUND)' ),
        ( True,  ktReason_Host_Reboot_OSX_Watchdog_Timeout,         ': "OSX Watchdog Timeout: ' ),
        ( False, ktReason_XPCOM_NS_ERROR_CALL_FAILED,
          'Exception: 0x800706be (Call to remote object failed (NS_ERROR_CALL_FAILED))' ),
        ( True,  ktReason_API_std_bad_alloc,                        'Unexpected exception: std::bad_alloc' ),
        ( True,  ktReason_Host_HostMemoryLow,                       'HostMemoryLow' ),
        ( True,  ktReason_Host_HostMemoryLow,                       'Failed to procure handy pages; rc=VERR_NO_MEMORY' ),
        ( True,  ktReason_Unknown_File_Not_Found,
          'Error: failed to start machine. Error message: File not found. (VERR_FILE_NOT_FOUND)' ),
        ( True,  ktReason_Unknown_File_Not_Found, # lump it in with file-not-found for now.
          'Error: failed to start machine. Error message: Not supported. (VERR_NOT_SUPPORTED)' ),
        ( False, ktReason_Unknown_VM_Crash,                         'txsDoConnectViaTcp: Machine state: Aborted' ),
        ( True,  ktReason_Host_Modprobe_Failed,                     'Kernel driver not installed' ),
        ( True,  ktReason_OSInstall_Sata_no_BM,                     'PCHS=14128/14134/8224' ),
        ( True,  ktReason_Host_DoubleFreeHeap,                      'double free or corruption' ),
        #( False, ktReason_Unknown_VM_Start_Error,                   'VMSetError: ' ), - false positives for stuff like:
        #           "VMSetError: VD: Backend 'VBoxIsoMaker' does not support async I/O"
        ( False, ktReason_Unknown_VM_Start_Error,                   'error: failed to open session for' ),
        ( False, ktReason_Unknown_VM_Runtime_Error,                 'Console: VM runtime error: fatal=true' ),
    ];

    ## This we search a main log for to figure out why something went bust.
    katSimpleMainLogReasons = [
        # ( Whether to stop on hit, reason tuple, needle text. )
        ( False, ktReason_Host_win32com_gen_py_not_found,           'ModuleNotFoundError: No module named \'win32com.gen_py' ),
        ( False, ktReason_Host_Python_vboxapi_not_found,            'ImportError: No module named vboxapi' ),
        ( False, ktReason_Host_Python_xpcom_not_found,              'ImportError: No module named xpcom' ),
    ];

    ## This we search a VM log  for to figure out why something went bust.
    katSimpleVmLogReasons = [
        # ( Whether to stop on hit, reason tuple, needle text. )
        # Note: Works for ATA and VD drivers.
        ( False, ktReason_Host_DiskFull,                            '_DISKFULL' ),
    ];

    ## Things we search a VBoxHardening.log file for to figure out why something went bust.
    katSimpleVBoxHardeningLogReasons = [
        # ( Whether to stop on hit, reason tuple, needle text. )
        ( True,  ktReason_Host_DriverNotLoaded,                     'Error opening VBoxDrvStub:  STATUS_OBJECT_NAME_NOT_FOUND' ),
        ( True,  ktReason_Host_NotSignedWithBuildCert,              'Not signed with the build certificate' ),
        ( True,  ktReason_Host_TSTInfo_Accuracy_OOR,                'RTCRTSPTSTINFO::Accuracy::Millis: Out of range' ),
        ( False, ktReason_Unknown_VM_Crash,                         'Quitting: ExitCode=0xc0000005 (rcNtWait=' ),
    ];

    ## Things we search a kernel.log file for to figure out why something went bust.
    katSimpleKernelLogReasons = [
        # (  Whether to stop on hit, reason tuple, needle text. )
        ( True,  ktReason_Panic_HugeMemory,                         'mm/huge_memory.c:1988' ),
        ( True,  ktReason_Panic_IOAPICDoesntWork,                   'IO-APIC + timer doesn\'t work' ),
        ( True,  ktReason_Panic_TxUnitHang,                         'Detected Tx Unit Hang' ),
        ( True,  ktReason_GuestBug_CompizVBoxQt,                    'error 4 in libQt5CoreVBox' ),
        ( True,  ktReason_GuestBug_CompizVBoxQt,                    'error 4 in libgtk-3' ),
    ];

    ## Things we search the _RIGHT_ _STRIPPED_ vgatext for.
    katSimpleVgaTextReasons = [
        # ( Whether to stop on hit, reason tuple, needle text. )
        ( True,  ktReason_Panic_MP_BIOS_IO_APIC,
          "..MP-BIOS bug: 8254 timer not connected to IO-APIC\n\n" ),
        ( True,  ktReason_Panic_MP_BIOS_IO_APIC,
          "..MP-BIOS bug: 8254 timer not connected to IO-APIC\n"
          "...trying to set up timer (IRQ0) through the 8259A ...  failed.\n"
          "...trying to set up timer as Virtual Wire IRQ... failed.\n"
          "...trying to set up timer as ExtINT IRQ... failed :(.\n"
          "Kernel panic - not syncing: IO-APIC + timer doesn't work!  Boot with apic=debug\n"
          "and send a report.  Then try booting with the 'noapic' option\n"
          "\n" ),
        ( True,  ktReason_OSInstall_GRUB_hang,
          "-----\nGRUB Loading stage2..\n\n\n\n" ),
        ( True,  ktReason_OSInstall_GRUB_hang,
          "-----\nGRUB Loading stage2...\n\n\n\n" ), # the 3 dot hang appears to be less frequent
        ( True,  ktReason_OSInstall_GRUB_hang,
          "-----\nGRUB Loading stage2....\n\n\n\n" ), # the 4 dot hang appears to be very infrequent
        ( True,  ktReason_OSInstall_GRUB_hang,
          "-----\nGRUB Loading stage2.....\n\n\n\n" ), # the 5 dot hang appears to be more frequent again
        ( True,  ktReason_OSInstall_Udev_hang,
          "\nStarting udev:\n\n\n\n" ),
        ( True,  ktReason_OSInstall_Udev_hang,
          "\nStarting udev:\n------" ),
        ( True,  ktReason_Panic_BootManagerC000000F,
          "Windows failed to start. A recent hardware or software change might be the" ),
        ( True,  ktReason_BootManager_Image_corrupt,
          "BOOTMGR image is corrupt.  The system cannot boot." ),
    ];

    ## Things we search for in the info.txt file.  Require handlers for now.
    katInfoTextHandlers = [
        # ( Trigger text,                       handler method )
        ( "kvm_lock_spinning",                  investigateInfoKvmLockSpinning ),
        ( "HalReturnToFirmware",                investigateInfoHalReturnToFirmware ),
    ];

    ## Mapping screenshot/failure SHA-256 hashes to failure reasons.
    katSimpleScreenshotHashReasons = [
        # ( Whether to stop on hit, reason tuple, lowercased sha-256 of PIL.Image.tostring output )
        ( True,  ktReason_BSOD_Recovery,                    '576f8e38d62b311cac7e3dc3436a0d0b9bd8cfd7fa9c43aafa95631520a45eac' ),
        ( True,  ktReason_BSOD_Automatic_Repair,            'c6a72076cc619937a7a39cfe9915b36d94cee0d4e3ce5ce061485792dcee2749' ),
        ( True,  ktReason_BSOD_Automatic_Repair,            '26c4d8a724ff2c5e1051f3d5b650dbda7b5fdee0aa3e3c6059797f7484a515df' ),
        ( True,  ktReason_BSOD_0000007F,                    '57e1880619e13042a87100e7a38c8974b85ce3866501be621bea0cc696bb2c63' ),
        ( True,  ktReason_BSOD_000000D1,                    '134621281f00a3f8aeeb7660064bffbf6187ed56d5852142328d0bcb18ef0ede' ),
        ( True,  ktReason_BSOD_000000D1,                    '279f11258150c9d2fef041eca65501f3141da8df39256d8f6377e897e3b45a93' ),
        ( True,  ktReason_BSOD_C0000225,                    'bd13a144be9dcdfb16bc863ff4c8f02a86e263c174f2cd5ffd27ca5f3aa31789' ),
        ( True,  ktReason_BSOD_C0000225,                    '8348b465e7ee9e59dd4e785880c57fd8677de05d11ac21e786bfde935307b42f' ),
        ( True,  ktReason_BSOD_C0000225,                    '1316e1fc818a73348412788e6910b8c016f237d8b4e15b20caf4a866f7a7840e' ),
        ( True,  ktReason_BSOD_C0000225,                    '54e0acbff365ce20a85abbe42bcd53647b8b9e80c68e45b2cd30e86bf177a0b5' ),
        ( True,  ktReason_BSOD_C0000225,                    '50fec50b5199923fa48b3f3e782687cc381e1c8a788ebda14e6a355fbe3bb1b3' ),
    ];


    def scanLog(self, asLogs, atNeedles, oCaseFile, idTestResult):
        """
        Scans for atNeedles in sLog.

        Returns True if a stop-on-hit neelde was found.
        Returns None if a no-stop reason was found.
        Returns False if no hit.
        """
        fRet = False;
        for fStopOnHit, tReason, oNeedle in atNeedles:
            fMatch = False;
            if utils.isString(oNeedle):
                for sLog in asLogs:
                    if sLog:
                        fMatch |= sLog.find(oNeedle) > 0;
            else:
                for sLog in asLogs:
                    if sLog:
                        fMatch |= oNeedle.search(sLog) is not None;
            if fMatch:
                oCaseFile.noteReasonForId(tReason, idTestResult);
                if fStopOnHit:
                    return True;
                fRet = None;
        return fRet;


    ## Fallback reasons based on GA test groups.
    kdGATestFallbacks = {
        'Guest Control':  ktReason_Add_GstCtl,
        'Shared Folders': ktReason_Add_ShFl,
    };

    def investigateGATest(self, oCaseFile, oFailedResult, sResultLog):
        """
        Investigates a failed VM run.
        """
        enmReason = None;
        sParentName = oFailedResult.oParent.sName if oFailedResult.oParent else '';
        if oFailedResult.sName == 'VBoxWindowsAdditions.exe' or sResultLog.find('VBoxWindowsAdditions.exe" failed with') > 0:
            enmReason = self.ktReason_Add_Installer_Win_Failed;
        # guest control:
        elif sParentName == 'Guest Control' and oFailedResult.sName == 'Preparations':
            enmReason = self.ktReason_Add_GstCtl_Preparations;
        elif oFailedResult.sName == 'Session Basics':
            enmReason = self.ktReason_Add_GstCtl_SessionBasics;
        elif oFailedResult.sName == 'Session Process References':
            enmReason = self.ktReason_Add_GstCtl_SessionProcRefs;
        elif oFailedResult.sName == 'Copy from guest':
            if sResultLog.find('*** abort action ***') >= 0:
                enmReason = self.ktReason_Add_GstCtl_CopyFromGuest_Timeout;
        elif oFailedResult.sName == 'Copy to guest':
            off = sResultLog.find('"Guest directory "');
            if off > 0 and sResultLog.find('" already exists"', off, off + 80):
                enmReason = self.ktReason_Add_GstCtl_CopyToGuest_DstExists;
            elif sResultLog.find('Guest destination must not be empty') >= 0:
                enmReason = self.ktReason_Add_GstCtl_CopyToGuest_DstEmpty;
            elif sResultLog.find('*** abort action ***') >= 0:
                enmReason = self.ktReason_Add_GstCtl_CopyToGuest_Timeout;
        elif oFailedResult.sName.find('Session w/ Guest Reboot') >= 0:
            enmReason = self.ktReason_Add_GstCtl_Session_Reboot;
        # shared folders:
        elif sParentName == 'Shared Folders' and oFailedResult.sName == 'Automounting':
            enmReason = self.ktReason_Add_ShFl_Automount;
        elif oFailedResult.sName == 'mmap':
            if sResultLog.find('FsPerf: Flush issue at offset ') >= 0:
                enmReason = self.ktReason_Add_Mmap_Coherency;
            elif sResultLog.find('FlushViewOfFile') >= 0:
                enmReason = self.ktReason_Add_FlushViewOfFile;
        elif sParentName == 'Shared Folders' and oFailedResult.sName == 'Running FsPerf':
            enmReason = self.ktReason_Add_ShFl_FsPerf;  ## Maybe it would be better to be more specific...

        if enmReason is not None:
            return oCaseFile.noteReasonForId(enmReason, oFailedResult.idTestResult);

        # Generalistic fallbacks:
        for sKey in utils.iteritems(self.kdGATestFallbacks):
            oTmpFailedResult = oFailedResult;
            while oTmpFailedResult:
                if oTmpFailedResult.sName == sKey:
                    return oCaseFile.noteReasonForId(self.kdGATestFallbacks[sKey], oFailedResult.idTestResult);
                oTmpFailedResult = oTmpFailedResult.oParent;

        self.vprint(u'TODO: Cannot place GA failure idTestResult=%u - %s' % (oFailedResult.idTestResult, oFailedResult.sName,));
        self.dprint(u'%s + %s <<\n%s\n<<' % (oFailedResult.tsCreated, oFailedResult.tsElapsed, sResultLog,));
        return False;

    def isResultFromGATest(self, oCaseFile, oFailedResult):
        """
        Checks if this result and corresponding log snippet looks like a GA test run.
        """
        while oFailedResult is not None:
            if oFailedResult.sName in [ 'Guest Control', 'Shared Folders', 'FsPerf', 'VBoxWindowsAdditions.exe' ]:
                return True;
            if oCaseFile.oTestCase.sName == 'Guest Additions' and oFailedResult.sName in [ 'Install', ]:
                return True;
            oFailedResult = oFailedResult.oParent;
        return False;


    def investigateVMResult(self, oCaseFile, oFailedResult, sResultLog):
        """
        Investigates a failed VM run.
        """

        def investigateLogSet():
            """
            Investigates the current set of VM related logs.
            """
            self.dprint('investigateLogSet: log lengths: result %u, VM %u, kernel %u, vga text %u, info text %u, hard %u'
                        % ( len(sResultLog if sResultLog else ''),
                            len(sVMLog     if sVMLog else ''),
                            len(sKrnlLog   if sKrnlLog else ''),
                            len(sVgaText   if sVgaText else ''),
                            len(sInfoText  if sInfoText else ''),
                            len(sNtHardLog if sNtHardLog else ''),));

            #self.dprint(u'main.log<<<\n%s\n<<<\n' % (sResultLog,));
            #self.dprint(u'vbox.log<<<\n%s\n<<<\n' % (sVMLog,));
            #self.dprint(u'krnl.log<<<\n%s\n<<<\n' % (sKrnlLog,));
            #self.dprint(u'vgatext.txt<<<\n%s\n<<<\n' % (sVgaText,));
            #self.dprint(u'info.txt<<<\n%s\n<<<\n' % (sInfoText,));
            #self.dprint(u'hard.txt<<<\n%s\n<<<\n' % (sNtHardLog,));

            # TODO: more

            #
            # Look for BSODs. Some stupid stupid inconsistencies in reason and log messages here, so don't try prettify this.
            #
            sDetails = self.findInAnyAndReturnRestOfLine([ sVMLog, sResultLog ],
                                                         'GIM: HyperV: Guest indicates a fatal condition! P0=');
            if sDetails is not None:
                # P0=%#RX64 P1=%#RX64 P2=%#RX64 P3=%#RX64 P4=%#RX64 "
                sKey = sDetails.split(' ', 1)[0];
                try:    sKey = '0x%08X' % (int(sKey, 16),);
                except: pass;
                if sKey in self.asBsodReasons:
                    tReason = ( self.ksBsodCategory, sKey );
                elif sKey.lower() in self.asBsodReasons: # just in case.
                    tReason = ( self.ksBsodCategory, sKey.lower() );
                else:
                    self.dprint(u'BSOD "%s" not found in %s;' % (sKey, self.asBsodReasons));
                    tReason = ( self.ksBsodCategory, self.ksBsodAddNew );
                return oCaseFile.noteReasonForId(tReason, oFailedResult.idTestResult, sComment = sDetails.strip());

            fFoundSomething = False;

            #
            # Look for linux panic.
            #
            if sKrnlLog is not None:
                fRet = self.scanLog([sKrnlLog,], self.katSimpleKernelLogReasons, oCaseFile, oFailedResult.idTestResult);
                if fRet is True:
                    return fRet;
                fFoundSomething |= fRet is None;

            #
            # Loop thru the simple stuff.
            #

            # Main log.
            fRet = self.scanLog([sResultLog,], self.katSimpleMainLogReasons, oCaseFile, oFailedResult.idTestResult);
            if fRet is True:
                return fRet;
            fFoundSomething |= fRet is None;

            # VM log.
            fRet = self.scanLog([sVMLog,], self.katSimpleVmLogReasons, oCaseFile, oFailedResult.idTestResult);
            if fRet is True:
                return fRet;
            fFoundSomething |= fRet is None;

            # Old main + vm log.
            fRet = self.scanLog([sResultLog, sVMLog], self.katSimpleMainAndVmLogReasonsDeprecated,
                                oCaseFile, oFailedResult.idTestResult);
            if fRet is True:
                return fRet;
            fFoundSomething |= fRet is None;

            # Continue with vga text.
            if sVgaText:
                fRet = self.scanLog([sVgaText,], self.katSimpleVgaTextReasons, oCaseFile, oFailedResult.idTestResult);
                if fRet is True:
                    return fRet;
                fFoundSomething |= fRet is None;

            # Continue with screen hashes.
            if sScreenHash is not None:
                for fStopOnHit, tReason, sHash in self.katSimpleScreenshotHashReasons:
                    if sScreenHash == sHash:
                        oCaseFile.noteReasonForId(tReason, oFailedResult.idTestResult);
                        if fStopOnHit:
                            return True;
                        fFoundSomething = True;

            # Check VBoxHardening.log.
            if sNtHardLog is not None:
                fRet = self.scanLog([sNtHardLog,], self.katSimpleVBoxHardeningLogReasons, oCaseFile, oFailedResult.idTestResult);
                if fRet is True:
                    return fRet;
                fFoundSomething |= fRet is None;

            #
            # Complicated stuff.
            #
            dLogs = {
                'sVMLog':       sVMLog,
                'sNtHardLog':   sNtHardLog,
                'sScreenHash':  sScreenHash,
                'sKrnlLog':     sKrnlLog,
                'sVgaText':     sVgaText,
                'sInfoText':    sInfoText,
            };

            # info.txt.
            if sInfoText:
                for sNeedle, fnHandler in self.katInfoTextHandlers:
                    if sInfoText.find(sNeedle) > 0:
                        (fStop, tReason) = fnHandler(self, oCaseFile, sInfoText, dLogs);# ? pylint: disable=too-many-function-args
                        if tReason is not None:
                            oCaseFile.noteReasonForId(tReason, oFailedResult.idTestResult);
                            if fStop:
                                return True;
                            fFoundSomething = True;

            #
            # Check for repeated reboots...
            #
            if sVMLog is not None:
                cResets = sVMLog.count('Changing the VM state from \'RUNNING\' to \'RESETTING\'');
                if cResets > 10:
                    return oCaseFile.noteReasonForId(self.ktReason_Unknown_Reboot_Loop, oFailedResult.idTestResult,
                                                     sComment = 'Counted %s reboots' % (cResets,));

            return fFoundSomething;

        #
        # Check if we got any VM or/and kernel logs.  Treat them as sets in
        # case we run multiple VMs here (this is of course ASSUMING they
        # appear in the order that terminateVmBySession uploads them).
        #
        cTimes      = 0;
        sVMLog      = None;
        sNtHardLog  = None;
        sScreenHash = None;
        sKrnlLog    = None;
        sVgaText    = None;
        sInfoText   = None;
        for oFile in oFailedResult.aoFiles:
            if oFile.sKind == TestResultFileData.ksKind_LogReleaseVm:
                if 'VBoxHardening.log' not in oFile.sFile:
                    if sVMLog is not None:
                        if investigateLogSet() is True:
                            return True;
                        cTimes += 1;
                    sInfoText   = None;
                    sVgaText    = None;
                    sKrnlLog    = None;
                    sScreenHash = None;
                    sNtHardLog  = None;
                    sVMLog      = oCaseFile.getLogFile(oFile);
                else:
                    sNtHardLog  = oCaseFile.getLogFile(oFile);
            elif oFile.sKind == TestResultFileData.ksKind_LogGuestKernel:
                sKrnlLog  = oCaseFile.getLogFile(oFile);
            elif oFile.sKind == TestResultFileData.ksKind_InfoVgaText:
                sVgaText  = '\n'.join([sLine.rstrip() for sLine in oCaseFile.getLogFile(oFile).split('\n')]);
            elif oFile.sKind == TestResultFileData.ksKind_InfoCollection:
                sInfoText = oCaseFile.getLogFile(oFile);
            elif oFile.sKind == TestResultFileData.ksKind_ScreenshotFailure:
                sScreenHash = oCaseFile.getScreenshotSha256(oFile);
                if sScreenHash is not None:
                    sScreenHash = sScreenHash.lower();
                    self.vprint(u'%s  %s' % ( sScreenHash, oFile.sFile,));

        if    (   sVMLog     is not None \
               or sNtHardLog is not None \
               or cTimes == 0) \
          and investigateLogSet() is True:
            return True;

        return None;

    def isResultFromVMRun(self, oFailedResult, sResultLog):
        """
        Checks if this result and corresponding log snippet looks like a VM run.
        """

        # Look for startVmEx/ startVmAndConnectToTxsViaTcp and similar output in the log.
        if sResultLog.find(' startVm') > 0:
            return True;

        # Any other indicators? No?
        _ = oFailedResult;
        return False;


    ## Things we search a VBoxSVC log for to figure out why something went bust.
    katSimpleSvcLogReasons = [
        # ( Whether to stop on hit, reason tuple, needle text. )
        ( False, ktReason_Unknown_VM_Crash,      re.compile(r'Reaper.* exited normally: -1073741819 \(0xc0000005\)') ),
        ( False, ktReason_Unknown_VM_Crash,      re.compile(r'Reaper.* was signalled: 11 \(0xb\)') ), # For VBox < 6.1.
        ( False, ktReason_Unknown_VM_Crash,      re.compile(r'Reaper.* was signalled: SIGABRT.*') ),  # Since VBox 7.0.
        ( False, ktReason_Unknown_VM_Crash,      re.compile(r'Reaper.* was signalled: SIGSEGV.*') ),
        ( False, ktReason_Unknown_VM_Terminated, re.compile(r'Reaper.* was signalled: SIGTERM.*') ),
        ( False, ktReason_Unknown_VM_Terminated, re.compile(r'Reaper.* was signalled: SIGKILL.*') ),
    ];

    def investigateSvcLogForVMRun(self, oCaseFile, sSvcLog):
        """
        Check the VBoxSVC log for a single VM run.
        """
        if sSvcLog:
            fRet = self.scanLog([sSvcLog,], self.katSimpleSvcLogReasons, oCaseFile, oCaseFile.oTree.idTestResult);
            if fRet is True or fRet is None:
                return True;
        return False;

    def investigateNtHardLogForVMRun(self, oCaseFile):
        """
        Check if the hardening log for a single VM run contains VM crash indications.
        """
        aoLogFiles = oCaseFile.oTree.getListOfLogFilesByKind(TestResultFileData.ksKind_LogReleaseVm);
        for oLogFile in aoLogFiles:
            if oLogFile.sFile.find('VBoxHardening.log') >= 0:
                sLog = oCaseFile.getLogFile(oLogFile);
                if sLog.find('Quitting: ExitCode=0xc0000005') >= 0:
                    return oCaseFile.noteReasonForId(self.ktReason_Unknown_VM_Crash, oCaseFile.oTree.idTestResult);
        return False;


    def investigateVBoxVMTest(self, oCaseFile, fSingleVM):
        """
        Checks out a VBox VM test.

        This is generic investigation of a test running one or more VMs, like
        for example a smoke test or a guest installation test.

        The fSingleVM parameter is a hint, which probably won't come in useful.
        """
        _ = fSingleVM;

        #
        # Get a list of test result failures we should be looking into and the main log.
        #
        aoFailedResults = oCaseFile.oTree.getListOfFailures();
        sMainLog        = oCaseFile.getMainLog();

        #
        # There are a set of errors ending up on the top level result record.
        # Should deal with these first.
        #
        if len(aoFailedResults) == 1 and aoFailedResults[0] == oCaseFile.oTree:
            # Check if we've just got that XPCOM client smoke test shutdown issue.  This will currently always
            # be reported on the top result because vboxinstall.py doesn't add an error for it.  It is easy to
            # ignore other failures in the test if we're not a little bit careful here.
            if sMainLog.find('vboxinstaller: Exit code: -11 (') > 0:
                oCaseFile.noteReason(self.ktReason_XPCOM_Exit_Minus_11);
                return self.caseClosed(oCaseFile);

            # Hang after starting VBoxSVC (e.g. idTestSet=136307258)
            if self.isThisFollowedByTheseLines(sMainLog, 'oVBoxMgr=<vboxapi.VirtualBoxManager object at',
                                               (' Timeout: ', ' Attempting to abort child...',) ):
                if sMainLog.find('*** glibc detected *** /') > 0:
                    oCaseFile.noteReason(self.ktReason_XPCOM_VBoxSVC_Hang_Plus_Heap_Corruption);
                else:
                    oCaseFile.noteReason(self.ktReason_XPCOM_VBoxSVC_Hang);
                return self.caseClosed(oCaseFile);

            # Look for heap corruption without visible hang.
            if   sMainLog.find('*** glibc detected *** /') > 0 \
              or sMainLog.find("-1073740940") > 0: # STATUS_HEAP_CORRUPTION / 0xc0000374
                oCaseFile.noteReason(self.ktReason_Unknown_Heap_Corruption);
                return self.caseClosed(oCaseFile);

            # Out of memory w/ timeout.
            if sMainLog.find('sErrId=HostMemoryLow') > 0:
                oCaseFile.noteReason(self.ktReason_Host_HostMemoryLow);
                return self.caseClosed(oCaseFile);

            # Stale files like vts_rm.exe (windows).
            offEnd = sMainLog.rfind('*** The test driver exits successfully. ***');
            if offEnd > 0 and sMainLog.find('[Error 145] The directory is not empty: ', offEnd) > 0:
                oCaseFile.noteReason(self.ktReason_Ignore_Stale_Files);
                return self.caseClosed(oCaseFile);

        #
        # XPCOM screwup
        #
        if sMainLog.find('AttributeError: \'NoneType\' object has no attribute \'addObserver\'') > 0:
            oCaseFile.noteReason(self.ktReason_Buggy_Build_Broken_Build);
            return self.caseClosed(oCaseFile);

        #
        # Go thru each failed result.
        #
        for oFailedResult in aoFailedResults:
            self.dprint(u'Looking at test result #%u - %s' % (oFailedResult.idTestResult, oFailedResult.getFullName(),));
            sResultLog = TestSetData.extractLogSectionElapsed(sMainLog, oFailedResult.tsCreated, oFailedResult.tsElapsed);
            if oFailedResult.sName == 'Installing VirtualBox':
                self.investigateInstallUninstallFailure(oCaseFile, oFailedResult, sResultLog, fInstall = True)

            elif oFailedResult.sName == 'Uninstalling VirtualBox':
                self.investigateInstallUninstallFailure(oCaseFile, oFailedResult, sResultLog, fInstall = False)

            elif self.isResultFromVMRun(oFailedResult, sResultLog):
                self.investigateVMResult(oCaseFile, oFailedResult, sResultLog);

            elif self.isResultFromGATest(oCaseFile, oFailedResult):
                self.investigateGATest(oCaseFile, oFailedResult, sResultLog);

            elif sResultLog.find('most likely not unique') > 0:
                oCaseFile.noteReasonForId(self.ktReason_Host_NetworkMisconfiguration, oFailedResult.idTestResult)
            elif sResultLog.find('Exception: 0x800706be (Call to remote object failed (NS_ERROR_CALL_FAILED))') > 0:
                oCaseFile.noteReasonForId(self.ktReason_XPCOM_NS_ERROR_CALL_FAILED, oFailedResult.idTestResult);

            elif sResultLog.find('The machine is not mutable (state is ') > 0:
                self.vprint('Ignoring "machine not mutable" error as it is probably due to an earlier problem');
                oCaseFile.noteReasonForId(self.ktHarmless, oFailedResult.idTestResult);

            elif  sResultLog.find('** error: no action was specified') > 0 \
               or sResultLog.find('(len(self._asXml, asText))') > 0:
                oCaseFile.noteReasonForId(self.ktReason_Ignore_Buggy_Test_Driver, oFailedResult.idTestResult);

            else:
                self.vprint(u'TODO: Cannot place idTestResult=%u - %s' % (oFailedResult.idTestResult, oFailedResult.sName,));
                self.dprint(u'%s + %s <<\n%s\n<<' % (oFailedResult.tsCreated, oFailedResult.tsElapsed, sResultLog,));

        #
        # Python bindings installation problem.
        #
        if sMainLog.find('ImportError: No module named vboxapi') > 0:
            oCaseFile.noteReason(self.ktReason_Host_Python_vboxapi_not_found);
            return self.caseClosed(oCaseFile);

        if sMainLog.find('ImportError: No module named xpcom') > 0:
            oCaseFile.noteReason(self.ktReason_Host_Python_xpcom_not_found);
            return self.caseClosed(oCaseFile);

        #
        # Windows python/com screwup.
        #
        if sMainLog.find('ModuleNotFoundError: No module named \'win32com.gen_py') > 0:
            oCaseFile.noteReason(self.ktReason_Host_win32com_gen_py_not_found);
            return self.caseClosed(oCaseFile);

        #
        # Check VBoxSVC.log and VBoxHardening.log for VM crashes if inconclusive on single VM runs.
        #
        if fSingleVM and len(oCaseFile.dReasonForResultId) < len(aoFailedResults):
            self.dprint(u'Got %u out of %u - checking VBoxSVC.log...'
                        % (len(oCaseFile.dReasonForResultId), len(aoFailedResults)));
            if self.investigateSvcLogForVMRun(oCaseFile, oCaseFile.getSvcLog()):
                return self.caseClosed(oCaseFile);
            if self.investigateNtHardLogForVMRun(oCaseFile):
                return self.caseClosed(oCaseFile);

        #
        # Report home and close the case if we got them all, otherwise log it.
        #
        if len(oCaseFile.dReasonForResultId) >= len(aoFailedResults):
            return self.caseClosed(oCaseFile);

        if oCaseFile.dReasonForResultId:
            self.vprint(u'TODO: Got %u out of %u - close, but no cigar. :-/'
                        % (len(oCaseFile.dReasonForResultId), len(aoFailedResults)));
        else:
            self.vprint(u'XXX: Could not figure out anything at all! :-(');
        return False;


    ## Things we search a main log for to figure out why something in the API test went bust.
    katSimpleApiMainLogReasons = [
        # ( Whether to stop on hit, reason tuple, needle text. )
        ( True,  ktReason_Networking_Nonexistent_host_nic,
          'rc=E_FAIL text="Nonexistent host networking interface, name \'eth0\' (VERR_INTERNAL_ERROR)"' ),
        ( False, ktReason_XPCOM_NS_ERROR_CALL_FAILED,
          'Exception: 0x800706be (Call to remote object failed (NS_ERROR_CALL_FAILED))' ),
        ( True,  ktReason_API_std_bad_alloc,                        'Unexpected exception: std::bad_alloc' ),
        ( True,  ktReason_API_Digest_Mismatch,                      'Digest mismatch (VERR_NOT_EQUAL)' ),
        ( True,  ktReason_API_MoveVM_SharingViolation,              'rc=VBOX_E_IPRT_ERROR text="Could not copy the log file ' ),
        ( True,  ktReason_API_MoveVM_InvalidParameter,
          'rc=VBOX_E_IPRT_ERROR text="Could not copy the setting file ' ),
        ( True,  ktReason_API_Open_Session_Failed,                  'error: failed to open session for' ),
    ];

    def investigateVBoxApiTest(self, oCaseFile):
        """
        Checks out a VBox API test.
        """

        #
        # Get a list of test result failures we should be looking into and the main log.
        #
        aoFailedResults = oCaseFile.oTree.getListOfFailures();
        sMainLog        = oCaseFile.getMainLog();

        #
        # Go thru each failed result.
        #
        for oFailedResult in aoFailedResults:
            self.dprint(u'Looking at test result #%u - %s' % (oFailedResult.idTestResult, oFailedResult.getFullName(),));
            sResultLog = TestSetData.extractLogSectionElapsed(sMainLog, oFailedResult.tsCreated, oFailedResult.tsElapsed);
            if oFailedResult.sName == 'Installing VirtualBox':
                self.investigateInstallUninstallFailure(oCaseFile, oFailedResult, sResultLog, fInstall = True)

            elif oFailedResult.sName == 'Uninstalling VirtualBox':
                self.investigateInstallUninstallFailure(oCaseFile, oFailedResult, sResultLog, fInstall = False)

            elif sResultLog.find('Exception: 0x800706be (Call to remote object failed (NS_ERROR_CALL_FAILED))') > 0:
                oCaseFile.noteReasonForId(self.ktReason_XPCOM_NS_ERROR_CALL_FAILED, oFailedResult.idTestResult);

            else:
                fFoundSomething = False;
                for fStopOnHit, tReason, sNeedle in self.katSimpleApiMainLogReasons:
                    if sResultLog.find(sNeedle) > 0:
                        oCaseFile.noteReasonForId(tReason, oFailedResult.idTestResult);
                        fFoundSomething = True;
                        if fStopOnHit:
                            break;
                if fFoundSomething:
                    self.vprint(u'TODO: Cannot place idTestResult=%u - %s' % (oFailedResult.idTestResult, oFailedResult.sName,));
                    self.dprint(u'%s + %s <<\n%s\n<<' % (oFailedResult.tsCreated, oFailedResult.tsElapsed, sResultLog,));

        #
        # Report home and close the case if we got them all, otherwise log it.
        #
        if len(oCaseFile.dReasonForResultId) >= len(aoFailedResults):
            return self.caseClosed(oCaseFile);

        if oCaseFile.dReasonForResultId:
            self.vprint(u'TODO: Got %u out of %u - close, but no cigar. :-/'
                        % (len(oCaseFile.dReasonForResultId), len(aoFailedResults)));
        else:
            self.vprint(u'XXX: Could not figure out anything at all! :-(');
        return False;


    def reasoningFailures(self):
        """
        Guess the reason for failures.
        """
        #
        # Get a list of failed test sets without any assigned failure reason.
        #
        cGot = 0;
        if not self.oConfig.aidTestSets:
            aoTestSets = self.oTestSetLogic.fetchFailedSetsWithoutReason(cHoursBack = self.oConfig.cHoursBack,
                                                                         tsNow = self.tsNow);
        else:
            aoTestSets = [self.oTestSetLogic.getById(idTestSet) for idTestSet in self.oConfig.aidTestSets];
        for oTestSet in aoTestSets:
            self.dprint(u'----------------------------------- #%u, status %s -----------------------------------'
                        % ( oTestSet.idTestSet, oTestSet.enmStatus,));

            #
            # Open a case file and assign it to the right investigator.
            #
            (oTree, _ ) = self.oTestResultLogic.fetchResultTree(oTestSet.idTestSet);
            oBuild      = BuildDataEx().initFromDbWithId(       self.oDb, oTestSet.idBuild,       oTestSet.tsCreated);
            oTestBox    = TestBoxData().initFromDbWithGenId(    self.oDb, oTestSet.idGenTestBox);
            oTestGroup  = TestGroupData().initFromDbWithId(     self.oDb, oTestSet.idTestGroup,   oTestSet.tsCreated);
            oTestCase   = TestCaseDataEx().initFromDbWithGenId( self.oDb, oTestSet.idGenTestCase, oTestSet.tsConfig);

            oCaseFile = VirtualTestSheriffCaseFile(self, oTestSet, oTree, oBuild, oTestBox, oTestGroup, oTestCase);

            if oTestSet.enmStatus == TestSetData.ksTestStatus_BadTestBox:
                self.dprint(u'investigateBadTestBox is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateBadTestBox(oCaseFile);

            elif oCaseFile.isVBoxUnitTest():
                self.dprint(u'investigateVBoxUnitTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxUnitTest(oCaseFile);

            elif oCaseFile.isVBoxInstallTest() or oCaseFile.isVBoxUnattendedInstallTest():
                self.dprint(u'investigateVBoxVMTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxVMTest(oCaseFile, fSingleVM = True);

            elif oCaseFile.isVBoxUSBTest():
                self.dprint(u'investigateVBoxVMTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxVMTest(oCaseFile, fSingleVM = True);

            elif oCaseFile.isVBoxStorageTest():
                self.dprint(u'investigateVBoxVMTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxVMTest(oCaseFile, fSingleVM = True);

            elif oCaseFile.isVBoxGAsTest():
                self.dprint(u'investigateVBoxVMTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxVMTest(oCaseFile, fSingleVM = True);

            elif oCaseFile.isVBoxAPITest():
                self.dprint(u'investigateVBoxApiTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxApiTest(oCaseFile);

            elif oCaseFile.isVBoxBenchmarkTest():
                self.dprint(u'investigateVBoxVMTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxVMTest(oCaseFile, fSingleVM = False);

            elif oCaseFile.isVBoxSmokeTest():
                self.dprint(u'investigateVBoxVMTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxVMTest(oCaseFile, fSingleVM = False);

            elif oCaseFile.isVBoxSerialTest():
                self.dprint(u'investigateVBoxVMTest is taking over %s.' % (oCaseFile.sLongName,));
                fRc = self.investigateVBoxVMTest(oCaseFile, fSingleVM = False);

            else:
                self.vprint(u'reasoningFailures: Unable to classify test set: %s' % (oCaseFile.sLongName,));
                fRc = False;
            cGot += fRc is True;

        self.vprint(u'reasoningFailures: Got %u out of %u' % (cGot, len(aoTestSets), ));
        return 0;


    def main(self):
        """
        The 'main' function.
        Return exit code (0, 1, etc).
        """
        # Database stuff.
        self.oDb                     = TMDatabaseConnection()
        self.oTestResultLogic        = TestResultLogic(self.oDb);
        self.oTestSetLogic           = TestSetLogic(self.oDb);
        self.oFailureReasonLogic     = FailureReasonLogic(self.oDb);
        self.oTestResultFailureLogic = TestResultFailureLogic(self.oDb);
        self.asBsodReasons           = self.oFailureReasonLogic.fetchForSheriffByNamedCategory(self.ksBsodCategory);
        self.asUnitTestReasons       = self.oFailureReasonLogic.fetchForSheriffByNamedCategory(self.ksUnitTestCategory);

        # Get a fix on our 'now' before we do anything..
        self.oDb.execute('SELECT CURRENT_TIMESTAMP - interval \'%s hours\'', (self.oConfig.cStartHoursAgo,));
        self.tsNow = self.oDb.fetchOne();

        # If we're suppost to commit anything we need to get our user ID.
        rcExit = 0;
        if self.oConfig.fRealRun:
            self.oLogin = UserAccountLogic(self.oDb).tryFetchAccountByLoginName(VirtualTestSheriff.ksLoginName);
            if self.oLogin is None:
                rcExit = self.eprint('Cannot find my user account "%s"!' % (VirtualTestSheriff.ksLoginName,));
            else:
                self.uidSelf = self.oLogin.uid;

        #
        # Do the stuff.
        #
        if rcExit == 0:
            rcExit  = self.selfCheck();
        if rcExit == 0:
            rcExit  = self.badTestBoxManagement();
            rcExit2 = self.reasoningFailures();
            if rcExit == 0:
                rcExit = rcExit2;
            # Redo the bad testbox management after failure reasons have been assigned (got timing issues).
            if rcExit == 0:
                rcExit = self.badTestBoxManagement();

        # Cleanup.
        self.oFailureReasonLogic     = None;
        self.oTestResultFailureLogic = None;
        self.oTestSetLogic           = None;
        self.oTestResultLogic        = None;
        self.oDb.close();
        self.oDb = None;
        if self.oLogFile is not None:
            self.oLogFile.close();
            self.oLogFile = None;
        return rcExit;

if __name__ == '__main__':
    sys.exit(VirtualTestSheriff().main());
