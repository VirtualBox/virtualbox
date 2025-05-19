#!/usr/bin/env python
# -*- coding: utf-8 -*-
# pylint: disable=too-many-lines
# $Id: tdAddGuestCtrl.py 168933 2025-05-19 17:09:43Z andreas.loeffler@oracle.com $

"""
VirtualBox Validation Kit - Guest Control Tests.
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
__version__ = "$Revision: 168933 $"

# Standard Python imports.
import errno
import os
import random
import string
import struct
import sys
import threading
import time

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from testdriver import testfileset;
from testdriver import vbox;
from testdriver import vboxcon;
from testdriver import vboxtestfileset;
from testdriver import vboxwrappers;
from common     import utils;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int      # pylint: disable=redefined-builtin,invalid-name
    xrange = range; # pylint: disable=redefined-builtin,invalid-name

def limitString(sString, cLimit = 128):
    """
    Returns a string with ellipsis ("...") when exceeding the specified limit.
    Useful for toning down logging. By default strings will be shortened at 128 characters.
    """
    if not isinstance(sString, str):
        sString = str(sString);
    cLen = len(sString);
    if not cLen:
        return '';
    return (sString[:cLimit] + '...[%d more]' % (cLen - cLimit)) if cLen > cLimit else sString;

class GuestStream(bytearray):
    """
    Class for handling a guest process input/output stream.

    @todo write stdout/stderr tests.
    """
    def appendStream(self, stream, convertTo = '<b'):
        """
        Appends and converts a byte sequence to this object;
        handy for displaying a guest stream.
        """
        self.extend(struct.pack(convertTo, stream));


class tdCtxCreds(object):
    """
    Provides credentials to pass to the guest.
    """
    def __init__(self, sUser = None, sPassword = None, sDomain = None):
        self.oTestVm   = None;
        self.sUser     = sUser;
        self.sPassword = sPassword;
        self.sDomain   = sDomain;

    def applyDefaultsIfNotSet(self, oTestVm):
        """
        Applies credential defaults, based on the test VM (guest OS), if
        no credentials were set yet.

        Returns success status.
        """
        self.oTestVm = oTestVm;
        if not self.oTestVm:
            reporter.log('VM object is invalid -- did VBoxSVC or a client crash?');
            return False;

        if self.sUser is None:
            self.sUser = self.oTestVm.getTestUser();

        if self.sPassword is None:
            self.sPassword = self.oTestVm.getTestUserPassword(self.sUser);

        if self.sDomain is None:
            self.sDomain   = '';

        return True;

class tdTestGuestCtrlBase(object):
    """
    Base class for all guest control tests.

    Note: This test ASSUMES that working Guest Additions
          were installed and running on the guest to be tested.
    """
    def __init__(self, oCreds = None):
        self.oGuest    = None;      ##< IGuest.
        self.oTestVm   = None;
        self.oCreds    = oCreds     ##< type: tdCtxCreds
        self.timeoutMS = 30 * 1000; ##< 30s timeout
        self.oGuestSession = None;  ##< IGuestSession reference or None.

    def setEnvironment(self, oSession, oTxsSession, oTestVm):
        """
        Sets the test environment required for this test.

        Returns success status.
        """
        _ = oTxsSession;

        fRc = True;
        try:
            self.oGuest  = oSession.o.console.guest;
            self.oTestVm = oTestVm;
        except:
            fRc = reporter.errorXcpt();

        if self.oCreds is None:
            self.oCreds = tdCtxCreds();

        fRc = fRc and self.oCreds.applyDefaultsIfNotSet(self.oTestVm);

        if not fRc:
            reporter.log('Error setting up Guest Control testing environment!');

        return fRc;

    def uploadLogData(self, oTstDrv, aData, sFileName, sDesc):
        """
        Uploads (binary) data to a log file for manual (later) inspection.
        """
        reporter.log('Creating + uploading log data file "%s"' % sFileName);
        sHstFileName = os.path.join(oTstDrv.sScratchPath, sFileName);
        try:
            with open(sHstFileName, "wb") as oCurTestFile:
                oCurTestFile.write(aData);
        except:
            return reporter.error('Unable to create temporary file for "%s"' % (sDesc,));
        return reporter.addLogFile(sHstFileName, 'misc/other', sDesc);

    def createSession(self, sName, fIsError = True):
        """
        Creates (opens) a guest session.
        Returns (True, IGuestSession) on success or (False, None) on failure.
        """
        if self.oGuestSession is None:
            if sName is None:
                sName = "<untitled>";

            reporter.log('Creating session "%s" ...' % (sName,));
            try:
                self.oGuestSession = self.oGuest.createSession(self.oCreds.sUser,
                                                               self.oCreds.sPassword,
                                                               self.oCreds.sDomain,
                                                               sName);
            except:
                # Just log, don't assume an error here (will be done in the main loop then).
                reporter.maybeErrXcpt(fIsError, 'Creating a guest session "%s" failed; sUser="%s", pw="%s", sDomain="%s":'
                                      % (sName, self.oCreds.sUser, self.oCreds.sPassword, self.oCreds.sDomain));
                return (False, None);

            tsStartMs = base.timestampMilli();
            while base.timestampMilli() - tsStartMs < self.timeoutMS:
                reporter.log('Waiting for session "%s" to start within %dms...' % (sName, self.timeoutMS));
                aeWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start, ];
                try:
                    waitResult = self.oGuestSession.waitForArray(aeWaitFor, self.timeoutMS);

                    # Log session status changes.
                    if waitResult is vboxcon.GuestSessionWaitResult_Status:
                        reporter.log('Session "%s" indicated status change (status is now %d)' \
                                     % (sName, self.oGuestSession.status));
                        if self.oGuestSession.status is vboxcon.GuestSessionStatus_Started:
                            # We indicate an error here, as we intentionally waited for the session start
                            # in the wait call above and got back a status change instead.
                            reporter.error('Session "%s" successfully started (thru status change)' % (sName,));
                            break;
                        continue; # Continue waiting for the session to start.

                    #
                    # Be nice to Guest Additions < 4.3: They don't support session handling and
                    # therefore return WaitFlagNotSupported.
                    #
                    if waitResult not in (vboxcon.GuestSessionWaitResult_Start, \
                                          vboxcon.GuestSessionWaitResult_WaitFlagNotSupported):
                        # Just log, don't assume an error here (will be done in the main loop then).
                        reporter.maybeErr(fIsError, 'Session did not start successfully, returned wait result: %d' \
                                          % (waitResult,));
                        return (False, None);
                    reporter.log('Session "%s" successfully started' % (sName,));

                    #
                    # Make sure that the test VM configuration and Guest Control use the same path separator style for the guest.
                    #
                    sGstSep = '\\' if self.oGuestSession.pathStyle is vboxcon.PathStyle_DOS else '/';
                    if self.oTestVm.pathSep() != sGstSep:
                        reporter.error('Configured test VM uses a different path style (%s) than Guest Control (%s)' \
                                       % (self.oTestVm.pathSep(), sGstSep));
                    break;
                except:
                    # Just log, don't assume an error here (will be done in the main loop then).
                    reporter.maybeErrXcpt(fIsError, 'Waiting for guest session "%s" (usr=%s;pw=%s;dom=%s) to start failed:'
                                          % (sName, self.oCreds.sUser, self.oCreds.sPassword, self.oCreds.sDomain,));
                    return (False, None);
        else:
            reporter.log('Warning: Session already set; this is probably not what you want');
        return (True, self.oGuestSession);

    def setSession(self, oGuestSession):
        """
        Sets the current guest session and closes
        an old one if necessary.
        """
        if self.oGuestSession is not None:
            self.closeSession();
        self.oGuestSession = oGuestSession;
        return self.oGuestSession;

    def closeSession(self, fIsError = True):
        """
        Closes the guest session.
        """
        if self.oGuestSession is not None:
            try:
                sName = self.oGuestSession.name;
            except:
                return reporter.errorXcpt();

            reporter.log('Closing session "%s" ...' % (sName,));
            try:
                self.oGuestSession.close();
                self.oGuestSession = None;
            except:
                # Just log, don't assume an error here (will be done in the main loop then).
                reporter.maybeErrXcpt(fIsError, 'Closing guest session "%s" failed:' % (sName,));
                return False;
        return True;

class tdTestCopyFrom(tdTestGuestCtrlBase):
    """
    Test for copying files from the guest to the host.
    """
    def __init__(self, sSrc = "", sDst = "", oCreds = None, afFlags = None, oSrc = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sSrc = sSrc;
        self.sDst = sDst;
        self.afFlags = afFlags;
        self.oSrc = oSrc  # type: testfileset.TestFsObj
        if oSrc and not sSrc:
            self.sSrc = oSrc.sPath;

class tdTestCopyFromDir(tdTestCopyFrom):

    def __init__(self, sSrc = "", sDst = "", oCreds = None, afFlags = None, oSrc = None, fIntoDst = False):
        tdTestCopyFrom.__init__(self, sSrc, sDst, oCreds, afFlags, oSrc);
        self.fIntoDst = fIntoDst; # hint to the verification code that sDst == oSrc, rather than sDst+oSrc.sNAme == oSrc.

class tdTestCopyFromFile(tdTestCopyFrom):
    pass;

class tdTestRemoveHostDir(object):
    """
    Test step that removes a host directory tree.
    """
    def __init__(self, sDir):
        self.sDir = sDir;

    def execute(self, oTstDrv, oVmSession, oTxsSession, oTestVm, sMsgPrefix):
        _ = oTstDrv; _ = oVmSession; _ = oTxsSession; _ = oTestVm; _ = sMsgPrefix;
        if os.path.exists(self.sDir):
            if base.wipeDirectory(self.sDir) != 0:
                return False;
            try:
                os.rmdir(self.sDir);
            except:
                return reporter.errorXcpt('%s: sDir=%s' % (sMsgPrefix, self.sDir,));
        return True;



class tdTestCopyTo(tdTestGuestCtrlBase):
    """
    Test for copying files from the host to the guest.
    """
    def __init__(self, sSrc = "", sDst = "", oCreds = None, afFlags = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sSrc = sSrc;
        self.sDst = sDst;
        self.afFlags = afFlags;

class tdTestCopyToFile(tdTestCopyTo):
    pass;

class tdTestCopyToDir(tdTestCopyTo):
    pass;

class tdTestDirCreate(tdTestGuestCtrlBase):
    """
    Test for directoryCreate call.
    """
    def __init__(self, sDirectory = "", oCreds = None, fMode = 0, afFlags = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sDirectory = sDirectory;
        self.fMode = fMode;
        self.afFlags = afFlags;

class tdTestDirCreateTemp(tdTestGuestCtrlBase):
    """
    Test for the directoryCreateTemp call.
    """
    def __init__(self, sDirectory = "", sTemplate = "", oCreds = None, fMode = 0, fSecure = False):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sDirectory = sDirectory;
        self.sTemplate = sTemplate;
        self.fMode = fMode;
        self.fSecure = fSecure;

class tdTestDirOpen(tdTestGuestCtrlBase):
    """
    Test for the directoryOpen call.
    """
    def __init__(self, sDirectory = "", oCreds = None, sFilter = "", afFlags = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sDirectory = sDirectory;
        self.sFilter = sFilter;
        self.afFlags = afFlags or [];

class tdTestDirRead(tdTestDirOpen):
    """
    Test for the opening, reading and closing a certain directory.
    """
    def __init__(self, sDirectory = "", oCreds = None, sFilter = "", afFlags = None):
        tdTestDirOpen.__init__(self, sDirectory, oCreds, sFilter, afFlags);

class tdTestExec(tdTestGuestCtrlBase):
    """
    Specifies exactly one guest control execution test.
    Has a default timeout of 5 minutes (for safety).
    """
    def __init__(self, sCmd = "", sCwd = "", asArgs = None, aEnv = None, afFlags = None,             # pylint: disable=too-many-arguments
                 timeoutMS = 5 * 60 * 1000, oCreds = None, fWaitForExit = True):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sCmd = sCmd;
        self.sCwd = sCwd;
        self.asArgs = asArgs if asArgs is not None else [sCmd,];
        self.aEnv = aEnv;
        self.afFlags = afFlags or [];
        self.timeoutMS = timeoutMS;
        self.fWaitForExit = fWaitForExit;
        self.uExitStatus = 0;
        self.iExitCode = 0;
        self.cbStdOut = 0;
        self.cbStdErr = 0;
        self.sBuf = '';

class tdTestFileExists(tdTestGuestCtrlBase):
    """
    Test for the file exists API call (fileExists).
    """
    def __init__(self, sFile = "", oCreds = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sFile = sFile;

class tdTestFileRemove(tdTestGuestCtrlBase):
    """
    Test querying guest file information.
    """
    def __init__(self, sFile = "", oCreds = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sFile = sFile;

class tdTestRemoveBase(tdTestGuestCtrlBase):
    """
    Removal base.
    """
    def __init__(self, sPath, fRcExpect = True, oCreds = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sPath     = sPath;
        self.fRcExpect = fRcExpect;

    def execute(self, oSubTstDrv):
        """
        Executes the test, returns True/False.
        """
        _ = oSubTstDrv;
        return True;

    def checkRemoved(self, sType):
        """ Check that the object was removed using fObjExists. """
        try:
            fExists = self.oGuestSession.fsObjExists(self.sPath, False);
        except:
            return reporter.errorXcpt('fsObjExists failed on "%s" after deletion (type: %s)' % (self.sPath, sType));
        if fExists:
            return reporter.error('fsObjExists says "%s" still exists after deletion (type: %s)!' % (self.sPath, sType));
        return True;

class tdTestRemoveFile(tdTestRemoveBase):
    """
    Remove a single file.
    """
    def __init__(self, sPath, fRcExpect = True, oCreds = None):
        tdTestRemoveBase.__init__(self, sPath, fRcExpect, oCreds);

    def execute(self, oSubTstDrv):
        reporter.log2('Deleting file "%s" ...' % (limitString(self.sPath),));
        try:
            if oSubTstDrv.oTstDrv.fpApiVer >= 5.0:
                self.oGuestSession.fsObjRemove(self.sPath);
            else:
                self.oGuestSession.fileRemove(self.sPath);
        except:
            reporter.maybeErrXcpt(self.fRcExpect, 'Removing "%s" failed' % (self.sPath,));
            return not self.fRcExpect;
        if not self.fRcExpect:
            return reporter.error('Expected removing "%s" to failed, but it succeeded' % (self.sPath,));

        return self.checkRemoved('file');

class tdTestRemoveDir(tdTestRemoveBase):
    """
    Remove a single directory if empty.
    """
    def __init__(self, sPath, fRcExpect = True, oCreds = None):
        tdTestRemoveBase.__init__(self, sPath, fRcExpect, oCreds);

    def execute(self, oSubTstDrv):
        _ = oSubTstDrv;
        reporter.log2('Deleting directory "%s" ...' % (limitString(self.sPath),));
        try:
            self.oGuestSession.directoryRemove(self.sPath);
        except:
            reporter.maybeErrXcpt(self.fRcExpect, 'Removing "%s" (as a directory) failed' % (self.sPath,));
            return not self.fRcExpect;
        if not self.fRcExpect:
            return reporter.error('Expected removing "%s" (dir) to failed, but it succeeded' % (self.sPath,));

        return self.checkRemoved('directory');

class tdTestRemoveTree(tdTestRemoveBase):
    """
    Recursively remove a directory tree.
    """
    def __init__(self, sPath, afFlags = None, fRcExpect = True, fNotExist = False, oCreds = None):
        tdTestRemoveBase.__init__(self, sPath, fRcExpect, oCreds = None);
        self.afFlags = afFlags if afFlags is not None else [];
        self.fNotExist = fNotExist; # Hack for the ContentOnly scenario where the dir does not exist.

    def execute(self, oSubTstDrv):
        reporter.log2('Deleting tree "%s" ...' % (limitString(self.sPath),));
        try:
            oProgress = self.oGuestSession.directoryRemoveRecursive(self.sPath, self.afFlags);
        except:
            reporter.maybeErrXcpt(self.fRcExpect, 'Removing directory tree "%s" failed (afFlags=%s)'
                                  % (self.sPath, self.afFlags));
            return not self.fRcExpect;

        oWrappedProgress = vboxwrappers.ProgressWrapper(oProgress, oSubTstDrv.oTstDrv.oVBoxMgr, oSubTstDrv.oTstDrv,
                                                        "remove-tree: %s" % (self.sPath,));
        oWrappedProgress.wait();
        if not oWrappedProgress.isSuccess():
            oWrappedProgress.logResult(fIgnoreErrors = not self.fRcExpect);
            return not self.fRcExpect;
        if not self.fRcExpect:
            return reporter.error('Expected removing "%s" (tree) to failed, but it succeeded' % (self.sPath,));

        if vboxcon.DirectoryRemoveRecFlag_ContentAndDir not in self.afFlags  and  not self.fNotExist:
            # Cannot use directoryExists here as it is buggy.
            try:
                if oSubTstDrv.oTstDrv.fpApiVer >= 5.0:
                    oFsObjInfo = self.oGuestSession.fsObjQueryInfo(self.sPath, False);
                else:
                    oFsObjInfo = self.oGuestSession.fileQueryInfo(self.sPath);
                eType = oFsObjInfo.type;
            except:
                return reporter.errorXcpt('sPath=%s' % (self.sPath,));
            if eType != vboxcon.FsObjType_Directory:
                return reporter.error('Found file type %d, expected directory (%d) for %s after rmtree/OnlyContent'
                                      % (eType, vboxcon.FsObjType_Directory, self.sPath,));
            return True;

        return self.checkRemoved('tree');


class tdTestFileStat(tdTestGuestCtrlBase):
    """
    Test querying guest file information.
    """
    def __init__(self, sFile = "", oCreds = None, cbSize = 0, eFileType = 0):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sFile = sFile;
        self.cbSize = cbSize;
        self.eFileType = eFileType;

class tdTestFileIO(tdTestGuestCtrlBase):
    """
    Test for the IGuestFile object.
    """
    def __init__(self, sFile = "", oCreds = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sFile = sFile;

class tdTestFileQuerySize(tdTestGuestCtrlBase):
    """
    Test for the file size query API call (fileQuerySize).
    """
    def __init__(self, sFile = "", oCreds = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sFile = sFile;

class tdTestFileOpen(tdTestGuestCtrlBase):
    """
    Tests opening a guest files.
    """
    def __init__(self, sFile = "", eAccessMode = None, eAction = None, eSharing = None,
                 fCreationMode = 0o660, oCreds = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sFile          = sFile;
        self.eAccessMode    = eAccessMode if eAccessMode is not None else vboxcon.FileAccessMode_ReadOnly;
        self.eAction        = eAction if eAction is not None else vboxcon.FileOpenAction_OpenExisting;
        self.eSharing       = eSharing if eSharing is not None else vboxcon.FileSharingMode_All;
        self.fCreationMode  = fCreationMode;
        self.afOpenFlags    = [];
        self.oOpenedFile    = None;

    def toString(self):
        """ Get a summary string. """
        return 'eAccessMode=%s eAction=%s sFile=%s' % (self.eAccessMode, self.eAction, self.sFile);

    def doOpenStep(self, fExpectSuccess):
        """
        Does the open step, putting the resulting file in oOpenedFile.
        """
        try:
            self.oOpenedFile = self.oGuestSession.fileOpenEx(self.sFile, self.eAccessMode, self.eAction,
                                                             self.eSharing, self.fCreationMode, self.afOpenFlags);
        except:
            reporter.maybeErrXcpt(fExpectSuccess, 'fileOpenEx(%s, %s, %s, %s, %s, %s)'
                                  % (self.sFile, self.eAccessMode, self.eAction, self.eSharing,
                                     self.fCreationMode, self.afOpenFlags,));
            return False;
        return True;

    def doStepsOnOpenedFile(self, fExpectSuccess, oSubTst):
        """ Overridden by children to do more testing. """
        _ = fExpectSuccess; _ = oSubTst;
        return True;

    def doCloseStep(self):
        """ Closes the file. """
        if self.oOpenedFile:
            try:
                self.oOpenedFile.close();
            except:
                return reporter.errorXcpt('close([%s, %s, %s, %s, %s, %s])'
                                          % (self.sFile, self.eAccessMode, self.eAction, self.eSharing,
                                             self.fCreationMode, self.afOpenFlags,));
            self.oOpenedFile = None;
        return True;

    def doSteps(self, fExpectSuccess, oSubTst):
        """ Do the tests. """
        fRc = self.doOpenStep(fExpectSuccess);
        if fRc is True:
            fRc = self.doStepsOnOpenedFile(fExpectSuccess, oSubTst);
        if self.oOpenedFile:
            fRc = self.doCloseStep() and fRc;
        return fRc;


class tdTestFileOpenCheckSize(tdTestFileOpen):
    """
    Opens a file and checks the size.
    """
    def __init__(self, sFile = "", eAccessMode = None, eAction = None, eSharing = None,
                 fCreationMode = 0o660, cbOpenExpected = 0, oCreds = None):
        tdTestFileOpen.__init__(self, sFile, eAccessMode, eAction, eSharing, fCreationMode, oCreds);
        self.cbOpenExpected = cbOpenExpected;

    def toString(self):
        return 'cbOpenExpected=%s %s' % (self.cbOpenExpected, tdTestFileOpen.toString(self),);

    def doStepsOnOpenedFile(self, fExpectSuccess, oSubTst):
        #
        # Call parent.
        #
        fRc = tdTestFileOpen.doStepsOnOpenedFile(self, fExpectSuccess, oSubTst);

        #
        # Check the size.  Requires 6.0 or later (E_NOTIMPL in 5.2).
        #
        if oSubTst.oTstDrv.fpApiVer >= 6.0:
            try:
                oFsObjInfo = self.oOpenedFile.queryInfo();
            except:
                return reporter.errorXcpt('queryInfo([%s, %s, %s, %s, %s, %s])'
                                          % (self.sFile, self.eAccessMode, self.eAction, self.eSharing,
                                             self.fCreationMode, self.afOpenFlags,));
            if oFsObjInfo is None:
                return reporter.error('IGuestFile::queryInfo returned None');
            try:
                cbFile = oFsObjInfo.objectSize;
            except:
                return reporter.errorXcpt();
            if cbFile != self.cbOpenExpected:
                return reporter.error('Wrong file size after open (%d): %s, expected %s (file %s) (#1)'
                                      % (self.eAction, cbFile, self.cbOpenExpected, self.sFile));

            try:
                cbFile = self.oOpenedFile.querySize();
            except:
                return reporter.errorXcpt('querySize([%s, %s, %s, %s, %s, %s])'
                                          % (self.sFile, self.eAccessMode, self.eAction, self.eSharing,
                                             self.fCreationMode, self.afOpenFlags,));
            if cbFile != self.cbOpenExpected:
                return reporter.error('Wrong file size after open (%d): %s, expected %s (file %s) (#2)'
                                      % (self.eAction, cbFile, self.cbOpenExpected, self.sFile));

        return fRc;


class tdTestFileOpenAndWrite(tdTestFileOpen):
    """
    Opens the file and writes one or more chunks to it.

    The chunks are a list of tuples(offset, bytes), where offset can be None
    if no seeking should be performed.
    """
    def __init__(self, sFile = "", eAccessMode = None, eAction = None, eSharing = None, # pylint: disable=too-many-arguments
                 fCreationMode = 0o660, atChunks = None, fUseAtApi = False, abContent = None, oCreds = None):
        tdTestFileOpen.__init__(self, sFile, eAccessMode if eAccessMode is not None else vboxcon.FileAccessMode_WriteOnly,
                                eAction, eSharing, fCreationMode, oCreds);
        assert atChunks is not None;
        self.atChunks  = atChunks   # type: list(tuple(int,bytearray))
        self.fUseAtApi = fUseAtApi;
        self.fAppend   = (   eAccessMode in (vboxcon.FileAccessMode_AppendOnly, vboxcon.FileAccessMode_AppendRead)
                          or eAction == vboxcon.FileOpenAction_AppendOrCreate);
        self.abContent = abContent  # type: bytearray

    def toString(self):
        sChunks = ', '.join('%s LB %s' % (tChunk[0], len(tChunk[1]),) for tChunk in self.atChunks);
        sApi = 'writeAt' if self.fUseAtApi else 'write';
        return '%s [%s] %s' % (sApi, sChunks, tdTestFileOpen.toString(self),);

    def doStepsOnOpenedFile(self, fExpectSuccess, oSubTst):
        #
        # Call parent.
        #
        fRc = tdTestFileOpen.doStepsOnOpenedFile(self, fExpectSuccess, oSubTst);

        #
        # Do the writing.
        #
        for offFile, abBuf in self.atChunks:
            if self.fUseAtApi:
                #
                # writeAt:
                #
                assert offFile is not None;
                reporter.log2('writeAt(%s, %s bytes)' % (offFile, len(abBuf),));
                if self.fAppend:
                    if self.abContent is not None: # Try avoid seek as it updates the cached offset in GuestFileImpl.
                        offExpectAfter = len(self.abContent);
                    else:
                        try:
                            offSave        = self.oOpenedFile.seek(0, vboxcon.FileSeekOrigin_Current);
                            offExpectAfter = self.oOpenedFile.seek(0, vboxcon.FileSeekOrigin_End);
                            self.oOpenedFile.seek(offSave, vboxcon.FileSeekOrigin_Begin);
                        except:
                            return reporter.errorXcpt();
                    offExpectAfter += len(abBuf);
                else:
                    offExpectAfter = offFile + len(abBuf);

                try:
                    cbWritten = self.oOpenedFile.writeAt(offFile, abBuf, 30*1000);
                except:
                    return reporter.errorXcpt('writeAt(%s, %s bytes)' % (offFile, len(abBuf),));

            else:
                #
                # write:
                #
                if self.fAppend:
                    if self.abContent is not None: # Try avoid seek as it updates the cached offset in GuestFileImpl.
                        offExpectAfter = len(self.abContent);
                    else:
                        try:
                            offSave        = self.oOpenedFile.seek(0, vboxcon.FileSeekOrigin_Current);
                            offExpectAfter = self.oOpenedFile.seek(0, vboxcon.FileSeekOrigin_End);
                            self.oOpenedFile.seek(offSave, vboxcon.FileSeekOrigin_Begin);
                        except:
                            return reporter.errorXcpt('seek(0,End)');
                if offFile is not None:
                    try:
                        self.oOpenedFile.seek(offFile, vboxcon.FileSeekOrigin_Begin);
                    except:
                        return reporter.errorXcpt('seek(%s,Begin)' % (offFile,));
                else:
                    try:
                        offFile = self.oOpenedFile.seek(0, vboxcon.FileSeekOrigin_Current);
                    except:
                        return reporter.errorXcpt();
                if not self.fAppend:
                    offExpectAfter = offFile;
                offExpectAfter += len(abBuf);

                reporter.log2('write(%s bytes @ %s)' % (len(abBuf), offFile,));
                try:
                    cbWritten = self.oOpenedFile.write(abBuf, 30*1000);
                except:
                    return reporter.errorXcpt('write(%s bytes @ %s)' % (len(abBuf), offFile));

            #
            # Check how much was written, ASSUMING nothing we push thru here is too big:
            #
            if cbWritten != len(abBuf):
                fRc = reporter.errorXcpt('Wrote less than expected: %s out of %s, expected all to be written'
                                         % (cbWritten, len(abBuf),));
                if not self.fAppend:
                    offExpectAfter -= len(abBuf) - cbWritten;

            #
            # Update the file content tracker if we've got one and can:
            #
            if self.abContent is not None:
                if cbWritten < len(abBuf):
                    abBuf = abBuf[:cbWritten];

                #
                # In append mode, the current file offset shall be disregarded and the
                # write always goes to the end of the file, regardless of writeAt or write.
                # Note that RTFileWriteAt only naturally behaves this way on linux and
                # (probably) windows, so VBoxService makes that behaviour generic across
                # all OSes.
                #
                if self.fAppend:
                    reporter.log2('len(self.abContent)=%s + %s' % (len(self.abContent), cbWritten, ));
                    self.abContent.extend(abBuf);
                else:
                    if offFile is None:
                        offFile = offExpectAfter - cbWritten;
                    reporter.log2('len(self.abContent)=%s + %s @ %s' % (len(self.abContent), cbWritten, offFile, ));
                    if offFile > len(self.abContent):
                        self.abContent.extend(bytearray(offFile - len(self.abContent)));
                    self.abContent[offFile:offFile + cbWritten] = abBuf;
                reporter.log2('len(self.abContent)=%s' % (len(self.abContent),));

            #
            # Check the resulting file offset with IGuestFile::offset.
            #
            try:
                offApi  = self.oOpenedFile.offset; # Must be gotten first!
                offSeek = self.oOpenedFile.seek(0, vboxcon.FileSeekOrigin_Current);
            except:
                fRc = reporter.errorXcpt();
            else:
                reporter.log2('offApi=%s offSeek=%s offExpectAfter=%s' % (offApi, offSeek, offExpectAfter,));
                if offSeek != offExpectAfter:
                    fRc = reporter.error('Seek offset is %s, expected %s after %s bytes write @ %s (offApi=%s)'
                                         % (offSeek, offExpectAfter, len(abBuf), offFile, offApi,));
                if offApi != offExpectAfter:
                    fRc = reporter.error('IGuestFile::offset is %s, expected %s after %s bytes write @ %s (offSeek=%s)'
                                         % (offApi, offExpectAfter, len(abBuf), offFile, offSeek,));
        # for each chunk - end
        return fRc;


class tdTestFileOpenAndCheckContent(tdTestFileOpen):
    """
    Opens the file and checks the content using the read API.
    """
    def __init__(self, sFile = "", eSharing = None, abContent = None, cbContentExpected = None, oCreds = None):
        tdTestFileOpen.__init__(self, sFile = sFile, eSharing = eSharing, oCreds = oCreds);
        self.abContent = abContent  # type: bytearray
        self.cbContentExpected = cbContentExpected;

    def toString(self):
        return 'check content %s (%s) %s' % (len(self.abContent), self.cbContentExpected, tdTestFileOpen.toString(self),);

    def doStepsOnOpenedFile(self, fExpectSuccess, oSubTst):
        #
        # Call parent.
        #
        fRc = tdTestFileOpen.doStepsOnOpenedFile(self, fExpectSuccess, oSubTst);

        #
        # Check the expected content size.
        #
        if self.cbContentExpected is not None:
            if len(self.abContent) != self.cbContentExpected:
                fRc = reporter.error('Incorrect abContent size: %s, expected %s'
                                     % (len(self.abContent), self.cbContentExpected,));

        #
        # Read the file and compare it with the content.
        #
        offFile = 0;
        while True:
            try:
                abChunk = self.oOpenedFile.read(512*1024, 30*1000);
            except:
                return reporter.errorXcpt('read(512KB) @ %s' % (offFile,));
            cbChunk = len(abChunk);
            if cbChunk == 0:
                if offFile != len(self.abContent):
                    fRc = reporter.error('Unexpected EOF @ %s, len(abContent)=%s' % (offFile, len(self.abContent),));
                break;
            if offFile + cbChunk > len(self.abContent):
                fRc = reporter.error('File is larger than expected: at least %s bytes, expected %s bytes'
                                     % (offFile + cbChunk, len(self.abContent),));
            elif not utils.areBytesEqual(abChunk, self.abContent[offFile:(offFile + cbChunk)]):
                fRc = reporter.error('Mismatch in range %s LB %s!' % (offFile, cbChunk,));
            offFile += cbChunk;

        return fRc;


class tdTestSession(tdTestGuestCtrlBase):
    """
    Test the guest session handling.
    """
    def __init__(self, sUser = None, sPassword = None, sDomain = None, sSessionName = ""):
        tdTestGuestCtrlBase.__init__(self, oCreds = tdCtxCreds(sUser, sPassword, sDomain));
        self.sSessionName = sSessionName;

    def getSessionCount(self, oVBoxMgr):
        """
        Helper for returning the number of currently
        opened guest sessions of a VM.
        """
        if self.oGuest is None:
            return 0;
        try:
            aoSession = oVBoxMgr.getArray(self.oGuest, 'sessions')
        except:
            reporter.errorXcpt('sSessionName: %s' % (self.sSessionName,));
            return 0;
        return len(aoSession);


class tdTestSessionEx(tdTestGuestCtrlBase):
    """
    Test the guest session.
    """
    def __init__(self, aoSteps = None, enmUser = None):
        tdTestGuestCtrlBase.__init__(self);
        assert enmUser is None; # For later.
        self.enmUser = enmUser;
        self.aoSteps = aoSteps if aoSteps is not None else [];

    def execute(self, oTstDrv, oVmSession, oTxsSession, oTestVm, sMsgPrefix):
        """
        Executes the test.
        """
        #
        # Create a session.
        #
        assert self.enmUser is None; # For later.
        self.oCreds = tdCtxCreds();
        fRc = self.setEnvironment(oVmSession, oTxsSession, oTestVm);
        if not fRc:
            return False;
        reporter.log2('%s: %s steps' % (sMsgPrefix, len(self.aoSteps),));
        fRc, oCurSession = self.createSession(sMsgPrefix);
        if fRc is True:
            #
            # Execute the tests.
            #
            try:
                fRc = self.executeSteps(oTstDrv, oCurSession, sMsgPrefix);
            except:
                fRc = reporter.errorXcpt('%s: Unexpected exception executing test steps' % (sMsgPrefix,));

            #
            # Close the session.
            #
            fRc2 = self.closeSession();
            if fRc2 is False:
                fRc = reporter.error('%s: Session could not be closed' % (sMsgPrefix,));
        else:
            fRc = reporter.error('%s: Session creation failed' % (sMsgPrefix,));
        return fRc;

    def executeSteps(self, oTstDrv, oGstCtrlSession, sMsgPrefix):
        """
        Executes just the steps.
        Returns True on success, False on test failure.
        """
        fRc = True;
        for (i, oStep) in enumerate(self.aoSteps):
            fRc2 = oStep.execute(oTstDrv, oGstCtrlSession, sMsgPrefix + ', step #%d' % i);
            if fRc2 is True:
                pass;
            elif fRc2 is None:
                reporter.log('%s: skipping remaining %d steps' % (sMsgPrefix, len(self.aoSteps) - i - 1,));
                break;
            else:
                fRc = False;
        return fRc;

    @staticmethod
    def executeListTestSessions(aoTests, oTstDrv, oVmSession, oTxsSession, oTestVm, sMsgPrefix):
        """
        Works thru a list of tdTestSessionEx object.
        """
        fRc = True;
        for (i, oCurTest) in enumerate(aoTests):
            try:
                fRc2 = oCurTest.execute(oTstDrv, oVmSession, oTxsSession, oTestVm, '%s / %#d' % (sMsgPrefix, i,));
                if fRc2 is not True:
                    fRc = False;
            except:
                fRc = reporter.errorXcpt('%s: Unexpected exception executing test #%d' % (sMsgPrefix, i ,));

        return (fRc, oTxsSession);


class tdSessionStepBase(object):
    """
    Base class for the guest control session test steps.
    """

    def execute(self, oTstDrv, oGstCtrlSession, sMsgPrefix):
        """
        Executes the test step.

        Returns True on success.
        Returns False on failure (must be reported as error).
        Returns None if to skip the remaining steps.
        """
        _ = oTstDrv;
        _ = oGstCtrlSession;
        return reporter.error('%s: Missing execute implementation: %s' % (sMsgPrefix, self,));


class tdStepRequireMinimumApiVer(tdSessionStepBase):
    """
    Special test step which will cause executeSteps to skip the remaining step
    if the VBox API is too old:
    """
    def __init__(self, fpMinApiVer):
        self.fpMinApiVer = fpMinApiVer;

    def execute(self, oTstDrv, oGstCtrlSession, sMsgPrefix):
        """ Returns None if API version is too old, otherwise True. """
        if oTstDrv.fpApiVer >= self.fpMinApiVer:
            return True;
        _ = oGstCtrlSession;
        _ = sMsgPrefix;
        return None; # Special return value. Don't use elsewhere.


#
# Scheduling Environment Changes with the Guest Control Session.
#

class tdStepSessionSetEnv(tdSessionStepBase):
    """
    Guest session environment: schedule putenv
    """
    def __init__(self, sVar, sValue, hrcExpected = 0):
        self.sVar        = sVar;
        self.sValue      = sValue;
        self.hrcExpected = hrcExpected;

    def execute(self, oTstDrv, oGstCtrlSession, sMsgPrefix):
        """
        Executes the step.
        Returns True on success, False on test failure.
        """
        reporter.log2('tdStepSessionSetEnv: sVar=%s sValue=%s hrcExpected=%#x' % (self.sVar, self.sValue, self.hrcExpected,));
        try:
            if oTstDrv.fpApiVer >= 5.0:
                oGstCtrlSession.environmentScheduleSet(self.sVar, self.sValue);
            else:
                oGstCtrlSession.environmentSet(self.sVar, self.sValue);
        except vbox.ComException as oXcpt:
            # Is this an expected failure?
            if vbox.ComError.equal(oXcpt, self.hrcExpected):
                return True;
            return reporter.errorXcpt('%s: Expected hrc=%#x (%s) got %#x (%s) instead (setenv %s=%s)'
                                      % (sMsgPrefix, self.hrcExpected, vbox.ComError.toString(self.hrcExpected),
                                         vbox.ComError.getXcptResult(oXcpt),
                                         vbox.ComError.toString(vbox.ComError.getXcptResult(oXcpt)),
                                         self.sVar, self.sValue,));
        except:
            return reporter.errorXcpt('%s: Unexpected exception in tdStepSessionSetEnv::execute (%s=%s)'
                                      % (sMsgPrefix, self.sVar, self.sValue,));

        # Should we succeed?
        if self.hrcExpected != 0:
            return reporter.error('%s: Expected hrcExpected=%#x, got S_OK (putenv %s=%s)'
                                  % (sMsgPrefix, self.hrcExpected, self.sVar, self.sValue,));
        return True;

class tdStepSessionUnsetEnv(tdSessionStepBase):
    """
    Guest session environment: schedule unset.
    """
    def __init__(self, sVar, hrcExpected = 0):
        self.sVar        = sVar;
        self.hrcExpected = hrcExpected;

    def execute(self, oTstDrv, oGstCtrlSession, sMsgPrefix):
        """
        Executes the step.
        Returns True on success, False on test failure.
        """
        reporter.log2('tdStepSessionUnsetEnv: sVar=%s hrcExpected=%#x' % (self.sVar, self.hrcExpected,));
        try:
            if oTstDrv.fpApiVer >= 5.0:
                oGstCtrlSession.environmentScheduleUnset(self.sVar);
            else:
                oGstCtrlSession.environmentUnset(self.sVar);
        except vbox.ComException as oXcpt:
            # Is this an expected failure?
            if vbox.ComError.equal(oXcpt, self.hrcExpected):
                return True;
            return reporter.errorXcpt('%s: Expected hrc=%#x (%s) got %#x (%s) instead (unsetenv %s)'
                                       % (sMsgPrefix, self.hrcExpected, vbox.ComError.toString(self.hrcExpected),
                                          vbox.ComError.getXcptResult(oXcpt),
                                          vbox.ComError.toString(vbox.ComError.getXcptResult(oXcpt)),
                                          self.sVar,));
        except:
            return reporter.errorXcpt('%s: Unexpected exception in tdStepSessionUnsetEnv::execute (%s)'
                                      % (sMsgPrefix, self.sVar,));

        # Should we succeed?
        if self.hrcExpected != 0:
            return reporter.error('%s: Expected hrcExpected=%#x, got S_OK (unsetenv %s)'
                                  % (sMsgPrefix, self.hrcExpected, self.sVar,));
        return True;

class tdStepSessionBulkEnv(tdSessionStepBase):
    """
    Guest session environment: Bulk environment changes.
    """
    def __init__(self, asEnv = None, hrcExpected = 0):
        self.asEnv = asEnv if asEnv is not None else [];
        self.hrcExpected = hrcExpected;

    def execute(self, oTstDrv, oGstCtrlSession, sMsgPrefix):
        """
        Executes the step.
        Returns True on success, False on test failure.
        """
        reporter.log2('tdStepSessionBulkEnv: asEnv=%s hrcExpected=%#x' % (self.asEnv, self.hrcExpected,));
        try:
            if oTstDrv.fpApiVer >= 5.0:
                oTstDrv.oVBoxMgr.setArray(oGstCtrlSession, 'environmentChanges', self.asEnv);
            else:
                oTstDrv.oVBoxMgr.setArray(oGstCtrlSession, 'environment', self.asEnv);
        except vbox.ComException as oXcpt:
            # Is this an expected failure?
            if vbox.ComError.equal(oXcpt, self.hrcExpected):
                return True;
            return reporter.errorXcpt('%s: Expected hrc=%#x (%s) got %#x (%s) instead (asEnv=%s)'
                                       % (sMsgPrefix, self.hrcExpected, vbox.ComError.toString(self.hrcExpected),
                                          vbox.ComError.getXcptResult(oXcpt),
                                          vbox.ComError.toString(vbox.ComError.getXcptResult(oXcpt)),
                                          self.asEnv,));
        except:
            return reporter.errorXcpt('%s: Unexpected exception writing the environmentChanges property (asEnv=%s).'
                                      % (sMsgPrefix, self.asEnv));
        return True;

class tdStepSessionClearEnv(tdStepSessionBulkEnv):
    """
    Guest session environment: clears the scheduled environment changes.
    """
    def __init__(self):
        tdStepSessionBulkEnv.__init__(self);


class tdStepSessionCheckEnv(tdSessionStepBase):
    """
    Check the currently scheduled environment changes of a guest control session.
    """
    def __init__(self, asEnv = None):
        self.asEnv = asEnv if asEnv is not None else [];

    def execute(self, oTstDrv, oGstCtrlSession, sMsgPrefix):
        """
        Executes the step.
        Returns True on success, False on test failure.
        """
        reporter.log2('tdStepSessionCheckEnv: asEnv=%s' % (self.asEnv,));

        #
        # Get the environment change list.
        #
        try:
            if oTstDrv.fpApiVer >= 5.0:
                asCurEnv = oTstDrv.oVBoxMgr.getArray(oGstCtrlSession, 'environmentChanges');
            else:
                asCurEnv = oTstDrv.oVBoxMgr.getArray(oGstCtrlSession, 'environment');
        except:
            return reporter.errorXcpt('%s: Unexpected exception reading the environmentChanges property.' % (sMsgPrefix,));

        #
        # Compare it with the expected one by trying to remove each expected value
        # and the list anything unexpected.
        #
        fRc = True;
        asCopy = list(asCurEnv); # just in case asCurEnv is immutable
        for sExpected in self.asEnv:
            try:
                asCopy.remove(sExpected);
            except:
                fRc = reporter.error('%s: Expected "%s" to be in the resulting environment' % (sMsgPrefix, sExpected,));
        for sUnexpected in asCopy:
            fRc = reporter.error('%s: Unexpected "%s" in the resulting environment' % (sMsgPrefix, sUnexpected,));

        if fRc is not True:
            reporter.log2('%s: Current environment: %s' % (sMsgPrefix, asCurEnv));
        return fRc;


#
# File system object statistics (i.e. stat()).
#

class tdStepStat(tdSessionStepBase):
    """
    Stats a file system object.
    """
    def __init__(self, sPath, hrcExpected = 0, fFound = True, fFollowLinks = True, enmType = None, oTestFsObj = None):
        self.sPath        = sPath;
        self.hrcExpected  = hrcExpected;
        self.fFound       = fFound;
        self.fFollowLinks = fFollowLinks;
        self.enmType      = enmType if enmType is not None else vboxcon.FsObjType_File;
        self.cbExactSize  = None;
        self.cbMinSize    = None;
        self.oTestFsObj   = oTestFsObj  # type: testfileset.TestFsObj

    def execute(self, oTstDrv, oGstCtrlSession, sMsgPrefix):
        """
        Execute the test step.
        """
        reporter.log2('tdStepStat: sPath=%s enmType=%s hrcExpected=%s fFound=%s fFollowLinks=%s'
                      % (limitString(self.sPath), self.enmType, self.hrcExpected, self.fFound, self.fFollowLinks,));

        # Don't execute non-file tests on older VBox version.
        if oTstDrv.fpApiVer >= 5.0 or self.enmType == vboxcon.FsObjType_File or not self.fFound:
            #
            # Call the API.
            #
            try:
                if oTstDrv.fpApiVer >= 5.0:
                    oFsInfo = oGstCtrlSession.fsObjQueryInfo(self.sPath, self.fFollowLinks);
                else:
                    oFsInfo = oGstCtrlSession.fileQueryInfo(self.sPath);
            except vbox.ComException as oXcpt:
                ## @todo: The error reporting in the API just plain sucks! Most of the errors are
                ##        VBOX_E_IPRT_ERROR and there seems to be no way to distinguish between
                ##        non-existing files/path and a lot of other errors.  Fix API and test!
                if not self.fFound:
                    return True;
                if vbox.ComError.equal(oXcpt, self.hrcExpected): # Is this an expected failure?
                    return True;
                return reporter.errorXcpt('%s: Unexpected exception for exiting path "%s" (enmType=%s, hrcExpected=%s):'
                                          % (sMsgPrefix, self.sPath, self.enmType, self.hrcExpected,));
            except:
                return reporter.errorXcpt('%s: Unexpected exception in tdStepStat::execute (%s)'
                                          % (sMsgPrefix, self.sPath,));
            if oFsInfo is None:
                return reporter.error('%s: "%s" got None instead of IFsObjInfo instance!' % (sMsgPrefix, self.sPath,));

            #
            # Check type expectations.
            #
            try:
                enmType = oFsInfo.type;
            except:
                return reporter.errorXcpt('%s: Unexpected exception in reading "IFsObjInfo::type"' % (sMsgPrefix,));
            if enmType != self.enmType:
                return reporter.error('%s: "%s" has type %s, expected %s'
                                      % (sMsgPrefix, self.sPath, enmType, self.enmType));

            #
            # Check size expectations.
            # Note! This is unicode string here on windows, for some reason.
            #       long long mapping perhaps?
            #
            try:
                cbObject = long(oFsInfo.objectSize);
            except:
                return reporter.errorXcpt('%s: Unexpected exception in reading "IFsObjInfo::objectSize"'
                                          % (sMsgPrefix,));
            if    self.cbExactSize is not None \
              and cbObject != self.cbExactSize:
                return reporter.error('%s: "%s" has size %s bytes, expected %s bytes'
                                      % (sMsgPrefix, self.sPath, cbObject, self.cbExactSize));
            if    self.cbMinSize is not None \
              and cbObject < self.cbMinSize:
                return reporter.error('%s: "%s" has size %s bytes, expected as least %s bytes'
                                      % (sMsgPrefix, self.sPath, cbObject, self.cbMinSize));
        return True;

class tdStepStatDir(tdStepStat):
    """ Checks for an existing directory. """
    def __init__(self, sDirPath, oTestDir = None):
        tdStepStat.__init__(self, sPath = sDirPath, enmType = vboxcon.FsObjType_Directory, oTestFsObj = oTestDir);

class tdStepStatDirEx(tdStepStatDir):
    """ Checks for an existing directory given a TestDir object. """
    def __init__(self, oTestDir): # type: (testfileset.TestDir)
        tdStepStatDir.__init__(self, oTestDir.sPath, oTestDir);

class tdStepStatFile(tdStepStat):
    """ Checks for an existing file  """
    def __init__(self, sFilePath = None, oTestFile = None):
        tdStepStat.__init__(self, sPath = sFilePath, enmType = vboxcon.FsObjType_File, oTestFsObj = oTestFile);

class tdStepStatFileEx(tdStepStatFile):
    """ Checks for an existing file given a TestFile object. """
    def __init__(self, oTestFile): # type: (testfileset.TestFile)
        tdStepStatFile.__init__(self, oTestFile.sPath, oTestFile);

class tdStepStatFileSize(tdStepStat):
    """ Checks for an existing file of a given expected size.. """
    def __init__(self, sFilePath, cbExactSize = 0):
        tdStepStat.__init__(self, sPath = sFilePath, enmType = vboxcon.FsObjType_File);
        self.cbExactSize = cbExactSize;

class tdStepStatFileNotFound(tdStepStat):
    """ Checks for an existing directory. """
    def __init__(self, sPath):
        tdStepStat.__init__(self, sPath = sPath, fFound = False);

class tdStepStatPathNotFound(tdStepStat):
    """ Checks for an existing directory. """
    def __init__(self, sPath):
        tdStepStat.__init__(self, sPath = sPath, fFound = False);


#
#
#

class tdTestSessionFileRefs(tdTestGuestCtrlBase):
    """
    Tests session file (IGuestFile) reference counting.
    """
    def __init__(self, cRefs = 0):
        tdTestGuestCtrlBase.__init__(self);
        self.cRefs = cRefs;

class tdTestSessionDirRefs(tdTestGuestCtrlBase):
    """
    Tests session directory (IGuestDirectory) reference counting.
    """
    def __init__(self, cRefs = 0):
        tdTestGuestCtrlBase.__init__(self);
        self.cRefs = cRefs;

class tdTestSessionProcRefs(tdTestGuestCtrlBase):
    """
    Tests session process (IGuestProcess) reference counting.
    """
    def __init__(self, cRefs = 0):
        tdTestGuestCtrlBase.__init__(self);
        self.cRefs = cRefs;

class tdTestUpdateAdditions(tdTestGuestCtrlBase):
    """
    Test updating the Guest Additions inside the guest.
    """
    def __init__(self, sSrc = "", asArgs = None, afFlags = None, oCreds = None):
        tdTestGuestCtrlBase.__init__(self, oCreds = oCreds);
        self.sSrc = sSrc;
        self.asArgs = asArgs;
        self.afFlags = afFlags;

class tdTestResult(object):
    """
    Base class for test results.
    """
    def __init__(self, fRc = False):
        ## The overall test result.
        self.fRc = fRc;

class tdTestResultFailure(tdTestResult):
    """
    Base class for test results.
    """
    def __init__(self):
        tdTestResult.__init__(self, fRc = False);

class tdTestResultSuccess(tdTestResult):
    """
    Base class for test results.
    """
    def __init__(self):
        tdTestResult.__init__(self, fRc = True);

class tdTestResultDirRead(tdTestResult):
    """
    Test result for reading guest directories.
    """
    def __init__(self, fRc = False, cFiles = 0, cDirs = 0, cOthers = None):
        tdTestResult.__init__(self, fRc = fRc);
        self.cFiles = cFiles;
        self.cDirs = cDirs;
        self.cOthers = cOthers;

class tdTestResultExec(tdTestResult):
    """
    Holds a guest process execution test result,
    including the exit code, status + afFlags.
    """
    def __init__(self, fRc = False, uExitStatus = 500, iExitCode = 0, sBuf = None, cbBuf = 0, cbStdOut = None, cbStdErr = None):
        tdTestResult.__init__(self);
        ## The overall test result.
        self.fRc = fRc;
        ## Process exit stuff.
        self.uExitStatus = uExitStatus;
        self.iExitCode = iExitCode;
        ## Desired buffer length returned back from stdout/stderr.
        self.cbBuf = cbBuf;
        ## Desired buffer result from stdout/stderr. Use with caution!
        self.sBuf = sBuf;
        self.cbStdOut = cbStdOut;
        self.cbStdErr = cbStdErr;

class tdTestResultFileStat(tdTestResult):
    """
    Test result for stat'ing guest files.
    """
    def __init__(self, fRc = False,
                 cbSize = 0, eFileType = 0):
        tdTestResult.__init__(self, fRc = fRc);
        self.cbSize = cbSize;
        self.eFileType = eFileType;
        ## @todo Add more information.

class tdTestResultFileReadWrite(tdTestResult):
    """
    Test result for reading + writing guest directories.
    """
    def __init__(self, fRc = False,
                 cbProcessed = 0, offFile = 0, abBuf = None):
        tdTestResult.__init__(self, fRc = fRc);
        self.cbProcessed = cbProcessed;
        self.offFile = offFile;
        self.abBuf = abBuf;

class tdTestResultSession(tdTestResult):
    """
    Test result for guest session counts.
    """
    def __init__(self, fRc = False, cNumSessions = 0):
        tdTestResult.__init__(self, fRc = fRc);
        self.cNumSessions = cNumSessions;

class tdDebugSettings(object):
    """
    Contains local test debug settings.
    """
    def __init__(self, sVBoxServiceExeHst = None):
        self.sVBoxServiceExeHst = sVBoxServiceExeHst;
        self.sGstVBoxServiceLogPath = '';

class SubTstDrvAddGuestCtrl(base.SubTestDriverBase):
    """
    Sub-test driver for executing guest control (VBoxService, IGuest) tests.
    """

    def __init__(self, oTstDrv):
        base.SubTestDriverBase.__init__(self, oTstDrv, 'add-guest-ctrl', 'Guest Control');

        ## @todo base.TestBase.
        self.asTestsDef = [
            'debug',
            'session_basic', 'session_env', 'session_file_ref', 'session_dir_ref', 'session_proc_ref', 'session_reboot',
            'exec_basic', 'exec_timeout',
            'dir_create', 'dir_create_temp', 'dir_read',
            'file_open', 'file_remove', 'file_stat', 'file_read', 'file_write',
            'copy_to', 'copy_from',
            'update_additions',
            '3d'
        ];

        # Possible paths for the Guest Control Helper binary.
        self.asGstCtlHelperPaths   = [
            # Debugging stuff (SCP'd over to the guest).
            '/tmp/VBoxGuestControlHelper',
            'C:\\Temp\\VBoxGuestControlHelper',
            # Validation Kit .ISO.
            '${CDROM}/vboxvalidationkit/${OS/ARCH}/VBoxGuestControlHelper${EXESUFF}',
            '${CDROM}/${OS/ARCH}/VBoxGuestControlHelper${EXESUFF}',
            # Test VMs.
            '/opt/apps/VBoxGuestControlHelper',
            '/opt/apps/VBoxGuestControlHelper',
            '/apps/VBoxGuestControlHelper',
            'C:\\Apps\\VBoxGuestControlHelper${EXESUFF}'
        ];
        # Full path to the Guest Control Helper binary we're going to use. Only gets resolved once.
        self.sGstCtlHelperExe       = '';

        self.asTests                = self.asTestsDef;
        self.fSkipKnownBugs         = False;
        self.oTestFiles             = None # type: vboxtestfileset.TestFileSet
        self.oDebug                 = tdDebugSettings();
        self.sPathVBoxServiceExeGst = '';
        self.tpAdditionsVer         = ();

    def parseOption(self, asArgs, iArg):                                        # pylint: disable=too-many-branches,too-many-statements
        if asArgs[iArg] == '--add-guest-ctrl-tests':
            iArg += 1;
            iNext = self.oTstDrv.requireMoreArgs(1, asArgs, iArg);
            if asArgs[iArg] == 'all': # Nice for debugging scripts.
                self.asTests = self.asTestsDef;
            else:
                self.asTests = asArgs[iArg].split(':');
                for s in self.asTests:
                    if s not in self.asTestsDef:
                        raise base.InvalidOption('The "--add-guest-ctrl-tests" value "%s" is not valid; valid values are: %s'
                                                 % (s, ' '.join(self.asTestsDef)));
            return iNext;
        if asArgs[iArg] == '--add-guest-ctrl-skip-known-bugs':
            self.fSkipKnownBugs = True;
            return iArg + 1;
        if asArgs[iArg] == '--no-add-guest-ctrl-skip-known-bugs':
            self.fSkipKnownBugs = False;
            return iArg + 1;
        if asArgs[iArg] == '--add-guest-ctrl-debug-img':
            iArg += 1;
            iNext = self.oTstDrv.requireMoreArgs(1, asArgs, iArg);
            self.oDebug.sVBoxServiceExeHst = asArgs[iArg];
            return iNext;
        return iArg;

    def showUsage(self):
        base.SubTestDriverBase.showUsage(self);
        reporter.log('  --add-guest-ctrl-tests  <s1[:s2[:]]>');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asTestsDef)));
        reporter.log('  --add-guest-ctrl-skip-known-bugs');
        reporter.log('      Skips known bugs.  Default: --no-add-guest-ctrl-skip-known-bugs');
        reporter.log('Debugging:');
        reporter.log('  --add-guest-ctrl-debug-img');
        reporter.log('      Sets VBoxService image to deploy for debugging');
        return True;

    def testIt(self, oTestVm, oSession, oTxsSession):
        """
        Executes the test.

        Returns fRc, oTxsSession.  The latter may have changed.
        """

        self.sPathVBoxServiceExeGst =   oTestVm.pathJoin(self.oTstDrv.getGuestSystemAdminDir(oTestVm), 'VBoxService') \
                                      + base.exeSuff();

        # For some tests we need know the Guest Additions version, so fetch it now.
        self.tpAdditionsVer         = self.oTstDrv.getGuestAdditionsVersion(oSession);

        reporter.log("Active tests: %s" % (self.asTests,));

        # The tests. Must-succeed tests should be first.
        atTests = [
            ( True,  self.prepareGuestForDebugging,         None,               'Manual Debugging',),
            ( True,  self.prepareGuestForTesting,           None,               'Preparations',),
            ( True,  self.testGuestCtrlSession,             'session_basic',    'Session Basics',),
            ( True,  self.testGuestCtrlExec,                'exec_basic',       'Execution',),
            ( False, self.testGuestCtrlExecTimeout,         'exec_timeout',     'Execution Timeouts',),
            ( False, self.testGuestCtrlSessionEnvironment,  'session_env',      'Session Environment',),
            ( False, self.testGuestCtrlSessionFileRefs,     'session_file_ref', 'Session File References',),
            #( False, self.testGuestCtrlSessionDirRefs,      'session_dir_ref',  'Session Directory References',),
            ( False, self.testGuestCtrlSessionProcRefs,     'session_proc_ref', 'Session Process References',),
            ( False, self.testGuestCtrlDirCreate,           'dir_create',       'Creating directories',),
            ( False, self.testGuestCtrlDirCreateTemp,       'dir_create_temp',  'Creating temporary directories',),
            ( False, self.testGuestCtrlDirRead,             'dir_read',         'Reading directories',),
            ( False, self.testGuestCtrlCopyTo,              'copy_to',          'Copy to guest',),
            ( False, self.testGuestCtrlCopyFrom,            'copy_from',        'Copy from guest',),
            ( False, self.testGuestCtrlFileStat,            'file_stat',        'Querying file information (stat)',),
            ( False, self.testGuestCtrlFileOpen,            'file_open',        'File open',),
            ( False, self.testGuestCtrlFileRead,            'file_read',        'File read',),
            ( False, self.testGuestCtrlFileWrite,           'file_write',       'File write',),
            ( False, self.testGuestCtrlFileRemove,          'file_remove',      'Removing files',), # Destroys prepped files.
            ( False, self.testGuestCtrlUpdateAdditions,     'update_additions', 'Updating Guest Additions',),
            # @todo r=aeichner Only enable it again when this really works,
            #                      the 3D tests should probably live in a separate file
            # ( False, self.testGuestCtrl3D,                  '3d',               '3D acceleration',),
        ];

        if not self.fSkipKnownBugs:
            atTests.extend([
                # @todo Seems to (mainly?) fail on Linux guests, primarily running with systemd as service supervisor.
                #        Needs to be investigated and fixed.
                ( False, self.testGuestCtrlSessionReboot,   'session_reboot',   'Session w/ Guest Reboot',), # May zap /tmp.
            ]);

        fRc = True;
        for fMustSucceed, fnHandler, sShortNm, sTestNm in atTests:

            # If for whatever reason the VM object became invalid, bail out.
            if not oTestVm:
                reporter.error('Test VM object invalid (VBoxSVC or client process crashed?), aborting tests');
                fRc = False;
                break;

            reporter.testStart(sTestNm);
            if sShortNm is None or sShortNm in self.asTests:
                # Returns (fRc, oTxsSession, oSession) - but only the first one is mandatory.
                aoResult = fnHandler(oSession, oTxsSession, oTestVm);
                if aoResult is None or isinstance(aoResult, bool):
                    fRcTest = aoResult;
                else:
                    fRcTest = aoResult[0];
                    if len(aoResult) > 1:
                        oTxsSession = aoResult[1];
                        if len(aoResult) > 2:
                            oSession = aoResult[2];
                            assert len(aoResult) == 3;
            else:
                fRcTest = None;

            if fRcTest is False and reporter.testErrorCount() == 0:
                fRcTest = reporter.error('Buggy test! Returned False w/o logging the error!');
            if reporter.testDone(fRcTest is None)[1] != 0:
                fRcTest = False;
                fRc     = False;

            # Stop execution if this is a must-succeed test and it failed.
            if fRcTest is False and fMustSucceed is True:
                reporter.log('Skipping any remaining tests since the previous one failed.');
                break;

        # Upload VBoxService logs on failure.
        if  reporter.testErrorCount() > 0 \
        and self.oDebug.sGstVBoxServiceLogPath:
            sVBoxServiceLogsTarGz    = 'ga-vboxservice-logs-%s.tar.gz' % oTestVm.sVmName;
            sGstVBoxServiceLogsTarGz = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), sVBoxServiceLogsTarGz);
            if self.oTstDrv.txsPackFile(oSession, oTxsSession, \
                                        sGstVBoxServiceLogsTarGz, self.oDebug.sGstVBoxServiceLogPath, fIgnoreErrors = True):
                self.oTstDrv.txsDownloadFiles(oSession, oTxsSession, [ (sGstVBoxServiceLogsTarGz, sVBoxServiceLogsTarGz) ], \
                                              fIgnoreErrors = True);

        return (fRc, oTxsSession);

    def prepareGuestForDebugging(self, oSession, oTxsSession, oTestVm): # pylint: disable=unused-argument
        """
        Prepares a guest for (manual) debugging.

        This involves copying over and invoking a the locally built VBoxService binary.
        """

        if self.oDebug.sVBoxServiceExeHst is None: # If no debugging enabled, bail out.
            reporter.log('Skipping debugging');
            return True;

        reporter.log('Preparing for debugging ...');

        try:
            self.vboxServiceControl(oTxsSession, oTestVm, fStart = False);

            self.oTstDrv.sleep(5); # Fudge factor -- wait until the service stopped.

            reporter.log('Uploading "%s" to "%s" ...' % (self.oDebug.sVBoxServiceExeHst, self.sPathVBoxServiceExeGst));
            oTxsSession.syncUploadFile(self.oDebug.sVBoxServiceExeHst, self.sPathVBoxServiceExeGst);

            if oTestVm.isLinux():
                oTxsSession.syncChMod(self.sPathVBoxServiceExeGst, 0o755);

            self.vboxServiceControl(oTxsSession, oTestVm, fStart = True);

            self.oTstDrv.sleep(5); # Fudge factor -- wait until the service is ready.

        except:
            return reporter.errorXcpt('Unable to prepare for debugging');

        return True;

    def locateGstBinary(self, oTxsSession, asPaths):
        """
        Locates a guest binary on the guest by checking the paths in \a asPaths.

        Returns a tuple (success, path).
        """
        for sCurPath in asPaths:
            reporter.log2('Checking for \"%s\" ...' % (sCurPath));
            if oTxsSession.syncIsFile(sCurPath, fIgnoreErrors = True):
                return (True, oTxsSession.syncExpandString(sCurPath));
        reporter.error('Unable to find guest binary in any of these places:\n%s' % ('\n'.join(asPaths),));
        return (False, "");

    #
    # VBoxService handling.
    #
    def vboxServiceControl(self, oTxsSession, oTestVm, fStart):
        """
        Controls VBoxService on the guest by starting or stopping the service.
        Returns success indicator.
        """

        fRc = True;

        if oTestVm.isWindows():
            sPathSC = os.path.join(self.oTstDrv.getGuestSystemDir(oTestVm), 'sc.exe');
            if oTestVm.sKind in ('WindowsNT3x', 'WindowsNT4', 'Windows2000',): # W2K too?
                sPathSC = os.path.join(self.oTstDrv.getGuestSystemDir(oTestVm), 'net.exe');
            if fStart is True:
                fRc = self.oTstDrv.txsRunTest(oTxsSession, 'Starting VBoxService', 30 * 1000,
                                              sPathSC, (sPathSC, 'start', 'VBoxService'));
            else:
                fRc = self.oTstDrv.txsRunTest(oTxsSession, 'Stopping VBoxService', 30 * 1000,
                                              sPathSC, (sPathSC, 'stop', 'VBoxService'));
        elif oTestVm.isLinux():
            sPathService = "/sbin/rcvboxadd-service";
            if fStart is True:
                fRc = self.oTstDrv.txsRunTest(oTxsSession, 'Starting VBoxService', 30 * 1000,
                                              sPathService, (sPathService, 'start'));
            else:
                fRc = self.oTstDrv.txsRunTest(oTxsSession, 'Stopping VBoxService', 30 * 1000,
                                              sPathService, (sPathService, 'stop'));
        else:
            reporter.log('Controlling VBoxService not supported for this guest yet');

        return fRc;

    def waitForGuestFacility(self, oSession, eFacilityType, sDesc,
                             eFacilityStatus, cMsTimeout = 30 * 1000):
        """
        Waits for a guest facility to enter a certain status.
        By default the "Active" status is being used.

        Returns success status.
        """

        reporter.log('Waiting for Guest Additions facility "%s" to change to status %s (%dms timeout)...'
                     % (sDesc, str(eFacilityStatus), cMsTimeout));

        fRc = False;

        eStatusOld = None;
        tsStart    = base.timestampMilli();
        while base.timestampMilli() - tsStart < cMsTimeout:
            try:
                eStatus, _ = oSession.o.console.guest.getFacilityStatus(eFacilityType);
                reporter.log('Current status is %s' % (str(eStatus)));
                if eStatusOld is None:
                    eStatusOld = eStatus;
            except:
                reporter.errorXcpt('Getting facility status failed');
                break;
            if eStatus != eStatusOld:
                reporter.log('Status changed to %s' % (str(eStatus)));
                eStatusOld = eStatus;
            if eStatus == eFacilityStatus:
                fRc = True;
                break;
            self.oTstDrv.sleep(5); # Do some busy waiting.

        if not fRc:
            reporter.error('Waiting for Guest Additions facility "%s" timed out' % (sDesc));
        else:
            reporter.log('Guest Additions facility "%s" reached requested status %s after %dms'
                         % (sDesc, str(eFacilityStatus), base.timestampMilli() - tsStart));

        return fRc;

    #
    # Guest test files.
    #

    def prepareGuestForTesting(self, oSession, oTxsSession, oTestVm):
        """
        Prepares the VM for testing, uploading a bunch of files and stuff via TXS.
        Returns success indicator.
        """
        _ = oSession;

        #
        # Make sure the temporary directory exists.
        #
        for sDir in [self.oTstDrv.getGuestTempDir(oTestVm), ]:
            if oTxsSession.syncMkDirPath(sDir, 0o777) is not True:
                return reporter.error('Failed to create directory "%s"!' % (sDir,));

        # Query the TestExecService (TXS) version first to find out on what we run.
        fGotTxsVer = self.oTstDrv.txsVer(oSession, oTxsSession, 30 * 100, fIgnoreErrors = True);

        # Whether to enable verbose logging for VBoxService.
        fEnableVerboseLogging = False;

        # On Windows and Linux guests we always can (try to) enable verbose logging.
        if (oTestVm.isWindows() or oTestVm.isLinux()):
            fEnableVerboseLogging = True;

        # Old TxS versions had a bug which caused an infinite loop when executing stuff containing "$xxx",
        # so check if we got the version here first and skip enabling verbose logging nonetheless if needed.
        if not fGotTxsVer:
            reporter.log('Too old TxS service running')
            fEnableVerboseLogging = False;

        self.oDebug.sGstVBoxServiceLogPath = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), "VBoxService");
        sPathLogFile = oTestVm.pathJoin(self.oDebug.sGstVBoxServiceLogPath, 'VBoxService.log');

        if oTestVm.isWindows():
            sImagePath = '%s -vvvv --logfile %s' % (self.sPathVBoxServiceExeGst, sPathLogFile);
            # For newer revisions we use VBoxGuestInstallHelper.exe. Should work on all Windows versions.
            if  self.oTstDrv.fpApiVer >= 7.0 \
            and self.oTstDrv.getGuestAdditionsRevision(oSession) >= 166162:
                sRegEditorExeBasePath = 'C:\\Program Files\\Oracle\\VirtualBox Guest Additions\\';
                if  self.oTstDrv.fpApiVer >= 7.2 \
                and self.oTstDrv.getGuestAdditionsRevision(oSession) >= 168202:
                    sRegEditorExePath = sRegEditorExeBasePath + 'Tools\\VBoxGuestInstallHelper.exe';
                else:
                    sRegEditorExePath = sRegEditorExeBasePath + 'VBoxGuestInstallHelper.exe';
                asRegEditorArgs       = [ sRegEditorExePath, 'registry', 'write', 'HKLM',
                                         'SYSTEM\\CurrentControlSet\\Services\\VBoxService', 'ImagePath', 'REG_SZ',
                                         sImagePath ];
            # reg.exe is not able to write keys on older Windows versions (NT4, 2k).
            elif oTestVm.sKind not in ('WindowsNT4', 'Windows2000',):
                sRegEditorExePath     = oTestVm.pathJoin(self.oTstDrv.getGuestSystemDir(oTestVm), 'reg.exe');
                asRegEditorArgs       = [ sRegEditorExePath, 'add', 'HKLM\\SYSTEM\\CurrentControlSet\\Services\\VBoxService',
                                          '/v', 'ImagePath', '/t', 'REG_SZ', '/d', sImagePath, '/f' ];
            else:
                reporter.log('VBoxService logging is not available on this Windows guest');
                fEnableVerboseLogging = False;

        # Some older Linux test VMs (like tst-rhel5) don't have a pre-configured 'vbox' user.
        # So make sure that this user exists and has the appropriate password set. Ignore any errors.
        elif oTestVm.isLinux():
            sCmdUserAdd = oTestVm.pathJoin(self.oTstDrv.getGuestSystemAdminDir(oTestVm, sPathPrefix = '/usr'), 'useradd');
            asArgs = [ sCmdUserAdd, '-m', 'vbox' ];
            self.oTstDrv.txsRunTest(oTxsSession, sCmdUserAdd, 5 * 60 * 1000, sCmdUserAdd, asArgs,
                                    fCheckSessionStatus = False);
            # We must supply the password in an encrypted form using chpasswd (via stdin).
            sCmdChPasswd = oTestVm.pathJoin(self.oTstDrv.getGuestSystemAdminDir(oTestVm, sPathPrefix = '/usr'), 'chpasswd');
            asArgs = [ sCmdChPasswd ];
            self.oTstDrv.txsRunTestStdIn(oTxsSession, sCmdChPasswd, 5 * 60 * 1000, sCmdChPasswd, asArgs,
                                         sStdIn = 'vbox:password\n', fIgnoreErrors = True);
            # Show SELinux status (might not be available everywhere, so check for binary first).
            asCmdSELinuxSts = [
                ( oTestVm.pathJoin(self.oTstDrv.getGuestSystemAdminDir(oTestVm, sPathPrefix = '/usr'), 'sestatus'), ),
                ( oTestVm.pathJoin(self.oTstDrv.getGuestSystemAdminDir(oTestVm, sPathPrefix = '/usr/local'), 'sestatus'), ),
            ];
            fSELinuxFound = False;
            for sCmdSELinuxSts in asCmdSELinuxSts:
                fSELinuxFound = self.oTstDrv.txsIsFile(oSession, oTxsSession, sCmdSELinuxSts, fIgnoreErrors = True):
                if fSELinuxFound:
                    reporter.log('SELinux found:');
                    asArgs = [ sCmdSELinuxSts ];
                    self.oTstDrv.txsRunTest(oTxsSession, sCmdSELinuxSts, 5 * 60 * 1000, sCmdSELinuxSts, asArgs,
                                            fCheckSessionStatus = False);
                    break;
            if not fSELinuxFound:
                reporter.log('SELinux not found');
        #
        # Enable VBoxService verbose logging.
        #
        reporter.log('Enabling verbose VBoxService logging: %s' % (fEnableVerboseLogging));
        if fEnableVerboseLogging:
            if oTxsSession.syncMkDirPath(self.oDebug.sGstVBoxServiceLogPath, 0o777) is not True:
                return reporter.error('Failed to create directory "%s"!' % (self.oDebug.sGstVBoxServiceLogPath,));
            reporter.log('VBoxService logs will be stored in "%s"' % (self.oDebug.sGstVBoxServiceLogPath,));

            fRestartVBoxService = False;

            if oTestVm.isWindows():
                asArgs = [ sRegEditorExePath ] + asRegEditorArgs;
                fRestartVBoxService = self.oTstDrv.txsRunTest(oTxsSession, 'Enabling VBoxService verbose logging (via registry)',
                                                              30 * 1000,
                                                              sRegEditorExePath, asRegEditorArgs);
            elif oTestVm.isLinux():
                # Need to use some stupid trickery here to locate the sed binary,
                # as this might differ on various Linux hosts, sigh. We also could use 'which' or some sort on the guest.
                # Note: Sorted by likeliness.
                asSedPaths = [
                    '/bin/sed',
                    '/usr/bin/sed',
                    '/usr/local/bin/sed'
                ];
                fRc, sPathSed = self.locateGstBinary(oTxsSession, asSedPaths);
                if fRc:
                    fRestartVBoxService = self.oTstDrv.txsRunTest(oTxsSession, 'Enabling VBoxService verbose logging', 30 * 1000,
                                             sPathSed,
                                            (sPathSed, '-i', '-e', 's/'
                                             '\\$2 \\$3'
                                             '/'
                                             '\\$2 \\$3 -vvvv --logfile \\/var\\/tmp\\/VBoxService\\/VBoxService.log'
                                             '/g',
                                             '/sbin/rcvboxadd-service'));
            else:
                reporter.log('Verbose logging for VBoxService not supported for this guest yet');

            if fRestartVBoxService:
                # Wait for VBoxService to start up properly so, we can shut it down again and restart.
                fRc = self.waitForGuestFacility(oSession, vboxcon.AdditionsFacilityType_VBoxService, "VBoxService",
                                                vboxcon.AdditionsFacilityStatus_Active);
                if not fRc:
                    reporter.log('VBoxService didn\'t startup in time');
                    return False;

                self.vboxServiceControl(oTxsSession, oTestVm, fStart = False);
                self.oTstDrv.sleep(5);
                self.vboxServiceControl(oTxsSession, oTestVm, fStart = True);

        # Wait for VBoxService to start up in any case.
        reporter.testStart('Waiting for VBoxService to get started');
        fRc = self.waitForGuestFacility(oSession, vboxcon.AdditionsFacilityType_VBoxService, "VBoxService",
                                        vboxcon.AdditionsFacilityStatus_Active);
        reporter.testDone();
        if not fRc:
            return False;

        #
        # Generate and upload some random files and dirs to the guest.
        # Note! Make sure we don't run into too-long-path issues when using
        #       the test files on the host if.
        #
        cchGst = len(self.oTstDrv.getGuestTempDir(oTestVm)) + 1 + len('addgst-1') + 1;
        cchHst = len(self.oTstDrv.sScratchPath) + 1 + len('copyto/addgst-1') + 1;
        cchMaxPath = 230;
        if cchHst > cchGst:
            cchMaxPath -= cchHst - cchGst;
            reporter.log('cchMaxPath=%s (cchHst=%s, cchGst=%s)' % (cchMaxPath, cchHst, cchGst,));
        self.oTestFiles = vboxtestfileset.TestFileSet(oTestVm,
                                                      self.oTstDrv.getGuestTempDir(oTestVm), 'addgst-1',
                                                      # Make sure that we use a lowest common denominator across all supported
                                                      # platforms, to make testing the randomly generated file paths work
                                                      # reliably.
                                                      cchMaxPath = cchMaxPath, asCompatibleWith = [ ('cross') ]);
        return self.oTestFiles.upload(oTxsSession, self.oTstDrv);


    #
    # gctrlXxxx stuff.
    #

    def gctrlCopyFileFrom(self, oGuestSession, oTest, fExpected):
        """
        Helper function to copy a single file from the guest to the host.
        """

        # As we pass-in randomly generated file names, the source sometimes can be empty, which
        # in turn will result in a (correct) error by the API. Simply skip this function then.
        if not oTest.sSrc:
            reporter.log2('Skipping guest file "%s"' % (limitString(oTest.sSrc)));
            return fExpected;

        #
        # Do the copying.
        #
        reporter.log2('Copying guest file "%s" to host "%s"' % (limitString(oTest.sSrc), limitString(oTest.sDst)));
        try:
            if self.oTstDrv.fpApiVer >= 5.0:
                oCurProgress = oGuestSession.fileCopyFromGuest(oTest.sSrc, oTest.sDst, oTest.afFlags);
            else:
                oCurProgress = oGuestSession.copyFrom(oTest.sSrc, oTest.sDst, oTest.afFlags);
        except:
            reporter.maybeErrXcpt(fExpected, 'Copy from exception for sSrc="%s", sDst="%s":' % (oTest.sSrc, oTest.sDst,));
            return False;
        if oCurProgress is None:
            return reporter.error('No progress object returned');
        oProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr, self.oTstDrv, "gctrlFileCopyFrom");
        oProgress.wait();
        if not oProgress.isSuccess():
            oProgress.logResult(fIgnoreErrors = not fExpected);
            return False;

        #
        # Check the result if we can.
        #
        if oTest.oSrc:
            assert isinstance(oTest.oSrc, testfileset.TestFile);
            sDst = oTest.sDst;
            if os.path.isdir(sDst):
                sDst = os.path.join(sDst, oTest.oSrc.sName);
            try:
                oFile = open(sDst, 'rb');                       # pylint: disable=consider-using-with
            except:
                # Don't report expected non-existing paths / files as an error.
                return reporter.maybeErrXcpt(fExpected, 'open(%s) failed during verfication (file / path not found)' % (sDst,));
            fEqual = oTest.oSrc.equalFile(oFile);
            oFile.close();
            if not fEqual:
                return reporter.error('Content differs for "%s"' % (sDst,));

        return True;

    def __compareTestDir(self, oDir, sHostPath): # type: (testfileset.TestDir, str) -> bool
        """
        Recursively compare the content of oDir and sHostPath.

        Returns True on success, False + error logging on failure.

        Note! This ASSUMES that nothing else was copied to sHostPath!
        """
        #
        # First check out all the entries and files in the directory.
        #
        dLeftUpper = dict(oDir.dChildrenUpper);
        try:
            asEntries = os.listdir(sHostPath);
        except:
            return reporter.errorXcpt('os.listdir(%s) failed' % (sHostPath,));

        fRc = True;
        for sEntry in asEntries:
            sEntryUpper = sEntry.upper();
            if sEntryUpper not in dLeftUpper:
                fRc = reporter.error('Unexpected entry "%s" in "%s"' % (sEntry, sHostPath,));
            else:
                oFsObj = dLeftUpper[sEntryUpper];
                del dLeftUpper[sEntryUpper];

                if isinstance(oFsObj, testfileset.TestFile):
                    sFilePath = os.path.join(sHostPath, oFsObj.sName);
                    try:
                        oFile = open(sFilePath, 'rb');          # pylint: disable=consider-using-with
                    except:
                        fRc = reporter.errorXcpt('open(%s) failed during verfication' % (sFilePath,));
                    else:
                        fEqual = oFsObj.equalFile(oFile);
                        oFile.close();
                        if not fEqual:
                            fRc = reporter.error('Content differs for "%s"' % (sFilePath,));

        # List missing entries:
        for sKey in dLeftUpper:
            oEntry = dLeftUpper[sKey];
            fRc = reporter.error('%s: Missing %s "%s" (src path: %s)'
                                 % (sHostPath, oEntry.sName,
                                    'file' if isinstance(oEntry, testfileset.TestFile) else 'directory', oEntry.sPath));

        #
        # Recurse into subdirectories.
        #
        for oFsObj in oDir.aoChildren:
            if isinstance(oFsObj, testfileset.TestDir):
                fRc = self.__compareTestDir(oFsObj, os.path.join(sHostPath, oFsObj.sName)) and fRc;
        return fRc;

    def gctrlCopyDirFrom(self, oGuestSession, oTest, fExpected):
        """
        Helper function to copy a directory from the guest to the host.
        """

        # As we pass-in randomly generated directories, the source sometimes can be empty, which
        # in turn will result in a (correct) error by the API. Simply skip this function then.
        if not oTest.sSrc:
            reporter.log2('Skipping guest dir "%s"' % (limitString(oTest.sSrc)));
            return fExpected;

        #
        # Do the copying.
        #
        reporter.log2('Copying guest dir "%s" to host "%s"' % (limitString(oTest.sSrc), limitString(oTest.sDst)));
        try:
            if self.oTstDrv.fpApiVer >= 7.0:
                ## @todo Make the following new flags implicit for 7.0 for now. Develop dedicated tests for this later and remove.
                if not oTest.afFlags:
                    oTest.afFlags = [ vboxcon.DirectoryCopyFlag_Recursive, ];
                elif vboxcon.DirectoryCopyFlag_Recursive not in oTest.afFlags:
                    oTest.afFlags.append(vboxcon.DirectoryCopyFlag_Recursive);
                ## @todo Ditto.
                if not oTest.afFlags:
                    oTest.afFlags = [ vboxcon.DirectoryCopyFlag_FollowLinks, ];
                elif vboxcon.DirectoryCopyFlag_FollowLinks not in oTest.afFlags:
                    oTest.afFlags.append(vboxcon.DirectoryCopyFlag_FollowLinks);
            oCurProgress = oGuestSession.directoryCopyFromGuest(oTest.sSrc, oTest.sDst, oTest.afFlags);
        except:
            reporter.maybeErrXcpt(fExpected, 'Copy dir from exception for sSrc="%s", sDst="%s":' % (oTest.sSrc, oTest.sDst,));
            return False;
        if oCurProgress is None:
            return reporter.error('No progress object returned');

        oProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr, self.oTstDrv, "gctrlDirCopyFrom");
        oProgress.wait();
        if not oProgress.isSuccess():
            oProgress.logResult(fIgnoreErrors = not fExpected);
            return False;

        #
        # Check the result if we can.
        #
        if oTest.oSrc:
            assert isinstance(oTest.oSrc, testfileset.TestDir);
            sDst = oTest.sDst;
            if oTest.fIntoDst:
                return self.__compareTestDir(oTest.oSrc, os.path.join(sDst, oTest.oSrc.sName));
            oDummy = testfileset.TestDir(None, 'dummy');
            oDummy.aoChildren = [oTest.oSrc,]
            oDummy.dChildrenUpper = { oTest.oSrc.sName.upper(): oTest.oSrc, };
            return self.__compareTestDir(oDummy, sDst);
        return True;

    def gctrlCopyFileTo(self, oGuestSession, sSrc, sDst, afFlags, fIsError):
        """
        Helper function to copy a single file from the host to the guest.

        afFlags is either None or an array of vboxcon.DirectoryCopyFlag_Xxxx values.
        """
        reporter.log2('Copying host file "%s" to guest "%s" (flags %s)' % (limitString(sSrc), limitString(sDst), afFlags));
        try:
            if self.oTstDrv.fpApiVer >= 5.0:
                oCurProgress = oGuestSession.fileCopyToGuest(sSrc, sDst, afFlags);
            else:
                oCurProgress = oGuestSession.copyTo(sSrc, sDst, afFlags);
        except:
            reporter.maybeErrXcpt(fIsError, 'sSrc=%s sDst=%s' % (sSrc, sDst,));
            return False;

        if oCurProgress is None:
            return reporter.error('No progress object returned');
        oProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr, self.oTstDrv, "gctrlCopyFileTo");

        try:
            oProgress.wait();
            if not oProgress.isSuccess():
                oProgress.logResult(fIgnoreErrors = not fIsError);
                return False;
        except:
            reporter.maybeErrXcpt(fIsError, 'Wait exception for sSrc="%s", sDst="%s":' % (sSrc, sDst));
            return False;
        return True;

    def gctrlCopyDirTo(self, oGuestSession, sSrc, sDst, afFlags, fIsError):
        """
        Helper function to copy a directory (tree) from the host to the guest.

        afFlags is either None or an array of vboxcon.DirectoryCopyFlag_Xxxx values.
        """
        reporter.log2('Copying host directory "%s" to guest "%s" (flags %s)' % (limitString(sSrc), limitString(sDst), afFlags));
        try:
            if self.oTstDrv.fpApiVer >= 7.0:
                ## @todo Make the following new flags implicit for 7.0 for now. Develop dedicated tests for this later and remove.
                if not afFlags:
                    afFlags = [ vboxcon.DirectoryCopyFlag_Recursive, ];
                elif vboxcon.DirectoryCopyFlag_Recursive not in afFlags:
                    afFlags.append(vboxcon.DirectoryCopyFlag_Recursive);
                ## @todo Ditto.
                if not afFlags:
                    afFlags = [vboxcon.DirectoryCopyFlag_FollowLinks,];
                elif vboxcon.DirectoryCopyFlag_FollowLinks not in afFlags:
                    afFlags.append(vboxcon.DirectoryCopyFlag_FollowLinks);
            oCurProgress = oGuestSession.directoryCopyToGuest(sSrc, sDst, afFlags);
        except:
            reporter.maybeErrXcpt(fIsError, 'sSrc=%s sDst=%s' % (sSrc, sDst,));
            return False;

        if oCurProgress is None:
            return reporter.error('No progress object returned');
        oProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr, self.oTstDrv, "gctrlCopyFileTo");

        try:
            oProgress.wait();
            if not oProgress.isSuccess():
                oProgress.logResult(fIgnoreErrors = not fIsError);
                return False;
        except:
            reporter.maybeErrXcpt(fIsError, 'Wait exception for sSrc="%s", sDst="%s":' % (sSrc, sDst));
            return False;
        return True;

    def gctrlCreateDir(self, oTest, oRes, oGuestSession):
        """
        Helper function to create a guest directory specified in the current test.
        """
        reporter.log2('Creating directory "%s"' % (limitString(oTest.sDirectory),));
        try:
            oGuestSession.directoryCreate(oTest.sDirectory, oTest.fMode, oTest.afFlags);
        except:
            reporter.maybeErrXcpt(oRes.fRc, 'Failed to create "%s" fMode=%o afFlags=%s'
                                  % (oTest.sDirectory, oTest.fMode, oTest.afFlags,));
            return not oRes.fRc;
        if oRes.fRc is not True:
            return reporter.error('Did not expect to create directory "%s"!' % (oTest.sDirectory,));

        # Check if the directory now exists.
        try:
            if self.oTstDrv.fpApiVer >= 5.0:
                fDirExists = oGuestSession.directoryExists(oTest.sDirectory, False);
            else:
                fDirExists = oGuestSession.directoryExists(oTest.sDirectory);
        except:
            return reporter.errorXcpt('directoryExists failed on "%s"!' % (oTest.sDirectory,));
        if not fDirExists:
            return reporter.errorXcpt('directoryExists returned False on "%s" after directoryCreate succeeded!'
                                       % (oTest.sDirectory,));
        return True;

    def gctrlReadDirTree(self, oTest, oGuestSession, fIsError, fUseDirList = False, cEntriesPerRead = 0, sSubDir = None):
        """
        Helper function to recursively read a guest directory tree specified in the current test.
        """
        sDir     = oTest.sDirectory;
        sFilter  = oTest.sFilter;
        afFlags  = oTest.afFlags;
        oTestVm  = oTest.oCreds.oTestVm;
        sCurDir  = oTestVm.pathJoin(sDir, sSubDir) if sSubDir else sDir;

        fRc      = True; # Be optimistic.
        cDirs    = 0;    # Number of directories read.
        cFiles   = 0;    # Number of files read.
        cOthers  = 0;    # Other files.

        cEntriesToRead = cEntriesPerRead; # Only used when listing directories.
        aFsObjInfo     = [];

        # Open the directory:
        reporter.log2('Directory="%s", filter="%s", afFlags="%s", fUseDirList=%s, cEntriesPerRead=%d'
                      % (limitString(sCurDir), sFilter, afFlags, fUseDirList, cEntriesPerRead));
        try:
            oCurDir = oGuestSession.directoryOpen(sCurDir, sFilter, afFlags);
        except:
            reporter.maybeErrXcpt(fIsError, 'sCurDir=%s sFilter=%s afFlags=%s' % (sCurDir, sFilter, afFlags,))
            return (False, 0, 0, 0);

        # Read the directory.
        fDone = False;
        while fRc is True:
            reporter.log3('Reading next batch ...');
            aFsObjInfo = [];
            try:
                if not fUseDirList:
                    aFsObjInfo.append(oCurDir.read());
                else:
                    if not cEntriesToRead:
                        cEntriesToRead = random.randrange(1, 32768);
                    aFsObjInfo = oCurDir.list(cEntriesToRead);
            except Exception as oXcpt:
                if vbox.ComError.notEqual(oXcpt, vbox.ComError.VBOX_E_OBJECT_NOT_FOUND):
                    if fUseDirList:
                        fRc = reporter.errorXcpt('Error listing directory "%s" (cEntriesToRead=%d):' % (sCurDir, cEntriesToRead));
                    else:
                        if self.oTstDrv.fpApiVer > 5.2:
                            reporter.errorXcpt('Error reading directory "%s":' % (sCurDir,));
                        else:
                            # Unlike fileOpen, directoryOpen will not fail if the directory does not exist.
                            reporter.maybeErrXcpt(fIsError, 'Error reading directory "%s":' % (sCurDir,));
                    fRc = False;
                else:
                    reporter.log2('\tNo more directory entries for "%s"' % (limitString(sCurDir),));
                    fDone = True;
                break;

            if fDone or not fRc: # Abort reading?
                break;

            reporter.log3('Processing next batch (%d items)...' % (len(aFsObjInfo)));
            for oFsObjInfo in aFsObjInfo:
                try:
                    sName = oFsObjInfo.name;
                    eType = oFsObjInfo.type;
                except:
                    fRc = reporter.errorXcpt();
                    break;

                if sName in ('.', '..', ):
                    if eType != vboxcon.FsObjType_Directory:
                        fRc = reporter.error('Wrong type for "%s": %d, expected %d (Directory)'
                                            % (sName, eType, vboxcon.FsObjType_Directory));
                elif eType == vboxcon.FsObjType_Directory:
                    reporter.log2('  Directory "%s"' % limitString(oFsObjInfo.name));
                    aSubResult = self.gctrlReadDirTree(oTest, oGuestSession, fIsError,
                                                       fUseDirList, cEntriesPerRead,
                                                       oTestVm.pathJoin(sSubDir, sName) if sSubDir else sName);
                    fRc      = aSubResult[0];
                    cDirs   += aSubResult[1] + 1;
                    cFiles  += aSubResult[2];
                    cOthers += aSubResult[3];
                elif eType is vboxcon.FsObjType_File:
                    reporter.log4('  File "%s"' % oFsObjInfo.name);
                    cFiles += 1;
                elif eType is vboxcon.FsObjType_Symlink:
                    reporter.log4('  Symlink "%s" -- not tested yet' % oFsObjInfo.name);
                    cOthers += 1;
                elif    oTestVm.isWindows() \
                    or oTestVm.isOS2() \
                    or eType not in (vboxcon.FsObjType_Fifo, vboxcon.FsObjType_DevChar, vboxcon.FsObjType_DevBlock,
                                    vboxcon.FsObjType_Socket, vboxcon.FsObjType_WhiteOut):
                    fRc = reporter.error('Directory "%s" contains invalid directory entry "%s" (type %d)' %
                                        (sCurDir, oFsObjInfo.name, oFsObjInfo.type,));
                else:
                    cOthers += 1;

        # Close the directory
        try:
            oCurDir.close();
        except:
            fRc = reporter.errorXcpt('sCurDir=%s' % (sCurDir));

        return (fRc, cDirs, cFiles, cOthers);

    def gctrlReadDirTree2(self, oGuestSession, oDir, fUseDirList = False, cEntriesPerRead = 0):
        # type: (testfileset.TestDir) -> bool
        """
        Helper function to recursively read a guest directory tree specified in the current test.
        """

        #
        # Process the directory.
        #

        # Open the directory:
        try:
            oCurDir = oGuestSession.directoryOpen(oDir.sPath, '', None);
        except:
            return reporter.errorXcpt('sPath=%s' % (oDir.sPath,));

        # Read the directory.
        dLeftUpper     = dict(oDir.dChildrenUpper);
        cDot           = 0;
        cDotDot        = 0;
        fRc            = True;
        cEntriesToRead = cEntriesPerRead; # Only used when listing directories.
        aFsObjInfo     = [];
        while True:
            reporter.log3('Reading next batch ...');
            aFsObjInfo = [];
            try:
                if not fUseDirList:
                    aFsObjInfo.append(oCurDir.read());
                else:
                    if not cEntriesToRead:
                        cEntriesToRead = random.randrange(1, 32768);
                    aFsObjInfo = oCurDir.list(cEntriesToRead);
            except Exception as oXcpt:
                if vbox.ComError.notEqual(oXcpt, vbox.ComError.VBOX_E_OBJECT_NOT_FOUND):
                    if fUseDirList:
                        fRc = reporter.errorXcpt('Error listing directory "%s" (cEntriesToRead=%d):' % \
                                                 (oDir.sPath, cEntriesToRead));
                    else:
                        fRc = reporter.errorXcpt('Error reading directory "%s":' % (oDir.sPath));
                else:
                    reporter.log2('\tNo more directory entries for "%s"' % (limitString(oDir.sPath),));
                break;

            reporter.log3('Processing next batch (%d items)...' % (len(aFsObjInfo)));
            for oFsObjInfo in aFsObjInfo:
                try:
                    sName  = oFsObjInfo.name;
                    eType  = oFsObjInfo.type;
                    cbFile = oFsObjInfo.objectSize;
                    ## @todo check further attributes.
                except:
                    fRc = reporter.errorXcpt();
                    break;

                # '.' and '..' entries are not present in oDir.aoChildren, so special treatment:
                if sName in ('.', '..', ):
                    if eType != vboxcon.FsObjType_Directory:
                        fRc = reporter.error('Wrong type for "%s": %d, expected %d (Directory)'
                                            % (sName, eType, vboxcon.FsObjType_Directory));
                    if sName == '.': cDot    += 1;
                    else:            cDotDot += 1;
                else:
                    # Find the child and remove it from the dictionary.
                    sNameUpper = sName.upper();
                    oFsObj = dLeftUpper.get(sNameUpper);
                    if oFsObj is None:
                        fRc = reporter.error('Unknown object "%s" found in "%s" (type %s, size %s)!'
                                            % (sName, oDir.sPath, eType, cbFile,));
                    else:
                        del dLeftUpper[sNameUpper];

                        # Check type
                        if isinstance(oFsObj, testfileset.TestDir):
                            if eType != vboxcon.FsObjType_Directory:
                                fRc = reporter.error('%s: expected directory (%d), got eType=%d!'
                                                    % (oFsObj.sPath, vboxcon.FsObjType_Directory, eType,));
                        elif isinstance(oFsObj, testfileset.TestFile):
                            if eType != vboxcon.FsObjType_File:
                                fRc = reporter.error('%s: expected file (%d), got eType=%d!'
                                                    % (oFsObj.sPath, vboxcon.FsObjType_File, eType,));
                        else:
                            fRc = reporter.error('%s: WTF? type=%s' % (oFsObj.sPath, type(oFsObj),));

                        # Check the name.
                        if oFsObj.sName != sName:
                            fRc = reporter.error('%s: expected name "%s", got "%s" instead!' % \
                                                 (oFsObj.sPath, oFsObj.sName, sName,));

                        # Check the size if a file.
                        if isinstance(oFsObj, testfileset.TestFile) and cbFile != oFsObj.cbContent:
                            fRc = reporter.error('%s: expected size %s, got %s instead!' % \
                                                 (oFsObj.sPath, oFsObj.cbContent, cbFile,));

                        ## @todo check timestamps and attributes.

        # Close the directory
        try:
            oCurDir.close();
        except:
            fRc = reporter.errorXcpt('oDir.sPath=%s' % (oDir.sPath,));

        # Any files left over?
        for sKey in dLeftUpper:
            oFsObj = dLeftUpper[sKey];
            fRc = reporter.error('%s: Was not returned! (%s)' % (oFsObj.sPath, type(oFsObj),));

        # Check the dot and dot-dot counts.
        if cDot != 1:
            fRc = reporter.error('%s: Found %s "." entries, expected exactly 1!' % (oDir.sPath, cDot,));
        if cDotDot != 1:
            fRc = reporter.error('%s: Found %s ".." entries, expected exactly 1!' % (oDir.sPath, cDotDot,));

        #
        # Recurse into subdirectories using info from oDir.
        #
        for oFsObj in oDir.aoChildren:
            if isinstance(oFsObj, testfileset.TestDir):
                fRc = self.gctrlReadDirTree2(oGuestSession, oFsObj, fUseDirList, cEntriesPerRead) and fRc;

        return fRc;

    def gctrlExecDoTest(self, i, oTest, oRes, oGuestSession):
        """
        Wrapper function around gctrlExecute to provide more sanity checking
        when needed in actual execution tests.
        """
        reporter.log('Testing #%d, cmd="%s" ...' % (i, oTest.sCmd));
        fRcExec = self.gctrlExecute(oTest, oGuestSession, oRes.fRc);
        if fRcExec == oRes.fRc:
            fRc = True;
            if fRcExec is True:
                # Compare exit status / code on successful process execution.
                if     oTest.uExitStatus != oRes.uExitStatus \
                    or oTest.iExitCode   != oRes.iExitCode:
                    fRc = reporter.error('Test #%d (%s) failed: Got exit status + code %d,%d, expected %d,%d'
                                         % (i, oTest.asArgs,  oTest.uExitStatus, oTest.iExitCode,
                                            oRes.uExitStatus, oRes.iExitCode));

                # Compare test / result buffers on successful process execution.
                if oTest.sBuf is not None and oRes.sBuf is not None:
                    if not utils.areBytesEqual(oTest.sBuf, oRes.sBuf):
                        fRc = reporter.error('Test #%d (%s) failed: Got buffer\n%s (%d bytes), expected\n%s (%d bytes)'
                                             % (i, oTest.asArgs,
                                                map(hex, map(ord, oTest.sBuf)), len(oTest.sBuf),
                                                map(hex, map(ord, oRes.sBuf)),  len(oRes.sBuf)));
                    reporter.log2('Test #%d passed: Buffers match (%d bytes)' % (i, len(oRes.sBuf)));
                elif oRes.sBuf and not oTest.sBuf:
                    fRc = reporter.error('Test #%d (%s) failed: Got no buffer data, expected\n%s (%dbytes)' %
                                         (i, oTest.asArgs, map(hex, map(ord, oRes.sBuf)), len(oRes.sBuf),));

                if oRes.cbStdOut is not None and oRes.cbStdOut != oTest.cbStdOut:
                    fRc = reporter.error('Test #%d (%s) failed: Got %d bytes of stdout data, expected %d'
                                         % (i, oTest.asArgs, oTest.cbStdOut, oRes.cbStdOut));
                if oRes.cbStdErr is not None and oRes.cbStdErr != oTest.cbStdErr:
                    fRc = reporter.error('Test #%d (%s) failed: Got %d bytes of stderr data, expected %d'
                                         % (i, oTest.asArgs, oTest.cbStdErr, oRes.cbStdErr));
        else:
            fRc = reporter.error('Test #%d (%s) failed: Got %s, expected %s' % (i, oTest.asArgs, fRcExec, oRes.fRc));
        return fRc;

    def processCreateWrapper(self, oGuestSession, sCmd, asArgs, sCwd, aEnv, afFlags, timeoutMS):
        """
        Wrapepr function to deal with different flavors of the IGuestProcess::processCreate call,
        depending on the API version.

        Returns oProcess object on success, None on failure.
        """
        oProcess = None;

        reporter.log2('Executing sCmd=%s, cCwd=%s, afFlags=%s, timeoutMS=%d, asArgs=%s, asEnv=%s'
                      % (sCmd, sCwd, afFlags, timeoutMS, limitString(asArgs), limitString(aEnv),));

        if self.oTstDrv.fpApiVer >= 7.1 and self.oTstDrv.uRevision >= 156485:
            # 7.1 adds a current working directory parameter.
            oProcess = oGuestSession.processCreate(sCmd, asArgs, sCwd, aEnv, afFlags, timeoutMS);
        else:
            oProcess = oGuestSession.processCreate(sCmd,
                                                   asArgs if self.oTstDrv.fpApiVer >= 5.0 else asArgs[1:],
                                                   aEnv, afFlags, timeoutMS);
        return oProcess;

    def processExecute(self, oGuestSession, sCmd, asArgs, sCwd, aEnv, afFlags, timeoutMS, fIsError = True):
        """
        Executes a process on the guest and deals with input/output + waiting flags.

        Returns tuple (success, exit status, exit code, stdout len, stderr len, stdout / stderr output).
        """

        #
        # Start the process:
        #
        try:
            oProcess = self.processCreateWrapper(oGuestSession, sCmd, asArgs, sCwd, aEnv, afFlags, timeoutMS);
        except:
            reporter.maybeErrXcpt(fIsError, 'type=%s, asArgs=%s' % (type(asArgs), asArgs,));
            return (False, vboxcon.ProcessStatus_Undefined, 0, 0, 0, '');
        if oProcess is None:
            reporter.error('oProcess is None! (%s)' % (asArgs,));
            return (False, vboxcon.ProcessStatus_Undefined, 0, 0, 0, '');

        fRc         = True;
        uExitStatus = vboxcon.ProcessStatus_Undefined;
        iExitCode   = -1;
        cbStdOut    = 0;
        cbStdErr    = 0;
        sBufOut     = '';

        #time.sleep(5); # try this if you want to see races here.

        # Wait for the process to start properly:
        reporter.log2('Process start requested, waiting for start (%dms) ...' % (timeoutMS,));
        iPid = -1;
        aeWaitFor = [ vboxcon.ProcessWaitForFlag_Start, ];
        try:
            eWaitResult = oProcess.waitForArray(aeWaitFor, timeoutMS);
        except:
            reporter.maybeErrXcpt(fIsError, 'waitforArray failed for asArgs=%s' % (asArgs,));
            fRc = False;
        else:
            try:
                eStatus = oProcess.status;
                iPid    = oProcess.PID;
            except:
                fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
            else:
                reporter.log2('Wait result returned: %d, current process status is: %d' % (eWaitResult, eStatus,));

                #
                # Wait for the process to run to completion if necessary.
                #
                # Note! The above eWaitResult return value can be ignored as it will
                #       (mostly) reflect the process status anyway.
                #
                if eStatus == vboxcon.ProcessStatus_Started:

                    # What to wait for:
                    aeWaitFor = [ vboxcon.ProcessWaitForFlag_Terminate, ];
                    if vboxcon.ProcessCreateFlag_WaitForStdOut in afFlags:
                        aeWaitFor.append(vboxcon.ProcessWaitForFlag_StdOut);
                    if vboxcon.ProcessCreateFlag_WaitForStdErr in afFlags:
                        aeWaitFor.append(vboxcon.ProcessWaitForFlag_StdErr);
                    ## @todo Add vboxcon.ProcessWaitForFlag_StdIn.

                    reporter.log2('Process (PID %d) started, waiting for termination (%dms), aeWaitFor=%s ...'
                                  % (iPid, timeoutMS, aeWaitFor));
                    acbFdOut = [0,0,0];
                    while True:
                        try:
                            eWaitResult = oProcess.waitForArray(aeWaitFor, timeoutMS);
                        except KeyboardInterrupt: # Not sure how helpful this is, but whatever.
                            reporter.error('Process (PID %d) execution interrupted' % (iPid,));
                            try: oProcess.close();
                            except: pass;
                            break;
                        except:
                            fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
                            break;
                        #reporter.log2('Wait returned: %d' % (eWaitResult,));

                        # Process output:
                        for eFdResult, iFd, sFdNm in [ (vboxcon.ProcessWaitResult_StdOut, 1, 'stdout'),
                                                       (vboxcon.ProcessWaitResult_StdErr, 2, 'stderr'), ]:
                            if eWaitResult in (eFdResult, vboxcon.ProcessWaitResult_WaitFlagNotSupported):
                                try:
                                    abBuf = oProcess.read(iFd, 64 * 1024, timeoutMS);
                                except KeyboardInterrupt: # Not sure how helpful this is, but whatever.
                                    reporter.error('Process (PID %d) execution interrupted' % (iPid,));
                                    try: oProcess.close();
                                    except: pass;
                                except:
                                    reporter.maybeErrXcpt(fIsError, 'asArgs=%s' % (asArgs,));
                                else:
                                    if abBuf:
                                        reporter.log2('Process (PID %d) got %d bytes of %s data (type: %s)'
                                                      % (iPid, len(abBuf), sFdNm, type(abBuf)));
                                        if reporter.getVerbosity() >= 4:
                                            sBufOut = '';
                                            if sys.version_info >= (2, 7):
                                                if isinstance(abBuf, memoryview): ## @todo Why is this happening?
                                                    abBuf   = abBuf.tobytes();
                                                    sBufOut = abBuf.decode("utf-8");
                                            if sys.version_info <= (2, 7):
                                                if isinstance(abBuf, buffer):   # (for 3.0+) pylint: disable=undefined-variable
                                                    sBufOut = str(abBuf);
                                            for sLine in sBufOut.splitlines():
                                                reporter.log4('%s: %s' % (sFdNm, sLine));
                                        acbFdOut[iFd] += len(abBuf);
                                        sBufOut = abBuf; ## @todo Figure out how to uniform + append!

                        ## Process input (todo):
                        #if eWaitResult in (vboxcon.ProcessWaitResult_StdIn, vboxcon.ProcessWaitResult_WaitFlagNotSupported):
                        #    reporter.log2('Process (PID %d) needs stdin data' % (iPid,));

                        # Termination or error?
                        if eWaitResult in (vboxcon.ProcessWaitResult_Terminate,
                                           vboxcon.ProcessWaitResult_Error,
                                           vboxcon.ProcessWaitResult_Timeout,):
                            try:    eStatus = oProcess.status;
                            except: fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
                            reporter.log2('Process (PID %d) reported terminate/error/timeout: %d, status: %d'
                                          % (iPid, eWaitResult, eStatus,));
                            break;

                    # End of the wait loop.
                    _, cbStdOut, cbStdErr = acbFdOut;

                    try:    eStatus = oProcess.status;
                    except: fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
                    reporter.log2('Final process status (PID %d) is: %d' % (iPid, eStatus));
                    reporter.log2('Process (PID %d) %d stdout, %d stderr' % (iPid, cbStdOut, cbStdErr));

        #
        # Get the final status and exit code of the process.
        #
        try:
            uExitStatus = oProcess.status;
            iExitCode   = oProcess.exitCode;
        except:
            fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
        reporter.log2('Process (PID %d) has exit code: %d; status: %d ' % (iPid, iExitCode, uExitStatus));
        return (fRc, uExitStatus, iExitCode, cbStdOut, cbStdErr, sBufOut);

    def gctrlExecute(self, oTest, oGuestSession, fIsError):                     # pylint: disable=too-many-statements
        """
        Helper function to execute a program on a guest, specified in the current test.

        Note! This weirdo returns results (process exitcode and status) in oTest.
        """
        fRc = True; # Be optimistic.

        # Reset the weird result stuff:
        oTest.cbStdOut    = 0;
        oTest.cbStdErr    = 0;
        oTest.sBuf        = '';
        oTest.uExitStatus = 0;
        oTest.iExitCode   = 0;

        ## @todo Compare execution timeouts!
        #tsStart = base.timestampMilli();

        try:
            reporter.log2('Using session user=%s, sDomain=%s, name=%s, timeout=%d'
                          % (oGuestSession.user, oGuestSession.domain, oGuestSession.name, oGuestSession.timeout,));
        except:
            return reporter.errorXcpt();

        fRc, oTest.uExitStatus, oTest.iExitCode, oTest.cbStdOut, oTest.cbStdErr, oTest.sBuf = \
            self.processExecute(oGuestSession, oTest.sCmd, oTest.asArgs, oTest.sCwd, \
                                oTest.aEnv, oTest.afFlags, oTest.timeoutMS, fIsError);

        return fRc;

    def executeGstCtlHelper(self, oTxsSession, oGuestSession, asArgs, asEnv = None, sCwd = '', timeoutMS = 30 * 1000):
        """
        Wrapper to invoke the Guest Control Helper on the guest.

        Returns tuple (success, exit status, exit code, stdout len, stderr len, stdout / stderr output).
        """
        fRc         = True;
        eExitStatus = vboxcon.ProcessStatus_Undefined;
        iExitCode   = -1;
        cbStdOut    = 0;
        cbStdErr    = 0;
        sBuf        = '';

        if not self.sGstCtlHelperExe:
            fRc, self.sGstCtlHelperExe = self.locateGstBinary(oTxsSession, self.asGstCtlHelperPaths);
            if fRc:
                reporter.log('Using VBoxGuestControlHelper on guest at \"%s\"' % (self.sGstCtlHelperExe));

        if fRc \
        and self.sGstCtlHelperExe:
            try:
                asArgs2 = [ self.sGstCtlHelperExe ]; # Always set argv0.
                asArgs2.extend(asArgs);              # Add the arguments passed-in.
                if not asEnv:
                    asEnv = [];
                aeWaitFor = [ vboxcon.ProcessWaitForFlag_Terminate, \
                              vboxcon.ProcessWaitForFlag_StdOut, \
                              vboxcon.ProcessWaitForFlag_StdErr ];

                fRc, eExitStatus, iExitCode, cbStdOut, cbStdErr, sBuf = \
                    self.processExecute(oGuestSession, self.sGstCtlHelperExe, asArgs2, sCwd, \
                                        asEnv, aeWaitFor, timeoutMS);
                if eExitStatus != vboxcon.ProcessStatus_TerminatedNormally:
                    reporter.log('VBoxGuestControlHelper failed to run; got exit status %d' % (eExitStatus,));
                    fRc = False;
            except:
                fRc = reporter.errorXcpt();

        return (fRc, eExitStatus, iExitCode, cbStdOut, cbStdErr, sBuf);

    def testGuestCtrlSessionEnvironment(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests the guest session environment changes.
        """
        aoTests = [
            # Check basic operations.
            tdTestSessionEx([ # Initial environment is empty.
                tdStepSessionCheckEnv(),
                # Check clearing empty env.
                tdStepSessionClearEnv(),
                tdStepSessionCheckEnv(),
                # Check set.
                tdStepSessionSetEnv('FOO', 'BAR'),
                tdStepSessionCheckEnv(['FOO=BAR',]),
                tdStepRequireMinimumApiVer(5.0), # 4.3 can't cope with the remainder.
                tdStepSessionClearEnv(),
                tdStepSessionCheckEnv(),
                # Check unset.
                tdStepSessionUnsetEnv('BAR'),
                tdStepSessionCheckEnv(['BAR']),
                tdStepSessionClearEnv(),
                tdStepSessionCheckEnv(),
                # Set + unset.
                tdStepSessionSetEnv('FOO', 'BAR'),
                tdStepSessionCheckEnv(['FOO=BAR',]),
                tdStepSessionUnsetEnv('FOO'),
                tdStepSessionCheckEnv(['FOO']),
                # Bulk environment changes (via attrib) (shall replace existing 'FOO').
                tdStepSessionBulkEnv( ['PATH=/bin:/usr/bin', 'TMPDIR=/var/tmp', 'USER=root']),
                tdStepSessionCheckEnv(['PATH=/bin:/usr/bin', 'TMPDIR=/var/tmp', 'USER=root']),
                ]),
            tdTestSessionEx([ # Check that setting the same value several times works.
                tdStepSessionSetEnv('FOO','BAR'),
                tdStepSessionCheckEnv([ 'FOO=BAR',]),
                tdStepSessionSetEnv('FOO','BAR2'),
                tdStepSessionCheckEnv([ 'FOO=BAR2',]),
                tdStepSessionSetEnv('FOO','BAR3'),
                tdStepSessionCheckEnv([ 'FOO=BAR3',]),
                tdStepRequireMinimumApiVer(5.0), # 4.3 can't cope with the remainder.
                # Add a little unsetting to the mix.
                tdStepSessionSetEnv('BAR', 'BEAR'),
                tdStepSessionCheckEnv([ 'FOO=BAR3', 'BAR=BEAR',]),
                tdStepSessionUnsetEnv('FOO'),
                tdStepSessionCheckEnv([ 'FOO', 'BAR=BEAR',]),
                tdStepSessionSetEnv('FOO','BAR4'),
                tdStepSessionCheckEnv([ 'FOO=BAR4', 'BAR=BEAR',]),
                # The environment is case sensitive.
                tdStepSessionSetEnv('foo','BAR5'),
                tdStepSessionCheckEnv([ 'FOO=BAR4', 'BAR=BEAR', 'foo=BAR5']),
                tdStepSessionUnsetEnv('foo'),
                tdStepSessionCheckEnv([ 'FOO=BAR4', 'BAR=BEAR', 'foo']),
                ]),
            tdTestSessionEx([ # Bulk settings merges stuff, last entry standing.
                tdStepSessionBulkEnv(['FOO=bar', 'foo=bar', 'FOO=doofus', 'TMPDIR=/tmp', 'foo=bar2']),
                tdStepSessionCheckEnv(['FOO=doofus', 'TMPDIR=/tmp', 'foo=bar2']),
                tdStepRequireMinimumApiVer(5.0), # 4.3 is buggy!
                tdStepSessionBulkEnv(['2=1+1', 'FOO=doofus2', ]),
                tdStepSessionCheckEnv(['2=1+1', 'FOO=doofus2' ]),
                ]),
            # Invalid variable names.
            tdTestSessionEx([
                tdStepSessionSetEnv('', 'FOO', vbox.ComError.E_INVALIDARG),
                tdStepSessionCheckEnv(),
                tdStepRequireMinimumApiVer(5.0), # 4.3 is too relaxed checking input!
                tdStepSessionBulkEnv(['', 'foo=bar'], vbox.ComError.E_INVALIDARG),
                tdStepSessionCheckEnv(),
                tdStepSessionSetEnv('FOO=', 'BAR', vbox.ComError.E_INVALIDARG),
                tdStepSessionCheckEnv(),
                ]),
            # A bit more weird keys/values.
            tdTestSessionEx([ tdStepSessionSetEnv('$$$', ''),
                              tdStepSessionCheckEnv([ '$$$=',]), ]),
            tdTestSessionEx([ tdStepSessionSetEnv('$$$', '%%%'),
                              tdStepSessionCheckEnv([ '$$$=%%%',]),
                            ]),
            tdTestSessionEx([ tdStepRequireMinimumApiVer(5.0), # 4.3 is buggy!
                              tdStepSessionSetEnv(u'ß$%ß&', ''),
                              tdStepSessionCheckEnv([ u'ß$%ß&=',]),
                            ]),
            # Misc stuff.
            tdTestSessionEx([ tdStepSessionSetEnv('FOO', ''),
                              tdStepSessionCheckEnv(['FOO=',]),
                            ]),
            tdTestSessionEx([ tdStepSessionSetEnv('FOO', 'BAR'),
                              tdStepSessionCheckEnv(['FOO=BAR',])
                            ],),
            tdTestSessionEx([ tdStepSessionSetEnv('FOO', 'BAR'),
                              tdStepSessionSetEnv('BAR', 'BAZ'),
                              tdStepSessionCheckEnv([ 'FOO=BAR', 'BAR=BAZ',]),
                            ]),
        ];
        # Leading '=' in the name is okay for windows guests in 6.1 and later (for driver letter CWDs).
        if (self.oTstDrv.fpApiVer < 6.1 and self.oTstDrv.fpApiVer >= 5.0) or not oTestVm.isWindows():
            aoTests.append(tdTestSessionEx([tdStepSessionSetEnv('=', '===', vbox.ComError.E_INVALIDARG),
                                            tdStepSessionCheckEnv(),
                                            tdStepSessionSetEnv('=FOO', 'BAR', vbox.ComError.E_INVALIDARG),
                                            tdStepSessionCheckEnv(),
                                            tdStepSessionBulkEnv(['=', 'foo=bar'], vbox.ComError.E_INVALIDARG),
                                            tdStepSessionCheckEnv(),
                                            tdStepSessionBulkEnv(['=FOO', 'foo=bar'], vbox.ComError.E_INVALIDARG),
                                            tdStepSessionCheckEnv(),
                                            tdStepSessionBulkEnv(['=D:=D:/tmp', 'foo=bar'], vbox.ComError.E_INVALIDARG),
                                            tdStepSessionCheckEnv(),
                                            tdStepSessionSetEnv('=D:', 'D:/temp', vbox.ComError.E_INVALIDARG),
                                            tdStepSessionCheckEnv(),
                                            ]));
        elif self.oTstDrv.fpApiVer >= 6.1 and oTestVm.isWindows():
            aoTests.append(tdTestSessionEx([tdStepSessionSetEnv('=D:', 'D:/tmp'),
                                            tdStepSessionCheckEnv(['=D:=D:/tmp',]),
                                            tdStepSessionBulkEnv(['=D:=D:/temp', '=FOO', 'foo=bar']),
                                            tdStepSessionCheckEnv(['=D:=D:/temp', '=FOO', 'foo=bar']),
                                            tdStepSessionUnsetEnv('=D:'),
                                            tdStepSessionCheckEnv(['=D:', '=FOO', 'foo=bar']),
                                            ]));

        return tdTestSessionEx.executeListTestSessions(aoTests, self.oTstDrv, oSession, oTxsSession, oTestVm, 'SessionEnv');

    def testGuestCtrlSessionSimple(self, oSession, oTxsSession, oTestVm):
        """
        Tests simple session-based API calls.
        """

        reporter.log('Testing simple session-based API calls ...');

        # Start a valid session.
        aeWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start ];
        try:
            oCreds = tdCtxCreds();
            oCreds.applyDefaultsIfNotSet(oTestVm);
            oGuest        = oSession.o.console.guest;
            oGuestSession = oGuest.createSession(oCreds.sUser, oCreds.sPassword, oCreds.sDomain, "testGuestCtrlSessionSimple");
            oGuestSession.waitForArray(aeWaitFor, 30 * 1000);
        except:
            reporter.logXcpt('Starting session for session-based API calls failed');
            return False;

        fRc = True;

        # Note: Starting with r161502 this should be fixed.
        #       Session 0 separation started with Windows Vista, so skip everything older.
        if  self.oTstDrv.uRevision >= 161502 \
        and oTestVm.isWindows() \
        and oTestVm.sKind not in ('WindowsNT3x', 'WindowsNT4', 'Windows2000', 'WindowsXP'):
            reporter.testStart('Windows guest processes in session >= 1');
            # Test in which Windows session Guest Control processes are being started.
            # We don't want them to be started in session 0, as this would prevent desktop interaction and other stuff.
            fRc, eExitStatus, iExitCode, _, _, _ = \
                self.executeGstCtlHelper(oTxsSession, oGuestSession, [ "show", "win-session-id" ]);
            if  fRc \
            and eExitStatus == vboxcon.ProcessStatus_TerminatedNormally:
                if iExitCode >= 1000: # We report 1000 + <session ID> as exit code.
                    uSessionId = iExitCode - 1000;
                    if uSessionId >= 1:
                        reporter.log('Guest processes start in session %d, good' % (uSessionId));
                    else:
                        reporter.error('Guest processes start in session 0, expected session >= 1');
                else:
                    reporter.error('Guest Control Helper returned error %d (exit code)' % (iExitCode));
            reporter.testDone();

        # User home.
        if self.oTstDrv.fpApiVer >= 7.0:
            reporter.log('Getting user\'s home directory ...');
            try:
                reporter.log('User home directory: %s' % (oGuestSession.userHome));
            except:
                fRc = reporter.logXcpt('Getting user home directory failed');

        # User documents.
        if self.oTstDrv.fpApiVer >= 7.0:
            reporter.log('Getting user\'s documents directory ...');
            try:
                reporter.log('User documents directory: %s' % (oGuestSession.userDocuments));
            except:
                fRc = reporter.logXcpt('Getting user documents directory failed');

        # Mount points. Only available for Guest Additions >= 7.1.
        if self.oTstDrv.fpApiVer >= 7.1:
            reporter.log('Getting mount points ...');
            try:
                aMountpoints = oGuestSession.getMountPoints();
                reporter.log('Got %ld mount points' % len(aMountpoints))
                for mountPoint in aMountpoints:
                    reporter.log('Mountpoint: %s' % (mountPoint));
            except:
                fRc = reporter.logXcpt('Getting mount points failed');

        # Close the session.
        try:
            oGuestSession.close();
        except:
            fRc = reporter.errorXcpt('Closing guest session failed');

        return fRc;

    def testGuestCtrlSession(self, oSession, oTxsSession, oTestVm):
        """
        Tests the guest session handling.
        """

        #
        # Tests:
        #
        atTests = [
            # Invalid parameters.
            [ tdTestSession(sUser = ''),                                        tdTestResultSession() ],
            # User account without a passwort - forbidden.
            [ tdTestSession(sPassword = "" ),                                   tdTestResultSession() ],
            # Various wrong credentials.
            # Note! Only windows cares about sDomain, the other guests ignores it.
            # Note! On Guest Additions < 4.3 this always succeeds because these don't
            #       support creating dedicated sessions. Instead, guest process creation
            #       then will fail. See note below.
            [ tdTestSession(sPassword = 'bar'),                                 tdTestResultSession() ],
            [ tdTestSession(sUser = 'foo', sPassword = 'bar'),                  tdTestResultSession() ],
            [ tdTestSession(sPassword = 'bar', sDomain = 'boo'),                tdTestResultSession() ],
            [ tdTestSession(sUser = 'foo', sPassword = 'bar', sDomain = 'boo'), tdTestResultSession() ],
        ];
        if oTestVm.isWindows(): # domain is ignored elsewhere.
            atTests.append([ tdTestSession(sDomain = 'boo'),                    tdTestResultSession() ]);

        # Finally, correct credentials.
        atTests.append([ tdTestSession(),           tdTestResultSession(fRc = True, cNumSessions = 1) ]);

        #
        # Run the tests.
        #
        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0] # type: tdTestSession
            oCurRes  = tTest[1] # type: tdTestResult

            fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            reporter.log('Testing #%d, user="%s", sPassword="%s", sDomain="%s" ...'
                         % (i, oCurTest.oCreds.sUser, oCurTest.oCreds.sPassword, oCurTest.oCreds.sDomain));
            sCurGuestSessionName = 'testGuestCtrlSession: Test #%d' % (i,);
            fRc2, oCurGuestSession = oCurTest.createSession(sCurGuestSessionName, fIsError = oCurRes.fRc);

            # See note about < 4.3 Guest Additions above.
            uProtocolVersion = 2;
            if oCurGuestSession is not None:
                try:
                    uProtocolVersion = oCurGuestSession.protocolVersion;
                except:
                    fRc = reporter.errorXcpt('Test #%d' % (i,));

            if uProtocolVersion >= 2 and fRc2 is not oCurRes.fRc:
                fRc = reporter.error('Test #%d failed: Session creation failed: Got %s, expected %s' % (i, fRc2, oCurRes.fRc,));

            if fRc2 and oCurGuestSession is None:
                fRc = reporter.error('Test #%d failed: no session object' % (i,));
                fRc2 = False;

            if fRc2:
                if uProtocolVersion >= 2: # For Guest Additions < 4.3 getSessionCount() always will return 1.
                    cCurSessions = oCurTest.getSessionCount(self.oTstDrv.oVBoxMgr);
                    if cCurSessions != oCurRes.cNumSessions:
                        fRc = reporter.error('Test #%d failed: Session count does not match: Got %d, expected %d'
                                             % (i, cCurSessions, oCurRes.cNumSessions));
                try:
                    sObjName = oCurGuestSession.name;
                except:
                    fRc = reporter.errorXcpt('Test #%d' % (i,));
                else:
                    if sObjName != sCurGuestSessionName:
                        fRc = reporter.error('Test #%d failed: Session name does not match: Got "%s", expected "%s"'
                                             % (i, sObjName, sCurGuestSessionName));
            fRc2 = oCurTest.closeSession();
            if fRc2 is False:
                fRc = reporter.error('Test #%d failed: Session could not be closed' % (i,));

        if fRc is False:
            return (False, oTxsSession);

        #
        # Multiple sessions.
        #
        cMaxGuestSessions = 31; # Maximum number of concurrent guest session allowed.
                                # Actually, this is 32, but we don't test session 0.
        aoMultiSessions = {};
        reporter.log2('Opening multiple guest tsessions at once ...');
        for i in xrange(cMaxGuestSessions + 1):
            aoMultiSessions[i] = tdTestSession(sSessionName = 'MultiSession #%d' % (i,));
            fRc = aoMultiSessions[i].setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;

            cCurSessions = aoMultiSessions[i].getSessionCount(self.oTstDrv.oVBoxMgr);
            reporter.log2('MultiSession test #%d count is %d' % (i, cCurSessions));
            if cCurSessions != i:
                return (reporter.error('MultiSession count is %d, expected %d' % (cCurSessions, i)), oTxsSession);
            fRc2, _ = aoMultiSessions[i].createSession('MultiSession #%d' % (i,), i < cMaxGuestSessions);
            if fRc2 is not True:
                if i < cMaxGuestSessions:
                    return (reporter.error('MultiSession #%d test failed' % (i,)), oTxsSession);
                reporter.log('MultiSession #%d exceeded concurrent guest session count, good' % (i,));
                break;

        cCurSessions = aoMultiSessions[i].getSessionCount(self.oTstDrv.oVBoxMgr);
        if cCurSessions is not cMaxGuestSessions:
            return (reporter.error('Final session count %d, expected %d ' % (cCurSessions, cMaxGuestSessions,)), oTxsSession);

        reporter.log2('Closing MultiSessions ...');
        for i in xrange(cMaxGuestSessions):
            # Close this session:
            oClosedGuestSession = aoMultiSessions[i].oGuestSession;
            fRc2 = aoMultiSessions[i].closeSession();
            cCurSessions = aoMultiSessions[i].getSessionCount(self.oTstDrv.oVBoxMgr)
            reporter.log2('MultiSession #%d count is %d' % (i, cCurSessions,));
            if fRc2 is False:
                fRc = reporter.error('Closing MultiSession #%d failed' % (i,));
            elif cCurSessions != cMaxGuestSessions - (i + 1):
                fRc = reporter.error('Expected %d session after closing #%d, got %d instead'
                                     % (cMaxGuestSessions - (i + 1), cCurSessions, i,));
            assert aoMultiSessions[i].oGuestSession is None or not fRc2;
            ## @todo any way to check that the session is closed other than the 'sessions' attribute?

            # Try check that none of the remaining sessions got closed.
            try:
                aoGuestSessions = self.oTstDrv.oVBoxMgr.getArray(atTests[0][0].oGuest, 'sessions');
            except:
                return (reporter.errorXcpt('i=%d/%d' % (i, cMaxGuestSessions,)), oTxsSession);
            if oClosedGuestSession in aoGuestSessions:
                fRc = reporter.error('i=%d/%d: %s should not be in %s'
                                     % (i, cMaxGuestSessions, oClosedGuestSession, aoGuestSessions));
            if i + 1 < cMaxGuestSessions: # Not sure what xrange(2,2) does...
                for j in xrange(i + 1, cMaxGuestSessions):
                    if aoMultiSessions[j].oGuestSession not in aoGuestSessions:
                        fRc = reporter.error('i=%d/j=%d/%d: %s should be in %s'
                                             % (i, j, cMaxGuestSessions, aoMultiSessions[j].oGuestSession, aoGuestSessions));
                    ## @todo any way to check that they work?

        ## @todo Test session timeouts.

        fRc2 = self.testGuestCtrlSessionSimple(oSession, oTxsSession, oTestVm);
        if fRc:
            fRc = fRc2;

        return (fRc, oTxsSession);

    def testGuestCtrlSessionFileRefs(self, oSession, oTxsSession, oTestVm):
        """
        Tests the guest session file reference handling.
        """

        # Find a file to play around with:
        sFile = self.oTstDrv.getGuestSystemFileForReading(oTestVm);

        # Use credential defaults.
        oCreds = tdCtxCreds();
        oCreds.applyDefaultsIfNotSet(oTestVm);

        # Number of stale guest files to create.
        cStaleFiles = 10;

        #
        # Start a session.
        #
        aeWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start ];
        try:
            oGuest        = oSession.o.console.guest;
            oGuestSession = oGuest.createSession(oCreds.sUser, oCreds.sPassword, oCreds.sDomain, "testGuestCtrlSessionFileRefs");
            eWaitResult   = oGuestSession.waitForArray(aeWaitFor, 30 * 1000);
        except:
            return (reporter.errorXcpt(), oTxsSession);

        # Be nice to Guest Additions < 4.3: They don't support session handling and therefore return WaitFlagNotSupported.
        if eWaitResult not in (vboxcon.GuestSessionWaitResult_Start, vboxcon.GuestSessionWaitResult_WaitFlagNotSupported):
            return (reporter.error('Session did not start successfully - wait error: %d' % (eWaitResult,)), oTxsSession);
        reporter.log('Session successfully started');

        #
        # Open guest files and "forget" them (stale entries).
        # For them we don't have any references anymore intentionally.
        #
        reporter.log2('Opening stale files');
        fRc = True;
        for i in xrange(0, cStaleFiles):
            try:
                if self.oTstDrv.fpApiVer >= 5.0:
                    oGuestSession.fileOpen(sFile, vboxcon.FileAccessMode_ReadOnly, vboxcon.FileOpenAction_OpenExisting, 0);
                else:
                    oGuestSession.fileOpen(sFile, "r", "oe", 0);
                # Note: Use a timeout in the call above for not letting the stale processes
                #       hanging around forever.  This can happen if the installed Guest Additions
                #       do not support terminating guest processes.
            except:
                fRc = reporter.errorXcpt('Opening stale file #%d failed:' % (i,));
                break;

        try:    cFiles = len(self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'files'));
        except: fRc = reporter.errorXcpt();
        else:
            if cFiles != cStaleFiles:
                fRc = reporter.error('Got %d stale files, expected %d' % (cFiles, cStaleFiles));

        if fRc is True:
            #
            # Open non-stale files and close them again.
            #
            reporter.log2('Opening non-stale files');
            aoFiles = [];
            for i in xrange(0, cStaleFiles):
                try:
                    if self.oTstDrv.fpApiVer >= 5.0:
                        oCurFile = oGuestSession.fileOpen(sFile, vboxcon.FileAccessMode_ReadOnly,
                                                          vboxcon.FileOpenAction_OpenExisting, 0);
                    else:
                        oCurFile = oGuestSession.fileOpen(sFile, "r", "oe", 0);
                    aoFiles.append(oCurFile);
                except:
                    fRc = reporter.errorXcpt('Opening non-stale file #%d failed:' % (i,));
                    break;

            # Check the count.
            try:    cFiles = len(self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'files'));
            except: fRc = reporter.errorXcpt();
            else:
                if cFiles != cStaleFiles * 2:
                    fRc = reporter.error('Got %d total files, expected %d' % (cFiles, cStaleFiles * 2));

            # Close them.
            reporter.log2('Closing all non-stale files again ...');
            for i, oFile in enumerate(aoFiles):
                try:
                    oFile.close();
                except:
                    fRc = reporter.errorXcpt('Closing non-stale file #%d failed:' % (i,));

            # Check the count again.
            try:    cFiles = len(self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'files'));
            except: fRc = reporter.errorXcpt();
            # Here we count the stale files (that is, files we don't have a reference
            # anymore for) and the opened and then closed non-stale files (that we still keep
            # a reference in aoFiles[] for).
            if cFiles != cStaleFiles:
                fRc = reporter.error('Got %d total files, expected %d' % (cFiles, cStaleFiles));

            #
            # Check that all (referenced) non-stale files are now in the "closed" state.
            #
            reporter.log2('Checking statuses of all non-stale files ...');
            for i, oFile in enumerate(aoFiles):
                try:
                    eFileStatus = aoFiles[i].status;
                except:
                    fRc = reporter.errorXcpt('Checking status of file #%d failed:' % (i,));
                else:
                    if eFileStatus != vboxcon.FileStatus_Closed:
                        fRc = reporter.error('Non-stale file #%d has status %d, expected %d'
                                             % (i, eFileStatus, vboxcon.FileStatus_Closed));

        if fRc is True:
            reporter.log2('All non-stale files closed');

        try:    cFiles = len(self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'files'));
        except: fRc = reporter.errorXcpt();
        else:   reporter.log2('Final guest session file count: %d' % (cFiles,));

        #
        # Now try to close the session and see what happens.
        # Note! Session closing is why we've been doing all the 'if fRc is True' stuff above rather than returning.
        #
        reporter.log2('Closing guest session ...');
        try:
            oGuestSession.close();
        except:
            fRc = reporter.errorXcpt('Testing for stale processes failed:');

        return (fRc, oTxsSession);

    #def testGuestCtrlSessionDirRefs(self, oSession, oTxsSession, oTestVm):
    #    """
    #    Tests the guest session directory reference handling.
    #    """

    #    fRc = True;
    #    return (fRc, oTxsSession);

    def testGuestCtrlSessionProcRefs(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals,too-many-statements
        """
        Tests the guest session process reference handling.
        """

        sShell = self.oTstDrv.getGuestSystemShell(oTestVm);
        asArgs = [sShell,];

        # Use credential defaults.
        oCreds = tdCtxCreds();
        oCreds.applyDefaultsIfNotSet(oTestVm);

        # Number of guest processes per group to create.
        cProcsPerGroup = 10;

        #
        # Start a session.
        #
        aeWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start ];
        try:
            oGuest        = oSession.o.console.guest;
            oGuestSession = oGuest.createSession(oCreds.sUser, oCreds.sPassword, oCreds.sDomain, "testGuestCtrlSessionProcRefs");
            eWaitResult   = oGuestSession.waitForArray(aeWaitFor, 30 * 1000);
        except:
            return (reporter.errorXcpt(), oTxsSession);

        # Be nice to Guest Additions < 4.3: They don't support session handling and therefore return WaitFlagNotSupported.
        if eWaitResult not in (vboxcon.GuestSessionWaitResult_Start, vboxcon.GuestSessionWaitResult_WaitFlagNotSupported):
            return (reporter.error('Session did not start successfully - wait error: %d' % (eWaitResult,)), oTxsSession);
        reporter.log('Session successfully started');

        #
        # Fire off forever-running processes and "forget" them (stale entries).
        # For them we don't have any references anymore intentionally.
        #
        reporter.log('Starting stale processes...');
        fRc = True;
        for i in xrange(0, cProcsPerGroup):
            try:
                reporter.log2('Starting stale process #%d...' % (i));
                self.processCreateWrapper(oGuestSession, sShell, asArgs, "", # Working directory.
                                          [], # Environment changes.
                                          [ vboxcon.ProcessCreateFlag_WaitForStdOut ], 30 * 1000);
                # Note: Not keeping a process reference from the created process above is intentional and part of the test!

                # Note: Use a timeout in the call above for not letting the stale processes
                #       hanging around forever.  This can happen if the installed Guest Additions
                #       do not support terminating guest processes.
            except:
                fRc = reporter.errorXcpt('Creating stale process #%d failed:' % (i,));
                break;

        if fRc:
            reporter.log2('Starting stale processes successful');
            try:    cProcs = len(self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'processes'));
            except: fRc    = reporter.errorXcpt();
            else:
                reporter.log2('Proccess count is: %d' % (cProcs));
                if cProcs != cProcsPerGroup:
                    fRc = reporter.error('Got %d stale processes, expected %d (stale)' % (cProcs, cProcsPerGroup));

        if fRc:
            #
            # Fire off non-stale processes and wait for termination.
            #
            if oTestVm.isWindows() or oTestVm.isOS2():
                asArgs = [ sShell, '/C', 'dir', '/S', self.oTstDrv.getGuestSystemDir(oTestVm), ];
            else:
                asArgs = [ sShell, '-c', 'ls -la ' + self.oTstDrv.getGuestSystemDir(oTestVm), ];
            reporter.log('Starting non-stale processes...');
            aoProcs = [];
            for i in xrange(0, cProcsPerGroup):
                try:
                    reporter.log2('Starting non-stale process #%d...' % (i));
                    oCurProc = self.processCreateWrapper(oGuestSession, sShell, asArgs,
                                                         "",         # Working directory.
                                                         [], [], 0); # Infinite timeout.
                    aoProcs.append(oCurProc);
                except:
                    fRc = reporter.errorXcpt('Creating non-stale process #%d failed:' % (i,));
                    break;

            try:    cProcs = len(self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'processes'));
            except: fRc    = reporter.errorXcpt();
            else:
                reporter.log2('Proccess count is: %d' % (cProcs));

            reporter.log('Waiting for non-stale processes to terminate...');
            for i, oProcess in enumerate(aoProcs):
                try:
                    reporter.log('Waiting for non-stale process #%d...' % (i));
                    eWaitResult = oProcess.waitForArray([ vboxcon.ProcessWaitForFlag_Terminate, ], 30 * 1000);
                    eProcessStatus = oProcess.status;
                except:
                    fRc = reporter.errorXcpt('Waiting for non-stale process #%d failed:' % (i,));
                else:
                    if eProcessStatus != vboxcon.ProcessStatus_TerminatedNormally:
                        fRc = reporter.error('Waiting for non-stale processes #%d resulted in status %d, expected %d (wr=%d)'
                                             % (i, eProcessStatus, vboxcon.ProcessStatus_TerminatedNormally, eWaitResult));
            if fRc:
                reporter.log('All non-stale processes ended successfully');

            try:    cProcs = len(self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'processes'));
            except: fRc    = reporter.errorXcpt();
            else:
                reporter.log2('Proccess count is: %d' % (cProcs));

                # Here we count the stale processes (that is, processes we don't have a reference
                # anymore for) and the started + ended non-stale processes (that we still keep
                # a reference in aoProcesses[] for).
                cProcsExpected = cProcsPerGroup * 2;
                if cProcs != cProcsExpected:
                    fRc = reporter.error('Got %d total processes, expected %d (stale vs. non-stale)' \
                                         % (cProcs, cProcsExpected));
            #
            # Fire off non-stale blocking processes which are terminated via terminate().
            #
            if oTestVm.isWindows() or oTestVm.isOS2():
                sCmd   = sShell;
                asArgs = [ sCmd, '/C', 'pause'];
            else:
                sCmd   = '/usr/bin/yes';
                asArgs = [ sCmd ];
            reporter.log('Starting blocking processes...');
            aoProcs = [];
            for i in xrange(0, cProcsPerGroup):
                try:
                    reporter.log2('Starting blocking process #%d...' % (i));
                    oCurProc = self.processCreateWrapper(oGuestSession, sCmd, asArgs,
                                                         "", # Working directory.
                                                         [],  [], 30 * 1000);

                    # Note: Use a timeout in the call above for not letting the stale processes
                    #       hanging around forever.  This can happen if the installed Guest Additions
                    #       do not support terminating guest processes.
                    try:
                        reporter.log('Waiting for blocking process #%d getting started...' % (i));
                        eWaitResult = oCurProc.waitForArray([ vboxcon.ProcessWaitForFlag_Start, ], 30 * 1000);
                        eProcessStatus = oCurProc.status;
                    except:
                        fRc = reporter.errorXcpt('Waiting for blocking process #%d failed:' % (i,));
                    else:
                        if eProcessStatus != vboxcon.ProcessStatus_Started:
                            fRc = reporter.error('Waiting for blocking processes #%d resulted in status %d, expected %d (wr=%d)'
                                                 % (i, eProcessStatus, vboxcon.ProcessStatus_Started, eWaitResult));
                    aoProcs.append(oCurProc);
                except:
                    fRc = reporter.errorXcpt('Creating blocking process #%d failed:' % (i,));
                    break;

            if fRc:
                reporter.log2('Starting blocking processes successful');

            reporter.log2('Terminating blocking processes...');
            for i, oProcess in enumerate(aoProcs):
                try:
                    reporter.log('Terminating blocking process #%d...' % (i));
                    oProcess.terminate();
                except: # Termination might not be supported, just skip and log it.
                    reporter.logXcpt('Termination of blocking process #%d failed, skipped:' % (i,));
                    if self.oTstDrv.fpApiVer >= 6.1: # Termination is supported since 5.2 or so.
                        fRc = False;
            if fRc:
                reporter.log('All blocking processes were terminated successfully');

            try:    cProcs = len(self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'processes'));
            except: fRc    = reporter.errorXcpt();
            else:
                # There still should be 20 processes because we just terminated the 10 blocking ones above.
                cProcsExpected = cProcsPerGroup * 2;
                if cProcs != cProcsExpected:
                    fRc = reporter.error('Got %d total processes, expected %d (final)' % (cProcs, cProcsExpected));
                reporter.log2('Final guest session processes count: %d' % (cProcs,));

        if not fRc:
            aoProcs = self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'processes');
            for i, oProc in enumerate(aoProcs):
                try:
                    aoArgs = self.oTstDrv.oVBoxMgr.getArray(oProc, 'arguments');
                    reporter.log('Process %d (\'%s\') still around, status is %d' \
                                 % (i, ' '.join([str(x) for x in aoArgs]), oProc.status));
                except:
                    reporter.errorXcpt('Process lookup failed:');
        #
        # Now try to close the session and see what happens.
        #
        reporter.log('Closing guest session ...');
        try:
            oGuestSession.close();
        except:
            fRc = reporter.errorXcpt('Closing session for testing process references failed:');

        return (fRc, oTxsSession);

    def testGuestCtrlExec(self, oSession, oTxsSession, oTestVm):                # pylint: disable=too-many-locals,too-many-statements
        """
        Tests the basic execution feature.
        """

        # Paths:
        sVBoxControl    = None; # Only available on supported Windows guests.
        sShell          = self.oTstDrv.getGuestSystemShell(oTestVm);
        sShellOpt       = '/C' if oTestVm.isWindows() or oTestVm.isOS2() else '-c';
        sSystemDir      = self.oTstDrv.getGuestSystemDir(oTestVm);
        sTempDir        = self.oTstDrv.getGuestTempDir(oTestVm);
        sFileForReading = self.oTstDrv.getGuestSystemFileForReading(oTestVm);
        if oTestVm.isWindows() or oTestVm.isOS2():
            sImageOut = self.oTstDrv.getGuestSystemShell(oTestVm);
            if oTestVm.isWindows():
                sVBoxControl = oTestVm.pathJoin(self.oTstDrv.getGuestSystemAdminDir(oTestVm), 'VBoxControl.exe');
        else:
            # Really old guests (like OL 6) have their coreutils in /bin instead of /usr/bin.
            if self.oTstDrv.txsIsFile(oSession, oTxsSession, "/bin/ls", fIgnoreErrors = True):
                sImageOut = "/bin/ls";
            else:
                sImageOut = oTestVm.pathJoin(self.oTstDrv.getGuestSystemDir(oTestVm), 'ls');
            if oTestVm.isLinux(): ## @todo check solaris and darwin.
                sVBoxControl = "/usr/bin/VBoxControl"; # Symlink

        # Use credential defaults.
        oCreds = tdCtxCreds();
        oCreds.applyDefaultsIfNotSet(oTestVm);

        atInvalid = [
            # Invalid parameters.
            [ tdTestExec(), tdTestResultExec() ],
            # Non-existent / invalid image.
            [ tdTestExec(sCmd = "non-existent"), tdTestResultExec() ],
            [ tdTestExec(sCmd = "non-existent2"), tdTestResultExec() ],
            # Use an invalid format string.
            [ tdTestExec(sCmd = "%$%%%&"), tdTestResultExec() ],
            # More stuff.
            [ tdTestExec(sCmd = u"ƒ‰‹ˆ÷‹¸"), tdTestResultExec() ],
            [ tdTestExec(sCmd = "???://!!!"), tdTestResultExec() ],
            [ tdTestExec(sCmd = "<>!\\"), tdTestResultExec() ],
            # Enable as soon as ERROR_BAD_DEVICE is implemented.
            #[ tdTestExec(sCmd = "CON", tdTestResultExec() ],
        ];

        atExec = [];
        if oTestVm.isWindows() or oTestVm.isOS2():
            if oTestVm.sKind == 'WindowsNT4':
                # For whatever reason NT4 SP6 (tst-nt4sp6) returns exit code 2 for existing *and* non-existing files.
                # I've manually checked that on the VM itself, so this is *not* a bug in the Guest Control code.
                # So we have to tweak the expected exit codes here in order to make the following tests pass.
                iExitCodeForExistingFiles    = 2
                iExitCodeForNonExistingFiles = 2
            else:
                iExitCodeForExistingFiles    = 0
                iExitCodeForNonExistingFiles = 1
            atExec += [
                # Basic execution.
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', sSystemDir ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', sFileForReading ]),
                  tdTestResultExec(fRc = True, iExitCode = iExitCodeForExistingFiles) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', sSystemDir + '\\nonexist.dll' ]),
                  tdTestResultExec(fRc = True, iExitCode = iExitCodeForNonExistingFiles) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', '/wrongparam' ]),
                  tdTestResultExec(fRc = True, iExitCode = 1) ],
                [ tdTestExec(sCmd = sShell, asArgs = [ sShell, sShellOpt, 'wrongcommand' ]),
                  tdTestResultExec(fRc = True, iExitCode = 1) ],
                # StdOut.
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', sSystemDir ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', 'stdout-non-existing' ]),
                  tdTestResultExec(fRc = True, iExitCode = iExitCodeForNonExistingFiles) ],
                # StdErr.
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', sSystemDir ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', 'stderr-non-existing' ]),
                  tdTestResultExec(fRc = True, iExitCode = iExitCodeForNonExistingFiles) ],
                # StdOut + StdErr.
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', sSystemDir ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'dir', '/S', 'stdouterr-non-existing' ]),
                  tdTestResultExec(fRc = True, iExitCode = iExitCodeForNonExistingFiles) ],
            ];

            if self.oTstDrv.isVersionEqualOrBigger(self.tpAdditionsVer, "7.1.0"):
                atExec.extend([
                    # Current working directory set (VBox >= 7.1).
                    [ tdTestExec(sCmd = sImageOut, sCwd = sTempDir, asArgs = [ sImageOut, '/C', 'dir', '/S', sSystemDir ]),
                      tdTestResultExec(fRc = True) ]
                ]);

            # atExec.extend([
                # FIXME: Failing tests.
                # Environment variables.
                # [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'set', 'TEST_NONEXIST' ],
                #   tdTestResultExec(fRc = True, iExitCode = 1) ]
                # [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'set', 'windir' ],
                #              afFlags = [ vboxcon.ProcessCreateFlag_WaitForStdOut, vboxcon.ProcessCreateFlag_WaitForStdErr ]),
                #   tdTestResultExec(fRc = True, sBuf = 'windir=C:\\WINDOWS\r\n') ],
                # [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'set', 'TEST_FOO' ],
                #              aEnv = [ 'TEST_FOO=BAR' ],
                #              afFlags = [ vboxcon.ProcessCreateFlag_WaitForStdOut, vboxcon.ProcessCreateFlag_WaitForStdErr ]),
                #   tdTestResultExec(fRc = True, sBuf = 'TEST_FOO=BAR\r\n') ],
                # [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'set', 'TEST_FOO' ],
                #              aEnv = [ 'TEST_FOO=BAR', 'TEST_BAZ=BAR' ],
                #              afFlags = [ vboxcon.ProcessCreateFlag_WaitForStdOut, vboxcon.ProcessCreateFlag_WaitForStdErr ]),
                #   tdTestResultExec(fRc = True, sBuf = 'TEST_FOO=BAR\r\n') ]

                ## @todo Create some files (or get files) we know the output size of to validate output length!
                ## @todo Add task which gets killed at some random time while letting the guest output something.
            #];
        else:
            atExec += [
                # Basic execution.
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '-R', sSystemDir ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, sFileForReading ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '--wrong-parameter' ]),
                  tdTestResultExec(fRc = True, iExitCode = 2) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/non/existent' ]),
                  tdTestResultExec(fRc = True, iExitCode = 2) ],
                [ tdTestExec(sCmd = sShell, asArgs = [ sShell, sShellOpt, 'wrongcommand' ]),
                  tdTestResultExec(fRc = True, iExitCode = 127) ],
                # StdOut.
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, sSystemDir ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, 'stdout-non-existing' ]),
                  tdTestResultExec(fRc = True, iExitCode = 2) ],
                # StdErr.
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, sSystemDir ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, 'stderr-non-existing' ]),
                  tdTestResultExec(fRc = True, iExitCode = 2) ],
                # StdOut + StdErr.
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, sSystemDir ]),
                  tdTestResultExec(fRc = True) ],
                [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, 'stdouterr-non-existing' ]),
                  tdTestResultExec(fRc = True, iExitCode = 2) ],
            ];

            if self.oTstDrv.isVersionEqualOrBigger(self.tpAdditionsVer, "7.1.0"):
                atExec.extend([
                    # Current working directory set (VBox >= 7.1).
                    [ tdTestExec(sCmd = sImageOut, sCwd = sTempDir, asArgs = [ sImageOut, '-R', sSystemDir ]),
                      tdTestResultExec(fRc = True) ]
                ]);

            # atExec.extend([
                # FIXME: Failing tests.
                # Environment variables.
                # [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'set', 'TEST_NONEXIST' ],
                #   tdTestResultExec(fRc = True, iExitCode = 1) ]
                # [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'set', 'windir' ],
                #
                #              afFlags = [ vboxcon.ProcessCreateFlag_WaitForStdOut, vboxcon.ProcessCreateFlag_WaitForStdErr ]),
                #   tdTestResultExec(fRc = True, sBuf = 'windir=C:\\WINDOWS\r\n') ],
                # [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'set', 'TEST_FOO' ],
                #              aEnv = [ 'TEST_FOO=BAR' ],
                #              afFlags = [ vboxcon.ProcessCreateFlag_WaitForStdOut, vboxcon.ProcessCreateFlag_WaitForStdErr ]),
                #   tdTestResultExec(fRc = True, sBuf = 'TEST_FOO=BAR\r\n') ],
                # [ tdTestExec(sCmd = sImageOut, asArgs = [ sImageOut, '/C', 'set', 'TEST_FOO' ],
                #              aEnv = [ 'TEST_FOO=BAR', 'TEST_BAZ=BAR' ],
                #              afFlags = [ vboxcon.ProcessCreateFlag_WaitForStdOut, vboxcon.ProcessCreateFlag_WaitForStdErr ]),
                #   tdTestResultExec(fRc = True, sBuf = 'TEST_FOO=BAR\r\n') ]

                ## @todo Create some files (or get files) we know the output size of to validate output length!
                ## @todo Add task which gets killed at some random time while letting the guest output something.
            #];

        #
        #for iExitCode in xrange(0, 127):
        #    atExec.append([ tdTestExec(sCmd = sShell, asArgs = [ sShell, sShellOpt, 'exit %s' % iExitCode ]),
        #                    tdTestResultExec(fRc = True, iExitCode = iExitCode) ]);

        if  sVBoxControl \
        and self.oTstDrv.fpApiVer >= 6.0: # Investigate with this doesn't work on (<) 5.2.
            # Paths with spaces on windows.
            atExec.append([ tdTestExec(sCmd = sVBoxControl, asArgs = [ sVBoxControl, 'version' ],
                                       afFlags = [ vboxcon.ProcessCreateFlag_WaitForStdOut,
                                                   vboxcon.ProcessCreateFlag_WaitForStdErr ]),
                            tdTestResultExec(fRc = True) ]);

        # Test very long arguments. Be careful when tweaking this to not break the tests.
        # Regarding paths:
        # - We have RTPATH_BIG_MAX (64K)
        # - MSDN says 32K for CreateFileW()
        # - On Windows, each path component must not be longer than MAX_PATH (260), see
        #   https://docs.microsoft.com/en-us/windows/win32/fileio/filesystem-functionality-comparison#limits
        #
        # Old(er) Windows OSes tend to crash in cmd.exe, so skip this on these OSes.
        if  self.oTstDrv.fpApiVer >= 6.1 \
        and oTestVm.sKind not in ('WindowsNT4', 'Windows2000', 'WindowsXP', 'Windows2003'):
            sEndMarker = '--end-marker';
            if oTestVm.isWindows() \
            or oTestVm.isOS2():
                sCmd   = sShell;
            else:
                # Really old guests (like OL 6) have their coreutils in /bin instead of /usr/bin.
                if self.oTstDrv.txsIsFile(oSession, oTxsSession, "/bin/echo", fIgnoreErrors = True):
                    sCmd = "/bin/echo";
                else:
                    sCmd = oTestVm.pathJoin(self.oTstDrv.getGuestSystemDir(oTestVm), 'echo');

            for _ in xrange(0, 16):
                if oTestVm.isWindows() \
                or oTestVm.isOS2():
                    asArgs = [ sShell, sShellOpt, "echo" ];
                else:
                    asArgs = [ sCmd ];

                # Append a random number of arguments with random length.
                for _ in xrange(0, self.oTestFiles.oRandom.randrange(1, 64)):
                    asArgs.append(''.join(random.choice(string.ascii_lowercase)
                                          for _ in range(self.oTestFiles.oRandom.randrange(1, 196))));

                asArgs.append(sEndMarker);

                reporter.log2('asArgs=%s (%d args), type=%s' % (limitString(asArgs), len(asArgs), type(asArgs)));

                ## @todo Check limits; on Ubuntu with 256KB IPRT returns VERR_NOT_IMPLEMENTED.
                # Use a higher timeout (15 min) than usual for these long checks.
                atExec.append([ tdTestExec(sCmd = sCmd, asArgs = asArgs,
                                           afFlags = [ vboxcon.ProcessCreateFlag_WaitForStdOut,
                                                       vboxcon.ProcessCreateFlag_WaitForStdErr ],
                                           timeoutMS = 15 * 60 * 1000),
                                tdTestResultExec(fRc = True) ]);

        # Build up the final test array for the first batch.
        atTests = atInvalid + atExec;

        #
        # First batch: One session per guest process.
        #
        reporter.log('One session per guest process ...');
        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0]  # type: tdTestExec
            oCurRes  = tTest[1]  # type: tdTestResultExec
            fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            fRc2, oCurGuestSession = oCurTest.createSession('testGuestCtrlExec: Test #%d' % (i,));
            if fRc2 is not True:
                fRc = reporter.error('Test #%d failed: Could not create session' % (i,));
                break;
            fRc = self.gctrlExecDoTest(i, oCurTest, oCurRes, oCurGuestSession) and fRc;
            fRc = oCurTest.closeSession() and fRc;

        reporter.log('Execution of all tests done, checking for stale sessions');

        # No sessions left?
        try:
            aSessions = self.oTstDrv.oVBoxMgr.getArray(oSession.o.console.guest, 'sessions');
        except:
            fRc = reporter.errorXcpt();
        else:
            cSessions = len(aSessions);
            if cSessions != 0:
                fRc = reporter.error('Found %d stale session(s), expected 0:' % (cSessions,));
                for (i, aSession) in enumerate(aSessions):
                    try:    reporter.log('  Stale session #%d ("%s")' % (aSession.id, aSession.name));
                    except: reporter.errorXcpt();

        if fRc is not True:
            return (fRc, oTxsSession);

        reporter.log('Now using one guest session for all tests ...');

        #
        # Second batch: One session for *all* guest processes.
        #

        # Create session.
        reporter.log('Creating session for all tests ...');
        aeWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start, ];
        try:
            oGuest = oSession.o.console.guest;
            oCurGuestSession = oGuest.createSession(oCreds.sUser, oCreds.sPassword, oCreds.sDomain,
                                                   'testGuestCtrlExec: One session for all tests');
        except:
            return (reporter.errorXcpt(), oTxsSession);

        try:
            eWaitResult = oCurGuestSession.waitForArray(aeWaitFor, 30 * 1000);
        except:
            fRc = reporter.errorXcpt('Waiting for guest session to start failed:');
        else:
            if eWaitResult not in (vboxcon.GuestSessionWaitResult_Start, vboxcon.GuestSessionWaitResult_WaitFlagNotSupported):
                fRc = reporter.error('Session did not start successfully, returned wait result: %d' % (eWaitResult,));
            else:
                reporter.log('Session successfully started');

                # Do the tests within this session.
                for (i, tTest) in enumerate(atTests):
                    oCurTest = tTest[0] # type: tdTestExec
                    oCurRes  = tTest[1] # type: tdTestResultExec

                    fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
                    if not fRc:
                        break;
                    fRc = self.gctrlExecDoTest(i, oCurTest, oCurRes, oCurGuestSession);
                    if fRc is False:
                        break;

            # Close the session.
            reporter.log2('Closing guest session ...');
            try:
                oCurGuestSession.close();
                oCurGuestSession = None;
            except:
                fRc = reporter.errorXcpt('Closing guest session failed:');

            # No sessions left?
            reporter.log('Execution of all tests done, checking for stale sessions again');
            try:    cSessions = len(self.oTstDrv.oVBoxMgr.getArray(oSession.o.console.guest, 'sessions'));
            except: fRc = reporter.errorXcpt();
            else:
                if cSessions != 0:
                    fRc = reporter.error('Found %d stale session(s), expected 0' % (cSessions,));
        return (fRc, oTxsSession);

    def threadForTestGuestCtrlSessionReboot(self, oGuestProcess):
        """
        Thread routine which waits for the stale guest process getting terminated (or some error)
        while the main test routine reboots the guest. It then compares the expected guest process result
        and logs an error if appropriate.
        """
        reporter.log('Waiting for process to get terminated at reboot ...');
        try:
            eWaitResult = oGuestProcess.waitForArray([ vboxcon.ProcessWaitForFlag_Terminate ], 5 * 60 * 1000);
        except:
            return reporter.errorXcpt('waitForArray failed');
        try:
            eStatus = oGuestProcess.status
        except:
            return reporter.errorXcpt('failed to get status (wait result %d)' % (eWaitResult,));

        if eWaitResult == vboxcon.ProcessWaitResult_Terminate and eStatus == vboxcon.ProcessStatus_Down:
            reporter.log('Stale process was correctly terminated (status: down)');
            return True;

        return reporter.error('Process wait across reboot failed: eWaitResult=%d, expected %d; eStatus=%d, expected %d'
                              % (eWaitResult, vboxcon.ProcessWaitResult_Terminate, eStatus, vboxcon.ProcessStatus_Down,));

    def testGuestCtrlSessionReboot(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests guest object notifications when a guest gets rebooted / shutdown.

        These notifications gets sent from the guest sessions in order to make API clients
        aware of guest session changes.

        To test that we create a stale guest process and trigger a reboot of the guest.
        """

        ## @todo backport fixes to 6.0 and maybe 5.2
        if self.oTstDrv.fpApiVer <= 6.0:
            reporter.log('Skipping: Required fixes not yet backported!');
            return None;

        # Use credential defaults.
        oCreds = tdCtxCreds();
        oCreds.applyDefaultsIfNotSet(oTestVm);

        fRebooted = False;
        fRc = True;

        #
        # Start a session.
        #
        aeWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start ];
        try:
            oGuest        = oSession.o.console.guest;
            oGuestSession = oGuest.createSession(oCreds.sUser, oCreds.sPassword, oCreds.sDomain, "testGuestCtrlSessionReboot");
            eWaitResult   = oGuestSession.waitForArray(aeWaitFor, 30 * 1000);
        except:
            return (reporter.errorXcpt(), oTxsSession);

        # Be nice to Guest Additions < 4.3: They don't support session handling and therefore return WaitFlagNotSupported.
        if eWaitResult not in (vboxcon.GuestSessionWaitResult_Start, vboxcon.GuestSessionWaitResult_WaitFlagNotSupported):
            return (reporter.error('Session did not start successfully - wait error: %d' % (eWaitResult,)), oTxsSession);
        reporter.log('Session successfully started');

        #
        # Create a process.
        #
        # That process will also be used later to see if the session cleanup worked upon reboot.
        #
        sImage = self.oTstDrv.getGuestSystemShell(oTestVm);
        asArgs  = [ sImage, ];
        aEnv    = [];
        afFlags = [];
        try:
            oGuestProcess = self.processCreateWrapper(oGuestSession, sImage, asArgs,
                                                      "", # Working directory.
                                                      aEnv, afFlags, 30 * 1000);
        except:
            fRc = reporter.error('Failed to start shell process (%s)' % (sImage,));
        else:
            try:
                eWaitResult = oGuestProcess.waitForArray([ vboxcon.ProcessWaitForFlag_Start ], 30 * 1000);
            except:
                fRc = reporter.errorXcpt('Waiting for shell process (%s) to start failed' % (sImage,));
            else:
                # Check the result and state:
                try:    eStatus = oGuestProcess.status;
                except: fRc = reporter.errorXcpt('Waiting for shell process (%s) to start failed' % (sImage,));
                else:
                    reporter.log2('Starting process wait result returned: %d;  Process status is: %d' % (eWaitResult, eStatus,));
                    if eWaitResult != vboxcon.ProcessWaitResult_Start:
                        fRc = reporter.error('wait for ProcessWaitForFlag_Start failed: %d, expected %d (Start)'
                                             % (eWaitResult, vboxcon.ProcessWaitResult_Start,));
                    elif eStatus != vboxcon.ProcessStatus_Started:
                        fRc = reporter.error('Unexpected process status after startup: %d, wanted %d (Started)'
                                             % (eStatus, vboxcon.ProcessStatus_Started,));
                    else:
                        # Create a thread that waits on the process to terminate
                        reporter.log('Creating reboot thread ...');
                        oThreadReboot = threading.Thread(target = self.threadForTestGuestCtrlSessionReboot,
                                                         args = (oGuestProcess,),
                                                         name = 'threadForTestGuestCtrlSessionReboot');
                        oThreadReboot.setDaemon(True); # pylint: disable=deprecated-method
                        oThreadReboot.start();

                        # Do the reboot.
                        reporter.log('Rebooting guest and reconnecting TXS ...');
                        (oSession, oTxsSession) = self.oTstDrv.txsRebootAndReconnectViaTcp(oSession, oTxsSession,
                                                                                           cMsTimeout = 3 * 60000);
                        if  oSession \
                        and oTxsSession:
                            # Set reboot flag (needed later for session closing).
                            fRebooted = True;
                        else:
                            reporter.error('Rebooting via TXS failed');
                            try:    oGuestProcess.terminate();
                            except: reporter.logXcpt();
                            fRc = False;

                        reporter.log('Waiting for thread to finish ...');
                        oThreadReboot.join();

                        # Check that the guest session now still has the formerly guest process object created above,
                        # but with the "down" status now (because of guest reboot).
                        try:
                            aoGuestProcs = self.oTstDrv.oVBoxMgr.getArray(oGuestSession, 'processes');
                            if len(aoGuestProcs) == 1:
                                enmProcSts = aoGuestProcs[0].status;
                                if enmProcSts != vboxcon.ProcessStatus_Down:
                                    fRc = reporter.error('Old guest process (before reboot) has status %d, expected %s' \
                                                         % (enmProcSts, vboxcon.ProcessStatus_Down));
                            else:
                                fRc = reporter.error('Old guest session (before reboot) has %d processes registered, expected 1' \
                                                     % (len(aoGuestProcs)));
                        except:
                            fRc = reporter.errorXcpt();
            #
            # Try make sure we don't leave with a stale process on failure.
            #
            try:    oGuestProcess.terminate();
            except: reporter.logXcpt();

        #
        # Close the session.
        #
        reporter.log2('Closing guest session ...');
        try:
            oGuestSession.close();
        except:
            # Closing the guest session will fail when the guest reboot has been triggered,
            # as the session object will be cleared on a guest reboot.
            if fRebooted:
                reporter.logXcpt('Closing guest session failed, good (guest rebooted)');
            else: # ... otherwise this (still) should succeed. Report so if it doesn't.
                reporter.errorXcpt('Closing guest session failed');

        return (fRc, oTxsSession);

    def testGuestCtrlExecTimeout(self, oSession, oTxsSession, oTestVm):
        """
        Tests handling of timeouts of started guest processes.
        """

        sShell = self.oTstDrv.getGuestSystemShell(oTestVm);

        # Use credential defaults.
        oCreds = tdCtxCreds();
        oCreds.applyDefaultsIfNotSet(oTestVm);

        #
        # Create a session.
        #
        try:
            oGuest = oSession.o.console.guest;
            oGuestSession = oGuest.createSession(oCreds.sUser, oCreds.sPassword, oCreds.sDomain, "testGuestCtrlExecTimeout");
            eWaitResult = oGuestSession.waitForArray([ vboxcon.GuestSessionWaitForFlag_Start, ], 30 * 1000);
        except:
            return (reporter.errorXcpt(), oTxsSession);

        # Be nice to Guest Additions < 4.3: They don't support session handling and therefore return WaitFlagNotSupported.
        if eWaitResult not in (vboxcon.GuestSessionWaitResult_Start, vboxcon.GuestSessionWaitResult_WaitFlagNotSupported):
            return (reporter.error('Session did not start successfully - wait error: %d' % (eWaitResult,)), oTxsSession);
        reporter.log('Session successfully started');

        #
        # Create a process which never terminates and should timeout when
        # waiting for termination.
        #
        fRc = True;
        try:
            oCurProcess = self.processCreateWrapper(oGuestSession, sShell, [ sShell,] if self.oTstDrv.fpApiVer >= 5.0 else [],
                                                    "", # Working directory.
                                                    [], [], 30 * 1000);
        except:
            fRc = reporter.errorXcpt();
        else:
            reporter.log('Waiting for process 1 being started ...');
            try:
                eWaitResult = oCurProcess.waitForArray([ vboxcon.ProcessWaitForFlag_Start ], 30 * 1000);
            except:
                fRc = reporter.errorXcpt();
            else:
                if eWaitResult != vboxcon.ProcessWaitResult_Start:
                    fRc = reporter.error('Waiting for process 1 to start failed, got status %d' % (eWaitResult,));
                else:
                    for msWait in (1, 32, 2000,):
                        reporter.log('Waiting for process 1 to time out within %sms ...' % (msWait,));
                        try:
                            eWaitResult = oCurProcess.waitForArray([ vboxcon.ProcessWaitForFlag_Terminate, ], msWait);
                        except:
                            fRc = reporter.errorXcpt();
                            break;
                        if eWaitResult != vboxcon.ProcessWaitResult_Timeout:
                            fRc = reporter.error('Waiting for process 1 did not time out in %sms as expected: %d'
                                                 % (msWait, eWaitResult,));
                            break;
                        reporter.log('Waiting for process 1 timed out in %u ms, good' % (msWait,));

                try:
                    oCurProcess.terminate();
                except:
                    reporter.errorXcpt();
            oCurProcess = None;

            #
            # Create another process that doesn't terminate, but which will be killed by VBoxService
            # because it ran out of execution time (3 seconds).
            #
            try:
                oCurProcess = self.processCreateWrapper(oGuestSession, sShell, [sShell,] if self.oTstDrv.fpApiVer >= 5.0 else [],
                                                        "", # Working directory.
                                                        [], [], 3 * 1000);
            except:
                fRc = reporter.errorXcpt();
            else:
                reporter.log('Waiting for process 2 being started ...');
                try:
                    eWaitResult = oCurProcess.waitForArray([ vboxcon.ProcessWaitForFlag_Start ], 30 * 1000);
                except:
                    fRc = reporter.errorXcpt();
                else:
                    if eWaitResult != vboxcon.ProcessWaitResult_Start:
                        fRc = reporter.error('Waiting for process 2 to start failed, got status %d' % (eWaitResult,));
                    else:
                        reporter.log('Waiting for process 2 to get killed for running out of execution time ...');
                        try:
                            eWaitResult = oCurProcess.waitForArray([ vboxcon.ProcessWaitForFlag_Terminate, ], 15 * 1000);
                        except:
                            fRc = reporter.errorXcpt();
                        else:
                            if eWaitResult != vboxcon.ProcessWaitResult_Timeout:
                                fRc = reporter.error('Waiting for process 2 did not time out when it should, got wait result %d'
                                                     % (eWaitResult,));
                            else:
                                reporter.log('Waiting for process 2 did not time out, good: %s' % (eWaitResult,));
                                try:
                                    eStatus = oCurProcess.status;
                                except:
                                    fRc = reporter.errorXcpt();
                                else:
                                    if eStatus != vboxcon.ProcessStatus_TimedOutKilled:
                                        fRc = reporter.error('Status of process 2 wrong; excepted %d, got %d'
                                                             % (vboxcon.ProcessStatus_TimedOutKilled, eStatus));
                                    else:
                                        reporter.log('Status of process 2 is TimedOutKilled (%d) is it should be.'
                                                     % (vboxcon.ProcessStatus_TimedOutKilled,));
                        try:
                            oCurProcess.terminate();
                        except:
                            reporter.logXcpt();
                oCurProcess = None;

        #
        # Clean up the session.
        #
        try:
            oGuestSession.close();
        except:
            fRc = reporter.errorXcpt();

        return (fRc, oTxsSession);

    def testGuestCtrlDirCreate(self, oSession, oTxsSession, oTestVm):
        """
        Tests creation of guest directories.
        """

        sScratch = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), 'testGuestCtrlDirCreate');

        atTests = [
            # Invalid stuff.
            [ tdTestDirCreate(sDirectory = '' ), tdTestResultFailure() ],
            # More unusual stuff.
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin('..', '.') ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin('..', '..') ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = '..' ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = '../' ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = '../../' ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = '/' ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = '/..' ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = '/../' ), tdTestResultFailure() ],
        ];
        if oTestVm.isWindows() or oTestVm.isOS2():
            atTests.extend([
                [ tdTestDirCreate(sDirectory = 'C:\\' ), tdTestResultFailure() ],
                [ tdTestDirCreate(sDirectory = 'C:\\..' ), tdTestResultFailure() ],
                [ tdTestDirCreate(sDirectory = 'C:\\..\\' ), tdTestResultFailure() ],
                [ tdTestDirCreate(sDirectory = 'C:/' ), tdTestResultFailure() ],
                [ tdTestDirCreate(sDirectory = 'C:/.' ), tdTestResultFailure() ],
                [ tdTestDirCreate(sDirectory = 'C:/./' ), tdTestResultFailure() ],
                [ tdTestDirCreate(sDirectory = 'C:/..' ), tdTestResultFailure() ],
                [ tdTestDirCreate(sDirectory = 'C:/../' ), tdTestResultFailure() ],
                [ tdTestDirCreate(sDirectory = '\\\\uncrulez\\foo' ), tdTestResultFailure() ],
            ]);
        atTests.extend([
            # Existing directories and files.
            [ tdTestDirCreate(sDirectory = self.oTstDrv.getGuestSystemDir(oTestVm) ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = self.oTstDrv.getGuestSystemShell(oTestVm) ), tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = self.oTstDrv.getGuestSystemFileForReading(oTestVm) ), tdTestResultFailure() ],
            # Creating directories.
            [ tdTestDirCreate(sDirectory = sScratch ), tdTestResultSuccess() ],
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, 'foo', 'bar', 'baz'),
                              afFlags = (vboxcon.DirectoryCreateFlag_Parents,) ), tdTestResultSuccess() ],
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, 'foo', 'bar', 'baz'),
                              afFlags = (vboxcon.DirectoryCreateFlag_Parents,) ), tdTestResultSuccess() ],
            # Try format strings as directories.
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, 'foo%sbar%sbaz%d' )), tdTestResultSuccess() ],
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, '%f%%boo%%bar%RI32' )), tdTestResultSuccess() ],
            # Long random names.
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, self.oTestFiles.generateFilenameEx(36, 28))),
              tdTestResultSuccess() ],
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, self.oTestFiles.generateFilenameEx(140, 116))),
              tdTestResultSuccess() ],
            # Too long names. ASSUMES a guests has a 255 filename length limitation.
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, self.oTestFiles.generateFilenameEx(2048, 256))),
              tdTestResultFailure() ],
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, self.oTestFiles.generateFilenameEx(2048, 256))),
              tdTestResultFailure() ],
            # Missing directory in path.
            [ tdTestDirCreate(sDirectory = oTestVm.pathJoin(sScratch, 'foo1', 'bar') ), tdTestResultFailure() ],
        ]);

        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0] # type: tdTestDirCreate
            oCurRes  = tTest[1] # type: tdTestResult
            reporter.log('Testing #%d, sDirectory="%s" ...' % (i, limitString(oCurTest.sDirectory),));

            fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            fRc, oCurGuestSession = oCurTest.createSession('testGuestCtrlDirCreate: Test #%d' % (i,));
            if fRc is False:
                return reporter.error('Test #%d failed: Could not create session' % (i,));

            fRc = self.gctrlCreateDir(oCurTest, oCurRes, oCurGuestSession);

            fRc = oCurTest.closeSession() and fRc;
            if fRc is False:
                fRc = reporter.error('Test #%d failed' % (i,));

        return (fRc, oTxsSession);

    def testGuestCtrlDirCreateTemp(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests creation of temporary directories.
        """

        sSystemDir = self.oTstDrv.getGuestSystemDir(oTestVm);
        atTests = [
            # Invalid stuff (template must have one or more trailin 'X'es (upper case only), or a cluster of three or more).
            [ tdTestDirCreateTemp(sDirectory = ''), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sDirectory = sSystemDir, fMode = 1234), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'xXx', sDirectory = sSystemDir, fMode = 0o700), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'xxx', sDirectory = sSystemDir, fMode = 0o700), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'XXx', sDirectory = sSystemDir, fMode = 0o700), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'bar', sDirectory = 'whatever', fMode = 0o700), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'foo', sDirectory = 'it is not used', fMode = 0o700), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'X,so', sDirectory = 'pointless test', fMode = 0o700), tdTestResultFailure() ],
            # Non-existing stuff.
            [ tdTestDirCreateTemp(sTemplate = 'XXXXXXX',
                                  sDirectory = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), 'non', 'existing')),
                                  tdTestResultFailure() ],
            # Working stuff:
            [ tdTestDirCreateTemp(sTemplate = 'X', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm)), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'XX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm)), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'XXX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm)), tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'XXXXXXX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm)),
              tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm)),
              tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm)),
              tdTestResultFailure() ],
            [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm)),
              tdTestResultFailure() ],
        ];

        if self.oTstDrv.fpApiVer >= 7.0:
            # Weird mode set.
            atTests.extend([
                [ tdTestDirCreateTemp(sTemplate = 'XXX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fMode = 0o42333),
                  tdTestResultFailure() ]
            ]);
            # Same as working stuff above, but with a different mode set.
            atTests.extend([
                [ tdTestDirCreateTemp(sTemplate = 'X', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fMode = 0o777),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'XX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fMode = 0o777),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'XXX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fMode = 0o777),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'XXXXXXX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fMode = 0o777),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fMode = 0o777),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fMode = 0o777),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fMode = 0o777),
                  tdTestResultFailure() ]
            ]);
            # Same as working stuff above, but with secure mode set.
            atTests.extend([
                [ tdTestDirCreateTemp(sTemplate = 'X', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fSecure = True),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'XX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fSecure = True),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'XXX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fSecure = True),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'XXXXXXX', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm), fSecure = True),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm),
                                      fSecure = True),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm),
                                      fSecure = True),
                  tdTestResultFailure() ],
                [ tdTestDirCreateTemp(sTemplate = 'tmpXXXtst', sDirectory = self.oTstDrv.getGuestTempDir(oTestVm),
                                      fSecure = True),
                  tdTestResultFailure() ]
            ]);

        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0] # type: tdTestDirCreateTemp
            oCurRes  = tTest[1] # type: tdTestResult
            reporter.log('Testing #%d, sTemplate="%s", fMode=%#o, path="%s", secure="%s" ...' %
                         (i, oCurTest.sTemplate, oCurTest.fMode, oCurTest.sDirectory, oCurTest.fSecure));

            fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            fRc, oCurGuestSession = oCurTest.createSession('testGuestCtrlDirCreateTemp: Test #%d' % (i,));
            if fRc is False:
                fRc = reporter.error('Test #%d failed: Could not create session' % (i,));
                break;

            sDirTemp = '';
            try:
                sDirTemp = oCurGuestSession.directoryCreateTemp(oCurTest.sTemplate, oCurTest.fMode,
                                                                oCurTest.sDirectory, oCurTest.fSecure);
            except:
                if oCurRes.fRc is True:
                    fRc = reporter.errorXcpt('Creating temp directory "%s" failed:' % (oCurTest.sDirectory,));
                else:
                    reporter.logXcpt('Creating temp directory "%s" failed expectedly, skipping:' % (oCurTest.sDirectory,));
            else:
                reporter.log2('Temporary directory is: "%s"' % (limitString(sDirTemp),));
                if not sDirTemp:
                    fRc = reporter.error('Resulting directory is empty!');
                else:
                    try:
                        oFsObjInfo = oCurGuestSession.fsObjQueryInfo(sDirTemp, False);
                        eType = oFsObjInfo.type;
                    except:
                        fRc = reporter.errorXcpt('sDirTemp="%s"' % (sDirTemp,));
                    else:
                        reporter.log2('%s: eType=%s (dir=%d)' % (limitString(sDirTemp), eType, vboxcon.FsObjType_Directory,));
                        if eType != vboxcon.FsObjType_Directory:
                            fRc = reporter.error('Temporary directory "%s" not created as a directory: eType=%d'
                                                 % (sDirTemp, eType));
            fRc = oCurTest.closeSession() and fRc;
        return (fRc, oTxsSession);

    def testGuestCtrlDirRead(self, oSession, oTxsSession, oTestVm):
        """
        Tests opening and reading (enumerating) guest directories.
        """

        sSystemDir = self.oTstDrv.getGuestSystemDir(oTestVm);
        atTests = [
            # Invalid stuff.
            [ tdTestDirRead(sDirectory = ''), tdTestResultDirRead() ],
            [ tdTestDirRead(sDirectory = sSystemDir, afFlags = [ 1234 ]), tdTestResultDirRead() ],
            [ tdTestDirRead(sDirectory = sSystemDir, sFilter = '*.foo'), tdTestResultDirRead() ],
            # Non-existing stuff.
            [ tdTestDirRead(sDirectory = oTestVm.pathJoin(sSystemDir, 'really-no-such-subdir')), tdTestResultDirRead() ],
            [ tdTestDirRead(sDirectory = oTestVm.pathJoin(sSystemDir, 'non', 'existing')), tdTestResultDirRead() ],
        ];

        if oTestVm.isWindows() or oTestVm.isOS2():
            atTests.extend([
                # More unusual stuff.
                [ tdTestDirRead(sDirectory = 'z:\\'), tdTestResultDirRead() ],
                [ tdTestDirRead(sDirectory = '\\\\uncrulez\\foo'), tdTestResultDirRead() ],
            ]);

        # Read the system directory (ASSUMES at least 5 files in it):
        # Windows 7+ has inaccessible system32/com/dmp directory that screws up this test, so skip it on windows:
        if not oTestVm.isWindows():
            atTests.append([ tdTestDirRead(sDirectory = sSystemDir),
                             tdTestResultDirRead(fRc = True, cFiles = -5, cDirs = None) ]);
        ## @todo trailing slash

        # Read from the test file set.
        atTests.extend([
            [ tdTestDirRead(sDirectory = self.oTestFiles.oEmptyDir.sPath),
              tdTestResultDirRead(fRc = True, cFiles = 0, cDirs = 0, cOthers = 0) ],
            [ tdTestDirRead(sDirectory = self.oTestFiles.oManyDir.sPath),
              tdTestResultDirRead(fRc = True, cFiles = len(self.oTestFiles.oManyDir.aoChildren), cDirs = 0, cOthers = 0) ],
            [ tdTestDirRead(sDirectory = self.oTestFiles.oTreeDir.sPath),
              tdTestResultDirRead(fRc = True, cFiles = self.oTestFiles.cTreeFiles, cDirs = self.oTestFiles.cTreeDirs,
                                  cOthers = self.oTestFiles.cTreeOthers) ],
        ]);


        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0]   # type: tdTestExec
            oCurRes  = tTest[1]   # type: tdTestResultDirRead

            reporter.log('Testing #%d, dir="%s" ...' % (i, oCurTest.sDirectory));
            fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            fRc, oCurGuestSession = oCurTest.createSession('testGuestCtrlDirRead: Test #%d' % (i,));
            if fRc is not True:
                break;
            fUseDirList = False;
            cEntriesPerRead = random.randrange(1, 32768);
            if  self.oTstDrv.fpApiVer >= 7.1 and self.oTstDrv.uRevision >= 156485 \
            and self.oTstDrv.isVersionEqualOrBigger(self.tpAdditionsVer, "7.1.0"):
                 # Listing directories only is available for >= VBox 7.1.
                fUseDirList = random.choice( [True, False] );
            (fRc2, cDirs, cFiles, cOthers) = self.gctrlReadDirTree(oCurTest, oCurGuestSession, oCurRes.fRc,
                                                                   fUseDirList, cEntriesPerRead);
            fRc = oCurTest.closeSession() and fRc;

            reporter.log2('Test #%d: Returned %d directories, %d files total' % (i, cDirs, cFiles));
            if fRc2 is oCurRes.fRc:
                if fRc2 is True:
                    if oCurRes.cFiles is None:
                        pass; # ignore
                    elif oCurRes.cFiles >= 0 and cFiles != oCurRes.cFiles:
                        fRc = reporter.error('Test #%d failed: Got %d files, expected %d' % (i, cFiles, oCurRes.cFiles));
                    elif oCurRes.cFiles < 0 and cFiles < -oCurRes.cFiles:
                        fRc = reporter.error('Test #%d failed: Got %d files, expected at least %d'
                                             % (i, cFiles, -oCurRes.cFiles));
                    if oCurRes.cDirs is None:
                        pass; # ignore
                    elif oCurRes.cDirs >= 0 and cDirs != oCurRes.cDirs:
                        fRc = reporter.error('Test #%d failed: Got %d directories, expected %d' % (i, cDirs, oCurRes.cDirs));
                    elif oCurRes.cDirs < 0 and cDirs < -oCurRes.cDirs:
                        fRc = reporter.error('Test #%d failed: Got %d directories, expected at least %d'
                                             % (i, cDirs, -oCurRes.cDirs));
                    if oCurRes.cOthers is None:
                        pass; # ignore
                    elif oCurRes.cOthers >= 0 and cOthers != oCurRes.cOthers:
                        fRc = reporter.error('Test #%d failed: Got %d other types, expected %d' % (i, cOthers, oCurRes.cOthers));
                    elif oCurRes.cOthers < 0 and cOthers < -oCurRes.cOthers:
                        fRc = reporter.error('Test #%d failed: Got %d other types, expected at least %d'
                                             % (i, cOthers, -oCurRes.cOthers));

            else:
                fRc = reporter.error('Test #%d failed: Got %s, expected %s' % (i, fRc2, oCurRes.fRc));


        #
        # Go over a few directories in the test file set and compare names,
        # types and sizes rather than just the counts like we did above.
        #
        if fRc is True:
            oCurTest = tdTestDirRead();
            fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if fRc:
                fRc, oCurGuestSession = oCurTest.createSession('testGuestCtrlDirRead: gctrlReadDirTree2');
            if fRc is True:
                for oDir in (self.oTestFiles.oEmptyDir, self.oTestFiles.oManyDir, self.oTestFiles.oTreeDir):
                    reporter.log('Checking "%s" ...' % (oDir.sPath,));
                    fUseDirList = False;
                    cEntriesPerRead = random.randrange(1, 32768);
                    if  self.oTstDrv.fpApiVer >= 7.1 and self.oTstDrv.uRevision >= 156485 \
                    and self.oTstDrv.isVersionEqualOrBigger(self.tpAdditionsVer, "7.1.0"):
                        # Listing directories only is available for >= VBox 7.1.
                        fUseDirList = random.choice( [True, False] );
                    fRc = self.gctrlReadDirTree2(oCurGuestSession, oDir, fUseDirList, cEntriesPerRead) and fRc;
                fRc = oCurTest.closeSession() and fRc;

        return (fRc, oTxsSession);


    def testGuestCtrlFileRemove(self, oSession, oTxsSession, oTestVm):
        """
        Tests removing guest files.
        """

        #
        # Create a directory with a few files in it using TXS that we'll use for the initial tests.
        #
        asTestDirs = [
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-1'),                             # [0]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-1', 'subdir-1'),                 # [1]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-1', 'subdir-1', 'subsubdir-1'),  # [2]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-2'),                             # [3]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-2', 'subdir-2'),                 # [4]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-2', 'subdir-2', 'subsbudir-2'),  # [5]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-3'),                             # [6]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-4'),                             # [7]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-5'),                             # [8]
            oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'rmtestdir-5', 'subdir-5'),                 # [9]
        ]
        asTestFiles = [
            oTestVm.pathJoin(asTestDirs[0], 'file-0'), # [0]
            oTestVm.pathJoin(asTestDirs[0], 'file-1'), # [1]
            oTestVm.pathJoin(asTestDirs[0], 'file-2'), # [2]
            oTestVm.pathJoin(asTestDirs[1], 'file-3'), # [3] - subdir-1
            oTestVm.pathJoin(asTestDirs[1], 'file-4'), # [4] - subdir-1
            oTestVm.pathJoin(asTestDirs[2], 'file-5'), # [5] - subsubdir-1
            oTestVm.pathJoin(asTestDirs[3], 'file-6'), # [6] - rmtestdir-2
            oTestVm.pathJoin(asTestDirs[4], 'file-7'), # [7] - subdir-2
            oTestVm.pathJoin(asTestDirs[5], 'file-8'), # [8] - subsubdir-2
        ];
        for sDir in asTestDirs:
            if oTxsSession.syncMkDir(sDir, 0o777) is not True:
                return reporter.error('Failed to create test dir "%s"!' % (sDir,));
        for sFile in asTestFiles:
            if oTxsSession.syncUploadString(sFile, sFile, 0o666) is not True:
                return reporter.error('Failed to create test file "%s"!' % (sFile,));

        #
        # Tear down the directories and files.
        #
        aoTests = [
            # Negative tests first:
            tdTestRemoveFile(asTestDirs[0], fRcExpect = False),
            tdTestRemoveDir(asTestDirs[0], fRcExpect = False),
            tdTestRemoveDir(asTestFiles[0], fRcExpect = False),
            tdTestRemoveFile(oTestVm.pathJoin(self.oTestFiles.oEmptyDir.sPath, 'no-such-file'), fRcExpect = False),
            tdTestRemoveDir(oTestVm.pathJoin(self.oTestFiles.oEmptyDir.sPath, 'no-such-dir'), fRcExpect = False),
            tdTestRemoveFile(oTestVm.pathJoin(self.oTestFiles.oEmptyDir.sPath, 'no-such-dir', 'no-file'), fRcExpect = False),
            tdTestRemoveDir(oTestVm.pathJoin(self.oTestFiles.oEmptyDir.sPath, 'no-such-dir', 'no-subdir'), fRcExpect = False),
            tdTestRemoveTree(asTestDirs[0], afFlags = [], fRcExpect = False), # Only removes empty dirs, this isn't empty.
            tdTestRemoveTree(asTestDirs[0], afFlags = [vboxcon.DirectoryRemoveRecFlag_None,], fRcExpect = False), # ditto
            # Empty paths:
            tdTestRemoveFile('', fRcExpect = False),
            tdTestRemoveDir('', fRcExpect = False),
            tdTestRemoveTree('', fRcExpect = False),
            # Now actually remove stuff:
            tdTestRemoveDir(asTestDirs[7], fRcExpect = True),
            tdTestRemoveFile(asTestDirs[6], fRcExpect = False),
            tdTestRemoveDir(asTestDirs[6], fRcExpect = True),
            tdTestRemoveFile(asTestFiles[0], fRcExpect = True),
            tdTestRemoveFile(asTestFiles[0], fRcExpect = False),
            # 17:
            tdTestRemoveTree(asTestDirs[8], fRcExpect = True),  # Removes empty subdirs and leaves the dir itself.
            tdTestRemoveDir(asTestDirs[8], fRcExpect = True),
            tdTestRemoveTree(asTestDirs[3], fRcExpect = False), # Have subdirs & files,
            tdTestRemoveTree(asTestDirs[3], afFlags = [vboxcon.DirectoryRemoveRecFlag_ContentOnly,], fRcExpect = True),
            tdTestRemoveDir(asTestDirs[3], fRcExpect = True),
            tdTestRemoveTree(asTestDirs[0], afFlags = [vboxcon.DirectoryRemoveRecFlag_ContentAndDir,], fRcExpect = True),
            # No error if already delete (RTDirRemoveRecursive artifact).
            tdTestRemoveTree(asTestDirs[0], afFlags = [vboxcon.DirectoryRemoveRecFlag_ContentAndDir,], fRcExpect = True),
            tdTestRemoveTree(asTestDirs[0], afFlags = [vboxcon.DirectoryRemoveRecFlag_ContentOnly,],
                             fNotExist = True, fRcExpect = True),
            tdTestRemoveTree(asTestDirs[0], afFlags = [vboxcon.DirectoryRemoveRecFlag_None,], fNotExist = True, fRcExpect = True),
        ];

        #
        # Execution loop
        #
        fRc = True;
        for (i, oTest) in enumerate(aoTests): # int, tdTestRemoveBase
            reporter.log('Testing #%d, path="%s" %s ...' % (i, oTest.sPath, oTest.__class__.__name__));
            fRc = oTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            fRc, _ = oTest.createSession('testGuestCtrlFileRemove: Test #%d' % (i,));
            if fRc is False:
                fRc = reporter.error('Test #%d failed: Could not create session' % (i,));
                break;
            fRc = oTest.execute(self) and fRc;
            fRc = oTest.closeSession() and fRc;

        if fRc is True:
            oCurTest = tdTestDirRead();
            fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if fRc:
                fRc, oCurGuestSession = oCurTest.createSession('remove final');
            if fRc is True:

                #
                # Delete all the files in the many subdir of the test set.
                #
                reporter.log('Deleting the file in "%s" ...' % (self.oTestFiles.oManyDir.sPath,));
                for oFile in self.oTestFiles.oManyDir.aoChildren:
                    reporter.log2('"%s"' % (limitString(oFile.sPath),));
                    try:
                        if self.oTstDrv.fpApiVer >= 5.0:
                            oCurGuestSession.fsObjRemove(oFile.sPath);
                        else:
                            oCurGuestSession.fileRemove(oFile.sPath);
                    except:
                        fRc = reporter.errorXcpt('Removing "%s" failed' % (oFile.sPath,));

                # Remove the directory itself to verify that we've removed all the files in it:
                reporter.log('Removing the directory "%s" ...' % (self.oTestFiles.oManyDir.sPath,));
                try:
                    oCurGuestSession.directoryRemove(self.oTestFiles.oManyDir.sPath);
                except:
                    fRc = reporter.errorXcpt('Removing directory "%s" failed' % (self.oTestFiles.oManyDir.sPath,));

                #
                # Recursively delete the entire test file tree from the root up.
                #
                # Note! On unix we cannot delete the root dir itself since it is residing
                #       in /var/tmp where only the owner may delete it.  Root is the owner.
                #
                if oTestVm.isWindows() or oTestVm.isOS2():
                    afFlags = [vboxcon.DirectoryRemoveRecFlag_ContentAndDir,];
                else:
                    afFlags = [vboxcon.DirectoryRemoveRecFlag_ContentOnly,];
                try:
                    oProgress = oCurGuestSession.directoryRemoveRecursive(self.oTestFiles.oRoot.sPath, afFlags);
                except:
                    fRc = reporter.errorXcpt('Removing tree "%s" failed' % (self.oTestFiles.oRoot.sPath,));
                else:
                    oWrappedProgress = vboxwrappers.ProgressWrapper(oProgress, self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                                    "remove-tree-root: %s" % (self.oTestFiles.oRoot.sPath,));
                    reporter.log2('waiting ...')
                    oWrappedProgress.wait();
                    reporter.log2('isSuccess=%s' % (oWrappedProgress.isSuccess(),));
                    if not oWrappedProgress.isSuccess():
                        fRc = oWrappedProgress.logResult();

                fRc = oCurTest.closeSession() and fRc;

        return (fRc, oTxsSession);


    def testGuestCtrlFileStat(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests querying file information through stat.
        """

        # Basic stuff, existing stuff.
        aoTests = [
            tdTestSessionEx([
                tdStepStatDir('.'),
                tdStepStatDir('..'),
                tdStepStatDir(self.oTstDrv.getGuestTempDir(oTestVm)),
                tdStepStatDir(self.oTstDrv.getGuestSystemDir(oTestVm)),
                tdStepStatDirEx(self.oTestFiles.oRoot),
                tdStepStatDirEx(self.oTestFiles.oEmptyDir),
                tdStepStatDirEx(self.oTestFiles.oTreeDir),
                tdStepStatDirEx(self.oTestFiles.chooseRandomDirFromTree()),
                tdStepStatDirEx(self.oTestFiles.chooseRandomDirFromTree()),
                tdStepStatDirEx(self.oTestFiles.chooseRandomDirFromTree()),
                tdStepStatDirEx(self.oTestFiles.chooseRandomDirFromTree()),
                tdStepStatDirEx(self.oTestFiles.chooseRandomDirFromTree()),
                tdStepStatDirEx(self.oTestFiles.chooseRandomDirFromTree()),
                tdStepStatFile(self.oTstDrv.getGuestSystemFileForReading(oTestVm)),
                tdStepStatFile(self.oTstDrv.getGuestSystemShell(oTestVm)),
                tdStepStatFileEx(self.oTestFiles.chooseRandomFile()),
                tdStepStatFileEx(self.oTestFiles.chooseRandomFile()),
                tdStepStatFileEx(self.oTestFiles.chooseRandomFile()),
                tdStepStatFileEx(self.oTestFiles.chooseRandomFile()),
                tdStepStatFileEx(self.oTestFiles.chooseRandomFile()),
                tdStepStatFileEx(self.oTestFiles.chooseRandomFile()),
            ]),
        ];

        # None existing stuff.
        sSysDir = self.oTstDrv.getGuestSystemDir(oTestVm);
        sSep    = oTestVm.pathSep();
        aoTests += [
            tdTestSessionEx([
                tdStepStatFileNotFound(oTestVm.pathJoin(sSysDir, 'NoSuchFileOrDirectory')),
                tdStepStatPathNotFound(oTestVm.pathJoin(sSysDir, 'NoSuchFileOrDirectory') + sSep),
                tdStepStatPathNotFound(oTestVm.pathJoin(sSysDir, 'NoSuchFileOrDirectory', '.')),
                tdStepStatPathNotFound(oTestVm.pathJoin(sSysDir, 'NoSuchFileOrDirectory', 'NoSuchFileOrSubDirectory')),
                tdStepStatPathNotFound(oTestVm.pathJoin(sSysDir, 'NoSuchFileOrDirectory', 'NoSuchFileOrSubDirectory') + sSep),
                tdStepStatPathNotFound(oTestVm.pathJoin(sSysDir, 'NoSuchFileOrDirectory', 'NoSuchFileOrSubDirectory', '.')),
                #tdStepStatPathNotFound('N:\\'), # ASSUMES nothing mounted on N:!
                #tdStepStatPathNotFound('\\\\NoSuchUncServerName\\NoSuchShare'),
            ]),
        ];
        # Invalid parameter check.
        aoTests += [ tdTestSessionEx([ tdStepStat('', vbox.ComError.E_INVALIDARG), ]), ];

        #
        # Execute the tests.
        #
        fRc, oTxsSession = tdTestSessionEx.executeListTestSessions(aoTests, self.oTstDrv, oSession, oTxsSession,
                                                                   oTestVm, 'FsStat');
        #
        # Test the full test file set.
        #
        if self.oTstDrv.fpApiVer < 5.0:
            return (fRc, oTxsSession);

        oTest = tdTestGuestCtrlBase();
        fRc = oTest.setEnvironment(oSession, oTxsSession, oTestVm);
        if not fRc:
            return (False, oTxsSession);
        fRc2, oGuestSession = oTest.createSession('FsStat on TestFileSet');
        if fRc2 is not True:
            return (False, oTxsSession);

        for oFsObj in self.oTestFiles.dPaths.values():
            reporter.log2('testGuestCtrlFileStat: %s sPath=%s'
                          % ('file' if isinstance(oFsObj, testfileset.TestFile) else 'dir ', limitString(oFsObj.sPath),));

            # Query the information:
            try:
                oFsInfo = oGuestSession.fsObjQueryInfo(oFsObj.sPath, False);
            except:
                fRc = reporter.errorXcpt('sPath=%s type=%s: fsObjQueryInfo trouble!' % (oFsObj.sPath, type(oFsObj),));
                continue;
            if oFsInfo is None:
                fRc = reporter.error('sPath=%s type=%s: No info object returned!' % (oFsObj.sPath, type(oFsObj),));
                continue;

            # Check attributes:
            try:
                eType    = oFsInfo.type;
                cbObject = oFsInfo.objectSize;
            except:
                fRc = reporter.errorXcpt('sPath=%s type=%s: attribute access trouble!' % (oFsObj.sPath, type(oFsObj),));
                continue;

            if isinstance(oFsObj, testfileset.TestFile):
                if eType != vboxcon.FsObjType_File:
                    fRc = reporter.error('sPath=%s type=file: eType=%s, expected %s!'
                                         % (oFsObj.sPath, eType, vboxcon.FsObjType_File));
                if cbObject != oFsObj.cbContent:
                    fRc = reporter.error('sPath=%s type=file: cbObject=%s, expected %s!'
                                         % (oFsObj.sPath, cbObject, oFsObj.cbContent));
                fFileExists = True;
                fDirExists  = False;
            elif isinstance(oFsObj, testfileset.TestDir):
                if eType != vboxcon.FsObjType_Directory:
                    fRc = reporter.error('sPath=%s type=dir: eType=%s, expected %s!'
                                         % (oFsObj.sPath, eType, vboxcon.FsObjType_Directory));
                fFileExists = False;
                fDirExists  = True;
            else:
                fRc = reporter.error('sPath=%s type=%s: Unexpected oFsObj type!' % (oFsObj.sPath, type(oFsObj),));
                continue;

            # Check the directoryExists and fileExists results too.
            try:
                fExistsResult = oGuestSession.fileExists(oFsObj.sPath, False);
            except:
                fRc = reporter.errorXcpt('sPath=%s type=%s: fileExists trouble!' % (oFsObj.sPath, type(oFsObj),));
            else:
                if fExistsResult != fFileExists:
                    fRc = reporter.error('sPath=%s type=%s: fileExists returned %s, expected %s!'
                                         % (oFsObj.sPath, type(oFsObj), fExistsResult, fFileExists));
            try:
                fExistsResult = oGuestSession.directoryExists(oFsObj.sPath, False);
            except:
                fRc = reporter.errorXcpt('sPath=%s type=%s: directoryExists trouble!' % (oFsObj.sPath, type(oFsObj),));
            else:
                if fExistsResult != fDirExists:
                    fRc = reporter.error('sPath=%s type=%s: directoryExists returned %s, expected %s!'
                                            % (oFsObj.sPath, type(oFsObj), fExistsResult, fDirExists));

        fRc = oTest.closeSession() and fRc;
        return (fRc, oTxsSession);

    def testGuestCtrlFileOpen(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests opening guest files.
        """
        if self.oTstDrv.fpApiVer < 5.0:
            reporter.log('Skipping because of pre 5.0 API');
            return None;

        #
        # Paths.
        #
        sTempDir        = self.oTstDrv.getGuestTempDir(oTestVm);
        sFileForReading = self.oTstDrv.getGuestSystemFileForReading(oTestVm);
        asFiles = [
            oTestVm.pathJoin(sTempDir, 'file-open-0'),
            oTestVm.pathJoin(sTempDir, 'file-open-1'),
            oTestVm.pathJoin(sTempDir, 'file-open-2'),
            oTestVm.pathJoin(sTempDir, 'file-open-3'),
            oTestVm.pathJoin(sTempDir, 'file-open-4'),
        ];
        asNonEmptyFiles = [
            oTestVm.pathJoin(sTempDir, 'file-open-10'),
            oTestVm.pathJoin(sTempDir, 'file-open-11'),
            oTestVm.pathJoin(sTempDir, 'file-open-12'),
            oTestVm.pathJoin(sTempDir, 'file-open-13'),
        ];
        sContent = 'abcdefghijklmnopqrstuvwxyz0123456789';
        for sFile in asNonEmptyFiles:
            if oTxsSession.syncUploadString(sContent, sFile, 0o666) is not True:
                return reporter.error('Failed to create "%s" via TXS' % (sFile,));

        #
        # The tests.
        #
        atTests = [
            # Invalid stuff.
            [ tdTestFileOpen(sFile = ''),                                                               tdTestResultFailure() ],
            # Wrong open mode.
            [ tdTestFileOpen(sFile = sFileForReading, eAccessMode = -1),                                tdTestResultFailure() ],
            # Wrong disposition.
            [ tdTestFileOpen(sFile = sFileForReading, eAction = -1),                                    tdTestResultFailure() ],
            # Non-existing file or path.
            [ tdTestFileOpen(sFile = oTestVm.pathJoin(sTempDir, 'no-such-file-or-dir')),                tdTestResultFailure() ],
            [ tdTestFileOpen(sFile = oTestVm.pathJoin(sTempDir, 'no-such-file-or-dir'),
                             eAction = vboxcon.FileOpenAction_OpenExistingTruncated),                   tdTestResultFailure() ],
            [ tdTestFileOpen(sFile = oTestVm.pathJoin(sTempDir, 'no-such-file-or-dir'),
                             eAccessMode = vboxcon.FileAccessMode_WriteOnly,
                             eAction = vboxcon.FileOpenAction_OpenExistingTruncated),                   tdTestResultFailure() ],
            [ tdTestFileOpen(sFile = oTestVm.pathJoin(sTempDir, 'no-such-file-or-dir'),
                             eAccessMode = vboxcon.FileAccessMode_ReadWrite,
                             eAction = vboxcon.FileOpenAction_OpenExistingTruncated),                   tdTestResultFailure() ],
            [ tdTestFileOpen(sFile = oTestVm.pathJoin(sTempDir, 'no-such-dir', 'no-such-file')),        tdTestResultFailure() ],
        ];
        if self.oTstDrv.fpApiVer > 5.2: # Fixed since 6.0.
            atTests.extend([
                # Wrong type:
                [ tdTestFileOpen(sFile = self.oTstDrv.getGuestTempDir(oTestVm)),                        tdTestResultFailure() ],
                [ tdTestFileOpen(sFile = self.oTstDrv.getGuestSystemDir(oTestVm)),                      tdTestResultFailure() ],
            ]);
        atTests.extend([
            # O_EXCL and such:
            [ tdTestFileOpen(sFile = sFileForReading, eAction = vboxcon.FileOpenAction_CreateNew,
                             eAccessMode = vboxcon.FileAccessMode_ReadWrite),                           tdTestResultFailure() ],
            [ tdTestFileOpen(sFile = sFileForReading, eAction = vboxcon.FileOpenAction_CreateNew),      tdTestResultFailure() ],
            # Open a file.
            [ tdTestFileOpen(sFile = sFileForReading),                                                  tdTestResultSuccess() ],
            [ tdTestFileOpen(sFile = sFileForReading,
                             eAction = vboxcon.FileOpenAction_OpenOrCreate),                            tdTestResultSuccess() ],
            # Create a new file.
            [ tdTestFileOpenCheckSize(sFile = asFiles[0], eAction = vboxcon.FileOpenAction_CreateNew,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asFiles[0], eAction = vboxcon.FileOpenAction_CreateNew,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultFailure() ],
            [ tdTestFileOpenCheckSize(sFile = asFiles[0], eAction = vboxcon.FileOpenAction_OpenExisting,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asFiles[0], eAction = vboxcon.FileOpenAction_CreateOrReplace,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asFiles[0], eAction = vboxcon.FileOpenAction_OpenOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asFiles[0], eAction = vboxcon.FileOpenAction_OpenExistingTruncated,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asFiles[0], eAction = vboxcon.FileOpenAction_AppendOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            # Open or create a new file.
            [ tdTestFileOpenCheckSize(sFile = asFiles[1], eAction = vboxcon.FileOpenAction_OpenOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            # Create or replace a new file.
            [ tdTestFileOpenCheckSize(sFile = asFiles[2], eAction = vboxcon.FileOpenAction_CreateOrReplace,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            # Create and append to file (weird stuff).
            [ tdTestFileOpenCheckSize(sFile = asFiles[3], eAction = vboxcon.FileOpenAction_AppendOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asFiles[4], eAction = vboxcon.FileOpenAction_AppendOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_WriteOnly),                  tdTestResultSuccess() ],
            # Open the non-empty files in non-destructive modes.
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[0], cbOpenExpected = len(sContent)),      tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[1], cbOpenExpected = len(sContent),
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[2], cbOpenExpected = len(sContent),
                                      eAccessMode = vboxcon.FileAccessMode_WriteOnly),                  tdTestResultSuccess() ],

            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[0], cbOpenExpected = len(sContent),
                                      eAction = vboxcon.FileOpenAction_OpenOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[1], cbOpenExpected = len(sContent),
                                      eAction = vboxcon.FileOpenAction_OpenOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_WriteOnly),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[2], cbOpenExpected = len(sContent),
                                      eAction = vboxcon.FileOpenAction_OpenOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_WriteOnly),                  tdTestResultSuccess() ],

            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[0], cbOpenExpected = len(sContent),
                                      eAction = vboxcon.FileOpenAction_AppendOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_ReadWrite),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[1], cbOpenExpected = len(sContent),
                                      eAction = vboxcon.FileOpenAction_AppendOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_WriteOnly),                  tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[2], cbOpenExpected = len(sContent),
                                      eAction = vboxcon.FileOpenAction_AppendOrCreate,
                                      eAccessMode = vboxcon.FileAccessMode_WriteOnly),                  tdTestResultSuccess() ],

            # Now the destructive stuff:
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[0], eAccessMode = vboxcon.FileAccessMode_WriteOnly,
                                      eAction = vboxcon.FileOpenAction_OpenExistingTruncated),          tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[1], eAccessMode = vboxcon.FileAccessMode_WriteOnly,
                                      eAction = vboxcon.FileOpenAction_CreateOrReplace),                tdTestResultSuccess() ],
            [ tdTestFileOpenCheckSize(sFile = asNonEmptyFiles[2], eAction = vboxcon.FileOpenAction_CreateOrReplace,
                                      eAccessMode = vboxcon.FileAccessMode_WriteOnly),                  tdTestResultSuccess() ],
        ]);

        #
        # Do the testing.
        #
        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0]     # type: tdTestFileOpen
            oCurRes  = tTest[1]     # type: tdTestResult

            reporter.log('Testing #%d: %s - sFile="%s", eAccessMode=%d, eAction=%d, (%s, %s, %s) ...'
                         % (i, oCurTest.__class__.__name__, oCurTest.sFile, oCurTest.eAccessMode, oCurTest.eAction,
                            oCurTest.eSharing, oCurTest.fCreationMode, oCurTest.afOpenFlags,));

            oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            fRc, _ = oCurTest.createSession('testGuestCtrlFileOpen: Test #%d' % (i,));
            if fRc is not True:
                fRc = reporter.error('Test #%d failed: Could not create session' % (i,));
                break;

            fRc2 = oCurTest.doSteps(oCurRes.fRc, self);
            if fRc2 != oCurRes.fRc:
                fRc = reporter.error('Test #%d result mismatch: Got %s, expected %s' % (i, fRc2, oCurRes.fRc,));

            fRc = oCurTest.closeSession() and fRc;

        return (fRc, oTxsSession);


    def testGuestCtrlFileRead(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-branches,too-many-statements
        """
        Tests reading from guest files.
        """
        if self.oTstDrv.fpApiVer < 5.0:
            reporter.log('Skipping because of pre 5.0 API');
            return None;

        #
        # Do everything in one session.
        #
        oTest = tdTestGuestCtrlBase();
        fRc = oTest.setEnvironment(oSession, oTxsSession, oTestVm);
        if not fRc:
            return (False, oTxsSession);
        fRc2, oGuestSession = oTest.createSession('FsStat on TestFileSet');
        if fRc2 is not True:
            return (False, oTxsSession);

        #
        # Create a really big zero filled, up to 1 GiB, adding it to the list of
        # files from the set.
        #
        # Note! This code sucks a bit because we don't have a working setSize nor
        #       any way to figure out how much free space there is in the guest.
        #
        aoExtraFiles = [];
        sBigName = self.oTestFiles.generateFilenameEx();
        sBigPath = oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, sBigName);
        fRc = True;
        try:
            oFile = oGuestSession.fileOpenEx(sBigPath, vboxcon.FileAccessMode_ReadWrite, vboxcon.FileOpenAction_CreateOrReplace,
                                             vboxcon.FileSharingMode_All, 0, []);
        except:
            fRc = reporter.errorXcpt('sBigName=%s' % (sBigPath,));
        else:
            # Does setSize work now?
            fUseFallback = True;
            try:
                oFile.setSize(0);
                oFile.setSize(64);
                fUseFallback = False;
            except:
                reporter.logXcpt();

            # Grow the file till we hit trouble, typical VERR_DISK_FULL, then
            # reduce the file size if we have a working setSize.
            cbBigFile = 0;
            while cbBigFile < (1024 + 32)*1024*1024:
                if not fUseFallback:
                    cbBigFile += 16*1024*1024;
                    try:
                        oFile.setSize(cbBigFile);
                    except Exception:
                        reporter.logXcpt('cbBigFile=%s' % (sBigPath,));
                        try:
                            cbBigFile -= 16*1024*1024;
                            oFile.setSize(cbBigFile);
                        except:
                            reporter.logXcpt('cbBigFile=%s' % (sBigPath,));
                        break;
                else:
                    cbBigFile += 32*1024*1024;
                    try:
                        oFile.seek(cbBigFile, vboxcon.FileSeekOrigin_Begin);
                        oFile.write(bytearray(1), 60*1000);
                    except:
                        reporter.logXcpt('cbBigFile=%s' % (sBigPath,));
                        break;
            try:
                cbBigFile = oFile.seek(0, vboxcon.FileSeekOrigin_End);
            except:
                fRc = reporter.errorXcpt('sBigName=%s' % (sBigPath,));
            try:
                oFile.close();
            except:
                fRc = reporter.errorXcpt('sBigName=%s' % (sBigPath,));
            if fRc is True:
                reporter.log('Big file: %s bytes: %s' % (cbBigFile, sBigPath,));
                aoExtraFiles.append(testfileset.TestFileZeroFilled(None, sBigPath, cbBigFile));
            else:
                try:
                    oGuestSession.fsObjRemove(sBigPath);
                except:
                    reporter.errorXcpt('fsObjRemove(sBigName=%s)' % (sBigPath,));

        #
        # Open and read all the files in the test file set.
        #
        for oTestFile in aoExtraFiles + self.oTestFiles.aoFiles: # type: testfileset.TestFile
            reporter.log2('Test file: %s bytes, "%s" ...' % (oTestFile.cbContent, limitString(oTestFile.sPath),));

            #
            # Open it:
            #
            try:
                oFile = oGuestSession.fileOpenEx(oTestFile.sPath, vboxcon.FileAccessMode_ReadOnly,
                                                 vboxcon.FileOpenAction_OpenExisting, vboxcon.FileSharingMode_All, 0, []);
            except:
                fRc = reporter.errorXcpt('sPath=%s' % (oTestFile.sPath, ));
                continue;

            #
            # Read the file in different sized chunks:
            #
            if oTestFile.cbContent < 128:
                acbChunks = xrange(1,128);
            elif oTestFile.cbContent < 1024:
                acbChunks = (2048, 127, 63, 32, 29, 17, 16, 15, 9);
            elif oTestFile.cbContent < 8*1024*1024:
                acbChunks = (128*1024, 32*1024, 8191, 255);
            else:
                acbChunks = (768*1024, 128*1024);

            reporter.log2('Chunked reads');

            for cbChunk in acbChunks:
                # Read the whole file straight thru:
                #if oTestFile.cbContent >= 1024*1024: reporter.log2('... cbChunk=%s' % (cbChunk,));
                offFile = 0;
                cReads = 0;
                while offFile <= oTestFile.cbContent:
                    try:
                        abRead = oFile.read(cbChunk, 30*1000);
                    except:
                        fRc = reporter.errorXcpt('%s: offFile=%s cbChunk=%s cbContent=%s'
                                                 % (oTestFile.sPath, offFile, cbChunk, oTestFile.cbContent));
                        break;
                    cbRead = len(abRead);
                    if cbRead == 0 and offFile == oTestFile.cbContent:
                        break;
                    if cbRead <= 0:
                        fRc = reporter.error('%s @%s: cbRead=%s, cbContent=%s'
                                             % (oTestFile.sPath, offFile, cbRead, oTestFile.cbContent));
                        break;
                    if not oTestFile.equalMemory(abRead, offFile):
                        fRc = reporter.error('%s: read mismatch @ %s LB %s' % (oTestFile.sPath, offFile, cbRead));
                        break;
                    offFile += cbRead;
                    cReads  += 1;
                    if cReads > 8192:
                        break;

                # Seek to start of file.
                try:
                    offFile = oFile.seek(0, vboxcon.FileSeekOrigin_Begin);
                except:
                    fRc = reporter.errorXcpt('%s: error seeking to start of file' % (oTestFile.sPath,));
                    break;
                if offFile != 0:
                    fRc = reporter.error('%s: seek to start of file returned %u, expected 0' % (oTestFile.sPath, offFile));
                    break;

            #
            # Random reads.
            #
            reporter.log2('Random reads (seek)');
            for _ in xrange(8):
                offFile  = self.oTestFiles.oRandom.randrange(0, oTestFile.cbContent + 1024);
                cbToRead = self.oTestFiles.oRandom.randrange(1, min(oTestFile.cbContent + 256, 768*1024));
                #if oTestFile.cbContent >= 1024*1024: reporter.log2('... %s LB %s' % (offFile, cbToRead,));

                try:
                    offActual = oFile.seek(offFile, vboxcon.FileSeekOrigin_Begin);
                except:
                    fRc = reporter.errorXcpt('%s: error seeking to %s' % (oTestFile.sPath, offFile));
                    break;
                if offActual != offFile:
                    fRc = reporter.error('%s: seek(%s,Begin) -> %s, expected %s'
                                         % (oTestFile.sPath, offFile, offActual, offFile));
                    break;

                try:
                    abRead = oFile.read(cbToRead, 30*1000);
                except:
                    fRc = reporter.errorXcpt('%s: offFile=%s cbToRead=%s cbContent=%s'
                                             % (oTestFile.sPath, offFile, cbToRead, oTestFile.cbContent));
                    cbRead = 0;
                else:
                    cbRead = len(abRead);
                    if not oTestFile.equalMemory(abRead, offFile):
                        fRc = reporter.error('%s: random read mismatch @ %s LB %s' % (oTestFile.sPath, offFile, cbRead,));

                try:
                    offActual = oFile.offset;
                except:
                    fRc = reporter.errorXcpt('%s: offFile=%s cbToRead=%s cbContent=%s (#1)'
                                             % (oTestFile.sPath, offFile, cbToRead, oTestFile.cbContent));
                else:
                    if offActual != offFile + cbRead:
                        fRc = reporter.error('%s: IFile.offset is %s, expected %s (offFile=%s cbToRead=%s cbRead=%s) (#1)'
                                             % (oTestFile.sPath, offActual, offFile + cbRead, offFile, cbToRead, cbRead));
                try:
                    offActual = oFile.seek(0, vboxcon.FileSeekOrigin_Current);
                except:
                    fRc = reporter.errorXcpt('%s: offFile=%s cbToRead=%s cbContent=%s (#1)'
                                             % (oTestFile.sPath, offFile, cbToRead, oTestFile.cbContent));
                else:
                    if offActual != offFile + cbRead:
                        fRc = reporter.error('%s: seek(0,cur) -> %s, expected %s (offFile=%s cbToRead=%s cbRead=%s) (#1)'
                                             % (oTestFile.sPath, offActual, offFile + cbRead, offFile, cbToRead, cbRead));

            #
            # Random reads using readAt.
            #
            reporter.log2('Random reads (readAt)');
            for _ in xrange(12):
                offFile  = self.oTestFiles.oRandom.randrange(0, oTestFile.cbContent + 1024);
                cbToRead = self.oTestFiles.oRandom.randrange(1, min(oTestFile.cbContent + 256, 768*1024));
                #if oTestFile.cbContent >= 1024*1024: reporter.log2('... %s LB %s (readAt)' % (offFile, cbToRead,));

                try:
                    abRead = oFile.readAt(offFile, cbToRead, 30*1000);
                except:
                    fRc = reporter.errorXcpt('%s: offFile=%s cbToRead=%s cbContent=%s'
                                             % (oTestFile.sPath, offFile, cbToRead, oTestFile.cbContent));
                    cbRead = 0;
                else:
                    cbRead = len(abRead);
                    if not oTestFile.equalMemory(abRead, offFile):
                        fRc = reporter.error('%s: random readAt mismatch @ %s LB %s' % (oTestFile.sPath, offFile, cbRead,));

                try:
                    offActual = oFile.offset;
                except:
                    fRc = reporter.errorXcpt('%s: offFile=%s cbToRead=%s cbContent=%s (#2)'
                                             % (oTestFile.sPath, offFile, cbToRead, oTestFile.cbContent));
                else:
                    if offActual != offFile + cbRead:
                        fRc = reporter.error('%s: IFile.offset is %s, expected %s (offFile=%s cbToRead=%s cbRead=%s) (#2)'
                                             % (oTestFile.sPath, offActual, offFile + cbRead, offFile, cbToRead, cbRead));

                try:
                    offActual = oFile.seek(0, vboxcon.FileSeekOrigin_Current);
                except:
                    fRc = reporter.errorXcpt('%s: offFile=%s cbToRead=%s cbContent=%s (#2)'
                                             % (oTestFile.sPath, offFile, cbToRead, oTestFile.cbContent));
                else:
                    if offActual != offFile + cbRead:
                        fRc = reporter.error('%s: seek(0,cur) -> %s, expected %s (offFile=%s cbToRead=%s cbRead=%s) (#2)'
                                             % (oTestFile.sPath, offActual, offFile + cbRead, offFile, cbToRead, cbRead));

            #
            # A few negative things.
            #

            # Zero byte reads -> E_INVALIDARG.
            reporter.log2('Zero byte reads');
            try:
                abRead = oFile.read(0, 30*1000);
            except Exception as oXcpt:
                if vbox.ComError.notEqual(oXcpt, vbox.ComError.E_INVALIDARG):
                    fRc = reporter.errorXcpt('read(0,30s) did not raise E_INVALIDARG as expected!');
            else:
                fRc = reporter.error('read(0,30s) did not fail!');

            try:
                abRead = oFile.readAt(0, 0, 30*1000);
            except Exception as oXcpt:
                if vbox.ComError.notEqual(oXcpt, vbox.ComError.E_INVALIDARG):
                    fRc = reporter.errorXcpt('readAt(0,0,30s) did not raise E_INVALIDARG as expected!');
            else:
                fRc = reporter.error('readAt(0,0,30s) did not fail!');

            # See what happens when we read 1GiB.  We should get a max of 1MiB back.
            ## @todo Document this behaviour in VirtualBox.xidl.
            reporter.log2('1GB reads');
            try:
                oFile.seek(0, vboxcon.FileSeekOrigin_Begin);
            except:
                fRc = reporter.error('seek(0)');
            try:
                abRead = oFile.read(1024*1024*1024, 30*1000);
            except:
                fRc = reporter.errorXcpt('read(1GiB,30s)');
            else:
                if len(abRead) != min(oTestFile.cbContent, 1024*1024):
                    fRc = reporter.error('Expected read(1GiB,30s) to return %s bytes, got %s bytes instead'
                                         % (min(oTestFile.cbContent, 1024*1024), len(abRead),));

            try:
                abRead = oFile.readAt(0, 1024*1024*1024, 30*1000);
            except:
                fRc = reporter.errorXcpt('readAt(0,1GiB,30s)');
            else:
                if len(abRead) != min(oTestFile.cbContent, 1024*1024):
                    reporter.error('Expected readAt(0, 1GiB,30s) to return %s bytes, got %s bytes instead'
                                   % (min(oTestFile.cbContent, 1024*1024), len(abRead),));

            #
            # Check stat info on the file as well as querySize.
            #
            if self.oTstDrv.fpApiVer > 5.2:
                try:
                    oFsObjInfo = oFile.queryInfo();
                except:
                    fRc = reporter.errorXcpt('%s: queryInfo()' % (oTestFile.sPath,));
                else:
                    if oFsObjInfo is None:
                        fRc = reporter.error('IGuestFile::queryInfo returned None');
                    else:
                        try:
                            cbFile = oFsObjInfo.objectSize;
                        except:
                            fRc = reporter.errorXcpt();
                        else:
                            if cbFile != oTestFile.cbContent:
                                fRc = reporter.error('%s: queryInfo returned incorrect file size: %s, expected %s'
                                                     % (oTestFile.sPath, cbFile, oTestFile.cbContent));

                try:
                    cbFile = oFile.querySize();
                except:
                    fRc = reporter.errorXcpt('%s: querySize()' % (oTestFile.sPath,));
                else:
                    if cbFile != oTestFile.cbContent:
                        fRc = reporter.error('%s: querySize returned incorrect file size: %s, expected %s'
                                             % (oTestFile.sPath, cbFile, oTestFile.cbContent));

            #
            # Use seek to test the file size and do a few other end-relative seeks.
            #
            try:
                cbFile = oFile.seek(0, vboxcon.FileSeekOrigin_End);
            except:
                fRc = reporter.errorXcpt('%s: seek(0,End)' % (oTestFile.sPath,));
            else:
                if cbFile != oTestFile.cbContent:
                    fRc = reporter.error('%s: seek(0,End) returned incorrect file size: %s, expected %s'
                                         % (oTestFile.sPath, cbFile, oTestFile.cbContent));
            if oTestFile.cbContent > 0:
                for _ in xrange(5):
                    offSeek = self.oTestFiles.oRandom.randrange(oTestFile.cbContent + 1);
                    try:
                        offFile = oFile.seek(-offSeek, vboxcon.FileSeekOrigin_End);
                    except:
                        fRc = reporter.errorXcpt('%s: seek(%s,End)' % (oTestFile.sPath, -offSeek,));
                    else:
                        if offFile != oTestFile.cbContent - offSeek:
                            fRc = reporter.error('%s: seek(%s,End) returned incorrect offset: %s, expected %s (cbContent=%s)'
                                                 % (oTestFile.sPath, -offSeek, offSeek, oTestFile.cbContent - offSeek,
                                                    oTestFile.cbContent,));

            #
            # Close it and we're done with this file.
            #
            try:
                oFile.close();
            except:
                fRc = reporter.errorXcpt('%s: error closing the file' % (oTestFile.sPath,));

        #
        # Clean up.
        #
        for oTestFile in aoExtraFiles:
            try:
                oGuestSession.fsObjRemove(sBigPath);
            except:
                fRc = reporter.errorXcpt('fsObjRemove(%s)' % (sBigPath,));

        fRc = oTest.closeSession() and fRc;

        return (fRc, oTxsSession);


    def testGuestCtrlFileWrite(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests writing to guest files.
        """
        if self.oTstDrv.fpApiVer < 5.0:
            reporter.log('Skipping because of pre 5.0 API');
            return None;

        #
        # The test file and its content.
        #
        sFile     = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), 'gctrl-write-1');
        abContent = bytearray(0);

        #
        # The tests.
        #
        def randBytes(cbHowMany):
            """ Returns an bytearray of random bytes. """
            return bytearray(self.oTestFiles.oRandom.getrandbits(8) for _ in xrange(cbHowMany));

        aoTests = [
            # Write at end:
            tdTestFileOpenAndWrite(sFile = sFile, eAction = vboxcon.FileOpenAction_CreateNew, abContent = abContent,
                                   atChunks = [(None, randBytes(1)), (None, randBytes(77)), (None, randBytes(98)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 1+77+98), # 176
            # Appending:
            tdTestFileOpenAndWrite(sFile = sFile, eAction = vboxcon.FileOpenAction_AppendOrCreate, abContent = abContent,
                                   atChunks = [(None, randBytes(255)), (None, randBytes(33)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 176 + 255+33), # 464
            tdTestFileOpenAndWrite(sFile = sFile, eAction = vboxcon.FileOpenAction_AppendOrCreate, abContent = abContent,
                                   atChunks = [(10, randBytes(44)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 464 + 44), # 508
            # Write within existing:
            tdTestFileOpenAndWrite(sFile = sFile, eAction = vboxcon.FileOpenAction_OpenExisting, abContent = abContent,
                                   atChunks = [(0, randBytes(1)), (50, randBytes(77)), (255, randBytes(199)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 508),
            # Writing around and over the end:
            tdTestFileOpenAndWrite(sFile = sFile, abContent = abContent,
                                   atChunks = [(500, randBytes(9)), (508, randBytes(15)), (512, randBytes(12)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 512+12),

            # writeAt appending:
            tdTestFileOpenAndWrite(sFile = sFile, abContent = abContent, fUseAtApi = True,
                                   atChunks = [(0, randBytes(23)), (6, randBytes(1018)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 6+1018), # 1024
            # writeAt within existing:
            tdTestFileOpenAndWrite(sFile = sFile, abContent = abContent, fUseAtApi = True,
                                   atChunks = [(1000, randBytes(23)), (1, randBytes(990)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 1024),
            # writeAt around and over the end:
            tdTestFileOpenAndWrite(sFile = sFile, abContent = abContent, fUseAtApi = True,
                                   atChunks = [(1024, randBytes(63)), (1080, randBytes(968)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 1080+968), # 2048

            # writeAt beyond the end (gap is filled with zeros):
            tdTestFileOpenAndWrite(sFile = sFile, abContent = abContent, fUseAtApi = True, atChunks = [(3070, randBytes(2)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 3072),
            # write beyond the end (gap is filled with zeros):
            tdTestFileOpenAndWrite(sFile = sFile, abContent = abContent, atChunks = [(4090, randBytes(6)),]),
            tdTestFileOpenAndCheckContent(sFile = sFile, abContent = abContent, cbContentExpected = 4096),
        ];

        for (i, oCurTest) in enumerate(aoTests):
            reporter.log('Testing #%d: %s ...' % (i, oCurTest.toString(),));
            fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            fRc, _ = oCurTest.createSession('testGuestCtrlFileWrite: Test #%d' % (i,));
            if fRc is not True:
                fRc = reporter.error('Test #%d failed: Could not create session' % (i,));
                break;

            fRc2 = oCurTest.doSteps(True, self);
            if fRc2 is not True:
                fRc = reporter.error('Test #%d failed!' % (i,));

            fRc = oCurTest.closeSession() and fRc;

        #
        # Cleanup
        #
        if oTxsSession.syncRmFile(sFile) is not True:
            fRc = reporter.error('Failed to remove write-test file: %s' % (sFile, ));

        return (fRc, oTxsSession);

    @staticmethod
    def __generateFile(sName, cbFile):
        """ Helper for generating a file with a given size. """
        with open(sName, 'wb') as oFile:
            while cbFile > 0:
                cb = cbFile if cbFile < 256*1024 else 256*1024;
                oFile.write(bytearray(random.getrandbits(8) for _ in xrange(cb)));
                cbFile -= cb;
        return True;

    def testGuestCtrlCopyTo(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests copying files from host to the guest.
        """

        #
        # Paths and test files.
        #
        sScratchHst             = os.path.join(self.oTstDrv.sScratchPath,         'copyto');
        sScratchTestFilesHst    = os.path.join(sScratchHst, self.oTestFiles.sSubDir);
        sScratchEmptyDirHst     = os.path.join(sScratchTestFilesHst, self.oTestFiles.oEmptyDir.sName);
        sScratchNonEmptyDirHst  = self.oTestFiles.chooseRandomDirFromTree().buildPath(sScratchHst, os.path.sep);
        sScratchTreeDirHst      = os.path.join(sScratchTestFilesHst, self.oTestFiles.oTreeDir.sName);

        sScratchGst             = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), 'copyto');
        sScratchDstDir1Gst      = oTestVm.pathJoin(sScratchGst, 'dstdir1');
        sScratchDstDir2Gst      = oTestVm.pathJoin(sScratchGst, 'dstdir2');
        sScratchDstDir3Gst      = oTestVm.pathJoin(sScratchGst, 'dstdir3');
        sScratchDstDir4Gst      = oTestVm.pathJoin(sScratchGst, 'dstdir4');
        sScratchDotDotDirGst    = oTestVm.pathJoin(sScratchGst, '..');
        #sScratchGstNotExist     = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), 'no-such-file-or-directory');
        sScratchHstNotExist     = os.path.join(self.oTstDrv.sScratchPath,         'no-such-file-or-directory');
        sScratchGstPathNotFound = oTestVm.pathJoin(self.oTstDrv.getGuestTempDir(oTestVm), 'no-such-directory', 'or-file');
        #sScratchHstPathNotFound = os.path.join(self.oTstDrv.sScratchPath,         'no-such-directory', 'or-file');

        if oTestVm.isWindows() or oTestVm.isOS2():
            sScratchGstInvalid  = "?*|<invalid-name>";
        else:
            sScratchGstInvalid  = None;
        if utils.getHostOs() in ('win', 'os2'):
            sScratchHstInvalid  = "?*|<invalid-name>";
        else:
            sScratchHstInvalid  = None;

        for sDir in (sScratchGst, sScratchDstDir1Gst, sScratchDstDir2Gst, sScratchDstDir3Gst, sScratchDstDir4Gst):
            if oTxsSession.syncMkDir(sDir, 0o777) is not True:
                return reporter.error('TXS failed to create directory "%s"!' % (sDir,));

        # Put the test file set under sScratchHst.
        if os.path.exists(sScratchHst):
            if base.wipeDirectory(sScratchHst) != 0:
                return reporter.error('Failed to wipe "%s"' % (sScratchHst,));
        else:
            try:
                os.mkdir(sScratchHst);
            except:
                return reporter.errorXcpt('os.mkdir(%s)' % (sScratchHst, ));
        if self.oTestFiles.writeToDisk(sScratchHst) is not True:
            return reporter.error('Filed to write test files to "%s" on the host!' % (sScratchHst,));

        # If for whatever reason the directory tree does not exist on the host, let us know.
        # Copying an non-existing tree *will* fail the tests which otherwise should succeed!
        assert os.path.exists(sScratchTreeDirHst);

        # Generate a test file in 32MB to 64 MB range.
        sBigFileHst  = os.path.join(self.oTstDrv.sScratchPath, 'gctrl-random.data');
        cbBigFileHst = random.randrange(32*1024*1024, 64*1024*1024);
        reporter.log('cbBigFileHst=%s' % (cbBigFileHst,));
        cbLeft       = cbBigFileHst;
        try:
            self.__generateFile(sBigFileHst, cbBigFileHst);
        except:
            return reporter.errorXcpt('sBigFileHst=%s cbBigFileHst=%s cbLeft=%s' % (sBigFileHst, cbBigFileHst, cbLeft,));
        reporter.log('cbBigFileHst=%s' % (cbBigFileHst,));

        # Generate an empty file on the host that we can use to save space in the guest.
        sEmptyFileHst = os.path.join(self.oTstDrv.sScratchPath, 'gctrl-empty.data');
        try:
            open(sEmptyFileHst, "wb").close();                  # pylint: disable=consider-using-with
        except:
            return reporter.errorXcpt('sEmptyFileHst=%s' % (sEmptyFileHst,));

        # os.path.join() is too clever for "..", so we just build up the path here ourselves.
        sScratchDotDotFileHst = sScratchHst + os.path.sep + '..' + os.path.sep + 'gctrl-empty.data';

        #
        # Tests.
        #
        atTests = [
            # Nothing given:
            [ tdTestCopyToFile(), tdTestResultFailure() ],
            [ tdTestCopyToDir(),  tdTestResultFailure() ],
            # Only source given:
            [ tdTestCopyToFile(sSrc = sBigFileHst), tdTestResultFailure() ],
            [ tdTestCopyToDir( sSrc = sScratchEmptyDirHst), tdTestResultFailure() ],
            # Only destination given:
            [ tdTestCopyToFile(sDst = oTestVm.pathJoin(sScratchGst, 'dstfile')), tdTestResultFailure() ],
            [ tdTestCopyToDir( sDst = sScratchGst), tdTestResultFailure() ],
            # Both given, but invalid flags.
            [ tdTestCopyToFile(sSrc = sBigFileHst, sDst = sScratchGst, afFlags = [ 0x40000000, ] ), tdTestResultFailure() ],
            [ tdTestCopyToDir( sSrc = sScratchEmptyDirHst, sDst = sScratchGst, afFlags = [ 0x40000000, ] ),
              tdTestResultFailure() ],
        ];
        atTests.extend([
            # Non-existing source, but no destination:
            [ tdTestCopyToFile(sSrc = sScratchHstNotExist), tdTestResultFailure() ],
            [ tdTestCopyToDir( sSrc = sScratchHstNotExist), tdTestResultFailure() ],
            # Valid sources, but destination path not found:
            [ tdTestCopyToFile(sSrc = sBigFileHst, sDst = sScratchGstPathNotFound), tdTestResultFailure() ],
            [ tdTestCopyToDir( sSrc = sScratchEmptyDirHst, sDst = sScratchGstPathNotFound), tdTestResultFailure() ],
            # Valid destination, but source file/dir not found:
            [ tdTestCopyToFile(sSrc = sScratchHstNotExist, sDst = oTestVm.pathJoin(sScratchGst, 'dstfile')),
                               tdTestResultFailure() ],
            [ tdTestCopyToDir( sSrc = sScratchHstNotExist, sDst = sScratchGst), tdTestResultFailure() ],
            # Wrong type:
            [ tdTestCopyToFile(sSrc = sScratchEmptyDirHst, sDst = oTestVm.pathJoin(sScratchGst, 'dstfile')),
              tdTestResultFailure() ],
            [ tdTestCopyToDir( sSrc = sBigFileHst, sDst = sScratchGst), tdTestResultFailure() ],
        ]);
        # Invalid characters in destination or source path:
        if sScratchGstInvalid is not None:
            atTests.extend([
                [ tdTestCopyToFile(sSrc = sBigFileHst, sDst = oTestVm.pathJoin(sScratchGst, sScratchGstInvalid)),
                  tdTestResultFailure() ],
                [ tdTestCopyToDir( sSrc = sScratchEmptyDirHst, sDst = oTestVm.pathJoin(sScratchGst, sScratchGstInvalid)),
                  tdTestResultFailure() ],
        ]);
        if sScratchHstInvalid is not None:
            atTests.extend([
                [ tdTestCopyToFile(sSrc = os.path.join(self.oTstDrv.sScratchPath, sScratchHstInvalid), sDst = sScratchGst),
                  tdTestResultFailure() ],
                [ tdTestCopyToDir( sSrc = os.path.join(self.oTstDrv.sScratchPath, sScratchHstInvalid), sDst = sScratchGst),
                  tdTestResultFailure() ],
        ]);

        #
        # Single file handling.
        #
        atTests.extend([
            [ tdTestCopyToFile(sSrc = sBigFileHst,   sDst = oTestVm.pathJoin(sScratchGst, 'HostGABig.dat')),
              tdTestResultSuccess() ],
            [ tdTestCopyToFile(sSrc = sBigFileHst,   sDst = oTestVm.pathJoin(sScratchGst, 'HostGABig.dat')),    # Overwrite
              tdTestResultSuccess() ],
            [ tdTestCopyToFile(sSrc = sEmptyFileHst, sDst = oTestVm.pathJoin(sScratchGst, 'HostGABig.dat')),    # Overwrite
              tdTestResultSuccess() ],
        ]);
        if self.oTstDrv.fpApiVer > 5.2: # Copying files into directories via Main is supported only 6.0 and later.
            atTests.extend([
                # Should succeed, as the file isn't there yet on the destination.
                [ tdTestCopyToFile(sSrc = sBigFileHst,   sDst = sScratchGst + oTestVm.pathSep()), tdTestResultSuccess() ],
                # Overwrite the existing file.
                [ tdTestCopyToFile(sSrc = sBigFileHst,   sDst = sScratchGst + oTestVm.pathSep()), tdTestResultSuccess() ],
                # Same file, but with a different name on the destination.
                [ tdTestCopyToFile(sSrc = sEmptyFileHst, sDst = oTestVm.pathJoin(sScratchGst, os.path.split(sBigFileHst)[1])),
                  tdTestResultSuccess() ],                                                                  # Overwrite
            ]);

        if oTestVm.isWindows():
            # Copy to a Windows alternative data stream (ADS).
            atTests.extend([
                [ tdTestCopyToFile(sSrc = sBigFileHst,   sDst = oTestVm.pathJoin(sScratchGst, 'HostGABig.dat:ADS-Test')),
                  tdTestResultSuccess() ],
                [ tdTestCopyToFile(sSrc = sEmptyFileHst, sDst = oTestVm.pathJoin(sScratchGst, 'HostGABig.dat:ADS-Test')),
                  tdTestResultSuccess() ],
            ]);

        #
        # Directory handling.
        #
        if self.oTstDrv.fpApiVer > 5.2: # Copying directories via Main is supported only in versions > 5.2.
            atTests.extend([
                # Without a trailing slash added to the destination this should fail,
                # as the destination directory already exists.
                [ tdTestCopyToDir(sSrc = sScratchEmptyDirHst, sDst = sScratchDstDir1Gst),   tdTestResultFailure() ],
                # Same existing host directory, but this time with DirectoryCopyFlag_CopyIntoExisting set.
                # This should copy the contents of oEmptyDirGst to sScratchDstDir1Gst (empty, but anyway).
                [ tdTestCopyToDir(sSrc = sScratchEmptyDirHst, sDst = sScratchDstDir1Gst,
                                  afFlags = [vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]),  tdTestResultSuccess() ],
                # Try again.
                [ tdTestCopyToDir(sSrc = sScratchEmptyDirHst, sDst = sScratchDstDir1Gst,
                                  afFlags = [vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]),  tdTestResultSuccess() ],
                # With a trailing slash added to the destination, copy the empty guest directory
                # (should end up as sScratchDstDir2Gst/empty):
                [ tdTestCopyToDir(sSrc = sScratchEmptyDirHst, sDst = sScratchDstDir2Gst + oTestVm.pathSep()),
                  tdTestResultSuccess() ],
                # Repeat -- this time it should fail, as the destination directory already exists (and
                # DirectoryCopyFlag_CopyIntoExisting is not specified):
                [ tdTestCopyToDir(sSrc = sScratchEmptyDirHst, sDst = sScratchDstDir2Gst + oTestVm.pathSep()),
                  tdTestResultFailure() ],
                # Add the DirectoryCopyFlag_CopyIntoExisting flag being set and it should work (again).
                [ tdTestCopyToDir(sSrc = sScratchEmptyDirHst, sDst = sScratchDstDir2Gst + oTestVm.pathSep(),
                                  afFlags = [ vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
                # Copy with a different destination name just for the heck of it:
                [ tdTestCopyToDir(sSrc = sScratchEmptyDirHst, sDst = oTestVm.pathJoin(sScratchDstDir2Gst, 'empty2')),
                  tdTestResultSuccess() ],
            ]);
            atTests.extend([
                # Now the same using a directory with files in it:
                [ tdTestCopyToDir(sSrc = sScratchNonEmptyDirHst, sDst = sScratchDstDir3Gst,
                                  afFlags = [vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
                # Again.
                [ tdTestCopyToDir(sSrc = sScratchNonEmptyDirHst, sDst = sScratchDstDir3Gst,
                                  afFlags = [vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
            ]);
            atTests.extend([
                # Copy the entire test tree:
                [ tdTestCopyToDir(sSrc = sScratchTreeDirHst, sDst = sScratchDstDir4Gst + oTestVm.pathSep()),
                  tdTestResultSuccess() ],
                # Again, should fail this time.
                [ tdTestCopyToDir(sSrc = sScratchTreeDirHst, sDst = sScratchDstDir4Gst + oTestVm.pathSep()),
                  tdTestResultFailure() ],
                # Works again, as DirectoryCopyFlag_CopyIntoExisting is specified.
                [ tdTestCopyToDir(sSrc = sScratchTreeDirHst, sDst = sScratchDstDir4Gst + oTestVm.pathSep(),
                                  afFlags = [ vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
            ]);
        #
        # Dotdot path handling.
        #
        if self.oTstDrv.fpApiVer >= 6.1:
            atTests.extend([
                # Test if copying stuff from a host dotdot ".." directory works.
                [ tdTestCopyToFile(sSrc = sScratchDotDotFileHst, sDst = sScratchDstDir1Gst + oTestVm.pathSep()),
                  tdTestResultSuccess() ],
                # Test if copying stuff from the host to a guest's dotdot ".." directory works.
                # That should fail on destinations.
                [ tdTestCopyToFile(sSrc = sEmptyFileHst, sDst = sScratchDotDotDirGst), tdTestResultFailure() ],
            ]);

        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0]; # tdTestCopyTo
            oCurRes  = tTest[1]; # tdTestResult
            reporter.log('Testing #%d, sSrc=%s, sDst=%s, afFlags=%s ...'
                         % (i, limitString(oCurTest.sSrc), limitString(oCurTest.sDst), oCurTest.afFlags));

            oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
            if not fRc:
                break;
            fRc, oCurGuestSession = oCurTest.createSession('testGuestCtrlCopyTo: Test #%d' % (i,));
            if fRc is not True:
                fRc = reporter.error('Test #%d failed: Could not create session' % (i,));
                break;

            fRc2 = False;
            if isinstance(oCurTest, tdTestCopyToFile):
                fRc2 = self.gctrlCopyFileTo(oCurGuestSession, oCurTest.sSrc, oCurTest.sDst, oCurTest.afFlags, oCurRes.fRc);
            else:
                fRc2 = self.gctrlCopyDirTo(oCurGuestSession, oCurTest.sSrc, oCurTest.sDst, oCurTest.afFlags, oCurRes.fRc);
            if fRc2 is not oCurRes.fRc:
                fRc = reporter.error('Test #%d failed: Got %s, expected %s' % (i, fRc2, oCurRes.fRc));

            fRc = oCurTest.closeSession() and fRc;

        return (fRc, oTxsSession);

    def testGuestCtrlCopyFrom(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests copying files from guest to the host.
        """

        reporter.log2('Entered');

        #
        # Paths.
        #
        sScratchHst             = os.path.join(self.oTstDrv.sScratchPath, "testGctrlCopyFrom");
        sScratchDstDir1Hst      = os.path.join(sScratchHst, "dstdir1");
        sScratchDstDir2Hst      = os.path.join(sScratchHst, "dstdir2");
        sScratchDstDir3Hst      = os.path.join(sScratchHst, "dstdir3");
        sScratchDstDir4Hst      = os.path.join(sScratchHst, "dstdir4");
        # os.path.join() is too clever for "..", so we just build up the path here ourselves.
        sScratchDotDotDirHst    = sScratchHst + os.path.sep + '..' + os.path.sep;
        oExistingFileGst        = self.oTestFiles.chooseRandomFile();
        oNonEmptyDirGst         = self.oTestFiles.chooseRandomDirFromTree(fNonEmpty = True);
        oTreeDirGst             = self.oTestFiles.oTreeDir;
        oEmptyDirGst            = self.oTestFiles.oEmptyDir;

        if oTestVm.isWindows() or oTestVm.isOS2():
            sScratchGstInvalid  = "?*|<invalid-name>";
        else:
            sScratchGstInvalid  = None;
        if utils.getHostOs() in ('win', 'os2'):
            sScratchHstInvalid  = "?*|<invalid-name>";
        else:
            sScratchHstInvalid  = None;

        sScratchDotDotDirGst = oTestVm.pathJoin(oEmptyDirGst.sPath, '..');

        if os.path.exists(sScratchHst):
            if base.wipeDirectory(sScratchHst) != 0:
                return reporter.error('Failed to wipe "%s"' % (sScratchHst,));
        else:
            try:
                os.mkdir(sScratchHst);
            except:
                return reporter.errorXcpt('os.mkdir(%s)' % (sScratchHst, ));

        reporter.log2('Creating host sub dirs ...');

        for sSubDir in (sScratchDstDir1Hst, sScratchDstDir2Hst, sScratchDstDir3Hst, sScratchDstDir4Hst):
            try:
                os.mkdir(sSubDir);
            except:
                return reporter.errorXcpt('os.mkdir(%s)' % (sSubDir, ));

        reporter.log2('Defining tests ...');

        #
        # Bad parameter tests.
        #
        atTests = [
            # Missing both source and destination:
            [ tdTestCopyFromFile(), tdTestResultFailure() ],
            [ tdTestCopyFromDir(),  tdTestResultFailure() ],
            # Missing source.
            [ tdTestCopyFromFile(sDst = os.path.join(sScratchHst, 'somefile')), tdTestResultFailure() ],
            [ tdTestCopyFromDir( sDst = sScratchHst), tdTestResultFailure() ],
            # Missing destination.
            [ tdTestCopyFromFile(oSrc = oExistingFileGst), tdTestResultFailure() ],
            [ tdTestCopyFromDir( sSrc = self.oTestFiles.oManyDir.sPath), tdTestResultFailure() ],
            # Invalid flags:
            [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = os.path.join(sScratchHst, 'somefile'), afFlags = [0x40000000]),
              tdTestResultFailure() ],
            [ tdTestCopyFromDir( oSrc = oEmptyDirGst, sDst = os.path.join(sScratchHst, 'somedir'),  afFlags = [ 0x40000000] ),
              tdTestResultFailure() ],
            # Non-existing sources:
            [ tdTestCopyFromFile(sSrc = oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'no-such-file-or-directory'),
                                 sDst = os.path.join(sScratchHst, 'somefile')), tdTestResultFailure() ],
            [ tdTestCopyFromDir( sSrc = oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'no-such-file-or-directory'),
                                 sDst = os.path.join(sScratchHst, 'somedir')), tdTestResultFailure() ],
            [ tdTestCopyFromFile(sSrc = oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'no-such-directory', 'no-such-file'),
                                 sDst = os.path.join(sScratchHst, 'somefile')), tdTestResultFailure() ],
            [ tdTestCopyFromDir( sSrc = oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, 'no-such-directory', 'no-such-subdir'),
                                 sDst = os.path.join(sScratchHst, 'somedir')), tdTestResultFailure() ],
            # Non-existing destinations:
            [ tdTestCopyFromFile(oSrc = oExistingFileGst,
                                 sDst = os.path.join(sScratchHst, 'no-such-directory', 'somefile') ), tdTestResultFailure() ],
            [ tdTestCopyFromDir( oSrc = oEmptyDirGst, sDst = os.path.join(sScratchHst, 'no-such-directory', 'somedir') ),
              tdTestResultFailure() ],
            [ tdTestCopyFromFile(oSrc = oExistingFileGst,
                                 sDst = os.path.join(sScratchHst, 'no-such-directory-slash' + os.path.sep)),
              tdTestResultFailure() ],
            # Wrong source type:
            [ tdTestCopyFromFile(oSrc = oNonEmptyDirGst, sDst = os.path.join(sScratchHst, 'somefile') ), tdTestResultFailure() ],
            [ tdTestCopyFromDir(oSrc = oExistingFileGst, sDst = os.path.join(sScratchHst, 'somedir') ), tdTestResultFailure() ],
        ];
        # Bogus names:
        if sScratchHstInvalid:
            atTests.extend([
                [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = os.path.join(sScratchHst, sScratchHstInvalid)),
                  tdTestResultFailure() ],
                [ tdTestCopyFromDir( sSrc = self.oTestFiles.oManyDir.sPath, sDst = os.path.join(sScratchHst, sScratchHstInvalid)),
                  tdTestResultFailure() ],
            ]);
        if sScratchGstInvalid:
            atTests.extend([
                [ tdTestCopyFromFile(sSrc = oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, sScratchGstInvalid),
                                     sDst = os.path.join(sScratchHst, 'somefile')), tdTestResultFailure() ],
                [ tdTestCopyFromDir( sSrc = oTestVm.pathJoin(self.oTestFiles.oRoot.sPath, sScratchGstInvalid),
                                     sDst = os.path.join(sScratchHst, 'somedir')), tdTestResultFailure() ],
            ]);

        #
        # Single file copying.
        #
        atTests.extend([
            # Should succeed, as the file isn't there yet on the destination.
            [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = os.path.join(sScratchHst, 'copyfile1')), tdTestResultSuccess() ],
            # Overwrite the existing file.
            [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = os.path.join(sScratchHst, 'copyfile1')), tdTestResultSuccess() ],
            # Same file, but with a different name on the destination.
            [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = os.path.join(sScratchHst, 'copyfile2')), tdTestResultSuccess() ],
        ]);

        if self.oTstDrv.fpApiVer > 5.2: # Copying files into directories via Main is supported only 6.0 and later.
            # Copy into a directory.
            atTests.extend([
                # This should fail, as sScratchHst exists and is a directory.
                [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = sScratchHst), tdTestResultFailure() ],
                # Same existing host directory, but this time with a trailing slash.
                # This should succeed, as the file isn't there yet on the destination.
                [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = sScratchHst + os.path.sep), tdTestResultSuccess() ],
                # Overwrite the existing file.
                [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = sScratchHst + os.path.sep), tdTestResultSuccess() ],
            ]);

        #
        # Directory handling.
        #
        if self.oTstDrv.fpApiVer > 5.2: # Copying directories via Main is supported only in versions > 5.2.
            atTests.extend([
                # Without a trailing slash added to the destination this should fail,
                # as the destination directory already exist.
                [ tdTestCopyFromDir(sSrc = oEmptyDirGst.sPath, sDst = sScratchDstDir1Hst), tdTestResultFailure() ],
                # Same existing host directory, but this time with DirectoryCopyFlag_CopyIntoExisting set.
                # This should copy the contents of oEmptyDirGst to sScratchDstDir1Hst (empty, but anyway).
                [ tdTestCopyFromDir(sSrc = oEmptyDirGst.sPath, sDst = sScratchDstDir1Hst,
                                    afFlags = [ vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
                # Try again.
                [ tdTestCopyFromDir(sSrc = oEmptyDirGst.sPath, sDst = sScratchDstDir1Hst,
                                    afFlags = [ vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
                # With a trailing slash added to the destination, copy the empty guest directory
                # (should end up as sScratchHst/empty):
                [ tdTestCopyFromDir(oSrc = oEmptyDirGst, sDst = sScratchDstDir2Hst + os.path.sep), tdTestResultSuccess() ],
                # Repeat -- this time it should fail, as the destination directory already exists (and
                # DirectoryCopyFlag_CopyIntoExisting is not specified):
                [ tdTestCopyFromDir(oSrc = oEmptyDirGst, sDst = sScratchDstDir2Hst + os.path.sep), tdTestResultFailure() ],
                # Add the DirectoryCopyFlag_CopyIntoExisting flag being set and it should work (again).
                [ tdTestCopyFromDir(oSrc = oEmptyDirGst, sDst = sScratchDstDir2Hst + os.path.sep,
                                    afFlags = [ vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
                # Copy with a different destination name just for the heck of it:
                [ tdTestCopyFromDir(sSrc = oEmptyDirGst.sPath, sDst = os.path.join(sScratchDstDir2Hst, 'empty2'),
                                    fIntoDst = True),
                  tdTestResultSuccess() ],
            ]);
            atTests.extend([
                # Now the same using a directory with files in it:
                [ tdTestCopyFromDir(oSrc = oNonEmptyDirGst, sDst = sScratchDstDir3Hst + os.path.sep), tdTestResultSuccess() ],
                # Again.
                [ tdTestCopyFromDir(oSrc = oNonEmptyDirGst, sDst = sScratchDstDir3Hst, fIntoDst = True,
                                    afFlags = [ vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
            ]);
            atTests.extend([
                # Copy the entire test tree:
                [ tdTestCopyFromDir(oSrc = oTreeDirGst, sDst = sScratchDstDir4Hst + os.path.sep), tdTestResultSuccess() ],
                # Again, should fail this time.
                [ tdTestCopyFromDir(oSrc = oTreeDirGst, sDst = sScratchDstDir4Hst + os.path.sep), tdTestResultFailure() ],
                # Works again, as DirectoryCopyFlag_CopyIntoExisting is specified.
                [ tdTestCopyFromDir(oSrc = oTreeDirGst, sDst = sScratchDstDir4Hst + os.path.sep,
                                    afFlags = [ vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]), tdTestResultSuccess() ],
            ]);
        #
        # Dotdot path handling.
        #
        if self.oTstDrv.fpApiVer >= 6.1:
            atTests.extend([
                # Test if copying stuff from a guest dotdot ".." directory works.
                [ tdTestCopyFromDir(sSrc = sScratchDotDotDirGst, sDst = sScratchDstDir1Hst + os.path.sep,
                                    afFlags = [ vboxcon.DirectoryCopyFlag_CopyIntoExisting, ]),
                  tdTestResultFailure() ],
                # Test if copying stuff from the guest to a host's dotdot ".." directory works.
                # That should fail on destinations.
                [ tdTestCopyFromFile(oSrc = oExistingFileGst, sDst = sScratchDotDotDirHst), tdTestResultFailure() ],
            ]);

        reporter.log2('Executing tests ...');

        #
        # Execute the tests.
        #
        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0]
            oCurRes  = tTest[1] # type: tdTestResult
            if isinstance(oCurTest, tdTestCopyFrom):
                reporter.log('Testing #%d, %s: sSrc="%s", sDst="%s", afFlags="%s" ...'
                             % (i, "directory" if isinstance(oCurTest, tdTestCopyFromDir) else "file",
                                limitString(oCurTest.sSrc), limitString(oCurTest.sDst), oCurTest.afFlags,));
            else:
                reporter.log('Testing #%d, tdTestRemoveHostDir "%s"  ...' % (i, oCurTest.sDir,));
            if isinstance(oCurTest, tdTestCopyFromDir) and self.oTstDrv.fpApiVer < 6.0:
                reporter.log('Skipping directoryCopyFromGuest test, not implemented in %s' % (self.oTstDrv.fpApiVer,));
                continue;

            if isinstance(oCurTest, tdTestRemoveHostDir):
                fRc = oCurTest.execute(self.oTstDrv, oSession, oTxsSession, oTestVm, 'testing #%d' % (i,));
            else:
                fRc = oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);
                if not fRc:
                    break;
                fRc2, oCurGuestSession = oCurTest.createSession('testGuestCtrlCopyFrom: Test #%d' % (i,));
                if fRc2 is not True:
                    fRc = reporter.error('Test #%d failed: Could not create session' % (i,));
                    break;

                if isinstance(oCurTest, tdTestCopyFromFile):
                    fRc2 = self.gctrlCopyFileFrom(oCurGuestSession, oCurTest, oCurRes.fRc);
                else:
                    fRc2 = self.gctrlCopyDirFrom(oCurGuestSession, oCurTest, oCurRes.fRc);

                if fRc2 != oCurRes.fRc:
                    fRc = reporter.error('Test #%d failed: Got %s, expected %s' % (i, fRc2, oCurRes.fRc));

                fRc = oCurTest.closeSession() and fRc;

        return (fRc, oTxsSession);

    def testGuestCtrlUpdateAdditions(self, oSession, oTxsSession, oTestVm): # pylint: disable=too-many-locals
        """
        Tests updating the Guest Additions inside the guest.

        """

        # Skip test for updating Guest Additions if we run on a too old (Windows) guest.
        ##
        ## @todo make it work everywhere!
        ##
        if oTestVm.sKind in ('WindowsNT4', 'Windows2000', 'WindowsXP', 'Windows2003'):
            reporter.log("Skipping updating GAs on old windows vm (sKind=%s)" % (oTestVm.sKind,));
            return (None, oTxsSession);
        if oTestVm.isOS2():
            reporter.log("Skipping updating GAs on OS/2 guest");
            return (None, oTxsSession);

        sVBoxValidationKitIso = self.oTstDrv.sVBoxValidationKitIso;
        if not os.path.isfile(sVBoxValidationKitIso):
            return reporter.log('Validation Kit .ISO not found at "%s"' % (sVBoxValidationKitIso,));

        sScratch = os.path.join(self.oTstDrv.sScratchPath, "testGctrlUpdateAdditions");
        try:
            os.makedirs(sScratch);
        except OSError as e:
            if e.errno != errno.EEXIST:
                return reporter.error('Failed: Unable to create scratch directory \"%s\"' % (sScratch,));
        reporter.log('Scratch path is: %s' % (sScratch,));

        atTests = [];
        if oTestVm.isWindows():
            atTests.extend([
                # Source is missing.
                [ tdTestUpdateAdditions(sSrc = ''), tdTestResultFailure() ],

                # Wrong flags.
                [ tdTestUpdateAdditions(sSrc = self.oTstDrv.getGuestAdditionsIso(),
                                        afFlags = [ 1234 ]), tdTestResultFailure() ],

                # Non-existing .ISO.
                [ tdTestUpdateAdditions(sSrc = "non-existing.iso"), tdTestResultFailure() ],

                # Wrong .ISO.
                [ tdTestUpdateAdditions(sSrc = sVBoxValidationKitIso), tdTestResultFailure() ],

                # The real thing.
                [ tdTestUpdateAdditions(sSrc = self.oTstDrv.getGuestAdditionsIso()),
                  tdTestResultSuccess() ],
                # Test the (optional) installer arguments. This will extract the
                # installer into our guest's scratch directory.
                [ tdTestUpdateAdditions(sSrc = self.oTstDrv.getGuestAdditionsIso(),
                                        asArgs = [ '/extract', '/D=' + sScratch ]),
                  tdTestResultSuccess() ]
                # Some debg ISO. Only enable locally.
                #[ tdTestUpdateAdditions(
                #                      sSrc = "V:\\Downloads\\VBoxGuestAdditions-r80354.iso"),
                #  tdTestResultSuccess() ]
            ]);
        else:
            reporter.log('No OS-specific tests for non-Windows yet!');

        fRc = True;
        for (i, tTest) in enumerate(atTests):
            oCurTest = tTest[0]  # type: tdTestUpdateAdditions
            oCurRes  = tTest[1]  # type: tdTestResult
            reporter.log('Testing #%d, sSrc="%s", afFlags="%s" ...' % (i, oCurTest.sSrc, oCurTest.afFlags,));

            oCurTest.setEnvironment(oSession, oTxsSession, oTestVm);

            fRc, _ = oCurTest.createSession('Test #%d' % (i,));
            if fRc is not True:
                fRc = reporter.error('Test #%d failed: Could not create session' % (i,));
                break;

            try:
                oCurProgress = oCurTest.oGuest.updateGuestAdditions(oCurTest.sSrc, oCurTest.asArgs, oCurTest.afFlags);
            except:
                reporter.maybeErrXcpt(oCurRes.fRc, 'Updating Guest Additions exception for sSrc="%s", afFlags="%s":'
                                      % (oCurTest.sSrc, oCurTest.afFlags,));
                fRc = False;
            else:
                if oCurProgress is not None:
                    oWrapperProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr,
                                                                    self.oTstDrv, "gctrlUpGA");
                    oWrapperProgress.wait();
                    assert oWrapperProgress.isCompleted();
                    fRc = oWrapperProgress.isSuccess();
                    if not fRc:
                        oWrapperProgress.logResult(fIgnoreErrors = True);
                else:
                    fRc = reporter.error('No progress object returned');

            oCurTest.closeSession();
            if fRc is oCurRes.fRc:
                if fRc:
                    ## @todo Verify if Guest Additions were really updated (build, revision, ...).
                    ## @todo r=bird: Not possible since you're installing the same GAs as before...
                    ##               Maybe check creation dates on certain .sys/.dll/.exe files?
                    pass;
            else:
                reporter.error('Test #%d failed: Got %s, expected %s' % (i, fRc, oCurRes.fRc));

        return (True, oTxsSession); # Always return True here; errors are counted via the reporter.

    def checkScreenShot(self, iWidth, iHeight, aRGBData):      # pylint: disable=unused-argument
        """
        TBD: Implement basic validation of the captured screenshot content.
        """
        cPixels = iHeight * iWidth

        if cPixels == 0:
            reporter.logXcpt("Empty screenshot");
            return False

        # The simple algoritm below assumes that Windows OS desktop consists of a pixels that
        # are not too dark or bright but usually of blue tint.
        cDesktopPixels = 0
        cDesktopPixelsBlue = 0
        iThreshold = 20
        for i in range(0, cPixels, 4) :
            if sys.version_info[0] >= 3:
                iRed   = aRGBData[i];
                iGreen = aRGBData[i + 1];
                iBlue  = aRGBData[i + 2];
            else: # Python 2.7 treats a pixel data returned by takeScreenShotToArray as a string
                iRed   = ord(aRGBData[i])
                iGreen = ord(aRGBData[i + 1])
                iBlue  = ord(aRGBData[i + 2])

            iBright = (3 * iRed + 6 * iGreen + iBlue) / 10
            if iThreshold < iBright < 255 - iThreshold :
                cDesktopPixels += 1;
                cDesktopPixelsBlue += int(iBlue > iRed and iBlue > iGreen);

        fpRatioDesktop = float(cDesktopPixels) / float(cPixels);
        reporter.log2('Ratio of not too dark or bright pixels %.2f' % (fpRatioDesktop));

        if fpRatioDesktop > 0.1:
            fpRatioBlue = float(cDesktopPixelsBlue) / float(cDesktopPixels);
            reporter.log2('Ratio of blue pixels %.2f ' % (fpRatioBlue));
            if fpRatioBlue > 0.5:
                return True

        return True # Always return True until the parameters will be calibrated.

    def testGuestCtrl3D(self, oSession, oTxsSession, oTestVm):  # pylint: disable=unused-argument
        """
        Tests for VMSVGA device.
        """

        if oTestVm.sKind not in ('Windows8_64', 'Windows10', 'Windows10_64', 'Windows11_64'):
            return (True, oTxsSession);

        iScreenId = 0 # TBD: Use a loop to iterate and check all virtual displays
        try:
            if self.oTstDrv.fpApiVer >= 5.0:
                iWidth, iHeight, _, _, _, _ = oSession.o.console.display.getScreenResolution(iScreenId);
            else:
                iWidth, iHeight, _, _, _ = oSession.o.console.display.getScreenResolution(iScreenId);

            aRGBData = oSession.o.console.display.takeScreenShotToArray(iScreenId, iWidth, iHeight,
                                                                        vboxcon.BitmapFormat_RGBA);
        except:
            reporter.logXcpt("Unable to take screenshot");
            return False

        reporter.log2('Got screenshot (%s x %s) having %s bytes' % (iWidth, iHeight, len(aRGBData)));
        # @todo r=aeichner Where is this result incorporated in the test result?
        #                      It gets overwritten afterwards without being taken into account
        fRc = self.checkScreenShot(iWidth, iHeight, aRGBData);

        fRc = fRc and self.oTstDrv.txsRunTest(oTxsSession, 'Checking DX11 feature level', 30 * 1000,
                                      '${CDROM}/${OS/ARCH}/ntDisplay${EXESUFF}', ('ntDisplay', ));
        fRc = True; # TBD: Fix for Unattended tests when the ValidationKit.iso is mounted as drive D:
        return (fRc, oTxsSession);

class tdAddGuestCtrl(vbox.TestDriver):                                         # pylint: disable=too-many-instance-attributes,too-many-public-methods
    """
    Guest control using VBoxService on the guest.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.oTestVmSet = self.oTestVmManager.getSmokeVmSet('nat');
        self.asRsrcs    = None;
        self.fQuick     = False; # Don't skip lengthly tests by default.
        self.addSubTestDriver(SubTstDrvAddGuestCtrl(self));

    #
    # Overridden methods.
    #
    def showUsage(self):
        """
        Shows the testdriver usage.
        """
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdAddGuestCtrl Options:');
        reporter.log('  --quick');
        reporter.log('      Same as --virt-modes hwvirt --cpu-counts 1.');
        return rc;

    def parseOption(self, asArgs, iArg):                                        # pylint: disable=too-many-branches,too-many-statements
        """
        Parses the testdriver arguments from the command line.
        """
        if asArgs[iArg] == '--quick':
            self.parseOption(['--virt-modes', 'hwvirt'], 0);
            self.parseOption(['--cpu-counts', '1'], 0);
            self.fQuick = True;
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def actionConfig(self):
        if not self.importVBoxApi(): # So we can use the constant below.
            return False;

        eNic0AttachType = vboxcon.NetworkAttachmentType_NAT;
        sGaIso = self.getGuestAdditionsIso();
        return self.oTestVmSet.actionConfig(self, eNic0AttachType = eNic0AttachType, sDvdImage = sGaIso);

    def actionExecute(self):
        return self.oTestVmSet.actionExecute(self, self.testOneCfg);

    #
    # Test execution helpers.
    #
    def testOneCfg(self, oVM, oTestVm): # pylint: disable=too-many-statements
        """
        Runs the specified VM thru the tests.

        Returns a success indicator on the general test execution. This is not
        the actual test result.
        """

        self.logVmInfo(oVM);

        fRc = True;
        oSession, oTxsSession = self.startVmAndConnectToTxsViaTcp(oTestVm.sVmName, fCdWait = False);
        reporter.log("TxsSession: %s" % (oTxsSession,));
        if oSession is not None:
            fRc, oTxsSession = self.aoSubTstDrvs[0].testIt(oTestVm, oSession, oTxsSession);
            self.terminateVmBySession(oSession);
        else:
            fRc = False;
        return fRc;

    def onExit(self, iRc):
        return vbox.TestDriver.onExit(self, iRc);

    def gctrlReportError(self, progress):
        """
        Helper function to report an error of a
        given progress object.
        """
        if progress is None:
            reporter.log('No progress object to print error for');
        else:
            errInfo = progress.errorInfo;
            if errInfo:
                reporter.log('%s' % (errInfo.text,));
        return False;

    def gctrlGetRemainingTime(self, msTimeout, msStart):
        """
        Helper function to return the remaining time (in ms)
        based from a timeout value and the start time (both in ms).
        """
        if msTimeout == 0:
            return 0xFFFFFFFE; # Wait forever.
        msElapsed = base.timestampMilli() - msStart;
        if msElapsed > msTimeout:
            return 0; # No time left.
        return msTimeout - msElapsed;

    def testGuestCtrlManual(self, oSession, oTxsSession, oTestVm):                # pylint: disable=too-many-locals,too-many-statements,unused-argument,unused-variable
        """
        For manually testing certain bits.
        """

        reporter.log('Manual testing ...');
        fRc = True;

        sUser = 'Administrator';
        sPassword = 'password';

        oGuest = oSession.o.console.guest;
        oGuestSession = oGuest.createSession(sUser,
                                             sPassword,
                                             "", "Manual Test");

        aWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start ];
        _ = oGuestSession.waitForArray(aWaitFor, 30 * 1000);

        #sCmd = self.getGuestSystemShell(oTestVm);
        #asArgs = [ sCmd, '/C', 'dir', '/S', 'c:\\windows' ];
        #aEnv = [];
        #afFlags = [];

        # Fix this once being used (again).
        #for _ in xrange(100):
        #    oProc = self.processCreateWrapper(oGuestSession, sCmd, asArgs if self.fpApiVer >= 5.0 else asArgs[1:],
        #                                      "", # Working directory.
        #                                      aEnv, afFlags, 30 * 1000);
        #
        #    aWaitFor = [ vboxcon.ProcessWaitForFlag_Terminate ];
        #    _ = oProc.waitForArray(aWaitFor, 30 * 1000);

        oGuestSession.close();
        oGuestSession = None;

        time.sleep(5);

        oSession.o.console.PowerDown();

        return (fRc, oTxsSession);

if __name__ == '__main__':
    sys.exit(tdAddGuestCtrl().main(sys.argv));
