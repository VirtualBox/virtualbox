# -*- coding: utf-8 -*-
# $Id: base.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
Base testdriver module.
"""

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
import os
import os.path
import signal
import socket
import stat
import sys
import time
if sys.version_info[0] < 3: import thread;            # pylint: disable=import-error
else:                       import _thread as thread; # pylint: disable=import-error
import threading
import traceback
import tempfile;
import unittest;

# Validation Kit imports.
from common                 import utils;
from common.constants       import rtexitcode;
from testdriver             import reporter;
if sys.platform == 'win32':
    from testdriver         import winbase;

# Figure where we are.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)));

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


#
# Some utility functions.
#

def exeSuff():
    """
    Returns the executable suffix.
    """
    if os.name in ('nt', 'os2'):
        return '.exe';
    return '';

def searchPath(sExecName):
    """
    Searches the PATH for the specified executable name, returning the first
    existing file/directory/whatever.  The return is abspath'ed.
    """
    sSuff = exeSuff();

    sPath = os.getenv('PATH', os.getenv('Path', os.path.defpath));
    aPaths = sPath.split(os.path.pathsep)
    for sDir in aPaths:
        sFullExecName = os.path.join(sDir, sExecName);
        if os.path.exists(sFullExecName):
            return os.path.abspath(sFullExecName);
        sFullExecName += sSuff;
        if os.path.exists(sFullExecName):
            return os.path.abspath(sFullExecName);
    return sExecName;

def getEnv(sVar, sLocalAlternative = None):
    """
    Tries to get an environment variable, optionally with a local run alternative.
    Will raise an exception if sLocalAlternative is None and the variable is
    empty or missing.
    """
    try:
        sVal = os.environ.get(sVar, None);
        if sVal is None:
            raise GenError('environment variable "%s" is missing' % (sVar));
        if sVal == "":
            raise GenError('environment variable "%s" is empty' % (sVar));
    except:
        if sLocalAlternative is None  or  not reporter.isLocal():
            raise
        sVal = sLocalAlternative;
    return sVal;

def getDirEnv(sVar, sAlternative = None, fLocalReq = False, fTryCreate = False):
    """
    Tries to get an environment variable specifying a directory path.

    Resolves it into an absolute path and verifies its existance before
    returning it.

    If the environment variable is empty or isn't set, or if the directory
    doesn't exist or isn't a directory, sAlternative is returned instead.
    If sAlternative is None, then we'll raise a GenError.  For local runs we'll
    only do this if fLocalReq is True.
    """
    assert sAlternative is None or fTryCreate is False;
    try:
        sVal = os.environ.get(sVar, None);
        if sVal is None:
            raise GenError('environment variable "%s" is missing' % (sVar));
        if sVal == "":
            raise GenError('environment variable "%s" is empty' % (sVar));

        sVal = os.path.abspath(sVal);
        if not os.path.isdir(sVal):
            if not fTryCreate  or  os.path.exists(sVal):
                reporter.error('the value of env.var. "%s" is not a dir: "%s"' % (sVar, sVal));
                raise GenError('the value of env.var. "%s" is not a dir: "%s"' % (sVar, sVal));
            try:
                os.makedirs(sVal, 0o700);
            except:
                reporter.error('makedirs failed on the value of env.var. "%s": "%s"' % (sVar, sVal));
                raise GenError('makedirs failed on the value of env.var. "%s": "%s"' % (sVar, sVal));
    except:
        if sAlternative is None:
            if reporter.isLocal() and fLocalReq:
                raise;
            sVal = None;
        else:
            sVal = os.path.abspath(sAlternative);
    return sVal;

def timestampMilli():
    """
    Gets a millisecond timestamp.
    """
    return utils.timestampMilli();

def timestampNano():
    """
    Gets a nanosecond timestamp.
    """
    return utils.timestampNano();

def tryGetHostByName(sName):
    """
    Wrapper around gethostbyname.
    """
    if sName is not None:
        try:
            sIpAddr = socket.gethostbyname(sName);
        except:
            reporter.errorXcpt('gethostbyname(%s)' % (sName));
        else:
            if sIpAddr != '0.0.0.0':
                sName = sIpAddr;
            else:
                reporter.error('gethostbyname(%s) -> %s' % (sName, sIpAddr));
    return sName;

def __processSudoKill(uPid, iSignal, fSudo):
    """
    Does the sudo kill -signal pid thing if fSudo is true, else uses os.kill.
    """
    try:
        if fSudo:
            return utils.sudoProcessCall(['/bin/kill', '-%s' % (iSignal,), str(uPid)]) == 0;
        os.kill(uPid, iSignal);
        return True;
    except:
        reporter.logXcpt('uPid=%s, iSignal=%d, fSudo=%s' % (uPid, iSignal, fSudo));
    return False;

def processInterrupt(uPid, fSudo = False):
    """
    Sends a SIGINT or equivalent to interrupt the specified process.
    Returns True on success, False on failure.

    On Windows hosts this may not work unless the process happens to be a
    process group leader.
    """
    if sys.platform == 'win32':
        fRc = winbase.processInterrupt(uPid)
    else:
        fRc = __processSudoKill(uPid, signal.SIGINT, fSudo);
    return fRc;

def sendUserSignal1(uPid, fSudo = False):
    """
    Sends a SIGUSR1 or equivalent to nudge the process into shutting down
    (VBoxSVC) or something.
    Returns True on success, False on failure or if not supported (win).

    On Windows hosts this may not work unless the process happens to be a
    process group leader.
    """
    if sys.platform == 'win32':
        fRc = False;
    else:
        fRc = __processSudoKill(uPid, signal.SIGUSR1, fSudo); # pylint: disable=no-member
    return fRc;

def processTerminate(uPid, fSudo = False):
    """
    Terminates the process in a nice manner (SIGTERM or equivalent).
    Returns True on success, False on failure (logged).
    """
    fRc = False;
    if sys.platform == 'win32':
        fRc = winbase.processTerminate(uPid);
    else:
        fRc = __processSudoKill(uPid, signal.SIGTERM, fSudo);
    return fRc;

def processKill(uPid, fSudo = False):
    """
    Terminates the process with extreme prejudice (SIGKILL).
    Returns True on success, False on failure.
    """
    fRc = False;
    if sys.platform == 'win32':
        fRc = winbase.processKill(uPid);
    else:
        fRc = __processSudoKill(uPid, signal.SIGKILL, fSudo); # pylint: disable=no-member
    return fRc;

def processKillWithNameCheck(uPid, sName):
    """
    Like processKill(), but checks if the process name matches before killing
    it.  This is intended for killing using potentially stale pid values.

    Returns True on success, False on failure.
    """

    if processCheckPidAndName(uPid, sName) is not True:
        return False;
    return processKill(uPid);


def processExists(uPid):
    """
    Checks if the specified process exits.
    This will only work if we can signal/open the process.

    Returns True if it positively exists, False otherwise.
    """
    return utils.processExists(uPid);

def processCheckPidAndName(uPid, sName):
    """
    Checks if a process PID and NAME matches.
    """
    return utils.processCheckPidAndName(uPid, sName);

def wipeDirectory(sDir):
    """
    Deletes all file and sub-directories in sDir, leaving sDir in empty afterwards.
    Returns the number of errors after logging them as errors.
    """
    if not os.path.exists(sDir):
        return 0;

    try:
        asNames = os.listdir(sDir);
    except:
        return reporter.errorXcpt('os.listdir("%s")' % (sDir));

    cErrors = 0;
    for sName in asNames:
        # Build full path and lstat the object.
        sFullName = os.path.join(sDir, sName)
        try:
            oStat = os.lstat(sFullName);
        except:
            reporter.errorXcpt('lstat("%s")' % (sFullName,));
            cErrors = cErrors + 1;
            continue;

        if stat.S_ISDIR(oStat.st_mode):
            # Directory - recurse and try remove it.
            cErrors = cErrors + wipeDirectory(sFullName);
            try:
                os.rmdir(sFullName);
            except:
                reporter.errorXcpt('rmdir("%s")' % (sFullName,));
                cErrors = cErrors + 1;
        else:
            # File, symlink, fifo or something - remove/unlink.
            try:
                os.remove(sFullName);
            except:
                reporter.errorXcpt('remove("%s")' % (sFullName,));
                cErrors = cErrors + 1;
    return cErrors;


#
# Classes
#

class GenError(Exception):
    """
    Exception class which only purpose it is to allow us to only catch our own
    exceptions.  Better design later.
    """

    def __init__(self, sWhat = "whatever"):
        Exception.__init__(self);
        self.sWhat = sWhat

    def str(self):
        """Get the message string."""
        return self.sWhat;


class InvalidOption(GenError):
    """
    Exception thrown by TestDriverBase.parseOption(). It contains the error message.
    """
    def __init__(self, sWhat):
        GenError.__init__(self, sWhat);


class QuietInvalidOption(GenError):
    """
    Exception thrown by TestDriverBase.parseOption(). Error already printed, just
    return failure.
    """
    def __init__(self):
        GenError.__init__(self, "");


class TdTaskBase(object):
    """
    The base task.
    """

    def __init__(self, sCaller, fnProcessEvents = None):
        self.sDbgCreated        = '%s: %s' % (utils.getTimePrefix(), sCaller);
        self.fSignalled         = False;
        self.__oRLock           = threading.RLock();
        self.oCv                = threading.Condition(self.__oRLock);
        self.oOwner             = None;
        self.msStart            = timestampMilli();
        self.oLocker            = None;

        ## Callback function that takes no parameters and will not be called holding the lock.
        ## It is a hack to work the XPCOM and COM event queues, so we won't hold back events
        ## that could block task progress (i.e. hangs VM).
        self.fnProcessEvents    = fnProcessEvents;

    def __del__(self):
        """In case we need it later on."""
        pass;   # pylint: disable=unnecessary-pass

    def toString(self):
        """
        Stringifies the object, mostly as a debug aid.
        """
        return '<%s: fSignalled=%s, __oRLock=%s, oCv=%s, oOwner=%s, oLocker=%s, msStart=%s, sDbgCreated=%s>' \
             % (type(self).__name__, self.fSignalled, self.__oRLock, self.oCv, repr(self.oOwner), self.oLocker, self.msStart,
                self.sDbgCreated,);

    def __str__(self):
        return self.toString();

    def lockTask(self):
        """ Wrapper around oCv.acquire(). """
        # Change this to False for debugging deadlocks.
        if True is True: # pylint: disable=comparison-with-itself,comparison-of-constants
            self.oCv.acquire();
        else:
            msStartWait = timestampMilli();
            while self.oCv.acquire(0) is False:
                if timestampMilli() - msStartWait > 30*1000:
                    reporter.error('!!! timed out waiting for %s' % (self, ));
                    traceback.print_stack();
                    reporter.logAllStacks()
                    self.oCv.acquire();
                    break;
                time.sleep(0.5);
        self.oLocker = thread.get_ident()
        return None;

    def unlockTask(self):
        """ Wrapper around oCv.release(). """
        self.oLocker = None;
        self.oCv.release();
        return None;

    def getAgeAsMs(self):
        """
        Returns the number of milliseconds the task has existed.
        """
        return timestampMilli() - self.msStart;

    def setTaskOwner(self, oOwner):
        """
        Sets or clears the task owner.  (oOwner can be None.)

        Returns the previous owner, this means None if not owned.
        """
        self.lockTask();
        oOldOwner = self.oOwner;
        self.oOwner = oOwner;
        self.unlockTask();
        return oOldOwner;

    def signalTaskLocked(self):
        """
        Variant of signalTask that can be called while owning the lock.
        """
        fOld = self.fSignalled;
        if not fOld:
            reporter.log2('signalTaskLocked(%s)' % (self,));
        self.fSignalled = True;
        self.oCv.notifyAll(); # pylint: disable=deprecated-method
        if self.oOwner is not None:
            self.oOwner.notifyAboutReadyTask(self);
        return fOld;

    def signalTask(self):
        """
        Signals the task, internal use only.

        Returns the previous state.
        """
        self.lockTask();
        fOld = self.signalTaskLocked();
        self.unlockTask();
        return fOld

    def resetTaskLocked(self):
        """
        Variant of resetTask that can be called while owning the lock.
        """
        fOld = self.fSignalled;
        self.fSignalled = False;
        return fOld;

    def resetTask(self):
        """
        Resets the task signal, internal use only.

        Returns the previous state.
        """
        self.lockTask();
        fOld = self.resetTaskLocked();
        self.unlockTask();
        return fOld

    def pollTask(self, fLocked = False):
        """
        Poll the signal status of the task.
        Returns True if signalled, False if not.

        Override this method.
        """
        if not fLocked:
            self.lockTask();
        fState = self.fSignalled;
        if not fLocked:
            self.unlockTask();
        return fState

    def waitForTask(self, cMsTimeout = 0):
        """
        Waits for the task to be signalled.

        Returns True if the task is/became ready before the timeout expired.
        Returns False if the task is still not after cMsTimeout have elapsed.

        Overriable.
        """
        if self.fnProcessEvents:
            self.fnProcessEvents();

        self.lockTask();

        fState = self.pollTask(True);
        if not fState:
            # Don't wait more than 1s.  This allow lazy state polling and avoid event processing trouble.
            msStart = timestampMilli();
            while not fState:
                cMsElapsed = timestampMilli() - msStart;
                if cMsElapsed >= cMsTimeout:
                    break;

                cMsWait = cMsTimeout - cMsElapsed
                cMsWait = min(cMsWait, 1000);
                try:
                    self.oCv.wait(cMsWait / 1000.0);
                except:
                    pass;

                if self.fnProcessEvents:
                    self.unlockTask();
                    self.fnProcessEvents();
                    self.lockTask();

                reporter.doPollWork('TdTaskBase.waitForTask');
                fState = self.pollTask(True);

        self.unlockTask();

        if self.fnProcessEvents:
            self.fnProcessEvents();

        return fState;


class Process(TdTaskBase):
    """
    Child Process.
    """

    def __init__(self, sName, asArgs, uPid, hWin = None, uTid = None):
        TdTaskBase.__init__(self, utils.getCallerName());
        self.sName      = sName;
        self.asArgs     = asArgs;
        self.uExitCode  = -127;
        self.uPid       = uPid;
        self.hWin       = hWin;
        self.uTid       = uTid;
        self.sKindCrashReport = None;
        self.sKindCrashDump   = None;

    def toString(self):
        return '<%s uExitcode=%s, uPid=%s, sName=%s, asArgs=%s, hWin=%s, uTid=%s>' \
             % (TdTaskBase.toString(self), self.uExitCode, self.uPid, self.sName, self.asArgs, self.hWin, self.uTid);

    #
    # Instantiation methods.
    #

    @staticmethod
    def spawn(sName, *asArgsIn):
        """
        Similar to os.spawnl(os.P_NOWAIT,).

        """
        # Make argument array (can probably use asArgsIn directly, but wtf).
        asArgs = [];
        for sArg in asArgsIn:
            asArgs.append(sArg);

        # Special case: Windows.
        if sys.platform == 'win32':
            (uPid, hProcess, uTid) = winbase.processCreate(searchPath(sName), asArgs);
            if uPid == -1:
                return None;
            return Process(sName, asArgs, uPid, hProcess, uTid);

        # Unixy.
        try:
            uPid = os.spawnv(os.P_NOWAIT, sName, asArgs);
        except:
            reporter.logXcpt('sName=%s' % (sName,));
            return None;
        return Process(sName, asArgs, uPid);

    @staticmethod
    def spawnp(sName, *asArgsIn):
        """
        Similar to os.spawnlp(os.P_NOWAIT,).

        """
        return Process.spawn(searchPath(sName), *asArgsIn);

    #
    # Task methods
    #

    def pollTask(self, fLocked = False):
        """
        Overridden pollTask method.
        """
        if not fLocked:
            self.lockTask();

        fRc = self.fSignalled;
        if not fRc:
            if sys.platform == 'win32':
                if winbase.processPollByHandle(self.hWin):
                    try:
                        if hasattr(self.hWin, '__int__'): # Needed for newer pywin32 versions.
                            (uPid, uStatus) = os.waitpid(self.hWin.__int__(), 0); # pylint: disable=unnecessary-dunder-call
                        else:
                            (uPid, uStatus) = os.waitpid(self.hWin, 0);
                        if uPid in (self.hWin, self.uPid,):
                            self.hWin.Detach(); # waitpid closed it, so it's now invalid.
                            self.hWin = None;
                            uPid = self.uPid;
                    except:
                        reporter.logXcpt();
                        uPid    = self.uPid;
                        uStatus = 0xffffffff;
                else:
                    uPid    = 0;
                    uStatus = 0;        # pylint: disable=redefined-variable-type
            else:
                try:
                    (uPid, uStatus) = os.waitpid(self.uPid, os.WNOHANG); # pylint: disable=no-member
                except:
                    reporter.logXcpt();
                    uPid    = self.uPid;
                    uStatus = 0xffffffff;

            # Got anything?
            if uPid == self.uPid:
                self.uExitCode = uStatus;
                reporter.log('Process %u -> %u (%#x)' % (uPid, uStatus, uStatus));
                self.signalTaskLocked();
                if self.uExitCode != 0 and (self.sKindCrashReport is not None or self.sKindCrashDump is not None):
                    reporter.error('Process "%s" returned/crashed with a non-zero status code!! rc=%u sig=%u%s (raw=%#x)'
                                   % ( self.sName, self.uExitCode >> 8, self.uExitCode & 0x7f,
                                       ' w/ core' if self.uExitCode & 0x80 else '', self.uExitCode))
                    utils.processCollectCrashInfo(self.uPid, reporter.log, self._addCrashFile);

            fRc = self.fSignalled;
        if not fLocked:
            self.unlockTask();
        return fRc;

    def _addCrashFile(self, sFile, fBinary):
        """
        Helper for adding a crash report or dump to the test report.
        """
        sKind = self.sKindCrashDump if fBinary else self.sKindCrashReport;
        if sKind is not None:
            reporter.addLogFile(sFile, sKind);
        return None;


    #
    # Methods
    #

    def enableCrashReporting(self, sKindCrashReport, sKindCrashDump):
        """
        Enabling (or disables) automatic crash reporting on systems where that
        is possible.  The two file kind parameters are on the form
        'crash/log/client' and 'crash/dump/client'.  If both are None,
        reporting will be disabled.
        """
        self.sKindCrashReport = sKindCrashReport;
        self.sKindCrashDump   = sKindCrashDump;

        sOs       = utils.getHostOs();
        if sOs == 'solaris':
            # Both 'coreadm -e ...' and 'svccfg apply' only work if running with all privileges.
            fIsRoot = os.geteuid() == 0; # pylint: disable=no-member
            if fIsRoot is False:
                return True;

            sScratchPath = os.environ.get('TESTBOX_PATH_SCRATCH', '/var/tmp');
            sCoreadmXmlFile = os.path.join(sScratchPath, 'coreadm.xml');
            if sKindCrashDump is not None:
                # If the current core file configuration has been modified from the system default
                # then save the configuration to coreadm.xml so it can be restored afterwards.
                (iExitCode, sStdOut, sStdErr) = utils.processOutputUnchecked([ 'svcprop', '-p', 'config_params', \
                                                                               '-l', 'admin', 'svc:/system/coreadm:default' ]);
                if iExitCode == 0 and sStdOut != '':
                    (iExitCode, _, sStdErr) = utils.processOutputUnchecked([ 'svccfg', 'extract', '-l', 'admin', \
                                                                             'svc:/system/coreadm:default', '>', \
                                                                             sCoreadmXmlFile ]);
                # Annoyingly svccfg(1M) returns zero for both success and failure but if the
                # command fails errors are written to stderr.
                if iExitCode != 0 or sStdErr != '':
                    reporter.error('Failed to backup current system-wide core dump configuration: %s' % sStdErr);
                    return False;

                # Configure all core dumps, including those of setuid and setgid binaries, to be
                # written to /var/cores using the naming pattern of core.argv0.process-ID, e.g.
                # core.VBoxSVC.12345.
                (iExitCode, _, sStdErr) = utils.processOutputUnchecked([ 'coreadm', '-e', 'global', '-e', 'global-setid', \
                                                                         '-e', 'log', '-G', 'all', \
                                                                         '-g', '/var/cores/core.%f.%p' ]);
                if iExitCode != 0:
                    reporter.error('Failed to update system-wide core dump configuration: %s' % sStdErr);
                    return False;

                reporter.log('Core file configuration successfully updated: All core files will be written to /var/cores.');
            else:
                # Restore the core file configuration to what it was before making the
                # changes above.
                (iExitCode, _, sStdErr) = utils.processOutputUnchecked([ 'svccfg', '-s', 'svc:/system/coreadm:default', \
                                                                         'delcust' ]);
                # Annoyingly svccfg(1M) returns zero for both success and failure but if the
                # command fails errors are written to stderr.
                if sStdErr == '' and os.path.exists(sCoreadmXmlFile):
                    (iExitCode, _, sStdErr) = utils.processOutputUnchecked([ 'svccfg', 'apply', sCoreadmXmlFile ]);

                if sStdErr != '':
                    reporter.error('Failed to restore system-wide core dump configuration: %s' % sStdErr);
                    return False;

                reporter.log('Core file configuration successfully restored to previous state.');

        return True;

    def isRunning(self):
        """
        Returns True if the process is still running, False if not.
        """
        return not self.pollTask();

    def wait(self, cMsTimeout = 0):
        """
        Wait for the process to exit.

        Returns True if the process exited withint the specified wait period.
        Returns False if still running.
        """
        return self.waitForTask(cMsTimeout);

    def getExitCode(self):
        """
        Returns the exit code of the process.
        The process must have exited or the result will be wrong.
        """
        if self.isRunning():
            return -127;
        return self.uExitCode >> 8;

    def isNormalExit(self):
        """
        Returns True if regular exit(), False if signal or still running.
        """
        if self.isRunning():
            return False;
        if sys.platform == 'win32':
            return True;
        return os.WIFEXITED(self.uExitCode); # pylint: disable=no-member

    def interrupt(self):
        """
        Sends a SIGINT or equivalent to interrupt the process.
        Returns True on success, False on failure.

        On Windows hosts this may not work unless the process happens to be a
        process group leader.
        """
        if sys.platform == 'win32':
            return winbase.postThreadMesssageQuit(self.uTid);
        return processInterrupt(self.uPid);

    def sendUserSignal1(self):
        """
        Sends a SIGUSR1 or equivalent to nudge the process into shutting down
        (VBoxSVC) or something.
        Returns True on success, False on failure.

        On Windows hosts this may not work unless the process happens to be a
        process group leader.
        """
        #if sys.platform == 'win32':
        #    return winbase.postThreadMesssageClose(self.uTid);
        return sendUserSignal1(self.uPid);

    def terminate(self):
        """
        Terminates the process in a nice manner (SIGTERM or equivalent).
        Returns True on success, False on failure (logged).
        """
        if sys.platform == 'win32':
            return winbase.processTerminateByHandle(self.hWin);
        return processTerminate(self.uPid);

    def getPid(self):
        """ Returns the process id. """
        return self.uPid;


class SubTestDriverBase(object):
    """
    The base sub-test driver.

    It helps thinking of these as units/sets/groups of tests, where the test
    cases are (mostly) realized in python.

    The sub-test drivers are subordinates of one or more test drivers.  They
    can be viewed as test code libraries that is responsible for parts of a
    test driver run in different setups.  One example would be testing a guest
    additions component, which is applicable both to freshly installed guest
    additions and VMs with old guest.

    The test drivers invokes the sub-test drivers in a private manner during
    test execution, but some of the generic bits are done automagically by the
    base class: options, help, resources, various other actions.
    """

    def __init__(self, oTstDrv, sName, sTestName):
        self.oTstDrv            = oTstDrv       # type: TestDriverBase
        self.sName              = sName;        # For use with options (--enable-sub-driver sName:sName2)
        self.sTestName          = sTestName;    # More descriptive for passing to reporter.testStart().
        self.asRsrcs            = []            # type: List(str)
        self.fEnabled           = True;         # TestDriverBase --enable-sub-driver and --disable-sub-driver.

    def showUsage(self):
        """
        Show usage information if any.

        The default implementation only prints the name.
        """
        reporter.log('');
        reporter.log('Options for sub-test driver %s (%s):' % (self.sTestName, self.sName,));
        return True;

    def parseOption(self, asArgs, iArg):
        """
        Parse an option. Override this.

        @param  asArgs      The argument vector.
        @param  iArg        The index of the current argument.

        @returns The index of the next argument if consumed, @a iArg if not.

        @throws  InvalidOption or QuietInvalidOption on syntax error or similar.
        """
        _ = asArgs;
        return iArg;


class TestDriverBase(object): # pylint: disable=too-many-instance-attributes
    """
    The base test driver.
    """

    def __init__(self):
        self.fInterrupted       = False;

        # Actions.
        self.asSpecialActions   = ['extract', 'abort'];
        self.asNormalActions    = ['cleanup-before', 'verify', 'config', 'execute', 'cleanup-after' ];
        self.asActions          = [];
        self.sExtractDstPath    = None;

        # Options.
        self.fNoWipeClean       = False;

        # Tasks - only accessed by one thread atm, so no need for locking.
        self.aoTasks            = [];

        # Host info.
        self.sHost              = utils.getHostOs();
        self.sHostArch          = utils.getHostArch();

        # Skipped status modifier (see end of innerMain()).
        self.fBadTestbox        = False;

        #
        # Get our bearings and adjust the environment.
        #
        if not utils.isRunningFromCheckout():
            self.sBinPath = os.path.join(g_ksValidationKitDir, utils.getHostOs(), utils.getHostArch());
        else:
            self.sBinPath = os.path.join(g_ksValidationKitDir, os.pardir, os.pardir, os.pardir, 'out', utils.getHostOsDotArch(),
                                         os.environ.get('KBUILD_TYPE', 'debug'),
                                         'validationkit', utils.getHostOs(), utils.getHostArch());
        self.sOrgShell = os.environ.get('SHELL');
        self.sOurShell = os.path.join(self.sBinPath, 'vts_shell' + exeSuff()); # No shell yet.
        os.environ['SHELL'] = self.sOurShell;

        self.sScriptPath = getDirEnv('TESTBOX_PATH_SCRIPTS');
        if self.sScriptPath is None:
            self.sScriptPath = os.path.abspath(os.path.join(os.getcwd(), '..'));
        os.environ['TESTBOX_PATH_SCRIPTS'] = self.sScriptPath;

        self.sScratchPath = getDirEnv('TESTBOX_PATH_SCRATCH', fTryCreate = True);
        if self.sScratchPath is None:
            sTmpDir = tempfile.gettempdir();
            if sTmpDir == '/tmp': # /var/tmp is generally more suitable on all platforms.
                sTmpDir = '/var/tmp';
            self.sScratchPath = os.path.abspath(os.path.join(sTmpDir, 'VBoxTestTmp'));
            if not os.path.isdir(self.sScratchPath):
                os.makedirs(self.sScratchPath, 0o700);
        os.environ['TESTBOX_PATH_SCRATCH'] = self.sScratchPath;

        self.sTestBoxName  = getEnv(   'TESTBOX_NAME', 'local');
        self.sTestSetId    = getEnv(   'TESTBOX_TEST_SET_ID', 'local');
        self.sBuildPath    = getDirEnv('TESTBOX_PATH_BUILDS');
        self.sUploadPath   = getDirEnv('TESTBOX_PATH_UPLOAD');
        self.sResourcePath = getDirEnv('TESTBOX_PATH_RESOURCES');
        if self.sResourcePath is None:
            if self.sHost == 'darwin':      self.sResourcePath = "/Volumes/testrsrc/";
            elif self.sHost == 'freebsd':   self.sResourcePath = "/mnt/testrsrc/";
            elif self.sHost == 'linux':     self.sResourcePath = "/mnt/testrsrc/";
            elif self.sHost == 'os2':       self.sResourcePath = "T:/";
            elif self.sHost == 'solaris':   self.sResourcePath = "/mnt/testrsrc/";
            elif self.sHost == 'win':       self.sResourcePath = "T:/";
            else: raise GenError('unknown host OS "%s"' % (self.sHost));

        # PID file for the testdriver.
        self.sPidFile = os.path.join(self.sScratchPath, 'testdriver.pid');

        # Some stuff for the log...
        reporter.log('scratch: %s' % (self.sScratchPath,));

        # Get the absolute timeout (seconds since epoch, see
        # utils.timestampSecond()). None if not available.
        self.secTimeoutAbs = os.environ.get('TESTBOX_TIMEOUT_ABS', None);
        if self.secTimeoutAbs is not None:
            self.secTimeoutAbs = long(self.secTimeoutAbs);
            reporter.log('secTimeoutAbs: %s' % (self.secTimeoutAbs,));
        else:
            reporter.log('TESTBOX_TIMEOUT_ABS not found in the environment');

        # Distance from secTimeoutAbs that timeouts should be adjusted to.
        self.secTimeoutFudge = 30;

        # List of sub-test drivers (SubTestDriverBase derivatives).
        self.aoSubTstDrvs    = [] # type: list(SubTestDriverBase)

        # Use the scratch path for temporary files.
        if self.sHost in ['win', 'os2']:
            os.environ['TMP']     = self.sScratchPath;
            os.environ['TEMP']    = self.sScratchPath;
        os.environ['TMPDIR']      = self.sScratchPath;
        os.environ['IPRT_TMPDIR'] = self.sScratchPath; # IPRT/VBox specific.


    #
    # Resource utility methods.
    #

    def isResourceFile(self, sFile):
        """
        Checks if sFile is in in the resource set.
        """
        ## @todo need to deal with stuff in the validationkit.zip and similar.
        asRsrcs = self.getResourceSet();
        if sFile in asRsrcs:
            return os.path.isfile(os.path.join(self.sResourcePath, sFile));
        for sRsrc in asRsrcs:
            if sFile.startswith(sRsrc):
                sFull = os.path.join(self.sResourcePath, sRsrc);
                if os.path.isdir(sFull):
                    return os.path.isfile(os.path.join(self.sResourcePath, sRsrc));
        return False;

    def getFullResourceName(self, sName):
        """
        Returns the full resource name.
        """
        if os.path.isabs(sName): ## @todo Hack. Need to deal properly with stuff in the validationkit.zip and similar.
            return sName;
        return os.path.join(self.sResourcePath, sName);

    #
    # Scratch related utility methods.
    #

    def wipeScratch(self):
        """
        Removes the content of the scratch directory.
        Returns True on no errors, False + log entries on errors.
        """
        cErrors = wipeDirectory(self.sScratchPath);
        return cErrors == 0;

    #
    # Sub-test driver related methods.
    #

    def addSubTestDriver(self, oSubTstDrv):
        """
        Adds a sub-test driver.

        Returns True on success, false on failure.
        """
        assert isinstance(oSubTstDrv, SubTestDriverBase);
        if oSubTstDrv in self.aoSubTstDrvs:
            reporter.error('Attempt at adding sub-test driver %s twice.' % (oSubTstDrv.sName,));
            return False;
        self.aoSubTstDrvs.append(oSubTstDrv);
        return True;

    def showSubTstDrvUsage(self):
        """
        Shows the usage of the sub-test drivers.
        """
        for oSubTstDrv in self.aoSubTstDrvs:
            oSubTstDrv.showUsage();
        return True;

    def subTstDrvParseOption(self, asArgs, iArgs):
        """
        Lets the sub-test drivers have a go at the option.
        Returns the index of the next option if handled, otherwise iArgs.
        """
        for oSubTstDrv in self.aoSubTstDrvs:
            iNext = oSubTstDrv.parseOption(asArgs, iArgs)
            if iNext != iArgs:
                assert iNext > iArgs;
                assert iNext <= len(asArgs);
                return iNext;
        return iArgs;

    def findSubTstDrvByShortName(self, sShortName):
        """
        Locates a sub-test driver by it's short name.
        Returns sub-test driver object reference if found, None if not.
        """
        for oSubTstDrv in self.aoSubTstDrvs:
            if oSubTstDrv.sName == sShortName:
                return oSubTstDrv;
        return None;


    #
    # Task related methods.
    #

    def addTask(self, oTask):
        """
        Adds oTask to the task list.

        Returns True if the task was added.

        Returns False if the task was already in the task list.
        """
        if oTask in self.aoTasks:
            return False;
        #reporter.log2('adding task %s' % (oTask,));
        self.aoTasks.append(oTask);
        oTask.setTaskOwner(self);
        #reporter.log2('tasks now in list: %d - %s' % (len(self.aoTasks), self.aoTasks));
        return True;

    def removeTask(self, oTask):
        """
        Removes oTask to the task list.

        Returns oTask on success and None on failure.
        """
        try:
            #reporter.log2('removing task %s' % (oTask,));
            self.aoTasks.remove(oTask);
        except:
            return None;
        oTask.setTaskOwner(None);
        #reporter.log2('tasks left: %d - %s' % (len(self.aoTasks), self.aoTasks));
        return oTask;

    def removeAllTasks(self):
        """
        Removes all the task from the task list.

        Returns None.
        """
        aoTasks = self.aoTasks;
        self.aoTasks = [];
        for oTask in aoTasks:
            oTask.setTaskOwner(None);
        return None;

    def notifyAboutReadyTask(self, oTask):
        """
        Notificiation that there is a ready task.  May be called owning the
        task lock, so be careful wrt deadlocks.

        Remember to call super when overriding this.
        """
        if oTask is None: pass; # lint
        return None;

    def pollTasks(self):
        """
        Polls the task to see if any of them are ready.
        Returns the ready task, None if none are ready.
        """
        for oTask in self.aoTasks:
            if oTask.pollTask():
                return oTask;
        return None;

    def waitForTasksSleepWorker(self, cMsTimeout):
        """
        Overridable method that does the sleeping for waitForTask().

        cMsTimeout will not be larger than 1000, so there is normally no need
        to do any additional splitting up of the polling interval.

        Returns True if cMillieSecs elapsed.
        Returns False if some exception was raised while we waited or
        there turned out to be nothing to wait on.
        """
        try:
            self.aoTasks[0].waitForTask(cMsTimeout);
            return True;
        except Exception as oXcpt:
            reporter.log("waitForTasksSleepWorker: %s" % (str(oXcpt),));
            return False;

    def waitForTasks(self, cMsTimeout):
        """
        Waits for any of the tasks to require attention or a KeyboardInterrupt.
        Returns the ready task on success, None on timeout or interrupt.
        """
        try:
            #reporter.log2('waitForTasks: cMsTimeout=%d' % (cMsTimeout,));

            if cMsTimeout == 0:
                return self.pollTasks();

            if not self.aoTasks:
                return None;

            fMore = True;
            if cMsTimeout < 0:
                while fMore:
                    oTask = self.pollTasks();
                    if oTask is not None:
                        return oTask;
                    fMore = self.waitForTasksSleepWorker(1000);
            else:
                msStart = timestampMilli();
                while fMore:
                    oTask = self.pollTasks();
                    if oTask is not None:
                        #reporter.log2('waitForTasks: returning %s, msStart=%d' % \
                        #              (oTask, msStart));
                        return oTask;

                    cMsElapsed = timestampMilli() - msStart;
                    if cMsElapsed > cMsTimeout: # not ==, we want the final waitForEvents.
                        break;
                    cMsSleep = cMsTimeout - cMsElapsed;
                    cMsSleep = min(cMsSleep, 1000);
                    fMore = self.waitForTasksSleepWorker(cMsSleep);
        except KeyboardInterrupt:
            self.fInterrupted = True;
            reporter.errorXcpt('KeyboardInterrupt', 6);
        except:
            reporter.errorXcpt(None, 6);
        return None;

    #
    # PID file management methods.
    #

    def pidFileRead(self):
        """
        Worker that reads the PID file.
        Returns dictionary of PID with value (sName, fSudo), empty if no file.
        """
        dPids = {};
        if os.path.isfile(self.sPidFile):
            try:
                oFile = utils.openNoInherit(self.sPidFile, 'r');
                sContent = str(oFile.read());
                oFile.close();
            except:
                reporter.errorXcpt();
                return dPids;

            sContent = str(sContent).strip().replace('\n', ' ').replace('\r', ' ').replace('\t', ' ');
            for sProcess in sContent.split(' '):
                asFields = sProcess.split(':');
                if len(asFields) == 3 and asFields[0].isdigit():
                    try:
                        dPids[int(asFields[0])] = (asFields[2], asFields[1] == 'sudo');
                    except:
                        reporter.logXcpt('sProcess=%s' % (sProcess,));
                else:
                    reporter.log('%s: "%s"' % (self.sPidFile, sProcess));

        return dPids;

    def pidFileAdd(self, iPid, sName, fSudo = False):
        """
        Adds a PID to the PID file, creating the file if necessary.
        """
        try:
            oFile = utils.openNoInherit(self.sPidFile, 'a');
            oFile.write('%s:%s:%s\n'
                        % ( iPid,
                            'sudo' if fSudo else 'normal',
                             sName.replace(' ', '_').replace(':','_').replace('\n','_').replace('\r','_').replace('\t','_'),));
            oFile.close();
        except:
            reporter.errorXcpt();
            return False;
        ## @todo s/log/log2/
        reporter.log('pidFileAdd: added %s (%#x) %s fSudo=%s (new content: %s)'
                     % (iPid, iPid, sName, fSudo, self.pidFileRead(),));
        return True;

    def pidFileRemove(self, iPid, fQuiet = False):
        """
        Removes a PID from the PID file.
        """
        dPids = self.pidFileRead();
        if iPid not in dPids:
            if not fQuiet:
                reporter.log('pidFileRemove could not find %s in the PID file (content: %s)' % (iPid, dPids));
            return False;

        sName = dPids[iPid][0];
        del dPids[iPid];

        sPid = '';
        for iPid2, tNameSudo in dPids.items():
            sPid += '%s:%s:%s\n' % (iPid2, 'sudo' if tNameSudo[1] else 'normal', tNameSudo[0]);

        try:
            oFile = utils.openNoInherit(self.sPidFile, 'w');
            oFile.write(sPid);
            oFile.close();
        except:
            reporter.errorXcpt();
            return False;
        ## @todo s/log/log2/
        reporter.log('pidFileRemove: removed PID %d [%s] (new content: %s)' % (iPid, sName, self.pidFileRead(),));
        return True;

    def pidFileDelete(self):
        """Creates the testdriver PID file."""
        if os.path.isfile(self.sPidFile):
            try:
                os.unlink(self.sPidFile);
            except:
                reporter.logXcpt();
                return False;
            ## @todo s/log/log2/
            reporter.log('pidFileDelete: deleted "%s"' % (self.sPidFile,));
        return True;

    #
    # Misc helper methods.
    #

    def requireMoreArgs(self, cMinNeeded, asArgs, iArg):
        """
        Checks that asArgs has at least cMinNeeded args following iArg.

        Returns iArg + 1 if it checks out fine.
        Raise appropritate exception if not, ASSUMING that the current argument
        is found at iArg.
        """
        assert cMinNeeded >= 1;
        if iArg + cMinNeeded > len(asArgs):
            if cMinNeeded > 1:
                raise InvalidOption('The "%s" option takes %s values' % (asArgs[iArg], cMinNeeded,));
            raise InvalidOption('The "%s" option takes 1 value' % (asArgs[iArg],));
        return iArg + 1;

    def getBinTool(self, sName):
        """
        Returns the full path to the given binary validation kit tool.
        """
        return os.path.join(self.sBinPath, sName) + exeSuff();

    def adjustTimeoutMs(self, cMsTimeout, cMsMinimum = None):
        """
        Adjusts the given timeout (milliseconds) to take TESTBOX_TIMEOUT_ABS
        and cMsMinimum (optional) into account.

        Returns adjusted timeout.
        Raises no exceptions.
        """
        if self.secTimeoutAbs is not None:
            cMsToDeadline = self.secTimeoutAbs * 1000 - utils.timestampMilli();
            if cMsToDeadline >= 0:
                # Adjust for fudge and enforce the minimum timeout
                cMsToDeadline -= self.secTimeoutFudge * 1000;
                if cMsToDeadline < (cMsMinimum if cMsMinimum is not None else 10000):
                    cMsToDeadline = cMsMinimum if cMsMinimum is not None else 10000;

                # Is the timeout beyond the (adjusted) deadline, if so change it.
                if cMsTimeout > cMsToDeadline:
                    reporter.log('adjusting timeout: %s ms -> %s ms (deadline)\n' % (cMsTimeout, cMsToDeadline,));
                    return cMsToDeadline;
                reporter.log('adjustTimeoutMs: cMsTimeout (%s) <= cMsToDeadline (%s)' % (cMsTimeout, cMsToDeadline,));
            else:
                # Don't bother, we've passed the deadline.
                reporter.log('adjustTimeoutMs: ooops! cMsToDeadline=%s (%s), timestampMilli()=%s, timestampSecond()=%s'
                             % (cMsToDeadline, cMsToDeadline*1000, utils.timestampMilli(), utils.timestampSecond()));

        # Only enforce the minimum timeout if specified.
        if cMsMinimum is not None and cMsTimeout < cMsMinimum:
            reporter.log('adjusting timeout: %s ms -> %s ms (minimum)\n' % (cMsTimeout, cMsMinimum,));
            cMsTimeout = cMsMinimum;

        return cMsTimeout;

    def prepareResultFile(self, sName = 'results.xml'):
        """
        Given a base name (no path, but extension if required), a scratch file
        name is computed and any previous file removed.

        Returns the full path to the file sName.
        Raises exception on failure.
        """
        sXmlFile = os.path.join(self.sScratchPath, sName);
        if os.path.exists(sXmlFile):
            os.unlink(sXmlFile);
        return sXmlFile;


    #
    # Overridable methods.
    #

    def showUsage(self):
        """
        Shows the usage.

        When overriding this, call super first.
        """
        sName = os.path.basename(sys.argv[0]);
        reporter.log('Usage: %s [options] <action(s)>' % (sName,));
        reporter.log('');
        reporter.log('Actions (in execution order):');
        reporter.log('  cleanup-before');
        reporter.log('      Cleanups done at the start of testing.');
        reporter.log('  verify');
        reporter.log('      Verify that all necessary resources are present.');
        reporter.log('  config');
        reporter.log('      Configure the tests.');
        reporter.log('  execute');
        reporter.log('      Execute the tests.');
        reporter.log('  cleanup-after');
        reporter.log('      Cleanups done at the end of the testing.');
        reporter.log('');
        reporter.log('Special Actions:');
        reporter.log('  all');
        reporter.log('      Alias for: %s' % (' '.join(self.asNormalActions),));
        reporter.log('  extract <path>');
        reporter.log('      Extract the test resources and put them in the specified');
        reporter.log('      path for off side/line testing.');
        reporter.log('  abort');
        reporter.log('      Aborts the test.');
        reporter.log('');
        reporter.log('Base Options:');
        reporter.log('  -h, --help');
        reporter.log('      Show this help message.');
        reporter.log('  -v, --verbose');
        reporter.log('      Increase logging verbosity, repeat for more logging.');
        reporter.log('  -d, --debug');
        reporter.log('      Increase the debug logging level, repeat for more info.');
        reporter.log('  --no-wipe-clean');
        reporter.log('      Do not wipe clean the scratch area during the two clean up');
        reporter.log('      actions.  This is for facilitating nested test driver execution.');
        if self.aoSubTstDrvs:
            reporter.log('  --enable-sub-driver <sub1>[:..]');
            reporter.log('  --disable-sub-driver <sub1>[:..]');
            reporter.log('     Enables or disables one or more of the sub drivers: %s'
                         % (', '.join([oSubTstDrv.sName for oSubTstDrv in self.aoSubTstDrvs]),));
        return True;

    def parseOption(self, asArgs, iArg):
        """
        Parse an option. Override this.

        Keyword arguments:
        asArgs -- The argument vector.
        iArg   -- The index of the current argument.

        Returns iArg if the option was not recognized.
        Returns the index of the next argument when something is consumed.
        In the event of a syntax error, a InvalidOption or QuietInvalidOption
        should be thrown.
        """

        if asArgs[iArg] in ('--help', '-help', '-h', '-?', '/?', '/help', '/H', '-H'):
            self.showUsage();
            self.showSubTstDrvUsage();
            raise QuietInvalidOption();

        # options
        if asArgs[iArg] in ('--verbose', '-v'):
            reporter.incVerbosity()
        elif asArgs[iArg] in ('--debug', '-d'):
            reporter.incDebug()
        elif asArgs[iArg] == '--no-wipe-clean':
            self.fNoWipeClean = True;
        elif asArgs[iArg] in ('--enable-sub-driver', '--disable-sub-driver') and self.aoSubTstDrvs:
            sOption = asArgs[iArg];
            iArg = self.requireMoreArgs(1, asArgs, iArg);
            for sSubTstDrvName in asArgs[iArg].split(':'):
                oSubTstDrv = self.findSubTstDrvByShortName(sSubTstDrvName);
                if oSubTstDrv is None:
                    raise InvalidOption('Unknown sub-test driver given to %s: %s' % (sOption, sSubTstDrvName,));
                oSubTstDrv.fEnabled = sOption == '--enable-sub-driver';
        elif (asArgs[iArg] == 'all' or asArgs[iArg] in self.asNormalActions) \
          and self.asActions in self.asSpecialActions:
            raise InvalidOption('selected special action "%s" already' % (self.asActions[0], ));
        # actions
        elif asArgs[iArg] == 'all':
            self.asActions = [ 'all' ];
        elif asArgs[iArg] in self.asNormalActions:
            self.asActions.append(asArgs[iArg])
        elif asArgs[iArg] in self.asSpecialActions:
            if self.asActions:
                raise InvalidOption('selected special action "%s" already' % (self.asActions[0], ));
            self.asActions = [ asArgs[iArg] ];
            # extact <destination>
            if asArgs[iArg] == 'extract':
                iArg = iArg + 1;
                if iArg >= len(asArgs): raise InvalidOption('The "extract" action requires a destination directory');
                self.sExtractDstPath = asArgs[iArg];
        else:
            return iArg;
        return iArg + 1;

    def completeOptions(self):
        """
        This method is called after parsing all the options.
        Returns success indicator. Use the reporter to complain.

        Overriable, call super.
        """
        return True;

    def getResourceSet(self):
        """
        Returns a set of file and/or directory names relative to
        TESTBOX_PATH_RESOURCES.

        Override this, call super when using sub-test drivers.
        """
        asRsrcs = [];
        for oSubTstDrv in self.aoSubTstDrvs:
            asRsrcs.extend(oSubTstDrv.asRsrcs);
        return asRsrcs;

    def actionExtract(self):
        """
        Handle the action that extracts the test resources for off site use.
        Returns a success indicator and error details with the reporter.

        There is usually no need to override this.
        """
        fRc = True;
        asRsrcs = self.getResourceSet();
        for iRsrc, sRsrc in enumerate(asRsrcs):
            reporter.log('Resource #%s: "%s"' % (iRsrc, sRsrc));
            sSrcPath = os.path.normpath(os.path.abspath(os.path.join(self.sResourcePath, sRsrc.replace('/', os.path.sep))));
            sDstPath = os.path.normpath(os.path.join(self.sExtractDstPath, sRsrc.replace('/', os.path.sep)));

            sDstDir = os.path.dirname(sDstPath);
            if not os.path.exists(sDstDir):
                try:    os.makedirs(sDstDir, 0o775);
                except: fRc = reporter.errorXcpt('Error creating directory "%s":' % (sDstDir,));

            if os.path.isfile(sSrcPath):
                try:    utils.copyFileSimple(sSrcPath, sDstPath);
                except: fRc = reporter.errorXcpt('Error copying "%s" to "%s":' % (sSrcPath, sDstPath,));
            elif os.path.isdir(sSrcPath):
                fRc = reporter.error('Extracting directories have not been implemented yet');
            else:
                fRc = reporter.error('Missing or unsupported resource type: %s' % (sSrcPath,));
        return fRc;

    def actionVerify(self):
        """
        Handle the action that verify the test resources.
        Returns a success indicator and error details with the reporter.

        There is usually no need to override this.
        """

        asRsrcs = self.getResourceSet();
        for sRsrc in asRsrcs:
            # Go thru some pain to catch escape sequences.
            if sRsrc.find("//") >= 0:
                reporter.error('Double slash test resource name: "%s"' % (sRsrc));
                return False;
            if   sRsrc == ".." \
              or sRsrc.startswith("../") \
              or sRsrc.find("/../") >= 0 \
              or sRsrc.endswith("/.."):
                reporter.error('Relative path in test resource name: "%s"' % (sRsrc));
                return False;

            sFull = os.path.normpath(os.path.abspath(os.path.join(self.sResourcePath, sRsrc)));
            if not sFull.startswith(os.path.normpath(self.sResourcePath)):
                reporter.error('sFull="%s" self.sResourcePath=%s' % (sFull, self.sResourcePath));
                reporter.error('The resource "%s" seems to specify a relative path' % (sRsrc));
                return False;

            reporter.log2('Checking for resource "%s" at "%s" ...' % (sRsrc, sFull));
            if os.path.isfile(sFull):
                try:
                    oFile = utils.openNoInherit(sFull, "rb");
                    oFile.close();
                except Exception as oXcpt:
                    reporter.error('The file resource "%s" cannot be accessed: %s' % (sFull, oXcpt));
                    return False;
            elif os.path.isdir(sFull):
                if not os.path.isdir(os.path.join(sFull, '.')):
                    reporter.error('The directory resource "%s" cannot be accessed' % (sFull));
                    return False;
            elif os.path.exists(sFull):
                reporter.error('The resource "%s" is not a file or directory' % (sFull));
                return False;
            else:
                reporter.error('The resource "%s" was not found' % (sFull));
                return False;
        return True;

    def actionConfig(self):
        """
        Handle the action that configures the test.
        Returns True (success), False (failure) or None (skip the test),
        posting complaints and explanations with the reporter.

        Override this.
        """
        return True;

    def actionExecute(self):
        """
        Handle the action that executes the test.

        Returns True (success), False (failure) or None (skip the test),
        posting complaints and explanations with the reporter.

        Override this.
        """
        return True;

    def actionCleanupBefore(self):
        """
        Handle the action that cleans up spills from previous tests before
        starting the tests.  This is mostly about wiping the scratch space
        clean in local runs.  On a testbox the testbox script will use the
        cleanup-after if the test is interrupted.

        Returns True (success), False (failure) or None (skip the test),
        posting complaints and explanations with the reporter.

        Override this, but call super to wipe the scratch directory.
        """
        if self.fNoWipeClean is False:
            self.wipeScratch();
        return True;

    def actionCleanupAfter(self):
        """
        Handle the action that cleans up all spills from executing the test.

        Returns True (success) or False (failure) posting complaints and
        explanations with the reporter.

        Override this, but call super to wipe the scratch directory.
        """
        if self.fNoWipeClean is False:
            self.wipeScratch();
        return True;

    def actionAbort(self):
        """
        Handle the action that aborts a (presumed) running testdriver, making
        sure to include all it's children.

        Returns True (success) or False (failure) posting complaints and
        explanations with the reporter.

        Override this, but call super to kill the testdriver script and any
        other process covered by the testdriver PID file.
        """

        dPids = self.pidFileRead();
        reporter.log('The pid file contained: %s' % (dPids,));

        #
        # Try convince the processes to quit with increasing impoliteness.
        #
        if sys.platform == 'win32':
            afnMethods = [ processInterrupt, processTerminate ];
        else:
            afnMethods = [ sendUserSignal1, processInterrupt, processTerminate, processKill ];
        for fnMethod in afnMethods:
            for iPid, tNameSudo in dPids.items():
                fnMethod(iPid, fSudo = tNameSudo[1]);

            for i in range(10):
                if i > 0:
                    time.sleep(1);

                dPidsToRemove = []; # Temporary dict to append PIDs to remove later.

                for iPid, tNameSudo in dPids.items():
                    if not processExists(iPid):
                        reporter.log('%s (%s) terminated' % (tNameSudo[0], iPid,));
                        self.pidFileRemove(iPid, fQuiet = True);
                        dPidsToRemove.append(iPid);
                        continue;

                # Remove PIDs from original dictionary, as removing keys from a
                # dictionary while iterating on it won't work and will result in a RuntimeError.
                for iPidToRemove in dPidsToRemove:
                    del dPids[iPidToRemove];

                if not dPids:
                    reporter.log('All done.');
                    return True;

                if i in [4, 8]:
                    reporter.log('Still waiting for: %s (method=%s)' % (dPids, fnMethod,));

        reporter.log('Failed to terminate the following processes: %s' % (dPids,));
        return False;


    def onExit(self, iRc):
        """
        Hook for doing very important cleanups on the way out.

        iRc is the exit code or -1 in the case of an unhandled exception.
        Returns nothing and shouldn't raise exceptions (will be muted+ignored).
        """
        _ = iRc;
        return None;


    #
    # main() - don't override anything!
    #

    def main(self, asArgs = None):
        """
        The main function of the test driver.

        Keyword arguments:
        asArgs -- The argument vector.  Defaults to sys.argv.

        Returns exit code. No exceptions.
        """

        #
        # Wrap worker in exception handler and always call a 'finally' like
        # method to do crucial cleanups on the way out.
        #
        try:
            iRc = self.innerMain(asArgs);
        except:
            reporter.logXcpt(cFrames = None);
            try:
                self.onExit(-1);
            except:
                reporter.logXcpt();
            raise;
        self.onExit(iRc);
        return iRc;


    def innerMain(self, asArgs = None): # pylint: disable=too-many-statements
        """
        Exception wrapped main() worker.
        """

        #
        # Parse the arguments.
        #
        if asArgs is None:
            asArgs = list(sys.argv);
        iArg = 1;
        try:
            while iArg < len(asArgs):
                iNext = self.parseOption(asArgs, iArg);
                if iNext == iArg:
                    iNext = self.subTstDrvParseOption(asArgs, iArg);
                    if iNext == iArg:
                        raise InvalidOption('unknown option: %s' % (asArgs[iArg]))
                iArg = iNext;
        except QuietInvalidOption:
            return rtexitcode.RTEXITCODE_SYNTAX;
        except InvalidOption as oXcpt:
            reporter.error(oXcpt.str());
            return rtexitcode.RTEXITCODE_SYNTAX;
        except:
            reporter.error('unexpected exception while parsing argument #%s' % (iArg));
            traceback.print_exc();
            return rtexitcode.RTEXITCODE_SYNTAX;

        if not self.completeOptions():
            return rtexitcode.RTEXITCODE_SYNTAX;

        if not self.asActions:
            reporter.error('no action was specified');
            reporter.error('valid actions: %s' % (self.asNormalActions + self.asSpecialActions + ['all']));
            return rtexitcode.RTEXITCODE_SYNTAX;

        #
        # Execte the actions.
        #
        fRc = True;         # Tristate - True (success), False (failure), None (skipped).
        asActions = list(self.asActions); # Must copy it or vboxinstaller.py breaks.
        if 'extract' in asActions:
            reporter.log('*** extract action ***');
            asActions.remove('extract');
            fRc = self.actionExtract();
            reporter.log('*** extract action completed (fRc=%s) ***' % (fRc));
        elif 'abort' in asActions:
            reporter.appendToProcessName('/abort'); # Make it easier to spot in the log.
            reporter.log('*** abort action ***');
            asActions.remove('abort');
            fRc = self.actionAbort();
            reporter.log('*** abort action completed (fRc=%s) ***' % (fRc));
        else:
            if asActions == [ 'all' ]:
                asActions = list(self.asNormalActions);

            if 'verify' in asActions:
                reporter.log('*** verify action ***');
                asActions.remove('verify');
                fRc = self.actionVerify();
                if fRc is True: reporter.log("verified succeeded");
                else:           reporter.log("verified failed (fRc=%s)" % (fRc,));
                reporter.log('*** verify action completed (fRc=%s) ***' % (fRc,));

            if 'cleanup-before' in asActions:
                reporter.log('*** cleanup-before action ***');
                asActions.remove('cleanup-before');
                fRc2 = self.actionCleanupBefore();
                if fRc2 is not True: reporter.log("cleanup-before failed");
                if fRc2 is not True and fRc is True: fRc = fRc2;
                reporter.log('*** cleanup-before action completed (fRc2=%s, fRc=%s) ***' % (fRc2, fRc,));

            self.pidFileAdd(os.getpid(), os.path.basename(sys.argv[0]));

            if 'config' in asActions and fRc is True:
                asActions.remove('config');
                reporter.log('*** config action ***');
                fRc = self.actionConfig();
                if fRc is True:     reporter.log("config succeeded");
                elif fRc is None:   reporter.log("config skipping test");
                else:               reporter.log("config failed");
                reporter.log('*** config action completed (fRc=%s) ***' % (fRc,));

            if 'execute' in asActions and fRc is True:
                asActions.remove('execute');
                reporter.log('*** execute action ***');
                fRc = self.actionExecute();
                if fRc is True:     reporter.log("execute succeeded");
                elif fRc is None:   reporter.log("execute skipping test");
                else:               reporter.log("execute failed (fRc=%s)" % (fRc,));
                reporter.testCleanup();
                reporter.log('*** execute action completed (fRc=%s) ***' % (fRc,));

            if 'cleanup-after' in asActions:
                reporter.log('*** cleanup-after action ***');
                asActions.remove('cleanup-after');
                fRc2 = self.actionCleanupAfter();
                if fRc2 is not True: reporter.log("cleanup-after failed");
                if fRc2 is not True and fRc is True: fRc = fRc2;
                reporter.log('*** cleanup-after action completed (fRc2=%s, fRc=%s) ***' % (fRc2, fRc,));

            self.pidFileRemove(os.getpid());

        if asActions and fRc is True:
            reporter.error('unhandled actions: %s' % (asActions,));
            fRc = False;

        #
        # Done - report the final result.
        #
        if fRc is None:
            if self.fBadTestbox:
                reporter.log('****************************************************************');
                reporter.log('*** The test driver SKIPPED the test because of BAD_TESTBOX. ***');
                reporter.log('****************************************************************');
                return rtexitcode.RTEXITCODE_BAD_TESTBOX;
            reporter.log('*****************************************');
            reporter.log('*** The test driver SKIPPED the test. ***');
            reporter.log('*****************************************');
            return rtexitcode.RTEXITCODE_SKIPPED;
        if fRc is not True:
            reporter.error('!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!');
            reporter.error('!!! The test driver FAILED (in case we forgot to mention it). !!!');
            reporter.error('!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!');
            return rtexitcode.RTEXITCODE_FAILURE;
        reporter.log('*******************************************');
        reporter.log('*** The test driver exits successfully. ***');
        reporter.log('*******************************************');
        return rtexitcode.RTEXITCODE_SUCCESS;

# The old, deprecated name.
TestDriver = TestDriverBase; # pylint: disable=invalid-name


#
# Unit testing.
#

# pylint: disable=missing-docstring
class TestDriverBaseTestCase(unittest.TestCase):
    def setUp(self):
        self.oTstDrv = TestDriverBase();
        self.oTstDrv.pidFileDelete();

    def tearDown(self):
        pass; # clean up scratch dir and such.

    def testPidFile(self):

        iPid1 = os.getpid() + 1;
        iPid2 = os.getpid() + 2;

        self.assertTrue(self.oTstDrv.pidFileAdd(iPid1, 'test1'));
        self.assertEqual(self.oTstDrv.pidFileRead(), {iPid1:('test1',False)});

        self.assertTrue(self.oTstDrv.pidFileAdd(iPid2, 'test2', fSudo = True));
        self.assertEqual(self.oTstDrv.pidFileRead(), {iPid1:('test1',False), iPid2:('test2',True)});

        self.assertTrue(self.oTstDrv.pidFileRemove(iPid1));
        self.assertEqual(self.oTstDrv.pidFileRead(), {iPid2:('test2',True)});

        self.assertTrue(self.oTstDrv.pidFileRemove(iPid2));
        self.assertEqual(self.oTstDrv.pidFileRead(), {});

        self.assertTrue(self.oTstDrv.pidFileDelete());

if __name__ == '__main__':
    unittest.main();
    # not reached.
