# -*- coding: utf-8 -*-
# $Id: testboxtasks.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
TestBox Script - Async Tasks.
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
from datetime import datetime
import os
import re
import signal;
import sys
import subprocess
import threading
import time

# Validation Kit imports.
from common import constants
from common import utils;
from common import webutils;
import testboxcommons

# Figure where we are.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksTestScriptDir = os.path.dirname(os.path.abspath(__file__));



class TestBoxBaseTask(object):
    """
    Asynchronous task employing a thread to do the actual work.
    """

    ## Time to wait for a task to terminate.
    kcSecTerminateTimeout = 60

    def __init__(self, oTestBoxScript, cSecTimeout, fnThreadProc):
        self._oTestBoxScript    = oTestBoxScript;
        self._cSecTimeout       = cSecTimeout;
        self._tsSecStarted      = utils.timestampSecond();
        self.__oRLock           = threading.RLock();
        self._oCv               = threading.Condition(self.__oRLock);
        self._fRunning          = True;     # Protected by lock.
        self._fShouldTerminate  = False;    # Protected by lock.

        # Spawn the worker thread.
        self._oThread           = threading.Thread(target=fnThreadProc);
        self._oThread.daemon    = True;
        self._oThread.start();

    def _lock(self):
        """ Take the CV lock. """
        self._oCv.acquire();

    def _unlock(self):
        """ Release the CV lock. """
        self._oCv.release();

    def _complete(self):
        """
        Indicate that the task is complete, waking up the main thread.
        Usually called at the end of the thread procedure.
        """
        self._lock();
        self._fRunning = False;
        self._oCv.notifyAll(); # pylint: disable=deprecated-method
        self._unlock();

    def isRunning(self):
        """ Check if the task is still running. """
        self._lock();
        fRunning = self._fRunning;
        self._unlock();
        return fRunning;

    def wait(self, cSecTimeout):
        """ Wait for the task to complete. """
        self._lock();
        fRunning = self._fRunning;
        if fRunning is True and cSecTimeout > 0:
            self._oCv.wait(cSecTimeout)
        self._unlock();
        return fRunning;

    def terminate(self, cSecTimeout = kcSecTerminateTimeout):
        """ Terminate the task. """
        self._lock();
        self._fShouldTerminate = True;
        self._unlock();

        return self.wait(cSecTimeout);

    def _shouldTerminate(self):
        """
        Returns True if we should terminate, False if not.
        """
        self._lock();
        fShouldTerminate = self._fShouldTerminate is True;
        self._unlock();
        return fShouldTerminate;


class TestBoxTestDriverTask(TestBoxBaseTask):
    """
    Base class for tasks involving test drivers.
    """

    ## When to flush the backlog of log messages.
    kcchMaxBackLog = 32768;

    ## The backlog sync time (seconds).
    kcSecBackLogFlush = 30;

    ## The timeout for the cleanup job (5 mins).
    kcSecCleanupTimeout = 300;
    ## The timeout to wait for the abort command before killing it.
    kcSecAbortTimeout = 300;

    ## The timeout to wait for the final output to be processed.
    kcSecFinalOutputTimeout = 180;
    ## The timeout to wait for the abort command output to be processed.
    kcSecAbortCmdOutputTimeout = 30;
    ## The timeout to wait for the terminate output to be processed.
    kcSecTerminateOutputTimeout = 30;
    ## The timeout to wait for the kill output to be processed.
    kcSecKillOutputTimeout = 30;

    ## The timeout for talking to the test manager.
    ksecTestManagerTimeout = 60;


    def __init__(self, oTestBoxScript, fnThreadProc, cSecTimeout, idResult, sScriptCmdLine):
        """
        Class instance init
        """
        # Init our instance data.
        self._idResult          = idResult;
        self._sScriptCmdLine    = sScriptCmdLine;
        self._oChild            = None;
        self._oBackLogLock      = threading.RLock();
        self._oBackLogFlushLock = threading.RLock();
        self._asBackLog         = [];
        self._cchBackLog        = 0;
        self._secTsBackLogFlush = utils.timestampSecond();

        # Init super.
        TestBoxBaseTask.__init__(self, oTestBoxScript, cSecTimeout, fnThreadProc);

    def terminate(self, cSecTimeout = kcSecCleanupTimeout):
        """ Reimplement with higher default timeout. """
        return TestBoxBaseTask.terminate(self, cSecTimeout);

    def _logFlush(self, oGivenConnection = None):
        """
        Flushes the log to the test manager.

        No exceptions.
        """
        fRc = True;

        with self._oBackLogFlushLock:
            # Grab the current back log.
            with self._oBackLogLock:
                asBackLog = self._asBackLog;
                self._asBackLog  = [];
                self._cchBackLog = 0;
                self._secTsBackLogFlush = utils.timestampSecond();

            # If there is anything to flush, flush it.
            if asBackLog:
                sBody = '';
                for sLine in asBackLog:
                    sBody += sLine + '\n';

                oConnection = None;
                try:
                    if oGivenConnection is None:
                        oConnection = self._oTestBoxScript.openTestManagerConnection();
                        oConnection.postRequest(constants.tbreq.LOG_MAIN, {constants.tbreq.LOG_PARAM_BODY: sBody});
                        oConnection.close();
                    else:
                        oGivenConnection.postRequest(constants.tbreq.LOG_MAIN, {constants.tbreq.LOG_PARAM_BODY: sBody});
                except Exception as oXcpt:
                    testboxcommons.log('_logFlush error: %s' % (oXcpt,));
                    if len(sBody) < self.kcchMaxBackLog * 4:
                        with self._oBackLogLock:
                            asBackLog.extend(self._asBackLog);
                            self._asBackLog = asBackLog;
                            # Don't restore _cchBackLog as there is no point in retrying immediately.
                    if oConnection is not None: # Be kind to apache.
                        try:    oConnection.close();
                        except: pass;
                    fRc = False;

        return fRc;

    def flushLogOnConnection(self, oConnection):
        """
        Attempts to flush the logon the given connection.

        No exceptions.
        """
        return self._logFlush(oConnection);

    def _logInternal(self, sMessage, fPrefix = True, fFlushCheck = False):
        """
        Internal logging.
        Won't flush the backlog, returns a flush indicator so the caller can
        do it instead.
        """
        if fPrefix:
            try:
                oNow = datetime.utcnow();
                sTs = '%02u:%02u:%02u.%06u ' % (oNow.hour, oNow.minute, oNow.second, oNow.microsecond);
            except Exception as oXcpt:
                sTs = 'oXcpt=%s ' % (oXcpt);
            sFullMsg = sTs + sMessage;
        else:
            sFullMsg = sMessage;

        with self._oBackLogLock:
            self._asBackLog.append(sFullMsg);
            cchBackLog = self._cchBackLog + len(sFullMsg) + 1;
            self._cchBackLog = cchBackLog;
            secTsBackLogFlush = self._secTsBackLogFlush;

        testboxcommons.log(sFullMsg);
        return fFlushCheck \
            and (   cchBackLog >= self.kcchMaxBackLog \
                 or utils.timestampSecond() - secTsBackLogFlush >= self.kcSecBackLogFlush);

    def _log(self, sMessage):
        """
        General logging function, will flush.
        """
        if self._logInternal(sMessage, fFlushCheck = True):
            self._logFlush();
        return True;

    def _reportDone(self, sResult):
        """
        Report EXEC job done to the test manager.

        sResult is a value from constants.result.
        """
        ## @todo optimize this to use one server connection.

        #
        # Log it.
        #
        assert sResult in constants.result.g_kasValidResults;
        self._log('Done %s' % (sResult,));

        #
        # Report it.
        #
        fRc = True;
        secStart = utils.timestampSecond();
        while True:
            self._logFlush(); ## @todo Combine this with EXEC_COMPLETED.
            oConnection = None;
            try:
                oConnection = self._oTestBoxScript.openTestManagerConnection();
                oConnection.postRequest(constants.tbreq.EXEC_COMPLETED, {constants.tbreq.EXEC_COMPLETED_PARAM_RESULT: sResult});
                oConnection.close();
            except Exception as oXcpt:
                if utils.timestampSecond() - secStart < self.ksecTestManagerTimeout:
                    self._log('_reportDone exception (%s) - retrying...' % (oXcpt,));
                    time.sleep(2);
                    continue;
                self._log('_reportDone error: %s' % (oXcpt,));
                if oConnection is not None: # Be kind to apache.
                    try:    oConnection.close();
                    except: pass;
                fRc = False;
            break;

        #
        # Mark the task as completed.
        #
        self._complete();
        return fRc;

    def _assembleArguments(self, sAction, fWithInterpreter = True):
        """
        Creates an argument array for subprocess.Popen, splitting the
        sScriptCmdLine like bourne shell would.
        fWithInterpreter is used (False) when checking that the script exists.

        Returns None on bad input.
        """

        #
        # This is a good place to export the test set id to the environment.
        #
        os.environ['TESTBOX_TEST_SET_ID'] = str(self._idResult);
        cTimeoutLeft = utils.timestampSecond() - self._tsSecStarted;
        cTimeoutLeft = 0 if cTimeoutLeft >= self._cSecTimeout else self._cSecTimeout - cTimeoutLeft;
        os.environ['TESTBOX_TIMEOUT']     = str(cTimeoutLeft);
        os.environ['TESTBOX_TIMEOUT_ABS'] = str(self._tsSecStarted + self._cSecTimeout);

        #
        # Do replacements and split the command line into arguments.
        #
        if self._sScriptCmdLine.find('@ACTION@') >= 0:
            sCmdLine = self._sScriptCmdLine.replace('@ACTION@', sAction);
        else:
            sCmdLine = self._sScriptCmdLine + ' ' + sAction;
        for sVar in [ 'TESTBOX_PATH_BUILDS', 'TESTBOX_PATH_RESOURCES', 'TESTBOX_PATH_SCRATCH', 'TESTBOX_PATH_SCRIPTS',
                      'TESTBOX_PATH_UPLOAD', 'TESTBOX_UUID', 'TESTBOX_REPORTER', 'TESTBOX_ID', 'TESTBOX_TEST_SET_ID',
                      'TESTBOX_TIMEOUT', 'TESTBOX_TIMEOUT_ABS' ]:
            if sCmdLine.find('${' + sVar + '}') >= 0:
                sCmdLine = sCmdLine.replace('${' + sVar + '}', os.environ[sVar]);

        asArgs = utils.argsSplit(sCmdLine);

        #
        # Massage argv[0]:
        #   - Convert portable slashes ('/') to the flavor preferred by the
        #     OS we're currently running on.
        #   - Run python script thru the current python interpreter (important
        #     on systems that doesn't sport native hash-bang script execution).
        #
        asArgs[0] = asArgs[0].replace('/', os.path.sep);
        if not os.path.isabs(asArgs[0]):
            asArgs[0] = os.path.join(self._oTestBoxScript.getPathScripts(), asArgs[0]);

        if asArgs[0].endswith('.py') and fWithInterpreter:
            if sys.executable:
                asArgs.insert(0, sys.executable);
            else:
                asArgs.insert(0, 'python');

        return asArgs;

    def _outputThreadProc(self, oChild, oStdOut, sAction):
        """
        Thread procedure for the thread that reads the output of the child
        process.  We use a dedicated thread for this purpose since non-blocking
        I/O may be hard to keep portable according to hints around the web...
        """
        oThread = oChild.oOutputThread;
        while not oThread.fPleaseQuit:
            # Get a line.
            try:
                sLine = oStdOut.readline();
            except Exception as oXcpt:
                self._log('child (%s) pipe I/O error: %s' % (sAction, oXcpt,));
                break;

            # EOF?
            if not sLine:
                break;

            # Strip trailing new line (DOS and UNIX).
            if sLine.endswith("\r\n"):
                sLine = sLine[0:-2];
            elif sLine.endswith("\n"):
                sLine = sLine[0:-1];

            # Log it.
            if self._logInternal(sLine, fPrefix = False, fFlushCheck = True):
                self._logFlush();

        # Close the stdout pipe in case we were told to get lost.
        try:
            oStdOut.close();
        except Exception as oXcpt:
            self._log('warning: Exception closing stdout pipe of "%s" child: %s' % (sAction, oXcpt,));

        # This is a bit hacky, but try reap the child so it won't hang as
        # defunkt during abort/timeout.
        if oChild.poll() is None:
            for _ in range(15):
                time.sleep(0.2);
                if oChild.poll() is not None:
                    break;

        oChild = None;
        return None;

    def _spawnChild(self, sAction):
        """
        Spawns the child process, returning success indicator + child object.
        """

        # Argument list.
        asArgs = self._assembleArguments(sAction)
        if asArgs is None:
            self._log('Malformed command line: "%s"' % (self._sScriptCmdLine,));
            return (False, None);

        # Spawn child.
        try:
            oChild = utils.processPopenSafe(asArgs,
                                            shell      = False,
                                            bufsize    = -1,
                                            stdout     = subprocess.PIPE,
                                            stderr     = subprocess.STDOUT,
                                            cwd        = self._oTestBoxScript.getPathSpill(),
                                            universal_newlines = True,
                                            close_fds  = utils.getHostOs() != 'win',
                                            preexec_fn = (None if utils.getHostOs() in ['win', 'os2']
                                                          else os.setsid)); # pylint: disable=no-member
        except Exception as oXcpt:
            self._log('Error creating child process %s: %s' % (asArgs, oXcpt));
            return (False, None);

        oChild.sTestBoxScriptAction = sAction;

        # Start output thread, extending the child object to keep track of it.
        oChild.oOutputThread = threading.Thread(target=self._outputThreadProc, args=(oChild, oChild.stdout, sAction))
        oChild.oOutputThread.daemon = True;
        oChild.oOutputThread.fPleaseQuit = False; # Our extension.
        oChild.oOutputThread.start();

        return (True, oChild);

    def _monitorChild(self, cSecTimeout, fTryKillCommand = True, oChild = None):
        """
        Monitors the child process.  If the child executes longer that
        cSecTimeout allows, we'll terminate it.
        Returns Success indicator and constants.result value.
        """

        if oChild is None:
            oChild = self._oChild;

        iProcGroup = oChild.pid;
        if utils.getHostOs() in ['win', 'os2'] or iProcGroup <= 0:
            iProcGroup = -2;

        #
        # Do timeout processing and check the health of the child.
        #
        sResult   = constants.result.PASSED;
        seStarted = utils.timestampSecond();
        while True:
            # Check status.
            iRc = oChild.poll();
            if iRc is not None:
                self._log('Child doing "%s" completed with exit code %d' % (oChild.sTestBoxScriptAction, iRc));
                oChild.oOutputThread.join(self.kcSecFinalOutputTimeout);

                if oChild is self._oChild:
                    self._oChild = None;

                if iRc == constants.rtexitcode.SUCCESS:
                    return (True, constants.result.PASSED);
                if iRc == constants.rtexitcode.SKIPPED:
                    return (True, constants.result.SKIPPED);
                if iRc == constants.rtexitcode.BAD_TESTBOX:
                    return (False, constants.result.BAD_TESTBOX);
                return (False, constants.result.FAILED);

            # Check for abort first, since that has less of a stigma.
            if self._shouldTerminate() is True:
                sResult = constants.result.ABORTED;
                break;

            # Check timeout.
            cSecElapsed = utils.timestampSecond() - seStarted;
            if cSecElapsed > cSecTimeout:
                self._log('Timeout: %u secs (limit %u secs)' % (cSecElapsed, cSecTimeout));
                sResult = constants.result.TIMED_OUT;
                break;

            # Wait.
            cSecLeft = cSecTimeout - cSecElapsed;
            oChild.oOutputThread.join(15 if cSecLeft > 15 else (cSecLeft + 1));

        #
        # If the child is still alive, try use the abort command to stop it
        # very gently.  This let's the testdriver clean up daemon processes
        # and such that our code below won't catch.
        #
        if fTryKillCommand and oChild.poll() is None:
            self._log('Attempting to abort child...');
            (fRc2, oAbortChild) = self._spawnChild('abort');
            if oAbortChild is not None and fRc2 is True:
                self._monitorChild(self.kcSecAbortTimeout, False, oAbortChild);
                oAbortChild = None;

        #
        # If the child is still alive, try the polite way.
        #
        if oChild.poll() is None:
            self._log('Attempting to terminate child doing "%s"...' % (oChild.sTestBoxScriptAction,));

            if iProcGroup > 0:
                try:
                    os.killpg(iProcGroup, signal.SIGTERM); # pylint: disable=no-member
                except Exception as oXcpt:
                    self._log('killpg() failed: %s' % (oXcpt,));

            try:
                self._oChild.terminate();
                oChild.oOutputThread.join(self.kcSecTerminateOutputTimeout);
            except Exception as oXcpt:
                self._log('terminate() failed: %s' % (oXcpt,));

        #
        # If the child doesn't respond to polite, kill it.  Always do a killpg
        # should there be any processes left in the group.
        #
        if iProcGroup > 0:
            try:
                os.killpg(iProcGroup, signal.SIGKILL); # pylint: disable=no-member
            except Exception as oXcpt:
                self._log('killpg() failed: %s' % (oXcpt,));

        if oChild.poll() is None:
            self._log('Attemting to kill child doing "%s"...' % (oChild.sTestBoxScriptAction,));
            try:
                self._oChild.kill();
                oChild.oOutputThread.join(self.kcSecKillOutputTimeout);
            except Exception as oXcpt:
                self._log('kill() failed: %s' % (oXcpt,));

        #
        # Give the whole mess a couple of more seconds to respond in case the
        # output thread exitted prematurely for some weird reason.
        #
        if oChild.poll() is None:
            time.sleep(2);
            time.sleep(2);
            time.sleep(2);

        iRc = oChild.poll();
        if iRc is not None:
            self._log('Child doing "%s" aborted with exit code %d' % (oChild.sTestBoxScriptAction, iRc));
        else:
            self._log('Child doing "%s" is still running, giving up...' % (oChild.sTestBoxScriptAction,));
            ## @todo in this case we should probably try reboot the testbox...
            oChild.oOutputThread.fPleaseQuit = True;

        if oChild is self._oChild:
            self._oChild = None;
        return (False, sResult);

    def _terminateChild(self):
        """
        Terminates the child forcefully.
        """
        if self._oChild is not None:
            pass;

    def _cleanupAfter(self):
        """
        Cleans up after a test failure. (On success, cleanup is implicit.)
        """
        assert self._oChild is None;

        #
        # Tell the script to clean up.
        #
        if self._sScriptCmdLine: # can be empty if cleanup crashed.
            (fRc, self._oChild) = self._spawnChild('cleanup-after');
            if fRc is True:
                (fRc, _) = self._monitorChild(self.kcSecCleanupTimeout, False);
                self._terminateChild();
        else:
            fRc = False;

        #
        # Wipe the stuff clean.
        #
        fRc2 = self._oTestBoxScript.reinitScratch(fnLog = self._log, cRetries = 6);

        return fRc and fRc2;



class TestBoxCleanupTask(TestBoxTestDriverTask):
    """
    Special asynchronous task for cleaning up a stale test when starting the
    testbox script.  It's assumed that the reason for the stale test lies in
    it causing a panic, reboot, or similar, so we'll also try collect some
    info about recent system crashes and reboots.
    """

    def __init__(self, oTestBoxScript):
        # Read the old state, throwing a fit if it's invalid.
        sScriptState   = oTestBoxScript.getPathState();
        sScriptCmdLine = self._readStateFile(os.path.join(sScriptState, 'script-cmdline.txt'));
        sResultId      = self._readStateFile(os.path.join(sScriptState, 'result-id.txt'));
        try:
            idResult = int(sResultId);
            if idResult <= 0 or idResult >= 0x7fffffff:
                raise Exception('');
        except:
            raise Exception('Invalid id value "%s" found in %s' % (sResultId, os.path.join(sScriptState, 'result-id.txt')));

        sTestBoxId   = self._readStateFile(os.path.join(sScriptState, 'testbox-id.txt'));
        try:
            self.idTestBox = int(sTestBoxId);
            if self.idTestBox <= 0 or self.idTestBox >= 0x7fffffff:
                raise Exception('');
        except:
            raise Exception('Invalid id value "%s" found in %s' % (sTestBoxId, os.path.join(sScriptState, 'testbox-id.txt')));
        self.sTestBoxName = self._readStateFile(os.path.join(sScriptState, 'testbox-name.txt'));

        # Init super.
        TestBoxTestDriverTask.__init__(self, oTestBoxScript, self._threadProc, self.kcSecCleanupTimeout,
                                       idResult, sScriptCmdLine);

    @staticmethod
    def _readStateFile(sPath):
        """
        Reads a state file, returning a string on success and otherwise raising
        an exception.
        """
        try:
            with open(sPath, "rb") as oFile:
                sStr = oFile.read();
            sStr = sStr.decode('utf-8');
            return sStr.strip();
        except Exception as oXcpt:
            raise Exception('Failed to read "%s": %s' % (sPath, oXcpt));

    def _threadProc(self):
        """
        Perform the actual clean up on script startup.
        """

        #
        # First make sure we won't repeat this exercise should it turn out to
        # trigger another reboot/panic/whatever.
        #
        sScriptCmdLine = os.path.join(self._oTestBoxScript.getPathState(), 'script-cmdline.txt');
        try:
            os.remove(sScriptCmdLine);
            open(sScriptCmdLine, 'wb').close();                 # pylint: disable=consider-using-with
        except Exception as oXcpt:
            self._log('Error truncating "%s": %s' % (sScriptCmdLine, oXcpt));

        #
        # Report the incident.
        #
        self._log('Seems we rebooted!');
        self._log('script-cmdline="%s"' % (self._sScriptCmdLine));
        self._log('result-id=%d' % (self._idResult));
        self._log('testbox-id=%d' % (self.idTestBox));
        self._log('testbox-name=%s' % (self.sTestBoxName));
        self._logFlush();

        # System specific info.
        sOs = utils.getHostOs();
        if sOs == 'darwin':
            self._log('NVRAM Panic Info:\n%s\n' % (self.darwinGetPanicInfo(),));

        self._logFlush();
        ## @todo Add some special command for reporting this situation so we get something
        #        useful in the event log.

        #
        # Do the cleaning up.
        #
        self._cleanupAfter();

        self._reportDone(constants.result.REBOOTED);
        return False;

    def darwinGetPanicInfo(self):
        """
        Returns a string with the aapl,panic-info content.
        """
        # Retriev the info.
        try:
            sRawInfo = utils.processOutputChecked(['nvram', 'aapl,panic-info']);
        except Exception as oXcpt:
            return 'exception running nvram: %s' % (oXcpt,);

        # Decode (%xx) and decompact it (7-bit -> 8-bit).
        ahDigits = \
        {
            '0': 0,  '1': 1,  '2': 2,  '3': 3,  '4': 4,  '5': 5,  '6': 6,  '7': 7,
            '8': 8,  '9': 9,  'a': 10, 'b': 11, 'c': 12, 'd': 13, 'e': 14, 'f': 15,
        };
        sInfo = '';
        off   = len('aapl,panic-info') + 1;
        iBit  = 0;
        bLow  = 0;

        while off < len(sRawInfo):
            # isprint is used to determine whether to %xx or %c it, so we have to
            # be a little careful before assuming % sequences are hex bytes.
            if    sRawInfo[off] == '%' \
              and off + 3 <= len(sRawInfo) \
              and sRawInfo[off + 1] in ahDigits \
              and sRawInfo[off + 2] in ahDigits:
                bCur = ahDigits[sRawInfo[off + 1]] * 0x10 + ahDigits[sRawInfo[off + 2]];
                off += 3;
            else:
                bCur = ord(sRawInfo[off]);
                off += 1;

            sInfo += chr(((bCur & (0x7f >> iBit)) << iBit) | bLow);
            bLow = bCur >> (7 - iBit);

            if iBit < 6:
                iBit += 1;
            else:
                # Final bit in sequence.
                sInfo += chr(bLow);
                bLow = 0;
                iBit = 0;

        # Expand shorthand.
        sInfo = sInfo.replace('@', 'com.apple.');
        sInfo = sInfo.replace('>', 'com.apple.driver.');
        sInfo = sInfo.replace('|', 'com.apple.iokit.');
        sInfo = sInfo.replace('$', 'com.apple.security.');
        sInfo = sInfo.replace('!A', 'Apple');
        sInfo = sInfo.replace('!a', 'Action');
        sInfo = sInfo.replace('!B', 'Bluetooth');
        sInfo = sInfo.replace('!C', 'Controller');
        sInfo = sInfo.replace('!F', 'Family');
        sInfo = sInfo.replace('!I', 'Intel');
        sInfo = sInfo.replace('!U', 'AppleUSB');
        sInfo = sInfo.replace('!P', 'Profile');

        # Done.
        return sInfo


class TestBoxExecTask(TestBoxTestDriverTask):
    """
    Implementation of a asynchronous EXEC task.

    This uses a thread for doing the actual work, i.e. starting and monitoring
    the child process, processing its output, and more.
    """

    def __init__(self, oTestBoxScript, idResult, sScriptZips, sScriptCmdLine, cSecTimeout):
        """
        Class instance init
        """
        # Init our instance data.
        self._sScriptZips = sScriptZips;

        # Init super.
        TestBoxTestDriverTask.__init__(self, oTestBoxScript, self._threadProc, cSecTimeout, idResult, sScriptCmdLine);

    @staticmethod
    def _writeStateFile(sPath, sContent):
        """
        Writes a state file, raising an exception on failure.
        """
        try:
            with open(sPath, "wb") as oFile:
                oFile.write(sContent.encode('utf-8'));
                oFile.flush();
                try:     os.fsync(oFile.fileno());
                except:  pass;
        except Exception as oXcpt:
            raise Exception('Failed to write "%s": %s' % (sPath, oXcpt));
        return True;

    @staticmethod
    def _environTxtContent():
        """
        Collects environment variables and values for the environ.txt stat file
        (for external monitoring tool).
        """
        sText = '';
        for sVar in [ 'TESTBOX_PATH_BUILDS',   'TESTBOX_PATH_RESOURCES', 'TESTBOX_PATH_SCRATCH',      'TESTBOX_PATH_SCRIPTS',
                      'TESTBOX_PATH_UPLOAD',   'TESTBOX_HAS_HW_VIRT',    'TESTBOX_HAS_NESTED_PAGING', 'TESTBOX_HAS_IOMMU',
                      'TESTBOX_SCRIPT_REV',    'TESTBOX_CPU_COUNT',      'TESTBOX_MEM_SIZE',          'TESTBOX_SCRATCH_SIZE',
                      'TESTBOX_WITH_RAW_MODE', 'TESTBOX_WITH_RAW_MODE',  'TESTBOX_MANAGER_URL',       'TESTBOX_UUID',
                      'TESTBOX_REPORTER',      'TESTBOX_NAME',           'TESTBOX_ID',                'TESTBOX_TEST_SET_ID',
                      'TESTBOX_TIMEOUT',       'TESTBOX_TIMEOUT_ABS', ]:
            sValue = os.environ.get(sVar);
            if sValue:
                sText += sVar + '=' + sValue + '\n';
        return sText;

    def _saveState(self):
        """
        Saves the task state on disk so we can launch a TestBoxCleanupTask job
        if the test should cause system panic or similar.

        Note! May later be extended to support tests that reboots the host.
        """
        sScriptState = self._oTestBoxScript.getPathState();
        try:
            self._writeStateFile(os.path.join(sScriptState, 'script-cmdline.txt'), self._sScriptCmdLine);
            self._writeStateFile(os.path.join(sScriptState, 'result-id.txt'),      str(self._idResult));
            self._writeStateFile(os.path.join(sScriptState, 'testbox-id.txt'),     str(self._oTestBoxScript.getTestBoxId()));
            self._writeStateFile(os.path.join(sScriptState, 'testbox-name.txt'),   self._oTestBoxScript.getTestBoxName());
            self._writeStateFile(os.path.join(sScriptState, 'environ.txt'),        self._environTxtContent());
        except Exception as oXcpt:
            self._log('Failed to write state: %s' % (oXcpt,));
            return False;
        return True;

    def _downloadAndUnpackScriptZips(self):
        """
        Downloads/copies the script ZIPs into TESTBOX_SCRIPT and unzips them to
        the same directory.

        Raises no exceptions, returns log + success indicator instead.
        """
        sPathScript = self._oTestBoxScript.getPathScripts();
        asArchives = self._sScriptZips.split(',');
        for sArchive in asArchives:
            sArchive = sArchive.strip();
            if not sArchive:
                continue;

            # Figure the destination name (in scripts).
            sDstFile = webutils.getFilename(sArchive);
            if   not sDstFile \
              or re.search('[^a-zA-Z0-9 !#$%&\'()@^_`{}~.-]', sDstFile) is not None: # FAT charset sans 128-255 + '.'.
                self._log('Malformed script zip filename: %s' % (sArchive,));
                return False;
            sDstFile = os.path.join(sPathScript, sDstFile);

            # Do the work.
            if webutils.downloadFile(sArchive, sDstFile, self._oTestBoxScript.getPathBuilds(), self._log, self._log) is not True:
                return False;
            asFiles = utils.unpackFile(sDstFile, sPathScript, self._log, self._log);
            if asFiles is None:
                return False;

            # Since zip files doesn't always include mode masks, set the X bit
            # of all of them so we can execute binaries and hash-bang scripts.
            for sFile in asFiles:
                utils.chmodPlusX(sFile);

        return True;

    def _threadProc(self):
        """
        Do the work of an EXEC command.
        """

        sResult = constants.result.PASSED;

        #
        # Start by preparing the scratch directories.
        #
        # Note! Failures at this stage are not treated as real errors since
        #       they may be caused by the previous test and other circumstances
        #       so we don't want to go fail a build because of this.
        #
        fRc = self._oTestBoxScript.reinitScratch(self._logInternal);
        fNeedCleanUp = fRc;
        if fRc is True:
            fRc = self._downloadAndUnpackScriptZips();
            testboxcommons.log2('_threadProc: _downloadAndUnpackScriptZips -> %s' % (fRc,));
        if fRc is not True:
            sResult = constants.result.BAD_TESTBOX;

        #
        # Make sure the script exists.
        #
        if fRc is True:
            sScript = self._assembleArguments('none', fWithInterpreter = False)[0];
            if not os.path.exists(sScript):
                self._log('The test driver script "%s" cannot be found.' % (sScript,));
                sDir = sScript;
                while len(sDir) > 3:
                    sDir = os.path.dirname(sDir);
                    if os.path.exists(sDir):
                        self._log('First existing parent directory is "%s".' % (sDir,));
                        break;
                fRc = False;

        if fRc is True:
            #
            # Start testdriver script.
            #
            fRc = self._saveState();
            if fRc:
                (fRc, self._oChild) = self._spawnChild('all');
                testboxcommons.log2('_threadProc: _spawnChild -> %s, %s' % (fRc, self._oChild));
            if fRc:
                (fRc, sResult) = self._monitorChild(self._cSecTimeout);
                testboxcommons.log2('_threadProc: _monitorChild -> %s' % (fRc,));

                # If the run failed, do explicit cleanup unless its a BAD_TESTBOX, since BAD_TESTBOX is
                # intended for pre-cleanup problems caused by previous test failures.  Do a cleanup on
                # a BAD_TESTBOX could easily trigger an uninstallation error and change status to FAILED.
                if fRc is not True:
                    if sResult != constants.result.BAD_TESTBOX:
                        testboxcommons.log2('_threadProc: explicit cleanups...');
                        self._terminateChild();
                        self._cleanupAfter();
                    fNeedCleanUp = False;
            assert self._oChild is None;

        #
        # Clean up scratch.
        #
        if fNeedCleanUp:
            if self._oTestBoxScript.reinitScratch(self._logInternal, cRetries = 6) is not True:
                self._log('post run reinitScratch failed.');
                fRc = False;

        #
        # Report status and everything back to the test manager.
        #
        if fRc is False and sResult == constants.result.PASSED:
            sResult = constants.result.FAILED;
        self._reportDone(sResult);
        return fRc;

