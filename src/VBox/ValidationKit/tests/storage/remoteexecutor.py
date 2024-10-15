# -*- coding: utf-8 -*-
# $Id: remoteexecutor.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Storage benchmark, test execution helpers.
"""

__copyright__ = \
"""
Copyright (C) 2016-2024 Oracle and/or its affiliates.

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
import array;
import os;
import shutil;
import sys;
if sys.version_info[0] >= 3:
    from io import StringIO as StringIO;            # pylint: disable=import-error,no-name-in-module,useless-import-alias
else:
    from StringIO import StringIO as StringIO;      # pylint: disable=import-error,no-name-in-module,useless-import-alias
import subprocess;

# Validation Kit imports.
from common     import utils;
from testdriver import reporter;



class StdInOutBuffer(object):
    """ Standard input output buffer """

    def __init__(self, sInput = None):
        self.sInput = StringIO();
        if sInput is not None:
            self.sInput.write(self._toString(sInput));
            self.sInput.seek(0);
        self.sOutput  = '';

    def _toString(self, sText):
        """
        Converts any possible array to
        a string.
        """
        if isinstance(sText, array.array):
            try:
                if sys.version_info < (3, 9, 0):
                    # Removed since Python 3.9.
                    return str(sText.tostring()); # pylint: disable=no-member
                return str(sText.tobytes());
            except:
                pass;
        elif isinstance(sText, bytes):
            return sText.decode('utf-8');

        return sText;

    def read(self, cb):
        """file.read"""
        return self.sInput.read(cb);

    def write(self, sText):
        """file.write"""
        self.sOutput += self._toString(sText);
        return None;

    def getOutput(self):
        """
        Returns the output of the buffer.
        """
        return self.sOutput;

    def close(self):
        """ file.close """
        return;

class RemoteExecutor(object):
    """
    Helper for executing tests remotely through TXS or locally
    """

    def __init__(self, oTxsSession = None, asBinaryPaths = None, sScratchPath = None):
        self.oTxsSession  = oTxsSession;
        self.asPaths      = asBinaryPaths;
        self.sScratchPath = sScratchPath;
        if self.asPaths is None:
            self.asPaths = [ ];

    def _getBinaryPath(self, sBinary):
        """
        Returns the complete path of the given binary if found
        from the configured search path or None if not found.
        """
        for sPath in self.asPaths:
            sFile = sPath + '/' + sBinary;
            if self.isFile(sFile):
                return sFile;
        return sBinary;

    def _sudoExecuteSync(self, asArgs, sInput):
        """
        Executes a sudo child process synchronously.
        Returns a tuple [True, 0] if the process executed successfully
        and returned 0, otherwise [False, rc] is returned.
        """
        reporter.log('Executing [sudo]: %s' % (asArgs, ));
        reporter.flushall();
        fRc = True;
        sOutput = '';
        sError = '';
        try:
            oProcess = utils.sudoProcessPopen(asArgs, stdout=subprocess.PIPE, stdin=subprocess.PIPE,
                                              stderr=subprocess.PIPE, shell = False, close_fds = False);

            sOutput, sError = oProcess.communicate(sInput);
            iExitCode  = oProcess.poll();

            if iExitCode != 0:
                fRc = False;
        except:
            reporter.errorXcpt();
            fRc = False;
        reporter.log('Exit code [sudo]: %s (%s)' % (fRc, asArgs));
        return (fRc, str(sOutput), str(sError));

    def _execLocallyOrThroughTxs(self, sExec, asArgs, sInput, cMsTimeout):
        """
        Executes the given program locally or through TXS based on the
        current config.
        """
        fRc = False;
        sOutput = None;
        if self.oTxsSession is not None:
            reporter.log('Executing [remote]: %s %s %s' % (sExec, asArgs, sInput));
            reporter.flushall();
            oStdOut = StdInOutBuffer();
            oStdErr = StdInOutBuffer();
            oTestPipe = reporter.FileWrapperTestPipe();
            oStdIn = None;
            if sInput is not None:
                oStdIn = StdInOutBuffer(sInput);
            else:
                oStdIn = '/dev/null'; # pylint: disable=redefined-variable-type
            fRc = self.oTxsSession.syncExecEx(sExec, (sExec,) + asArgs,
                                              oStdIn = oStdIn, oStdOut = oStdOut,
                                              oStdErr = oStdErr, oTestPipe = oTestPipe,
                                              cMsTimeout = cMsTimeout);
            sOutput = oStdOut.getOutput();
            sError = oStdErr.getOutput();
            if fRc is False:
                reporter.log('Exit code [remote]: %s (stdout: %s stderr: %s)' % (fRc, sOutput, sError));
            else:
                reporter.log('Exit code [remote]: %s' % (fRc,));
        else:
            fRc, sOutput, sError = self._sudoExecuteSync([sExec, ] + list(asArgs), sInput);
        return (fRc, sOutput, sError);

    def execBinary(self, sExec, asArgs, sInput = None, cMsTimeout = 3600000):
        """
        Executes the given binary with the given arguments
        providing some optional input through stdin and
        returning whether the process exited successfully and the output
        in a string.
        """

        fRc = True;
        sOutput = None;
        sError = None;
        sBinary = self._getBinaryPath(sExec);
        if sBinary is not None:
            fRc, sOutput, sError = self._execLocallyOrThroughTxs(sBinary, asArgs, sInput, cMsTimeout);
            # If verbose logging is enabled and the process failed for whatever reason, log its output to the reporter.
            fLog = False;
            if  not fRc \
            and reporter.getVerbosity() >= 2: # Verbose logging starts at level 2.
                fLog = True;
            elif reporter.getVerbosity() >= 3:
                fLog = True;

            if fLog:
                asOutput = sOutput.splitlines();
                for sLine in asOutput:
                    reporter.log('%s [stdout]: %s' % (sExec, sLine.encode(encoding = 'UTF-8', errors = 'strict'),));
                asError = sError.splitlines();
                for sLine in asError:
                    reporter.log('%s [stderr]: %s' % (sExec, sLine.encode(encoding = 'UTF-8', errors = 'strict'),));
        else:
            fRc = False;
        return (fRc, sOutput, sError);

    def execBinaryNoStdOut(self, sExec, asArgs, sInput = None):
        """
        Executes the given binary with the given arguments
        providing some optional input through stdin and
        returning whether the process exited successfully.
        """
        fRc, _, _ = self.execBinary(sExec, asArgs, sInput);
        return fRc;

    def copyFile(self, sLocalFile, sFilename, cMsTimeout = 30000):
        """
        Copies the local file to the remote destination
        if configured

        Returns a file ID which can be used as an input parameter
        to execBinary() resolving to the real filepath on the remote side
        or locally.
        """
        sFileId = None;
        if self.oTxsSession is not None:
            sFileId = '${SCRATCH}/' + sFilename;
            fRc = self.oTxsSession.syncUploadFile(sLocalFile, sFileId, cMsTimeout);
            if not fRc:
                sFileId = None;
        else:
            sFileId = self.sScratchPath + '/' + sFilename;
            try:
                shutil.copy(sLocalFile, sFileId);
            except:
                sFileId = None;

        return sFileId;

    def copyString(self, sContent, sFilename, cMsTimeout = 30000):
        """
        Creates a file remotely or locally with the given content.

        Returns a file ID which can be used as an input parameter
        to execBinary() resolving to the real filepath on the remote side
        or locally.
        """
        sFileId = None;
        if self.oTxsSession is not None:
            sFileId = '${SCRATCH}/' + sFilename;
            fRc = self.oTxsSession.syncUploadString(sContent, sFileId, cMsTimeout);
            if not fRc:
                sFileId = None;
        else:
            sFileId = self.sScratchPath + '/' + sFilename;
            try:
                with open(sFileId, 'wb') as oFile:
                    oFile.write(sContent);
            except:
                sFileId = None;

        return sFileId;

    def mkDir(self, sDir, fMode = 0o700, cMsTimeout = 30000):
        """
        Creates a new directory at the given location.
        """
        fRc = True;
        if self.oTxsSession is not None:
            fRc = self.oTxsSession.syncMkDir(sDir, fMode, cMsTimeout, fIgnoreErrors=False);
        elif not os.path.isdir(sDir):
            reporter.log("if no txs session found and os.path.isdir is False do os.mkdir for %s" % sDir)
            os.mkdir(sDir, fMode)  # os.mkdir function returns nothing
            fRc = os.path.isdir(sDir)
        return fRc;

    def rmDir(self, sDir, cMsTimeout = 30000):
        """
        Removes the given directory.
        """
        fRc = True;
        if self.oTxsSession is not None:
            fRc = self.oTxsSession.syncRmDir(sDir, cMsTimeout);
        else:
            fRc = self.execBinaryNoStdOut('rmdir', (sDir,));

        return fRc;

    def rmTree(self, sDir, cMsTimeout = 30000):
        """
        Recursively removes all files and subdirectories including the given directory.
        """
        fRc = True;
        if self.oTxsSession is not None:
            reporter.log("rmTree (%s) using txs" % sDir);
            fRc = self.oTxsSession.syncRmTree(sDir, cMsTimeout);
        else:
            try:
                if os.path.isfile(sDir):
                    reporter.log("deleting file: %s" % sDir)
                    os.remove(sDir)
                else:
                    reporter.log("rmTree (%s) using shutil.rmtree function" % sDir);
                    shutil.rmtree(sDir);
            except:
                fRc = False;

        return fRc;

    def isFile(self, sPath, cMsTimeout = 30000):
        """
        Checks that the given file  exists.
        """
        fRc = True;
        if self.oTxsSession is not None:
            fRc = self.oTxsSession.syncIsFile(sPath, cMsTimeout);
        else:
            try:
                fRc = os.path.isfile(sPath);
            except:
                fRc = False;

        return fRc;
