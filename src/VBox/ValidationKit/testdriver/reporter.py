# -*- coding: utf-8 -*-
# $Id: reporter.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
Testdriver reporter module.
"""

from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2010-2024 Oracle and/or its affiliates.

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


# Standard Python imports.
import array
import datetime
import errno
import gc
import os
import os.path
import sys
import time
import threading
import traceback

# Validation Kit imports.
from common import utils;

## test reporter instance
g_oReporter = None # type: ReporterBase
g_sReporterName = None;


class ReporterLock(object):
    """
    Work around problem with garbage collection triggering __del__ method with
    logging while inside the logger lock and causing a deadlock.
    """

    def __init__(self, sName):
        self.sName          = sName;
        self.oLock          = threading.RLock();
        self.oOwner         = None;
        self.cRecursion     = 0;
        self.fRestoreGC     = False;

    def acquire(self):
        """ Acquire the lock. """
        oSelf = threading.current_thread();

        # Take the lock.
        if not self.oLock.acquire():                            # pylint: disable=consider-using-with
            return False;

        self.oOwner      = oSelf;
        self.cRecursion += 1;

        # Disable GC to avoid __del__ w/ log statement randomly reenter the logger.
        if self.cRecursion == 1:
            self.fRestoreGC = gc.isenabled();
            if self.fRestoreGC:
                gc.disable();

        return True;

    def release(self):
        """ Release the lock. """
        oSelf = threading.current_thread();

        # Check the ownership.
        if oSelf != self.oOwner:
            raise threading.ThreadError();

        # Drop one recursion.
        self.cRecursion -= 1;
        if self.cRecursion <= 0:

            # Final recursion. Clear owner and re-enable GC.
            self.oOwner = None;
            if self.fRestoreGC:
                self.fRestoreGC = False;
                gc.enable();

        self.oLock.release();

## Reporter lock.
g_oLock = ReporterLock('reporter');



class PythonLoggingStream(object):
    """
    Python logging => testdriver/reporter.py stream.
    """

    def write(self, sText):
        """Writes python log message to our stream."""
        if g_oReporter is not None:
            sText = sText.rstrip("\r\n");
            #g_oReporter.log(0, 'python: %s' % (sText), utils.getCallerName(), utils.getTimePrefix());
        return True;

    def flush(self):
        """Flushes the stream."""
        return True;


class ReporterBase(object):
    """
    Base class for the reporters.
    """

    def __init__(self):
        self.iVerbose   = 1;
        self.iDebug     = 0;
        self.cErrors    = 0;
        self.fTimedOut  = False; # Once set, it trickles all the way up.
        self.atTests    = [];
        self.sName      = os.path.splitext(os.path.basename(sys.argv[0]))[0];

        # Hook into the python logging.
        import logging;
        logging.basicConfig(stream = PythonLoggingStream(),
                            level  = logging.DEBUG,
                            format = '%(name)-12s %(levelname)-8s %(message)s');
    #
    # Introspection and configuration.
    #

    def isLocal(self):
        """Is this a local reporter?"""
        return False;

    def incVerbosity(self):
        """Increases the verbosity level."""
        self.iVerbose += 1;

    def incDebug(self):
        """Increases the debug level."""
        self.iDebug += 1;

    def getVerbosity(self):
        """Returns the current verbosity level."""
        return self.iVerbose;

    def getDebug(self):
        """Returns the current debug level."""
        return self.iDebug;

    def appendToProcessName(self, sAppend):
        """
        Appends sAppend to the base process name.
        Returns the new process name.
        """
        self.sName = os.path.splitext(os.path.basename(sys.argv[0]))[0] + sAppend;
        return self.sName;


    #
    # Generic logging.
    #

    def log(self, iLevel, sText, sCaller, sTsPrf):
        """
        Writes the specfied text to the log if iLevel is less or requal
        to iVerbose.
        """
        _ = iLevel; _ = sText; _ = sCaller; _ = sTsPrf;
        return 0;

    #
    # XML output from the reporter.
    #

    def _xmlEscAttr(self, sValue):
        """Escapes an XML attribute value."""
        sValue = sValue.replace('&',  '&amp;');
        sValue = sValue.replace('<',  '&lt;');
        sValue = sValue.replace('>',  '&gt;');
        #sValue = sValue.replace('\'', '&apos;');
        sValue = sValue.replace('"',  '&quot;');
        sValue = sValue.replace('\n', '&#xA');
        sValue = sValue.replace('\r', '&#xD');
        return sValue;

    def _xmlWrite(self, asText, fIndent = True):
        """XML output function for the reporter."""
        _ = asText; _ = fIndent;
        return None;

    def xmlFlush(self, fRetry = False, fForce = False):
        """Flushes XML output if buffered."""
        _ = fRetry; _ = fForce;
        return True;

    #
    # XML output from child.
    #

    def subXmlStart(self, oFileWrapper):
        """Called by the file wrapper when the first bytes are written to the test pipe."""
        _ = oFileWrapper;
        return None;

    def subXmlWrite(self, oFileWrapper, sRawXml, sCaller):
        """Called by the file wrapper write method for test pipes."""
        return self.log(0, 'raw xml%s: %s' % (oFileWrapper.sPrefix, sRawXml), sCaller, utils.getTimePrefix());

    def subXmlEnd(self, oFileWrapper):
        """Called by the file wrapper __del__ method for test pipes."""
        _ = oFileWrapper;
        return None;

    #
    # File output.
    #

    def addLogFile(self, oSrcFile, sSrcFilename, sAltName, sDescription, sKind, sCaller, sTsPrf):
        """
        Adds the file to the report.
        Returns True on success, False on failure.
        """
        _ = oSrcFile; _ = sSrcFilename; _ = sAltName; _ = sDescription; _ = sKind; _ = sCaller; _ = sTsPrf;
        return True;

    def addLogString(self, sLog, sLogName, sDescription, sKind, sCaller, sTsPrf):
        """
        Adds the file to the report.
        Returns True on success, False on failure.
        """
        _ = sLog; _ = sLogName; _ = sDescription; _ = sKind; _ = sCaller; _ = sTsPrf;
        return True;

    #
    # Test reporting
    #

    def _testGetFullName(self):
        """
        Mangles the test names in atTest into a single name to make it easier
        to spot where we are.
        """
        sName = '';
        for t in self.atTests:
            if sName != '':
                sName += ', ';
            sName += t[0];
        return sName;

    def testIncErrors(self):
        """Increates the error count."""
        self.cErrors += 1;
        return self.cErrors;

    def testSetTimedOut(self):
        """Sets time out indicator for the current test and increases the error counter."""
        self.fTimedOut = True;
        self.cErrors += 1;
        return None;

    def testStart(self, sName, sCaller):
        """ Starts a new test, may be nested. """
        (sTsPrf, sTsIso) = utils.getTimePrefixAndIsoTimestamp();
        self._xmlWrite([ '<Test timestamp="%s" name="%s">' % (sTsIso, self._xmlEscAttr(sName),), ]);
        self.atTests.append((sName, self.cErrors, self.fTimedOut));
        self.fTimedOut = False;
        return self.log(1, ' %-50s: TESTING' % (self._testGetFullName()), sCaller, sTsPrf);

    def testValue(self, sName, sValue, sUnit, sCaller):
        """ Reports a benchmark value or something simiarlly useful. """
        (sTsPrf, sTsIso) = utils.getTimePrefixAndIsoTimestamp();
        self._xmlWrite([ '<Value timestamp="%s" name="%s" unit="%s" value="%s"/>'
                         % (sTsIso, self._xmlEscAttr(sName), self._xmlEscAttr(sUnit), self._xmlEscAttr(sValue)), ]);
        return self.log(0, '**  %-48s: %12s %s' % (sName, sValue, sUnit), sCaller, sTsPrf);

    def testFailure(self, sDetails, sCaller):
        """ Reports a failure. """
        (sTsPrf, sTsIso) = utils.getTimePrefixAndIsoTimestamp();
        self.cErrors = self.cErrors + 1;
        self._xmlWrite([ '<FailureDetails timestamp="%s" text="%s"/>' % (sTsIso, self._xmlEscAttr(sDetails),), ]);
        return self.log(0, sDetails, sCaller, sTsPrf);

    def testDone(self, fSkipped, sCaller):
        """
        Marks the current test as DONE, pops it and maks the next test on the
        stack current.
        Returns (name, errors).
        """
        (sTsPrf, sTsIso) = utils.getTimePrefixAndIsoTimestamp();
        sFullName        = self._testGetFullName();

        # safe pop
        if not self.atTests:
            self.log(0, 'testDone on empty test stack!', sCaller, sTsPrf);
            return ('internal error', 0);
        fTimedOut = self.fTimedOut;
        sName, cErrorsStart, self.fTimedOut = self.atTests.pop();

        # log + xml.
        cErrors = self.cErrors - cErrorsStart;
        if cErrors == 0:
            if fSkipped is not True:
                self._xmlWrite([ '  <Passed timestamp="%s"/>' % (sTsIso,), '</Test>' ],);
                self.log(1, '** %-50s: PASSED' % (sFullName,), sCaller, sTsPrf);
            else:
                self._xmlWrite([ '  <Skipped timestamp="%s"/>' % (sTsIso,), '</Test>' ]);
                self.log(1, '** %-50s: SKIPPED' % (sFullName,), sCaller, sTsPrf);
        elif fTimedOut:
            self._xmlWrite([ '  <TimedOut timestamp="%s" errors="%d"/>' % (sTsIso, cErrors), '</Test>' ]);
            self.log(0, '** %-50s: TIMED-OUT - %d errors' % (sFullName, cErrors), sCaller, sTsPrf);
        else:
            self._xmlWrite([ '  <Failed timestamp="%s" errors="%d"/>' % (sTsIso, cErrors), '</Test>' ]);
            self.log(0, '** %-50s: FAILED - %d errors' % (sFullName, cErrors), sCaller, sTsPrf);

        # Flush buffers when reaching the last test.
        if not self.atTests:
            self.xmlFlush(fRetry = True);

        return (sName, cErrors);

    def testErrorCount(self):
        """
        Returns the number of errors accumulated by the current test.
        """
        cTests = len(self.atTests);
        if cTests <= 0:
            return self.cErrors;
        return self.cErrors - self.atTests[cTests - 1][1];

    def testCleanup(self, sCaller):
        """
        Closes all open test as failed.
        Returns True if no open tests, False if there were open tests.
        """
        if not self.atTests:
            return True;
        for _ in range(len(self.atTests)):
            self.testFailure('Test not closed by test drver', sCaller)
            self.testDone(False, sCaller);
        return False;

    #
    # Misc.
    #

    def doPollWork(self, sDebug = None):
        """
        Check if any pending stuff expired and needs doing.
        """
        _ = sDebug;
        return None;




class LocalReporter(ReporterBase):
    """
    Local reporter instance.
    """

    def __init__(self):
        ReporterBase.__init__(self);
        self.oLogFile   = None;
        self.oXmlFile   = None;
        self.fXmlOk     = True;
        self.iSubXml    = 0;
        self.iOtherFile = 0;
        self.fnGetIsoTimestamp  = utils.getIsoTimestamp;    # Hack to get a timestamp in __del__.
        self.oStdErr            = sys.stderr;               # Hack for __del__ output.

        #
        # Figure the main log directory.
        #
        try:
            self.sDefLogDir = os.path.abspath(os.path.expanduser(os.path.join('~', 'VBoxTestLogs')));
        except:
            self.sDefLogDir = os.path.abspath("VBoxTestLogs");
        try:
            sLogDir = os.path.abspath(os.environ.get('TESTBOX_REPORTER_LOG_DIR', self.sDefLogDir));
            if not os.path.isdir(sLogDir):
                os.makedirs(sLogDir, 0o750);
        except:
            sLogDir = self.sDefLogDir;
            if not os.path.isdir(sLogDir):
                os.makedirs(sLogDir, 0o750);

        #
        # Make a subdirectory for this test run.
        #
        sTs = datetime.datetime.utcnow().strftime('%Y-%m-%dT%H-%M-%S.log');
        self.sLogDir = sLogDir = os.path.join(sLogDir, '%s-%s' % (sTs, self.sName));
        try:
            os.makedirs(self.sLogDir, 0o750);
        except:
            self.sLogDir = '%s-%s' % (self.sLogDir, os.getpid());
            os.makedirs(self.sLogDir, 0o750);

        #
        # Open the log file and write a header.
        #
        sLogName = os.path.join(self.sLogDir, 'testsuite.log');
        sTsIso = utils.getIsoTimestamp();
        if sys.version_info[0] >= 3: # Add 'b' to prevent write taking issue with encode('utf-8') not returning a string.
            self.oLogFile = utils.openNoInherit(sLogName, "wb");
        else:
            self.oLogFile = utils.openNoInherit(sLogName, "w");
        self.oLogFile.write(('Created log file at %s.\nRunning: %s' % (sTsIso, sys.argv)).encode('utf-8'));

        #
        # Open the xml log file and write the mandatory introduction.
        #
        # Note! This is done here and not in the base class because the remote
        #       logger doesn't really need this.  It doesn't need the outer
        #       test wrapper either.
        #
        sXmlName = os.path.join(self.sLogDir, 'testsuite.xml');
        if sys.version_info[0] >= 3: # Add 'b' to prevent write taking issue with encode('utf-8') not returning a string.
            self.oXmlFile = utils.openNoInherit(sXmlName, "wb");
        else:
            self.oXmlFile = utils.openNoInherit(sXmlName, "w");
        self._xmlWrite([ '<?xml version="1.0" encoding="UTF-8" ?>',
                         '<Test timestamp="%s" name="%s">' % (sTsIso, self._xmlEscAttr(self.sName),), ],
                       fIndent = False);

    def __del__(self):
        """Ends and completes the log files."""
        try:    sTsIso = self.fnGetIsoTimestamp();
        except Exception as oXcpt:
            sTsIso = str(oXcpt);

        if self.oLogFile is not None:
            try:
                self.oLogFile.write(('\nThe End %s\n' % (sTsIso,)).encode('utf-8'));
                self.oLogFile.close();
            except: pass;
            self.oLogFile = None;

        if self.oXmlFile is not None:
            self._closeXml(sTsIso);
            self.oXmlFile = None;

    def _closeXml(self, sTsIso):
        """Closes the XML file."""
        if self.oXmlFile is not None:
            # pop the test stack
            while self.atTests:
                sName, cErrorsStart, self.fTimedOut = self.atTests.pop();
                self._xmlWrite([ '<End timestamp="%s" errors="%d"/>' % (sTsIso, self.cErrors - cErrorsStart,),
                                 '</%s>' % (sName,), ]);

            # The outer one is not on the stack.
            self._xmlWrite([ '  <End timestamp="%s"/>' % (sTsIso,),
                             '</Test>', ], fIndent = False);
            try:
                self.oXmlFile.close();
                self.oXmlFile = None;
            except:
                pass;

    def _xmlWrite(self, asText, fIndent = True):
        """Writes to the XML file."""
        for sText in asText:
            if fIndent:
                sIndent = ''.ljust((len(self.atTests) + 1) * 2);
                sText = sIndent + sText;
            sText += '\n';

            try:
                self.oXmlFile.write(sText.encode('utf-8'));
            except:
                if self.fXmlOk:
                    traceback.print_exc();
                    self.fXmlOk = False;
                return False;
        return True;

    #
    # Overridden methods.
    #

    def isLocal(self):
        """Is this a local reporter?"""
        return True;

    def log(self, iLevel, sText, sCaller, sTsPrf):
        if iLevel <= self.iVerbose:
            # format it.
            if self.iDebug <= 0:
                sLogText = '%s %s' % (sTsPrf, sText);
            elif self.iDebug <= 1:
                sLogText = '%s %30s: %s' % (sTsPrf, sCaller, sText);
            else:
                sLogText = '%s e=%u %30s: %s' % (sTsPrf, self.cErrors, sCaller, sText);

            # output it.
            if sys.version_info[0] >= 3:
                sAscii = sLogText;
            else:
                sAscii = sLogText.encode('ascii', 'replace');
            if self.iDebug == 0:
                print('%s: %s' % (self.sName, sAscii), file = self.oStdErr);
            else:
                print('%s' % (sAscii), file = self.oStdErr);
            sLogText += '\n';
            try:
                self.oLogFile.write(sLogText.encode('utf-8'));
            except:
                pass;
        return 0;

    def addLogFile(self, oSrcFile, sSrcFilename, sAltName, sDescription, sKind, sCaller, sTsPrf):
        # Figure the destination filename.
        iOtherFile = self.iOtherFile;
        self.iOtherFile += 1;
        sDstFilename = os.path.join(self.sLogDir, 'other-%d-%s.log' \
                                    % (iOtherFile, os.path.splitext(os.path.basename(sSrcFilename))[0]));
        self.log(0, '** Other log file: %s - %s (%s)' % (sDstFilename, sDescription, sSrcFilename), sCaller, sTsPrf);

        # Open the destination file and copy over the data.
        fRc = True;
        try:
            oDstFile = utils.openNoInherit(sDstFilename, 'wb');
        except Exception as oXcpt:
            self.log(0, 'error opening %s: %s' % (sDstFilename, oXcpt), sCaller, sTsPrf);
        else:
            while True:
                try:
                    abBuf = oSrcFile.read(65536);
                except Exception as oXcpt:
                    fRc = False;
                    self.log(0, 'error reading %s: %s' % (sSrcFilename, oXcpt), sCaller, sTsPrf);
                else:
                    try:
                        oDstFile.write(abBuf);
                    except Exception as oXcpt:
                        fRc = False;
                        self.log(0, 'error writing %s: %s' % (sDstFilename, oXcpt), sCaller, sTsPrf);
                    else:
                        if abBuf:
                            continue;
                break;
            oDstFile.close();

            # Leave a mark in the XML log.
            self._xmlWrite(['<LogFile timestamp="%s" filename="%s" source="%s" kind="%s" ok="%s">%s</LogFile>\n'
                % (utils.getIsoTimestamp(), self._xmlEscAttr(os.path.basename(sDstFilename)), self._xmlEscAttr(sSrcFilename), \
                   self._xmlEscAttr(sKind), fRc, self._xmlEscAttr(sDescription))] );
        _ = sAltName;
        return fRc;

    def addLogString(self, sLog, sLogName, sDescription, sKind, sCaller, sTsPrf):
        # Figure the destination filename.
        iOtherFile = self.iOtherFile;
        self.iOtherFile += 1;
        sDstFilename = os.path.join(self.sLogDir, 'other-%d-%s.log' \
                                    % (iOtherFile, os.path.splitext(os.path.basename(sLogName))[0]));
        self.log(0, '** Other log file: %s - %s (%s)' % (sDstFilename, sDescription, sLogName), sCaller, sTsPrf);

        # Open the destination file and copy over the data.
        fRc = True;
        try:
            oDstFile = utils.openNoInherit(sDstFilename, 'w');
        except Exception as oXcpt:
            self.log(0, 'error opening %s: %s' % (sDstFilename, oXcpt), sCaller, sTsPrf);
        else:
            try:
                oDstFile.write(sLog);
            except Exception as oXcpt:
                fRc = False;
                self.log(0, 'error writing %s: %s' % (sDstFilename, oXcpt), sCaller, sTsPrf);

            oDstFile.close();

            # Leave a mark in the XML log.
            self._xmlWrite(['<LogFile timestamp="%s" filename="%s" source="%s" kind="%s" ok="%s">%s</LogFile>\n'
                % (utils.getIsoTimestamp(), self._xmlEscAttr(os.path.basename(sDstFilename)), self._xmlEscAttr(sLogName), \
                   self._xmlEscAttr(sKind), fRc, self._xmlEscAttr(sDescription))] );
        return fRc;

    def subXmlStart(self, oFileWrapper):
        # Open a new file and just include it from the main XML.
        iSubXml = self.iSubXml;
        self.iSubXml += 1;
        sSubXmlName = os.path.join(self.sLogDir, 'sub-%d.xml' % (iSubXml,));
        try:
            oFileWrapper.oSubXmlFile = utils.openNoInherit(sSubXmlName, "w");
        except:
            errorXcpt('open(%s)' % oFileWrapper.oSubXmlName);
            oFileWrapper.oSubXmlFile = None;
        else:
            self._xmlWrite(['<Include timestamp="%s" filename="%s"/>\n'
                    % (utils.getIsoTimestamp(), self._xmlEscAttr(os.path.basename(sSubXmlName)))]);
        return None;

    def subXmlWrite(self, oFileWrapper, sRawXml, sCaller):
        if oFileWrapper.oSubXmlFile is not None:
            try:
                oFileWrapper.oSubXmlFile.write(sRawXml);
            except:
                pass;
        if sCaller is None: pass; # pychecker - NOREF
        return None;

    def subXmlEnd(self, oFileWrapper):
        if oFileWrapper.oSubXmlFile is not None:
            try:
                oFileWrapper.oSubXmlFile.close();
                oFileWrapper.oSubXmlFile = None;
            except:
                pass;
        return None;



class RemoteReporter(ReporterBase):
    """
    Reporter that talks to the test manager server.
    """


    ## The XML sync min time (seconds).
    kcSecXmlFlushMin    = 30;
    ## The XML sync max time (seconds).
    kcSecXmlFlushMax    = 120;
    ## The XML sync idle time before flushing (seconds).
    kcSecXmlFlushIdle   = 5;
    ## The XML sync line count threshold.
    kcLinesXmlFlush     = 512;

    ## The retry timeout.
    kcSecTestManagerRetryTimeout    = 120;
    ## The request timeout.
    kcSecTestManagerRequestTimeout  = 30;


    def __init__(self):
        ReporterBase.__init__(self);
        self.sTestManagerUrl    = os.environ.get('TESTBOX_MANAGER_URL');
        self.sTestBoxUuid       = os.environ.get('TESTBOX_UUID');
        self.idTestBox          = int(os.environ.get('TESTBOX_ID'));
        self.idTestSet          = int(os.environ.get('TESTBOX_TEST_SET_ID'));
        self._asXml             = [];
        self._secTsXmlFlush     = utils.timestampSecond();
        self._secTsXmlLast      = self._secTsXmlFlush;
        self._fXmlFlushing      = False;
        self.oOutput            = sys.stdout; # Hack for __del__ output.
        self.fFlushEachLine     = True;
        self.fDebugXml          = 'TESTDRIVER_REPORTER_DEBUG_XML' in os.environ;

        # Prepare the TM connecting.
        from common import constants;
        if sys.version_info[0] >= 3:
            import urllib;
            self._fnUrlEncode       = urllib.parse.urlencode;                       # pylint: disable=no-member
            self._fnUrlParseQs      = urllib.parse.parse_qs;                        # pylint: disable=no-member
            self._oParsedTmUrl      = urllib.parse.urlparse(self.sTestManagerUrl);  # pylint: disable=no-member
            import http.client as httplib;                                      # pylint: disable=no-name-in-module,import-error
        else:
            import urllib;
            self._fnUrlEncode       = urllib.urlencode;                             # pylint: disable=no-member
            import urlparse;                                                        # pylint: disable=import-error
            self._fnUrlParseQs      = urlparse.parse_qs;                            # pylint: disable=no-member
            self._oParsedTmUrl      = urlparse.urlparse(self.sTestManagerUrl);      # pylint: disable=no-member
            import httplib;                                                     # pylint: disable=no-name-in-module,import-error

        if     sys.version_info[0] >= 3 \
           or (sys.version_info[0] == 2 and sys.version_info[1] >= 6):
            if self._oParsedTmUrl.scheme == 'https': # pylint: disable=no-member
                self._fnTmConnect = lambda: httplib.HTTPSConnection(self._oParsedTmUrl.hostname,
                                                                    timeout = self.kcSecTestManagerRequestTimeout);
            else:
                self._fnTmConnect = lambda: httplib.HTTPConnection( self._oParsedTmUrl.hostname,
                                                                    timeout = self.kcSecTestManagerRequestTimeout);
        else:
            if self._oParsedTmUrl.scheme == 'https': # pylint: disable=no-member
                self._fnTmConnect = lambda: httplib.HTTPSConnection(self._oParsedTmUrl.hostname);
            else:
                self._fnTmConnect = lambda: httplib.HTTPConnection( self._oParsedTmUrl.hostname);
        self._dHttpHeader = \
        {
            'Content-Type':     'application/x-www-form-urlencoded; charset=utf-8',
            'User-Agent':       'TestDriverReporter/%s.0 (%s, %s)' % (__version__, utils.getHostOs(), utils.getHostArch(),),
            'Accept':           'text/plain,application/x-www-form-urlencoded',
            'Accept-Encoding':  'identity',
            'Cache-Control':    'max-age=0',
            #'Connection':       'keep-alive',
        };

        dParams = {
            constants.tbreq.ALL_PARAM_TESTBOX_UUID:     self.sTestBoxUuid,
            constants.tbreq.ALL_PARAM_TESTBOX_ID:       self.idTestBox,
            constants.tbreq.RESULT_PARAM_TEST_SET_ID:   self.idTestSet,
        };
        self._sTmServerPath = '/%s/testboxdisp.py?%s' \
                            % ( self._oParsedTmUrl.path.strip('/'), # pylint: disable=no-member
                                self._fnUrlEncode(dParams), );

    def __del__(self):
        """Flush pending log messages?"""
        if self._asXml:
            self._xmlDoFlush(self._asXml, fRetry = True, fDtor = True);

    def _writeOutput(self, sText):
        """ Does the actual writing and flushing. """
        if sys.version_info[0] >= 3:
            print(sText, file = self.oOutput);
        else:
            print(sText.encode('ascii', 'replace'), file = self.oOutput);
        if self.fFlushEachLine: self.oOutput.flush();
        return None;

    #
    # Talking to TM.
    #

    def _processTmStatusResponse(self, oConn, sOperation, fClose = True):
        """
        Processes HTTP reponse from the test manager.
        Returns True, False or None.  None should be retried, the others not.
        May raise exception on HTTP issue (retry ok).
        """
        if sys.version_info[0] >= 3:    import http.client as httplib;  # pylint: disable=no-name-in-module,import-error
        else:                           import httplib;                 # pylint: disable=import-error
        from common import constants;

        # Read the response and (optionally) close the connection.
        oResponse = oConn.getresponse();
        try:
            sRspBody  = oResponse.read();
        except httplib.IncompleteRead as oXcpt:
            self._writeOutput('%s: %s: Warning: httplib.IncompleteRead: %s [expected %s, got %s]'
                              % (utils.getTimePrefix(), sOperation, oXcpt, oXcpt.expected, len(oXcpt.partial),));
            sRspBody = oXcpt.partial;
        if fClose is True:
            try:    oConn.close();
            except: pass;

        # Make sure it's a string which encoding we grok.
        if hasattr(sRspBody, 'decode'):
            sRspBody = sRspBody.decode('utf-8', 'ignore');

        # Check the content type.
        sContentType = oResponse.getheader('Content-Type');
        if sContentType is not None  and  sContentType == 'application/x-www-form-urlencoded; charset=utf-8':

            # Parse the body and check the RESULT parameter.
            dResponse = self._fnUrlParseQs(sRspBody, strict_parsing = True);
            sResult   = dResponse.get(constants.tbresp.ALL_PARAM_RESULT, None);
            if isinstance(sResult, list):
                sResult = sResult[0] if len(sResult) == 1 else '%d results' % (len(sResult),);

            if sResult is not None:
                if sResult == constants.tbresp.STATUS_ACK:
                    return True;
                if sResult == constants.tbresp.STATUS_NACK:
                    self._writeOutput('%s: %s: Failed (%s). (dResponse=%s)'
                                      % (utils.getTimePrefix(), sOperation, sResult, dResponse,));
                return False;

            self._writeOutput('%s: %s: Failed - dResponse=%s' % (utils.getTimePrefix(), sOperation, dResponse,));
        else:
            self._writeOutput('%s: %s: Unexpected Content-Type: %s' % (utils.getTimePrefix(), sOperation, sContentType,));
            self._writeOutput('%s: %s: Body: %s' % (utils.getTimePrefix(), sOperation, sRspBody,));
        return None;

    def _doUploadFile(self, oSrcFile, sSrcFilename, sDescription, sKind, sMime):
        """ Uploads the given file to the test manager. """

        # Prepare header and url.
        dHeader = dict(self._dHttpHeader);
        dHeader['Content-Type'] = 'application/octet-stream';
        self._writeOutput('%s: _doUploadFile: sHeader=%s' % (utils.getTimePrefix(), dHeader,));
        oSrcFile.seek(0, 2);
        cbFileSize = oSrcFile.tell();
        self._writeOutput('%s: _doUploadFile: size=%d' % (utils.getTimePrefix(), cbFileSize,));
        oSrcFile.seek(0);

        if cbFileSize <= 0: # The Test Manager will bitch if the file size is 0, so skip uploading.
            self._writeOutput('%s: _doUploadFile: Empty file, skipping upload' % utils.getTimePrefix());
            return False;

        from common import constants;
        sUrl = self._sTmServerPath + '&' \
             + self._fnUrlEncode({ constants.tbreq.UPLOAD_PARAM_NAME: os.path.basename(sSrcFilename),
                                   constants.tbreq.UPLOAD_PARAM_DESC: sDescription,
                                   constants.tbreq.UPLOAD_PARAM_KIND: sKind,
                                   constants.tbreq.UPLOAD_PARAM_MIME: sMime,
                                   constants.tbreq.ALL_PARAM_ACTION:  constants.tbreq.UPLOAD,
                                });

        # Retry loop.
        secStart = utils.timestampSecond();
        while True:
            try:
                oConn = self._fnTmConnect();
                oConn.request('POST', sUrl, oSrcFile.read(), dHeader);
                fRc = self._processTmStatusResponse(oConn, '_doUploadFile', fClose = True);
                oConn.close();
                if fRc is not None:
                    return fRc;
            except:
                logXcpt('warning: exception during UPLOAD request');

            if utils.timestampSecond() - secStart >= self.kcSecTestManagerRetryTimeout:
                self._writeOutput('%s: _doUploadFile: Timed out.' % (utils.getTimePrefix(),));
                break;
            try: oSrcFile.seek(0);
            except:
                logXcpt();
                break;
            self._writeOutput('%s: _doUploadFile: Retrying...' % (utils.getTimePrefix(), ));
            time.sleep(2);

        return False;

    def _doUploadString(self, sSrc, sSrcName, sDescription, sKind, sMime):
        """ Uploads the given string as a separate file to the test manager. """

        # Prepare header and url.
        dHeader = dict(self._dHttpHeader);
        dHeader['Content-Type'] = 'application/octet-stream';
        self._writeOutput('%s: _doUploadString: sHeader=%s' % (utils.getTimePrefix(), dHeader,));
        self._writeOutput('%s: _doUploadString: size=%d' % (utils.getTimePrefix(), sys.getsizeof(sSrc),));

        from common import constants;
        sUrl = self._sTmServerPath + '&' \
             + self._fnUrlEncode({ constants.tbreq.UPLOAD_PARAM_NAME: os.path.basename(sSrcName),
                                   constants.tbreq.UPLOAD_PARAM_DESC: sDescription,
                                   constants.tbreq.UPLOAD_PARAM_KIND: sKind,
                                   constants.tbreq.UPLOAD_PARAM_MIME: sMime,
                                   constants.tbreq.ALL_PARAM_ACTION:  constants.tbreq.UPLOAD,
                                });

        # Retry loop.
        secStart = utils.timestampSecond();
        while True:
            try:
                oConn = self._fnTmConnect();
                oConn.request('POST', sUrl, sSrc, dHeader);
                fRc = self._processTmStatusResponse(oConn, '_doUploadString', fClose = True);
                oConn.close();
                if fRc is not None:
                    return fRc;
            except:
                logXcpt('warning: exception during UPLOAD request');

            if utils.timestampSecond() - secStart >= self.kcSecTestManagerRetryTimeout:
                self._writeOutput('%s: _doUploadString: Timed out.' % (utils.getTimePrefix(),));
                break;
            self._writeOutput('%s: _doUploadString: Retrying...' % (utils.getTimePrefix(), ));
            time.sleep(2);

        return False;

    def _xmlDoFlush(self, asXml, fRetry = False, fDtor = False):
        """
        The code that does the actual talking to the server.
        Used by both xmlFlush and __del__.
        """
        secStart = utils.timestampSecond();
        while True:
            fRc = None;
            try:
                # Post.
                from common import constants;
                sPostBody = self._fnUrlEncode({constants.tbreq.XML_RESULT_PARAM_BODY: '\n'.join(asXml),});
                oConn = self._fnTmConnect();
                oConn.request('POST',
                              self._sTmServerPath + ('&%s=%s' % (constants.tbreq.ALL_PARAM_ACTION, constants.tbreq.XML_RESULTS)),
                              sPostBody,
                              self._dHttpHeader);

                fRc = self._processTmStatusResponse(oConn, '_xmlDoFlush', fClose = True);
                if fRc is True:
                    if self.fDebugXml:
                        self._writeOutput('_xmlDoFlush:\n%s' % ('\n'.join(asXml),));
                    return (None, False);
                if fRc is False:
                    self._writeOutput('_xmlDoFlush: Failed - we should abort the test, really.');
                    return (None, True);
            except Exception as oXcpt:
                if not fDtor:
                    logXcpt('warning: exception during XML_RESULTS request');
                else:
                    self._writeOutput('warning: exception during XML_RESULTS request: %s' % (oXcpt,));

            if   fRetry is not True \
              or utils.timestampSecond() - secStart >= self.kcSecTestManagerRetryTimeout:
                break;
            time.sleep(2);

        return (asXml, False);


    #
    # Overridden methods.
    #

    def isLocal(self):
        return False;

    def log(self, iLevel, sText, sCaller, sTsPrf):
        if iLevel <= self.iVerbose:
            if self.iDebug <= 0:
                sLogText = '%s %s' % (sTsPrf, sText);
            elif self.iDebug <= 1:
                sLogText = '%s %30s: %s' % (sTsPrf, sCaller, sText);
            else:
                sLogText = '%s e=%u %30s: %s' % (sTsPrf, self.cErrors, sCaller, sText);
            self._writeOutput(sLogText);
        return 0;

    def addLogFile(self, oSrcFile, sSrcFilename, sAltName, sDescription, sKind, sCaller, sTsPrf):
        fRc = True;
        if    sKind in [ 'text', 'log', 'process'] \
           or sKind.startswith('log/') \
           or sKind.startswith('info/') \
           or sKind.startswith('process/'):
            self.log(0, '*** Uploading "%s" - KIND: "%s" - DESC: "%s" ***'
                        % (sSrcFilename, sKind, sDescription),  sCaller, sTsPrf);
            self.xmlFlush();
            g_oLock.release();
            try:
                self._doUploadFile(oSrcFile, sAltName, sDescription, sKind, 'text/plain');
            finally:
                g_oLock.acquire();
        elif sKind.startswith('screenshot/'):
            self.log(0, '*** Uploading "%s" - KIND: "%s" - DESC: "%s" ***'
                        % (sSrcFilename, sKind, sDescription),  sCaller, sTsPrf);
            self.xmlFlush();
            g_oLock.release();
            try:
                self._doUploadFile(oSrcFile, sAltName, sDescription, sKind, 'image/png');
            finally:
                g_oLock.acquire();
        elif sKind.startswith('screenrecording/'):
            self.log(0, '*** Uploading "%s" - KIND: "%s" - DESC: "%s" ***'
                        % (sSrcFilename, sKind, sDescription),  sCaller, sTsPrf);
            self.xmlFlush();
            g_oLock.release();
            try:
                self._doUploadFile(oSrcFile, sAltName, sDescription, sKind, 'video/webm');
            finally:
                g_oLock.acquire();
        elif sKind.startswith('misc/'):
            self.log(0, '*** Uploading "%s" - KIND: "%s" - DESC: "%s" ***'
                        % (sSrcFilename, sKind, sDescription),  sCaller, sTsPrf);
            self.xmlFlush();
            g_oLock.release();
            try:
                self._doUploadFile(oSrcFile, sAltName, sDescription, sKind, 'application/octet-stream');
            finally:
                g_oLock.acquire();
        else:
            self.log(0, '*** UNKNOWN FILE "%s" - KIND "%s" - DESC "%s" ***'
                     % (sSrcFilename, sKind, sDescription),  sCaller, sTsPrf);
        return fRc;

    def addLogString(self, sLog, sLogName, sDescription, sKind, sCaller, sTsPrf):
        fRc = True;
        if    sKind in [ 'text', 'log', 'process'] \
           or sKind.startswith('log/') \
           or sKind.startswith('info/') \
           or sKind.startswith('process/'):
            self.log(0, '*** Uploading "%s" - KIND: "%s" - DESC: "%s" ***'
                        % (sLogName, sKind, sDescription),  sCaller, sTsPrf);
            self.xmlFlush();
            g_oLock.release();
            try:
                self._doUploadString(sLog, sLogName, sDescription, sKind, 'text/plain');
            finally:
                g_oLock.acquire();
        else:
            self.log(0, '*** UNKNOWN FILE "%s" - KIND "%s" - DESC "%s" ***'
                     % (sLogName, sKind, sDescription),  sCaller, sTsPrf);
        return fRc;

    def xmlFlush(self, fRetry = False, fForce = False):
        """
        Flushes the XML back log. Called with the lock held, may leave it
        while communicating with the server.
        """
        if not self._fXmlFlushing:
            asXml = self._asXml;
            self._asXml = [];
            if asXml or fForce is True:
                self._fXmlFlushing = True;

                g_oLock.release();
                try:
                    (asXml, fIncErrors) = self._xmlDoFlush(asXml, fRetry = fRetry);
                finally:
                    g_oLock.acquire();

                if fIncErrors:
                    self.testIncErrors();

                self._fXmlFlushing = False;
                if asXml is None:
                    self._secTsXmlFlush = utils.timestampSecond();
                else:
                    self._asXml = asXml + self._asXml;
                return True;

            self._secTsXmlFlush = utils.timestampSecond();
        return False;

    def _xmlFlushIfNecessary(self, fPolling = False, sDebug = None):
        """Flushes the XML back log if necessary."""
        tsNow = utils.timestampSecond();
        cSecs     = tsNow - self._secTsXmlFlush;
        cSecsLast = tsNow - self._secTsXmlLast;
        if fPolling is not True:
            self._secTsXmlLast = tsNow;

        # Absolute flush thresholds.
        if cSecs >= self.kcSecXmlFlushMax:
            return self.xmlFlush();
        if len(self._asXml) >= self.kcLinesXmlFlush:
            return self.xmlFlush();

        # Flush if idle long enough.
        if    cSecs     >= self.kcSecXmlFlushMin \
          and cSecsLast >= self.kcSecXmlFlushIdle:
            return self.xmlFlush();

        _ = sDebug;
        return False;

    def _xmlWrite(self, asText, fIndent = True):
        """XML output function for the reporter."""
        self._asXml += asText;
        self._xmlFlushIfNecessary();
        _ = fIndent; # No pretty printing, thank you.
        return None;

    def subXmlStart(self, oFileWrapper):
        oFileWrapper.sXmlBuffer = '';
        return None;

    def subXmlWrite(self, oFileWrapper, sRawXml, sCaller):
        oFileWrapper.sXmlBuffer += sRawXml;
        _ = sCaller;
        return None;

    def subXmlEnd(self, oFileWrapper):
        sRawXml = oFileWrapper.sXmlBuffer;
        ## @todo should validate the document here and maybe auto terminate things.  Adding some hints to have the server do
        # this instead.
        g_oLock.acquire();
        try:
            self._asXml += [ '<PushHint testdepth="%d"/>' % (len(self.atTests),),
                             sRawXml,
                             '<PopHint  testdepth="%d"/>' % (len(self.atTests),),];
            self._xmlFlushIfNecessary();
        finally:
            g_oLock.release();
        return None;

    def doPollWork(self, sDebug = None):
        if self._asXml:
            g_oLock.acquire();
            try:
                self._xmlFlushIfNecessary(fPolling = True, sDebug = sDebug);
            finally:
                g_oLock.release();
        return None;


#
# Helpers
#

g_fnComXcptFormatter = None;

def setComXcptFormatter(fnCallback):
    """
    Install callback for prettier COM exception formatting.

    The callback replaces the work done by format_exception_only() and
    takes the same arguments.  It returns None if not interested in the
    exception.
    """
    global g_fnComXcptFormatter;
    g_fnComXcptFormatter = fnCallback;
    return True;

def formatExceptionOnly(oType, oXcpt, sCaller, sTsPrf):
    """
    Wrapper around traceback.format_exception_only and __g_fnComXcptFormatter.
    """
    #asRet = ['oType=%s type(oXcpt)=%s' % (oType, type(oXcpt),)];
    asRet = [];

    # Try the callback first.
    fnCallback = g_fnComXcptFormatter;
    if fnCallback:
        try:
            asRetCb = fnCallback(oType, oXcpt);
            if asRetCb:
                return asRetCb;
                #asRet += asRetCb;
        except:
            g_oReporter.log(0, '** internal-error: Hit exception #2 in __g_fnComXcptFormatter! %s'
                            % (traceback.format_exc()), sCaller, sTsPrf);
            asRet += ['internal error: exception in __g_fnComXcptFormatter'];

    # Now try format_exception_only:
    try:
        asRet += traceback.format_exception_only(oType, oXcpt);
    except:
        g_oReporter.log(0, '** internal-error: Hit exception #2 in format_exception_only! %s'
                        % (traceback.format_exc()), sCaller, sTsPrf);
        asRet += ['internal error: Exception in format_exception_only!'];
    return asRet;


def logXcptWorker(iLevel, fIncErrors, sPrefix="", sText=None, cFrames=1):
    """
    Log an exception, optionally with a preceeding message and more than one
    call frame.
    """
    g_oLock.acquire();
    try:

        if fIncErrors:
            g_oReporter.testIncErrors();

        ## @todo skip all this if iLevel is too high!

        # Try get exception info.
        sTsPrf = utils.getTimePrefix();
        try:
            oType, oValue, oTraceback = sys.exc_info();
        except:
            oType = oValue = oTraceback = None;
        if oType is not None:

            # Try format the info
            try:
                rc      = 0;
                sCaller = utils.getCallerName(oTraceback.tb_frame);
                if sText is not None:
                    rc = g_oReporter.log(iLevel, "%s%s" % (sPrefix, sText), sCaller, sTsPrf);
                asInfo = None;
                try:
                    asInfo = formatExceptionOnly(oType, oValue, sCaller, sTsPrf);
                    atEntries = traceback.extract_tb(oTraceback);
                    atEntries.reverse();
                    if cFrames is not None and cFrames <= 1:
                        if atEntries:
                            asInfo = asInfo + traceback.format_list(atEntries[:1]);
                    else:
                        asInfo.append('Traceback (stack order):')
                        if cFrames is not None and cFrames < len(atEntries):
                            asInfo = asInfo + traceback.format_list(atEntries[:cFrames]);
                        else:
                            asInfo = asInfo + traceback.format_list(atEntries);
                        asInfo.append('Stack:')
                        asInfo = asInfo + traceback.format_stack(oTraceback.tb_frame.f_back, cFrames);
                except:
                    g_oReporter.log(0, '** internal-error: Hit exception #2! %s' % (traceback.format_exc()), sCaller, sTsPrf);

                if asInfo:
                    # Do the logging.
                    for sItem in asInfo:
                        asLines = sItem.splitlines();
                        for sLine in asLines:
                            rc = g_oReporter.log(iLevel, '%s%s' % (sPrefix, sLine), sCaller, sTsPrf);

                else:
                    g_oReporter.log(iLevel, 'No exception info...', sCaller, sTsPrf);
                    rc = -3;
            except:
                g_oReporter.log(0, '** internal-error: Hit exception! %s' % (traceback.format_exc()), None, sTsPrf);
                rc = -2;
        else:
            g_oReporter.log(0, '** internal-error: No exception! %s'
                            % (utils.getCallerName(iFrame=3)), utils.getCallerName(iFrame=3), sTsPrf);
            rc = -1;

    finally:
        g_oLock.release();
    return rc;


#
# The public Classes
#
class FileWrapper(object):
    """ File like class for TXS EXEC and similar. """
    def __init__(self, sPrefix):
        self.sPrefix = sPrefix;

    def __del__(self):
        self.close();

    def close(self):
        """ file.close """
        # Nothing to be done.
        return;

    def read(self, cb):
        """file.read"""
        _ = cb;
        return "";

    def write(self, sText):
        """file.write"""
        if not utils.isString(sText):
            if isinstance(sText, array.array):
                try:
                    if sys.version_info < (3, 9, 0):
                        # Removed since Python 3.9.
                        sText = sText.tostring(); # pylint: disable=no-member
                    else:
                        sText = sText.tobytes();
                except:
                    pass;
            if hasattr(sText, 'decode'):
                try:
                    sText = sText.decode('utf-8', 'ignore');
                except:
                    pass;
        g_oLock.acquire();
        try:
            sTsPrf  = utils.getTimePrefix();
            sCaller = utils.getCallerName();
            asLines = sText.splitlines();
            for sLine in asLines:
                g_oReporter.log(0, '%s: %s' % (self.sPrefix, sLine), sCaller, sTsPrf);
        except:
            traceback.print_exc();
        finally:
            g_oLock.release();
        return None;

class FileWrapperTestPipe(object):
    """
    File like class for the test pipe (TXS EXEC and similar).

    This is also used to submit XML test result files.
    """
    def __init__(self):
        self.sPrefix    = '';
        self.fStarted   = False;
        self.fClosed    = False;
        self.sTagBuffer = None;
        self.cTestDepth = 0;
        self.acTestErrors = [];

    def __del__(self):
        self.close();

    def close(self):
        """ file.close """
        if self.fStarted is True and self.fClosed is False:
            self.fClosed = True;

            # Close open <Test> elements:
            if self.cTestDepth > 0:
                sNow = utils.getIsoTimestamp()
                cErrors = 0;
                while self.cTestDepth > 0:
                    self.cTestDepth -= 1;
                    if self.acTestErrors:
                        cErrors += self.acTestErrors.pop();
                    cErrors += 1;
                    g_oReporter.subXmlWrite(self,
                                            '\n%s  <Failed timestamp="%s" errors="%s"/>\n%s</Test>\n'
                                            % ('  ' * self.cTestDepth, sNow, cErrors, '  ' * self.cTestDepth),
                                            utils.getCallerName());

            # Tell the reporter that the XML input is done.
            try:    g_oReporter.subXmlEnd(self);
            except:
                try:    traceback.print_exc();
                except: pass;
        return True;

    def read(self, cb = None):
        """file.read"""
        _ = cb;
        return "";

    def write(self, sText):
        """file.write"""
        # lazy start.
        if self.fStarted is not True:
            try:
                g_oReporter.subXmlStart(self);
            except:
                traceback.print_exc();
            self.fStarted = True;

        # Turn non-string stuff into strings.
        if not utils.isString(sText):
            if isinstance(sText, array.array):
                try:
                    if sys.version_info < (3, 9, 0):
                        # Removed since Python 3.9.
                        sText = sText.tostring(); # pylint: disable=no-member
                    else:
                        sText = sText.tobytes();
                except:
                    pass;
            if hasattr(sText, 'decode'):
                try:    sText = sText.decode('utf-8', 'ignore');
                except: pass;

        try:
            #
            # Write the XML to the reporter.
            #
            g_oReporter.subXmlWrite(self, sText, utils.getCallerName());

            #
            # Parse the supplied text and look for <Failed.../> tags to keep track of the
            # error counter. This is only a very lazy aproach.
            #
            idxText = 0;
            while sText:
                if self.sTagBuffer is None:
                    # Look for the start of a tag.
                    idxStart = sText.find('<', idxText);
                    if idxStart != -1:
                        # If the end was found inside the current buffer, parse the line,
                        # otherwise we have to save it for later.
                        idxEnd = sText.find('>', idxStart);
                        if idxEnd != -1:
                            self._processXmlElement(sText[idxStart:idxEnd+1]);
                            idxText = idxEnd;
                        else:
                            self.sTagBuffer = sText[idxStart:];
                            break;
                    else:
                        break;
                else:
                    # Search for the end of the tag and parse the whole tag.
                    assert(idxText == 0);
                    idxEnd = sText.find('>');
                    if idxEnd != -1:
                        self._processXmlElement(self.sTagBuffer + sText[:idxEnd+1]);
                        self.sTagBuffer = None;
                        idxText = idxEnd;
                    else:
                        self.sTagBuffer = self.sTagBuffer + sText[idxText:];
                        break;
        except:
            traceback.print_exc();
        return None;

    def _processXmlElement(self, sElement):
        """
        Processes a complete XML tag.

        We handle the 'Failed' tag to keep track of the error counter.
        We also track 'Test' tags to make sure we close with all of them properly closed.
        """
        # Make sure we don't parse any space between < and the element name.
        sElement = sElement.strip();

        # Find the end of the name
        idxEndName = sElement.find(' ');
        if idxEndName == -1:
            idxEndName = sElement.find('>');
            if idxEndName >= 0:
                if sElement[idxEndName - 1] == '/':
                    idxEndName -= 1;
            else:
                idxEndName = len(sElement);
        sElementName = sElement[1:idxEndName];

        # <Failed>:
        if sElementName == 'Failed':
            g_oLock.acquire();
            try:
                g_oReporter.testIncErrors();
            finally:
                g_oLock.release();
            if self.acTestErrors:
                self.acTestErrors[-1] += 1; # get errors attrib
        # <Test>
        elif sElementName == 'Test':
            self.cTestDepth += 1;
            self.acTestErrors.append(0);
        # </Test>
        elif sElementName == '/Test':
            self.cTestDepth -= 1;
            if self.acTestErrors:
                cErrors = self.acTestErrors.pop();
                if self.acTestErrors:
                    self.acTestErrors[-1] += cErrors;


#
# The public APIs.
#

def log(sText, sCaller = None):
    """Writes the specfied text to the log."""
    g_oLock.acquire();
    try:
        rc = g_oReporter.log(1, sText, sCaller if sCaller else utils.getCallerName(), utils.getTimePrefix());
    except:
        rc = -1;
    finally:
        g_oLock.release();
    return rc;

def logXcpt(sText=None, cFrames=1):
    """
    Log an exception, optionally with a preceeding message and more than one
    call frame.
    """
    return logXcptWorker(1, False, "", sText, cFrames);

def log2(sText, sCaller = None):
    """Log level 2: Writes the specfied text to the log."""
    g_oLock.acquire();
    try:
        rc = g_oReporter.log(2, sText, sCaller if sCaller else utils.getCallerName(), utils.getTimePrefix());
    except:
        rc = -1;
    finally:
        g_oLock.release();
    return rc;

def log2Xcpt(sText=None, cFrames=1):
    """
    Log level 2: Log an exception, optionally with a preceeding message and
    more than one call frame.
    """
    return logXcptWorker(2, False, "", sText, cFrames);

def log3(sText, sCaller = None):
    """Log level 3: Writes the specfied text to the log."""
    g_oLock.acquire();
    try:
        rc = g_oReporter.log(3, sText, sCaller if sCaller else utils.getCallerName(), utils.getTimePrefix());
    except:
        rc = -1;
    finally:
        g_oLock.release();
    return rc;

def log3Xcpt(sText=None, cFrames=1):
    """
    Log level 3: Log an exception, optionally with a preceeding message and
    more than one call frame.
    """
    return logXcptWorker(3, False, "", sText, cFrames);

def log4(sText, sCaller = None):
    """Log level 4: Writes the specfied text to the log."""
    g_oLock.acquire();
    try:
        rc = g_oReporter.log(4, sText, sCaller if sCaller else utils.getCallerName(), utils.getTimePrefix());
    except:
        rc = -1;
    finally:
        g_oLock.release();
    return rc;

def log4Xcpt(sText=None, cFrames=1):
    """
    Log level 4: Log an exception, optionally with a preceeding message and
    more than one call frame.
    """
    return logXcptWorker(4, False, "", sText, cFrames);

def log5(sText, sCaller = None):
    """Log level 2: Writes the specfied text to the log."""
    g_oLock.acquire();
    try:
        rc = g_oReporter.log(5, sText, sCaller if sCaller else utils.getCallerName(), utils.getTimePrefix());
    except:
        rc = -1;
    finally:
        g_oLock.release();
    return rc;

def log5Xcpt(sText=None, cFrames=1):
    """
    Log level 5: Log an exception, optionally with a preceeding message and
    more than one call frame.
    """
    return logXcptWorker(5, False, "", sText, cFrames);

def log6(sText, sCaller = None):
    """Log level 6: Writes the specfied text to the log."""
    g_oLock.acquire();
    try:
        rc = g_oReporter.log(6, sText, sCaller if sCaller else utils.getCallerName(), utils.getTimePrefix());
    except:
        rc = -1;
    finally:
        g_oLock.release();
    return rc;

def log6Xcpt(sText=None, cFrames=1):
    """
    Log level 6: Log an exception, optionally with a preceeding message and
    more than one call frame.
    """
    return logXcptWorker(6, False, "", sText, cFrames);

def maybeErr(fIsError, sText):
    """ Maybe error or maybe normal log entry. """
    if fIsError is True:
        return error(sText, sCaller = utils.getCallerName());
    return log(sText, sCaller = utils.getCallerName());

def maybeErrXcpt(fIsError, sText=None, cFrames=1):
    """ Maybe error or maybe normal log exception entry. """
    if fIsError is True:
        return errorXcpt(sText, cFrames);
    return logXcpt(sText, cFrames);

def maybeLog(fIsNotError, sText):
    """ Maybe error or maybe normal log entry. """
    if fIsNotError is not True:
        return error(sText, sCaller = utils.getCallerName());
    return log(sText, sCaller = utils.getCallerName());

def maybeLogXcpt(fIsNotError, sText=None, cFrames=1):
    """ Maybe error or maybe normal log exception entry. """
    if fIsNotError is not True:
        return errorXcpt(sText, cFrames);
    return logXcpt(sText, cFrames);

def error(sText, sCaller = None):
    """
    Writes the specfied error message to the log.

    This will add an error to the current test.

    Always returns False for the convenience of methods returning boolean
    success indicators.
    """
    g_oLock.acquire();
    try:
        g_oReporter.testIncErrors();
        g_oReporter.log(0, '** error: %s' % (sText), sCaller if sCaller else utils.getCallerName(), utils.getTimePrefix());
    except:
        pass;
    finally:
        g_oLock.release();
    return False;

def errorXcpt(sText=None, cFrames=1):
    """
    Log an error caused by an exception.  If sText is given, it will preceed
    the exception information.  cFrames can be used to display more stack.

    This will add an error to the current test.

    Always returns False for the convenience of methods returning boolean
    success indicators.
    """
    logXcptWorker(0, True, '** error: ', sText, cFrames);
    return False;

def errorTimeout(sText):
    """
    Flags the current test as having timed out and writes the specified message to the log.

    This will add an error to the current test.

    Always returns False for the convenience of methods returning boolean
    success indicators.
    """
    g_oLock.acquire();
    try:
        g_oReporter.testSetTimedOut();
        g_oReporter.log(0, '** timeout-error: %s' % (sText), utils.getCallerName(), utils.getTimePrefix());
    except:
        pass;
    finally:
        g_oLock.release();
    return False;

def fatal(sText):
    """
    Writes a fatal error to the log.

    This will add an error to the current test.

    Always returns False for the convenience of methods returning boolean
    success indicators.
    """
    g_oLock.acquire();
    try:
        g_oReporter.testIncErrors();
        g_oReporter.log(0, '** fatal error: %s' % (sText), utils.getCallerName(), utils.getTimePrefix());
    except:
        pass
    finally:
        g_oLock.release();
    return False;

def fatalXcpt(sText=None, cFrames=1):
    """
    Log a fatal error caused by an exception.  If sText is given, it will
    preceed the exception information.  cFrames can be used to display more
    stack.

    This will add an error to the current test.

    Always returns False for the convenience of methods returning boolean
    success indicators.
    """
    logXcptWorker(0, True, "** fatal error: ", sText, cFrames);
    return False;

def addLogFile(sFilename, sKind, sDescription = '', sAltName = None):
    """
    Adds the specified log file to the report if the file exists.

    The sDescription is a free form description of the log file.

    The sKind parameter is for adding some machine parsable hint what kind of
    log file this really is.

    Returns True on success, False on failure (no ENOENT errors are logged).
    """
    sTsPrf  = utils.getTimePrefix();
    sCaller = utils.getCallerName();
    fRc     = False;
    if sAltName is None:
        sAltName = sFilename;

    try:
        oSrcFile = utils.openNoInherit(sFilename, 'rb');
    except IOError as oXcpt:
        if oXcpt.errno != errno.ENOENT:
            logXcpt('addLogFile(%s,%s,%s)' % (sFilename, sDescription, sKind));
        else:
            logXcpt('addLogFile(%s,%s,%s) IOError' % (sFilename, sDescription, sKind));
    except:
        logXcpt('addLogFile(%s,%s,%s)' % (sFilename, sDescription, sKind));
    else:
        g_oLock.acquire();
        try:
            fRc = g_oReporter.addLogFile(oSrcFile, sFilename, sAltName, sDescription, sKind, sCaller, sTsPrf);
        finally:
            g_oLock.release();
            oSrcFile.close();
    return fRc;

def addLogString(sLog, sLogName, sKind, sDescription = ''):
    """
    Adds the specified log string to the report.

    The sLog parameter sets the name of the log file.

    The sDescription is a free form description of the log file.

    The sKind parameter is for adding some machine parsable hint what kind of
    log file this really is.

    Returns True on success, False on failure (no ENOENT errors are logged).
    """
    sTsPrf  = utils.getTimePrefix();
    sCaller = utils.getCallerName();
    fRc     = False;

    g_oLock.acquire();
    try:
        fRc = g_oReporter.addLogString(sLog, sLogName, sDescription, sKind, sCaller, sTsPrf);
    finally:
        g_oLock.release();
    return fRc;

def isLocal():
    """Is this a local reporter?"""
    return g_oReporter.isLocal()

def incVerbosity():
    """Increases the verbosity level."""
    return g_oReporter.incVerbosity()

def incDebug():
    """Increases the debug level."""
    return g_oReporter.incDebug()

def getVerbosity():
    """Returns the current verbosity level."""
    return g_oReporter.getVerbosity()

def getDebug():
    """Returns the current debug level."""
    return g_oReporter.getDebug()

def appendToProcessName(sAppend):
    """
    Appends sAppend to the base process name.
    Returns the new process name.
    """
    return g_oReporter.appendToProcessName(sAppend);

def getErrorCount():
    """
    Get the current error count for the entire test run.
    """
    g_oLock.acquire();
    try:
        cErrors = g_oReporter.cErrors;
    finally:
        g_oLock.release();
    return cErrors;

def doPollWork(sDebug = None):
    """
    This can be called from wait loops and similar to make the reporter call
    home with pending XML and such.
    """
    g_oReporter.doPollWork(sDebug);
    return None;


#
# Test reporting, a bit similar to RTTestI*.
#

def testStart(sName):
    """
    Starts a new test (pushes it).
    """
    g_oLock.acquire();
    try:
        rc = g_oReporter.testStart(sName, utils.getCallerName());
    finally:
        g_oLock.release();
    return rc;

def testValue(sName, sValue, sUnit):
    """
    Reports a benchmark value or something simiarlly useful.
    """
    g_oLock.acquire();
    try:
        rc = g_oReporter.testValue(sName, str(sValue), sUnit, utils.getCallerName());
    finally:
        g_oLock.release();
    return rc;

def testFailure(sDetails):
    """
    Reports a failure.
    We count these calls and testDone will use them to report PASSED or FAILED.

    Returns False so that a return False line can be saved.
    """
    g_oLock.acquire();
    try:
        g_oReporter.testFailure(sDetails, utils.getCallerName());
    finally:
        g_oLock.release();
    return False;

def testFailureXcpt(sDetails = ''):
    """
    Reports a failure with exception.
    We count these calls and testDone will use them to report PASSED or FAILED.

    Returns False so that a return False line can be saved.
    """
    # Extract exception info.
    try:
        oType, oValue, oTraceback  = sys.exc_info();
    except:
        oType = oValue, oTraceback = None;
    if oType is not None:
        sCaller = utils.getCallerName(oTraceback.tb_frame);
        sXcpt   = ' '.join(formatExceptionOnly(oType, oValue, sCaller, utils.getTimePrefix()));
    else:
        sCaller = utils.getCallerName();
        sXcpt   = 'No exception at %s' % (sCaller,);

    # Use testFailure to do the work.
    g_oLock.acquire();
    try:
        if sDetails == '':
            g_oReporter.testFailure('Exception: %s' % (sXcpt,), sCaller);
        else:
            g_oReporter.testFailure('%s: %s' % (sDetails, sXcpt), sCaller);
    finally:
        g_oLock.release();
    return False;

def testDone(fSkipped = False):
    """
    Completes the current test (pops it), logging PASSED / FAILURE.

    Returns a tuple with the name of the test and its error count.
    """
    g_oLock.acquire();
    try:
        rc = g_oReporter.testDone(fSkipped, utils.getCallerName());
    finally:
        g_oLock.release();
    return rc;

def testErrorCount():
    """
    Gets the error count of the current test.

    Returns the number of errors.
    """
    g_oLock.acquire();
    try:
        cErrors = g_oReporter.testErrorCount();
    finally:
        g_oLock.release();
    return cErrors;

def testCleanup():
    """
    Closes all open tests with a generic error condition.

    Returns True if no open tests, False if something had to be closed with failure.
    """
    g_oLock.acquire();
    try:
        fRc = g_oReporter.testCleanup(utils.getCallerName());
        g_oReporter.xmlFlush(fRetry = False, fForce = True);
    finally:
        g_oLock.release();
        fRc = False;
    return fRc;


#
# Sub XML stuff.
#

def addSubXmlFile(sFilename):
    """
    Adds a sub-xml result file to the party.
    """
    fRc = False;
    try:
        oSrcFile = utils.openNoInherit(sFilename, 'r');
    except IOError as oXcpt:
        if oXcpt.errno != errno.ENOENT:
            logXcpt('addSubXmlFile(%s)' % (sFilename,));
    except:
        logXcpt('addSubXmlFile(%s)' % (sFilename,));
    else:
        try:
            oWrapper = FileWrapperTestPipe()
            oWrapper.write(oSrcFile.read());
            oWrapper.close();
        except:
            logXcpt('addSubXmlFile(%s)' % (sFilename,));
        oSrcFile.close();

    return fRc;


#
# Other useful debugging tools.
#

def logAllStacks(cFrames = None):
    """
    Logs the stacks of all python threads.
    """
    sTsPrf  = utils.getTimePrefix();
    sCaller = utils.getCallerName();
    g_oLock.acquire();

    cThread = 0;
    for idThread, oStack in sys._current_frames().items(): # >=2.5, a bit ugly - pylint: disable=protected-access
        try:
            if cThread > 0:
                g_oReporter.log(1, '', sCaller, sTsPrf);
            g_oReporter.log(1, 'Thread %s (%#x)' % (idThread, idThread), sCaller, sTsPrf);
            try:
                asInfo = traceback.format_stack(oStack, cFrames);
            except:
                g_oReporter.log(1, '  Stack formatting failed w/ exception', sCaller, sTsPrf);
            else:
                for sInfo in asInfo:
                    asLines = sInfo.splitlines();
                    for sLine in asLines:
                        g_oReporter.log(1, sLine, sCaller, sTsPrf);
        except:
            pass;
        cThread += 1;

    g_oLock.release();
    return None;

def checkTestManagerConnection():
    """
    Checks the connection to the test manager.

    Returns True if the connection is fine, False if not, None if not remote
    reporter.

    Note! This as the sideeffect of flushing XML.
    """
    g_oLock.acquire();
    try:
        fRc = g_oReporter.xmlFlush(fRetry = False, fForce = True);
    finally:
        g_oLock.release();
        fRc = False;
    return fRc;

def flushall(fSkipXml = False):
    """
    Flushes all output streams, both standard and logger related.
    This may also push data to the remote test manager.
    """
    try:    sys.stdout.flush();
    except: pass;
    try:    sys.stderr.flush();
    except: pass;

    if fSkipXml is not True:
        g_oLock.acquire();
        try:
            g_oReporter.xmlFlush(fRetry = False);
        finally:
            g_oLock.release();

    return True;


#
# Module initialization.
#

def _InitReporterModule():
    """
    Instantiate the test reporter.
    """
    global g_oReporter, g_sReporterName

    g_sReporterName = os.getenv("TESTBOX_REPORTER", "local");
    if g_sReporterName == "local":
        g_oReporter = LocalReporter();
    elif g_sReporterName == "remote":
        g_oReporter = RemoteReporter(); # Correct, but still plain stupid. pylint: disable=redefined-variable-type
    else:
        print(os.path.basename(__file__) + ": Unknown TESTBOX_REPORTER value: '" + g_sReporterName + "'", file = sys.stderr);
        raise Exception("Unknown TESTBOX_REPORTER value '" + g_sReporterName + "'");

if __name__ != "checker": # pychecker avoidance.
    _InitReporterModule();
