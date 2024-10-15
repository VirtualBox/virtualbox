#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Autostart testcase using <please-tell-what-I-am-doing>.
"""

__copyright__ = \
"""
Copyright (C) 2013-2024 Oracle and/or its affiliates.

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
__version__ = "$Id: tdAutostart1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $"

# Standard Python imports.
import os;
import sys;
import re;

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from testdriver import vbox;
from testdriver import vboxcon;
from testdriver import vboxtestvms;
from testdriver import vboxwrappers;

class VBoxManageStdOutWrapper(object):
    """ Parser for VBoxManage list runningvms """

    def __init__(self):
        self.sVmRunning = '';

    def __del__(self):
        self.close();

    def close(self):
        """file.close"""
        return;

    def read(self, cb):
        """file.read"""
        _ = cb;
        return "";

    def write(self, sText):
        """VBoxManage stdout write"""
        if sText is None:
            return None;
        try:    sText = str(sText); # pylint: disable=redefined-variable-type
        except: pass;
        asLines = sText.splitlines();
        for sLine in asLines:
            sLine = sLine.strip();
            reporter.log('Logging: ' + sLine);
                # Extract the value
            idxVmNameStart = sLine.find('"');
            if idxVmNameStart == -1:
                raise Exception('VBoxManageStdOutWrapper: Invalid output');
            idxVmNameStart += 1;
            idxVmNameEnd = idxVmNameStart;
            while sLine[idxVmNameEnd] != '"':
                idxVmNameEnd += 1;
            self.sVmRunning = sLine[idxVmNameStart:idxVmNameEnd];
            reporter.log('Logging: ' + self.sVmRunning);
        return None;

class tdAutostartOs(vboxtestvms.BaseTestVm):
    """
    Base autostart helper class to provide common methods.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None):
        vboxtestvms.BaseTestVm.__init__(self, sVmName, oSet = oSet, sKind = sKind);
        self.oTstDrv = oTstDrv;
        self.sHdd = sHdd;
        self.eNic0Type = eNic0Type;
        self.cMbRam = cMbRam;
        self.cCpus = cCpus;
        self.fPae = fPae;
        self.sGuestAdditionsIso = sGuestAdditionsIso;
        self.asTestBuildDirs = oTstDrv.asTestBuildDirs;
        self.sVBoxInstaller = "";
        self.asVirtModesSup = ['hwvirt-np',];
        self.asParavirtModesSup = ['default',];

    def _findFile(self, sRegExp, asTestBuildDirs):
        """
        Returns a filepath based on the given regex and paths to look into
        or None if no matching file is found.
        """
        oRegExp = re.compile(sRegExp);
        for sTestBuildDir in asTestBuildDirs:
            try:
                #return most recent file if there are several ones matching the pattern
                asFiles = [s for s in os.listdir(sTestBuildDir)
                           if os.path.isfile(os.path.join(sTestBuildDir, s))];
                asFiles = (s for s in asFiles
                           if oRegExp.match(os.path.basename(s))
                           and os.path.exists(sTestBuildDir + '/' + s));
                asFiles = sorted(asFiles, reverse = True,
                                 key = lambda s, sTstBuildDir = sTestBuildDir: os.path.getmtime(os.path.join(sTstBuildDir, s)));
                if asFiles:
                    return sTestBuildDir + '/' + asFiles[0];
            except:
                pass;
        reporter.error('Failed to find a file matching "%s" in %s.' % (sRegExp, ','.join(asTestBuildDirs)));
        return None;

    def _createAutostartCfg(self, sDefaultPolicy = 'allow', asUserAllow = (), asUserDeny = ()):
        """
        Creates a autostart config for VirtualBox
        """
        sVBoxCfg = 'default_policy=' + sDefaultPolicy + '\n';
        for sUserAllow in asUserAllow:
            sVBoxCfg = sVBoxCfg + sUserAllow + ' = {\n allow = true\n }\n';
        for sUserDeny in asUserDeny:
            sVBoxCfg = sVBoxCfg + sUserDeny + ' = {\n allow = false\n }\n';
        return sVBoxCfg;

    def _waitAdditionsIsRunning(self, oGuest, fWaitTrayControl):
        """
        Check is the additions running
        """
        cAttempt = 0;
        fRc = False;
        while cAttempt < 30:
            fRc = oGuest.additionsRunLevel in [vboxcon.AdditionsRunLevelType_Userland,
                                               vboxcon.AdditionsRunLevelType_Desktop];
            if fRc:
                eServiceStatus, _ = oGuest.getFacilityStatus(vboxcon.AdditionsFacilityType_VBoxService);
                fRc = eServiceStatus == vboxcon.AdditionsFacilityStatus_Active;
                if fRc and not fWaitTrayControl:
                    break;
                if fRc:
                    eServiceStatus, _ = oGuest.getFacilityStatus(vboxcon.AdditionsFacilityType_VBoxTrayClient);
                    fRc = eServiceStatus == vboxcon.AdditionsFacilityStatus_Active;
                    if fRc:
                        break;
            self.oTstDrv.sleep(10);
            cAttempt += 1;
        return fRc;

    def createSession(self, oSession, sName, sUser, sPassword, cMsTimeout = 10 * 1000, fIsError = True):
        """
        Creates (opens) a guest session.
        Returns (True, IGuestSession) on success or (False, None) on failure.
        """
        oGuest = oSession.o.console.guest;
        if sName is None:
            sName = "<untitled>";
        reporter.log('Creating session "%s" ...' % (sName,));
        try:
            oGuestSession = oGuest.createSession(sUser, sPassword, '', sName);
        except:
            # Just log, don't assume an error here (will be done in the main loop then).
            reporter.maybeErrXcpt(fIsError, 'Creating a guest session "%s" failed; sUser="%s", pw="%s"'
                                  % (sName, sUser, sPassword));
            return (False, None);
        reporter.log('Waiting for session "%s" to start within %dms...' % (sName, cMsTimeout));
        aeWaitFor = [ vboxcon.GuestSessionWaitForFlag_Start, ];
        try:
            waitResult = oGuestSession.waitForArray(aeWaitFor, cMsTimeout);
            #
            # Be nice to Guest Additions < 4.3: They don't support session handling and
            # therefore return WaitFlagNotSupported.
            #
            if waitResult not in (vboxcon.GuestSessionWaitResult_Start, vboxcon.GuestSessionWaitResult_WaitFlagNotSupported):
                # Just log, don't assume an error here (will be done in the main loop then).
                reporter.maybeErr(fIsError, 'Session did not start successfully, returned wait result: %d' % (waitResult,));
                return (False, None);
            reporter.log('Session "%s" successfully started' % (sName,));
        except:
            # Just log, don't assume an error here (will be done in the main loop then).
            reporter.maybeErrXcpt(fIsError, 'Waiting for guest session "%s" (usr=%s;pw=%s) to start failed:'
                                  % (sName, sUser, sPassword,));
            return (False, None);
        return (True, oGuestSession);

    def closeSession(self, oGuestSession, fIsError = True):
        """
        Closes the guest session.
        """
        if oGuestSession is not None:
            try:
                sName = oGuestSession.name;
            except:
                return reporter.errorXcpt();
            reporter.log('Closing session "%s" ...' % (sName,));
            try:
                oGuestSession.close();
                oGuestSession = None;
            except:
                # Just log, don't assume an error here (will be done in the main loop then).
                reporter.maybeErrXcpt(fIsError, 'Closing guest session "%s" failed:' % (sName,));
                return False;
        return True;

    def guestProcessExecute(self, oGuestSession, sTestName, cMsTimeout, sExecName, asArgs = (),
                            fGetStdOut = True, fIsError = True):
        """
        Helper function to execute a program on a guest, specified in the current test.
        Returns (True, ProcessStatus, ProcessExitCode, ProcessStdOutBuffer) on success or (False, 0, 0, None) on failure.
        """
        _ = sTestName;
        fRc = True; # Be optimistic.
        reporter.log2('Using session user=%s, name=%s, timeout=%d'
                      % (oGuestSession.user, oGuestSession.name, oGuestSession.timeout,));
        #
        # Start the process:
        #
        reporter.log2('Executing sCmd=%s, timeoutMS=%d, asArgs=%s'
                      % (sExecName, cMsTimeout, asArgs, ));
        fTaskFlags = [];
        if fGetStdOut:
            fTaskFlags = [vboxcon.ProcessCreateFlag_WaitForStdOut,
                          vboxcon.ProcessCreateFlag_WaitForStdErr];
        try:
            oProcess = oGuestSession.processCreate(sExecName,
                                                   asArgs if self.oTstDrv.fpApiVer >= 5.0 else asArgs[1:],
                                                   [], fTaskFlags, cMsTimeout);
        except:
            reporter.maybeErrXcpt(fIsError, 'asArgs=%s' % (asArgs,));
            return (False, 0, 0, None);
        if oProcess is None:
            return (reporter.error('oProcess is None! (%s)' % (asArgs,)), 0, 0, None);
        #time.sleep(5); # try this if you want to see races here.
        # Wait for the process to start properly:
        reporter.log2('Process start requested, waiting for start (%dms) ...' % (cMsTimeout,));
        iPid = -1;
        aeWaitFor = [ vboxcon.ProcessWaitForFlag_Start, ];
        aBuf = None;
        try:
            eWaitResult = oProcess.waitForArray(aeWaitFor, cMsTimeout);
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
                    aeWaitFor = [ vboxcon.ProcessWaitForFlag_Terminate,
                                  vboxcon.ProcessWaitForFlag_StdOut,
                                  vboxcon.ProcessWaitForFlag_StdErr];
                    reporter.log2('Process (PID %d) started, waiting for termination (%dms), aeWaitFor=%s ...'
                                  % (iPid, cMsTimeout, aeWaitFor));
                    acbFdOut = [0,0,0];
                    while True:
                        try:
                            eWaitResult = oProcess.waitForArray(aeWaitFor, cMsTimeout);
                        except KeyboardInterrupt: # Not sure how helpful this is, but whatever.
                            reporter.error('Process (PID %d) execution interrupted' % (iPid,));
                            try: oProcess.close();
                            except: pass;
                            break;
                        except:
                            fRc = reporter.errorXcpt('asArgs=%s' % (asArgs,));
                            break;
                        reporter.log2('Wait returned: %d' % (eWaitResult,));
                        # Process output:
                        for eFdResult, iFd, sFdNm in [ (vboxcon.ProcessWaitResult_StdOut, 1, 'stdout'),
                                                       (vboxcon.ProcessWaitResult_StdErr, 2, 'stderr'), ]:
                            if eWaitResult in (eFdResult, vboxcon.ProcessWaitResult_WaitFlagNotSupported):
                                reporter.log2('Reading %s ...' % (sFdNm,));
                                try:
                                    abBuf = oProcess.read(iFd, 64 * 1024, cMsTimeout);
                                except KeyboardInterrupt: # Not sure how helpful this is, but whatever.
                                    reporter.error('Process (PID %d) execution interrupted' % (iPid,));
                                    try: oProcess.close();
                                    except: pass;
                                except:
                                    pass; ## @todo test for timeouts and fail on anything else!
                                else:
                                    if abBuf:
                                        reporter.log2('Process (PID %d) got %d bytes of %s data' % (iPid, len(abBuf), sFdNm,));
                                        acbFdOut[iFd] += len(abBuf);
                                        ## @todo Figure out how to uniform + append!
                                        sBuf = '';
                                        if sys.version_info >= (2, 7) and isinstance(abBuf, memoryview):
                                            abBuf = abBuf.tobytes();
                                            sBuf  = abBuf.decode("utf-8");
                                        else:
                                            sBuf = str(abBuf);
                                        if aBuf:
                                            aBuf += sBuf;
                                        else:
                                            aBuf = sBuf;
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
        return (fRc, uExitStatus, iExitCode, aBuf);

    def uploadString(self, oGuestSession, sSrcString, sDst):
        """
        Upload the string into guest.
        """
        fRc = True;
        try:
            oFile = oGuestSession.fileOpenEx(sDst, vboxcon.FileAccessMode_ReadWrite, vboxcon.FileOpenAction_CreateOrReplace,
                                             vboxcon.FileSharingMode_All, 0, []);
        except:
            fRc = reporter.errorXcpt('Upload string failed. Could not create and open the file %s' % sDst);
        else:
            try:
                oFile.write(bytearray(sSrcString), 60*1000);
            except:
                fRc = reporter.errorXcpt('Upload string failed. Could not write the string into the file %s' % sDst);
        try:
            oFile.close();
        except:
            fRc = reporter.errorXcpt('Upload string failed. Could not close the file %s' % sDst);
        return fRc;

    def uploadFile(self, oGuestSession, sSrc, sDst):
        """
        Upload the string into guest.
        """
        fRc = True;
        try:
            if self.oTstDrv.fpApiVer >= 5.0:
                oCurProgress = oGuestSession.fileCopyToGuest(sSrc, sDst, [0]);
            else:
                oCurProgress = oGuestSession.copyTo(sSrc, sDst, [0]);
        except:
            reporter.maybeErrXcpt(True, 'Upload file exception for sSrc="%s":'
                                  % (self.sGuestAdditionsIso,));
            fRc = False;
        else:
            if oCurProgress is not None:
                oWrapperProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr, self.oTstDrv, "uploadFile");
                oWrapperProgress.wait();
                if not oWrapperProgress.isSuccess():
                    oWrapperProgress.logResult(fIgnoreErrors = False);
                    fRc = False;
            else:
                fRc = reporter.error('No progress object returned');
        return fRc;

    def downloadFile(self, oGuestSession, sSrc, sDst, fIgnoreErrors = False):
        """
        Get a file (sSrc) from the guest storing it on the host (sDst).
        """
        fRc = True;
        try:
            if self.oTstDrv.fpApiVer >= 5.0:
                oCurProgress = oGuestSession.fileCopyFromGuest(sSrc, sDst, [0]);
            else:
                oCurProgress = oGuestSession.copyFrom(sSrc, sDst, [0]);
        except:
            if not fIgnoreErrors:
                reporter.errorXcpt('Download file exception for sSrc="%s":' % (sSrc,));
            else:
                reporter.log('warning: Download file exception for sSrc="%s":' % (sSrc,));
            fRc = False;
        else:
            if oCurProgress is not None:
                oWrapperProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr,
                                                                self.oTstDrv, "downloadFile");
                oWrapperProgress.wait();
                if not oWrapperProgress.isSuccess():
                    oWrapperProgress.logResult(fIgnoreErrors);
                    fRc = False;
            else:
                if not fIgnoreErrors:
                    reporter.error('No progress object returned');
                else:
                    reporter.log('warning: No progress object returned');
                fRc = False;
        return fRc;

    def downloadFiles(self, oGuestSession, asFiles, fIgnoreErrors = False):
        """
        Convenience function to get files from the guest and stores it
        into the scratch directory for later (manual) review.
        Returns True on success.
        Returns False on failure, logged.
        """
        fRc = True;
        for sGstFile in asFiles:
            ## @todo r=bird: You need to use the guest specific path functions here.
            ##       Best would be to add basenameEx to common/pathutils.py.  See how joinEx
            ##       is used by BaseTestVm::pathJoin and such.
            sTmpFile = os.path.join(self.oTstDrv.sScratchPath, 'tmp-' + os.path.basename(sGstFile));
            reporter.log2('Downloading file "%s" to "%s" ...' % (sGstFile, sTmpFile));
            # First try to remove (unlink) an existing temporary file, as we don't truncate the file.
            try:    os.unlink(sTmpFile);
            except: pass;
            ## @todo Check for already existing files on the host and create a new
            #        name for the current file to download.
            fRc = self.downloadFile(oGuestSession, sGstFile, sTmpFile, fIgnoreErrors);
            if fRc:
                reporter.addLogFile(sTmpFile, 'misc/other', 'guest - ' + sGstFile);
            else:
                if fIgnoreErrors is not True:
                    reporter.error('error downloading file "%s" to "%s"' % (sGstFile, sTmpFile));
                    return fRc;
                reporter.log('warning: file "%s" was not downloaded, ignoring.' % (sGstFile,));
        return True;

    def _checkVmIsReady(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Start a guest process',
                                                  30 * 1000, '/sbin/ifconfig',
                                                  ['ifconfig',],
                                                  False, False);
        return fRc;

    def waitVmIsReady(self, oSession, fWaitTrayControl):
        """
        Waits the VM is ready after start or reboot.
        Returns result (true or false) and guest session obtained
        """
        _ = fWaitTrayControl;
        # Give the VM a time to reboot
        self.oTstDrv.sleep(30);
        # Waiting the VM is ready.
        # To do it, one will try to open the guest session and start the guest process in loop
        if not self._waitAdditionsIsRunning(oSession.o.console.guest, False):
            return (False, None);
        cAttempt = 0;
        oGuestSession = None;
        fRc = False;
        while cAttempt < 30:
            fRc, oGuestSession = self.createSession(oSession, 'Session for user: vbox',
                                                    'vbox', 'password', 10 * 1000, False);
            if fRc:
                fRc = self._checkVmIsReady(oGuestSession);
                if fRc:
                    break;
                self.closeSession(oGuestSession, False);
            self.oTstDrv.sleep(10);
            cAttempt += 1;
        return (fRc, oGuestSession);

    def _rebootVM(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Reboot the VM',
                                                  30 * 1000, '/usr/bin/sudo',
                                                  ['sudo', 'reboot'],
                                                  False, True);
        if not fRc:
            reporter.error('Calling the reboot utility failed');
        return fRc;

    def rebootVMAndCheckReady(self, oSession, oGuestSession):
        """
        Reboot the VM and wait the VM is ready.
        Returns result and guest session obtained after reboot
        """
        reporter.testStart('Reboot VM and wait for readiness');
        fRc = self._rebootVM(oGuestSession);
        fRc = self.closeSession(oGuestSession, True) and fRc and True; # pychecker hack.
        if fRc:
            (fRc, oGuestSession) = self.waitVmIsReady(oSession, False);
        if not fRc:
            reporter.error('VM is not ready after reboot');
        reporter.testDone();
        return (fRc, oGuestSession);

    def _powerDownVM(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Power down the VM',
                                                  30 * 1000, '/usr/bin/sudo',
                                                  ['sudo', 'poweroff'],
                                                  False, True);
        if not fRc:
            reporter.error('Calling the poweroff utility failed');
        return fRc;

    def powerDownVM(self, oGuestSession):
        """
        Power down the VM by calling guest process without wating
        the VM is really powered off. Also, closes the guest session.
        It helps the terminateBySession to stop the VM without aborting.
        """
        if oGuestSession is None:
            return False;
        reporter.testStart('Power down the VM');
        fRc = self._powerDownVM(oGuestSession);
        fRc = self.closeSession(oGuestSession, True) and fRc and True; # pychecker hack.
        if not fRc:
            reporter.error('Power down the VM failed');
        reporter.testDone();
        return fRc;

    def installAdditions(self, oSession, oGuestSession, oVM):
        """
        Installs the Windows guest additions using the test execution service.
        """
        _ = oSession;
        _ = oGuestSession;
        _ = oVM;
        reporter.error('Not implemented');
        return False;

    def installVirtualBox(self, oGuestSession):
        """
        Install VirtualBox in the guest.
        """
        _ = oGuestSession;
        reporter.error('Not implemented');
        return False;

    def configureAutostart(self, oGuestSession, sDefaultPolicy = 'allow', asUserAllow = (), asUserDeny = ()):
        """
        Configures the autostart feature in the guest.
        """
        _ = oGuestSession;
        _ = sDefaultPolicy;
        _ = asUserAllow; # pylint: disable=redefined-variable-type
        _ = asUserDeny;
        reporter.error('Not implemented');
        return False;

    def createUser(self, oGuestSession, sUser):
        """
        Create a new user with the given name
        """
        _ = oGuestSession;
        _ = sUser;
        reporter.error('Not implemented');
        return False;

    def checkForRunningVM(self, oSession, oGuestSession, sUser, sVmName):
        """
        Check for VM running in the guest after autostart.
        Due to the sUser is created whithout password,
        all calls will be perfomed using 'sudo -u sUser'
        """
        _ = oSession;
        _ = oGuestSession;
        _ = sUser;
        _ = sVmName;
        reporter.error('Not implemented');
        return False;

    def getResourceSet(self):
        asRet = [];
        if not os.path.isabs(self.sHdd):
            asRet.append(self.sHdd);
        return asRet;

    def _createVmDoIt(self, oTestDrv, eNic0AttachType, sDvdImage):
        """
        Creates the VM.
        Returns Wrapped VM object on success, None on failure.
        """
        _ = eNic0AttachType;
        _ = sDvdImage;
        return oTestDrv.createTestVM(self.sVmName, self.iGroup, self.sHdd, sKind = self.sKind, \
                                     fIoApic = True, eNic0AttachType = vboxcon.NetworkAttachmentType_NAT, \
                                     eNic0Type = self.eNic0Type, cMbRam = self.cMbRam, \
                                     sHddControllerType = "SATA Controller", fPae = self.fPae, \
                                     cCpus = self.cCpus, sDvdImage = self.sGuestAdditionsIso);

    def _createVmPost(self, oTestDrv, oVM, eNic0AttachType, sDvdImage):
        _ = eNic0AttachType;
        _ = sDvdImage;
        fRc = True;
        oSession = oTestDrv.openSession(oVM);
        if oSession is not None:
            fRc = fRc and oSession.enableVirtExX86(True);
            fRc = fRc and oSession.enableNestedPagingX86(True);
            fRc = fRc and oSession.enableNestedHwVirtX86(True);
            # disable 3D until the error is fixed. ## @todo r=andy Which error?
            fRc = fRc and oSession.setAccelerate3DEnabled(False);
            fRc = fRc and oSession.setVRamSize(256);
            fRc = fRc and oSession.setVideoControllerType(vboxcon.GraphicsControllerType_VBoxSVGA);
            fRc = fRc and oSession.enableUsbOhci(True);
            fRc = fRc and oSession.enableUsbHid(True);
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack.
            oSession = None;
        else:
            fRc = False;
        return oVM if fRc else None;

    def getReconfiguredVm(self, oTestDrv, cCpus, sVirtMode, sParavirtMode = None):
        #
        # Current test uses precofigured VMs. This override disables any changes in the machine.
        #
        _ = cCpus;
        _ = sVirtMode;
        _ = sParavirtMode;
        oVM = oTestDrv.getVmByName(self.sVmName);
        if oVM is None:
            return (False, None);
        return (True, oVM);

class tdAutostartOsLinux(tdAutostartOs):
    """
    Autostart support methods for Linux guests.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None):
        tdAutostartOs.__init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type, cMbRam, \
                               cCpus, fPae, sGuestAdditionsIso);
        try:    self.sVBoxInstaller = '^VirtualBox-.*\\.run$';
        except: pass;
        return;

    def installAdditions(self, oSession, oGuestSession, oVM):
        """
        Install guest additions in the guest.
        """
        reporter.testStart('Install Guest Additions');
        fRc = False;
        # Install Kernel headers, which are required for actually installing the Linux Additions.
        if   oVM.OSTypeId.startswith('Debian') \
          or oVM.OSTypeId.startswith('Ubuntu'):
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing Kernel headers',
                                                  5 * 60 *1000, '/usr/bin/apt-get',
                                                  ['/usr/bin/apt-get', 'install', '-y',
                                                   'linux-headers-generic'],
                                                  False, True);
            if not fRc:
                reporter.error('Error installing Kernel headers');
            else:
                (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing Guest Additions depdendencies',
                                                          5 * 60 *1000, '/usr/bin/apt-get',
                                                          ['/usr/bin/apt-get', 'install', '-y', 'build-essential',
                                                           'perl'], False, True);
                if not fRc:
                    reporter.error('Error installing additional installer dependencies');
        elif oVM.OSTypeId.startswith('OL') \
          or oVM.OSTypeId.startswith('Oracle') \
          or oVM.OSTypeId.startswith('RHEL') \
          or oVM.OSTypeId.startswith('Redhat') \
          or oVM.OSTypeId.startswith('Cent'):
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing Kernel headers',
                                                  5 * 60 *1000, '/usr/bin/yum',
                                                  ['/usr/bin/yum', '-y', 'install', 'kernel-headers'],
                                                  False, True);
            if not fRc:
                reporter.error('Error installing Kernel headers');
            else:
                (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing Guest Additions depdendencies',
                                                          5 * 60 *1000, '/usr/bin/yum',
                                                          ['/usr/bin/yum', '-y', 'install', 'make', 'automake', 'gcc',
                                                           'kernel-devel', 'dkms', 'bzip2', 'perl'], False, True);
                if not fRc:
                    reporter.error('Error installing additional installer dependencies');
        else:
            reporter.error('Installing Linux Additions for the "%s" is not supported yet' % oVM.OSTypeId);
            fRc = False;
        if fRc:
            #
            # The actual install.
            # Also tell the installer to produce the appropriate log files.
            #
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing guest additions',
                                                      10 * 60 *1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/sh',
                                                       '/media/cdrom/VBoxLinuxAdditions.run'],
                                                      False, True);
            if fRc:
                # Due to the GA updates as separate process the above function returns before
                # the actual installation finished. So just wait until the GA installed
                fRc = self.closeSession(oGuestSession);
                if fRc:
                    (fRc, oGuestSession) = self.waitVmIsReady(oSession, False);
                # Download log files.
                # Ignore errors as all files above might not be present for whatever reason.
                #
                if fRc:
                    asLogFile = [];
                    asLogFile.append('/var/log/vboxadd-install.log');
                    self.downloadFiles(oGuestSession, asLogFile, fIgnoreErrors = True);
            else:
                reporter.error('Installing guest additions failed: Error occured during vbox installer execution')
        if fRc:
            (fRc, oGuestSession) = self.rebootVMAndCheckReady(oSession, oGuestSession);
            if not fRc:
                reporter.error('Reboot after installing GuestAdditions failed');
        reporter.testDone();
        return (fRc, oGuestSession);

    def installVirtualBox(self, oGuestSession):
        """
        Install VirtualBox in the guest.
        """
        reporter.testStart('Install Virtualbox into the guest VM');
        sTestBuild = self._findFile(self.sVBoxInstaller, self.asTestBuildDirs);
        reporter.log("Virtualbox install file: %s" % os.path.basename(sTestBuild));
        fRc = sTestBuild is not None;
        if fRc:
            fRc = self.uploadFile(oGuestSession, sTestBuild,
                                  '/tmp/' + os.path.basename(sTestBuild));
        else:
            reporter.error("VirtualBox install package is not defined");

        if not fRc:
            reporter.error('Upload the vbox installer into guest VM failed');
        else:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession,
                                                      'Allowing execution for the vbox installer',
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/chmod', '755',
                                                       '/tmp/' + os.path.basename(sTestBuild)],
                                                      False, True);
            if not fRc:
                reporter.error('Allowing execution for the vbox installer failed');
        if fRc:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing VBox',
                                                      240 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo',
                                                       '/tmp/' + os.path.basename(sTestBuild),],
                                                      False, True);
            if not fRc:
                reporter.error('Installing VBox failed');
        reporter.testDone();
        return fRc;

    def configureAutostart(self, oGuestSession, sDefaultPolicy = 'allow', asUserAllow = (), asUserDeny = ()):
        """
        Configures the autostart feature in the guest.
        """
        reporter.testStart('Configure autostart');
        # Create autostart database directory writeable for everyone
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Creating autostart database',
                                                  30 * 1000, '/usr/bin/sudo',
                                                  ['/usr/bin/sudo', '/bin/mkdir', '-m', '1777', '/etc/vbox/autostart.d'],
                                                  False, True);
        if not fRc:
            reporter.error('Creating autostart database failed');
        # Create /etc/default/virtualbox
        if fRc:
            sVBoxCfg =   'VBOXAUTOSTART_CONFIG=/etc/vbox/autostart.cfg\n' \
                       + 'VBOXAUTOSTART_DB=/etc/vbox/autostart.d\n';
            fRc = self.uploadString(oGuestSession, sVBoxCfg, '/tmp/virtualbox');
            if not fRc:
                reporter.error('Upload to /tmp/virtualbox failed');
        if fRc:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Moving to destination',
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/mv', '/tmp/virtualbox',
                                                       '/etc/default/virtualbox'],
                                                      False, True);
            if not fRc:
                reporter.error('Moving the /tmp/virtualbox to destination failed');
        if fRc:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Setting permissions',
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/chmod', '644',
                                                       '/etc/default/virtualbox'],
                                                      False, True);
            if not fRc:
                reporter.error('Setting permissions for the virtualbox failed');
        if fRc:
            sVBoxCfg = self._createAutostartCfg(sDefaultPolicy, asUserAllow, asUserDeny);
            fRc = self.uploadString(oGuestSession, sVBoxCfg, '/tmp/autostart.cfg');
            if not fRc:
                reporter.error('Upload to /tmp/autostart.cfg failed');
        if fRc:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Moving to destination',
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/mv', '/tmp/autostart.cfg',
                                                       '/etc/vbox/autostart.cfg'],
                                                      False, True);
            if not fRc:
                reporter.error('Moving the /tmp/autostart.cfg to destination failed');
        if fRc:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Setting permissions',
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '/bin/chmod', '644',
                                                       '/etc/vbox/autostart.cfg'],
                                                      False, True);
            if not fRc:
                reporter.error('Setting permissions for the autostart.cfg failed');
        reporter.testDone();
        return fRc;

    def createUser(self, oGuestSession, sUser):
        """
        Create a new user with the given name
        """
        reporter.testStart('Create user %s' % sUser);
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Creating new user',
                                                  30 * 1000, '/usr/bin/sudo',
                                                  ['/usr/bin/sudo', '/usr/sbin/useradd', '-m', '-U',
                                                   sUser], False, True);
        if not fRc:
            reporter.error('Create user %s failed' % sUser);
        reporter.testDone();
        return fRc;

    # pylint: enable=too-many-arguments
    def createTestVM(self, oSession, oGuestSession, sUser, sVmName):
        """
        Create a test VM in the guest and enable autostart.
        Due to the sUser is created whithout password,
        all calls will be perfomed using 'sudo -u sUser'
        """
        _ = oSession;
        reporter.testStart('Create test VM for user %s' % sUser);
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Configuring autostart database',
                                                  30 * 1000, '/usr/bin/sudo',
                                                  ['/usr/bin/sudo', '-u', sUser, '-H', '/opt/VirtualBox/VBoxManage',
                                                   'setproperty', 'autostartdbpath', '/etc/vbox/autostart.d'],
                                                  False, True);
        if not fRc:
            reporter.error('Configuring autostart database failed');
        else:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Create VM ' + sVmName,
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '-u', sUser, '-H',
                                                       '/opt/VirtualBox/VBoxManage', 'createvm',
                                                       '--name', sVmName, '--register'], False, True);
            if not fRc:
                reporter.error('Create VM %s failed' % sVmName);
        if fRc:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Enabling autostart for test VM',
                                                      30 * 1000, '/usr/bin/sudo',
                                                      ['/usr/bin/sudo', '-u', sUser, '-H',
                                                       '/opt/VirtualBox/VBoxManage', 'modifyvm',
                                                      sVmName, '--autostart-enabled', 'on'], False, True);
            if not fRc:
                reporter.error('Enabling autostart for %s failed' % sVmName);
        reporter.testDone();
        return fRc;

    def checkForRunningVM(self, oSession, oGuestSession, sUser, sVmName):
        """
        Check for VM running in the guest after autostart.
        Due to the sUser is created whithout password,
        all calls will be perfomed using 'sudo -u sUser'
        """
        self.oTstDrv.sleep(30);
        _ = oSession;
        reporter.testStart('Check the VM %s is running for user %s' % (sVmName, sUser));
        (fRc, _, _, aBuf) = self.guestProcessExecute(oGuestSession, 'Check for running VM',
                                                     30 * 1000, '/usr/bin/sudo',
                                                     ['/usr/bin/sudo', '-u', sUser, '-H',
                                                      '/opt/VirtualBox/VBoxManage',
                                                      'list', 'runningvms'], True, True);
        if not fRc:
            reporter.error('Checking the VM %s is running for user %s failed' % (sVmName, sUser));
        else:
            bufWrapper = VBoxManageStdOutWrapper();
            bufWrapper.write(aBuf);
            fRc = bufWrapper.sVmRunning == sVmName;
        reporter.testDone();
        return fRc;

class tdAutostartOsDarwin(tdAutostartOs):
    """
    Autostart support methods for Darwin guests.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None):
        tdAutostartOs.__init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type, cMbRam, \
                               cCpus, fPae, sGuestAdditionsIso);
        raise base.GenError('Testing the autostart functionality for Darwin is not implemented');

class tdAutostartOsSolaris(tdAutostartOs):
    """
    Autostart support methods for Solaris guests.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None):
        tdAutostartOs.__init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type, cMbRam, \
                               cCpus, fPae, sGuestAdditionsIso);
        raise base.GenError('Testing the autostart functionality for Solaris is not implemented');

class tdAutostartOsWin(tdAutostartOs):
    """
    Autostart support methods for Windows guests.
    """
    # pylint: disable=too-many-arguments
    def __init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type = None, cMbRam = None,  \
                 cCpus = 1, fPae = None, sGuestAdditionsIso = None):
        tdAutostartOs.__init__(self, oSet, oTstDrv, sVmName, sKind, sHdd, eNic0Type, cMbRam, \
                               cCpus, fPae, sGuestAdditionsIso);
        try:    self.sVBoxInstaller = '^VirtualBox-.*\\.(exe|msi)$';
        except: pass;
        return;

    def _checkVmIsReady(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Start a guest process',
                                                  30 * 1000, 'C:\\Windows\\System32\\ipconfig.exe',
                                                  ['C:\\Windows\\System32\\ipconfig.exe',],
                                                  False, False);
        return fRc;

    def _rebootVM(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Reboot the VM',
                                                  30 * 1000, 'C:\\Windows\\System32\\shutdown.exe',
                                                  ['C:\\Windows\\System32\\shutdown.exe', '/f',
                                                   '/r', '/t', '0'],
                                                  False, True);
        if not fRc:
            reporter.error('Calling the shutdown utility failed');
        return fRc;

    def _powerDownVM(self, oGuestSession):
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Power down the VM',
                                                  30 * 1000, 'C:\\Windows\\System32\\shutdown.exe',
                                                  ['C:\\Windows\\System32\\shutdown.exe', '/f',
                                                   '/s', '/t', '0'],
                                                  False, True);
        if not fRc:
            reporter.error('Calling the shutdown utility failed');
        return fRc;

    def installAdditions(self, oSession, oGuestSession, oVM):
        """
        Installs the Windows guest additions using the test execution service.
        """
        _ = oVM;
        reporter.testStart('Install Guest Additions');
        asLogFiles = [];
        fRc = self.closeSession(oGuestSession, True); # pychecker hack.
        try:
            oCurProgress = oSession.o.console.guest.updateGuestAdditions(self.sGuestAdditionsIso, ['/l',], None);
        except:
            reporter.maybeErrXcpt(True, 'Updating Guest Additions exception for sSrc="%s":'
                                  % (self.sGuestAdditionsIso,));
            fRc = False;
        else:
            if oCurProgress is not None:
                oWrapperProgress = vboxwrappers.ProgressWrapper(oCurProgress, self.oTstDrv.oVBoxMgr,
                                                                self.oTstDrv, "installAdditions");
                oWrapperProgress.wait(cMsTimeout = 10 * 60 * 1000);
                if not oWrapperProgress.isSuccess():
                    oWrapperProgress.logResult(fIgnoreErrors = False);
                    fRc = False;
            else:
                fRc = reporter.error('No progress object returned');
        #---------------------------------------
        #
        ##
        ## Install the public signing key.
        ##
        #
        #self.oTstDrv.sleep(60 * 2);
        #
        #if oVM.OSTypeId not in ('WindowsNT4', 'Windows2000', 'WindowsXP', 'Windows2003'):
        #    (fRc, _, _, _) = \
        #        self.guestProcessExecute(oGuestSession, 'Installing  SHA1 certificate',
        #                                 60 * 1000, 'D:\\cert\\VBoxCertUtil.exe',
        #                                 ['D:\\cert\\VBoxCertUtil.exe', 'add-trusted-publisher',
        #                                  'D:\\cert\\vbox-sha1.cer'],
        #                                 False, True);
        #    if not fRc:
        #        reporter.error('Error installing SHA1 certificate');
        #    else:
        #        (fRc, _, _, _) = \
        #            self.guestProcessExecute(oGuestSession, 'Installing  SHA1 certificate',
        #                                     60 * 1000, 'D:\\cert\\VBoxCertUtil.exe',
        #                                     ['D:\\cert\\VBoxCertUtil.exe', 'add-trusted-publisher',
        #                                      'D:\\cert\\vbox-sha256.cer'],
        #                                     False, True);
        #        if not fRc:
        #            reporter.error('Error installing SHA256 certificate');
        #
        #(fRc, _, _, _) = \
        #        self.guestProcessExecute(oGuestSession, 'Installing  GA',
        #                                 60 * 1000, 'D:\\VBoxWindowsAdditions.exe',
        #                                 ['D:\\VBoxWindowsAdditions.exe', '/S', '/l',
        #                                  '/no_vboxservice_exit'],
        #                                 False, True);
        #
        #if fRc:
        #    # Due to the GA updates as separate process the above function returns before
        #    # the actual installation finished. So just wait until the GA installed
        #    fRc = self.closeSession(oGuestSession, True);
        #    if fRc:
        #        (fRc, oGuestSession) = self.waitVmIsReady(oSession, False, False);
        #---------------------------------------
        # Store the result and try download logs anyway.
        fGaRc = fRc;
        fRc, oGuestSession = self.createSession(oSession, 'Session for user: vbox',
                                        'vbox', 'password', 10 * 1000, True);
        if fRc is True:
            (fRc, oGuestSession) = self.rebootVMAndCheckReady(oSession, oGuestSession);
            if fRc is True:
                # Add the Windows Guest Additions installer files to the files we want to download
                # from the guest.
                sGuestAddsDir = 'C:/Program Files/Oracle/VirtualBox Guest Additions/';
                asLogFiles.append(sGuestAddsDir + 'install.log');
                # Note: There won't be a install_ui.log because of the silent installation.
                asLogFiles.append(sGuestAddsDir + 'install_drivers.log');
                # Download log files.
                # Ignore errors as all files above might not be present (or in different locations)
                # on different Windows guests.
                #
                self.downloadFiles(oGuestSession, asLogFiles, fIgnoreErrors = True);
            else:
                reporter.error('Reboot after installing GuestAdditions failed');
        else:
            reporter.error('Create session for user vbox after GA updating failed');
        reporter.testDone();
        return (fRc and fGaRc, oGuestSession);

    def installVirtualBox(self, oGuestSession):
        """
        Install VirtualBox in the guest.
        """
        reporter.testStart('Install Virtualbox into the guest VM');
        # Used windows image already contains the C:\Temp
        sTestBuild = self._findFile(self.sVBoxInstaller, self.asTestBuildDirs);
        reporter.log("Virtualbox install file: %s" % os.path.basename(sTestBuild));
        fRc = sTestBuild is not None;
        if fRc:
            fRc = self.uploadFile(oGuestSession, sTestBuild,
                              'C:\\Temp\\' + os.path.basename(sTestBuild));
        else:
            reporter.error("VirtualBox install package is not defined");

        if not fRc:
            reporter.error('Upload the installing into guest VM failed');
        else:
            if sTestBuild.endswith('.msi'):
                sLogFile = 'C:/Temp/VBoxInstallLog.txt';
                (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing VBox',
                                                        600 * 1000, 'C:\\Windows\\System32\\msiexec.exe',
                                                        ['msiexec', '/quiet', '/norestart', '/i',
                                                         'C:\\Temp\\' + os.path.basename(sTestBuild),
                                                        '/lv', sLogFile],
                                                        False, True);
                if not fRc:
                    reporter.error('Installing the VBox from msi installer failed');
            else:
                sLogFile = 'C:/Temp/Virtualbox/VBoxInstallLog.txt';
                (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Installing VBox',
                                                        600 * 1000, 'C:\\Temp\\' + os.path.basename(sTestBuild),
                                                        ['C:\\Temp\\' + os.path.basename(sTestBuild), '-vvvv',
                                                         '--silent', '--logging',
                                                         '--msiparams', 'REBOOT=ReallySuppress'],
                                                        False, True);
                if not fRc:
                    reporter.error('Installing the VBox failed');
                else:
                    (_, _, _, aBuf) = self.guestProcessExecute(oGuestSession, 'Check installation',
                                                               240 * 1000, 'C:\\Windows\\System32\\cmd.exe',
                                                               ['c:\\Windows\\System32\\cmd.exe', '/c',
                                                                'dir', 'C:\\Program Files\\Oracle\\VirtualBox\\*.*'],
                                                               True, True);
                    reporter.log('Content of  VirtualBxox folder:');
                    reporter.log(str(aBuf));
            asLogFiles = [sLogFile,];
            self.downloadFiles(oGuestSession, asLogFiles, fIgnoreErrors = True);
        reporter.testDone();
        return fRc;

    def configureAutostart(self, oGuestSession, sDefaultPolicy = 'allow', asUserAllow = (), asUserDeny = ()):
        """
        Configures the autostart feature in the guest.
        """
        reporter.testStart('Configure autostart');
        # Create autostart database directory writeable for everyone
        (fRc, _, _, _) = \
            self.guestProcessExecute(oGuestSession, 'Setting the autostart environment variable',
                                     30 * 1000, 'C:\\Windows\\System32\\reg.exe',
                                     ['reg', 'add',
                                      'HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment',
                                      '/v', 'VBOXAUTOSTART_CONFIG', '/d',
                                      'C:\\ProgramData\\autostart.cfg', '/f'],
                                     False, True);
        if fRc:
            sVBoxCfg = self._createAutostartCfg(sDefaultPolicy, asUserAllow, asUserDeny);
            fRc = self.uploadString(oGuestSession, sVBoxCfg, 'C:\\ProgramData\\autostart.cfg');
            if not fRc:
                reporter.error('Upload the autostart.cfg failed');
        else:
            reporter.error('Setting the autostart environment variable failed');
        reporter.testDone();
        return fRc;

    def createTestVM(self, oSession, oGuestSession, sUser, sVmName):
        """
        Create a test VM in the guest and enable autostart.
        """
        _ = oGuestSession;
        reporter.testStart('Create test VM for user %s' % sUser);
        fRc, oGuestSession = self.createSession(oSession, 'Session for user: %s' % (sUser,),
                                                sUser, 'password', 10 * 1000, True);
        if not fRc:
            reporter.error('Create session for user %s failed' % sUser);
        else:
            (fRc, _, _, _) = \
                self.guestProcessExecute(oGuestSession, 'Create VM ' + sVmName,
                                         30 * 1000, 'C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe',
                                         ['C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe', 'createvm',
                                          '--name', sVmName, '--register'], False, True);
            if not fRc:
                reporter.error('Create VM %s for user %s failed' % (sVmName, sUser));
            else:
                (fRc, _, _, _) = \
                    self.guestProcessExecute(oGuestSession, 'Enabling autostart for test VM',
                                             30 * 1000, 'C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe',
                                             ['C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe',
                                              'modifyvm', sVmName, '--autostart-enabled', 'on'], False, True);
                if not fRc:
                    reporter.error('Enabling autostart for VM %s for user %s failed' % (sVmName, sUser));
            if fRc:
                fRc = self.uploadString(oGuestSession, 'password', 'C:\\ProgramData\\password.cfg');
                if not fRc:
                    reporter.error('Upload the password.cfg failed');
            if fRc:
                (fRc, _, _, _) = \
                    self.guestProcessExecute(oGuestSession, 'Install autostart service for the user',
                                             30 * 1000, 'C:\\Program Files\\Oracle\\VirtualBox\\VBoxAutostartSvc.exe',
                                             ['C:\\Program Files\\Oracle\\VirtualBox\\VBoxAutostartSvc.exe',
                                              'install', '--user=' + sUser,
                                              '--password-file=C:\\ProgramData\\password.cfg'],
                                             False, True);
                if not fRc:
                    reporter.error('Install autostart service for user %s failed' % sUser);
            fRc1 = self.closeSession(oGuestSession, True);
            if not fRc1:
                reporter.error('Closing session for user %s failed' % sUser);
            fRc = fRc1 and fRc and True; # pychecker hack.
        reporter.testDone();
        return fRc;

    def checkForRunningVM(self, oSession, oGuestSession, sUser, sVmName):
        """
        Check for VM running in the guest after autostart.
        """
        self.oTstDrv.sleep(30);
        _ = oGuestSession;
        reporter.testStart('Check the VM %s is running for user %s' % (sVmName, sUser));
        fRc, oGuestSession = self.createSession(oSession, 'Session for user: %s' % (sUser,),
                                                sUser, 'password', 10 * 1000, True);
        if not fRc:
            reporter.error('Create session for user %s failed' % sUser);
        else:
            (fRc, _, _, aBuf) = self.guestProcessExecute(oGuestSession, 'Check for running VM',
                                                         60 * 1000, 'C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe',
                                                         [ 'C:\\Program Files\\Oracle\\VirtualBox\\VBoxManage.exe',
                                                           'list', 'runningvms' ], True, True);
            if not fRc:
                reporter.error('Checking the VM %s is running for user %s failed' % (sVmName, sUser));
            else:
                bufWrapper = VBoxManageStdOutWrapper();
                bufWrapper.write(aBuf);
                fRc = bufWrapper.sVmRunning == sVmName;
            fRc1 = self.closeSession(oGuestSession, True);
            if not fRc1:
                reporter.error('Closing session for user %s failed' % sUser);
            fRc = fRc1 and fRc and True; # pychecker hack.
        reporter.testDone();
        return fRc;

    def createUser(self, oGuestSession, sUser):
        """
        Create a new user with the given name
        """
        reporter.testStart('Create user %s' % sUser);
        # Create user
        (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Creating user %s to run a VM' % sUser,
                                                30 * 1000, 'C:\\Windows\\System32\\net.exe',
                                                ['net', 'user', sUser, 'password', '/add' ], False, True);
        if not fRc:
            reporter.error('Creating user %s to run a VM failed' % sUser);
        # Add the user to Administrators group
        else:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession, 'Adding the user %s to Administrators group' % sUser,
                                                      30 * 1000, 'C:\\Windows\\System32\\net.exe',
                                                      ['net', 'localgroup', 'Administrators', sUser, '/add' ], False, True);
            if not fRc:
                reporter.error('Adding the user %s to Administrators group failed' % sUser);
        #Allow the user to logon as service
        if fRc:
            sSecPolicyEditor = """
$sUser = '%s'
$oUser = New-Object System.Security.Principal.NTAccount("$sUser")
$oSID = $oUser.Translate([System.Security.Principal.SecurityIdentifier])
$sExportFile = 'C:\\Temp\\cfg.inf'
$sSecDb = 'C:\\Temp\\secedt.sdb'
$sImportFile = 'C:\\Temp\\newcfg.inf'
secedit /export /cfg $sExportFile
$sCurrServiceLogonRight = Get-Content -Path $sExportFile |
    Where-Object {$_ -Match 'SeServiceLogonRight'}
$asFileContent = @'
[Unicode]
Unicode=yes
[System Access]
[Event Audit]
[Registry Values]
[Version]
signature="$CHICAGO$"
Revision=1
[Profile Description]
Description=GrantLogOnAsAService security template
[Privilege Rights]
{0}*{1}
'@ -f $(
        if($sCurrServiceLogonRight){"$sCurrServiceLogonRight,"}
        else{'SeServiceLogonRight = '}
    ), $oSid.Value
Set-Content -Path $sImportFile -Value $asFileContent
secedit /import /db $sSecDb /cfg $sImportFile
secedit /configure /db $sSecDb
Remove-Item -Path $sExportFile
Remove-Item -Path $sSecDb
Remove-Item -Path $sImportFile
                           """ % (sUser,);
            fRc = self.uploadString(oGuestSession, sSecPolicyEditor, 'C:\\Temp\\adjustsec.ps1');
            if not fRc:
                reporter.error('Upload the adjustsec.ps1 failed');
        if fRc:
            (fRc, _, _, _) = self.guestProcessExecute(oGuestSession,
                                                      'Setting the "Logon as service" policy to the user %s' % sUser,
                                                      300 * 1000, 'C:\\Windows\\System32\\cmd.exe',
                                                      ['cmd.exe', '/c', "type C:\\Temp\\adjustsec.ps1 | powershell -"],
                                                      False, True);
            if not fRc:
                reporter.error('Setting the "Logon as service" policy to the user %s failed' % sUser);
        try:
            oGuestSession.fsObjRemove('C:\\Temp\\adjustsec.ps1');
        except:
            fRc = reporter.errorXcpt('Removing policy script failed');
        reporter.testDone();
        return fRc;

class tdAutostart(vbox.TestDriver):                                      # pylint: disable=too-many-instance-attributes
    """
    Autostart testcase.
    """
    ksOsLinux   = 'tst-linux'
    ksOsWindows = 'tst-win'
    ksOsDarwin  = 'tst-darwin'
    ksOsSolaris = 'tst-solaris'
    ksOsFreeBSD = 'tst-freebsd'

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asRsrcs            = None;
        self.asTestVMsDef       = [self.ksOsWindows, self.ksOsLinux]; #[self.ksOsLinux, self.ksOsWindows];
        self.asTestVMs          = self.asTestVMsDef;
        self.asSkipVMs          = [];
        ## @todo r=bird: The --test-build-dirs option as primary way to get the installation files to test
        ## is not an acceptable test practice as we don't know wtf you're testing.  See defect for more.
        self.asTestBuildDirs    = [os.path.join(self.sScratchPath, 'bin'),];
        self.sGuestAdditionsIso = None; #'D:/AlexD/TestBox/TestAdditionalFiles/VBoxGuestAdditions_6.1.2.iso';
        oSet = vboxtestvms.TestVmSet(self.oTestVmManager, acCpus = [2], asVirtModes = ['hwvirt-np',], fIgnoreSkippedVm = True);
        # pylint: disable=line-too-long
        self.asTestVmClasses = {
            'win'     : tdAutostartOsWin(oSet, self, self.ksOsWindows, 'Windows7_64', \
                             '6.0/windows7piglit/windows7piglit.vdi', eNic0Type = None, cMbRam = 2048,  \
                             cCpus = 2, fPae = True, sGuestAdditionsIso = self.getGuestAdditionsIso()),
            'linux'   : tdAutostartOsLinux(oSet, self, self.ksOsLinux, 'Ubuntu_64', \
                               '6.0/ub1804piglit/ub1804piglit.vdi', eNic0Type = None, \
                               cMbRam = 2048, cCpus = 2, fPae = None, sGuestAdditionsIso = self.getGuestAdditionsIso()),
            'solaris' : None, #'tdAutostartOsSolaris',
            'darwin'  : None  #'tdAutostartOsDarwin'
        };
        oSet.aoTestVms.extend([oTestVm for oTestVm in self.asTestVmClasses.values() if oTestVm is not None]);
        sOs = self.getBuildOs();
        if sOs in self.asTestVmClasses:
            for oTestVM in oSet.aoTestVms:
                if oTestVM is not None:
                    oTestVM.fSkip = oTestVM != self.asTestVmClasses[sOs];

        # pylint: enable=line-too-long
        self.oTestVmSet = oSet;

    #
    # Overridden methods.
    #

    def showUsage(self):
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdAutostart Options:');
        reporter.log('  --test-build-dirs <path1[,path2[,...]]>');
        reporter.log('      The list of directories with VirtualBox distros. Overrides default path.');
        reporter.log('      Default path is $TESTBOX_SCRATCH_PATH/bin.');
        reporter.log('  --vbox-<os>-build <path>');
        reporter.log('      The path to vbox build for the specified OS.');
        reporter.log('      The OS can be one of "win", "linux", "solaris" and "darwin".');
        reporter.log('      This option alse enables corresponding VM for testing.');
        reporter.log('      (Default behaviour is testing only VM having host-like OS.)');
        return rc;

    def parseOption(self, asArgs, iArg): # pylint: disable=too-many-branches,too-many-statements
        if asArgs[iArg] == '--test-build-dirs':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--test-build-dirs" takes a path argument');
            self.asTestBuildDirs = asArgs[iArg].split(',');
            for oTestVm in self.oTestVmSet.aoTestVms:
                oTestVm.asTestBuildDirs = self.asTestBuildDirs;
        elif asArgs[iArg] in [ '--vbox-%s-build' % sKey for sKey in self.asTestVmClasses]:
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "%s" take a path argument' % (asArgs[iArg - 1],));
            oMatch = re.match("--vbox-([^-]+)-build", asArgs[iArg - 1]);
            if oMatch is not None:
                sOs = oMatch.group(1);
                oTestVm = self.asTestVmClasses.get(sOs);
                if oTestVm is not None:
                    oTestVm.sTestBuild = asArgs[iArg];
                    oTestVm.fSkip = False;
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def actionConfig(self):
        if not self.importVBoxApi(): # So we can use the constant below.
            return False;
        return self.oTestVmSet.actionConfig(self);

    def actionExecute(self):
        """
        Execute the testcase.
        """
        return self.oTestVmSet.actionExecute(self, self.testAutostartOneCfg)

    #
    # Test execution helpers.
    #
    def testAutostartOneCfg(self, oVM, oTestVm):
        # Reconfigure the VM
        fRc = True;
        self.logVmInfo(oVM);
        oSession = self.startVmByName(oTestVm.sVmName);
        if oSession is not None:
            sTestUserAllow = 'test1';
            sTestUserDeny = 'test2';
            sTestVmName = 'TestVM';
            #wait the VM is ready after starting
            (fRc, oGuestSession) = oTestVm.waitVmIsReady(oSession, True);
            #install fresh guest additions
            if fRc:
                (fRc, oGuestSession) = oTestVm.installAdditions(oSession, oGuestSession, oVM);
            # Create two new users
            fRc = fRc and oTestVm.createUser(oGuestSession, sTestUserAllow);
            fRc = fRc and oTestVm.createUser(oGuestSession, sTestUserDeny);
            if fRc is True:
                # Install VBox first
                fRc = oTestVm.installVirtualBox(oGuestSession);
                if fRc is True:
                    fRc = oTestVm.configureAutostart(oGuestSession, 'allow', (sTestUserAllow,), (sTestUserDeny,));
                    if fRc is True:
                        # Create a VM with autostart enabled in the guest for both users
                        fRc = oTestVm.createTestVM(oSession, oGuestSession, sTestUserAllow, sTestVmName);
                        fRc = fRc and oTestVm.createTestVM(oSession, oGuestSession, sTestUserDeny, sTestVmName);
                        if fRc is True:
                            # Reboot the guest
                            (fRc, oGuestSession) = oTestVm.rebootVMAndCheckReady(oSession, oGuestSession);
                            if fRc is True:
                                # Fudge factor - Allow the guest VMs to finish starting up.
                                self.sleep(60);
                                fRc = oTestVm.checkForRunningVM(oSession, oGuestSession, sTestUserAllow, sTestVmName);
                                if fRc is True:
                                    fRc = oTestVm.checkForRunningVM(oSession, oGuestSession, sTestUserDeny, sTestVmName);
                                    if fRc is True:
                                        reporter.error('Test VM is running inside the guest for denied user');
                                    fRc = not fRc;
                                else:
                                    reporter.error('Test VM is not running inside the guest for allowed user');
                            else:
                                reporter.error('Rebooting the guest failed');
                        else:
                            reporter.error('Creating test VM failed');
                    else:
                        reporter.error('Configuring autostart in the guest failed');
                else:
                    reporter.error('Installing VirtualBox in the guest failed');
            else:
                reporter.error('Creating test users failed');
            if oGuestSession is not None:
                try:    oTestVm.powerDownVM(oGuestSession);
                except: pass;
            try:    self.terminateVmBySession(oSession);
            except: pass;
            fRc = oSession.close() and fRc and True; # pychecker hack.
            oSession = None;
        else:
            fRc = False;
        return fRc;

if __name__ == '__main__':
    sys.exit(tdAutostart().main(sys.argv));
